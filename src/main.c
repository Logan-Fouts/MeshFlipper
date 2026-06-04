#include <stdio.h>
#include <zephyr/kernel.h>
#include <zephyr/drivers/uart.h>

#include "models/node.h"
#include "models/message.h"


const struct device *uart_dev = DEVICE_DT_GET(DT_NODELABEL(uart0));

int main(void)
{
    printk("Starting MeshFlipper...\n");

    if (!device_is_ready(uart_dev)) {
        printk("Error: UART device is not ready\n");
        return 0;
    }

    const char *msg_packet = "Hello from MeshFlipper";
    struct message test_message = parse_message(msg_packet);

    const char* node_packet = "Node data packet";
    struct node test_node = parse_node(node_packet);

    while (1) {
        k_sleep(K_SECONDS(10));
        print_message(&test_message);
        printk("\n");
        print_node(&test_node);
    }



    return 0;
}