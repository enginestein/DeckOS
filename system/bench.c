#include <stdio.h>
#include <string.h>
#include "pico/stdlib.h"
#include "pico/time.h"
#include "hardware/clocks.h"
#include "bench.h"
#include "commands.h"   // commands_execute

bench_result_t bench_run(const char* command, uint32_t iterations) {
    bench_result_t r = {0};
    if (!command || !iterations) return r;
    if (iterations > 100000) iterations = 100000;

    r.iterations = iterations;

    char tmp[128];
    uint64_t t0 = time_us_64();

    for (uint32_t i = 0; i < iterations; i++) {
        strncpy(tmp, command, sizeof(tmp) - 1);
        tmp[sizeof(tmp) - 1] = '\0';
        commands_execute(tmp);
    }

    uint64_t t1 = time_us_64();
    uint64_t elapsed_us = t1 - t0;

    r.elapsed_ms   = (uint32_t)(elapsed_us / 1000);
    r.us_per_cmd   = (elapsed_us > 0)
                     ? (float)elapsed_us / (float)iterations
                     : 0.0f;
    r.cmds_per_sec = (r.us_per_cmd > 0.0f)
                     ? 1000000.0f / r.us_per_cmd
                     : 0.0f;
    return r;
}

void bench_print(const bench_result_t* r, const char* cmd) {
    printf("=== bench results ===\n");
    printf("  command     : %s\n",    cmd);
    printf("  iterations  : %lu\n",   r->iterations);
    printf("  total time  : %lu ms\n", r->elapsed_ms);
    printf("  per command : %.1f us\n", r->us_per_cmd);
    printf("  throughput  : %.0f cmd/s\n", r->cmds_per_sec);
    printf("  cpu freq    : %lu MHz\n",
           clock_get_hz(clk_sys) / 1000000);
    printf("=====================\n");
}