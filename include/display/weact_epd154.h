#ifndef WEACT_EPD154_H
#define WEACT_EPD154_H

#include <stdint.h>

int weact_epd154_init(void);
int weact_epd154_show_boot_pattern(void);
int weact_epd154_show_message_screen(const char *message);
int weact_epd154_show_received_message(int32_t msg_id, const char *message, const char *from_name);
int weact_epd154_sleep(void);

#endif
