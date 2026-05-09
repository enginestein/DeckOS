#pragma once
#include <stdint.h>
#include <stdbool.h>

// Maximum background tasks that can run on core 1
#define SCHED_MAX_TASKS 8

typedef void (*task_fn_t)(void);

typedef struct {
    const char* name;
    task_fn_t   fn;
    uint32_t    interval_ms;   // 0 = run as fast as possible
    uint32_t    last_run_ms;
    bool        enabled;
} sched_task_t;

// Call once from kernel_init() — launches the core-1 scheduler loop
void sched_init(void);

// Register a background task to run on core 1
// Returns task id (>=0) or -1 on failure
int  sched_register(const char* name, task_fn_t fn, uint32_t interval_ms);

// Enable / disable a registered task by id
void sched_enable(int id, bool enable);

// Print registered tasks and their state
void sched_list(void);