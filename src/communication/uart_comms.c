#include <stdint.h>
#include <stddef.h>
#include <errno.h>
#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/drivers/uart.h>
#include <pb_encode.h>
#include <pb_decode.h>
#include "meshtastic/mesh.pb.h"
#include "communication/uart_comms.h"
#include "communication/manage_pb.h"

#include "models/message.h"

#define RX_FRAME_MAX 512
#define MESHTASTIC_START1 0x94 // First start byte for Meshtastic frames. Used to identify the beginning of a new frame in the UART data stream.
#define MESHTASTIC_START2 0xC3 // Second start byte for Meshtastic frames.

// RX state machine for assembling incoming UART bytes into meshtatsic frames.
enum rx_state {
    RX_WAIT_START1,
    RX_WAIT_START2,
    RX_WAIT_LEN_MSB,
    RX_WAIT_LEN_LSB,
    RX_READ_PAYLOAD,
};

extern const struct device *uart_dev;
static uint8_t rx_frame[RX_FRAME_MAX + 4];
static size_t rx_pos;
static size_t rx_expected_len;
static enum rx_state rx_state = RX_WAIT_START1;

// Constructs and sends a Meshtastic frame with given payload over UART. The frame consists of a 4-byte header followed by the payload.
int send_meshtastic_frame(const uint8_t *payload, size_t payload_len)
{
    uint8_t header[4];

    if (payload == NULL || payload_len == 0 || payload_len > RX_FRAME_MAX) {
        return -EINVAL;
    }

    // Construct header
    header[0] = MESHTASTIC_START1;
    header[1] = MESHTASTIC_START2;
    header[2] = (uint8_t)((payload_len >> 8) & 0xFF);
    header[3] = (uint8_t)(payload_len & 0xFF);
   
    // Send header over UART
    for (size_t i = 0; i < sizeof(header); i++) {
        uart_poll_out(uart_dev, header[i]);
    }

    // Send payload over UART
    for (size_t i = 0; i < payload_len; i++) {
        uart_poll_out(uart_dev, payload[i]);
    }

    return 0;
}

// Resets the RX state machine to wait for the next frame. Called on errors or after successfully processing a frame.
static void rx_reset(void)
{
    rx_state = RX_WAIT_START1;
    rx_pos = 0;
    rx_expected_len = 0;
    return;
}

// State Machine: Consumes one byte of UART data and updates the RX state machine accordingly. Assembles bytes into a complete frame before decoding.
static meshtastic_FromRadio *rx_consume_byte(uint8_t c)
{
    switch (rx_state) {
        // Wait for start byte. If we see it store it as the first byte and move to the next state. Otherwise keep waiting.
        case RX_WAIT_START1:
            if (c == MESHTASTIC_START1) {
                rx_frame[0] = c;
                rx_pos = 1;
                rx_state = RX_WAIT_START2;
            }
            break;
        
        // Wait for second start byte. If we see it store it and move to the next state. If we see the first start byte again, treat it as a new frame and stay in this state. Otherwise reset.
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
        
        // Wait for length MSB (Most Significant Byte). Store it and move to the next state to wait for LSB.
        case RX_WAIT_LEN_MSB:
            rx_frame[rx_pos++] = c;
            // Shift the MSB to its position in the expected length and store it. The LSB will be ORed in the next state.
            rx_expected_len = ((size_t)c) << 8;
            rx_state = RX_WAIT_LEN_LSB;
            break;

        // Wait for length LSB (Least Significant Byte). Store it and move to the next state to read the payload.
        case RX_WAIT_LEN_LSB:
            rx_frame[rx_pos++] = c;
            // OR the LSB with the previously stored MSB to get the full expected length of the payload.
            rx_expected_len |= c;

            if (rx_expected_len == 0 || rx_expected_len > RX_FRAME_MAX) {
                printk("Invalid Meshtastic frame length: %u\n", (unsigned int)rx_expected_len);
                rx_reset();
                break;
            }

            rx_pos = 4;
            rx_state = RX_READ_PAYLOAD;
            break;

        // Read payload bytes. Once the expected length is reached, decode the frame and reset the state machine.
        case RX_READ_PAYLOAD:
            rx_frame[rx_pos++] = c;

            if ((rx_pos - 4) >= rx_expected_len) {
                meshtastic_FromRadio *msg = decode_from_radio(&rx_frame[4], rx_expected_len);
                rx_reset();
                if (msg != NULL) {
                    return msg;
                }
            }
            break;
    }
    return NULL;
}

// Sends a ToRadio message called want_config_id to get radio config and node db.
int send_want_config(void)
{
    // Setup message as ToRadio with the want_config_id_tag variant and set want_config_id to 1 to request the config.
    meshtastic_ToRadio msg = meshtastic_ToRadio_init_zero;
    msg.which_payload_variant = meshtastic_ToRadio_want_config_id_tag;
    msg.want_config_id = 1; // Best practice would be to make this a random non-zero value

    uint8_t buf[meshtastic_ToRadio_size];
    // Set up a nanopb output stream to encode the ToRadio message into the buffer.
    pb_ostream_t stream = pb_ostream_from_buffer(buf, sizeof(buf));

    if (!pb_encode(&stream, meshtastic_ToRadio_fields, &msg)) {
        printk("ToRadio encode failed: %s\n", PB_GET_ERROR(&stream));
        return -EIO;
    }

    return send_meshtastic_frame(buf, stream.bytes_written);
}

// UART interrupt callback function to handle incoming data, read raw bytes and assemble one Meshtastic frame.
void uart_cb(const struct device *dev, void *user_data)
{
    struct messageHistory *message_history = (struct messageHistory *)user_data;
    uart_irq_update(dev);

    while (uart_irq_rx_ready(dev)) {
        uint8_t c;

        // Pass in uart device, pointer to c to hold rx_data, and 1 as container size. Returns number of bytes read.
        int recv = uart_fifo_read(dev, &c, 1);

        if (recv <= 0) {
            break;
        }

        // Check for overflow before consuming the byte
        if (rx_state == RX_READ_PAYLOAD && rx_pos >= sizeof(rx_frame)) {
            printk("RX overflow, dropping Meshtastic frame\n");
            rx_reset();
            continue;
        }

        meshtastic_FromRadio *msg = rx_consume_byte(c);
        // If message is a text message, parse it into our internal message struct and print it out for debugging.
        if (msg != NULL && msg->which_payload_variant == meshtastic_FromRadio_packet_tag &&
            msg->packet.which_payload_variant == meshtastic_MeshPacket_decoded_tag &&
            msg->packet.decoded.portnum == meshtastic_PortNum_TEXT_MESSAGE_APP) {
            // printk("Received text message");
            struct message parsed_msg = parse_message(msg);
            // print_message(&parsed_msg);
            if (message_history->count < MAX_MESSAGE_HISTORY) {
                message_history->messages[message_history->count++] = parsed_msg;
            } else {
                // If we exceed the max message history, overwrite the oldest message in a circular buffer fashion.
                 message_history->messages[message_history->count % MAX_MESSAGE_HISTORY] = parsed_msg;
                 message_history->count++;
            }
        } else if (msg != NULL) {
            printk("Received non-packet FromRadio message with payload variant %u\n",
                   (unsigned int)msg->which_payload_variant);
        }
    }
    return;
}