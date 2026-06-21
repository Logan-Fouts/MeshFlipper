#include "hardware/button_hal.h"
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(button_hal, LOG_LEVEL_DBG);

int button_hal_init(const button_hal_config_t *config) {
    if (!config) {
        LOG_ERR("Config is NULL");
        return -EINVAL;
    }
    
    if (!device_is_ready(config->gpio_dev)) {
        LOG_ERR("GPIO device not ready for pin %d", config->pin);
        return -ENODEV;
    }
    return 0;
}

int button_hal_read(const button_hal_config_t *config) {
    if (!config) {
        return -EINVAL;
    }
    return gpio_pin_get(config->gpio_dev, config->pin);
}

int button_hal_configure(const button_hal_config_t *config, gpio_flags_t flags) {
    if (!config) {
        return -EINVAL;
    }
    return gpio_pin_configure(config->gpio_dev, config->pin, flags);
}