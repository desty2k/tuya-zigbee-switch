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

static bool interlock_peer_is_on(const interlock_group_t *group,
                                 uint8_t relay_id, uint8_t peer_id) {
    return peer_id != relay_id &&
           (group->relay_mask & ((uint16_t)1 << peer_id)) != 0 &&
           relay_ctrl_is_on(peer_id);
}

static void interlock_dead_time_elapsed(void *arg) {
    interlock_group_t *group    = (interlock_group_t *)arg;
    uint8_t            relay_id = group->pending_on_relay;

    group->pending_on_relay = INTERLOCK_NO_PENDING;
    if (relay_id == INTERLOCK_NO_PENDING) {
        return;
    }

    /* resume_relay lets interlock_prepare() admit this one switch-on without
     * restarting the dead time. It is cleared whatever the submit outcome:
     * a blocked or unchanged request must not leave a later, unrelated ON
     * skipping the peer shutdown. */
    group->resume_relay = relay_id;
    (void)relay_ctrl_submit(&(relay_request_t){
        .relay_id = relay_id,
        .type     = RELAY_REQUEST_ON,
        .source   = RELAY_SOURCE_INTERLOCK,
    });
    group->resume_relay = INTERLOCK_NO_PENDING;
}

void interlock_init(void) {
    interlock_group_count = 0;
    for (uint8_t relay_id = 0; relay_id < RELAY_CONTROLLER_MAX_RELAYS;
         relay_id++) {
        interlock_relay_groups[relay_id] = INTERLOCK_NO_GROUP;
    }
}

bool interlock_add_group(uint8_t group_id, uint16_t dead_time_ms) {
    interlock_group_t *group;

    if (group_id == INTERLOCK_NO_GROUP || interlock_find_group(group_id) != NULL ||
        interlock_group_count >= INTERLOCK_MAX_GROUPS) {
        return false;
    }
    group  = &interlock_groups[interlock_group_count++];
    *group = (interlock_group_t){
        .group_id         = group_id,
        .dead_time_ms     = dead_time_ms,
        .pending_on_relay = INTERLOCK_NO_PENDING,
        .resume_relay     = INTERLOCK_NO_PENDING,
    };
    timer_init(&group->dead_time_timer, interlock_dead_time_elapsed, group);
    return true;
}

bool interlock_set_relay_group(uint8_t relay_id, uint8_t group_id) {
    interlock_group_t *group = NULL;
    interlock_group_t *previous;

    if (relay_id >= RELAY_CONTROLLER_MAX_RELAYS) {
        return false;
    }
    if (group_id != INTERLOCK_NO_GROUP) {
        group = interlock_find_group(group_id);
        if (group == NULL) {
            return false;
        }
    }
    previous = interlock_find_group(interlock_relay_groups[relay_id]);
    if (previous != NULL) {
        previous->relay_mask &= ~((uint16_t)1 << relay_id);
        interlock_cancel_pending(relay_id);
    }
    interlock_relay_groups[relay_id] = INTERLOCK_NO_GROUP;
    if (group == NULL) {
        return true;
    }
    group->relay_mask |= (uint16_t)1 << relay_id;
    interlock_relay_groups[relay_id] = group_id;

    /* A relay joining while it and a member are both on would leave two
     * group relays energised; the joining one gives way. */
    if (relay_ctrl_is_on(relay_id)) {
        for (uint8_t peer_id = 0; peer_id < relay_ctrl_count(); peer_id++) {
            if (interlock_peer_is_on(group, relay_id, peer_id)) {
                (void)relay_ctrl_submit(&(relay_request_t){
                    .relay_id = relay_id,
                    .type     = RELAY_REQUEST_OFF,
                    .source   = RELAY_SOURCE_INTERLOCK,
                });
                break;
            }
        }
    }
    return true;
}

uint8_t interlock_relay_group(uint8_t relay_id) {
    return relay_id < RELAY_CONTROLLER_MAX_RELAYS ? interlock_relay_groups[relay_id]
                                                  : INTERLOCK_NO_GROUP;
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
        if (interlock_peer_is_on(group, relay_id, peer_id)) {
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
