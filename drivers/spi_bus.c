#include <stdio.h>
#include <string.h>
#include "pico/stdlib.h"
#include "hardware/spi.h"
#include "hardware/gpio.h"
#include "spi_bus.h"

void spi_bus_init(spi_inst_t* spi, uint sck, uint mosi, uint miso, uint baud) {
    spi_init(spi, baud);
    gpio_set_function(sck,  GPIO_FUNC_SPI);
    gpio_set_function(mosi, GPIO_FUNC_SPI);
    gpio_set_function(miso, GPIO_FUNC_SPI);
    printf("[spi] init %s  SCK=GP%d MOSI=GP%d MISO=GP%d  @%lu Hz\n",
           spi == spi0 ? "SPI0" : "SPI1", sck, mosi, miso, (uint32_t)baud);
}

void spi_bus_deinit(spi_inst_t* spi) {
    spi_deinit(spi);
}

int spi_bus_transfer(spi_inst_t* spi, uint cs_pin,
                     const uint8_t* tx, uint8_t* rx, size_t len) {
    if (!len) return 0;

    bool manage_cs = (cs_pin != 0xFF);
    if (manage_cs) {
        gpio_init(cs_pin);
        gpio_set_dir(cs_pin, GPIO_OUT);
        gpio_put(cs_pin, 0);
    }

    int rc;
    if (rx) {
        rc = spi_write_read_blocking(spi, tx, rx, len);
    } else {
        static uint8_t dummy[256];
        size_t chunk = len < sizeof(dummy) ? len : sizeof(dummy);
        rc = spi_write_read_blocking(spi, tx, dummy, chunk);
    }

    if (manage_cs)
        gpio_put(cs_pin, 1);   // deassert CS

    return rc;
}

void spi_bus_write_reg(spi_inst_t* spi, uint cs_pin, uint8_t reg, uint8_t val) {
    uint8_t tx[2] = { reg & 0x7F, val };   // MSB=0 => write for most sensors
    spi_bus_transfer(spi, cs_pin, tx, NULL, 2);
}

uint8_t spi_bus_read_reg(spi_inst_t* spi, uint cs_pin, uint8_t reg) {
    uint8_t tx[2] = { reg | 0x80, 0x00 };  // MSB=1 => read for most sensors
    uint8_t rx[2] = {0};
    spi_bus_transfer(spi, cs_pin, tx, rx, 2);
    return rx[1];
}