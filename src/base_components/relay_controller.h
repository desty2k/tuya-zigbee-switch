#ifndef _RELAY_CONTROLLER_H_
#define _RELAY_CONTROLLER_H_

#include "base_components/relay_driver.h"
#include "hal/zigbee.h"
#include <stdbool.h>
#include <stdint.h>

#define RELAY_CONTROLLER_MAX_RELAYS    10

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
} relay_request_source_t;

typedef struct {
    uint8_t                relay_id;
    relay_request_type_t   type;
    uint32_t               duration_ms;
    relay_request_source_t source;
} relay_request_t;

typedef void (*relay_state_cb_t)(void *param, uint8_t relay_id, bool is_on);

void relay_ctrl_init(void);
uint8_t relay_ctrl_add(relay_driver_t *driver);
void relay_ctrl_set_state_callback(uint8_t relay_id, relay_state_cb_t callback,
                                   void *param);
hal_zigbee_cmd_result_t relay_ctrl_submit(const relay_request_t *request);
bool relay_ctrl_is_on(uint8_t relay_id);
uint8_t relay_ctrl_count(void);

#endif
