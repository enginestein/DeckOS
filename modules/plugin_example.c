#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "module.h"



#define MAX_COUNTERS 16
static char        s_names[MAX_COUNTERS][24];
static int         s_values[MAX_COUNTERS];
static int         s_count = 0;

static int counter_find(const char *name) {
    for (int i = 0; i < s_count; i++)
        if (strcmp(s_names[i], name) == 0) return i;
    return -1;
}

static void cmd_counter(int argc, char *argv[]) {
    if (argc < 2) { printf("usage: counter <name> [value] | ls | reset\n"); return; }

    if (strcmp(argv[1], "ls") == 0) {
        if (s_count == 0) { printf("no counters\n"); return; }
        for (int i = 0; i < s_count; i++)
            printf("  %s = %d\n", s_names[i], s_values[i]);
        return;
    }
    if (strcmp(argv[1], "reset") == 0) { s_count = 0; printf("counters cleared\n"); return; }

    int idx = counter_find(argv[1]);
    if (argc >= 3) {
       
        int val = atoi(argv[2]);
        if (idx < 0) {
            if (s_count >= MAX_COUNTERS) { printf("max %d counters\n", MAX_COUNTERS); return; }
            idx = s_count++;
            strncpy(s_names[idx], argv[1], sizeof(s_names[idx]) - 1);
        }
        s_values[idx] = val;
    } else {
       
        if (idx < 0) {
            if (s_count >= MAX_COUNTERS) { printf("max %d counters\n", MAX_COUNTERS); return; }
            idx = s_count++;
            strncpy(s_names[idx], argv[1], sizeof(s_names[idx]) - 1);
            s_values[idx] = 0;
        }
        s_values[idx]++;
    }
    printf("  %s = %d\n", s_names[idx], s_values[idx]);
}



static int  s_hb_interval = 0;  
static int  s_hb_ticks    = 0;
static int  s_hb_count    = 0;  

static void print_heartbeat(void) {
    printf("[heartbeat] ticks=%d counters=%d\n", s_hb_count, s_count);
}

static void cmd_heartbeat(int argc, char *argv[]) {
    if (argc < 2) {
        printf("heartbeat:\n");
        printf("  ticks     : %d\n", s_hb_count);
        printf("  counters  : %d\n", s_count);
        printf("  auto      : every %d s%s\n", s_hb_interval,
               s_hb_interval ? "" : " (off, use 'heartbeat every <N>')");
        return;
    }
    if (strcmp(argv[1], "every") == 0 && argc >= 3) {
        s_hb_interval = atoi(argv[2]);
        if (s_hb_interval < 1) s_hb_interval = 0;
        s_hb_ticks = 0;
        printf("heartbeat: reporting every %d s\n", s_hb_interval);
    } else if (strcmp(argv[1], "stop") == 0) {
        s_hb_interval = 0;
        printf("heartbeat: stopped\n");
    } else {
        printf("usage: heartbeat [every <sec> | stop]\n");
    }
}



static module_cmd_t s_cmds[] = {
    {"counter",   "counter <name> [value] | ls | reset  named counters",     cmd_counter},
    {"heartbeat", "heartbeat [every <sec> | stop]       periodic health",    cmd_heartbeat},
};



static bool mod_load(void) {
    s_count = 0;
    s_hb_interval = 0;
    s_hb_ticks = 0;
    s_hb_count = 0;
    printf("plugin_example: loaded (try: counter, heartbeat)\n");
    return true;
}

static void mod_unload(void) {
    printf("plugin_example: unloaded\n");
}



static void on_event(module_event_t event, void *data) {
    (void)data;
    switch (event) {
    case MODULE_EVENT_BOOT_COMPLETE:
        printf("plugin_example: boot complete -- system is ready\n");
        break;
    case MODULE_EVENT_TICK:
        s_hb_count++;
        if (s_hb_interval > 0) {
            s_hb_ticks++;
            if (s_hb_ticks >= s_hb_interval) {
                s_hb_ticks = 0;
                print_heartbeat();
            }
        }
        break;
    default:
        break;
    }
}



plugin_api_t MOD_EXAMPLE = {
    .init          = mod_load,
    .deinit        = mod_unload,
    .commands      = s_cmds,
    .command_count = sizeof(s_cmds) / sizeof(s_cmds[0]),
    .on_event      = on_event,
};

/* Community Plugin Registration:
 *
 * To write your own plugin:
 *
 *   1. Copy this file to modules/my_plugin.c and rename the symbols.
 *   2. Define a plugin_api_t struct with your init/deinit/commands/events.
 *   3. In kernel/module.c's modules_init(), call module_register_plugin()
 *      with a module_t that references your plugin_api_t fields.
 *   4. Add your .c file to CMakeLists.txt.
 *   5. Users run "module load my-plugin" to activate it.
 *
 * Features demonstrated:
 *   - Shell commands (counter, heartbeat) -> registered/unregistered on load
 *   - Event callbacks -> BOOT_COMPLETE fires once at boot, TICK fires ~1/sec
 *   - Load/unload hooks -> state is reset when module loads, cleaned on unload
 *   - module_cmd_t / plugin_api_t -> standard export pattern
 */
