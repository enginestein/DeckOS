#pragma once
#include <stdint.h>

/** Send a string in morse code on the specified GPIO pin.
 *  @param text   ASCII string to encode.
 *  @param pin    GPIO pin to blink (25 = onboard LED).
 *  @param wpm    Words per minute (typical: 5-20, default 13). */
void morse_send(const char* text, uint8_t pin, uint8_t wpm);