#include <stdio.h>
#include <stdbool.h>
#include <stdint.h>
#include <errno.h>
#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/drivers/uart.h>
#include <pb_encode.h>
#include <pb_decode.h>
#include "meshtastic/mesh.pb.h"

#include "models/node.h"
#include "models/message.h"

static void decode_from_radio(const uint8_t *payload, size_t len);

#define RX_FRAME_MAX 512 // Maximum payload size for a Meshtastic frame. The actual frame size will be this plus 4 bytes for the header.
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

const struct device *uart_dev = DEVICE_DT_GET(DT_NODELABEL(uart0));
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
}

// State Machine: Consumes one byte of UART data and updates the RX state machine accordingly. Assembles bytes into a complete frame before decoding.
static void rx_consume_byte(uint8_t c)
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
                decode_from_radio(&rx_frame[4], rx_expected_len);
                rx_reset();
            }
            break;
    }
}

// Decodes a complete Meshtastic frame payload using nanopb and prints out the contents based on the message type.
static void decode_from_radio(const uint8_t *payload, size_t len)
{
    // Initialize the FromRadio message struct to zero and set up a nanopb input stream from the received payload buffer.
    meshtastic_FromRadio msg = meshtastic_FromRadio_init_zero;
    pb_istream_t stream = pb_istream_from_buffer(payload, len);

    // Decode the payload into the FromRadio message struct. If decoding fails, print an error and return.
    if (!pb_decode(&stream, meshtastic_FromRadio_fields, &msg)) {
        printk("FromRadio decode failed: %s\n", PB_GET_ERROR(&stream));
        return;
    }

    // Handle the different payload variants of the FromRadio message.
    switch (msg.which_payload_variant) {
    case meshtastic_FromRadio_my_info_tag:
        printk("[FromRadio] MyNodeInfo: node_num=%u\n",
               (unsigned int)msg.my_info.my_node_num);
        break;

    case meshtastic_FromRadio_node_info_tag:
        printk("[FromRadio] NodeInfo: num=%u long_name=%s short_name=%s\n",
               (unsigned int)msg.node_info.num,
               msg.node_info.user.long_name,
               msg.node_info.user.short_name);
        break;

    case meshtastic_FromRadio_config_tag:
        printk("[FromRadio] Config received (variant=%u)\n",
               (unsigned int)msg.config.which_payload_variant);
        break;

    case meshtastic_FromRadio_moduleConfig_tag:
        printk("[FromRadio] ModuleConfig received (variant=%u)\n",
               (unsigned int)msg.moduleConfig.which_payload_variant);
        break;

    case meshtastic_FromRadio_channel_tag:
        printk("[FromRadio] Channel: index=%d name=%s\n",
               msg.channel.index,
               msg.channel.settings.name);
        break;

    case meshtastic_FromRadio_config_complete_id_tag:
        printk("[FromRadio] Config complete! id=%u\n",
               (unsigned int)msg.config_complete_id);
        break;

    case meshtastic_FromRadio_rebooted_tag:
        printk("[FromRadio] Device rebooted\n");
        break;

    case meshtastic_FromRadio_packet_tag:
        printk("[FromRadio] MeshPacket: from=0x%08x to=0x%08x id=%u\n",
               (unsigned int)msg.packet.from,
               (unsigned int)msg.packet.to,
               (unsigned int)msg.packet.id);

        if (msg.packet.which_payload_variant == meshtastic_MeshPacket_decoded_tag) {
            size_t payload_len = msg.packet.decoded.payload.size;

            if (msg.packet.decoded.portnum == meshtastic_PortNum_TEXT_MESSAGE_APP) {
                char text[234];
                size_t copy_len = payload_len;

                if (copy_len >= sizeof(text)) {
                    copy_len = sizeof(text) - 1;
                }

                memcpy(text, msg.packet.decoded.payload.bytes, copy_len);
                text[copy_len] = '\0';
                printk("[FromRadio] Text: %s\n", text);
            } else {
                size_t preview_len = payload_len;
                if (preview_len > 16) {
                    preview_len = 16;
                }

                printk("[FromRadio] Data port=%d len=%u",
                       msg.packet.decoded.portnum,
                       (unsigned int)payload_len);

                for (size_t i = 0; i < preview_len; i++) {
                    printk(" %02x", msg.packet.decoded.payload.bytes[i]);
                }

                if (payload_len > preview_len) {
                    printk(" ...");
                }

                printk("\n");
            }
        }
        break;

    case meshtastic_FromRadio_log_record_tag:
        printk("[FromRadio] Log [%d]: %s\n",
               msg.log_record.level,
               msg.log_record.message);
        break;

    case meshtastic_FromRadio_metadata_tag:
        printk("[FromRadio] DeviceMetadata: fw=%s hw=%u\n",
               msg.metadata.firmware_version,
               (unsigned int)msg.metadata.hw_model);
        break;

    default:
        printk("[FromRadio] id=%u variant=%u (%zu bytes)\n",
               (unsigned int)msg.id,
               (unsigned int)msg.which_payload_variant,
               len);
        break;
    }
}

// Sends a ToRadio message called want_config_id to get radio config and node db.
static int send_want_config(void)
{
    // Setup message as ToRadio with the want_config_id_tag variant and set want_config_id to 1 to request the config.
    meshtastic_ToRadio msg = meshtastic_ToRadio_init_zero;
    msg.which_payload_variant = meshtastic_ToRadio_want_config_id_tag;
    msg.want_config_id = 1;

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
static void uart_cb(const struct device *dev, void *user_data)
{
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

    // Enable RX interrupts to start receiving data. The callback will handle data as it comes in and assemble complete frames.
    uart_irq_rx_enable(uart_dev);
    printk("UART listener ready on uart0 @ 115200. Waiting for Meshtastic frames.\n");

    // Send a want_config_id message to the radio to trigger the radio to send its config and node db on startup.
    if (send_want_config() < 0) {
        printk("Failed to send want_config_id\n");
    } else {
        printk("Sent ToRadio want_config_id\n");
    }


    while (1) {
        k_sleep(K_SECONDS(2));
    }



    return 0;
}