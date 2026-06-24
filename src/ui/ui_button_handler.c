#include "ui/ui_button_handler.h"
#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>
#include "hardware/button_hal.h"


#define BUTTON_PREV_PIN     9
#define BUTTON_NEXT_PIN     10
#define BUTTON_PRIMARY_PIN  8
#define BUTTON_SECONDARY_PIN 5

#define BUTTON_POLL_INTERVAL_MS 10
#define BUTTON_DEBOUNCE_MS 50

// Thread stack and thread structure
K_THREAD_STACK_DEFINE(g_btn_task_stack, 2048);
static struct k_thread g_btn_task_thread;

// Static button context
static ui_button_context_t *g_btn_ctx = NULL;

static void on_button_press(uint8_t pin, void *user_data)
{
    ui_button_context_t *ctx = (ui_button_context_t *)user_data;
    if (!ctx || !ctx->initialized || !ctx->display_ui) {
        printk("BUTTON: Invalid context!\n");
        return;
    }
    
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
            printk("Unknown pin!\n");
            return;
    }
    
    int ret = display_ui_handle_action(ctx->display_ui, action);
    if (ret < 0) {
        printk("UI action failed: %d\n", ret);
    } 
}

static void on_button_long_press(uint8_t pin, void *user_data)
{
    ui_button_context_t *ctx = (ui_button_context_t *)user_data;
    if (!ctx || !ctx->initialized || !ctx->display_ui) return;
    
    if (pin == BUTTON_SECONDARY_PIN) {
        int ret = display_ui_handle_action(ctx->display_ui, SCREEN_UI_ACTION_HOME);
        if (ret < 0) {
            printk("Home action failed: %d\n", ret);
        }
    }
}

// Button processing thread entry, waits for button events and processes them
static void button_processor_thread_entry(void *p1, void *p2, void *p3)
{
    (void)p1; (void)p2; (void)p3;
    
    while (1) {
        if (g_btn_ctx != NULL && g_btn_ctx->initialized) {
            ui_button_handler_process(g_btn_ctx);
        }

        k_sleep(K_MSEC(BUTTON_POLL_INTERVAL_MS));
    }
}

// Initialize buttons and set up callbacks
int ui_button_handler_init(ui_button_context_t *ctx, display_ui_t *display_ui)
{
    if (!ctx || !display_ui) {
        printk("ERROR: Invalid parameters for button init\n");
        return -EINVAL;
    }
    
    ctx->display_ui = display_ui;
    g_btn_ctx = ctx;
    
    // Get GPIO device
    const struct device *gpio_dev = DEVICE_DT_GET(DT_NODELABEL(gpio0));
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
    
    // Initialize each button with its config and callbacks
    for (int i = 0; i < NUM_BUTTONS; i++) {
        int ret = button_init(&ctx->buttons[i], &hal_configs[i]);
        if (ret < 0) {
            printk("ERROR: Failed to init button %d: %d\n", i, ret);
            return ret;
        }
        
        // Set callbacks and user data for each button
        ctx->buttons[i].callbacks = callbacks;
        ctx->buttons[i].user_data = ctx;
    }
    
    ctx->initialized = true;
    return 0;
}

// Starts the button processing thread
int ui_button_handler_start(void)
{
    if (g_btn_ctx == NULL || !g_btn_ctx->initialized) {
        printk("Button handler not initialized\n");
        return -EINVAL;
    }
    
    k_thread_create(&g_btn_task_thread, g_btn_task_stack,
                    K_THREAD_STACK_SIZEOF(g_btn_task_stack),
                    (k_thread_entry_t)button_processor_thread_entry,
                    NULL, NULL, NULL,
                    K_PRIO_PREEMPT(6), 0, K_NO_WAIT);
    
    return 0;
}

void ui_button_handler_process(ui_button_context_t *ctx)
{
    if (!ctx || !ctx->initialized) return;
    
    for (int i = 0; i < NUM_BUTTONS; i++) {
        int ret = button_update(&ctx->buttons[i]);
        if (ret < 0) {
            // Don't spam the log if it's just a read error
            if (ret != -EINVAL) {
                printk("Button update failed for button %d: %d\n", i, ret);
            }
        }
    }
}