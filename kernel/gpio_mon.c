#include <stdio.h>
#include <string.h>
#include "pico/stdlib.h"
#include "hardware/gpio.h"
#include "gpio_mon.h"
#include "syslog.h"

typedef struct {
    gpio_event_t events[GPIO_MON_SLOTS];
    int          head;
    int          count;
    bool         active;
} pin_mon_t;

static pin_mon_t s_monitors[29];   // one per GPIO (0-28)

static void gpio_irq_cb(uint gpio, uint32_t events) {
    if (gpio > 28) return;
    pin_mon_t* m = &s_monitors[gpio];
    if (!m->active) return;

    gpio_event_t* e = &m->events[m->head];
    e->timestamp_ms = to_ms_since_boot(get_absolute_time());
    e->pin          = (uint8_t)gpio;
    e->edge         = (events & GPIO_IRQ_EDGE_RISE) ? 1 : 0;

    m->head = (m->head + 1) % GPIO_MON_SLOTS;
    if (m->count < GPIO_MON_SLOTS) m->count++;

    char buf[32];
    snprintf(buf, sizeof(buf), "GPIO%d %s edge",
             gpio, e->edge ? "RISING" : "FALLING");
    syslog_write(LOG_INFO, "gpio_irq", buf);
}

int gpio_mon_start(uint8_t pin) {
    if (pin > 28) return -1;
    pin_mon_t* m = &s_monitors[pin];
    memset(m, 0, sizeof(*m));
    m->active = true;

    gpio_init(pin);
    gpio_set_dir(pin, GPIO_IN);
    gpio_pull_up(pin);
    gpio_set_irq_enabled_with_callback(pin,
        GPIO_IRQ_EDGE_RISE | GPIO_IRQ_EDGE_FALL,
        true, gpio_irq_cb);

    char buf[24];
    snprintf(buf, sizeof(buf), "monitoring GPIO%d", pin);
    LOG_I("gpio_irq", buf);
    return 0;
}

void gpio_mon_stop(uint8_t pin) {
    if (pin > 28) return;
    gpio_set_irq_enabled(pin, GPIO_IRQ_EDGE_RISE | GPIO_IRQ_EDGE_FALL, false);
    s_monitors[pin].active = false;
    char buf[24];
    snprintf(buf, sizeof(buf), "stopped GPIO%d", pin);
    LOG_I("gpio_irq", buf);
}

void gpio_mon_dump(uint8_t pin) {
    if (pin > 28) return;
    pin_mon_t* m = &s_monitors[pin];

    if (m->count == 0) {
        printf("GPIO%d: no events captured\n", pin);
        return;
    }

    printf("GPIO%d event log (%d events):\n", pin, m->count);
    printf("  TIME (ms)    EDGE\n");

    int start = (m->head - m->count + GPIO_MON_SLOTS * 2) % GPIO_MON_SLOTS;
    for (int i = 0; i < m->count; i++) {
        gpio_event_t* e = &m->events[(start + i) % GPIO_MON_SLOTS];
        printf("  %10lu   %s\n",
               e->timestamp_ms,
               e->edge ? "RISING ↑" : "FALLING ↓");
    }
    // Clear after dump
    m->head  = 0;
    m->count = 0;
}

bool gpio_mon_active(uint8_t pin) {
    return (pin <= 28) && s_monitors[pin].active;
}

void gpio_mon_watch(uint8_t pin, uint32_t timeout_ms) {
    if (gpio_mon_start(pin) < 0) {
        printf("failed to start monitor on GPIO%d\n", pin);
        return;
    }

    printf("watching GPIO%d  (press any key to stop)...\n", pin);
    uint32_t start_ms = to_ms_since_boot(get_absolute_time());
    int      last_val = gpio_get(pin);
    uint32_t edge_count = 0;

    while (true) {
        // Check for keypress to stop
        int c = getchar_timeout_us(0);
        if (c != PICO_ERROR_TIMEOUT) break;

        // Print newly queued events
        pin_mon_t* m = &s_monitors[pin];
        if (m->count > 0) {
            int s2 = (m->head - m->count + GPIO_MON_SLOTS * 2) % GPIO_MON_SLOTS;
            for (int i = 0; i < m->count; i++) {
                gpio_event_t* e = &m->events[(s2 + i) % GPIO_MON_SLOTS];
                printf("  [%7lu ms] GPIO%d %s\n",
                       e->timestamp_ms,
                       pin,
                       e->edge ? "RISING ↑" : "FALLING ↓");
                edge_count++;
            }
            m->head  = 0;
            m->count = 0;
        }

        if (timeout_ms && (to_ms_since_boot(get_absolute_time()) - start_ms) >= timeout_ms)
            break;

        sleep_ms(1);
    }

    gpio_mon_stop(pin);
    printf("GPIO%d monitor stopped  (%lu edges captured)\n", pin, edge_count);
}