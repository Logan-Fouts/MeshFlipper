#ifndef BUTTON_HAL_H
#define BUTTON_HAL_H

#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>

#define GPIO_HIGH 1
#define GPIO_LOW 0

typedef struct {
    uint8_t pin;
    const struct device *gpio_dev;  // Make sure this is a pointer
} button_hal_config_t;

int button_hal_init(const button_hal_config_t *config);
int button_hal_read(const button_hal_config_t *config);
int button_hal_configure(const button_hal_config_t *config, gpio_flags_t flags);

#endif