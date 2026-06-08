#include <stdbool.h>
#include <ctype.h>
#include <errno.h>
#include <stdint.h>
#include <string.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/spi.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>

#include "display/weact_epd154.h"

#define EPD_WIDTH 200
#define EPD_HEIGHT 200
#define EPD_BUF_SIZE ((EPD_WIDTH * EPD_HEIGHT) / 8)
#define MAX_MESSAGES 5
#define INBOX_VISIBLE_ROWS 4
#define POPUP_DURATION_MS 1800

// Selection state: which message is currently highlighted in inbox
static int current_display_index = 0;

/*
 * WeAct 1.54in e-paper default wiring for this project (Raspberry Pi Pico):
 * DIN  -> GP19 (SPI0 MOSI)
 * CLK  -> GP18 (SPI0 SCK)
 * CS   -> GP17
 * DC   -> GP20
 * RST  -> GP21
 * BUSY -> GP22
 */
static const struct device *spi_dev = DEVICE_DT_GET(DT_NODELABEL(spi0));
static const struct gpio_dt_spec epd_cs = {
    .port = DEVICE_DT_GET(DT_NODELABEL(gpio0)),
    .pin = 17,
    .dt_flags = GPIO_ACTIVE_HIGH,
};
static const struct gpio_dt_spec epd_dc = {
    .port = DEVICE_DT_GET(DT_NODELABEL(gpio0)),
    .pin = 20,
    .dt_flags = GPIO_ACTIVE_HIGH,
};
static const struct gpio_dt_spec epd_rst = {
    .port = DEVICE_DT_GET(DT_NODELABEL(gpio0)),
    .pin = 21,
    .dt_flags = GPIO_ACTIVE_HIGH,
};
static const struct gpio_dt_spec epd_busy = {
    .port = DEVICE_DT_GET(DT_NODELABEL(gpio0)),
    .pin = 22,
    .dt_flags = GPIO_ACTIVE_HIGH,
};

static struct spi_config epd_spi_cfg = {
    .frequency = 4000000,
    .operation = SPI_WORD_SET(8) | SPI_TRANSFER_MSB | SPI_MODE_CPOL | SPI_MODE_CPHA,
    .slave = 0,
    .cs = NULL,
};

static uint8_t epd_frame[EPD_BUF_SIZE];

// Forward declaration
static void epd_draw_text(int x, int y, int scale, bool bold, const char *text);

// Helper to get node's long name from nodeHistory, falls back to "Unknown"
static const char* get_node_name(const struct nodeHistory *node_hist, int32_t node_num)
{
    if (node_hist == NULL) {
        return "Unknown";
    }

    // Search for the node
    for (size_t i = 0; i < node_hist->count; i++) {
        if ((int32_t)node_hist->nodes[i].num == node_num) {
            if (node_hist->nodes[i].long_name[0] != '\0') {
                return node_hist->nodes[i].long_name;
            }
            break;
        }
    }

    return "Unknown";
}

// Helper to get message at history index
static bool history_get_message_at(struct messageHistory *hist, int idx,
                                    int32_t *out_id, int32_t *out_from_num, int32_t *out_to_num,
                                    const char **out_text)
{
    if (hist == NULL || idx < 0 || idx >= (int)hist->count) {
        return false;
    }

    if (out_id != NULL) {
        *out_id = hist->messages[idx].id;
    }
    if (out_from_num != NULL) {
        *out_from_num = hist->messages[idx].from;
    }
    if (out_to_num != NULL) {
        *out_to_num = hist->messages[idx].to;
    }
    if (out_text != NULL) {
        *out_text = hist->messages[idx].text;
    }

    return true;
}

static void epd_draw_text_limited(int x, int y, int scale, bool bold, const char *text, size_t max_chars)
{
    if (text == NULL || max_chars == 0) {
        return;
    }

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

    if (truncated && copy_len >= 1) {
        if (copy_len >= 2) {
            line[copy_len - 2] = '.';
            line[copy_len - 1] = '.';
        }
    }

    epd_draw_text(x, y, scale, bold, line);
}

static void epd_build_wrapped_preview(char *out,
                                      size_t out_size,
                                      const char *text,
                                      size_t chars_per_line,
                                      size_t max_lines)
{
    if (out == NULL || out_size == 0) {
        return;
    }

    out[0] = '\0';

    if (text == NULL || chars_per_line == 0 || max_lines == 0) {
        return;
    }

    size_t out_idx = 0;
    size_t line = 0;
    size_t col = 0;
    bool truncated = false;

    for (size_t i = 0; text[i] != '\0'; i++) {
        char ch = text[i];

        if (ch == '\n') {
            if (line + 1 >= max_lines) {
                truncated = true;
                break;
            }
            if (out_idx + 1 >= out_size) {
                truncated = true;
                break;
            }
            out[out_idx++] = '\n';
            line++;
            col = 0;
            continue;
        }

        if (col >= chars_per_line) {
            if (line + 1 >= max_lines) {
                truncated = true;
                break;
            }
            if (out_idx + 1 >= out_size) {
                truncated = true;
                break;
            }
            out[out_idx++] = '\n';
            line++;
            col = 0;
        }

        if (out_idx + 1 >= out_size) {
            truncated = true;
            break;
        }

        out[out_idx++] = ch;
        col++;
    }

    out[out_idx] = '\0';

    if (truncated && out_idx >= 2) {
        out[out_idx - 2] = '.';
        out[out_idx - 1] = '.';
    }
}

static int epd_write_raw(const uint8_t *data, size_t len)
{
    struct spi_buf tx_buf = {
        .buf = (void *)data,
        .len = len,
    };
    struct spi_buf_set tx = {
        .buffers = &tx_buf,
        .count = 1,
    };

    return spi_write(spi_dev, &epd_spi_cfg, &tx);
}

static int epd_write_cmd(uint8_t cmd)
{
    gpio_pin_set_dt(&epd_dc, 0);
    gpio_pin_set_dt(&epd_cs, 0);
    int ret = epd_write_raw(&cmd, 1);
    gpio_pin_set_dt(&epd_cs, 1);
    return ret;
}

static int epd_write_data(const uint8_t *data, size_t len)
{
    gpio_pin_set_dt(&epd_dc, 1);
    gpio_pin_set_dt(&epd_cs, 0);
    int ret = epd_write_raw(data, len);
    gpio_pin_set_dt(&epd_cs, 1);
    return ret;
}

static int epd_write_u8(uint8_t data)
{
    return epd_write_data(&data, 1);
}

static int epd_wait_busy(int timeout_ms)
{
    int elapsed = 0;

    while (gpio_pin_get_dt(&epd_busy) > 0) {
        if (elapsed >= timeout_ms) {
            return -ETIMEDOUT;
        }
        k_sleep(K_MSEC(10));
        elapsed += 10;
    }

    return 0;
}

static int epd_set_ram_pointer(void)
{
    int ret = epd_write_cmd(0x4E);
    if (ret < 0) {
        return ret;
    }
    ret = epd_write_u8(0x00);
    if (ret < 0) {
        return ret;
    }

    ret = epd_write_cmd(0x4F);
    if (ret < 0) {
        return ret;
    }
    ret = epd_write_u8(0xC7);
    if (ret < 0) {
        return ret;
    }

    return epd_write_u8(0x00);
}

static void epd_frame_clear(void)
{
    memset(epd_frame, 0xFF, sizeof(epd_frame));
}

static void epd_set_pixel(int x, int y, bool black)
{
    if (x < 0 || x >= EPD_WIDTH || y < 0 || y >= EPD_HEIGHT) {
        return;
    }

    x = (EPD_WIDTH - 1) - x;

    size_t bit_index = (size_t)y * EPD_WIDTH + (size_t)x;
    size_t byte_index = bit_index / 8;
    uint8_t bit_mask = (uint8_t)(0x80U >> (x & 7));

    if (black) {
        epd_frame[byte_index] &= (uint8_t)~bit_mask;
    } else {
        epd_frame[byte_index] |= bit_mask;
    }
}

// Drawing primitives
static void epd_draw_rect(int x, int y, int w, int h, bool black)
{
    for (int i = 0; i < w; i++) {
        epd_set_pixel(x + i, y, black);
        epd_set_pixel(x + i, y + h - 1, black);
    }
    for (int i = 0; i < h; i++) {
        epd_set_pixel(x, y + i, black);
        epd_set_pixel(x + w - 1, y + i, black);
    }
}

static void epd_draw_filled_rect(int x, int y, int w, int h, bool black)
{
    for (int i = 0; i < w; i++) {
        for (int j = 0; j < h; j++) {
            epd_set_pixel(x + i, y + j, black);
        }
    }
}

static void epd_draw_hline(int x, int y, int w, bool black)
{
    for (int i = 0; i < w; i++) {
        epd_set_pixel(x + i, y, black);
    }
}

static void epd_draw_vline(int x, int y, int h, bool black)
{
    for (int i = 0; i < h; i++) {
        epd_set_pixel(x, y + i, black);
    }
}

// Font and text rendering
static bool epd_get_glyph(char ch, uint8_t glyph[5])
{
    char c = (char)toupper((unsigned char)ch);

    memset(glyph, 0, 5);

    switch (c) {
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
    case ' ': return true;
    case '-': glyph[0] = 0x08; glyph[1] = 0x08; glyph[2] = 0x08; glyph[3] = 0x08; glyph[4] = 0x08; return true;
    case '.': glyph[0] = 0x00; glyph[1] = 0x60; glyph[2] = 0x60; glyph[3] = 0x00; glyph[4] = 0x00; return true;
    case ',': glyph[0] = 0x00; glyph[1] = 0x40; glyph[2] = 0x20; glyph[3] = 0x00; glyph[4] = 0x00; return true;
    case '!': glyph[0] = 0x00; glyph[1] = 0x00; glyph[2] = 0x7D; glyph[3] = 0x00; glyph[4] = 0x00; return true;
    case '?': glyph[0] = 0x02; glyph[1] = 0x01; glyph[2] = 0x51; glyph[3] = 0x09; glyph[4] = 0x06; return true;
    case ':': glyph[0] = 0x00; glyph[1] = 0x36; glyph[2] = 0x36; glyph[3] = 0x00; glyph[4] = 0x00; return true;
    case '/': glyph[0] = 0x60; glyph[1] = 0x10; glyph[2] = 0x08; glyph[3] = 0x04; glyph[4] = 0x03; return true;
    case '+': glyph[0] = 0x08; glyph[1] = 0x08; glyph[2] = 0x3E; glyph[3] = 0x08; glyph[4] = 0x08; return true;
    case '>': glyph[0] = 0x41; glyph[1] = 0x22; glyph[2] = 0x14; glyph[3] = 0x08; glyph[4] = 0x00; return true;
    case '<': glyph[0] = 0x08; glyph[1] = 0x14; glyph[2] = 0x22; glyph[3] = 0x41; glyph[4] = 0x00; return true;
    case '(': glyph[0] = 0x00; glyph[1] = 0x1C; glyph[2] = 0x22; glyph[3] = 0x41; glyph[4] = 0x00; return true;
    case ')': glyph[0] = 0x00; glyph[1] = 0x41; glyph[2] = 0x22; glyph[3] = 0x1C; glyph[4] = 0x00; return true;
    case '_': glyph[0] = 0x40; glyph[1] = 0x40; glyph[2] = 0x40; glyph[3] = 0x40; glyph[4] = 0x40; return true;
    case '\'': glyph[0] = 0x00; glyph[1] = 0x03; glyph[2] = 0x00; glyph[3] = 0x00; glyph[4] = 0x00; return true;
    case '#': glyph[0] = 0x14; glyph[1] = 0x7F; glyph[2] = 0x14; glyph[3] = 0x7F; glyph[4] = 0x14; return true;
    default:
        glyph[0] = 0x02; glyph[1] = 0x01; glyph[2] = 0x51; glyph[3] = 0x09; glyph[4] = 0x06;
        return false;
    }
}

static void epd_draw_char(int x, int y, char ch, int scale, bool bold)
{
    uint8_t glyph[5];

    epd_get_glyph(ch, glyph);

    for (int col = 0; col < 5; col++) {
        for (int row = 0; row < 7; row++) {
            if ((glyph[col] & (1U << row)) != 0U) {
                for (int dx = 0; dx < scale; dx++) {
                    for (int dy = 0; dy < scale; dy++) {
                        epd_set_pixel(x + (col * scale) + dx, y + (row * scale) + dy, true);
                        if (bold) {
                            epd_set_pixel(x + (col * scale) + dx + 1, y + (row * scale) + dy, true);
                        }
                    }
                }
            }
        }
    }
}

static void epd_draw_text(int x, int y, int scale, bool bold, const char *text)
{
    const int char_width = (5 * scale) + scale;
    int cursor_x = x;
    int cursor_y = y;

    if (text == NULL) {
        return;
    }

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

        epd_draw_char(cursor_x, cursor_y, ch, scale, bold);
        cursor_x += char_width;
    }
}

static void epd_draw_text_centered(int y, int scale, bool bold, const char *text)
{
    int text_width = strlen(text) * ((5 * scale) + scale);
    int x = (EPD_WIDTH - text_width) / 2;
    if (x < 2) x = 2;
    epd_draw_text(x, y, scale, bold, text);
}

static int epd_refresh_framebuffer(void)
{
    int ret = epd_set_ram_pointer();
    if (ret < 0) return ret;

    ret = epd_write_cmd(0x24);
    if (ret < 0) return ret;
    ret = epd_write_data(epd_frame, sizeof(epd_frame));
    if (ret < 0) return ret;

    ret = epd_write_cmd(0x26);
    if (ret < 0) return ret;
    ret = epd_write_data(epd_frame, sizeof(epd_frame));
    if (ret < 0) return ret;

    ret = epd_write_cmd(0x22);
    if (ret < 0) return ret;
    ret = epd_write_u8(0xF7);
    if (ret < 0) return ret;

    ret = epd_write_cmd(0x20);
    if (ret < 0) return ret;

    return epd_wait_busy(5000);
}

// Add a message to history, returns true when a new row is created.
static bool is_broadcast_message(struct messageHistory *hist, int idx)
{
    if (hist == NULL || idx < 0 || idx >= (int)hist->count) {
        return false;
    }

    return (uint32_t)hist->messages[idx].to == 0xFFFFFFFFu;
}

static int build_inbox_indices(struct messageHistory *hist, const struct nodeHistory *node_hist, int out_indices[MAX_MESSAGES])
{
    if (hist == NULL || hist->count == 0 || node_hist == NULL) {
        return 0;
    }

    uint32_t my_node_num = node_hist->my_info.num;

    int latest_broadcast = -1;
    for (int i = 0; i < (int)hist->count; i++) {
        if (is_broadcast_message(hist, i)) {
            latest_broadcast = i;
        }
    }

    int per_node_latest[MAX_MESSAGES];
    int per_node_count = 0;
    int32_t seen_nodes[MAX_MESSAGES];
    int seen_count = 0;

    for (int i = (int)hist->count - 1; i >= 0 && per_node_count < MAX_MESSAGES; i--) {
        if (is_broadcast_message(hist, i)) {
            continue;
        }

        // Exclude self-messages (from == to)
        if (hist->messages[i].from == hist->messages[i].to) {
            continue;
        }

        // Exclude messages sent by the device itself
        if ((uint32_t)hist->messages[i].from == my_node_num) {
            continue;
        }

        int32_t node_id = hist->messages[i].from;
        if (node_id == 0) {
            node_id = hist->messages[i].to;
        }

        bool already_seen = false;
        for (int s = 0; s < seen_count; s++) {
            if (seen_nodes[s] == node_id) {
                already_seen = true;
                break;
            }
        }

        if (already_seen) {
            continue;
        }

        seen_nodes[seen_count++] = node_id;
        per_node_latest[per_node_count++] = i;
    }

    int count = 0;
    for (int i = per_node_count - 1; i >= 0 && count < MAX_MESSAGES; i--) {
        out_indices[count++] = per_node_latest[i];
    }

    if (latest_broadcast >= 0) {
        bool already_included = false;
        for (int i = 0; i < count; i++) {
            if (out_indices[i] == latest_broadcast) {
                already_included = true;
                break;
            }
        }

        if (!already_included) {
            if (count < MAX_MESSAGES) {
                out_indices[count++] = latest_broadcast;
            } else {
                int oldest_pos = 0;
                for (int i = 1; i < count; i++) {
                    if (out_indices[i] < out_indices[oldest_pos]) {
                        oldest_pos = i;
                    }
                }
                out_indices[oldest_pos] = latest_broadcast;
            }
        }
    }

    for (int i = 0; i < count - 1; i++) {
        for (int j = i + 1; j < count; j++) {
            if (out_indices[j] < out_indices[i]) {
                int tmp = out_indices[i];
                out_indices[i] = out_indices[j];
                out_indices[j] = tmp;
            }
        }
    }

    return count;
}

static int inbox_selected_position(const int *inbox_indices, int inbox_count)
{
    for (int i = 0; i < inbox_count; i++) {
        if (inbox_indices[i] == current_display_index) {
            return i;
        }
    }

    return inbox_count > 0 ? inbox_count - 1 : 0;
}

static int inbox_start_index(int selected_pos, int inbox_count)
{
    if (inbox_count <= INBOX_VISIBLE_ROWS) {
        return 0;
    }

    int start = selected_pos - (INBOX_VISIBLE_ROWS - 1);
    if (start < 0) {
        start = 0;
    }

    int max_start = inbox_count - INBOX_VISIBLE_ROWS;
    if (start > max_start) {
        start = max_start;
    }

    return start;
}

static void draw_message_popup(struct messageHistory *hist, const struct nodeHistory *node_hist, int index)
{
    epd_draw_filled_rect(12, 46, EPD_WIDTH - 24, EPD_HEIGHT - 74, false);
    epd_draw_rect(12, 46, EPD_WIDTH - 24, EPD_HEIGHT - 74, true);
    epd_draw_hline(12, 66, EPD_WIDTH - 24, true);

    epd_draw_text_centered(53, 1, true, "NEW MESSAGE");

    const char *msg_text = "";
    int32_t from_num = 0;
    history_get_message_at(hist, index, NULL, &from_num, NULL, &msg_text);

    const char *from_name = get_node_name(node_hist, from_num);

    char from_line[52];
    snprintf(from_line, sizeof(from_line), "FROM %s", from_name);
    epd_draw_text_limited(18, 72, 1, false, from_line, 28);

    epd_draw_hline(16, 84, EPD_WIDTH - 32, true);

    char popup_text[192];
    size_t popup_chars_per_line = (size_t)((EPD_WIDTH - 36) / 6);
    epd_build_wrapped_preview(popup_text,
                              sizeof(popup_text),
                              msg_text,
                              popup_chars_per_line,
                              9);
    epd_draw_text(18, 90, 1, true, popup_text);
}

// Draw the main inbox UI
static void draw_ui(struct messageHistory *hist, const struct nodeHistory *node_hist)
{
    epd_frame_clear();
    int inbox_indices[MAX_MESSAGES];
    int inbox_count = build_inbox_indices(hist, node_hist, inbox_indices);
    int selected_pos = inbox_selected_position(inbox_indices, inbox_count);

    if (inbox_count > 0) {
        current_display_index = inbox_indices[selected_pos];
    }
    
    // ===== TOP BAR =====
    // epd_draw_filled_rect(0, 0, EPD_WIDTH, 22, true);  // Black bar
    epd_draw_text_centered(5, 1, false, "MESSAGES");
    epd_draw_hline(0, 23, EPD_WIDTH, true);            // Separator
    
    // ===== BOTTOM BAR =====
    epd_draw_hline(0, EPD_HEIGHT - 14, EPD_WIDTH, true);
    char status[32];
    snprintf(status, sizeof(status), "INBOX: %d", inbox_count);
    epd_draw_text(4, EPD_HEIGHT - 11, 1, false, status);
    
    // Navigation arrows if multiple messages
    if (inbox_count > 1) {
        epd_draw_text(EPD_WIDTH - 24, EPD_HEIGHT - 11, 1, false, "< >");
        char idx_str[16];
        snprintf(idx_str, sizeof(idx_str), "%u/%u",
                 (unsigned int)(selected_pos + 1),
                 (unsigned int)inbox_count);
        epd_draw_text(EPD_WIDTH - 44, EPD_HEIGHT - 11, 1, false, idx_str);
    }
    
    // ===== CONTENT AREA =====
    epd_draw_rect(2, 26, EPD_WIDTH - 4, EPD_HEIGHT - 44, true);
    
    if (inbox_count == 0) {
        // No messages - show idle screen
        epd_draw_text_centered(EPD_HEIGHT / 2 - 20, 2, true, "NO MESSAGES");
        epd_draw_text_centered(EPD_HEIGHT / 2, 1, false, "GPIO3 Reply  GPIO5 Bcast");
        epd_draw_text_centered(EPD_HEIGHT / 2 + 20, 1, false, "GPIO2/4 Select");
    } else {
        const int row_h = 36;
        int start = inbox_start_index(selected_pos, inbox_count);
        int rows = inbox_count - start;
        if (rows > INBOX_VISIBLE_ROWS) {
            rows = INBOX_VISIBLE_ROWS;
        }

        for (int i = 0; i < rows; i++) {
            int visible_idx = start + i;
            int idx = inbox_indices[visible_idx];
            int row_y = 28 + (i * row_h);

            if (visible_idx == selected_pos) {
                epd_draw_rect(4, row_y - 1, EPD_WIDTH - 8, row_h - 1, true);
            }

            int32_t from_num = 0;
            const char *msg_text = "";
            history_get_message_at(hist, idx, NULL, &from_num, NULL, &msg_text);

            const char *from_name = get_node_name(node_hist, from_num);

            epd_draw_text_limited(8, row_y + 3, 1, true, from_name, 22);
            if (is_broadcast_message(hist, idx)) {
                char broadcast_line[72];
                snprintf(broadcast_line,
                         sizeof(broadcast_line),
                         "BRDCST: %s",
                         msg_text);
                epd_draw_text_limited(8, row_y + 16, 1, false, broadcast_line, 31);
            } else {
                epd_draw_text_limited(8, row_y + 16, 1, false, msg_text, 31);
            }

            if (i < rows - 1) {
                epd_draw_hline(6, row_y + row_h - 2, EPD_WIDTH - 12, true);
            }
        }
    }
}

int weact_epd154_init(void)
{
    if (!device_is_ready(spi_dev) || !device_is_ready(epd_cs.port) ||
        !device_is_ready(epd_dc.port) || !device_is_ready(epd_rst.port) ||
        !device_is_ready(epd_busy.port)) {
        printk("EPD: SPI/GPIO not ready\n");
        return -ENODEV;
    }

    int ret = gpio_pin_configure_dt(&epd_cs, GPIO_OUTPUT_HIGH);
    if (ret < 0) return ret;
    ret = gpio_pin_configure_dt(&epd_dc, GPIO_OUTPUT_HIGH);
    if (ret < 0) return ret;
    ret = gpio_pin_configure_dt(&epd_rst, GPIO_OUTPUT_HIGH);
    if (ret < 0) return ret;
    ret = gpio_pin_configure_dt(&epd_busy, GPIO_INPUT);
    if (ret < 0) return ret;

    gpio_pin_set_dt(&epd_rst, 0);
    k_sleep(K_MSEC(20));
    gpio_pin_set_dt(&epd_rst, 1);
    k_sleep(K_MSEC(20));

    ret = epd_wait_busy(3000);
    if (ret < 0) {
        printk("EPD: busy timeout before init\n");
        return ret;
    }

    ret = epd_write_cmd(0x12);
    if (ret < 0) return ret;
    k_sleep(K_MSEC(100));
    ret = epd_wait_busy(5000);
    if (ret < 0) return ret;

    ret = epd_write_cmd(0x01);
    if (ret < 0) return ret;
    ret = epd_write_u8(0xC7);
    if (ret < 0) return ret;
    ret = epd_write_u8(0x00);
    if (ret < 0) return ret;
    ret = epd_write_u8(0x00);
    if (ret < 0) return ret;

    ret = epd_write_cmd(0x11);
    if (ret < 0) return ret;
    ret = epd_write_u8(0x01);
    if (ret < 0) return ret;

    ret = epd_write_cmd(0x44);
    if (ret < 0) return ret;
    ret = epd_write_u8(0x00);
    if (ret < 0) return ret;
    ret = epd_write_u8(0x18);
    if (ret < 0) return ret;

    ret = epd_write_cmd(0x45);
    if (ret < 0) return ret;
    ret = epd_write_u8(0xC7);
    if (ret < 0) return ret;
    ret = epd_write_u8(0x00);
    if (ret < 0) return ret;
    ret = epd_write_u8(0x00);
    if (ret < 0) return ret;
    ret = epd_write_u8(0x00);
    if (ret < 0) return ret;

    ret = epd_write_cmd(0x3C);
    if (ret < 0) return ret;
    ret = epd_write_u8(0x05);
    if (ret < 0) return ret;

    ret = epd_write_cmd(0x18);
    if (ret < 0) return ret;
    ret = epd_write_u8(0x80);
    if (ret < 0) return ret;

    ret = epd_set_ram_pointer();
    if (ret < 0) return ret;

    printk("EPD: init complete\n");
    return 0;
}

int weact_epd154_show_boot_pattern(void)
{
    epd_frame_clear();
    epd_draw_text_centered(80, 2, true, "MESHFLIPPER");
    epd_draw_text_centered(110, 1, false, "Starting...");
    epd_draw_hline(0, 130, EPD_WIDTH, true);
    epd_draw_text(4, 140, 1, false, "v1.0");
    return epd_refresh_framebuffer();
}

int weact_epd154_show_message_screen(struct messageHistory *message_history,
                                     const struct nodeHistory *node_history)
{
    draw_ui(message_history, node_history);
    return epd_refresh_framebuffer();
}

int weact_epd154_show_received_message(struct messageHistory *message_history,
                                       const struct nodeHistory *node_history,
                                       bool show_popup)
{
    if (show_popup && message_history != NULL && message_history->count > 0) {
        draw_ui(message_history, node_history);
        int latest_idx = message_history->count - 1;
        draw_message_popup(message_history, node_history, latest_idx);
        int ret = epd_refresh_framebuffer();
        if (ret < 0) {
            return ret;
        }

        k_sleep(K_MSEC(POPUP_DURATION_MS));
    }

    draw_ui(message_history, node_history);
    return epd_refresh_framebuffer();
}

int weact_epd154_record_received_message(void)
{
    // Messages are already stored in main's messageHistory by main.c
    // Nothing to do here - this is now a no-op
    return 0;
}

int weact_epd154_show_compose_screen(const char *target_label,
                                     const char *draft_text,
                                     bool broadcast_mode)
{
    epd_frame_clear();

    epd_draw_filled_rect(10, 42, EPD_WIDTH - 20, EPD_HEIGHT - 56, false);
    epd_draw_rect(10, 42, EPD_WIDTH - 20, EPD_HEIGHT - 56, true);

    epd_draw_text(16, 48, 1, true, "COMPOSE");
    epd_draw_hline(12, 60, EPD_WIDTH - 24, true);

    char to_line[72];
    snprintf(to_line,
             sizeof(to_line),
             "TO %s %s",
             broadcast_mode ? "ALL" : "NODE",
             target_label != NULL ? target_label : "Unknown");
    epd_draw_text_limited(16, 66, 1, false, to_line, 34);

    epd_draw_hline(14, 78, EPD_WIDTH - 28, true);
    epd_draw_text_limited(16, 86, 1, true, draft_text, 84);

    epd_draw_hline(12, EPD_HEIGHT - 32, EPD_WIDTH - 24, true);
    epd_draw_text(16, EPD_HEIGHT - 28, 1, false, "2/4:Draft 5:Mode 3:Send");

    return epd_refresh_framebuffer();
}

int weact_epd154_show_thread_screen(const char *target_label,
                                    const struct weact_epd154_thread_entry *entries,
                                    size_t entry_count,
                                    size_t selected_visible_index,
                                    size_t global_index,
                                    size_t total)
{
    epd_frame_clear();

    epd_draw_filled_rect(8, 30, EPD_WIDTH - 16, EPD_HEIGHT - 50, false);
    epd_draw_rect(8, 30, EPD_WIDTH - 16, EPD_HEIGHT - 50, true);

    char title[72];
    snprintf(title,
             sizeof(title),
             "DM %s",
             target_label != NULL ? target_label : "Unknown");
    epd_draw_text_limited(14, 36, 1, true, title, 26);
    epd_draw_hline(10, 48, EPD_WIDTH - 20, true);

    const int bubble_top = 54;
    const int bubble_gap = 3;
    const int bubble_area_bottom = EPD_HEIGHT - 32;
    const int bubble_w = EPD_WIDTH - 84;

    int bubble_heights[WEACT_EPD154_THREAD_VISIBLE] = {0};
    size_t bubble_lines[WEACT_EPD154_THREAD_VISIBLE] = {0};

    if (entries == NULL || entry_count == 0) {
        epd_draw_text(14, bubble_top + 8, 1, false, "NO DM MESSAGES YET");
    } else {
        size_t chars_per_line = (size_t)((bubble_w - 8) / 6);
        if (chars_per_line == 0) {
            chars_per_line = 1;
        }

        for (size_t i = 0; i < entry_count && i < WEACT_EPD154_THREAD_VISIBLE; i++) {
            size_t lines = 1;
            size_t col = 0;
            const char *text = entries[i].text != NULL ? entries[i].text : "";

            for (size_t p = 0; text[p] != '\0'; p++) {
                if (text[p] == '\n') {
                    lines++;
                    col = 0;
                    continue;
                }

                col++;
                if (col > chars_per_line) {
                    lines++;
                    col = 1;
                }
            }

            bubble_lines[i] = lines;
            bubble_heights[i] = 20 + (int)(lines * 8);
        }

        size_t start_visible = 0;
        while (start_visible < entry_count) {
            int used_height = 0;
            for (size_t i = start_visible; i < entry_count; i++) {
                used_height += bubble_heights[i];
                if (i + 1 < entry_count) {
                    used_height += bubble_gap;
                }
            }

            if ((bubble_top + used_height) <= bubble_area_bottom) {
                break;
            }

            start_visible++;
        }

        int y = bubble_top;
        for (size_t i = start_visible; i < entry_count; i++) {
            bool outgoing = entries[i].is_outgoing;
            int bubble_h = bubble_heights[i];
            int x = outgoing ? (EPD_WIDTH - bubble_w - 14) : 14;

            epd_draw_rect(x, y, bubble_w, bubble_h, true);

            size_t selected_draw_index = selected_visible_index;
            if (selected_draw_index < start_visible) {
                selected_draw_index = start_visible;
            }

            if (selected_draw_index == i) {
                epd_draw_rect(x - 1, y - 1, bubble_w + 2, bubble_h + 2, true);
            }

            epd_draw_text(x + 4, y + 3, 1, false, outgoing ? "YOU" : "THEM");
            epd_draw_hline(x + 2, y + 12, bubble_w - 4, true);

            char bubble_text[512];
            epd_build_wrapped_preview(bubble_text,
                                      sizeof(bubble_text),
                                      entries[i].text != NULL ? entries[i].text : "",
                                      chars_per_line,
                                      bubble_lines[i]);
            epd_draw_text(x + 4, y + 15, 1, true, bubble_text);

            y += bubble_h + bubble_gap;
            if (y >= bubble_area_bottom) {
                break;
            }
        }
    }

    char pos[24];
    snprintf(pos, sizeof(pos), "%u/%u", (unsigned int)global_index, (unsigned int)total);
    epd_draw_text(EPD_WIDTH - 52, 36, 1, false, pos);

    epd_draw_hline(10, EPD_HEIGHT - 30, EPD_WIDTH - 20, true);
    epd_draw_text(14, EPD_HEIGHT - 26, 1, false, "2/4:Msg 3:Reply 5:Back");

    return epd_refresh_framebuffer();
}

bool weact_epd154_get_selected_message(struct messageHistory *message_history,
                                       const struct nodeHistory *node_history,
                                       int32_t *msg_id, int32_t *from_num, int32_t *to_num)
{
    int inbox_indices[MAX_MESSAGES];
    int inbox_count = build_inbox_indices(message_history, node_history, inbox_indices);
    if (inbox_count <= 0) {
        return false;
    }

    int selected_pos = inbox_selected_position(inbox_indices, inbox_count);
    int selected_raw = inbox_indices[selected_pos];
    current_display_index = selected_raw;

    (void)node_history;  // Unused but passed for consistency

    return history_get_message_at(message_history, selected_raw, msg_id, from_num, to_num, NULL);
}

// Cycle through messages (call this from button press)
int weact_epd154_next_message(struct messageHistory *message_history,
                              const struct nodeHistory *node_history)
{
    int inbox_indices[MAX_MESSAGES];
    int inbox_count = build_inbox_indices(message_history, node_history, inbox_indices);

    if (inbox_count > 0) {
        int selected_pos = inbox_selected_position(inbox_indices, inbox_count);
        selected_pos = (selected_pos + 1) % inbox_count;
        current_display_index = inbox_indices[selected_pos];
        draw_ui(message_history, node_history);
        return epd_refresh_framebuffer();
    }

    return 0;
}

int weact_epd154_previous_message(struct messageHistory *message_history,
                                  const struct nodeHistory *node_history)
{
    int inbox_indices[MAX_MESSAGES];
    int inbox_count = build_inbox_indices(message_history, node_history, inbox_indices);

    if (inbox_count > 0) {
        int selected_pos = inbox_selected_position(inbox_indices, inbox_count);
        selected_pos = (selected_pos - 1 + inbox_count) % inbox_count;
        current_display_index = inbox_indices[selected_pos];
        draw_ui(message_history, node_history);
        return epd_refresh_framebuffer();
    }

    return 0;
}

int weact_epd154_sleep(void)
{
    int ret = epd_write_cmd(0x10);
    if (ret < 0) return ret;
    ret = epd_write_u8(0x01);
    if (ret < 0) return ret;
    return 0;
}