#include <stdio.h>
#include <string.h>
#include "bg_job.h"
#include "spinlock_util.h"
#include "print_lock.h"

static bg_job_t s_jobs[BG_JOB_MAX];

void bg_job_tick(void) {
    for (int i = 0; i < BG_JOB_MAX; i++) {
        uint32_t saved = sched_lock();
        bg_job_t snap = s_jobs[i];
        sched_unlock(saved);

        if (snap.state != BG_JOB_RUNNING) continue;
        snap.fn(snap.arg);
        uint32_t s2 = sched_lock();
        if (s_jobs[i].state == BG_JOB_RUNNING)
            s_jobs[i].state = BG_JOB_DONE;
        sched_unlock(s2);

        print_lock();
printf("\n[bg] job '%s' finished\n", snap.name);
print_unlock();
    }
}

int bg_job_submit(const char* name, bg_job_fn_t fn, void* arg) {
    uint32_t saved = sched_lock();
    for (int i = 0; i < BG_JOB_MAX; i++) {
        if (s_jobs[i].state == BG_JOB_IDLE ||
            s_jobs[i].state == BG_JOB_DONE ||
            s_jobs[i].state == BG_JOB_ERROR) {
            strncpy(s_jobs[i].name, name, BG_JOB_NAME_LEN - 1);
            s_jobs[i].fn     = fn;
            s_jobs[i].arg    = arg;
            s_jobs[i].cancel = false;   // reset flag
            s_jobs[i].state  = BG_JOB_RUNNING;
            sched_unlock(saved);
            printf("[bg] job '%s' started in slot %d\n", name, i);
            return i;
        }
    }
    sched_unlock(saved);
    printf("[bg] job queue full (%d slots)\n", BG_JOB_MAX);
    return -1;
}

void bg_job_list(void) {
    static const char* state_names[] = {"idle", "running", "done", "error"};
    printf("ID  STATE    NAME\n");
    printf("--  -------  --------------------\n");
    for (int i = 0; i < BG_JOB_MAX; i++) {
        uint32_t saved = sched_lock();
        bg_job_t snap = s_jobs[i];
        sched_unlock(saved);
        printf(" %d  %-7s  %s\n", i,
               state_names[snap.state],
               snap.state != BG_JOB_IDLE ? snap.name : "-");
    }
}

void bg_job_cancel(int id) {
    if (id < 0 || id >= BG_JOB_MAX) { printf("invalid job id\n"); return; }
    uint32_t saved = sched_lock();
    s_jobs[id].cancel = true;     
    s_jobs[id].state  = BG_JOB_IDLE;
    sched_unlock(saved);
    printf("[bg] job %d cancelled\n", id);
}

bool bg_job_cancel_requested(int id) {
    return (id >= 0 && id < BG_JOB_MAX) && s_jobs[id].cancel;
}