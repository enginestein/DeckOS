#include "print_lock.h"
#include "pico/mutex.h"

static mutex_t s_print_mutex;

void print_lock_init(void) {
    mutex_init(&s_print_mutex);
}

void print_lock(void) {
    mutex_enter_blocking(&s_print_mutex);
}

void print_unlock(void) {
    mutex_exit(&s_print_mutex);
}