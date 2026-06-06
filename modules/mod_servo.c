#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "pico/stdlib.h"
#include "module.h"
#include "servo.h"

static void cmd_servo_handler(int argc, char *argv[]) {
  if (argc < 2) {
    printf("usage:\n");
    printf("  servo <pin> <angle 0-180>             - set position\n");
    printf("  servo sweep <pin> [from to step_ms]   - blocking sweep\n");
    printf("  servo bg <pin> sweep [min max step step_ms] - background sweep\n");
    printf("  servo bg <pin> goto <angle> [step_ms] - background move to angle\n");
    printf("  servo bg <pin> stop                   - stop background servo\n");
    printf("  servo bg list                         - list background servos\n");
    return;
  }

  if (strcmp(argv[1], "bg") == 0) {
    if (argc < 3) { printf("servo bg: need subcommand\n"); return; }
    if (strcmp(argv[2], "list") == 0) { servo_bg_list(); return; }
    int pin = atoi(argv[2]);
    if (pin < 0 || pin > 28) { printf("invalid pin\n"); return; }
    if (argc < 4) { printf("servo bg <pin> sweep|goto|stop\n"); return; }
    if (strcmp(argv[3], "sweep") == 0) {
      int min_deg = (argc >= 5) ? atoi(argv[4]) : 0;
      int max_deg = (argc >= 6) ? atoi(argv[5]) : 180;
      int step_deg = (argc >= 7) ? atoi(argv[6]) : 1;
      uint32_t step_ms = (argc >= 8) ? (uint32_t)atoi(argv[7]) : 20;
      int slot = servo_bg_add((uint8_t)pin, "bg-servo");
      if (slot < 0) return;
      servo_bg_set_sweep(slot, min_deg, max_deg, step_deg, step_ms);
    } else if (strcmp(argv[3], "goto") == 0) {
      if (argc < 5) { printf("servo bg <pin> goto <angle> [step_ms]\n"); return; }
      int target = atoi(argv[4]);
      uint32_t sms = (argc >= 6) ? (uint32_t)atoi(argv[5]) : 10;
      int slot = servo_bg_add((uint8_t)pin, "bg-servo");
      if (slot < 0) return;
      servo_bg_set_goto(slot, target, sms);
      printf("servo bg: GPIO%d going to %d°\n", pin, target);
    } else if (strcmp(argv[3], "stop") == 0) {
      int slot = servo_bg_find((uint8_t)pin);
      if (slot < 0) { printf("no background servo on GPIO%d\n", pin); return; }
      servo_bg_stop(slot);
    } else { printf("unknown servo bg subcommand: %s\n", argv[3]); }
    return;
  }

  if (strcmp(argv[1], "sweep") == 0) {
    if (argc < 3) { printf("servo sweep <pin> [from to step_ms]\n"); return; }
    int pin = atoi(argv[2]);
    int from = (argc >= 4) ? atoi(argv[3]) : 0;
    int to = (argc >= 5) ? atoi(argv[4]) : 180;
    int step_ms = (argc >= 6) ? atoi(argv[5]) : 20;
    if (pin < 0 || pin > 28) { printf("invalid pin\n"); return; }
    servo_sweep_blocking((uint8_t)pin, from, to, step_ms);
    return;
  }

  if (argc < 3) { printf("usage: servo <pin> <angle 0-180>\n"); return; }
  int pin = atoi(argv[1]);
  int angle = atoi(argv[2]);
  if (pin < 0 || pin > 28) { printf("invalid pin\n"); return; }
  if (angle < 0 || angle > 180) { printf("angle must be 0-180\n"); return; }
  servo_write_angle((uint8_t)pin, angle);
}

static module_cmd_t s_cmds[] = {
    {"servo", "Servo control (pin angle / sweep / bg sweep/goto/stop/list)", cmd_servo_handler},
};

static bool mod_servo_load(void) {
    printf("servo: module loaded\n");
    return true;
}

static void mod_servo_unload(void) {
    for (int i = 0; i < SERVO_MAX_SLOTS; i++)
        servo_bg_stop(i);
    printf("servo: module unloaded\n");
}

plugin_api_t MOD_SERVO = {
    .init = mod_servo_load,
    .deinit = mod_servo_unload,
    .commands = s_cmds,
    .command_count = sizeof(s_cmds) / sizeof(s_cmds[0]),
    .on_event = NULL,
};
