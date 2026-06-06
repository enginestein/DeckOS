#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "pico/stdlib.h"
#include "module.h"
#include "esp.h"

static void cmd_mqtt_handler(int argc, char *argv[]) {
  if (argc < 2) {
    printf("usage:\n");
    printf("  mqtt server <host>        - set broker address\n");
    printf("  mqtt port <n>             - set broker port (default 1883)\n");
    printf("  mqtt id <name>            - set client ID\n");
    printf("  mqtt connect              - connect to broker\n");
    printf("  mqtt disconnect           - disconnect\n");
    printf("  mqtt status               - show connection state\n");
    printf("  mqtt pub <topic> <msg>    - publish message\n");
    printf("  mqtt sub <topic>          - subscribe to topic\n");
    printf("  mqtt unsub <topic>        - unsubscribe\n");
    return;
  }

  if (!esp8266_is_ready()) {
    printf("mqtt: wifi not initialised - run 'wifi init' first\n");
    return;
  }

  char cmd[128];

  if (strcmp(argv[1], "server") == 0 && argc >= 3) {
    snprintf(cmd, sizeof(cmd), "@mqtt server %s", argv[2]);
    esp8266_send_raw(cmd);
  } else if (strcmp(argv[1], "port") == 0 && argc >= 3) {
    snprintf(cmd, sizeof(cmd), "@mqtt port %s", argv[2]);
    esp8266_send_raw(cmd);
  } else if (strcmp(argv[1], "id") == 0 && argc >= 3) {
    snprintf(cmd, sizeof(cmd), "@mqtt id %s", argv[2]);
    esp8266_send_raw(cmd);
  } else if (strcmp(argv[1], "connect") == 0) {
    esp8266_send_raw("@mqtt connect");
  } else if (strcmp(argv[1], "disconnect") == 0) {
    esp8266_send_raw("@mqtt disconnect");
  } else if (strcmp(argv[1], "status") == 0) {
    esp8266_send_raw("@mqtt status");
  } else if (strcmp(argv[1], "pub") == 0 && argc >= 4) {
    snprintf(cmd, sizeof(cmd), "@mqtt pub %s %s", argv[2], argv[3]);
    esp8266_send_raw(cmd);
  } else if (strcmp(argv[1], "sub") == 0 && argc >= 3) {
    snprintf(cmd, sizeof(cmd), "@mqtt sub %s", argv[2]);
    esp8266_send_raw(cmd);
  } else if (strcmp(argv[1], "unsub") == 0 && argc >= 3) {
    snprintf(cmd, sizeof(cmd), "@mqtt unsub %s", argv[2]);
    esp8266_send_raw(cmd);
  } else {
    printf("mqtt: unknown subcommand '%s'\n", argv[1]);
  }

  sleep_ms(300);
  esp8266_drain_response();
}

static module_cmd_t s_cmds[] = {
    {"mqtt", "MQTT client (server/port/id/connect/disconnect/status/pub/sub/unsub)", cmd_mqtt_handler},
};

static bool mod_mqtt_load(void) {
    printf("mqtt: module loaded (requires wifi init first)\n");
    return true;
}

static void mod_mqtt_unload(void) {
    esp8266_send_raw("@mqtt disconnect");
    sleep_ms(300);
    esp8266_drain_response();
    printf("mqtt: module unloaded\n");
}

plugin_api_t MOD_MQTT = {
    .init = mod_mqtt_load,
    .deinit = mod_mqtt_unload,
    .commands = s_cmds,
    .command_count = sizeof(s_cmds) / sizeof(s_cmds[0]),
    .on_event = NULL,
};
