#include "ui/ui_button_handler.h"
// #include "ui/screen_ui.h"
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(ui_button, LOG_LEVEL_DBG);

/* Pin definitions */
#define BUTTON_PREV_PIN 2
#define BUTTON_PRIMARY_PIN 3
#define BUTTON_NEXT_PIN 4
#define BUTTON_SECONDARY_PIN 5

static void on_button_press(uint8_t pin) {
    LOG_DBG("Button %d pressed", pin);
}

static void on_button_release(uint8_t pin) {
    LOG_DBG("Button %d released", pin);
}

static void on_button_long_press(uint8_t pin) {
    LOG_DBG("Button %d long pressed", pin);
    // Handle long press actions here
}

static button_callbacks_t callbacks = {
    .on_press = on_button_press,
    .on_release = on_button_release,
    .on_long_press = on_button_long_press,
};

int ui_button_handler_init(ui_button_context_t *ctx) {
    if (!ctx) return -EINVAL;
    
    /* Get GPIO device - you might want to pass this in */
    const struct device *gpio_dev = device_get_binding("GPIO_0");
    if (!gpio_dev) {
        LOG_ERR("Failed to get GPIO device");
        return -ENODEV;
    }
    
    /* Initialize each button */
    button_hal_config_t hal_configs[] = {
        {.pin = BUTTON_PREV_PIN, .gpio_dev = gpio_dev},
        {.pin = BUTTON_PRIMARY_PIN, .gpio_dev = gpio_dev},
        {.pin = BUTTON_NEXT_PIN, .gpio_dev = gpio_dev},
        {.pin = BUTTON_SECONDARY_PIN, .gpio_dev = gpio_dev},
    };
    
    for (int i = 0; i < 4; i++) {
        int ret = button_init(&ctx->buttons[i], &hal_configs[i]);
        if (ret < 0) {
            LOG_ERR("Failed to init button %d: %d", i, ret);
            return ret;
        }
    }
    
    return 0;
}

void ui_button_handler_process(ui_button_context_t *ctx) {
    if (!ctx) return;
    
    for (int i = 0; i < 4; i++) {
        button_update(&ctx->buttons[i], &callbacks);
    }
}