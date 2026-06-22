#ifndef UI_BUTTON_HANDLER_H
#define UI_BUTTON_HANDLER_H

#include "hardware/button.h"
#include "ui/display_ui.h"

#define NUM_BUTTONS 4

/*
    This module manages the hardware buttons and translates them into UI actions.
    params:
        - buttons: Array of button_t structs representing the hardware buttons
        - display_ui: Pointer to the display_ui_t instance for interacting with the UI
        - initialized: Flag to indicate if the button handler has been initialized
*/
typedef struct {
    button_t buttons[NUM_BUTTONS];
    display_ui_t *display_ui;
    bool initialized;
} ui_button_context_t;

int ui_button_handler_init(ui_button_context_t *ctx, display_ui_t *display_ui);
int ui_button_handler_start(void);
void ui_button_handler_process(ui_button_context_t *ctx);

#endif