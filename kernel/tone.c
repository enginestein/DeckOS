#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include "pico/stdlib.h"
#include "hardware/pwm.h"
#include "hardware/clocks.h"
#include "tone.h"

// C  C# D  D# E  F  F# G  G# A  A# B
static const uint32_t note_hz_oct4[12] = {
    262, 277, 294, 311, 330, 349, 370, 392, 415, 440, 466, 494
};

// Note names for lookup
static const char* note_names[12] = {
    "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"
};

uint32_t tone_note_to_hz(const char* note) {
    if (!note) return 0;
    if (strcasecmp(note, "REST") == 0 || toupper(note[0]) == 'R') return 0;

    // Parse note name (1-2 chars) + octave digit
    char name[4] = {0};
    int  ni = 0;
    int  i  = 0;
    while (note[i] && !isdigit((unsigned char)note[i]) && ni < 3)
        name[ni++] = toupper((unsigned char)note[i++]);
    name[ni] = '\0';

    int octave = isdigit((unsigned char)note[i]) ? (note[i] - '0') : 4;

    // Support flats: Bb = A#
    if (name[1] == 'B') { name[1] = '#'; name[0]--; }

    int semitone = -1;
    for (int s = 0; s < 12; s++) {
        if (strcasecmp(name, note_names[s]) == 0) { semitone = s; break; }
    }
    if (semitone < 0) return 0;

    // Scale from octave 4
    uint32_t hz = note_hz_oct4[semitone];
    int diff = octave - 4;
    if (diff > 0) hz <<= diff;
    else if (diff < 0) hz >>= (-diff);
    return hz;
}

void tone_play(uint8_t pin, uint32_t freq_hz, uint32_t duration_ms) {
    gpio_set_function(pin, GPIO_FUNC_PWM);
    uint slice  = pwm_gpio_to_slice_num(pin);
    uint channel = pwm_gpio_to_channel(pin);

    if (freq_hz == 0) {
        pwm_set_enabled(slice, false);
        sleep_ms(duration_ms);
        return;
    }

    uint32_t sys_hz = clock_get_hz(clk_sys);
    // divider: find integer + frac such that wrap ~ 1000 for 50% duty
    // freq = sys_hz / (divider * wrap)  => divider = sys_hz / (freq * wrap)
    uint32_t wrap    = 999;
    uint32_t divider = sys_hz / (freq_hz * (wrap + 1));
    if (divider < 1)  divider = 1;
    if (divider > 255) divider = 255;

    pwm_config cfg = pwm_get_default_config();
    pwm_config_set_clkdiv_int(&cfg, divider);
    pwm_config_set_wrap(&cfg, wrap);
    pwm_init(slice, &cfg, false);
    pwm_set_chan_level(slice, channel, wrap / 2);   // 50% duty
    pwm_set_enabled(slice, true);

    sleep_ms(duration_ms);

    pwm_set_enabled(slice, false);
    gpio_set_function(pin, GPIO_FUNC_SIO);
    gpio_set_dir(pin, GPIO_OUT);
    gpio_put(pin, 0);
}

void tone_stop(uint8_t pin) {
    uint slice = pwm_gpio_to_slice_num(pin);
    pwm_set_enabled(slice, false);
}

void tone_melody(uint8_t pin, const char* sequence) {
    if (!sequence) return;

    // tokenise by spaces
    char buf[256];
    strncpy(buf, sequence, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';

    char* tok = strtok(buf, " ");
    while (tok) {
        // Split on ':'
        char* colon = strchr(tok, ':');
        uint32_t ms = 200;
        if (colon) {
            *colon = '\0';
            ms = (uint32_t)atoi(colon + 1);
            if (ms < 10) ms = 10;
        }
        uint32_t hz = tone_note_to_hz(tok);
        printf("  %s -> %lu Hz, %lu ms\n", tok, hz, ms);
        tone_play(pin, hz, ms);
        sleep_ms(30);   // tiny gap between notes
        tok = strtok(NULL, " ");
    }
}