#ifndef FEATURE_WIRING_H_
#define FEATURE_WIRING_H_
#include "device_config/device_composition.h"
typedef enum { FEATURE_WIRING_OK,
               FEATURE_WIRING_INVALID }      feature_wiring_result_t;
typedef enum { INPUT_BUTTON_ONBOARD, INPUT_BUTTON_SWITCH,
               INPUT_BUTTON_COVER_SWITCH }   input_button_role_t;
feature_wiring_result_t feature_wiring_build(const device_composition_t *composition);
void feature_wiring_start(void);

#endif
