#ifndef _FEATURE_WIRING_H_
#define _FEATURE_WIRING_H_

#include <stdint.h>

typedef enum {
    INPUT_BUTTON_ONBOARD,
    INPUT_BUTTON_SWITCH,
    INPUT_BUTTON_COVER_SWITCH,
} input_button_role_t;

void feature_wiring_init(void);
void feature_wiring_init_relays(void);

#endif
