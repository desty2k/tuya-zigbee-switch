#include "interlock.h"

#include "base_components/relay_controller.h"
#include "hal/timer.h"
#include <stddef.h>

static interlock_group_t interlock_groups[INTERLOCK_MAX_GROUPS];
static uint8_t           interlock_group_count;
static uint8_t           interlock_relay_groups[RELAY_CONTROLLER_MAX_RELAYS];

static interlock_group_t *interlock_find_group(uint8_t group_id) {
    for (uint8_t i = 0; i < interlock_group_count; i++) {
        if (interlock_groups[i].group_id == group_id) {
            return &interlock_groups[i];
        }
    }
    return NULL;
}

static void interlock_dead_time_elapsed(void *arg) {
    interlock_group_t *group    = (interlock_group_t *)arg;
    uint8_t            relay_id = group->pending_on_relay;

    group->pending_on_relay = INTERLOCK_NO_PENDING;
    if (relay_id == INTERLOCK_NO_PENDING) {
        return;
    }
    group->resume_relay = relay_id;
    relay_ctrl_submit(&(relay_request_t){
        .relay_id = relay_id,
        .type     = RELAY_REQUEST_ON,
        .source   = RELAY_SOURCE_INTERLOCK,
    });
}

void interlock_init(void) {
    interlock_group_count = 0;
    for (uint8_t relay_id = 0; relay_id < RELAY_CONTROLLER_MAX_RELAYS;
         relay_id++) {
        interlock_relay_groups[relay_id] = INTERLOCK_NO_GROUP;
    }
}

bool interlock_add_group(uint8_t group_id, uint16_t relay_mask,
                         uint16_t dead_time_ms) {
    interlock_group_t *group;

    if (group_id == INTERLOCK_NO_GROUP || relay_mask == 0) {
        return false;
    }
    group = interlock_find_group(group_id);
    if (group == NULL) {
        if (interlock_group_count >= INTERLOCK_MAX_GROUPS) {
            return false;
        }
        group = &interlock_groups[interlock_group_count++];
        timer_init(&group->dead_time_timer, interlock_dead_time_elapsed, group);
    } else {
        timer_cancel(&group->dead_time_timer);
        if (group->pending_on_relay != INTERLOCK_NO_PENDING) {
            relay_ctrl_cancel_deferred_on(group->pending_on_relay);
        }
        for (uint8_t relay_id = 0;
             relay_id < RELAY_CONTROLLER_MAX_RELAYS; relay_id++) {
            if ((group->relay_mask & ((uint16_t)1 << relay_id)) != 0) {
                interlock_relay_groups[relay_id] = INTERLOCK_NO_GROUP;
            }
        }
    }
    group->group_id         = group_id;
    group->relay_mask       = 0;
    group->dead_time_ms     = dead_time_ms;
    group->pending_on_relay = INTERLOCK_NO_PENDING;
    group->resume_relay     = INTERLOCK_NO_PENDING;
    group->last_off_ms      = 0;
    group->has_last_off     = false;

    for (uint8_t relay_id = 0; relay_id < RELAY_CONTROLLER_MAX_RELAYS;
         relay_id++) {
        if ((relay_mask & ((uint16_t)1 << relay_id)) != 0) {
            interlock_set_relay_group(relay_id, group_id);
        }
    }
    return true;
}

void interlock_set_relay_group(uint8_t relay_id, uint8_t group_id) {
    interlock_group_t *group;
    interlock_group_t *previous;

    if (relay_id >= RELAY_CONTROLLER_MAX_RELAYS) {
        return;
    }
    previous = interlock_find_group(interlock_relay_groups[relay_id]);
    if (previous != NULL) {
        previous->relay_mask &= ~((uint16_t)1 << relay_id);
        interlock_cancel_pending(relay_id);
    }
    interlock_relay_groups[relay_id] = INTERLOCK_NO_GROUP;
    if (group_id == INTERLOCK_NO_GROUP) {
        return;
    }
    group = interlock_find_group(group_id);
    if (group == NULL) {
        if (!interlock_add_group(group_id, (uint16_t)1 << relay_id, 0)) {
            return;
        }
        group = interlock_find_group(group_id);
    }
    group->relay_mask |= (uint16_t)1 << relay_id;
    interlock_relay_groups[relay_id] = group_id;
}

bool interlock_prepare(uint8_t relay_id) {
    interlock_group_t *group;
    uint32_t           elapsed;
    bool peer_was_on = false;

    if (relay_id >= RELAY_CONTROLLER_MAX_RELAYS) {
        return true;
    }
    group = interlock_find_group(interlock_relay_groups[relay_id]);
    if (group == NULL) {
        return true;
    }
    if (group->resume_relay == relay_id) {
        group->resume_relay = INTERLOCK_NO_PENDING;
        return true;
    }
    if (group->pending_on_relay != INTERLOCK_NO_PENDING &&
        group->pending_on_relay != relay_id) {
        relay_ctrl_cancel_deferred_on(group->pending_on_relay);
        group->last_off_ms  = hal_millis();
        group->has_last_off = true;
        timer_cancel(&group->dead_time_timer);
    }
    for (uint8_t peer_id = 0; peer_id < relay_ctrl_count(); peer_id++) {
        if (peer_id != relay_id &&
            (group->relay_mask & ((uint16_t)1 << peer_id)) != 0 &&
            relay_ctrl_is_on(peer_id)) {
            peer_was_on = true;
            relay_ctrl_submit(&(relay_request_t){
                .relay_id = peer_id,
                .type     = RELAY_REQUEST_OFF,
                .source   = RELAY_SOURCE_INTERLOCK,
            });
        }
    }
    if (group->dead_time_ms == 0 ||
        (!peer_was_on && !group->has_last_off)) {
        return true;
    }
    elapsed = hal_millis() - group->last_off_ms;
    if (!peer_was_on && elapsed >= group->dead_time_ms) {
        return true;
    }
    group->pending_on_relay = relay_id;
    timer_restart(&group->dead_time_timer,
                  peer_was_on ? group->dead_time_ms
                              : group->dead_time_ms - elapsed);
    return false;
}

void interlock_cancel_pending(uint8_t relay_id) {
    interlock_group_t *group;

    if (relay_id >= RELAY_CONTROLLER_MAX_RELAYS) {
        return;
    }
    group = interlock_find_group(interlock_relay_groups[relay_id]);
    if (group != NULL && group->pending_on_relay == relay_id) {
        timer_cancel(&group->dead_time_timer);
        group->pending_on_relay = INTERLOCK_NO_PENDING;
        relay_ctrl_cancel_deferred_on(relay_id);
    }
}

void interlock_note_off(uint8_t relay_id) {
    interlock_group_t *group;

    if (relay_id >= RELAY_CONTROLLER_MAX_RELAYS) {
        return;
    }
    group = interlock_find_group(interlock_relay_groups[relay_id]);
    if (group != NULL) {
        group->last_off_ms  = hal_millis();
        group->has_last_off = true;
    }
}
