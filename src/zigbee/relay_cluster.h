#ifndef _RELAY_CLUSTER_H_
#define _RELAY_CLUSTER_H_

#include "base_components/indicator_feedback.h"
#include "base_components/relay_controller.h"
#include <stdint.h>

#include "hal/zigbee.h"

typedef struct {
    uint8_t               relay_idx;
    uint8_t               endpoint;
    uint8_t               startup_mode;
    uint8_t               indicator_led_mode;
    hal_zigbee_attribute  attr_infos[6];
    uint8_t               relay_id;
    uint8_t               on_off;
    indicator_feedback_t *indicator_led;
    uint8_t               indicator_state;
    uint16_t              inching_ms;
    uint8_t               interlock_group;

    /* Group the device config string assigns to this relay (a `K` group or
     * the implicit group of a cover); it cannot be changed over Zigbee. */
    uint8_t               config_interlock_group;
} zigbee_relay_cluster;

/* Number of `K` groups, numbered 1..count. A relay the config leaves
 * ungrouped may join one of them through the InterlockGroup attribute. */
extern uint8_t relay_cluster_config_interlock_count;

void relay_cluster_add_to_endpoint(zigbee_relay_cluster *cluster,
                                   hal_zigbee_endpoint *endpoint);

void update_relay_clusters();

#endif
