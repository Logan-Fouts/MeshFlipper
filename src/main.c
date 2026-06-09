#include <stdio.h>
#include <stdbool.h>
#include <stdint.h>
#include <errno.h>
#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/uart.h>

#include "models/node.h"
#include "models/message.h"
#include "communication/manage_pb.h"
#include "communication/uart_comms.h"
#include "communication/ring_buffer.h"
#include "display/weact_epd154.h"
#include "ui/screen_ui.h"
#include "cb_args.h"
#include "ui/buttons.h"


const struct device *uart_dev = DEVICE_DT_GET(DT_NODELABEL(uart0));
static const struct device *gpio_dev = DEVICE_DT_GET(DT_NODELABEL(gpio0));

ring_buffer_t msg_ring_buffer;

// Allocate stack for the message processing thread. Size is set to 4096 bytes to accommodate potential large messages and processing time, especially when updating the EPD screen which can be resource-intensive.
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

int setup(struct button_state *buttons, int num_buttons)
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

    if (configure_button_inputs(buttons, gpio_dev, num_buttons) < 0) {
        return 0;
    }

    return 1;
}

void print_debug_stats()
{
    printk("\n\n");
    printk("╔══════════════════════════════════════════════════════════════╗\n");
    printk("║                    SYSTEM STATUS REPORT                      ║\n");
    printk("╠══════════════════════════════════════════════════════════════╣\n");
    printk("║ Message Store                                                 \n");
    printk("║   • Total messages   : %-8zu                                   \n", message_history.count);
    printk("║   • Total nodes      : %-8zu                                   \n", node_list.count);
    printk("╚══════════════════════════════════════════════════════════════╝\n");
    if (message_history.count > 0) {
        printk("\n━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n");
        printk("  COMPLETE MESSAGE HISTORY (%zu messages)\n", message_history.count);
        printk("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n");
        print_message_history(&message_history);
    } else {
        printk("\n  No messages received yet.\n");
    }
}

void process_messages_task(void)
{
    meshtastic_FromRadio msg;
    
    while (1) {
        if (ring_buffer_wait(&msg_ring_buffer, K_FOREVER)) { // Wait for a message to be available in the ring buffer
            while (ring_buffer_get(&msg_ring_buffer, &msg)) { // Get all available messages
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
                    // Skip messages that I sent, since they will already be in the history and we don't want to trigger a UI update that would cause them to jump to the inbox screen.
                    if (node_list.my_info.valid && msg.packet.from == node_list.my_info.num) {
                        continue;
                    }

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

static void poll_buttons_and_drive_ui(struct button_state *buttons, int num_buttons)
{
    for (size_t i = 0; i < num_buttons; i++) {
        bool falling_edge, rising_edge, is_secondary;

        if (!process_button_state(&buttons[i], &falling_edge, &rising_edge, &is_secondary, gpio_dev, &message_history, &node_list)) {
            continue;
        }

        drive_ui(&buttons[i], falling_edge, rising_edge, is_secondary, &message_history, &node_list);
        
    }
}

void run_loop(int64_t want_config_interval_ms, struct button_state *buttons, int num_buttons, bool print_stats)
{
    int64_t next_want_config_ms = k_uptime_get() + want_config_interval_ms;
    int64_t next_stats_ms = k_uptime_get() + 10000;

    while (1) {
        k_sleep(K_MSEC(BUTTON_POLL_MS));
        poll_buttons_and_drive_ui(buttons, num_buttons);

        int64_t now_ms = k_uptime_get();
        if (now_ms >= next_want_config_ms) {
            send_want_config();
            next_want_config_ms = now_ms + want_config_interval_ms;
        }

        if (print_stats) {
            if (now_ms < next_stats_ms) continue;
            next_stats_ms = now_ms + 10000;
            print_debug_stats();
        }
    }
}

int main(void)
{
    printk("Starting MeshFlipper...\n");
    bool debug_stats = false;
    const int max_node_info_wait_time = 30000; // 30 seconds
    const int64_t want_config_interval_ms = 2LL * 60LL * 1000LL; // 2 minutes

    struct button_state buttons[] = {
        { .pin = BUTTON_PREV_PIN, .last_level = 1, .prev_press_time = 0, .long_press_handled = false },
        { .pin = BUTTON_PRIMARY_PIN, .last_level = 1, .prev_press_time = 0, .long_press_handled = false },
        { .pin = BUTTON_NEXT_PIN, .last_level = 1, .prev_press_time = 0, .long_press_handled = false },
        { .pin = BUTTON_SECONDARY_PIN, .last_level = 1, .prev_press_time = 0, .long_press_handled = false },
    };

    if (setup(buttons, ARRAY_SIZE(buttons)) == 0) {
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

    if (!wait_for_my_node_info(max_node_info_wait_time)) {
        printk("Could not get my node info after %d seconds. Exiting.\n", max_node_info_wait_time / 1000);
        return -1;
    }


    run_loop(want_config_interval_ms, buttons, ARRAY_SIZE(buttons), debug_stats);

    return 0;
}