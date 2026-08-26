#ifndef _TEST_FAKE_GPIO_H_
#define _TEST_FAKE_GPIO_H_

#include "hal/gpio.h"
#include <stdint.h>

void fake_gpio_reset(void);
void fake_gpio_set_input(hal_gpio_pin_t pin, uint8_t level);
void fake_gpio_inject_edge(hal_gpio_pin_t pin, uint8_t level,
                           uint32_t timestamp_ms);

#endif
