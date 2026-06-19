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
#include "tusb.h"
#include "dscript.h"
#include "autorun_script.h"
#include "vault_data.h"
#include <ctype.h>

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

extern void commands_api_register(const char *name, const char *desc, void (*handler)(int, char**));
extern void commands_api_unregister(const char *name);
extern void cron_schedule(const char *cmd, uint32_t delay_ms);

void kernel_init(void) {
    fat_disk_init();
    stdio_init_all();
    for (int i = 0; i < 300 && !stdio_usb_connected(); i++) sleep_ms(10);
    print_lock_init(); 
    heap_track_init();
    syslog_init();
    LOG_I("kernel", "booting DeckOS v9.0");

    bootloader_run();
    vfs_load();
    drivers_init_all();
    LOG_I("kernel", "drivers ready");

    sched_init();
    LOG_I("kernel", "scheduler ready");

    modules_init();
    module_set_cmd_api(commands_api_register, commands_api_unregister);
    LOG_I("kernel", "modules registered");

    /*if (vfs_resolve("/home/autorun.ds") < 0) {
        vfs_write("/home/autorun.ds", (const uint8_t*)AUTORUN_SCRIPT,
                  (uint32_t)strlen(AUTORUN_SCRIPT), false);
        printf("[kernel] autorun: default script injected\n");
    }

    if (vfs_resolve("/home/contacts.txt") < 0)
        vfs_write("/home/contacts.txt", (const uint8_t*)CONTACTS_DATA,
                  (uint32_t)strlen(CONTACTS_DATA), false);
    if (vfs_resolve("/home/todo.txt") < 0)
        vfs_write("/home/todo.txt", (const uint8_t*)TODO_DATA,
                  (uint32_t)strlen(TODO_DATA), false);
    if (vfs_resolve("/home/journal.txt") < 0)
        vfs_write("/home/journal.txt", (const uint8_t*)JOURNAL_DATA,
                  (uint32_t)strlen(JOURNAL_DATA), false);*/

    printf("[kernel] initialized\n");
    shell_init();
    LOG_I("kernel", "shell ready");

    module_fire_event(MODULE_EVENT_BOOT_COMPLETE, NULL);
}

static void import_msc_payloads(void) {
    int n = fat_disk_count();
    if (n <= 0) return;

    for (int i = 0; i < n; i++) {
        char name[13];
        uint32_t size;
        if (fat_disk_entry(i, name, &size) != 0) continue;

        int len = (int)strlen(name);
        if (len < 3 || strcasecmp(name + len - 3, ".ds") != 0) continue;

        uint8_t buf[512];
        uint32_t got = 0;
        if (fat_disk_read_file(name, buf, sizeof(buf), &got) != 0) continue;
        buf[got] = '\0';

        char vpath[64];
        snprintf(vpath, sizeof(vpath), "/home/%s", name);
        for (char *p = vpath; *p; p++) *p = (char)tolower((unsigned char)*p);

        vfs_write(vpath, buf, got, false);
        printf("[kernel] MSC import: %s -> %s (%u bytes)\n", name, vpath, got);
    }
}

void kernel_run(void) {
    static uint64_t last_tick = 0;
    static bool boot_actions_done = false;
    while (true) {
        tud_task();
        if (!boot_actions_done) {
            boot_actions_done = true;

            import_msc_payloads();

            if (vfs_resolve("/home/autorun.ds") >= 0) {
                printf("[kernel] autorun: executing /home/autorun.ds\n");
                script_run_file("/home/autorun.ds");
                printf("[kernel] autorun: complete\n");
            }

            if (vfs_resolve("/home/autorun.ds") < 0) {
                int home_idx = vfs_resolve("/home");
                if (home_idx >= 0) {
                    for (int i = 0; i < VFS_MAX_NODES; i++) {
                        if (!s_nodes[i].used || s_nodes[i].type != VFS_FILE) continue;
                        if (s_nodes[i].parent != home_idx) continue;
                        int nl = (int)strlen(s_nodes[i].name);
                        if (nl >= 3 && strcasecmp(s_nodes[i].name + nl - 3, ".ds") == 0) {
                            char p[64];
                            snprintf(p, sizeof(p), "/home/%s", s_nodes[i].name);
                            printf("[kernel] autorun: executing %s (payload)\n", p);
                            script_run_file(p);
                            break;
                        }
                    }
                }
            }
        }
        cron_poll();
        pending_commands_poll();
        shell_run();
        uint64_t now = time_us_64();
        if (now - last_tick >= 1000000) {
            module_fire_event(MODULE_EVENT_TICK, NULL);
            last_tick = now;
        }
        tight_loop_contents();
    }
}