#include <stdbool.h>
#include <stddef.h>
#include <string.h>

#include "sl_clock_manager.h"
#include "sl_gpio.h"

#include "hal/gpio.h"
#include "hal/timer.h"
#include "silabs/hal/silabs_gpio_utils.h"
#include <stdio.h>

#define LINE_MISSING     0xFF
#define MAX_INT_LINES    16

// ------ One-time init guards ------
static bool s_gpio_clock_enabled = false;
static bool s_gpio_inited        = false;

static void hal_gpio_ensure_clock(void) {
    if (!s_gpio_clock_enabled) {
        sl_clock_manager_enable_bus_clock(SL_BUS_CLOCK_GPIO);
        s_gpio_clock_enabled = true;
    }
}

static void hal_gpio_ensure_gpio_init(void) {
    if (!s_gpio_inited) {
        sl_gpio_init();
        s_gpio_inited = true;
    }
}

// ------ Per-interrupt bookkeeping ------
typedef struct {
    bool           in_use;
    hal_gpio_pin_t hal_pin;
    uint8_t        last_level;
    int32_t        line;
} int_slot_t;

static int_slot_t             s_slots[MAX_INT_LINES]; // 16 EXTI lines total
static hal_gpio_edge_sink_t   edge_sink;
static hal_gpio_diagnostics_t diagnostics;
static uint32_t edge_seq;

static int32_t alloc_int_slot(void) {
    for (uint8_t i = 0; i < 16; i++) {
        if (!s_slots[i].in_use) {
            s_slots[i].in_use = true;
            s_slots[i].line   = SL_GPIO_INTERRUPT_UNAVAILABLE;
            return i;
        }
    }
    return LINE_MISSING;
}

static void free_int_slot(int32_t slot_no) {
    if (slot_no < MAX_INT_LINES) {
        memset(&s_slots[slot_no], 0, sizeof(s_slots[slot_no]));
    }
}

static void _dispatch_regular(uint8_t intNo, void *ctx) {
    (void)intNo;
    int_slot_t *slot = (int_slot_t *)ctx;

    diagnostics.gpio_irq_count++;
    if (slot != NULL) {
        uint8_t level = hal_gpio_read(slot->hal_pin);
        if (level != slot->last_level) {
            hal_gpio_edge_t edge = {
                .pin          = slot->hal_pin,
                .level        = level,
                .timestamp_ms = hal_millis(),
                .seq          = edge_seq++,
            };

            slot->last_level = level;
            diagnostics.gpio_edges_captured++;
            if (edge_sink != NULL) {
                edge_sink(&edge);
            }
        }
    }
}

// API
void hal_gpio_init(hal_gpio_pin_t gpio_pin, uint8_t is_input,
                   hal_gpio_pull_t pull_direction) {
    hal_gpio_ensure_clock();

    const sl_gpio_t sl_gpio = silabs_hal_gpio_to_sl_gpio(gpio_pin);

    if (is_input) {
        switch (pull_direction) {
        case HAL_GPIO_PULL_UP:
            sl_gpio_set_pin_mode(&sl_gpio, SL_GPIO_MODE_INPUT_PULL,
                                 1); // DOUT=1 => pull-up
            break;
        case HAL_GPIO_PULL_DOWN:
            sl_gpio_set_pin_mode(&sl_gpio, SL_GPIO_MODE_INPUT_PULL,
                                 0); // DOUT=0 => pull-down
            break;
        default:
            sl_gpio_set_pin_mode(&sl_gpio, SL_GPIO_MODE_INPUT, 0);
            break;
        }
    } else {
        // Output: push-pull, initial low
        sl_gpio_set_pin_mode(&sl_gpio, SL_GPIO_MODE_PUSH_PULL, 0);
    }

    // Optional: store pull direction for interrupt polarity later.
    // We don’t keep a global pin map; polarity is picked at registration time
    // by looking up the slot when the user calls hal_gpio_int_callback.
}

void hal_gpio_set(hal_gpio_pin_t gpio_pin) {
    const sl_gpio_t sl_gpio = silabs_hal_gpio_to_sl_gpio(gpio_pin);

    sl_gpio_set_pin(&sl_gpio);
}

void hal_gpio_clear(hal_gpio_pin_t gpio_pin) {
    const sl_gpio_t sl_gpio = silabs_hal_gpio_to_sl_gpio(gpio_pin);

    sl_gpio_clear_pin(&sl_gpio);
}

uint8_t hal_gpio_read(hal_gpio_pin_t gpio_pin) {
    const sl_gpio_t sl_gpio = silabs_hal_gpio_to_sl_gpio(gpio_pin);
    bool            value   = 0;

    sl_gpio_get_pin_input(&sl_gpio, &value);
    return value;
}

// Register an interrupt that also attempts EM4 wake-up.
// - Wakes from EM2/EM3 via normal EXTI (edge-sensitive).
// - Wakes from EM4 if the pin supports EM4WU (level-sensitive).
//   Polarity rule:
//     * If input has pull-up  -> active-low (wake on low level).
//     * If input has pull-down-> active-high (wake on high level).
//     * Otherwise default to rising+falling EXTI and active-low EM4WU.
void hal_gpio_set_edge_sink(hal_gpio_edge_sink_t sink) {
    edge_sink = sink;
}

void hal_gpio_watch_pin(hal_gpio_pin_t gpio_pin) {
    hal_gpio_ensure_clock();
    hal_gpio_ensure_gpio_init();

    for (uint8_t i = 0; i < MAX_INT_LINES; i++) {
        if (s_slots[i].in_use && s_slots[i].hal_pin == gpio_pin) {
            return;
        }
    }

    const sl_gpio_t sl_gpio = silabs_hal_gpio_to_sl_gpio(gpio_pin);

    // Allocate a regular EXTI line (for edge interrupts while awake)
    int32_t slot_no = alloc_int_slot();
    if (slot_no == LINE_MISSING) {
        printf("hal_gpio_watch_pin: no free EXTI lines\r\n");
        return;
    }
    int_slot_t *slot = &s_slots[slot_no];
    slot->hal_pin    = gpio_pin;
    slot->last_level = hal_gpio_read(gpio_pin);

    // Register regular edge-sensitive callback (both edges)

    sl_status_t status = sl_gpio_configure_external_interrupt(
        &sl_gpio, &slot->line, SL_GPIO_INTERRUPT_RISING_FALLING_EDGE,
        (sl_gpio_irq_callback_t)_dispatch_regular, slot);
    printf("hal_gpio_watch_pin: exti line %ld status %lu\r\n", slot->line,
           (unsigned long)status);
}

void hal_gpio_unwatch_pin(hal_gpio_pin_t gpio_pin) {
    printf("hal_gpio_unwatch_pin pin %02X\r\n", gpio_pin);

    for (uint8_t i = 0; i < MAX_INT_LINES; i++) {
        if (s_slots[i].in_use && s_slots[i].hal_pin == gpio_pin) {
            sl_gpio_deconfigure_external_interrupt(s_slots[i].line);
            free_int_slot(i);
            return;
        }
    }
}

hal_gpio_diagnostics_t hal_gpio_get_diagnostics(void) {
    return diagnostics;
}

hal_gpio_pin_t hal_gpio_parse_pin(const char *s) {
    if (!s || s[0] < 'A' || s[0] > 'D' || s[1] < '0' || s[1] > '8')
        return HAL_INVALID_PIN;

    static const uint8_t ports[] = { gpioPortA, gpioPortB, gpioPortC, gpioPortD };
    return (hal_gpio_pin_t)((ports[s[0] - 'A'] << 8) | (uint8_t)(s[1] - '0'));
}

hal_gpio_pull_t hal_gpio_parse_pull(const char *pull_str) {
    if (pull_str[0] == 'u' || pull_str[0] == 'U') {
        return HAL_GPIO_PULL_UP;
    }
    if (pull_str[0] == 'd') {
        return HAL_GPIO_PULL_DOWN;
    }
    if (pull_str[0] == 'f') {
        return HAL_GPIO_PULL_NONE;
    }
    return HAL_GPIO_PULL_INVALID;
}
