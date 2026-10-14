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

void capability_state_clear_owned(void) {
    for (uint8_t i = 0; i < MAX_RELAYS; i++) {
        hal_nvm_delete(NV_ITEM_RELAY_STATE(i));
    }
    for (uint8_t i = 0; i < MAX_SWITCHES; i++) {
        hal_nvm_delete(NV_ITEM_ACTION_SWITCH_STATE(i));
    }
    for (uint8_t i = 0; i < MAX_COVER_SWITCHES; i++) {
        hal_nvm_delete(NV_ITEM_ACTION_COVER_SWITCH_STATE(i));
    }
}

void capability_state_add_cover_switch_hold(void) {
    struct { uint8_t  cover_index, reversal, local_mode, binded_mode, switch_type;
             uint16_t multi_click_gap_ms, debounce_ms; } old;

    for (uint8_t i = 0; i < MAX_COVER_SWITCHES; i++) {
        if (!load(NV_ITEM_ACTION_COVER_SWITCH_STATE(i), sizeof(old), &old)) continue;

        action_cover_switch_record_t record = {
            .cover_index        = old.cover_index,
            .reversal           = old.reversal,
            .local_mode         = old.local_mode,
            .binded_mode        = old.binded_mode,
            .switch_type        = old.switch_type,
            .multi_click_gap_ms = old.multi_click_gap_ms,
            .debounce_ms        = old.debounce_ms,
            .hold_duration_ms   = COVER_SWITCH_DEFAULT_HOLD_MS,
        };
        (void)store(NV_ITEM_ACTION_COVER_SWITCH_STATE(i), sizeof(record), &record);
    }
}
