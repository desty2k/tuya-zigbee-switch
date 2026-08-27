#include "relay_driver.h"

#include "hal/printf_selector.h"
#include <stddef.h>

extern uint8_t allow_simultaneous_latching_pulses;

static relay_driver_t *relay_driver_active_latch;

static void relay_driver_start_latch(relay_driver_t *driver);

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
        hal_gpio_write(pin, driver->on_high);
        relay_driver_active_latch = driver;
        driver->latch_waiting     = 0;
        timer_restart(&driver->latch_timer, RELAY_LATCH_PULSE_MS);
    } else {
        printf("relay_driver_start_latch: another pulse is active\r\n");
        driver->latch_waiting = 1;
        timer_restart(&driver->latch_timer, RELAY_LATCH_WAIT_MS);
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
    driver->pending_on = on;
    relay_driver_start_latch(driver);
}
