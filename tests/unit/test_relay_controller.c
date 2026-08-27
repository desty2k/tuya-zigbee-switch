#include "base_components/relay_controller.h"
#include "support/fake_clock.h"
#include "support/fake_relay_hw.h"
#include "support/fake_tasks.h"
#include "support/runner.h"
#include <stdbool.h>
#include <stdint.h>

uint8_t allow_simultaneous_latching_pulses;

typedef struct {
    uint8_t count;
    uint8_t relay_id;
    bool    is_on;
} state_log_t;

static relay_driver_t driver;
static relay_config_t config;
static state_log_t    state_log;

static void record_state(void *param, uint8_t relay_id, bool is_on) {
    state_log_t *log = (state_log_t *)param;

    log->count++;
    log->relay_id = relay_id;
    log->is_on    = is_on;
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
    state_log.count         = 0;
    state_log.relay_id      = 0;
    state_log.is_on         = false;
    allow_simultaneous_latching_pulses = 0;

    relay_ctrl_init();
    ASSERT_EQ(0, relay_ctrl_add(&driver, &config));
    relay_ctrl_set_state_callback(0, record_state, &state_log);
    fake_relay_hw_clear_log();
}

static hal_zigbee_cmd_result_t submit(relay_request_type_t type,
                                      relay_request_source_t source) {
    relay_request_t request = {
        .relay_id = 0,
        .type     = type,
        .source   = source,
    };

    return relay_ctrl_submit(&request);
}

static hal_zigbee_cmd_result_t submit_timed(relay_request_type_t type,
                                            uint32_t duration_ms) {
    relay_request_t request = {
        .relay_id    = 0,
        .type        = type,
        .duration_ms = duration_ms,
        .source      = RELAY_SOURCE_ZIGBEE,
    };

    return relay_ctrl_submit(&request);
}

TEST(on_off_toggle_update_state_and_hardware) {
    reset_controller(false);

    ASSERT_EQ(HAL_ZIGBEE_CMD_PROCESSED,
              submit(RELAY_REQUEST_ON, RELAY_SOURCE_ZIGBEE));
    ASSERT_TRUE(relay_ctrl_is_on(0));
    ASSERT_EQ(1, fake_relay_hw_level(10));
    ASSERT_EQ(1, fake_relay_hw_write_count());
    ASSERT_EQ(1, state_log.count);

    ASSERT_EQ(HAL_ZIGBEE_CMD_PROCESSED,
              submit(RELAY_REQUEST_OFF, RELAY_SOURCE_BUTTON));
    ASSERT_TRUE(!relay_ctrl_is_on(0));
    ASSERT_EQ(0, fake_relay_hw_level(10));
    ASSERT_EQ(2, fake_relay_hw_write_count());
    ASSERT_EQ(2, state_log.count);

    ASSERT_EQ(HAL_ZIGBEE_CMD_PROCESSED,
              submit(RELAY_REQUEST_TOGGLE, RELAY_SOURCE_GESTURE));
    ASSERT_TRUE(relay_ctrl_is_on(0));
    ASSERT_EQ(3, fake_relay_hw_write_count());
    ASSERT_EQ(3, state_log.count);
}

TEST(repeated_state_does_not_write_or_notify) {
    reset_controller(false);
    submit(RELAY_REQUEST_ON, RELAY_SOURCE_ZIGBEE);
    fake_relay_hw_clear_log();
    state_log.count = 0;

    ASSERT_EQ(HAL_ZIGBEE_CMD_PROCESSED,
              submit(RELAY_REQUEST_ON, RELAY_SOURCE_BUTTON));
    ASSERT_EQ(0, fake_relay_hw_write_count());
    ASSERT_EQ(0, state_log.count);
}

TEST(first_off_request_reaches_latching_driver) {
    reset_controller(true);

    ASSERT_EQ(HAL_ZIGBEE_CMD_PROCESSED,
              submit(RELAY_REQUEST_OFF, RELAY_SOURCE_ZIGBEE));
    ASSERT_EQ(1, fake_relay_hw_level(11));
    ASSERT_EQ(0, state_log.count);
    fake_clock_advance_ms(RELAY_LATCH_PULSE_MS);
    fake_tasks_poll();
    ASSERT_EQ(0, fake_relay_hw_level(10));
    ASSERT_EQ(0, fake_relay_hw_level(11));
}

TEST(latching_relay_pulses_requested_coil) {
    reset_controller(true);
    submit(RELAY_REQUEST_ON, RELAY_SOURCE_ZIGBEE);

    ASSERT_EQ(1, fake_relay_hw_level(10));
    ASSERT_EQ(0, fake_relay_hw_level(11));
    ASSERT_TRUE(timer_is_active(&driver.latch_timer));
    fake_clock_advance_ms(RELAY_LATCH_PULSE_MS - 1);
    fake_tasks_poll();
    ASSERT_EQ(1, fake_relay_hw_level(10));
    fake_clock_advance_ms(1);
    fake_tasks_poll();
    ASSERT_EQ(0, fake_relay_hw_level(10));
    ASSERT_TRUE(!timer_is_active(&driver.latch_timer));
}

TEST(invalid_requests_are_rejected) {
    relay_request_t request = {
        .relay_id = 1,
        .type     = RELAY_REQUEST_ON,
        .source   = RELAY_SOURCE_ZIGBEE,
    };

    reset_controller(false);
    ASSERT_EQ(HAL_ZIGBEE_INVALID_VALUE, relay_ctrl_submit(NULL));
    ASSERT_EQ(HAL_ZIGBEE_INVALID_VALUE, relay_ctrl_submit(&request));
    ASSERT_EQ(HAL_ZIGBEE_INVALID_VALUE,
              submit(RELAY_REQUEST_PULSE, RELAY_SOURCE_BUTTON));
}

TEST(pulse_turns_off_at_deadline) {
    reset_controller(false);

    ASSERT_EQ(HAL_ZIGBEE_CMD_PROCESSED,
              submit_timed(RELAY_REQUEST_PULSE, 500));
    ASSERT_TRUE(relay_ctrl_is_on(0));
    ASSERT_EQ(AUTO_OFF_PULSE, relay_ctrl_auto_off_reason(0));
    fake_clock_advance_ms(499);
    fake_tasks_poll();
    ASSERT_TRUE(relay_ctrl_is_on(0));
    fake_clock_advance_ms(1);
    fake_tasks_poll();
    ASSERT_TRUE(!relay_ctrl_is_on(0));
    ASSERT_EQ(AUTO_OFF_NONE, relay_ctrl_auto_off_reason(0));
}

TEST(pulse_restart_replaces_deadline) {
    reset_controller(false);
    submit_timed(RELAY_REQUEST_PULSE, 500);
    fake_clock_advance_ms(300);
    fake_tasks_poll();
    submit_timed(RELAY_REQUEST_PULSE, 500);
    fake_clock_advance_ms(499);
    fake_tasks_poll();
    ASSERT_TRUE(relay_ctrl_is_on(0));
    fake_clock_advance_ms(1);
    fake_tasks_poll();
    ASSERT_TRUE(!relay_ctrl_is_on(0));
}

TEST(pulse_uses_configured_default) {
    reset_controller(false);
    relay_ctrl_set_inching_ms(0, 250);

    ASSERT_EQ(250, relay_ctrl_get_inching_ms(0));
    submit_timed(RELAY_REQUEST_PULSE, 0);
    fake_clock_advance_ms(250);
    fake_tasks_poll();
    ASSERT_TRUE(!relay_ctrl_is_on(0));
}

TEST(explicit_request_cancels_pulse) {
    reset_controller(false);
    submit_timed(RELAY_REQUEST_PULSE, 500);
    submit(RELAY_REQUEST_ON, RELAY_SOURCE_BUTTON);
    ASSERT_EQ(AUTO_OFF_NONE, relay_ctrl_auto_off_reason(0));
    fake_clock_advance_ms(500);
    fake_tasks_poll();
    ASSERT_TRUE(relay_ctrl_is_on(0));

    submit_timed(RELAY_REQUEST_PULSE, 500);
    submit(RELAY_REQUEST_OFF, RELAY_SOURCE_BUTTON);
    ASSERT_EQ(AUTO_OFF_NONE, relay_ctrl_auto_off_reason(0));
    ASSERT_TRUE(!relay_ctrl_is_on(0));
}

TEST(timed_on_restarts_and_toggle_cancels) {
    reset_controller(false);
    submit_timed(RELAY_REQUEST_ON_TIMED, 30000);
    ASSERT_EQ(AUTO_OFF_TIMED, relay_ctrl_auto_off_reason(0));
    fake_clock_advance_ms(10000);
    fake_tasks_poll();
    submit_timed(RELAY_REQUEST_ON_TIMED, 30000);
    fake_clock_advance_ms(29999);
    fake_tasks_poll();
    ASSERT_TRUE(relay_ctrl_is_on(0));
    submit(RELAY_REQUEST_TOGGLE, RELAY_SOURCE_ZIGBEE);
    ASSERT_TRUE(!relay_ctrl_is_on(0));
    ASSERT_EQ(AUTO_OFF_NONE, relay_ctrl_auto_off_reason(0));
}

TEST(controller_capacity_is_bounded) {
    relay_driver_t drivers[RELAY_CONTROLLER_MAX_RELAYS + 1];
    relay_config_t configs[RELAY_CONTROLLER_MAX_RELAYS + 1] = { 0 };

    fake_clock_reset();
    fake_tasks_reset();
    fake_relay_hw_reset();
    relay_ctrl_init();
    for (uint8_t i = 0; i < RELAY_CONTROLLER_MAX_RELAYS; i++) {
        drivers[i].on_pin          = i;
        drivers[i].off_pin         = HAL_INVALID_PIN;
        drivers[i].on_high         = 1;
        drivers[i].is_latching     = 0;
        ASSERT_EQ(i, relay_ctrl_add(&drivers[i], &configs[i]));
    }
    ASSERT_EQ(RELAY_CONTROLLER_MAX_RELAYS, relay_ctrl_count());
    ASSERT_EQ(UINT8_MAX,
              relay_ctrl_add(&drivers[RELAY_CONTROLLER_MAX_RELAYS],
                             &configs[RELAY_CONTROLLER_MAX_RELAYS]));
}

int main(void) {
    RUN_TEST(on_off_toggle_update_state_and_hardware);
    RUN_TEST(repeated_state_does_not_write_or_notify);
    RUN_TEST(first_off_request_reaches_latching_driver);
    RUN_TEST(latching_relay_pulses_requested_coil);
    RUN_TEST(invalid_requests_are_rejected);
    RUN_TEST(pulse_turns_off_at_deadline);
    RUN_TEST(pulse_restart_replaces_deadline);
    RUN_TEST(pulse_uses_configured_default);
    RUN_TEST(explicit_request_cancels_pulse);
    RUN_TEST(timed_on_restarts_and_toggle_cancels);
    RUN_TEST(controller_capacity_is_bounded);

    return test_runner_failures == 0 ? 0 : 1;
}
