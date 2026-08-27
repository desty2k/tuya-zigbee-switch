#include "hal/nvm.h"
#include "hal/printf_selector.h"
#include "nvm_items.h"

#ifdef HAL_SILABS
#include "silabs_config.h"
#endif

#define UNKNOWN_VERSION                0
#define RELAY_CONFIG_VERSION           2
#define INTERLOCK_CONFIG_VERSION       3
#define BUTTON_EVENT_CONFIG_VERSION    4

static void delete_switch_configs(void) {
    for (uint8_t switch_idx = 0; switch_idx < MAX_SWITCHES; switch_idx++) {
        hal_nvm_delete(NV_ITEM_SWITCH_CLUSTER_DATA(switch_idx));
    }
    for (uint8_t switch_idx = 0; switch_idx < MAX_COVER_SWITCHES;
         switch_idx++) {
        hal_nvm_delete(NV_ITEM_COVER_SWITCH_CONFIG(switch_idx));
    }
}

static void delete_relay_configs(void) {
    for (uint8_t relay_idx = 0; relay_idx < MAX_RELAYS; relay_idx++) {
        hal_nvm_delete(NV_ITEM_RELAY_CLUSTER_DATA(relay_idx));
    }
}

uint16_t read_version_in_nv() {
    uint16_t version;

    hal_nvm_status_t res = hal_nvm_read(NV_ITEM_CURRENT_VERSION_IN_NV,
                                        sizeof(version), (uint8_t *)&version);

    if (res == HAL_NVM_SUCCESS) {
        printf("read version form new location\r\n");
        return version;
    }

    return UNKNOWN_VERSION;
}

void write_version_to_nv(uint16_t version) {
    hal_nvm_status_t res = hal_nvm_write(NV_ITEM_CURRENT_VERSION_IN_NV,
                                         sizeof(version), (uint8_t *)&version);

    if (res != HAL_NVM_SUCCESS) {
        printf("Failed to write lastSeenVersion to NV, st: %d\r\n", res);
    }
}

void handle_version_changes() {
    uint16_t oldVersion     = read_version_in_nv();
    uint16_t currentVersion = NVM_MIGRATIONS_VERSION;

    printf("Old version: %d\r\n", oldVersion);
    printf("Current version: %d\r\n", currentVersion);

    if (oldVersion == currentVersion) {
        // Same version, nothing to do
        return;
    }

    if (oldVersion == UNKNOWN_VERSION) {
        // Either old device or it first boot after re-flash, just store version
        write_version_to_nv(currentVersion);
        return;
    }

    if (oldVersion < RELAY_CONFIG_VERSION) {
        delete_relay_configs();
    } else if (oldVersion < INTERLOCK_CONFIG_VERSION) {
        delete_relay_configs();
    }
    if (oldVersion < BUTTON_EVENT_CONFIG_VERSION) {
        delete_switch_configs();
    }
    write_version_to_nv(currentVersion);
}
