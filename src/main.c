#include <stdio.h>
#include <zephyr/kernel.h>
#include <zephyr/drivers/uart.h>

#include "models/node.h"
#include "models/message.h"


#define RX_MSG_MAX 256

const struct device *uart_dev = DEVICE_DT_GET(DT_NODELABEL(uart0));
static uint8_t rx_msg[RX_MSG_MAX];
static size_t rx_pos;

// UART interrupt callback function to handle incoming data, read bytes into a buffer until newline.
static void uart_cb(const struct device *dev, void *user_data)
{
    ARG_UNUSED(user_data);

    if (!uart_irq_update(dev)) {
        return;
    }

    // If the interrupt for rx is ready, and there is pending data, read it.
    while (uart_irq_is_pending(dev) && uart_irq_rx_ready(dev)) {
        uint8_t c;
        int recv = uart_fifo_read(dev, &c, 1);

        // If current byte read is <= 0, it means there is no more data to read, so we can exit the loop.
        if (recv <= 0) {
            break;
        }

        // Ignore carriage return characters as this is not a typewritter.
        if (c == '\r') {
            continue;
        }

        if (c == '\n') {
            rx_msg[rx_pos] = '\0';
            if (rx_pos > 0) {
                printk("RX: %s\n", rx_msg);
            }
            rx_pos = 0;
            continue;
        }

        // If we have room in the buffer, add the byte. Otherwise, reset the buffer and log an overflow.
        if (rx_pos < (RX_MSG_MAX - 1)) {
            rx_msg[rx_pos++] = c;
        } else {
            rx_pos = 0;
            printk("RX overflow, dropping message\n");
        }
    }
}

int main(void)
{
    printk("Starting MeshFlipper...\n");

    if (!device_is_ready(uart_dev)) {
        printk("Error: UART device is not ready\n");
        return 0;
    }

    if (uart_irq_callback_user_data_set(uart_dev, uart_cb, NULL) < 0) {
        printk("Error: cannot set UART callback\n");
        return 0;
    }

    // Enable RX interrupts for the UART device
    uart_irq_rx_enable(uart_dev);
    printk("UART listener ready on uart0 @ 115200. Send text ending with newline.\n");


    while (1) {
        k_sleep(K_SECONDS(2));
    }



    return 0;
}