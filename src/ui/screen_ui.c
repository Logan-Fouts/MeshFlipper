#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <zephyr/spinlock.h>

#include "display/weact_epd154.h"
#include "ui/screen_ui.h"

static const char *const quick_replies[] = {
    "Copy that.",
    "On my way.",
    "Thanks!",
    "Need more info.",
    "Talk soon.",
};

struct screen_ui_state {
    bool thread_active;
    bool compose_active;
    bool compose_broadcast;
    uint8_t quick_reply_index;
    size_t thread_message_index;
    int32_t thread_node_num;
    int32_t selected_target_node;
    struct screen_ui_outgoing pending;
};

static struct screen_ui_state g_ui_state;
static struct message g_thread_snapshot[MAX_MESSAGE_HISTORY];
static size_t g_thread_snapshot_count;

static size_t quick_reply_count(void)
{
    return sizeof(quick_replies) / sizeof(quick_replies[0]);
}

static bool is_dm_message(const struct message *msg, uint32_t my_node_num, uint32_t peer_node_num)
{
    uint32_t from = (uint32_t)msg->from;
    uint32_t to = (uint32_t)msg->to;

    return (from == my_node_num && to == peer_node_num) ||
           (from == peer_node_num && to == my_node_num);
}

static void rebuild_thread_snapshot(struct messageHistory *history, uint32_t my_node_num, uint32_t peer_node_num)
{
    g_thread_snapshot_count = 0;

    if (history == NULL || my_node_num == 0 || peer_node_num == 0) {
        return;
    }

    k_spinlock_key_t key = k_spin_lock(&history->lock);
    size_t count = history->count;
    size_t start = 0;

    if (count > MAX_MESSAGE_HISTORY) {
        start = count - MAX_MESSAGE_HISTORY;
    }

    for (size_t seq = start; seq < count; seq++) {
        struct message candidate = history->messages[seq % MAX_MESSAGE_HISTORY];

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

    k_spin_unlock(&history->lock, key);

    if (g_thread_snapshot_count == 0) {
        g_ui_state.thread_message_index = 0;
        return;
    }

    if (g_ui_state.thread_message_index >= g_thread_snapshot_count) {
        g_ui_state.thread_message_index = g_thread_snapshot_count - 1;
    }
}

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
    snprintf(out, out_size, "0x%08X", (uint32_t)sender_num);
    return out;
}

static int render_thread_screen(struct messageHistory *message_history, struct nodeHistory *node_history)
{
    if (!g_ui_state.thread_active || node_history == NULL || !node_history->my_info.valid) {
        return 0;
    }

    uint32_t my_node_num = node_history->my_info.num;
    uint32_t peer_node_num = (uint32_t)g_ui_state.thread_node_num;
    rebuild_thread_snapshot(message_history, my_node_num, peer_node_num);

    char target_label[40];
    resolve_sender_name(node_history,
                        g_ui_state.thread_node_num,
                        target_label,
                        sizeof(target_label));

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

static bool resolve_thread_target(const struct nodeHistory *node_history, int32_t *out_node_num)
{
    int32_t selected_id = 0;
    int32_t selected_from = 0;
    int32_t selected_to = 0;

    if (out_node_num == NULL || node_history == NULL || !node_history->my_info.valid) {
        return false;
    }

    bool have_selection = weact_epd154_get_selected_message(&selected_id, &selected_from, &selected_to);
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

        bool show_popup = true;
        if (g_ui_state.thread_active &&
            (uint32_t)g_ui_state.thread_node_num == (uint32_t)latest.from) {
            show_popup = false;
        }

        int ret;
        if (g_ui_state.thread_active) {
            ret = weact_epd154_record_received_message(latest.id,
                                                       screen_text,
                                                       sender_name,
                                                       latest.from,
                                                       latest.to);
            if ((uint32_t)g_ui_state.thread_node_num == (uint32_t)latest.from) {
                // Clamp to newest so thread window shows the latest 3 entries.
                g_ui_state.thread_message_index = (size_t)MAX_MESSAGE_HISTORY;
            }
        } else {
            ret = weact_epd154_show_received_message(latest.id,
                                                     screen_text,
                                                     sender_name,
                                                     latest.from,
                                                     latest.to,
                                                     show_popup);
        }
        if (ret < 0) {
            return ret;
        }
    } else if (!g_ui_state.thread_active && !g_ui_state.compose_active) {
        int ret = weact_epd154_show_message_screen(screen_text);
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

    if (g_ui_state.compose_active) {
        render_compose(node_history);
    }

    return 0;
}

int screen_ui_handle_action(struct messageHistory *message_history,
                            struct nodeHistory *node_history,
                            enum screen_ui_action action)
{
    if (action == SCREEN_UI_ACTION_HOME) {
        g_ui_state.compose_active = false;
        g_ui_state.thread_active = false;
        return screen_ui_refresh(message_history, node_history);
    }

    if (!g_ui_state.compose_active && !g_ui_state.thread_active) {
        if (action == SCREEN_UI_ACTION_PREVIOUS) {
            return weact_epd154_previous_message();
        }

        if (action == SCREEN_UI_ACTION_NEXT) {
            return weact_epd154_next_message();
        }

        if (action == SCREEN_UI_ACTION_PRIMARY) {
            int32_t thread_node = 0;
            if (!resolve_thread_target(node_history, &thread_node)) {
                return 0;
            }

            g_ui_state.thread_active = true;
            g_ui_state.thread_node_num = thread_node;
            g_ui_state.thread_message_index = 0;
            if (node_history != NULL && node_history->my_info.valid) {
                rebuild_thread_snapshot(message_history,
                                        node_history->my_info.num,
                                        (uint32_t)thread_node);
                if (g_thread_snapshot_count > 0) {
                    g_ui_state.thread_message_index = g_thread_snapshot_count - 1;
                }
            }

            return render_thread_screen(message_history, node_history);
        }

        if (action == SCREEN_UI_ACTION_SECONDARY) {
            g_ui_state.compose_active = true;
            g_ui_state.quick_reply_index = 0;
            g_ui_state.selected_target_node = 0;
            g_ui_state.compose_broadcast = true;
            render_compose(node_history);
            return 0;
        }

        return 0;
    }

    if (g_ui_state.thread_active && !g_ui_state.compose_active) {
        if (action == SCREEN_UI_ACTION_PREVIOUS || action == SCREEN_UI_ACTION_NEXT) {
            if (node_history == NULL || !node_history->my_info.valid) {
                return 0;
            }

            rebuild_thread_snapshot(message_history,
                                    node_history->my_info.num,
                                    (uint32_t)g_ui_state.thread_node_num);
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

        if (action == SCREEN_UI_ACTION_PRIMARY) {
            g_ui_state.compose_active = true;
            g_ui_state.quick_reply_index = 0;
            g_ui_state.selected_target_node = g_ui_state.thread_node_num;
            g_ui_state.compose_broadcast = false;
            render_compose(node_history);
            return 0;
        }

        if (action == SCREEN_UI_ACTION_SECONDARY) {
            g_ui_state.thread_active = false;
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
            g_ui_state.thread_active = false;
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
