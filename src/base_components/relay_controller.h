#ifndef _RELAY_CONTROLLER_H_
#define _RELAY_CONTROLLER_H_

#include "base_components/relay_driver.h"
#include <stdbool.h>
#include <stdint.h>

#define RELAY_CONTROLLER_MAX_RELAYS        10
#define RELAY_CONTROLLER_MAX_OBSERVERS     4
#define RELAY_CONTROLLER_MAX_INHIBITORS    16

typedef struct {
    uint16_t inching_ms;
    uint8_t  interlock_group;
} relay_config_t;

typedef enum {
    AUTO_OFF_NONE,
    AUTO_OFF_PULSE,
    AUTO_OFF_TIMED,
} auto_off_reason_t;

typedef enum {
    RELAY_RESULT_APPLIED,
    RELAY_RESULT_UNCHANGED,
    RELAY_RESULT_DEFERRED,
    RELAY_RESULT_BLOCKED,
    RELAY_RESULT_INVALID,
} relay_result_t;

typedef enum {
    RELAY_REQUEST_ON,
    RELAY_REQUEST_OFF,
    RELAY_REQUEST_TOGGLE,
    RELAY_REQUEST_PULSE,
    RELAY_REQUEST_ON_TIMED,
} relay_request_type_t;

typedef enum {
    RELAY_SOURCE_BUTTON,
    RELAY_SOURCE_GESTURE,
    RELAY_SOURCE_ZIGBEE,
    RELAY_SOURCE_TIMER,
    RELAY_SOURCE_INTERLOCK,
    RELAY_SOURCE_STARTUP,
    RELAY_SOURCE_COVER,
    RELAY_SOURCE_PROTECTION,
} relay_request_source_t;

typedef struct {
    uint8_t                relay_id;
    relay_request_type_t   type;
    uint32_t               duration_ms;
    relay_request_source_t source;
} relay_request_t;

typedef void (*relay_state_cb_t)(void *context, uint8_t relay_id, bool is_on,
                                 relay_request_source_t source);

void relay_ctrl_init(void);
uint8_t relay_ctrl_add(relay_driver_t *driver, relay_config_t *config);
bool relay_ctrl_subscribe(uint8_t relay_id, relay_state_cb_t callback,
                          void *context);
relay_result_t relay_ctrl_submit(const relay_request_t *request);
bool relay_ctrl_is_on(uint8_t relay_id);
uint8_t relay_ctrl_count(void);
void relay_ctrl_set_inching_ms(uint8_t relay_id, uint16_t inching_ms);
uint16_t relay_ctrl_get_inching_ms(uint8_t relay_id);
void relay_ctrl_set_interlock_group(uint8_t relay_id, uint8_t group_id);
uint8_t relay_ctrl_get_interlock_group(uint8_t relay_id);
auto_off_reason_t relay_ctrl_auto_off_reason(uint8_t relay_id);
void relay_ctrl_cancel_deferred_on(uint8_t relay_id);
bool relay_ctrl_inhibit(uint16_t relay_mask, uint8_t inhibit_id);
bool relay_ctrl_release(uint16_t relay_mask, uint8_t inhibit_id);

#endif
