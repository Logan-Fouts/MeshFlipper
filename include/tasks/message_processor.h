#ifndef MESSAGE_PROCESSOR_H
#define MESSAGE_PROCESSOR_H

#include <stdint.h>
#include <stdbool.h>
#include "models/ring_buffer.h"

// Forward declarations
struct messageHistory;
struct nodeHistory;

// Initialize the message processor
int message_processor_init(ring_buffer_t *rx_queue,
                          struct messageHistory *message_history,
                          struct nodeHistory *node_list);

// Start the message processing thread
int message_processor_start(void);

// Get statistics
void message_processor_get_stats(uint32_t *processed_count, uint32_t *error_count);

// Wait for my node info (blocking)
bool message_processor_wait_for_my_node_info(int timeout_ms);

#endif // MESSAGE_PROCESSOR_H