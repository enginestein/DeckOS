/**
 * commands.c  —  DeckOS command table
 *
 * NEW commands added:
 *   watch   <ms> <cmd>      repeatedly run a command at an interval
 *   morse   <text>          blink LED in morse code
 *   memmap                  detailed memory layout
 *   gpio irq <pin>          monitor GPIO for IRQ edges
 *   syslog  [options]       in-memory ring log
 *   tone    <pin> <note> [ms]  play a tone on a buzzer
 *   melody  <pin> <seq>     play a melody (e.g. "C4:200 E4:200 G4:400")
 *   avg     <ch> [n]        ADC averaging (noise reduction)
 *   trigger <pin> <edge> <cmd> run cmd when GPIO edge fires (one-shot)
 *   cron    <ms> <cmd>      run cmd once after N ms
 *   stats                   runtime statistics
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include "pico/stdlib.h"
#include "pico/time.h"
#include "hardware/adc.h"
#include "hardware/gpio.h"
#include "hardware/watchdog.h"
#include "hardware/pwm.h"
#include "hardware/clocks.h"
#include "hardware/i2c.h"
#include "pico/bootrom.h"
#include "pico/unique_id.h"
#include "commands.h"
#include "kernel.h"
#include "drivers.h"
#include "scheduler.h"
#include "config.h"
#include "syslog.h"
#include "gpio_mon.h"
#include "morse.h"
#include "tone.h"

#define LED_PIN 25

extern flash_config_t g_config;

static uint32_t s_cmd_count   = 0;
static uint32_t s_unknown_count = 0;
static uint64_t s_boot_us;    // set in commands_init

static void print_uptime(void) {
    uint64_t us = time_us_64();
    uint32_t s  = (uint32_t)(us / 1000000);
    printf("%02uh %02um %02us", s / 3600, (s % 3600) / 60, s % 60);
}


static void cmd_help(int argc, char* argv[]) {
    commands_list();
}

static void cmd_version(int argc, char* argv[]) {
    printf("DeckOS v1.1.0  |  Raspberry Pi Pico\n");
    printf("Build: %s %s\n", __DATE__, __TIME__);
}

static void cmd_clear(int argc, char* argv[]) {
    printf("\033[2J\033[H");
}

static void cmd_echo(int argc, char* argv[]) {
    for (int i = 1; i < argc; i++)
        printf("%s%s", argv[i], (i < argc - 1) ? " " : "");
    printf("\n");
}

static void cmd_uptime(int argc, char* argv[]) {
    printf("uptime: ");
    print_uptime();
    printf("\n");
}

static void cmd_temp(int argc, char* argv[]) {
    adc_select_input(4);
    uint16_t raw  = adc_read();
    float voltage = raw * 3.3f / (1 << 12);
    float temp_c  = 27.0f - (voltage - 0.706f) / 0.001721f;
    float temp_f  = temp_c * 9.0f / 5.0f + 32.0f;
    printf("core temp: %.1f C  /  %.1f F\n", temp_c, temp_f);
}

static void cmd_mem(int argc, char* argv[]) {
    extern char __StackLimit, __bss_end__;
    uint32_t heap = (uint32_t)(&__StackLimit - &__bss_end__);
    printf("heap available : ~%lu bytes  (~%lu KB)\n", heap, heap / 1024);
    printf("total SRAM     : 264 KB\n");
    printf("flash          : 2 MB\n");
}

static void cmd_memmap(int argc, char* argv[]) {
    extern char __flash_binary_start, __flash_binary_end;
    extern char __data_start__, __data_end__;
    extern char __bss_start__, __bss_end__;
    extern char __StackLimit, __StackTop;
    extern char __end__;   // end of heap region (heap starts here)

    printf("=== RP2040 Memory Map ===\n");
    printf("\n[ FLASH  0x10000000 - 0x10200000  (2 MB) ]\n");
    printf("  .flash_binary_start : 0x%08lX\n", (uint32_t)&__flash_binary_start);
    printf("  .flash_binary_end   : 0x%08lX   (%lu KB used)\n",
           (uint32_t)&__flash_binary_end,
           ((uint32_t)&__flash_binary_end - (uint32_t)&__flash_binary_start) / 1024);
    printf("  config sector       : 0x%08lX   (last 4 KB)\n",
           (uint32_t)(0x10200000 - 4096));

    printf("\n[ SRAM   0x20000000 - 0x20042000  (264 KB) ]\n");
    printf("  .data start         : 0x%08lX\n", (uint32_t)&__data_start__);
    printf("  .data end           : 0x%08lX   (%lu B)\n",
           (uint32_t)&__data_end__,
           (uint32_t)&__data_end__ - (uint32_t)&__data_start__);
    printf("  .bss  start         : 0x%08lX\n", (uint32_t)&__bss_start__);
    printf("  .bss  end           : 0x%08lX   (%lu B)\n",
           (uint32_t)&__bss_end__,
           (uint32_t)&__bss_end__ - (uint32_t)&__bss_start__);
    printf("  heap  start         : 0x%08lX\n", (uint32_t)&__end__);
    printf("  heap  end (limit)   : 0x%08lX\n", (uint32_t)&__StackLimit);
    printf("  heap  size          : %lu KB\n",
           ((uint32_t)&__StackLimit - (uint32_t)&__end__) / 1024);
    printf("  stack top           : 0x%08lX\n", (uint32_t)&__StackTop);
    printf("  stack size          : %lu B (estimated)\n",
           (uint32_t)&__StackTop - (uint32_t)&__StackLimit);

    printf("\n[ CORE1 SRAM  0x20040000 - 0x20042000  (8 KB) ]\n");
    printf("  (used by scheduler and core1 stack)\n");

    printf("\n[ PERIPHERAL BASE: 0x40000000 ]\n");
    printf("  I2C0   : 0x40044000\n");
    printf("  I2C1   : 0x40048000\n");
    printf("  SPI0   : 0x4003C000\n");
    printf("  SPI1   : 0x40040000\n");
    printf("  UART0  : 0x40034000\n");
    printf("  PWM    : 0x40050000\n");
    printf("  ADC    : 0x4004C000\n");
    printf("  USB    : 0x50110000\n");
}

static void cmd_uid(int argc, char* argv[]) {
    pico_unique_board_id_t id;
    pico_get_unique_board_id(&id);
    printf("board UID: ");
    for (int i = 0; i < PICO_UNIQUE_BOARD_ID_SIZE_BYTES; i++)
        printf("%02X", id.id[i]);
    printf("\n");
}

static void cmd_wdog(int argc, char* argv[]) {
    printf("last reboot by watchdog: %s\n",
        watchdog_caused_reboot() ? "YES" : "no");
    printf("watchdog hw scratch[0] : 0x%08lX\n",
        watchdog_hw->scratch[0]);
}

static void cmd_pin(int argc, char* argv[]) {
    printf("GPIO state snapshot:\n");
    printf("PIN  DIR  VAL\n");
    for (int i = 0; i <= 28; i++) {
        gpio_init(i);
        int dir = gpio_get_dir(i);
        int val = gpio_get(i);
        printf(" %-3d  %-4s  %d\n", i, dir ? "OUT" : "IN", val);
    }
}

static void cmd_i2c(int argc, char* argv[]) {
    if (argc < 2) {
        printf("usage:\n");
        printf("  i2c scan              - scan bus for devices\n");
        printf("  i2c read  <addr> <reg>       - read one byte\n");
        printf("  i2c write <addr> <reg> <val> - write one byte\n");
        return;
    }

    if (strcmp(argv[1], "scan") == 0) {
        printf("I2C0 scan (SDA=GP4 SCL=GP5):\n");
        printf("     0  1  2  3  4  5  6  7  8  9  A  B  C  D  E  F\n");
        int found = 0;
        for (int addr = 0; addr < 128; addr++) {
            if (addr % 16 == 0) printf("%02X: ", addr);
            uint8_t rxdata;
            int ret = i2c_read_timeout_us(i2c0, (uint8_t)addr,
                                          &rxdata, 1, false, 2000);
            if (ret >= 0) { printf("%02X ", addr); found++; }
            else          { printf("-- "); }
            if ((addr + 1) % 16 == 0) printf("\n");
        }
        printf("%d device(s) found\n", found);

    } else if (strcmp(argv[1], "read") == 0) {
        if (argc < 4) { printf("usage: i2c read <addr_hex> <reg_hex>\n"); return; }
        uint8_t addr = (uint8_t)strtol(argv[2], NULL, 16);
        uint8_t reg  = (uint8_t)strtol(argv[3], NULL, 16);
        uint8_t val  = 0;
        i2c_write_timeout_us(i2c0, addr, &reg, 1, true, 2000);
        int ret = i2c_read_timeout_us(i2c0, addr, &val, 1, false, 2000);
        if (ret < 0) { printf("I2C error (no ACK?)\n"); return; }
        printf("0x%02X reg[0x%02X] = 0x%02X (%d)\n", addr, reg, val, val);

    } else if (strcmp(argv[1], "write") == 0) {
        if (argc < 5) { printf("usage: i2c write <addr_hex> <reg_hex> <val_hex>\n"); return; }
        uint8_t addr = (uint8_t)strtol(argv[2], NULL, 16);
        uint8_t reg  = (uint8_t)strtol(argv[3], NULL, 16);
        uint8_t val  = (uint8_t)strtol(argv[4], NULL, 16);
        uint8_t buf[2] = { reg, val };
        int ret = i2c_write_timeout_us(i2c0, addr, buf, 2, false, 2000);
        if (ret < 0) { printf("I2C write failed\n"); return; }
        printf("wrote 0x%02X -> 0x%02X[0x%02X]\n", val, addr, reg);

    } else {
        printf("unknown i2c subcommand: %s\n", argv[1]);
    }
}

static void cmd_drivers(int argc, char* argv[]) { drivers_list(); }
static void cmd_tasks(int argc, char* argv[]) {
    if (argc >= 3 && strcmp(argv[1], "enable") == 0)  { sched_enable(atoi(argv[2]), true);  printf("task %d enabled\n",  atoi(argv[2])); return; }
    if (argc >= 3 && strcmp(argv[1], "disable") == 0) { sched_enable(atoi(argv[2]), false); printf("task %d disabled\n", atoi(argv[2])); return; }
    sched_list();
}

static void cmd_config(int argc, char* argv[]) {
    if (argc < 2 || strcmp(argv[1], "show") == 0) { config_print(&g_config); return; }
    if (strcmp(argv[1], "save") == 0)  { config_save(&g_config); return; }
    if (strcmp(argv[1], "reset") == 0) { config_defaults(&g_config); config_save(&g_config); printf("config reset to defaults\n"); return; }
    if (strcmp(argv[1], "set") == 0 && argc >= 4) {
        if (strcmp(argv[2], "hostname") == 0) {
            strncpy(g_config.hostname, argv[3], sizeof(g_config.hostname) - 1);
            printf("hostname = %s\n", g_config.hostname);
        } else if (strcmp(argv[2], "cpu_mhz") == 0) {
            int mhz = atoi(argv[3]);
            if (mhz != 0 && (mhz < 48 || mhz > 200)) { printf("safe range: 48-200 MHz\n"); return; }
            g_config.boot_cpu_mhz = (uint32_t)mhz;
            printf("boot_cpu_mhz = %d\n", mhz);
        } else if (strcmp(argv[2], "boot_led") == 0) {
            g_config.boot_led = (uint8_t)(atoi(argv[3]) ? 1 : 0);
            printf("boot_led = %d\n", g_config.boot_led);
        } else {
            printf("unknown key: %s  (valid: hostname cpu_mhz boot_led)\n", argv[2]);
        }
        return;
    }
    printf("usage: config show|set <key> <val>|save|reset\n");
}

static void cmd_dfu(int argc, char* argv[]) {
    printf("entering USB DFU (BOOTSEL) mode...\n");
    sleep_ms(200);
    reset_usb_boot(0, 0);
}

static void cmd_sysinfo(int argc, char* argv[]) {
    printf("=================================\n");
    printf("  DeckOS v1.1.0  —  system info  \n");
    printf("=================================\n");
    printf("board   : Raspberry Pi Pico\n");
    printf("cpu     : RP2040  dual-core Cortex-M0+  125 MHz\n");
    printf("ram     : 264 KB SRAM\n");
    printf("flash   : 2 MB\n");
    printf("uptime  : "); print_uptime(); printf("\n");
    adc_select_input(4);
    uint16_t raw = adc_read();
    float v      = raw * 3.3f / (1 << 12);
    float tc     = 27.0f - (v - 0.706f) / 0.001721f;
    printf("temp    : %.1f C\n", tc);
    printf("cmds    : %lu executed, %lu unknown\n", s_cmd_count, s_unknown_count);
    printf("log     : %lu total entries\n", syslog_total());
    printf("=================================\n");
}

static void cmd_pwm(int argc, char* argv[]) {
    if (argc < 3) { printf("usage: pwm <pin> <duty 0-100>\n"); return; }
    int pin  = atoi(argv[1]);
    int duty = atoi(argv[2]);
    if (pin < 0 || pin > 28)    { printf("invalid pin\n"); return; }
    if (duty < 0 || duty > 100) { printf("duty must be 0-100\n"); return; }
    gpio_set_function(pin, GPIO_FUNC_PWM);
    uint slice = pwm_gpio_to_slice_num(pin);
    pwm_set_wrap(slice, 999);
    pwm_set_gpio_level(pin, duty * 10);
    pwm_set_enabled(slice, true);
    printf("PWM on GPIO%-2d  duty=%d%%\n", pin, duty);
}

static void cmd_clock(int argc, char* argv[]) {
    if (argc < 2) { printf("cpu clock: %lu MHz\n", clock_get_hz(clk_sys) / 1000000); return; }
    int mhz = atoi(argv[1]);
    if (mhz < 48 || mhz > 200) { printf("safe range: 48-200 MHz\n"); return; }
    set_sys_clock_khz(mhz * 1000, false);
    printf("cpu clock set to %d MHz\n", mhz);
}

static void cmd_pull(int argc, char* argv[]) {
    if (argc < 3) { printf("usage: pull <pin> <up|down|none>\n"); return; }
    int pin = atoi(argv[1]);
    if (pin < 0 || pin > 28) { printf("invalid pin\n"); return; }
    gpio_init(pin);
    if      (strcmp(argv[2], "up")   == 0) { gpio_pull_up(pin);       printf("GPIO%d pull-up\n", pin); }
    else if (strcmp(argv[2], "down") == 0) { gpio_pull_down(pin);     printf("GPIO%d pull-down\n", pin); }
    else if (strcmp(argv[2], "none") == 0) { gpio_disable_pulls(pin); printf("GPIO%d pulls disabled\n", pin); }
    else printf("unknown: %s\n", argv[2]);
}

static void cmd_led(int argc, char* argv[]) {
    static bool led_state = false;
    gpio_init(LED_PIN); gpio_set_dir(LED_PIN, GPIO_OUT);
    if (argc < 2) { printf("usage: led <on|off|toggle|blink [n]>\n"); return; }
    if (strcmp(argv[1], "on")     == 0) { led_state = true;  gpio_put(LED_PIN, 1); printf("LED on\n"); }
    else if (strcmp(argv[1], "off")    == 0) { led_state = false; gpio_put(LED_PIN, 0); printf("LED off\n"); }
    else if (strcmp(argv[1], "toggle") == 0) { led_state = !led_state; gpio_put(LED_PIN, led_state); printf("LED %s\n", led_state ? "on" : "off"); }
    else if (strcmp(argv[1], "blink")  == 0) {
        int n = (argc >= 3) ? atoi(argv[2]) : 5;
        if (n < 1 || n > 50) { printf("count 1-50\n"); return; }
        for (int i = 0; i < n; i++) { gpio_put(LED_PIN, 1); sleep_ms(150); gpio_put(LED_PIN, 0); sleep_ms(150); }
        printf("blinked %d times\n", n);
    } else printf("unknown led subcommand: %s\n", argv[1]);
}

static void cmd_gpio(int argc, char* argv[]) {
    if (argc < 3) { printf("usage: gpio <read|write|mode|irq> <pin> [val]\n"); return; }

    // gpio irq <pin> [stop|dump] — delegate to gpio_mon
    if (strcmp(argv[1], "irq") == 0) {
        int pin = atoi(argv[2]);
        if (pin < 0 || pin > 28) { printf("invalid pin\n"); return; }
        if (argc >= 4 && strcmp(argv[3], "stop") == 0) {
            gpio_mon_stop((uint8_t)pin);
            printf("stopped IRQ monitor on GPIO%d\n", pin);
        } else if (argc >= 4 && strcmp(argv[3], "dump") == 0) {
            gpio_mon_dump((uint8_t)pin);
        } else {
            // default: live watch with 30 s timeout
            uint32_t timeout = (argc >= 4) ? (uint32_t)atoi(argv[3]) * 1000 : 30000;
            gpio_mon_watch((uint8_t)pin, timeout);
        }
        return;
    }

    int pin = atoi(argv[2]);
    if (pin < 0 || pin > 28) { printf("invalid pin\n"); return; }

    if (strcmp(argv[1], "read") == 0) {
        gpio_init(pin); gpio_set_dir(pin, GPIO_IN);
        printf("GPIO%-2d = %d\n", pin, gpio_get(pin));
    } else if (strcmp(argv[1], "write") == 0) {
        if (argc < 4) { printf("usage: gpio write <pin> <0|1>\n"); return; }
        int val = atoi(argv[3]);
        gpio_init(pin); gpio_set_dir(pin, GPIO_OUT); gpio_put(pin, val ? 1 : 0);
        printf("GPIO%-2d <- %d\n", pin, val ? 1 : 0);
    } else if (strcmp(argv[1], "mode") == 0) {
        if (argc < 4) { printf("usage: gpio mode <pin> <in|out>\n"); return; }
        gpio_init(pin);
        if      (strcmp(argv[3], "in")  == 0) { gpio_set_dir(pin, GPIO_IN);  printf("GPIO%-2d -> INPUT\n",  pin); }
        else if (strcmp(argv[3], "out") == 0) { gpio_set_dir(pin, GPIO_OUT); printf("GPIO%-2d -> OUTPUT\n", pin); }
        else printf("unknown mode: %s\n", argv[3]);
    } else {
        printf("unknown gpio subcommand: %s\n", argv[1]);
    }
}

static void cmd_adc(int argc, char* argv[]) {
    if (argc < 2) { printf("usage: adc <0|1|2>  (GPIO26-28)\n"); return; }
    int ch = atoi(argv[1]);
    if (ch < 0 || ch > 2) { printf("channel must be 0-2\n"); return; }
    adc_select_input((uint)ch);
    uint16_t raw  = adc_read();
    float voltage = raw * 3.3f / (1 << 12);
    printf("ADC%d  raw=%4d  voltage=%.3f V\n", ch, raw, voltage);
}

static void cmd_sleep(int argc, char* argv[]) {
    if (argc < 2) { printf("usage: sleep <ms>\n"); return; }
    int ms = atoi(argv[1]);
    if (ms < 1 || ms > 30000) { printf("range: 1-30000 ms\n"); return; }
    printf("sleeping %d ms...\n", ms);
    sleep_ms((uint32_t)ms);
}

static void cmd_repeat(int argc, char* argv[]) {
    if (argc < 3) { printf("usage: repeat <n> <command>\n"); return; }
    int n = atoi(argv[1]);
    if (n < 1 || n > 100) { printf("count must be 1-100\n"); return; }
    char subcmd[INPUT_SIZE]; subcmd[0] = '\0';
    for (int i = 2; i < argc; i++) {
        if (i > 2) strncat(subcmd, " ", sizeof(subcmd) - strlen(subcmd) - 1);
        strncat(subcmd, argv[i], sizeof(subcmd) - strlen(subcmd) - 1);
    }
    for (int i = 0; i < n; i++) {
        char tmp[INPUT_SIZE];
        strncpy(tmp, subcmd, sizeof(tmp) - 1);
        tmp[sizeof(tmp) - 1] = '\0';
        commands_execute(tmp);
    }
}

static void cmd_reboot(int argc, char* argv[]) {
    printf("rebooting in 1s...\n");
    sleep_ms(1000);
    watchdog_enable(1, 1);
    while (1) tight_loop_contents();
}

static void cmd_watch(int argc, char* argv[]) {
    if (argc < 3) {
        printf("usage: watch <interval_ms> <command>\n");
        printf("       press any key to stop\n");
        return;
    }
    int ms = atoi(argv[1]);
    if (ms < 10 || ms > 60000) { printf("interval must be 10-60000 ms\n"); return; }

    // Reconstruct sub-command string
    char subcmd[INPUT_SIZE]; subcmd[0] = '\0';
    for (int i = 2; i < argc; i++) {
        if (i > 2) strncat(subcmd, " ", sizeof(subcmd) - strlen(subcmd) - 1);
        strncat(subcmd, argv[i], sizeof(subcmd) - strlen(subcmd) - 1);
    }

    printf("watching '%s' every %d ms  (any key to stop)\n", subcmd, ms);
    uint32_t iter = 0;
    while (true) {
        int c = getchar_timeout_us(0);
        if (c != PICO_ERROR_TIMEOUT) { printf("\nwatch stopped.\n"); break; }

        printf("\033[2J\033[H");   // clear screen each iteration
        printf("--- watch [%lu] '%s' @ %d ms ---\n", ++iter, subcmd, ms);
        char tmp[INPUT_SIZE];
        strncpy(tmp, subcmd, sizeof(tmp) - 1);
        tmp[sizeof(tmp) - 1] = '\0';
        commands_execute(tmp);

        // wait in small chunks so we catch keypresses quickly
        uint32_t waited = 0;
        while (waited < (uint32_t)ms) {
            if (getchar_timeout_us(0) != PICO_ERROR_TIMEOUT) { printf("\nwatch stopped.\n"); return; }
            sleep_ms(10);
            waited += 10;
        }
    }
}

static void cmd_morse(int argc, char* argv[]) {
    if (argc < 2) {
        printf("usage: morse <text> [wpm]\n");
        printf("       blinks the onboard LED in morse code\n");
        return;
    }
    uint8_t wpm = (argc >= 3) ? (uint8_t)atoi(argv[2]) : 13;

    // Reconstruct the text (supports spaces)
    char text[128]; text[0] = '\0';
    for (int i = 1; i < argc - (argc >= 3 ? 1 : 0); i++) {
        if (i > 1) strncat(text, " ", sizeof(text) - strlen(text) - 1);
        strncat(text, argv[i], sizeof(text) - strlen(text) - 1);
    }

    morse_send(text, LED_PIN, wpm);
    LOG_I("morse", text);
}

static void cmd_syslog(int argc, char* argv[]) {
    if (argc < 2 || strcmp(argv[1], "show") == 0) {
        int tail = (argc >= 3) ? atoi(argv[2]) : 0;
        syslog_dump(LOG_DEBUG, tail);
        return;
    }
    if (strcmp(argv[1], "warn") == 0) { syslog_dump(LOG_WARN, 0); return; }
    if (strcmp(argv[1], "err")  == 0) { syslog_dump(LOG_ERR,  0); return; }
    if (strcmp(argv[1], "clear") == 0) { syslog_clear(); return; }
    if (strcmp(argv[1], "write") == 0 && argc >= 4) {
        // syslog write <tag> <message>
        syslog_write(LOG_INFO, argv[2], argv[3]);
        printf("logged.\n");
        return;
    }
    if (strcmp(argv[1], "stats") == 0) {
        printf("total entries written : %lu\n", syslog_total());
        return;
    }
    printf("usage:\n");
    printf("  syslog show [n]        - show log (last n entries)\n");
    printf("  syslog warn            - show WARN+ entries only\n");
    printf("  syslog err             - show ERR entries only\n");
    printf("  syslog write <tag> <msg> - add manual entry\n");
    printf("  syslog clear           - wipe the log\n");
    printf("  syslog stats           - show total count\n");
}

static void cmd_tone(int argc, char* argv[]) {
    if (argc < 3) {
        printf("usage: tone <pin> <note|hz> [duration_ms]\n");
        printf("       note examples: C4 G#3 A5 REST\n");
        printf("       hz   example : 440\n");
        return;
    }
    int pin = atoi(argv[1]);
    if (pin < 0 || pin > 28) { printf("invalid pin\n"); return; }

    uint32_t duration = (argc >= 4) ? (uint32_t)atoi(argv[3]) : 500;
    if (duration < 10 || duration > 10000) { printf("duration 10-10000 ms\n"); return; }

    // Determine if arg is a number (Hz) or note name
    uint32_t hz;
    if (isdigit((unsigned char)argv[2][0])) {
        hz = (uint32_t)atoi(argv[2]);
    } else {
        hz = tone_note_to_hz(argv[2]);
    }

    printf("tone: GPIO%d  %lu Hz  %lu ms\n", pin, hz, duration);
    tone_play((uint8_t)pin, hz, duration);
}

static void cmd_melody(int argc, char* argv[]) {
    if (argc < 3) {
        printf("usage: melody <pin> <C4:200 E4:200 G4:400 ...>\n");
        printf("       REST:100 for silence\n");
        return;
    }
    int pin = atoi(argv[1]);
    if (pin < 0 || pin > 28) { printf("invalid pin\n"); return; }

    // Reassemble sequence
    char seq[256]; seq[0] = '\0';
    for (int i = 2; i < argc; i++) {
        if (i > 2) strncat(seq, " ", sizeof(seq) - strlen(seq) - 1);
        strncat(seq, argv[i], sizeof(seq) - strlen(seq) - 1);
    }
    printf("melody on GPIO%d: %s\n", pin, seq);
    tone_melody((uint8_t)pin, seq);
}

static void cmd_avg(int argc, char* argv[]) {
    if (argc < 2) {
        printf("usage: avg <adc_ch 0-2> [samples 1-256]\n");
        return;
    }
    int ch = atoi(argv[1]);
    if (ch < 0 || ch > 2) { printf("channel must be 0-2\n"); return; }
    int samples = (argc >= 3) ? atoi(argv[2]) : 64;
    if (samples < 1 || samples > 256) { printf("samples: 1-256\n"); return; }

    adc_select_input((uint)ch);
    uint32_t sum = 0;
    uint16_t mn  = 0xFFFF, mx = 0;
    for (int i = 0; i < samples; i++) {
        uint16_t r = adc_read();
        sum += r;
        if (r < mn) mn = r;
        if (r > mx) mx = r;
    }
    uint16_t avg_raw = (uint16_t)(sum / samples);
    float voltage    = avg_raw * 3.3f / (1 << 12);
    float v_min      = mn * 3.3f / (1 << 12);
    float v_max      = mx * 3.3f / (1 << 12);
    printf("ADC%d  samples=%d\n", ch, samples);
    printf("  avg  : %4d  /  %.3f V\n", avg_raw, voltage);
    printf("  min  : %4d  /  %.3f V\n", mn, v_min);
    printf("  max  : %4d  /  %.3f V\n", mx, v_max);
    printf("  noise: %4d  counts p-p\n", mx - mn);
}

static void cmd_trigger(int argc, char* argv[]) {
    if (argc < 4) {
        printf("usage: trigger <pin> <rise|fall|both> <command>\n");
        printf("       waits for a GPIO edge then runs command once\n");
        return;
    }
    int pin = atoi(argv[1]);
    if (pin < 0 || pin > 28) { printf("invalid pin\n"); return; }

    uint32_t mask = 0;
    if      (strcmp(argv[2], "rise") == 0) mask = GPIO_IRQ_EDGE_RISE;
    else if (strcmp(argv[2], "fall") == 0) mask = GPIO_IRQ_EDGE_FALL;
    else if (strcmp(argv[2], "both") == 0) mask = GPIO_IRQ_EDGE_RISE | GPIO_IRQ_EDGE_FALL;
    else { printf("edge must be rise|fall|both\n"); return; }

    char subcmd[INPUT_SIZE]; subcmd[0] = '\0';
    for (int i = 3; i < argc; i++) {
        if (i > 3) strncat(subcmd, " ", sizeof(subcmd) - strlen(subcmd) - 1);
        strncat(subcmd, argv[i], sizeof(subcmd) - strlen(subcmd) - 1);
    }

    gpio_init(pin);
    gpio_set_dir(pin, GPIO_IN);
    gpio_pull_up(pin);

    printf("trigger: waiting for %s edge on GPIO%d\n", argv[2], pin);
    printf("         command: '%s'\n", subcmd);
    printf("         (press any key to cancel)\n");

    int last = gpio_get(pin);
    while (true) {
        if (getchar_timeout_us(0) != PICO_ERROR_TIMEOUT) { printf("cancelled.\n"); return; }
        int cur = gpio_get(pin);
        bool fire = false;
        if ((mask & GPIO_IRQ_EDGE_RISE) && cur == 1 && last == 0) fire = true;
        if ((mask & GPIO_IRQ_EDGE_FALL) && cur == 0 && last == 1) fire = true;
        if (fire) {
            printf("trigger fired! running '%s'\n", subcmd);
            LOG_I("trigger", subcmd);
            char tmp[INPUT_SIZE];
            strncpy(tmp, subcmd, sizeof(tmp) - 1);
            tmp[sizeof(tmp) - 1] = '\0';
            commands_execute(tmp);
            return;
        }
        last = cur;
        sleep_us(500);
    }
}

static void cmd_cron(int argc, char* argv[]) {
    if (argc < 3) {
        printf("usage: cron <delay_ms> <command>\n");
        return;
    }
    int ms = atoi(argv[1]);
    if (ms < 1 || ms > 60000) { printf("delay 1-60000 ms\n"); return; }

    char subcmd[INPUT_SIZE]; subcmd[0] = '\0';
    for (int i = 2; i < argc; i++) {
        if (i > 2) strncat(subcmd, " ", sizeof(subcmd) - strlen(subcmd) - 1);
        strncat(subcmd, argv[i], sizeof(subcmd) - strlen(subcmd) - 1);
    }

    printf("cron: '%s' will run in %d ms  (blocking)\n", subcmd, ms);
    sleep_ms((uint32_t)ms);
    char tmp[INPUT_SIZE];
    strncpy(tmp, subcmd, sizeof(tmp) - 1);
    tmp[sizeof(tmp) - 1] = '\0';
    printf("cron: running '%s'\n", tmp);
    commands_execute(tmp);
}

static void cmd_stats(int argc, char* argv[]) {
    uint64_t up_us  = time_us_64();
    uint32_t up_s   = (uint32_t)(up_us / 1000000);
    printf("=== DeckOS Runtime Stats ===\n");
    printf("uptime          : %02uh %02um %02us\n",
           up_s / 3600, (up_s % 3600) / 60, up_s % 60);
    printf("commands run    : %lu\n", s_cmd_count);
    printf("unknown cmds    : %lu\n", s_unknown_count);
    printf("log entries     : %lu\n", syslog_total());
    printf("watchdog reboot : %s\n", watchdog_caused_reboot() ? "yes" : "no");
    printf("cpu freq        : %lu MHz\n", clock_get_hz(clk_sys) / 1000000);
    adc_select_input(4);
    float v  = adc_read() * 3.3f / (1 << 12);
    float tc = 27.0f - (v - 0.706f) / 0.001721f;
    printf("core temp       : %.1f C\n", tc);
    printf("============================\n");
}

static command_t command_table[] = {
    // Core / info
    {"help",    cmd_help,    "show this command list"},
    {"version", cmd_version, "show OS version and build info"},
    {"clear",   cmd_clear,   "clear the terminal screen"},
    {"echo",    cmd_echo,    "echo <text>"},
    {"uptime",  cmd_uptime,  "show time since boot"},
    {"sysinfo", cmd_sysinfo, "full system info"},
    {"stats",   cmd_stats,   "runtime statistics"},
    // Hardware
    {"temp",    cmd_temp,    "read internal core temperature"},
    {"mem",     cmd_mem,     "show memory info"},
    {"memmap",  cmd_memmap,  "detailed memory map"},
    {"led",     cmd_led,     "led <on|off|toggle|blink [n]>"},
    {"gpio",    cmd_gpio,    "gpio <read|write|mode|irq> <pin> [val]"},
    {"pwm",     cmd_pwm,     "pwm <pin> <duty 0-100>"},
    {"adc",     cmd_adc,     "adc <ch 0-2>  raw ADC read"},
    {"avg",     cmd_avg,     "avg <ch> [samples]  averaged ADC read"},
    {"pull",    cmd_pull,    "pull <pin> <up|down|none>"},
    {"clock",   cmd_clock,   "clock [mhz]  get/set CPU freq (48-200)"},
    {"i2c",     cmd_i2c,     "i2c scan|read|write  I2C bus ops"},
    // Audio / signalling
    {"tone",    cmd_tone,    "tone <pin> <note|hz> [ms]  play a tone"},
    {"melody",  cmd_melody,  "melody <pin> <C4:200 E4:200 ...>"},
    {"morse",   cmd_morse,   "morse <text> [wpm]  blink LED in morse"},
    // Scripting / automation
    {"sleep",   cmd_sleep,   "sleep <ms>  pause"},
    {"repeat",  cmd_repeat,  "repeat <n> <cmd>"},
    {"watch",   cmd_watch,   "watch <ms> <cmd>  run cmd at interval"},
    {"trigger", cmd_trigger, "trigger <pin> <rise|fall|both> <cmd>"},
    {"cron",    cmd_cron,    "cron <delay_ms> <cmd>  deferred run"},
    // System
    {"reboot",  cmd_reboot,  "reboot via watchdog"},
    {"dfu",     cmd_dfu,     "reboot into USB DFU (BOOTSEL)"},
    {"uid",     cmd_uid,     "show unique board ID"},
    {"wdog",    cmd_wdog,    "show watchdog status"},
    {"pin",     cmd_pin,     "dump all GPIO pin states"},
    // Subsystems
    {"drivers", cmd_drivers, "list loaded drivers"},
    {"tasks",   cmd_tasks,   "list/enable/disable background tasks"},
    {"config",  cmd_config,  "config show|set|save|reset"},
    {"syslog",  cmd_syslog,  "syslog show|warn|err|write|clear|stats"},
};

static const int command_count = sizeof(command_table) / sizeof(command_t);

void commands_init(void) {
    s_boot_us   = time_us_64();
    s_cmd_count = 0;
    printf("[commands] %d commands registered\n", command_count);
}

void commands_list(void) {
    printf("available commands:\n");
    for (int i = 0; i < command_count; i++)
        printf("  %-10s - %s\n", command_table[i].name, command_table[i].description);
}

void commands_execute(char* input) {
    if (!input || strlen(input) == 0) return;

    static char  buf[128];
    static char* argv[MAX_ARGS];
    int argc = 0;

    strncpy(buf, input, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';
 
    char* tok = strtok(buf, " ");
    while (tok && argc < MAX_ARGS) { argv[argc++] = tok; tok = strtok(NULL, " "); }
    if (argc == 0) return;

    for (int i = 0; i < command_count; i++) {
        if (strcmp(argv[0], command_table[i].name) == 0) {
            s_cmd_count++;
            LOG_D("shell", argv[0]);
            command_table[i].handler(argc, argv);
            return;
        }
    }
    s_unknown_count++;
    char logbuf[64];
    snprintf(logbuf, sizeof(logbuf), "unknown: %s", argv[0]);
    LOG_W("shell", logbuf);
    printf("unknown command: '%s'  (type 'help' for list)\n", argv[0]);
}