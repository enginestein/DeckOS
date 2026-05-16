#pragma once
#include <stdbool.h>
#include <stdint.h>

typedef enum {
    BOOT_NORMAL   = 0,
    BOOT_RECOVERY, 
    BOOT_DFU,       
} boot_mode_t;

boot_mode_t bootloader_run(void);

// Print the boot mode string
const char* bootloader_mode_str(boot_mode_t m);