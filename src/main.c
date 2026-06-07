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
#include "cb_args.h"


const struct device *uart_dev = DEVICE_DT_GET(DT_NODELABEL(uart0));
struct messageHistory message_history = { .count = 0 };
struct nodeHistory node_list = { .count = 0 };
struct cb_args user_cb_args = { .message_history = &message_history, .node_list = &node_list };

int setup()
{
    if (!device_is_ready(uart_dev)) {
        printk("Error: UART device is not ready\n");
        return 0;
    }

    // Pass messages list and node list to the UART callback so it can store incoming messages for later retrieval.
    if (uart_irq_callback_user_data_set(uart_dev, uart_cb, &user_cb_args) < 0) {
        printk("Error: cannot set UART callback\n");
        return 0;
    }

    // Enable RX interrupts to start receiving data. The callback will handle data as it comes in and assemble complete frames.
    uart_irq_rx_enable(uart_dev);

    printk("UART listener ready on uart0 @ 115200. Waiting for Meshtastic frames.\n");

    // Send a want_config_id message to the radio to trigger the radio to send its config and node db on startup.
    if (send_want_config() < 0) {
        printk("Error: failed to send want_config message\n");
        return 0;
    }

    return 1;
}

int main(void)
{
    printk("Starting MeshFlipper...\n");

    if (setup() == 0) {
        printk("Setup failed. Exiting.\n");
        return -1;
    }

    k_sleep(K_SECONDS(2));
    print_my_node_info(&node_list.my_info);

    printk("Node history: %zu nodes stored\n", node_list.count);
    print_node_history_brief(&node_list);

    // TODO: Needs testing. Not sure if sending want more than at the begining is really needed, but was stuggling with reciveing packet stability.
    const int64_t want_config_interval_ms = 5LL * 60LL * 1000LL;
    int64_t next_want_config_ms = k_uptime_get() + want_config_interval_ms;
     
    while (1) {
        k_sleep(K_SECONDS(10));

        int64_t now_ms = k_uptime_get();
        if (now_ms >= next_want_config_ms) {
            send_want_config();
            next_want_config_ms = now_ms + want_config_interval_ms;
        }

        printk("\n\n~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~\n");
        printk("RX stats: %zu frames received, %zu dropped, %zu bytes processed\n",
               rx_frames_received, rx_frames_dropped, rx_bytes_processed);

        printk("Message history: %zu messages stored\n", message_history.count);
        print_message_history(&message_history);
    }

    return 0;
}