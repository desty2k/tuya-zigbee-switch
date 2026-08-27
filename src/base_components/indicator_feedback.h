#ifndef _INDICATOR_FEEDBACK_H_
#define _INDICATOR_FEEDBACK_H_

#include "base_components/led.h"
#include <stdbool.h>
#include <stdint.h>

typedef struct {
    led_t *     led;
    bool        base_on;
    uint8_t     priority;
    app_timer_t restore_timer;
} indicator_feedback_t;

void indicator_feedback_init(indicator_feedback_t *feedback, led_t *led);
void indicator_feedback_set_base_state(indicator_feedback_t *feedback,
                                       bool on);
void indicator_feedback_request_press(indicator_feedback_t *feedback);
void indicator_feedback_request_click_count(indicator_feedback_t *feedback,
                                            uint8_t count);
void indicator_feedback_network_connected(indicator_feedback_t *feedback);
void indicator_feedback_network_not_connected(indicator_feedback_t *feedback);
void indicator_feedback_commission_success(indicator_feedback_t *feedback);

#endif
