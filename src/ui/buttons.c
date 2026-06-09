#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>
#include "ui/buttons.h"

int configure_button_inputs(struct button_state *buttons, const struct device *gpio_dev, int num_buttons)
{
    if (!device_is_ready(gpio_dev)) {
        printk("Error: GPIO device is not ready\n");
        return -ENODEV;
    }

    for (size_t i = 0; i < num_buttons; i++) {
        int ret = gpio_pin_configure(gpio_dev, buttons[i].pin, GPIO_INPUT | GPIO_PULL_UP);
        if (ret < 0) {
            printk("Error: failed to configure GPIO%u (%d)\n", buttons[i].pin, ret);
            return ret;
        }

        buttons[i].last_level = gpio_pin_get(gpio_dev, buttons[i].pin);
        buttons[i].pressed_since_ms = 0;
        buttons[i].long_press_handled = false;
    }

    return 0;
}