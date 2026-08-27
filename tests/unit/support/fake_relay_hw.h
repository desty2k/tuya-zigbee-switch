#ifndef _TEST_FAKE_RELAY_HW_H_
#define _TEST_FAKE_RELAY_HW_H_

#include "hal/gpio.h"
#include <stddef.h>
#include <stdint.h>

typedef struct {
    hal_gpio_pin_t pin;
    uint8_t        level;
    uint32_t       timestamp_ms;
} fake_relay_hw_write_t;

void fake_relay_hw_reset(void);
void fake_relay_hw_clear_log(void);
uint8_t fake_relay_hw_level(hal_gpio_pin_t pin);
size_t fake_relay_hw_write_count(void);
const fake_relay_hw_write_t *fake_relay_hw_write_at(size_t index);

#endif
