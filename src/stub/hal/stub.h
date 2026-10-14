#ifndef _HAL_STUB_H_
#define _HAL_STUB_H_

#include "hal/gpio.h"
#include "hal/zigbee.h"
#include <stdint.h>

// GPIO stub functions
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
