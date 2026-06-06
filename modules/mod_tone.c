#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include "pico/stdlib.h"
#include "module.h"
#include "tone.h"

static void cmd_tone_handler(int argc, char *argv[]) {
  if (argc < 3) {
    printf("usage: tone <pin> <note|hz> [duration_ms]\n");
    printf("       note examples: C4 G#3 A5 REST\n");
    return;
  }
  int pin = atoi(argv[1]);
  if (pin < 0 || pin > 28) { printf("invalid pin\n"); return; }
  uint32_t duration = (argc >= 4) ? (uint32_t)atoi(argv[3]) : 500;
  if (duration < 10 || duration > 10000) { printf("duration 10-10000 ms\n"); return; }
  uint32_t hz;
  if (isdigit((unsigned char)argv[2][0])) hz = (uint32_t)atoi(argv[2]);
  else hz = tone_note_to_hz(argv[2]);
  printf("tone: GPIO%d %lu Hz %lu ms\n", pin, hz, duration);
  tone_play((uint8_t)pin, hz, duration);
}

static void cmd_melody_handler(int argc, char *argv[]) {
  if (argc < 3) {
    printf("usage: melody <pin> <C4:200 E4:200 G4:400 ...>\n");
    return;
  }
  int pin = atoi(argv[1]);
  if (pin < 0 || pin > 28) { printf("invalid pin\n"); return; }
  char seq[256] = "";
  for (int i = 2; i < argc; i++) {
    if (i > 2) strncat(seq, " ", sizeof(seq) - strlen(seq) - 1);
    strncat(seq, argv[i], sizeof(seq) - strlen(seq) - 1);
  }
  tone_melody((uint8_t)pin, seq);
}

static void cmd_piano_handler(int argc, char *argv[]) {
  if (argc < 2) {
    printf("usage: piano <pin> [duration_ms]\n");
    return;
  }
  int pin = atoi(argv[1]);
  if (pin < 0 || pin > 28) { printf("invalid pin\n"); return; }
  uint32_t dur = (argc >= 3) ? (uint32_t)atoi(argv[2]) : 150;
  if (dur < 20 || dur > 2000) dur = 150;
  printf("piano: GPIO%d, keys a-k = notes, q to quit\n", pin);
  while (true) {
    int c = getchar_timeout_us(50000);
    if (c == 'q' || c == 'Q' || c == EOF) break;
    if (c >= 'a' && c <= 'k') {
      static const uint32_t notes[] = {262, 294, 330, 349, 392, 440, 494, 523, 587, 659, 698};
      uint32_t hz = notes[c - 'a'];
      tone_play((uint8_t)pin, hz, dur);
    }
  }
}

static module_cmd_t s_cmds[] = {
    {"tone", "tone <pin> <note|hz> [ms] - play tone on buzzer", cmd_tone_handler},
    {"melody", "melody <pin> <C4:200 E4:200 ...> - play melody", cmd_melody_handler},
    {"piano", "piano <pin> [ms] - interactive piano keyboard", cmd_piano_handler},
};

static bool mod_tone_load(void) {
    printf("tone: module loaded\n");
    return true;
}

static void mod_tone_unload(void) {
    for (uint8_t p = 0; p < 29; p++) tone_stop(p);
    printf("tone: module unloaded\n");
}

plugin_api_t MOD_TONE = {
    .init = mod_tone_load,
    .deinit = mod_tone_unload,
    .commands = s_cmds,
    .command_count = sizeof(s_cmds) / sizeof(s_cmds[0]),
    .on_event = NULL,
};
