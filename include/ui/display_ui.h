#ifndef DISPLAY_UI_H
#define DISPLAY_UI_H

#include <stdbool.h>
#include <stdint.h>
#include <zephyr/kernel.h>
#include "middleware/display_driver.h"
#include "models/mesh_message.h"
#include "models/mesh_node.h"
#include "ui/on_screen_keyb.h"

// UI Actions
enum screen_ui_action {
    SCREEN_UI_ACTION_PREVIOUS = 0,
    SCREEN_UI_ACTION_NEXT,
    SCREEN_UI_ACTION_PRIMARY,
    SCREEN_UI_ACTION_SECONDARY,
    SCREEN_UI_ACTION_HOME,
    SCREEN_UI_ACTION_EXT,
};

// Outgoing message from UI
struct screen_ui_outgoing {
    bool valid;
    int32_t target_node;
    char text[96];
};

// Thread entry for rendering
struct display_thread_entry {
    const char *text;
    bool is_outgoing;
    const char *sender_name;
};

// Node entry for node picker
struct display_node_entry {
    uint32_t node_num;
    const char *label;
    bool favorited;
};

/*
    The main display UI struct. Manages the display driver and UI state.

    non-intuitive params:
        - thread_message_index: the index of the currently selected message in the active thread
        - last_handled_incoming_id: the ID of the last incoming message that was handled
        - thread_node_num: the node number of the peer in the active thread (0 for broadcast)
        - pending: if valid, contains an outgoing message that the UI wants to send
        - thread_snapshot: a snapshot of the messages in the currently active thread, used for rendering the thread screen without needing to access the message history while rendering
        - node_snapshot: a snapshot of the nodes in the node history, used for rendering the node picker screen without needing to access the node history while rendering
        - node_snapshot_labels: the labels for the nodes in the node snapshot
        - node_snapshot_last_heard: the last heard timestamp for each node in the node snapshot, used for rendering the "last heard" info in the node picker
*/
typedef struct display_ui_t {
    display_driver_t driver;
    struct messageHistory *message_history;
    struct nodeHistory *node_history;
    bool initialized;
    
    // UI State
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
    
    // Mutex for thread safety
    struct k_mutex ui_mutex;
    
    // Snapshot buffers
    struct message thread_snapshot[32];
    size_t thread_snapshot_count;
    struct display_node_entry node_snapshot[32];
    char node_snapshot_labels[32][40];
    uint32_t node_snapshot_last_heard[32];
    size_t node_snapshot_count;

    // On screen keyboard
    on_screen_keyb_t keyboard;
    bool keyboard_active;
    char compose_buffer[64];
    int compose_buffer_pos;
} display_ui_t;

// Initialization
int display_ui_init(display_ui_t *ui, const display_hal_config_t *hal_config,
                    struct messageHistory *msg_hist, struct nodeHistory *node_hist);
int display_ui_deinit(display_ui_t *ui);

// Screen rendering
int display_ui_show_boot(display_ui_t *ui);
int display_ui_show_inbox(display_ui_t *ui);
int display_ui_show_thread(display_ui_t *ui, int32_t peer_node, bool broadcast);
int display_ui_show_compose(display_ui_t *ui, int32_t target_node, bool broadcast);
int display_ui_show_node_picker(display_ui_t *ui);
int display_ui_show_popup(display_ui_t *ui, const char *title, const char *message);
int display_ui_refresh(display_ui_t *ui);
int display_ui_test_pattern(display_ui_t *ui);

// Action handling
int display_ui_handle_action(display_ui_t *ui, enum screen_ui_action action);
bool display_ui_take_outgoing(display_ui_t *ui, struct screen_ui_outgoing *outgoing);
void display_ui_notify_new_message(display_ui_t *ui, const struct message *msg);

// Keyboard handling
int display_ui_show_keyboard(display_ui_t *ui);
char display_ui_get_selected_key(display_ui_t *ui);
void display_ui_keyboard_navigate(display_ui_t *ui, int direction);
void display_ui_keyboard_select(display_ui_t *ui);

#endif