#ifndef _FEATURE_WIRING_H_
#define _FEATURE_WIRING_H_

#include <stdint.h>

#include "base_components/interlock.h"

typedef enum {
    INPUT_BUTTON_ONBOARD,
    INPUT_BUTTON_SWITCH,
    INPUT_BUTTON_COVER_SWITCH,
} input_button_role_t;

typedef struct {
    uint16_t relay_mask;
    uint16_t dead_time_ms;
} device_interlock_config_t;

void feature_wiring_init(void);
void feature_wiring_init_relays(void);

#endif
