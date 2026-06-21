#ifndef DISPLAY_UI_H
#define DISPLAY_UI_H

#include <stdbool.h>
#include <stdint.h>
#include "middleware/display_driver.h"
#include "models/mesh_message.h"
#include "models/mesh_node.h"

enum screen_ui_action {
    SCREEN_UI_ACTION_PREVIOUS = 0,
    SCREEN_UI_ACTION_NEXT,
    SCREEN_UI_ACTION_PRIMARY,
    SCREEN_UI_ACTION_SECONDARY,
    SCREEN_UI_ACTION_HOME,
};

struct screen_ui_outgoing {
    bool valid;
    int32_t target_node;
    char text[96];
};

struct display_thread_entry {
    const char *text;
    bool is_outgoing;
    const char *sender_name;
};

struct display_node_entry {
    uint32_t node_num;
    const char *label;
    bool favorited;
};

typedef struct display_ui_t {
    display_driver_t driver;
    struct messageHistory *message_history;
    struct nodeHistory *node_history;
    bool initialized;
    
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
    bool popup_active;
    
    struct message thread_snapshot[32];
    size_t thread_snapshot_count;
    struct display_node_entry node_snapshot[32];
    char node_snapshot_labels[32][40];
    uint32_t node_snapshot_last_heard[32];
    size_t node_snapshot_count;
} display_ui_t;

int display_ui_init(display_ui_t *ui, const display_hal_config_t *hal_config,
                    struct messageHistory *msg_hist, struct nodeHistory *node_hist);
int display_ui_deinit(display_ui_t *ui);
int display_ui_show_boot(display_ui_t *ui);
int display_ui_show_inbox(display_ui_t *ui);
int display_ui_show_thread(display_ui_t *ui, int32_t peer_node, bool broadcast);
int display_ui_show_compose(display_ui_t *ui, int32_t target_node, bool broadcast);
int display_ui_show_node_picker(display_ui_t *ui);
int display_ui_show_popup(display_ui_t *ui, const char *title, const char *message);
int display_ui_refresh(display_ui_t *ui);
int display_ui_test_pattern(display_ui_t *ui);
int display_ui_handle_action(display_ui_t *ui, enum screen_ui_action action);
bool display_ui_take_outgoing(display_ui_t *ui, struct screen_ui_outgoing *outgoing);
void display_ui_notify_new_message(display_ui_t *ui, const struct message *msg);  // NEW

#endif