#ifndef _HAL_ZIGBEE_LIMITS_H_
#define _HAL_ZIGBEE_LIMITS_H_

/*
 * Descriptor capacities shared by every platform. One device config must
 * produce the same Zigbee graph on Telink and Silabs, so each value is the
 * smallest capacity of the platforms. Composition preflight, the stub and the
 * platform HALs all check graphs against these values. The Telink SDK
 * configuration (stack_cfg.h) includes this header, so it holds macros only.
 */

/* Application endpoints. Telink AF table holds MAX_ACTIVE_EP_NUMBER (8);
 * Silabs has 12 fixed endpoints. */
#define HAL_ZIGBEE_MAX_ENDPOINTS            8

/* Server (input) and client (output) clusters across all endpoints. Telink
 * keeps one fixed list per direction. */
#define HAL_ZIGBEE_MAX_SERVER_CLUSTERS      32
#define HAL_ZIGBEE_MAX_CLIENT_CLUSTERS      32

/* All clusters across all endpoints. Silabs cluster metadata buffer. */
#define HAL_ZIGBEE_MAX_CLUSTERS             48

/* Attributes across all clusters. Telink and Silabs attribute tables. */
#define HAL_ZIGBEE_MAX_ATTRIBUTES           160

/* Telink ZCL cluster table entries (ZCL_CLUSTER_NUM_MAX). Every cluster of
 * every role takes one entry; the OTA client takes one entry for the whole
 * device. Each entry costs 18 bytes of RAM. */
#define HAL_ZIGBEE_MAX_ZCL_REGISTRATIONS    48

#endif
