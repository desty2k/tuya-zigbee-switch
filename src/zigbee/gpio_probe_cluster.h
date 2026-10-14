#ifndef ZIGBEE_GPIO_PROBE_CLUSTER_H_
#define ZIGBEE_GPIO_PROBE_CLUSTER_H_

#include "base_components/gpio_probe_service.h"
#include "hal/zigbee.h"

/*
 * GPIO probe commands and results on the Device Diagnostics cluster
 * (0xFC11, fork manufacturer code); the protocol is defined in
 * helper_scripts/management_protocol.json. Built only with the `probe`
 * feature.
 */
#define GPIO_PROBE_CLUSTER_ATTRIBUTE_COUNT    6

typedef struct {
    uint8_t  status; // gpio_probe_status_t of the latest command
    uint8_t  state;  // GPIO_PROBE_STATE_* bits
    uint8_t  pins[1 + 2 * HAL_GPIO_PROBE_MAX_PINS];
    uint32_t levels;
    uint8_t  classes[1 + HAL_GPIO_PROBE_MAX_PINS];
    uint8_t  watch[1 + GPIO_PROBE_WATCH_HEADER_SIZE +
                   GPIO_PROBE_WATCH_RECORD_SIZE * GPIO_PROBE_WATCH_SLOTS];
} zigbee_gpio_probe_attrs_t;

/** Fill GPIO_PROBE_CLUSTER_ATTRIBUTE_COUNT attribute entries. */
void gpio_probe_cluster_setup(zigbee_gpio_probe_attrs_t *attrs,
                              hal_zigbee_attribute *table);
void gpio_probe_cluster_refresh(void);
hal_zigbee_cmd_result_t gpio_probe_cluster_command(
    const hal_zigbee_incoming_command_t *command);

#endif
