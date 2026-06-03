#ifndef MESSAGE_H
#define MESSAGE_H

#include <zephyr/kernel.h>

struct message {
    int id;
    int sender_id;
    int destination_id;
    char message[256]; // Default max meshtastic message length
};

// Function to parse the incoming message and populate the message structure
struct message parse_message(const char *msg_packet);

// Function to print the message details (for debugging purposes)
void print_message(struct message *msg);

#endif