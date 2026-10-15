#include "stub/hal/gpio_probe.h"
#include "hal/timer.h"

#include <string.h>

/*
 * The probe table mirrors the TLSR8258 of the outlets the probe is built for
 * (src/telink/hal/gpio_probe.c): ports A-D, PA7 (SWS) left out.
 */
static const hal_gpio_probe_id_t probe_pins[] = {
    HAL_GPIO_PROBE_ID(0, 0), HAL_GPIO_PROBE_ID(0, 1), HAL_GPIO_PROBE_ID(0, 2),
    HAL_GPIO_PROBE_ID(0, 3), HAL_GPIO_PROBE_ID(0, 4), HAL_GPIO_PROBE_ID(0, 5),
    HAL_GPIO_PROBE_ID(0, 6),
    HAL_GPIO_PROBE_ID(1, 0), HAL_GPIO_PROBE_ID(1, 1), HAL_GPIO_PROBE_ID(1, 2),
    HAL_GPIO_PROBE_ID(1, 3), HAL_GPIO_PROBE_ID(1, 4), HAL_GPIO_PROBE_ID(1, 5),
    HAL_GPIO_PROBE_ID(1, 6), HAL_GPIO_PROBE_ID(1, 7),
    HAL_GPIO_PROBE_ID(2, 0), HAL_GPIO_PROBE_ID(2, 1), HAL_GPIO_PROBE_ID(2, 2),
    HAL_GPIO_PROBE_ID(2, 3), HAL_GPIO_PROBE_ID(2, 4), HAL_GPIO_PROBE_ID(2, 5),
    HAL_GPIO_PROBE_ID(2, 6), HAL_GPIO_PROBE_ID(2, 7),
    HAL_GPIO_PROBE_ID(3, 0), HAL_GPIO_PROBE_ID(3, 1), HAL_GPIO_PROBE_ID(3, 2),
    HAL_GPIO_PROBE_ID(3, 3), HAL_GPIO_PROBE_ID(3, 4), HAL_GPIO_PROBE_ID(3, 5),
    HAL_GPIO_PROBE_ID(3, 6), HAL_GPIO_PROBE_ID(3, 7),
};

#define PROBE_PIN_COUNT         (sizeof(probe_pins) / sizeof(probe_pins[0]))
#define STUB_PROBE_COUNTERS     2
#define STUB_PROBE_NO_PIN       0xFFu

#define MODEL_VDD_MV            3300u
#define MODEL_VBE_MV            600u
#define MODEL_VIH_MV            (MODEL_VDD_MV * 7u / 10u)
#define MODEL_PULL_UP_OHM       10000u
#define MODEL_PULL_UP_1M_OHM    1000000u

typedef struct {
    stub_probe_source_t source;
    uint8_t             external_level;
    uint32_t            period_us;
    uint32_t            high_us;
    uint64_t            origin_us;
    uint8_t             output_enabled;
    uint8_t             output_level;
    hal_gpio_pull_t     pull;
    uint8_t             node_level; // charge a floating node keeps
    uint8_t             level;      // level at the last model update
    uint32_t            drives;
    uint32_t            contention;
    uint32_t            base_ohm;
    uint32_t            pull_in_ua;
    uint8_t             relay_on;
    uint32_t            relay_pull_ins;
} model_pin_t;

typedef struct {
    uint8_t                pin; // index into probe_pins, or STUB_PROBE_NO_PIN
    uint8_t                counting;
    uint32_t               burst;
    hal_gpio_probe_count_t count;
} model_counter_t;

static model_pin_t     pins[PROBE_PIN_COUNT];
static model_counter_t counters[STUB_PROBE_COUNTERS];
static uint64_t        now_us;
static uint32_t        last_ms;
static uint8_t         initialized;

static int8_t pin_index(hal_gpio_probe_id_t id) {
    for (uint8_t i = 0; i < PROBE_PIN_COUNT; i++) {
        if (probe_pins[i] == id) {
            return (int8_t)i;
        }
    }
    return -1;
}

static uint8_t train_level(const model_pin_t *pin, uint64_t at_us) {
    if (at_us < pin->origin_us) {
        return 0;
    }
    return ((at_us - pin->origin_us) % pin->period_us) < pin->high_us;
}

/* Base current into the relay driver, in microamps. */
static uint32_t base_current_ua(const model_pin_t *pin) {
    uint32_t pull_ohm;

    if (pin->output_enabled) {
        return pin->output_level ?
               (MODEL_VDD_MV - MODEL_VBE_MV) * 1000u / pin->base_ohm : 0;
    }
    if (pin->pull == HAL_GPIO_PULL_UP) {
        pull_ohm = MODEL_PULL_UP_OHM;
    } else if (pin->pull == HAL_GPIO_PULL_UP_1M) {
        pull_ohm = MODEL_PULL_UP_1M_OHM;
    } else {
        return 0;
    }
    return (uint32_t)((uint64_t)(MODEL_VDD_MV - MODEL_VBE_MV) * 1000u /
                      (pull_ohm + pin->base_ohm));
}

static uint8_t base_level(const model_pin_t *pin) {
    uint64_t pin_mv;

    if (pin->output_enabled) {
        return pin->output_level;
    }
    if (pin->pull != HAL_GPIO_PULL_UP && pin->pull != HAL_GPIO_PULL_UP_1M) {
        return 0;
    }
    pin_mv = MODEL_VBE_MV + (uint64_t)base_current_ua(pin) * pin->base_ohm / 1000u;
    return pin_mv >= MODEL_VIH_MV;
}

static uint8_t resolve_level(const model_pin_t *pin) {
    switch (pin->source) {
    case STUB_PROBE_SOURCE_BASE:
        return base_level(pin);

    case STUB_PROBE_SOURCE_DRIVEN:
        return pin->external_level;

    case STUB_PROBE_SOURCE_TRAIN:
        return train_level(pin, now_us);

    default:
        if (pin->output_enabled) {
            return pin->output_level;
        }
        if (pin->pull == HAL_GPIO_PULL_UP || pin->pull == HAL_GPIO_PULL_UP_1M) {
            return 1;
        }
        if (pin->pull == HAL_GPIO_PULL_DOWN) {
            return 0;
        }
        return pin->node_level;
    }
}

static void count_edges(uint8_t index, uint32_t edges, uint64_t last_edge_us) {
    for (uint8_t slot = 0; slot < STUB_PROBE_COUNTERS; slot++) {
        model_counter_t *counter = &counters[slot];
        uint32_t         room;

        if (counter->pin != index || !counter->counting || edges == 0) {
            continue;
        }
        room = HAL_GPIO_PROBE_BURST_LIMIT - counter->burst;
        if (edges >= room) {
            counter->count.edges         += room;
            counter->count.saturated      = 1;
            counter->counting             = 0;
            counter->burst                = HAL_GPIO_PROBE_BURST_LIMIT;
            counter->count.last_edge_tick = (uint32_t)last_edge_us;
            continue;
        }
        counter->count.edges         += edges;
        counter->burst               += edges;
        counter->count.last_edge_tick = (uint32_t)last_edge_us;
    }
}

/* Re-resolve one pin at now_us and count a rising edge if it went high. */
static void update_pin(uint8_t index) {
    model_pin_t *pin   = &pins[index];
    uint8_t      level = resolve_level(pin);

    if (pin->source == STUB_PROBE_SOURCE_FLOATING) {
        pin->node_level = level;
    }
    if (pin->source == STUB_PROBE_SOURCE_BASE) {
        uint8_t relay_on = base_current_ua(pin) >= pin->pull_in_ua;

        if (relay_on && !pin->relay_on) {
            pin->relay_pull_ins++;
        }
        pin->relay_on = relay_on;
    }
    if (!pin->level && level) {
        count_edges(index, 1, now_us);
    }
    pin->level = level;
}

static void init_model(void) {
    if (initialized) {
        return;
    }
    memset(pins, 0, sizeof(pins));
    for (uint8_t slot = 0; slot < STUB_PROBE_COUNTERS; slot++) {
        memset(&counters[slot], 0, sizeof(counters[slot]));
        counters[slot].pin = STUB_PROBE_NO_PIN;
    }
    now_us      = 0;
    last_ms     = hal_millis();
    initialized = 1;
}

/* Advance model time to hal_millis(), generating the train edges in between. */
static void sync_time(void) {
    uint32_t ms = hal_millis();
    uint64_t previous_us;

    init_model();
    previous_us = now_us;
    now_us     += (uint64_t)(uint32_t)(ms - last_ms) * 1000u;
    last_ms     = ms;
    for (uint8_t i = 0; i < PROBE_PIN_COUNT; i++) {
        model_pin_t *pin = &pins[i];

        if (pin->source == STUB_PROBE_SOURCE_TRAIN && now_us >= pin->origin_us) {
            /* Rising edges sit at origin + k * period; count k in (previous, now]. */
            uint64_t last_k = (now_us - pin->origin_us) / pin->period_us;
            uint64_t first_k;

            if (previous_us < pin->origin_us) {
                first_k = 0;
            } else {
                first_k = (previous_us - pin->origin_us) / pin->period_us + 1;
            }
            if (last_k >= first_k) {
                count_edges(i, (uint32_t)(last_k - first_k + 1),
                            pin->origin_us + last_k * pin->period_us);
            }
            pin->level = train_level(pin, now_us);
        } else {
            update_pin(i);
        }
    }
}

uint8_t hal_gpio_probe_pins(const hal_gpio_probe_id_t **ids) {
    *ids = probe_pins;
    return (uint8_t)PROBE_PIN_COUNT;
}

uint8_t hal_gpio_probe_read(hal_gpio_probe_id_t id) {
    int8_t index = pin_index(id);

    sync_time();
    return index < 0 ? 0 : pins[index].level;
}

void hal_gpio_probe_input(hal_gpio_probe_id_t id, hal_gpio_pull_t pull) {
    int8_t index = pin_index(id);

    sync_time();
    if (index < 0) {
        return;
    }
    pins[index].output_enabled = 0;
    pins[index].output_level   = 0;
    pins[index].pull           = pull;
    update_pin((uint8_t)index);
}

void hal_gpio_probe_drive(hal_gpio_probe_id_t id, uint8_t level) {
    int8_t index = pin_index(id);

    sync_time();
    if (index < 0) {
        return;
    }
    pins[index].drives++;
    if (pins[index].source != STUB_PROBE_SOURCE_FLOATING &&
        pins[index].source != STUB_PROBE_SOURCE_BASE) {
        pins[index].contention++;
    }
    pins[index].output_enabled = 1;
    pins[index].output_level   = level != 0;
    update_pin((uint8_t)index);
}

uint8_t hal_gpio_probe_counter_capacity(void) {
    return STUB_PROBE_COUNTERS;
}

bool hal_gpio_probe_counter_start(uint8_t slot, hal_gpio_probe_id_t id) {
    int8_t index = pin_index(id);

    sync_time();
    if (slot >= STUB_PROBE_COUNTERS || index < 0) {
        return false;
    }
    memset(&counters[slot], 0, sizeof(counters[slot]));
    counters[slot].pin      = (uint8_t)index;
    counters[slot].counting = 1;
    return true;
}

void hal_gpio_probe_counter_stop(uint8_t slot) {
    sync_time();
    if (slot < STUB_PROBE_COUNTERS) {
        counters[slot].counting = 0;
        counters[slot].pin      = STUB_PROBE_NO_PIN;
    }
}

void hal_gpio_probe_counter_read(uint8_t slot, hal_gpio_probe_count_t *out) {
    sync_time();
    if (slot >= STUB_PROBE_COUNTERS) {
        memset(out, 0, sizeof(*out));
        return;
    }
    *out = counters[slot].count;
    counters[slot].burst = 0;
}

uint32_t hal_gpio_probe_ticks(void) {
    sync_time();
    return (uint32_t)now_us;
}

uint32_t hal_gpio_probe_ticks_per_us(void) {
    return 1;
}

void stub_gpio_probe_reset(void) {
    initialized = 0;
    init_model();
}

static model_pin_t *set_source(hal_gpio_probe_id_t id,
                               stub_probe_source_t source) {
    int8_t index = pin_index(id);

    sync_time();
    if (index < 0) {
        return NULL;
    }
    pins[index].source = source;
    return &pins[index];
}

bool stub_gpio_probe_set_floating(hal_gpio_probe_id_t id) {
    model_pin_t *pin = set_source(id, STUB_PROBE_SOURCE_FLOATING);

    if (pin == NULL) {
        return false;
    }
    pin->node_level = pin->level;
    update_pin((uint8_t)(pin - pins));
    return true;
}

bool stub_gpio_probe_set_driven(hal_gpio_probe_id_t id, uint8_t level) {
    model_pin_t *pin = set_source(id, STUB_PROBE_SOURCE_DRIVEN);

    if (pin == NULL) {
        return false;
    }
    pin->external_level = level != 0;
    if (pin->output_enabled && pin->output_level != pin->external_level) {
        pin->contention++;
    }
    update_pin((uint8_t)(pin - pins));
    return true;
}

bool stub_gpio_probe_set_train(hal_gpio_probe_id_t id, uint32_t period_us,
                               uint32_t high_us) {
    model_pin_t *pin;

    if (period_us == 0 || high_us == 0 || high_us >= period_us) {
        return false;
    }
    pin = set_source(id, STUB_PROBE_SOURCE_TRAIN);
    if (pin == NULL) {
        return false;
    }
    pin->period_us = period_us;
    pin->high_us   = high_us;
    /* The first rising edge comes one period from now. */
    pin->origin_us = now_us + period_us;
    pin->level     = 0;
    if (pin->output_enabled) {
        pin->contention++;
    }
    return true;
}

bool stub_gpio_probe_set_base(hal_gpio_probe_id_t id, uint32_t base_ohm,
                              uint32_t pull_in_ua) {
    model_pin_t *pin;

    if (base_ohm == 0 || pull_in_ua == 0) {
        return false;
    }
    pin = set_source(id, STUB_PROBE_SOURCE_BASE);
    if (pin == NULL) {
        return false;
    }
    pin->base_ohm   = base_ohm;
    pin->pull_in_ua = pull_in_ua;
    update_pin((uint8_t)(pin - pins));
    return true;
}

bool stub_gpio_probe_info(hal_gpio_probe_id_t id, stub_probe_pin_info_t *out) {
    int8_t index = pin_index(id);

    sync_time();
    if (index < 0) {
        return false;
    }
    *out = (stub_probe_pin_info_t){
        .source         = pins[index].source,
        .level          = pins[index].level,
        .output_enabled = pins[index].output_enabled,
        .output_level   = pins[index].output_level,
        .pull           = pins[index].pull,
        .drives         = pins[index].drives,
        .contention     = pins[index].contention,
        .relay_on       = pins[index].relay_on,
        .relay_pull_ins = pins[index].relay_pull_ins,
    };
    return true;
}

bool stub_gpio_probe_parse(const char *name, hal_gpio_probe_id_t *id) {
    hal_gpio_probe_id_t candidate;

    if (name == NULL || name[0] < 'A' || name[0] > 'Z' ||
        name[1] < '0' || name[1] > '9' || name[2] != '\0') {
        return false;
    }
    candidate = HAL_GPIO_PROBE_ID(name[0] - 'A', name[1] - '0');
    if (pin_index(candidate) < 0) {
        return false;
    }
    *id = candidate;
    return true;
}

/* The stub encodes pins like Telink: port << 8 | 1 << index. */
hal_gpio_probe_id_t hal_gpio_probe_id_from_pin(hal_gpio_pin_t pin) {
    uint8_t mask  = (uint8_t)(pin & 0xFFu);
    uint8_t index = 0;

    while (mask > 1u) {
        mask >>= 1;
        index++;
    }
    return HAL_GPIO_PROBE_ID(pin >> 8, index);
}
