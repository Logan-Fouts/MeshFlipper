#ifndef HARDWARE_H
#define HARDWARE_H

#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/uart.h>

// LED
extern const struct gpio_dt_spec led;

// GPIO device
extern const struct device *gpio_dev;

#endif // HARDWARE_H