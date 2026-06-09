#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <zephyr/spinlock.h>

#include "display/weact_epd154.h"
#include "ui/screen_ui.h"
#include "communication/uart_comms.h"

static const char *const quick_replies[] = {
    "Copy that.",
    "On my way.",
    "Thanks!",
    "Need more info.",
    "Talk soon.",
    "Hi",
    "Bye",
};

//  UI state management
struct screen_ui_state {
    bool thread_active;
    bool thread_broadcast;
    bool node_picker_active;
    bool compose_active;
    bool compose_broadcast;
    uint8_t quick_reply_index;
    size_t thread_message_index;
    size_t node_picker_index;
    int32_t last_handled_incoming_id;
    int32_t thread_node_num;
    int32_t selected_target_node;
    struct screen_ui_outgoing pending;
};

static struct screen_ui_state g_ui_state;
static struct message g_thread_snapshot[MAX_MESSAGE_HISTORY];
static size_t g_thread_snapshot_count;
static struct weact_epd154_node_entry g_node_snapshot[MAX_NODE_HISTORY];
static char g_node_snapshot_labels[MAX_NODE_HISTORY][40];
static uint32_t g_node_snapshot_last_heard[MAX_NODE_HISTORY];
static size_t g_node_snapshot_count;

// Get number of quick replies
static size_t quick_reply_count(void)
{
    return sizeof(quick_replies) / sizeof(quick_replies[0]);
}

// Check if a message is a DM between my node and a peer node
static bool is_dm_message(const struct message *msg, uint32_t my_node_num, uint32_t peer_node_num)
{
    uint32_t from = (uint32_t)msg->from;
    uint32_t to = (uint32_t)msg->to;

    // Exclude self-messages (from == to)
    if (from == to) {
        return false;
    }

    return (from == my_node_num && to == peer_node_num) ||
           (from == peer_node_num && to == my_node_num);
}

static bool is_broadcast_thread_message(const struct message *msg)
{
    if (msg == NULL || msg->id == 0) {
        return false;
    }

    return (uint32_t)msg->to == 0xFFFFFFFFu;
}

// Rebuild the thread snapshot based on the current message history and the selected peer node for the thread view.
static void rebuild_thread_snapshot(struct messageHistory *message_history, uint32_t my_node_num, uint32_t peer_node_num)
{
    g_thread_snapshot_count = 0;

    if (message_history == NULL || my_node_num == 0 || peer_node_num == 0) {
        return;
    }

    k_spinlock_key_t key = k_spin_lock(&message_history->lock);
    size_t count = message_history->count;
    size_t start = 0;

    if (count > MAX_MESSAGE_HISTORY) {
        start = count - MAX_MESSAGE_HISTORY;
    }

    for (size_t seq = start; seq < count; seq++) {
        struct message candidate = message_history->messages[seq % MAX_MESSAGE_HISTORY];

        if (candidate.id == 0) {
            continue;
        }

        if (!is_dm_message(&candidate, my_node_num, peer_node_num)) {
            continue;
        }

        if (g_thread_snapshot_count < MAX_MESSAGE_HISTORY) {
            g_thread_snapshot[g_thread_snapshot_count++] = candidate;
        }
    }

    k_spin_unlock(&message_history->lock, key);

    if (g_thread_snapshot_count == 0) {
        g_ui_state.thread_message_index = 0;
        return;
    }

    if (g_ui_state.thread_message_index >= g_thread_snapshot_count) {
        g_ui_state.thread_message_index = g_thread_snapshot_count - 1;
    }
}

static void rebuild_broadcast_thread_snapshot(struct messageHistory *message_history)
{
    g_thread_snapshot_count = 0;

    if (message_history == NULL) {
        return;
    }

    k_spinlock_key_t key = k_spin_lock(&message_history->lock);
    size_t count = message_history->count;
    size_t start = 0;

    if (count > MAX_MESSAGE_HISTORY) {
        start = count - MAX_MESSAGE_HISTORY;
    }

    for (size_t seq = start; seq < count; seq++) {
        struct message candidate = message_history->messages[seq % MAX_MESSAGE_HISTORY];

        if (!is_broadcast_thread_message(&candidate)) {
            continue;
        }

        if (g_thread_snapshot_count < MAX_MESSAGE_HISTORY) {
            g_thread_snapshot[g_thread_snapshot_count++] = candidate;
        }
    }

    k_spin_unlock(&message_history->lock, key);

    if (g_thread_snapshot_count == 0) {
        g_ui_state.thread_message_index = 0;
        return;
    }

    if (g_ui_state.thread_message_index >= g_thread_snapshot_count) {
        g_ui_state.thread_message_index = g_thread_snapshot_count - 1;
    }
}

// Copy the latest received message that is not from my node and populate the output parameter.
static bool copy_latest_received_message(struct messageHistory *message_history, uint32_t my_node_num, struct message *out)
{
    if (message_history == NULL || out == NULL) {
        return false;
    }

    k_spinlock_key_t key = k_spin_lock(&message_history->lock);
    size_t count = message_history->count;
    size_t visible = count < MAX_MESSAGE_HISTORY ? count : MAX_MESSAGE_HISTORY;

    // Find the latest message that is not from my node and set it to the output parameter.
    if (message_history->newest_message != NULL && message_history->newest_message->from != (int)my_node_num) {
        *out = *(message_history->newest_message);
        k_spin_unlock(&message_history->lock, key);
        return true;
    }
    for (size_t i = 0; i < visible; i++) {
        size_t index = (count - 1 - i) % MAX_MESSAGE_HISTORY;
        struct message candidate = message_history->messages[index];

        if (candidate.id == 0) {
            continue;
        }

        if (candidate.from == (int)my_node_num) {
            continue;
        }

        *out = candidate;
        k_spin_unlock(&message_history->lock, key);
        return true;
    }

    k_spin_unlock(&message_history->lock, key);
    return false;
}

// Resolve a sender's name based on the node history. If the sender is not found, return "Unknown". If the sender has a long name, use that. Otherwise, if they have a short name, use that. If neither is available, return the sender's number as a hex string.
static const char *resolve_sender_name(const struct nodeHistory *node_history, int32_t sender_num, char *out, size_t out_size)
{
    if (node_history == NULL || out == NULL || out_size == 0) {
        return "Unknown";
    }

    out[0] = '\0';

    // Lock node history lock and find the sender's name based on the sender_num. If not found, return "Unknown".
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

    snprintf(out, out_size, "0x%08X", (uint32_t)sender_num);

    return out;
}

// Show latest message from each node who has messaged user node, sorted by most recent message from that node. This is the "inbox" screen. 
static int render_thread_screen(struct messageHistory *message_history, struct nodeHistory *node_history)
{
    if (!g_ui_state.thread_active || node_history == NULL || !node_history->my_info.valid) {
        return 0;
    }

    uint32_t my_node_num = node_history->my_info.num;
    char target_label[40] = "BROADCAST";
    if (g_ui_state.thread_broadcast) {
        rebuild_broadcast_thread_snapshot(message_history);
    } else {
        uint32_t peer_node_num = (uint32_t)g_ui_state.thread_node_num;
        rebuild_thread_snapshot(message_history, my_node_num, peer_node_num);
        resolve_sender_name(node_history,
                            g_ui_state.thread_node_num,
                            target_label,
                            sizeof(target_label));
    }

    if (g_thread_snapshot_count == 0) {
        return weact_epd154_show_thread_screen(target_label,
                                               NULL,
                                               0,
                                               0,
                                               0,
                                               0);
    }

    size_t start = 0;
    if (g_ui_state.thread_message_index >= WEACT_EPD154_THREAD_VISIBLE - 1) {
        start = g_ui_state.thread_message_index - (WEACT_EPD154_THREAD_VISIBLE - 1);
    }

    size_t max_start = g_thread_snapshot_count > WEACT_EPD154_THREAD_VISIBLE
                           ? g_thread_snapshot_count - WEACT_EPD154_THREAD_VISIBLE
                           : 0;
    if (start > max_start) {
        start = max_start;
    }

    struct weact_epd154_thread_entry window[WEACT_EPD154_THREAD_VISIBLE];
    size_t window_count = g_thread_snapshot_count - start;
    if (window_count > WEACT_EPD154_THREAD_VISIBLE) {
        window_count = WEACT_EPD154_THREAD_VISIBLE;
    }

    for (size_t i = 0; i < window_count; i++) {
        const struct message *msg = &g_thread_snapshot[start + i];
        window[i].text = msg->text;
        window[i].is_outgoing = ((uint32_t)msg->from == my_node_num);
    }

    size_t selected_visible = g_ui_state.thread_message_index - start;

    return weact_epd154_show_thread_screen(target_label,
                                           window,
                                           window_count,
                                           selected_visible,
                                           g_ui_state.thread_message_index + 1,
                                           g_thread_snapshot_count);
}

// Shows compose screen with quick reply options if applicable, otherwise shows empty compose screen. In compose screen, primary button sends the message and secondary button cycles through quick replies. If in broadcast mode, there is no selected target and the message will be sent to all nodes.
static void render_compose(const struct nodeHistory *node_history)
{
    char target_label[48];

    if (g_ui_state.compose_broadcast || g_ui_state.selected_target_node == 0) {
        strncpy(target_label, "BROADCAST", sizeof(target_label) - 1);
        target_label[sizeof(target_label) - 1] = '\0';
    } else {
        resolve_sender_name(node_history,
                            g_ui_state.selected_target_node,
                            target_label,
                            sizeof(target_label));
    }

    weact_epd154_show_compose_screen(target_label,
                                     quick_replies[g_ui_state.quick_reply_index],
                                     g_ui_state.compose_broadcast);
}

static void rebuild_node_picker_snapshot(const struct nodeHistory *node_history)
{
    g_node_snapshot_count = 0;

    if (node_history == NULL) {
        return;
    }

    uint32_t my_node = node_history->my_info.valid ? node_history->my_info.num : 0;
    k_spinlock_key_t key = k_spin_lock((struct k_spinlock *)&node_history->lock);
    size_t count = node_history->count < MAX_NODE_HISTORY ? node_history->count : MAX_NODE_HISTORY;

    for (size_t i = 0; i < count && g_node_snapshot_count < MAX_NODE_HISTORY; i++) {
        const struct node_info *n = &node_history->nodes[i];
        if (!n->valid || n->num == 0 || n->num == my_node) {
            continue;
        }

        bool duplicate = false;
        for (size_t s = 0; s < g_node_snapshot_count; s++) {
            if (g_node_snapshot[s].node_num == n->num) {
                duplicate = true;
                break;
            }
        }
        if (duplicate) {
            continue;
        }

        char *label = g_node_snapshot_labels[g_node_snapshot_count];
        if (n->long_name[0] != '\0') {
            strncpy(label, n->long_name, 39);
            label[39] = '\0';
        } else if (n->short_name[0] != '\0') {
            strncpy(label, n->short_name, 39);
            label[39] = '\0';
        } else {
            snprintf(label, 40, "0x%08X", (unsigned int)n->num);
        }

        g_node_snapshot[g_node_snapshot_count].node_num = n->num;
        g_node_snapshot[g_node_snapshot_count].label = label;
        g_node_snapshot_last_heard[g_node_snapshot_count] = n->last_heard;
        g_node_snapshot_count++;
    }

    k_spin_unlock((struct k_spinlock *)&node_history->lock, key);

    for (size_t i = 0; i + 1 < g_node_snapshot_count; i++) {
        for (size_t j = i + 1; j < g_node_snapshot_count; j++) {
            if (g_node_snapshot_last_heard[j] > g_node_snapshot_last_heard[i]) {
                uint32_t tmp_heard = g_node_snapshot_last_heard[i];
                g_node_snapshot_last_heard[i] = g_node_snapshot_last_heard[j];
                g_node_snapshot_last_heard[j] = tmp_heard;

                struct weact_epd154_node_entry tmp_node = g_node_snapshot[i];
                g_node_snapshot[i] = g_node_snapshot[j];
                g_node_snapshot[j] = tmp_node;
            }
        }
    }

    if (g_node_snapshot_count == 0) {
        g_ui_state.node_picker_index = 0;
    } else if (g_ui_state.node_picker_index >= g_node_snapshot_count) {
        g_ui_state.node_picker_index = g_node_snapshot_count - 1;
    }
}

static int render_node_picker(const struct nodeHistory *node_history)
{
    rebuild_node_picker_snapshot(node_history);
    return weact_epd154_show_node_picker_screen(g_node_snapshot,
                                                g_node_snapshot_count,
                                                g_ui_state.node_picker_index);
}

static bool resolve_thread_target(struct messageHistory *message_history,
                                  const struct nodeHistory *node_history, int32_t *out_node_num)
{
    int32_t selected_id = 0;
    int32_t selected_from = 0;
    int32_t selected_to = 0;

    if (out_node_num == NULL || node_history == NULL || !node_history->my_info.valid) {
        return false;
    }

    bool have_selection = weact_epd154_get_selected_message(message_history, node_history, &selected_id, &selected_from, &selected_to);
    ARG_UNUSED(selected_id);

    if (!have_selection) {
        return false;
    }

    uint32_t my_node_num = node_history->my_info.num;
    uint32_t from = (uint32_t)selected_from;
    uint32_t to = (uint32_t)selected_to;

    if (from == my_node_num) {
        if (to == 0 || to == 0xFFFFFFFFu || to == my_node_num) {
            return false;
        }
        *out_node_num = (int32_t)to;
        return true;
    }

    if (from == 0 || from == 0xFFFFFFFFu || from == my_node_num) {
        return false;
    }

    *out_node_num = (int32_t)from;
    return true;
}

int screen_ui_refresh(struct messageHistory *message_history, struct nodeHistory *node_history)
{
    struct message latest;
    const char *screen_text = "Waiting for messages...";
    const char *sender_name = "Unknown";
    char sender_name_buf[40];
    bool has_latest = false;

    if (node_history != NULL && node_history->my_info.valid &&
        copy_latest_received_message(message_history, node_history->my_info.num, &latest)) {
        has_latest = true;
        screen_text = latest.text;
        sender_name = resolve_sender_name(node_history, latest.from, sender_name_buf, sizeof(sender_name_buf));

        bool show_popup = (latest.id != g_ui_state.last_handled_incoming_id);
        if (g_ui_state.thread_active &&
            ((g_ui_state.thread_broadcast && (uint32_t)latest.to == 0xFFFFFFFFu) ||
             (!g_ui_state.thread_broadcast && (uint32_t)g_ui_state.thread_node_num == (uint32_t)latest.from))) {
            show_popup = false;
        }

        int ret;
        if (g_ui_state.thread_active) {
            // Just a no-op now - display will read from main's message_history when rendering
            ret = weact_epd154_record_received_message();
            if ((g_ui_state.thread_broadcast && (uint32_t)latest.to == 0xFFFFFFFFu) ||
                (!g_ui_state.thread_broadcast && (uint32_t)g_ui_state.thread_node_num == (uint32_t)latest.from)) {
                // Clamp to newest so thread window shows the latest 3 entries.
                g_ui_state.thread_message_index = (size_t)MAX_MESSAGE_HISTORY;
            }
        } else if (g_ui_state.compose_active || g_ui_state.node_picker_active) {
            ret = weact_epd154_record_received_message();
        } else {
            ret = weact_epd154_show_received_message(message_history, node_history, show_popup);
        }
        if (ret < 0) {
            return ret;
        }

        g_ui_state.last_handled_incoming_id = latest.id;
    } else if (!g_ui_state.thread_active && !g_ui_state.compose_active && !g_ui_state.node_picker_active) {
        int ret = weact_epd154_show_message_screen(message_history, node_history);
        if (ret < 0) {
            return ret;
        }
    }

    if (!has_latest && g_ui_state.thread_active) {
        // Keep thread rendering active without bouncing through inbox.
    }

    if (g_ui_state.thread_active) {
        int thread_ret = render_thread_screen(message_history, node_history);
        if (thread_ret < 0) {
            return thread_ret;
        }
    }

    if (g_ui_state.node_picker_active) {
        int picker_ret = render_node_picker(node_history);
        if (picker_ret < 0) {
            return picker_ret;
        }
    }

    if (g_ui_state.compose_active) {
        render_compose(node_history);
    }

    return 0;
}

// Handle button action based on current UI state and action triggered
int screen_ui_handle_action(struct messageHistory *message_history,
                            struct nodeHistory *node_history,
                            enum screen_ui_action action)
{
    // Handle home action globally to allow quick exit from any screen back to inbox.
    if (action == SCREEN_UI_ACTION_HOME) {
        g_ui_state.compose_active = false;
        g_ui_state.thread_active = false;
        g_ui_state.thread_broadcast = false;
        g_ui_state.node_picker_active = false;
        return screen_ui_refresh(message_history, node_history);
    }

    // If not in a dm or compose screen
    if (!g_ui_state.compose_active && !g_ui_state.thread_active && !g_ui_state.node_picker_active) {
        if (action == SCREEN_UI_ACTION_PREVIOUS) {
            return weact_epd154_previous_message(message_history, node_history);
        }

        if (action == SCREEN_UI_ACTION_NEXT) {
            return weact_epd154_next_message(message_history, node_history);
        }

        // Enter thread view for the selected message's peer node.
        if (action == SCREEN_UI_ACTION_PRIMARY) {
            if (weact_epd154_is_broadcast_compose_selected(message_history, node_history)) {
                g_ui_state.thread_active = true;
                g_ui_state.thread_broadcast = true;
                g_ui_state.thread_message_index = 0;
                rebuild_broadcast_thread_snapshot(message_history);
                if (g_thread_snapshot_count > 0) {
                    g_ui_state.thread_message_index = g_thread_snapshot_count - 1;
                }
                return render_thread_screen(message_history, node_history);
            }

            int32_t thread_node = 0;
            // If broadcast or unable to resolve a peer node, do not enter thread view since there is no meaningful thread to show.
            if (!resolve_thread_target(message_history, node_history, &thread_node)) {
                return 0;
            }

            g_ui_state.thread_active = true;
            g_ui_state.thread_broadcast = false;
            g_ui_state.thread_node_num = thread_node;
            g_ui_state.thread_message_index = 0;
            if (node_history != NULL && node_history->my_info.valid) {
                rebuild_thread_snapshot(message_history,
                                        node_history->my_info.num,
                                        (uint32_t)thread_node);
                // Clamp to newest so thread window shows the latest 3 entries.
                if (g_thread_snapshot_count > 0) {
                    g_ui_state.thread_message_index = g_thread_snapshot_count - 1;
                }
            }

            return render_thread_screen(message_history, node_history);
        }

        // Enter compose screen in broadcast mode with no selected target.
        if (action == SCREEN_UI_ACTION_SECONDARY) {
            g_ui_state.node_picker_active = true;
            g_ui_state.node_picker_index = 0;
            return render_node_picker(node_history);
        }

        return 0;
    }

    if (g_ui_state.node_picker_active && !g_ui_state.compose_active && !g_ui_state.thread_active) {
        if (action == SCREEN_UI_ACTION_PREVIOUS || action == SCREEN_UI_ACTION_NEXT) {
            rebuild_node_picker_snapshot(node_history);

            if (g_node_snapshot_count == 0) {
                return render_node_picker(node_history);
            }

            if (action == SCREEN_UI_ACTION_PREVIOUS) {
                if (g_ui_state.node_picker_index == 0) {
                    g_ui_state.node_picker_index = g_node_snapshot_count - 1;
                } else {
                    g_ui_state.node_picker_index--;
                }
            } else {
                g_ui_state.node_picker_index = (g_ui_state.node_picker_index + 1) % g_node_snapshot_count;
            }

            return render_node_picker(node_history);
        }

        if (action == SCREEN_UI_ACTION_PRIMARY) {
            rebuild_node_picker_snapshot(node_history);
            if (g_node_snapshot_count == 0) {
                return render_node_picker(node_history);
            }

            g_ui_state.node_picker_active = false;
            g_ui_state.compose_active = true;
            g_ui_state.compose_broadcast = false;
            g_ui_state.quick_reply_index = 0;
            g_ui_state.selected_target_node = (int32_t)g_node_snapshot[g_ui_state.node_picker_index].node_num;
            render_compose(node_history);
            return 0;
        }

        if (action == SCREEN_UI_ACTION_SECONDARY) {
            g_ui_state.node_picker_active = false;
            return screen_ui_refresh(message_history, node_history);
        }

        return 0;
    }

    // If in thread view and not in compose, allow navigating messages in the thread, quick replying to the thread peer, or exiting back to inbox. Quick reply will enter compose mode with the target set to the thread peer and the quick reply text filled in. If already in broadcast compose mode, do not allow entering thread view since it would be confusing to show a thread when the message being composed is not part of that thread.
    if (g_ui_state.thread_active && !g_ui_state.compose_active) {
        if (action == SCREEN_UI_ACTION_PREVIOUS || action == SCREEN_UI_ACTION_NEXT) {
            if (node_history == NULL || !node_history->my_info.valid) {
                return 0;
            }

            if (g_ui_state.thread_broadcast) {
                rebuild_broadcast_thread_snapshot(message_history);
            } else {
                rebuild_thread_snapshot(message_history,
                                        node_history->my_info.num,
                                        (uint32_t)g_ui_state.thread_node_num);
            }
            if (g_thread_snapshot_count == 0) {
                return render_thread_screen(message_history, node_history);
            }

            if (action == SCREEN_UI_ACTION_PREVIOUS) {
                if (g_ui_state.thread_message_index == 0) {
                    g_ui_state.thread_message_index = g_thread_snapshot_count - 1;
                } else {
                    g_ui_state.thread_message_index--;
                }
            } else {
                g_ui_state.thread_message_index = (g_ui_state.thread_message_index + 1) % g_thread_snapshot_count;
            }

            return render_thread_screen(message_history, node_history);
        }
        

        // Enter compose mode with quick reply options for the thread peer.
        if (action == SCREEN_UI_ACTION_PRIMARY) {
            g_ui_state.compose_active = true;
            g_ui_state.quick_reply_index = 0;
            g_ui_state.selected_target_node = g_ui_state.thread_broadcast ? 0 : g_ui_state.thread_node_num;
            g_ui_state.compose_broadcast = g_ui_state.thread_broadcast;
            render_compose(node_history);
            return 0;
        }

        // Exit back to inbox and reset thread state.
        if (action == SCREEN_UI_ACTION_SECONDARY) {
            g_ui_state.thread_active = false;
            g_ui_state.thread_broadcast = false;
            return screen_ui_refresh(message_history, node_history);
        }

        return 0;
    }

    if (action == SCREEN_UI_ACTION_PREVIOUS) {
        if (g_ui_state.quick_reply_index == 0) {
            g_ui_state.quick_reply_index = (uint8_t)(quick_reply_count() - 1U);
        } else {
            g_ui_state.quick_reply_index--;
        }
        render_compose(node_history);
        return 0;
    }

    if (action == SCREEN_UI_ACTION_NEXT) {
        g_ui_state.quick_reply_index = (uint8_t)((g_ui_state.quick_reply_index + 1U) % quick_reply_count());
        render_compose(node_history);
        return 0;
    }

    if (action == SCREEN_UI_ACTION_SECONDARY) {
        g_ui_state.compose_broadcast = !g_ui_state.compose_broadcast;
        render_compose(node_history);
        return 0;
    }

    if (action == SCREEN_UI_ACTION_PRIMARY) {
        g_ui_state.pending.valid = true;
        g_ui_state.pending.target_node = g_ui_state.compose_broadcast ? 0 : g_ui_state.selected_target_node;
        strncpy(g_ui_state.pending.text, quick_replies[g_ui_state.quick_reply_index], sizeof(g_ui_state.pending.text) - 1);
        g_ui_state.pending.text[sizeof(g_ui_state.pending.text) - 1] = '\0';

        g_ui_state.compose_active = false;
        if (g_ui_state.compose_broadcast) {
            if (g_ui_state.thread_active && g_ui_state.thread_broadcast) {
                g_ui_state.thread_message_index = (size_t)MAX_MESSAGE_HISTORY;
            } else {
                g_ui_state.thread_active = false;
                g_ui_state.thread_broadcast = false;
            }
        } else if (g_ui_state.thread_active) {
            // Force the DM screen to clamp to the newest entry after the send is recorded.
            g_ui_state.thread_message_index = (size_t)MAX_MESSAGE_HISTORY;
        }
        return 0;
    }

    return 0;
}

bool screen_ui_take_outgoing(struct screen_ui_outgoing *outgoing)
{
    if (outgoing == NULL || !g_ui_state.pending.valid) {
        return false;
    }

    *outgoing = g_ui_state.pending;
    g_ui_state.pending.valid = false;
    return true;
}

void drive_ui(struct button_state *button, bool falling_edge, bool rising_edge, bool is_secondary, struct messageHistory *message_history, struct nodeHistory *node_list)
{
    // For secondary button, we want to trigger on the rising edge but only if the long press action hasn't already been triggered. For other buttons, we trigger on the falling edge.
    bool trigger_action = false;
    if (is_secondary) trigger_action = rising_edge && !button->long_press_handled;
    else trigger_action = falling_edge;

    if (!trigger_action) {
        return;
    }

    enum screen_ui_action action;
    switch (button->pin)
    {
    case BUTTON_PREV_PIN:
        action = SCREEN_UI_ACTION_PREVIOUS;
        break;
    case BUTTON_NEXT_PIN:
        action = SCREEN_UI_ACTION_NEXT;
        break;
    case BUTTON_PRIMARY_PIN:
        action = SCREEN_UI_ACTION_PRIMARY;
        break;
    case BUTTON_SECONDARY_PIN:
        action = SCREEN_UI_ACTION_SECONDARY;
        break;
    default:
        return;
    }

    int ui_ret = screen_ui_handle_action(message_history, node_list, action);
    if (ui_ret < 0) {
        printk("UI action failed: %d\n", ui_ret);
    }

    // Check for pending message from the UI and if none then skip send logic.
    struct screen_ui_outgoing outgoing;
    if (!screen_ui_take_outgoing(&outgoing) || !outgoing.valid) {
        return;
    }

    if (!node_list->my_info.valid) {
        printk("Cannot send yet: my node info not ready\n");
        screen_ui_refresh(message_history, node_list);
        return;
    }

    int send_ret = send_message_to_node(outgoing.target_node, outgoing.text, node_list->my_info.num, message_history);

    if (send_ret < 0) {
        printk("Send failed: %d\n", send_ret);
    }

    screen_ui_refresh(message_history, node_list);
}


