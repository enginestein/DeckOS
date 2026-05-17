#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include "pico/stdlib.h"
#include "hardware/pwm.h"
#include "hardware/clocks.h"
#include "tone.h"

static uint8_t  s_active_pin  = 0xFF;
static uint32_t s_active_freq = 0;

static const uint32_t note_hz_oct4[12] = {
    262, 277, 294, 311, 330, 349, 370, 392, 415, 440, 466, 494
};
static const char* note_names[12] = {
    "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"
};

uint32_t tone_note_to_hz(const char* note) {
    if (!note) return 0;
    if (strcasecmp(note, "REST") == 0 || toupper(note[0]) == 'R') return 0;

    char name[4] = {0};
    int  ni = 0;
    int  i  = 0;
    while (note[i] && !isdigit((unsigned char)note[i]) && ni < 3)
        name[ni++] = toupper((unsigned char)note[i++]);
    name[ni] = '\0';
    int octave = isdigit((unsigned char)note[i]) ? (note[i] - '0') : 4;

    // Handle flats: Bb -> A#
    if (ni >= 2 && name[1] == 'B') { name[1] = '#'; name[0]--; }

    int semitone = -1;
    for (int s = 0; s < 12; s++)
        if (strcasecmp(name, note_names[s]) == 0) { semitone = s; break; }
    if (semitone < 0) return 0;

    uint32_t hz = note_hz_oct4[semitone];
    int diff = octave - 4;
    if (diff > 0) hz <<= diff;
    else if (diff < 0) hz >>= (-diff);
    return hz;
}

void tone_play(uint8_t pin, uint32_t freq_hz, uint32_t duration_ms) {
    uint slice   = pwm_gpio_to_slice_num(pin);
    uint channel = pwm_gpio_to_channel(pin);

    if (freq_hz == 0) {
        if (s_active_pin == pin) {
            pwm_set_chan_level(slice, channel, 0);
        }
        sleep_ms(duration_ms);
        return;
    }

    uint32_t sys_hz = clock_get_hz(clk_sys);

    // Choose the largest wrap that keeps divider in range [1, 255]
    // freq = sys_hz / (divider * (wrap + 1))
    // So: divider * (wrap+1) = sys_hz / freq
    uint32_t target_counts = sys_hz / freq_hz;  // total counts needed

    // Pick wrap to keep divider <= 255, as large as possible for accuracy
    uint32_t wrap;
    float divider_real;

    if (target_counts <= 65536) {
        // Divider can be 1.0, vary wrap freely
        wrap         = target_counts > 0 ? target_counts - 1 : 0;
        divider_real = 1.0f;
    } else {
        // Need divider > 1; fix wrap at 0xFFFF for max resolution
        wrap         = 0xFFFF;
        divider_real = (float)target_counts / 65536.0f;
        if (divider_real > 255.9f) divider_real = 255.9f;
    }

    uint div_int  = (uint)divider_real;
    uint div_frac = (uint)((divider_real - (float)div_int) * 16.0f + 0.5f);
    if (div_frac > 15) { div_frac = 0; div_int++; }
    if (div_int  > 255) div_int = 255;
    if (div_int  < 1)   div_int = 1;

    if (s_active_pin != pin) {
        gpio_set_function(pin, GPIO_FUNC_PWM);
        pwm_config cfg = pwm_get_default_config();
        pwm_config_set_clkdiv_mode(&cfg, PWM_DIV_FREE_RUNNING);
        pwm_config_set_clkdiv(&cfg, (float)div_int + (float)div_frac / 16.0f);
        pwm_config_set_wrap(&cfg, wrap);
        pwm_init(slice, &cfg, true);
        s_active_pin = pin;
    } else {
        pwm_set_enabled(slice, false);
        pwm_set_clkdiv_int_frac(slice, (uint8_t)div_int, (uint8_t)div_frac);
        pwm_set_wrap(slice, wrap);
        pwm_set_enabled(slice, true);
    }

    pwm_set_chan_level(slice, channel, wrap / 2);  // 50% duty
    s_active_freq = freq_hz;

    sleep_ms(duration_ms);
    pwm_set_chan_level(slice, channel, 0);
}
void tone_stop(uint8_t pin) {
    uint slice = pwm_gpio_to_slice_num(pin);
    pwm_set_enabled(slice, false);
    gpio_set_function(pin, GPIO_FUNC_SIO);
    gpio_set_dir(pin, GPIO_OUT);
    gpio_put(pin, 0);
    s_active_pin  = 0xFF;
    s_active_freq = 0;
}

void tone_melody(uint8_t pin, const char* sequence) {
    if (!sequence) return;
    char buf[2048];
    strncpy(buf, sequence, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';

    char* saveptr = NULL;
    char* tok = strtok_r(buf, " ", &saveptr);
    while (tok) {
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
        // Inter-note articulation gap — scales with note length
        if      (ms >= 300) sleep_ms(12);
        else if (ms >= 150) sleep_ms(8);
        else                sleep_ms(4);
        tok = strtok_r(NULL, " ", &saveptr);
    }
    tone_stop(pin);  // clean up GPIO at end
}