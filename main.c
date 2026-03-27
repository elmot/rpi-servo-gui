#include "bsp/board.h"
#include "tusb.h"
#include "pico/time.h"
#include "pico/multicore.h"
#include "servo_errors.h"
#include <stdio.h>
#include <string.h>

#include "hardware/watchdog.h"

extern void servo_core1_entry(void);


void schedule_reboot(void)
{
    watchdog_reboot(0,0,1);
}

/* ---- Current parameters (read via EP0 vendor request) ---- */
const char params[] = "version=0.1";

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

    char log_buf[64];
    int len;
    if (servo_error_msg)
    {
        len = snprintf(log_buf, sizeof(log_buf),
                       "[%lu] ERROR: %s\n",
                       (unsigned long)log_seq++,
                       servo_error_msg);

        if (tud_vendor_write(log_buf, (uint32_t)len) > 0)
        {
            tud_vendor_flush();
        }
        servo_error_msg = NULL;
    }
}

static void drain_param_writes(void)
{
    if (!tud_vendor_available()) return;
    uint8_t discard[64];
    while (tud_vendor_available())
    {
        tud_vendor_read(discard, sizeof(discard));
    }
}

int main(void)
{
    board_init();
    tusb_init();
    multicore_launch_core1(servo_core1_entry);

    // ReSharper disable once CppDFAEndlessLoop
    while (true)
    {
        tud_task();
        log_task();
        drain_param_writes();
    }
}
