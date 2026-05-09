#include <stdio.h>
#include <string.h>
#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "scheduler.h"

static sched_task_t tasks[SCHED_MAX_TASKS];
static int          task_count = 0;

static void core1_entry(void) {
    while (1) {
        uint32_t now = to_ms_since_boot(get_absolute_time());
        for (int i = 0; i < task_count; i++) {
            sched_task_t* t = &tasks[i];
            if (!t->enabled) continue;
            if ((now - t->last_run_ms) >= t->interval_ms) {
                t->fn();
                t->last_run_ms = now;
            }
        }
        sleep_us(100);
    }
}


void sched_init(void) {
    memset(tasks, 0, sizeof(tasks));
    multicore_launch_core1(core1_entry);
    printf("[sched] core1 launched\n");
}

int sched_register(const char* name, task_fn_t fn, uint32_t interval_ms) {
    if (task_count >= SCHED_MAX_TASKS || !fn) return -1;
    tasks[task_count].name        = name;
    tasks[task_count].fn          = fn;
    tasks[task_count].interval_ms = interval_ms ? interval_ms : 1;
    tasks[task_count].last_run_ms = 0;
    tasks[task_count].enabled     = true;
    return task_count++;
}

void sched_enable(int id, bool enable) {
    if (id >= 0 && id < task_count)
        tasks[id].enabled = enable;
}

void sched_list(void) {
    printf("ID  ENABLED  INTERVAL  NAME\n");
    for (int i = 0; i < task_count; i++) {
        printf(" %d   %-5s   %4lu ms   %s\n",
            i,
            tasks[i].enabled ? "yes" : "no",
            tasks[i].interval_ms,
            tasks[i].name);
    }
}