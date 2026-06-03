#ifndef NODE_H
#define NODE_H

#include <stdbool.h>

struct node {
    int id;
    char name[36]; // Default max meshtastic node name length
    char model[50];
    char role[15]; // e.g., "router", "end device", "gateway"
    bool is_active;
    uint8_t battery_level;
    uint32_t last_heard_timestamp;
    uint32_t first_heard_timestamp;
    uint8_t signal_strength;
};

// Function to parse the incoming node data and populate the node structure
struct node parse_node(const char *node_packet);

// Function to print the node details (for debugging purposes)
void print_node(struct node *n);

#endif