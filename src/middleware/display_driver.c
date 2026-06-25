#include "middleware/display_driver.h"
#include <string.h>

// TODO: Add support for more graphics

static uint8_t framebuffer_static[EPD_BUF_SIZE];

/* Font glyph table - 5x7 pixel font */
static bool get_glyph(char ch, uint8_t glyph[5])
{
    memset(glyph, 0, 5);
    switch (ch) {
    case 'A': glyph[0] = 0x7E; glyph[1] = 0x11; glyph[2] = 0x11; glyph[3] = 0x11; glyph[4] = 0x7E; return true;
    case 'B': glyph[0] = 0x7F; glyph[1] = 0x49; glyph[2] = 0x49; glyph[3] = 0x49; glyph[4] = 0x36; return true;
    case 'C': glyph[0] = 0x3E; glyph[1] = 0x41; glyph[2] = 0x41; glyph[3] = 0x41; glyph[4] = 0x22; return true;
    case 'D': glyph[0] = 0x7F; glyph[1] = 0x41; glyph[2] = 0x41; glyph[3] = 0x22; glyph[4] = 0x1C; return true;
    case 'E': glyph[0] = 0x7F; glyph[1] = 0x49; glyph[2] = 0x49; glyph[3] = 0x49; glyph[4] = 0x41; return true;
    case 'F': glyph[0] = 0x7F; glyph[1] = 0x09; glyph[2] = 0x09; glyph[3] = 0x09; glyph[4] = 0x01; return true;
    case 'G': glyph[0] = 0x3E; glyph[1] = 0x41; glyph[2] = 0x49; glyph[3] = 0x49; glyph[4] = 0x7A; return true;
    case 'H': glyph[0] = 0x7F; glyph[1] = 0x08; glyph[2] = 0x08; glyph[3] = 0x08; glyph[4] = 0x7F; return true;
    case 'I': glyph[0] = 0x41; glyph[1] = 0x41; glyph[2] = 0x7F; glyph[3] = 0x41; glyph[4] = 0x41; return true;
    case 'J': glyph[0] = 0x20; glyph[1] = 0x40; glyph[2] = 0x41; glyph[3] = 0x3F; glyph[4] = 0x01; return true;
    case 'K': glyph[0] = 0x7F; glyph[1] = 0x08; glyph[2] = 0x14; glyph[3] = 0x22; glyph[4] = 0x41; return true;
    case 'L': glyph[0] = 0x7F; glyph[1] = 0x40; glyph[2] = 0x40; glyph[3] = 0x40; glyph[4] = 0x40; return true;
    case 'M': glyph[0] = 0x7F; glyph[1] = 0x02; glyph[2] = 0x04; glyph[3] = 0x02; glyph[4] = 0x7F; return true;
    case 'N': glyph[0] = 0x7F; glyph[1] = 0x04; glyph[2] = 0x08; glyph[3] = 0x10; glyph[4] = 0x7F; return true;
    case 'O': glyph[0] = 0x3E; glyph[1] = 0x41; glyph[2] = 0x41; glyph[3] = 0x41; glyph[4] = 0x3E; return true;
    case 'P': glyph[0] = 0x7F; glyph[1] = 0x09; glyph[2] = 0x09; glyph[3] = 0x09; glyph[4] = 0x06; return true;
    case 'Q': glyph[0] = 0x3E; glyph[1] = 0x41; glyph[2] = 0x51; glyph[3] = 0x21; glyph[4] = 0x5E; return true;
    case 'R': glyph[0] = 0x7F; glyph[1] = 0x09; glyph[2] = 0x19; glyph[3] = 0x29; glyph[4] = 0x46; return true;
    case 'S': glyph[0] = 0x46; glyph[1] = 0x49; glyph[2] = 0x49; glyph[3] = 0x49; glyph[4] = 0x31; return true;
    case 'T': glyph[0] = 0x01; glyph[1] = 0x01; glyph[2] = 0x7F; glyph[3] = 0x01; glyph[4] = 0x01; return true;
    case 'U': glyph[0] = 0x3F; glyph[1] = 0x40; glyph[2] = 0x40; glyph[3] = 0x40; glyph[4] = 0x3F; return true;
    case 'V': glyph[0] = 0x1F; glyph[1] = 0x20; glyph[2] = 0x40; glyph[3] = 0x20; glyph[4] = 0x1F; return true;
    case 'W': glyph[0] = 0x7F; glyph[1] = 0x20; glyph[2] = 0x18; glyph[3] = 0x20; glyph[4] = 0x7F; return true;
    case 'X': glyph[0] = 0x63; glyph[1] = 0x14; glyph[2] = 0x08; glyph[3] = 0x14; glyph[4] = 0x63; return true;
    case 'Y': glyph[0] = 0x07; glyph[1] = 0x08; glyph[2] = 0x70; glyph[3] = 0x08; glyph[4] = 0x07; return true;
    case 'Z': glyph[0] = 0x61; glyph[1] = 0x51; glyph[2] = 0x49; glyph[3] = 0x45; glyph[4] = 0x43; return true;
    case 'a': glyph[0] = 0x20; glyph[1] = 0x54; glyph[2] = 0x54; glyph[3] = 0x54; glyph[4] = 0x78; return true;
    case 'b': glyph[0] = 0x7F; glyph[1] = 0x44; glyph[2] = 0x44; glyph[3] = 0x44; glyph[4] = 0x38; return true;
    case 'c': glyph[0] = 0x38; glyph[1] = 0x44; glyph[2] = 0x44; glyph[3] = 0x44; glyph[4] = 0x28; return true;
    case 'd': glyph[0] = 0x38; glyph[1] = 0x44; glyph[2] = 0x44; glyph[3] = 0x44; glyph[4] = 0x7F; return true;
    case 'e': glyph[0] = 0x38; glyph[1] = 0x54; glyph[2] = 0x54; glyph[3] = 0x54; glyph[4] = 0x18; return true;
    case 'f': glyph[0] = 0x08; glyph[1] = 0x7E; glyph[2] = 0x09; glyph[3] = 0x01; glyph[4] = 0x02; return true;
    case 'g': glyph[0] = 0x18; glyph[1] = 0xA4; glyph[2] = 0xA4; glyph[3] = 0xA4; glyph[4] = 0x7C; return true;
    case 'h': glyph[0] = 0x7F; glyph[1] = 0x08; glyph[2] = 0x04; glyph[3] = 0x04; glyph[4] = 0x78; return true;
    case 'i': glyph[0] = 0x00; glyph[1] = 0x44; glyph[2] = 0x7D; glyph[3] = 0x40; glyph[4] = 0x00; return true;
    case 'j': glyph[0] = 0x40; glyph[1] = 0x80; glyph[2] = 0x84; glyph[3] = 0x7D; glyph[4] = 0x00; return true;
    case 'k': glyph[0] = 0x7F; glyph[1] = 0x10; glyph[2] = 0x28; glyph[3] = 0x44; glyph[4] = 0x00; return true;
    case 'l': glyph[0] = 0x41; glyph[1] = 0x41; glyph[2] = 0x7F; glyph[3] = 0x40; glyph[4] = 0x00; return true;
    case 'm': glyph[0] = 0x7C; glyph[1] = 0x04; glyph[2] = 0x7C; glyph[3] = 0x04; glyph[4] = 0x78; return true;
    case 'n': glyph[0] = 0x7C; glyph[1] = 0x04; glyph[2] = 0x04; glyph[3] = 0x04; glyph[4] = 0x78; return true;
    case 'o': glyph[0] = 0x38; glyph[1] = 0x44; glyph[2] = 0x44; glyph[3] = 0x44; glyph[4] = 0x38; return true;
    case 'p': glyph[0] = 0xFC; glyph[1] = 0x24; glyph[2] = 0x24; glyph[3] = 0x24; glyph[4] = 0x18; return true;
    case 'q': glyph[0] = 0x18; glyph[1] = 0x24; glyph[2] = 0x24; glyph[3] = 0x24; glyph[4] = 0xFC; return true;
    case 'r': glyph[0] = 0x7C; glyph[1] = 0x08; glyph[2] = 0x04; glyph[3] = 0x04; glyph[4] = 0x08; return true;
    case 's': glyph[0] = 0x48; glyph[1] = 0x54; glyph[2] = 0x54; glyph[3] = 0x54; glyph[4] = 0x24; return true;
    case 't': glyph[0] = 0x04; glyph[1] = 0x3F; glyph[2] = 0x44; glyph[3] = 0x44; glyph[4] = 0x20; return true;
    case 'u': glyph[0] = 0x3C; glyph[1] = 0x40; glyph[2] = 0x40; glyph[3] = 0x20; glyph[4] = 0x7C; return true;
    case 'v': glyph[0] = 0x1C; glyph[1] = 0x20; glyph[2] = 0x40; glyph[3] = 0x20; glyph[4] = 0x1C; return true;
    case 'w': glyph[0] = 0x7C; glyph[1] = 0x20; glyph[2] = 0x18; glyph[3] = 0x20; glyph[4] = 0x7C; return true;
    case 'x': glyph[0] = 0x44; glyph[1] = 0x28; glyph[2] = 0x10; glyph[3] = 0x28; glyph[4] = 0x44; return true;
    case 'y': glyph[0] = 0x1C; glyph[1] = 0xA0; glyph[2] = 0xA0; glyph[3] = 0xA0; glyph[4] = 0x7C; return true;
    case 'z': glyph[0] = 0x44; glyph[1] = 0x64; glyph[2] = 0x54; glyph[3] = 0x4C; glyph[4] = 0x44; return true;
    case '0': glyph[0] = 0x3E; glyph[1] = 0x45; glyph[2] = 0x49; glyph[3] = 0x51; glyph[4] = 0x3E; return true;
    case '1': glyph[0] = 0x00; glyph[1] = 0x21; glyph[2] = 0x7F; glyph[3] = 0x40; glyph[4] = 0x00; return true;
    case '2': glyph[0] = 0x42; glyph[1] = 0x61; glyph[2] = 0x51; glyph[3] = 0x49; glyph[4] = 0x46; return true;
    case '3': glyph[0] = 0x42; glyph[1] = 0x41; glyph[2] = 0x51; glyph[3] = 0x69; glyph[4] = 0x46; return true;
    case '4': glyph[0] = 0x18; glyph[1] = 0x14; glyph[2] = 0x12; glyph[3] = 0x7F; glyph[4] = 0x10; return true;
    case '5': glyph[0] = 0x27; glyph[1] = 0x45; glyph[2] = 0x45; glyph[3] = 0x45; glyph[4] = 0x39; return true;
    case '6': glyph[0] = 0x3C; glyph[1] = 0x4A; glyph[2] = 0x49; glyph[3] = 0x49; glyph[4] = 0x30; return true;
    case '7': glyph[0] = 0x01; glyph[1] = 0x71; glyph[2] = 0x09; glyph[3] = 0x05; glyph[4] = 0x03; return true;
    case '8': glyph[0] = 0x36; glyph[1] = 0x49; glyph[2] = 0x49; glyph[3] = 0x49; glyph[4] = 0x36; return true;
    case '9': glyph[0] = 0x06; glyph[1] = 0x49; glyph[2] = 0x49; glyph[3] = 0x29; glyph[4] = 0x1E; return true;
    case ' ': glyph[0] = 0x00; glyph[1] = 0x00; glyph[2] = 0x00; glyph[3] = 0x00; glyph[4] = 0x00; return true;
    case '-': glyph[0] = 0x08; glyph[1] = 0x08; glyph[2] = 0x08; glyph[3] = 0x08; glyph[4] = 0x08; return true;
    case '.': glyph[0] = 0x00; glyph[1] = 0x60; glyph[2] = 0x60; glyph[3] = 0x00; glyph[4] = 0x00; return true;
    case ',': glyph[0] = 0x00; glyph[1] = 0x40; glyph[2] = 0x20; glyph[3] = 0x00; glyph[4] = 0x00; return true;
    case '!': glyph[0] = 0x00; glyph[1] = 0x00; glyph[2] = 0x7D; glyph[3] = 0x00; glyph[4] = 0x00; return true;
    case '?': glyph[0] = 0x02; glyph[1] = 0x01; glyph[2] = 0x51; glyph[3] = 0x09; glyph[4] = 0x06; return true;
    case ':': glyph[0] = 0x00; glyph[1] = 0x36; glyph[2] = 0x36; glyph[3] = 0x00; glyph[4] = 0x00; return true;
    case '/': glyph[0] = 0x60; glyph[1] = 0x10; glyph[2] = 0x08; glyph[3] = 0x04; glyph[4] = 0x03; return true;
    case '+': glyph[0] = 0x08; glyph[1] = 0x08; glyph[2] = 0x3E; glyph[3] = 0x08; glyph[4] = 0x08; return true;
    case '_': glyph[0] = 0x40; glyph[1] = 0x40; glyph[2] = 0x40; glyph[3] = 0x40; glyph[4] = 0x40; return true;
    default: return false;
    }
}

// Basic framebuffer driver that uses the display HAL to manage the framebuffer and drawing operations.
int display_driver_init(display_driver_t *drv, const display_hal_config_t *hal_config)
{
    if (!drv || !hal_config) return -EINVAL;
    
    drv->hal_config = *hal_config;
    drv->framebuffer_size = EPD_BUF_SIZE;
    drv->framebuffer = framebuffer_static;
    
    int ret = display_hal_init(&drv->hal_config);
    if (ret < 0) return ret;
    
    drv->initialized = true;
    display_driver_clear(drv);
    return 0;
}

int display_driver_deinit(display_driver_t *drv)
{
    if (!drv) return -EINVAL;
    drv->initialized = false;
    return 0;
}

// Clear the framebuffer
int display_driver_clear(display_driver_t *drv)
{
    if (!drv || !drv->initialized) return -EINVAL;
    memset(drv->framebuffer, 0xFF, drv->framebuffer_size);
    return 0;
}

int display_driver_set_pixel(display_driver_t *drv, int x, int y, bool black)
{
    if (!drv || !drv->initialized) return -EINVAL;
    if (x < 0 || x >= EPD_WIDTH || y < 0 || y >= EPD_HEIGHT) {
        return -EINVAL;
    }

    x = (EPD_WIDTH - 1) - x;
    size_t bit_index = (size_t)y * EPD_WIDTH + (size_t)x;
    size_t byte_index = bit_index / 8;
    uint8_t bit_mask = (uint8_t)(0x80U >> (x & 7));

    if (black) {
        drv->framebuffer[byte_index] &= (uint8_t)~bit_mask;
    } else {
        drv->framebuffer[byte_index] |= bit_mask;
    }
    return 0;
}

int display_driver_draw_hline(display_driver_t *drv, int x, int y, int w, bool black)
{
    for (int i = 0; i < w; i++) {
        display_driver_set_pixel(drv, x + i, y, black);
    }
    return 0;
}

int display_driver_draw_vline(display_driver_t *drv, int x, int y, int h, bool black)
{
    for (int i = 0; i < h; i++) {
        display_driver_set_pixel(drv, x, y + i, black);
    }
    return 0;
}

int display_driver_draw_rect(display_driver_t *drv, int x, int y, int w, int h, bool black)
{
    display_driver_draw_hline(drv, x, y, w, black);
    display_driver_draw_hline(drv, x, y + h - 1, w, black);
    display_driver_draw_vline(drv, x, y, h, black);
    display_driver_draw_vline(drv, x + w - 1, y, h, black);
    return 0;
}

int display_driver_draw_filled_rect(display_driver_t *drv, int x, int y, int w, int h, bool black)
{
    for (int i = 0; i < w; i++) {
        for (int j = 0; j < h; j++) {
            display_driver_set_pixel(drv, x + i, y + j, black);
        }
    }
    return 0;
}

int display_driver_draw_char(display_driver_t *drv, int x, int y, char ch, int scale, bool bold)
{
    uint8_t glyph[5];
    if (!get_glyph(ch, glyph)) {
        return -EINVAL;
    }

    for (int col = 0; col < 5; col++) {
        for (int row = 0; row < 7; row++) {
            if ((glyph[col] & (1U << row)) != 0U) {
                for (int dx = 0; dx < scale; dx++) {
                    for (int dy = 0; dy < scale; dy++) {
                        display_driver_set_pixel(drv, 
                            x + (col * scale) + dx, 
                            y + (row * scale) + dy, 
                            true);
                        if (bold) {
                            display_driver_set_pixel(drv,
                                x + (col * scale) + dx + 1,
                                y + (row * scale) + dy,
                                true);
                        }
                    }
                }
            }
        }
    }
    return 0;
}

int display_driver_draw_text(display_driver_t *drv, int x, int y, int scale, bool bold, const char *text)
{
    if (!drv || !drv->initialized || !text) return -EINVAL;
    
    const int char_width = (5 * scale) + scale;
    int cursor_x = x;
    int cursor_y = y;

    for (size_t i = 0; text[i] != '\0'; i++) {
        char ch = text[i];

        if (ch == '\n') {
            cursor_x = x;
            cursor_y += (7 * scale) + scale;
            continue;
        }

        if (cursor_x + char_width > EPD_WIDTH - 4) {
            cursor_x = x;
            cursor_y += (7 * scale) + scale;
        }

        if (cursor_y + (7 * scale) > EPD_HEIGHT - 4) {
            break;
        }

        display_driver_draw_char(drv, cursor_x, cursor_y, ch, scale, bold);
        cursor_x += char_width;
    }
    return 0;
}

int display_driver_draw_text_centered(display_driver_t *drv, int y, int scale, bool bold, const char *text)
{
    if (!text) return -EINVAL;
    int text_width = strlen(text) * ((5 * scale) + scale);
    int x = (EPD_WIDTH - text_width) / 2;
    if (x < 2) x = 2;
    return display_driver_draw_text(drv, x, y, scale, bold, text);
}

int display_driver_draw_text_limited(display_driver_t *drv, int x, int y, int scale, bool bold,
                                     const char *text, size_t max_chars)
{
    if (text == NULL || max_chars == 0) return -EINVAL;

    char line[96];
    size_t limit = max_chars;
    if (limit > sizeof(line) - 2) {
        limit = sizeof(line) - 2;
    }

    size_t text_len = strnlen(text, limit + 1);
    bool truncated = text_len > limit;
    size_t copy_len = truncated ? limit : text_len;

    memcpy(line, text, copy_len);
    line[copy_len] = '\0';

    if (truncated && copy_len >= 2) {
        line[copy_len - 2] = '.';
        line[copy_len - 1] = '.';
    }

    return display_driver_draw_text(drv, x, y, scale, bold, line);
}

// Refresh sequence from weact studio driver
int display_driver_refresh(display_driver_t *drv)
{
    if (!drv || !drv->initialized) return -EINVAL;
    
    int ret = display_hal_set_ram_pointer(&drv->hal_config);
    if (ret < 0) return ret;

    ret = display_hal_write_cmd(&drv->hal_config, 0x24);
    if (ret < 0) return ret;
    ret = display_hal_write_data(&drv->hal_config, drv->framebuffer, drv->framebuffer_size);
    if (ret < 0) return ret;

    ret = display_hal_write_cmd(&drv->hal_config, 0x26);
    if (ret < 0) return ret;
    ret = display_hal_write_data(&drv->hal_config, drv->framebuffer, drv->framebuffer_size);
    if (ret < 0) return ret;

    ret = display_hal_write_cmd(&drv->hal_config, 0x22);
    if (ret < 0) return ret;
    ret = display_hal_write_u8(&drv->hal_config, 0xF7);
    if (ret < 0) return ret;

    ret = display_hal_write_cmd(&drv->hal_config, 0x20);
    if (ret < 0) return ret;

    return display_hal_wait_busy(&drv->hal_config, 5000);
}

int display_driver_sleep(display_driver_t *drv)
{
    if (!drv || !drv->initialized) return -EINVAL;
    return display_hal_sleep(&drv->hal_config);
}