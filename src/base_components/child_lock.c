#include "child_lock.h"

#include "base_components/button_dispatcher.h"
#include "base_components/timer_service.h"
#include <stddef.h>

typedef struct {
    uint8_t first;
    uint8_t second;
} child_lock_gesture_t;

static bool locked;
static child_lock_gesture_t gestures[CHILD_LOCK_GESTURE_MAX];
static uint8_t               gesture_count;
static uint8_t               armed_gesture;
static app_timer_t           hold_timer;
static bool                  hold_timer_ready;
static indicator_feedback_t *indicator;
static child_lock_observer_t observer;
static void *                observer_context;

/* Bit n: the current or last press of button n started while locked. */
static uint16_t blocked_presses;

static bool gesture_held(const child_lock_gesture_t *gesture) {
    return button_input_is_down(gesture->first) &&
           (gesture->second == CHILD_LOCK_NO_BUTTON ||
            button_input_is_down(gesture->second));
}

static void hold_elapsed(void *arg) {
    (void)arg;
    if (armed_gesture >= gesture_count || !gesture_held(&gestures[armed_gesture])) {
        return;
    }
    locked = !locked;
    indicator_feedback_request_blinks(indicator, locked ? CHILD_LOCK_BLINKS_LOCK :
                                      CHILD_LOCK_BLINKS_UNLOCK);
    if (observer != NULL) {
        observer(observer_context, locked);
    }
}

/* One gesture is timed at a time: the first one held keeps the timer until
 * any of its buttons is released. */
static void track_gestures(const button_event_t *event, void *arg) {
    (void)event;
    (void)arg;
    if (timer_is_active(&hold_timer)) {
        if (gesture_held(&gestures[armed_gesture])) {
            return;
        }
        timer_cancel(&hold_timer);
    }
    for (uint8_t i = 0; i < gesture_count; i++) {
        if (gesture_held(&gestures[i])) {
            armed_gesture = i;
            (void)timer_restart(&hold_timer,
                                gestures[i].second == CHILD_LOCK_NO_BUTTON ?
                                CHILD_LOCK_SINGLE_HOLD_MS : CHILD_LOCK_PAIR_HOLD_MS);
            return;
        }
    }
}

void child_lock_reset_wiring(void) {
    if (!hold_timer_ready) {
        timer_init(&hold_timer, hold_elapsed, NULL);
        hold_timer_ready = true;
    }
    timer_cancel(&hold_timer);
    gesture_count   = 0;
    armed_gesture   = 0;
    indicator       = NULL;
    blocked_presses = 0;
}

void child_lock_register_input(void) {
    button_dispatcher_register(track_gestures, NULL);
}

bool child_lock_add_gesture(uint8_t first, uint8_t second) {
    if (gesture_count >= CHILD_LOCK_GESTURE_MAX || first >= BUTTON_INPUT_MAX ||
        (second != CHILD_LOCK_NO_BUTTON && second >= BUTTON_INPUT_MAX)) {
        return false;
    }
    gestures[gesture_count++] = (child_lock_gesture_t){ first, second };
    return true;
}

void child_lock_set_indicator(indicator_feedback_t *feedback) {
    indicator = feedback;
}

void child_lock_set_observer(child_lock_observer_t new_observer, void *context) {
    observer         = new_observer;
    observer_context = context;
}

void child_lock_set(bool on) {
    locked = on;
}

bool child_lock_is_on(void) {
    return locked;
}

bool child_lock_blocks(const button_event_t *event) {
    uint16_t bit;

    if (event == NULL || event->button_id >= BUTTON_INPUT_MAX) {
        return false;
    }
    bit = (uint16_t)(1u << event->button_id);
    if (event->type == BUTTON_EVENT_DOWN) {
        if (!locked) {
            blocked_presses &= (uint16_t) ~bit;
            return false;
        }
        blocked_presses |= bit;
        indicator_feedback_request_blinks(indicator, CHILD_LOCK_BLINKS_PRESS);
        return true;
    }
    return (blocked_presses & bit) != 0;
}

bool child_lock_press_blocked(uint8_t button_id) {
    return button_id < BUTTON_INPUT_MAX &&
           (blocked_presses & (uint16_t)(1u << button_id)) != 0;
}
