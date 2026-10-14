#ifndef DEVICE_CONFIG_CAPABILITY_STATE_H_
#define DEVICE_CONFIG_CAPABILITY_STATE_H_

#include <stdbool.h>
#include <stdint.h>

typedef struct { uint8_t  on_off, startup_mode, indicator_mode, indicator_state;
                 uint16_t inching_ms; uint8_t interlock_group; } relay_state_record_t;
typedef struct { uint8_t  mode, action, relay_mode, relay_index;
                 uint16_t hold_duration_ms; uint8_t level_move_rate, binded_mode;
                 uint16_t multi_click_gap_ms, debounce_ms; } action_switch_record_t;
typedef struct { uint8_t  cover_index, reversal, local_mode, binded_mode, switch_type;
                 uint16_t multi_click_gap_ms, debounce_ms; } action_cover_switch_record_t;

bool capability_state_load_relay(uint8_t id, relay_state_record_t *record);
bool capability_state_store_relay(uint8_t id, const relay_state_record_t *record);
bool capability_state_load_switch(uint8_t id, action_switch_record_t *record);
bool capability_state_store_switch(uint8_t id, const action_switch_record_t *record);
bool capability_state_load_cover_switch(uint8_t id, action_cover_switch_record_t *record);
bool capability_state_store_cover_switch(uint8_t id, const action_cover_switch_record_t *record);
void capability_state_clear_owned(void);

/**
 * Deletes every relay record when the relay topology differs from the one
 * the records were written under, then stores the new topology. Without a
 * stored topology the existing records are kept and the topology recorded.
 */
void capability_state_sync_relay_topology(uint32_t topology);

#endif
