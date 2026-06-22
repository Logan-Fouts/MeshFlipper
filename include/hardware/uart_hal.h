#ifndef UART_HAL_H
#define UART_HAL_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <zephyr/drivers/uart.h>

// UART configuration structure
typedef struct {
    uint32_t baud_rate;
    uint8_t data_bits;
    uint8_t stop_bits;
    uint8_t parity;
    uint8_t flow_ctrl;
} uart_config_t;

// UART HAL instance
typedef struct {
    const struct device *dev;
    uart_config_t config;
    bool initialized;
    
    // RX callback
    void (*rx_callback)(uint8_t byte, void *context);
    void *callback_context;
} uart_hal_t;

// Global UART HAL instance
extern uart_hal_t g_uart_hal;

// Initialization
int uart_hal_init(uart_hal_t *hal, const struct device *dev);
int uart_hal_configure(uart_hal_t *hal, const uart_config_t *config);
int uart_hal_configure_default(uart_hal_t *hal);

// Callback management
void uart_hal_set_rx_callback(uart_hal_t *hal, 
                              void (*callback)(uint8_t byte, void *context), 
                              void *context);

// Transmit
int uart_hal_send_byte(uart_hal_t *hal, uint8_t byte);
int uart_hal_send_bytes(uart_hal_t *hal, const uint8_t *data, size_t len);

// Interrupt control
void uart_hal_irq_enable(uart_hal_t *hal);
void uart_hal_irq_disable(uart_hal_t *hal);

// ISR handler (called from Zephyr interrupt)
void uart_hal_isr_handler(const struct device *dev, void *user_data);

#endif