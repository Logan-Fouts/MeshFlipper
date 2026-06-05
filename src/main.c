#include <stdio.h>
#include <stdbool.h>
#include <stdint.h>
#include <errno.h>
#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/drivers/uart.h>

#include "models/node.h"
#include "models/message.h"
#include "communication/manage_pb.h"
#include "communication/uart_comms.h"



const struct device *uart_dev = DEVICE_DT_GET(DT_NODELABEL(uart0));

struct messageHistory message_history = { .count = 0 };

int main(void)
{
    printk("Starting MeshFlipper...\n");

    if (!device_is_ready(uart_dev)) {
        printk("Error: UART device is not ready\n");
        return 0;
    }

    // Pass is messages list and count to the UART callback so it can store incoming messages for later retrieval.
    if (uart_irq_callback_user_data_set(uart_dev, uart_cb, &message_history) < 0) {
        printk("Error: cannot set UART callback\n");
        return 0;
    }

    // Enable RX interrupts to start receiving data. The callback will handle data as it comes in and assemble complete frames.
    uart_irq_rx_enable(uart_dev);
    printk("UART listener ready on uart0 @ 115200. Waiting for Meshtastic frames.\n");

    // Send a want_config_id message to the radio to trigger the radio to send its config and node db on startup.
    if (send_want_config() < 0) {
        printk("Failed to send want_config_id\n");
    }

    while (1) {
        k_sleep(K_SECONDS(30));
        printk("\n\n~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~\n");
        printk("Message history: %zu messages stored\n", message_history.count);
        for (size_t i = 0; i < message_history.count && i < MAX_MESSAGE_HISTORY; i++) {
            printk("Message %zu: id=%d from=%d to=%d text=%s\n",
                   i, message_history.messages[i].id,
                   message_history.messages[i].from,
                   message_history.messages[i].to,
                   message_history.messages[i].text);
        }
    }

    return 0;
}