#include "device_config/device_composition.h"

#include <stddef.h>

static bool count_is_within_bounds(const device_composition_t *composition) {
    return composition->relay_count <= DEVICE_COMPOSITION_MAX_RELAYS &&
           composition->relay_endpoint_count <= DEVICE_COMPOSITION_MAX_RELAYS &&
           composition->button_count <= DEVICE_COMPOSITION_MAX_BUTTONS &&
           composition->indicator_count <= DEVICE_COMPOSITION_MAX_INDICATORS &&
           composition->interlock_count <= DEVICE_COMPOSITION_MAX_INTERLOCKS &&
           composition->cover_count <= DEVICE_COMPOSITION_MAX_COVERS &&
           composition->switch_count <= DEVICE_COMPOSITION_MAX_SWITCHES &&
           composition->cover_switch_count <= DEVICE_COMPOSITION_MAX_COVER_SWITCHES;
}

static bool pin_is_reused(hal_gpio_pin_t pin, uint8_t *used_count,
                          hal_gpio_pin_t used_pins[37]) {
    for (uint8_t i = 0; i < *used_count; i++) {
        if (used_pins[i] == pin) {
            return true;
        }
    }
    used_pins[*used_count] = pin;
    (*used_count)++;
    return false;
}

static uint8_t endpoint_count(const device_composition_t *composition) {
    uint8_t count = (uint8_t)(composition->switch_count +
                              composition->relay_endpoint_count +
                              composition->cover_switch_count +
                              composition->cover_count);

    return count == 0 ? 1 : count;
}

static uint8_t cluster_count(const device_composition_t *composition) {
    uint8_t count = (uint8_t)(2 + (composition->battery_pin != HAL_INVALID_PIN));

#ifdef END_DEVICE
    count++;
#endif
    return (uint8_t)(count + composition->switch_count * 5 +
                     composition->relay_endpoint_count * 3 +
                     composition->cover_switch_count * 4 +
                     composition->cover_count);
}

config_parse_result_t device_composition_validate(
    const device_composition_t *composition) {
    hal_gpio_pin_t used_pins[37];
    uint8_t        used_count = 0;

    if (composition == NULL || !count_is_within_bounds(composition)) {
        return CONFIG_PARSE_LIMIT;
    }
    if (composition->endpoint_count != endpoint_count(composition) ||
        composition->endpoint_count > DEVICE_COMPOSITION_MAX_ENDPOINTS ||
        cluster_count(composition) > DEVICE_COMPOSITION_MAX_CLUSTERS) {
        return CONFIG_PARSE_RESOURCE;
    }
    for (uint8_t i = 0; i < composition->button_count; i++) {
        if (pin_is_reused(composition->buttons[i].pin, &used_count, used_pins)) {
            return CONFIG_PARSE_PIN_CONFLICT;
        }
    }
    for (uint8_t i = 0; i < composition->relay_count; i++) {
        if (pin_is_reused(composition->relays[i].on_pin, &used_count, used_pins) ||
            (composition->relays[i].is_latching &&
             pin_is_reused(composition->relays[i].off_pin, &used_count, used_pins))) {
            return CONFIG_PARSE_PIN_CONFLICT;
        }
    }
    for (uint8_t i = 0; i < composition->indicator_count; i++) {
        if (composition->indicators[i].pin == HAL_INVALID_PIN) {
            return CONFIG_PARSE_REFERENCE;
        }
    }
    if (composition->battery_pin != HAL_INVALID_PIN &&
        pin_is_reused(composition->battery_pin, &used_count, used_pins)) {
        return CONFIG_PARSE_PIN_CONFLICT;
    }
    for (uint8_t i = 0; i < composition->relay_endpoint_count; i++) {
        if (composition->relay_endpoint_ids[i] >= composition->relay_count) {
            return CONFIG_PARSE_REFERENCE;
        }
        for (uint8_t j = 0; j < i; j++) {
            if (composition->relay_endpoint_ids[i] ==
                composition->relay_endpoint_ids[j]) {
                return CONFIG_PARSE_REFERENCE;
            }
        }
    }
    for (uint8_t i = 0; i < composition->switch_count; i++) {
        if (composition->switches[i].button_id >= composition->button_count ||
            composition->switches[i].relay_id >= DEVICE_COMPOSITION_MAX_RELAYS) {
            return CONFIG_PARSE_REFERENCE;
        }
    }
    for (uint8_t i = 0; i < composition->cover_switch_count; i++) {
        if (composition->cover_switches[i].open_button_id >= composition->button_count ||
            composition->cover_switches[i].close_button_id >= composition->button_count ||
            composition->cover_switches[i].open_button_id ==
            composition->cover_switches[i].close_button_id) {
            return CONFIG_PARSE_REFERENCE;
        }
    }
    for (uint8_t i = 0; i < composition->interlock_count; i++) {
        if ((composition->interlocks[i].relay_mask >> composition->relay_count) != 0) {
            return CONFIG_PARSE_REFERENCE;
        }
    }
    for (uint8_t i = 0; i < composition->cover_count; i++) {
        const device_cover_config_t *cover = &composition->covers[i];

        if (cover->open_relay_id >= composition->relay_count ||
            cover->close_relay_id >= composition->relay_count ||
            cover->open_relay_id == cover->close_relay_id) {
            return CONFIG_PARSE_REFERENCE;
        }
        for (uint8_t j = 0; j < i; j++) {
            const device_cover_config_t *other = &composition->covers[j];

            if (cover->open_relay_id == other->open_relay_id ||
                cover->open_relay_id == other->close_relay_id ||
                cover->close_relay_id == other->open_relay_id ||
                cover->close_relay_id == other->close_relay_id) {
                return CONFIG_PARSE_REFERENCE;
            }
        }
    }
    return CONFIG_PARSE_OK;
}
