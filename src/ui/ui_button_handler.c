#include "ui/ui_button_handler.h"
#include <zephyr/logging/log.h>
#include <zephyr/drivers/gpio.h>

LOG_MODULE_REGISTER(ui_button_handler, LOG_LEVEL_INF);

// Button pins - adjust these to match your actual wiring
#define BUTTON_PREV_PIN     2
#define BUTTON_NEXT_PIN     4
#define BUTTON_PRIMARY_PIN  3
#define BUTTON_SECONDARY_PIN 5

// Direct GPIO device for testing
static const struct device *gpio_dev = NULL;
static ui_button_context_t *g_ctx = NULL;

static void on_button_press(uint8_t pin, void *user_data)
{
    ui_button_context_t *ctx = (ui_button_context_t *)user_data;
    if (!ctx || !ctx->initialized || !ctx->display_ui) {
        printk("BUTTON: Invalid context!\n");
        return;
    }
    
    printk("BUTTON PRESS: pin %d\n", pin);
    
    enum screen_ui_action action;
    switch (pin) {
        case BUTTON_PREV_PIN:
            action = SCREEN_UI_ACTION_PREVIOUS;
            printk("  Action: PREVIOUS\n");
            break;
        case BUTTON_NEXT_PIN:
            action = SCREEN_UI_ACTION_NEXT;
            printk("  Action: NEXT\n");
            break;
        case BUTTON_PRIMARY_PIN:
            action = SCREEN_UI_ACTION_PRIMARY;
            printk("  Action: PRIMARY\n");
            break;
        case BUTTON_SECONDARY_PIN:
            action = SCREEN_UI_ACTION_SECONDARY;
            printk("  Action: SECONDARY\n");
            break;
        default:
            printk("  Unknown pin!\n");
            return;
    }
    
    int ret = display_ui_handle_action(ctx->display_ui, action);
    if (ret < 0) {
        printk("UI action failed: %d\n", ret);
    } else {
        printk("UI action completed\n");
    }
}

static void on_button_long_press(uint8_t pin, void *user_data)
{
    ui_button_context_t *ctx = (ui_button_context_t *)user_data;
    if (!ctx || !ctx->initialized || !ctx->display_ui) return;
    
    printk("BUTTON LONG PRESS: pin %d\n", pin);
    
    if (pin == BUTTON_SECONDARY_PIN) {
        printk("  Action: HOME\n");
        int ret = display_ui_handle_action(ctx->display_ui, SCREEN_UI_ACTION_HOME);
        if (ret < 0) {
            printk("Home action failed: %d\n", ret);
        }
    }
}

int ui_button_handler_init(ui_button_context_t *ctx, display_ui_t *display_ui)
{
    if (!ctx || !display_ui) {
        printk("ERROR: Invalid parameters for button init\n");
        return -EINVAL;
    }
    
    ctx->display_ui = display_ui;
    g_ctx = ctx;
    
    gpio_dev = DEVICE_DT_GET(DT_NODELABEL(gpio0));
    if (!device_is_ready(gpio_dev)) {
        printk("ERROR: GPIO device not ready!\n");
        return -ENODEV;
    }
    
    // Button configuration
    button_hal_config_t hal_configs[] = {
        {.pin = BUTTON_PREV_PIN, .gpio_dev = gpio_dev},
        {.pin = BUTTON_PRIMARY_PIN, .gpio_dev = gpio_dev},
        {.pin = BUTTON_NEXT_PIN, .gpio_dev = gpio_dev},
        {.pin = BUTTON_SECONDARY_PIN, .gpio_dev = gpio_dev},
    };
    
    button_callbacks_t callbacks = {
        .on_press = on_button_press,
        .on_release = NULL,
        .on_long_press = on_button_long_press,
    };
    
    for (int i = 0; i < 4; i++) {
        int ret = button_init(&ctx->buttons[i], &hal_configs[i]);
        if (ret < 0) {
            printk("ERROR: Failed to init button %d: %d\n", i, ret);
            return ret;
        }
        
        ctx->buttons[i].callbacks = callbacks;
        ctx->buttons[i].user_data = ctx;
        ctx->buttons[i].long_press_threshold_ms = 2000; // TODO: define in common header
        
        // Read initial state
        int initial = button_hal_read(&hal_configs[i]);
    }
    
    ctx->initialized = true;
    return 0;
}

void ui_button_handler_process(ui_button_context_t *ctx)
{
    if (!ctx || !ctx->initialized) return;
    
    for (int i = 0; i < 4; i++) {
        int ret = button_update(&ctx->buttons[i]);
        if (ret < 0) {
            // Don't spam the log if it's just a read error
            if (ret != -EINVAL) {
                printk("Button update failed for button %d: %d\n", i, ret);
            }
        }
    }
}