#include "hal/gpio_counter.h"

/* Profiles requiring counters are rejected until a real routed implementation exists. */
hal_gpio_counter_id_t hal_gpio_counter_open(hal_gpio_pin_t pin,
                                            hal_gpio_counter_edge_t edge,
                                            hal_gpio_pull_t pull) {
    (void)pin;
    (void)edge;
    (void)pull;
    return HAL_GPIO_COUNTER_INVALID_ID;
}

bool hal_gpio_counter_snapshot(hal_gpio_counter_id_t id,
                               hal_gpio_counter_snapshot_t *out) {
    (void)id;
    (void)out;
    return false;
}

void hal_gpio_counter_close(hal_gpio_counter_id_t id) {
    (void)id;
}

uint8_t hal_gpio_counter_capacity(void) {
    return 0;
}

bool hal_gpio_counter_can_route(uint8_t slot, hal_gpio_pin_t pin) {
    (void)slot;
    (void)pin;
    return false;
}
