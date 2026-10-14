#include "capability_state.h"
#include "nvm_items.h"
#include "hal/nvm.h"

static bool load(uint8_t item, uint16_t size, void *record) {
    return hal_nvm_read(item, size, record) == HAL_NVM_SUCCESS;
}

static bool store(uint8_t item, uint16_t size, const void *record) {
    return hal_nvm_write(item, size, (uint8_t *)record) == HAL_NVM_SUCCESS;
}

bool capability_state_load_relay(uint8_t id, relay_state_record_t *r) {
    return r != 0 && load(NV_ITEM_RELAY_STATE(id), sizeof(*r), r);
}

bool capability_state_store_relay(uint8_t id, const relay_state_record_t *r) {
    return r != 0 && store(NV_ITEM_RELAY_STATE(id), sizeof(*r), r);
}

bool capability_state_load_switch(uint8_t id, action_switch_record_t *r) {
    return r != 0 && load(NV_ITEM_ACTION_SWITCH_STATE(id), sizeof(*r), r);
}

bool capability_state_store_switch(uint8_t id, const action_switch_record_t *r) {
    return r != 0 && store(NV_ITEM_ACTION_SWITCH_STATE(id), sizeof(*r), r);
}

bool capability_state_load_cover_switch(uint8_t id, action_cover_switch_record_t *r) {
    return r != 0 && load(NV_ITEM_ACTION_COVER_SWITCH_STATE(id), sizeof(*r), r);
}

bool capability_state_store_cover_switch(uint8_t id, const action_cover_switch_record_t *r) {
    return r != 0 && store(NV_ITEM_ACTION_COVER_SWITCH_STATE(id), sizeof(*r), r);
}

static void clear_relay_states(void) {
    for (uint8_t i = 0; i < MAX_RELAY_STATES; i++) {
        hal_nvm_delete(NV_ITEM_RELAY_STATE(i));
    }
}

void capability_state_sync_relay_topology(uint32_t topology) {
    uint32_t stored;

    if (load(NV_ITEM_RELAY_TOPOLOGY, sizeof(stored), &stored)) {
        if (stored == topology) {
            return;
        }
        clear_relay_states();
    }
    (void)store(NV_ITEM_RELAY_TOPOLOGY, sizeof(topology), &topology);
}

void capability_state_clear_owned(void) {
    clear_relay_states();
    for (uint8_t i = 0; i < MAX_SWITCH_STATES; i++) {
        hal_nvm_delete(NV_ITEM_ACTION_SWITCH_STATE(i));
    }
    for (uint8_t i = 0; i < MAX_COVER_SWITCHES; i++) {
        hal_nvm_delete(NV_ITEM_ACTION_COVER_SWITCH_STATE(i));
    }
}
