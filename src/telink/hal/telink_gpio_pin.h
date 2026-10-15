#ifndef _TELINK_GPIO_PIN_H_
#define _TELINK_GPIO_PIN_H_

#include "hal/gpio.h"
#include "hal/gpio_pin_text.h"

/*
 * TLSR825x pin ids are GPIO_PinTypeDef values: GPIO_GROUPx (port A..E at
 * 0x000..0x400) ORed with the pin's bit in its 8-bit port. gpio.c checks these
 * constants against the SDK at compile time; this header stays SDK-free so
 * host tests can use it.
 */
#define TELINK_GPIO_PORT_COUNT    5
#define TELINK_GPIO_PORT_PINS     8

static inline hal_gpio_pin_t telink_gpio_pin_from_text(const char *s) {
    uint8_t port;
    uint8_t index;

    if (!hal_gpio_pin_text_split(s, &port, &index) ||
        port >= TELINK_GPIO_PORT_COUNT || index >= TELINK_GPIO_PORT_PINS) {
        return HAL_INVALID_PIN;
    }
    return (hal_gpio_pin_t)(((uint16_t)port << 8) | (1U << index));
}

#endif
