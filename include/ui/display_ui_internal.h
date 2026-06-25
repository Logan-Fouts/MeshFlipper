#ifndef DISPLAY_UI_INTERNAL_H
#define DISPLAY_UI_INTERNAL_H

#include "ui/display_ui.h"

// Constants shared across files
#define MAX_VISIBLE_MESSAGES 6
#define INBOX_VISIBLE_ROWS 4
#define NODE_PICKER_VISIBLE_ROWS 4
#define POPUP_DURATION_MS 1800
#define INBOX_BROADCAST_COMPOSE_INDEX (-1)
#define MAX_QUICK_REPLIES 10

// Quick replies
extern const char *const quick_replies[];

// Global state
extern int current_display_index;
extern int keyboard_search_low;
extern int keyboard_search_high;
extern bool keyboard_search_mode;

// Core rendering functions (used by actions)
int render_inbox(display_ui_t *ui);
int render_thread_screen(display_ui_t *ui);
int render_node_picker(display_ui_t *ui);
void render_compose(display_ui_t *ui);

// Snapshot rebuild functions
void rebuild_thread_snapshot(display_ui_t *ui, uint32_t peer_node_num);
void rebuild_broadcast_thread_snapshot(display_ui_t *ui);
void rebuild_node_picker_snapshot(display_ui_t *ui);

// Helper functions
bool is_broadcast_compose_selected(display_ui_t *ui);
bool resolve_thread_target(display_ui_t *ui, int32_t *out_node_num);
void draw_message_popup(display_ui_t *ui, const struct message *msg);
void ui_add_sent_message(display_ui_t *ui, const char *text, int32_t target_node);
bool is_broadcast_message(const struct message *msg);
int build_inbox_indices(display_ui_t *ui, int out_indices[MAX_VISIBLE_MESSAGES]);
int inbox_selected_position(const int *inbox_indices, int inbox_count);
int inbox_start_index(int selected_pos, int inbox_count);
const char* get_node_name(const struct nodeHistory *node_hist, int32_t node_num);
void build_wrapped_preview(char *out, size_t out_size, const char *text,
                          size_t chars_per_line, size_t max_lines);

// Keyboard rendering
void render_keyboard_with_search(display_ui_t *ui);

#endif