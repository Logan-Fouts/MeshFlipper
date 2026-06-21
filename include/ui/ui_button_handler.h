#ifndef UI_BUTTON_HANDLER_H
#define UI_BUTTON_HANDLER_H

#include "hardware/button.h"
#include "ui/display_ui.h"

typedef struct {
    button_t buttons[4];
    display_ui_t *display_ui;
    bool initialized;
} ui_button_context_t;

int ui_button_handler_init(ui_button_context_t *ctx, display_ui_t *display_ui);
void ui_button_handler_process(ui_button_context_t *ctx);

#endif