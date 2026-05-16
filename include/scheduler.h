#pragma once
#include <stdint.h>
#include <stdbool.h>

#define SCHED_MAX_TASKS 8

typedef void (*task_fn_t)(void);

typedef struct {
    const char* name;
    task_fn_t   fn;
    uint32_t    interval_ms;
    uint32_t    last_run_ms;
    bool        enabled;
} sched_task_t;

void sched_init(void);

int  sched_register(const char* name, task_fn_t fn, uint32_t interval_ms);

void sched_enable(int id, bool enable);

void sched_list(void);