cmake_minimum_required(VERSION 3.13)
include($ENV{PICO_SDK_PATH}/external/pico_sdk_import.cmake)
project(DeckOS C CXX ASM)
set(PICO_BOARD pico)                              
pico_sdk_init()
include_directories(include)

add_executable(DeckOS
    main.c
    kernel/kernel.c
    shell/shell.c
    commands/commands.c
)

target_link_libraries(DeckOS
    pico_stdlib
    hardware_adc           
    hardware_gpio          
    hardware_watchdog
    hardware_pwm 
    hardware_clocks    
    pico_unique_id                    
)

pico_enable_stdio_usb(DeckOS 1)
pico_enable_stdio_uart(DeckOS 0)
pico_add_extra_outputs(DeckOS)