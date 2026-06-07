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
#include "communication/ring_buffer.h"
#include "models/message.h"
#include "models/node.h"
#include "cb_args.h"

#define RX_FRAME_MAX 512
#define MESHTASTIC_START1 0x94
#define MESHTASTIC_START2 0xC3

// RX state machine
enum rx_state {
    RX_WAIT_START1,
    RX_WAIT_START2,
    RX_WAIT_LEN_MSB,
    RX_WAIT_LEN_LSB,
    RX_READ_PAYLOAD,
};

// External reference to ring buffer
extern ring_buffer_t msg_ring_buffer;

extern const struct device *uart_dev;
static uint8_t rx_frame[RX_FRAME_MAX + 4];
static size_t rx_pos;
static size_t rx_expected_len;
static enum rx_state rx_state = RX_WAIT_START1;

// RX statistics counters
volatile size_t rx_frames_received = 0;
volatile size_t rx_frames_dropped = 0;
volatile size_t rx_bytes_processed = 0;

int send_meshtastic_frame(const uint8_t *payload, size_t payload_len)
{
    uint8_t header[4];

    if (payload == NULL || payload_len == 0 || payload_len > RX_FRAME_MAX) {
        return -EINVAL;
    }

    header[0] = MESHTASTIC_START1;
    header[1] = MESHTASTIC_START2;
    header[2] = (uint8_t)((payload_len >> 8) & 0xFF);
    header[3] = (uint8_t)(payload_len & 0xFF);
   
    for (size_t i = 0; i < sizeof(header); i++) {
        uart_poll_out(uart_dev, header[i]);
    }

    for (size_t i = 0; i < payload_len; i++) {
        uart_poll_out(uart_dev, payload[i]);
    }

    return 0;
}

static void rx_reset(void)
{
    rx_state = RX_WAIT_START1;
    rx_pos = 0;
    rx_expected_len = 0;
}

static meshtastic_FromRadio *rx_consume_byte(uint8_t c)
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
                meshtastic_FromRadio *msg = decode_from_radio(&rx_frame[4], rx_expected_len); // This may be heavy to be in isr callback
                rx_reset();
                if (msg != NULL) {
                    return msg;
                }
            }
            break;
    }
    return NULL;
}

int send_want_config(void)
{
    meshtastic_ToRadio msg = meshtastic_ToRadio_init_zero;
    msg.which_payload_variant = meshtastic_ToRadio_want_config_id_tag;
    msg.want_config_id = 1;

    uint8_t buf[meshtastic_ToRadio_size];
    pb_ostream_t stream = pb_ostream_from_buffer(buf, sizeof(buf));

    if (!pb_encode(&stream, meshtastic_ToRadio_fields, &msg)) {
        printk("ToRadio encode failed: %s\n", PB_GET_ERROR(&stream));
        return -EIO;
    }

    return send_meshtastic_frame(buf, stream.bytes_written);
}

int send_message_to_node(int node_num, const char *text, uint32_t my_node_num)
{
    if (text == NULL)
        return -EINVAL;

    size_t text_len = strlen(text);
    if (text_len == 0)
        return -EINVAL;

    if (my_node_num == 0) {
        printk("Cannot send: my_node_num is not initialized yet\n");
        return -EAGAIN;
    }

    uint32_t dest;
    if (node_num == 0) {
        dest = 0xFFFFFFFFu;
    } else {
        dest = (uint32_t)node_num;
    }

    static uint32_t next_packet_id = 0;

    meshtastic_ToRadio msg = meshtastic_ToRadio_init_zero;
    
    msg.which_payload_variant = meshtastic_ToRadio_packet_tag; 
    msg.packet.id = next_packet_id++;
    msg.packet.from = 0;
    msg.packet.to = dest;
    msg.packet.want_ack = true;
    msg.packet.priority = meshtastic_MeshPacket_Priority_RELIABLE;
    msg.packet.which_payload_variant = meshtastic_MeshPacket_decoded_tag;
    msg.packet.decoded.portnum = meshtastic_PortNum_TEXT_MESSAGE_APP;
    msg.packet.decoded.want_response = false;

    if (text_len > sizeof(msg.packet.decoded.payload.bytes))
        text_len = sizeof(msg.packet.decoded.payload.bytes);

    memcpy(msg.packet.decoded.payload.bytes, text, text_len);
    msg.packet.decoded.payload.size = text_len;

    uint8_t buf[meshtastic_ToRadio_size];
    pb_ostream_t stream = pb_ostream_from_buffer(buf, sizeof(buf));

    if (!pb_encode(&stream, meshtastic_ToRadio_fields, &msg)) {
        printk("ToRadio encode failed: %s\n", PB_GET_ERROR(&stream));
        return -EIO;
    }

    printk("TX queued: id=%u to=%u len=%u\n",
           (unsigned int)msg.packet.id,
           (unsigned int)msg.packet.to,
           (unsigned int)stream.bytes_written);

    return send_meshtastic_frame(buf, stream.bytes_written);
}

void uart_cb(const struct device *dev, void *user_data)
{
    // User data not needed for ring buffer operations
    (void)user_data;
    
    uart_irq_update(dev);

    while (uart_irq_rx_ready(dev)) {
        uint8_t c;
        int recv = uart_fifo_read(dev, &c, 1);

        if (recv <= 0) {
            break;
        }

        if (rx_state == RX_READ_PAYLOAD && rx_pos >= sizeof(rx_frame)) {
            rx_frames_dropped++;
            rx_reset();
            continue;
        }

        rx_bytes_processed++;
        meshtastic_FromRadio *msg = rx_consume_byte(c);
        if (msg == NULL) {
            continue;
        }
        
        rx_frames_received++;
        
        // Add to ring buffer (this handles the copy and semaphore)
        if (!ring_buffer_put(&msg_ring_buffer, msg)) {
            rx_frames_dropped++;
        }
    }
}