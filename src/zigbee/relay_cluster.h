#ifndef _RELAY_CLUSTER_H_
#define _RELAY_CLUSTER_H_

#include "base_components/led.h"
#include "base_components/relay_controller.h"
#include <stdint.h>

#include "hal/zigbee.h"

typedef struct {
    uint8_t              relay_idx;
    uint8_t              endpoint;
    uint8_t              startup_mode;
    uint8_t              indicator_led_mode;
    hal_zigbee_attribute attr_infos[4];
    uint8_t              relay_id;
    uint8_t              on_off;
    led_t *              indicator_led;
    uint8_t              indicator_state;
} zigbee_relay_cluster;

void relay_cluster_add_to_endpoint(zigbee_relay_cluster *cluster,
                                   hal_zigbee_endpoint *endpoint);

void update_relay_clusters();

void relay_cluster_callback_attr_write_trampoline(uint8_t endpoint,
                                                  uint16_t attribute_id);

#endif
