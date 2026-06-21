#include "hardware/button.h"
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(button, LOG_LEVEL_DBG);

int button_init(button_t *btn, const button_hal_config_t *hal_config) {
    if (!btn || !hal_config) return -EINVAL;
    
    btn->hal_config = *hal_config;
    btn->state = BUTTON_IDLE;
    btn->long_press_threshold_ms = BUTTON_HOME_HOLD_MS;
    btn->long_press_handled = false;
    btn->press_start_time = 0;
    btn->callbacks.on_press = NULL;
    btn->callbacks.on_release = NULL;
    btn->callbacks.on_long_press = NULL;
    btn->user_data = NULL;
    
    int ret = button_hal_init(&btn->hal_config);
    if (ret < 0) return ret;
    
    ret = button_hal_configure(&btn->hal_config, GPIO_INPUT | GPIO_PULL_UP);
    if (ret < 0) return ret;
    
    btn->last_level = button_hal_read(&btn->hal_config);
    return 0;
}

int button_update(button_t *btn) {
    if (!btn) return -EINVAL;
    
    int current_level = button_hal_read(&btn->hal_config);
    if (current_level < 0) return current_level;
    
    bool falling_edge = (btn->last_level == GPIO_HIGH && current_level == GPIO_LOW);
    bool rising_edge = (btn->last_level == GPIO_LOW && current_level == GPIO_HIGH);
    
    // Handle state transitions
    if (falling_edge) {
        btn->state = BUTTON_PRESSED;
        btn->press_start_time = k_uptime_get();
        btn->long_press_handled = false;
        
        if (btn->callbacks.on_press) {
            btn->callbacks.on_press(btn->hal_config.pin, btn->user_data);
        }
    }
    
    // Long press detection
    if (btn->state == BUTTON_PRESSED && !btn->long_press_handled) {
        if ((k_uptime_get() - btn->press_start_time) >= btn->long_press_threshold_ms) {
            btn->state = BUTTON_LONG_PRESS_DETECTED;
            btn->long_press_handled = true;
            
            if (btn->callbacks.on_long_press) {
                btn->callbacks.on_long_press(btn->hal_config.pin, btn->user_data);
            }
        }
    }
    
    if (rising_edge) {
        btn->state = BUTTON_RELEASED;
        btn->press_start_time = 0;
        
        if (btn->callbacks.on_release) {
            btn->callbacks.on_release(btn->hal_config.pin, btn->user_data);
        }
    }
    
    btn->last_level = current_level;
    return 0;
}

void button_reset(button_t *btn) {
    if (btn) {
        btn->state = BUTTON_IDLE;
        btn->long_press_handled = false;
        btn->press_start_time = 0;
    }
}