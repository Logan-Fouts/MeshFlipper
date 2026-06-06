#ifndef MESSAGE_H
#define MESSAGE_H

#include <zephyr/kernel.h>
#include <zephyr/spinlock.h>
#include "meshtastic/mesh.pb.h"

#define MAX_MESSAGE_HISTORY 100

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

// bool is_duplicate_message(struct message *msg, struct message *message_list, size_t list_size);

struct messageHistory {
    struct message messages[MAX_MESSAGE_HISTORY];
    size_t count;
    struct k_spinlock lock;
};

void print_message_history(struct messageHistory *mes_history);

#endif