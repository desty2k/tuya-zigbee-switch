#pragma pack(push, 1)
#include "tl_common.h"
#pragma pack(pop)

#include "hal/gpio_probe.h"
#include "telink_gpio_pin.h"
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

/* The probe table is the rest set of hal_gpio_rest_all() (gpio.c). */
static hal_gpio_probe_id_t probe_pins[HAL_GPIO_PROBE_MAX_PINS];
static uint8_t             probe_pin_count;

/*
 * 100 kOhm pull-down against a few pF of pad and trace settles in well under
 * a microsecond; 10 us leaves margin for board capacitance.
 */
#define PROBE_SETTLE_US    10

/*
 * One counter per GPIO-to-RISC interrupt source, so each pin has a source of
 * its own and only its rising edge raises it. GPIO_IRQ_MODE stays with the
 * button pipeline (gpio_interrupts.c).
 */
#define PROBE_COUNTERS     2

typedef struct {
    GPIO_PinTypeDef                 pin;
    volatile uint8_t                counting;
    volatile uint32_t               burst;
    volatile hal_gpio_probe_count_t count;
} probe_counter_t;

static probe_counter_t counters[PROBE_COUNTERS];
static bool            isr_registered[PROBE_COUNTERS];

static GPIO_PinTypeDef native_pin(hal_gpio_probe_id_t id) {
    return (GPIO_PinTypeDef)(((uint16_t)HAL_GPIO_PROBE_PORT(id) << 8) |
                             BIT(HAL_GPIO_PROBE_INDEX(id)));
}

static GPIO_PullTypeDef native_pull(hal_gpio_pull_t pull) {
    switch (pull) {
    case HAL_GPIO_PULL_UP:
        return PM_PIN_PULLUP_10K;

    case HAL_GPIO_PULL_UP_1M:
        return PM_PIN_PULLUP_1M;

    case HAL_GPIO_PULL_DOWN:
        return PM_PIN_PULLDOWN_100K;

    default:
        return PM_PIN_UP_DOWN_FLOAT;
    }
}

static void counter_irq_enable(uint8_t slot, GPIO_PinTypeDef pin, bool enable) {
    if (slot == 0) {
        if (enable) {
            drv_gpio_irq_risc0_set((u32)pin, GPIO_RISING_EDGE);
            drv_gpio_irq_risc0_en((u32)pin);
        } else {
            drv_gpio_irq_risc0_dis((u32)pin);
        }
    } else if (enable) {
        drv_gpio_irq_risc1_set((u32)pin, GPIO_RISING_EDGE);
        drv_gpio_irq_risc1_en((u32)pin);
    } else {
        drv_gpio_irq_risc1_dis((u32)pin);
    }
}

/* Interrupt context: count, stamp, and stop at the burst limit. */
static void count_edge(uint8_t slot) {
    probe_counter_t *counter = &counters[slot];

    if (!counter->counting) {
        return;
    }
    counter->count.edges++;
    counter->count.last_edge_tick = clock_time();
    if (++counter->burst >= HAL_GPIO_PROBE_BURST_LIMIT) {
        counter_irq_enable(slot, counter->pin, false);
        counter->counting        = 0;
        counter->count.saturated = 1;
    }
}

static void risc0_isr(void) {
    count_edge(0);
}

static void risc1_isr(void) {
    count_edge(1);
}

/*
 * The SDK keeps one handler per (mode, pin) in a list of three for the life
 * of the image, so each source is registered once, under pin 0; the pin
 * itself is selected per watch.
 */
static bool register_isr(uint8_t slot) {
    if (isr_registered[slot]) {
        return true;
    }
    if (drv_gpio_irq_config(slot == 0 ? GPIO_IRQ_RISC0_MODE : GPIO_IRQ_RISC1_MODE,
                            0, GPIO_RISING_EDGE,
                            slot == 0 ? risc0_isr : risc1_isr) != 0) {
        return false;
    }
    isr_registered[slot] = true;
    return true;
}

uint8_t hal_gpio_probe_pins(const hal_gpio_probe_id_t **ids) {
    if (probe_pin_count == 0) {
        const hal_gpio_pin_t *rest;
        uint8_t count = telink_gpio_rest_pins(&rest);

        for (uint8_t i = 0; i < count && i < HAL_GPIO_PROBE_MAX_PINS; i++) {
            probe_pins[probe_pin_count++] = hal_gpio_probe_id_from_pin(rest[i]);
        }
    }
    *ids = probe_pins;
    return probe_pin_count;
}

uint8_t hal_gpio_probe_read(hal_gpio_probe_id_t id) {
    return gpio_read(native_pin(id)) ? 1 : 0;
}

/*
 * The output latch is cleared after the driver is off. On the TLSR8258 a PA
 * or PD pad whose latch holds 1 keeps reading 1 with its output disabled and
 * the 100 kOhm pull-down applied; PB and PC pads follow the pull.
 */
void hal_gpio_probe_input(hal_gpio_probe_id_t id, hal_gpio_pull_t pull) {
    GPIO_PinTypeDef pin = native_pin(id);

    gpio_set_output_en(pin, 0);
    gpio_write(pin, 0);
    gpio_set_func(pin, AS_GPIO);
    gpio_set_input_en(pin, 1);
    gpio_setup_up_down_resistor(pin, native_pull(pull));
    sleep_us(PROBE_SETTLE_US);
}

void hal_gpio_probe_drive(hal_gpio_probe_id_t id, uint8_t level) {
    GPIO_PinTypeDef pin = native_pin(id);

    gpio_set_func(pin, AS_GPIO);
    gpio_setup_up_down_resistor(pin, PM_PIN_UP_DOWN_FLOAT);
    gpio_write(pin, level ? 1 : 0);
    gpio_set_output_en(pin, 1);
}

uint8_t hal_gpio_probe_counter_capacity(void) {
    return PROBE_COUNTERS;
}

bool hal_gpio_probe_counter_start(uint8_t slot, hal_gpio_probe_id_t id) {
    u32 irq_state;

    if (slot >= PROBE_COUNTERS || !register_isr(slot)) {
        return false;
    }
    irq_state = drv_disable_irq();
    memset((void *)&counters[slot], 0, sizeof(counters[slot]));
    counters[slot].pin      = native_pin(id);
    counters[slot].counting = 1;
    counter_irq_enable(slot, counters[slot].pin, true);
    drv_restore_irq(irq_state);
    return true;
}

void hal_gpio_probe_counter_stop(uint8_t slot) {
    u32 irq_state;

    if (slot >= PROBE_COUNTERS) {
        return;
    }
    irq_state = drv_disable_irq();
    if (counters[slot].counting) {
        counter_irq_enable(slot, counters[slot].pin, false);
    }
    counters[slot].counting = 0;
    drv_restore_irq(irq_state);
}

void hal_gpio_probe_counter_read(uint8_t slot, hal_gpio_probe_count_t *out) {
    u32 irq_state;

    if (slot >= PROBE_COUNTERS) {
        memset(out, 0, sizeof(*out));
        return;
    }
    irq_state            = drv_disable_irq();
    out->edges           = counters[slot].count.edges;
    out->last_edge_tick  = counters[slot].count.last_edge_tick;
    out->saturated       = counters[slot].count.saturated;
    counters[slot].burst = 0;
    drv_restore_irq(irq_state);
}

uint32_t hal_gpio_probe_ticks(void) {
    return clock_time();
}

uint32_t hal_gpio_probe_ticks_per_us(void) {
    return CLOCK_16M_SYS_TIMER_CLK_1US;
}

hal_gpio_probe_id_t hal_gpio_probe_id_from_pin(hal_gpio_pin_t pin) {
    return HAL_GPIO_PROBE_ID(pin >> 8, telink_gpio_pin_index(pin));
}
