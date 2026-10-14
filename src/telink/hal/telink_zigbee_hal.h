#pragma once

#include "hal/zigbee.h"

#pragma pack(push, 1)
#include "tl_common.h"
#include "zb_api.h"
#include "zcl_include.h"
#pragma pack(pop)

#define OTA_QUERY_INTERVAL    15 * 60 // 15 minutes

// Network module functions (implemented in zigbee_network.c)
void telink_zigbee_hal_network_init(void);
void telink_zigbee_hal_bdb_init(af_simple_descriptor_t *endpoint_descriptor);

// ZCL module functions (implemented in zigbee_zcl.c)
/** Check a graph against the shared limits and the SDK cluster support. */
bool telink_zigbee_hal_zcl_graph_supported(hal_zigbee_endpoint *endpoints,
                                           uint8_t endpoints_cnt);

/** Register endpoints and clusters. On a refused registration it removes
 * what it registered and returns false. */
bool telink_zigbee_hal_zcl_init(hal_zigbee_endpoint *endpoints,
                                uint8_t endpoints_cnt);
af_simple_descriptor_t *telink_zigbee_hal_zcl_get_descriptors(void);

void telink_gpio_hal_setup_wake_ups();

void telink_gpio_reinit_after_deep_retention();
void telink_gpio_to_pull_for_deep_retention();
void telink_gpio_reinit_interrupts();

/** Keep hal_millis() extended across clock_time() wraps; call every loop. */
void telink_millis_poll(void);

/** Bound deep-retention sleep so hal_millis() stays extended; call before. */
void telink_millis_prepare_sleep(void);
