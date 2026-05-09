#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/adc.h"
#include "kernel.h"
#include "shell.h"

void kernel_init() {
    stdio_init_all();
    while (!stdio_usb_connected()) sleep_ms(100);
    adc_init();
    adc_set_temp_sensor_enabled(true);
    sleep_ms(200);

    printf("\n");
    printf("  ================================\n");
    printf("    DeckOS v1.0.0  -  Pico Control Shell    \n");
    printf("  ================================\n");
    printf("[KERNEL] initialized\n");
    shell_init();
}

void kernel_run() {
    shell_run();
}