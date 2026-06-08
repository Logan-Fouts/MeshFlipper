#ifndef SCREEN_UI_H
#define SCREEN_UI_H

#include <stdbool.h>
#include <stdint.h>

#include "models/message.h"
#include "models/node.h"

enum screen_ui_action {
    SCREEN_UI_ACTION_PREVIOUS = 0,
    SCREEN_UI_ACTION_NEXT,
    SCREEN_UI_ACTION_PRIMARY,
    SCREEN_UI_ACTION_SECONDARY,
    SCREEN_UI_ACTION_HOME,
};

struct screen_ui_outgoing {
    bool valid;
    int32_t target_node;
    char text[96];
};

int screen_ui_refresh(struct messageHistory *message_history, struct nodeHistory *node_history);
int screen_ui_handle_action(struct messageHistory *message_history,
                            struct nodeHistory *node_history,
                            enum screen_ui_action action);
bool screen_ui_take_outgoing(struct screen_ui_outgoing *outgoing);

#endif
