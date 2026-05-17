#pragma once
#include <stdint.h>

typedef struct {
    uint32_t iterations;
    uint32_t elapsed_ms;
    float    cmds_per_sec;
    float    us_per_cmd;
} bench_result_t;

bench_result_t bench_run(const char* command, uint32_t iterations);
void           bench_print(const bench_result_t* r, const char* cmd);