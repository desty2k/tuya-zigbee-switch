#include "base_components/interlock.h"
#include "base_components/relay_controller.h"
#include "support/fake_clock.h"
#include "support/fake_relay_hw.h"
#include "support/fake_tasks.h"
#include "support/runner.h"

#define TEST_RELAY_COUNT    4

uint8_t allow_simultaneous_latching_pulses;

static relay_driver_t drivers[TEST_RELAY_COUNT];
static relay_config_t configs[TEST_RELAY_COUNT];

static void reset_relays(void) {
    fake_clock_reset();
    fake_tasks_reset();
    fake_relay_hw_reset();
    allow_simultaneous_latching_pulses = 0;
    relay_ctrl_init();
    for (uint8_t relay_id = 0; relay_id < TEST_RELAY_COUNT; relay_id++) {
        drivers[relay_id].on_pin      = 10 + relay_id;
        drivers[relay_id].off_pin     = HAL_INVALID_PIN;
        drivers[relay_id].on_high     = 1;
        drivers[relay_id].is_latching = 0;
        configs[relay_id].inching_ms  = 250;
        ASSERT_EQ(relay_id,
                  relay_ctrl_add(&drivers[relay_id], &configs[relay_id]));
    }
    fake_relay_hw_clear_log();
}

static void add_group(uint16_t dead_time_ms) {
    ASSERT_TRUE(interlock_add_group(1, dead_time_ms));
    interlock_set_relay_group(0, 1);
    interlock_set_relay_group(1, 1);
    interlock_set_relay_group(2, 1);
}

static void submit(uint8_t relay_id, relay_request_type_t type,
                   relay_request_source_t source, uint32_t duration_ms) {
    ASSERT_TRUE(relay_ctrl_submit(&(relay_request_t){
        .relay_id    = relay_id,
        .type        = type,
        .duration_ms = duration_ms,
        .source      = source,
    }) != RELAY_RESULT_INVALID);
}

TEST(group_keeps_only_latest_relay_on) {
    reset_relays();
    add_group(0);

    submit(0, RELAY_REQUEST_ON, RELAY_SOURCE_ZIGBEE, 0);
    ASSERT_TRUE(relay_ctrl_is_on(0));
    submit(1, RELAY_REQUEST_ON, RELAY_SOURCE_GESTURE, 0);
    ASSERT_TRUE(!relay_ctrl_is_on(0));
    ASSERT_TRUE(relay_ctrl_is_on(1));
}

TEST(pulse_turns_off_peer_and_expires) {
    reset_relays();
    add_group(0);
    submit(1, RELAY_REQUEST_ON, RELAY_SOURCE_BUTTON, 0);
    submit(2, RELAY_REQUEST_PULSE, RELAY_SOURCE_ZIGBEE, 500);

    ASSERT_TRUE(!relay_ctrl_is_on(1));
    ASSERT_TRUE(relay_ctrl_is_on(2));
    fake_clock_advance_ms(500);
    fake_tasks_poll();
    ASSERT_TRUE(!relay_ctrl_is_on(2));
}

TEST(dead_time_defers_after_peer_turns_off) {
    reset_relays();
    add_group(50);
    submit(0, RELAY_REQUEST_ON, RELAY_SOURCE_STARTUP, 0);
    ASSERT_TRUE(relay_ctrl_is_on(0));

    submit(1, RELAY_REQUEST_ON, RELAY_SOURCE_ZIGBEE, 0);
    ASSERT_TRUE(!relay_ctrl_is_on(0));
    ASSERT_TRUE(!relay_ctrl_is_on(1));
    fake_clock_advance_ms(49);
    fake_tasks_poll();
    ASSERT_TRUE(!relay_ctrl_is_on(1));
    fake_clock_advance_ms(1);
    fake_tasks_poll();
    ASSERT_TRUE(relay_ctrl_is_on(1));
}

TEST(deferred_pulse_starts_timer_when_relay_turns_on) {
    reset_relays();
    add_group(50);
    submit(0, RELAY_REQUEST_ON, RELAY_SOURCE_BUTTON, 0);
    submit(1, RELAY_REQUEST_PULSE, RELAY_SOURCE_ZIGBEE, 500);

    fake_clock_advance_ms(50);
    fake_tasks_poll();
    ASSERT_TRUE(relay_ctrl_is_on(1));
    ASSERT_EQ(AUTO_OFF_PULSE, relay_ctrl_auto_off_reason(1));
    fake_clock_advance_ms(499);
    fake_tasks_poll();
    ASSERT_TRUE(relay_ctrl_is_on(1));
    fake_clock_advance_ms(1);
    fake_tasks_poll();
    ASSERT_TRUE(!relay_ctrl_is_on(1));
}

TEST(conflicting_request_replaces_pending_target) {
    reset_relays();
    add_group(50);
    submit(0, RELAY_REQUEST_ON, RELAY_SOURCE_BUTTON, 0);
    submit(1, RELAY_REQUEST_ON, RELAY_SOURCE_ZIGBEE, 0);
    fake_clock_advance_ms(20);
    fake_tasks_poll();
    submit(2, RELAY_REQUEST_ON, RELAY_SOURCE_GESTURE, 0);
    fake_clock_advance_ms(49);
    fake_tasks_poll();
    ASSERT_TRUE(!relay_ctrl_is_on(1));
    ASSERT_TRUE(!relay_ctrl_is_on(2));
    fake_clock_advance_ms(1);
    fake_tasks_poll();
    ASSERT_TRUE(!relay_ctrl_is_on(1));
    ASSERT_TRUE(relay_ctrl_is_on(2));
}

TEST(off_clears_pending_target) {
    reset_relays();
    add_group(50);
    submit(0, RELAY_REQUEST_ON, RELAY_SOURCE_BUTTON, 0);
    submit(1, RELAY_REQUEST_ON, RELAY_SOURCE_ZIGBEE, 0);
    submit(1, RELAY_REQUEST_OFF, RELAY_SOURCE_ZIGBEE, 0);
    fake_clock_advance_ms(50);
    fake_tasks_poll();
    ASSERT_TRUE(!relay_ctrl_is_on(1));
}

TEST(relay_outside_group_is_unaffected) {
    reset_relays();
    add_group(0);
    submit(0, RELAY_REQUEST_ON, RELAY_SOURCE_BUTTON, 0);
    submit(3, RELAY_REQUEST_ON_TIMED, RELAY_SOURCE_ZIGBEE, 100);

    ASSERT_TRUE(relay_ctrl_is_on(0));
    ASSERT_TRUE(relay_ctrl_is_on(3));
    fake_clock_advance_ms(100);
    fake_tasks_poll();
    ASSERT_TRUE(relay_ctrl_is_on(0));
    ASSERT_TRUE(!relay_ctrl_is_on(3));
}

TEST(second_toggle_during_dead_time_cancels_pending_on) {
    reset_relays();
    add_group(50);
    submit(0, RELAY_REQUEST_ON, RELAY_SOURCE_BUTTON, 0);
    ASSERT_EQ(RELAY_RESULT_DEFERRED, relay_ctrl_submit(&(relay_request_t){
        .relay_id = 1, .type = RELAY_REQUEST_TOGGLE, .source = RELAY_SOURCE_GESTURE,
    }));
    fake_clock_advance_ms(20);
    fake_tasks_poll();
    submit(1, RELAY_REQUEST_TOGGLE, RELAY_SOURCE_GESTURE, 0);
    fake_clock_advance_ms(50);
    fake_tasks_poll();
    ASSERT_TRUE(!relay_ctrl_is_on(0));
    ASSERT_TRUE(!relay_ctrl_is_on(1));
}

TEST(blocked_resume_does_not_skip_peer_shutdown) {
    reset_relays();
    add_group(50);
    submit(0, RELAY_REQUEST_ON, RELAY_SOURCE_BUTTON, 0);
    submit(1, RELAY_REQUEST_ON, RELAY_SOURCE_ZIGBEE, 0);
    ASSERT_TRUE(relay_ctrl_inhibit(0x02, 0));
    fake_clock_advance_ms(50);
    fake_tasks_poll();
    ASSERT_TRUE(!relay_ctrl_is_on(1));
    ASSERT_TRUE(relay_ctrl_release(0x02, 0));

    submit(0, RELAY_REQUEST_ON, RELAY_SOURCE_BUTTON, 0);
    ASSERT_TRUE(relay_ctrl_is_on(0));
    submit(1, RELAY_REQUEST_ON, RELAY_SOURCE_ZIGBEE, 0);
    ASSERT_TRUE(!(relay_ctrl_is_on(0) && relay_ctrl_is_on(1)));
    fake_clock_advance_ms(50);
    fake_tasks_poll();
    ASSERT_TRUE(!relay_ctrl_is_on(0));
    ASSERT_TRUE(relay_ctrl_is_on(1));
}

TEST(unknown_group_id_does_not_create_a_group) {
    reset_relays();
    add_group(50);
    ASSERT_TRUE(!interlock_add_group(1, 0));
    ASSERT_TRUE(!interlock_add_group(INTERLOCK_NO_GROUP, 0));
    ASSERT_TRUE(!interlock_set_relay_group(3, 5));
    ASSERT_EQ(INTERLOCK_NO_GROUP, interlock_relay_group(3));
    ASSERT_TRUE(interlock_set_relay_group(3, 1));
    ASSERT_EQ(1, interlock_relay_group(3));
    submit(0, RELAY_REQUEST_ON, RELAY_SOURCE_BUTTON, 0);
    submit(3, RELAY_REQUEST_ON, RELAY_SOURCE_BUTTON, 0);
    ASSERT_TRUE(!relay_ctrl_is_on(0));
}

int main(void) {
    RUN_TEST(group_keeps_only_latest_relay_on);
    RUN_TEST(pulse_turns_off_peer_and_expires);
    RUN_TEST(dead_time_defers_after_peer_turns_off);
    RUN_TEST(deferred_pulse_starts_timer_when_relay_turns_on);
    RUN_TEST(conflicting_request_replaces_pending_target);
    RUN_TEST(off_clears_pending_target);
    RUN_TEST(relay_outside_group_is_unaffected);
    RUN_TEST(second_toggle_during_dead_time_cancels_pending_on);
    RUN_TEST(blocked_resume_does_not_skip_peer_shutdown);
    RUN_TEST(unknown_group_id_does_not_create_a_group);

    return test_runner_failures == 0 ? 0 : 1;
}
