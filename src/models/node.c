#include <zephyr/kernel.h>
#include "models/node.h"
#include <stdio.h>

struct node parse_node(const char *node_packet)
{
    // TODO: Implement actual parsing logic to populate the node structure based on the incoming packet
    return (struct node){
        .id = 0, // Placeholder, actual parsing logic needed
        .name = "NodeName", // Placeholder
        .model = "NodeModel", // Placeholder
        .role = "NodeRole", // Placeholder
        .is_active = true, // Placeholder
        .battery_level = 100, // Placeholder
        .last_heard_timestamp = 0, // Placeholder
        .first_heard_timestamp = 0, // Placeholder
        .signal_strength = 0 // Placeholder
    };
}

void print_node(struct node *n)
{
    if (n == NULL) {
        printk("\n[Node]\n  <null>\n");
        return;
    }

    printk("\n[Node]\n");
    printk("  id:            %d\n", n->id);
    printk("  name:          %s\n", n->name);
    printk("  model:         %s\n", n->model);
    printk("  role:          %s\n", n->role);
    printk("  active:        %s\n", n->is_active ? "yes" : "no");
    printk("  battery:       %d%%\n", n->battery_level);
    printk("  last heard:    %d\n", n->last_heard_timestamp);
    printk("  first heard:   %d\n", n->first_heard_timestamp);
    printk("  signal:        %d\n", n->signal_strength);
}
