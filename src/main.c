#include <stdio.h>
#include <stdbool.h>
#include <stdint.h>
#include <zephyr/kernel.h>
#include <zephyr/drivers/uart.h>

#include "models/node.h"
#include "models/message.h"


#define RX_FRAME_MAX 512
#define MESHTASTIC_START1 0x94
#define MESHTASTIC_START2 0xC3

enum rx_state {
    RX_WAIT_START1,
    RX_WAIT_START2,
    RX_WAIT_LEN_MSB,
    RX_WAIT_LEN_LSB,
    RX_READ_PAYLOAD,
};

const struct device *uart_dev = DEVICE_DT_GET(DT_NODELABEL(uart0));
static uint8_t rx_frame[RX_FRAME_MAX + 4];
static size_t rx_pos;
static size_t rx_expected_len;
static enum rx_state rx_state = RX_WAIT_START1;

static void rx_reset(void)
{
    rx_state = RX_WAIT_START1;
    rx_pos = 0;
    rx_expected_len = 0;
}

static void rx_consume_byte(uint8_t c)
{
    switch (rx_state) {
    case RX_WAIT_START1:
        if (c == MESHTASTIC_START1) {
            rx_frame[0] = c;
            rx_pos = 1;
            rx_state = RX_WAIT_START2;
        }
        break;

    case RX_WAIT_START2:
        if (c == MESHTASTIC_START2) {
            rx_frame[rx_pos++] = c;
            rx_state = RX_WAIT_LEN_MSB;
        } else if (c == MESHTASTIC_START1) {
            rx_frame[0] = c;
            rx_pos = 1;
        } else {
            rx_reset();
        }
        break;

    case RX_WAIT_LEN_MSB:
        rx_frame[rx_pos++] = c;
        rx_expected_len = ((size_t)c) << 8;
        rx_state = RX_WAIT_LEN_LSB;
        break;

    case RX_WAIT_LEN_LSB:
        rx_frame[rx_pos++] = c;
        rx_expected_len |= c;

        if (rx_expected_len == 0 || rx_expected_len > RX_FRAME_MAX) {
            printk("Invalid Meshtastic frame length: %u\n", (unsigned int)rx_expected_len);
            rx_reset();
            break;
        }

        rx_pos = 4;
        rx_state = RX_READ_PAYLOAD;
        break;

    case RX_READ_PAYLOAD:
        rx_frame[rx_pos++] = c;

        if ((rx_pos - 4) >= rx_expected_len) {
            printk("Meshtastic frame received: payload=%u bytes total=%u bytes\n",
                   (unsigned int)rx_expected_len,
                   (unsigned int)(rx_expected_len + 4));
            rx_reset();
        }
        break;
    }
}

// UART interrupt callback function to handle incoming data, read raw bytes and assemble one Meshtastic frame.
static void uart_cb(const struct device *dev, void *user_data)
{
    ARG_UNUSED(user_data);

    uart_irq_update(dev);

    while (uart_irq_rx_ready(dev)) {
        uint8_t c;
        int recv = uart_fifo_read(dev, &c, 1);

        if (recv <= 0) {
            break;
        }

        if (rx_state == RX_READ_PAYLOAD && rx_pos >= sizeof(rx_frame)) {
            printk("RX overflow, dropping Meshtastic frame\n");
            rx_reset();
            continue;
        }

        rx_consume_byte(c);
    }
}

int main(void)
{
    printk("Starting MeshFlipper...\n");

    if (!device_is_ready(uart_dev)) {
        printk("Error: UART device is not ready\n");
        return 0;
    }

    if (uart_irq_callback_user_data_set(uart_dev, uart_cb, NULL) < 0) {
        printk("Error: cannot set UART callback\n");
        return 0;
    }

    uart_irq_rx_enable(uart_dev);
    printk("UART listener ready on uart0 @ 115200. Waiting for Meshtastic frames.\n");


    while (1) {
        k_sleep(K_SECONDS(2));
    }



    return 0;
}