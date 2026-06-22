#include "hardware/button_hal.h"

// Initialize the button hardware (GPIO pin)
int button_hal_init(const button_hal_config_t *config) {
    if (!config) {
        printk("button_hal_init: Invalid config\n");
        return -EINVAL;
    }
    
    if (!device_is_ready(config->gpio_dev)) {
        printk("button_hal_init: GPIO device not ready\n");
        return -ENODEV;
    }
    return 0;
}

// Read the current state of the button (0 or 1)
int button_hal_read(const button_hal_config_t *config) {
    if (!config) {
        return -EINVAL;
    }
    return gpio_pin_get(config->gpio_dev, config->pin);
}

// Configure the button hardware (GPIO pin)
int button_hal_configure(const button_hal_config_t *config, gpio_flags_t flags) {
    if (!config) {
        return -EINVAL;
    }
    return gpio_pin_configure(config->gpio_dev, config->pin, flags);
}