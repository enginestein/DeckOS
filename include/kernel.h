#ifndef KERNEL_H
#define KERNEL_H
#include <stdbool.h>
void kernel_init();
void kernel_run();
void kernel_enqueue_command(const char* cmd);
#endif