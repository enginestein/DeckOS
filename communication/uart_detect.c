#include <stdio.h>

#include <string.h>

#include <stdlib.h>

#include "pico/stdlib.h"

#include "hardware/gpio.h"

#include "hardware/uart.h"

#include "uart_detect.h"


static
const uint32_t COMMON_BAUDS[] = {
  9600,
  19200,
  38400,
  57600,
  115200,
  4800,
  2400,
  1200
};
static
const int BAUD_COUNT = (int)(sizeof(COMMON_BAUDS) / sizeof(COMMON_BAUDS[0]));

typedef struct {
  const char * name;
  const char * probe;
  const char * response;
  uint32_t baud;
}
uart_device_t;

static
const uart_device_t KNOWN_DEVICES[] = {
  {
    "HC-05 Bluetooth",
    "AT\r\n",
    "OK",
    9600
  },
  {
    "HC-05 Bluetooth",
    "AT\r\n",
    "OK",
    38400
  },
  {
    "HC-06 Bluetooth",
    "AT",
    "OK",
    9600
  },
  {
    "ESP8266 WiFi",
    "AT\r\n",
    "OK",
    115200
  },
  {
    "ESP8266 WiFi",
    "AT\r\n",
    "OK",
    9600
  },
  {
    "SIM800 GSM",
    "AT\r\n",
    "OK",
    9600
  },
  {
    "GPS NMEA",
    NULL,
    "$GP",
    9600
  },
  {
    "GPS NMEA",
    NULL,
    "$GP",
    4800
  },
  {
    "Arduino",
    NULL,
    NULL,
    9600
  },
};
static
const int DEVICE_COUNT = (int)(sizeof(KNOWN_DEVICES) / sizeof(KNOWN_DEVICES[0]));

static uart_inst_t * pin_to_uart(uint8_t rx_pin) {

  switch (rx_pin) {
  case 1:
  case 13:
  case 17:
  case 29:
    return uart0;
  case 5:
  case 9:
  case 21:
  case 25:
    return uart1;
  default:
    return NULL;
  }
}

static uint32_t estimate_baud_from_pulses(uint8_t pin, uint32_t timeout_ms) {
  gpio_init(pin);
  gpio_set_dir(pin, GPIO_IN);
  gpio_pull_up(pin);

  int initial = gpio_get(pin);
  uint32_t start = to_ms_since_boot(get_absolute_time());

  while (gpio_get(pin) == initial) {
    if ((to_ms_since_boot(get_absolute_time()) - start) > timeout_ms) return 0;
    sleep_us(10);
  }

  uint32_t widths[64];
  int wcount = 0;
  int last = gpio_get(pin);
  uint64_t t_last = time_us_64();

  uint32_t measure_end = to_ms_since_boot(get_absolute_time()) + 50;
  while (to_ms_since_boot(get_absolute_time()) < measure_end && wcount < 64) {
    int cur = gpio_get(pin);
    if (cur != last) {
      uint64_t now = time_us_64();
      uint32_t width = (uint32_t)(now - t_last);
      if (width > 1 && width < 100000) widths[wcount++] = width;
      t_last = now;
      last = cur;
    }
    sleep_us(1);
  }

  if (wcount == 0) return 0;

  uint32_t min_width = 0xFFFFFFFF;
  for (int i = 0; i < wcount; i++)
    if (widths[i] < min_width) min_width = widths[i];

  if (min_width == 0) return 0;
  uint32_t raw_baud = 1000000 / min_width;

  uint32_t best = COMMON_BAUDS[0];
  uint32_t best_err = (raw_baud > best) ? raw_baud - best : best - raw_baud;
  for (int i = 1; i < BAUD_COUNT; i++) {
    uint32_t err = (raw_baud > COMMON_BAUDS[i]) ?
      raw_baud - COMMON_BAUDS[i] : COMMON_BAUDS[i] - raw_baud;
    if (err < best_err) {
      best_err = err;
      best = COMMON_BAUDS[i];
    }
  }
  return best;
}

static
const char * probe_device(uart_inst_t * port, uint8_t tx_pin, uint8_t rx_pin,
  uint32_t baud) {
  uart_init(port, baud);
  gpio_set_function(tx_pin, GPIO_FUNC_UART);
  gpio_set_function(rx_pin, GPIO_FUNC_UART);
  uart_set_format(port, 8, 1, UART_PARITY_NONE);
  uart_set_fifo_enabled(port, false);

  while (uart_is_readable(port)) uart_getc(port);

  static char resp[128];
  const char * matched = NULL;

  for (int d = 0; d < DEVICE_COUNT && !matched; d++) {
    const uart_device_t * dev = & KNOWN_DEVICES[d];
    if (dev -> baud != baud) continue;
    if (!dev -> response) continue;

    if (dev -> probe) {
      uart_puts(port, dev -> probe);
    }

    int pos = 0;
    uint32_t t0 = to_ms_since_boot(get_absolute_time());
    while ((to_ms_since_boot(get_absolute_time()) - t0) < 800 &&
      pos < (int) sizeof(resp) - 1) {
      if (uart_is_readable(port)) {
        resp[pos++] = uart_getc(port);
      }
      sleep_us(200);
    }
    resp[pos] = '\0';

    if (pos > 0 && strstr(resp, dev -> response)) {
      matched = dev -> name;
    }
  }

  if (!matched) {
    int pos = 0;
    uint32_t t0 = to_ms_since_boot(get_absolute_time());
    while ((to_ms_since_boot(get_absolute_time()) - t0) < 1200 &&
      pos < (int) sizeof(resp) - 1) {
      if (uart_is_readable(port)) {
        resp[pos++] = uart_getc(port);
        resp[pos] = '\0';
        if (strstr(resp, "$GP") || strstr(resp, "$GN")) {
          matched = "GPS (NMEA)";
          break;
        }
      }
      sleep_us(200);
    }
  }

  uart_deinit(port);
  gpio_set_function(tx_pin, GPIO_FUNC_SIO);
  gpio_set_function(rx_pin, GPIO_FUNC_SIO);

  return matched;
}

void uart_detect_run(uint8_t rx_pin, uint32_t timeout_ms) {
  printf("=== UART auto-detect on GP%d ===\n", rx_pin);
  printf("  step 1: measuring pulse widths to estimate baud...\n");

  uint32_t t = timeout_ms ? timeout_ms : 3000;
  uint32_t baud = estimate_baud_from_pulses(rx_pin, t);

  if (baud == 0) {
    printf("  no activity detected on GP%d within %lu ms\n", rx_pin, t);
    printf("  tips: check wiring, ensure remote device is transmitting\n");
    return;
  }

  printf("  estimated baud : ~%lu\n", baud);

  gpio_init(rx_pin);
  gpio_set_dir(rx_pin, GPIO_IN);
  gpio_pull_up(rx_pin);
  sleep_ms(5);
  int idle = gpio_get(rx_pin);
  printf("  RX idle state  : %s (%s for UART)\n",
    idle ? "HIGH" : "LOW",
    idle ? "normal" : "inverted/RS232?");

  uint8_t tx_pin = (rx_pin % 2 == 1) ? rx_pin - 1 : rx_pin + 1;
  uart_inst_t * port = pin_to_uart(rx_pin);

  printf("  step 2: probing for known devices at %lu baud...\n", baud);

  const char * device = NULL;
  if (port) {
    device = probe_device(port, tx_pin, rx_pin, baud);
  } else {
    printf("  (GP%d is not a standard UART RX pin - skipping AT probe)\n", rx_pin);
  }

  printf("\n  RESULT:\n");
  printf("  ┌──────────────────────────────┐\n");
  printf("  │ pin      : GP%d              \n", rx_pin);
  printf("  │ baud     : %lu              \n", baud);
  printf("  │ idle     : %s              \n", idle ? "HIGH" : "LOW");
  printf("  │ device   : %s              \n", device ? device : "unknown");
  if (port)
    printf("  │ uart     : UART%d (TX=GP%d RX=GP%d)\n",
      port == uart0 ? 0 : 1, tx_pin, rx_pin);
  printf("  └──────────────────────────────┘\n\n");

  if (device) {
    printf("  Suggestion: uart %lu %d %d\n", baud, tx_pin, rx_pin);
  }
}

void la_detect_protocol(uint8_t pin, int samples, int us_per_sample) {
  if (samples < 16 || samples > 512) samples = 256;
  if (us_per_sample < 1 || us_per_sample > 10000) us_per_sample = 5;

  printf("=== LA protocol detect on GP%d ===\n", pin);
  printf("  %d samples @ %d us/sample  (%.2f ms window)\n",
    samples, us_per_sample,
    (float)(samples * us_per_sample) / 1000.0f);
  printf("  sampling");

  uint8_t * buf = (uint8_t * ) malloc((size_t) samples);
  if (!buf) {
    printf("\n  out of memory\n");
    return;
  }

  gpio_init(pin);
  gpio_set_dir(pin, GPIO_IN);
  gpio_pull_up(pin);

  for (int i = 0; i < samples; i++) {
    buf[i] = (uint8_t) gpio_get(pin);
    if (i % 32 == 0) printf(".");
    sleep_us((uint32_t) us_per_sample);
  }
  printf(" done.\n\n");

  int edges = 0, highs = 0;
  uint32_t widths[256];
  int wcount = 0;
  int run = 1;
  int prev = buf[0];

  for (int i = 1; i < samples; i++) {
    if (buf[i] == prev) {
      run++;
    } else {
      edges++;
      if (wcount < 256) widths[wcount++] = (uint32_t) run;
      run = 1;
      prev = buf[i];
    }
    if (buf[i]) highs++;
  }
  if (run > 0 && wcount < 256) widths[wcount++] = (uint32_t) run;

  float duty = (float) highs / (float) samples * 100.0f;
  float window_ms = (float)(samples * us_per_sample) / 1000.0f;

  uint32_t min_w = 0xFFFFFFFF;
  uint32_t max_w = 0;
  for (int i = 0; i < wcount; i++) {
    if (widths[i] < min_w) min_w = widths[i];
    if (widths[i] > max_w) max_w = widths[i];
  }

  uint32_t min_us = min_w * (uint32_t) us_per_sample;
  uint32_t max_us = max_w * (uint32_t) us_per_sample;

  printf("  edges      : %d\n", edges);
  printf("  duty cycle : %.1f%%\n", duty);
  printf("  min pulse  : %lu us\n", min_us);
  printf("  max pulse  : %lu us\n", max_us);
  printf("  window     : %.2f ms\n", window_ms);

  printf("\n  PROTOCOL ANALYSIS:\n");

  if (edges == 0) {
    printf("  → no transitions: line is %s (stuck %s)\n",
      buf[0] ? "HIGH" : "LOW",
      buf[0] ? "(idle UART or NC)" : "(stuck low – check GND)");
    free(buf);
    return;
  }

  if (min_us >= 5) {
    uint32_t raw_baud = 1000000 / min_us;
    uint32_t snapped = 0;
    uint32_t best_err = 0xFFFFFFFF;
    for (int i = 0; i < BAUD_COUNT; i++) {
      uint32_t err = (raw_baud > COMMON_BAUDS[i]) ?
        raw_baud - COMMON_BAUDS[i] :
        COMMON_BAUDS[i] - raw_baud;
      if (err < best_err) {
        best_err = err;
        snapped = COMMON_BAUDS[i];
      }
    }
    float pct_err = (float) best_err / (float) snapped * 100.0f;
    if (pct_err < 10.0f) {
      printf("  → likely UART\n");
      printf("     estimated baud : ~%lu (%.1f%% from std %lu)\n",
        raw_baud, pct_err, snapped);
      printf("     idle state     : %s\n", buf[0] ? "HIGH (normal)" : "LOW (inverted)");
      printf("     tip: uart %lu <tx_pin> %d\n", snapped, pin);
    }
  }

  if (min_us <= 10 && max_us >= 500) {
    printf("  → possible I2C (mixed pulse widths, bursts)\n");
    printf("     If SCL pin: ~%.0f kHz clock\n",
      min_us > 0 ? 1000.0f / (2.0f * (float) min_us) : 0.0f);
    printf("     tip: i2c scan (SDA=GP4 SCL=GP5)\n");
  }

  if (edges > 20 && duty > 40.0f && duty < 60.0f && min_us < 50) {
    printf("  → possible SPI clock line\n");
    printf("     tip: spi init, then spi xfer\n");
  }

  if (edges < 10 && min_us > 500) {
    if (min_us >= 500 && max_us <= 2500) {
      printf("  → likely servo PWM (pulse %lu-%lu us)\n", min_us, max_us);
      printf("     tip: servo <pin> <angle>\n");
    } else {
      printf("  → likely PWM signal  (~%.0f Hz, %.1f%% duty)\n",
        edges > 1 ? 1000.0f / window_ms * (edges / 2.0f) : 0.0f,
        duty);
    }
  }

  if (edges >= 2) {
    float freq = (float) edges / 2.0f / (window_ms / 1000.0f);
    printf("  → signal frequency : ~%.1f Hz  (from %d edges)\n", freq, edges);
  }

  free(buf);
}