#pragma once
#include <stdint.h>
#include <stdbool.h>

// Persisted system configuration, stored in the last 4 KB of flash.
// Layout: [magic 4B][version 2B][config struct][crc32 4B]

#define CONFIG_MAGIC   0xDEC0CAFE
#define CONFIG_VERSION 1

typedef struct __attribute__((packed)) {
    uint32_t magic;
    uint16_t version;

    // -- add your own fields below this line --
    uint32_t boot_cpu_mhz;       // 0 = use default (125)
    uint8_t  boot_led;           // 1 = LED on at boot
    uint8_t  shell_echo;         // 1 = echo input (default)
    char     hostname[16];       // board name shown in prompt
    uint8_t  _reserved[32];      // room to grow without bumping version

    uint32_t crc32;
} flash_config_t;

// Load config from flash into *cfg; fills defaults if flash is blank/corrupt.
// Returns true if flash had valid data.
bool  config_load(flash_config_t* cfg);

// Erase flash sector and write *cfg (also recomputes crc32 field).
void  config_save(flash_config_t* cfg);

// Print human-readable config dump
void  config_print(const flash_config_t* cfg);

// Fill *cfg with factory defaults (does NOT write to flash)
void  config_defaults(flash_config_t* cfg);