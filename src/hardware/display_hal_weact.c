#include "hardware/display_hal_weact.h"
#include <zephyr/device.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(display_hal_weact, LOG_LEVEL_DBG);

display_hal_config_t weact_display_get_config(void)
{
    const struct device *spi_dev = DEVICE_DT_GET(DT_NODELABEL(spi0));
    const struct device *gpio_dev = DEVICE_DT_GET(DT_NODELABEL(gpio0));
    
    display_hal_config_t config = {
        .spi_dev = spi_dev,
        .cs_pin = {
            .port = gpio_dev,
            .pin = WEACT_CS_PIN,
            .dt_flags = GPIO_ACTIVE_HIGH,
        },
        .dc_pin = {
            .port = gpio_dev,
            .pin = WEACT_DC_PIN,
            .dt_flags = GPIO_ACTIVE_HIGH,
        },
        .rst_pin = {
            .port = gpio_dev,
            .pin = WEACT_RST_PIN,
            .dt_flags = GPIO_ACTIVE_HIGH,
        },
        .busy_pin = {
            .port = gpio_dev,
            .pin = WEACT_BUSY_PIN,
            .dt_flags = GPIO_ACTIVE_HIGH,
        },
        .spi_config = {
            .frequency = WEACT_SPI_FREQUENCY,
            .operation = SPI_WORD_SET(8) | SPI_TRANSFER_MSB | SPI_MODE_CPOL | SPI_MODE_CPHA,
            .slave = 0,
            .cs = NULL,
        },
        .width = EPD_WIDTH,
        .height = EPD_HEIGHT,
    };
    
    return config;
}