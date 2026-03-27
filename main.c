#include "bsp/board.h"
#include "tusb.h"
#include "pico/time.h"
#include "pico/multicore.h"
#include <stdio.h>
#include <string.h>

extern void servo_core1_entry(void);

/* ---- Current parameters (read via EP0 vendor request) ---- */
const char params[] = "version=0.1";

/* ---- Simulated log (streamed on bulk IN) ---- */
static uint32_t log_seq = 0;

static void log_task(void) {
    if (!tud_vendor_mounted()) return;

    static uint32_t next_ms = 0;
    uint32_t now = to_ms_since_boot(get_absolute_time());
    if (now < next_ms) return;

    char log_buf[64];
    int len = snprintf(log_buf, sizeof(log_buf),
                       "[%lu] tick=%lu\n",
                       (unsigned long)log_seq++,
                       (unsigned long)now);

    if (tud_vendor_write(log_buf, (uint32_t)len) > 0) {
        tud_vendor_flush();
        next_ms = now + 100;
    }
}

static void drain_param_writes(void) {
    if (!tud_vendor_available()) return;
    uint8_t discard[64];
    while (tud_vendor_available()) {
        tud_vendor_read(discard, sizeof(discard));
    }
}

int main(void) {
    board_init();
    tusb_init();
    multicore_launch_core1(servo_core1_entry);

    while (true) {
        tud_task();
        log_task();
        drain_param_writes();
    }
}
