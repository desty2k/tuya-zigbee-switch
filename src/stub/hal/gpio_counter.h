#ifndef STUB_HAL_GPIO_COUNTER_H_
#define STUB_HAL_GPIO_COUNTER_H_

#include "hal/gpio.h"
#include <stdbool.h>
#include <stdint.h>

/* Close every counter and remove every generator. */
void stub_gpio_counter_reset(void);

/* Count the generated pulses up to now; call before a gate pin changes. */
void stub_gpio_counter_advance(void);

/* Rate 0 removes the generator; false when all generator slots are taken. */
bool stub_gpio_counter_set_rate(hal_gpio_pin_t pin, uint32_t rate_mhz,
                                hal_gpio_pin_t gate_pin, uint8_t gate_level);
void stub_gpio_counter_capture_edge(hal_gpio_pin_t pin, uint8_t previous_level,
                                    uint8_t level, uint32_t timestamp_ms);

#endif /* STUB_HAL_GPIO_COUNTER_H_ */
