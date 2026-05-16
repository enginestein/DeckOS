#include <stdio.h>
#include "pico/stdlib.h"
#include "kernel.h"
#include "shell.h"
#include "drivers.h"
#include "scheduler.h"
#include "bootloader.h"
#include "syslog.h"
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
    syslog_init();
    LOG_I("kernel", "booting DeckOS v1.1.0");

    bootloader_run();

    drivers_init_all();
    LOG_I("kernel", "drivers ready");

    sched_init();
    sched_register("heartbeat", task_heartbeat, 1000);
    LOG_I("kernel", "scheduler ready");

    printf("[kernel] initialized\n");
    shell_init();
    LOG_I("kernel", "shell ready");
}

void kernel_run(void) {
    shell_run();
}