#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include "pico/stdlib.h"
#include "module.h"
#include "esp.h"

static void cmd_wifi_handler(int argc, char *argv[]) {
  if (argc < 2) {
    printf("usage:\n");
    printf("  wifi init [baud]        - initialise ESP8266 on UART1\n");
    printf("  wifi status             - show ESP8266 wiring and runtime state\n");
    printf("  wifi ping               - probe module\n");
    printf("  wifi scan               - scan nearby access points\n");
    printf("  wifi join <ssid> <pass> - join a network\n");
    printf("  wifi ip                 - show assigned IP\n");
    printf("  wifi shell              - interactive AT shell\n");
    printf("  wifi deinit             - release UART1\n");
    printf("  wifi bridge <sub>       - bridge control (auto|at|raw|status|reset)\n");
    printf("  wifi serve              - start HTTP server on port 80\n");
    printf("  wifi get <url>          - HTTP GET request\n");
    printf("  wifi post <url> <body>  - HTTP POST request\n");
    printf("  wifi telnet             - start telnet server on port 23\n");
    return;
  }

  if (strcmp(argv[1], "init") == 0) {
    uint32_t baud = (argc >= 3) ? (uint32_t)atoi(argv[2]) : ESP8266_DEFAULT_BAUD;
    if (baud < 1200 || baud > 921600) { printf("baud must be 1200-921600\n"); return; }
    esp8266_init(baud);
  } else if (strcmp(argv[1], "status") == 0) {
    esp8266_print_status();
  } else if (strcmp(argv[1], "ping") == 0) {
    esp8266_ping();
  } else if (strcmp(argv[1], "scan") == 0) {
    esp8266_scan();
  } else if (strcmp(argv[1], "join") == 0 && argc >= 4) {
    esp8266_join(argv[2], argv[3]);
  } else if (strcmp(argv[1], "ip") == 0) {
    esp8266_ip();
  } else if (strcmp(argv[1], "shell") == 0) {
    esp8266_shell();
  } else if (strcmp(argv[1], "deinit") == 0) {
    if (!esp8266_is_ready()) { printf("wifi: already deinitialised\n"); return; }
    esp8266_deinit();
    printf("wifi: ESP8266 released from UART1\n");
  } else if (strcmp(argv[1], "bridge") == 0) {
    if (argc < 3) { printf("bridge: auto|at|raw|status|reset|scan|connect\n"); return; }
    if (strcmp(argv[2], "auto") == 0) esp8266_bridge_mode_set("auto");
    else if (strcmp(argv[2], "at") == 0) esp8266_bridge_mode_set("at");
    else if (strcmp(argv[2], "raw") == 0) esp8266_bridge_mode_set("raw");
    else if (strcmp(argv[2], "status") == 0) esp8266_bridge_status();
    else if (strcmp(argv[2], "reset") == 0) esp8266_bridge_reset();
    else if (strcmp(argv[2], "scan") == 0) esp8266_bridge_scan();
    else if (strcmp(argv[2], "connect") == 0) esp8266_bridge_connect();
    else printf("unknown bridge subcommand: %s\n", argv[2]);
  } else if (strcmp(argv[1], "serve") == 0) {
    esp8266_http_serve();
  } else if (strcmp(argv[1], "telnet") == 0) {
    if (argc >= 3 && strcmp(argv[2], "stop") == 0) esp8266_telnet_stop();
    else esp8266_telnet_start();
  } else if (strcmp(argv[1], "get") == 0 && argc >= 3) {
    esp8266_http_get(argv[2]);
  } else if (strcmp(argv[1], "post") == 0 && argc >= 4) {
    esp8266_http_post(argv[2], argv[3]);
  } else {
    printf("wifi: unknown subcommand '%s'\n", argv[1]);
  }
}

static module_cmd_t s_cmds[] = {
    {"wifi", "ESP8266 WiFi control (init/status/ping/scan/join/ip/shell/deinit/bridge/serve/telnet/get/post)", cmd_wifi_handler},
};

static bool mod_wifi_load(void) {
    printf("wifi: module loaded\n");
    return true;
}

static void mod_wifi_unload(void) {
    esp8266_deinit();
    printf("wifi: module unloaded\n");
}

plugin_api_t MOD_WIFI = {
    .init = mod_wifi_load,
    .deinit = mod_wifi_unload,
    .commands = s_cmds,
    .command_count = sizeof(s_cmds) / sizeof(s_cmds[0]),
    .on_event = NULL,
};
