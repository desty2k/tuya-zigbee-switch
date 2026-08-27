#ifndef _RELAY_DRIVER_H_
#define _RELAY_DRIVER_H_

#include "base_components/timer_service.h"
#include "hal/gpio.h"
#include <stdbool.h>
#include <stdint.h>

#define RELAY_LATCH_PULSE_MS    100
#define RELAY_LATCH_WAIT_MS     50

typedef struct {
    hal_gpio_pin_t on_pin;
    hal_gpio_pin_t off_pin;
    uint8_t        on_high;
    uint8_t        is_latching;
    app_timer_t    latch_timer;
    uint8_t        latch_waiting;
    bool           pending_on;
} relay_driver_t;

void relay_driver_init(relay_driver_t *driver);
void relay_driver_apply(relay_driver_t *driver, bool on);

#endif
