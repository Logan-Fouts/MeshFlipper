#include <zephyr/kernel.h>
#include "comms/protobuf_handler.h"
#include "comms/uart_comms.h"
#include <pb_encode.h>
#include <pb_decode.h>
#include <string.h>
#include <errno.h>

// Decodes a complete Meshtastic frame payload using nanopb.
meshtastic_FromRadio *decode_from_radio(const uint8_t *payload, size_t len)
{
    // Keep the decoded message in static storage because the caller uses the pointer after this function returns.
    static meshtastic_FromRadio msg;
    memset(&msg, 0, sizeof(msg));
    pb_istream_t stream = pb_istream_from_buffer(payload, len);

    // Decode the payload into the FromRadio message struct. If decoding fails, print an error and return.
    if (!pb_decode(&stream, meshtastic_FromRadio_fields, &msg)) {
        // Only print error if it's not a simple end-of-stream (which can happen with partial frames)
        const char *error = PB_GET_ERROR(&stream);
        if (strcmp(error, "end-of-stream") != 0 && strcmp(error, "wrong wire type") != 0) {
            printk("FromRadio decode failed: %s\n", error);
        }
        return NULL;
    }

    return &msg;
}

int send_meshtastic_frame(const uint8_t *payload, size_t payload_len)
{
    // Use the global UART comms instance
    return uart_comms_send_frame(&g_uart_comms, payload, payload_len);
}

// Constructs a ToRadio message to retrieve the config, encodes it using nanopb, and sends it over UART.
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

// Constructs a ToRadio message to send a text message, encodes it using nanopb, and sends it over UART.
int send_text_message(uint32_t target_node, const char *text, size_t text_len)
{
    // Build and send a text message
    meshtastic_ToRadio msg = meshtastic_ToRadio_init_zero;
    msg.which_payload_variant = meshtastic_ToRadio_packet_tag;
    msg.packet.which_payload_variant = meshtastic_MeshPacket_decoded_tag;
    msg.packet.id = 0;  // Let radio assign
    msg.packet.from = 0;
    msg.packet.to = target_node;
    msg.packet.want_ack = true;
    msg.packet.priority = meshtastic_MeshPacket_Priority_RELIABLE;
    msg.packet.decoded.portnum = meshtastic_PortNum_TEXT_MESSAGE_APP;
    msg.packet.decoded.want_response = false;
    
    if (text_len > sizeof(msg.packet.decoded.payload.bytes)) {
        printk("Message too long!\n");
        return -EMSGSIZE;
    }
    
    memcpy(msg.packet.decoded.payload.bytes, text, text_len);
    msg.packet.decoded.payload.size = text_len;
    
    uint8_t buf[meshtastic_ToRadio_size];
    pb_ostream_t stream = pb_ostream_from_buffer(buf, sizeof(buf));
    
    if (!pb_encode(&stream, meshtastic_ToRadio_fields, &msg)) {
        printk("ToRadio encode failed: %s\n", PB_GET_ERROR(&stream));
        return -EIO;
    }
    
    return send_meshtastic_frame(buf, stream.bytes_written);
}