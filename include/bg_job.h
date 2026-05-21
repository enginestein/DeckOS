#ifndef BG_JOB_H
#define BG_JOB_H

#include <stdint.h>
#include <stdbool.h>

#define BG_JOB_MAX      4
#define BG_JOB_NAME_LEN 32

typedef enum {
    BG_JOB_IDLE    = 0,
    BG_JOB_RUNNING = 1,
    BG_JOB_DONE    = 2,
    BG_JOB_ERROR   = 3,
} bg_job_state_t;

typedef void (*bg_job_fn_t)(void* arg);

typedef struct {
    char        name[BG_JOB_NAME_LEN];
    bg_job_fn_t fn;
    void*       arg;
    bg_job_state_t state;
    volatile bool  cancel;
} bg_job_t;

void bg_job_tick(void);
int  bg_job_submit(const char* name, bg_job_fn_t fn, void* arg);
void bg_job_list(void);
void bg_job_cancel(int id);
bool bg_job_cancel_requested(int id);

#endif