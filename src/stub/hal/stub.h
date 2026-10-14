#ifndef _HAL_STUB_H_
#define _HAL_STUB_H_

#include "hal/gpio.h"
#include "hal/zigbee.h"
#include <stdint.h>

// GPIO stub functions
void stub_gpio_enable_debug(int enable);
void stub_gpio_simulate_input(hal_gpio_pin_t gpio_pin, uint8_t value);
uint8_t stub_gpio_get_output(hal_gpio_pin_t gpio_pin);
uint32_t stub_gpio_init_count(void);
uint32_t stub_gpio_write_count(void);

// Tasks stub functions
void stub_tasks_poll(void);
void stub_tasks_pause(void);
void stub_tasks_resume(void);
uint32_t stub_tasks_active_count(void);

// NVM stub functions
void stub_nvm_enable_debug(int enable);
void stub_nvm_set_data_dir(const char *dir);
void stub_nvm_reject_next_writes(uint32_t count);

// Zigbee stub functions
void stub_zigbee_enable_debug(int enable);
void stub_zigbee_set_network_status(hal_zigbee_network_status_t status);
void stub_zigbee_reject_next_coordinator_sends(uint32_t count);
void stub_zigbee_reject_next_announces(uint32_t count);
void stub_zigbee_add_binding(uint16_t short_addr, uint8_t endpoint,
                             uint16_t cluster_id);
void stub_zigbee_clear_bindings(void);
hal_zigbee_endpoint *stub_zigbee_get_endpoints(uint8_t *count);
hal_zigbee_graph_usage_t stub_zigbee_get_graph_usage(void);

typedef enum {
    STUB_ZIGBEE_REPORT_CONFIG_SUCCESS,
    STUB_ZIGBEE_REPORT_CONFIG_UNSUPPORTED_ATTRIBUTE,
    STUB_ZIGBEE_REPORT_CONFIG_UNREPORTABLE_ATTRIBUTE,
} stub_zigbee_report_config_status_t;
/** Answer a Configure Reporting record the way the platform stacks do. */
stub_zigbee_report_config_status_t
stub_zigbee_configure_reporting(uint8_t endpoint, uint16_t cluster_id,
                                uint16_t attribute_id);
hal_zigbee_cmd_result_t stub_zigbee_simulate_command(uint8_t endpoint,
                                                     uint16_t cluster_id,
                                                     uint8_t command_id,
                                                     void *payload,
                                                     uint16_t payload_len);
hal_zigbee_cmd_result_t
stub_zigbee_simulate_command_without_activity(uint8_t endpoint,
                                              uint16_t cluster_id,
                                              uint8_t command_id,
                                              void *payload,
                                              uint16_t payload_len);
hal_zigbee_cmd_result_t stub_simulate_zigbee_attribute_write(
    uint8_t endpoint, uint16_t cluster_id, uint16_t attribute_id,
    const uint8_t *candidate, uint16_t candidate_len);

// ADC stub functions
void stub_set_adc_voltage_mv(uint16_t voltage_mv);
void stub_set_battery_voltage_mv(uint16_t voltage_mv);

// Millis stub functions
void stub_millis_init();
void stub_millis_freeze();
void stub_millis_unfreeze();
void stub_millis_step(uint64_t step);

#endif // _HAL_STUB_H_
