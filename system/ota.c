#include <stdio.h>
#include <string.h>
#include "pico/stdlib.h"
#include "hardware/flash.h"
#include "hardware/watchdog.h"
#include "hardware/sync.h"
#include "ota.h"

#define OTA_PAGE_SIZE 256
static uint8_t ota_page_buf[OTA_PAGE_SIZE] __attribute__((aligned(4)));

bool ota_pending(uint32_t *out_size) {
    uint32_t magic = *(volatile uint32_t *)(XIP_BASE + OTA_STAGING_BASE + OTA_MAGIC_OFFSET);
    if (magic != OTA_MAGIC_VAL) return false;
    uint32_t sz = *(volatile uint32_t *)(XIP_BASE + OTA_STAGING_BASE + OTA_SIZE_OFFSET);
    if (sz < 256 || sz > OTA_STAGING_SIZE - 16) return false;
    if (out_size) *out_size = sz;
    return true;
}

// Runs entirely from RAM via __not_in_flash_func.
// Erases the main firmware area and copies from staging.
// Never returns — issues a watchdog reset.
__attribute__((noreturn))
__not_in_flash_func(static void ota_apply_impl)(uint32_t fw_size) {
    uint32_t ints = save_and_disable_interrupts();

    // Erase entire main firmware area (flash offset 0 to staging base)
    flash_range_erase(0, OTA_STAGING_BASE);

    // Copy from staging to main flash in 256-byte pages
    // flash_range_program requires a RAM buffer, so copy each page from XIP first
    uint32_t staging_data = OTA_STAGING_BASE + 16; // skip 16-byte header area
    for (uint32_t off = 0; off < fw_size; off += OTA_PAGE_SIZE) {
        uint32_t chunk = (fw_size - off < OTA_PAGE_SIZE) ? (fw_size - off) : OTA_PAGE_SIZE;
        // Copy source from staging (XIP) into RAM buffer
        memcpy(ota_page_buf, (const void *)(XIP_BASE + staging_data + off), chunk);
        // Pad remainder with 0xFF (erase value)
        if (chunk < OTA_PAGE_SIZE)
            memset(ota_page_buf + chunk, 0xFF, OTA_PAGE_SIZE - chunk);
        flash_range_program(off, ota_page_buf, OTA_PAGE_SIZE);
    }

    // Clear the OTA magic so next boot boots normally
    // Erase the sector containing the magic footer
    uint32_t magic_sector = OTA_STAGING_BASE + OTA_MAGIC_OFFSET;
    magic_sector &= ~(FLASH_SECTOR_SIZE - 1);
    flash_range_erase(magic_sector, FLASH_SECTOR_SIZE);
    // After erase the sector is all 0xFF, so magic won't match OTA_MAGIC_VAL

    restore_interrupts(ints);

    // Reset to boot from new firmware
    watchdog_reboot(0, 0, 0);
    while (1);
}

void ota_apply(uint32_t fw_size) {
    ota_apply_impl(fw_size);
}
