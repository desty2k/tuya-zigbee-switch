#include "gpio_probe_cluster.h"

#include "cluster_common.h"
#include "management_protocol.h"
#include "zcl_types.h"
#include <stddef.h>
#include <string.h>

static zigbee_gpio_probe_attrs_t *instance;

static uint16_t get_u16(const uint8_t *data) {
    return (uint16_t)(data[0] | ((uint16_t)data[1] << 8));
}

static uint8_t *put_u16(uint8_t *out, uint16_t value) {
    out[0] = (uint8_t)value;
    out[1] = (uint8_t)(value >> 8);
    return out + 2;
}

static uint8_t *put_u32(uint8_t *out, uint32_t value) {
    out = put_u16(out, (uint16_t)value);
    return put_u16(out, (uint16_t)(value >> 16));
}

void gpio_probe_cluster_refresh(void) {
    uint8_t  count;
    uint8_t *out;

    if (instance == NULL) {
        return;
    }
    count                = gpio_probe_service_pin_count();
    instance->state      = gpio_probe_service_state();
    instance->pins[0]    = (uint8_t)(2u * count);
    instance->classes[0] = count;
    for (uint8_t i = 0; i < count; i++) {
        put_u16(&instance->pins[1 + 2u * i], gpio_probe_service_pin(i));
        instance->classes[1 + i] = (uint8_t)gpio_probe_service_class(i);
    }
    count = gpio_probe_service_watch_count();
    out   = put_u32(&instance->watch[1], gpio_probe_service_watch_elapsed_ms());
    for (uint8_t i = 0; i < count; i++) {
        const gpio_probe_watch_result_t *result = gpio_probe_service_watch_result(i);

        out    = put_u16(out, result->pin);
        out    = put_u32(out, result->edges);
        out    = put_u32(out, result->min_interval_us);
        out    = put_u32(out, result->max_interval_us);
        *out++ = result->level;
        *out++ = result->flags;
    }
    instance->watch[0] = (uint8_t)(out - &instance->watch[1]);
}

void gpio_probe_cluster_setup(zigbee_gpio_probe_attrs_t *attrs,
                              hal_zigbee_attribute *table) {
    memset(attrs, 0, sizeof(*attrs));
    instance = attrs;
    SETUP_ATTR_FOR_TABLE(table, 0, ZCL_ATTR_DIAGNOSTICS_PROBE_STATUS,
                         ZCL_DATA_TYPE_ENUM8, HAL_ATTR_READ, attrs->status);
    SETUP_ATTR_FOR_TABLE(table, 1, ZCL_ATTR_DIAGNOSTICS_PROBE_STATE,
                         ZCL_DATA_TYPE_BITMAP8, HAL_ATTR_READ, attrs->state);
    SETUP_ATTR_FOR_TABLE(table, 2, ZCL_ATTR_DIAGNOSTICS_PROBE_PINS,
                         ZCL_DATA_TYPE_OCTET_STR, HAL_ATTR_READ, attrs->pins);
    SETUP_ATTR_FOR_TABLE(table, 3, ZCL_ATTR_DIAGNOSTICS_PROBE_LEVELS,
                         ZCL_DATA_TYPE_BITMAP32, HAL_ATTR_READ, attrs->levels);
    SETUP_ATTR_FOR_TABLE(table, 4, ZCL_ATTR_DIAGNOSTICS_PROBE_CLASSES,
                         ZCL_DATA_TYPE_OCTET_STR, HAL_ATTR_READ, attrs->classes);
    SETUP_ATTR_FOR_TABLE(table, 5, ZCL_ATTR_DIAGNOSTICS_PROBE_WATCH,
                         ZCL_DATA_TYPE_OCTET_STR, HAL_ATTR_READ, attrs->watch);
    gpio_probe_cluster_refresh();
}

static gpio_probe_status_t pull_test(const uint8_t *payload, uint16_t len,
                                     bool *malformed) {
    hal_gpio_probe_id_t pins[HAL_GPIO_PROBE_MAX_PINS];
    uint8_t             count;

    if (len < 1 || payload[0] > HAL_GPIO_PROBE_MAX_PINS ||
        len != 1u + 2u * payload[0]) {
        *malformed = true;
        return GPIO_PROBE_STATUS_RANGE;
    }
    count = payload[0];
    for (uint8_t i = 0; i < count; i++) {
        pins[i] = get_u16(&payload[1 + 2u * i]);
    }
    return gpio_probe_service_pull_test(pins, count);
}

static gpio_probe_status_t watch(const uint8_t *payload, uint16_t len,
                                 bool *malformed) {
    gpio_probe_watch_pin_t pins[GPIO_PROBE_WATCH_SLOTS];
    uint8_t count;

    if (len < 3 || len != 3u + 3u * payload[2]) {
        *malformed = true;
        return GPIO_PROBE_STATUS_RANGE;
    }
    count = payload[2];
    if (count > GPIO_PROBE_WATCH_SLOTS) {
        return GPIO_PROBE_STATUS_RESOURCE;
    }
    for (uint8_t i = 0; i < count; i++) {
        pins[i].pin  = get_u16(&payload[3 + 3u * i]);
        pins[i].pull = payload[5 + 3u * i];
    }
    return gpio_probe_service_watch(pins, count, get_u16(payload));
}

static hal_zigbee_cmd_result_t to_zcl(gpio_probe_status_t status) {
    switch (status) {
    case GPIO_PROBE_STATUS_OK:
        return HAL_ZIGBEE_CMD_PROCESSED;

    case GPIO_PROBE_STATUS_BAD_PIN:
    case GPIO_PROBE_STATUS_RANGE:
        return HAL_ZIGBEE_INVALID_VALUE;

    default:
        return HAL_ZIGBEE_ACTION_DENIED;
    }
}

hal_zigbee_cmd_result_t gpio_probe_cluster_command(
    const hal_zigbee_incoming_command_t *command) {
    const uint8_t *     payload   = command->payload;
    uint16_t            len       = command->payload_len;
    bool                malformed = false;
    gpio_probe_status_t status;

    switch (command->command_id) {
    case ZCL_CMD_DIAGNOSTICS_PROBE_STOP:
        malformed = len != 0;
        if (!malformed) {
            gpio_probe_service_stop();
        }
        status = GPIO_PROBE_STATUS_OK;
        break;

    case ZCL_CMD_DIAGNOSTICS_PROBE_SNAPSHOT:
        malformed = len != 0;
        status    = malformed ? GPIO_PROBE_STATUS_RANGE :
                    gpio_probe_service_snapshot(&instance->levels);
        break;

    case ZCL_CMD_DIAGNOSTICS_PROBE_PULL_TEST:
        status = pull_test(payload, len, &malformed);
        break;

    case ZCL_CMD_DIAGNOSTICS_PROBE_WATCH:
        status = watch(payload, len, &malformed);
        break;

    case ZCL_CMD_DIAGNOSTICS_PROBE_ARM:
        malformed = len != 2;
        status    = malformed ? GPIO_PROBE_STATUS_RANGE :
                    gpio_probe_service_arm(get_u16(payload));
        break;

    case ZCL_CMD_DIAGNOSTICS_PROBE_PULSE:
        malformed = len != 5;
        status    = malformed ? GPIO_PROBE_STATUS_RANGE :
                    gpio_probe_service_pulse(get_u16(payload), payload[2],
                                             get_u16(&payload[3]));
        break;

    default:
        return HAL_ZIGBEE_CMD_SKIPPED;
    }
    if (malformed) {
        return HAL_ZIGBEE_MALFORMED_COMMAND;
    }
    instance->status = (uint8_t)status;
    gpio_probe_cluster_refresh();
    return to_zcl(status);
}
