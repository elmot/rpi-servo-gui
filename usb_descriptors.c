#include "tusb.h"
#include <string.h>

/* ---- Device identity ---- */
#define USB_VID 0xCAFE
#define USB_PID 0x4002
#define USB_BCD 0x0100

/* ---- Interface / endpoint layout ---- */
#define ITF_NUM_VENDOR 0
#define ITF_NUM_TOTAL 1

#define EPNUM_VENDOR_OUT 0x01
#define EPNUM_VENDOR_IN  0x81

#define CONFIG_TOTAL_LEN (TUD_CONFIG_DESC_LEN + TUD_VENDOR_DESC_LEN)

/* ---- Vendor request codes (used in BOS capabilities and control xfer handler) ---- */
#define VENDOR_REQUEST_WEBUSB    0x01  /* bRequest for WebUSB GET_URL */
#define VENDOR_REQUEST_MICROSOFT 0x02  /* bRequest for MS OS 2.0 descriptor set */

/* ---- WebUSB landing page URL (https:// prefix implied by bScheme=1) ---- */
#define URL "localhost:8000/usb" //todo set normal URL

/* ---- BOS descriptor sizes ---- */
#define BOS_TOTAL_LEN (TUD_BOS_DESC_LEN + TUD_BOS_WEBUSB_DESC_LEN + TUD_BOS_MICROSOFT_OS_DESC_LEN)

/* Total size of the MS OS 2.0 descriptor set: 10 + 20 + 132 = 162 (0xA2) */
#define MS_OS_20_DESC_LEN 0xA2

/* ==========================================================================
 * Device Descriptor
 * USB 2.1 vendor-specific device. Class 0xFF tells the host this device
 * does not conform to any standard USB class — the WinUSB driver is
 * assigned via the MS OS 2.0 compatible ID descriptor below.
 * ========================================================================== */
static const tusb_desc_device_t desc_device = {
    .bLength = sizeof(tusb_desc_device_t),
    .bDescriptorType = TUSB_DESC_DEVICE,
    .bcdUSB = 0x0210,  /* USB 2.1 — required for BOS descriptor support */

    .bDeviceClass = TUSB_CLASS_VENDOR_SPECIFIC,  /* 0xFF */
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
 * Single configuration with one vendor-class interface and two bulk
 * endpoints (IN 0x81, OUT 0x01), 64-byte max packet size.
 * ========================================================================== */
static const uint8_t desc_configuration[] = {
    TUD_CONFIG_DESCRIPTOR(1, ITF_NUM_TOTAL, 0, CONFIG_TOTAL_LEN, 0x00, 100),
    TUD_VENDOR_DESCRIPTOR(ITF_NUM_VENDOR, 4, EPNUM_VENDOR_OUT, EPNUM_VENDOR_IN, 64)
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
 * Microsoft OS 2.0 Descriptor Set  (162 bytes, flat — no subset headers)
 *
 * Returned when the host sends a vendor request with
 *   bRequest = VENDOR_REQUEST_MICROSOFT (0x02), wIndex = 0x0007.
 *
 * Layout (flat, device-wide):
 *   [10] Set Header           — identifies this as an MS OS 2.0 set
 *   [20] Compatible ID        — "WINUSB" → Windows loads the WinUSB driver
 *   [132] Registry Property   — writes DeviceInterfaceGUIDs into the registry
 *                                so user-mode apps can find the device
 *
 * GUID: {C56D9F32-245A-44F2-AC18-76E120ABBF31}
 * ========================================================================== */
static const uint8_t desc_ms_os_20[] = {
    /* ---- Set Header (10 bytes) ---- */
    /* wLength              */ U16_TO_U8S_LE(0x000A),
    /* wDescriptorType      */ U16_TO_U8S_LE(MS_OS_20_SET_HEADER_DESCRIPTOR),
    /* dwWindowsVersion     */ U32_TO_U8S_LE(0x06030000),  /* Windows 8.1+ */
    /* wTotalLength         */ U16_TO_U8S_LE(MS_OS_20_DESC_LEN),

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
 * ========================================================================== */
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
 * Index 4: interface name
 * ========================================================================== */
static const char *string_desc_arr[] = {
    (const char[]){0x09, 0x04},  /* Language ID: English US */
    "Elmot",                     /* Manufacturer */
    "Elmot Smart Servo",         /* Product */
    "0001",                      /* Serial */
    "WebUSB Vendor"              /* Interface */
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
