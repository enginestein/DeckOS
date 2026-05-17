#include <stdio.h>
#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "kernel.h"
#include "shell.h"
#include "drivers.h"
#include "scheduler.h"
#include "bootloader.h"
#include "syslog.h"
#include "heap_track.h"
#include "hardware/gpio.h"

static void (*s_core1_fn)(void) = NULL;

// Called by scheduler.c — sets the Core1 function pointer.
void kernel_set_core1_fn(void (*fn)(void)) {
    s_core1_fn = fn;
}

// Called by config.c after flash write.
void core1_restart(void) {
    if (s_core1_fn) {
        multicore_launch_core1(s_core1_fn);
        printf("[kernel] Core1 relaunched\n");
    } else {
        printf("[kernel] WARNING: core1_restart called but fn is NULL\n");
    }
}

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

    heap_track_init();
    syslog_init();
    LOG_I("kernel", "booting DeckOS v1.3");

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