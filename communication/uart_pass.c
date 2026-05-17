#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/uart.h"
#include "hardware/gpio.h"
#include "uart_pass.h"


void uart_passthrough(uart_inst_t* uart_port, uint tx_pin, uint rx_pin,
                      uint baud, uint32_t timeout_ms) {
    // Initialise the target UART
    uart_init(uart_port, baud);
    gpio_set_function(tx_pin, GPIO_FUNC_UART);
    gpio_set_function(rx_pin, GPIO_FUNC_UART);
    uart_set_format(uart_port, 8, 1, UART_PARITY_NONE);
    uart_set_fifo_enabled(uart_port, false);

    const char* port_name = (uart_port == uart0) ? "UART0" : "UART1";
    printf("[uart] passthrough on %s  TX=GP%d RX=GP%d  %u baud\n",
           port_name, tx_pin, rx_pin, baud);
    printf("       Press Ctrl-X (0x18) to exit.\n");

    uint32_t start_ms  = to_ms_since_boot(get_absolute_time());
    uint32_t rx_total  = 0;
    uint32_t tx_total  = 0;

    while (true) {
        int c = getchar_timeout_us(0);
        if (c != PICO_ERROR_TIMEOUT) {
            if (c == 0x18) {   // Ctrl-X
                printf("\n[uart] passthrough ended  (rx=%lu tx=%lu bytes)\n",
                       rx_total, tx_total);
                break;
            }
            uart_putc_raw(uart_port, (char)c);
            tx_total++;
        }
        
        while (uart_is_readable(uart_port)) {
            char ch = uart_getc(uart_port);
            putchar_raw(ch);
            rx_total++;
        }

        // Timeout guard (0 = no timeout)
        if (timeout_ms && (to_ms_since_boot(get_absolute_time()) - start_ms) >= timeout_ms) {
            printf("\n[uart] passthrough timeout\n");
            break;
        }

        sleep_us(100);
    }

    uart_deinit(uart_port);
    // Return pins to SIO so the user can re-use them
    gpio_set_function(tx_pin, GPIO_FUNC_SIO);
    gpio_set_function(rx_pin, GPIO_FUNC_SIO);
}