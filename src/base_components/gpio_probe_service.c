#include "gpio_probe_service.h"

#include "hal/tasks.h"
#include "hal/timer.h"
#include <stddef.h>
#include <string.h>

/* Pull test: up, down, up, down, GPIO_PROBE_PULL_PHASE_MS each. */
#define PULL_PHASES               4u
/* A watch measures its rate over windows of at least this length. */
#define RATE_WINDOW_MS            10u

/*
 * Edge ticks wrap (Telink: 2^32 at 16 MHz, 268 s). Gaps longer than this
 * between two counted edges are measured on the millisecond clock instead.
 */
#define TICK_INTERVAL_LIMIT_MS    200000u
#define VERIFY_SAMPLES            3u

typedef struct {
    gpio_probe_watch_result_t result;
    uint8_t                   counter_running;
    uint8_t                   have_edge;
    uint32_t                  seen_edges;
    uint32_t                  seen_tick;
    uint32_t                  seen_ms;
    uint32_t                  window_edges;
    uint32_t                  window_ms;
} watch_slot_t;

static bool enabled;
static hal_gpio_probe_id_t pins[HAL_GPIO_PROBE_MAX_PINS];
static uint8_t             pin_count;
static hal_gpio_probe_id_t claimed[HAL_GPIO_PROBE_MAX_PINS];
static uint8_t             claimed_count;
static uint8_t             classes[HAL_GPIO_PROBE_MAX_PINS];

static bool     pull_running;
static uint32_t pull_mask; // positions under test
static uint8_t  pull_phase;
static uint32_t pull_phase_ms;
static uint32_t up_ones, up_zeros, down_ones, down_zeros;

static bool         watch_running;
static uint8_t      watch_count;
static uint16_t     watch_duration_s;
static uint32_t     watch_start_ms;
static uint32_t     watch_elapsed_ms;
static watch_slot_t watch_slots[GPIO_PROBE_WATCH_SLOTS];

static bool armed;
static hal_gpio_probe_id_t armed_pin;
static uint32_t            armed_ms;

static bool pulse_running;
static hal_gpio_probe_id_t pulse_pin;
static uint32_t            pulse_start_ms;
static uint16_t            pulse_duration_ms;
static hal_task_t          pulse_task;

static int8_t position_of(hal_gpio_probe_id_t pin) {
    for (uint8_t i = 0; i < pin_count; i++) {
        if (pins[i] == pin) {
            return (int8_t)i;
        }
    }
    return -1;
}

static bool is_claimed(hal_gpio_probe_id_t pin) {
    for (uint8_t i = 0; i < claimed_count; i++) {
        if (claimed[i] == pin) {
            return true;
        }
    }
    return false;
}

static void build_pin_list(void) {
    const hal_gpio_probe_id_t *table;
    uint8_t count = hal_gpio_probe_pins(&table);

    pin_count = 0;
    for (uint8_t i = 0; i < count && pin_count < HAL_GPIO_PROBE_MAX_PINS; i++) {
        if (!is_claimed(table[i])) {
            pins[pin_count++] = table[i];
        }
    }
}

/*
 * Rest state of every unclaimed pin: input with the 100 kOhm pull-down.
 * Relay drivers such as the ULN2003 have no input pull-down of their own; a
 * floating input picks up mains hum and half-energises the coil, which can
 * also pull the supply down far enough to reset the MCU.
 */
static void release(hal_gpio_probe_id_t pin) {
    hal_gpio_probe_input(pin, HAL_GPIO_PULL_DOWN);
}

static hal_gpio_pull_t hal_pull(uint8_t pull) {
    if (pull == GPIO_PROBE_PULL_UP) {
        return HAL_GPIO_PULL_UP;
    }
    if (pull == GPIO_PROBE_PULL_DOWN) {
        return HAL_GPIO_PULL_DOWN;
    }
    return HAL_GPIO_PULL_NONE;
}

static bool is_watched(hal_gpio_probe_id_t pin) {
    for (uint8_t i = 0; watch_running && i < watch_count; i++) {
        if (watch_slots[i].result.pin == pin &&
            (watch_slots[i].result.flags & GPIO_PROBE_WATCH_RUNNING)) {
            return true;
        }
    }
    return false;
}

static gpio_probe_class_t classify(bool up_one, bool up_zero, bool down_one,
                                   bool down_zero) {
    if ((up_one && up_zero) || (down_one && down_zero)) {
        return GPIO_PROBE_CLASS_ACTIVE;
    }
    if (up_one && down_zero) {
        return GPIO_PROBE_CLASS_FLOATING;
    }
    if (up_one && down_one) {
        return GPIO_PROBE_CLASS_DRIVEN_HIGH;
    }
    if (up_zero && down_zero) {
        return GPIO_PROBE_CLASS_DRIVEN_LOW;
    }
    return GPIO_PROBE_CLASS_ACTIVE;
}

/* ---- pull test ---- */

static void pull_sample(void) {
    bool up = (pull_phase & 1u) == 0;

    for (uint8_t i = 0; i < pin_count; i++) {
        uint32_t bit = 1u << i;

        if (!(pull_mask & bit)) {
            continue;
        }
        if (hal_gpio_probe_read(pins[i])) {
            if (up) {
                up_ones |= bit;
            } else {
                down_ones |= bit;
            }
        } else if (up) {
            up_zeros |= bit;
        } else {
            down_zeros |= bit;
        }
    }
}

/*
 * The pull-up is the strong 10 kOhm one. A pin that loads to ground, such as
 * a relay transistor base behind a series resistor, reads lower under a
 * weaker pull: the 1 MOhm pull-up leaves every such base at V_BE, driven low
 * and never pulsed, while 10 kOhm lifts a base resistor of about 20 kOhm and
 * up above V_IH (it may pull the relay in during the up phases).
 */
static void pull_enter_phase(uint8_t phase, uint32_t now) {
    hal_gpio_pull_t pull = (phase & 1u) == 0 ? HAL_GPIO_PULL_UP : HAL_GPIO_PULL_DOWN;

    pull_phase    = phase;
    pull_phase_ms = now;
    for (uint8_t i = 0; i < pin_count; i++) {
        if (pull_mask & (1u << i)) {
            hal_gpio_probe_input(pins[i], pull);
        }
    }
    pull_sample();
}

static void pull_end(bool completed) {
    for (uint8_t i = 0; i < pin_count; i++) {
        uint32_t bit = 1u << i;

        if (!(pull_mask & bit)) {
            continue;
        }
        release(pins[i]);
        if (completed) {
            classes[i] = (uint8_t)classify(up_ones & bit, up_zeros & bit,
                                           down_ones & bit, down_zeros & bit);
        }
    }
    pull_running = false;
    pull_mask    = 0;
}

static void pull_poll(uint32_t now) {
    if (!pull_running) {
        return;
    }
    if ((uint32_t)(now - pull_phase_ms) < GPIO_PROBE_PULL_PHASE_MS) {
        pull_sample();
        return;
    }
    if (pull_phase + 1u < PULL_PHASES) {
        pull_enter_phase((uint8_t)(pull_phase + 1u), now);
        return;
    }
    pull_end(true);
}

/* ---- watch ---- */

static uint32_t interval_us(const watch_slot_t *slot, uint32_t tick,
                            uint32_t now, uint32_t edges) {
    uint32_t span_ms = now - slot->seen_ms;
    uint32_t span_us;

    if (span_ms >= TICK_INTERVAL_LIMIT_MS) {
        span_us = span_ms >= UINT32_MAX / 1000u ? UINT32_MAX : span_ms * 1000u;
    } else {
        span_us = (tick - slot->seen_tick) / hal_gpio_probe_ticks_per_us();
    }
    return span_us / edges;
}

static void watch_stop_slot(watch_slot_t *slot, uint8_t slot_index) {
    if (slot->counter_running) {
        hal_gpio_probe_counter_stop(slot_index);
        slot->counter_running = 0;
    }
    if (slot->result.flags & GPIO_PROBE_WATCH_RUNNING) {
        release(slot->result.pin);
        slot->result.flags &= (uint8_t) ~GPIO_PROBE_WATCH_RUNNING;
    }
}

static void watch_update_slot(watch_slot_t *slot, uint8_t slot_index,
                              uint32_t now) {
    hal_gpio_probe_count_t count;
    uint32_t fresh;

    if (!(slot->result.flags & GPIO_PROBE_WATCH_RUNNING)) {
        return;
    }
    hal_gpio_probe_counter_read(slot_index, &count);
    fresh = count.edges - slot->seen_edges;
    slot->result.edges = count.edges;
    slot->result.level = hal_gpio_probe_read(slot->result.pin);
    if (fresh > 0) {
        /* The gap from the previous edge to the newest one, averaged over
         * the edges between; exact when the main loop outpaces the signal. */
        if (slot->have_edge) {
            uint32_t interval = interval_us(slot, count.last_edge_tick, now, fresh);

            if (slot->result.min_interval_us == 0 ||
                interval < slot->result.min_interval_us) {
                slot->result.min_interval_us = interval;
            }
            if (interval > slot->result.max_interval_us) {
                slot->result.max_interval_us = interval;
            }
        }
        slot->have_edge  = 1;
        slot->seen_edges = count.edges;
        slot->seen_tick  = count.last_edge_tick;
        slot->seen_ms    = now;
    }
    if (count.saturated) {
        slot->result.flags |= GPIO_PROBE_WATCH_OVER_RATE;
        watch_stop_slot(slot, slot_index);
        return;
    }
    if ((uint32_t)(now - slot->window_ms) >= RATE_WINDOW_MS) {
        uint32_t window_edges = count.edges - slot->window_edges;
        uint32_t window_ms    = now - slot->window_ms;

        /* Edges per millisecond; 32-bit only, the TC32 has no 64-bit multiply. */
        if (window_edges > (GPIO_PROBE_RATE_CEILING_HZ / 1000u) * window_ms) {
            slot->result.flags |= GPIO_PROBE_WATCH_OVER_RATE;
            watch_stop_slot(slot, slot_index);
            return;
        }
        slot->window_edges = count.edges;
        slot->window_ms    = now;
    }
}

static void watch_end(void) {
    for (uint8_t i = 0; i < watch_count; i++) {
        watch_stop_slot(&watch_slots[i], i);
    }
    watch_running = false;
}

static void watch_poll(uint32_t now) {
    bool done;

    if (!watch_running) {
        return;
    }
    watch_elapsed_ms = now - watch_start_ms;
    done             = watch_elapsed_ms >= (uint32_t)watch_duration_s * 1000u;
    if (done) {
        watch_elapsed_ms = (uint32_t)watch_duration_s * 1000u;
    }
    for (uint8_t i = 0; i < watch_count; i++) {
        watch_update_slot(&watch_slots[i], i, now);
    }
    if (done) {
        watch_end();
    }
}

/* ---- pulse ---- */

static void pulse_end(void) {
    if (!pulse_running) {
        return;
    }
    release(pulse_pin);
    hal_tasks_unschedule(&pulse_task);
    pulse_running = false;
}

static void pulse_task_handler(void *arg) {
    (void)arg;
    pulse_end();
}

static void pulse_poll(uint32_t now) {
    if (pulse_running && (uint32_t)(now - pulse_start_ms) >= pulse_duration_ms) {
        pulse_end();
    }
}

/* The class the pin shows under a pull-up and a pull-down right now; leaves it in pull-down. */
static gpio_probe_class_t verify_class(hal_gpio_probe_id_t pin) {
    bool up_one = false, up_zero = false, down_one = false, down_zero = false;

    hal_gpio_probe_input(pin, HAL_GPIO_PULL_UP);
    for (uint8_t i = 0; i < VERIFY_SAMPLES; i++) {
        if (hal_gpio_probe_read(pin)) {
            up_one = true;
        } else {
            up_zero = true;
        }
    }
    hal_gpio_probe_input(pin, HAL_GPIO_PULL_DOWN);
    for (uint8_t i = 0; i < VERIFY_SAMPLES; i++) {
        if (hal_gpio_probe_read(pin)) {
            down_one = true;
        } else {
            down_zero = true;
        }
    }
    return classify(up_one, up_zero, down_one, down_zero);
}

static bool pulse_eligible(gpio_probe_class_t pin_class) {
    return pin_class == GPIO_PROBE_CLASS_FLOATING || pin_class == GPIO_PROBE_CLASS_DRIVEN_LOW;
}

/*
 * A driven-low pin is a transistor base behind a low resistor or a chip
 * output such as BL0937 CF; it may only be lifted, briefly, so that a relay
 * clicks while an output fights the pin for at most
 * GPIO_PROBE_DRIVEN_LOW_PULSE_MAX_MS.
 */
static bool pulse_in_range(gpio_probe_class_t pin_class, uint8_t level,
                           uint16_t duration_ms) {
    if (duration_ms < GPIO_PROBE_PULSE_MIN_MS ||
        duration_ms > GPIO_PROBE_PULSE_MAX_MS || level > 1) {
        return false;
    }
    if (pin_class == GPIO_PROBE_CLASS_DRIVEN_LOW) {
        return level == 1 && duration_ms <= GPIO_PROBE_DRIVEN_LOW_PULSE_MAX_MS;
    }
    return true;
}

static void disarm_if_expired(uint32_t now) {
    if (armed && (uint32_t)(now - armed_ms) >= GPIO_PROBE_ARM_TIMEOUT_MS) {
        armed = false;
    }
}

/* ---- public ---- */

void gpio_probe_service_init(void) {
    gpio_probe_service_stop();
    enabled       = false;
    claimed_count = 0;
    watch_count   = 0;
    memset(classes, 0, sizeof(classes));
    memset(watch_slots, 0, sizeof(watch_slots));
    watch_elapsed_ms = 0;
    hal_tasks_init(&pulse_task);
    pulse_task.handler = pulse_task_handler;
    pulse_task.arg     = NULL;
    build_pin_list();
}

void gpio_probe_service_claim(uint8_t port, uint8_t index) {
    hal_gpio_probe_id_t pin = HAL_GPIO_PROBE_ID(port, index);

    if (!is_claimed(pin) && claimed_count < HAL_GPIO_PROBE_MAX_PINS) {
        claimed[claimed_count++] = pin;
    }
    build_pin_list();
}

void gpio_probe_service_enable(void) {
    build_pin_list();
    for (uint8_t i = 0; i < pin_count; i++) {
        release(pins[i]);
    }
    enabled = true;
}

uint8_t gpio_probe_service_pin_count(void) {
    return enabled ? pin_count : 0;
}

hal_gpio_probe_id_t gpio_probe_service_pin(uint8_t position) {
    return position < pin_count ? pins[position] : 0;
}

gpio_probe_class_t gpio_probe_service_class(uint8_t position) {
    return position < pin_count ? (gpio_probe_class_t)classes[position] :
           GPIO_PROBE_CLASS_UNTESTED;
}

gpio_probe_status_t gpio_probe_service_snapshot(uint32_t *levels) {
    uint32_t bits = 0;

    if (!enabled) {
        return GPIO_PROBE_STATUS_DISABLED;
    }
    for (uint8_t i = 0; i < pin_count; i++) {
        if (hal_gpio_probe_read(pins[i])) {
            bits |= 1u << i;
        }
    }
    *levels = bits;
    return GPIO_PROBE_STATUS_OK;
}

gpio_probe_status_t gpio_probe_service_pull_test(const hal_gpio_probe_id_t *list,
                                                 uint8_t count) {
    uint32_t mask = 0;

    if (!enabled) {
        return GPIO_PROBE_STATUS_DISABLED;
    }
    if (pull_running || watch_running || pulse_running) {
        return GPIO_PROBE_STATUS_BUSY;
    }
    if (count == 0) {
        mask = pin_count >= 32 ? UINT32_MAX : (1u << pin_count) - 1u;
    }
    for (uint8_t i = 0; i < count; i++) {
        int8_t position = position_of(list[i]);

        if (position < 0) {
            return GPIO_PROBE_STATUS_BAD_PIN;
        }
        mask |= 1u << position;
    }
    /* Pulling an armed pin is harmless; the arm stays valid. */
    pull_mask    = mask;
    up_ones      = up_zeros = down_ones = down_zeros = 0;
    pull_running = true;
    pull_enter_phase(0, hal_millis());
    return GPIO_PROBE_STATUS_OK;
}

gpio_probe_status_t gpio_probe_service_watch(const gpio_probe_watch_pin_t *list,
                                             uint8_t count, uint16_t duration_s) {
    uint8_t  capacity = hal_gpio_probe_counter_capacity();
    uint32_t now;

    if (!enabled) {
        return GPIO_PROBE_STATUS_DISABLED;
    }
    if (watch_running || pull_running) {
        return GPIO_PROBE_STATUS_BUSY;
    }
    if (count == 0 || duration_s < GPIO_PROBE_WATCH_MIN_S ||
        duration_s > GPIO_PROBE_WATCH_MAX_S) {
        return GPIO_PROBE_STATUS_RANGE;
    }
    if (capacity > GPIO_PROBE_WATCH_SLOTS) {
        capacity = GPIO_PROBE_WATCH_SLOTS;
    }
    if (count > capacity) {
        return GPIO_PROBE_STATUS_RESOURCE;
    }
    for (uint8_t i = 0; i < count; i++) {
        if (position_of(list[i].pin) < 0) {
            return GPIO_PROBE_STATUS_BAD_PIN;
        }
        if (list[i].pull > GPIO_PROBE_PULL_DOWN) {
            return GPIO_PROBE_STATUS_RANGE;
        }
        for (uint8_t j = 0; j < i; j++) {
            if (list[j].pin == list[i].pin) {
                return GPIO_PROBE_STATUS_RANGE;
            }
        }
        if (pulse_running && list[i].pin == pulse_pin) {
            return GPIO_PROBE_STATUS_BUSY;
        }
    }

    now = hal_millis();
    memset(watch_slots, 0, sizeof(watch_slots));
    watch_count      = count;
    watch_duration_s = duration_s;
    watch_start_ms   = now;
    watch_elapsed_ms = 0;
    for (uint8_t i = 0; i < count; i++) {
        watch_slot_t *slot = &watch_slots[i];

        slot->result.pin   = list[i].pin;
        slot->result.flags = GPIO_PROBE_WATCH_RUNNING;
        slot->window_ms    = now;
        hal_gpio_probe_input(list[i].pin, hal_pull(list[i].pull));
        slot->result.level = hal_gpio_probe_read(list[i].pin);
        if (!hal_gpio_probe_counter_start(i, list[i].pin)) {
            watch_count = (uint8_t)(i + 1u);
            watch_end();
            return GPIO_PROBE_STATUS_RESOURCE;
        }
        slot->counter_running = 1;
    }
    watch_running = true;
    return GPIO_PROBE_STATUS_OK;
}

uint8_t gpio_probe_service_watch_count(void) {
    return watch_count;
}

uint32_t gpio_probe_service_watch_elapsed_ms(void) {
    return watch_elapsed_ms;
}

const gpio_probe_watch_result_t *gpio_probe_service_watch_result(uint8_t slot) {
    return slot < GPIO_PROBE_WATCH_SLOTS ? &watch_slots[slot].result : NULL;
}

gpio_probe_status_t gpio_probe_service_arm(hal_gpio_probe_id_t pin) {
    int8_t position = position_of(pin);

    if (!enabled) {
        return GPIO_PROBE_STATUS_DISABLED;
    }
    if (position < 0) {
        return GPIO_PROBE_STATUS_BAD_PIN;
    }
    if (pulse_running || pull_running || is_watched(pin)) {
        return GPIO_PROBE_STATUS_BUSY;
    }
    if (!pulse_eligible((gpio_probe_class_t)classes[position])) {
        return GPIO_PROBE_STATUS_NOT_FLOATING;
    }
    armed     = true;
    armed_pin = pin;
    armed_ms  = hal_millis();
    return GPIO_PROBE_STATUS_OK;
}

gpio_probe_status_t gpio_probe_service_pulse(hal_gpio_probe_id_t pin,
                                             uint8_t level, uint16_t duration_ms) {
    int8_t             position = position_of(pin);
    uint32_t           now      = hal_millis();
    bool               was_armed;
    gpio_probe_class_t pin_class;
    gpio_probe_class_t observed;

    if (!enabled) {
        return GPIO_PROBE_STATUS_DISABLED;
    }
    if (pulse_running) {
        return GPIO_PROBE_STATUS_BUSY;
    }
    if (position < 0) {
        return GPIO_PROBE_STATUS_BAD_PIN;
    }
    disarm_if_expired(now);
    /* Every attempt consumes the arm, so each pulse needs a fresh one. */
    was_armed = armed && armed_pin == pin;
    armed     = false;
    if (!was_armed) {
        return GPIO_PROBE_STATUS_NOT_ARMED;
    }
    if (pull_running || is_watched(pin)) {
        return GPIO_PROBE_STATUS_BUSY;
    }
    pin_class = (gpio_probe_class_t)classes[position];
    if (!pulse_eligible(pin_class)) {
        return GPIO_PROBE_STATUS_NOT_FLOATING;
    }
    if (!pulse_in_range(pin_class, level, duration_ms)) {
        return GPIO_PROBE_STATUS_RANGE;
    }
    observed = verify_class(pin);
    if (observed != pin_class) {
        classes[position] = (uint8_t)observed;
        release(pin);
        return GPIO_PROBE_STATUS_VERIFY_FAILED;
    }
    /* Arm the cutoff before driving: no timer, no pulse. */
    if (!hal_tasks_schedule(&pulse_task, duration_ms)) {
        release(pin);
        return GPIO_PROBE_STATUS_RESOURCE;
    }
    pulse_running     = true;
    pulse_pin         = pin;
    pulse_start_ms    = hal_millis();
    pulse_duration_ms = duration_ms;
    hal_gpio_probe_drive(pin, level);
    return GPIO_PROBE_STATUS_OK;
}

void gpio_probe_service_stop(void) {
    pulse_end();
    if (pull_running) {
        pull_end(false);
    }
    if (watch_running) {
        watch_poll(hal_millis());
        watch_end();
    }
    armed = false;
}

uint8_t gpio_probe_service_state(void) {
    uint8_t state = 0;

    disarm_if_expired(hal_millis());
    if (pull_running) {
        state |= GPIO_PROBE_STATE_PULL_TEST;
    }
    if (watch_running) {
        state |= GPIO_PROBE_STATE_WATCH;
    }
    if (armed) {
        state |= GPIO_PROBE_STATE_ARMED;
    }
    if (pulse_running) {
        state |= GPIO_PROBE_STATE_PULSE;
    }
    return state;
}

void gpio_probe_service_poll(void) {
    uint32_t now = hal_millis();

    pulse_poll(now);
    pull_poll(now);
    watch_poll(now);
    disarm_if_expired(now);
}
