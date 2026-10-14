#include "fake_gpio_counter.h"

#include "hal/gpio_counter.h"

#define FAKE_GPIO_COUNTER_MAX    2

static uint8_t fake_capacity;
static bool fake_routable[FAKE_GPIO_COUNTER_MAX];

void fake_gpio_counter_reset(void) {
    fake_capacity = FAKE_GPIO_COUNTER_MAX;
    for (uint8_t slot = 0; slot < FAKE_GPIO_COUNTER_MAX; slot++) {
        fake_routable[slot] = true;
    }
}

void fake_gpio_counter_set_capacity(uint8_t capacity) {
    fake_capacity = capacity;
}

void fake_gpio_counter_set_route(uint8_t slot, bool routable) {
    if (slot < FAKE_GPIO_COUNTER_MAX) {
        fake_routable[slot] = routable;
    }
}

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
    return fake_capacity;
}

bool hal_gpio_counter_can_route(uint8_t slot, hal_gpio_pin_t pin) {
    return slot < fake_capacity && slot < FAKE_GPIO_COUNTER_MAX &&
           pin != HAL_INVALID_PIN && fake_routable[slot];
}
