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

// Initialize message history and node list with zero count and default values. These will be populated as messages and nodes are received from the radio.
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

void run_loop(int64_t next_want_config_ms, int64_t want_config_interval_ms)
{
    int node_num = -160769812;

    while (1) {
        k_sleep(K_SECONDS(10));

        int64_t now_ms = k_uptime_get();
        if (now_ms >= next_want_config_ms) {
            send_want_config();
            next_want_config_ms = now_ms + want_config_interval_ms;
        }

        // ========== STATISTICS SECTION ==========
        printk("\n\n");
        printk("╔══════════════════════════════════════════════════════════════╗\n");
        printk("║                    SYSTEM STATUS REPORT                      ║\n");
        printk("╠══════════════════════════════════════════════════════════════╣\n");
        printk("║ UART RX Statistics                                           \n");
        printk("║   • Frames received : %-8zu                                   \n", rx_frames_received);
        printk("║   • Frames dropped   : %-8zu                                   \n", rx_frames_dropped);
        printk("║   • Bytes processed  : %-8zu                                   \n", rx_bytes_processed);
        printk("╠══════════════════════════════════════════════════════════════╣\n");
        printk("║ Message Store                                                 \n");
        printk("║   • Total messages   : %-8zu                                   \n", message_history.count);
        printk("║   • Total nodes      : %-8zu                                   \n", node_list.count);
        printk("╚══════════════════════════════════════════════════════════════╝\n");

        // ========== MESSAGE HISTORY (all messages) ==========
        if (message_history.count > 0) {
            printk("\n━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n");
            printk("  COMPLETE MESSAGE HISTORY (%zu messages)\n", message_history.count);
            printk("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n");
            print_message_history(&message_history);
        } else {
            printk("\n  No messages received yet.\n");
        }

        printk("\nNext report in 10 seconds...\n");
    }
}

int main(void)
{
    printk("Starting MeshFlipper...\n");

    if (setup() == 0) {
        printk("Setup failed. Exiting.\n");
        return -1;
    }


    // Wait briefly for MyNodeInfo to arrive before trying to send outbound traffic.
    for (int i = 0; i < 20 && !node_list.my_info.valid; i++) {
        k_sleep(K_MSEC(250));
    }

    print_my_node_info(&node_list.my_info);
    k_sleep(K_SECONDS(5));
    print_node_history_brief(&node_list);


    // Periodically send want config id to get all updates
    const int64_t want_config_interval_ms = 2LL * 60LL * 1000LL; // Use long long to avoid overflow
    int64_t next_want_config_ms = k_uptime_get() + want_config_interval_ms;


    // TODO: (Remove) Test sending a message to a specific node. 
    k_sleep(K_SECONDS(3));
    int node_num = -160769812;
    int ret = send_message_to_node(node_num, "Pico->Heltec->Mesh Ping Pong", node_list.my_info.num);


    print_node_history(&node_list);

    run_loop(next_want_config_ms, want_config_interval_ms);

    return 0;
}