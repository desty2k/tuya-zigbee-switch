#ifndef DEVICE_CONFIG_NVM_ITEMS_H_
#define DEVICE_CONFIG_NVM_ITEMS_H_

#include "device_config/device_composition.h"

#define MAX_RELAYS                       5
#define MAX_SWITCHES                     5
#define MAX_COVER_SWITCHES               3
#define MAX_COVERS                       3
#define MAX_METERS                       2

#define NV_ITEM_CURRENT_VERSION_IN_NV    1
#define NV_ITEM_DEVICE_CONFIG            2
#define NV_ITEM_BASIC_CLUSTER_DATA       3
// switch_idx and relay_idx below are zero indexes, e.g. first switch has
// switch_idx = 0
#define NV_ITEM_SWITCH_CLUSTER_DATA(switch_idx) \
        (NV_ITEM_BASIC_CLUSTER_DATA + 1 + switch_idx)
#define NV_ITEM_RELAY_CLUSTER_DATA(relay_idx) \
        (NV_ITEM_BASIC_CLUSTER_DATA + MAX_SWITCHES + 1 + relay_idx)
#define NV_ITEM_COVER_SWITCH_CONFIG(cover_switch_idx) \
        (NV_ITEM_BASIC_CLUSTER_DATA + MAX_SWITCHES + MAX_RELAYS + 1 + cover_switch_idx)
#define NV_ITEM_COVER_CONFIG(cover_idx)                                                    \
        (NV_ITEM_BASIC_CLUSTER_DATA + MAX_SWITCHES + MAX_RELAYS + MAX_COVER_SWITCHES + 1 + \
         cover_idx)

// 3 + 5 (switches) + 5 (relays) + 3 (cover switches) + 3 (covers) = 19
// Adding room for future items, so starting from 32
#define NV_ITEM_DEVICE_TYPE    32

/* Relay and switch records sit in windows of 10 NV ids (see below). */
#define MAX_RELAY_STATES       DEVICE_COMPOSITION_MAX_RELAYS
#define MAX_SWITCH_STATES      DEVICE_COMPOSITION_MAX_SWITCHES
#if MAX_RELAY_STATES > 10 || MAX_SWITCH_STATES > 10
#error "relay or switch records overflow their NV id window"
#endif

#define NV_ITEM_RELAY_STATE(relay_idx)                   (40 + (relay_idx))
#define NV_ITEM_INDICATOR_STATE(indicator_idx)           (50 + (indicator_idx))
#define NV_ITEM_ACTION_SWITCH_STATE(switch_idx)          (60 + (switch_idx))
#define NV_ITEM_ACTION_COVER_SWITCH_STATE(switch_idx)    (70 + (switch_idx))
#define NV_ITEM_COVER_STATE(cover_idx)                   (80 + (cover_idx))
#define NV_ITEM_METER_STATE(meter_idx)                   (90 + (meter_idx))
#define NV_ITEM_DEVICE_CONFIG_SERVICE      100

/* uint32_t device_composition_relay_topology() of the config the relay
 * records were written under. */
#define NV_ITEM_RELAY_TOPOLOGY             101

/* uint8_t child lock state: 1 locked, 0 or missing unlocked. */
#define NV_ITEM_CHILD_LOCK                 102

#define NV_ITEM_MULTI_PRESS_RESET_COUNT    33
#define NV_ITEM_POLL_CONTROL_CONFIG        34

#endif /* DEVICE_CONFIG_NVM_ITEMS_H_ */
