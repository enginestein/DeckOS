#pragma once
#include <stdint.h>
#include <stdbool.h>

#define SCHED_MAX_TASKS  16

typedef void (*task_fn_t)(void);

typedef struct {
    const char* name;
    task_fn_t   fn;
    uint32_t    interval_ms;
    uint32_t    last_run_ms;
    bool        enabled;
} sched_task_t;

void  sched_init(void);
int   sched_register(const char* name, task_fn_t fn, uint32_t interval_ms);
void  sched_enable(int id, bool enable);
void  sched_list(void);

// For top command
int      sched_snapshot(sched_task_t* out, uint64_t* totals_out, int max);
uint64_t sched_core1_total_us(void);