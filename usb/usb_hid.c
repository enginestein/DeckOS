#include "tusb.h"

#include "usb_hid.h"

#include "pico/stdlib.h"

extern uint8_t deckos_hid_keyboard_report_id(void);

static
const uint8_t conv_table[128][2] = {
  HID_ASCII_TO_KEYCODE
};

bool usb_hid_ready(void) {
  return tud_hid_ready();
}

static bool wait_ready(void) {
  for (int i = 0; i < 200; i++) {
    if (tud_hid_ready()) return true;
    sleep_us(500);
  }
  return false;
}

bool usb_hid_tap(uint8_t modifier, uint8_t keycode) {
  if (!tud_mounted()) return false;
  uint8_t rid = deckos_hid_keyboard_report_id();
  uint8_t keys[6] = {
    keycode,
    0,
    0,
    0,
    0,
    0
  };

  if (!wait_ready()) return false;
  tud_hid_keyboard_report(rid, modifier, keys);

  if (!wait_ready()) return false;
  tud_hid_keyboard_report(rid, 0, NULL);

  if (!wait_ready()) return false;
  return true;
}

bool usb_hid_type_char(char c) {
  uint8_t uc = (uint8_t) c;
  if (uc >= 128) return false;
  uint8_t keycode = conv_table[uc][1];
  if (keycode == 0) return false;
  uint8_t modifier = conv_table[uc][0] ? KEYBOARD_MODIFIER_LEFTSHIFT : 0;
  return usb_hid_tap(modifier, keycode);
}

int usb_hid_type_str(const char * s) {
  int n = 0;
  for (;* s; s++) {
    if (usb_hid_type_char( * s)) n++;
  }
  return n;
}

uint16_t tud_hid_get_report_cb(uint8_t instance, uint8_t report_id,
  hid_report_type_t report_type,
  uint8_t * buffer, uint16_t reqlen) {
  (void) instance;
  (void) report_id;
  (void) report_type;
  (void) buffer;
  (void) reqlen;
  return 0;
}

void tud_hid_set_report_cb(uint8_t instance, uint8_t report_id,
  hid_report_type_t report_type,
  uint8_t
  const * buffer, uint16_t bufsize) {

  (void) instance;
  (void) report_id;
  (void) report_type;
  (void) buffer;
  (void) bufsize;
}