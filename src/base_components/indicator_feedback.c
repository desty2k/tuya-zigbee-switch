#include "indicator_feedback.h"
#include <stddef.h>

enum {
    INDICATOR_PRIORITY_BASE,
    INDICATOR_PRIORITY_BUTTON,
    INDICATOR_PRIORITY_COMMISSIONING,
    INDICATOR_PRIORITY_NETWORK,
};

static void indicator_feedback_apply_base(indicator_feedback_t *feedback) {
    if (feedback->base_on) {
        led_on(feedback->led);
    } else {
        led_off(feedback->led);
    }
}

static void indicator_feedback_restore(void *arg) {
    indicator_feedback_t *feedback = (indicator_feedback_t *)arg;

    if (feedback->priority == INDICATOR_PRIORITY_BUTTON ||
        feedback->priority == INDICATOR_PRIORITY_COMMISSIONING) {
        feedback->priority = INDICATOR_PRIORITY_BASE;
        indicator_feedback_apply_base(feedback);
    }
}

static void indicator_feedback_start_finite(indicator_feedback_t *feedback,
                                            uint16_t on_time_ms,
                                            uint16_t off_time_ms,
                                            uint16_t times,
                                            uint8_t priority) {
    feedback->priority = priority;
    led_blink(feedback->led, on_time_ms, off_time_ms, times);
    timer_restart(&feedback->restore_timer,
                  (uint32_t)times * (on_time_ms + off_time_ms));
}

void indicator_feedback_init(indicator_feedback_t *feedback, led_t *led) {
    if (feedback == NULL || led == NULL) {
        return;
    }

    feedback->led      = led;
    feedback->base_on  = false;
    feedback->priority = INDICATOR_PRIORITY_BASE;
    timer_init(&feedback->restore_timer, indicator_feedback_restore, feedback);
    led_init(led);
}

void indicator_feedback_set_base_state(indicator_feedback_t *feedback,
                                       bool on) {
    if (feedback == NULL || feedback->led == NULL) {
        return;
    }

    feedback->base_on = on;
    if (feedback->priority == INDICATOR_PRIORITY_BASE) {
        indicator_feedback_apply_base(feedback);
    }
}

void indicator_feedback_request_press(indicator_feedback_t *feedback) {
    if (feedback == NULL || feedback->led == NULL ||
        feedback->priority != INDICATOR_PRIORITY_BASE) {
        return;
    }

    indicator_feedback_start_finite(feedback, 50, 50, 1,
                                    INDICATOR_PRIORITY_BUTTON);
}

void indicator_feedback_request_click_count(indicator_feedback_t *feedback,
                                            uint8_t count) {
    if (feedback == NULL || feedback->led == NULL || count < 2 || count > 3 ||
        feedback->priority != INDICATOR_PRIORITY_BASE) {
        return;
    }

    indicator_feedback_start_finite(feedback, 60, 70, count,
                                    INDICATOR_PRIORITY_BUTTON);
}

void indicator_feedback_network_connected(indicator_feedback_t *feedback) {
    if (feedback == NULL || feedback->led == NULL ||
        feedback->priority != INDICATOR_PRIORITY_NETWORK) {
        return;
    }

    timer_cancel(&feedback->restore_timer);
    feedback->priority = INDICATOR_PRIORITY_BASE;
    indicator_feedback_apply_base(feedback);
}

void indicator_feedback_network_not_connected(indicator_feedback_t *feedback) {
    if (feedback == NULL || feedback->led == NULL ||
        feedback->priority == INDICATOR_PRIORITY_NETWORK) {
        return;
    }

    timer_cancel(&feedback->restore_timer);
    feedback->priority = INDICATOR_PRIORITY_NETWORK;
    led_blink(feedback->led, 500, 500, LED_BLINK_FOREVER);
}

void indicator_feedback_commission_success(indicator_feedback_t *feedback) {
    if (feedback == NULL || feedback->led == NULL ||
        feedback->priority >= INDICATOR_PRIORITY_COMMISSIONING) {
        return;
    }

    timer_cancel(&feedback->restore_timer);
    indicator_feedback_start_finite(feedback, 500, 500, 7,
                                    INDICATOR_PRIORITY_COMMISSIONING);
}
