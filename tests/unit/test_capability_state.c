#include "device_config/capability_state.h"
#include "hal/nvm.h"
#include "support/runner.h"
#include <stdbool.h>
#include <string.h>

#define NVM_TEST_ITEM_SIZE    32

static uint8_t nvm_items[256][NVM_TEST_ITEM_SIZE];
static uint8_t nvm_sizes[256];

hal_nvm_status_t hal_nvm_write(uint8_t item_id, uint16_t size, uint8_t *data) {
    if (size > NVM_TEST_ITEM_SIZE || data == NULL) return HAL_NVM_ERROR;
    memcpy(nvm_items[item_id], data, size);
    nvm_sizes[item_id] = (uint8_t)size;
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
    return HAL_NVM_SUCCESS;
}

TEST(clear_owned_records_resets_every_capability_owner) {
    relay_state_record_t relay = { .on_off = 1 };
    action_switch_record_t sw = { .mode = 2 };
    action_cover_switch_record_t cover = { .cover_index = 1 };

    ASSERT_TRUE(capability_state_store_relay(0, &relay));
    ASSERT_TRUE(capability_state_store_switch(0, &sw));
    ASSERT_TRUE(capability_state_store_cover_switch(0, &cover));
    capability_state_clear_owned();
    ASSERT_TRUE(!capability_state_load_relay(0, &relay));
    ASSERT_TRUE(!capability_state_load_switch(0, &sw));
    ASSERT_TRUE(!capability_state_load_cover_switch(0, &cover));
}

int main(void) {
    RUN_TEST(clear_owned_records_resets_every_capability_owner);
    return test_runner_failures == 0 ? 0 : 1;
}
