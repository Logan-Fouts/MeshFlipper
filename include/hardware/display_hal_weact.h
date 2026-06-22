#ifndef DISPLAY_HAL_WEACT_H
#define DISPLAY_HAL_WEACT_H

#include "hardware/display_hal.h"

/* WeAct 1.54in E-Paper configuration for Raspberry Pi Pico */
#define WEACT_CS_PIN   17
#define WEACT_DC_PIN   20
#define WEACT_RST_PIN  21
#define WEACT_BUSY_PIN 22
#define WEACT_SPI_FREQUENCY 4000000

display_hal_config_t weact_display_get_config(void);

#endif