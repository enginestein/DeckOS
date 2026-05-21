#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include "pico/stdlib.h"
#include "pico/time.h"
#include "bg_job.h"
#include "pico/unique_id.h"
#include "editor.h"
#include "pico/multicore.h"
#include "print_lock.h"
#include "hardware/adc.h"
#include "hardware/gpio.h"
#include "hardware/watchdog.h"
#include "hardware/pwm.h"
#include "hardware/clocks.h"
#include "hardware/i2c.h"
#include "hardware/spi.h"
#include "hardware/flash.h"
#include "hardware/uart.h"
#include "pico/bootrom.h"
#include "commands.h"
#include "kernel.h"
#include "dscript.h"
#include "drivers.h"
#include "vfs.h"
#include "uart_detect.h"
#include "bt.h"
#include "esp8266.h"
#include "scheduler.h"
#include "config.h"
#include "syslog.h"
#include "gpio_mon.h"
#include "morse.h"
#include "tone.h"
#include "servo.h"
#include "spi_bus.h"
#include "uart_pass.h"
#include "device_detect.h"
#include "bench.h"
#include "heap_track.h"

#define LED_PIN    25

extern flash_config_t g_config;

static uint32_t s_cmd_count     = 0;
static uint32_t s_unknown_count = 0;
static uint64_t s_boot_us;

static void print_uptime(void) {
    uint64_t us = time_us_64();
    uint32_t s  = (uint32_t)(us / 1000000);
    printf("%02uh %02um %02us", s / 3600, (s % 3600) / 60, s % 60);
}

static void cmd_help(int argc, char* argv[]) {
    (void)argc; (void)argv;

    typedef struct { const char* name; const char* desc; } entry_t;
    typedef struct { const char* group; const entry_t* cmds; int count; } group_t;

    static const entry_t g_core[] = {
        {"help",    "show this help"},
        {"version", "OS version and build info"},
        {"clear",   "clear terminal screen"},
        {"echo",    "echo <text>"},
        {"uptime",  "time since boot"},
        {"sysinfo", "full system summary"},
        {"stats",   "runtime statistics"},
        {"top",     "live task monitor"},
    };
    static const entry_t g_hardware[] = {
        {"temp",    "internal core temperature"},
        {"mem",     "memory overview"},
        {"memmap",  "detailed memory map"},
        {"free",    "heap allocator stats"},
        {"gpio",    "read / write / mode / irq <pin>"},
        {"led",     "on | off | toggle | blink [n]"},
        {"pwm",     "pwm <pin> <duty 0-100>"},
        {"adc",     "raw ADC read  (ch 0-2, GPIO26-28)"},
        {"avg",     "averaged ADC read  [samples]"},
        {"pull",    "pull <pin> up | down | none"},
        {"clock",   "get / set CPU freq (48-200 MHz)"},
        {"pin",     "snapshot of all GPIO states"},
        {"pinout",  "ASCII Pico pinout with live states"},
        {"uid",     "unique board ID"},
        {"wdog",    "watchdog status"},
    };
    static const entry_t g_buses[] = {
        {"i2c", "scan [sda scl] | read | write  (default SDA=GP4 SCL=GP5)"},
        {"spi",     "init | write | read | xfer"},
        {"uart",    "passthrough <baud> <tx> <rx> [timeout_s]"},
    };
    static const entry_t g_probes[] = {
        {"la", "logic analyser  <pin> [samples] [us] [trigger]"},
        {"detect",  "scan | uart <pin> | analyze <pin>"},
        {"imu",     "MPU6050 read|stream|attitude|calibrate"}
    };
    static const entry_t g_servo[] = {
        {"servo",   "<pin> <angle> | sweep | bg sweep/goto/stop"},
    };
    static const entry_t g_audio[] = {
        {"tone",    "<pin> <note|Hz> [ms]  passive buzzer"},
        {"melody",  "<pin> <C4:200 E4:200 ...> | elise | canon"},
        {"morse",   "<text> [wpm]  blink LED in morse"},
        {"piano",   "<pin>  play buzzer from keyboard"},
    };
    static const entry_t g_scripting[] = {
        {"sleep",   "sleep <ms>"},
        {"repeat",  "repeat <n> <command>"},
        {"watch",   "watch <ms> <command>  run at interval"},
        {"trigger", "trigger <pin> <rise|fall|both> <cmd>"},
        {"cron",    "cron <delay_ms> <command>  deferred run"},
        {"bench",   "bench <iters> <cmd>  throughput test"},
    };
    static const entry_t g_flash[] = {
        {"flash",   "read | write | erase <addr>  raw flash"},
    };
    static const entry_t g_system[] = {
        {"reboot",  "reboot via watchdog"},
        {"dfu",     "enter USB DFU (BOOTSEL) mode"},
        {"drivers", "list loaded drivers"},
        {"tasks",   "list / enable / disable background tasks"},
        {"config",  "show | set | save | reset"},
        {"syslog",  "show | warn | err | write | clear | stats"},
        {"jobs", "list / cancel background Core1 jobs"},
    };
    static const entry_t g_bluetooth[] = {
        {"bt shell",  "wireless DeckOS terminal over HC-05"},
        {"bt log",    "on|off  mirror syslog to BT"},
        {"bt exec",   "<cmd>  remote command execution"},
        {"bt top",    "[ms]  stream live stats to BT"},
        {"bt send",   "<file>  send VFS file over BT"},
        {"bt recv",   "<file>  receive file into VFS"},
        {"bt sniff",  "[s]  raw byte sniffer"},
        {"bt at",     "interactive AT command mode"},
        {"bt name",   "<n>  set module name (AT mode)"},
        {"bt pin",    "<n>  set pairing PIN (AT mode)"},
        {"bt baud",   "<n>  change UART baud (AT mode)"},
        {"bt status", "show BT state"},
    };
static const entry_t g_wifi[] = {
    {"wifi init",          "[baud]  initialise ESP8266 on UART1"},
    {"wifi status",        "show ESP8266 wiring and runtime state"},
    {"wifi ping",          "probe module and test connectivity"},
    {"wifi scan",          "scan nearby access points"},
    {"wifi join",          "<ssid> <password>  join a network"},
    {"wifi ip",            "show assigned IP / station info"},
    {"wifi shell",         "interactive ESP8266 AT shell"},
    {"wifi deinit",        "release UART1 from ESP8266"},
    {"wifi bridge auto",   "auto-detect ESP firmware type"},
    {"wifi bridge at",     "force AT passthrough mode"},
    {"wifi bridge raw",    "pass commands without translation"},
    {"wifi bridge status", "show bridge mode and WiFi state"},
    {"wifi bridge reset",  "restart the ESP8266"},
    {"wifi bridge scan",   "scan networks via bridge (@scan)"},
    {"wifi bridge connect","connect using stored SSID via bridge"},
    {"wifi telnet",        "start telnet server on port 23"},
    {"wifi telnet stop",   "stop telnet server"},
    {"wifi serve",         "start HTTP server (connect first)"},
    {"wifi get",           "<url>  HTTP GET request"},
    {"wifi post",          "<url> <body>  HTTP POST request"},
};
    static const entry_t g_fs[] = {
        {"ls",      "list directory  [path]"},
        {"cat",     "print file contents"},
        {"touch",   "create / update file"},
        {"mkdir",   "create directory"},
        {"rm",      "rm [-r] <path>  remove file or dir"},
        {"write",   "overwrite file with text"},
        {"append",  "append text to file"},
        {"hexdump", "hex + ASCII dump"},
        {"cd",      "change directory"},
        {"pwd",     "print working directory"},
        {"cp",      "cp <src> <dst>"},
        {"mv",      "mv <src> <dst>  move / rename"},
        {"stat",    "file / dir metadata"},
        {"wc",      "count lines, words, bytes"},
        {"grep",    "grep <pattern> <file>"},
        {"find",    "recursive name search"},
        {"df",      "filesystem usage summary"},
        {"tree",    "print directory tree"},
    };

#define COUNT(a) (int)(sizeof(a) / sizeof((a)[0]))

    static const group_t groups[] = {
        {"core",              g_core,      COUNT(g_core)},
        {"hardware",          g_hardware,  COUNT(g_hardware)},
        {"buses",             g_buses,     COUNT(g_buses)},
        {"sensors & probes",  g_probes,    COUNT(g_probes)},
        {"servo",             g_servo,     COUNT(g_servo)},
        {"audio & signalling",g_audio,     COUNT(g_audio)},
        {"scripting",         g_scripting, COUNT(g_scripting)},
        {"flash",             g_flash,     COUNT(g_flash)},
        {"system",            g_system,    COUNT(g_system)},
        {"bluetooth",         g_bluetooth, COUNT(g_bluetooth)},
        {"wifi / esp8266",    g_wifi,      COUNT(g_wifi)},
        {"filesystem",        g_fs,        COUNT(g_fs)},
    };

#undef COUNT

    static const int group_count = (int)(sizeof(groups) / sizeof(group_t));
    static const int NAME_COL    = 12;

    printf("DeckOS v2.1  \xe2\x80\x94  available commands\n");
    printf("====================================================\n");

    for (int g = 0; g < group_count; g++) {
        printf("\n  \xe2\x96\xb8 %s\n", groups[g].group);
        for (int i = 0; i < groups[g].count; i++) {
            printf("    %-*s %s\n",
                   NAME_COL,
                   groups[g].cmds[i].name,
                   groups[g].cmds[i].desc);
        }
    }

    printf("\n----------------------------------------------------\n");
    printf("  type <command> without args for usage details\n");
}

static void cmd_version(int argc, char* argv[]) {
    printf("DeckOS v2.1  |  Raspberry Pi Pico\n");
    printf("Build: %s %s\n", __DATE__, __TIME__);
}

static void cmd_clear(int argc, char* argv[])   { printf("\033[2J\033[H"); }

static void cmd_echo(int argc, char* argv[]) {
    for (int i = 1; i < argc; i++)
        printf("%s%s", argv[i], (i < argc - 1) ? " " : "");
    printf("\n");
}

static void cmd_uptime(int argc, char* argv[]) {
    printf("uptime: "); print_uptime(); printf("\n");
}

static void cmd_temp(int argc, char* argv[]) {
    adc_select_input(4);
    uint16_t raw  = adc_read();
    float voltage = raw * 3.3f / (1 << 12);
    float temp_c  = 27.0f - (voltage - 0.706f) / 0.001721f;
    float temp_f  = temp_c * 9.0f / 5.0f + 32.1f;
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
    extern char __end__;

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

static void cmd_bt(int argc, char* argv[]) {
    if (argc < 2) {
        printf("usage:\n");
        printf("  bt shell              - wireless DeckOS terminal\n");
        printf("  bt log <on|off>       - mirror syslog to BT\n");
        printf("  bt exec <command>     - run command, reply to BT\n");
        printf("  bt top [interval_ms]  - stream live stats to BT\n");
        printf("  bt send <file>        - send VFS file over BT\n");
        printf("  bt recv <file>        - receive file from BT into VFS\n");
        printf("  bt sniff [timeout_s]  - raw byte sniffer (hex + ASCII)\n");
        printf("  bt at                 - interactive AT command mode\n");
        printf("  bt name <name>        - set HC-05 module name (AT mode)\n");
        printf("  bt pin  <code>        - set HC-05 PIN code  (AT mode)\n");
        printf("  bt baud <rate>        - set HC-05 UART baud (AT mode)\n");
        printf("  bt status             - show BT subsystem state\n");
        printf("  bt init [baud]        - (re)initialise BT UART\n");
        return;
    }

    if (strcmp(argv[1], "status") == 0) {
        printf("BT subsystem status:\n");
        printf("  initialised : %s\n", bt_is_ready()     ? "yes" : "no");
        printf("  connected   : %s\n", bt_is_connected() ? "yes" : "no (or STATE pin not wired)");
        printf("  log mirror  : %s\n", bt_log_is_enabled()? "on" : "off");
        printf("  uart        : UART%d  TX=GP%d  RX=GP%d\n",
               BT_UART == uart0 ? 0 : 1, BT_TX_PIN, BT_RX_PIN);
        printf("  default baud: %d\n", BT_DEFAULT_BAUD);
        return;
    }

    if (strcmp(argv[1], "init") == 0) {
        uint32_t baud = (argc >= 3) ? (uint32_t)atoi(argv[2]) : BT_DEFAULT_BAUD;
        if (baud < 300 || baud > 921600) { printf("baud must be 300-921600\n"); return; }
        bt_init(baud);
        return;
    }

    if (strcmp(argv[1], "shell") == 0) {
        if (!bt_is_ready()) { printf("bt: not initialised - run 'bt init'\n"); return; }
        printf("Starting BT shell. Connect phone terminal to HC-05.\n");
        printf("USB terminal will also show output.\n");
        printf("Type 'exit' in BT terminal to stop.\n");
        bt_shell_run();
        return;
    }

    if (strcmp(argv[1], "log") == 0) {
        if (!bt_is_ready()) { printf("bt: not initialised\n"); return; }
        if (argc < 3) {
            printf("bt log: %s\n", bt_log_is_enabled() ? "on" : "off");
            return;
        }
        bool on = (strcmp(argv[2], "on") == 0 || strcmp(argv[2], "1") == 0);
        bt_log_enable(on);
        printf("BT log mirror: %s\n", on ? "on" : "off");
        return;
    }

    if (strcmp(argv[1], "exec") == 0) {
        if (!bt_is_ready()) { printf("bt: not initialised\n"); return; }
        if (argc < 3) { printf("bt exec <command>\n"); return; }
        char subcmd[INPUT_SIZE]; subcmd[0] = '\0';
        for (int i = 2; i < argc; i++) {
            if (i > 2) strncat(subcmd, " ", sizeof(subcmd) - strlen(subcmd) - 1);
            strncat(subcmd, argv[i], sizeof(subcmd) - strlen(subcmd) - 1);
        }
        printf("bt exec: '%s'\n", subcmd);
        bt_exec(subcmd);
        return;
    }

    if (strcmp(argv[1], "top") == 0) {
        if (!bt_is_ready()) { printf("bt: not initialised\n"); return; }
        uint32_t ms = (argc >= 3) ? (uint32_t)atoi(argv[2]) : 1000;
        if (ms < 100 || ms > 30000) ms = 1000;
        bt_top_stream(ms);
        return;
    }

    if (strcmp(argv[1], "send") == 0) {
        if (!bt_is_ready()) { printf("bt: not initialised\n"); return; }
        if (argc < 3) { printf("bt send <vfs_path>\n"); return; }
        bt_send_file(argv[2]);
        return;
    }

    if (strcmp(argv[1], "recv") == 0) {
        if (!bt_is_ready()) { printf("bt: not initialised\n"); return; }
        if (argc < 3) { printf("bt recv <vfs_path>\n"); return; }
        bt_recv_file(argv[2]);
        return;
    }

    if (strcmp(argv[1], "sniff") == 0) {
        if (!bt_is_ready()) { printf("bt: not initialised\n"); return; }
        uint32_t timeout_ms = (argc >= 3)
            ? (uint32_t)(atoi(argv[2]) * 1000) : 0;
        bt_sniff(timeout_ms);
        return;
    }

    if (strcmp(argv[1], "at") == 0) {
        if (!bt_is_ready()) bt_init(BT_DEFAULT_BAUD);
        bt_at_mode();
        return;
    }

    if (strcmp(argv[1], "name") == 0) {
        if (!bt_is_ready()) { printf("bt: not initialised\n"); return; }
        if (argc < 3) { printf("bt name <new_name>\n"); return; }
        char cmd[64], resp[64];
        snprintf(cmd, sizeof(cmd), "AT+NAME=%s", argv[2]);
        extern void bt_uart_init_pub(uint32_t baud);
        printf("Sending: %s\n", cmd);
        bool ok = bt_at_cmd(cmd, resp, sizeof(resp), 2000);
        printf("Response: %s\n", ok ? resp : "(no response)");
        printf("Note: HC-05 must be in AT mode (KEY pin HIGH at power-on)\n");
        return;
    }

    if (strcmp(argv[1], "pin") == 0) {
        if (!bt_is_ready()) { printf("bt: not initialised\n"); return; }
        if (argc < 3) { printf("bt pin <4-digit-code>\n"); return; }
        char cmd[64], resp[64];
        snprintf(cmd, sizeof(cmd), "AT+PSWD=%s", argv[2]);
        printf("Sending: %s\n", cmd);
        bool ok = bt_at_cmd(cmd, resp, sizeof(resp), 2000);
        printf("Response: %s\n", ok ? resp : "(no response)");
        printf("Note: HC-05 must be in AT mode (KEY pin HIGH at power-on)\n");
        return;
    }

    if (strcmp(argv[1], "baud") == 0) {
        if (!bt_is_ready()) { printf("bt: not initialised\n"); return; }
        if (argc < 3) { printf("bt baud <rate>  (e.g. 9600 or 115200)\n"); return; }
        uint32_t new_baud = (uint32_t)atoi(argv[2]);
        if (new_baud < 1200 || new_baud > 921600) {
            printf("baud must be 1200-921600\n"); return;
        }
        // HC-05 AT+UART=<baud>,<stop>,<parity>
        char cmd[64], resp[64];
        snprintf(cmd, sizeof(cmd), "AT+UART=%lu,1,0", new_baud);
        printf("Sending: %s\n", cmd);
        bool ok = bt_at_cmd(cmd, resp, sizeof(resp), 2000);
        printf("Response: %s\n", ok ? resp : "(no response)");
        if (ok && strstr(resp, "OK")) {
            printf("Success - reinitialising UART at %lu baud\n", new_baud);
            bt_init(new_baud);
        }
        return;
    }

    printf("bt: unknown subcommand '%s'  (type 'bt' for help)\n", argv[1]);
}

static void cmd_wifi(int argc, char* argv[]) {
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
        printf("  wifi telnet stop        - stop telnet server\n");
        return;
    }

if (strcmp(argv[1], "bridge") == 0) {
    if (argc < 3) {
        printf("wifi bridge subcommands: auto|at|raw|status|reset|scan|connect\n");
        return;
    }
    if (!esp8266_is_ready()) {
        printf("wifi: not initialised -- run 'wifi init' first\n");
        return;
    }
    if      (strcmp(argv[2], "auto")     == 0) esp8266_bridge_mode_set("auto");
    else if (strcmp(argv[2], "at")       == 0) esp8266_bridge_mode_set("at");
    else if (strcmp(argv[2], "raw")      == 0) esp8266_bridge_mode_set("raw");
    else if (strcmp(argv[2], "status")   == 0) esp8266_bridge_status();
    else if (strcmp(argv[2], "reset")    == 0) esp8266_bridge_reset();
    else if (strcmp(argv[2], "scan")     == 0) esp8266_bridge_scan();
    else if (strcmp(argv[2], "connect")  == 0) esp8266_bridge_connect();
    else printf("unknown bridge subcommand: %s\n", argv[2]);
    return;
}
    if (strcmp(argv[1], "serve") == 0) {
    esp8266_http_serve();
    return;
}
if (strcmp(argv[1], "telnet") == 0) {
    if (argc >= 3 && strcmp(argv[2], "stop") == 0) {
        esp8266_telnet_stop();
    } else {
        esp8266_telnet_start();
    }
    return;
}
if (strcmp(argv[1], "get") == 0) {
    if (argc < 3) { printf("usage: wifi get <url>\n"); return; }
    esp8266_http_get(argv[2]);
    return;
}

if (strcmp(argv[1], "post") == 0) {
    if (argc < 4) { printf("usage: wifi post <url> <body>\n"); return; }
    esp8266_http_post(argv[2], argv[3]);
    return;
}
    if (strcmp(argv[1], "init") == 0) {
        uint32_t baud = (argc >= 3) ? (uint32_t)atoi(argv[2]) : ESP8266_DEFAULT_BAUD;
        if (baud < 1200 || baud > 921600) { printf("baud must be 1200-921600\n"); return; }
        esp8266_init(baud);
        return;
    }

    if (strcmp(argv[1], "status") == 0) {
        esp8266_print_status();
        return;
    }

    if (strcmp(argv[1], "ping") == 0) {
        esp8266_ping();
        return;
    }

    if (strcmp(argv[1], "scan") == 0) {
        esp8266_scan();
        return;
    }

    if (strcmp(argv[1], "join") == 0) {
        if (argc < 4) { printf("usage: wifi join <ssid> <password>\n"); return; }
        esp8266_join(argv[2], argv[3]);
        return;
    }

    if (strcmp(argv[1], "ip") == 0) {
        esp8266_ip();
        return;
    }

    if (strcmp(argv[1], "shell") == 0) {
        esp8266_shell();
        return;
    }

    if (strcmp(argv[1], "deinit") == 0) {
        if (!esp8266_is_ready()) { printf("wifi: already deinitialised\n"); return; }
        esp8266_deinit();
        printf("wifi: ESP8266 released from UART1\n");
        return;
    }

    printf("wifi: unknown subcommand '%s'  (type 'wifi' for help)\n", argv[1]);
}

static void cmd_wifi_bridge(int argc, char* argv[]) {
    if (argc < 2) {
        printf("bridge commands:\n");
        printf("  wifi bridge auto      - auto-detect firmware\n");
        printf("  wifi bridge at        - AT passthrough mode\n");
        printf("  wifi bridge raw       - raw command mode\n");
        printf("  wifi bridge status    - show bridge status\n");
        printf("  wifi bridge reset     - reset ESP8266\n");
        return;
    }

    // argv[0] = "wifi bridge", argv[1] = subcommand
    if (!esp8266_is_ready()) {
        printf("wifi: not initialised - run 'wifi init' first\n");
        return;
    }

    if      (strcmp(argv[1], "auto")     == 0) esp8266_bridge_mode_set("auto");
    else if (strcmp(argv[1], "at")       == 0) esp8266_bridge_mode_set("at");
    else if (strcmp(argv[1], "raw")      == 0) esp8266_bridge_mode_set("raw");
    else if (strcmp(argv[1], "status")   == 0) esp8266_bridge_status();
    else if (strcmp(argv[1], "reset")    == 0) esp8266_bridge_reset();
    else printf("unknown bridge command: %s\n", argv[1]);
}

static void cmd_run(int argc, char* argv[]) {
    if (argc < 2) {
        printf("usage: run <vfs_script_path>\n");
        printf("       run /home/blink.ds\n");
        return;
    }
    script_run_file(argv[1]);
}

static void cmd_script(int argc, char* argv[]) {
    if (argc < 2) {
        printf("usage:\n");
        printf("  script run <file>     - run a .ds script from VFS\n");
        printf("  script test           - run built-in self-test\n");
        return;
    }
    if (strcmp(argv[1], "run") == 0 && argc >= 3) {
        script_run_file(argv[2]);
    } else if (strcmp(argv[1], "test") == 0) {
        const char* test =
            "# DeckScript self-test\n"
            "let x = 10\n"
            "let y = 3\n"
            "let z = x + y\n"
            "print z is $z\n"
            "if $z == 13\n"
            "  print PASS: arithmetic ok\n"
            "else\n"
            "  print FAIL\n"
            "endif\n"
            "let i = 0\n"
            "repeat 3\n"
            "  print loop $_i\n"
            "endrepeat\n"
            "print test done\n";
        script_ctx_t ctx;
        script_ctx_init(&ctx);
        script_run_string(&ctx, test);
    }
}

static void cmd_detect_extended(int argc, char* argv[]) {
    if (argc < 2) {
        uint sda = 4, scl = 5;
        if (argc >= 2 && isdigit((unsigned char)argv[1][0])) sda = (uint)atoi(argv[1]);
        if (argc >= 3 && isdigit((unsigned char)argv[2][0])) scl = (uint)atoi(argv[2]);
        device_detect_print(sda, scl);
        return;
    }

    if (strcmp(argv[1], "uart") == 0) {
        int pin = (argc >= 3) ? atoi(argv[2]) : -1;
        if (pin < 0 || pin > 28) {
            printf("usage: detect uart <pin> [timeout_s]\n");
            return;
        }
        uint32_t timeout_ms = (argc >= 4) ? (uint32_t)(atoi(argv[3]) * 1000) : 3000;
        uart_detect_run((uint8_t)pin, timeout_ms);
        return;
    }

    if (strcmp(argv[1], "analyze") == 0 || strcmp(argv[1], "analyse") == 0) {
        int pin = (argc >= 3) ? atoi(argv[2]) : -1;
        if (pin < 0 || pin > 28) {
            printf("usage: detect analyze <pin> [samples] [us_per_sample]\n");
            return;
        }
        int samples       = (argc >= 4) ? atoi(argv[3]) : 256;
        int us_per_sample = (argc >= 5) ? atoi(argv[4]) : 5;
        la_detect_protocol((uint8_t)pin, samples, us_per_sample);
        return;
    }

    // fallback: treat as detect [sda] [scl]
    uint sda = (uint)atoi(argv[1]);
    uint scl = (argc >= 3) ? (uint)atoi(argv[2]) : sda + 1;
    device_detect_print(sda, scl);
}

static void cmd_sysinfo(int argc, char* argv[]) {
    printf("=================================\n");
    printf("  DeckOS v2.1  -  system info  \n");
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

static void cmd_stats(int argc, char* argv[]) {
    uint64_t up_us = time_us_64();
    uint32_t up_s  = (uint32_t)(up_us / 1000000);
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

static void argv_join(char *buf, int buflen, int argc, char *argv[], int start) {
    buf[0] = '\0';
    for (int i = start; i < argc; i++) {
        if (i > start) strncat(buf, " ", (size_t)(buflen - 1) - strlen(buf));
        strncat(buf, argv[i], (size_t)(buflen - 1) - strlen(buf));
    }
    int len = (int)strlen(buf);
    if (len >= 2 && buf[0] == '"' && buf[len - 1] == '"') {
        memmove(buf, buf + 1, (size_t)(len - 2));
        buf[len - 2] = '\0';
    }
}
 
static void cmd_ls(int argc, char *argv[]) {
    vfs_ls(argc >= 2 ? argv[1] : ".");
}
 
static void cmd_cat(int argc, char *argv[]) {
    if (argc < 2) { printf("usage: cat <file> [file2 ...]\n"); return; }
    for (int a = 1; a < argc; a++) {
        if (argc > 2) printf("==> %s <==\n", argv[a]);
        vfs_cat(argv[a]);
    }
}
 
static void cmd_touch(int argc, char *argv[]) {
    if (argc < 2) { printf("usage: touch <file> [file2 ...]\n"); return; }
    for (int a = 1; a < argc; a++) {
        int idx = vfs_touch(argv[a]);
        if (idx >= 0) printf("touched '%s'\n", argv[a]);
    }
}
 
static void cmd_mkdir(int argc, char *argv[]) {
    if (argc < 2) { printf("usage: mkdir <dir> [dir2 ...]\n"); return; }
    for (int a = 1; a < argc; a++) {
        if (vfs_mkdir(argv[a]) >= 0) printf("created dir '%s'\n", argv[a]);
    }
}
 
static void cmd_rm(int argc, char *argv[]) {
    if (argc < 2) { printf("usage: rm [-r] <path> [path2 ...]\n"); return; }
    bool recursive = false;
    int  start     = 1;
    if (strcmp(argv[1], "-r") == 0) { recursive = true; start = 2; }
    if (start >= argc) { printf("rm: missing path argument\n"); return; }
    for (int a = start; a < argc; a++) {
        if (vfs_rm(argv[a], recursive) == 0)
            printf("removed '%s'\n", argv[a]);
    }
}

static bool interactive_write(const char *path) {
    printf("iwrite: writing to '%s'\n", path);
    printf("  paste or type content; end with '.' on its own line.\n");
    printf("  type '.abort' to cancel.\n");
    printf("---\n");

    int  pushback   = -1;   /* one-char unget slot              */
    int  esc_step   = 0;    /* ESC sequence filter state        */
    bool first_line = true; /* first write = overwrite, rest = append */
    int  total      = 0;

    while (true) {
        char line[SCRIPT_LINE_LEN];
        int  lpos      = 0;
        bool line_done = false;

        while (!line_done && lpos < (int)(sizeof(line) - 1)) {
            int c;

            if (pushback >= 0) { c = pushback; pushback = -1; }
            else {
                c = getchar_timeout_us(30000000UL);
                if (c == PICO_ERROR_TIMEOUT) {
                    printf("\niwrite: timeout — aborting\n");
                    return false;
                }
            }

            /* swallow VT/ANSI ESC sequences, including bracketed-paste
               ESC[200~ (start) and ESC[201~ (end) sent by many terminals */
            if (c == 27)       { esc_step = 1; continue; }
            if (esc_step == 1) { esc_step = (c == '[') ? 2 : 0; continue; }
            if (esc_step >= 2) {
                /* consume numeric args; stop on terminating letter or '~' */
                if (c == '~' || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z'))
                    esc_step = 0;
                else
                    esc_step++;
                continue;
            }

            if (c == '\r') {
                /* CRLF: consume the \n that may follow — only peek after \r */
                int next = getchar_timeout_us(5000);
                if (next != PICO_ERROR_TIMEOUT && next != '\n')
                    pushback = next;   /* real char — save for next read */
                putchar('\n'); fflush(stdout);
                line_done = true;
            } else if (c == '\n') {
                /* bare LF — end line immediately, no peeking */
                putchar('\n'); fflush(stdout);
                line_done = true;
            } else if (c == 3) {
                printf("\niwrite: cancelled\n");
                return false;
            } else if ((c == 127 || c == '\b') && lpos > 0) {
                lpos--;
                printf("\b \b"); fflush(stdout);
            } else if (c >= 32 && c < 127) {
                line[lpos++] = (char)c;
                putchar(c); fflush(stdout);
            }
        }
        line[lpos] = '\0';

        if (strcmp(line, ".") == 0) break;
        if (strcmp(line, ".abort") == 0) {
            printf("iwrite: aborted — nothing written\n");
            return false;
        }

        /* Write directly to VFS per line — no large intermediate buffer.
           First write uses append=false (create/overwrite); rest append. */
        uint8_t wbuf[SCRIPT_LINE_LEN + 1];
        memcpy(wbuf, line, (size_t)lpos);
        wbuf[lpos] = '\n';
        int n = vfs_write(path, wbuf, (uint32_t)(lpos + 1), !first_line);
        if (n < 0) {
            printf("iwrite: write error on '%s'\n", path);
            return false;
        }
        total      += lpos + 1;
        first_line  = false;
    }

    if (total == 0) { printf("iwrite: empty content — nothing written\n"); return false; }
    printf("---\niwrite: saved %d bytes to '%s'\n", total, path);
    return true;
}

extern void cmd_imu(int argc, char* argv[]);
 
static void cmd_write(int argc, char *argv[]) {
    if (argc < 2) {
        printf("usage:\n");
        printf("  write <file> <content...>    overwrite file with one line of text\n");
        printf("  write -i <file>              interactive multi-line write (end with '.')\n");
        return;
    }
 
   
    if (strcmp(argv[1], "-i") == 0) {
        if (argc < 3) {
            printf("write -i: missing filename\n");
            return;
        }
        interactive_write(argv[2]);
        return;
    }
 
   
    if (argc < 3) {
        printf("usage: write <file> <content...>\n");
        printf("       use 'write -i <file>' for multi-line interactive input\n");
        return;
    }
 
    char content[VFS_MAX_FILE_SIZE];
    argv_join(content, sizeof(content), argc, argv, 2);
    int clen = (int)strlen(content);
    if (clen < (int)sizeof(content) - 1) {
        content[clen]     = '\n';
        content[clen + 1] = '\0';
    }
    int n = vfs_write(argv[1], (const uint8_t *)content, (uint32_t)strlen(content), false);
    if (n >= 0) printf("wrote %d B to '%s'\n", n, argv[1]);
}
 

static void cmd_iwrite(int argc, char *argv[]) {
    if (argc < 2) {
        printf("usage: iwrite <file>\n");
        printf("  Enter or paste lines, then type '.' alone to save.\n");
        printf("  Type '.abort' to cancel.\n");
        return;
    }
    interactive_write(argv[1]);
}

 
static void cmd_append(int argc, char *argv[]) {
    if (argc < 3) {
        printf("usage: append <file> <content...>\n");
        return;
    }
    char content[VFS_MAX_FILE_SIZE];
    argv_join(content, sizeof(content), argc, argv, 2);
    int clen = (int)strlen(content);
    if (clen < VFS_MAX_FILE_SIZE - 1) { content[clen] = '\n'; content[clen + 1] = '\0'; }
    int n = vfs_write(argv[1], (const uint8_t *)content, (uint32_t)strlen(content), true);
    if (n >= 0) printf("appended %d B to '%s'\n", n, argv[1]);
}
 
static void cmd_hexdump(int argc, char *argv[]) {
    if (argc < 2) { printf("usage: hexdump <file>\n"); return; }
    vfs_hexdump(argv[1]);
}
 
static void cmd_cd(int argc, char *argv[]) {
    const char *path = (argc >= 2) ? argv[1] : "/";
    if (vfs_cd(path)) printf("%s\n", vfs_cwd_path());
}
 
static void cmd_pwd(int argc, char *argv[]) {
    (void)argc; (void)argv;
    vfs_pwd();
}
 
static void cmd_cp(int argc, char *argv[]) {
    if (argc < 3) { printf("usage: cp <src> <dst>\n"); return; }
    vfs_cp(argv[1], argv[2]);
}
 
static void cmd_mv(int argc, char *argv[]) {
    if (argc < 3) { printf("usage: mv <src> <dst>\n"); return; }
    vfs_mv(argv[1], argv[2]);
}
 
static void cmd_stat(int argc, char *argv[]) {
    if (argc < 2) { printf("usage: stat <path>\n"); return; }
    for (int a = 1; a < argc; a++) {
        if (argc > 2) printf("==> %s\n", argv[a]);
        vfs_stat(argv[a]);
    }
}
 
static void cmd_wc(int argc, char *argv[]) {
    if (argc < 2) { printf("usage: wc <file> [file2 ...]\n"); return; }
    for (int a = 1; a < argc; a++) vfs_wc(argv[a]);
}
 
static void cmd_grep(int argc, char *argv[]) {
    if (argc < 3) { printf("usage: grep <pattern> <file>\n"); return; }
    vfs_grep(argv[2], argv[1]);
}
 
static void cmd_find(int argc, char *argv[]) {
    const char *name = (argc >= 2) ? argv[1] : "";
    vfs_find_all(name);
}
 
static void cmd_df(int argc, char *argv[]) {
    (void)argc; (void)argv;
    printf("=== VFS disk usage ===\n");
    vfs_df();
    printf("======================\n");
}
 
static void cmd_tree(int argc, char *argv[]) {
    (void)argc; (void)argv;
    vfs_tree();
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
    if      (strcmp(argv[2], "up")   == 0) { gpio_pull_up(pin);       printf("GPIO%d pull-up\n",          pin); }
    else if (strcmp(argv[2], "down") == 0) { gpio_pull_down(pin);     printf("GPIO%d pull-down\n",        pin); }
    else if (strcmp(argv[2], "none") == 0) { gpio_disable_pulls(pin); printf("GPIO%d pulls disabled\n",   pin); }
    else printf("unknown: %s\n", argv[2]);
}

static void cmd_led(int argc, char* argv[]) {
    static bool led_state = false;
    gpio_init(LED_PIN); gpio_set_dir(LED_PIN, GPIO_OUT);
    if (argc < 2) { printf("usage: led <on|off|toggle|blink [n]>\n"); return; }
    if      (strcmp(argv[1], "on")     == 0) { led_state = true;  gpio_put(LED_PIN, 1); printf("LED on\n"); }
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

    if (strcmp(argv[1], "irq") == 0) {
        int pin = atoi(argv[2]);
        if (pin < 0 || pin > 28) { printf("invalid pin\n"); return; }
        if (argc >= 4 && strcmp(argv[3], "stop") == 0) {
            gpio_mon_stop((uint8_t)pin);
            printf("stopped IRQ monitor on GPIO%d\n", pin);
        } else if (argc >= 4 && strcmp(argv[3], "dump") == 0) {
            gpio_mon_dump((uint8_t)pin);
        } else {
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

static void cmd_avg(int argc, char* argv[]) {
    if (argc < 2) { printf("usage: avg <adc_ch 0-2> [samples 1-256]\n"); return; }
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
    printf("ADC%d  samples=%d\n", ch, samples);
    printf("  avg  : %4d  /  %.3f V\n", avg_raw, voltage);
    printf("  min  : %4d  /  %.3f V\n", mn, mn * 3.3f / (1 << 12));
    printf("  max  : %4d  /  %.3f V\n", mx, mx * 3.3f / (1 << 12));
    printf("  noise: %4d  counts p-p\n", mx - mn);
}

static void cmd_i2c(int argc, char* argv[]) {
    if (argc < 2) {
        printf("usage:\n");
        printf("  i2c scan [sda] [scl]              - scan bus (default GP4 GP5)\n");
        printf("  i2c read  <addr> <reg>            - read one byte\n");
        printf("  i2c write <addr> <reg> <val>      - write one byte\n");
        printf("  i2c read  <addr> <reg> [sda] [scl]\n");
        printf("  i2c write <addr> <reg> <val> [sda] [scl]\n");
        return;
    }

    if (strcmp(argv[1], "scan") == 0) {
        // i2c scan [sda_pin] [scl_pin]
        uint sda = (argc >= 3) ? (uint)atoi(argv[2]) : 4;
        uint scl = (argc >= 4) ? (uint)atoi(argv[3]) : 5;

        if (sda > 28 || scl > 28) { printf("invalid pin\n"); return; }
        i2c_inst_t* bus;
        bool valid = false;
        if ((sda % 4 == 0) && (scl == sda + 1)) { bus = i2c0; valid = true; }
        else if ((sda % 4 == 2) && (scl == sda + 1)) { bus = i2c1; valid = true; }
        else {
            printf("warning: GP%d/GP%d may not be a valid I2C pair — trying anyway\n", sda, scl);
            bus = (sda < 8 || (sda >= 16 && sda < 24)) ? i2c0 : i2c1;
            valid = true;
        }

        i2c_init(bus, 100000);
        gpio_set_function(sda, GPIO_FUNC_I2C);
        gpio_set_function(scl, GPIO_FUNC_I2C);
        gpio_pull_up(sda);
        gpio_pull_up(scl);

          int bus_num = (bus == i2c0) ? 0 : 1;
   int found = 0;
printf("I2C%d scan (SDA=GP%d SCL=GP%d):\n", bus_num, sda, scl);
printf("     0  1  2  3  4  5  6  7  8  9  A  B  C  D  E  F\n");

for (int row = 0; row < 8; row++) {
    char line[64];
    int  pos = 0;
    pos += snprintf(line + pos, sizeof(line) - pos, "%02X: ", row * 16);

    for (int col = 0; col < 16; col++) {
        uint8_t addr = row * 16 + col;
        uint8_t rxdata;
        // I2C read is OUTSIDE any lock — interrupts must stay live
        int ret = i2c_read_timeout_us(bus, addr, &rxdata, 1, false, 2000);
        if (ret >= 0) {
            pos += snprintf(line + pos, sizeof(line) - pos, "%02X ", addr);
            found++;
        } else {
            pos += snprintf(line + pos, sizeof(line) - pos, "-- ");
        }
    }

    // print the completed row atomically
    print_lock();
    printf("%s\n", line);
    print_unlock();
}

print_lock();
printf("%d device(s) found\n", found);
print_unlock();

    } else if (strcmp(argv[1], "read") == 0) {
        if (argc < 4) { printf("usage: i2c read <addr_hex> <reg_hex> [sda] [scl]\n"); return; }
        uint8_t addr = (uint8_t)strtol(argv[2], NULL, 16);
        uint8_t reg  = (uint8_t)strtol(argv[3], NULL, 16);
        uint sda = (argc >= 5) ? (uint)atoi(argv[4]) : 4;
        uint scl = (argc >= 6) ? (uint)atoi(argv[5]) : 5;
        i2c_inst_t* bus = (sda % 4 == 0) ? i2c0 : i2c1;
        i2c_init(bus, 100000);
        gpio_set_function(sda, GPIO_FUNC_I2C);
        gpio_set_function(scl, GPIO_FUNC_I2C);
        gpio_pull_up(sda); gpio_pull_up(scl);
        uint8_t val = 0;
        i2c_write_timeout_us(bus, addr, &reg, 1, true, 2000);
        int ret = i2c_read_timeout_us(bus, addr, &val, 1, false, 2000);
        if (ret < 0) { printf("I2C error (no ACK?)\n"); return; }
        printf("0x%02X reg[0x%02X] = 0x%02X (%d)\n", addr, reg, val, val);

    } else if (strcmp(argv[1], "write") == 0) {
        if (argc < 5) { printf("usage: i2c write <addr_hex> <reg_hex> <val_hex> [sda] [scl]\n"); return; }
        uint8_t addr = (uint8_t)strtol(argv[2], NULL, 16);
        uint8_t reg  = (uint8_t)strtol(argv[3], NULL, 16);
        uint8_t val  = (uint8_t)strtol(argv[4], NULL, 16);
        uint sda = (argc >= 6) ? (uint)atoi(argv[5]) : 4;
        uint scl = (argc >= 7) ? (uint)atoi(argv[6]) : 5;
        i2c_inst_t* bus = (sda % 4 == 0) ? i2c0 : i2c1;
        i2c_init(bus, 100000);
        gpio_set_function(sda, GPIO_FUNC_I2C);
        gpio_set_function(scl, GPIO_FUNC_I2C);
        gpio_pull_up(sda); gpio_pull_up(scl);
        uint8_t buf[2] = { reg, val };
        int ret = i2c_write_timeout_us(bus, addr, buf, 2, false, 2000);
        if (ret < 0) { printf("I2C write failed\n"); return; }
        printf("wrote 0x%02X -> 0x%02X[0x%02X]\n", val, addr, reg);

    } else {
        printf("unknown i2c subcommand: %s\n", argv[1]);
    }
}

static void cmd_servo(int argc, char* argv[]) {
    if (argc < 2) {
        printf("usage:\n");
        printf("  servo <pin> <angle 0-180>             - set position\n");
        printf("  servo sweep <pin> [from to step_ms]   - blocking sweep\n");
        printf("  servo bg <pin> sweep [min max step step_ms] - background sweep\n");
        printf("  servo bg <pin> goto <angle> [step_ms] - background move to angle\n");
        printf("  servo bg <pin> stop                   - stop background servo\n");
        printf("  servo bg list                         - list background servos\n");
        return;
    }

    if (strcmp(argv[1], "bg") == 0) {
        if (argc < 3) { printf("servo bg: need subcommand\n"); return; }

        if (strcmp(argv[2], "list") == 0) {
            servo_bg_list();
            return;
        }

        int pin = atoi(argv[2]);
        if (pin < 0 || pin > 28) { printf("invalid pin\n"); return; }
        if (argc < 4) { printf("servo bg <pin> sweep|goto|stop\n"); return; }

        if (strcmp(argv[3], "sweep") == 0) {
            int min_deg  = (argc >= 5) ? atoi(argv[4]) : 0;
            int max_deg  = (argc >= 6) ? atoi(argv[5]) : 180;
            int step_deg = (argc >= 7) ? atoi(argv[6]) : 1;
            uint32_t step_ms = (argc >= 8) ? (uint32_t)atoi(argv[7]) : 20;
            int slot = servo_bg_add((uint8_t)pin, "bg-servo");
            if (slot < 0) return;
            servo_bg_set_sweep(slot, min_deg, max_deg, step_deg, step_ms);

        } else if (strcmp(argv[3], "goto") == 0) {
            if (argc < 5) { printf("servo bg <pin> goto <angle> [step_ms]\n"); return; }
            int target   = atoi(argv[4]);
            uint32_t sms = (argc >= 6) ? (uint32_t)atoi(argv[5]) : 10;
            int slot = servo_bg_add((uint8_t)pin, "bg-servo");
            if (slot < 0) return;
            servo_bg_set_goto(slot, target, sms);
            printf("servo bg: GPIO%d going to %d°\n", pin, target);

        } else if (strcmp(argv[3], "stop") == 0) {
            int slot = servo_bg_find((uint8_t)pin);
            if (slot < 0) { printf("no background servo on GPIO%d\n", pin); return; }
            servo_bg_stop(slot);

        } else {
            printf("unknown servo bg subcommand: %s\n", argv[3]);
        }
        return;
    }

    if (strcmp(argv[1], "sweep") == 0) {
        if (argc < 3) { printf("servo sweep <pin> [from to step_ms]\n"); return; }
        int pin     = atoi(argv[2]);
        int from    = (argc >= 4) ? atoi(argv[3]) : 0;
        int to      = (argc >= 5) ? atoi(argv[4]) : 180;
        int step_ms = (argc >= 6) ? atoi(argv[5]) : 20;
        if (pin < 0 || pin > 28) { printf("invalid pin\n"); return; }
        servo_sweep_blocking((uint8_t)pin, from, to, step_ms);
        return;
    }

    if (argc < 3) { printf("usage: servo <pin> <angle 0-180>\n"); return; }
    int pin   = atoi(argv[1]);
    int angle = atoi(argv[2]);
    if (pin < 0 || pin > 28)      { printf("invalid pin\n"); return; }
    if (angle < 0 || angle > 180) { printf("angle must be 0-180\n"); return; }
    servo_write_angle((uint8_t)pin, angle);
    printf("servo GPIO%d -> %d°\n", pin, angle);
}

static bool s_spi_inited = false;

static void cmd_spi(int argc, char* argv[]) {
    if (argc < 2) {
        printf("usage:\n");
        printf("  spi init [sck mosi miso baud]       - init SPI0 (default GP2/3/4 1MHz)\n");
        printf("  spi write <cs_pin> <hex bytes...>   - write bytes\n");
        printf("  spi read  <cs_pin> <reg_hex> [len]  - read register\n");
        printf("  spi xfer  <cs_pin> <hex bytes...>   - full-duplex transfer + print rx\n");
        return;
    }

    if (strcmp(argv[1], "init") == 0) {
        uint sck  = (argc >= 3) ? (uint)atoi(argv[2]) : SPI_DEFAULT_SCK;
        uint mosi = (argc >= 4) ? (uint)atoi(argv[3]) : SPI_DEFAULT_MOSI;
        uint miso = (argc >= 5) ? (uint)atoi(argv[4]) : SPI_DEFAULT_MISO;
        uint baud = (argc >= 6) ? (uint)atoi(argv[5]) : SPI_DEFAULT_BAUD;
        spi_bus_init(spi0, sck, mosi, miso, baud);
        s_spi_inited = true;
        return;
    }

    if (!s_spi_inited) {
        printf("SPI not initialised - run 'spi init' first\n");
        return;
    }

    if (strcmp(argv[1], "write") == 0 || strcmp(argv[1], "xfer") == 0) {
        if (argc < 4) { printf("spi %s <cs_pin> <hex bytes...>\n", argv[1]); return; }
        uint cs = (uint)atoi(argv[2]);
        bool do_rx = (strcmp(argv[1], "xfer") == 0);

        uint8_t tx[32], rx[32];
        int len = 0;
        for (int i = 3; i < argc && len < 32; i++)
            tx[len++] = (uint8_t)strtol(argv[i], NULL, 16);

        spi_bus_transfer(spi0, cs, tx, do_rx ? rx : NULL, (size_t)len);

        if (do_rx) {
            printf("rx: ");
            for (int i = 0; i < len; i++) printf("%02X ", rx[i]);
            printf("\n");
        } else {
            printf("wrote %d byte(s) to CS=GP%d\n", len, cs);
        }
        return;
    }

    if (strcmp(argv[1], "read") == 0) {
        if (argc < 4) { printf("spi read <cs_pin> <reg_hex> [len]\n"); return; }
        uint    cs  = (uint)atoi(argv[2]);
        uint8_t reg = (uint8_t)strtol(argv[3], NULL, 16);
        int     len = (argc >= 5) ? atoi(argv[4]) : 1;
        if (len < 1 || len > 31) len = 1;

        uint8_t tx[32] = {0};
        uint8_t rx[32] = {0};
        tx[0] = reg | 0x80;   // read bit for most sensors
        spi_bus_transfer(spi0, cs, tx, rx, (size_t)(len + 1));
        printf("reg 0x%02X: ", reg);
        for (int i = 1; i <= len; i++) printf("%02X ", rx[i]);
        printf("\n");
        return;
    }

    printf("unknown spi subcommand: %s\n", argv[1]);
}

static void cmd_uart(int argc, char* argv[]) {
    if (argc < 4) {
        printf("usage: uart <baud> <tx_pin> <rx_pin> [timeout_s]\n");
        printf("       bridges USB-CDC to UART; Ctrl-X to exit\n");
        printf("       UART0: TX=GP0/GP12/GP16, RX=GP1/GP13/GP17\n");
        printf("       UART1: TX=GP4/GP8,       RX=GP5/GP9\n");
        return;
    }
    uint baud    = (uint)atoi(argv[1]);
    uint tx_pin  = (uint)atoi(argv[2]);
    uint rx_pin  = (uint)atoi(argv[3]);
    uint32_t timeout_ms = (argc >= 5) ? (uint32_t)(atoi(argv[4]) * 1000) : 0;

    if (baud < 300 || baud > 921600) { printf("baud must be 300-921600\n"); return; }
    if (tx_pin > 28 || rx_pin > 28)  { printf("invalid pin\n"); return; }

    uart_inst_t* uart_port = uart0;
    if (tx_pin == 4 || tx_pin == 8 || tx_pin == 20 || tx_pin == 24)
        uart_port = uart1;

    uart_passthrough(uart_port, tx_pin, rx_pin, baud, timeout_ms);
}

static void cmd_top(int argc, char* argv[]) {
    printf("DeckOS top  (any key to exit)\n\n");

    while (true) {
        if (getchar_timeout_us(0) != PICO_ERROR_TIMEOUT) {
            printf("\ntop: exiting\n");
            break;
        }

        sched_task_t snap[SCHED_MAX_TASKS];
        uint64_t     totals[SCHED_MAX_TASKS];
        int          n        = sched_snapshot(snap, totals, SCHED_MAX_TASKS);
        uint64_t     grand    = sched_core1_total_us();
        if (grand == 0) grand = 1;

        printf("\033[2J\033[H");   // clear screen
        printf("=== DeckOS top ===  uptime: ");
        print_uptime();
        printf("\n\n");

        adc_select_input(4);
        float v  = adc_read() * 3.3f / (1 << 12);
        float tc = 27.0f - (v - 0.706f) / 0.001721f;
        printf("cpu: %lu MHz    temp: %.1f C    cmds: %lu\n\n",
               clock_get_hz(clk_sys) / 1000000, tc, s_cmd_count);

        printf("TASK            STATE    INTERVAL  CPU%%\n");
        printf("--------------- -------- --------- ------\n");

        // Shell (Core0) always shown first
        printf("%-15s %-8s %5s ms   --\n", "shell", "running", "-");

        for (int i = 0; i < n; i++) {
            uint32_t pct_x10 = (uint32_t)((totals[i] * 1000) / grand);
            printf("%-15s %-8s %5lu ms  %2lu.%lu%%\n",
                   snap[i].name,
                   snap[i].enabled ? "active" : "sleep",
                   snap[i].interval_ms,
                   pct_x10 / 10, pct_x10 % 10);
        }
        printf("\npress any key to stop\n");
        sleep_ms(500);
    }
}

static void cmd_bench(int argc, char* argv[]) {
    if (argc < 3) {
        printf("usage: bench <iterations> <command>\n");
        printf("       bench 1000 echo hi\n");
        return;
    }
    uint32_t iters = (uint32_t)atoi(argv[1]);
    if (iters < 1 || iters > 100000) { printf("iterations: 1-100000\n"); return; }

    char subcmd[INPUT_SIZE]; subcmd[0] = '\0';
    for (int i = 2; i < argc; i++) {
        if (i > 2) strncat(subcmd, " ", sizeof(subcmd) - strlen(subcmd) - 1);
        strncat(subcmd, argv[i], sizeof(subcmd) - strlen(subcmd) - 1);
    }

    printf("bench: running '%s' x%lu  (output suppressed)\n", subcmd, iters);

    bench_result_t r = bench_run(subcmd, iters);
    bench_print(&r, subcmd);
}

static void cmd_free(int argc, char* argv[]) {
    heap_track_print();
}

static void cmd_pinout(int argc, char* argv[]) {

    static const struct { int gp; const char* lbl; } left[20] = {
        {-1,"VBUS"},{0,""},{1,""},{-1,"GND"},{2,""},{3,""},{4,""},{5,""},{-1,"GND"},
        {6,""},{7,""},{8,""},{9,""},{-1,"GND"},{10,""},{11,""},{12,""},{13,""},{-1,"GND"},{14,""}
    };
    static const struct { int gp; const char* lbl; } right[20] = {
        {-1,"3V3"},{28,""},{27,""},{26,""},{-1,"VREF"},{22,""},{-1,"GND"},{21,""},{20,""},{19,""},
        {18,""},{-1,"GND"},{17,""},{16,""},{15,""},{-1,"RUN"},{-1,"GND"},{-1,"GND"},{-1,"GND"},{-1,"GND"}
    };

    printf("\n");
    printf("  .------------------------------------------------------.\n");
    printf("  |          Raspberry Pi Pico  --  DeckOS               |\n");
    printf("  |   .-----------.              .-----------.            |\n");
    printf("  |   |  RP2040   | Cortex-M0+  | 264KB SRAM|            |\n");
    printf("  |   '-----------'  125 MHz    '-----------'            |\n");
    printf("  |________________[USB]_________________________________  |\n");
    printf("  |                                                       |\n");
    printf("  |  val fn dir  label [pin]       [pin] label  dir fn val|\n");
    printf("  |  ---+--+---+-------+---+     +---+-------+---+--+---  |\n");

    for (int r = 0; r < 20; r++) {
        int lphy = r + 1;
        int rphy = 40 - r;

        char llabel[8] = "";
        char ldir = ' ', lfc = '-', lval = ' ';
        if (left[r].gp < 0) {
            snprintf(llabel, sizeof(llabel), "%s", left[r].lbl);
            lfc = ' ';
        } else {
            int g = left[r].gp;
            uint32_t f = gpio_get_function((uint)g);
            ldir = gpio_get_dir((uint)g) ? 'O' : 'I';
            lfc  = (f == GPIO_FUNC_PWM)  ? 'P' :
                   (f == GPIO_FUNC_I2C)  ? 'I' :
                   (f == GPIO_FUNC_SPI)  ? 'S' :
                   (f == GPIO_FUNC_UART) ? 'U' : '-';
            lval = gpio_get((uint)g) ? '*' : '.';
            snprintf(llabel, sizeof(llabel), "GP%-2d", g);
        }

        char rlabel[8] = "";
        char rdir = ' ', rfc = '-', rval = ' ';
        if (right[r].gp < 0) {
            snprintf(rlabel, sizeof(rlabel), "%s", right[r].lbl);
            rfc = ' ';
        } else {
            int g = right[r].gp;
            uint32_t f = gpio_get_function((uint)g);
            rdir = gpio_get_dir((uint)g) ? 'O' : 'I';
            rfc  = (f == GPIO_FUNC_PWM)  ? 'P' :
                   (f == GPIO_FUNC_I2C)  ? 'I' :
                   (f == GPIO_FUNC_SPI)  ? 'S' :
                   (f == GPIO_FUNC_UART) ? 'U' : '-';
            rval = gpio_get((uint)g) ? '*' : '.';
            snprintf(rlabel, sizeof(rlabel), "GP%-2d", g);
        }

        if (left[r].gp >= 0) {
            printf("  |  %c  %c  %c   %-4s  [%02d] |===| [%02d]  %-4s   %c  %c  %c  |\n",
                lval, lfc, ldir, llabel, lphy,
                rphy, rlabel,
                rdir, rfc, rval);
        } else {
            printf("  |            %-4s  [%02d] |===| [%02d]  %-4s             |\n",
                llabel, lphy,
                rphy, rlabel);
        }
    }

    printf("  |                                                       |\n");
    printf("  '-------------------------------------------------------'\n");
    printf("\n");
    printf("  val: * = HIGH   . = LOW\n");
    printf("  fn : P=PWM  I=I2C  S=SPI  U=UART  - =GPIO\n");
    printf("  dir: O=output  I=input\n\n");
}

static void cmd_piano(int argc, char* argv[]) {
    if (argc < 2) {
        printf("usage: piano <pin> [duration_ms]\n");
        printf("  connect passive buzzer to pin + GND\n");
        return;
    }
    int pin = atoi(argv[1]);
    if (pin < 0 || pin > 28) { printf("invalid pin\n"); return; }
    uint32_t dur = (argc >= 3) ? (uint32_t)atoi(argv[2]) : 150;
    if (dur < 20 || dur > 2000) dur = 150;

    static const uint32_t NOTE_TABLE[8][12] = {
        /* C0 */  { 16,  17,  18,  19,  21,  22,  23,  24,  26,  27,  29,  31},
        /* C1 */  { 33,  35,  37,  39,  41,  44,  46,  49,  52,  55,  58,  62},
        /* C2 */  { 65,  69,  73,  78,  82,  87,  92,  98, 104, 110, 117, 123},
        /* C3 */  {131, 139, 147, 156, 165, 175, 185, 196, 208, 220, 233, 247},
        /* C4 */  {262, 277, 294, 311, 330, 349, 370, 392, 415, 440, 466, 494},
        /* C5 */  {523, 554, 587, 622, 659, 698, 740, 784, 831, 880, 932, 988},
        /* C6 */  {1047,1109,1175,1245,1319,1397,1480,1568,1661,1760,1865,1976},
        /* C7 */  {2093,2217,2349,2489,2637,2794,2960,3136,3322,3520,3729,3951},
    };

    typedef struct { char key; int semi; int oct_offset; const char* name; } piano_key_t;

    static const piano_key_t keys[] = {
        /* White keys - bottom row */
        {'a',  0,  0, "C "},
        {'s',  2,  0, "D "},
        {'d',  4,  0, "E "},
        {'f',  5,  0, "F "},
        {'g',  7,  0, "G "},
        {'h',  9,  0, "A "},
        {'j', 11,  0, "B "},
        {'k',  0,  1, "C+"},
        {'l',  2,  1, "D+"},
        {';',  4,  1, "E+"},
        /* Black keys - top row */
        {'w',  1,  0, "C#"},
        {'e',  3,  0, "D#"},
        {'t',  6,  0, "F#"},
        {'y',  8,  0, "G#"},
        {'u', 10,  0, "A#"},
        {'o',  1,  1, "C#+"},
        {'p',  3,  1, "D#+"},
    };
    static const int num_keys = (int)(sizeof(keys) / sizeof(keys[0]));

    int octave = 4; /* base octave, 0-7 */

    printf("\n");
    printf("  .----------------------------------------------------.\n");
    printf("  |         DeckOS Piano   GPIO%02d   %lums/note         |\n", pin, dur);
    printf("  |----------------------------------------------------|  \n");
    printf("  |  Black:  W  E     T  Y  U     O  P  (sharps)      |\n");
    printf("  |  White: A  S  D  F  G  H  J  K  L  ;              |\n");
    printf("  |                                                    |\n");
    printf("  |  Octave: [  = down    ]  = up    (current: %d)     |\n", octave);
    printf("  |  Quit  : q                                        |\n");
    printf("  '----------------------------------------------------'\n");
    printf("\n  ready. base octave: %d\n\n", octave);

    while (true) {
        int c = getchar_timeout_us(10000);
        if (c == PICO_ERROR_TIMEOUT) continue;

        if (c == 'q' || c == 3) {
            printf("\n  piano: bye!\n");
            return;
        }

        if (c == '[') {
            if (octave > 0) octave--;
            printf("  octave: %d\n", octave);
            continue;
        }
        if (c == ']') {
            if (octave < 7) octave++;
            printf("  octave: %d\n", octave);
            continue;
        }

        int found = -1;
        for (int i = 0; i < num_keys; i++) {
            if ((char)c == keys[i].key) { found = i; break; }
        }

        if (found >= 0) {
            int oct = octave + keys[found].oct_offset;
            if (oct > 7) oct = 7;
            uint32_t hz = NOTE_TABLE[oct][keys[found].semi];

            /* Print visual keyboard */
            printf("  | ");
            static const char all_keys[] = "wsedtyhujokp";
            for (int i = 0; i < (int)sizeof(all_keys)-1; i++)
                printf("%c", (char)c == all_keys[i] ? '#' : '-');
            printf(" |  %s%d  %4lu Hz\n", keys[found].name, oct, hz);

            tone_play((uint8_t)pin, hz, dur);
        }
    }
}

static void cmd_flash(int argc, char* argv[]) {
    if (argc < 3) {
        printf("usage:\n");
        printf("  flash read  <addr_hex> <len>        - dump bytes from flash\n");
        printf("  flash write <addr_hex> <hex bytes>  - program bytes (addr must be page-aligned)\n");
        printf("  flash erase <addr_hex>              - erase 4 KB sector\n");
        printf("  note: addresses are XIP offsets (0x00000000 = start of flash)\n");
        printf("  config sector is at 0x%05X - do not erase it manually\n",
               (uint32_t)(2*1024*1024 - 4096));
        return;
    }

    uint32_t addr = (uint32_t)strtol(argv[2], NULL, 16);

    if (strcmp(argv[1], "read") == 0) {
        if (argc < 4) { printf("flash read <addr_hex> <len>\n"); return; }
        int len = atoi(argv[3]);
        if (len < 1 || len > 256) { printf("len 1-256\n"); return; }
        const uint8_t* ptr = (const uint8_t*)(XIP_BASE + addr);
        printf("flash[0x%05lX] +%d bytes:\n", addr, len);
        for (int i = 0; i < len; i++) {
            if (i % 16 == 0) printf("  %05lX: ", addr + i);
            printf("%02X ", ptr[i]);
            if (i % 16 == 15 || i == len-1) printf("\n");
        }
        return;
    }

    if (strcmp(argv[1], "erase") == 0) {
        // Align down to 4KB sector
        uint32_t sector = addr & ~(FLASH_SECTOR_SIZE - 1);
        printf("erasing sector at 0x%05lX ...\n", sector);
        multicore_reset_core1();
        uint32_t irq = save_and_disable_interrupts();
        flash_range_erase(sector, FLASH_SECTOR_SIZE);
        restore_interrupts(irq);
        extern void core1_restart(void);
        core1_restart();
        printf("done.\n");
        return;
    }

    if (strcmp(argv[1], "write") == 0) {
        if (argc < 4) { printf("flash write <addr_hex> <hex bytes...>\n"); return; }
        uint8_t buf[FLASH_PAGE_SIZE];
        memset(buf, 0xFF, sizeof(buf));
        int len = 0;
        for (int i = 3; i < argc && len < FLASH_PAGE_SIZE; i++)
            buf[len++] = (uint8_t)strtol(argv[i], NULL, 16);

        // addr must be page-aligned
        if (addr % FLASH_PAGE_SIZE != 0) {
            printf("write addr must be %d-byte page-aligned\n", FLASH_PAGE_SIZE);
            return;
        }
        printf("writing %d byte(s) to 0x%05lX ...\n", len, addr);
        multicore_reset_core1();
        uint32_t irq = save_and_disable_interrupts();
        flash_range_program(addr, buf, FLASH_PAGE_SIZE);
        restore_interrupts(irq);
        extern void core1_restart(void);
        core1_restart();
        printf("done.\n");
        return;
    }

    printf("unknown flash subcommand: %s\n", argv[1]);
}

/*static void cmd_detect(int argc, char* argv[]) {
    device_detect_print();
}*/

static void cmd_detect(int argc, char* argv[]) {
    cmd_detect_extended(argc, argv);
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
        printf("\033[2J\033[H");
        printf("--- watch [%lu] '%s' @ %d ms ---\n", ++iter, subcmd, ms);
        char tmp[INPUT_SIZE];
        strncpy(tmp, subcmd, sizeof(tmp) - 1);
        tmp[sizeof(tmp) - 1] = '\0';
        commands_execute(tmp);
        uint32_t waited = 0;
        while (waited < (uint32_t)ms) {
            if (getchar_timeout_us(0) != PICO_ERROR_TIMEOUT) { printf("\nwatch stopped.\n"); return; }
            sleep_ms(10);
            waited += 10;
        }
    }
}

static void cmd_edit(int argc, char* argv[]) {
    if (argc < 2) {
        printf("usage: edit <file>\n");
        printf("  opens file in built-in editor, creates if needed\n");
        printf("  example: edit test.ds\n");
        printf("  example: edit /home/blink.ds\n");
        return;
    }
    char path[64];
    if (argv[1][0] != '/') {
        const char* cwd = vfs_cwd_path();
        if (strcmp(cwd, "/") == 0)
            snprintf(path, sizeof(path), "/%s", argv[1]);
        else
            snprintf(path, sizeof(path), "%s/%s", cwd, argv[1]);
    } else {
        strncpy(path, argv[1], sizeof(path) - 1);
    }
    editor_run(path);
}

static void cmd_morse(int argc, char* argv[]) {
    if (argc < 2) {
        printf("usage: morse <text> [wpm]\n");
        printf("       blinks the onboard LED in morse code\n");
        return;
    }
    uint8_t wpm = (argc >= 3) ? (uint8_t)atoi(argv[2]) : 13;
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
    if (strcmp(argv[1], "warn")  == 0) { syslog_dump(LOG_WARN, 0); return; }
    if (strcmp(argv[1], "err")   == 0) { syslog_dump(LOG_ERR,  0); return; }
    if (strcmp(argv[1], "clear") == 0) { syslog_clear(); return; }
    if (strcmp(argv[1], "write") == 0 && argc >= 4) {
        syslog_write(LOG_INFO, argv[2], argv[3]);
        printf("logged.\n");
        return;
    }
    if (strcmp(argv[1], "stats") == 0) {
        printf("total entries written : %lu\n", syslog_total());
        return;
    }
    printf("usage:\n");
    printf("  syslog show [n]          - show log (last n entries)\n");
    printf("  syslog warn              - show WARN+ entries only\n");
    printf("  syslog err               - show ERR entries only\n");
    printf("  syslog write <tag> <msg> - add manual entry\n");
    printf("  syslog clear             - wipe the log\n");
    printf("  syslog stats             - show total count\n");
}

static void cmd_tone(int argc, char* argv[]) {
    if (argc < 3) {
        printf("usage: tone <pin> <note|hz> [duration_ms]\n");
        printf("       note examples: C4 G#3 A5 REST\n");
        printf("       hz   example : 440\n");
        printf("  passive buzzer: connect + to a GPIO, - to GND\n");
        return;
    }
    int pin = atoi(argv[1]);
    if (pin < 0 || pin > 28) { printf("invalid pin\n"); return; }
    uint32_t duration = (argc >= 4) ? (uint32_t)atoi(argv[3]) : 500;
    if (duration < 10 || duration > 10000) { printf("duration 10-10000 ms\n"); return; }
    uint32_t hz;
    if (isdigit((unsigned char)argv[2][0])) hz = (uint32_t)atoi(argv[2]);
    else                                    hz = tone_note_to_hz(argv[2]);
    printf("tone: GPIO%d  %lu Hz  %lu ms\n", pin, hz, duration);
    tone_play((uint8_t)pin, hz, duration);
}

typedef struct {
    int pin;
    int samples;
    int us_per_sample;
    int slot;          
} la_trigger_arg_t;
static la_trigger_arg_t s_la_arg;

static void la_trigger_job(void* arg) {
    la_trigger_arg_t* a = (la_trigger_arg_t*)arg;
    int pin           = a->pin;
    int samples       = a->samples;
    int us_per_sample = a->us_per_sample;

    uint8_t* buf = (uint8_t*)malloc((size_t)samples);
    if (!buf) { printf("[la-bg] out of memory\n"); return; }

    gpio_init(pin);
    gpio_set_dir(pin, GPIO_IN);

    print_lock();
    printf("[la-bg] GPIO%d armed, waiting for falling edge...\n", pin);
    print_unlock();

    absolute_time_t deadline = make_timeout_time_ms(5000);
while (!gpio_get(pin)) {
    if (bg_job_cancel_requested(a->slot)) { free(buf); return; }
    if (absolute_time_diff_us(get_absolute_time(), deadline) <= 0) {
            print_lock();
            printf("[la-bg] timeout waiting for idle HIGH\n");
            print_unlock();
            free(buf); return;
        }
        sleep_us(10);
    }

    print_lock();
    printf("[la-bg] idle HIGH confirmed, trigger ready\n");
    print_unlock();

    deadline = make_timeout_time_ms(30000);
while (gpio_get(pin)) {
    if (bg_job_cancel_requested(a->slot)) { free(buf); return; }
    if (absolute_time_diff_us(get_absolute_time(), deadline) <= 0) {
            print_lock();
            printf("[la-bg] timeout: no falling edge on GPIO%d in 30s\n", pin);
            printf("        tip: run 'i2c scan' to trigger\n");
            print_unlock();
            free(buf); return;
        }
    }

    print_lock();
    printf("[la-bg] triggered! sampling");
    print_unlock();

    for (int i = 0; i < samples; i++) {
        buf[i] = (uint8_t)gpio_get(pin);
        if (i % 32 == 0) {
            print_lock();
            printf(".");
            print_unlock();
        }
        sleep_us((uint32_t)us_per_sample);
    }

    int edges = 0, highs = 0;
    for (int i = 1; i < samples; i++) {
        if (buf[i] != buf[i-1]) edges++;
        if (buf[i]) highs++;
    }
    float duty      = (float)highs / (float)samples * 100.0f;
    float window_ms = (float)(samples * us_per_sample) / 1000.0f;

    print_lock();
    printf(" done.\n\n");

    printf("  1  ");
    int prev = buf[0];
    for (int i = 0; i < samples; i++) {
        int cur = buf[i];
        if (i == 0)                 printf(cur ? "_" : " ");
        else if (prev==0 && cur==1) printf("/");
        else if (prev==1 && cur==0) printf(" ");
        else if (cur==1)            printf("_");
        else                        printf(" ");
        prev = cur;
    }
    printf("\n     ");
    prev = buf[0];
    for (int i = 0; i < samples; i++) {
        int cur = buf[i];
        printf("%s", (i > 0 && prev != cur) ? "|" : " ");
        prev = cur;
    }
    printf("\n  0  ");
    prev = buf[0];
    for (int i = 0; i < samples; i++) {
        int cur = buf[i];
        if (i == 0)                 printf(cur ? " " : "_");
        else if (prev==1 && cur==0) printf("\\");
        else if (prev==0 && cur==1) printf(" ");
        else if (cur==0)            printf("_");
        else                        printf(" ");
        prev = cur;
    }
    printf("\n\n");

    printf("  edges    : %d\n", edges);
    printf("  duty     : %.1f%%\n", duty);
    printf("  window   : %.2f ms\n", window_ms);
    if (edges >= 2) {
        float period_ms = window_ms / ((float)edges / 2.1f);
        printf("  ~freq    : %.1f Hz\n", 1000.0f / period_ms);
    }
    printf("  trigger  : falling edge on GPIO%d\n", pin);
    print_unlock();

    free(buf);
}

static void cmd_la(int argc, char* argv[]) {
    if (argc < 2) {
        printf("usage:\n");
        printf("  la <pin> [samples] [us_per_sample] [trigger]\n");
        printf("  la <pin> 256 10          - free-running capture\n");
        printf("  la <pin> 256 2 trigger   - wait for falling edge then capture\n");
        printf("  press any key to abort\n");
        return;
    }

    int pin = atoi(argv[1]);
    if (pin < 0 || pin > 28) { printf("invalid pin\n"); return; }

    int samples       = (argc >= 3) ? atoi(argv[2]) : 128;
    int us_per_sample = (argc >= 4) ? atoi(argv[3]) : 10;
    bool do_trigger   = false;

    for (int i = 2; i < argc; i++) {
        if (strcmp(argv[i], "trigger") == 0) { do_trigger = true; break; }
    }

    if (samples < 8 || samples > 512)                { printf("samples: 8-512\n"); return; }
    if (us_per_sample < 1 || us_per_sample > 100000)  { printf("us_per_sample: 1-100000\n"); return; }

    gpio_init(pin);
    gpio_set_dir(pin, GPIO_IN);

    printf("LA: GPIO%d  %d samples @ %d us each  (%.2f ms window)\n",
           pin, samples, us_per_sample,
           (float)(samples * us_per_sample) / 1000.0f);

    if (do_trigger) {
        s_la_arg.pin           = pin;
        s_la_arg.samples       = samples;
        s_la_arg.us_per_sample = us_per_sample;
        int slot = bg_job_submit("la-trigger", la_trigger_job, &s_la_arg);
        s_la_arg.slot = slot;   
        printf("la: background trigger armed on GPIO%d\n", pin);
        printf("    wait for '[la-bg] idle HIGH confirmed' then run your command\n");
        printf("    timeout: 30s\n");
        return;
    }

    uint8_t* buf = (uint8_t*)malloc((size_t)samples);
    if (!buf) { printf("out of memory\n"); return; }

    printf("sampling");
    for (int i = 0; i < samples; i++) {
        if (getchar_timeout_us(0) != PICO_ERROR_TIMEOUT) {
            printf("\naborted.\n");
            free(buf);
            return;
        }
        buf[i] = (uint8_t)gpio_get(pin);
        if (i % 32 == 0) printf(".");
        sleep_us((uint32_t)us_per_sample);
    }
    printf(" done.\n\n");

    printf("     0");
    for (int i = 16; i < samples; i += 16)
        printf("%*d", 16, i);
    printf("\n     |");
    for (int i = 1; i < samples; i++)
        printf("%s", (i % 16 == 0) ? "|" : (i % 4 == 0) ? "." : " ");
    printf("\n");

    printf("  1  ");
    int prev = buf[0];
    for (int i = 0; i < samples; i++) {
        int cur = buf[i];
        if (i == 0)                     printf(cur ? "_" : " ");
        else if (prev == 0 && cur == 1) printf("/");
        else if (prev == 1 && cur == 0) printf(" ");
        else if (cur == 1)              printf("_");
        else                            printf(" ");
        prev = cur;
    }
    printf("\n     ");
    prev = buf[0];
    for (int i = 0; i < samples; i++) {
        int cur = buf[i];
        printf("%s", (i > 0 && prev != cur) ? "|" : " ");
        prev = cur;
    }
    printf("\n  0  ");
    prev = buf[0];
    for (int i = 0; i < samples; i++) {
        int cur = buf[i];
        if (i == 0)                     printf(cur ? " " : "_");
        else if (prev == 1 && cur == 0) printf("\\");
        else if (prev == 0 && cur == 1) printf(" ");
        else if (cur == 0)              printf("_");
        else                            printf(" ");
        prev = cur;
    }
    printf("\n\n");

    int edges = 0, highs = 0;
    for (int i = 1; i < samples; i++) {
        if (buf[i] != buf[i-1]) edges++;
        if (buf[i]) highs++;
    }
    float duty      = (float)highs / (float)samples * 100.0f;
    float window_ms = (float)(samples * us_per_sample) / 1000.0f;

    printf("  edges      : %d\n", edges);
    printf("  duty cycle : %.1f%%\n", duty);
    printf("  window     : %.2f ms\n", window_ms);
    if (edges >= 2) {
        float period_ms = window_ms / ((float)edges / 2.1f);
        printf("  ~freq      : %.1f Hz\n", 1000.0f / period_ms);
    }
    printf("\n");

    free(buf);
}

static void cmd_jobs(int argc, char* argv[]) {
    if (argc >= 3 && strcmp(argv[1], "cancel") == 0) {
        bg_job_cancel(atoi(argv[2]));
        return;
    }
    bg_job_list();
}

static void cmd_melody(int argc, char* argv[]) {
    if (argc < 3) {
        printf("usage: melody <pin> <C4:200 E4:200 G4:400 ...>\n");
        printf("       REST:100 for silence\n");
        printf("  passive buzzer: connect + to a GPIO, - to GND\n");
        return;
    }

    int pin = atoi(argv[1]);
    if (pin < 0 || pin > 28) { printf("invalid pin\n"); return; }

if (strcmp(argv[2], "elise") == 0) {
    static const char elise[] =
        "E5:160 D#5:160 E5:160 D#5:160 E5:160 B4:320 D5:320 C5:320 "
        "A4:640 REST:160 C4:160 E4:160 A4:160 "
        "B4:640 REST:160 E4:160 G#4:160 B4:160 "
        "C5:640 REST:160 E4:160 "
        "E5:160 D#5:160 E5:160 D#5:160 E5:160 B4:320 D5:320 C5:320 "
        "A4:640 REST:160 C4:160 E4:160 A4:160 "
        "B4:640 REST:160 E4:160 C5:160 B4:160 "
        "A4:960 REST:350 "

        "B4:320 C5:320 D5:320 "
        "E5:480 G4:160 F5:320 E5:320 "
        "D5:480 F4:160 E5:320 D5:320 "
        "C5:480 E4:160 D5:320 C5:320 "
        "B4:640 REST:160 "
        "B4:160 C5:160 D5:160 E5:160 "
        "G5:640 REST:160 "
        "F5:320 E5:320 D5:320 "
        "F5:480 A4:160 E5:320 D5:320 "
        "C5:480 E4:160 D5:320 C5:320 "
        "B4:640 REST:160 "
        "E4:160 E5:160 D#5:160 E5:160 "
        "D#5:160 E5:160 B4:320 D5:320 C5:320 "
        "A4:640 REST:160 C4:160 E4:160 A4:160 "
        "B4:640 REST:160 E4:160 C5:160 B4:160 "
        "A4:960 REST:350 "

        "E5:160 D#5:160 E5:160 D#5:160 E5:160 B4:320 D5:320 C5:320 "
        "A4:640 REST:160 C4:160 E4:160 A4:160 "
        "B4:640 REST:160 E4:160 G#4:160 B4:160 "
        "C5:640 REST:160 E4:160 "
        "E5:160 D#5:160 E5:160 D#5:160 E5:160 B4:320 D5:320 C5:320 "
        "A4:640 REST:160 C4:160 E4:160 A4:160 "
        "B4:640 REST:160 E4:160 C5:160 B4:160 "
        "A4:960 REST:350 "

        "C5:320 D5:320 "
        "E5:640 REST:160 E4:160 F#4:160 G#4:160 "
        "A4:640 REST:160 A4:160 B4:160 C5:160 "
        "D5:640 REST:160 "
        "D5:160 E5:160 F5:160 "
        "G5:640 REST:160 G4:160 A4:160 B4:160 "
        "C5:640 REST:160 C5:160 D5:160 E5:160 "
        "F5:640 REST:160 "
        "F5:160 E5:160 D5:160 "
        "E5:640 REST:160 E4:160 G#4:160 B4:160 "
        "E5:480 D#5:160 E5:160 D#5:160 E5:160 "
        "B4:320 D5:320 C5:320 "
        "A4:640 REST:160 C4:160 E4:160 A4:160 "
        "B4:640 REST:160 E4:160 C5:160 B4:160 "
        "A4:960 REST:400 "

        "E5:160 D#5:160 E5:160 D#5:160 E5:160 B4:320 D5:320 C5:320 "
        "A4:640 REST:160 C4:160 E4:160 A4:160 "
        "B4:640 REST:160 E4:160 G#4:160 B4:160 "
        "C5:640 REST:160 E4:160 "
        "E5:160 D#5:160 E5:160 D#5:160 E5:160 B4:320 D5:320 C5:320 "
        "A4:640 REST:160 C4:160 E4:160 A4:160 "
        "B4:640 REST:160 E4:160 C5:160 B4:160 "
        "A4:640 REST:160 "
        "E5:160 D#5:160 E5:160 B4:320 D5:320 "
        "C5:640 A4:640 "
        "E4:320 A4:320 C5:320 "
        "E5:1600";

    printf("melody on GPIO%d: [fur elise]\n", pin);
    tone_melody((uint8_t)pin, elise);
    return;
}

if (strcmp(argv[2], "canon") == 0) {
    static const char canon[] =
        "D5:800 "
        "C#5:800 "
        "B4:800 "
        "A4:800 "
        "G4:800 "
        "F#4:800 "
        "G4:800 "
        "E4:800 "
        "REST:300 "

        "D5:800 "
        "C#5:800 "
        "B4:800 "
        "A4:800 "
        "G4:800 "
        "F#4:800 "
        "G4:800 "
        "A4:800 "
        "REST:400 "

        "F#5:400 E5:400 "
        "D5:400 C#5:400 "
        "B4:400 A4:400 "
        "B4:400 C#5:400 "
        "REST:100 "

        "D5:400 C#5:400 "
        "B4:400 A4:400 "
        "G4:400 F#4:400 "
        "G4:400 A4:400 "
        "REST:100 "

        "D5:400 E5:400 "
        "F#5:400 E5:400 "
        "D5:400 B4:400 "
        "D5:600 C#5:200 "
        "REST:100 "

        "B4:400 C#5:400 "
        "D5:400 C#5:400 "
        "B4:400 A4:400 "
        "G4:800 "
        "REST:300 "

        "F#5:200 G5:200 A5:200 G5:200 "
        "F#5:200 E5:200 D5:200 E5:200 "
        "F#5:200 A4:200 B4:200 C#5:200 "
        "D5:200 E5:200 F#5:200 E5:200 "
        "REST:150 "

        "D5:200 E5:200 F#5:200 G5:200 "
        "A5:200 G5:200 F#5:200 E5:200 "
        "D5:200 F#5:200 E5:200 D5:200 "
        "C#5:600 B4:200 "
        "REST:150 "

        "A4:200 B4:200 C#5:200 D5:200 "
        "E5:200 D5:200 C#5:200 B4:200 "
        "C#5:200 E5:200 A4:200 B4:200 "
        "C#5:600 D5:200 "
        "REST:150 "

        "E5:200 D5:200 C#5:200 B4:200 "
        "A4:200 B4:200 C#5:200 D5:200 "
        "E5:200 F#5:200 G5:200 A5:200 "
        "B5:800 "
        "REST:300 "

        "F#5:150 E5:150 D5:150 C#5:150 "
        "B4:150 A4:150 G4:150 F#4:150 "
        "G4:150 A4:150 B4:150 C#5:150 "
        "D5:150 E5:150 F#5:150 G5:150 "
        "REST:120 "

        "A5:150 G5:150 F#5:150 E5:150 "
        "D5:150 C#5:150 B4:150 A4:150 "
        "B4:150 C#5:150 D5:150 E5:150 "
        "F#5:150 G5:150 A5:150 B5:150 "
        "REST:120 "

        "G5:150 F#5:150 E5:150 D5:150 "
        "C#5:150 B4:150 A4:150 G4:150 "
        "A4:150 B4:150 C#5:150 D5:150 "
        "E5:150 D5:150 C#5:150 B4:150 "
        "REST:120 "

        "C#5:150 D5:150 E5:150 F#5:150 "
        "G5:150 A5:150 B5:150 A5:150 "
        "G5:150 F#5:150 E5:150 D5:150 "
        "C#5:600 D5:200 "
        "REST:300 "

        "D5:250 F#5:250 A5:250 D6:250 "
        "A4:250 E5:250 A5:250 C#6:250 "
        "B4:250 D5:250 G5:250 B5:250 "
        "F#4:250 A4:250 D5:250 F#5:250 "
        "REST:150 "

        "G4:250 B4:250 D5:250 G5:250 "
        "D4:250 F#4:250 A4:250 D5:250 "
        "G4:250 B4:250 D5:250 G5:250 "
        "A4:250 C#5:250 E5:250 A5:250 "
        "REST:150 "

        "D5:300 E5:300 F#5:300 G5:300 "
        "A5:300 G5:300 F#5:300 E5:300 "
        "D5:300 C#5:300 B4:300 A4:300 "
        "B4:300 C#5:300 D5:300 E5:300 "
        "REST:150 "

        "F#5:300 G5:300 A5:300 B5:300 "
        "G5:300 A5:300 B5:300 C#6:300 "
        "D6:300 C#6:300 B5:300 A5:300 "
        "G5:600 F#5:200 E5:200 "
        "REST:300 "
        "D5:600 C#5:400 "
        "B4:600 A4:400 "
        "G4:600 F#4:400 "
        "G4:600 A4:400 "
        "REST:200 "

        "D5:600 E5:400 "
        "F#5:600 E5:400 "
        "D5:400 C#5:400 B4:400 "
        "A4:1000 "
        "REST:200 "

        "D5:500 C#5:300 B4:500 A4:300 "
        "G4:500 F#4:300 E4:500 F#4:300 "
        "G4:400 A4:400 B4:400 C#5:400 "
        "D5:2000";

    printf("melody on GPIO%d: [canon in d - full]\n", pin);
    tone_melody((uint8_t)pin, canon);
    return;
}
    char seq[2048]; seq[0] = '\0';
    for (int i = 2; i < argc; i++) {
        if (i > 2) strncat(seq, " ", sizeof(seq) - strlen(seq) - 1);
        strncat(seq, argv[i], sizeof(seq) - strlen(seq) - 1);
    }
    printf("melody on GPIO%d: %s\n", pin, seq);
    tone_melody((uint8_t)pin, seq);
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



typedef struct {
    bool active;
    absolute_time_t execute_at;
    char command[INPUT_SIZE];
} cron_job_t;

static cron_job_t cron_jobs[MAX_CRON_JOBS];

void cron_poll(void) {
    absolute_time_t now = get_absolute_time();

    for (int i = 0; i < MAX_CRON_JOBS; i++) {
        if (!cron_jobs[i].active)
            continue;

        if (absolute_time_diff_us(now, cron_jobs[i].execute_at) <= 0) {
            cron_jobs[i].active = false;

            char tmp[INPUT_SIZE];
            strncpy(tmp, cron_jobs[i].command, sizeof(tmp) - 1);
            tmp[sizeof(tmp) - 1] = '\0';

            printf("cron: running '%s'\n", tmp);
            kernel_enqueue_command(tmp); 
        }
    }
}
static void cmd_cron(int argc, char* argv[]) {
    if (argc < 3) {
        printf("usage: cron <delay_ms> <command>\n");
        return;
    }

    int ms = atoi(argv[1]);

    if (ms < 1 || ms > 60000) {
        printf("delay 1-60000 ms\n");
        return;
    }

    char subcmd[INPUT_SIZE];
    subcmd[0] = '\0';

    for (int i = 2; i < argc; i++) {
        if (i > 2)
            strncat(subcmd, " ", sizeof(subcmd) - strlen(subcmd) - 1);

        strncat(subcmd, argv[i], sizeof(subcmd) - strlen(subcmd) - 1);
    }

    for (int i = 0; i < MAX_CRON_JOBS; i++) {

        if (!cron_jobs[i].active) {

            cron_jobs[i].active = true;
            cron_jobs[i].execute_at = make_timeout_time_ms(ms);

            strncpy(cron_jobs[i].command,
                    subcmd,
                    sizeof(cron_jobs[i].command) - 1);

            cron_jobs[i].command[
                sizeof(cron_jobs[i].command) - 1
            ] = '\0';

            printf("cron: scheduled '%s' in %d ms\n",
                   subcmd,
                   ms);

            return;
        }
    }

    printf("cron: job queue full\n");
}
static void cmd_drivers(int argc, char* argv[]) { drivers_list(); }

static void cmd_tasks(int argc, char* argv[]) {
    if (argc >= 3 && strcmp(argv[1], "enable")  == 0) { sched_enable(atoi(argv[2]), true);  printf("task %d enabled\n",  atoi(argv[2])); return; }
    if (argc >= 3 && strcmp(argv[1], "disable") == 0) { sched_enable(atoi(argv[2]), false); printf("task %d disabled\n", atoi(argv[2])); return; }
    sched_list();
}

static void cmd_config(int argc, char* argv[]) {
    if (argc < 2 || strcmp(argv[1], "show") == 0) { config_print(&g_config); return; }
    if (strcmp(argv[1], "save")  == 0) { config_save(&g_config); return; }
    if (strcmp(argv[1], "reset") == 0) { config_defaults(&g_config); config_save(&g_config); printf("config reset to defaults\n"); return; }
    if (strcmp(argv[1], "set")   == 0 && argc >= 4) {
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

static command_t command_table[] = {
    // Core / info
    {"help",    cmd_help,    "show this command list"},
    {"version", cmd_version, "show OS version and build info"},
    {"clear",   cmd_clear,   "clear the terminal screen"},
    {"echo",    cmd_echo,    "echo <text>"},
    {"uptime",  cmd_uptime,  "show time since boot"},
    {"sysinfo", cmd_sysinfo, "full system info"},
    {"stats",   cmd_stats,   "runtime statistics"},
    {"top",     cmd_top,     "live task monitor (any key to exit)"},
    {"jobs", cmd_jobs, "jobs  list background jobs | jobs cancel <id>"},
    // Hardware
    {"temp",    cmd_temp,    "read internal core temperature"},
    {"mem",     cmd_mem,     "show memory info"},
    {"memmap",  cmd_memmap,  "detailed memory map"},
    {"free",    cmd_free,    "heap allocator stats and live allocs"},
    {"led",     cmd_led,     "led <on|off|toggle|blink [n]>"},
    {"gpio",    cmd_gpio,    "gpio <read|write|mode|irq> <pin> [val]"},
    {"pwm",     cmd_pwm,     "pwm <pin> <duty 0-100>"},
    {"adc",     cmd_adc,     "adc <ch 0-2>  raw ADC read"},
    {"avg",     cmd_avg,     "avg <ch> [samples]  averaged ADC read"},
    {"pull",    cmd_pull,    "pull <pin> <up|down|none>"},
    {"clock",   cmd_clock,   "clock [mhz]  get/set CPU freq (48-200)"},
    {"i2c", cmd_i2c, "i2c scan [sda scl] | read [sda scl] | write [sda scl]"},
    {"spi",     cmd_spi,     "spi init|write|read|xfer  SPI bus ops"},
    {"uart",    cmd_uart,    "uart <baud> <tx> <rx> [timeout_s]  passthrough"},
    {"pinout",  cmd_pinout,  "ASCII Pico pinout with live pin states"},
    {"flash",   cmd_flash,   "flash read|write|erase <addr> raw flash access"},
    {"detect",  cmd_detect,  "scan and report connected devices"},
    {"la",      cmd_la,     "la <pin> [samples] [us]  logic analyser + timing diagram"},
    {"imu", cmd_imu, "imu read|stream|attitude|calibrate|raw|whoami"},
    // Servo
    {"servo",   cmd_servo,   "servo <pin> <angle> | sweep | bg sweep/goto/stop"},
    // Audio / signalling
    {"tone",    cmd_tone,    "tone <pin> <note|hz> [ms]  (connect buzzer to pin+GND)"},
    {"melody",  cmd_melody,  "melody <pin> <C4:200 E4:200 ...>"},
    {"morse",   cmd_morse,   "morse <text> [wpm]  blink LED in morse"},
    {"piano",   cmd_piano,  "piano <pin>  play buzzer like a keyboard (a-k keys)"},
    // Scripting / automation
    {"sleep",   cmd_sleep,   "sleep <ms>  pause"},
    {"repeat",  cmd_repeat,  "repeat <n> <cmd>"},
    {"watch",   cmd_watch,   "watch <ms> <cmd>  run cmd at interval"},
    {"trigger", cmd_trigger, "trigger <pin> <rise|fall|both> <cmd>"},
    {"cron",    cmd_cron,    "cron <delay_ms> <cmd>  deferred run"},
    {"bench",   cmd_bench,   "bench <iters> <cmd>  measure cmd throughput"},
    // System
    {"reboot",  cmd_reboot,  "reboot via watchdog"},
    {"dfu",     cmd_dfu,     "reboot into USB DFU (BOOTSEL)"},
    {"uid",     cmd_uid,     "show unique board ID"},
    {"wdog",    cmd_wdog,    "show watchdog status"},
    {"pin",     cmd_pin,     "dump all GPIO pin states"},
    {"bt", cmd_bt, "bt shell|log|exec|top|send|recv|sniff|at|name|pin|baud|status"},
    {"wifi",        cmd_wifi,        "wifi init|status|ping|scan|join|ip|shell|deinit"},
    {"run",    cmd_run,    "run <file>  execute a DeckScript file from VFS"},
    {"script", cmd_script, "script run|test  DeckScript interpreter"},
    // Subsystems
    {"drivers", cmd_drivers, "list loaded drivers"},
    {"tasks",   cmd_tasks,   "list/enable/disable background tasks"},
    {"config",  cmd_config,  "config show|set|save|reset"},
    {"syslog",  cmd_syslog,  "syslog show|warn|err|write|clear|stats"},
    // Filesystem
    {"ls",       cmd_ls,       "ls [path]  list directory"},
    {"cat",      cmd_cat,      "cat <file>  print file contents"},
    {"touch",    cmd_touch,    "touch <file>  create empty file (or update mtime)"},
    {"mkdir",    cmd_mkdir,    "mkdir <dir>  create directory"},
    {"rm",       cmd_rm,       "rm [-r] <path>  remove file or directory"},
    {"write",    cmd_write,    "write <file> <text>  write (overwrite) text to file"},
    {"iwrite", cmd_iwrite, "iwrite <file>  interactive multi-line write (end with '.')"},
    {"append",   cmd_append,   "append <file> <text>  append text to file"},
    {"hexdump",  cmd_hexdump,  "hexdump <file>  hex + ASCII dump"},
    {"cd",       cmd_cd,       "cd [dir]  change directory"},
    {"pwd",      cmd_pwd,      "pwd  print working directory"},
    {"cp",       cmd_cp,       "cp <src> <dst>  copy file"},
    {"mv",       cmd_mv,       "mv <src> <dst>  move / rename"},
    {"stat",     cmd_stat,     "stat <path>  file / dir metadata"},
    {"edit",   cmd_edit,   "edit <file>  nano-style text editor"},
    {"wc",       cmd_wc,       "wc <file>  count lines, words, bytes"},
    {"grep",     cmd_grep,     "grep <pattern> <file>  search file"},
    {"find",     cmd_find,     "find [name]  recursive name search"},
    {"df",       cmd_df,       "df  filesystem usage summary"},
    {"tree",     cmd_tree,     "tree  print directory tree"},

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

    char  buf[INPUT_SIZE];
    char* argv[MAX_ARGS];
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
