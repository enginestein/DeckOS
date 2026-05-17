#pragma once
#include <stdint.h>
#include <stdbool.h>

// Known I2C device signatures
typedef struct {
    uint8_t     addr;
    const char* name;
    const char* description;
} i2c_device_sig_t;

// Known SPI device detection (CS-pin based probe)
typedef struct {
    uint8_t     whoami_reg;
    uint8_t     whoami_val;
    const char* name;
} spi_device_sig_t;

// Detected device entry
typedef struct {
    char    bus[8];
    char    name[24];
    char    detail[48];
    uint8_t addr_or_pin;
} detected_device_t;

#define MAX_DETECTED 32

int  device_detect_all(detected_device_t* out, int max);
void device_detect_print(void);