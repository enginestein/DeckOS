#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "pico/stdlib.h"
#include "module.h"

extern void cmd_imu(int argc, char *argv[]);

static module_cmd_t s_cmds[] = {
    {"imu", "MPU6050 IMU (read/stream/attitude/calibrate/raw/whoami)", cmd_imu},
};

static bool mod_imu_load(void) {
    printf("imu: module loaded\n");
    return true;
}

static void mod_imu_unload(void) {
    printf("imu: module unloaded\n");
}

plugin_api_t MOD_IMU = {
    .init = mod_imu_load,
    .deinit = mod_imu_unload,
    .commands = s_cmds,
    .command_count = sizeof(s_cmds) / sizeof(s_cmds[0]),
    .on_event = NULL,
};
