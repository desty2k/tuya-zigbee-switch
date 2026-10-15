#ifndef _HAL_GPIO_PIN_TEXT_H_
#define _HAL_GPIO_PIN_TEXT_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/*
 * Splits a pin name of the form "<port letter><index>" ("C2", "B7") into a
 * zero-based port number (A = 0) and a pin index of 0..15. The whole string
 * must be the name: lowercase ports, missing or 3+ digit indices and trailing
 * characters are rejected. Platform HALs narrow the ranges to the pins they
 * have and apply their own encoding.
 */
static inline uint8_t hal_gpio_pin_text_digit(char c) {
    return (uint8_t)(c - '0'); // 10..255 for anything that is not a digit
}

static inline bool hal_gpio_pin_text_split(const char *s, uint8_t *port,
                                           uint8_t *index) {
    uint8_t value;

    if (s == NULL || (uint8_t)(s[0] - 'A') > 'Z' - 'A' ||
        hal_gpio_pin_text_digit(s[1]) > 9) {
        return false;
    }
    value = hal_gpio_pin_text_digit(s[1]);
    if (hal_gpio_pin_text_digit(s[2]) <= 9) {
        value = (uint8_t)(value * 10U + hal_gpio_pin_text_digit(s[2]));
        if (s[3] != '\0') {
            return false;
        }
    } else if (s[2] != '\0') {
        return false;
    }
    if (value > 15) {
        return false;
    }
    *port  = (uint8_t)(s[0] - 'A');
    *index = value;
    return true;
}

#endif
