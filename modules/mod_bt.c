#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "pico/stdlib.h"
#include "module.h"
#include "bt.h"

static void cmd_bt_handler(int argc, char *argv[]) {
  if (argc < 2) {
    printf("usage:\n");
    printf("  bt shell                           - interactive BT terminal\n");
    printf("  bt log <on|off>                    - enable/disable BT mirror log\n");
    printf("  bt exec <cmdline>                  - execute AT command\n");
    printf("  bt top <ms>                        - stream BT throughput\n");
    printf("  bt send <vfs_path>                 - send file via BT\n");
    printf("  bt recv <vfs_path>                 - receive file via BT\n");
    printf("  bt sniff <timeout_ms>              - sniff BT traffic\n");
    printf("  bt at                              - enter AT command mode\n");
    printf("  bt name <name>                     - set HC-05 device name\n");
    printf("  bt pin <code>                      - set HC-05 pairing PIN\n");
    printf("  bt baud <rate>                     - set HC-05 UART baud\n");
    printf("  bt status                          - show HC-05 status\n");
    printf("  bt init [baud]                     - init HC-05 on UART0\n");
    return;
  }

  if (strcmp(argv[1], "init") == 0) {
    uint32_t baud = (argc >= 3) ? (uint32_t)atoi(argv[2]) : BT_DEFAULT_BAUD;
    bt_init(baud);
  } else if (strcmp(argv[1], "shell") == 0) {
    bt_shell_run();
  } else if (strcmp(argv[1], "log") == 0 && argc >= 3) {
    bt_log_enable(strcmp(argv[2], "on") == 0);
    printf("bt: log %s\n", argv[2]);
  } else if (strcmp(argv[1], "exec") == 0 && argc >= 3) {
    bt_exec(argv[2]);
  } else if (strcmp(argv[1], "top") == 0 && argc >= 3) {
    bt_top_stream((uint32_t)atoi(argv[2]));
  } else if (strcmp(argv[1], "send") == 0 && argc >= 3) {
    bt_send_file(argv[2]);
  } else if (strcmp(argv[1], "recv") == 0 && argc >= 3) {
    bt_recv_file(argv[2]);
  } else if (strcmp(argv[1], "sniff") == 0 && argc >= 3) {
    bt_sniff((uint32_t)atoi(argv[2]));
  } else if (strcmp(argv[1], "at") == 0) {
    bt_at_mode();
  } else if (strcmp(argv[1], "name") == 0 && argc >= 3) {
    char cmd[64];
    snprintf(cmd, sizeof(cmd), "AT+NAME=%s", argv[2]);
    char resp[128];
    bt_at_cmd(cmd, resp, sizeof(resp), 2000);
    printf("%s\n", resp);
  } else if (strcmp(argv[1], "pin") == 0 && argc >= 3) {
    char cmd[64];
    snprintf(cmd, sizeof(cmd), "AT+PSWD=%s", argv[2]);
    char resp[128];
    bt_at_cmd(cmd, resp, sizeof(resp), 2000);
    printf("%s\n", resp);
  } else if (strcmp(argv[1], "baud") == 0 && argc >= 3) {
    char cmd[64];
    snprintf(cmd, sizeof(cmd), "AT+UART=%s,0,0", argv[2]);
    char resp[128];
    bt_at_cmd(cmd, resp, sizeof(resp), 2000);
    printf("%s\n", resp);
  } else if (strcmp(argv[1], "status") == 0) {
    printf("BT: ready=%d connected=%d log=%d\n",
           bt_is_ready(), bt_is_connected(), bt_log_is_enabled());
  } else {
    printf("bt: unknown subcommand '%s'\n", argv[1]);
  }
}

static module_cmd_t s_cmds[] = {
    {"bt", "HC-05 Bluetooth control (shell/log/exec/top/send/recv/sniff/at/name/pin/baud/status/init)", cmd_bt_handler},
};

static bool mod_bt_load(void) {
    bt_init(BT_DEFAULT_BAUD);
    printf("bt: module loaded\n");
    return true;
}

static void mod_bt_unload(void) {
    printf("bt: module unloaded (no deinit available)\n");
}

plugin_api_t MOD_BT = {
    .init = mod_bt_load,
    .deinit = mod_bt_unload,
    .commands = s_cmds,
    .command_count = sizeof(s_cmds) / sizeof(s_cmds[0]),
    .on_event = NULL,
};
