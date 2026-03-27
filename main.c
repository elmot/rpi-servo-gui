#include "bsp/board.h"
#include "tusb.h"
#include "pico/time.h"
#include "pico/multicore.h"
#include "params.h"
#include "servo_errors.h"
#include <stdio.h>
#include <string.h>

#include "hardware/watchdog.h"

extern void servo_core1_entry(void);


void schedule_reboot(void)
{
    watchdog_reboot(0,0,1);
}

/* ---- Log (streamed on bulk IN) ---- */
static uint32_t log_seq = 0;
static const char* servo_error_msg = NULL;


static void log_task(void)
{
    if (!tud_vendor_mounted()) return;

    /* Check for errors from core 1 */
    uint32_t err;
    if (multicore_fifo_pop_timeout_us(0, &err))
    {
        switch (err)
        {
        case SERVO_ERR_I2C: servo_error_msg = "I2C";
            break;
        case SERVO_ERR_MAGNET: servo_error_msg = "MAGNET";
            break;
        default: servo_error_msg = "UNKNOWN";
            break;
        }
    }

    static uint32_t next_ms = 0;
    uint32_t now = to_ms_since_boot(get_absolute_time());
    if (now < next_ms) return;

    char log_buf[96];
    int len;
    if (servo_error_msg)
    {
        len = snprintf(log_buf, sizeof(log_buf),
                       "[%lu] ERROR: %s\n",
                       (unsigned long)log_seq++,
                       servo_error_msg);
        servo_error_msg = NULL;
    } else {
        len = snprintf(log_buf, sizeof(log_buf),
                       "[%lu] pwm=%u angle=%d target=%d pwr=%d/%d\n",
                       (unsigned long)log_seq++,
                       g_telemetry.pwm_us,
                       g_telemetry.current_angle,
                       g_telemetry.target_angle,
                       g_telemetry.power_a,
                       g_telemetry.power_b);
    }

    if (tud_vendor_write(log_buf, (uint32_t)len) > 0)
    {
        tud_vendor_flush();
        next_ms = now + 250;
    }
}

static char param_buf[256];
static uint32_t param_buf_len = 0;

static void process_param_writes(void)
{
    if (!tud_vendor_available()) return;

    /* Accumulate incoming data */
    while (tud_vendor_available() && param_buf_len < sizeof(param_buf) - 1) {
        param_buf_len += tud_vendor_read(param_buf + param_buf_len,
                                         sizeof(param_buf) - 1 - param_buf_len);
    }

    /* Only parse when we have a complete message (ends with \n) */
    if (param_buf_len > 0 && param_buf[param_buf_len - 1] == '\n') {
        param_buf[param_buf_len] = '\0';
        params_deserialize(param_buf, (int)param_buf_len);
        param_buf_len = 0;
    }

    /* Drain overflow */
    if (param_buf_len >= sizeof(param_buf) - 1) {
        param_buf_len = 0;
        uint8_t discard[64];
        while (tud_vendor_available()) tud_vendor_read(discard, sizeof(discard));
    }
}

int main(void)
{
    board_init();
    params_load_from_flash();
    tusb_init();
    multicore_launch_core1(servo_core1_entry);

    // ReSharper disable once CppDFAEndlessLoop
    while (true)
    {
        tud_task();
        log_task();
        process_param_writes();
    }
}
