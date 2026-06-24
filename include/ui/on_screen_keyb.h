#ifndef ON_SCREEN_KEYB_H
#define ON_SCREEN_KEYB_H

#include <zephyr/kernel.h>

typedef struct on_screen_keyb_t {
    int num_keys;
    char *keyboard; // Pointer to the keyboard array
    int current_key_ix; // Index of the currently selected key
} on_screen_keyb_t;

int init_on_screen_keyb(struct on_screen_keyb_t *keyb);
int increment_keyb_ix(struct on_screen_keyb_t *keyb, int direction);
char get_current_key(struct on_screen_keyb_t *keyb);

// Forward declaration of display_ui_t
struct display_ui_t;
void render_on_screen_keyboard(struct on_screen_keyb_t *keyb, struct display_ui_t *ui);

#endif