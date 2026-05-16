cmake_minimum_required(VERSION 3.13)
include($ENV{PICO_SDK_PATH}/external/pico_sdk_import.cmake)

project(DeckOS C CXX ASM)
set(PICO_BOARD pico)
pico_sdk_init()

include_directories(include)

add_executable(DeckOS
    main.c

    # Kernel subsystems
    kernel/kernel.c
    kernel/bootloader.c
    kernel/config.c
    kernel/scheduler.c
    kernel/syslog.c          # in-memory ring log
    kernel/gpio_mon.c        # GPIO IRQ monitor
    kernel/morse.c           # Morse code LED blinker
    kernel/tone.c            # PWM tone / melody generator

    # Shell
    shell/shell.c

    # Commands
    commands/commands.c

    # Drivers
    drivers/drivers.c
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
    hardware_flash
    hardware_sync
    hardware_irq            # GPIO IRQ support
    pico_unique_id
    pico_bootrom
)

pico_enable_stdio_usb(DeckOS 1)
pico_enable_stdio_uart(DeckOS 0)
pico_add_extra_outputs(DeckOS)