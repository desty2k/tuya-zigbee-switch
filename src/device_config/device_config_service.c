#include "device_config_service.h"

#include "config_nv.h"
#include "device_config/config_parser.h"
#include "device_config/feature_wiring.h"
#include "device_config/nvm_items.h"
#include "device_config/reset.h"
#include "base_components/action_mapper.h"
#include "base_components/child_lock.h"
#include "base_components/gesture_fsm.h"
#include "hal/nvm.h"
#include <string.h>

typedef struct {
    uint32_t revision;
    uint16_t config_len;
    uint8_t  led_mode;
    uint8_t  multi_press_reset;
    char     config[DEVICE_CONFIG_TEXT_CAPACITY];
} device_config_service_record_t;

static device_config_service_record_t    record;
static device_config_service_state_t     state;
static device_config_led_mode_provider_t led_mode_provider;
static void *led_mode_provider_context;

static void update_state(device_config_status_t status,
                         device_config_error_t error) {
    state.schema_version    = DEVICE_CONFIG_SCHEMA_VERSION;
    state.revision          = record.revision;
    state.config            = record.config;
    state.config_len        = record.config_len;
    state.status            = status;
    state.error             = error;
    state.led_mode          = (device_config_led_mode_t)record.led_mode;
    state.multi_press_reset = record.multi_press_reset;
    state.child_lock        = child_lock_is_on();
}

static device_config_error_t parse_error(config_parse_result_t result) {
    switch (result) {
    case CONFIG_PARSE_RESOURCE:
        return DEVICE_CONFIG_ERROR_RESOURCE;

    case CONFIG_PARSE_FEATURE_MISSING:
        return DEVICE_CONFIG_ERROR_FEATURE_MISSING;

    default:
        return DEVICE_CONFIG_ERROR_PARSE;
    }
}

void device_config_service_init(void) {
    if (hal_nvm_read(NV_ITEM_DEVICE_CONFIG_SERVICE, sizeof(record),
                     (uint8_t *)&record) != HAL_NVM_SUCCESS ||
        record.config_len >= DEVICE_CONFIG_TEXT_CAPACITY ||
        record.config[record.config_len] != '\0') {
        memset(&record, 0, sizeof(record));
        record.led_mode          = DEVICE_CONFIG_LED_NETWORK;
        record.multi_press_reset = 10;
        device_config_read_from_nv();
        strncpy(record.config, (const char *)device_config_str.data,
                sizeof(record.config) - 1);
        record.config_len = strlen(record.config);
    }
    action_mapper_set_multi_press_reset_count(record.multi_press_reset);

    uint8_t locked;

    child_lock_set(hal_nvm_read(NV_ITEM_CHILD_LOCK, sizeof(locked), &locked) ==
                   HAL_NVM_SUCCESS && locked == 1);
    update_state(DEVICE_CONFIG_STATUS_READY, DEVICE_CONFIG_ERROR_NONE);
}

const device_config_service_state_t *device_config_service_get_state(void) {
    return &state;
}

const char *device_config_service_config(void) {
    return record.config;
}

config_parse_result_t device_config_service_preflight(const char *config,
                                                      size_t config_len) {
    device_composition_t  composition;
    config_parse_result_t result;

    if (config == NULL || config_len == 0 ||
        config_len >= DEVICE_CONFIG_TEXT_CAPACITY ||
        memchr(config, '\0', config_len) != NULL) {
        return CONFIG_PARSE_SYNTAX;
    }
    char candidate[DEVICE_CONFIG_TEXT_CAPACITY];
    memcpy(candidate, config, config_len);
    candidate[config_len] = '\0';
    result = config_parser_parse(candidate, &composition);
    if (result != CONFIG_PARSE_OK) return result;

    return feature_wiring_preflight(&composition) == FEATURE_WIRING_OK ?
           CONFIG_PARSE_OK : CONFIG_PARSE_RESOURCE;
}

device_config_error_t device_config_service_apply(const char *config,
                                                  size_t config_len) {
    if (state.status == DEVICE_CONFIG_STATUS_REBOOT_PENDING) {
        return DEVICE_CONFIG_ERROR_PENDING;
    }
    config_parse_result_t parse_result =
        device_config_service_preflight(config, config_len);

    if (parse_result != CONFIG_PARSE_OK) {
        device_config_error_t error = parse_error(parse_result);
        update_state(DEVICE_CONFIG_STATUS_ERROR, error);
        return error;
    }
    if (config_len == record.config_len &&
        memcmp(config, record.config, config_len) == 0) {
        return DEVICE_CONFIG_ERROR_NONE;
    }
    if (record.revision == UINT32_MAX) {
        update_state(DEVICE_CONFIG_STATUS_ERROR, DEVICE_CONFIG_ERROR_OVERFLOW);
        return DEVICE_CONFIG_ERROR_OVERFLOW;
    }

    device_config_service_record_t candidate = record;
    memcpy(candidate.config, config, config_len);
    candidate.config[config_len] = '\0';
    candidate.config_len         = config_len;
    candidate.revision++;
    if (hal_nvm_write(NV_ITEM_DEVICE_CONFIG_SERVICE, sizeof(candidate),
                      (uint8_t *)&candidate) != HAL_NVM_SUCCESS) {
        update_state(DEVICE_CONFIG_STATUS_ERROR, DEVICE_CONFIG_ERROR_NVM);
        return DEVICE_CONFIG_ERROR_NVM;
    }
    record = candidate;
    update_state(DEVICE_CONFIG_STATUS_REBOOT_PENDING, DEVICE_CONFIG_ERROR_NONE);
    schedule_reboot(0);
    return DEVICE_CONFIG_ERROR_NONE;
}

void device_config_service_set_led_mode_provider(
    device_config_led_mode_provider_t provider, void *context) {
    led_mode_provider         = provider;
    led_mode_provider_context = context;
    if (led_mode_provider != NULL) {
        led_mode_provider(led_mode_provider_context,
                          (device_config_led_mode_t)record.led_mode);
    }
}

device_config_error_t device_config_service_set_led_mode(uint8_t mode) {
    if (mode > DEVICE_CONFIG_LED_ON) return DEVICE_CONFIG_ERROR_ARGUMENT;

    if (led_mode_provider == NULL) return DEVICE_CONFIG_ERROR_UNSUPPORTED;

    device_config_service_record_t candidate = record;
    candidate.led_mode = mode;
    if (hal_nvm_write(NV_ITEM_DEVICE_CONFIG_SERVICE, sizeof(candidate),
                      (uint8_t *)&candidate) != HAL_NVM_SUCCESS) {
        update_state(state.status, DEVICE_CONFIG_ERROR_NVM);
        return DEVICE_CONFIG_ERROR_NVM;
    }
    record = candidate;
    led_mode_provider(led_mode_provider_context,
                      (device_config_led_mode_t)record.led_mode);
    update_state(state.status, DEVICE_CONFIG_ERROR_NONE);
    return DEVICE_CONFIG_ERROR_NONE;
}

device_config_error_t device_config_service_set_multi_press_reset(uint8_t count) {
    device_config_service_record_t candidate = record;

    if (count > GESTURE_MAX_N_CLICK) return DEVICE_CONFIG_ERROR_ARGUMENT;

    candidate.multi_press_reset = count;
    if (hal_nvm_write(NV_ITEM_DEVICE_CONFIG_SERVICE, sizeof(candidate),
                      (uint8_t *)&candidate) != HAL_NVM_SUCCESS) {
        update_state(state.status, DEVICE_CONFIG_ERROR_NVM);
        return DEVICE_CONFIG_ERROR_NVM;
    }
    record = candidate;
    action_mapper_set_multi_press_reset_count(count);
    update_state(state.status, DEVICE_CONFIG_ERROR_NONE);
    return DEVICE_CONFIG_ERROR_NONE;
}

device_config_error_t device_config_service_set_child_lock(uint8_t locked) {
    if (locked > 1) return DEVICE_CONFIG_ERROR_ARGUMENT;

    if (hal_nvm_write(NV_ITEM_CHILD_LOCK, sizeof(locked), &locked) != HAL_NVM_SUCCESS) {
        update_state(state.status, DEVICE_CONFIG_ERROR_NVM);
        return DEVICE_CONFIG_ERROR_NVM;
    }
    child_lock_set(locked == 1);
    update_state(state.status, DEVICE_CONFIG_ERROR_NONE);
    return DEVICE_CONFIG_ERROR_NONE;
}

void device_config_service_set_error(device_config_error_t error) {
    update_state(DEVICE_CONFIG_STATUS_ERROR, error);
}
