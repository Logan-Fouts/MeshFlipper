#ifndef BUTTON_H
#define BUTTON_H

#include <zephyr/kernel.h>
#include <stdbool.h>
#include "hardware/button_hal.h"

#define BUTTON_POLL_MS 50
#define BUTTON_HOME_HOLD_MS 700

typedef enum {
    BUTTON_IDLE,
    BUTTON_PRESSED,
    BUTTON_LONG_PRESS_DETECTED,
    BUTTON_RELEASED
} button_state_t;

typedef struct {
    button_hal_config_t hal_config;
    button_state_t state;
    int last_level;
    int64_t press_start_time;
    bool long_press_handled;
    uint32_t long_press_threshold_ms;
} button_t;

/* Function pointers for callbacks - allows decoupling from application */
typedef struct {
    void (*on_press)(uint8_t pin);
    void (*on_release)(uint8_t pin);
    void (*on_long_press)(uint8_t pin);
} button_callbacks_t;

int button_init(button_t *btn, const button_hal_config_t *hal_config);
int button_update(button_t *btn, button_callbacks_t *callbacks);
void button_reset(button_t *btn);

#endif