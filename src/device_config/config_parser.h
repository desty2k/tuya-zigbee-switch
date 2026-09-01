#ifndef _DEVICE_INIT_H_
#define _DEVICE_INIT_H_

#include "base_components/network_indicator.h"
#include "base_components/battery.h"
#include "hal/zigbee.h"
#include "device_config/device_composition.h"

#include "config_nv.h"

extern network_indicator_t network_indicator;

extern hal_zigbee_endpoint endpoints[10];

extern uint8_t allow_simultaneous_latching_pulses;

extern battery_t battery;

void parse_config();
config_parse_result_t config_parser_parse(const char *text,
                                          device_composition_t *out);
void init_reporting();
void handle_version_changes();

#endif
