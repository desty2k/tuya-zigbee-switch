#include "relay_driver.h"

#include "hal/printf_selector.h"
#include <stddef.h>

extern uint8_t allow_simultaneous_latching_pulses;

static relay_driver_t *relay_driver_active_latch;
static relay_driver_t *relay_driver_retry[RELAY_DRIVER_MAX_RETRY];

static void relay_driver_start_latch(relay_driver_t *driver);

static void relay_driver_forget_retry(relay_driver_t *driver) {
    driver->retry_pending = false;
    for (uint8_t i = 0; i < RELAY_DRIVER_MAX_RETRY; i++) {
        if (relay_driver_retry[i] == driver) {
            relay_driver_retry[i] = NULL;
        }
    }
}

/* Every driver is a distinct relay and at most RELAY_DRIVER_MAX_RETRY relays
 * exist, so a free slot is always available for a driver not yet queued. */
static void relay_driver_queue_retry(relay_driver_t *driver) {
    if (driver->retry_pending) {
        return;
    }
    for (uint8_t i = 0; i < RELAY_DRIVER_MAX_RETRY; i++) {
        if (relay_driver_retry[i] == NULL) {
            relay_driver_retry[i] = driver;
            driver->retry_pending = true;
            return;
        }
    }
}

static void relay_driver_end_latch(relay_driver_t *driver) {
    hal_gpio_write(driver->on_pin, !driver->on_high);
    hal_gpio_write(driver->off_pin, !driver->on_high);
    if (relay_driver_active_latch == driver) {
        relay_driver_active_latch = NULL;
    }
}

static void relay_driver_start_latch(relay_driver_t *driver) {
    hal_gpio_pin_t pin = driver->pending_on ? driver->on_pin
                                           : driver->off_pin;

    if (relay_driver_active_latch == NULL ||
        allow_simultaneous_latching_pulses) {
        driver->latch_waiting = 0;
        timer_restart(&driver->latch_timer, RELAY_LATCH_PULSE_MS);
        if (!timer_is_active(&driver->latch_timer)) {
            relay_driver_queue_retry(driver);
            return;
        }
        hal_gpio_write(pin, driver->on_high);
        relay_driver_active_latch = driver;
    } else {
        printf("relay_driver_start_latch: another pulse is active\r\n");
        driver->latch_waiting = 1;
        timer_restart(&driver->latch_timer, RELAY_LATCH_WAIT_MS);
        if (!timer_is_active(&driver->latch_timer)) {
            relay_driver_queue_retry(driver);
        }
    }
}

static void relay_driver_latch_handler(void *arg) {
    relay_driver_t *driver = (relay_driver_t *)arg;

    if (driver->latch_waiting) {
        relay_driver_start_latch(driver);
    } else {
        relay_driver_end_latch(driver);
    }
}

void relay_driver_init(relay_driver_t *driver) {
    relay_driver_forget_retry(driver);
    driver->latch_waiting = 0;
    driver->pending_on    = false;
    timer_init(&driver->latch_timer, relay_driver_latch_handler, driver);
    hal_gpio_write(driver->on_pin, !driver->on_high);
    if (driver->is_latching) {
        hal_gpio_write(driver->off_pin, !driver->on_high);
    }
}

void relay_driver_apply(relay_driver_t *driver, bool on) {
    if (!driver->is_latching) {
        hal_gpio_write(driver->on_pin, on == (driver->on_high != 0));
        return;
    }

    relay_driver_end_latch(driver);
    timer_cancel(&driver->latch_timer);
    relay_driver_forget_retry(driver);
    driver->pending_on = on;
    relay_driver_start_latch(driver);
}

void relay_driver_process_pending(void) {
    for (uint8_t i = 0; i < RELAY_DRIVER_MAX_RETRY; i++) {
        relay_driver_t *driver = relay_driver_retry[i];

        if (driver != NULL) {
            relay_driver_retry[i] = NULL;
            driver->retry_pending = false;
            relay_driver_start_latch(driver);
        }
    }
}
