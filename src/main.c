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

    // TODO: Not sure if sending want more than at the begining is really needed, but was stuggling with reciveing packet stability.
    const int64_t want_config_interval_ms = 5LL * 60LL * 1000LL;
    int64_t next_want_config_ms = k_uptime_get() + want_config_interval_ms;
     
    while (1) {
        k_sleep(K_SECONDS(10));

        int64_t now_ms = k_uptime_get();
        if (now_ms >= next_want_config_ms) {
            if (send_want_config() < 0) {
                printk("Periodic want_config_id send failed\n");

            }
            next_want_config_ms = now_ms + want_config_interval_ms;
        }

        printk("\n\n~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~\n");
        printk("RX stats: %zu frames received, %zu dropped, %zu bytes processed\n",
               rx_frames_received, rx_frames_dropped, rx_bytes_processed);

        // Aquire lock on message history before accessing it
        k_spinlock_key_t key = k_spin_lock(&message_history.lock);
        size_t history_count = message_history.count;
        size_t snapshot_count = history_count < MAX_MESSAGE_HISTORY ? history_count : MAX_MESSAGE_HISTORY;
        size_t start_index = history_count > MAX_MESSAGE_HISTORY ? (history_count % MAX_MESSAGE_HISTORY) : 0;
        k_spin_unlock(&message_history.lock, key);

        printk("Message history: %zu messages stored\n", history_count);
        for (size_t i = 0; i < snapshot_count; i++) {
            size_t ring_index = (start_index + i) % MAX_MESSAGE_HISTORY;
            size_t logical_index = history_count - snapshot_count + i;
            struct message msg_copy;

            //Breifly aquire lock before accessing specific mesage from history
            key = k_spin_lock(&message_history.lock);
            msg_copy = message_history.messages[ring_index];
            k_spin_unlock(&message_history.lock, key);

            printk("Message %zu: id=%d from=%d to=%d text=%s\n",
                   logical_index, msg_copy.id,
                   msg_copy.from,
                   msg_copy.to,
                   msg_copy.text);
        }
    }

    return 0;
}