#ifndef ZIGBEE_ZCL_CLUSTER_DEFS_H_
#define ZIGBEE_ZCL_CLUSTER_DEFS_H_

/* Attribute, command and value definitions owned by standard feature clusters. */
#define ZCL_ATTR_GLOBAL_CLUSTER_REVISION                               0xFFFDu

#define ZCL_ATTR_BASIC_ZCL_VER                                         0x0000u
#define ZCL_ATTR_BASIC_APP_VER                                         0x0001u
#define ZCL_ATTR_BASIC_STACK_VER                                       0x0002u
#define ZCL_ATTR_BASIC_HW_VER                                          0x0003u
#define ZCL_ATTR_BASIC_MFR_NAME                                        0x0004u
#define ZCL_ATTR_BASIC_MODEL_ID                                        0x0005u
#define ZCL_ATTR_BASIC_DATE_CODE                                       0x0006u
#define ZCL_ATTR_BASIC_POWER_SOURCE                                    0x0007u
#define ZCL_ATTR_BASIC_DEV_ENABLED                                     0x0012u
#define ZCL_ATTR_BASIC_SW_BUILD_ID                                     0x4000u

#define POWER_SOURCE_UNKNOWN                                           0x00u
#define POWER_SOURCE_MAINS_1_PHASE                                     0x01u
#define POWER_SOURCE_BATTERY                                           0x03u
#define POWER_SOURCE_DC                                                0x04u

#define ZCL_ATTR_POWER_CFG_BATTERY_VOLTAGE                             0x0020u
#define ZCL_ATTR_POWER_CFG_BATTERY_PERCENTAGE                          0x0021u

#define ZCL_ATTR_GROUP_NAME_SUPPORT                                    0x0000u

#define ZCL_ATTR_ONOFF                                                 0x0000u
#define ZCL_ATTR_START_UP_ONOFF                                        0x4003u
#define ZCL_ATTR_ONOFF_INDICATOR_MODE                                  0xFF01u
#define ZCL_ATTR_ONOFF_INDICATOR_STATE                                 0xFF02u
#define ZCL_ATTR_ONOFF_INCHING_DURATION                                0xFF03u
#define ZCL_ATTR_ONOFF_INTERLOCK_GROUP                                 0xFF04u
#define ZCL_START_UP_ONOFF_SET_ONOFF_TO_OFF                            0x00u
#define ZCL_START_UP_ONOFF_SET_ONOFF_TO_ON                             0x01u
#define ZCL_START_UP_ONOFF_SET_ONOFF_TOGGLE                            0x02u
#define ZCL_START_UP_ONOFF_SET_ONOFF_TO_PREVIOUS                       0xFFu
#define ZCL_ONOFF_INDICATOR_MODE_SAME                                  0x00u
#define ZCL_ONOFF_INDICATOR_MODE_OPPOSITE                              0x01u
#define ZCL_ONOFF_INDICATOR_MODE_MANUAL                                0x02u
#define ZCL_CMD_ONOFF_OFF                                              0x00u
#define ZCL_CMD_ONOFF_ON                                               0x01u
#define ZCL_CMD_ONOFF_TOGGLE                                           0x02u
#define ZCL_CMD_OFF_WITH_EFFECT                                        0x40u
#define ZCL_CMD_ON_WITH_RECALL_GLOBAL_SCENE                            0x41u
#define ZCL_CMD_ON_WITH_TIMED_OFF                                      0x42u

#define ZCL_ATTR_ONOFF_CONFIGURATION_SWITCH_TYPE                       0x0000u
#define ZCL_ATTR_ONOFF_CONFIGURATION_SWITCH_ACTIONS                    0x0010u
#define ZCL_ATTR_ONOFF_CONFIGURATION_SWITCH_MODE                       0xFF00u
#define ZCL_ATTR_ONOFF_CONFIGURATION_SWITCH_RELAY_MODE                 0xFF01u
#define ZCL_ATTR_ONOFF_CONFIGURATION_SWITCH_RELAY_INDEX                0xFF02u
#define ZCL_ATTR_ONOFF_CONFIGURATION_SWITCH_LONG_PRESS_DUR             0xFF03u
#define ZCL_ATTR_ONOFF_CONFIGURATION_SWITCH_LEVEL_MOVE_RATE            0xFF04u
#define ZCL_ATTR_ONOFF_CONFIGURATION_SWITCH_BINDING_MODE               0xFF05u
#define ZCL_ONOFF_CONFIGURATION_SWITCH_TYPE_TOGGLE                     0x00u
#define ZCL_ONOFF_CONFIGURATION_SWITCH_TYPE_MOMENTARY                  0x01u
#define ZCL_ONOFF_CONFIGURATION_SWITCH_TYPE_MOMENTARY_NC               0x02u
#define ZCL_ONOFF_CONFIGURATION_SWITCH_ACTION_ONOFF                    0x00u
#define ZCL_ONOFF_CONFIGURATION_SWITCH_ACTION_OFFON                    0x01u
#define ZCL_ONOFF_CONFIGURATION_SWITCH_ACTION_TOGGLE_SIMPLE            0x02u
#define ZCL_ONOFF_CONFIGURATION_SWITCH_ACTION_TOGGLE_SMART_SYNC        0x03u
#define ZCL_ONOFF_CONFIGURATION_SWITCH_ACTION_TOGGLE_SMART_OPPOSITE    0x04u
#define ZCL_ONOFF_CONFIGURATION_RELAY_MODE_DETACHED                    0x00u
#define ZCL_ONOFF_CONFIGURATION_RELAY_MODE_RISE                        0x01u
#define ZCL_ONOFF_CONFIGURATION_RELAY_MODE_LONG                        0x02u
#define ZCL_ONOFF_CONFIGURATION_RELAY_MODE_SHORT                       0x03u
#define ZCL_ONOFF_CONFIGURATION_RELAY_MODE_DEFERRED_SINGLE             0x04u
#define ZCL_ONOFF_CONFIGURATION_BINDED_MODE_RISE                       0x01u
#define ZCL_ONOFF_CONFIGURATION_BINDED_MODE_LONG                       0x02u
#define ZCL_ONOFF_CONFIGURATION_BINDED_MODE_SHORT                      0x03u

#define ZCL_ATTR_MULTISTATE_INPUT_NUMBER_OF_STATES                     0x004Au
#define ZCL_ATTR_MULTISTATE_INPUT_OUT_OF_SERVICE                       0x0051u
#define ZCL_ATTR_MULTISTATE_INPUT_PRESENT_VALUE                        0x0055u
#define ZCL_ATTR_MULTISTATE_INPUT_STATUS_FLAGS                         0x006Fu

#define ZCL_ATTR_WINDOW_COVERING_TYPE                                  0x0000u
#define ZCL_ATTR_WINDOW_COVERING_CURRENT_POSITION_LIFT_PERCENTAGE      0x0008u
#define ZCL_ATTR_WINDOW_COVERING_MOVING                                0xFF00u
#define ZCL_ATTR_WINDOW_COVERING_MOTOR_REVERSAL                        0xFF01u
#define ZCL_ATTR_WINDOW_COVERING_OPEN_TIME                             0xFF02u
#define ZCL_ATTR_WINDOW_COVERING_CLOSE_TIME                            0xFF03u
#define ZCL_ATTR_WINDOW_COVERING_CLOSED_DEADZONE                       0xFF04u
#define ZCL_ATTR_WINDOW_COVERING_OPEN_DEADZONE                         0xFF05u
#define ZCL_ATTR_WINDOW_COVERING_OVERRUN                               0xFF06u
#define ZCL_ATTR_WINDOW_COVERING_MOVING_STOPPED                        0x00u
#define ZCL_ATTR_WINDOW_COVERING_MOVING_OPENING                        0x01u
#define ZCL_ATTR_WINDOW_COVERING_MOVING_CLOSING                        0x02u
#define ZCL_CMD_WINDOW_COVERING_UP_OPEN                                0x00u
#define ZCL_CMD_WINDOW_COVERING_DOWN_CLOSE                             0x01u
#define ZCL_CMD_WINDOW_COVERING_STOP                                   0x02u
#define ZCL_CMD_WINDOW_COVERING_GOTO_LIFT_PERCENTAGE                   0x05u

#define ZCL_ATTR_COVER_SWITCH_CONFIG_SWITCH_TYPE                       0x0000u
#define ZCL_ATTR_COVER_SWITCH_CONFIG_COVER_INDEX                       0x0001u
#define ZCL_ATTR_COVER_SWITCH_CONFIG_REVERSAL                          0x0002u
#define ZCL_ATTR_COVER_SWITCH_CONFIG_LOCAL_MODE                        0x0003u
#define ZCL_ATTR_COVER_SWITCH_CONFIG_BINDED_MODE                       0x0004u
#define ZCL_ATTR_COVER_SWITCH_CONFIG_LONG_PRESS_DUR                    0x0005u
#define ZCL_COVER_SWITCH_TYPE_TOGGLE                                   0x00u
#define ZCL_COVER_SWITCH_TYPE_MOMENTARY                                0x01u
#define ZCL_COVER_SWITCH_MODE_IMMEDIATE                                0x00u
#define ZCL_COVER_SWITCH_MODE_SHORT_PRESS                              0x01u
#define ZCL_COVER_SWITCH_MODE_LONG_PRESS                               0x02u
#define ZCL_COVER_SWITCH_MODE_HYBRID                                   0x03u

#define ZCL_ATTR_POLL_CTRL_CHECK_IN_INTERVAL                           0x0000u
#define ZCL_ATTR_POLL_CTRL_LONG_POLL_INTERVAL                          0x0001u
#define ZCL_ATTR_POLL_CTRL_SHORT_POLL_INTERVAL                         0x0002u
#define ZCL_ATTR_POLL_CTRL_FAST_POLL_TIMEOUT                           0x0003u
#define ZCL_CMD_POLL_CTRL_CHECK_IN_RSP                                 0x00u
#define ZCL_CMD_POLL_CTRL_FAST_POLL_STOP                               0x01u
#define ZCL_CMD_POLL_CTRL_SET_LONG_POLL_INTERVAL                       0x02u
#define ZCL_CMD_POLL_CTRL_SET_SHORT_POLL_INTERVAL                      0x03u
#define ZCL_CMD_POLL_CTRL_CHECK_IN                                     0x00u

#define ZCL_ATTR_BUTTON_EVENT_STATE                                    0x0000u
#define ZCL_ATTR_BUTTON_EVENT_LAST_EVENT_SEQ                           0x0001u
#define ZCL_ATTR_BUTTON_EVENT_MULTI_CLICK_GAP                          0x0002u
#define ZCL_ATTR_BUTTON_EVENT_DEBOUNCE_MS                              0x0003u
#define ZCL_CMD_BUTTON_EVENT                                           0x00u

#define ZCL_CMD_LEVEL_MOVE_TO_LEVEL_WITH_ON_OFF                        0x04u
#define ZCL_CMD_LEVEL_MOVE_WITH_ON_OFF                                 0x05u
#define ZCL_CMD_LEVEL_STOP_WITH_ON_OFF                                 0x07u
#define ZCL_LEVEL_MOVE_UP                                              0x00u
#define ZCL_LEVEL_MOVE_DOWN                                            0x01u

#endif
