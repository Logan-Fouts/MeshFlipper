#include "ui/on_screen_keyb.h"

#include <zephyr/kernel.h>
#include <stdlib.h>

int init_on_screen_keyb(struct on_screen_keyb_t *keyb) {
    char tmp_keyboard[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ.";
    int tmp_num_keys = sizeof(tmp_keyboard) / sizeof(tmp_keyboard[0]);

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