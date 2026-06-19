#pragma once

#include <stdint.h>
#include <stdbool.h>

bool usb_hid_ready(void);

bool usb_hid_tap(uint8_t modifier, uint8_t keycode);

bool usb_hid_type_char(char c);

int usb_hid_type_str(const char *s);

/* Mouse */
bool usb_hid_mouse_move(int8_t x, int8_t y);

bool usb_hid_mouse_scroll(int8_t v);

bool usb_hid_mouse_click(uint8_t buttons);

bool usb_hid_mouse_press(uint8_t buttons);

bool usb_hid_mouse_release(uint8_t buttons);
