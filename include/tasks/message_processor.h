#ifndef MESSAGE_PROCESSOR_H
#define MESSAGE_PROCESSOR_H

#include <stdint.h>
#include <stdbool.h>
#include "models/ring_buffer.h"

// Forward declarations
struct messageHistory;
struct nodeHistory;
struct display_ui_t;  // Keep as struct forward declaration

// Initialize the message processor
int message_processor_init(ring_buffer_t *rx_queue,
                          struct messageHistory *message_history,
                          struct nodeHistory *node_list);

void message_processor_set_display_ui(struct display_ui_t *ui);

int message_processor_start(void);

void message_processor_get_stats(uint32_t *processed_count, uint32_t *error_count);

bool message_processor_wait_for_my_node_info(int timeout_ms);

#endif