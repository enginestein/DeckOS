#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include "pico/stdio/driver.h"
#include <ctype.h>
#include "pico/stdlib.h"
#include "hardware/uart.h"
#include "hardware/gpio.h"
#include "hardware/clocks.h"
#include "hardware/adc.h"
#include "bt.h"
#include "commands.h"
#include "syslog.h"
#include "scheduler.h"
#include "vfs.h"

// ── Internal state ────────────────────────────────────────────────────────────
static bool     s_ready       = false;
static bool     s_log_mirror  = false;
static uint32_t s_current_baud = BT_DEFAULT_BAUD;

// Tiny ring buffer for BT RX so bt_getchar() stays non-blocking
#define RING_SIZE 512
static volatile char   s_ring[RING_SIZE];
static volatile int    s_ring_head = 0;
static volatile int    s_ring_tail = 0;

static inline void ring_push(char c) {
    int next = (s_ring_head + 1) % RING_SIZE;
    if (next != s_ring_tail) {
        s_ring[s_ring_head] = c;
        s_ring_head = next;
    }
}

static inline int ring_pop(void) {
    if (s_ring_tail == s_ring_head) return -1;
    char c = s_ring[s_ring_tail];
    s_ring_tail = (s_ring_tail + 1) % RING_SIZE;
    return (unsigned char)c;
}

static inline bool ring_empty(void) {
    return s_ring_head == s_ring_tail;
}

// ── UART helpers ──────────────────────────────────────────────────────────────
static void bt_uart_init(uint32_t baud) {
    // Add these debug prints BEFORE init
    printf("[bt] claiming UART%d TX=GP%d RX=GP%d at %lu baud\n",
           BT_UART == uart0 ? 0 : 1, BT_TX_PIN, BT_RX_PIN, baud);

    uart_init(BT_UART, baud);
    gpio_set_function(BT_TX_PIN, GPIO_FUNC_UART);
    gpio_set_function(BT_RX_PIN, GPIO_FUNC_UART);
    uart_set_format(BT_UART, 8, 1, UART_PARITY_NONE);
    uart_set_fifo_enabled(BT_UART, true);
    s_current_baud = baud;

    // Verify the functions actually took
    printf("[bt] GP%d function = %lu (expected %d for UART)\n",
           BT_RX_PIN, gpio_get_function(BT_RX_PIN), GPIO_FUNC_UART);
    printf("[bt] GP%d function = %lu (expected %d for UART)\n",
           BT_TX_PIN, gpio_get_function(BT_TX_PIN), GPIO_FUNC_UART);
}

static void bt_uart_deinit(void) {
    uart_deinit(BT_UART);
    gpio_set_function(BT_TX_PIN, GPIO_FUNC_SIO);
    gpio_set_function(BT_RX_PIN, GPIO_FUNC_SIO);
}

// Drain any pending RX bytes into our ring buffer (call frequently)
static void bt_drain(void) {
    while (uart_is_readable(BT_UART)) {
        char c = uart_getc(BT_UART);
        ring_push(c);
    }
}

// ── Public init ───────────────────────────────────────────────────────────────
void bt_init(uint32_t baud) {
    bt_uart_init(baud ? baud : BT_DEFAULT_BAUD);

    if (BT_STATE_PIN != 0xFF) {
        gpio_init(BT_STATE_PIN);
        gpio_set_dir(BT_STATE_PIN, GPIO_IN);
        gpio_pull_down(BT_STATE_PIN);
    }

    s_ready      = true;
    s_log_mirror = false;

    LOG_I("bt", "HC-05 UART ready");
    printf("[bt] HC-05 on UART%d  TX=GP%d RX=GP%d  %lu baud\n",
           BT_UART == uart0 ? 0 : 1,
           BT_TX_PIN, BT_RX_PIN, baud ? baud : BT_DEFAULT_BAUD);
}

bool bt_is_ready(void) { return s_ready; }

bool bt_is_connected(void) {
    if (BT_STATE_PIN == 0xFF) return true;  // assume connected
    return gpio_get(BT_STATE_PIN);
}

// ── Output ─────────────────────────────────────────────────────────────────
void bt_puts(const char* s) {
    if (!s_ready || !s) return;
    while (*s) {
        uart_putc_raw(BT_UART, *s);
        s++;
    }
}

void bt_printf(const char* fmt, ...) {
    if (!s_ready) return;
    char buf[256];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    bt_puts(buf);
}

// ── Input ──────────────────────────────────────────────────────────────────
int bt_getchar(void) {
    bt_drain();
    return ring_pop();
}

// Blocking read with timeout (ms). Returns -1 on timeout.
static int bt_getchar_timeout(uint32_t timeout_ms) {
    uint32_t start = to_ms_since_boot(get_absolute_time());
    while (true) {
        bt_drain();
        int c = ring_pop();
        if (c >= 0) return c;
        if ((to_ms_since_boot(get_absolute_time()) - start) >= timeout_ms) return -1;
        sleep_us(100);
    }
}

// Read a line from BT into buf (strips \r\n). Returns length or -1 on timeout.
static int bt_readline(char* buf, int buflen, uint32_t timeout_ms) {
    int pos = 0;
    uint32_t start = to_ms_since_boot(get_absolute_time());
    while (pos < buflen - 1) {
        int c = -1;
        // Poll with 1ms granularity
        while (c < 0) {
            bt_drain();
            c = ring_pop();
            if (c < 0) {
                if ((to_ms_since_boot(get_absolute_time()) - start) >= timeout_ms)
                    return -1;
                sleep_us(500);
            }
        }
        if (c == '\r') continue;
        if (c == '\n') break;
        buf[pos++] = (char)c;
    }
    buf[pos] = '\0';
    return pos;
}

// ── Log mirroring ─────────────────────────────────────────────────────────────
void bt_log_enable(bool on) {
    s_log_mirror = on;
    bt_printf("[bt] log mirror %s\r\n", on ? "ON" : "OFF");
}

bool bt_log_is_enabled(void) { return s_log_mirror; }

void bt_log_mirror(const char* level, const char* tag, const char* msg,
                   uint32_t ts_ms) {
    if (!s_ready || !s_log_mirror) return;
    bt_printf("[%4lu.%03lu][%s][%s] %s\r\n",
              ts_ms / 1000, ts_ms % 1000, level, tag, msg);
}

// ── Output redirection for bt_exec ───────────────────────────────────────────
// We intercept putchar_raw / fwrite so commands_execute() output goes to BT.
// Simple approach: swap stdout file pointer using a flag checked at putchar level.
// Since Pico SDK uses __iob[] / custom stdio, the cleanest portable method
// is to wrap at the bt_exec level by temporarily hooking stdio_set_driver.
// For simplicity we use a "bt redirect" flag and provide bt_putchar hook via
// the Pico SDK custom stdio driver.

#include "pico/stdio.h"

static bool s_redirecting = false;



static void bt_stdio_out_chars(const char* buf, int len) {
    for (int i = 0; i < len; i++)
        uart_putc_raw(BT_UART, buf[i]);
}

static int bt_stdio_in_chars(char* buf, int len) {
    bt_drain();
    int got = 0;
    while (got < len) {
        int c = ring_pop();
        if (c < 0) break;
        buf[got++] = (char)c;
    }
    return got ? got : PICO_ERROR_NO_DATA;
}

static stdio_driver_t bt_stdio_driver = {
    .out_chars = bt_stdio_out_chars,
    .in_chars  = bt_stdio_in_chars,
};

// ── BT Shell ─────────────────────────────────────────────────────────────────
void bt_shell_run(void) {
    if (!s_ready) { printf("bt: not initialised\n"); return; }

    printf("[bt] shell started\n");
    printf("[bt] type 'exit' in BT terminal to stop\n");

    bt_puts("\r\n");
    bt_puts("================================\r\n");
    bt_puts("  DeckOS BT Shell  \r\n");
    bt_puts("  type 'help' for commands\r\n");
    bt_puts("  type 'exit' to disconnect\r\n");
    bt_puts("================================\r\n");
    bt_puts("> ");

    static char line[BT_CMD_BUF];
    int pos = 0;
    memset(line, 0, sizeof(line));

    while (true) {
        bt_drain();
        int c = ring_pop();

        if (c < 0) {
            sleep_us(500);
            continue;
        }

        // handle both \r and \n as line endings
        if (c == '\r' || c == '\n') {
            // drain a trailing \n if we got \r
            if (c == '\r') {
                sleep_us(2000);   // brief wait for \n to arrive
                bt_drain();
                int next = ring_pop();
                // if it's \n just discard it, otherwise it stays lost
                // (acceptable — we already have the line)
                (void)next;
            }

            bt_puts("\r\n");
            line[pos] = '\0';

            // trim leading spaces
            char* cmd = line;
            while (*cmd == ' ') cmd++;

            // trim trailing spaces
            int len = (int)strlen(cmd);
            while (len > 0 && cmd[len - 1] == ' ') cmd[--len] = '\0';

            if (len == 0) {
                bt_puts("> ");
                pos = 0;
                memset(line, 0, sizeof(line));
                continue;
            }

            // check exit before executing
            if (strcmp(cmd, "exit") == 0 || strcmp(cmd, "quit") == 0) {
                bt_puts("Goodbye!\r\n");
                printf("[bt] shell: exit command received\n");
                break;
            }

            // redirect printf output to BT for the duration of this command
            stdio_set_driver_enabled(&bt_stdio_driver, true);
            commands_execute(cmd);
            stdio_set_driver_enabled(&bt_stdio_driver, false);

            bt_puts("\r\n> ");
            pos = 0;
            memset(line, 0, sizeof(line));
            continue;
        }

        // Ctrl-C
        if (c == 3) {
            bt_puts("^C\r\n> ");
            pos = 0;
            memset(line, 0, sizeof(line));
            continue;
        }

        // backspace
        if (c == 127 || c == '\b') {
            if (pos > 0) {
                pos--;
                line[pos] = '\0';
                bt_puts("\b \b");
            }
            continue;
        }

        // printable character
        if (c >= 32 && c < 127 && pos < BT_CMD_BUF - 1) {
            line[pos++] = (char)c;
            char echo[2] = {(char)c, '\0'};
            bt_puts(echo);
        }
    }

    printf("[bt] shell ended\n");
}
// ── Remote exec ───────────────────────────────────────────────────────────────
void bt_exec(char* cmdline) {
    if (!s_ready || !cmdline || !*cmdline) return;
    stdio_set_driver_enabled(&bt_stdio_driver, true);
    commands_execute(cmdline);
    stdio_set_driver_enabled(&bt_stdio_driver, false);
}

// ── Top streaming ─────────────────────────────────────────────────────────────
void bt_top_stream(uint32_t interval_ms) {
    if (!s_ready) { printf("bt: not ready\n"); return; }
    if (interval_ms < 100) interval_ms = 500;

    printf("[bt] streaming top to BT — press any key (USB) to stop\n");
    bt_puts("[bt-top] streaming... type 'stop' to end\r\n");

    char stop_buf[8];
    int  sbuf_pos = 0;
    memset(stop_buf, 0, sizeof(stop_buf));

    while (true) {
        // Check USB keyboard to stop
        int uc = getchar_timeout_us(0);
        if (uc != PICO_ERROR_TIMEOUT) break;

        // Check BT keyboard for "stop"
        bt_drain();
        int bc = ring_pop();
        if (bc >= 0) {
            if (sbuf_pos < 7) stop_buf[sbuf_pos++] = (char)bc;
            if (strstr(stop_buf, "stop") || bc == 3) break;
        }

        // Build stats string
        uint64_t up_us = time_us_64();
        uint32_t up_s  = (uint32_t)(up_us / 1000000);
        adc_select_input(4);
        float v  = adc_read() * 3.3f / (1 << 12);
        float tc = 27.0f - (v - 0.706f) / 0.001721f;
        uint32_t cpu_mhz = clock_get_hz(clk_sys) / 1000000;

        bt_printf("\r\n=== DeckOS top (%02u:%02u:%02u) ===\r\n",
                  up_s / 3600, (up_s % 3600) / 60, up_s % 60);
        bt_printf("cpu: %lu MHz   temp: %.1f C\r\n", cpu_mhz, tc);

        sched_task_t snap[SCHED_MAX_TASKS];
        uint64_t     totals[SCHED_MAX_TASKS];
        int          n     = sched_snapshot(snap, totals, SCHED_MAX_TASKS);
        uint64_t     grand = sched_core1_total_us();
        if (grand == 0) grand = 1;

        bt_printf("TASK            STATE    INTERVAL  CPU%%\r\n");
        bt_printf("--------------- -------- --------- ------\r\n");
        bt_printf("%-15s %-8s %5s ms   --\r\n", "shell", "running", "-");
        for (int i = 0; i < n; i++) {
            uint32_t pct = (uint32_t)((totals[i] * 1000) / grand);
            bt_printf("%-15s %-8s %5lu ms  %2lu.%lu%%\r\n",
                      snap[i].name,
                      snap[i].enabled ? "active" : "sleep",
                      snap[i].interval_ms,
                      pct / 10, pct % 10);
        }
        bt_printf("(send 'stop' to end)\r\n");

        sleep_ms(interval_ms);
    }
    bt_puts("[bt-top] stopped\r\n");
    printf("[bt] top stream ended\n");
}

// ── File transfer ─────────────────────────────────────────────────────────────
#define XFER_TIMEOUT_MS 5000

void bt_send_file(const char* vfs_path) {
    if (!s_ready) { printf("bt: not ready\n"); return; }

    // Read file from VFS
    uint8_t  buf[VFS_MAX_FILE_SIZE];
    uint32_t flen = 0;
    int rc = vfs_read(vfs_path, buf, sizeof(buf), &flen);
    if (rc < 0) { printf("bt: file not found: %s\n", vfs_path); return; }

    printf("[bt] sending '%s' (%lu B) over BT...\n", vfs_path, flen);
    bt_printf("<<FILE:%s:%lu>>\r\n", vfs_path, flen);

    // Send in 64-byte chunks with a small delay to avoid overrunning HC-05 buffer
    uint32_t sent = 0;
    while (sent < flen) {
        uint32_t chunk = flen - sent;
        if (chunk > 64) chunk = 64;
        for (uint32_t i = 0; i < chunk; i++)
            uart_putc_raw(BT_UART, buf[sent + i]);
        sent += chunk;
        sleep_ms(10);
    }
    bt_puts("\r\n<<EOF>>\r\n");
    printf("[bt] sent %lu bytes\n", flen);
}

void bt_recv_file(const char* vfs_path) {
    if (!s_ready) { printf("bt: not ready\n"); return; }

    bt_printf("<<RECV:%s>>\r\n", vfs_path);
    printf("[bt] waiting for file data on BT (timeout %ds)...\n", XFER_TIMEOUT_MS / 1000);
    printf("     Send file followed by <<EOF>>\n");

    static uint8_t data[VFS_MAX_FILE_SIZE];
    int   dlen = 0;
    bool  done = false;
    char  eof_buf[8];
    int   eof_pos = 0;

    uint32_t start = to_ms_since_boot(get_absolute_time());
    while (!done) {
        bt_drain();
        int c = ring_pop();
        if (c < 0) {
            if ((to_ms_since_boot(get_absolute_time()) - start) >= XFER_TIMEOUT_MS) {
                printf("[bt] recv timeout\n");
                bt_puts("<<TIMEOUT>>\r\n");
                return;
            }
            sleep_us(200);
            continue;
        }

        // Slide EOF detector
        if (eof_pos < 7) eof_buf[eof_pos++] = (char)c;
        else {
            memmove(eof_buf, eof_buf + 1, 6);
            eof_buf[6] = (char)c;
        }
        eof_buf[7] = '\0';
        if (strstr(eof_buf, "<<EOF>>")) { done = true; break; }

        if (dlen < VFS_MAX_FILE_SIZE) data[dlen++] = (uint8_t)c;
    }

    if (done) {
        // Trim trailing <<EOF>> pattern from data
        if (dlen >= 7) dlen -= 7;
        int n = vfs_write(vfs_path, data, (uint32_t)dlen, false);
        if (n >= 0) {
            printf("[bt] received %d bytes -> '%s'\n", n, vfs_path);
            bt_printf("<<OK:%d>>\r\n", n);
        } else {
            bt_puts("<<ERR>>\r\n");
        }
    }
}

// ── Packet sniffer ────────────────────────────────────────────────────────────
void bt_sniff(uint32_t timeout_ms) {
    if (!s_ready) { printf("bt: not ready\n"); return; }

    printf("[bt] packet sniffer — RX on GP%d  (any key to stop)\n", BT_RX_PIN);
    printf("     OFFSET    HEX                                  ASCII\n");
    printf("     ------    ----------------------------------   --------\n");

    uint8_t  row[16];
    int      row_pos = 0;
    uint32_t offset  = 0;
    uint32_t last_ms = to_ms_since_boot(get_absolute_time());
    bool     first   = true;

    uint32_t start = to_ms_since_boot(get_absolute_time());

    while (true) {
        if (getchar_timeout_us(0) != PICO_ERROR_TIMEOUT) {
            printf("\n[bt] sniffer stopped\n");
            break;
        }

        bt_drain();
        int c = ring_pop();
        if (c < 0) {
            uint32_t now = to_ms_since_boot(get_absolute_time());
            // Flush partial row after 100ms idle
            if (!first && row_pos > 0 && (now - last_ms) > 100) {
                printf("     %06lu    ", offset - row_pos);
                for (int i = 0; i < row_pos; i++) printf("%02X ", row[i]);
                for (int i = row_pos; i < 16; i++) printf("   ");
                printf("  |");
                for (int i = 0; i < row_pos; i++)
                    putchar(row[i] >= 32 && row[i] < 127 ? row[i] : '.');
                printf("|\n");
                row_pos = 0;
            }
            if (timeout_ms && (now - start) >= timeout_ms) {
                printf("[bt] sniffer timeout\n");
                break;
            }
            sleep_us(200);
            continue;
        }

        first   = false;
        last_ms = to_ms_since_boot(get_absolute_time());

        // Print timestamp on first byte of burst
        if (row_pos == 0) {
            uint32_t ms = to_ms_since_boot(get_absolute_time());
            printf("  T+%lu ms\n", ms);
        }

        row[row_pos++] = (uint8_t)c;
        offset++;

        if (row_pos == 16) {
            printf("     %06lu    ", offset - 16);
            for (int i = 0; i < 16; i++) printf("%02X ", row[i]);
            printf("  |");
            for (int i = 0; i < 16; i++)
                putchar(row[i] >= 32 && row[i] < 127 ? row[i] : '.');
            printf("|\n");
            row_pos = 0;
        }
    }
}

// ── AT mode ───────────────────────────────────────────────────────────────────
bool bt_at_cmd(const char* cmd, char* resp_buf, int buf_len, uint32_t timeout_ms) {
    // Flush RX
    while (uart_is_readable(BT_UART)) uart_getc(BT_UART);
    s_ring_head = s_ring_tail = 0;

    // Send command + \r\n
    bt_puts(cmd);
    bt_puts("\r\n");

    // Read response
    int pos = 0;
    uint32_t start = to_ms_since_boot(get_absolute_time());
    while (pos < buf_len - 1) {
        bt_drain();
        int c = ring_pop();
        if (c >= 0) {
            resp_buf[pos++] = (char)c;
            // HC-05 responses end with \r\n
            if (pos >= 2 &&
                resp_buf[pos-2] == '\r' && resp_buf[pos-1] == '\n') break;
        }
        if ((to_ms_since_boot(get_absolute_time()) - start) >= timeout_ms) break;
        sleep_us(200);
    }
    resp_buf[pos] = '\0';
    return (pos > 0);
}

void bt_at_mode(void) {
    if (!s_ready) { printf("bt: not initialised\n"); return; }

    printf("[bt] entering AT mode at %d baud\n", BT_AT_BAUD);
    printf("     Put HC-05 into AT mode (hold KEY/EN pin high at power-on)\n");
    printf("     Commands are sent to module; type EXIT to leave\n");
    printf("     Common commands: AT  AT+NAME?  AT+PSWD?  AT+UART?\n\n");

    // Reinit at AT baud
    bt_uart_deinit();
    bt_uart_init(BT_AT_BAUD);

    char line[BT_CMD_BUF];
    int  pos = 0;

    // Flush
    while (uart_is_readable(BT_UART)) uart_getc(BT_UART);

    printf("at> ");
    while (true) {
        // USB -> HC-05
        int c = getchar_timeout_us(0);
        if (c != PICO_ERROR_TIMEOUT) {
            if (c == '\r' || c == '\n') {
                printf("\n");
                line[pos] = '\0';
                if (strcasecmp(line, "EXIT") == 0) break;

                if (pos > 0) {
                    char resp[256] = {0};
                    bool ok = bt_at_cmd(line, resp, sizeof(resp), 1500);
                    // Print raw response
                    if (ok) {
                        printf("  <- ");
                        for (int i = 0; resp[i]; i++) {
                            if (resp[i] == '\r') continue;
                            if (resp[i] == '\n') { printf("\n     "); continue; }
                            putchar(resp[i]);
                        }
                        printf("\n");
                    } else {
                        printf("  (no response / timeout)\n");
                    }
                }
                pos = 0;
                memset(line, 0, sizeof(line));
                printf("at> ");

            } else if (c == 127 || c == '\b') {
                if (pos > 0) { pos--; printf("\b \b"); }
            } else if (pos < BT_CMD_BUF - 1) {
                line[pos++] = (char)c;
                putchar(c);
            }
        }

        // HC-05 -> USB (unsolicited output)
        bt_drain();
        int bc = ring_pop();
        if (bc >= 0 && bc != '\r') putchar(bc);

        sleep_us(200);
    }

    // Restore normal baud
    bt_uart_deinit();
    bt_uart_init(s_current_baud);
    printf("\n[bt] AT mode exited — restored %lu baud\n", s_current_baud);
}

// ── UART auto-detect helper (used by detect_uart command) ────────────────────
// This is exposed via commands.c — see cmd_detect_uart in commands.c