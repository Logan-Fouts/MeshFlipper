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
#include "communication/ring_buffer.h"
#include "display/weact_epd154.h"
#include "ui/screen_ui.h"
#include "cb_args.h"

const struct device *uart_dev = DEVICE_DT_GET(DT_NODELABEL(uart0));

// Ring buffer instance (global so uart_comms.c can access it via extern)
ring_buffer_t msg_ring_buffer;

// Thread stack and control block for message processing
K_THREAD_STACK_DEFINE(msg_task_stack, 4096);
static struct k_thread msg_task_thread;

// Initialize message history and node list with zero count and default values.
struct messageHistory message_history = { .count = 0 };
struct nodeHistory node_list = { .count = 0 };
struct cb_args user_cb_args = { .message_history = &message_history, .node_list = &node_list };

static bool wait_for_my_node_info(int timeout_ms)
{
    int elapsed_ms = 0;

    while (!node_list.my_info.valid && elapsed_ms < timeout_ms) {
        send_want_config();
        k_sleep(K_SECONDS(2));
        elapsed_ms += 2000;
    }

    return node_list.my_info.valid;
}

int setup()
{
    if (!device_is_ready(uart_dev)) {
        printk("Error: UART device is not ready\n");
        return 0;
    }

    // Pass messages list and node list to the UART callback
    if (uart_irq_callback_user_data_set(uart_dev, uart_cb, &user_cb_args) < 0) {
        printk("Error: cannot set UART callback\n");
        return 0;
    }

    // Enable RX interrupts
    uart_irq_rx_enable(uart_dev);

    printk("UART listener ready on uart0 @ 115200. Waiting for Meshtastic frames.\n");

    // Send want_config_id to trigger radio to send config and node db
    if (send_want_config() < 0) {
        printk("Error: failed to send want_config message\n");
        return 0;
    }


    int epd_ret = weact_epd154_init();
    if (epd_ret == 0) {
        epd_ret = weact_epd154_show_boot_pattern();
        if (epd_ret < 0) {
            printk("EPD boot pattern failed: %d\n", epd_ret);
        }
    } else {
        printk("EPD init failed: %d\n", epd_ret);
    }

    ring_buffer_init(&msg_ring_buffer);


    return 1;
}

void process_messages_task(void)
{
    meshtastic_FromRadio msg;
    
    while (1) {
        // Wait for a message indefinitely
        if (ring_buffer_wait(&msg_ring_buffer, K_FOREVER)) {
            // Get all available messages
            while (ring_buffer_get(&msg_ring_buffer, &msg)) {
                // Process the message
                if (msg.which_payload_variant == meshtastic_FromRadio_queueStatus_tag) {
                    printk("QueueStatus: res=%d free=%u/%u mesh_packet_id=%u\n",
                            (int)msg.queueStatus.res,
                            (unsigned int)msg.queueStatus.free,
                            (unsigned int)msg.queueStatus.maxlen,
                            (unsigned int)msg.queueStatus.mesh_packet_id);
                }

                if (msg.which_payload_variant == meshtastic_FromRadio_my_info_tag ||
                    msg.which_payload_variant == meshtastic_FromRadio_node_info_tag) {
                    update_node_history(&node_list, &msg);
                }

                if (msg.which_payload_variant == meshtastic_FromRadio_packet_tag &&
                    msg.packet.which_payload_variant == meshtastic_MeshPacket_decoded_tag &&
                    msg.packet.decoded.portnum == meshtastic_PortNum_TEXT_MESSAGE_APP) {
                    update_message_history(&message_history, &msg);
                    int ret = screen_ui_refresh(&message_history, &node_list);
                    if (ret < 0) {
                        printk("EPD screen update failed: %d\n", ret);
                    }
                }
            }
        }
    }
}

void run_loop(int64_t next_want_config_ms, int64_t want_config_interval_ms)
{
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
        printk("║   • Ring buffer drops: %-8zu                                   \n", ring_buffer_get_dropped(&msg_ring_buffer));
        printk("╠══════════════════════════════════════════════════════════════╣\n");
        printk("║ Message Store                                                 \n");
        printk("║   • Total messages   : %-8zu                                   \n", message_history.count);
        printk("║   • Total nodes      : %-8zu                                   \n", node_list.count);
        printk("╚══════════════════════════════════════════════════════════════╝\n");

        // ========== MESSAGE HISTORY ==========
        if (message_history.count > 0) {
            printk("\n━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n");
            printk("  COMPLETE MESSAGE HISTORY (%zu messages)\n", message_history.count);
            printk("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n");
            print_message_history(&message_history);
        } else {
            printk("\n  No messages received yet.\n");
        }

    }
}

int main(void)
{
    printk("Starting MeshFlipper...\n");

    if (setup() == 0) {
        printk("Setup failed. Exiting.\n");
        return -1;
    }

    int ui_ret = screen_ui_refresh(&message_history, &node_list);
    if (ui_ret < 0) {
        printk("EPD screen update failed: %d\n", ui_ret);
    }

    // Create message processing thread
    k_thread_create(&msg_task_thread, msg_task_stack,
        K_THREAD_STACK_SIZEOF(msg_task_stack),
        (k_thread_entry_t)process_messages_task,
        NULL, NULL, NULL,
        K_PRIO_PREEMPT(5), 0, K_NO_WAIT);

    if (!wait_for_my_node_info(30000)) {
        printk("MyNodeInfo not received yet; skipping startup test send.\n");
    }

    print_my_node_info(&node_list.my_info);

    if (node_list.my_info.valid) {
        int node_num = -160769812;
        int ret = send_message_to_node(node_num, "Pico->Heltec->Mesh Ping Pong", node_list.my_info.num, &message_history);
        if (ret < 0) {
            printk("Startup test send failed: %d\n", ret);
        }
    }
    
    // Periodically send want_config_id
    const int64_t want_config_interval_ms = 2LL * 60LL * 1000LL; // 2 minutes
    int64_t next_want_config_ms = k_uptime_get() + want_config_interval_ms;

    run_loop(next_want_config_ms, want_config_interval_ms);

    return 0;
}