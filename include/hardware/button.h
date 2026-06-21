#ifndef BUTTON_H
#define BUTTON_H

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <stdbool.h>
#include "hardware/button_hal.h"

#define BUTTON_POLL_MS 50
#define BUTTON_HOME_HOLD_MS 700
#define GPIO_HIGH 1
#define GPIO_LOW 0

typedef enum {
    BUTTON_IDLE,
    BUTTON_PRESSED,
    BUTTON_LONG_PRESS_DETECTED,
    BUTTON_RELEASED
} button_state_t;

// Function pointer types for callbacks
typedef void (*button_press_callback_t)(uint8_t pin, void *user_data);
typedef void (*button_release_callback_t)(uint8_t pin, void *user_data);
typedef void (*button_long_press_callback_t)(uint8_t pin, void *user_data);

typedef struct {
    button_press_callback_t on_press;
    button_release_callback_t on_release;
    button_long_press_callback_t on_long_press;
} button_callbacks_t;

typedef struct {
    button_hal_config_t hal_config;
    button_state_t state;
    int last_level;
    int64_t press_start_time;
    bool long_press_handled;
    uint32_t long_press_threshold_ms;
    button_callbacks_t callbacks;
    void *user_data;
} button_t;

int button_init(button_t *btn, const button_hal_config_t *hal_config);
int button_update(button_t *btn);
void button_reset(button_t *btn);

#endif