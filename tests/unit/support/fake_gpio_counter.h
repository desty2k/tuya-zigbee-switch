#ifndef TEST_FAKE_GPIO_COUNTER_H_
#define TEST_FAKE_GPIO_COUNTER_H_

#include <stdbool.h>
#include <stdint.h>

void fake_gpio_counter_reset(void);
void fake_gpio_counter_set_capacity(uint8_t capacity);
void fake_gpio_counter_set_route(uint8_t slot, bool routable);

#endif /* TEST_FAKE_GPIO_COUNTER_H_ */
