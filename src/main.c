#include <stdio.h>
#include <zephyr/kernel.h>
#include <zephyr/drivers/uart.h>

#include "hardware/uart_hal.h"
#include "hardware/display_hal_weact.h"
#include "comms/uart_comms.h"
#include "models/ring_buffer.h"
#include "models/mesh_message.h"
#include "models/mesh_node.h"
#include "tasks/message_processor.h"
#include "comms/protobuf_handler.h"
#include "ui/display_ui.h"
#include "ui/ui_button_handler.h"

// ==================
// GLOBAL INSTANCES
// ==================

struct messageHistory g_message_history = { .count = 0 };
struct nodeHistory g_node_list = { .count = 0 };
ring_buffer_t g_msg_ring_buffer;

// Display and UI instances
static display_ui_t g_display_ui;
static ui_button_context_t g_button_ctx;

// ==================
// SETUP FUNCTIONS
// ==================

static int setup_uart_hal(void) 
{
    const struct device *uart_dev = DEVICE_DT_GET(DT_NODELABEL(uart0));

    if (uart_hal_init(&g_uart_hal, uart_dev) != 0) {
        printk("UART HAL init failed\n");
        return -1;
    }

    if (uart_hal_configure_default(&g_uart_hal) != 0) {
        printk("UART configure failed\n");
        return -1;
    }

    uart_comms_init(&g_uart_comms, &g_uart_hal);

    // Set UART callback to HAL's ISR handler
    uart_irq_callback_user_data_set(uart_dev, uart_hal_isr_handler, &g_uart_hal);

    // Set the HAL callback to forward to uart_comms
    uart_hal_set_rx_callback(&g_uart_hal, uart_comms_process_byte, &g_uart_comms);
    
    uart_hal_irq_enable(&g_uart_hal);
    
    return 0;
}

static int setup_message_processor(void) 
{
    if (message_processor_init(&g_msg_ring_buffer, 
                               &g_message_history, 
                               &g_node_list) != 0) {
        printk("Message processor init failed\n");
        return -1;
    } 

    if (message_processor_start() != 0) {
        printk("Message processor start failed\n");
        return -1;
    }

    return 0;
}

static int setup_display(void)
{
    display_hal_config_t hal_config = weact_display_get_config();
    
    int ret = display_ui_init(&g_display_ui, &hal_config, &g_message_history, &g_node_list);
    if (ret < 0) {
        printk("DISPLAY SETUP: Display UI init failed: %d\n", ret);
        return -1;
    }
    message_processor_set_display_ui(&g_display_ui);
    display_ui_show_boot(&g_display_ui);

    return 0;
}

static int setup_buttons(void)
{
    int ret = ui_button_handler_init(&g_button_ctx, &g_display_ui);
    if (ret < 0) {
        printk("BUTTON SETUP: Button handler init failed: %d\n", ret);
        return -1;
    }

    // Start button handler thread
    ret = ui_button_handler_start();
    if (ret < 0) {
        printk("BUTTON SETUP: Button handler start failed: %d\n", ret);
        return -1;
    }

    return 0;
}

static int setup(void) 
{
    ring_buffer_init(&g_msg_ring_buffer);

    if (setup_uart_hal() != 0) return -1;

    if (setup_message_processor() != 0) return -1;

    if (setup_display() != 0) return -1;

    if (setup_buttons() != 0) return -1;

    return 0;
}


static void check_for_outgoing_messages()
{
    struct screen_ui_outgoing outgoing;
    if (display_ui_take_outgoing(&g_display_ui, &outgoing))
    {
        // Use the existing send_text_message function
        int send_ret = send_text_message((uint32_t)outgoing.target_node,
                                         outgoing.text,
                                         strlen(outgoing.text));
        if (send_ret < 0)
        {
            printk("Send failed: %d\n", send_ret);
        }
    }
}

static void check_new_messages()
{
    if (g_display_ui.initialized && g_message_history.count > 0)
    {
        int last_idx = g_message_history.count - 1;
        if (g_message_history.messages[last_idx].id != g_display_ui.last_handled_incoming_id)
        {
            // New message received - update display
            if (!g_display_ui.thread_active && !g_display_ui.compose_active)
            {
                display_ui_refresh(&g_display_ui);
                g_display_ui.last_handled_incoming_id = g_message_history.messages[last_idx].id;
            }
        }
    }
}

// ========
// MAIN
// ========

int main(void)
{
    printk("Starting MeshFlipper...\n");
    
    if (setup() != 0) {
        printk("Setup failed\n");
        return -1;
    }

    printk("MeshFlipper ready!\n");
    
    if (!message_processor_wait_for_my_node_info(WAIT_FOR_NODE_INFO_TIMEOUT_MS)) {
        printk("Failed to get my node info\n");
    } 

    // Show inbox after getting node info
    if (g_display_ui.initialized) {
        display_ui_show_inbox(&g_display_ui);
    } else {
        printk("Display not initialized!\n");
    }

    // Main loop - Check for outgoing messages, and refresh display on new messages
    while (1) {
        check_for_outgoing_messages();
        check_new_messages();

        k_sleep(K_MSEC(50));
    }
    
    return 0;
}
