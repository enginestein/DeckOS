cmake_minimum_required(VERSION 3.13)
include($ENV{PICO_SDK_PATH}/external/pico_sdk_import.cmake)
project(DeckOS C CXX ASM)
set(PICO_BOARD pico)
pico_sdk_init()

include_directories(include)
add_executable(DeckOS
main.c
kernel/kernel.c
kernel/bootloader.c
kernel/config.c
kernel/scheduler.c
kernel/syslog.c
kernel/gpio_mon.c
kernel/morse.c
kernel/tone.c
kernel/vfs.c
kernel/bg_job.c
kernel/file_persist.c
kernel/module.c
shell/shell.c
shell/editor.c
commands/commands.c
commands/cmd_imu.c
system/bench.c
system/device_detect.c
system/heap_track.c
system/print_lock.c
system/board_detect.c
system/oled_console.c
scripting/dscript.c
usb/usb_descriptors.c
usb/fat_disk.c
usb/msc_disk.c
usb/usb_hid.c
commands/cmd_usb.c
hardware/servo.c
communication/uart_pass.c
communication/bt.c
communication/uart_detect.c
communication/esp.c
drivers/drivers.c
drivers/spi_bus.c
drivers/mpu6050.c
drivers/oled.c
)

pico_set_linker_script(DeckOS ${CMAKE_CURRENT_SOURCE_DIR}/memmap_deckos.ld)

target_compile_definitions(DeckOS PRIVATE
    PICO_STACK_SIZE=0x4000
    PICO_CORE1_STACK_SIZE=0x1000
    PICO_STDIO_USB_USE_DEFAULT_DESCRIPTORS=0
    PICO_STDIO_USB_ENABLE_RESET_VIA_VENDOR_INTERFACE=0
)

target_link_libraries(DeckOS
pico_stdlib
pico_multicore
hardware_adc
hardware_gpio
hardware_watchdog
hardware_pwm
hardware_clocks
hardware_i2c
hardware_spi
hardware_flash
hardware_sync
hardware_irq
hardware_rtc
pico_unique_id
pico_bootrom
pico_rand
m
)
pico_enable_stdio_usb(DeckOS 1)
pico_enable_stdio_uart(DeckOS 0)
pico_add_extra_outputs(DeckOS)