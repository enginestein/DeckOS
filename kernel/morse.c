#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include "pico/stdlib.h"
#include "hardware/gpio.h"
#include "morse.h"

// '.' = dot, '-' = dash
static const char* morse_alpha[26] = {
    ".-",   "-...", "-.-.", "-..",  ".",    "..-.", "--.",  "....",  // A-H
    "..",   ".---", "-.-",  ".-..", "--",   "-.",   "---",  ".--.",  // I-P
    "--.-", ".-.",  "...",  "-",    "..-",  "...-", ".--",  "-..-",  // Q-X
    "-.--", "--.."                                                     // Y-Z
};

static const char* morse_digit[10] = {
    "-----", ".----", "..---", "...--", "....-",   // 0-4
    ".....", "-....", "--...", "---..", "----."      // 5-9
};

// Common punctuation
static const char morse_punc_chars[] = ".,?!/()-=+";
static const char* morse_punc_code[] = {
    ".-.-.-", "--..--", "..--..", "-.-.--", "-..-.",
    "-.--.",  "-.--.-", "-...-",  ".-.-.",  "+-."
};

static const char* char_to_morse(char c) {
    c = toupper((unsigned char)c);
    if (c >= 'A' && c <= 'Z') return morse_alpha[c - 'A'];
    if (c >= '0' && c <= '9') return morse_digit[c - '0'];
    for (int i = 0; morse_punc_chars[i]; i++)
        if (c == morse_punc_chars[i]) return morse_punc_code[i];
    return NULL;
}

void morse_send(const char* text, uint8_t pin, uint8_t wpm) {
    if (!text) return;
    if (wpm < 1)  wpm = 1;
    if (wpm > 40) wpm = 40;

    // Standard: 1 WPM ≈ 60 ms per dot (PARIS word = 50 dots)
    uint32_t dot_ms = 1200 / wpm;

    gpio_init(pin);
    gpio_set_dir(pin, GPIO_OUT);
    gpio_put(pin, 0);

    printf("morse [%d WPM, dot=%lu ms]: ", wpm, dot_ms);

    for (int i = 0; text[i]; i++) {
        char c = text[i];

        if (c == ' ') {
            printf("  ");
            sleep_ms(dot_ms * 7);   // word gap
            continue;
        }

        const char* code = char_to_morse(c);
        if (!code) continue;

        printf("%c(%s) ", c, code);
        fflush(stdout);

        for (int j = 0; code[j]; j++) {
            gpio_put(pin, 1);
            sleep_ms(code[j] == '-' ? dot_ms * 3 : dot_ms);   // dash or dot
            gpio_put(pin, 0);
            if (code[j + 1]) sleep_ms(dot_ms);   // intra-char gap
        }
        if (text[i + 1] && text[i + 1] != ' ')
            sleep_ms(dot_ms * 3);   // inter-char gap
    }

    printf("\ndone.\n");
    gpio_put(pin, 0);
}