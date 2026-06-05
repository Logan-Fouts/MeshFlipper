#include <zephyr/kernel.h>
#include "models/message.h"


struct message parse_message(const meshtastic_FromRadio *msg_packet)
{
    // TODO: Implement actual parsing logic to populate the message structure based on the incoming packet
    struct message msg = {
        .id = 0,
        .from = 0,
        .to = 0,
        .text = {0},
    };

    if (msg_packet != NULL) {
        msg.id = msg_packet->packet.id;
        msg.from = msg_packet->packet.from;
        msg.to = msg_packet->packet.to;

        size_t copy_len = msg_packet->packet.decoded.payload.size;

        // This should never happen since the Meshtastic radio should enforce the max message length, but we check just in case to avoid buffer overflows.
        if (copy_len >= sizeof(msg.text)) {
            copy_len = sizeof(msg.text) - 1;
        }

        memcpy(msg.text, msg_packet->packet.decoded.payload.bytes, copy_len);
        msg.text[copy_len] = '\0';
    }

    return msg;
}

void print_message(struct message *msg)
{
    if (msg == NULL) {
        printk("\n[Message]\n  <null>\n");
        return;
    }

    printk("\n[Message]\n");
    printk("  id:            %d\n", msg->id);
    printk("  sender:        %d\n", msg->from);
    printk("  destination:   %d\n", msg->to);
    printk("  text:          %s\n", msg->text);
}
