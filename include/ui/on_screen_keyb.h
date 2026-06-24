#ifndef ON_SCREEN_KEYB_H
#define ON_SCREEN_KEYB_H

#include <zephyr/kernel.h>

typedef struct on_screen_keyb_t {
    int num_keys;
    char *keyboard; // Pointer to the keyboard array
    int current_key_ix; // Index of the currently selected key
} on_screen_keyb_t;

int init_on_screen_keyb(struct on_screen_keyb_t *keyb);

#endif