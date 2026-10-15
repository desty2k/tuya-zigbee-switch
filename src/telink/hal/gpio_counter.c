#pragma pack(push, 1)
#include "tl_common.h"
#pragma pack(pop)

#include "device_config/build_features.h"
#include "hal/gpio_counter.h"
#include "hal/timer.h"

#if FEATURE_METER

/*
 * TLSR8258 timer0 and timer1 in GPIO trigger mode count the edges of the pins
 * routed to GPIO-to-RISC source 0 and 1 (reg_gpio_irq_risc0_en/risc1_en);
 * counter slot n uses timer n and source n. The tick register is the count:
 * snapshots read it without stopping or resetting the timer, so no edge is
 * lost between samples. The CPU interrupts of the two sources and the two
 * timers stay masked, so counting costs no interrupt. Timer2 (watchdog) and
 * the system timer (MAC) are not used. The GPIO probe drives the same RISC
 * sources with CPU interrupts, so the probe and meter features never share an
 * image (src/features.mk). Timer registers do not survive deep retention;
 * meters run on mains-powered routers, which never sleep.
 */
#define COUNTER_SLOTS    2

typedef struct {
    GPIO_PinTypeDef pin;
    uint32_t        last_total;
    uint32_t        overflow_count;
    uint8_t         open;
} counter_slot_t;

static counter_slot_t slots[COUNTER_SLOTS];

/*
 * Ports A-D (pin ids hold exactly one bit of their port), without PC2/PC3
 * when a 32 kHz crystal is fitted on them.
 */
static bool pin_routable(hal_gpio_pin_t pin) {
    uint8_t bits = (uint8_t)(pin & 0xFFu);

    if (pin == HAL_INVALID_PIN || (pin >> 8) > 3 || bits == 0 || (bits & (bits - 1u)) != 0) {
        return false;
    }
#if defined(CLOCK_32K_EXT_CRYSTAL) && CLOCK_32K_EXT_CRYSTAL
    if (pin == GPIO_PC2 || pin == GPIO_PC3) {
        return false;
    }
#endif
    return true;
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

static void route_pin(uint8_t slot, GPIO_PinTypeDef pin, bool enable) {
    uint8_t bit = (uint8_t)(pin & 0xFFu);

    if (slot == 0) {
        if (enable) {
            BM_SET(reg_gpio_irq_risc0_en(pin), bit);
        } else {
            BM_CLR(reg_gpio_irq_risc0_en(pin), bit);
        }
    } else if (enable) {
        BM_SET(reg_gpio_irq_risc1_en(pin), bit);
    } else {
        BM_CLR(reg_gpio_irq_risc1_en(pin), bit);
    }
}

hal_gpio_counter_id_t hal_gpio_counter_open(hal_gpio_pin_t pin,
                                            hal_gpio_counter_edge_t edge,
                                            hal_gpio_pull_t pull) {
    GPIO_PinTypeDef native = (GPIO_PinTypeDef)pin;
    uint8_t         bit    = (uint8_t)(pin & 0xFFu);
    uint8_t         slot;

    if (!pin_routable(pin) ||
        (edge != HAL_GPIO_COUNTER_EDGE_RISING && edge != HAL_GPIO_COUNTER_EDGE_FALLING)) {
        return HAL_GPIO_COUNTER_INVALID_ID;
    }
    for (slot = 0; slot < COUNTER_SLOTS && slots[slot].open; slot++) {
    }
    if (slot == COUNTER_SLOTS) {
        return HAL_GPIO_COUNTER_INVALID_ID;
    }
    gpio_set_func(native, AS_GPIO);
    gpio_set_output_en(native, 0);
    gpio_set_input_en(native, 1);
    gpio_setup_up_down_resistor(native, native_pull(pull));
    if (edge == HAL_GPIO_COUNTER_EDGE_FALLING) {
        BM_SET(reg_gpio_pol(native), bit);
    } else {
        BM_CLR(reg_gpio_pol(native), bit);
    }
    timer_stop((TIMER_TypeDef)slot);
    reg_irq_mask &= ~(slot == 0 ? (FLD_IRQ_GPIO_RISC0_EN | FLD_IRQ_TMR0_EN) :
                      (FLD_IRQ_GPIO_RISC1_EN | FLD_IRQ_TMR1_EN));
    route_pin(slot, native, true);
    timer_set_mode((TIMER_TypeDef)slot, TIMER_MODE_GPIO_TRIGGER);
    timer_set_init_tick((TIMER_TypeDef)slot, 0);
    timer_set_cap_tick((TIMER_TypeDef)slot, 0xFFFFFFFFu);
    timer_start((TIMER_TypeDef)slot);
    slots[slot] = (counter_slot_t){ .pin = native, .open = 1 };
    return (hal_gpio_counter_id_t)slot;
}

bool hal_gpio_counter_snapshot(hal_gpio_counter_id_t id,
                               hal_gpio_counter_snapshot_t *out) {
    counter_slot_t *slot;
    uint32_t        total;

    if (id < 0 || id >= COUNTER_SLOTS || !slots[id].open || out == NULL) {
        return false;
    }
    slot  = &slots[id];
    total = reg_tmr_tick(id);
    if (total < slot->last_total) {
        slot->overflow_count++;
    }
    slot->last_total = total;
    *out             = (hal_gpio_counter_snapshot_t){
        .total          = total,
        .timestamp_ms   = hal_millis(),
        .overflow_count = slot->overflow_count,
        .running        = 1,
    };
    return true;
}

void hal_gpio_counter_close(hal_gpio_counter_id_t id) {
    if (id < 0 || id >= COUNTER_SLOTS || !slots[id].open) {
        return;
    }
    timer_stop((TIMER_TypeDef)id);
    route_pin((uint8_t)id, slots[id].pin, false);
    slots[id].open = 0;
}

uint8_t hal_gpio_counter_capacity(void) {
    return COUNTER_SLOTS;
}

bool hal_gpio_counter_can_route(uint8_t slot, hal_gpio_pin_t pin) {
    return slot < COUNTER_SLOTS && pin_routable(pin);
}

#else

/* Images without the meter feature reject every counter route. */
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

#endif
