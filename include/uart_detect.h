#pragma once
#include <stdint.h>
#include <stdbool.h>

/**
 * Attempt to auto-detect UART baud rate and device type on a given pin.
 * Uses pulse-width timing on GPIO RX to estimate baud, then tries AT ping.
 *
 * @param rx_pin   GPIO pin to listen on (0-28)
 * @param timeout_ms  How long to wait for activity (ms)
 */
void uart_detect_run(uint8_t rx_pin, uint32_t timeout_ms);

/**
 * Logic-analyser-assisted protocol identification on a pin.
 * Combines edge timing with heuristics to guess protocol + baud.
 *
 * @param pin         GPIO pin to analyse
 * @param samples     Number of LA samples
 * @param us_per_sample  Sampling interval
 */
void la_detect_protocol(uint8_t pin, int samples, int us_per_sample);