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

/*
 * Pulse generators model meter IC outputs in virtual time: `rate_mhz` pulses
 * per 1000 s on `pin` while `gate_pin` (HAL_INVALID_PIN: always) is at
 * `gate_level`. Generated pulses reach only the counters, never the GPIO edge
 * pipeline, as on hardware where counter pins are not watched.
 */
#define STUB_GPIO_GENERATORS    4

typedef struct {
    hal_gpio_pin_t pin;
    hal_gpio_pin_t gate_pin;
    uint8_t        gate_level;
    uint32_t       rate_mhz;
    uint64_t       accumulated; /* mHz * ms below one pulse */
} stub_pulse_generator_t;

static stub_pulse_generator_t generators[STUB_GPIO_GENERATORS];
static uint32_t generated_until_ms;

static void count_pulses(hal_gpio_pin_t pin, uint32_t pulses, uint32_t timestamp_ms) {
    for (uint8_t slot = 0; slot < STUB_GPIO_COUNTER_MAX; slot++) {
        stub_gpio_counter_t *counter = &counters[slot];
        uint32_t             before  = counter->snapshot.total;

        if (!counter->snapshot.running || counter->pin != pin || pulses == 0) {
            continue;
        }
        counter->snapshot.total += pulses;
        if (counter->snapshot.total < before) {
            counter->snapshot.overflow_count++;
        }
        counter->snapshot.timestamp_ms = timestamp_ms;
    }
}

void stub_gpio_counter_advance(void) {
    uint32_t now     = hal_millis();
    uint32_t elapsed = now - generated_until_ms;

    generated_until_ms = now;
    for (uint8_t i = 0; i < STUB_GPIO_GENERATORS; i++) {
        stub_pulse_generator_t *generator = &generators[i];

        if (generator->rate_mhz == 0 ||
            (generator->gate_pin != HAL_INVALID_PIN &&
             stub_gpio_get_output(generator->gate_pin) != generator->gate_level)) {
            continue;
        }
        generator->accumulated += (uint64_t)generator->rate_mhz * elapsed;
        count_pulses(generator->pin, (uint32_t)(generator->accumulated / 1000000u), now);
        generator->accumulated %= 1000000u;
    }
}

void stub_gpio_counter_reset(void) {
    memset(counters, 0, sizeof(counters));
    memset(generators, 0, sizeof(generators));
    generated_until_ms = hal_millis();
}

bool stub_gpio_counter_set_rate(hal_gpio_pin_t pin, uint32_t rate_mhz,
                                hal_gpio_pin_t gate_pin, uint8_t gate_level) {
    stub_pulse_generator_t *free_slot = NULL;

    stub_gpio_counter_advance();
    for (uint8_t i = 0; i < STUB_GPIO_GENERATORS; i++) {
        stub_pulse_generator_t *generator = &generators[i];

        if (generator->rate_mhz != 0 && generator->pin == pin &&
            generator->gate_pin == gate_pin && generator->gate_level == gate_level) {
            generator->rate_mhz = rate_mhz;
            return true;
        }
        if (generator->rate_mhz == 0 && free_slot == NULL) {
            free_slot = generator;
        }
    }
    if (rate_mhz == 0) {
        return true;
    }
    if (free_slot == NULL) {
        return false;
    }
    *free_slot = (stub_pulse_generator_t){
        .pin        = pin,
        .gate_pin   = gate_pin,
        .gate_level = gate_level,
        .rate_mhz   = rate_mhz,
    };
    return true;
}

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
    stub_gpio_counter_advance();
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
