#ifndef TEST_FAKE_GPIO_PARSE_H_
#define TEST_FAKE_GPIO_PARSE_H_

#include "hal/gpio.h"

/*
 * Pin id the fake hal_gpio_parse_pin() returns for a name. It differs from any
 * plain port/index packing so tests see whether the parser used the HAL.
 * Port Z stands for a port the chip lacks and is rejected.
 */
#define FAKE_GPIO_PIN(port, index) \
        ((hal_gpio_pin_t)(0x4000U | ((unsigned)((port) - 'A') << 4) | (index)))

#endif /* TEST_FAKE_GPIO_PARSE_H_ */
