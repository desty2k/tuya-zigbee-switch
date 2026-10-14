#ifndef HAL_GPIO_COUNTER_H_
#define HAL_GPIO_COUNTER_H_

#include "hal/gpio.h"
#include <stdbool.h>
#include <stdint.h>

typedef int8_t hal_gpio_counter_id_t;

#define HAL_GPIO_COUNTER_INVALID_ID    ((hal_gpio_counter_id_t)-1)

typedef enum {
    HAL_GPIO_COUNTER_EDGE_RISING,
    HAL_GPIO_COUNTER_EDGE_FALLING,
} hal_gpio_counter_edge_t;

typedef struct {
    uint32_t total;
    uint32_t timestamp_ms;
    uint32_t overflow_count;
    uint32_t error_count;
    uint8_t  running;
} hal_gpio_counter_snapshot_t;

hal_gpio_counter_id_t hal_gpio_counter_open(hal_gpio_pin_t pin,
                                            hal_gpio_counter_edge_t edge,
                                            hal_gpio_pull_t pull);
bool hal_gpio_counter_snapshot(hal_gpio_counter_id_t id,
                               hal_gpio_counter_snapshot_t *out);
void hal_gpio_counter_close(hal_gpio_counter_id_t id);
uint8_t hal_gpio_counter_capacity(void);
bool hal_gpio_counter_can_route(uint8_t slot, hal_gpio_pin_t pin);

#endif /* HAL_GPIO_COUNTER_H_ */
