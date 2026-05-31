#include "board_detect.h"
#include "hardware/clocks.h"
#include "hardware/flash.h"
#include "pico/unique_id.h"
#include "pico/stdlib.h"
#include <string.h>
#include <stdio.h>

static uint32_t probe_flash_size_kb(void) {
    static const uint32_t probe_sizes_kb[] = {
        512, 1024, 2048, 4096, 8192, 16384
    };
    static const int num_probes =
        (int)(sizeof(probe_sizes_kb) / sizeof(probe_sizes_kb[0]));

    const uint8_t *base = (const uint8_t *)XIP_BASE;
    uint8_t sig[4];
    for (int i = 0; i < 4; i++) sig[i] = base[i];

    for (int p = 0; p < num_probes; p++) {
        uint32_t offset = probe_sizes_kb[p] * 1024;
        bool mirror = true;
        for (int i = 0; i < 4; i++) {
            if (base[offset + i] != sig[i]) {
                mirror = false;
                break;
            }
        }
        if (mirror) return probe_sizes_kb[p];
    }

    return 16384;
}

static const char *guess_board_name(uint32_t flash_kb) {
    switch (flash_kb) {
        case 512:   return "RP2040 (512KB flash)";
        case 1024:  return "RP2040 (1MB flash)";
        case 2048:  return "Raspberry Pi Pico / Pico W";
        case 4096:  return "RP2040 (4MB) -- Waveshare Plus / Feather";
        case 8192:  return "RP2040 (8MB) -- Adafruit Feather / Pimoroni";
        case 16384: return "RP2040 (16MB) -- SparkFun / Arduino Nano";
        default:    return "RP2040 (unknown board)";
    }
}

board_info_t board_detect(void) {
    board_info_t info;
    info.flash_kb = probe_flash_size_kb();
    info.sram_kb  = 264;   // RP2040: always 264KB, die-fixed
    info.cpu_mhz  = clock_get_hz(clk_sys) / 1000000;
    info.name     = guess_board_name(info.flash_kb);
    return info;
}