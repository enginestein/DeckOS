#pragma once
#include <stdint.h>

/** Play a frequency on a GPIO pin for a duration.
 *  @param pin        GPIO with buzzer attached.
 *  @param freq_hz    Frequency in Hz (0 = silence/rest).
 *  @param duration_ms How long to play. */
void tone_play(uint8_t pin, uint32_t freq_hz, uint32_t duration_ms);

/** Stop any active tone on a pin. */
void tone_stop(uint8_t pin);

/** Play a space-separated sequence of note tokens.
 *  Format: "NOTE[:MS]" e.g. "C4:200 E4:200 G4:400 REST:100"
 *  Default duration if omitted: 200 ms. */
void tone_melody(uint8_t pin, const char* sequence);

/** Convert a note name ("C4", "A#3", etc.) to Hz.  Returns 0 for REST/unknown. */
uint32_t tone_note_to_hz(const char* note);