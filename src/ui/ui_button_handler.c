#include "ui/ui_button_handler.h"
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(ui_button_handler, LOG_LEVEL_DBG);

#define BUTTON_PREV_PIN 2
#define BUTTON_PRIMARY_PIN 3
#define BUTTON_NEXT_PIN 4
#define BUTTON_SECONDARY_PIN 5

static void on_button_press(uint8_t pin, void *user_data)
{
    ui_button_context_t *ctx = (ui_button_context_t *)user_data;
    if (!ctx || !ctx->initialized || !ctx->display_ui) return;
    
    enum screen_ui_action action;
    switch (pin) {
        case BUTTON_PREV_PIN:
            action = SCREEN_UI_ACTION_PREVIOUS;
            break;
        case BUTTON_NEXT_PIN:
            action = SCREEN_UI_ACTION_NEXT;
            break;
        case BUTTON_PRIMARY_PIN:
            action = SCREEN_UI_ACTION_PRIMARY;
            break;
        case BUTTON_SECONDARY_PIN:
            action = SCREEN_UI_ACTION_SECONDARY;
            break;
        default:
            return;
    }
    
    int ret = display_ui_handle_action(ctx->display_ui, action);
    if (ret < 0) {
        LOG_ERR("UI action failed: %d", ret);
    }
}

static void on_button_long_press(uint8_t pin, void *user_data)
{
    ui_button_context_t *ctx = (ui_button_context_t *)user_data;
    if (!ctx || !ctx->initialized || !ctx->display_ui) return;
    
    if (pin == BUTTON_SECONDARY_PIN) {
        int ret = display_ui_handle_action(ctx->display_ui, SCREEN_UI_ACTION_HOME);
        if (ret < 0) {
            LOG_ERR("Home action failed: %d", ret);
        }
    }
}

int ui_button_handler_init(ui_button_context_t *ctx, display_ui_t *display_ui)
{
    if (!ctx || !display_ui) return -EINVAL;
    
    ctx->display_ui = display_ui;
    
    const struct device *gpio_dev = device_get_binding("GPIO_0");
    if (!gpio_dev) {
        LOG_ERR("Failed to get GPIO device");
        return -ENODEV;
    }
    
    // Button configuration
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
        
        // Set button callbacks
        button_callbacks_t callbacks = {
            .on_press = on_button_press,
            .on_release = NULL,
            .on_long_press = on_button_long_press,
        };
        ctx->buttons[i].callbacks = callbacks;
        ctx->buttons[i].user_data = ctx;
    }
    
    ctx->initialized = true;
    LOG_INF("Button handler initialized");
    return 0;
}

void ui_button_handler_process(ui_button_context_t *ctx)
{
    if (!ctx || !ctx->initialized) return;
    
    for (int i = 0; i < 4; i++) {
        button_update(&ctx->buttons[i]);
    }
}