#include "button.h"

#include "base_components/button_dispatcher.h"
#include "hal/timer.h"
#include <stdbool.h>
#include <stddef.h>

#define BUTTON_CALLBACK_MAX    16

static button_t *button_callbacks[BUTTON_CALLBACK_MAX];
static bool      button_callback_sink_registered;

static void button_gesture_timer(void *arg) {
    button_t *button = (button_t *)arg;

    if (!button->pressed || button->long_pressed) {
        return;
    }
    button->long_pressed = true;
    if (button->on_long_press != NULL) {
        button->on_long_press(button->callback_param);
    }
}

static void button_callback_sink(const button_event_t *event, void *arg) {
    button_t *button;

    (void)arg;
    if (event->button_id >= BUTTON_CALLBACK_MAX) {
        return;
    }
    button = button_callbacks[event->button_id];
    if (button == NULL) {
        return;
    }

    if (event->type == BUTTON_EVENT_DOWN) {
        uint32_t elapsed;

        button->pressed       = true;
        button->long_pressed  = false;
        button->pressed_at_ms = event->timestamp_ms;
        if (button->on_press != NULL) {
            button->on_press(button->callback_param);
        }
        if (event->timestamp_ms - button->released_at_ms <
            button->multi_press_duration_ms) {
            button->multi_press_cnt++;
            if (button->on_multi_press != NULL) {
                button->on_multi_press(button->callback_param,
                                       button->multi_press_cnt);
            }
        } else {
            button->multi_press_cnt = 1;
        }
        elapsed = hal_millis() - event->timestamp_ms;
        timer_restart(&button->gesture_timer,
                      elapsed < button->long_press_duration_ms
                      ? button->long_press_duration_ms - elapsed
                      : 0);
        return;
    }

    timer_cancel(&button->gesture_timer);
    button->released_at_ms = event->timestamp_ms;
    button->pressed        = false;
    button->long_pressed   = false;
    if (button->on_release != NULL) {
        button->on_release(button->callback_param);
    }
}

void btn_init(button_t *button) {
    button_config_t config = {
        .pin         = button->pin,
        .active_high = button->pressed_when_high,
        .debounce_ms = button->debounce_delay_ms,
    };

    if (!button_callback_sink_registered) {
        button_dispatcher_register(button_callback_sink, NULL);
        button_callback_sink_registered = true;
    }
    timer_init(&button->gesture_timer, button_gesture_timer, button);
    button->button_id = button_input_add(&config);
    if (button->button_id < BUTTON_CALLBACK_MAX) {
        button_callbacks[button->button_id] = button;
    }
    button->pressed = button_input_is_down(button->button_id);
    if (button->pressed) {
        button->long_pressed = true;
    }
}

uint32_t btn_gpio_edges_dropped(void) {
    return button_input_gpio_edges_dropped();
}
