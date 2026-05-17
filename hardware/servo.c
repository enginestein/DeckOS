#include <stdio.h>
#include <string.h>
#include "pico/stdlib.h"
#include "hardware/pwm.h"
#include "hardware/clocks.h"
#include "servo.h"

static uint16_t angle_to_pulse(int angle) {
    if (angle < 0)   angle = 0;
    if (angle > 180) angle = 180;
    return (uint16_t)(SERVO_PULSE_MIN + (angle * (SERVO_PULSE_MAX - SERVO_PULSE_MIN)) / 180);
}

void servo_write_angle(uint8_t pin, int angle) {
    gpio_set_function(pin, GPIO_FUNC_PWM);
    uint slice = pwm_gpio_to_slice_num(pin);

    pwm_set_clkdiv(slice, 125.0f);
    pwm_set_wrap(slice, 20000 - 1);

    uint16_t pulse_us = angle_to_pulse(angle);
    pwm_set_gpio_level(pin, pulse_us);
    pwm_set_enabled(slice, true);
}

void servo_release(uint8_t pin) {
    uint slice = pwm_gpio_to_slice_num(pin);
    pwm_set_enabled(slice, false);
    gpio_set_function(pin, GPIO_FUNC_SIO);
    gpio_set_dir(pin, GPIO_OUT);
    gpio_put(pin, 0);
}


void servo_sweep_blocking(uint8_t pin, int from, int to, int step_ms) {
    if (from < 0)   from = 0;
    if (to   > 180) to   = 180;
    if (step_ms < 1) step_ms = 20;

    printf("servo sweep: GPIO%d  %d° -> %d°  (%d ms/step)\n",
           pin, from, to, step_ms);

    int dir   = (to >= from) ? 1 : -1;
    int angle = from;
    while (true) {
        servo_write_angle(pin, angle);
        printf("\r  angle: %3d°   ", angle);
        fflush(stdout);
        sleep_ms((uint32_t)step_ms);
        if (angle == to) break;
        angle += dir;
    }
    printf("\ndone.\n");
}


static servo_slot_t s_slots[SERVO_MAX_SLOTS];
static int          s_slot_count = 0;

int servo_bg_add(uint8_t pin, const char* name) {
    // Reuse existing slot for same pin
    for (int i = 0; i < s_slot_count; i++)
        if (s_slots[i].pin == pin) return i;

    if (s_slot_count >= SERVO_MAX_SLOTS) {
        printf("servo: no free slots (max %d)\n", SERVO_MAX_SLOTS);
        return -1;
    }
    int id = s_slot_count++;
    memset(&s_slots[id], 0, sizeof(servo_slot_t));
    s_slots[id].pin   = pin;
    s_slots[id].mode  = SERVO_IDLE;
    s_slots[id].active = true;
    strncpy(s_slots[id].name, name ? name : "servo", 11);
    servo_write_angle(pin, 90);   // park at centre
    s_slots[id].current_angle = 90;
    return id;
}

int servo_bg_find(uint8_t pin) {
    for (int i = 0; i < s_slot_count; i++)
        if (s_slots[i].pin == pin) return i;
    return -1;
}

void servo_bg_set_sweep(int slot, int min_deg, int max_deg,
                         int step_deg, uint32_t step_ms) {
    if (slot < 0 || slot >= s_slot_count) return;
    servo_slot_t* s = &s_slots[slot];
    s->sweep_min     = min_deg  < 0   ? 0   : min_deg;
    s->sweep_max     = max_deg  > 180 ? 180 : max_deg;
    s->step_deg      = step_deg < 1   ? 1   : step_deg;
    s->step_ms       = step_ms  < 10  ? 10  : step_ms;
    s->dir           = 1;
    s->current_angle = s->sweep_min;
    s->mode          = SERVO_SWEEP;
    s->last_step_ms  = to_ms_since_boot(get_absolute_time());
    printf("servo bg: slot %d GPIO%d sweep %d°<->%d°  step=%d° @%lums\n",
           slot, s->pin, s->sweep_min, s->sweep_max, s->step_deg, s->step_ms);
}

void servo_bg_set_goto(int slot, int target, uint32_t step_ms) {
    if (slot < 0 || slot >= s_slot_count) return;
    servo_slot_t* s = &s_slots[slot];
    s->target_angle  = target  < 0   ? 0   : (target > 180 ? 180 : target);
    s->step_deg      = 1;
    s->step_ms       = step_ms < 10  ? 10  : step_ms;
    s->dir           = (s->target_angle >= s->current_angle) ? 1 : -1;
    s->mode          = SERVO_GOTO;
    s->last_step_ms  = to_ms_since_boot(get_absolute_time());
}

void servo_bg_stop(int slot) {
    if (slot < 0 || slot >= s_slot_count) return;
    s_slots[slot].mode   = SERVO_HOLD;
    s_slots[slot].active = false;
    servo_release(s_slots[slot].pin);
    printf("servo bg: slot %d stopped\n", slot);
}

/* Called by the scheduler task on Core1 */
void servo_bg_tick(void) {
    uint32_t now = to_ms_since_boot(get_absolute_time());
    for (int i = 0; i < s_slot_count; i++) {
        servo_slot_t* s = &s_slots[i];
        if (!s->active) continue;
        if (s->mode == SERVO_IDLE || s->mode == SERVO_HOLD) continue;
        if ((now - s->last_step_ms) < s->step_ms) continue;
        s->last_step_ms = now;

        if (s->mode == SERVO_SWEEP) {
            s->current_angle += s->dir * s->step_deg;
            if (s->current_angle >= s->sweep_max) {
                s->current_angle = s->sweep_max;
                s->dir = -1;
            } else if (s->current_angle <= s->sweep_min) {
                s->current_angle = s->sweep_min;
                s->dir = 1;
            }
            servo_write_angle(s->pin, s->current_angle);

        } else if (s->mode == SERVO_GOTO) {
            s->current_angle += s->dir * s->step_deg;
            bool done = (s->dir > 0)
                ? (s->current_angle >= s->target_angle)
                : (s->current_angle <= s->target_angle);
            if (done) {
                s->current_angle = s->target_angle;
                s->mode          = SERVO_HOLD;
            }
            servo_write_angle(s->pin, s->current_angle);
        }
    }
}

void servo_bg_list(void) {
    if (s_slot_count == 0) { printf("no background servos registered\n"); return; }
    printf("SLOT  PIN    MODE     ANGLE  NAME\n");
    const char* mode_names[] = {"idle", "hold", "sweep", "goto"};
    for (int i = 0; i < s_slot_count; i++) {
        servo_slot_t* s = &s_slots[i];
        printf(" %-4d  GP%-2d   %-7s  %3d°  %s\n",
               i, s->pin,
               mode_names[s->mode],
               s->current_angle,
               s->name);
    }
}