#ifndef DEVICE_CONFIG_DEVICE_CONFIG_SERVICE_H_
#define DEVICE_CONFIG_DEVICE_CONFIG_SERVICE_H_

#include "device_config/device_composition.h"
#include "zigbee/management_protocol.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define DEVICE_CONFIG_TEXT_CAPACITY     128
#define DEVICE_CONFIG_SCHEMA_VERSION    1

typedef void (*device_config_led_mode_provider_t)(
    void *context, device_config_led_mode_t mode);

typedef struct {
    uint16_t                 schema_version;
    uint32_t                 revision;
    const char *             config;
    uint16_t                 config_len;
    device_config_status_t   status;
    device_config_error_t    error;
    device_config_led_mode_t led_mode;
    uint8_t                  multi_press_reset;
} device_config_service_state_t;

void device_config_service_init(void);
const device_config_service_state_t *device_config_service_get_state(void);
const char *device_config_service_config(void);
config_parse_result_t device_config_service_preflight(const char *config,
                                                      size_t config_len);
device_config_error_t device_config_service_apply(const char *config,
                                                  size_t config_len);
void device_config_service_set_led_mode_provider(
    device_config_led_mode_provider_t provider, void *context);
device_config_error_t device_config_service_set_led_mode(uint8_t mode);
device_config_error_t device_config_service_set_multi_press_reset(uint8_t count);
void device_config_service_set_error(device_config_error_t error);

#endif
