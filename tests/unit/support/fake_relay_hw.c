#include "fake_relay_hw.h"

#include "hal/timer.h"
#include "support/fake_clock.h"
#include <stddef.h>
#include <string.h>

#define FAKE_RELAY_HW_PIN_MAX      256
#define FAKE_RELAY_HW_LOG_SIZE     128

static uint8_t               fake_relay_hw_levels[FAKE_RELAY_HW_PIN_MAX];
static fake_relay_hw_write_t fake_relay_hw_log[FAKE_RELAY_HW_LOG_SIZE];
static size_t                fake_relay_hw_log_count;

static void fake_relay_hw_write(hal_gpio_pin_t pin, uint8_t level) {
    if (pin < FAKE_RELAY_HW_PIN_MAX) {
        fake_relay_hw_levels[pin] = level != 0;
    }
    if (fake_relay_hw_log_count < FAKE_RELAY_HW_LOG_SIZE) {
        fake_relay_hw_log[fake_relay_hw_log_count].pin          = pin;
        fake_relay_hw_log[fake_relay_hw_log_count].level        = level != 0;
        fake_relay_hw_log[fake_relay_hw_log_count].timestamp_ms =
            hal_millis();
        fake_relay_hw_log_count++;
    }
}

void fake_relay_hw_reset(void) {
    memset(fake_relay_hw_levels, 0, sizeof(fake_relay_hw_levels));
    fake_relay_hw_clear_log();
}

void fake_relay_hw_clear_log(void) {
    memset(fake_relay_hw_log, 0, sizeof(fake_relay_hw_log));
    fake_relay_hw_log_count = 0;
}

uint8_t fake_relay_hw_level(hal_gpio_pin_t pin) {
    return pin < FAKE_RELAY_HW_PIN_MAX ? fake_relay_hw_levels[pin] : 0;
}

size_t fake_relay_hw_write_count(void) {
    return fake_relay_hw_log_count;
}

const fake_relay_hw_write_t *fake_relay_hw_write_at(size_t index) {
    return index < fake_relay_hw_log_count ? &fake_relay_hw_log[index] : NULL;
}

void hal_gpio_init(hal_gpio_pin_t pin, uint8_t is_input,
                   hal_gpio_pull_t pull) {
    (void)pin;
    (void)is_input;
    (void)pull;
}

void hal_gpio_set(hal_gpio_pin_t pin) {
    fake_relay_hw_write(pin, 1);
}

void hal_gpio_clear(hal_gpio_pin_t pin) {
    fake_relay_hw_write(pin, 0);
}

uint8_t hal_gpio_read(hal_gpio_pin_t pin) {
    return fake_relay_hw_level(pin);
}

void hal_gpio_set_edge_sink(hal_gpio_edge_sink_t sink) {
    (void)sink;
}

void hal_gpio_watch_pin(hal_gpio_pin_t pin) {
    (void)pin;
}

void hal_gpio_unwatch_pin(hal_gpio_pin_t pin) {
    (void)pin;
}

hal_gpio_diagnostics_t hal_gpio_get_diagnostics(void) {
    hal_gpio_diagnostics_t diagnostics = { 0 };

    return diagnostics;
}

hal_gpio_pin_t hal_gpio_parse_pin(const char *text) {
    (void)text;
    return HAL_INVALID_PIN;
}

hal_gpio_pull_t hal_gpio_parse_pull(const char *text) {
    (void)text;
    return HAL_GPIO_PULL_INVALID;
}
