#ifndef BUTTONS_H
#define BUTTONS_H
#define BUTTON_PREV_PIN 2
#define BUTTON_PRIMARY_PIN 3
#define BUTTON_NEXT_PIN 4
#define BUTTON_SECONDARY_PIN 5
#define BUTTON_POLL_MS 35
#define BUTTON_HOME_HOLD_MS 700


struct button_state {
    uint8_t pin;
    int last_level;
    int64_t prev_press_time;
    bool long_press_handled;
};

int configure_button_inputs(struct button_state *buttons, const struct device *gpio_dev, int num_buttons);

#endif