#pragma once
#include <stdint.h>
#include "hardware/uart.h"

// Runs an interactive passthrough between USB-CDC and a UART port.
// Press Ctrl-X (0x18) to exit.
void uart_passthrough(uart_inst_t* uart, uint tx_pin, uint rx_pin,
                      uint baud, uint32_t timeout_ms);