#include <stdio.h>
#include <string.h>
#include "pico/stdlib.h"
#include "hardware/i2c.h"
#include "hardware/adc.h"
#include "hardware/gpio.h"
#include "hardware/pwm.h"
#include "hardware/clocks.h"
#include "device_detect.h"


static const i2c_device_sig_t known_i2c[] = {
    {0x3C, "SSD1306",   "OLED display (128x64)"},
    {0x3D, "SSD1306",   "OLED display (128x32)"},
    {0x48, "ADS1115",   "16-bit ADC"},
    {0x49, "ADS1115",   "16-bit ADC (ADDR=VCC)"},
    {0x4A, "ADS1115",   "16-bit ADC (ADDR=SDA)"},
    {0x4B, "ADS1115",   "16-bit ADC (ADDR=SCL)"},
    {0x57, "AT24C32",   "EEPROM (DS3231 module)"},
    {0x68, "DS3231",    "RTC real-time clock"},
    {0x68, "MPU6050",   "6-axis IMU (same addr as DS3231)"},
    {0x69, "MPU6050",   "6-axis IMU (ADDR pin high)"},
    {0x76, "BMP280",    "pressure/temperature sensor"},
    {0x77, "BMP280",    "pressure/temperature (SDO high)"},
    {0x77, "MS5611",    "barometric pressure sensor"},
    {0x23, "BH1750",    "ambient light sensor"},
    {0x5C, "BH1750",    "ambient light sensor (ADDR high)"},
    {0x29, "VL53L0X",   "ToF distance sensor"},
    {0x40, "INA219",    "current/power monitor"},
    {0x41, "INA219",    "current/power monitor (A0=1)"},
    {0x44, "SHT31",     "humidity/temperature sensor"},
    {0x45, "SHT31",     "humidity/temperature (ADDR high)"},
    {0x60, "MCP4725",   "12-bit DAC"},
    {0x70, "TCA9548A",  "I2C multiplexer"},
    {0x20, "PCF8574",   "I/O expander"},
    {0x27, "PCF8574",   "LCD I2C backpack (typical)"},
    {0x1E, "HMC5883L",  "magnetometer"},
    {0x0D, "QMC5883L",  "magnetometer"},
    {0x10, "VEML7700",  "ambient light sensor"},
    {0x53, "ADXL345",   "accelerometer"},
    {0x1C, "MMA8452",   "3-axis accelerometer"},
    {0x18, "LIS3DH",    "3-axis accelerometer"},
};
static const int known_i2c_count = sizeof(known_i2c) / sizeof(known_i2c[0]);

static const char* i2c_lookup(uint8_t addr) {
    for (int i = 0; i < known_i2c_count; i++)
        if (known_i2c[i].addr == addr) return known_i2c[i].name;
    return NULL;
}
static const char* i2c_desc(uint8_t addr) {
    for (int i = 0; i < known_i2c_count; i++)
        if (known_i2c[i].addr == addr) return known_i2c[i].description;
    return "unknown I2C device";
}


static int detect_i2c(detected_device_t* out, int max, int* count) {
    int found = 0;
    for (uint8_t addr = 0x08; addr < 0x78 && *count < max; addr++) {
        uint8_t rxdata;
        int ret = i2c_read_timeout_us(i2c0, addr, &rxdata, 1, false, 2000);
        if (ret >= 0) {
            detected_device_t* d = &out[(*count)++];
            strncpy(d->bus, "I2C0", 7);
            const char* nm = i2c_lookup(addr);
            strncpy(d->name, nm ? nm : "unknown", 23);
            snprintf(d->detail, sizeof(d->detail), "addr=0x%02X  %s", addr, i2c_desc(addr));
            d->addr_or_pin = addr;
            found++;
        }
    }
    return found;
}

static int detect_gpio_devices(detected_device_t* out, int max, int* count) {
    int found = 0;
    for (int pin = 0; pin <= 28 && *count < max; pin++) {
        uint func = gpio_get_function(pin);
        if (func == GPIO_FUNC_PWM) {
            uint slice = pwm_gpio_to_slice_num((uint)pin);
            if (pwm_hw->slice[slice].csr & PWM_CH0_CSR_EN_BITS) {
                detected_device_t* d = &out[(*count)++];
                strncpy(d->bus, "PWM", 7);
                strncpy(d->name, "PWM output", 23);
                snprintf(d->detail, sizeof(d->detail),
                         "slice=%d  likely servo/buzzer/LED", slice);
                d->addr_or_pin = (uint8_t)pin;
                found++;
            }
        }
    }
    return found;
}

static int detect_adc(detected_device_t* out, int max, int* count) {
    int found = 0;
    const int adc_pins[] = {26, 27, 28};
    for (int i = 0; i < 3 && *count < max; i++) {
        adc_select_input((uint)i);
        uint16_t raw = adc_read();
        float v = raw * 3.3f / (1 << 12);
        if (v > 0.05f) {
            detected_device_t* d = &out[(*count)++];
            strncpy(d->bus, "ADC", 7);
            snprintf(d->name, 24, "ADC ch%d (GP%d)", i, adc_pins[i]);
            snprintf(d->detail, sizeof(d->detail),
                     "%.3f V  (raw=%d) - sensor/pot/signal present", v, raw);
            d->addr_or_pin = (uint8_t)adc_pins[i];
            found++;
        }
    }
    return found;
}

int device_detect_all(detected_device_t* out, int max) {
    int count = 0;
    detect_i2c(out, max, &count);
    detect_gpio_devices(out, max, &count);
    detect_adc(out, max, &count);
    return count;
}

void device_detect_print(void) {
    detected_device_t devices[MAX_DETECTED];
    printf("scanning for connected devices...\n");

    int n = device_detect_all(devices, MAX_DETECTED);

    adc_select_input(4);
    uint16_t raw = adc_read();
    float v  = raw * 3.3f / (1 << 12);
    float tc = 27.0f - (v - 0.706f) / 0.001721f;
    printf("\n  [INTERNAL]  temp sensor         core temp = %.1f C\n", tc);

    if (n == 0) {
        printf("  no external devices detected\n\n");
        printf("  tips:\n");
        printf("    I2C devices : connect to SDA=GP4, SCL=GP5\n");
        printf("    ADC devices : connect to GP26 (ch0), GP27 (ch1), GP28 (ch2)\n");
        printf("    Servo/buzzer: will show once a PWM slice is enabled (use 'servo' or 'tone')\n");
        return;
    }

    printf("\n  BUS       NAME                   DETAIL\n");
    printf("  --------- ---------------------- -------------------------------------------\n");
    for (int i = 0; i < n; i++) {
        printf("  %-9s %-22s %s\n",
               devices[i].bus, devices[i].name, devices[i].detail);
    }
    printf("\n%d device(s) found.\n", n);
    printf("(I2C scan: SDA=GP4 SCL=GP5 / ADC threshold >0.05 V)\n");
}