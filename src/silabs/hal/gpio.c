#include <stdbool.h>
#include <stddef.h>
#include <string.h>

#ifdef SL_COMPONENT_CATALOG_PRESENT
#include "sl_component_catalog.h"
#endif
#include "sl_clock_manager.h"
#include "sl_gpio.h"
#if defined(SL_CATALOG_POWER_MANAGER_PRESENT)
#include "sl_power_manager.h"
#endif

#include "hal/gpio.h"
#include "hal/timer.h"
#include "silabs/hal/silabs_gpio_pin.h"
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

/* Set by the EXTI handler after delivering an edge, cleared by the main loop
 * that consumes it. */
static volatile bool edge_pending;

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
            };

            slot->last_level = level;
            diagnostics.gpio_edges_captured++;
            if (edge_sink != NULL) {
                edge_sink(&edge);
            }
            edge_pending = true;
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
}

/*
 * EFR32MG21/MG22 general-purpose pins: every pin of ports A-D the part
 * bonds out. Left out: PA1/PA2 (SWCLK/SWDIO, the debug and recovery
 * interface), PA5/PA6 while the debug USART owns them, PD0/PD1 while the
 * LFXO runs on them. The HFXO has dedicated pads. PA3/PA4 (SWV and JTAG TDI)
 * stay in: SWD does not use them and boards wire switches and relays to them.
 */
static bool rest_reserved(GPIO_Port_TypeDef port, uint8_t pin) {
    if (port == gpioPortA && (pin == 1 || pin == 2)) {
        return true;
    }
#if defined(SL_CATALOG_IOSTREAM_USART_PRESENT)
    if (port == gpioPortA && (pin == 5 || pin == 6)) {
        return true;
    }
#endif
#if defined(SL_CATALOG_DEVICE_INIT_LFXO_PRESENT)
    if (port == gpioPortD && (pin == 0 || pin == 1)) {
        return true;
    }
#endif
    return false;
}

void hal_gpio_rest_all(void) {
    hal_gpio_ensure_clock();
    for (uint8_t port = gpioPortA; port <= gpioPortD; port++) {
        for (uint8_t pin = 0; pin < 16; pin++) {
            sl_gpio_t sl_gpio = { .port = port, .pin = pin };

            if (!GPIO_PORT_PIN_VALID(port, pin) ||
                rest_reserved((GPIO_Port_TypeDef)port, pin) ||
                GPIO_PinModeGet((GPIO_Port_TypeDef)port, pin) > gpioModeInputPullFilter) {
                continue;
            }
            sl_gpio_set_pin_mode(&sl_gpio, SL_GPIO_MODE_INPUT_PULL, 0);
        }
    }
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

void hal_gpio_set_edge_sink(hal_gpio_edge_sink_t sink) {
    edge_sink = sink;
}

void hal_gpio_process_pending(void) {
    edge_pending = false;
}

#if defined(SL_CATALOG_POWER_MANAGER_PRESENT)

/*
 * Edge sinks only queue work for the main loop. Without these hooks the power
 * manager may go back to sleep after the EXTI interrupt (when the wake-up
 * also restored the HF clock) or enter sleep between the main loop pass and
 * the sleep call, leaving the press unhandled until the next stack event.
 */
bool app_is_ok_to_sleep(void) {
    return !edge_pending;
}

sl_power_manager_on_isr_exit_t app_sleep_on_isr_exit(void) {
    return edge_pending ? SL_POWER_MANAGER_WAKEUP : SL_POWER_MANAGER_IGNORE;
}

#endif

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

hal_gpio_diagnostics_t hal_gpio_get_diagnostics(void) {
    return diagnostics;
}

// silabs_gpio_pin.h encodes ports without emlib; fail the build on mismatch.
typedef char silabs_gpio_pin_encoding_matches_emlib[
        (gpioPortA == 0 && gpioPortB == 1 && gpioPortC == 2 && gpioPortD == 3) ?
        1 : -1];

hal_gpio_pin_t hal_gpio_parse_pin(const char *s) {
    return silabs_gpio_pin_from_text(s);
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
