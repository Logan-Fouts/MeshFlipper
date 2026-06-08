#ifndef WEACT_EPD154_H
#define WEACT_EPD154_H

int weact_epd154_init(void);
int weact_epd154_show_boot_pattern(void);
int weact_epd154_show_message_screen(const char *message);
int weact_epd154_sleep(void);

#endif
