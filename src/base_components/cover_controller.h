#ifndef BASE_COMPONENTS_COVER_CONTROLLER_H_
#define BASE_COMPONENTS_COVER_CONTROLLER_H_

#include <stdbool.h>
#include <stdint.h>

#define COVER_CONTROLLER_MAX_COVERS        3
#define COVER_CONTROLLER_MAX_OBSERVERS     4
#define COVER_DEFAULT_MANUAL_MAX_RUN_MS    120000u
#define COVER_POSITION_MAX_BP              10000u
#define COVER_POSITION_UPDATE_MS           100u
#define COVER_MAX_TRAVEL_TIME_MS           429496u

typedef enum {
    COVER_STOPPED,
    COVER_OPENING,
    COVER_CLOSING,
} cover_motion_t;

typedef enum {
    COVER_POSITION_UNKNOWN,
    COVER_POSITION_ESTIMATED,
    COVER_POSITION_KNOWN,
} cover_position_confidence_t;

typedef enum {
    COVER_RESULT_APPLIED,
    COVER_RESULT_UNCHANGED,
    COVER_RESULT_DEFERRED,
    COVER_RESULT_BLOCKED,
    COVER_RESULT_INVALID,
} cover_result_t;

typedef enum {
    COVER_ACTION_OPEN,
    COVER_ACTION_CLOSE,
    COVER_ACTION_STOP,
} cover_action_t;

typedef struct {
    uint8_t  open_relay_id;
    uint8_t  close_relay_id;
    uint32_t manual_max_run_ms;
    uint32_t default_open_time_ms;
    uint32_t default_close_time_ms;
    uint16_t default_closed_deadzone_bp;
    uint16_t default_open_deadzone_bp;
    uint16_t default_overrun_ms;
    uint8_t  nvm_item; /* NVM item holding this cover's state record */
} cover_controller_config_t;

typedef struct {
    uint8_t                     version;
    uint8_t                     motor_reversal;
    uint8_t                     motion_active;
    cover_position_confidence_t confidence;
    uint32_t                    open_time_ms;
    uint32_t                    close_time_ms;
    uint16_t                    closed_deadzone_bp;
    uint16_t                    open_deadzone_bp;
    uint16_t                    overrun_ms;
    uint16_t                    travel_position_bp;
} cover_state_record_t;

typedef struct {
    cover_motion_t              motion;
    cover_position_confidence_t confidence;
    uint16_t                    travel_position_bp;
    uint16_t                    open_position_bp;
    uint16_t                    target_travel_bp;
    uint32_t                    open_time_ms;
    uint32_t                    close_time_ms;
    uint16_t                    closed_deadzone_bp;
    uint16_t                    open_deadzone_bp;
    uint16_t                    overrun_ms;
    bool                        motor_reversal;
} cover_state_t;

typedef void (*cover_state_cb_t)(void *context, uint8_t cover_id,
                                 const cover_state_t *state);

void cover_controller_init(void);
uint8_t cover_controller_add(const cover_controller_config_t *config);
uint8_t cover_controller_count(void);
bool cover_controller_subscribe(uint8_t cover_id, cover_state_cb_t callback,
                                void *context);
cover_motion_t cover_controller_motion(uint8_t cover_id);
bool cover_controller_state(uint8_t cover_id, cover_state_t *state);

/** True when travel times and deadzones in `state` are within range and the
 * deadzones leave part of the travel between them. */
bool cover_controller_settings_valid(const cover_state_t *state);
uint8_t cover_controller_lift_percentage(uint8_t cover_id);
bool cover_controller_motor_reversal(uint8_t cover_id);
cover_result_t cover_controller_set_motor_reversal(uint8_t cover_id,
                                                   bool motor_reversal);
cover_result_t cover_open(uint8_t cover_id);
cover_result_t cover_close(uint8_t cover_id);
cover_result_t cover_stop(uint8_t cover_id);
cover_result_t cover_goto(uint8_t cover_id, uint16_t open_position_bp);
cover_result_t cover_controller_set_open_time(uint8_t cover_id,
                                              uint32_t open_time_ms);
cover_result_t cover_controller_set_close_time(uint8_t cover_id,
                                               uint32_t close_time_ms);
cover_result_t cover_controller_set_closed_deadzone(uint8_t cover_id,
                                                    uint16_t deadzone_bp);
cover_result_t cover_controller_set_open_deadzone(uint8_t cover_id,
                                                  uint16_t deadzone_bp);
cover_result_t cover_controller_set_overrun(uint8_t cover_id,
                                            uint16_t overrun_ms);
void cover_controller_action(void *context, uint8_t cover_id,
                             uint16_t action);

#endif
