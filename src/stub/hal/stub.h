#ifndef _HAL_STUB_H_
#define _HAL_STUB_H_

#include "hal/gpio.h"
#include "hal/zigbee.h"
#include <stdint.h>

// GPIO stub functions

/* Pin ids are below this bound: port Z (25) << 8 | 1 << 7 is the largest. */
#define STUB_GPIO_PIN_LIMIT    0x1A00

void stub_gpio_enable_debug(int enable);
void stub_gpio_simulate_input(hal_gpio_pin_t gpio_pin, uint8_t value);

/** Toggle an input count times at one instant, before any worker can run. */
void stub_gpio_burst(hal_gpio_pin_t gpio_pin, uint32_t count);

/** Change an input, stall lag_ms, then capture the change stamped late. */
void stub_gpio_simulate_late_input(hal_gpio_pin_t gpio_pin, uint8_t value,
                                   uint32_t lag_ms);
uint8_t stub_gpio_get_output(hal_gpio_pin_t gpio_pin);
uint32_t stub_gpio_init_count(void);
uint32_t stub_gpio_write_count(void);

// Tasks stub functions
void stub_tasks_poll(void);
void stub_tasks_pause(void);
void stub_tasks_resume(void);
uint32_t stub_tasks_active_count(void);

/** Make the next count hal_tasks_schedule() calls fail like a full pool. */
void stub_tasks_reject_next(uint32_t count);

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

/** Make the next `count` hal_zigbee_init calls fail the way a refused SDK
 * registration does on Telink. */
void stub_zigbee_reject_next_inits(uint32_t count);

typedef enum {
    STUB_ZIGBEE_REPORT_CONFIG_SUCCESS,
    STUB_ZIGBEE_REPORT_CONFIG_UNSUPPORTED_ATTRIBUTE,
} stub_zigbee_report_config_status_t;
/** Answer a Configure Reporting record the way the Telink stack does. */
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

/** Write one attribute as a Write Attributes record. `candidate` holds
 * `candidate_len` bytes of ZCL encoding; strings take their length prefix. */
hal_zigbee_cmd_result_t stub_simulate_zigbee_attribute_write(
    uint8_t endpoint, uint16_t cluster_id, uint16_t attribute_id,
    const uint8_t *candidate, uint16_t candidate_len);

#define STUB_ZIGBEE_MAX_WRITE_RECORDS    8
/** Write Attributes Undivided: all records are validated before any is written. */
hal_zigbee_cmd_result_t stub_simulate_zigbee_attribute_write_undivided(
    uint8_t endpoint, uint16_t cluster_id, uint8_t count,
    const uint16_t *attribute_ids, const uint8_t *const *candidates,
    const uint16_t *candidate_lens);

/** A received ZCL foundation command payload, handled the way the patched
 * Telink zcl_foundationCmdHandler() handles it: the frame check of
 * zigbee/zcl_frame_check.h runs before any record is parsed. Writes, Write
 * Undivided, Configure Reporting and Read Reporting Configuration are
 * simulated; other commands answer HAL_ZIGBEE_CMD_SKIPPED. */
hal_zigbee_cmd_result_t stub_zigbee_foundation_frame(uint8_t endpoint,
                                                     uint16_t cluster_id,
                                                     uint8_t command_id,
                                                     const uint8_t *payload,
                                                     uint16_t payload_len);

// ADC stub functions
void stub_set_adc_voltage_mv(uint16_t voltage_mv);
void stub_set_battery_voltage_mv(uint16_t voltage_mv);

// Millis stub functions
typedef enum {
    STUB_TIME_MODEL_NATIVE = 0,
    STUB_TIME_MODEL_TELINK = 1,
} stub_time_model_t;

void stub_millis_init();
void stub_millis_freeze();
void stub_millis_unfreeze();
void stub_millis_step(uint64_t step);

/** Monotonic 64-bit virtual time in ms; never wraps. */
uint64_t stub_millis_virtual(void);

/** Jump frozen virtual time forward to virtual_ms; -1 if not frozen or backwards. */
int stub_millis_set(uint64_t virtual_ms);
void stub_millis_set_model(stub_time_model_t model);
stub_time_model_t stub_millis_model(void);

#endif // _HAL_STUB_H_
