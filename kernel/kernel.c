#include <stdio.h>
#include "pico/stdlib.h"
#include "kernel.h"
#include "shell.h"
#include "drivers.h"
#include "scheduler.h"
#include "bootloader.h"


#include "hardware/gpio.h"

static void task_heartbeat(void) {
    static bool state = false;
    gpio_init(25);
    gpio_set_dir(25, GPIO_OUT);
    state = !state;
    gpio_put(25, state);
}


void kernel_init(void) {
    stdio_init_all();
    while (!stdio_usb_connected()) sleep_ms(100);

    bootloader_run();
    drivers_init_all();
    sched_init();
    sched_register("heartbeat", task_heartbeat, 1000); 

    printf("[kernel] initialized\n");
    shell_init();
}

void kernel_run(void) {
    shell_run();
}