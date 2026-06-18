#pragma once
#include <stdint.h>
#include <stdbool.h>
#include "pico/stdlib.h"

// Flash layout (2MB total):
//   0x10000000 – staging_base : Main firmware
//   staging_base – 0x10200000 : OTA staging area
//
// The last 4 bytes of the staging area hold OTA_MAGIC if an update is pending.
// The 4 bytes before that hold the firmware size.

#define FLASH_TOTAL_SIZE      (PICO_FLASH_SIZE_BYTES)
#define OTA_STAGING_SIZE      (512 * 1024)       // 512 KB staging
#define OTA_STAGING_BASE      (FLASH_TOTAL_SIZE - OTA_STAGING_SIZE)
#define OTA_MAGIC_OFFSET      (OTA_STAGING_SIZE - 4)
#define OTA_SIZE_OFFSET       (OTA_STAGING_SIZE - 8)
#define OTA_MAGIC_VAL         0xDEADBEEF

#define OTA_FLASH_OFFS(addr)  ((uint32_t)(addr) - XIP_BASE)

// Return true if a pending OTA update is detected in the staging area.
// *out_size is set to the firmware size.
bool ota_pending(uint32_t *out_size);

// Apply the pending OTA update: erase main firmware area and copy staging data.
// This function runs from RAM (__not_in_flash_func) and never returns –
// it issues a watchdog reset after the copy completes.
void ota_apply(uint32_t fw_size) __attribute__((noreturn));
