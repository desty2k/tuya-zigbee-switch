#ifndef _TELINK_GPIO_PIN_H_
#define _TELINK_GPIO_PIN_H_

#include "hal/gpio.h"
#include "hal/gpio_pin_text.h"

/*
 * TLSR825x pin ids are GPIO_PinTypeDef values: GPIO_GROUPx (port A..D at
 * 0x000..0x300) ORed with the pin's bit in its 8-bit port. Port E is not
 * accepted: PE0..PE3 carry the in-package flash SPI, so configuring one of
 * them as a feature pin breaks code fetch and NVM on every boot, and
 * PE4..PE7 do not exist. gpio.c checks these constants against the SDK at
 * compile time; this header stays SDK-free so host tests can use it.
 */
#define TELINK_GPIO_PORT_COUNT    4
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

/*
 * Pins hal_gpio_rest_all() holds on the pull-down, which are also the GPIO
 * probe's table; returns the count and points *pins at them.
 */
uint8_t telink_gpio_rest_pins(const hal_gpio_pin_t **pins);

/* Pin index 0..7 of a pin id; the id carries exactly one bit of its port. */
static inline uint8_t telink_gpio_pin_index(hal_gpio_pin_t pin) {
    uint8_t mask  = (uint8_t)(pin & 0xFFu);
    uint8_t index = 0;

    while (mask > 1u) {
        mask >>= 1;
        index++;
    }
    return index;
}

#endif
