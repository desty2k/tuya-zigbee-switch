#include "device_config/capability_state.h"
#include "device_config/nvm_items.h"
#include "hal/nvm.h"
#include "support/runner.h"
#include <stdbool.h>
#include <string.h>

#define NVM_TEST_ITEM_SIZE    32

static uint8_t nvm_items[256][NVM_TEST_ITEM_SIZE];
static uint8_t nvm_sizes[256];
static bool    fail_writes;

hal_nvm_status_t hal_nvm_write(uint8_t item_id, uint16_t size, uint8_t *data) {
    if (fail_writes || size > NVM_TEST_ITEM_SIZE || data == NULL) return HAL_NVM_ERROR;
    memcpy(nvm_items[item_id], data, size);
    nvm_sizes[item_id] = (uint8_t)size;
    return HAL_NVM_SUCCESS;
}
hal_nvm_status_t hal_nvm_read(uint8_t item_id, uint16_t size, uint8_t *data) {
    if (data == NULL || nvm_sizes[item_id] != size) return HAL_NVM_NOT_FOUND;
    memcpy(data, nvm_items[item_id], size);
    return HAL_NVM_SUCCESS;
}
hal_nvm_status_t hal_nvm_delete(uint8_t item_id) { nvm_sizes[item_id] = 0; return HAL_NVM_SUCCESS; }
hal_nvm_status_t hal_nvm_clear_all(void) { memset(nvm_sizes, 0, sizeof(nvm_sizes)); return HAL_NVM_SUCCESS; }

static void reset_nvm(void) { memset(nvm_items, 0, sizeof(nvm_items)); memset(nvm_sizes, 0, sizeof(nvm_sizes)); fail_writes = false; }

TEST(migrates_every_released_owner_record_byte_fixture) {
    relay_state_record_t legacy_relay = { .on_off = 1, .startup_mode = 3, .indicator_mode = 2,
                                          .indicator_state = 1, .inching_ms = 321, .interlock_group = 4 };
    action_switch_record_t legacy_switch = { .mode = 2, .action = 3, .relay_mode = 4,
                                              .relay_index = 5, .hold_duration_ms = 600,
                                              .level_move_rate = 7, .binded_mode = 8,
                                              .multi_click_gap_ms = 900, .debounce_ms = 10 };
    action_cover_switch_record_t legacy_cover = { .cover_index = 2, .reversal = 1,
                                                   .local_mode = 3, .binded_mode = 4,
                                                   .switch_type = 5, .multi_click_gap_ms = 700,
                                                   .debounce_ms = 20 };
    relay_state_record_t relay;
    action_switch_record_t sw;
    action_cover_switch_record_t cover;

    reset_nvm();
    ASSERT_EQ(HAL_NVM_SUCCESS, hal_nvm_write(NV_ITEM_RELAY_CLUSTER_DATA(0), sizeof(legacy_relay), (uint8_t *)&legacy_relay));
    ASSERT_EQ(HAL_NVM_SUCCESS, hal_nvm_write(NV_ITEM_SWITCH_CLUSTER_DATA(0), sizeof(legacy_switch), (uint8_t *)&legacy_switch));
    ASSERT_EQ(HAL_NVM_SUCCESS, hal_nvm_write(NV_ITEM_COVER_SWITCH_CONFIG(0), sizeof(legacy_cover), (uint8_t *)&legacy_cover));
    ASSERT_TRUE(capability_state_migrate_released());
    ASSERT_TRUE(capability_state_load_relay(0, &relay));
    ASSERT_TRUE(capability_state_load_switch(0, &sw));
    ASSERT_TRUE(capability_state_load_cover_switch(0, &cover));
    ASSERT_EQ(0, memcmp(&legacy_relay, &relay, sizeof(relay)));
    ASSERT_EQ(0, memcmp(&legacy_switch, &sw, sizeof(sw)));
    ASSERT_EQ(0, memcmp(&legacy_cover, &cover, sizeof(cover)));
}

TEST(migration_failure_clears_owned_records) {
    relay_state_record_t legacy = { .on_off = 1 };

    reset_nvm();
    ASSERT_EQ(HAL_NVM_SUCCESS, hal_nvm_write(NV_ITEM_RELAY_CLUSTER_DATA(0), sizeof(legacy), (uint8_t *)&legacy));
    fail_writes = true;
    ASSERT_TRUE(!capability_state_migrate_released());
    fail_writes = false;
    capability_state_clear_owned();
    ASSERT_TRUE(!capability_state_load_relay(0, &legacy));
}

int main(void) {
    RUN_TEST(migrates_every_released_owner_record_byte_fixture);
    RUN_TEST(migration_failure_clears_owned_records);
    return test_runner_failures == 0 ? 0 : 1;
}
