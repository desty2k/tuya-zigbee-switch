#ifndef STUB_HAL_GPIO_COUNTER_H_
#define STUB_HAL_GPIO_COUNTER_H_

#include "hal/gpio.h"
#include <stdint.h>

void stub_gpio_counter_capture_edge(hal_gpio_pin_t pin, uint8_t previous_level,
                                    uint8_t level, uint32_t timestamp_ms);

#endif /* STUB_HAL_GPIO_COUNTER_H_ */
