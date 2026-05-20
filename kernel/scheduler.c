#include <stdio.h>
#include <string.h>
#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "hardware/sync.h"
#include "scheduler.h"
#include "spinlock_util.h"
#include "servo.h"

static sched_task_t tasks[SCHED_MAX_TASKS];
static int          task_count = 0;

// CPU-usage tracking: total time spent inside each task (us)
static uint64_t task_total_us[SCHED_MAX_TASKS];
static uint64_t core1_total_us = 0;

static void core1_entry(void) {
    while (1) {
        uint32_t now = to_ms_since_boot(get_absolute_time());

        // Lock while iterating - short critical section
        uint32_t saved = sched_lock();
        int count_snap = task_count;
        sched_unlock(saved);

        for (int i = 0; i < count_snap; i++) {
            // Read under lock
            saved = sched_lock();
            sched_task_t snap = tasks[i];
            sched_unlock(saved);

            if (!snap.enabled) continue;
            if ((now - snap.last_run_ms) < snap.interval_ms) continue;

            // Run task outside lock so Core0 can call sched_enable freely
            uint64_t t0 = time_us_64();
            snap.fn();
            uint64_t elapsed = time_us_64() - t0;

            // Update last_run under lock
            saved = sched_lock();
            tasks[i].last_run_ms = now;
            sched_unlock(saved);

            task_total_us[i] += elapsed;
            core1_total_us   += elapsed;
        }

        // Tick background servos (very fast, just updates PWM levels)
        servo_bg_tick();

        sleep_us(100);
    }
}

void sched_init(void) {
    memset(tasks,        0, sizeof(tasks));
    memset(task_total_us,0, sizeof(task_total_us));

    // Claim our spinlock IDs
    spin_lock_claim(SCHED_SPINLOCK_ID);
    spin_lock_claim(CONFIG_SPINLOCK_ID);
    spin_lock_claim(SYSLOG_SPINLOCK_ID);
    multicore_launch_core1(core1_entry);
    printf("[sched] core1 launched (spinlock-protected)\n");
}

int sched_register(const char* name, task_fn_t fn, uint32_t interval_ms) {
    uint32_t saved = sched_lock();

    if (task_count >= SCHED_MAX_TASKS || !fn) {
        sched_unlock(saved);
        return -1;
    }

    int id = task_count;
    tasks[id].name        = name;
    tasks[id].fn          = fn;
    tasks[id].interval_ms = interval_ms ? interval_ms : 1;
    tasks[id].last_run_ms = 0;
    tasks[id].enabled     = true;
    task_count++;

    sched_unlock(saved);
    return id;
}

void sched_enable(int id, bool enable) {
    uint32_t saved = sched_lock();
    if (id >= 0 && id < task_count)
        tasks[id].enabled = enable;
    sched_unlock(saved);
}

void sched_list(void) {
    uint32_t saved = sched_lock();
    int count_snap = task_count;
    sched_task_t snap[SCHED_MAX_TASKS];
    uint64_t totals[SCHED_MAX_TASKS];
    memcpy(snap,   tasks,        sizeof(snap));
    memcpy(totals, task_total_us, sizeof(totals));
    sched_unlock(saved);

    uint64_t grand_total = core1_total_us;
    if (grand_total == 0) grand_total = 1;  // avoid div/0

    printf("ID  ENABLED  INTERVAL  CPU%%    NAME\n");
    for (int i = 0; i < count_snap; i++) {
        uint32_t pct_x10 = (uint32_t)((totals[i] * 1000) / grand_total);
        printf(" %d   %-5s   %4lu ms   %2lu.%lu%%  %s\n",
               i,
               snap[i].enabled ? "yes" : "no",
               snap[i].interval_ms,
               pct_x10 / 10, pct_x10 % 10,
               snap[i].name);
    }
}

// Returns a snapshot for top command
int sched_snapshot(sched_task_t* out, uint64_t* totals_out, int max) {
    uint32_t saved = sched_lock();
    int n = task_count < max ? task_count : max;
    memcpy(out,        tasks,        n * sizeof(sched_task_t));
    memcpy(totals_out, task_total_us, n * sizeof(uint64_t));
    sched_unlock(saved);
    return n;
}

uint64_t sched_core1_total_us(void) { return core1_total_us; }