#ifndef WEACT_EPD154_H
#define WEACT_EPD154_H

#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>

#include "models/message.h"
#include "models/node.h"

#define WEACT_EPD154_THREAD_VISIBLE 3

struct weact_epd154_thread_entry {
    const char *text;
    bool is_outgoing;
};

int weact_epd154_init(void);
int weact_epd154_show_boot_pattern(void);
int weact_epd154_show_message_screen(struct messageHistory *message_history,
                                     const struct nodeHistory *node_history);
int weact_epd154_show_received_message(struct messageHistory *message_history,
                                       const struct nodeHistory *node_history,
                                       bool show_popup);
int weact_epd154_record_received_message(void);
int weact_epd154_show_compose_screen(const char *target_label,
                                     const char *draft_text,
                                     bool broadcast_mode);
int weact_epd154_show_thread_screen(const char *target_label,
                                    const struct weact_epd154_thread_entry *entries,
                                    size_t entry_count,
                                    size_t selected_visible_index,
                                    size_t global_index,
                                    size_t total);
bool weact_epd154_get_selected_message(struct messageHistory *message_history,
                                       const struct nodeHistory *node_history,
                                       int32_t *msg_id, int32_t *from_num, int32_t *to_num);
int weact_epd154_next_message(struct messageHistory *message_history,
                              const struct nodeHistory *node_history);
int weact_epd154_previous_message(struct messageHistory *message_history,
                                  const struct nodeHistory *node_history);
int weact_epd154_sleep(void);

#endif
