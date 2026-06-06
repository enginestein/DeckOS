#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "pico/stdlib.h"
#include "module.h"
#include "morse.h"

#define LED_PIN 25

static void cmd_morse_handler(int argc, char *argv[]) {
  if (argc < 2) {
    printf("usage: morse <text> [wpm]\n");
    printf("       blinks the onboard LED in morse code\n");
    return;
  }
  uint8_t wpm = (argc >= 3) ? (uint8_t)atoi(argv[2]) : 13;
  char text[128];
  text[0] = '\0';
  for (int i = 1; i < argc - (argc >= 3 ? 1 : 0); i++) {
    if (i > 1) strncat(text, " ", sizeof(text) - strlen(text) - 1);
    strncat(text, argv[i], sizeof(text) - strlen(text) - 1);
  }
  morse_send(text, LED_PIN, wpm);
}

static module_cmd_t s_cmds[] = {
    {"morse", "morse <text> [wpm] - blink LED in morse code", cmd_morse_handler},
};

static bool mod_morse_load(void) {
    printf("morse: module loaded\n");
    return true;
}

static void mod_morse_unload(void) {
    printf("morse: module unloaded\n");
}

plugin_api_t MOD_MORSE = {
    .init = mod_morse_load,
    .deinit = mod_morse_unload,
    .commands = s_cmds,
    .command_count = sizeof(s_cmds) / sizeof(s_cmds[0]),
    .on_event = NULL,
};
