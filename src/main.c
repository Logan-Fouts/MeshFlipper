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

const struct device *uart_dev = DEVICE_DT_GET(DT_NODELABEL(uart0));
static const struct device *gpio_dev = DEVICE_DT_GET(DT_NODELABEL(gpio0));

#define BUTTON_PREV_PIN 2
#define BUTTON_PRIMARY_PIN 3
#define BUTTON_NEXT_PIN 4
#define BUTTON_SECONDARY_PIN 5
#define BUTTON_POLL_MS 60
#define BUTTON_HOME_HOLD_MS 700

// Ring buffer instance (global so uart_comms.c can access it via extern)
ring_buffer_t msg_ring_buffer;

// Thread stack and control block for message processing
K_THREAD_STACK_DEFINE(msg_task_stack, 4096);
static struct k_thread msg_task_thread;

// Initialize message history and node list with zero count and default values.
struct messageHistory message_history = { .count = 0 };
struct nodeHistory node_list = { .count = 0 };
struct cb_args user_cb_args = { .message_history = &message_history, .node_list = &node_list };

struct button_state {
    uint8_t pin;
    int last_level;
    int64_t pressed_since_ms;
    bool long_press_handled;
};

static struct button_state buttons[] = {
    { .pin = BUTTON_PREV_PIN, .last_level = 1, .pressed_since_ms = 0, .long_press_handled = false },
    { .pin = BUTTON_PRIMARY_PIN, .last_level = 1, .pressed_since_ms = 0, .long_press_handled = false },
    { .pin = BUTTON_NEXT_PIN, .last_level = 1, .pressed_since_ms = 0, .long_press_handled = false },
    { .pin = BUTTON_SECONDARY_PIN, .last_level = 1, .pressed_since_ms = 0, .long_press_handled = false },
};

static int configure_button_inputs(void)
{
    if (!device_is_ready(gpio_dev)) {
        printk("Error: GPIO device is not ready\n");
        return -ENODEV;
    }

    for (size_t i = 0; i < ARRAY_SIZE(buttons); i++) {
        int ret = gpio_pin_configure(gpio_dev, buttons[i].pin, GPIO_INPUT | GPIO_PULL_UP);
        if (ret < 0) {
            printk("Error: failed to configure GPIO%u (%d)\n", buttons[i].pin, ret);
            return ret;
        }

        buttons[i].last_level = gpio_pin_get(gpio_dev, buttons[i].pin);
        buttons[i].pressed_since_ms = 0;
        buttons[i].long_press_handled = false;
    }

    return 0;
}

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

    if (configure_button_inputs() < 0) {
        return 0;
    }

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

static void poll_buttons_and_drive_ui(void)
{
    for (size_t i = 0; i < ARRAY_SIZE(buttons); i++) {
        int level = gpio_pin_get(gpio_dev, buttons[i].pin);
        if (level < 0) {
            continue;
        }

        int previous_level = buttons[i].last_level;
        bool falling_edge = (previous_level > 0 && level == 0);
        bool rising_edge = (previous_level == 0 && level > 0);
        bool is_secondary = (buttons[i].pin == BUTTON_SECONDARY_PIN);

        if (falling_edge) {
            buttons[i].pressed_since_ms = k_uptime_get();
            buttons[i].long_press_handled = false;
        }

        if (is_secondary && level == 0 && !buttons[i].long_press_handled &&
            buttons[i].pressed_since_ms > 0 &&
            (k_uptime_get() - buttons[i].pressed_since_ms) >= BUTTON_HOME_HOLD_MS) {
            int ui_ret = screen_ui_handle_action(&message_history, &node_list, SCREEN_UI_ACTION_HOME);
            if (ui_ret < 0) {
                printk("UI home action failed: %d\n", ui_ret);
            }
            buttons[i].long_press_handled = true;
        }
        buttons[i].last_level = level;

        if (rising_edge) {
            buttons[i].pressed_since_ms = 0;
        }

        bool trigger_action = false;
        if (is_secondary) {
            trigger_action = rising_edge && !buttons[i].long_press_handled;
        } else {
            trigger_action = falling_edge;
        }

        if (!trigger_action) {
            continue;
        }

        enum screen_ui_action action;
        if (buttons[i].pin == BUTTON_PREV_PIN) {
            action = SCREEN_UI_ACTION_PREVIOUS;
        } else if (buttons[i].pin == BUTTON_NEXT_PIN) {
            action = SCREEN_UI_ACTION_NEXT;
        } else if (buttons[i].pin == BUTTON_PRIMARY_PIN) {
            action = SCREEN_UI_ACTION_PRIMARY;
        } else {
            action = SCREEN_UI_ACTION_SECONDARY;
        }

        int ui_ret = screen_ui_handle_action(&message_history, &node_list, action);
        if (ui_ret < 0) {
            printk("UI action failed: %d\n", ui_ret);
        }

        struct screen_ui_outgoing outgoing;
        if (!screen_ui_take_outgoing(&outgoing) || !outgoing.valid) {
            continue;
        }

        if (!node_list.my_info.valid) {
            printk("Cannot send yet: my node info not ready\n");
            screen_ui_refresh(&message_history, &node_list);
            continue;
        }

        int send_ret = send_message_to_node(outgoing.target_node,
                                            outgoing.text,
                                            node_list.my_info.num,
                                            &message_history);
        if (send_ret < 0) {
            printk("Send failed: %d\n", send_ret);
        }

        screen_ui_refresh(&message_history, &node_list);
    }
}

void run_loop(int64_t next_want_config_ms, int64_t want_config_interval_ms)
{
    int64_t next_stats_ms = k_uptime_get() + 10000;

    while (1) {
        k_sleep(K_MSEC(BUTTON_POLL_MS));
        poll_buttons_and_drive_ui();

        int64_t now_ms = k_uptime_get();
        if (now_ms >= next_want_config_ms) {
            send_want_config();
            next_want_config_ms = now_ms + want_config_interval_ms;
        }

        if (now_ms < next_stats_ms) {
            continue;
        }

        next_stats_ms = now_ms + 10000;

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

    // Periodically send want_config_id
    const int64_t want_config_interval_ms = 2LL * 60LL * 1000LL; // 2 minutes
    int64_t next_want_config_ms = k_uptime_get() + want_config_interval_ms;

    run_loop(next_want_config_ms, want_config_interval_ms);

    return 0;
}