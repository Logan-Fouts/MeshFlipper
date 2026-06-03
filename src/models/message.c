#include <zephyr/kernel.h>
#include "models/message.h"

struct message parse_message(const char *msg_packet)
{
    // TODO: Implement actual parsing logic to populate the message structure based on the incoming packet
    struct message msg = {
        .id = 0,
        .sender_id = 0,
        .destination_id = 0,
        .message = {0},
    };

    if (msg_packet != NULL) {
        strncpy(msg.message, msg_packet, sizeof(msg.message) - 1);
        msg.message[sizeof(msg.message) - 1] = '\0';
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
    printk("  sender:        %d\n", msg->sender_id);
    printk("  destination:   %d\n", msg->destination_id);
    printk("  payload:       %s\n", msg->message);
}
