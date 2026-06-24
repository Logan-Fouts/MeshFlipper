#include "ui/on_screen_keyb.h"
#include "ui/display_ui.h"
#include <zephyr/kernel.h>
#include <stdlib.h>
#include <string.h>

int init_on_screen_keyb(struct on_screen_keyb_t *keyb) {
    char tmp_keyboard[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ. ";
    int tmp_num_keys = sizeof(tmp_keyboard) - 1; // -1 for null terminator

    // Allocate memory for the keyboard array
    keyb->keyboard = malloc(tmp_num_keys * sizeof(char));
    if (keyb->keyboard == NULL) {
        printk("Failed to allocate memory for keyboard\n");
        return -1;
    }

    // Copy the contents of tmp_keyboard into keyb->keyboard
    memcpy(keyb->keyboard, tmp_keyboard, tmp_num_keys * sizeof(char));

    keyb->num_keys = tmp_num_keys;
    keyb->current_key_ix = tmp_num_keys / 2; // Start in middle of the keyboard

    return 0;
}

int increment_keyb_ix(struct on_screen_keyb_t *keyb, int direction) {
    // Pos direction is right, neg direction is left
    // Wrap around using modulus
    keyb->current_key_ix = (keyb->current_key_ix + direction + keyb->num_keys) % keyb->num_keys;
    return 0;
}

char get_current_key(struct on_screen_keyb_t *keyb) {
    if (!keyb || !keyb->keyboard) return '\0';
    return keyb->keyboard[keyb->current_key_ix];
}

void render_on_screen_keyboard(struct on_screen_keyb_t *keyb, struct display_ui_t *ui) {
    if (!keyb || !ui || !ui->initialized) return;
    
    // Calculate the visible range: 2 left, current, 2 right
    int visible_keys[5];
    int visible_indices[5];
    
    for (int i = 0; i < 5; i++) {
        int idx = (keyb->current_key_ix - 2 + i + keyb->num_keys) % keyb->num_keys;
        visible_indices[i] = idx;
        visible_keys[i] = keyb->keyboard[idx];
    }
    
    // Draw keyboard background
    int width = ui->driver.hal_config.width;
    int height = ui->driver.hal_config.height;
    
    // Draw a box for the keyboard
    int box_x = 20;
    int box_y = height - 80;
    int box_w = width - 40;
    int box_h = 60;
    
    // Clear the keyboard area first
    display_driver_draw_filled_rect(&ui->driver, box_x, box_y, box_w, box_h, false);
    display_driver_draw_rect(&ui->driver, box_x, box_y, box_w, box_h, true);
    
    // Draw the keys horizontally
    int key_width = 36;
    int key_height = 36;
    int spacing = 6;
    int total_keys_width = (5 * key_width) + (4 * spacing);
    int start_x = (width - total_keys_width) / 2;
    int start_y = box_y + (box_h - key_height) / 2;
    
    for (int i = 0; i < 5; i++) {
        int x = start_x + i * (key_width + spacing);
        int y = start_y;
        
        // Highlight the center key (current selection)
        bool is_selected = (i == 2); // Center key
        
        if (is_selected) {
            // Draw thick border for selected key
            display_driver_draw_rect(&ui->driver, x - 2, y - 2, key_width + 4, key_height + 4, true);
            display_driver_draw_filled_rect(&ui->driver, x, y, key_width, key_height, false);
            display_driver_draw_rect(&ui->driver, x, y, key_width, key_height, true);
        } else {
            display_driver_draw_filled_rect(&ui->driver, x, y, key_width, key_height, false);
            display_driver_draw_rect(&ui->driver, x, y, key_width, key_height, true);
        }
        
        // Draw the character
        char key_char[2] = {visible_keys[i], '\0'};
        int char_x = x + (key_width / 2) - 3;
        int char_y = y + (key_height / 2) - 4;
        display_driver_draw_text(&ui->driver, char_x, char_y, 1, is_selected ? false : true, key_char);
    }
    
    // Draw the index indicators below the keyboard
    char idx_text[32];
    snprintf(idx_text, sizeof(idx_text), "Key %d/%d", keyb->current_key_ix + 1, keyb->num_keys);
    display_driver_draw_text_centered(&ui->driver, box_y + box_h + 8, 1, false, idx_text);
    
    // Draw help text
    char help_text[] = "PREV/NEXT: Navigate  PRIMARY: Select  SECONDARY: Exit";
    display_driver_draw_text_centered(&ui->driver, box_y + box_h + 22, 1, false, help_text);
}