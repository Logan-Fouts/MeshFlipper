#ifndef BUTTONS_H

#include <zephyr/drivers/gpio.h>
#include <zephyr/device.h>
#include <zephyr/kernel.h>
#include "models/node.h"
#include "models/message.h"

#define BUTTONS_H
#define BUTTON_PREV_PIN 2
#define BUTTON_PRIMARY_PIN 3
#define BUTTON_NEXT_PIN 4
#define BUTTON_SECONDARY_PIN 5
#define BUTTON_POLL_MS 50
#define BUTTON_HOME_HOLD_MS 700
#define GPIO_HIGH 1
#define GPIO_LOW 0

struct button_state {
    uint8_t pin;
    int last_level;
    int64_t prev_press_time;
    bool long_press_handled;
};

int configure_button_inputs(struct button_state *buttons, const struct device *gpio_dev, int num_buttons);
bool process_button_state(struct button_state *button, bool *falling_edge, bool *rising_edge, bool *is_secondary, const struct device *gpio_dev, struct messageHistory *message_history, struct nodeHistory *node_list);

#endif