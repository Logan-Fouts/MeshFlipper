#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <zephyr/kernel.h>
#include <pb_encode.h>
#include <pb_decode.h>
#include "meshtastic/mesh.pb.h"
#include "communication/manage_pb.h"

// Decodes a complete Meshtastic frame payload using nanopb and prints out the contents based on the message type.
meshtastic_FromRadio *decode_from_radio(const uint8_t *payload, size_t len)
{
    // Keep the decoded message in static storage because the caller uses the pointer after this function returns.
    static meshtastic_FromRadio msg;
    memset(&msg, 0, sizeof(msg));
    pb_istream_t stream = pb_istream_from_buffer(payload, len);

    // Decode the payload into the FromRadio message struct. If decoding fails, print an error and return.
    if (!pb_decode(&stream, meshtastic_FromRadio_fields, &msg)) {
        printk("FromRadio decode failed: %s\n", PB_GET_ERROR(&stream));
        return NULL;
    }

    // Handle the different payload variants of the FromRadio message.
    switch (msg.which_payload_variant) {
    // TODO: handle case
    case meshtastic_FromRadio_my_info_tag:
        // printk("[FromRadio] MyNodeInfo: node_num=%u\n",
            //    (unsigned int)msg.my_info.my_node_num);
        break;

    case meshtastic_FromRadio_node_info_tag:
        // printk("[FromRadio] NodeInfo: num=%u long_name=%s short_name=%s\n",
            //    (unsigned int)msg.node_info.num,
            //    msg.node_info.user.long_name,
            //    msg.node_info.user.short_name);
        break;

    case meshtastic_FromRadio_config_tag:
        // printk("[FromRadio] Config received (variant=%u)\n",
            //    (unsigned int)msg.config.which_payload_variant);
        break;

    case meshtastic_FromRadio_moduleConfig_tag:
        // printk("[FromRadio] ModuleConfig received (variant=%u)\n",
            //    (unsigned int)msg.moduleConfig.which_payload_variant);
        break;

    case meshtastic_FromRadio_channel_tag:
        // printk("[FromRadio] Channel: index=%d name=%s\n",
            //    msg.channel.index,
            //    msg.channel.settings.name);
        break;

    case meshtastic_FromRadio_config_complete_id_tag:
        // printk("[FromRadio] Config complete! id=%u\n",
            //    (unsigned int)msg.config_complete_id);
        break;

    case meshtastic_FromRadio_rebooted_tag:
        // printk("[FromRadio] Device rebooted\n");
        break;

    case meshtastic_FromRadio_packet_tag:
        // printk("[FromRadio] MeshPacket: from=0x%08x to=0x%08x id=%u\n",
            //    (unsigned int)msg.packet.from,
            //    (unsigned int)msg.packet.to,
            //    (unsigned int)msg.packet.id);

        if (msg.packet.which_payload_variant == meshtastic_MeshPacket_decoded_tag) {
            size_t payload_len = msg.packet.decoded.payload.size;

            // Silently skip non-text payloads in ISR path to avoid blocking.
        } else {
            // Packet arrived encrypted. Skip logging in ISR.
        }
        break;

    case meshtastic_FromRadio_log_record_tag:
        // printk("[FromRadio] Log [%d]: %s\n",
            //    msg.log_record.level,
            //    msg.log_record.message);
        break;

    case meshtastic_FromRadio_metadata_tag:
        // printk("[FromRadio] DeviceMetadata: fw=%s hw=%u\n",
            //    msg.metadata.firmware_version,
            //    (unsigned int)msg.metadata.hw_model);
        break;

    default:
        // printk("[FromRadio] id=%u variant=%u (%zu bytes)\n",
            //    (unsigned int)msg.id,
            //    (unsigned int)msg.which_payload_variant,
            //    len);
        break;
    }

    // Return &msg for all successfully decoded frames so the caller
    // can inspect the variant and log what arrives.
    return &msg;
} 