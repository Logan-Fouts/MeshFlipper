#include <stdio.h>
#include <zephyr/kernel.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/logging/log.h>

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

LOG_MODULE_REGISTER(main, LOG_LEVEL_INF);

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
        LOG_ERR("UART HAL init failed");
        return -1;
    }

    if (uart_hal_configure_default(&g_uart_hal) != 0) {
        LOG_ERR("UART configure failed");
        return -1;
    }

    // Initialize uart_comms
    uart_comms_init(&g_uart_comms, &g_uart_hal);

    // Set UART callback to HAL's ISR handler
    uart_irq_callback_user_data_set(uart_dev, uart_hal_isr_handler, &g_uart_hal);

    // Set the HAL callback to forward to uart_comms
    uart_hal_set_rx_callback(&g_uart_hal, uart_comms_process_byte, &g_uart_comms);
    
    // Enable interrupts
    uart_hal_irq_enable(&g_uart_hal);
    
    return 0;
}

static int setup_message_processor(void) 
{
    if (message_processor_init(&g_msg_ring_buffer, 
                               &g_message_history, 
                               &g_node_list) != 0) {
        LOG_ERR("Message processor init failed");
        return -1;
    } 

    if (message_processor_start() != 0) {
        LOG_ERR("Message processor start failed");
        return -1;
    }

    return 0;
}

static void test_display_pins(void)
{
    printk("=== TESTING DISPLAY PINS ===\n");
    
    const struct device *gpio_dev = DEVICE_DT_GET(DT_NODELABEL(gpio0));
    if (!device_is_ready(gpio_dev)) {
        printk("GPIO not ready!\n");
        return;
    }
    printk("GPIO is ready\n");
    
    int pins[] = {17, 20, 21, 22};
    const char *pin_names[] = {"CS", "DC", "RST", "BUSY"};
    
    for (int i = 0; i < 4; i++) {
        struct gpio_dt_spec pin = {
            .port = gpio_dev,
            .pin = pins[i],
            .dt_flags = GPIO_ACTIVE_HIGH,
        };
        
        int ret = gpio_pin_configure_dt(&pin, GPIO_OUTPUT);
        if (ret < 0) {
            printk("Failed to configure pin %d (%s): %d\n", pins[i], pin_names[i], ret);
            continue;
        }
        printk("Pin %d (%s) configured\n", pins[i], pin_names[i]);
        
        // Toggle 5 times
        for (int j = 0; j < 5; j++) {
            gpio_pin_set_dt(&pin, 1);
            k_sleep(K_MSEC(100));
            gpio_pin_set_dt(&pin, 0);
            k_sleep(K_MSEC(100));
        }
        printk("Pin %d (%s) toggled 5 times\n", pins[i], pin_names[i]);
    }
    
    printk("=== PIN TEST COMPLETE ===\n");
}

static int setup_display(void)
{
    printk("=== DISPLAY SETUP STARTING ===\n");
    
    display_hal_config_t hal_config = weact_display_get_config();
    
    printk("DISPLAY SETUP: Config SPI=%p, CS=%d, DC=%d, RST=%d, BUSY=%d\n",
           hal_config.spi_dev, hal_config.cs_pin.pin, 
           hal_config.dc_pin.pin, hal_config.rst_pin.pin, 
           hal_config.busy_pin.pin);
    
    printk("DISPLAY SETUP: Calling display_ui_init...\n");
    int ret = display_ui_init(&g_display_ui, &hal_config, &g_message_history, &g_node_list);
    if (ret < 0) {
        printk("DISPLAY SETUP: Display UI init failed: %d\n", ret);
        return -1;
    }
    printk("DISPLAY SETUP: Display UI initialized\n");
    
    // Show test pattern
    printk("DISPLAY SETUP: Showing test pattern...\n");
    display_ui_test_pattern(&g_display_ui);
    k_sleep(K_MSEC(3000));
    
    printk("DISPLAY SETUP: Showing boot screen...\n");
    display_ui_show_boot(&g_display_ui);
    k_sleep(K_MSEC(2000));
    
    printk("DISPLAY SETUP: COMPLETE!\n");
    return 0;
}

static int setup_buttons(void)
{
    printk("Setting up buttons...\n");
    int ret = ui_button_handler_init(&g_button_ctx, &g_display_ui);
    if (ret < 0) {
        LOG_ERR("Button handler init failed: %d", ret);
        return -1;
    }
    printk("Buttons setup complete\n");
    return 0;
}

static int setup(void) 
{
    ring_buffer_init(&g_msg_ring_buffer);

    if (setup_uart_hal() != 0) {
        return -1;
    }

    if (setup_message_processor() != 0) {
        return -1;
    }

    // Test display pins first
    test_display_pins();

    if (setup_display() != 0) {
        LOG_WRN("Display setup failed - continuing without display");
    }

    if (setup_buttons() != 0) {
        LOG_WRN("Button setup failed - continuing without buttons");
    }

    return 0;
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
    
    // Wait for my node info
    bool got_info = message_processor_wait_for_my_node_info(WAIT_FOR_NODE_INFO_TIMEOUT_MS);
    
    if (got_info) {
        printk("My node info received! Node num: %u\n", g_node_list.my_info.num);
    } else {
        printk("Failed to get my node info\n");
    }

    // Show inbox after getting node info
    if (g_display_ui.initialized) {
        printk("Showing inbox...\n");
        display_ui_show_inbox(&g_display_ui);
    } else {
        printk("Display not initialized!\n");
    }

    // Main loop
    while (1) {
        // Process button inputs
        ui_button_handler_process(&g_button_ctx);
        
        // Check for outgoing messages from UI
        struct screen_ui_outgoing outgoing;
        if (display_ui_take_outgoing(&g_display_ui, &outgoing)) {
            LOG_INF("Sending message to %d: %s", outgoing.target_node, outgoing.text);
            
            // TODO: Implement send_message_to_node function
            // For now, just print the message
            printk("Would send to %d: %s\n", outgoing.target_node, outgoing.text);
            
            /* Commented out until send_message_to_node is implemented
            int send_ret = send_message_to_node(outgoing.target_node, 
                                               outgoing.text, 
                                               g_node_list.my_info.num,
                                               &g_message_history);
            if (send_ret < 0) {
                LOG_ERR("Send failed: %d", send_ret);
            }
            */
        }
        
        // Check for new messages to display
        if (g_display_ui.initialized && g_message_history.count > 0) {
            int last_idx = g_message_history.count - 1;
            if (g_message_history.messages[last_idx].id != g_display_ui.last_handled_incoming_id) {
                // New message received - update display
                if (!g_display_ui.thread_active && !g_display_ui.compose_active) {
                    display_ui_refresh(&g_display_ui);
                    g_display_ui.last_handled_incoming_id = g_message_history.messages[last_idx].id;
                }
            }
        }
        
        k_sleep(K_MSEC(50));
    }
    
    return 0;
}