#include <stdio.h>
#include <string.h>
#include "pico/stdlib.h"
#include "pico/bootrom.h"
#include "hardware/gpio.h"
#include "hardware/watchdog.h"
#include "hardware/clocks.h"
#include "bootloader.h"
#include "config.h"

#define RECOVERY_PIN  15
#define WD_SCRATCH_REG  0
#define WD_DFU_COOKIE   0xDFDFDFDF
flash_config_t g_config;

static boot_mode_t detect_mode(void) {
    if (watchdog_caused_reboot() &&
        watchdog_hw->scratch[WD_SCRATCH_REG] == WD_DFU_COOKIE) {
        return BOOT_DFU;
    }

    // Recovery pin held low
    gpio_init(RECOVERY_PIN);
    gpio_set_dir(RECOVERY_PIN, GPIO_IN);
    gpio_pull_up(RECOVERY_PIN);
    sleep_ms(5);   // debounce
    if (!gpio_get(RECOVERY_PIN)) return BOOT_RECOVERY;

    return BOOT_NORMAL;
}

static void apply_config(void) {
    if (g_config.boot_cpu_mhz >= 48 && g_config.boot_cpu_mhz <= 200) {
        set_sys_clock_khz(g_config.boot_cpu_mhz * 1000, false);
        printf("[boot] cpu set to %lu MHz\n", g_config.boot_cpu_mhz);
    }
    if (g_config.boot_led) {
        gpio_init(25);
        gpio_set_dir(25, GPIO_OUT);
        gpio_put(25, 1);
    }
}

static void print_banner(boot_mode_t mode) {
    printf("\n");
    printf("  ╔══════════════════════════════════╗\n");
    printf("  ║           DeckOS v1.7            ║\n");
    printf("  ║           Built: %s              ║\n", __DATE__);
    printf("  ╚══════════════════════════════════╝\n");
    printf("  mode   : %s\n", bootloader_mode_str(mode));
    printf("  host   : %s\n", g_config.hostname);
    printf("\n");
}


boot_mode_t bootloader_run(void) {
    bool had_valid = config_load(&g_config);
    if (!had_valid)
        printf("[boot] flash config blank — using defaults\n");

    boot_mode_t mode = detect_mode();

    if (mode == BOOT_DFU) {
        printf("[boot] entering USB DFU bootloader...\n");
        sleep_ms(200);
        reset_usb_boot(0, 0);  
    }

    apply_config();
    print_banner(mode);

    if (mode == BOOT_RECOVERY) {
        printf("  *** RECOVERY MODE — only built-in commands available ***\n\n");
    }

    return mode;
}

const char* bootloader_mode_str(boot_mode_t m) {
    switch (m) {
        case BOOT_NORMAL:   return "normal";
        case BOOT_RECOVERY: return "recovery";
        case BOOT_DFU:      return "dfu";
        default:            return "unknown";
    }
}