#pragma once
#include <stdbool.h>
#include <stdint.h>

// Boot modes detected at startup
typedef enum {
    BOOT_NORMAL   = 0,  // Regular boot
    BOOT_RECOVERY,      // GPIO held low at reset -> recovery shell
    BOOT_DFU,          // Two watchdog resets in a row -> drop into USB bootloader
} boot_mode_t;

// Run the pre-shell bootloader stage.
// Detects boot mode, prints banner, applies saved config.
// Returns the detected boot mode (shell should check for BOOT_RECOVERY).
boot_mode_t bootloader_run(void);

// Print the boot mode string
const char* bootloader_mode_str(boot_mode_t m);