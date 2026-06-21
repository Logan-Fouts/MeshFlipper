#include "ui/display_ui.h"
#include <zephyr/logging/log.h>
#include <string.h>
#include <stdio.h>

LOG_MODULE_REGISTER(display_ui, LOG_LEVEL_INF);

#define MAX_VISIBLE_MESSAGES 6
#define INBOX_VISIBLE_ROWS 4
#define NODE_PICKER_VISIBLE_ROWS 4
#define POPUP_DURATION_MS 1800
#define INBOX_BROADCAST_COMPOSE_INDEX (-1)
#define MAX_QUICK_REPLIES 7

static const char *const quick_replies[] = {
    "Copy that.",
    "On my way.",
    "Thanks!",
    "Need more info.",
    "Talk soon.",
    "Hi",
    "Bye",
};

// Current selection state for inbox
static int current_display_index = 0;

/* Forward declarations */
static const char* get_node_name(const struct nodeHistory *node_hist, int32_t node_num);
static bool is_broadcast_message(const struct message *msg);
static void build_wrapped_preview(char *out, size_t out_size, const char *text,
                                  size_t chars_per_line, size_t max_lines);
static int render_thread_screen(display_ui_t *ui);
static int render_node_picker(display_ui_t *ui);
static void render_compose(display_ui_t *ui);
static bool resolve_thread_target(display_ui_t *ui, int32_t *out_node_num);
static void rebuild_thread_snapshot(display_ui_t *ui, uint32_t peer_node_num);
static void rebuild_broadcast_thread_snapshot(display_ui_t *ui);
static void rebuild_node_picker_snapshot(display_ui_t *ui);
static bool history_get_message_at(struct messageHistory *hist, int idx,
                                   int32_t *out_id, int32_t *out_from_num, 
                                   int32_t *out_to_num, const char **out_text);
static bool get_thread_peer_for_message(const struct messageHistory *hist, int idx,
                                        uint32_t my_node_num, int32_t *out_peer,
                                        bool *out_outgoing);
static int build_inbox_indices(display_ui_t *ui, int out_indices[MAX_VISIBLE_MESSAGES]);
static int inbox_selected_position(const int *inbox_indices, int inbox_count);
static int inbox_start_index(int selected_pos, int inbox_count);
static void ui_add_sent_message(display_ui_t *ui, const char *text, int32_t target_node);
static bool is_broadcast_compose_selected(display_ui_t *ui);
static void draw_message_popup(display_ui_t *ui, const struct message *msg);

/* Helper: Get node name */
static const char* get_node_name(const struct nodeHistory *node_hist, int32_t node_num)
{
    if (node_hist == NULL) {
        return "Unknown";
    }

    for (size_t i = 0; i < node_hist->count; i++) {
        if ((int32_t)node_hist->nodes[i].num == node_num) {
            if (node_hist->nodes[i].long_name[0] != '\0') {
                return node_hist->nodes[i].long_name;
            }
            if (node_hist->nodes[i].short_name[0] != '\0') {
                return node_hist->nodes[i].short_name;
            }
            break;
        }
    }
    return "Unknown";
}

/* Helper: Check if message is broadcast */
static bool is_broadcast_message(const struct message *msg)
{
    return msg != NULL && (uint32_t)msg->to == 0xFFFFFFFFu;
}

/* Helper: Build wrapped text preview */
static void build_wrapped_preview(char *out, size_t out_size, const char *text,
                                  size_t chars_per_line, size_t max_lines)
{
    if (out == NULL || out_size == 0 || text == NULL) return;
    
    out[0] = '\0';
    size_t out_idx = 0;
    size_t line = 0;
    size_t col = 0;
    bool truncated = false;

    for (size_t i = 0; text[i] != '\0'; i++) {
        char ch = text[i];

        if (ch == '\n') {
            if (line + 1 >= max_lines) { truncated = true; break; }
            if (out_idx + 1 >= out_size) { truncated = true; break; }
            out[out_idx++] = '\n';
            line++;
            col = 0;
            continue;
        }

        if (col >= chars_per_line) {
            if (line + 1 >= max_lines) { truncated = true; break; }
            if (out_idx + 1 >= out_size) { truncated = true; break; }
            out[out_idx++] = '\n';
            line++;
            col = 0;
        }

        if (out_idx + 1 >= out_size) { truncated = true; break; }
        out[out_idx++] = ch;
        col++;
    }

    out[out_idx] = '\0';
    if (truncated && out_idx >= 2) {
        out[out_idx - 2] = '.';
        out[out_idx - 1] = '.';
    }
}

/* Helper: Get message at index */
static bool history_get_message_at(struct messageHistory *hist, int idx,
                                   int32_t *out_id, int32_t *out_from_num, 
                                   int32_t *out_to_num, const char **out_text)
{
    if (hist == NULL || idx < 0 || idx >= (int)hist->count) {
        return false;
    }

    if (out_id != NULL) *out_id = hist->messages[idx].id;
    if (out_from_num != NULL) *out_from_num = hist->messages[idx].from;
    if (out_to_num != NULL) *out_to_num = hist->messages[idx].to;
    if (out_text != NULL) *out_text = hist->messages[idx].text;
    return true;
}

/* Helper: Get thread peer for message */
static bool get_thread_peer_for_message(const struct messageHistory *hist, int idx,
                                        uint32_t my_node_num, int32_t *out_peer,
                                        bool *out_outgoing)
{
    if (hist == NULL || idx < 0 || idx >= (int)hist->count || 
        out_peer == NULL || out_outgoing == NULL) {
        return false;
    }

    const struct message *msg = &hist->messages[idx];
    uint32_t from = (uint32_t)msg->from;
    uint32_t to = (uint32_t)msg->to;

    if (from == to) return false;

    if (my_node_num != 0) {
        if (from == my_node_num) {
            if (to == 0 || to == 0xFFFFFFFFu || to == my_node_num) return false;
            *out_peer = (int32_t)to;
            *out_outgoing = true;
            return true;
        }
        if (to == my_node_num) {
            if (from == 0 || from == 0xFFFFFFFFu || from == my_node_num) return false;
            *out_peer = (int32_t)from;
            *out_outgoing = false;
            return true;
        }
        return false;
    }

    if (from == 0 || from == 0xFFFFFFFFu) return false;
    *out_peer = (int32_t)from;
    *out_outgoing = false;
    return true;
}

/* Build inbox indices */
static int build_inbox_indices(display_ui_t *ui, int out_indices[MAX_VISIBLE_MESSAGES])
{
    struct messageHistory *hist = ui->message_history;
    struct nodeHistory *node_hist = ui->node_history;
    
    if (node_hist == NULL || !node_hist->my_info.valid) return 0;

    uint32_t my_node_num = node_hist->my_info.num;
    int per_node_latest[MAX_VISIBLE_MESSAGES];
    int per_node_count = 0;
    int32_t seen_nodes[MAX_VISIBLE_MESSAGES];
    int seen_count = 0;

    if (hist == NULL) return 0;

    for (int i = (int)hist->count - 1; i >= 0 && per_node_count < MAX_VISIBLE_MESSAGES; i--) {
        if (is_broadcast_message(&hist->messages[i])) continue;

        int32_t node_id = 0;
        bool outgoing = false;
        if (!get_thread_peer_for_message(hist, i, my_node_num, &node_id, &outgoing)) {
            continue;
        }

        bool already_seen = false;
        for (int s = 0; s < seen_count; s++) {
            if (seen_nodes[s] == node_id) { already_seen = true; break; }
        }
        if (already_seen) continue;

        seen_nodes[seen_count++] = node_id;
        per_node_latest[per_node_count++] = i;
    }

    int count = 0;
    int real_indices[MAX_VISIBLE_MESSAGES];
    int real_count = 0;
    for (int i = per_node_count - 1; i >= 0 && real_count < MAX_VISIBLE_MESSAGES; i--) {
        real_indices[real_count++] = per_node_latest[i];
    }

    // Sort descending by index (most recent first)
    for (int i = 0; i < real_count - 1; i++) {
        for (int j = i + 1; j < real_count; j++) {
            if (real_indices[j] > real_indices[i]) {
                int tmp = real_indices[i];
                real_indices[i] = real_indices[j];
                real_indices[j] = tmp;
            }
        }
    }

    out_indices[count++] = INBOX_BROADCAST_COMPOSE_INDEX;
    for (int i = 0; i < real_count && count < MAX_VISIBLE_MESSAGES; i++) {
        out_indices[count++] = real_indices[i];
    }

    return count;
}

/* Inbox selection helpers */
static int inbox_selected_position(const int *inbox_indices, int inbox_count)
{
    for (int i = 0; i < inbox_count; i++) {
        if (inbox_indices[i] == current_display_index) {
            return i;
        }
    }
    return 0;
}

static int inbox_start_index(int selected_pos, int inbox_count)
{
    if (inbox_count <= INBOX_VISIBLE_ROWS) {
        return 0;
    }

    int start = selected_pos - (INBOX_VISIBLE_ROWS - 1);
    if (start < 0) {
        start = 0;
    }

    int max_start = inbox_count - INBOX_VISIBLE_ROWS;
    if (start > max_start) {
        start = max_start;
    }

    return start;
}

/* Add sent message to UI */
static void ui_add_sent_message(display_ui_t *ui, const char *text, int32_t target_node)
{
    printk("UI: Adding sent message to thread/inbox\n");
    
    // Create a temporary message structure for the sent message
    struct message sent_msg = {
        .id = -1,  // Temporary ID, will be replaced when radio echoes back
        .from = ui->node_history->my_info.num,
        .to = target_node,
    };
    strncpy(sent_msg.text, text, MAX_MESSAGE_TEXT_LENGTH - 1);
    sent_msg.text[MAX_MESSAGE_TEXT_LENGTH - 1] = '\0';
    
    // Add to message history
    k_spinlock_key_t key = k_spin_lock(&ui->message_history->lock);
    
    // Add to history
    size_t idx = ui->message_history->count % MAX_MESSAGE_HISTORY;
    ui->message_history->messages[idx] = sent_msg;
    ui->message_history->count++;
    ui->message_history->newest_message = &ui->message_history->messages[idx];
    
    k_spin_unlock(&ui->message_history->lock, key);
    
    printk("UI: Sent message added to history at index %zu\n", idx);
    
    // If in a thread, add to thread snapshot
    if (ui->thread_active) {
        // If this is a DM thread, add to thread snapshot
        if (!ui->thread_broadcast && target_node == ui->thread_node_num) {
            // Add to thread snapshot
            if (ui->thread_snapshot_count < 32) {
                ui->thread_snapshot[ui->thread_snapshot_count++] = sent_msg;
            } else {
                // Shift and add
                for (size_t i = 0; i < 31; i++) {
                    ui->thread_snapshot[i] = ui->thread_snapshot[i + 1];
                }
                ui->thread_snapshot[31] = sent_msg;
            }
            ui->thread_message_index = ui->thread_snapshot_count - 1;
            printk("UI: Added to DM thread snapshot\n");
        }
        // If broadcast thread, add to broadcast thread
        else if (ui->thread_broadcast && target_node == 0) {
            if (ui->thread_snapshot_count < 32) {
                ui->thread_snapshot[ui->thread_snapshot_count++] = sent_msg;
            } else {
                for (size_t i = 0; i < 31; i++) {
                    ui->thread_snapshot[i] = ui->thread_snapshot[i + 1];
                }
                ui->thread_snapshot[31] = sent_msg;
            }
            ui->thread_message_index = ui->thread_snapshot_count - 1;
            printk("UI: Added to broadcast thread snapshot\n");
        }
    }
    
    printk("UI: Sent message added successfully\n");
}

/* Draw message popup */
static void draw_message_popup(display_ui_t *ui, const struct message *msg)
{
    if (!msg || !ui) return;
    
    // Get sender name
    const char *sender_name = get_node_name(ui->node_history, msg->from);
    
    // Draw popup overlay
    display_driver_draw_filled_rect(&ui->driver, 12, 46, 
                                    ui->driver.hal_config.width - 24, 
                                    ui->driver.hal_config.height - 74, false);
    display_driver_draw_rect(&ui->driver, 12, 46, 
                             ui->driver.hal_config.width - 24, 
                             ui->driver.hal_config.height - 74, true);
    display_driver_draw_rect(&ui->driver, 12, 66, 
                             ui->driver.hal_config.width - 24, 1, true);
    
    display_driver_draw_text_centered(&ui->driver, 53, 1, true, "NEW MESSAGE");
    
    char from_line[52];
    snprintf(from_line, sizeof(from_line), "FROM %s", sender_name);
    display_driver_draw_text_limited(&ui->driver, 18, 72, 1, false, from_line, 28);
    
    display_driver_draw_rect(&ui->driver, 16, 84, 
                             ui->driver.hal_config.width - 32, 1, true);
    
    // Wrap message text
    char popup_text[192];
    size_t popup_chars_per_line = (size_t)((ui->driver.hal_config.width - 36) / 6);
    build_wrapped_preview(popup_text, sizeof(popup_text), msg->text, 
                          popup_chars_per_line, 9);
    display_driver_draw_text(&ui->driver, 18, 90, 1, true, popup_text);
}

/* Rebuild thread snapshot */
static void rebuild_thread_snapshot(display_ui_t *ui, uint32_t peer_node_num)
{
    ui->thread_snapshot_count = 0;
    struct messageHistory *hist = ui->message_history;
    struct nodeHistory *node_hist = ui->node_history;
    
    if (hist == NULL || node_hist == NULL || !node_hist->my_info.valid) return;

    uint32_t my_node_num = node_hist->my_info.num;
    size_t count = hist->count;
    size_t start = (count > 32) ? count - 32 : 0;

    for (size_t seq = start; seq < count; seq++) {
        struct message candidate = hist->messages[seq % 32];
        if (candidate.id == 0 && candidate.id != -1) continue;

        uint32_t from = (uint32_t)candidate.from;
        uint32_t to = (uint32_t)candidate.to;
        
        // Include sent messages (from == my_node_num) as well
        if ((from == my_node_num && to == peer_node_num) ||
            (from == peer_node_num && to == my_node_num)) {
            if (ui->thread_snapshot_count < 32) {
                ui->thread_snapshot[ui->thread_snapshot_count++] = candidate;
            }
        }
    }
}

/* Rebuild broadcast thread snapshot */
static void rebuild_broadcast_thread_snapshot(display_ui_t *ui)
{
    ui->thread_snapshot_count = 0;
    struct messageHistory *hist = ui->message_history;
    if (hist == NULL) return;

    size_t count = hist->count;
    size_t start = (count > 32) ? count - 32 : 0;

    for (size_t seq = start; seq < count; seq++) {
        struct message candidate = hist->messages[seq % 32];
        if (is_broadcast_message(&candidate)) {
            if (ui->thread_snapshot_count < 32) {
                ui->thread_snapshot[ui->thread_snapshot_count++] = candidate;
            }
        }
    }
}

/* Rebuild node picker snapshot */
static void rebuild_node_picker_snapshot(display_ui_t *ui)
{
    ui->node_snapshot_count = 0;
    struct nodeHistory *node_hist = ui->node_history;
    if (node_hist == NULL) return;

    uint32_t my_node = node_hist->my_info.valid ? node_hist->my_info.num : 0;
    size_t count = node_hist->count < 32 ? node_hist->count : 32;

    for (size_t i = 0; i < count && ui->node_snapshot_count < 32; i++) {
        const struct node_info *n = &node_hist->nodes[i];
        if (!n->valid || n->num == 0 || n->num == my_node) continue;

        bool duplicate = false;
        for (size_t s = 0; s < ui->node_snapshot_count; s++) {
            if (ui->node_snapshot[s].node_num == n->num) {
                duplicate = true;
                break;
            }
        }
        if (duplicate) continue;

        char *label = ui->node_snapshot_labels[ui->node_snapshot_count];
        if (n->long_name[0] != '\0') {
            strncpy(label, n->long_name, 39);
        } else if (n->short_name[0] != '\0') {
            strncpy(label, n->short_name, 39);
        } else {
            snprintf(label, 40, "0x%08X", (unsigned int)n->num);
        }
        label[39] = '\0';

        ui->node_snapshot[ui->node_snapshot_count].node_num = n->num;
        ui->node_snapshot[ui->node_snapshot_count].label = label;
        ui->node_snapshot[ui->node_snapshot_count].favorited = n->favorited;
        ui->node_snapshot_last_heard[ui->node_snapshot_count] = n->last_heard;
        ui->node_snapshot_count++;
    }

    /* Sort by favorited first, then by last_heard */
    for (size_t i = 0; i + 1 < ui->node_snapshot_count; i++) {
        for (size_t j = i + 1; j < ui->node_snapshot_count; j++) {
            bool should_swap = false;
            if (ui->node_snapshot[j].favorited && !ui->node_snapshot[i].favorited) {
                should_swap = true;
            } else if (ui->node_snapshot[j].favorited == ui->node_snapshot[i].favorited) {
                if (ui->node_snapshot_last_heard[j] > ui->node_snapshot_last_heard[i]) {
                    should_swap = true;
                }
            }
            if (should_swap) {
                uint32_t tmp_heard = ui->node_snapshot_last_heard[i];
                ui->node_snapshot_last_heard[i] = ui->node_snapshot_last_heard[j];
                ui->node_snapshot_last_heard[j] = tmp_heard;
                
                struct display_node_entry tmp_node = ui->node_snapshot[i];
                ui->node_snapshot[i] = ui->node_snapshot[j];
                ui->node_snapshot[j] = tmp_node;
            }
        }
    }
}

/* Render inbox screen */
static int render_inbox(display_ui_t *ui)
{
    display_driver_clear(&ui->driver);
    
    int inbox_indices[MAX_VISIBLE_MESSAGES];
    int inbox_count = build_inbox_indices(ui, inbox_indices);
    int selected_pos = inbox_selected_position(inbox_indices, inbox_count);

    if (inbox_count > 0) {
        current_display_index = inbox_indices[selected_pos];
    }
    
    /* Top bar */
    display_driver_draw_text_centered(&ui->driver, 5, 1, false, "MESSAGES");
    display_driver_draw_rect(&ui->driver, 0, 23, ui->driver.hal_config.width, 1, true);
    
    /* Bottom bar */
    display_driver_draw_rect(&ui->driver, 0, ui->driver.hal_config.height - 14, 
                             ui->driver.hal_config.width, 1, true);
    char status[32];
    snprintf(status, sizeof(status), "INBOX: %d", inbox_count);
    display_driver_draw_text(&ui->driver, 4, ui->driver.hal_config.height - 11, 1, false, status);
    
    /* Navigation arrows */
    if (inbox_count > 1) {
        display_driver_draw_text(&ui->driver, ui->driver.hal_config.width - 24, 
                                 ui->driver.hal_config.height - 11, 1, false, "< >");
        char idx_str[16];
        snprintf(idx_str, sizeof(idx_str), "%u/%u",
                 (unsigned int)(selected_pos + 1),
                 (unsigned int)inbox_count);
        display_driver_draw_text(&ui->driver, ui->driver.hal_config.width - 44, 
                                 ui->driver.hal_config.height - 11, 1, false, idx_str);
    }
    
    /* Content area */
    display_driver_draw_rect(&ui->driver, 2, 26, ui->driver.hal_config.width - 4, 
                             ui->driver.hal_config.height - 40, true);
    
    if (inbox_count == 0) {
        display_driver_draw_text_centered(&ui->driver, ui->driver.hal_config.height / 2 - 20, 
                                          2, true, "NO MESSAGES");
    } else {
        const int row_h = 36;
        int start = inbox_start_index(selected_pos, inbox_count);
        int rows = inbox_count - start;
        if (rows > INBOX_VISIBLE_ROWS) {
            rows = INBOX_VISIBLE_ROWS;
        }

        for (int i = 0; i < rows; i++) {
            int visible_idx = start + i;
            int idx = inbox_indices[visible_idx];
            int row_y = 28 + (i * row_h);

            if (visible_idx == selected_pos) {
                display_driver_draw_rect(&ui->driver, 4, row_y - 1, 
                                         ui->driver.hal_config.width - 8, row_h - 1, true);
                display_driver_draw_rect(&ui->driver, 4, row_y - 2, 
                                         ui->driver.hal_config.width - 8, row_h, true);
            }

            if (idx == INBOX_BROADCAST_COMPOSE_INDEX) {
                display_driver_draw_text_limited(&ui->driver, 8, row_y + 3, 1, true, "BROADCAST", 22);
                display_driver_draw_text_limited(&ui->driver, 8, row_y + 16, 1, false, "SEND TO ALL NODES", 31);
            } else {
                int32_t from_num = 0;
                const char *msg_text = "";
                history_get_message_at(ui->message_history, idx, NULL, &from_num, NULL, &msg_text);
                
                // Check if this message was sent by us
                bool is_outgoing = (from_num == (int32_t)ui->node_history->my_info.num);
                const char *display_name;
                
                if (is_outgoing) {
                    // For outgoing messages, show the recipient's name (or "BROADCAST" if it was a broadcast)
                    int32_t to_num = 0;
                    history_get_message_at(ui->message_history, idx, NULL, NULL, &to_num, NULL);
                    if (to_num == 0 || to_num == 0xFFFFFFFFu) {
                        display_name = "BROADCAST";
                    } else {
                        display_name = get_node_name(ui->node_history, to_num);
                    }
                } else {
                    display_name = get_node_name(ui->node_history, from_num);
                }
                
                display_driver_draw_text_limited(&ui->driver, 8, row_y + 3, 1, true, display_name, 22);
                display_driver_draw_text_limited(&ui->driver, 8, row_y + 16, 1, false, msg_text, 31);
            }

            if (i < rows - 1) {
                display_driver_draw_rect(&ui->driver, 6, row_y + row_h - 2, 
                                         ui->driver.hal_config.width - 12, 1, true);
            }
        }
    }
    
    return display_driver_refresh(&ui->driver);
}

/* Render thread screen */
static int render_thread_screen(display_ui_t *ui)
{
    display_driver_clear(&ui->driver);
    
    char target_label[40] = "BROADCAST";
    if (!ui->thread_broadcast) {
        const char *name = get_node_name(ui->node_history, ui->thread_node_num);
        strncpy(target_label, name, sizeof(target_label) - 1);
        target_label[sizeof(target_label) - 1] = '\0';
    }
    
    /* Header - always show the recipient name or BROADCAST */
    char title[72];
    snprintf(title, sizeof(title), "DM: %s", target_label);
    display_driver_draw_text_limited(&ui->driver, 4, 4, 1, false, title, 28);
    
    char pos[24];
    snprintf(pos, sizeof(pos), "%u/%u", (unsigned int)ui->thread_message_index + 1,
             (unsigned int)ui->thread_snapshot_count);
    display_driver_draw_text(&ui->driver, ui->driver.hal_config.width - 30, 4, 1, false, pos);
    
    display_driver_draw_rect(&ui->driver, 0, 20, ui->driver.hal_config.width, 3, true);
    
    /* Message bubbles */
    const int message_top = 28;
    const int message_bottom = ui->driver.hal_config.height - 28;
    const int bubble_width = (ui->driver.hal_config.width * 75) / 100;
    const int bubble_padding = 10;
    const int bubble_gap = 4;
    
    if (ui->thread_snapshot_count == 0) {
        display_driver_draw_text_centered(&ui->driver, message_top + 40, 1, false, "NO MESSAGES YET");
    } else {
        size_t chars_per_line = (size_t)((bubble_width - 12) / 6);
        if (chars_per_line == 0) chars_per_line = 1;
        
        int y = message_top;
        size_t start_idx = 0;
        if (ui->thread_message_index >= 3) {
            start_idx = ui->thread_message_index - 2;
        }
        
        for (size_t i = start_idx; i < ui->thread_snapshot_count && i < start_idx + 4; i++) {
            const struct message *msg = &ui->thread_snapshot[i];
            bool outgoing = ((uint32_t)msg->from == ui->node_history->my_info.num);
            
            int x = outgoing ? (ui->driver.hal_config.width - bubble_width - bubble_padding) : bubble_padding;
            
            char bubble_text[512];
            build_wrapped_preview(bubble_text, sizeof(bubble_text), msg->text, chars_per_line, 10);
            size_t lines = 1;
            for (size_t p = 0; bubble_text[p] != '\0'; p++) {
                if (bubble_text[p] == '\n') lines++;
            }
            int bubble_h = 20 + (int)(lines * 8);
            
            if (y + bubble_h > message_bottom) break;
            
            display_driver_draw_rect(&ui->driver, x, y, bubble_width, bubble_h, true);
            
            // For outgoing messages, show "YOU" as the sender
            const char *sender = outgoing ? "YOU" : get_node_name(ui->node_history, msg->from);
            display_driver_draw_text(&ui->driver, outgoing ? x + bubble_width - 30 : x + 6, y + 2, 1, false, sender);
            display_driver_draw_rect(&ui->driver, x + 4, y + 12, bubble_width - 8, 1, true);
            display_driver_draw_text(&ui->driver, x + 6, y + 16, 1, true, bubble_text);
            
            y += bubble_h + bubble_gap;
        }
    }
    
    /* Footer */
    display_driver_draw_rect(&ui->driver, 0, ui->driver.hal_config.height - 25, 
                             ui->driver.hal_config.width, 3, true);
    char footer[48];
    snprintf(footer, sizeof(footer), "2:Prev   3:Reply   4:Next   5:Back");
    int footer_width = strlen(footer) * 6;
    int footer_x = (ui->driver.hal_config.width - footer_width) / 2;
    display_driver_draw_text(&ui->driver, footer_x, ui->driver.hal_config.height - 18, 1, false, footer);
    
    return display_driver_refresh(&ui->driver);
}

/* Render node picker */
static int render_node_picker(display_ui_t *ui)
{
    rebuild_node_picker_snapshot(ui);
    display_driver_clear(&ui->driver);
    
    display_driver_draw_text_centered(&ui->driver, 5, 1, false, "SELECT NODE");
    display_driver_draw_rect(&ui->driver, 0, 23, ui->driver.hal_config.width, 1, true);
    display_driver_draw_rect(&ui->driver, 2, 26, ui->driver.hal_config.width - 4, 
                             ui->driver.hal_config.height - 44, true);
    
    if (ui->node_snapshot_count == 0) {
        display_driver_draw_text_centered(&ui->driver, ui->driver.hal_config.height / 2 - 14, 
                                          2, true, "NO NODES");
        display_driver_draw_text_centered(&ui->driver, ui->driver.hal_config.height / 2 + 8, 
                                          1, false, "WAITING FOR NODE INFO");
    } else {
        const int row_h = 36;
        // Calculate which nodes to show based on current index
        size_t start = 0;
        size_t max_visible = NODE_PICKER_VISIBLE_ROWS;
        
        // If we have more nodes than visible rows, calculate start
        if (ui->node_snapshot_count > max_visible) {
            // Center the selected item if possible
            if (ui->node_picker_index >= max_visible / 2) {
                start = ui->node_picker_index - max_visible / 2;
                // Make sure we don't go past the end
                if (start + max_visible > ui->node_snapshot_count) {
                    start = ui->node_snapshot_count - max_visible;
                }
            }
        }
        
        size_t rows = ui->node_snapshot_count - start;
        if (rows > max_visible) {
            rows = max_visible;
        }
        
        for (size_t i = 0; i < rows; i++) {
            size_t idx = start + i;
            int row_y = 28 + (int)(i * row_h);
            const struct display_node_entry *entry = &ui->node_snapshot[idx];
            
            // Draw thick border around selected entry (like the original style)
            if (idx == ui->node_picker_index) {
                // Draw a thicker border (2px) around the selected row
                display_driver_draw_rect(&ui->driver, 4, row_y - 1, 
                                         ui->driver.hal_config.width - 8, row_h - 1, true);
                display_driver_draw_rect(&ui->driver, 4, row_y - 2, 
                                         ui->driver.hal_config.width - 8, row_h, true);
            }
            
            char node_id[20];
            snprintf(node_id, sizeof(node_id), "0x%08X", (unsigned int)entry->node_num);
            
            // Always draw text in black (true = black)
            display_driver_draw_text_limited(&ui->driver, 8, row_y + 3, 1, true, entry->label, 24);
            display_driver_draw_text_limited(&ui->driver, 8, row_y + 16, 1, false, node_id, 16);
            
            if (i < rows - 1) {
                display_driver_draw_rect(&ui->driver, 6, row_y + row_h - 2, 
                                         ui->driver.hal_config.width - 12, 1, true);
            }
        }
    }
    
    /* Footer */
    display_driver_draw_rect(&ui->driver, 0, ui->driver.hal_config.height - 14, 
                             ui->driver.hal_config.width, 1, true);
    display_driver_draw_text(&ui->driver, 4, ui->driver.hal_config.height - 11, 1, false, 
                             "2/4 Nav 3 Pick 5 Back");
    
    char idx_text[24];
    if (ui->node_snapshot_count > 0) {
        snprintf(idx_text, sizeof(idx_text), "%u/%u", (unsigned int)(ui->node_picker_index + 1),
                 (unsigned int)ui->node_snapshot_count);
    } else {
        snprintf(idx_text, sizeof(idx_text), "0/0");
    }
    display_driver_draw_text(&ui->driver, ui->driver.hal_config.width - 46, 
                             ui->driver.hal_config.height - 11, 1, false, idx_text);
    
    return display_driver_refresh(&ui->driver);
}

/* Render compose screen */
static void render_compose(display_ui_t *ui)
{
    display_driver_clear(&ui->driver);
    
    char target_label[48];
    if (ui->compose_broadcast || ui->selected_target_node == 0) {
        strncpy(target_label, "BROADCAST", sizeof(target_label) - 1);
    } else {
        const char *name = get_node_name(ui->node_history, ui->selected_target_node);
        strncpy(target_label, name, sizeof(target_label) - 1);
    }
    target_label[sizeof(target_label) - 1] = '\0';
    
    display_driver_draw_filled_rect(&ui->driver, 10, 42, ui->driver.hal_config.width - 20,
                                    ui->driver.hal_config.height - 56, false);
    display_driver_draw_rect(&ui->driver, 10, 42, ui->driver.hal_config.width - 20,
                             ui->driver.hal_config.height - 56, true);
    
    display_driver_draw_text(&ui->driver, 16, 48, 1, true, "COMPOSE");
    display_driver_draw_rect(&ui->driver, 12, 60, ui->driver.hal_config.width - 24, 1, true);
    
    char to_line[72];
    snprintf(to_line, sizeof(to_line), "TO %s %s", ui->compose_broadcast ? "ALL" : "NODE", target_label);
    display_driver_draw_text_limited(&ui->driver, 16, 66, 1, false, to_line, 34);
    display_driver_draw_rect(&ui->driver, 14, 78, ui->driver.hal_config.width - 28, 1, true);
    
    const char *reply = quick_replies[ui->quick_reply_index];
    display_driver_draw_text_limited(&ui->driver, 16, 86, 1, true, reply, 84);
    
    display_driver_draw_rect(&ui->driver, 12, ui->driver.hal_config.height - 32,
                             ui->driver.hal_config.width - 24, 1, true);
    display_driver_draw_text(&ui->driver, 16, ui->driver.hal_config.height - 28, 1, false, 
                             "2/4:Draft 5:Mode 3:Send");
    
    display_driver_refresh(&ui->driver);
}

/* Resolve thread target */
static bool resolve_thread_target(display_ui_t *ui, int32_t *out_node_num)
{
    if (out_node_num == NULL || ui->node_history == NULL || !ui->node_history->my_info.valid) {
        return false;
    }

    int inbox_indices[MAX_VISIBLE_MESSAGES];
    int inbox_count = build_inbox_indices(ui, inbox_indices);
    if (inbox_count <= 0) return false;

    int selected_pos = inbox_selected_position(inbox_indices, inbox_count);
    int selected_raw = inbox_indices[selected_pos];
    current_display_index = selected_raw;

    if (selected_raw == INBOX_BROADCAST_COMPOSE_INDEX) {
        return false;
    }

    uint32_t my_node_num = ui->node_history->my_info.num;
    uint32_t from = (uint32_t)ui->message_history->messages[selected_raw].from;
    uint32_t to = (uint32_t)ui->message_history->messages[selected_raw].to;

    if (from == my_node_num) {
        if (to == 0 || to == 0xFFFFFFFFu || to == my_node_num) return false;
        *out_node_num = (int32_t)to;
        return true;
    }

    if (from == 0 || from == 0xFFFFFFFFu || from == my_node_num) return false;
    *out_node_num = (int32_t)from;
    return true;
}

/* Check if broadcast compose is selected */
static bool is_broadcast_compose_selected(display_ui_t *ui)
{
    int inbox_indices[MAX_VISIBLE_MESSAGES];
    int inbox_count = build_inbox_indices(ui, inbox_indices);
    if (inbox_count <= 0) return false;

    int selected_pos = inbox_selected_position(inbox_indices, inbox_count);
    int selected_raw = inbox_indices[selected_pos];
    current_display_index = selected_raw;

    return selected_raw == INBOX_BROADCAST_COMPOSE_INDEX;
}

/* Public API: Initialize */
int display_ui_init(display_ui_t *ui, const display_hal_config_t *hal_config,
                    struct messageHistory *msg_hist, struct nodeHistory *node_hist)
{
    printk("DISPLAY_UI_INIT: Starting...\n");
    
    if (!ui || !hal_config) {
        printk("DISPLAY_UI_INIT: Invalid params\n");
        return -EINVAL;
    }
    
    printk("DISPLAY_UI_INIT: Initializing driver...\n");
    int ret = display_driver_init(&ui->driver, hal_config);
    if (ret < 0) {
        printk("DISPLAY_UI_INIT: Driver init failed: %d\n", ret);
        return ret;
    }
    printk("DISPLAY_UI_INIT: Driver initialized\n");
    
    ui->message_history = msg_hist;
    ui->node_history = node_hist;
    ui->initialized = true;
    ui->quick_reply_index = 0;
    ui->thread_message_index = 0;
    ui->node_picker_index = 0;
    ui->last_handled_incoming_id = 0;
    ui->pending.valid = false;
    ui->thread_active = false;
    ui->thread_broadcast = false;
    ui->node_picker_active = false;
    ui->compose_active = false;
    ui->compose_broadcast = false;
    ui->popup_active = false;
    current_display_index = 0;
    
    printk("DISPLAY_UI_INIT: Complete!\n");
    return 0;
}

int display_ui_deinit(display_ui_t *ui)
{
    if (!ui) return -EINVAL;
    ui->initialized = false;
    return display_driver_deinit(&ui->driver);
}

/* Public API: Show boot screen */
int display_ui_show_boot(display_ui_t *ui)
{
    if (!ui || !ui->initialized) return -EINVAL;
    display_driver_clear(&ui->driver);
    display_driver_draw_text_centered(&ui->driver, 80, 2, true, "MESHFLIPPER");
    display_driver_draw_text_centered(&ui->driver, 110, 1, false, "Starting...");
    display_driver_draw_rect(&ui->driver, 0, 130, ui->driver.hal_config.width, 1, true);
    display_driver_draw_text(&ui->driver, 4, 140, 1, false, "v1.0");
    return display_driver_refresh(&ui->driver);
}

/* Public API: Show inbox */
int display_ui_show_inbox(display_ui_t *ui)
{
    if (!ui || !ui->initialized) return -EINVAL;
    current_display_index = 0;
    return render_inbox(ui);
}

/* Public API: Show thread */
int display_ui_show_thread(display_ui_t *ui, int32_t peer_node, bool broadcast)
{
    if (!ui || !ui->initialized) return -EINVAL;
    ui->thread_active = true;
    ui->thread_broadcast = broadcast;
    ui->thread_node_num = peer_node;
    ui->thread_message_index = 0;
    
    if (broadcast) {
        rebuild_broadcast_thread_snapshot(ui);
    } else {
        rebuild_thread_snapshot(ui, (uint32_t)peer_node);
    }
    if (ui->thread_snapshot_count > 0) {
        ui->thread_message_index = ui->thread_snapshot_count - 1;
    }
    
    return render_thread_screen(ui);
}

/* Public API: Show compose */
int display_ui_show_compose(display_ui_t *ui, int32_t target_node, bool broadcast)
{
    if (!ui || !ui->initialized) return -EINVAL;
    ui->compose_active = true;
    ui->compose_broadcast = broadcast;
    ui->selected_target_node = target_node;
    ui->quick_reply_index = 0;
    render_compose(ui);
    return 0;
}

/* Public API: Show node picker */
int display_ui_show_node_picker(display_ui_t *ui)
{
    if (!ui || !ui->initialized) return -EINVAL;
    ui->node_picker_active = true;
    ui->node_picker_index = 0;
    return render_node_picker(ui);
}

/* Public API: Show popup */
int display_ui_show_popup(display_ui_t *ui, const char *title, const char *message)
{
    if (!ui || !ui->initialized) return -EINVAL;
    
    display_driver_draw_filled_rect(&ui->driver, 12, 46, ui->driver.hal_config.width - 24,
                                    ui->driver.hal_config.height - 74, false);
    display_driver_draw_rect(&ui->driver, 12, 46, ui->driver.hal_config.width - 24,
                             ui->driver.hal_config.height - 74, true);
    display_driver_draw_rect(&ui->driver, 12, 66, ui->driver.hal_config.width - 24, 1, true);
    display_driver_draw_text_centered(&ui->driver, 53, 1, true, title ? title : "MESSAGE");
    display_driver_draw_text(&ui->driver, 18, 90, 1, true, message ? message : "");
    
    return display_driver_refresh(&ui->driver);
}

/* Public API: Refresh UI */
int display_ui_refresh(display_ui_t *ui)
{
    if (!ui || !ui->initialized) return -EINVAL;
    
    if (ui->thread_active) {
        if (ui->thread_broadcast) {
            rebuild_broadcast_thread_snapshot(ui);
        } else {
            rebuild_thread_snapshot(ui, (uint32_t)ui->thread_node_num);
        }
        return render_thread_screen(ui);
    } else if (ui->node_picker_active) {
        return render_node_picker(ui);
    } else if (ui->compose_active) {
        render_compose(ui);
        return 0;
    } else {
        return render_inbox(ui);
    }
}

/* Public API: Handle actions */
int display_ui_handle_action(display_ui_t *ui, enum screen_ui_action action)
{
    printk("HANDLE ACTION: %d\n", action);
    
    if (!ui || !ui->initialized) {
        printk("UI not initialized!\n");
        return -EINVAL;
    }

    // Home action - exit everything
    if (action == SCREEN_UI_ACTION_HOME) {
        printk("HOME action\n");
        ui->compose_active = false;
        ui->thread_active = false;
        ui->thread_broadcast = false;
        ui->node_picker_active = false;
        current_display_index = 0;
        return display_ui_refresh(ui);
    }

    // Inbox state - this is the main inbox view
    if (!ui->compose_active && !ui->thread_active && !ui->node_picker_active) {
        printk("Inbox state, action: %d\n", action);
        
        if (action == SCREEN_UI_ACTION_PREVIOUS) {
            // Move to previous message in inbox
            int inbox_indices[MAX_VISIBLE_MESSAGES];
            int inbox_count = build_inbox_indices(ui, inbox_indices);
            if (inbox_count > 0) {
                int selected_pos = inbox_selected_position(inbox_indices, inbox_count);
                selected_pos = (selected_pos - 1 + inbox_count) % inbox_count;
                current_display_index = inbox_indices[selected_pos];
                return render_inbox(ui);
            }
            return 0;
        }

        if (action == SCREEN_UI_ACTION_NEXT) {
            // Move to next message in inbox
            int inbox_indices[MAX_VISIBLE_MESSAGES];
            int inbox_count = build_inbox_indices(ui, inbox_indices);
            if (inbox_count > 0) {
                int selected_pos = inbox_selected_position(inbox_indices, inbox_count);
                selected_pos = (selected_pos + 1) % inbox_count;
                current_display_index = inbox_indices[selected_pos];
                return render_inbox(ui);
            }
            return 0;
        }

        if (action == SCREEN_UI_ACTION_PRIMARY) {
            // Enter thread view for selected message
            printk("PRIMARY in inbox - trying to enter thread\n");
            
            if (is_broadcast_compose_selected(ui)) {
                ui->thread_active = true;
                ui->thread_broadcast = true;
                ui->thread_message_index = 0;
                rebuild_broadcast_thread_snapshot(ui);
                if (ui->thread_snapshot_count > 0) {
                    ui->thread_message_index = ui->thread_snapshot_count - 1;
                }
                return render_thread_screen(ui);
            }

            int32_t thread_node = 0;
            if (resolve_thread_target(ui, &thread_node)) {
                return display_ui_show_thread(ui, thread_node, false);
            }
            printk("No thread target found\n");
            return 0;
        }

        if (action == SCREEN_UI_ACTION_SECONDARY) {
            // Show node picker
            printk("SECONDARY in inbox - showing node picker\n");
            return display_ui_show_node_picker(ui);
        }
        return 0;
    }

    // Node picker state
    if (ui->node_picker_active && !ui->compose_active && !ui->thread_active) {
        printk("Node picker state, action: %d\n", action);
        
        if (action == SCREEN_UI_ACTION_PREVIOUS || action == SCREEN_UI_ACTION_NEXT) {
            rebuild_node_picker_snapshot(ui);
            if (ui->node_snapshot_count == 0) {
                return render_node_picker(ui);
            }
            
            if (action == SCREEN_UI_ACTION_PREVIOUS) {
                // Wrap around to the end if at the beginning
                if (ui->node_picker_index == 0) {
                    ui->node_picker_index = ui->node_snapshot_count - 1;
                } else {
                    ui->node_picker_index--;
                }
            } else { // NEXT
                // Wrap around to the beginning if at the end
                ui->node_picker_index = (ui->node_picker_index + 1) % ui->node_snapshot_count;
            }
            printk("Node picker index: %zu/%zu\n", ui->node_picker_index + 1, ui->node_snapshot_count);
            return render_node_picker(ui);
        }
        
        if (action == SCREEN_UI_ACTION_PRIMARY) {
            rebuild_node_picker_snapshot(ui);
            if (ui->node_snapshot_count == 0) {
                return render_node_picker(ui);
            }
            
            ui->node_picker_active = false;
            return display_ui_show_compose(ui, (int32_t)ui->node_snapshot[ui->node_picker_index].node_num, false);
        }
        
        if (action == SCREEN_UI_ACTION_SECONDARY) {
            ui->node_picker_active = false;
            current_display_index = 0;
            return display_ui_refresh(ui);
        }
        return 0;
    }

    // Thread state
    if (ui->thread_active && !ui->compose_active) {
        printk("Thread state, action: %d\n", action);
        
        if (action == SCREEN_UI_ACTION_PREVIOUS || action == SCREEN_UI_ACTION_NEXT) {
            if (ui->thread_snapshot_count == 0) return render_thread_screen(ui);
            
            if (action == SCREEN_UI_ACTION_PREVIOUS) {
                ui->thread_message_index = (ui->thread_message_index == 0) ? 
                    ui->thread_snapshot_count - 1 : ui->thread_message_index - 1;
            } else {
                ui->thread_message_index = (ui->thread_message_index + 1) % ui->thread_snapshot_count;
            }
            return render_thread_screen(ui);
        }
        
        if (action == SCREEN_UI_ACTION_PRIMARY) {
            // Reply - enter compose mode
            return display_ui_show_compose(ui, ui->thread_node_num, ui->thread_broadcast);
        }
        
        if (action == SCREEN_UI_ACTION_SECONDARY) {
            // Back to inbox
            ui->thread_active = false;
            ui->thread_broadcast = false;
            current_display_index = 0;
            return display_ui_refresh(ui);
        }
        return 0;
    }

    // Compose state
    if (ui->compose_active) {
        printk("Compose state, action: %d\n", action);
        
        if (action == SCREEN_UI_ACTION_PREVIOUS) {
            ui->quick_reply_index = (ui->quick_reply_index == 0) ? 
                MAX_QUICK_REPLIES - 1 : ui->quick_reply_index - 1;
            render_compose(ui);
            return 0;
        }
        
        if (action == SCREEN_UI_ACTION_NEXT) {
            ui->quick_reply_index = (ui->quick_reply_index + 1) % MAX_QUICK_REPLIES;
            render_compose(ui);
            return 0;
        }
        
        if (action == SCREEN_UI_ACTION_SECONDARY) {
            ui->compose_broadcast = !ui->compose_broadcast;
            render_compose(ui);
            return 0;
        }
        
        if (action == SCREEN_UI_ACTION_PRIMARY) {
            // Get the message text
            const char *message_text = quick_replies[ui->quick_reply_index];
            int32_t target = ui->compose_broadcast ? 0 : ui->selected_target_node;
            
            printk("Sending message: '%s' to %d\n", message_text, target);
            
            // Store the pending message for sending
            ui->pending.valid = true;
            ui->pending.target_node = target;
            strncpy(ui->pending.text, message_text, sizeof(ui->pending.text) - 1);
            ui->pending.text[sizeof(ui->pending.text) - 1] = '\0';
            
            // Add the sent message to the UI immediately so user can see it
            ui_add_sent_message(ui, message_text, target);
            
            // Exit compose
            ui->compose_active = false;
            
            // Refresh the UI to show the sent message
            return display_ui_refresh(ui);
        }
        return 0;
    }

    printk("Unhandled action %d in unknown state\n", action);
    return 0;
}

/* Public API: Take outgoing message */
bool display_ui_take_outgoing(display_ui_t *ui, struct screen_ui_outgoing *outgoing)
{
    if (ui == NULL || outgoing == NULL || !ui->pending.valid) return false;
    
    *outgoing = ui->pending;
    ui->pending.valid = false;
    return true;
}

/* Public API: Test pattern */
int display_ui_test_pattern(display_ui_t *ui)
{
    printk("DISPLAY_TEST: Drawing test pattern...\n");
    
    if (!ui || !ui->initialized) {
        printk("DISPLAY_TEST: UI not initialized!\n");
        return -EINVAL;
    }
    
    display_driver_clear(&ui->driver);
    printk("DISPLAY_TEST: Display cleared\n");
    
    // Draw a border
    display_driver_draw_rect(&ui->driver, 10, 10, 
                             ui->driver.hal_config.width - 20, 
                             ui->driver.hal_config.height - 20, true);
    printk("DISPLAY_TEST: Border drawn\n");
    
    // Draw some shapes
    display_driver_draw_filled_rect(&ui->driver, 30, 30, 40, 40, true);
    display_driver_draw_rect(&ui->driver, 80, 30, 40, 40, true);
    printk("DISPLAY_TEST: Shapes drawn\n");
    
    // Draw text
    display_driver_draw_text(&ui->driver, 30, 80, 1, true, "TEST");
    display_driver_draw_text(&ui->driver, 30, 100, 2, true, "MESH");
    printk("DISPLAY_TEST: Text drawn\n");
    
    // Draw lines
    display_driver_draw_hline(&ui->driver, 30, 140, 100, true);
    display_driver_draw_vline(&ui->driver, 80, 140, 50, true);
    printk("DISPLAY_TEST: Lines drawn\n");
    
    printk("DISPLAY_TEST: Refreshing display...\n");
    int ret = display_driver_refresh(&ui->driver);
    printk("DISPLAY_TEST: Refresh returned: %d\n", ret);
    
    return ret;
}

void display_ui_notify_new_message(display_ui_t *ui, const struct message *msg)
{
    if (!ui || !ui->initialized || !msg) return;
    
    printk("UI: New message notification - from %d to %d, text: %s\n", msg->from, msg->to, msg->text);
    
    // Check if this is a broadcast message
    bool is_broadcast = is_broadcast_message(msg);
    uint32_t my_node = ui->node_history->my_info.num;
    bool is_from_me = (msg->from == (int32_t)my_node);
    
    // Don't show popup for messages we sent
    if (is_from_me) {
        printk("UI: Skipping popup - message from self\n");
        return;
    }
    
    // Determine the sender/peer node
    int32_t peer_node = msg->from;
    if (is_broadcast) {
        peer_node = 0;  // Broadcast has no specific peer
    }
    
    // Check if we're currently in a thread with this node
    bool in_this_thread = false;
    if (ui->thread_active) {
        if (is_broadcast && ui->thread_broadcast) {
            in_this_thread = true;
        } else if (!is_broadcast && !ui->thread_broadcast && ui->thread_node_num == peer_node) {
            in_this_thread = true;
        }
    }
    
    // If we're in this thread, just refresh the view
    if (in_this_thread) {
        printk("UI: In this thread - refreshing view\n");
        ui->last_handled_incoming_id = msg->id;
        // Rebuild thread snapshot to include new message
        if (ui->thread_broadcast) {
            rebuild_broadcast_thread_snapshot(ui);
        } else {
            rebuild_thread_snapshot(ui, (uint32_t)ui->thread_node_num);
        }
        // Update index to show newest message
        if (ui->thread_snapshot_count > 0) {
            ui->thread_message_index = ui->thread_snapshot_count - 1;
        }
        display_ui_refresh(ui);
        return;
    }
    
    // If we're in a different thread or inbox, show popup
    if (!ui->popup_active) {
        printk("UI: Showing popup for new message\n");
        ui->popup_active = true;
        
        // First, render the current screen
        int ret = display_ui_refresh(ui);
        if (ret < 0) {
            printk("UI: Refresh failed: %d\n", ret);
        }
        
        // Then draw popup overlay on top
        draw_message_popup(ui, msg);
        
        // Refresh to show the popup
        ret = display_driver_refresh(&ui->driver);
        if (ret < 0) {
            printk("UI: Popup refresh failed: %d\n", ret);
        }
        
        // Wait for popup duration
        k_sleep(K_MSEC(POPUP_DURATION_MS));
        ui->popup_active = false;
        
        // Refresh to clear popup
        display_ui_refresh(ui);
    }
    
    ui->last_handled_incoming_id = msg->id;
}