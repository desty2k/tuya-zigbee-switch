#ifndef _COVER_CLUSTER_H_
#define _COVER_CLUSTER_H_

#include "base_components/relay_controller.h"
#include "base_components/timer_service.h"
#include "hal/zigbee.h"
#include <stdint.h>

typedef struct {
    uint8_t motor_reversal;
} zigbee_cover_cluster_config;

typedef struct {
    // Parameters
    uint8_t              cover_idx;
    uint8_t              endpoint;
    uint8_t              open_relay_id;
    uint8_t              close_relay_id;

    // Attributes
    uint8_t              moving;
    uint8_t              motor_reversal;
    hal_zigbee_attribute attr_infos[4];

    // State
    uint32_t             last_switch_time;
    uint8_t              has_pending_movement;
    uint8_t              pending_movement;
    app_timer_t          movement_timer;
} zigbee_cover_cluster;

void cover_cluster_add_to_endpoint(zigbee_cover_cluster *cluster, hal_zigbee_endpoint *endpoint);

void cover_open(zigbee_cover_cluster *cluster);
void cover_close(zigbee_cover_cluster *cluster);
void cover_stop(zigbee_cover_cluster *cluster);

void cover_cluster_callback_attr_write_trampoline(uint8_t endpoint, uint16_t attribute_id);

#endif
