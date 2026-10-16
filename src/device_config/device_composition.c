#include "device_config/device_composition.h"
#include "device_config/build_features.h"

#include "hal/gpio_counter.h"

#include <stddef.h>

static bool count_is_within_bounds(const device_composition_t *composition) {
    return composition->relay_count <= DEVICE_COMPOSITION_MAX_RELAYS &&
           composition->relay_endpoint_count <= DEVICE_COMPOSITION_MAX_RELAYS &&
           composition->button_count <= DEVICE_COMPOSITION_MAX_BUTTONS &&
           composition->indicator_count <= DEVICE_COMPOSITION_MAX_INDICATORS &&
           composition->interlock_count <= DEVICE_COMPOSITION_MAX_INTERLOCKS &&
           composition->cover_count <= DEVICE_COMPOSITION_MAX_COVERS &&
           composition->meter_count <= DEVICE_COMPOSITION_MAX_METERS &&
           composition->switch_count <= DEVICE_COMPOSITION_MAX_SWITCHES &&
           composition->cover_switch_count <= DEVICE_COMPOSITION_MAX_COVER_SWITCHES;
}

enum {
    DEVICE_COMPOSITION_MAX_PINS =
        DEVICE_COMPOSITION_MAX_BUTTONS + DEVICE_COMPOSITION_MAX_RELAYS * 2 +
        DEVICE_COMPOSITION_MAX_INDICATORS + 1 +
        DEVICE_COMPOSITION_MAX_METERS * 3,
};

static bool pin_is_listed(hal_gpio_pin_t pin, const hal_gpio_pin_t *pins,
                          uint8_t count) {
    for (uint8_t i = 0; i < count; i++) {
        if (pins[i] == pin) {
            return true;
        }
    }
    return false;
}

static bool pin_is_reused(hal_gpio_pin_t pin, uint8_t *used_count,
                          hal_gpio_pin_t used_pins[DEVICE_COMPOSITION_MAX_PINS]) {
    if (pin_is_listed(pin, used_pins, *used_count)) {
        return true;
    }
    used_pins[*used_count] = pin;
    (*used_count)++;
    return false;
}

#if FEATURE_METER

static bool meter_uses_pin(const device_meter_config_t *meter, hal_gpio_pin_t pin) {
    return pin == meter->cf_pin || pin == meter->cf1_pin || pin == meter->sel_pin;
}

/* Indicator and lock LED pins stay out of the reuse check above; a meter pin
 * must not be one. */
static bool meter_pin_drives_indicator(const device_composition_t *composition,
                                       const device_meter_config_t *meter) {
    for (uint8_t i = 0; i < composition->indicator_count; i++) {
        if (meter_uses_pin(meter, composition->indicators[i].pin)) {
            return true;
        }
    }
    return composition->lock_led_pin != HAL_INVALID_PIN &&
           meter_uses_pin(meter, composition->lock_led_pin);
}

#endif

static uint8_t endpoint_count(const device_composition_t *composition) {
    uint8_t count = (uint8_t)(composition->switch_count +
                              composition->relay_endpoint_count +
                              composition->cover_switch_count +
                              composition->cover_count);

    return count == 0 ? 1 : count;
}

config_parse_result_t device_composition_validate(
    const device_composition_t *composition) {
    hal_gpio_pin_t used_pins[DEVICE_COMPOSITION_MAX_PINS];
    uint8_t        used_count = 0;
    uint8_t        button_pin_count;

    if (composition == NULL || !count_is_within_bounds(composition)) {
        return CONFIG_PARSE_LIMIT;
    }
    if (composition->endpoint_count != endpoint_count(composition) ||
        composition->endpoint_count > DEVICE_COMPOSITION_MAX_ENDPOINTS ||
        composition->meter_count > composition->endpoint_count) {
        return CONFIG_PARSE_RESOURCE;
    }
    for (uint8_t i = 0; i < composition->button_count; i++) {
        if (pin_is_reused(composition->buttons[i].pin, &used_count, used_pins)) {
            return CONFIG_PARSE_PIN_CONFLICT;
        }
    }
    button_pin_count = used_count;
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

    /* Battery remotes sample VBAT through one of their button pins: the
     * battery pin may be a button pin, never an output. */
    if (composition->battery_pin != HAL_INVALID_PIN &&
        !pin_is_listed(composition->battery_pin, used_pins, button_pin_count) &&
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
            composition->cover_switches[i].close_button_id ||
            (composition->cover_switches[i].stop_button_id != DEVICE_COMPOSITION_NO_BUTTON &&
             composition->cover_switches[i].stop_button_id >= composition->button_count)) {
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
#if FEATURE_METER
    {
        uint8_t counter_slot = 0;

        for (uint8_t i = 0; i < composition->meter_count; i++) {
            const device_meter_config_t *meter      = &composition->meters[i];
            const hal_gpio_pin_t         counted[2] = { meter->cf_pin, meter->cf1_pin };

            if (meter->ic != DEVICE_METER_BL0937 || meter->sel_pin == HAL_INVALID_PIN) {
                return CONFIG_PARSE_REFERENCE;
            }
            for (uint8_t j = 0; j < 2; j++) {
                if (counter_slot >= hal_gpio_counter_capacity() ||
                    counted[j] == HAL_INVALID_PIN ||
                    !hal_gpio_counter_can_route(counter_slot, counted[j])) {
                    return CONFIG_PARSE_RESOURCE;
                }
                counter_slot++;
            }
            if (pin_is_reused(meter->cf_pin, &used_count, used_pins) ||
                pin_is_reused(meter->cf1_pin, &used_count, used_pins) ||
                pin_is_reused(meter->sel_pin, &used_count, used_pins) ||
                meter_pin_drives_indicator(composition, meter)) {
                return CONFIG_PARSE_PIN_CONFLICT;
            }
        }
    }
#else
    if (composition->meter_count != 0) {
        return CONFIG_PARSE_FEATURE_MISSING;
    }
#endif
    return CONFIG_PARSE_OK;
}

/* FNV-1a style mixing of whole fields, so struct padding never enters the
 * hash. */
static uint32_t topology_mix(uint32_t hash, uint32_t value) {
    return (hash ^ value) * 16777619u;
}

uint32_t device_composition_relay_topology(
    const device_composition_t *composition) {
    uint32_t hash = 2166136261u;

    hash = topology_mix(hash, composition->relay_endpoint_count);
    for (uint8_t i = 0; i < composition->relay_endpoint_count; i++) {
        hash = topology_mix(hash, composition->relay_endpoint_ids[i]);
    }
    hash = topology_mix(hash, composition->interlock_count);
    for (uint8_t i = 0; i < composition->interlock_count; i++) {
        hash = topology_mix(hash, composition->interlocks[i].relay_mask);
    }
    return hash;
}
