#ifndef DISPLAY_DRIVER_H
#define DISPLAY_DRIVER_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "hardware/display_hal.h"

typedef struct {
    display_hal_config_t hal_config;
    uint8_t *framebuffer;
    size_t framebuffer_size;
    bool initialized;
} display_driver_t;

int display_driver_init(display_driver_t *drv, const display_hal_config_t *hal_config);
int display_driver_deinit(display_driver_t *drv);
int display_driver_clear(display_driver_t *drv);
int display_driver_set_pixel(display_driver_t *drv, int x, int y, bool black);
int display_driver_draw_hline(display_driver_t *drv, int x, int y, int w, bool black);
int display_driver_draw_vline(display_driver_t *drv, int x, int y, int h, bool black);
int display_driver_draw_rect(display_driver_t *drv, int x, int y, int w, int h, bool black);
int display_driver_draw_filled_rect(display_driver_t *drv, int x, int y, int w, int h, bool black);
int display_driver_draw_char(display_driver_t *drv, int x, int y, char ch, int scale, bool bold);
int display_driver_draw_text(display_driver_t *drv, int x, int y, int scale, bool bold, const char *text);
int display_driver_draw_text_centered(display_driver_t *drv, int y, int scale, bool bold, const char *text);
int display_driver_draw_text_limited(display_driver_t *drv, int x, int y, int scale, bool bold, 
                                     const char *text, size_t max_chars);
int display_driver_refresh(display_driver_t *drv);
int display_driver_sleep(display_driver_t *drv);

#endif