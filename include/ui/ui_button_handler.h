#ifndef UI_BUTTON_HANDLER_H
#define UI_BUTTON_HANDLER_H

#include "hardware/button.h"
#include "ui/display_ui.h"

#define NUM_BUTTONS 5

/*
    Defines the context for the UI button handler, including button states and a reference to the display UI for handling actions.
    params:
        - buttons: Array of button_t structures representing each button and its state
        - display_ui: Pointer to the display_ui_t instance for invoking UI actions based on button presses
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