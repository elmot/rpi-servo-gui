#include "params.h"
#include "hardware/flash.h"
#include "pico/flash.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

/* Last sector of 2 MB flash */
#define FLASH_PARAMS_OFFSET (PICO_FLASH_SIZE_BYTES - FLASH_SECTOR_SIZE)
#define FLASH_PARAMS_ADDR   (XIP_BASE + FLASH_PARAMS_OFFSET)
#define TEXT_BUF_SIZE       256

volatile servo_params_t g_params;
servo_telemetry_t g_telemetry;

/* ---- Key/value table for serialization ---- */
typedef struct {
    const char *key;
    size_t offset;
    bool persist;
} param_entry_t;

#define ENTRY(field)      { #field, offsetof(servo_params_t, field), true }
#define ENTRY_VOLATILE(field) { #field, offsetof(servo_params_t, field), false }

static const param_entry_t param_table[] = {
    ENTRY(zero_restricted_angle),
    ENTRY(angle_tolerance),
    ENTRY(dead_angle),
    ENTRY(slow_angle),
    ENTRY(angle_reversed),
    ENTRY(cutoff_pwm),
    ENTRY(slow_pwm),
    ENTRY(fast_pwm),
    ENTRY(slow_start_ms),
    ENTRY_VOLATILE(pwm_mock),
};

#define PARAM_COUNT (sizeof(param_table) / sizeof(param_table[0]))

static int serialize_impl(char *buf, int maxlen, bool persist_only) {
    int pos = snprintf(buf, maxlen, "version=%s\n", PARAMS_VERSION);
    for (unsigned i = 0; i < PARAM_COUNT && pos < maxlen; i++) {
        if (persist_only && !param_table[i].persist) continue;
        uint16_t val = *(const volatile uint16_t *)((const char *)&g_params + param_table[i].offset);
        pos += snprintf(buf + pos, maxlen - pos, "%s=%u\n", param_table[i].key, val);
    }
    return pos;
}

int params_serialize(char *buf, int maxlen) {
    return serialize_impl(buf, maxlen, false);
}

bool params_deserialize(const char *buf, int len) {
    bool updated = false;
    const char *p = buf;
    const char *end = buf + len;

    while (p < end) {
        const char *nl = memchr(p, '\n', end - p);
        int line_len = nl ? (int)(nl - p) : (int)(end - p);
        if (line_len == 0) { p = nl ? nl + 1 : end; continue; }

        const char *eq = memchr(p, '=', line_len);
        if (!eq) { p = nl ? nl + 1 : end; continue; }

        int key_len = (int)(eq - p);
        const char *val_str = eq + 1;
        int val_len = line_len - key_len - 1;

        /* Skip read-only "version" key */
        if (key_len == 7 && memcmp(p, "version", 7) == 0) {
            p = nl ? nl + 1 : end;
            continue;
        }

        /* Parse value */
        char vbuf[16];
        if (val_len > 0 && val_len < (int)sizeof(vbuf)) {
            memcpy(vbuf, val_str, val_len);
            vbuf[val_len] = '\0';
            uint16_t val = (uint16_t)atoi(vbuf);

            for (unsigned i = 0; i < PARAM_COUNT; i++) {
                if ((int)strlen(param_table[i].key) == key_len &&
                    memcmp(p, param_table[i].key, key_len) == 0) {
                    *(volatile uint16_t *)((char *)&g_params + param_table[i].offset) = val;
                    updated = true;
                    break;
                }
            }
        }

        p = nl ? nl + 1 : end;
    }
    return updated;
}

/* ---- Flash storage (text format) ---- */

void params_load_from_flash(void) {
    servo_params_t defaults = PARAMS_DEFAULTS;
    memcpy((void *)&g_params, &defaults, sizeof(servo_params_t));

    const char *stored = (const char *)FLASH_PARAMS_ADDR;
    /* Check for valid text: must start with "version=" */
    if (memcmp(stored, "version=", 8) == 0) {
        /* Find the end of the text (first 0xFF or NUL) */
        int len = 0;
        while (len < (int)FLASH_SECTOR_SIZE && stored[len] != '\0' && (uint8_t)stored[len] != 0xFF) {
            len++;
        }
        params_deserialize(stored, len);
    }
}

static void do_flash_write(void *param) {
    (void)param;
    uint8_t page[FLASH_PAGE_SIZE];
    memset(page, 0xFF, sizeof(page));
    int len = serialize_impl((char *)page, FLASH_PAGE_SIZE - 1, true);
    page[len] = '\0';
    flash_range_erase(FLASH_PARAMS_OFFSET, FLASH_SECTOR_SIZE);
    flash_range_program(FLASH_PARAMS_OFFSET, page, FLASH_PAGE_SIZE);
}

bool params_save_to_flash(void) {
    return flash_safe_execute(do_flash_write, NULL, 1000) == PICO_OK;
}
