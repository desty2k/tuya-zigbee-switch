#include "base_components/button_dispatcher.h"
#include "base_components/button_input.h"
#include "base_components/gesture_fsm.h"
#include "support/fake_clock.h"
#include "support/fake_gpio.h"
#include "support/fake_tasks.h"
#include "support/runner.h"

#define GESTURE_PIN    ((hal_gpio_pin_t)4)
#define GESTURE_LOG_MAX    32

static gesture_event_t gesture_log[GESTURE_LOG_MAX];
static uint8_t         gesture_count;
static uint32_t        next_press_id;

static void record_gesture(const gesture_event_t *event, void *arg) {
    (void)arg;
    if (gesture_count < GESTURE_LOG_MAX) {
        gesture_log[gesture_count++] = *event;
    }
}

static void setup_gesture(uint16_t hold_ms, uint8_t initial_level) {
    button_config_t button = {
        .pin = GESTURE_PIN, .active_high = false, .debounce_ms = 8,
    };
    gesture_config_t gesture = {
        .hold_ms = hold_ms, .multi_click_gap_ms = 350,
    };

    fake_clock_reset();
    fake_tasks_reset();
    fake_gpio_reset();
    fake_gpio_set_input(GESTURE_PIN, initial_level);
    gesture_count = 0;
    next_press_id = 0;
    button_input_init();
    button_input_add(&button);
    gesture_fsm_init();
    gesture_fsm_add(0, &gesture);
    gesture_fsm_register_sink(record_gesture, NULL);
}

static void emit_down(uint32_t at_ms) {
    button_event_t event;

    fake_clock_set(at_ms);
    next_press_id++;
    event.button_id    = 0;
    event.type         = BUTTON_EVENT_DOWN;
    event.timestamp_ms = at_ms;
    event.seq          = next_press_id * 2 - 2;
    event.press_id     = next_press_id;
    button_dispatcher_emit(&event);
}

static void emit_up(uint32_t at_ms) {
    button_event_t event;

    fake_clock_set(at_ms);
    event.button_id    = 0;
    event.type         = BUTTON_EVENT_UP;
    event.timestamp_ms = at_ms;
    event.seq          = next_press_id * 2 - 1;
    event.press_id     = next_press_id;
    button_dispatcher_emit(&event);
}

static void poll_at(uint32_t at_ms) {
    fake_clock_set(at_ms);
    fake_tasks_poll();
}

TEST(single_click) {
    setup_gesture(800, 1);
    emit_down(0);
    emit_up(20);
    poll_at(370);
    ASSERT_EQ(1, gesture_count);
    ASSERT_EQ(GESTURE_N_CLICK, gesture_log[0].type);
    ASSERT_EQ(1, gesture_log[0].count);
}

TEST(double_click) {
    setup_gesture(800, 1);
    emit_down(0);
    emit_up(20);
    emit_down(120);
    emit_up(140);
    poll_at(490);
    ASSERT_EQ(1, gesture_count);
    ASSERT_EQ(2, gesture_log[0].count);
}

TEST(five_clicks) {
    setup_gesture(800, 1);
    for (uint8_t i = 0; i < 5; i++) {
        emit_down(i * 50);
        emit_up(i * 50 + 20);
    }
    poll_at(570);
    ASSERT_EQ(1, gesture_count);
    ASSERT_EQ(5, gesture_log[0].count);
}

TEST(ten_clicks_emit_immediately) {
    setup_gesture(800, 1);
    for (uint8_t i = 0; i < 10; i++) {
        emit_down(i * 50);
        emit_up(i * 50 + 20);
    }
    ASSERT_EQ(1, gesture_count);
    ASSERT_EQ(GESTURE_N_CLICK, gesture_log[0].type);
    ASSERT_EQ(10, gesture_log[0].count);
    ASSERT_EQ(470, gesture_log[0].timestamp_ms);
}

TEST(hold_has_start_and_end_without_click) {
    setup_gesture(800, 1);
    emit_down(100);
    poll_at(900);
    emit_up(1100);
    ASSERT_EQ(2, gesture_count);
    ASSERT_EQ(GESTURE_HOLD_START, gesture_log[0].type);
    ASSERT_EQ(GESTURE_HOLD_END, gesture_log[1].type);
    ASSERT_EQ(1000, gesture_log[1].duration_ms);
}

TEST(hold_flushes_click_sequence) {
    setup_gesture(800, 1);
    emit_down(0);
    emit_up(20);
    emit_down(100);
    poll_at(900);
    emit_up(1000);
    ASSERT_EQ(3, gesture_count);
    ASSERT_EQ(GESTURE_N_CLICK, gesture_log[0].type);
    ASSERT_EQ(1, gesture_log[0].count);
    ASSERT_EQ(GESTURE_HOLD_START, gesture_log[1].type);
    ASSERT_EQ(GESTURE_HOLD_END, gesture_log[2].type);
}

TEST(gap_separates_clicks) {
    setup_gesture(800, 1);
    emit_down(0);
    emit_up(20);
    poll_at(370);
    emit_down(500);
    emit_up(520);
    poll_at(870);
    ASSERT_EQ(2, gesture_count);
    ASSERT_EQ(1, gesture_log[0].count);
    ASSERT_EQ(1, gesture_log[1].count);
}

TEST(zero_hold_disables_hold) {
    setup_gesture(0, 1);
    emit_down(0);
    poll_at(5000);
    emit_up(5000);
    poll_at(5350);
    ASSERT_EQ(1, gesture_count);
    ASSERT_EQ(GESTURE_N_CLICK, gesture_log[0].type);
}

TEST(boot_press_release_is_suppressed) {
    setup_gesture(800, 0);
    emit_up(50);
    poll_at(1000);
    ASSERT_EQ(0, gesture_count);
}

TEST(hold_wins_at_exact_release_time) {
    setup_gesture(800, 1);
    emit_down(0);
    poll_at(800);
    emit_up(800);
    ASSERT_EQ(2, gesture_count);
    ASSERT_EQ(GESTURE_HOLD_START, gesture_log[0].type);
    ASSERT_EQ(GESTURE_HOLD_END, gesture_log[1].type);
}

int main(void) {
    RUN_TEST(single_click);
    RUN_TEST(double_click);
    RUN_TEST(five_clicks);
    RUN_TEST(ten_clicks_emit_immediately);
    RUN_TEST(hold_has_start_and_end_without_click);
    RUN_TEST(hold_flushes_click_sequence);
    RUN_TEST(gap_separates_clicks);
    RUN_TEST(zero_hold_disables_hold);
    RUN_TEST(boot_press_release_is_suppressed);
    RUN_TEST(hold_wins_at_exact_release_time);
    return test_runner_failures == 0 ? 0 : 1;
}
