#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "pico/stdlib.h"
#include "module.h"
#include "esp.h"

static void cmd_swarm_handler(int argc, char *argv[]) {
  if (argc < 2) {
    printf("usage:\n");
    printf("  swarm init                       - start ESP-NOW mesh\n");
    printf("  swarm id <name>                  - set this node's name\n");
    printf("  swarm peer <MAC>                 - add a peer drone\n");
    printf("  swarm pub <lat> <lon> <alt> <hdg> <state> - broadcast position\n");
    printf("  swarm list                       - show known peers\n");
    printf("  swarm mac                        - show this ESP MAC address\n");
    printf("  swarm stop                       - stop mesh\n");
    return;
  }

  if (!esp8266_is_ready()) {
    printf("swarm: wifi not initialised - run 'wifi init' first\n");
    return;
  }

  char cmd[128];

  if (strcmp(argv[1], "init") == 0) {
    esp8266_send_raw("@swarm init");
  } else if (strcmp(argv[1], "id") == 0 && argc >= 3) {
    snprintf(cmd, sizeof(cmd), "@swarm id %s", argv[2]);
    esp8266_send_raw(cmd);
  } else if (strcmp(argv[1], "peer") == 0 && argc >= 3) {
    snprintf(cmd, sizeof(cmd), "@swarm peer %s", argv[2]);
    esp8266_send_raw(cmd);
  } else if (strcmp(argv[1], "pub") == 0 && argc >= 7) {
    snprintf(cmd, sizeof(cmd), "@swarm pub %s %s %s %s %s",
             argv[2], argv[3], argv[4], argv[5], argv[6]);
    esp8266_send_raw(cmd);
  } else if (strcmp(argv[1], "list") == 0) {
    esp8266_send_raw("@swarm list");
  } else if (strcmp(argv[1], "mac") == 0) {
    esp8266_send_raw("@swarm mac");
  } else if (strcmp(argv[1], "stop") == 0) {
    esp8266_send_raw("@swarm stop");
  } else {
    printf("swarm: unknown subcommand '%s'\n", argv[1]);
    return;
  }

  sleep_ms(300);
  esp8266_drain_response();
}

static module_cmd_t s_cmds[] = {
    {"swarm", "ESP-NOW swarm mesh (init/id/peer/pub/list/mac/stop)", cmd_swarm_handler},
};

static bool mod_swarm_load(void) {
    esp8266_send_raw("@swarm init");
    sleep_ms(300);
    esp8266_drain_response();
    printf("swarm: module loaded\n");
    return true;
}

static void mod_swarm_unload(void) {
    esp8266_send_raw("@swarm stop");
    sleep_ms(300);
    esp8266_drain_response();
    printf("swarm: module unloaded\n");
}

plugin_api_t MOD_SWARM = {
    .init = mod_swarm_load,
    .deinit = mod_swarm_unload,
    .commands = s_cmds,
    .command_count = sizeof(s_cmds) / sizeof(s_cmds[0]),
    .on_event = NULL,
};
