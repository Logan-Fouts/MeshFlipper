#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>
#include "ui/buttons.h"
#include "ui/screen_ui.h"

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
        buttons[i].prev_press_time = 0;
        buttons[i].long_press_handled = false;
    }

    return 0;
}

bool process_button_state(struct button_state *button, bool *falling_edge, bool *rising_edge, bool *is_secondary, const struct device *gpio_dev, struct messageHistory *message_history, struct nodeHistory *node_list) 
{
    int level = gpio_pin_get(gpio_dev, button->pin);

    // If we fail to read the button state, skip processing for this button.
    if (level < 0) {
        return false;
    }

    *falling_edge = (button->last_level == GPIO_HIGH && level == GPIO_LOW); // Button pressed
    *rising_edge = (button->last_level == GPIO_LOW && level == GPIO_HIGH); // Button released
    *is_secondary = (button->pin == BUTTON_SECONDARY_PIN);

    if (*falling_edge) {
        button->prev_press_time = k_uptime_get();
        button->long_press_handled = false;
    }

    // Check for long press on secondary button to trigger home action
    if (*is_secondary && level == GPIO_LOW && !button->long_press_handled &&
        button->prev_press_time > 0 && (k_uptime_get() - button->prev_press_time) >= BUTTON_HOME_HOLD_MS) {

        int ui_ret = screen_ui_handle_action(message_history, node_list, SCREEN_UI_ACTION_HOME);

        if (ui_ret < 0) {
            printk("UI home action failed: %d\n", ui_ret);
        }

        button->long_press_handled = true;
    }
    button->last_level = level;

    if (*rising_edge) {
        button->prev_press_time = 0;
    }

    return true;
}