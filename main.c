#include "bsp/board.h"
#include "tusb.h"

static void vendor_task(void) {
    if (!tud_vendor_available()) {
        return;
    }

    uint8_t buffer[64];
    const uint32_t count = tud_vendor_read(buffer, sizeof(buffer));
    if (count == 0) {
        return;
    }

    // Minimal loopback so the host can verify the WebUSB transport quickly.
    tud_vendor_write(buffer, count);
    tud_vendor_flush();
}

int main(void) {
    board_init();
    tusb_init();

    while (true) {
        tud_task();
        vendor_task();
    }
}
