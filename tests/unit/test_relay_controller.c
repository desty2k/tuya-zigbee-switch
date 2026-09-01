#include "base_components/interlock.h"
#include "base_components/relay_controller.h"
#include "support/fake_clock.h"
#include "support/fake_relay_hw.h"
#include "support/fake_tasks.h"
#include "support/runner.h"
#include <stdbool.h>
#include <stdint.h>

uint8_t allow_simultaneous_latching_pulses;

typedef struct {
    uint8_t                count;
    uint8_t                relay_id;
    bool                   is_on;
    relay_request_source_t source;
} state_log_t;

static relay_driver_t driver;
static relay_config_t config;
static state_log_t    state_logs[RELAY_CONTROLLER_MAX_OBSERVERS + 1];
static uint8_t        observer_order[RELAY_CONTROLLER_MAX_OBSERVERS];
static uint8_t        observer_order_count;

static void record_state(void *context, uint8_t relay_id, bool is_on,
                         relay_request_source_t source) {
    state_log_t *log = (state_log_t *)context;

    log->count++;
    log->relay_id = relay_id;
    log->is_on    = is_on;
    log->source   = source;
}

static void record_order(void *context, uint8_t relay_id, bool is_on,
                         relay_request_source_t source) {
    uint8_t *slot = (uint8_t *)context;

    (void)relay_id;
    (void)is_on;
    (void)source;
    observer_order[observer_order_count++] = *slot;
}

static void reset_controller(bool is_latching) {
    fake_clock_reset();
    fake_tasks_reset();
    fake_relay_hw_reset();
    driver.on_pin           = 10;
    driver.off_pin          = 11;
    driver.on_high          = 1;
    driver.is_latching      = is_latching;
    config.inching_ms       = 0;
    config.interlock_group  = 0;
    observer_order_count    = 0;
    allow_simultaneous_latching_pulses = 0;
    for (uint8_t i = 0; i < RELAY_CONTROLLER_MAX_OBSERVERS + 1; i++) {
        state_logs[i] = (state_log_t){ 0 };
    }

    relay_ctrl_init();
    ASSERT_EQ(0, relay_ctrl_add(&driver, &config));
    ASSERT_TRUE(relay_ctrl_subscribe(0, record_state, &state_logs[0]));
    fake_relay_hw_clear_log();
}

static relay_result_t submit(relay_request_type_t type,
                             relay_request_source_t source) {
    return relay_ctrl_submit(&(relay_request_t){
        .relay_id = 0,
        .type     = type,
        .source   = source,
    });
}

static relay_result_t submit_timed(relay_request_type_t type,
                                   uint32_t duration_ms) {
    return relay_ctrl_submit(&(relay_request_t){
        .relay_id    = 0,
        .type        = type,
        .duration_ms = duration_ms,
        .source      = RELAY_SOURCE_ZIGBEE,
    });
}

TEST(domain_outcomes_and_unchanged_requests) {
    reset_controller(false);

    ASSERT_EQ(RELAY_RESULT_APPLIED, submit(RELAY_REQUEST_ON, RELAY_SOURCE_ZIGBEE));
    ASSERT_TRUE(relay_ctrl_is_on(0));
    ASSERT_EQ(1, fake_relay_hw_write_count());
    ASSERT_EQ(1, state_logs[0].count);
    ASSERT_EQ(RELAY_SOURCE_ZIGBEE, state_logs[0].source);
    ASSERT_EQ(RELAY_RESULT_UNCHANGED, submit(RELAY_REQUEST_ON, RELAY_SOURCE_BUTTON));
    ASSERT_EQ(1, fake_relay_hw_write_count());
    ASSERT_EQ(1, state_logs[0].count);
    ASSERT_EQ(RELAY_RESULT_APPLIED, submit(RELAY_REQUEST_OFF, RELAY_SOURCE_BUTTON));
    ASSERT_TRUE(!relay_ctrl_is_on(0));
    ASSERT_EQ(2, state_logs[0].count);
    ASSERT_EQ(RELAY_SOURCE_BUTTON, state_logs[0].source);
}

TEST(invalid_requests_are_rejected) {
    relay_request_t request = {
        .relay_id = 1,
        .type     = RELAY_REQUEST_ON,
        .source   = RELAY_SOURCE_ZIGBEE,
    };

    reset_controller(false);
    ASSERT_EQ(RELAY_RESULT_INVALID, relay_ctrl_submit(NULL));
    ASSERT_EQ(RELAY_RESULT_INVALID, relay_ctrl_submit(&request));
    ASSERT_EQ(RELAY_RESULT_INVALID, submit(RELAY_REQUEST_PULSE, RELAY_SOURCE_BUTTON));
}

TEST(latching_relay_applies_first_off_without_notification) {
    reset_controller(true);

    ASSERT_EQ(RELAY_RESULT_APPLIED, submit(RELAY_REQUEST_OFF, RELAY_SOURCE_ZIGBEE));
    ASSERT_EQ(1, fake_relay_hw_level(11));
    ASSERT_EQ(0, state_logs[0].count);
    fake_clock_advance_ms(RELAY_LATCH_PULSE_MS);
    fake_tasks_poll();
    ASSERT_EQ(0, fake_relay_hw_level(11));
}

TEST(timed_requests_expire_across_millis_wraparound) {
    reset_controller(false);
    fake_clock_set(UINT32_MAX - 20);

    ASSERT_EQ(RELAY_RESULT_APPLIED, submit_timed(RELAY_REQUEST_PULSE, 50));
    fake_clock_advance_ms(49);
    fake_tasks_poll();
    ASSERT_TRUE(relay_ctrl_is_on(0));
    fake_clock_advance_ms(1);
    fake_tasks_poll();
    ASSERT_TRUE(!relay_ctrl_is_on(0));
    ASSERT_EQ(AUTO_OFF_NONE, relay_ctrl_auto_off_reason(0));
}

TEST(observer_capacity_order_and_actual_transitions) {
    uint8_t slots[RELAY_CONTROLLER_MAX_OBSERVERS] = { 0, 1, 2, 3 };

    reset_controller(false);
    for (uint8_t i = 0; i < RELAY_CONTROLLER_MAX_OBSERVERS - 1; i++) {
        ASSERT_TRUE(relay_ctrl_subscribe(0, record_order, &slots[i]));
    }
    ASSERT_TRUE(!relay_ctrl_subscribe(0, record_order,
                                      &slots[RELAY_CONTROLLER_MAX_OBSERVERS - 1]));
    ASSERT_EQ(RELAY_RESULT_APPLIED, submit(RELAY_REQUEST_ON, RELAY_SOURCE_BUTTON));
    ASSERT_EQ(RELAY_CONTROLLER_MAX_OBSERVERS - 1, observer_order_count);
    for (uint8_t i = 0; i < observer_order_count; i++) {
        ASSERT_EQ(i, observer_order[i]);
    }
    ASSERT_EQ(RELAY_RESULT_UNCHANGED, submit(RELAY_REQUEST_ON, RELAY_SOURCE_BUTTON));
    ASSERT_EQ(RELAY_CONTROLLER_MAX_OBSERVERS - 1, observer_order_count);
}

TEST(interlock_deferral_notifies_only_on_applied_activation) {
    relay_driver_t peer_driver = {
        .on_pin = 12, .off_pin = HAL_INVALID_PIN, .on_high = 1, .is_latching = 0,
    };
    relay_config_t peer_config = { 0 };

    reset_controller(false);
    ASSERT_EQ(1, relay_ctrl_add(&peer_driver, &peer_config));
    ASSERT_TRUE(relay_ctrl_subscribe(1, record_state, &state_logs[1]));
    ASSERT_TRUE(interlock_add_group(1, 0x03, 50));
    relay_ctrl_set_interlock_group(0, 1);
    relay_ctrl_set_interlock_group(1, 1);
    ASSERT_EQ(RELAY_RESULT_APPLIED, submit(RELAY_REQUEST_ON, RELAY_SOURCE_BUTTON));
    state_logs[0].count = 0;
    state_logs[1].count = 0;
    ASSERT_EQ(RELAY_RESULT_DEFERRED, relay_ctrl_submit(&(relay_request_t){
        .relay_id = 1, .type = RELAY_REQUEST_ON, .source = RELAY_SOURCE_ZIGBEE,
    }));
    ASSERT_EQ(0, state_logs[1].count);
    fake_clock_advance_ms(50);
    fake_tasks_poll();
    ASSERT_TRUE(relay_ctrl_is_on(1));
    ASSERT_EQ(1, state_logs[1].count);
    ASSERT_EQ(RELAY_SOURCE_INTERLOCK, state_logs[1].source);
}

TEST(source_owned_inhibitors_block_on_like_requests_and_preserve_off) {
    reset_controller(false);
    ASSERT_TRUE(relay_ctrl_inhibit(0x01, 1));
    ASSERT_TRUE(relay_ctrl_inhibit(0x01, 2));
    ASSERT_EQ(RELAY_RESULT_BLOCKED, submit(RELAY_REQUEST_ON, RELAY_SOURCE_PROTECTION));
    ASSERT_EQ(RELAY_RESULT_BLOCKED, submit_timed(RELAY_REQUEST_PULSE, 10));
    ASSERT_EQ(RELAY_RESULT_APPLIED, submit(RELAY_REQUEST_OFF, RELAY_SOURCE_PROTECTION));
    ASSERT_TRUE(relay_ctrl_release(0x01, 1));
    ASSERT_EQ(RELAY_RESULT_BLOCKED, submit(RELAY_REQUEST_ON, RELAY_SOURCE_BUTTON));
    ASSERT_TRUE(relay_ctrl_release(0x01, 2));
    ASSERT_TRUE(!relay_ctrl_is_on(0));
    ASSERT_EQ(RELAY_RESULT_APPLIED, submit(RELAY_REQUEST_ON, RELAY_SOURCE_BUTTON));
}

TEST(controller_capacity_is_bounded) {
    relay_driver_t drivers[RELAY_CONTROLLER_MAX_RELAYS + 1];
    relay_config_t configs[RELAY_CONTROLLER_MAX_RELAYS + 1] = { 0 };

    fake_clock_reset();
    fake_tasks_reset();
    fake_relay_hw_reset();
    relay_ctrl_init();
    for (uint8_t i = 0; i < RELAY_CONTROLLER_MAX_RELAYS; i++) {
        drivers[i] = (relay_driver_t){ .on_pin = i, .off_pin = HAL_INVALID_PIN,
                                       .on_high = 1, .is_latching = 0 };
        ASSERT_EQ(i, relay_ctrl_add(&drivers[i], &configs[i]));
    }
    ASSERT_EQ(UINT8_MAX, relay_ctrl_add(&drivers[RELAY_CONTROLLER_MAX_RELAYS],
                                        &configs[RELAY_CONTROLLER_MAX_RELAYS]));
}

int main(void) {
    RUN_TEST(domain_outcomes_and_unchanged_requests);
    RUN_TEST(invalid_requests_are_rejected);
    RUN_TEST(latching_relay_applies_first_off_without_notification);
    RUN_TEST(timed_requests_expire_across_millis_wraparound);
    RUN_TEST(observer_capacity_order_and_actual_transitions);
    RUN_TEST(interlock_deferral_notifies_only_on_applied_activation);
    RUN_TEST(source_owned_inhibitors_block_on_like_requests_and_preserve_off);
    RUN_TEST(controller_capacity_is_bounded);
    return test_runner_failures == 0 ? 0 : 1;
}
