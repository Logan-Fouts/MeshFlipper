#include "ui/display_ui.h"
#include "ui/display_ui_internal.h"
#include <string.h>
#include <stdio.h>

#define UI_LOCK(ui) k_mutex_lock(&(ui)->ui_mutex, K_FOREVER)
#define UI_UNLOCK(ui) k_mutex_unlock(&(ui)->ui_mutex)

// Handle actions
int display_ui_handle_action(display_ui_t *ui, enum screen_ui_action action)
{
    if (!ui || !ui->initialized) {
        printk("UI not initialized!\n");
        return -EINVAL;
    }
    
    if (k_mutex_lock(&ui->ui_mutex, K_MSEC(100)) != 0) {
        printk("UI busy, action %d deferred\n", action);
        return -EBUSY;
    }
    
    int ret = 0;
    
    // Home action
    if (action == SCREEN_UI_ACTION_HOME) {
        ui->compose_active = false;
        ui->thread_active = false;
        ui->thread_broadcast = false;
        ui->node_picker_active = false;
        ui->keyboard_active = false;
        ui->compose_buffer[0] = '\0';
        ui->compose_buffer_pos = 0;
        keyboard_search_mode = false;
        current_display_index = 0;
        ret = display_ui_refresh(ui);
        UI_UNLOCK(ui);
        return ret;
    }

    // Inbox state
    if (!ui->compose_active && !ui->thread_active && !ui->node_picker_active && !ui->keyboard_active) {
        
        if (action == SCREEN_UI_ACTION_PREVIOUS) {
            int inbox_indices[MAX_VISIBLE_MESSAGES];
            int inbox_count = build_inbox_indices(ui, inbox_indices);
            if (inbox_count > 0) {
                int selected_pos = inbox_selected_position(inbox_indices, inbox_count);
                selected_pos = (selected_pos - 1 + inbox_count) % inbox_count;
                current_display_index = inbox_indices[selected_pos];
                ret = render_inbox(ui);
            }
            UI_UNLOCK(ui);
            return ret;
        }

        if (action == SCREEN_UI_ACTION_NEXT) {
            int inbox_indices[MAX_VISIBLE_MESSAGES];
            int inbox_count = build_inbox_indices(ui, inbox_indices);
            if (inbox_count > 0) {
                int selected_pos = inbox_selected_position(inbox_indices, inbox_count);
                selected_pos = (selected_pos + 1) % inbox_count;
                current_display_index = inbox_indices[selected_pos];
                ret = render_inbox(ui);
            }
            UI_UNLOCK(ui);
            return ret;
        }

        if (action == SCREEN_UI_ACTION_PRIMARY) {
            if (is_broadcast_compose_selected(ui)) {
                ui->thread_active = true;
                ui->thread_broadcast = true;
                ui->thread_message_index = 0;
                rebuild_broadcast_thread_snapshot(ui);
                if (ui->thread_snapshot_count > 0) {
                    ui->thread_message_index = ui->thread_snapshot_count - 1;
                }
                ret = render_thread_screen(ui);
                UI_UNLOCK(ui);
                return ret;
            }

            int32_t thread_node = 0;
            if (resolve_thread_target(ui, &thread_node)) {
                ret = display_ui_show_thread(ui, thread_node, false);
                UI_UNLOCK(ui);
                return ret;
            }
            UI_UNLOCK(ui);
            return 0;
        }

        if (action == SCREEN_UI_ACTION_SECONDARY) {
            ret = display_ui_show_node_picker(ui);
            UI_UNLOCK(ui);
            return ret;
        }
        UI_UNLOCK(ui);
        return 0;
    }

    // Node picker state
    if (ui->node_picker_active && !ui->compose_active && !ui->thread_active) {
        
        if (action == SCREEN_UI_ACTION_PREVIOUS || action == SCREEN_UI_ACTION_NEXT) {
            rebuild_node_picker_snapshot(ui);
            if (ui->node_snapshot_count == 0) {
                ret = render_node_picker(ui);
                UI_UNLOCK(ui);
                return ret;
            }
            
            if (action == SCREEN_UI_ACTION_PREVIOUS) {
                if (ui->node_picker_index == 0) {
                    ui->node_picker_index = ui->node_snapshot_count - 1;
                } else {
                    ui->node_picker_index--;
                }
            } else {
                ui->node_picker_index = (ui->node_picker_index + 1) % ui->node_snapshot_count;
            }
            ret = render_node_picker(ui);
            UI_UNLOCK(ui);
            return ret;
        }
        
        if (action == SCREEN_UI_ACTION_PRIMARY) {
            rebuild_node_picker_snapshot(ui);
            if (ui->node_snapshot_count == 0) {
                ret = render_node_picker(ui);
                UI_UNLOCK(ui);
                return ret;
            }
            
            ui->node_picker_active = false;
            ret = display_ui_show_compose(ui, (int32_t)ui->node_snapshot[ui->node_picker_index].node_num, false);
            UI_UNLOCK(ui);
            return ret;
        }
        
        if (action == SCREEN_UI_ACTION_SECONDARY) {
            ui->node_picker_active = false;
            current_display_index = 0;
            ret = display_ui_refresh(ui);
            UI_UNLOCK(ui);
            return ret;
        }
        UI_UNLOCK(ui);
        return 0;
    }

    // Thread state
    if (ui->thread_active && !ui->compose_active) {
        
        if (action == SCREEN_UI_ACTION_PREVIOUS || action == SCREEN_UI_ACTION_NEXT) {
            if (ui->thread_snapshot_count == 0) {
                ret = render_thread_screen(ui);
                UI_UNLOCK(ui);
                return ret;
            }
            
            if (action == SCREEN_UI_ACTION_PREVIOUS) {
                ui->thread_message_index = (ui->thread_message_index == 0) ? 
                    ui->thread_snapshot_count - 1 : ui->thread_message_index - 1;
            } else {
                ui->thread_message_index = (ui->thread_message_index + 1) % ui->thread_snapshot_count;
            }
            ret = render_thread_screen(ui);
            UI_UNLOCK(ui);
            return ret;
        }
        
        if (action == SCREEN_UI_ACTION_PRIMARY) {
            ret = display_ui_show_compose(ui, ui->thread_node_num, ui->thread_broadcast);
            UI_UNLOCK(ui);
            return ret;
        }
        
        if (action == SCREEN_UI_ACTION_SECONDARY) {
            ui->thread_active = false;
            ui->thread_broadcast = false;
            current_display_index = 0;
            ret = display_ui_refresh(ui);
            UI_UNLOCK(ui);
            return ret;
        }
        UI_UNLOCK(ui);
        return 0;
    }

    // Compose state
    if (ui->compose_active) {
        
        // If keyboard is active, handle keyboard navigation
        if (ui->keyboard_active) {
            if (action == SCREEN_UI_ACTION_PREVIOUS) {
                display_ui_keyboard_navigate(ui, -1);
                UI_UNLOCK(ui);
                return 0;
            }
            if (action == SCREEN_UI_ACTION_NEXT) {
                display_ui_keyboard_navigate(ui, 1);
                UI_UNLOCK(ui);
                return 0;
            }
            if (action == SCREEN_UI_ACTION_PRIMARY) {
                display_ui_keyboard_select(ui);
                UI_UNLOCK(ui);
                return 0;
            }
            if (action == SCREEN_UI_ACTION_SECONDARY) {
                ui->keyboard_active = false;
                keyboard_search_mode = false;
                render_compose(ui);
                display_driver_refresh(&ui->driver);
                UI_UNLOCK(ui);
                return 0;
            }
            if (action == SCREEN_UI_ACTION_EXT) {
                if (ui->compose_buffer_pos > 0) {
                    ui->compose_buffer_pos -= 1;
                }
                render_compose(ui);
                UI_UNLOCK(ui);
            }
            UI_UNLOCK(ui);
            return 0;
        }
        
        // Regular compose actions
        if (action == SCREEN_UI_ACTION_PREVIOUS) {
            if (ui->compose_buffer_pos > 0) {
                ui->compose_buffer_pos--;
                ui->compose_buffer[ui->compose_buffer_pos] = '\0';
                render_compose(ui);
                display_driver_refresh(&ui->driver);
            } else {
                ui->quick_reply_index = (ui->quick_reply_index == 0) ? 
                    MAX_QUICK_REPLIES - 1 : ui->quick_reply_index - 1;
                render_compose(ui);
                display_driver_refresh(&ui->driver);
            }
            UI_UNLOCK(ui);
            return 0;
        }
        
        if (action == SCREEN_UI_ACTION_NEXT) {
            // Show keyboard on NEXT press in compose
            display_ui_show_keyboard(ui);
            UI_UNLOCK(ui);
            return 0;
        }
        
        if (action == SCREEN_UI_ACTION_SECONDARY) {
            if (ui->compose_buffer_pos > 0) {
                ui->compose_buffer[0] = '\0';
                ui->compose_buffer_pos = 0;
                render_compose(ui);
                display_driver_refresh(&ui->driver);
            } else {
                ui->compose_broadcast = !ui->compose_broadcast;
                render_compose(ui);
                display_driver_refresh(&ui->driver);
            }
            UI_UNLOCK(ui);
            return 0;
        }
        
        if (action == SCREEN_UI_ACTION_PRIMARY) {
            const char *message_text;
            if (ui->compose_buffer_pos > 0) {
                message_text = ui->compose_buffer;
            } else {
                message_text = quick_replies[ui->quick_reply_index];
            }
            
            int32_t target = ui->compose_broadcast ? 0 : ui->selected_target_node;
            
            ui->pending.valid = true;
            ui->pending.target_node = target;
            strncpy(ui->pending.text, message_text, sizeof(ui->pending.text) - 1);
            ui->pending.text[sizeof(ui->pending.text) - 1] = '\0';
            
            ui_add_sent_message(ui, message_text, target);
            ui->compose_active = false;
            ui->compose_buffer[0] = '\0';
            ui->compose_buffer_pos = 0;
            ui->keyboard_active = false;
            keyboard_search_mode = false;
            ret = display_ui_refresh(ui);
            UI_UNLOCK(ui);
            return ret;
        }
        
        UI_UNLOCK(ui);
        return 0;
    }

    UI_UNLOCK(ui);
    return 0;
}

// Take outgoing message from ui pending slot
bool display_ui_take_outgoing(display_ui_t *ui, struct screen_ui_outgoing *outgoing)
{
    if (ui == NULL || outgoing == NULL || !ui->pending.valid) return false;
    
    UI_LOCK(ui);
    *outgoing = ui->pending;
    ui->pending.valid = false;
    UI_UNLOCK(ui);
    return true;
}

// Notify UI about new message
void display_ui_notify_new_message(display_ui_t *ui, const struct message *msg)
{
    if (!ui || !ui->initialized || !msg) return;
    
    if (k_mutex_lock(&ui->ui_mutex, K_MSEC(100)) != 0) {
        printk("UI busy, skipping notification\n");
        return;
    }
    
    bool is_broadcast = is_broadcast_message(msg);
    uint32_t my_node = ui->node_history->my_info.num;
    bool is_from_me = (msg->from == (int32_t)my_node);
    
    if (is_from_me) {
        UI_UNLOCK(ui);
        return;
    }
    
    int32_t peer_node = msg->from;
    if (is_broadcast) {
        peer_node = 0;
    }
    
    bool in_this_thread = false;
    if (ui->thread_active) {
        if (is_broadcast && ui->thread_broadcast) {
            in_this_thread = true;
        } else if (!is_broadcast && !ui->thread_broadcast && ui->thread_node_num == peer_node) {
            in_this_thread = true;
        }
    }
    
    if (in_this_thread) {
        ui->last_handled_incoming_id = msg->id;
        if (ui->thread_broadcast) {
            rebuild_broadcast_thread_snapshot(ui);
        } else {
            rebuild_thread_snapshot(ui, (uint32_t)ui->thread_node_num);
        }
        if (ui->thread_snapshot_count > 0) {
            ui->thread_message_index = ui->thread_snapshot_count - 1;
        }
        display_ui_refresh(ui);
        UI_UNLOCK(ui);
        return;
    }
    
    if (!ui->popup_active) {
        ui->popup_active = true;
        
        int ret = display_ui_refresh(ui);
        if (ret < 0) {
            // Handle error if needed
        }
        
        draw_message_popup(ui, msg);
        
        ret = display_driver_refresh(&ui->driver);
        if (ret < 0) {
            // Handle error if needed
        }
        
        k_sleep(K_MSEC(POPUP_DURATION_MS));
        ui->popup_active = false;
        
        display_ui_refresh(ui);
    }
    
    ui->last_handled_incoming_id = msg->id;
    UI_UNLOCK(ui);
}