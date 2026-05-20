#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "hardware/uart.h"

#define ESP8266_UART         uart1
#define ESP8266_TX_PIN       5
#define ESP8266_RX_PIN       4
#define ESP8266_DEFAULT_BAUD 115200
#define ESP8266_BRIDGE_MODE 1


void esp8266_bridge_mode_set(const char* mode);
void esp8266_bridge_status(void);
void esp8266_bridge_reset(void);
void esp8266_bridge_scan(void);
void esp8266_bridge_connect(void);
void esp8266_http_serve(void);
void esp8266_http_get(const char *url);
void esp8266_http_post(const char *url, const char *body);
void esp8266_telnet_start(void);
void esp8266_telnet_stop(void);
void esp8266_init(uint32_t baud);
void esp8266_deinit(void);
bool esp8266_is_ready(void);
uint32_t esp8266_baud(void);
uint8_t esp8266_tx_pin(void);
uint8_t esp8266_rx_pin(void);

bool esp8266_at_cmd(const char* cmd, char* resp_buf, int buf_len, uint32_t timeout_ms);

void esp8266_print_status(void);
void esp8266_ping(void);
void esp8266_scan(void);
void esp8266_join(const char* ssid, const char* password);
void esp8266_ip(void);
void esp8266_shell(void);
