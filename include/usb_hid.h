#pragma once

#include <stdint.h>

#include <stdbool.h>

bool usb_hid_ready(void);

bool usb_hid_tap(uint8_t modifier, uint8_t keycode);

bool usb_hid_type_char(char c);

int usb_hid_type_str(const char * s);