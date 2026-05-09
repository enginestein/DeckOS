#include <stdio.h>
#include <string.h>
#include "hardware/flash.h"
#include "hardware/sync.h"
#include "pico/stdlib.h"
#include "config.h"

#define FLASH_SIZE_BYTES   (2 * 1024 * 1024)
#define CONFIG_OFFSET      (FLASH_SIZE_BYTES - FLASH_SECTOR_SIZE)
#define CONFIG_ADDR        (XIP_BASE + CONFIG_OFFSET)

static uint32_t crc32(const uint8_t* data, size_t len) {
    uint32_t crc = 0xFFFFFFFF;
    for (size_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (int b = 0; b < 8; b++)
            crc = (crc >> 1) ^ (0xEDB88320 & -(crc & 1));
    }
    return ~crc;
}

static uint32_t config_crc(const flash_config_t* cfg) {
    // Everything except the crc32 field itself
    size_t len = offsetof(flash_config_t, crc32);
    return crc32((const uint8_t*)cfg, len);
}

void config_defaults(flash_config_t* cfg) {
    memset(cfg, 0, sizeof(*cfg));
    cfg->magic        = CONFIG_MAGIC;
    cfg->version      = CONFIG_VERSION;
    cfg->boot_cpu_mhz = 0;
    cfg->boot_led     = 0;
    cfg->shell_echo   = 1;
    strncpy(cfg->hostname, "pico", sizeof(cfg->hostname) - 1);
    cfg->crc32        = config_crc(cfg);
}

bool config_load(flash_config_t* cfg) {
    const flash_config_t* flash = (const flash_config_t*)CONFIG_ADDR;

    if (flash->magic != CONFIG_MAGIC || flash->version != CONFIG_VERSION) {
        config_defaults(cfg);
        return false;
    }
    memcpy(cfg, flash, sizeof(*cfg));
    uint32_t expected = config_crc(cfg);
    if (cfg->crc32 != expected) {
        printf("[config] CRC mismatch — loading defaults\n");
        config_defaults(cfg);
        return false;
    }
    return true;
}

void config_save(flash_config_t* cfg) {
    cfg->magic   = CONFIG_MAGIC;
    cfg->version = CONFIG_VERSION;
    cfg->crc32   = config_crc(cfg);

    static uint8_t page[FLASH_SECTOR_SIZE];
    memset(page, 0xFF, sizeof(page));
    memcpy(page, cfg, sizeof(*cfg));

    uint32_t irq = save_and_disable_interrupts();
    flash_range_erase(CONFIG_OFFSET, FLASH_SECTOR_SIZE);
    flash_range_program(CONFIG_OFFSET, page, FLASH_SECTOR_SIZE);
    restore_interrupts(irq);

    printf("[config] saved to flash @ 0x%05X\n", CONFIG_OFFSET);
}

void config_print(const flash_config_t* cfg) {
    printf("hostname    : %s\n",   cfg->hostname);
    printf("boot_cpu_mhz: %lu  (%s)\n",
        cfg->boot_cpu_mhz,
        cfg->boot_cpu_mhz ? "custom" : "default 125 MHz");
    printf("boot_led    : %s\n",   cfg->boot_led    ? "on"  : "off");
    printf("shell_echo  : %s\n",   cfg->shell_echo  ? "yes" : "no");
    printf("crc32       : 0x%08lX\n", cfg->crc32);
}