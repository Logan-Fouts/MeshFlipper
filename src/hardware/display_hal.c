#include "hardware/display_hal.h"
#include <zephyr/logging/log.h>
#include <zephyr/kernel.h>
#include <string.h>

LOG_MODULE_REGISTER(display_hal, LOG_LEVEL_DBG);

static int display_hal_write_raw(const display_hal_config_t *config, const uint8_t *data, size_t len)
{
    struct spi_buf tx_buf = {
        .buf = (void *)data,
        .len = len,
    };
    struct spi_buf_set tx = {
        .buffers = &tx_buf,
        .count = 1,
    };

    return spi_write(config->spi_dev, &config->spi_config, &tx);
}

int display_hal_write_cmd(const display_hal_config_t *config, uint8_t cmd)
{
    gpio_pin_set_dt(&config->dc_pin, 0);
    gpio_pin_set_dt(&config->cs_pin, 0);
    int ret = display_hal_write_raw(config, &cmd, 1);
    gpio_pin_set_dt(&config->cs_pin, 1);
    return ret;
}

int display_hal_write_data(const display_hal_config_t *config, const uint8_t *data, size_t len)
{
    gpio_pin_set_dt(&config->dc_pin, 1);
    gpio_pin_set_dt(&config->cs_pin, 0);
    int ret = display_hal_write_raw(config, data, len);
    gpio_pin_set_dt(&config->cs_pin, 1);
    return ret;
}

int display_hal_write_u8(const display_hal_config_t *config, uint8_t data)
{
    return display_hal_write_data(config, &data, 1);
}

int display_hal_wait_busy(const display_hal_config_t *config, int timeout_ms)
{
    int elapsed = 0;
    while (gpio_pin_get_dt(&config->busy_pin) > 0) {
        if (elapsed >= timeout_ms) {
            LOG_ERR("Busy timeout after %d ms", timeout_ms);
            return -ETIMEDOUT;
        }
        k_sleep(K_MSEC(10));
        elapsed += 10;
    }
    return 0;
}

int display_hal_set_ram_pointer(const display_hal_config_t *config)
{
    int ret = display_hal_write_cmd(config, 0x4E);
    if (ret < 0) return ret;
    ret = display_hal_write_u8(config, 0x00);
    if (ret < 0) return ret;

    ret = display_hal_write_cmd(config, 0x4F);
    if (ret < 0) return ret;
    ret = display_hal_write_u8(config, 0xC7);
    if (ret < 0) return ret;
    return display_hal_write_u8(config, 0x00);
}

int display_hal_refresh(const display_hal_config_t *config)
{
    // Same as working code's epd_refresh_framebuffer
    int ret = display_hal_set_ram_pointer(config);
    if (ret < 0) return ret;

    ret = display_hal_write_cmd(config, 0x24);
    if (ret < 0) return ret;
    // Data is written separately by the driver

    ret = display_hal_write_cmd(config, 0x26);
    if (ret < 0) return ret;
    // Data is written separately by the driver

    ret = display_hal_write_cmd(config, 0x22);
    if (ret < 0) return ret;
    ret = display_hal_write_u8(config, 0xF7);
    if (ret < 0) return ret;

    ret = display_hal_write_cmd(config, 0x20);
    if (ret < 0) return ret;

    return display_hal_wait_busy(config, 5000);
}

int display_hal_init(const display_hal_config_t *config)
{
    LOG_INF("Display init starting...");
    
    if (!config) {
        LOG_ERR("Config is NULL");
        return -EINVAL;
    }
    
    if (!device_is_ready(config->spi_dev) ||
        !device_is_ready(config->cs_pin.port) ||
        !device_is_ready(config->dc_pin.port) ||
        !device_is_ready(config->rst_pin.port) ||
        !device_is_ready(config->busy_pin.port)) {
        LOG_ERR("SPI/GPIO not ready");
        return -ENODEV;
    }

    int ret = gpio_pin_configure_dt(&config->cs_pin, GPIO_OUTPUT_HIGH);
    if (ret < 0) return ret;
    ret = gpio_pin_configure_dt(&config->dc_pin, GPIO_OUTPUT_HIGH);
    if (ret < 0) return ret;
    ret = gpio_pin_configure_dt(&config->rst_pin, GPIO_OUTPUT_HIGH);
    if (ret < 0) return ret;
    ret = gpio_pin_configure_dt(&config->busy_pin, GPIO_INPUT);
    if (ret < 0) return ret;

    gpio_pin_set_dt(&config->rst_pin, 0);
    k_sleep(K_MSEC(20));
    gpio_pin_set_dt(&config->rst_pin, 1);
    k_sleep(K_MSEC(20));

    ret = display_hal_wait_busy(config, 3000);
    if (ret < 0) {
        LOG_ERR("Busy timeout before init");
        return ret;
    }

    ret = display_hal_write_cmd(config, 0x12);
    if (ret < 0) return ret;
    k_sleep(K_MSEC(100));
    ret = display_hal_wait_busy(config, 5000);
    if (ret < 0) return ret;

    ret = display_hal_write_cmd(config, 0x01);
    if (ret < 0) return ret;
    ret = display_hal_write_u8(config, 0xC7);
    if (ret < 0) return ret;
    ret = display_hal_write_u8(config, 0x00);
    if (ret < 0) return ret;
    ret = display_hal_write_u8(config, 0x00);
    if (ret < 0) return ret;

    ret = display_hal_write_cmd(config, 0x11);
    if (ret < 0) return ret;
    ret = display_hal_write_u8(config, 0x01);
    if (ret < 0) return ret;

    ret = display_hal_write_cmd(config, 0x44);
    if (ret < 0) return ret;
    ret = display_hal_write_u8(config, 0x00);
    if (ret < 0) return ret;
    ret = display_hal_write_u8(config, 0x18);
    if (ret < 0) return ret;

    ret = display_hal_write_cmd(config, 0x45);
    if (ret < 0) return ret;
    ret = display_hal_write_u8(config, 0xC7);
    if (ret < 0) return ret;
    ret = display_hal_write_u8(config, 0x00);
    if (ret < 0) return ret;
    ret = display_hal_write_u8(config, 0x00);
    if (ret < 0) return ret;
    ret = display_hal_write_u8(config, 0x00);
    if (ret < 0) return ret;

    ret = display_hal_write_cmd(config, 0x3C);
    if (ret < 0) return ret;
    ret = display_hal_write_u8(config, 0x05);
    if (ret < 0) return ret;

    ret = display_hal_write_cmd(config, 0x18);
    if (ret < 0) return ret;
    ret = display_hal_write_u8(config, 0x80);
    if (ret < 0) return ret;

    ret = display_hal_set_ram_pointer(config);
    if (ret < 0) return ret;

    LOG_INF("Display init complete");
    return 0;
}

int display_hal_sleep(const display_hal_config_t *config)
{
    int ret = display_hal_write_cmd(config, 0x10);
    if (ret < 0) return ret;
    return display_hal_write_u8(config, 0x01);
}