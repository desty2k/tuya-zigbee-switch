#ifndef _INTERLOCK_H_
#define _INTERLOCK_H_

#include "base_components/timer_service.h"
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

void interlock_init(void);
bool interlock_add_group(uint8_t group_id, uint16_t relay_mask,
                         uint16_t dead_time_ms);
void interlock_set_relay_group(uint8_t relay_id, uint8_t group_id);
bool interlock_prepare(uint8_t relay_id);
void interlock_cancel_pending(uint8_t relay_id);
void interlock_note_off(uint8_t relay_id);

#endif
