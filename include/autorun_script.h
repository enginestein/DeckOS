#pragma once

static const char AUTORUN_SCRIPT[] =
    "# DeckOS autorun\n"
    "gpio_write 25 1\n"
    "sleep 200\n"
    "gpio_write 25 0\n"
    "print Focus any text window!\n"
    "sleep 5000\n"
    "hid line DeckOS v9.0 - USB Automation\n"
    "hid line\n"
    "hid line Typed automatically by a $4\n"
    "hid line Raspberry Pi Pico running DeckOS.\n"
    "hid line USB: HID + serial + mass storage\n"
    "hid line I/O: gpio i2c spi pwm adc temp\n"
    "hid line Shell: 90+ cmds, VFS, DeckScript\n"
    "hid line Serial 115200. Type help.\n"
    "repeat 3\n"
    "  gpio_write 25 1\n"
    "  sleep 80\n"
    "  gpio_write 25 0\n"
    "  sleep 80\n"
    "endrepeat\n"
    "gpio_write 25 1\n";
