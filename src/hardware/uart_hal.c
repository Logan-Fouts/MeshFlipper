#include "hardware/uart_hal.h"
#include <zephyr/kernel.h>
#include <zephyr/drivers/uart.h>
#include <string.h>
#include <errno.h>

// Global UART HAL instance
uart_hal_t g_uart_hal = {
    .dev = NULL,
    .initialized = false,
    .rx_callback = NULL,
    .callback_context = NULL
};

int uart_hal_init(uart_hal_t *hal, const struct device *dev)
{
    if (!hal || !dev) return -EINVAL;
    if (!device_is_ready(dev)) {
        printk("UART device not ready\n");
        return -ENODEV;
    }
    
    hal->dev = dev;
    hal->initialized = true;
    
    // Set default config
    hal->config.baud_rate = 115200;
    hal->config.data_bits = 8;
    hal->config.stop_bits = 1;
    hal->config.parity = 0;
    
    return 0;
}

int uart_hal_configure(uart_hal_t *hal, const uart_config_t *config)
{
    if (!hal || !hal->initialized) return -EINVAL;
    if (!config) return -EINVAL;
    
    // Build Zephyr UART configuration structure
    struct uart_config uart_cfg = {
        .baudrate = config->baud_rate,
        .data_bits = config->data_bits,
        .stop_bits = config->stop_bits,
        .parity = config->parity,
        .flow_ctrl = config->flow_ctrl,
    };
    
    // Apply configuration to the hardware using Zephyr API
    int ret = uart_configure(hal->dev, &uart_cfg);
    if (ret != 0) {
        printk("Failed to configure UART: %d\n", ret);
        return ret;
    }
    
    // Store config
    hal->config = *config;
    
    return 0;
}

int uart_hal_configure_default(uart_hal_t *hal)
{
    uart_config_t config = {
        .baud_rate = 115200,
        .data_bits = UART_CFG_DATA_BITS_8,
        .stop_bits = UART_CFG_STOP_BITS_1,
        .parity = UART_CFG_PARITY_NONE,
        .flow_ctrl = UART_CFG_FLOW_CTRL_NONE,
    };
    return uart_hal_configure(hal, &config);
}

void uart_hal_set_rx_callback(uart_hal_t *hal, 
                              void (*callback)(uint8_t byte, void *context), 
                              void *context)
{
    if (hal) {
        hal->rx_callback = callback;
        hal->callback_context = context;
    }
}

int uart_hal_send_byte(uart_hal_t *hal, uint8_t byte)
{
    if (!hal || !hal->initialized || !hal->dev) return -EINVAL;
    uart_poll_out(hal->dev, byte);
    return 0;
}

int uart_hal_send_bytes(uart_hal_t *hal, const uint8_t *data, size_t len)
{
    if (!hal || !hal->initialized || !hal->dev || !data) return -EINVAL;
    
    for (size_t i = 0; i < len; i++) {
        uart_poll_out(hal->dev, data[i]);
    }
    return 0;
}

void uart_hal_irq_enable(uart_hal_t *hal)
{
    if (hal && hal->initialized && hal->dev) {
        uart_irq_rx_enable(hal->dev);
    }
}

void uart_hal_irq_disable(uart_hal_t *hal)
{
    if (hal && hal->initialized && hal->dev) {
        uart_irq_rx_disable(hal->dev);
    }
}

// ISR Handler - routes bytes to callback
void uart_hal_isr_handler(const struct device *dev, void *user_data)
{
    uart_hal_t *hal = &g_uart_hal;
    if (!hal->initialized || !hal->rx_callback) return;
    
    uart_irq_update(dev);
    
    // Read ALL available bytes quickly
    while (uart_irq_rx_ready(dev)) {
        uint8_t byte;
        int recv = uart_fifo_read(dev, &byte, 1);
        if (recv <= 0) break;
        
        // Pass to callback
        hal->rx_callback(byte, hal->callback_context);
    }
}