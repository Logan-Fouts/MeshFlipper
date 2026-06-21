#ifndef UI_BUTTON_HANDLER_H
#define UI_BUTTON_HANDLER_H

#include "hardware/button.h"
#include "models/mesh_node.h"
#include "models/mesh_message.h"

typedef struct {
    button_t buttons[4];  /* Prev, Primary, Next, Secondary */
    struct messageHistory *message_history;
    struct nodeHistory *node_list;
} ui_button_context_t;

int ui_button_handler_init(ui_button_context_t *ctx);
void ui_button_handler_process(ui_button_context_t *ctx);

#endif