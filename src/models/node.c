#include <zephyr/kernel.h>
#include "models/node.h"
#include <stdio.h>
#include <string.h>

// Helper function to safely copy strings from protobuf fields, ensuring null-termination and preventing buffer overflows.
static void copy_proto_string(char *dest, size_t dest_size, const char *src)
{
    if (dest == NULL || dest_size == 0) {
        return;
    }

    if (src == NULL) {
        dest[0] = '\0';
        return;
    }

    strncpy(dest, src, dest_size - 1);
    dest[dest_size - 1] = '\0';
}

static int find_node_index_by_num(const struct nodeHistory *node_history, uint32_t node_num)
{
    size_t limit = node_history->count;

    if (limit > MAX_NODE_HISTORY) {
        limit = MAX_NODE_HISTORY;
    }

    for (size_t i = 0; i < limit; i++) {
        if (node_history->nodes[i].valid && node_history->nodes[i].num == node_num) {
            return (int)i;
        }
    }

    return -1;
}

struct node_info parse_node(const meshtastic_FromRadio *node_packet)
{
    struct node_info n = {0};

    if (node_packet != NULL) {
        if (node_packet->which_payload_variant == meshtastic_FromRadio_node_info_tag) {
            n.valid = true;
            n.num = node_packet->node_info.num;
            n.last_heard = node_packet->node_info.last_heard;
            n.snr = node_packet->node_info.snr;
            n.has_hops_away = node_packet->node_info.has_hops_away;
            n.hops_away = node_packet->node_info.hops_away;
            n.via_mqtt = node_packet->node_info.via_mqtt;
            n.favorited = node_packet->node_info.is_favorite;

            if (node_packet->node_info.has_user) { // This has_user is a nanopb thing that indicates whether the optional user field is present in the protobuf message.
                copy_proto_string(n.user_id, sizeof(n.user_id), node_packet->node_info.user.id);
                copy_proto_string(n.long_name, sizeof(n.long_name), node_packet->node_info.user.long_name);
                copy_proto_string(n.short_name, sizeof(n.short_name), node_packet->node_info.user.short_name);
            }
        } else if (node_packet->which_payload_variant == meshtastic_FromRadio_my_info_tag) {
            n.valid = true;
            n.is_my_info = true;
            n.num = node_packet->my_info.my_node_num;
            n.reboot_count = node_packet->my_info.reboot_count;
            n.nodedb_count = node_packet->my_info.nodedb_count;
            n.device_id_len = node_packet->my_info.device_id.size;
        }
    }

    return n;
}

void update_node_history(struct nodeHistory *node_history, const meshtastic_FromRadio *msg)
{
    if (node_history == NULL || msg == NULL) {
        return;
    }

    k_spinlock_key_t key = k_spin_lock(&node_history->lock);

    if (msg->which_payload_variant == meshtastic_FromRadio_my_info_tag) {
        size_t device_id_len = msg->my_info.device_id.size;

        if (device_id_len > sizeof(node_history->my_info.device_id)) {
            device_id_len = sizeof(node_history->my_info.device_id);
        }

        node_history->my_info.valid = true;
        node_history->my_info.num = msg->my_info.my_node_num;
        node_history->my_info.reboot_count = msg->my_info.reboot_count;
        node_history->my_info.nodedb_count = msg->my_info.nodedb_count;
        node_history->my_info.device_id_len = device_id_len;
        memset(node_history->my_info.device_id, 0, sizeof(node_history->my_info.device_id));
        memcpy(node_history->my_info.device_id, msg->my_info.device_id.bytes, device_id_len);
        copy_proto_string(node_history->my_info.pio_env, sizeof(node_history->my_info.pio_env), msg->my_info.pio_env);
        k_spin_unlock(&node_history->lock, key);
        return;
    }

    if (msg->which_payload_variant == meshtastic_FromRadio_node_info_tag) {
        struct node_info parsed_node = parse_node(msg);
        int existing_index;

        if (!parsed_node.valid) {
            k_spin_unlock(&node_history->lock, key);
            return;
        }

        existing_index = find_node_index_by_num(node_history, parsed_node.num);
        if (existing_index >= 0) {
            node_history->nodes[existing_index] = parsed_node;
        } else if (node_history->count < MAX_NODE_HISTORY) {
            node_history->nodes[node_history->count++] = parsed_node;
        }
    }

    k_spin_unlock(&node_history->lock, key);
}

void print_node(struct node_info *n)
{
    if (n == NULL) {
        printk("\n[Node]\n  <null>\n");
        return;
    }

    printk("\n[Node]\n");
    printk("  num:           %u\n", (unsigned int)n->num);
    if (n->short_name[0] != '\0') {
        printk("  short_name:    %s\n", n->short_name);
    }
    if (n->long_name[0] != '\0') {
        printk("  long_name:     %s\n", n->long_name);
    }
    if (n->user_id[0] != '\0') {
        printk("  user_id:       %s\n", n->user_id);
    }
    printk("  last_heard:    %u\n", (unsigned int)n->last_heard);
    printk("  snr:           %d.%02d\n", (int)n->snr, (int)((n->snr < 0 ? -n->snr : n->snr) * 100.0f) % 100);
    if (n->has_hops_away) {
        printk("  hops_away:     %u\n", (unsigned int)n->hops_away);
    }
    printk("  via_mqtt:      %s\n", n->via_mqtt ? "true" : "false");
    printk("  favorited:     %s\n", n->favorited ? "true" : "false");
}

void print_my_node_info(const struct node_info *info)
{
    if (info == NULL || !info->valid) {
        printk("\n[MyNodeInfo]\n  <not received>\n");
        return;
    }

    printk("\n[MyNodeInfo]\n");
    printk("  node_num:      %u\n", (unsigned int)info->num);
    printk("  reboot_count:  %u\n", (unsigned int)info->reboot_count);
    printk("  nodedb_count:  %u\n", (unsigned int)info->nodedb_count);
    if (info->pio_env[0] != '\0') {
        printk("  pio_env:       %s\n", info->pio_env);
    }

    printk("  device_id:     ");
    for (size_t i = 0; i < info->device_id_len; i++) {
        printk("%02x", info->device_id[i]);
    }
    printk("\n");
}


void print_node_history(struct nodeHistory *node_history)
{
    size_t count;

    if (node_history == NULL) {
        return;
    }

    k_spinlock_key_t count_key = k_spin_lock(&node_history->lock);
    count = node_history->count;
    k_spin_unlock(&node_history->lock, count_key);

    if (count > MAX_NODE_HISTORY) {
        count = MAX_NODE_HISTORY;
    }

    if (count == 0) {
        return;
    }

    printk("\n=== Node History (total %zu nodes) ===\n", count);

    for (size_t i = 0; i < count; i++) {
        struct node_info node_copy;

        //Breifly aquire lock before accessing specific node from history
        k_spinlock_key_t key = k_spin_lock(&node_history->lock);
        node_copy = node_history->nodes[i];
        k_spin_unlock(&node_history->lock, key);

        print_node(&node_copy);
    }
}

void print_node_history_brief(struct nodeHistory *node_history)
{
    size_t count;

    if (node_history == NULL) {
        return;
    }

    k_spinlock_key_t count_key = k_spin_lock(&node_history->lock);
    count = node_history->count;
    k_spin_unlock(&node_history->lock, count_key);

    if (count > MAX_NODE_HISTORY) {
        count = MAX_NODE_HISTORY;
    }

    if (count == 0) {
        return;
    }

    for (size_t i = 0; i < count; i++) {
        struct node_info node_copy;

        //Breifly aquire lock before accessing specific node from history
        k_spinlock_key_t key = k_spin_lock(&node_history->lock);
        node_copy = node_history->nodes[i];
        k_spin_unlock(&node_history->lock, key);

        printk("[Node %zu]\n", i);
        if (node_copy.short_name[0] != '\0') {
            printk("  short_name, longname:    %s, %s\n", node_copy.short_name, node_copy.long_name);
        }
    }
}