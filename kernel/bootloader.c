#include <stdio.h>
#include <string.h>
#include <inttypes.h>
#include "pico/stdlib.h"
#include "pico/bootrom.h"
#include "pico/unique_id.h"
#include "hardware/gpio.h"
#include "hardware/adc.h"
#include "hardware/watchdog.h"
#include "hardware/clocks.h"
#include "hardware/flash.h"
#include "bootloader.h"
#include "config.h"
#include "ota.h"
#include "board_detect.h"

#define WD_SCRATCH_REG  0
#define WD_DFU_COOKIE   0xDFDFDFDF
#define LED_PIN         25

flash_config_t g_config;


#define MAX_STAGES 16
static struct {
    const char *name;
    uint64_t    start_us;
    uint64_t    elapsed_us;
    bool        ok;
} s_stages[MAX_STAGES];
static int s_stage_count = 0;

static void stage_begin(const char *name) {
    if (s_stage_count >= MAX_STAGES) return;
    s_stages[s_stage_count].name      = name;
    s_stages[s_stage_count].start_us  = time_us_64();
    s_stages[s_stage_count].elapsed_us = 0;
    s_stages[s_stage_count].ok        = false;
    printf("  %-28s ... ", name);
    fflush(stdout);
}

static void stage_end(bool ok) {
    if (s_stage_count >= MAX_STAGES) return;
    s_stages[s_stage_count].elapsed_us = time_us_64() - s_stages[s_stage_count].start_us;
    s_stages[s_stage_count].ok         = ok;
    const char *mark = ok ? "[ OK ]" : "[FAIL]";
    uint64_t ms = s_stages[s_stage_count].elapsed_us / 1000;
    uint64_t us = s_stages[s_stage_count].elapsed_us % 1000;
    printf("%s  (%"PRIu64".%03"PRIu64" ms)\n", mark, ms, us);
    s_stage_count++;
}


static boot_mode_t detect_mode(void) {
    if (watchdog_caused_reboot() &&
            watchdog_hw->scratch[WD_SCRATCH_REG] == WD_DFU_COOKIE) {
        return BOOT_DFU;
    }
    if (watchdog_caused_reboot() &&
            watchdog_hw->scratch[WD_SCRATCH_REG] == 0xDEAD0001) {
        return BOOT_RECOVERY;
    }
    return BOOT_NORMAL;
}

const char* bootloader_mode_str(boot_mode_t m) {
    switch (m) {
        case BOOT_NORMAL:   return "normal";
        case BOOT_RECOVERY: return "recovery";
        case BOOT_DFU:      return "dfu";
        default:            return "unknown";
    }
}

static float read_temp_c(void) {
    adc_init();
    adc_set_temp_sensor_enabled(true);
    adc_select_input(4);
    uint16_t raw = adc_read();
    float v = raw * 3.3f / (1 << 12);
    return 27.0f - (v - 0.706f) / 0.001721f;
}

static void print_uid(void) {
    pico_unique_board_id_t id;
    pico_get_unique_board_id(&id);
    printf("  uid       : ");
    for (int i = 0; i < 8; i++) printf("%02x", id.id[i]);
    printf("\n");
}

static void print_chip_info(void) {
    board_info_t board = board_detect();
    printf("  model     : %s\n", board.name);
    printf("  cpu       : %"PRIu32" MHz\n", board.cpu_mhz);
    printf("  cores     : 2 (Cortex-M0+)\n");
    printf("  flash     : %"PRIu32" KB\n", board.flash_kb);
    printf("  sram      : %"PRIu32" KB\n", board.sram_kb);
    printf("  temp      : %.1f °C\n", read_temp_c());
    print_uid();
    printf("  reset     : %s\n",
           watchdog_caused_reboot() ? "watchdog" : "power-on");
}

static void print_inventory(void) {
    printf("\n  ── Hardware inventory ──\n");
    printf("  ADC   : 4 x 12-bit (GP26-GP29)\n");
    printf("  I2C   : bus 0 (GP4 SDA, GP5 SCL) @ 100 kHz\n");
    printf("  SPI   : SPI0 (GP2 SCK, GP3 MOSI, GP4 MISO)\n");
    printf("  UART  : UART1 (GP5 TX, GP4 RX)\n");
    printf("  PWM   : 8 x PWM slices, 16 channels\n");
    printf("  PIO   : 2 x PIO blocks, 8 state machines\n");
    printf("  USB   : CDC + MSC + HID composite\n");
}

static void print_mem_summary(void) {
    extern char __StackLimit;
    extern char __bss_end__;
    uint32_t heap_used = (uint32_t)((char*)&__StackLimit - (char*)&__bss_end__);
    printf("\n  ── Memory ──\n");
    printf("  SRAM total : 264 KB\n");
    printf("  heap       : %"PRIu32" KB (%"PRIu32" B)\n",
           heap_used / 1024, heap_used);
}

static void print_banner(boot_mode_t mode) {
    printf("\n");
    printf("  ╔══════════════════════════════════════════════╗\n");
    printf("  ║              DeckOS  v10                    ║\n");
    printf("  ║          RP2040 Port  (Pico SDK)             ║\n");
    printf("  ║            Author: Enginestein               ║\n");
    printf("  ╚══════════════════════════════════════════════╝\n");
    printf("\n");
    printf("  hostname  : %s\n", g_config.hostname[0] ? g_config.hostname : "(none set)");
    printf("  mode      : %s\n", bootloader_mode_str(mode));
    printf("\n");
}

static void apply_config(void) {
    if (g_config.boot_cpu_mhz >= 48 && g_config.boot_cpu_mhz <= 200) {
        set_sys_clock_khz(g_config.boot_cpu_mhz * 1000, false);
    }
    if (g_config.boot_led) {
        gpio_init(LED_PIN);
        gpio_set_dir(LED_PIN, GPIO_OUT);
        gpio_put(LED_PIN, 1);
    }
}

boot_mode_t bootloader_run(void) {
    uint64_t boot_start = time_us_64();

    stage_begin("check OTA");
    uint32_t ota_size = 0;
    bool ota_found = ota_pending(&ota_size);
    stage_end(true);
    if (ota_found) {
        printf("\n  *** OTA update pending (%"PRIu32" bytes) ***\n", ota_size);
        printf("  applying...\n");
        fflush(stdout);
        ota_apply(ota_size);
    }

    stage_begin("load config");
    bool had_valid = config_load(&g_config);
    if (!had_valid) {
        printf("[defaults] ");
        config_defaults(&g_config);
    }
    stage_end(true);

    stage_begin("init GPIO");
    gpio_init(LED_PIN);
    stage_end(true);

    stage_begin("init ADC");
    adc_init();
    adc_set_temp_sensor_enabled(true);
    stage_end(true);

    stage_begin("apply config");
    apply_config();
    stage_end(true);

    boot_mode_t mode = detect_mode();

    if (mode == BOOT_DFU) {
        printf("\n  *** USB DFU bootloader ***\n");
        sleep_ms(200);
        reset_usb_boot(0, 0);
    }

    uint64_t total_us = time_us_64() - boot_start;
    uint64_t total_ms = total_us / 1000;
    uint64_t rem_us   = total_us % 1000;

    printf("\n  ── SoC info ──\n");
    print_chip_info();

    print_inventory();
    print_mem_summary();

    print_banner(mode);

    printf("  ── Boot stages (%d) ──\n", s_stage_count);
    for (int i = 0; i < s_stage_count; i++)
        printf("  [%s] %s  (%"PRIu64".%03"PRIu64" ms)\n",
               s_stages[i].ok ? "OK" : "!!",
               s_stages[i].name,
               s_stages[i].elapsed_us / 1000,
               s_stages[i].elapsed_us % 1000);

    printf("\n  Boot complete in %"PRIu64".%03"PRIu64" ms\n", total_ms, rem_us);
    printf("\n");

    if (mode == BOOT_RECOVERY) {
        printf("  *** RECOVERY MODE ***\n\n");
    }

    return mode;
}
