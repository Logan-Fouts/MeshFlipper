#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <zephyr/spinlock.h>

#include "display/weact_epd154.h"
#include "ui/screen_ui.h"

static bool copy_latest_received_message(struct messageHistory *history, uint32_t my_node_num, struct message *out)
{
    if (history == NULL || out == NULL) {
        return false;
    }

    k_spinlock_key_t key = k_spin_lock(&history->lock);
    size_t count = history->count;
    size_t visible = count < MAX_MESSAGE_HISTORY ? count : MAX_MESSAGE_HISTORY;

    for (size_t i = 0; i < visible; i++) {
        size_t index = (count - 1 - i) % MAX_MESSAGE_HISTORY;
        struct message candidate = history->messages[index];

        if (candidate.id == 0) {
            continue;
        }

        if (candidate.from == (int)my_node_num) {
            continue;
        }

        *out = candidate;
        k_spin_unlock(&history->lock, key);
        return true;
    }

    k_spin_unlock(&history->lock, key);
    return false;
}

static const char *resolve_sender_name(const struct nodeHistory *node_history, int32_t sender_num, char *out, size_t out_size)
{
    if (node_history == NULL || out == NULL || out_size == 0) {
        return "Unknown";
    }

    out[0] = '\0';

    k_spinlock_key_t key = k_spin_lock((struct k_spinlock *)&node_history->lock);
    size_t count = node_history->count < MAX_NODE_HISTORY ? node_history->count : MAX_NODE_HISTORY;

    for (size_t i = 0; i < count; i++) {
        if (!node_history->nodes[i].valid || node_history->nodes[i].num != (uint32_t)sender_num) {
            continue;
        }

        if (node_history->nodes[i].long_name[0] != '\0') {
            strncpy(out, node_history->nodes[i].long_name, out_size - 1);
        } else if (node_history->nodes[i].short_name[0] != '\0') {
            strncpy(out, node_history->nodes[i].short_name, out_size - 1);
        } else {
            strncpy(out, "Unknown", out_size - 1);
        }
        out[out_size - 1] = '\0';
        k_spin_unlock((struct k_spinlock *)&node_history->lock, key);
        return out;
    }

    k_spin_unlock((struct k_spinlock *)&node_history->lock, key);
    snprintf(out, out_size, "0x%04X", (uint32_t)sender_num & 0xFFFFU);
    return out;
}

int screen_ui_refresh(struct messageHistory *message_history, struct nodeHistory *node_history)
{
    struct message latest;
    const char *screen_text = "Waiting for messages...";
    const char *sender_name = "Unknown";
    char sender_name_buf[40];

    if (node_history != NULL && node_history->my_info.valid &&
        copy_latest_received_message(message_history, node_history->my_info.num, &latest)) {
        screen_text = latest.text;
        sender_name = resolve_sender_name(node_history, latest.from, sender_name_buf, sizeof(sender_name_buf));
        return weact_epd154_show_received_message(latest.id, screen_text, sender_name);
    }

    return weact_epd154_show_message_screen(screen_text);
}
