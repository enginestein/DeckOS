#pragma once
#include <stdint.h>
#include <stdbool.h>
#include "pico/stdlib.h"
#include "hardware/uart.h"

#define BT_UART         uart0
#define BT_TX_PIN       1
#define BT_RX_PIN       0
#define BT_DEFAULT_BAUD 115200

#define BT_STATE_PIN    0xFF

#define BT_RX_BUF       512
#define BT_CMD_BUF      256

#define BT_AT_BAUD      38400
void bt_init(uint32_t baud);
bool bt_is_ready(void);
bool bt_is_connected(void);
void bt_puts(const char* s);
void bt_printf(const char* fmt, ...);
int  bt_getchar(void);
void bt_log_enable(bool on);
bool bt_log_is_enabled(void);
void bt_log_mirror(const char* level, const char* tag, const char* msg,
                   uint32_t ts_ms);

void bt_shell_run(void);
void bt_exec(char* cmdline);
void bt_top_stream(uint32_t interval_ms);
void bt_send_file(const char* vfs_path);
void bt_recv_file(const char* vfs_path);
void bt_sniff(uint32_t timeout_ms);
void bt_at_mode(void);
bool bt_at_cmd(const char* cmd, char* resp_buf, int buf_len, uint32_t timeout_ms);