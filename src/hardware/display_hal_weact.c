#include "hardware/display_hal_weact.h"
#include <zephyr/device.h>

// Returns the display configuration for the WeAct EPD module based on devicetree.
display_hal_config_t weact_display_get_config(void)
{
    // Ensure that all the necessary constants are defined for WeAct EPD module.
    #ifndef WEACT_CS_PIN
    #error "WEACT_CS_PIN must be defined"
    #endif

    #ifndef WEACT_DC_PIN
    #error "WEACT_DC_PIN must be defined"
    #endif

    #ifndef WEACT_RST_PIN
    #error "WEACT_RST_PIN must be defined"
    #endif

    #ifndef WEACT_BUSY_PIN
    #error "WEACT_BUSY_PIN must be defined"
    #endif

    #ifndef WEACT_SPI_FREQUENCY
    #error "WEACT_SPI_FREQUENCY must be defined"
    #endif

    #ifndef EPD_WIDTH
    #error "EPD_WIDTH must be defined"
    #endif

    #ifndef EPD_HEIGHT
    #error "EPD_HEIGHT must be defined"
    #endif

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
