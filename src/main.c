#include <stdio.h>
#include <zephyr/kernel.h>
#include <zephyr/drivers/uart.h>

#include "hardware/uart_hal.h"
#include "comms/uart_comms.h"
#include "models/ring_buffer.h"
#include "models/mesh_message.h"
#include "models/mesh_node.h"
#include "tasks/message_processor.h"
#include "comms/protobuf_handler.h"

// ==================
// GLOBAL INSTANCES
// ==================

struct messageHistory g_message_history = { .count = 0 };
struct nodeHistory g_node_list = { .count = 0 };
ring_buffer_t g_msg_ring_buffer;

// ==================
// SETUP FUNCTIONS
// ==================

// UART interrupt handled by UART HAL, which forwards bytes to uart_comms for processing.
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
        printk("Message processor init failed\n");
        return -1;
    } 

    if (message_processor_start() != 0) {
        printk("Message processor start failed\n");
        return -1;
    }

    return 0;
}

// Sets up the UART HAL, message processor, and any other necessary components. Returns 0 on success, -1 on failure.
static int setup(void) 
{
    ring_buffer_init(&g_msg_ring_buffer);

    if (setup_uart_hal() != 0) {
        return -1;
    }

    if (setup_message_processor() != 0) {
        return -1;
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
        printk("My node info received!\n");
        print_my_node_info(&g_node_list.my_info);
    } else {
        printk("Failed to get my node info\n");
    }
    
    print_my_node_info(&g_node_list.my_info);

    // Main loop
    while (1) {
        k_sleep(K_SECONDS(5));
        print_node_history_brief(&g_node_list);
        print_message_history(&g_message_history);
    }
    
    return 0;
}