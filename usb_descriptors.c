#include "tusb.h"
#include <string.h>

/* ---- Device identity ---- */
#define USB_VID 0xCAFE
#define USB_PID 0x4002
#define USB_BCD 0x0100

/* ---- Interface / endpoint layout ---- */
#define ITF_NUM_VENDOR  0
#define ITF_NUM_MSC     1
#define ITF_NUM_TOTAL   2

#define EPNUM_VENDOR_OUT 0x01
#define EPNUM_VENDOR_IN  0x81
#define EPNUM_MSC_OUT    0x02
#define EPNUM_MSC_IN     0x82

#define CONFIG_TOTAL_LEN (TUD_CONFIG_DESC_LEN + TUD_VENDOR_DESC_LEN + TUD_MSC_DESC_LEN)

/* ---- Vendor request codes (used in BOS capabilities and control xfer handler) ---- */
#define VENDOR_REQUEST_WEBUSB    0x01  /* bRequest for WebUSB GET_URL */
#define VENDOR_REQUEST_MICROSOFT 0x02  /* bRequest for MS OS 2.0 descriptor set */
#define VENDOR_REQUEST_PARAMS    0x03  /* bRequest for reading device parameters */

/* ---- WebUSB landing page URL (https:// prefix implied by bScheme=1) ---- */
#define URL "localhost:8000/usb" //todo set normal URL

/* ---- BOS descriptor sizes ---- */
#define BOS_TOTAL_LEN (TUD_BOS_DESC_LEN + TUD_BOS_WEBUSB_DESC_LEN + TUD_BOS_MICROSOFT_OS_DESC_LEN)

/* Total size of the MS OS 2.0 descriptor set:
 * 10 (set header) + 8 (config subset) + 8 (function subset) + 20 (compat ID) + 132 (reg prop) = 178 (0xB2) */
#define MS_OS_20_DESC_LEN 0xB2

/* ==========================================================================
 * Device Descriptor
 * Composite USB 2.1 device (vendor + MSC). Device class 0x00 means
 * "class defined per interface" — each interface declares its own class.
 * ========================================================================== */
static const tusb_desc_device_t desc_device = {
    .bLength = sizeof(tusb_desc_device_t),
    .bDescriptorType = TUSB_DESC_DEVICE,
    .bcdUSB = 0x0210,  /* USB 2.1 — required for BOS descriptor support */

    .bDeviceClass = 0x00,     /* Composite: class defined per interface */
    .bDeviceSubClass = 0x00,
    .bDeviceProtocol = 0x00,

    .bMaxPacketSize0 = CFG_TUD_ENDPOINT0_SIZE,
    .idVendor = USB_VID,
    .idProduct = USB_PID,
    .bcdDevice = USB_BCD,

    .iManufacturer = 0x01,
    .iProduct = 0x02,
    .iSerialNumber = 0x03,

    .bNumConfigurations = 0x01
};

uint8_t const *tud_descriptor_device_cb(void) {
    return (uint8_t const *)&desc_device;
}

/* ==========================================================================
 * Configuration Descriptor
 * Two interfaces:
 *   Interface 0: Vendor (WebUSB) — EP OUT 0x01, EP IN 0x81, 64-byte packets
 *   Interface 1: MSC (read-only FAT16 disk) — EP OUT 0x02, EP IN 0x82, 64-byte packets
 * ========================================================================== */
static const uint8_t desc_configuration[] = {
    TUD_CONFIG_DESCRIPTOR(1, ITF_NUM_TOTAL, 0, CONFIG_TOTAL_LEN, 0x00, 100),
    TUD_VENDOR_DESCRIPTOR(ITF_NUM_VENDOR, 4, EPNUM_VENDOR_OUT, EPNUM_VENDOR_IN, 64),
    TUD_MSC_DESCRIPTOR(ITF_NUM_MSC, 5, EPNUM_MSC_OUT, EPNUM_MSC_IN, 64)
};

uint8_t const *tud_descriptor_configuration_cb(uint8_t index) {
    (void)index;
    return desc_configuration;
}

/* ==========================================================================
 * BOS (Binary Object Store) Descriptor
 * Advertises two platform capabilities:
 *   1. WebUSB  — lets browsers discover the device (vendor code 0x01)
 *   2. MS OS 2.0 — tells Windows to fetch the WinUSB descriptor set
 *                   (vendor code 0x02, descriptor index 0x07)
 * ========================================================================== */
static const uint8_t desc_bos[] = {
    TUD_BOS_DESCRIPTOR(BOS_TOTAL_LEN, 2),
    TUD_BOS_WEBUSB_DESCRIPTOR(VENDOR_REQUEST_WEBUSB, 1),
    TUD_BOS_MS_OS_20_DESCRIPTOR(MS_OS_20_DESC_LEN, VENDOR_REQUEST_MICROSOFT)
};

uint8_t const *tud_descriptor_bos_cb(void) {
    return desc_bos;
}

/* ==========================================================================
 * Microsoft OS 2.0 Descriptor Set  (178 bytes, with function subset)
 *
 * Returned when the host sends a vendor request with
 *   bRequest = VENDOR_REQUEST_MICROSOFT (0x02), wIndex = 0x0007.
 *
 * Uses configuration + function subset headers so that the WinUSB
 * compatible ID applies ONLY to interface 0 (vendor). Interface 1 (MSC)
 * is left alone and gets the standard USB mass storage driver.
 *
 * Layout:
 *   [10] Set Header
 *   [8]  Configuration Subset Header  (config 0)
 *     [8]  Function Subset Header     (interface 0 = vendor)
 *       [20]  Compatible ID           "WINUSB"
 *       [132] Registry Property       DeviceInterfaceGUIDs
 *
 * GUID: {C56D9F32-245A-44F2-AC18-76E120ABBF31}
 * ========================================================================== */
static const uint8_t desc_ms_os_20[] = {
    /* ---- Set Header (10 bytes) ---- */
    /* wLength              */ U16_TO_U8S_LE(0x000A),
    /* wDescriptorType      */ U16_TO_U8S_LE(MS_OS_20_SET_HEADER_DESCRIPTOR),
    /* dwWindowsVersion     */ U32_TO_U8S_LE(0x06030000),  /* Windows 8.1+ */
    /* wTotalLength         */ U16_TO_U8S_LE(MS_OS_20_DESC_LEN),

    /* ---- Configuration Subset Header (8 bytes) ---- */
    /* wLength              */ U16_TO_U8S_LE(0x0008),
    /* wDescriptorType      */ U16_TO_U8S_LE(MS_OS_20_SUBSET_HEADER_CONFIGURATION),
    /* bConfigurationValue  */ 0x00,
    /* bReserved            */ 0x00,
    /* wTotalLength         */ U16_TO_U8S_LE(MS_OS_20_DESC_LEN - 0x0A),

    /* ---- Function Subset Header (8 bytes) — targets interface 0 (vendor) only ---- */
    /* wLength              */ U16_TO_U8S_LE(0x0008),
    /* wDescriptorType      */ U16_TO_U8S_LE(MS_OS_20_SUBSET_HEADER_FUNCTION),
    /* bFirstInterface      */ ITF_NUM_VENDOR,
    /* bReserved            */ 0x00,
    /* wTotalLength         */ U16_TO_U8S_LE(MS_OS_20_DESC_LEN - 0x0A - 0x08),

    /* ---- Compatible ID (20 bytes) — tells Windows to use WinUSB driver ---- */
    /* wLength              */ U16_TO_U8S_LE(0x0014),
    /* wDescriptorType      */ U16_TO_U8S_LE(MS_OS_20_FEATURE_COMPATBLE_ID),
    /* compatibleID         */ 'W', 'I', 'N', 'U', 'S', 'B', 0x00, 0x00,
    /* subCompatibleID      */ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,

    /* ---- Registry Property (132 bytes) — DeviceInterfaceGUIDs ---- */
    /* wLength              */ U16_TO_U8S_LE(0x0084),
    /* wDescriptorType      */ U16_TO_U8S_LE(MS_OS_20_FEATURE_REG_PROPERTY),
    /* wPropertyDataType    */ U16_TO_U8S_LE(0x0007),  /* REG_MULTI_SZ */
    /* wPropertyNameLength  */ U16_TO_U8S_LE(0x002A),  /* 42 bytes */
    /* PropertyName: L"DeviceInterfaceGUIDs\0" */
    'D', 0x00, 'e', 0x00, 'v', 0x00, 'i', 0x00, 'c', 0x00, 'e', 0x00,
    'I', 0x00, 'n', 0x00, 't', 0x00, 'e', 0x00, 'r', 0x00, 'f', 0x00,
    'a', 0x00, 'c', 0x00, 'e', 0x00, 'G', 0x00, 'U', 0x00, 'I', 0x00,
    'D', 0x00, 's', 0x00, 0x00, 0x00,
    /* wPropertyDataLength  */ U16_TO_U8S_LE(0x0050),  /* 80 bytes */
    /* PropertyData: L"{C56D9F32-245A-44F2-AC18-76E120ABBF31}\0\0" */
    '{', 0x00, 'C', 0x00, '5', 0x00, '6', 0x00, 'D', 0x00, '9', 0x00,
    'F', 0x00, '3', 0x00, '2', 0x00, '-', 0x00, '2', 0x00, '4', 0x00,
    '5', 0x00, 'A', 0x00, '-', 0x00, '4', 0x00, '4', 0x00, 'F', 0x00,
    '2', 0x00, '-', 0x00, 'A', 0x00, 'C', 0x00, '1', 0x00, '8', 0x00,
    '-', 0x00, '7', 0x00, '6', 0x00, 'E', 0x00, '1', 0x00, '2', 0x00,
    '0', 0x00, 'A', 0x00, 'B', 0x00, 'B', 0x00, 'F', 0x00, '3', 0x00,
    '1', 0x00, '}', 0x00, 0x00, 0x00, 0x00, 0x00
};

TU_VERIFY_STATIC(sizeof(desc_ms_os_20) == MS_OS_20_DESC_LEN, "Incorrect size");

/* ==========================================================================
 * WebUSB URL Descriptor
 * Returned for vendor request VENDOR_REQUEST_WEBUSB (0x01).
 * bScheme=1 means https://, so the full URL is https://localhost:8000/usb
 * ========================================================================== */
static const tusb_desc_webusb_url_t desc_url = {
    .bLength = 3 + sizeof(URL) - 1,
    .bDescriptorType = 3,  /* WEBUSB_URL descriptor type */
    .bScheme = 1,          /* 1 = https:// */
    .url = URL
};

/* ==========================================================================
 * Vendor Control Transfer Handler
 * Responds to vendor-type control requests on EP0:
 *   0x01 (VENDOR_REQUEST_WEBUSB)    → return WebUSB URL descriptor
 *   0x02 (VENDOR_REQUEST_MICROSOFT) → return MS OS 2.0 descriptor set
 *                                      (only for wIndex == 0x0007)
 *   0x03 (VENDOR_REQUEST_PARAMS)    → return device parameters string
 * ========================================================================== */

/* Defined in main.c */
extern const char params[];

bool tud_vendor_control_xfer_cb(uint8_t rhport, uint8_t stage, const tusb_control_request_t *request) {
    if (stage != CONTROL_STAGE_SETUP) {
        return true;
    }

    if (request->bmRequestType_bit.type != TUSB_REQ_TYPE_VENDOR) {
        return false;
    }

    switch (request->bRequest) {
        case VENDOR_REQUEST_WEBUSB:
            return tud_control_xfer(rhport, request, (void *)(uintptr_t)&desc_url, desc_url.bLength);

        case VENDOR_REQUEST_MICROSOFT:
            if (request->wIndex == 0x0007) {
                uint16_t total_len = 0;
                memcpy(&total_len, desc_ms_os_20 + 8, sizeof(total_len));
                return tud_control_xfer(rhport, request, (void *)(uintptr_t)desc_ms_os_20, total_len);
            }
            return false;

        case VENDOR_REQUEST_PARAMS:
            return tud_control_xfer(rhport, request, (void *)(uintptr_t)params, (uint16_t)strlen(params));

        default:
            return false;
    }
}

/* ==========================================================================
 * String Descriptors
 * Index 0: supported language (English US, 0x0409)
 * Index 1: manufacturer
 * Index 2: product
 * Index 3: serial number
 * Index 4: vendor interface name
 * Index 5: MSC interface name
 * ========================================================================== */
static const char *string_desc_arr[] = {
    (const char[]){0x09, 0x04},  /* Language ID: English US */
    "Elmot",                     /* 1: Manufacturer */
    "Elmot Smart Servo",         /* 2: Product */
    "0001",                      /* 3: Serial */
    "Elmot Smart Servo",         /* 4: Vendor interface */
    "Elmot Servo Virtual Disk"         /* 5: MSC interface */
};

static uint16_t _desc_str[32];

uint16_t const *tud_descriptor_string_cb(uint8_t index, uint16_t langid) {
    (void)langid;

    uint8_t chr_count;
    if (index == 0) {
        _desc_str[1] = 0x0409;
        chr_count = 1;
    } else {
        if (index >= sizeof(string_desc_arr) / sizeof(string_desc_arr[0])) {
            return NULL;
        }

        const char *str = string_desc_arr[index];
        chr_count = (uint8_t)strlen(str);
        if (chr_count > 31) {
            chr_count = 31;
        }

        for (uint8_t i = 0; i < chr_count; i++) {
            _desc_str[1 + i] = (uint8_t)str[i];
        }
    }

    _desc_str[0] = (uint16_t)((TUSB_DESC_STRING << 8) | (2 * chr_count + 2));
    return _desc_str;
}
