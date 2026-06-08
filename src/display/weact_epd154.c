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

// Message history for UI
static struct {
    int32_t id;
    char text[64];
    char from_name[40];
    uint64_t time;
} message_history[MAX_MESSAGES];
static int message_count = 0;
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

// Add a message to history
static void add_to_history(int32_t msg_id, const char *message, const char *from_name)
{
    if (msg_id != 0 && message_count > 0 && message_history[message_count - 1].id == msg_id) {
        strncpy(message_history[message_count - 1].text, message, sizeof(message_history[0].text) - 1);
        message_history[message_count - 1].text[sizeof(message_history[0].text) - 1] = '\0';
        if (from_name == NULL || from_name[0] == '\0') {
            strncpy(message_history[message_count - 1].from_name, "Unknown", sizeof(message_history[0].from_name) - 1);
        } else {
            strncpy(message_history[message_count - 1].from_name, from_name, sizeof(message_history[0].from_name) - 1);
        }
        message_history[message_count - 1].from_name[sizeof(message_history[0].from_name) - 1] = '\0';
        message_history[message_count - 1].time = k_uptime_get();
        current_display_index = message_count - 1;
        return;
    }

    // Shift messages if full
    if (message_count >= MAX_MESSAGES) {
        for (int i = 0; i < MAX_MESSAGES - 1; i++) {
            memcpy(&message_history[i], &message_history[i + 1], sizeof(message_history[0]));
        }
        message_count = MAX_MESSAGES - 1;
    }
    
    // Add new message
    message_history[message_count].id = msg_id;
    strncpy(message_history[message_count].text, message, sizeof(message_history[0].text) - 1);
    message_history[message_count].text[sizeof(message_history[0].text) - 1] = '\0';
    if (from_name == NULL || from_name[0] == '\0') {
        strncpy(message_history[message_count].from_name, "Unknown", sizeof(message_history[0].from_name) - 1);
    } else {
        strncpy(message_history[message_count].from_name, from_name, sizeof(message_history[0].from_name) - 1);
    }
    message_history[message_count].from_name[sizeof(message_history[0].from_name) - 1] = '\0';
    message_history[message_count].time = k_uptime_get();
    message_count++;
    current_display_index = message_count - 1;
}

// Draw the main UI
static void draw_ui(void)
{
    epd_frame_clear();
    
    // ===== TOP BAR =====
    // epd_draw_filled_rect(0, 0, EPD_WIDTH, 22, true);  // Black bar
    epd_draw_text_centered(5, 1, false, "MESHFLIPPER"); // White text on black
    epd_draw_hline(0, 23, EPD_WIDTH, true);            // Separator
    
    // ===== BOTTOM BAR =====
    epd_draw_hline(0, EPD_HEIGHT - 14, EPD_WIDTH, true);
    char status[32];
    snprintf(status, sizeof(status), "MSGS: %d", message_count);
    epd_draw_text(4, EPD_HEIGHT - 11, 1, false, status);
    
    // Navigation arrows if multiple messages
    if (message_count > 1) {
        epd_draw_text(EPD_WIDTH - 24, EPD_HEIGHT - 11, 1, false, "< >");
        char idx_str[8];
        snprintf(idx_str, sizeof(idx_str), "%d/%d", current_display_index + 1, message_count);
        epd_draw_text(EPD_WIDTH - 44, EPD_HEIGHT - 11, 1, false, idx_str);
    }
    
    // ===== CONTENT AREA =====
    epd_draw_rect(2, 26, EPD_WIDTH - 4, EPD_HEIGHT - 44, true);
    
    if (message_count == 0) {
        // No messages - show idle screen
        epd_draw_text_centered(EPD_HEIGHT / 2 - 20, 2, true, "NO MESSAGES");
        epd_draw_text_centered(EPD_HEIGHT / 2, 1, false, "Waiting for mesh...");
        epd_draw_text_centered(EPD_HEIGHT / 2 + 20, 1, false, "Send via GPIO3");
    } else {
        // Show current message
        char header[64];
        snprintf(header, sizeof(header), "FROM: %s", message_history[current_display_index].from_name);
        epd_draw_text(6, 32, 1, false, header);
        epd_draw_hline(4, 44, EPD_WIDTH - 8, true);
        
        // Draw message text
        epd_draw_text(6, 52, 2, true, message_history[current_display_index].text);
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

int weact_epd154_show_message_screen(const char *message)
{
    ARG_UNUSED(message);
    draw_ui();
    return epd_refresh_framebuffer();
}

int weact_epd154_show_received_message(int32_t msg_id, const char *message, const char *from_name)
{
    add_to_history(msg_id, message, from_name);
    draw_ui();
    return epd_refresh_framebuffer();
}

// Cycle through messages (call this from button press)
int weact_epd154_next_message(void)
{
    if (message_count > 0) {
        current_display_index = (current_display_index + 1) % message_count;
        draw_ui();
        return epd_refresh_framebuffer();
    }
    return 0;
}

int weact_epd154_previous_message(void)
{
    if (message_count > 0) {
        current_display_index = (current_display_index - 1 + message_count) % message_count;
        draw_ui();
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