#ifndef _SILABS_GPIO_PIN_H_
#define _SILABS_GPIO_PIN_H_

#include "hal/gpio.h"
#include "hal/gpio_pin_text.h"

/*
 * EFR32 series 2 pin ids: GPIO_Port_TypeDef (gpioPortA..D = 0..3) in the upper
 * byte, pin number 0..15 in the lower byte (see silabs_gpio_utils.h). gpio.c
 * checks the port values against emlib at compile time; this header stays
 * SDK-free so host tests can use it.
 */
#define SILABS_GPIO_PORT_COUNT    4

static inline hal_gpio_pin_t silabs_gpio_pin_from_text(const char *s) {
    uint8_t port;
    uint8_t index;

    if (!hal_gpio_pin_text_split(s, &port, &index) ||
        port >= SILABS_GPIO_PORT_COUNT) {
        return HAL_INVALID_PIN;
    }
    return (hal_gpio_pin_t)(((uint16_t)port << 8) | index);
}

#endif
