#include "hardware/button_hal.h"

int button_hal_init(const button_hal_config_t *config) {
    if (!device_is_ready(config->gpio_dev)) {
        return -ENODEV;
    }
    return 0;
}

int button_hal_read(const button_hal_config_t *config) {
    return gpio_pin_get(config->gpio_dev, config->pin);
}

int button_hal_configure(const button_hal_config_t *config, gpio_flags_t flags) {
    return gpio_pin_configure(config->gpio_dev, config->pin, flags);
}