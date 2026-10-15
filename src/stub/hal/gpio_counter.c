#include "hal/gpio_counter.h"
#include "hal/timer.h"
#include "stub/hal/gpio_counter.h"
#include "stub/hal/stub.h"

#include <string.h>

#define STUB_GPIO_COUNTER_MAX    2

typedef struct {
    hal_gpio_pin_t              pin;
    hal_gpio_counter_edge_t     edge;
    hal_gpio_counter_snapshot_t snapshot;
} stub_gpio_counter_t;

static stub_gpio_counter_t counters[STUB_GPIO_COUNTER_MAX];

static bool valid_id(hal_gpio_counter_id_t id) {
    return id >= 0 && id < STUB_GPIO_COUNTER_MAX && counters[id].snapshot.running;
}

hal_gpio_counter_id_t hal_gpio_counter_open(hal_gpio_pin_t pin,
                                            hal_gpio_counter_edge_t edge,
                                            hal_gpio_pull_t pull) {
    if (pin == HAL_INVALID_PIN ||
        (edge != HAL_GPIO_COUNTER_EDGE_RISING && edge != HAL_GPIO_COUNTER_EDGE_FALLING)) {
        return HAL_GPIO_COUNTER_INVALID_ID;
    }
    for (uint8_t slot = 0; slot < STUB_GPIO_COUNTER_MAX; slot++) {
        if (!counters[slot].snapshot.running) {
            counters[slot] = (stub_gpio_counter_t){
                .pin      = pin,
                .edge     = edge,
                .snapshot = {
                    .timestamp_ms = hal_millis(),
                    .running      =            1,
                },
            };
            hal_gpio_init(pin, 1, pull);
            return (hal_gpio_counter_id_t)slot;
        }
    }
    return HAL_GPIO_COUNTER_INVALID_ID;
}

bool hal_gpio_counter_snapshot(hal_gpio_counter_id_t id,
                               hal_gpio_counter_snapshot_t *out) {
    if (!valid_id(id) || out == NULL) {
        return false;
    }
    *out = counters[id].snapshot;
    return true;
}

void hal_gpio_counter_close(hal_gpio_counter_id_t id) {
    if (valid_id(id)) {
        memset(&counters[id], 0, sizeof(counters[id]));
    }
}

uint8_t hal_gpio_counter_capacity(void) {
    return STUB_GPIO_COUNTER_MAX;
}

bool hal_gpio_counter_can_route(uint8_t slot, hal_gpio_pin_t pin) {
    return slot < STUB_GPIO_COUNTER_MAX && pin != HAL_INVALID_PIN && pin < STUB_GPIO_PIN_LIMIT;
}

void stub_gpio_counter_capture_edge(hal_gpio_pin_t pin, uint8_t previous_level,
                                    uint8_t level, uint32_t timestamp_ms) {
    for (uint8_t slot = 0; slot < STUB_GPIO_COUNTER_MAX; slot++) {
        stub_gpio_counter_t *counter = &counters[slot];
        bool rising  = previous_level == 0 && level != 0;
        bool falling = previous_level != 0 && level == 0;

        if (!counter->snapshot.running || counter->pin != pin ||
            !((counter->edge == HAL_GPIO_COUNTER_EDGE_RISING && rising) ||
              (counter->edge == HAL_GPIO_COUNTER_EDGE_FALLING && falling))) {
            continue;
        }
        counter->snapshot.total++;
        if (counter->snapshot.total == 0) {
            counter->snapshot.overflow_count++;
        }
        counter->snapshot.timestamp_ms = timestamp_ms;
    }
}
