#include <stdio.h>
#include "pico/stdlib.h"
#include "pico/multicore.h"
#include <string.h>
#include "kernel.h"
#include "file_persist.h"
#include "shell.h"
#include "drivers.h"
#include "scheduler.h"
#include "bootloader.h"
#include "syslog.h"
#include "bt.h"
#include "print_lock.h"
#include "heap_track.h"
#include "hardware/gpio.h"
#include "vfs.h"
#include "fat_disk.h"
#include "module.h"
#include "commands.h"

static void (*s_core1_fn)(void) = NULL;

void kernel_set_core1_fn(void (*fn)(void)) {
    s_core1_fn = fn;
}

void core1_restart(void) {
    if (s_core1_fn) {
        multicore_launch_core1(s_core1_fn);
        printf("[kernel] Core1 relaunched\n");
    } else {
        printf("[kernel] WARNING: core1_restart called but fn is NULL\n");
    }
}



static char pending_cmds[MAX_PENDING_CMDS][INPUT_SIZE];
static int pending_head = 0;
static int pending_tail = 0;


void pending_commands_poll(void) {

    if (pending_head == pending_tail)
        return;

    char tmp[INPUT_SIZE];

    strncpy(tmp,
            pending_cmds[pending_head],
            INPUT_SIZE - 1);

    tmp[INPUT_SIZE - 1] = '\0';

    pending_head =
        (pending_head + 1) % MAX_PENDING_CMDS;

    printf("exec: '%s'\n", tmp);

    commands_execute(tmp);
}
void kernel_enqueue_command(const char* cmd) {
    int next = (pending_tail + 1) % MAX_PENDING_CMDS;
    if (next != pending_head) {
        strncpy(pending_cmds[pending_tail], cmd, INPUT_SIZE - 1);
        pending_cmds[pending_tail][INPUT_SIZE - 1] = '\0';
        pending_tail = next;
    } else {
        printf("cron: command queue full\n");
    }
}

void kernel_init(void) {
    fat_disk_init();
    stdio_init_all();
    while (!stdio_usb_connected()) sleep_ms(100);
    print_lock_init(); 
    heap_track_init();
    syslog_init();
    LOG_I("kernel", "booting DeckOS v6.0");

    bootloader_run();
    vfs_load();
    drivers_init_all();
    bt_init(BT_DEFAULT_BAUD);
    LOG_I("kernel", "drivers ready");

    sched_init();
    LOG_I("kernel", "scheduler ready");

    modules_init();
    LOG_I("kernel", "modules registered");

    printf("[kernel] initialized\n");
    shell_init();
    LOG_I("kernel", "shell ready");
}

void kernel_run(void) {
    while (true) {  
        cron_poll();
        pending_commands_poll();
        shell_run();        
        tight_loop_contents();
    }
}