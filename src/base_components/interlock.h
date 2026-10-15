#ifndef _INTERLOCK_H_
#define _INTERLOCK_H_

#include "base_components/timer_service.h"
#include "device_config/build_features.h"
#include <stdbool.h>
#include <stdint.h>

#define INTERLOCK_MAX_GROUPS    13
#define INTERLOCK_NO_GROUP      0
#define INTERLOCK_NO_PENDING    UINT8_MAX

typedef struct {
    uint8_t     group_id;
    uint16_t    relay_mask;
    uint16_t    dead_time_ms;
    uint8_t     pending_on_relay;
    uint8_t     resume_relay;
    uint32_t    last_off_ms;
    bool        has_last_off;
    app_timer_t dead_time_timer;
} interlock_group_t;

#if FEATURE_INTERLOCK

void interlock_init(void);

/** Creates an empty group; relays join through interlock_set_relay_group.
 * Fails for INTERLOCK_NO_GROUP, an existing id or a full group table. */
bool interlock_add_group(uint8_t group_id, uint16_t dead_time_ms);

/** Moves a relay to an existing group, or out of any group with
 * INTERLOCK_NO_GROUP. Fails, changing nothing, for an unknown group id. */
bool interlock_set_relay_group(uint8_t relay_id, uint8_t group_id);

/** Group of a relay, INTERLOCK_NO_GROUP when it has none. */
uint8_t interlock_relay_group(uint8_t relay_id);
bool interlock_prepare(uint8_t relay_id);
void interlock_cancel_pending(uint8_t relay_id);
void interlock_note_off(uint8_t relay_id);

#else

/* Without the feature the parser rejects `K` and covers are absent, so no
 * group exists: every relay stays ungrouped and switches on immediately. */
static inline void interlock_init(void) {
}

static inline bool interlock_add_group(uint8_t group_id,
                                       uint16_t dead_time_ms) {
    (void)group_id;
    (void)dead_time_ms;
    return false;
}

static inline bool interlock_set_relay_group(uint8_t relay_id,
                                             uint8_t group_id) {
    (void)relay_id;
    return group_id == INTERLOCK_NO_GROUP;
}

static inline uint8_t interlock_relay_group(uint8_t relay_id) {
    (void)relay_id;
    return INTERLOCK_NO_GROUP;
}

static inline bool interlock_prepare(uint8_t relay_id) {
    (void)relay_id;
    return true;
}

static inline void interlock_cancel_pending(uint8_t relay_id) {
    (void)relay_id;
}

static inline void interlock_note_off(uint8_t relay_id) {
    (void)relay_id;
}

#endif

#endif
