#ifndef _CHILD_LOCK_H_
#define _CHILD_LOCK_H_

#include "base_components/button_input.h"
#include "base_components/indicator_feedback.h"
#include <stdbool.h>
#include <stdint.h>

/* Hold time of a two-button lock gesture, in ms. */
#define CHILD_LOCK_PAIR_HOLD_MS      3000

/* Hold time of a one-button lock gesture, in ms. Longer than the longest
 * long-press duration the converters offer (5000 ms), and a held press is
 * not a click, so it never counts towards the multi-press reset. */
#define CHILD_LOCK_SINGLE_HOLD_MS    10000

/* One gesture per cover switch plus one for the switch buttons. */
#define CHILD_LOCK_GESTURE_MAX       4

#define CHILD_LOCK_NO_BUTTON         0xFFu

/* Indicator blinks: on locking, on unlocking, on a press while locked. */
#define CHILD_LOCK_BLINKS_LOCK       3
#define CHILD_LOCK_BLINKS_UNLOCK     2
#define CHILD_LOCK_BLINKS_PRESS      1

/* Persists the state a lock gesture asks for; the lock changes only when it
 * returns true. */
typedef bool (*child_lock_store_t)(void *context, bool locked);

/* Whether a gesture may toggle the lock now. A latching input (toggle switch
 * type) stays down while its load is on, so its gesture must be disabled. */
typedef bool (*child_lock_gesture_enabled_t)(void *context);

/** Forgets every gesture and the indicator; keeps the lock state and store. */
void child_lock_reset_wiring(void);
void child_lock_register_input(void);

/**
 * Holding `first` and `second` together for CHILD_LOCK_PAIR_HOLD_MS, or
 * `first` alone for CHILD_LOCK_SINGLE_HOLD_MS when `second` is
 * CHILD_LOCK_NO_BUTTON, toggles the lock. `enabled` (NULL: always) is
 * checked when the hold starts and when it elapses.
 */
bool child_lock_add_gesture(uint8_t first, uint8_t second,
                            child_lock_gesture_enabled_t enabled, void *context);
void child_lock_set_indicator(indicator_feedback_t *indicator);
void child_lock_set_store(child_lock_store_t store, void *context);

/** Sets the lock without feedback or observer call (remote write, boot). */
void child_lock_set(bool locked);
bool child_lock_is_on(void);

/**
 * Whether a local input consumer must ignore this button event. A press that
 * starts while locked is ignored up to and including its release, and blinks
 * the indicator once; a press that started unlocked completes normally even
 * if the lock engages while it is held.
 */
bool child_lock_blocks(const button_event_t *event);

/** Whether the current or last press of the button started while locked. */
bool child_lock_press_blocked(uint8_t button_id);

#endif
