#include "ui/display_ui.h"
#include "ui/display_ui_internal.h"
#include <string.h>
#include <stdio.h>

#define UI_LOCK(ui) k_mutex_lock(&(ui)->ui_mutex, K_FOREVER)
#define UI_UNLOCK(ui) k_mutex_unlock(&(ui)->ui_mutex)

// Render keyboard with binary search
void render_keyboard_with_search(display_ui_t *ui)
{
    if (!ui || !ui->keyboard_active) return;
    
    on_screen_keyb_t *keyb = &ui->keyboard;
    int width = ui->driver.hal_config.width;
    int height = ui->driver.hal_config.height;
    
    // Calculate visible keys
    int visible_keys[5];
    for (int i = 0; i < 5; i++) {
        int idx = (keyb->current_key_ix - 2 + i + keyb->num_keys) % keyb->num_keys;
        visible_keys[i] = keyb->keyboard[idx];
    }
    
    // Keyboard box
    int box_x = 20;
    int box_y = height - 80;
    int box_w = width - 40;
    int box_h = 60;
    
    // Clear keyboard area
    display_driver_draw_filled_rect(&ui->driver, box_x, box_y, box_w, box_h, false);
    display_driver_draw_rect(&ui->driver, box_x, box_y, box_w, box_h, true);
    
    // Draw keys
    int key_width = 36;
    int key_height = 36;
    int spacing = 6;
    int total_keys_width = (5 * key_width) + (4 * spacing);
    int start_x = (width - total_keys_width) / 2;
    int start_y = box_y + (box_h - key_height) / 2;
    
    for (int i = 0; i < 5; i++) {
        int x = start_x + i * (key_width + spacing);
        int y = start_y;
        bool is_selected = (i == 2);
        
        if (is_selected) {
            display_driver_draw_rect(&ui->driver, x - 2, y - 2, key_width + 4, key_height + 4, true);
            display_driver_draw_filled_rect(&ui->driver, x, y, key_width, key_height, false);
            display_driver_draw_rect(&ui->driver, x, y, key_width, key_height, true);
        } else {
            display_driver_draw_filled_rect(&ui->driver, x, y, key_width, key_height, false);
            display_driver_draw_rect(&ui->driver, x, y, key_width, key_height, true);
        }
        
        char key_char[2] = {visible_keys[i], '\0'};
        int char_x = x + (key_width / 2) - 3;
        int char_y = y + (key_height / 2) - 4;
        display_driver_draw_text(&ui->driver, char_x, char_y, 1, is_selected ? false : true, key_char);
    }
    
    // Show search info with mode indicator
    char info_text[64];
    int range_size = keyboard_search_high - keyboard_search_low + 1;
    
    if (range_size <= 3) {
        // Free movement mode
        snprintf(info_text, sizeof(info_text), "FREE MOVE: %d-%d  Key: %d/%d",
                 keyboard_search_low, keyboard_search_high,
                 keyb->current_key_ix + 1, keyb->num_keys);
    } else {
        // Binary search mode
        snprintf(info_text, sizeof(info_text), "BINARY: %d-%d (size: %d)  Key: %d/%d",
                 keyboard_search_low, keyboard_search_high, range_size,
                 keyb->current_key_ix + 1, keyb->num_keys);
    }
    display_driver_draw_text_centered(&ui->driver, box_y + box_h + 8, 1, false, info_text);
    display_driver_draw_text_centered(&ui->driver, box_y + box_h + 22, 1, false, 
                                     "PREV/NEXT: Navigate  PRIMARY: Select  SECONDARY: Exit");
}

// Show keyboard overlay
int display_ui_show_keyboard(display_ui_t *ui)
{
    if (!ui || !ui->initialized) return -EINVAL;
    
    UI_LOCK(ui);
    ui->keyboard_active = true;
    
    // Initialize binary search - full range
    keyboard_search_low = 0;
    keyboard_search_high = ui->keyboard.num_keys - 1;
    keyboard_search_mode = true;
    
    // Start at middle of keyboard
    ui->keyboard.current_key_ix = ui->keyboard.num_keys / 2;
    
    // Render the current screen background once
    int ret;
    if (ui->thread_active) {
        if (ui->thread_broadcast) {
            rebuild_broadcast_thread_snapshot(ui);
        } else {
            rebuild_thread_snapshot(ui, (uint32_t)ui->thread_node_num);
        }
        ret = render_thread_screen(ui);
    } else if (ui->node_picker_active) {
        ret = render_node_picker(ui);
    } else if (ui->compose_active) {
        render_compose(ui);
        ret = 0;
    } else {
        ret = render_inbox(ui);
    }
    
    // Render keyboard on top
    render_keyboard_with_search(ui);
    
    // Refresh to show keyboard
    ret = display_driver_refresh(&ui->driver);
    
    UI_UNLOCK(ui);
    return ret;
}

// Get selected key
char display_ui_get_selected_key(display_ui_t *ui)
{
    if (!ui || !ui->keyboard_active) return '\0';
    return get_current_key(&ui->keyboard);
}

// Keyboard navigation with pure binary search
void display_ui_keyboard_navigate(display_ui_t *ui, int direction)
{
    if (!ui || !ui->keyboard_active) return;
    
    on_screen_keyb_t *keyb = &ui->keyboard;
    
    int mid = (keyboard_search_low + keyboard_search_high) / 2;
    
    if (direction > 0) {
        keyboard_search_low = mid + 1;
        keyb->current_key_ix = (keyboard_search_low + keyboard_search_high) / 2;
    } else {
        keyboard_search_high = mid - 1;
        keyb->current_key_ix = (keyboard_search_low + keyboard_search_high) / 2;
    }
    
    // Safety bounds
    if (keyb->current_key_ix < keyboard_search_low) {
        keyb->current_key_ix = keyboard_search_low;
    }
    if (keyb->current_key_ix > keyboard_search_high) {
        keyb->current_key_ix = keyboard_search_high;
    }
    if (keyb->current_key_ix < 0) keyb->current_key_ix = 0;
    if (keyb->current_key_ix >= keyb->num_keys) keyb->current_key_ix = keyb->num_keys - 1;

    // Update display - FULL REFRESH every time
    render_keyboard_with_search(ui);
    display_driver_refresh(&ui->driver);
}

// Keyboard select - selects current key and resets search to full range
void display_ui_keyboard_select(display_ui_t *ui)
{
    if (!ui || !ui->keyboard_active) return;
    
    char key = get_current_key(&ui->keyboard);
    
    // Add key to buffer
    if (key == ' ') {
        if (ui->compose_buffer_pos < sizeof(ui->compose_buffer) - 1) {
            ui->compose_buffer[ui->compose_buffer_pos++] = ' ';
            ui->compose_buffer[ui->compose_buffer_pos] = '\0';
        }
    } else if (key == '.') {
        if (ui->compose_buffer_pos < sizeof(ui->compose_buffer) - 1) {
            ui->compose_buffer[ui->compose_buffer_pos++] = '.';
            ui->compose_buffer[ui->compose_buffer_pos] = '\0';
        }
    } else {
        if (ui->compose_buffer_pos < sizeof(ui->compose_buffer) - 1) {
            ui->compose_buffer[ui->compose_buffer_pos++] = key;
            ui->compose_buffer[ui->compose_buffer_pos] = '\0';
        }
    }
    
    // Reset binary search to full range
    keyboard_search_low = 0;
    keyboard_search_high = ui->keyboard.num_keys - 1;
    keyboard_search_mode = true;
    ui->keyboard.current_key_ix = ui->keyboard.num_keys / 2;
    
    // Update compose screen and keyboard
    if (ui->compose_active) {
        render_compose(ui);
        render_keyboard_with_search(ui);
        display_driver_refresh(&ui->driver);
    }
}