#pragma once
#include <stdint.h>
#include <stdbool.h>

typedef struct {
    uint32_t timestamp_ms;
    uint8_t  pin;
    uint8_t  edge;  
} gpio_event_t;

#define GPIO_MON_SLOTS 32

int  gpio_mon_start(uint8_t pin);
void gpio_mon_stop(uint8_t pin);
void gpio_mon_dump(uint8_t pin);
bool gpio_mon_active(uint8_t pin);
void gpio_mon_watch(uint8_t pin, uint32_t timeout_ms);