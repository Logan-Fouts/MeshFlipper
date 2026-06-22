#ifndef DISPLAY_HAL_H
#define DISPLAY_HAL_H

#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/spi.h>
#include <zephyr/kernel.h>
#include <stdint.h>
#include <stdbool.h>

#define EPD_WIDTH 200
#define EPD_HEIGHT 200
#define EPD_BUF_SIZE ((EPD_WIDTH * EPD_HEIGHT) / 8)

/*
    Display HAL configuration structure. This will be used to initialize the display and perform operations on it.

    params:
    - spi_dev: The SPI device to which the display is connected.
    - cs_pin: GPIO specification for the Chip Select pin.
    - dc_pin: GPIO specification for the Data/Command pin.
    - rst_pin: GPIO specification for the Reset pin.
    - busy_pin: GPIO specification for the Busy pin.
    - spi_config: SPI configuration parameters such as frequency and operation mode.
    - width: Width of the display in pixels.
    - height: Height of the display in pixels.
*/
typedef struct {
    const struct device *spi_dev;
    struct gpio_dt_spec cs_pin;
    struct gpio_dt_spec dc_pin;
    struct gpio_dt_spec rst_pin;
    struct gpio_dt_spec busy_pin;
    struct spi_config spi_config;
    uint16_t width;
    uint16_t height;
} display_hal_config_t;

int display_hal_init(const display_hal_config_t *config);
int display_hal_write_cmd(const display_hal_config_t *config, uint8_t cmd);
int display_hal_write_data(const display_hal_config_t *config, const uint8_t *data, size_t len);
int display_hal_write_u8(const display_hal_config_t *config, uint8_t data);
int display_hal_wait_busy(const display_hal_config_t *config, int timeout_ms);
int display_hal_set_ram_pointer(const display_hal_config_t *config);
int display_hal_refresh(const display_hal_config_t *config);
int display_hal_sleep(const display_hal_config_t *config);

#endif