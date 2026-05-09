#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "pico/stdlib.h"
#include "pico/time.h"
#include "hardware/adc.h"
#include "hardware/gpio.h"
#include "hardware/watchdog.h"
#include "hardware/pwm.h"
#include "commands.h"
#include "kernel.h"
#include "pico/unique_id.h"
#include "hardware/clocks.h"

#define LED_PIN 25

static void print_uptime() {
    uint64_t us = time_us_64();
    uint32_t s  = (uint32_t)(us / 1000000);
    printf("%02uh %02um %02us", s / 3600, (s % 3600) / 60, s % 60);
}

static void cmd_help(int argc, char* argv[]) {
    commands_list();
}

static void cmd_version(int argc, char* argv[]) {
    printf("DeckOS v1.0.0  |  Raspberry Pi Pico\n");
    printf("Build: %s %s\n", __DATE__, __TIME__);
}

static void cmd_clear(int argc, char* argv[]) {
    printf("\033[2J\033[H");
}

static void cmd_echo(int argc, char* argv[]) {
    for (int i = 1; i < argc; i++) {
        printf("%s%s", argv[i], (i < argc - 1) ? " " : "");
    }
    printf("\n");
}

static void cmd_uptime(int argc, char* argv[]) {
    printf("uptime: ");
    print_uptime();
    printf("\n");
}

static void cmd_temp(int argc, char* argv[]) {
    adc_select_input(4);
    uint16_t raw   = adc_read();
    float voltage  = raw * 3.3f / (1 << 12);
    float temp_c   = 27.0f - (voltage - 0.706f) / 0.001721f;
    float temp_f   = temp_c * 9.0f / 5.0f + 32.0f;
    printf("core temp: %.1f C  /  %.1f F\n", temp_c, temp_f);
}

static void cmd_mem(int argc, char* argv[]) {
    extern char __StackLimit, __bss_end__;
    uint32_t heap = (uint32_t)(&__StackLimit - &__bss_end__);
    printf("heap available : ~%lu bytes  (~%lu KB)\n", heap, heap / 1024);
    printf("total SRAM     : 264 KB\n");
    printf("flash          : 2 MB\n");
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
        printf(" %-3d  %-4s  %d\n", i,
            dir ? "OUT" : "IN", val);
    }
}

static void cmd_sysinfo(int argc, char* argv[]) {
    printf("=================================\n");
    printf("  DeckOS v1.0.0  —  system info   \n");
    printf("=================================\n");
    printf("board   : Raspberry Pi Pico\n");
    printf("cpu     : RP2040  dual-core ARM Cortex-M0+  125 MHz\n");
    printf("ram     : 264 KB SRAM\n");
    printf("flash   : 2 MB\n");
    printf("uptime  : "); print_uptime(); printf("\n");
    adc_select_input(4);
    uint16_t raw  = adc_read();
    float v       = raw * 3.3f / (1 << 12);
    float tc      = 27.0f - (v - 0.706f) / 0.001721f;
    printf("temp    : %.1f C\n", tc);
    printf("=================================\n");
}

static void cmd_pwm(int argc, char* argv[]) {
    if (argc < 3) {
        printf("usage: pwm <pin> <duty 0-100>\n");
        return;
    }
    int pin  = atoi(argv[1]);
    int duty = atoi(argv[2]);
    if (pin < 0 || pin > 28)        { printf("invalid pin %d\n", pin); return; }
    if (duty < 0 || duty > 100)     { printf("duty must be 0-100\n"); return; }

    gpio_set_function(pin, GPIO_FUNC_PWM);
    uint slice = pwm_gpio_to_slice_num(pin);
    pwm_set_wrap(slice, 999);
    pwm_set_gpio_level(pin, duty * 10);  // 0-100 maps to 0-1000
    pwm_set_enabled(slice, true);
    printf("PWM on GPIO%-2d  duty=%d%%\n", pin, duty);
}

static void cmd_clock(int argc, char* argv[]) {
    if (argc < 2) {
        uint32_t freq = clock_get_hz(clk_sys) / 1000000;
        printf("cpu clock: %lu MHz\n", freq);
        return;
    }
    int mhz = atoi(argv[1]);
    if (mhz < 48 || mhz > 200) {
        printf("safe range: 48-200 MHz\n");
        return;
    }
    set_sys_clock_khz(mhz * 1000, false);
    printf("cpu clock set to %d MHz\n", mhz);
}

static void cmd_pull(int argc, char* argv[]) {
    if (argc < 3) {
        printf("usage: pull <pin> <up|down|none>\n");
        return;
    }
    int pin = atoi(argv[1]);
    if (pin < 0 || pin > 28) { printf("invalid pin\n"); return; }
    gpio_init(pin);
    if (strcmp(argv[2], "up") == 0) {
        gpio_pull_up(pin);
        printf("GPIO%d pull-up enabled\n", pin);
    } else if (strcmp(argv[2], "down") == 0) {
        gpio_pull_down(pin);
        printf("GPIO%d pull-down enabled\n", pin);
    } else if (strcmp(argv[2], "none") == 0) {
        gpio_disable_pulls(pin);
        printf("GPIO%d pulls disabled\n", pin);
    } else {
        printf("unknown: %s  (use up|down|none)\n", argv[2]);
    }
}

static void cmd_led(int argc, char* argv[]) {
    static bool led_state = false;
    gpio_init(LED_PIN);
    gpio_set_dir(LED_PIN, GPIO_OUT);

    if (argc < 2) {
        printf("usage: led <on|off|toggle|blink [n]>\n");
        return;
    }
    if (strcmp(argv[1], "on") == 0) {
        led_state = true;
        gpio_put(LED_PIN, 1);
        printf("LED on\n");
    } else if (strcmp(argv[1], "off") == 0) {
        led_state = false;
        gpio_put(LED_PIN, 0);
        printf("LED off\n");
    } else if (strcmp(argv[1], "toggle") == 0) {
        led_state = !led_state;
        gpio_put(LED_PIN, led_state);
        printf("LED %s\n", led_state ? "on" : "off");
    } else if (strcmp(argv[1], "blink") == 0) {
        int n = (argc >= 3) ? atoi(argv[2]) : 5;
        if (n < 1 || n > 50) { printf("blink count must be 1-50\n"); return; }
        printf("blinking %d times...\n", n);
        for (int i = 0; i < n; i++) {
            gpio_put(LED_PIN, 1); sleep_ms(150);
            gpio_put(LED_PIN, 0); sleep_ms(150);
        }
        printf("done\n");
    } else {
        printf("unknown led subcommand: %s\n", argv[1]);
    }
}

static void cmd_gpio(int argc, char* argv[]) {
    if (argc < 3) {
        printf("usage:\n");
        printf("  gpio read  <pin>\n");
        printf("  gpio write <pin> <0|1>\n");
        return;
    }

    int pin = atoi(argv[2]);
    if (pin < 0 || pin > 28) {
        printf("invalid pin %d  (valid range: 0-28)\n", pin);
        return;
    }

    if (strcmp(argv[1], "read") == 0) {
        gpio_init(pin);
        gpio_set_dir(pin, GPIO_IN);
        printf("GPIO%-2d = %d\n", pin, gpio_get(pin) ? 1 : 0);

    } else if (strcmp(argv[1], "write") == 0) {
        if (argc < 4) { printf("usage: gpio write <pin> <0|1>\n"); return; }
        int val = atoi(argv[3]);
        gpio_init(pin);
        gpio_set_dir(pin, GPIO_OUT);
        gpio_put(pin, val ? 1 : 0);
        printf("GPIO%-2d <- %d\n", pin, val ? 1 : 0);

    } else if (strcmp(argv[1], "mode") == 0) {
        if (argc < 4) { printf("usage: gpio mode <pin> <in|out>\n"); return; }
        gpio_init(pin);
        if (strcmp(argv[3], "in") == 0) {
            gpio_set_dir(pin, GPIO_IN);
            printf("GPIO%-2d set to INPUT\n", pin);
        } else if (strcmp(argv[3], "out") == 0) {
            gpio_set_dir(pin, GPIO_OUT);
            printf("GPIO%-2d set to OUTPUT\n", pin);
        } else {
            printf("unknown mode: %s  (use 'in' or 'out')\n", argv[3]);
        }

    } else {
        printf("unknown gpio subcommand: %s\n", argv[1]);
    }
}

static void cmd_adc(int argc, char* argv[]) {
    if (argc < 2) {
        printf("usage: adc <0|1|2>  (GPIO26, GPIO27, GPIO28)\n");
        return;
    }
    int ch = atoi(argv[1]);
    if (ch < 0 || ch > 2) { printf("channel must be 0, 1, or 2\n"); return; }
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

    char subcmd[INPUT_SIZE];
    subcmd[0] = '\0';
    for (int i = 2; i < argc; i++) {
        if (i > 2) strncat(subcmd, " ", sizeof(subcmd) - strlen(subcmd) - 1);
        strncat(subcmd, argv[i], sizeof(subcmd) - strlen(subcmd) - 1);
    }
    for (int i = 0; i < n; i++) {
        char tmp[INPUT_SIZE];
        strncpy(tmp, subcmd, sizeof(tmp));
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

static command_t command_table[] = {
    {"help",    cmd_help,    "show this command list"},
    {"version", cmd_version, "show OS version and build info"},
    {"clear",   cmd_clear,   "clear the terminal screen"},
    {"echo",    cmd_echo,    "echo <text>  - print text"},
    {"uptime",  cmd_uptime,  "show time elapsed since boot"},
    {"sysinfo", cmd_sysinfo, "show full system information"},
    {"temp",    cmd_temp,    "read internal core temperature"},
    {"mem",     cmd_mem,     "show memory info"},
    {"led",     cmd_led,     "led <on|off|toggle|blink [n]>"},
    {"gpio",    cmd_gpio,    "gpio <read|write|mode> <pin> [val]"},
    {"pwm",     cmd_pwm,     "pwm <pin> <duty 0-100>  - set PWM duty cycle"},
    {"adc",     cmd_adc,     "adc <ch>  - read ADC ch 0-2 (GPIO26-28)"},
    {"sleep",   cmd_sleep,   "sleep <ms>  - pause execution"},
    {"repeat",  cmd_repeat,  "repeat <n> <cmd>  - run cmd n times"},
    {"reboot",  cmd_reboot,  "reboot the device via watchdog"},
    {"pull",    cmd_pull,    "pull <pin> <up|down|none>  - set GPIO pull resistor"},
    {"clock",   cmd_clock,  "clock [mhz]  - get/set CPU frequency (48-200 MHz)"},
    {"uid",     cmd_uid,    "show unique board ID"},
    {"wdog",    cmd_wdog,   "show watchdog status"},
    {"pin",     cmd_pin,    "dump state of all GPIO pins"},
};

static const int command_count =
    sizeof(command_table) / sizeof(command_t);

void commands_init() {
    printf("[commands] %d commands registered\n", command_count);
}

void commands_list() {
    printf("available commands:\n");
    for (int i = 0; i < command_count; i++) {
        printf("  %-10s - %s\n",
            command_table[i].name,
            command_table[i].description);
    }
}

void commands_execute(char* input) {
    if (!input || strlen(input) == 0) return;

    static char buf[128];
    static char* argv[MAX_ARGS];
    int argc = 0;

    strncpy(buf, input, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';

    char* tok = strtok(buf, " ");
    while (tok && argc < MAX_ARGS) {
        argv[argc++] = tok;
        tok = strtok(NULL, " ");
    }
    if (argc == 0) return;

    for (int i = 0; i < command_count; i++) {
        if (strcmp(argv[0], command_table[i].name) == 0) {
            command_table[i].handler(argc, argv);
            return;
        }
    }

    printf("unknown command: '%s'  (type 'help' for list)\n", argv[0]);
}