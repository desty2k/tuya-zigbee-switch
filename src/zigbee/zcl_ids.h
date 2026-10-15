#ifndef ZIGBEE_ZCL_IDS_H_
#define ZIGBEE_ZCL_IDS_H_

#define ZCL_HA_PROFILE                        0x0104u

#define ZCL_CLUSTER_BASIC                     0x0000u
#define ZCL_CLUSTER_POWER_CFG                 0x0001u
#define ZCL_CLUSTER_GROUPS                    0x0004u
#define ZCL_CLUSTER_ON_OFF                    0x0006u
#define ZCL_CLUSTER_ON_OFF_SWITCH_CONFIG      0x0007u
#define ZCL_CLUSTER_LEVEL_CONTROL             0x0008u
#define ZCL_CLUSTER_MULTISTATE_INPUT_BASIC    0x0012u
#define ZCL_CLUSTER_OTA_BOOTLOAD              0x0019u
#define ZCL_CLUSTER_POLL_CONTROL              0x0020u
#define ZCL_CLUSTER_WINDOW_COVERING           0x0102u
#define ZCL_CLUSTER_METERING                  0x0702u
#define ZCL_CLUSTER_ELECTRICAL_MEASUREMENT    0x0B04u
#define ZCL_CLUSTER_COVER_SWITCH_CONFIG       0xFC01u
#define ZCL_CLUSTER_BUTTON_EVENT              0xFC02u
#define ZCL_CLUSTER_METER_CALIBRATION         0xFC12u

/*
 * The fork-private management clusters (ZCL_CLUSTER_DEVICE_CONFIG 0xFC10,
 * ZCL_CLUSTER_DEVICE_DIAGNOSTICS 0xFC11), their attributes and the
 * manufacturer code are generated from helper_scripts/management_protocol.json,
 * which also generates the converter constants.
 */
#include "management_protocol.h"

#endif
