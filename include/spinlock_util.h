#pragma once
#include "hardware/sync.h"
#include "hardware/structs/sio.h"

// Thin wrappers around the RP2040 hardware spinlocks.
// sched_lock / sched_unlock protect the tasks[] array that Core0 reads
// and Core1 mutates concurrently.

#define SCHED_SPINLOCK_ID   14   // spinlock IDs 0-31; 14 is unused by SDK
#define CONFIG_SPINLOCK_ID  15
#define SYSLOG_SPINLOCK_ID  13

static inline spin_lock_t* sched_spinlock(void) {
    return spin_lock_instance(SCHED_SPINLOCK_ID);
}

static inline spin_lock_t* config_spinlock(void) {
    return spin_lock_instance(CONFIG_SPINLOCK_ID);
}

// Claim + disable interrupts; returns saved interrupt state.
static inline uint32_t sched_lock(void) {
    return spin_lock_blocking(sched_spinlock());
}

static inline void sched_unlock(uint32_t saved) {
    spin_unlock(sched_spinlock(), saved);
}

static inline uint32_t config_lock(void) {
    return spin_lock_blocking(config_spinlock());
}

static inline void config_unlock(uint32_t saved) {
    spin_unlock(config_spinlock(), saved);
}

static inline uint32_t syslog_lock(void) {
    return spin_lock_blocking(spin_lock_instance(SYSLOG_SPINLOCK_ID));
}
static inline void syslog_unlock(uint32_t saved) {
    spin_unlock(spin_lock_instance(SYSLOG_SPINLOCK_ID), saved);
}