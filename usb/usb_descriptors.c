#include "tusb.h"
#include "pico/unique_id.h"

#ifndef USBD_VID
#define USBD_VID 0x2E8A
#endif
#ifndef USBD_PID
#define USBD_PID 0x000B
#endif

enum {
    USBD_STR_LANG = 0x00,
    USBD_STR_MANUF = 0x01,
    USBD_STR_PRODUCT = 0x02,
    USBD_STR_SERIAL = 0x03,
    USBD_STR_CDC = 0x04,
    USBD_STR_MSC = 0x05,
    USBD_STR_HID_KEYBOARD = 0x06,
    USBD_STR_HID_MOUSE = 0x07,
};

static
const tusb_desc_device_t desc_device = {
    .bLength = sizeof(tusb_desc_device_t),
    .bDescriptorType = TUSB_DESC_DEVICE,
    .bcdUSB = 0x0200,
    .bDeviceClass = TUSB_CLASS_MISC,
    .bDeviceSubClass = MISC_SUBCLASS_COMMON,
    .bDeviceProtocol = 0,
    .bMaxPacketSize0 = CFG_TUD_ENDPOINT0_SIZE,
    .idVendor = USBD_VID,
    .idProduct = USBD_PID,
    .bcdDevice = 0x0300,
    .iManufacturer = USBD_STR_MANUF,
    .iProduct = USBD_STR_PRODUCT,
    .iSerialNumber = USBD_STR_SERIAL,
    .bNumConfigurations = 1,
};

const uint8_t *tud_descriptor_device_cb(void) {
    return (const uint8_t *)&desc_device;
}

enum {
    REPORT_ID_KEYBOARD = 1,
    REPORT_ID_MOUSE = 2,
};

static
const uint8_t desc_hid_report_keyboard[] = {
    TUD_HID_REPORT_DESC_KEYBOARD(HID_REPORT_ID(REPORT_ID_KEYBOARD))
};

static
const uint8_t desc_hid_report_mouse[] = {
    TUD_HID_REPORT_DESC_MOUSE(HID_REPORT_ID(REPORT_ID_MOUSE))
};

const uint8_t *tud_hid_descriptor_report_cb(uint8_t instance) {
    switch (instance) {
    case 0: return desc_hid_report_keyboard;
    case 1: return desc_hid_report_mouse;
    default: return NULL;
    }
}

uint8_t deckos_hid_keyboard_report_id(void) {
    return REPORT_ID_KEYBOARD;
}

uint8_t deckos_hid_mouse_report_id(void) {
    return REPORT_ID_MOUSE;
}

enum {
    ITF_NUM_CDC = 0,
    ITF_NUM_CDC_DATA,
    ITF_NUM_MSC,
    ITF_NUM_HID_KEYBOARD,
    ITF_NUM_HID_MOUSE,
    ITF_NUM_TOTAL
};

#define EPNUM_CDC_NOTIF     0x81
#define EPNUM_CDC_OUT       0x02
#define EPNUM_CDC_IN        0x82
#define EPNUM_MSC_OUT       0x03
#define EPNUM_MSC_IN        0x83
#define EPNUM_HID_KB_IN     0x84
#define EPNUM_HID_MOUSE_IN  0x85

#define CONFIG_TOTAL_LEN (TUD_CONFIG_DESC_LEN + TUD_CDC_DESC_LEN + \
    TUD_MSC_DESC_LEN + TUD_HID_DESC_LEN + TUD_HID_DESC_LEN)

static
const uint8_t desc_configuration[] = {
    TUD_CONFIG_DESCRIPTOR(1, ITF_NUM_TOTAL, USBD_STR_LANG, CONFIG_TOTAL_LEN, 0x00, 250),

    TUD_CDC_DESCRIPTOR(ITF_NUM_CDC, USBD_STR_CDC, EPNUM_CDC_NOTIF, 8,
        EPNUM_CDC_OUT, EPNUM_CDC_IN, 64),

    TUD_MSC_DESCRIPTOR(ITF_NUM_MSC, USBD_STR_MSC, EPNUM_MSC_OUT, EPNUM_MSC_IN, 64),

    TUD_HID_DESCRIPTOR(ITF_NUM_HID_KEYBOARD, USBD_STR_HID_KEYBOARD,
        HID_ITF_PROTOCOL_KEYBOARD, sizeof(desc_hid_report_keyboard),
        EPNUM_HID_KB_IN, CFG_TUD_HID_EP_BUFSIZE, 10),

    TUD_HID_DESCRIPTOR(ITF_NUM_HID_MOUSE, USBD_STR_HID_MOUSE,
        HID_ITF_PROTOCOL_MOUSE, sizeof(desc_hid_report_mouse),
        EPNUM_HID_MOUSE_IN, CFG_TUD_HID_EP_BUFSIZE, 10),
};

const uint8_t *tud_descriptor_configuration_cb(uint8_t index) {
    (void)index;
    return desc_configuration;
}

static char serial_str[2 * PICO_UNIQUE_BOARD_ID_SIZE_BYTES + 1];

static const char *const string_desc[] = {
    [USBD_STR_LANG] = (const char[]){0x09, 0x04},
    [USBD_STR_MANUF] = "DeckOS",
    [USBD_STR_PRODUCT] = "DeckOS Portable",
    [USBD_STR_SERIAL] = serial_str,
    [USBD_STR_CDC] = "DeckOS Shell",
    [USBD_STR_MSC] = "DeckOS Disk",
    [USBD_STR_HID_KEYBOARD] = "DeckOS Keyboard",
    [USBD_STR_HID_MOUSE] = "DeckOS Mouse",
};

const uint16_t *tud_descriptor_string_cb(uint8_t index, uint16_t langid) {
    (void)langid;
    static uint16_t desc_str[32];
    uint8_t len;

    if (index == USBD_STR_LANG) {
        memcpy(&desc_str[1], string_desc[USBD_STR_LANG], 2);
        len = 1;
    } else {
        if (index >= (sizeof(string_desc) / sizeof(string_desc[0]))) return NULL;
        if (index == USBD_STR_SERIAL && !serial_str[0]) {
            pico_get_unique_board_id_string(serial_str, sizeof(serial_str));
        }
        const char *str = string_desc[index];
        if (!str) return NULL;
        len = (uint8_t)strlen(str);
        if (len > 31) len = 31;
        for (uint8_t i = 0; i < len; i++) desc_str[1 + i] = str[i];
    }
    desc_str[0] = (uint16_t)((TUSB_DESC_STRING << 8) | (2 * len + 2));
    return desc_str;
}
