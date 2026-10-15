#include "base_components/action_mapper.h"
#include "support/fake_clock.h"
#include "support/fake_relay_hw.h"
#include "support/fake_tasks.h"
#include "support/runner.h"
#include <stdint.h>

uint8_t allow_simultaneous_latching_pulses;
uint8_t g_multi_press_reset_count;

static gesture_sink_t registered_gesture_sink;
static uint8_t        factory_resets;
static uint8_t        cover_calls;
static uint8_t        cover_target;
static uint16_t       cover_command;

void gesture_fsm_register_sink(gesture_sink_t sink, void *context) {
    (void)context;
    registered_gesture_sink = sink;
}

void hal_factory_reset(void) {
    factory_resets++;
}

static void cover_sink(void *context, uint8_t target, uint16_t command) {
    (void)context;
    cover_calls++;
    cover_target  = target;
    cover_command = command;
}

static void gesture(uint8_t button_id, gesture_type_t type, uint8_t count) {
    registered_gesture_sink(&(gesture_event_t){ .button_id = button_id,
                                                .type      = type,
                                                .count     = count }, NULL);
}

static void reset_mapper(void) {
    relay_driver_t driver = { .on_pin = 10, .off_pin = HAL_INVALID_PIN,
                              .on_high = 1, .is_latching = 0 };
    static relay_driver_t persistent_driver;
    static relay_config_t config;

    fake_clock_reset();
    fake_tasks_reset();
    fake_relay_hw_reset();
    persistent_driver = driver;
    config = (relay_config_t){ 0 };
    relay_ctrl_init();
    ASSERT_EQ(0, relay_ctrl_add(&persistent_driver, &config));
    factory_resets = 0;
    cover_calls = 0;
    g_multi_press_reset_count = 10;
    action_mapper_init();
    ASSERT_TRUE(registered_gesture_sink != NULL);
    action_mapper_set_sinks(&(action_mapper_sinks_t){ .cover = cover_sink });
}

TEST(relay_requests_and_cover_commands_reach_their_targets) {
    reset_mapper();
    ASSERT_EQ(RELAY_RESULT_APPLIED,
              action_mapper_relay_request(0, RELAY_REQUEST_ON,
                                          RELAY_SOURCE_GESTURE));
    ASSERT_TRUE(relay_ctrl_is_on(0));
    action_mapper_cover_action(2, 7);
    ASSERT_EQ(1, cover_calls); ASSERT_EQ(2, cover_target); ASSERT_EQ(7, cover_command);
    action_mapper_set_sinks(NULL);
    action_mapper_cover_action(2, 7);
    ASSERT_EQ(1, cover_calls);
}

TEST(onboard_button_resets_on_hold_only) {
    reset_mapper();
    ASSERT_TRUE(action_mapper_add_reset_button(1, true));
    gesture(1, GESTURE_N_CLICK, 12);
    ASSERT_EQ(0, factory_resets);
    gesture(1, GESTURE_HOLD_START, 0);
    ASSERT_EQ(1, factory_resets);
}

TEST(other_buttons_reset_on_multi_press_threshold) {
    reset_mapper();
    ASSERT_TRUE(action_mapper_add_reset_button(2, false));
    gesture(2, GESTURE_HOLD_START, 0);
    gesture(2, GESTURE_N_CLICK, 9);
    gesture(3, GESTURE_N_CLICK, 10);
    ASSERT_EQ(0, factory_resets);
    gesture(2, GESTURE_N_CLICK, 10);
    ASSERT_EQ(1, factory_resets);
    g_multi_press_reset_count = 0;
    gesture(2, GESTURE_N_CLICK, 10);
    ASSERT_EQ(1, factory_resets);
}

TEST(reset_buttons_are_bounded_and_cleared_by_init) {
    reset_mapper();
    ASSERT_TRUE(!action_mapper_add_reset_button(BUTTON_INPUT_MAX, true));
    ASSERT_TRUE(action_mapper_add_reset_button(BUTTON_INPUT_MAX - 1, true));
    action_mapper_init();
    gesture(BUTTON_INPUT_MAX - 1, GESTURE_HOLD_START, 0);
    gesture(BUTTON_INPUT_MAX, GESTURE_HOLD_START, 0);
    ASSERT_EQ(0, factory_resets);
}

int main(void) {
    RUN_TEST(relay_requests_and_cover_commands_reach_their_targets);
    RUN_TEST(onboard_button_resets_on_hold_only);
    RUN_TEST(other_buttons_reset_on_multi_press_threshold);
    RUN_TEST(reset_buttons_are_bounded_and_cleared_by_init);
    return test_runner_failures == 0 ? 0 : 1;
}
