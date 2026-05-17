#pragma once
#include <stdint.h>
#include <stdbool.h>

#define SERVO_MAX_SLOTS   4
#define SERVO_PULSE_MIN   500    // us  (0°)
#define SERVO_PULSE_MAX   2500   // us  (180°)

typedef enum {
    SERVO_IDLE = 0,
    SERVO_HOLD,       // parked at fixed angle
    SERVO_SWEEP,      // sweeping back and forth
    SERVO_GOTO,       // moving to target then stopping
} servo_mode_t;

typedef struct {
    uint8_t      pin;
    servo_mode_t mode;
    int          current_angle;   // 0-180
    int          target_angle;
    int          sweep_min;
    int          sweep_max;
    int          step_deg;        // degrees per tick
    int          dir;             // +1 or -1
    uint32_t     step_ms;         // ms between steps
    uint32_t     last_step_ms;
    bool         active;
    char         name[12];
} servo_slot_t;

// Low-level
void     servo_write_angle(uint8_t pin, int angle);
void     servo_release(uint8_t pin);

// Background servo control (called from scheduler / Core1)
int      servo_bg_add(uint8_t pin, const char* name);
int      servo_bg_find(uint8_t pin);
void     servo_bg_set_sweep(int slot, int min_deg, int max_deg, int step_deg, uint32_t step_ms);
void     servo_bg_set_goto(int slot, int target, uint32_t step_ms);
void     servo_bg_stop(int slot);
void     servo_bg_tick(void);          // called by scheduler task
void     servo_bg_list(void);

// Blocking sweep (from cmd)
void     servo_sweep_blocking(uint8_t pin, int from, int to, int step_ms);