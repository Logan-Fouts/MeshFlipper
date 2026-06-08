#ifndef MESSAGE_H
#define MESSAGE_H

#include <zephyr/kernel.h>
#include <zephyr/spinlock.h>
#include "meshtastic/mesh.pb.h"

#define MAX_MESSAGE_HISTORY 300

struct message {
    int id;
    int from;
    int to;
    char text[256]; // Default max meshtastic message length
};

// Function to parse the incoming message and populate the message structure
struct message parse_message(const meshtastic_FromRadio *msg_packet);

// Function to print the message details (for debugging purposes)
void print_message(struct message *msg);


// History of messages, stored in a ring buffer.
struct messageHistory {
    struct message messages[MAX_MESSAGE_HISTORY];
    size_t count;
    struct k_spinlock lock;
};

void update_message_history(struct messageHistory *mes_history, const meshtastic_FromRadio *msg_packet);

void print_message_history(struct messageHistory *mes_history);

struct message* find_message_by_id(struct messageHistory *mes_history, int id);

struct message** find_my_messages_sent_to_node(struct messageHistory *mes_history, int node_num, size_t *out_count);

#endif