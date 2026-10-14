#include "base_components/cover_controller.h"
#include "base_components/interlock.h"
#include "base_components/relay_controller.h"
#include "device_config/nvm_items.h"
#include "hal/nvm.h"
#include "support/fake_clock.h"
#include "support/fake_relay_hw.h"
#include "support/fake_tasks.h"
#include "support/runner.h"
#include <stdbool.h>
#include <string.h>

#define NVM_ITEM_SIZE    32

uint8_t allow_simultaneous_latching_pulses;

static relay_driver_t drivers[2];
static relay_config_t relay_configs[2];
static uint8_t        nvm_items[256][NVM_ITEM_SIZE];
static uint8_t        nvm_sizes[256];
static bool           nvm_write_fails;

hal_nvm_status_t hal_nvm_write(uint8_t item_id, uint16_t size, uint8_t *data) {
    if (nvm_write_fails || data == NULL || size > NVM_ITEM_SIZE) return HAL_NVM_ERROR;
    memcpy(nvm_items[item_id], data, size);
    nvm_sizes[item_id] = size;
    return HAL_NVM_SUCCESS;
}

hal_nvm_status_t hal_nvm_read(uint8_t item_id, uint16_t size, uint8_t *data) {
    if (data == NULL || nvm_sizes[item_id] != size) return HAL_NVM_NOT_FOUND;
    memcpy(data, nvm_items[item_id], size);
    return HAL_NVM_SUCCESS;
}

hal_nvm_status_t hal_nvm_delete(uint8_t item_id) {
    nvm_sizes[item_id] = 0;
    return HAL_NVM_SUCCESS;
}

hal_nvm_status_t hal_nvm_clear_all(void) {
    memset(nvm_sizes, 0, sizeof(nvm_sizes));
    nvm_write_fails = false;
    return HAL_NVM_SUCCESS;
}

static void reset_controller(uint32_t safety_ms) {
    memset(nvm_items, 0, sizeof(nvm_items));
    memset(nvm_sizes, 0, sizeof(nvm_sizes));
    fake_clock_reset();
    fake_tasks_reset();
    fake_relay_hw_reset();
    allow_simultaneous_latching_pulses = 0;
    relay_ctrl_init();
    for (uint8_t i = 0; i < 2; i++) {
        drivers[i] = (relay_driver_t){
            .on_pin = 10 + i, .off_pin = HAL_INVALID_PIN, .on_high = 1,
            .is_latching = 0,
        };
        relay_configs[i] = (relay_config_t){ 0 };
        ASSERT_EQ(i, relay_ctrl_add(&drivers[i], &relay_configs[i]));
    }
    ASSERT_TRUE(interlock_add_group(1, 0x03, 50));
    relay_ctrl_set_interlock_group(0, 1);
    relay_ctrl_set_interlock_group(1, 1);
    cover_controller_init();
    ASSERT_EQ(0, cover_controller_add(&(cover_controller_config_t){
        .open_relay_id = 0, .close_relay_id = 1, .manual_max_run_ms = safety_ms,
    }));
}

TEST(open_close_and_stop_follow_applied_relays) {
    reset_controller(500);

    ASSERT_EQ(COVER_RESULT_APPLIED, cover_open(0));
    ASSERT_EQ(COVER_OPENING, cover_controller_motion(0));
    ASSERT_TRUE(relay_ctrl_is_on(0));
    ASSERT_TRUE(!relay_ctrl_is_on(1));
    ASSERT_EQ(COVER_RESULT_UNCHANGED, cover_open(0));
    ASSERT_EQ(COVER_RESULT_DEFERRED, cover_close(0));
    ASSERT_EQ(COVER_STOPPED, cover_controller_motion(0));
    ASSERT_TRUE(!relay_ctrl_is_on(0));
    ASSERT_TRUE(!relay_ctrl_is_on(1));
    fake_clock_advance_ms(50);
    fake_tasks_poll();
    ASSERT_EQ(COVER_CLOSING, cover_controller_motion(0));
    ASSERT_TRUE(!relay_ctrl_is_on(0));
    ASSERT_TRUE(relay_ctrl_is_on(1));
    ASSERT_EQ(COVER_RESULT_APPLIED, cover_stop(0));
    ASSERT_EQ(COVER_STOPPED, cover_controller_motion(0));
    ASSERT_TRUE(!relay_ctrl_is_on(0));
    ASSERT_TRUE(!relay_ctrl_is_on(1));
}

TEST(stop_and_replacement_cancel_a_deferred_reversal) {
    reset_controller(500);

    ASSERT_EQ(COVER_RESULT_APPLIED, cover_open(0));
    ASSERT_EQ(COVER_RESULT_DEFERRED, cover_close(0));
    ASSERT_EQ(COVER_RESULT_DEFERRED, cover_open(0));
    fake_clock_advance_ms(50);
    fake_tasks_poll();
    ASSERT_EQ(COVER_OPENING, cover_controller_motion(0));
    ASSERT_TRUE(relay_ctrl_is_on(0));
    ASSERT_TRUE(!relay_ctrl_is_on(1));
    ASSERT_EQ(COVER_RESULT_DEFERRED, cover_close(0));
    ASSERT_EQ(COVER_RESULT_APPLIED, cover_stop(0));
    fake_clock_advance_ms(50);
    fake_tasks_poll();
    ASSERT_EQ(COVER_STOPPED, cover_controller_motion(0));
    ASSERT_TRUE(!relay_ctrl_is_on(0));
    ASSERT_TRUE(!relay_ctrl_is_on(1));
}

TEST(protection_inhibit_and_manual_timeout_leave_both_relays_off) {
    reset_controller(100);

    ASSERT_TRUE(relay_ctrl_inhibit(0x01, 1));
    ASSERT_EQ(COVER_RESULT_BLOCKED, cover_open(0));
    ASSERT_EQ(COVER_STOPPED, cover_controller_motion(0));
    ASSERT_TRUE(!relay_ctrl_is_on(0));
    ASSERT_TRUE(!relay_ctrl_is_on(1));
    ASSERT_TRUE(relay_ctrl_release(0x01, 1));
    ASSERT_EQ(COVER_RESULT_APPLIED, cover_open(0));
    fake_clock_advance_ms(100);
    fake_tasks_poll();
    ASSERT_EQ(COVER_STOPPED, cover_controller_motion(0));
    ASSERT_TRUE(!relay_ctrl_is_on(0));
    ASSERT_TRUE(!relay_ctrl_is_on(1));
}

TEST(calibrated_goto_deadzone_and_overrun_track_position) {
    cover_state_t state;

    reset_controller(500);
    ASSERT_EQ(COVER_RESULT_APPLIED, cover_controller_set_open_time(0, 1000));
    ASSERT_EQ(COVER_RESULT_APPLIED, cover_controller_set_close_time(0, 2000));
    ASSERT_EQ(COVER_RESULT_APPLIED, cover_controller_set_closed_deadzone(0, 1000));
    ASSERT_EQ(COVER_RESULT_APPLIED, cover_controller_set_open_deadzone(0, 2000));
    ASSERT_EQ(COVER_RESULT_APPLIED, cover_controller_set_overrun(0, 100));

    ASSERT_EQ(COVER_RESULT_APPLIED, cover_goto(0, 5000));
    fake_clock_advance_ms(200);
    fake_tasks_poll();
    ASSERT_TRUE(cover_controller_state(0, &state));
    ASSERT_EQ(COVER_OPENING, state.motion);
    ASSERT_EQ(2000, state.travel_position_bp);
    ASSERT_EQ(1428, state.open_position_bp);

    fake_clock_advance_ms(250);
    fake_tasks_poll();
    ASSERT_TRUE(cover_controller_state(0, &state));
    ASSERT_EQ(COVER_STOPPED, state.motion);
    ASSERT_EQ(COVER_POSITION_ESTIMATED, state.confidence);
    ASSERT_EQ(4500, state.travel_position_bp);

    fake_clock_advance_ms(50);
    fake_tasks_poll();
    ASSERT_EQ(COVER_RESULT_APPLIED, cover_open(0));
    fake_clock_advance_ms(550);
    fake_tasks_poll();
    ASSERT_TRUE(cover_controller_state(0, &state));
    ASSERT_EQ(COVER_OPENING, state.motion);
    ASSERT_EQ(10000, state.travel_position_bp);
    ASSERT_EQ(10000, state.open_position_bp);
    fake_clock_advance_ms(100);
    fake_tasks_poll();
    ASSERT_TRUE(cover_controller_state(0, &state));
    ASSERT_EQ(COVER_STOPPED, state.motion);
    ASSERT_EQ(COVER_POSITION_KNOWN, state.confidence);
    ASSERT_EQ(10000, state.travel_position_bp);
}

TEST(one_time_fallback_wraparound_and_marker_failure_keep_manual_control) {
    cover_state_t state;

    reset_controller(100);
    ASSERT_EQ(COVER_RESULT_APPLIED, cover_controller_set_open_time(0, 1000));
    ASSERT_EQ(COVER_RESULT_APPLIED, cover_open(0));
    fake_clock_set(UINT32_MAX - 50);
    ASSERT_EQ(COVER_RESULT_APPLIED, cover_stop(0));
    fake_clock_advance_ms(50);
    fake_tasks_poll();
    ASSERT_EQ(COVER_RESULT_APPLIED, cover_close(0));
    fake_clock_advance_ms(100);
    fake_tasks_poll();
    ASSERT_TRUE(cover_controller_state(0, &state));
    ASSERT_EQ(COVER_CLOSING, state.motion);

    reset_controller(100);
    nvm_write_fails = true;
    ASSERT_EQ(COVER_RESULT_APPLIED, cover_open(0));
    ASSERT_EQ(COVER_OPENING, cover_controller_motion(0));
    ASSERT_TRUE(!relay_ctrl_is_on(1));
    ASSERT_TRUE(relay_ctrl_is_on(0));
    ASSERT_TRUE(cover_controller_state(0, &state));
    ASSERT_EQ(COVER_POSITION_UNKNOWN, state.confidence);
}

int main(void) {
    RUN_TEST(open_close_and_stop_follow_applied_relays);
    RUN_TEST(stop_and_replacement_cancel_a_deferred_reversal);
    RUN_TEST(protection_inhibit_and_manual_timeout_leave_both_relays_off);
    RUN_TEST(calibrated_goto_deadzone_and_overrun_track_position);
    RUN_TEST(one_time_fallback_wraparound_and_marker_failure_keep_manual_control);
    return test_runner_failures == 0 ? 0 : 1;
}
