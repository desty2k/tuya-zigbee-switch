#pragma pack(push, 1)
#include "tl_common.h"
#pragma pack(pop)

#include "hal/gpio.h"
#include "hal/timer.h"
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#define MAX_WATCHED_PINS    16
#define MAX_REARM_TRIES     50
#define GPIO_PORT_COUNT     5

typedef struct {
    hal_gpio_pin_t pin;
    uint8_t        last_level;
} watched_pin_t;

static watched_pin_t          watched_pins[MAX_WATCHED_PINS];
static hal_gpio_edge_sink_t   edge_sink;
static hal_gpio_diagnostics_t diagnostics;
static uint32_t      edge_seq;
static bool          is_isr_initialized;
static volatile bool gpio_dispatch_pending;

static void gpio_watch_init(void) {
    static bool initialized;

    if (initialized) {
        return;
    }
    for (uint8_t i = 0; i < MAX_WATCHED_PINS; i++) {
        watched_pins[i].pin = HAL_INVALID_PIN;
    }
    initialized = true;
}

static uint8_t pin_port(hal_gpio_pin_t pin) {
    return (uint8_t)((pin >> 8) & 0x07);
}

static uint8_t pin_mask(hal_gpio_pin_t pin) {
    return (uint8_t)(pin & 0xFF);
}

static uint8_t snapshot_level(const uint8_t *snapshot, hal_gpio_pin_t pin) {
    return (snapshot[pin_port(pin)] & pin_mask(pin)) ? 1 : 0;
}

static void read_snapshot(uint8_t *snapshot) {
    memset(snapshot, 0, GPIO_PORT_COUNT);
    drv_gpio_read_all(snapshot);
}

static bool capture_snapshot(uint8_t *snapshot) {
    bool     changed = false;
    uint32_t now     = hal_millis();

    read_snapshot(snapshot);
    for (uint8_t i = 0; i < MAX_WATCHED_PINS; i++) {
        if (watched_pins[i].pin == HAL_INVALID_PIN) {
            continue;
        }
        uint8_t level = snapshot_level(snapshot, watched_pins[i].pin);
        if (level == watched_pins[i].last_level) {
            continue;
        }

        hal_gpio_edge_t edge = {
            .pin          = watched_pins[i].pin,
            .level        = level,
            .timestamp_ms = now,
            .seq          = edge_seq++,
        };
        watched_pins[i].last_level = level;
        diagnostics.gpio_edges_captured++;
        changed = true;
        if (edge_sink != NULL) {
            edge_sink(&edge);
        }
    }
    return changed;
}

static void disable_watched_irqs(void) {
    for (uint8_t i = 0; i < MAX_WATCHED_PINS; i++) {
        if (watched_pins[i].pin != HAL_INVALID_PIN) {
            drv_gpio_irq_dis((u32)watched_pins[i].pin);
        }
    }
}

static void enable_watched_irqs(void) {
    for (uint8_t i = 0; i < MAX_WATCHED_PINS; i++) {
        if (watched_pins[i].pin != HAL_INVALID_PIN) {
            drv_gpio_irq_en((u32)watched_pins[i].pin);
        }
    }
}

static void set_irq_polarities(const uint8_t *snapshot) {
    for (uint8_t i = 0; i < MAX_WATCHED_PINS; i++) {
        hal_gpio_pin_t pin = watched_pins[i].pin;
        if (pin == HAL_INVALID_PIN) {
            continue;
        }
        drv_gpio_irq_set((u32)pin, snapshot_level(snapshot, pin)
                         ? GPIO_FALLING_EDGE
                         : GPIO_RISING_EDGE);
    }
}

void hal_gpio_process_pending(void) {
    uint8_t snapshot[GPIO_PORT_COUNT];
    uint8_t tries = 0;
    bool    changed;

    if (!gpio_dispatch_pending) {
        return;
    }
    gpio_dispatch_pending = false;
    read_snapshot(snapshot);
    do {
        set_irq_polarities(snapshot);
        changed = capture_snapshot(snapshot);
        tries++;
    } while (changed && tries < MAX_REARM_TRIES);

    if (changed) {
        diagnostics.gpio_rearm_limit_hits++;
    }
    enable_watched_irqs();
    capture_snapshot(snapshot);
}

static void gpio_isr_callback(void) {
    uint8_t snapshot[GPIO_PORT_COUNT];

    diagnostics.gpio_irq_count++;
    capture_snapshot(snapshot);
    disable_watched_irqs();
    gpio_dispatch_pending = true;
}

static void init_isr(void) {
    if (is_isr_initialized) {
        return;
    }
    int result = drv_gpio_irq_config(GPIO_IRQ_MODE, 0, 0, gpio_isr_callback);
    if (result != 0) {
        printf("Failed to configure GPIO interrupt: %d\r\n", result);
    } else {
        is_isr_initialized = true;
    }
}

void hal_gpio_set_edge_sink(hal_gpio_edge_sink_t sink) {
    edge_sink = sink;
}

void hal_gpio_watch_pin(hal_gpio_pin_t gpio_pin) {
    watched_pin_t *slot = NULL;

    gpio_watch_init();
    init_isr();
    for (uint8_t i = 0; i < MAX_WATCHED_PINS; i++) {
        if (watched_pins[i].pin == gpio_pin) {
            return;
        }
        if (slot == NULL && watched_pins[i].pin == HAL_INVALID_PIN) {
            slot = &watched_pins[i];
        }
    }
    if (slot == NULL) {
        return;
    }

    slot->pin        = gpio_pin;
    slot->last_level = hal_gpio_read(gpio_pin);
    drv_gpio_irq_set((u32)gpio_pin, slot->last_level
                     ? GPIO_FALLING_EDGE
                     : GPIO_RISING_EDGE);
    drv_gpio_irq_en((u32)gpio_pin);
}

void hal_gpio_unwatch_pin(hal_gpio_pin_t gpio_pin) {
    for (uint8_t i = 0; i < MAX_WATCHED_PINS; i++) {
        if (watched_pins[i].pin == gpio_pin) {
            drv_gpio_irq_dis((u32)gpio_pin);
            watched_pins[i].pin = HAL_INVALID_PIN;
            return;
        }
    }
}

hal_gpio_diagnostics_t hal_gpio_get_diagnostics(void) {
    return diagnostics;
}

void telink_gpio_reinit_interrupts(void) {
    uint8_t snapshot[GPIO_PORT_COUNT];

    is_isr_initialized = false;
    init_isr();
    capture_snapshot(snapshot);
    gpio_dispatch_pending = true;
    hal_gpio_process_pending();
}

void telink_gpio_hal_setup_wake_ups(void) {
    for (uint8_t i = 0; i < MAX_WATCHED_PINS; i++) {
        hal_gpio_pin_t pin = watched_pins[i].pin;
        if (pin != HAL_INVALID_PIN) {
            cpu_set_gpio_wakeup(pin, hal_gpio_read(pin) ? 0 : 1, 1);
        }
    }
}
