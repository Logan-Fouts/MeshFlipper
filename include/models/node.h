#ifndef NODE_H
#define NODE_H

#include <stdbool.h>
#include <stdint.h>
#include <zephyr/kernel.h>
#include <zephyr/spinlock.h>
#include "meshtastic/mesh.pb.h"

#define MAX_NODE_HISTORY 200

struct node {
    bool valid;
    uint32_t num;
    char user_id[16];
    char long_name[40];
    char short_name[5];
    uint32_t last_heard;
    float snr;
    bool has_hops_away;
    uint8_t hops_away;
    bool via_mqtt;
};

struct my_node_info {
    bool valid;
    uint32_t node_num;
    uint32_t reboot_count;
    uint16_t nodedb_count;
    size_t device_id_len;
    uint8_t device_id[16];
    char pio_env[40];
};

// Function to parse the incoming node data and populate the node structure
struct node parse_node(const meshtastic_FromRadio *node_packet);

// Function to print the node details (for debugging purposes)
void print_node(struct node *n);

struct nodeHistory {
    struct node nodes[MAX_NODE_HISTORY];
    size_t count;
    struct my_node_info my_info;
    struct k_spinlock lock;
};

void update_node_history(struct nodeHistory *node_history, const meshtastic_FromRadio *msg);
void print_my_node_info(const struct my_node_info *info);
void print_node_history(struct nodeHistory *node_history);
void print_node_history_brief(struct nodeHistory *node_history);

#endif