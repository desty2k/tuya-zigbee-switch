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
    for (uint8_t i = 0; i < MAX_RELAYS; i++) hal_nvm_delete(NV_ITEM_RELAY_STATE(i));
    for (uint8_t i = 0;
         i < MAX_SWITCHES; i++) hal_nvm_delete(NV_ITEM_ACTION_SWITCH_STATE(i)); for (uint8_t i = 0;
                                                                                     i <
                                                                                     MAX_COVER_SWITCHES;
                                                                                     i++)
        hal_nvm_delete(NV_ITEM_ACTION_COVER_SWITCH_STATE(i));
}

bool capability_state_migrate_released(void) {
    relay_state_record_t relay; action_switch_record_t sw; action_cover_switch_record_t cover;

    for (uint8_t i = 0; i < MAX_RELAYS;
         i++) if (load(NV_ITEM_RELAY_CLUSTER_DATA(i), sizeof(relay),
                       &relay) && !capability_state_store_relay(i, &relay)) return false;

    for (uint8_t i = 0; i < MAX_SWITCHES;
         i++) if (load(NV_ITEM_SWITCH_CLUSTER_DATA(i), sizeof(sw),
                       &sw) && !capability_state_store_switch(i, &sw)) return false;

    for (uint8_t i = 0; i < MAX_COVER_SWITCHES;
         i++) if (load(NV_ITEM_COVER_SWITCH_CONFIG(i), sizeof(cover),
                       &cover) && !capability_state_store_cover_switch(i, &cover)) return false;

    return true;
}
