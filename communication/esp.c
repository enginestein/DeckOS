#include <stdio.h>

#include <string.h>

#include <ctype.h>

#include "pico/stdlib.h"

#include "hardware/gpio.h"

#include "hardware/uart.h"

#include "esp.h"

#define ESP_RX_BUF 512

static bool s_ready = false;
static uint32_t s_baud = ESP8266_DEFAULT_BAUD;

static void esp8266_flush_rx(void) {
  while (uart_is_readable(ESP8266_UART)) {
    (void) uart_getc(ESP8266_UART);
  }
}

static void esp8266_write_line(const char * s) {
  if (!s) return;
  uart_puts(ESP8266_UART, s);
  uart_puts(ESP8266_UART, "\r\n");
}

static bool contains_ok(const char * s) {
  return s && (strstr(s, "OK") != NULL || strstr(s, "ready") != NULL);
}

void esp8266_send_raw(const char *cmd) {
    if (!s_ready) { printf("[wifi] not initialised\n"); return; }
    esp8266_bridge_send(cmd, 3000);
}

void esp8266_drain_response(void) {
    if (!s_ready) return;
    uint32_t start = to_ms_since_boot(get_absolute_time());
    while (to_ms_since_boot(get_absolute_time()) - start < 500) {
        if (uart_is_readable(ESP8266_UART)) {
            putchar((char)uart_getc(ESP8266_UART));
        } else {
            sleep_us(200);
        }
    }
}

void esp8266_init(uint32_t baud) {
  uint32_t use_baud = baud ? baud : ESP8266_DEFAULT_BAUD;

  uart_init(ESP8266_UART, use_baud);
  gpio_set_function(ESP8266_TX_PIN, GPIO_FUNC_UART);
  gpio_set_function(ESP8266_RX_PIN, GPIO_FUNC_UART);
  uart_set_format(ESP8266_UART, 8, 1, UART_PARITY_NONE);
  uart_set_fifo_enabled(ESP8266_UART, true);
  esp8266_flush_rx();

  s_ready = true;
  s_baud = use_baud;

  printf("[wifi] ESP8266 ready on UART%d  TX=GP%d RX=GP%d  %lu baud\n",
    ESP8266_UART == uart0 ? 0 : 1,
    ESP8266_TX_PIN, ESP8266_RX_PIN, s_baud);
  printf("[wifi] Tip: power the module from a stable 3.3V rail and tie EN/CH_PD high.\n");
}

void esp8266_deinit(void) {
  if (!s_ready) return;
  uart_deinit(ESP8266_UART);
  gpio_set_function(ESP8266_TX_PIN, GPIO_FUNC_SIO);
  gpio_set_function(ESP8266_RX_PIN, GPIO_FUNC_SIO);
  s_ready = false;
}

bool esp8266_is_ready(void) {
  return s_ready;
}

uint32_t esp8266_baud(void) {
  return s_baud;
}

uint8_t esp8266_tx_pin(void) {
  return ESP8266_TX_PIN;
}

uint8_t esp8266_rx_pin(void) {
  return ESP8266_RX_PIN;
}

bool esp8266_at_cmd(const char * cmd, char * resp_buf, int buf_len, uint32_t timeout_ms) {
  if (!s_ready || !cmd || !resp_buf || buf_len < 2) return false;

  esp8266_flush_rx();

  uart_puts(ESP8266_UART, cmd);
  uart_puts(ESP8266_UART, "\r\n");

  int pos = 0;
  uint32_t start = to_ms_since_boot(get_absolute_time());
  while (pos < buf_len - 1) {
    while (uart_is_readable(ESP8266_UART) && pos < buf_len - 1) {
      resp_buf[pos++] = (char) uart_getc(ESP8266_UART);
      resp_buf[pos] = '\0';

      if (strstr(resp_buf, "OK\r\n") || strstr(resp_buf, "ERROR\r\n")) {
        return true;
      }
    }

    if ((to_ms_since_boot(get_absolute_time()) - start) >= timeout_ms) break;
    sleep_us(200);
  }

  resp_buf[pos] = '\0';
  return pos > 0;
}

void esp8266_print_status(void) {
  printf("ESP8266 status:\n");
  printf("  ready      : %s\n", s_ready ? "yes" : "no");
  printf("  uart       : UART%d\n", ESP8266_UART == uart0 ? 0 : 1);
  printf("  tx/rx pins : GP%d / GP%d\n", ESP8266_TX_PIN, ESP8266_RX_PIN);
  printf("  baud       : %lu\n", s_baud);
  printf("  power      : use a stable 3.3V supply capable of 300mA+ peaks\n");
  printf("  boot pins  : EN/CH_PD=HIGH, RST=HIGH, GPIO0=HIGH, GPIO2=HIGH for normal boot\n");
}

void esp8266_bridge_send(const char *at_cmd, uint32_t timeout_ms) {
    if (!s_ready) { printf("[wifi] not initialised\n"); return; }

    esp8266_flush_rx();
    uart_puts(ESP8266_UART, at_cmd);
    uart_puts(ESP8266_UART, "\r\n");

    char line[256];
    int pos = 0;
    uint32_t start = to_ms_since_boot(get_absolute_time());

    while (true) {
        uint32_t now = to_ms_since_boot(get_absolute_time());
        if (now - start >= timeout_ms) break;

        if (uart_is_readable(ESP8266_UART)) {
            char c = (char)uart_getc(ESP8266_UART);

            if (pos < (int)sizeof(line) - 1) {
                line[pos++] = c;
                line[pos] = '\0';
            }
            if (c == '\n') {
                printf("%s", line);
                pos = 0;
                line[0] = '\0';
            }
        } else {
            sleep_us(500);
        }
    }

    if (pos > 0) { line[pos] = '\0'; printf("%s\n", line); }
}

void esp8266_bridge_mode_set(const char * mode) {
  char cmd[48];
  snprintf(cmd, sizeof(cmd), "@mode %s", mode);
  printf("[bridge] sending: %s\n", cmd);
  esp8266_bridge_send(cmd, 2000);
}

void esp8266_bridge_status(void) {
  printf("[bridge] sending: @status\n");
  esp8266_bridge_send("@status", 2000);
}

void esp8266_bridge_reset(void) {
  printf("[bridge] sending: @reset\n");
  esp8266_bridge_send("@reset", 1000);
}

void esp8266_bridge_scan(void) {
  if (!s_ready) {
    printf("[wifi] not initialised\n");
    return;
  }

  printf("[bridge] scanning WiFi networks...\n");
  esp8266_flush_rx();
  uart_puts(ESP8266_UART, "@scan\r\n");

  char line[256];
  int pos = 0;
  uint32_t start = to_ms_since_boot(get_absolute_time());

  while (to_ms_since_boot(get_absolute_time()) - start < 15000) {
    if (uart_is_readable(ESP8266_UART)) {
      char c = (char) uart_getc(ESP8266_UART);
      if (pos < (int) sizeof(line) - 1) {
        line[pos++] = c;
        line[pos] = '\0';
      }
      if (c == '\n') {
        printf("%s", line);

        if (strstr(line, "networks:") || strstr(line, "No networks")) {

          uint32_t quiet = to_ms_since_boot(get_absolute_time());
          while (to_ms_since_boot(get_absolute_time()) - quiet < 1000) {
            if (uart_is_readable(ESP8266_UART)) {
              char c2 = (char) uart_getc(ESP8266_UART);
              putchar(c2);
              quiet = to_ms_since_boot(get_absolute_time());
            } else {
              sleep_us(500);
            }
          }
          break;
        }
        pos = 0;
        line[0] = '\0';
      }
    } else {
      sleep_us(500);
    }
  }
}
void esp8266_bridge_connect(void) {
  printf("[bridge] sending: @connect\n");
  esp8266_bridge_send("@connect", 25000);
}

void esp8266_ping(void) {
  if (!s_ready) {
    printf("wifi: not initialised -- run 'wifi init' first\n");
    return;
  }

  char resp[ESP_RX_BUF];
  memset(resp, 0, sizeof(resp));

  printf("[wifi] bridge probe...\n");
  esp8266_bridge_send("@status", 2000);
}

void esp8266_scan(void) {
  if (!s_ready) {
    printf("wifi: not initialised -- run 'wifi init' first\n");
    return;
  }

  printf("[wifi] scanning via bridge...\n");
  esp8266_bridge_send("@scan", 15000);
}

void esp8266_join(const char *ssid, const char *password) {
    if (!s_ready) { printf("wifi: not initialised\n"); return; }
    if (!ssid || !*ssid) { printf("usage: wifi join <ssid> <password>\n"); return; }
    char cmd[256];
    snprintf(cmd, sizeof(cmd), "join %s %s", ssid, password ? password : "");
    printf("[wifi] storing credentials for '%s'...\n", ssid);
    
    esp8266_flush_rx();
    uart_puts(ESP8266_UART, cmd);
    uart_puts(ESP8266_UART, "\r\n");
    sleep_ms(500);                      
    esp8266_flush_rx();      

    printf("[wifi] connecting (this may take up to 20s)...\n");
    esp8266_bridge_send("@connect", 25000);
}

void esp8266_http_serve(void) {
    if (!s_ready) { printf("wifi: not initialised\n"); return; }
    printf("[wifi] starting HTTP server...\n");
    esp8266_bridge_send("@serve", 3000);
}

void esp8266_telnet_start(void) {
    if (!s_ready) { printf("wifi: not initialised\n"); return; }
    printf("[wifi] starting telnet server on port 23...\n");
    esp8266_bridge_send("@telnet", 3000);
}
void esp8266_telnet_stop(void) {
    if (!s_ready) return;
    printf("[wifi] stopping telnet server...\n");
    esp8266_flush_rx();
    uart_puts(ESP8266_UART, "@stoptelnet\r\n");
    sleep_ms(500);
    esp8266_flush_rx();
    printf("[wifi] telnet stopped\n");
}

void esp8266_http_get(const char *url) {
    if (!s_ready) { printf("wifi: not initialised\n"); return; }
    if (!url || !*url) { printf("usage: wifi get <url>\n"); return; }
    char cmd[256];
    snprintf(cmd, sizeof(cmd), "@get %s", url);
    printf("[wifi] GET %s\n", url);
    esp8266_bridge_send(cmd, 15000);
}

void esp8266_http_post(const char *url, const char *body) {
    if (!s_ready) { printf("wifi: not initialised\n"); return; }
    if (!url || !*url) { printf("usage: wifi post <url> <body>\n"); return; }
    char cmd[512];
    snprintf(cmd, sizeof(cmd), "@post %s %s", url, body ? body : "");
    printf("[wifi] POST %s\n", url);
    esp8266_bridge_send(cmd, 15000);
}

void esp8266_ip(void) {
  if (!s_ready) {
    printf("wifi: not initialised -- run 'wifi init' first\n");
    return;
  }
  esp8266_bridge_send("@status", 3000);
}

void esp8266_shell(void) {
  if (!s_ready) {
    printf("wifi: not initialised -- run 'wifi init' first\n");
    return;
  }

  printf("[wifi] interactive bridge shell\n");
  printf("       commands go straight to ESP8266; type EXIT to leave\n");
  printf("wifi> ");

  char line[128];
  int pos = 0;
  memset(line, 0, sizeof(line));

  while (true) {

    while (uart_is_readable(ESP8266_UART)) {
      putchar_raw((char) uart_getc(ESP8266_UART));
    }

    int c = getchar_timeout_us(0);
    if (c == PICO_ERROR_TIMEOUT) {
      sleep_us(200);
      continue;
    }

    if (c == '\r' || c == '\n') {
      printf("\r\n");
      line[pos] = '\0';

      if (strcasecmp(line, "EXIT") == 0) {
        printf("[wifi] leaving shell\n");
        break;
      }

      if (pos > 0) {
        uart_puts(ESP8266_UART, line);
        uart_puts(ESP8266_UART, "\r\n");
      }

      pos = 0;
      line[0] = '\0';
      printf("wifi> ");
      continue;
    }

    if ((c == 127 || c == '\b') && pos > 0) {
      pos--;
      line[pos] = '\0';
      printf("\b \b");
      continue;
    }

    if (c >= 32 && c < 127 && pos < (int) sizeof(line) - 1) {
      line[pos++] = (char) c;
      putchar(c);
    }
  }
}