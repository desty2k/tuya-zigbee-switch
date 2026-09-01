#include "device_config/config_parser.h"

#include <stddef.h>
#include <string.h>

#define CONFIG_DEFAULT_DEBOUNCE_MS           8
#define CONFIG_DEFAULT_MULTI_CLICK_GAP_MS    350

static bool parse_pin(const char *text, const char **end, hal_gpio_pin_t *pin) {
    uint16_t number = 0;
    char     port;

    if (text == NULL || text[0] < 'A' || text[0] > 'Z' ||
        text[1] < '0' || text[1] > '9') {
        return false;
    }
    port = *text++;
    while (*text >= '0' && *text <= '9') {
        number = (uint16_t)(number * 10U + (uint16_t)(*text++ - '0'));
    }
    if (number > 15) {
        return false;
    }
    *pin = (hal_gpio_pin_t)(((uint16_t)(port - 'A') << 4) | number);
    *end = text;
    return true;
}

static bool parse_pull(const char *text, hal_gpio_pull_t *pull) {
    if (*text == '\0' || (*text == 'f' && text[1] == '\0')) {
        *pull = HAL_GPIO_PULL_NONE;
    } else if (*text == 'u' && text[1] == '\0') {
        *pull = HAL_GPIO_PULL_UP;
    } else if (*text == 'd' && text[1] == '\0') {
        *pull = HAL_GPIO_PULL_DOWN;
    } else {
        return false;
    }
    return true;
}

static bool parse_uint16(const char *text, uint16_t *value) {
    uint32_t parsed = 0;

    if (*text < '0' || *text > '9') {
        return false;
    }
    while (*text >= '0' && *text <= '9') {
        parsed = parsed * 10U + (uint32_t)(*text++ - '0');
        if (parsed > UINT16_MAX) {
            return false;
        }
    }
    if (*text != '\0') {
        return false;
    }
    *value = (uint16_t)parsed;
    return true;
}

static bool parse_hex_mask(const char *text, uint16_t *mask,
                           const char **end) {
    uint16_t parsed    = 0;
    bool     has_digit = false;

    while ((*text >= '0' && *text <= '9') ||
           (*text >= 'a' && *text <= 'f') ||
           (*text >= 'A' && *text <= 'F')) {
        uint8_t digit = *text <= '9' ? (uint8_t)(*text - '0') :
                        (uint8_t)((*text | 0x20) - 'a' + 10);

        parsed    = (uint16_t)((parsed << 4) | digit);
        has_digit = true;
        text++;
    }
    *mask = parsed;
    *end  = text;
    return has_digit;
}

static bool parse_button_token(const char *token, device_composition_t *out,
                               uint16_t debounce_ms, uint16_t gap_ms) {
    const char *            end;
    hal_gpio_pull_t         pull;
    device_button_config_t *button;

    if (out->button_count >= DEVICE_COMPOSITION_MAX_BUTTONS ||
        !parse_pin(token + 1, &end, &out->buttons[out->button_count].pin) ||
        !parse_pull(end, &pull)) {
        return false;
    }
    button  = &out->buttons[out->button_count];
    *button = (device_button_config_t){
        .pin                = button->pin,
        .pull               = pull,
        .debounce_ms        = debounce_ms,
        .hold_ms            = token[0] == 'S' ? 800 : 2000,
        .multi_click_gap_ms = gap_ms,
        .role               = token[0] == 'S' ? DEVICE_BUTTON_SWITCH : DEVICE_BUTTON_ONBOARD,
    };
    if (token[0] == 'S') {
        if (out->switch_count >= DEVICE_COMPOSITION_MAX_SWITCHES) {
            return false;
        }
        out->switches[out->switch_count] = (device_switch_config_t){
            .button_id = out->button_count,
            .relay_id  = out->switch_count,
        };
        out->switch_count++;
    }
    out->button_count++;
    return true;
}

static bool parse_cover_switch_token(const char *token,
                                     device_composition_t *out,
                                     uint16_t debounce_ms, uint16_t gap_ms) {
    const char *    end;
    hal_gpio_pin_t  open_pin;
    hal_gpio_pin_t  close_pin;
    hal_gpio_pull_t pull;
    device_cover_switch_config_t *cover_switch;

    if (out->cover_switch_count >= DEVICE_COMPOSITION_MAX_COVER_SWITCHES ||
        out->button_count + 2 > DEVICE_COMPOSITION_MAX_BUTTONS ||
        !parse_pin(token + 1, &end, &open_pin) ||
        !parse_pin(end, &end, &close_pin) || !parse_pull(end, &pull)) {
        return false;
    }
    cover_switch = &out->cover_switches[out->cover_switch_count++];
    cover_switch->open_button_id  = out->button_count++;
    cover_switch->close_button_id = out->button_count++;
    out->buttons[cover_switch->open_button_id] = (device_button_config_t){
        .pin                = open_pin,
        .pull               = pull,
        .debounce_ms        = debounce_ms,
        .hold_ms            = 800,
        .multi_click_gap_ms = gap_ms,
        .role               = DEVICE_BUTTON_COVER_SWITCH,
    };
    out->buttons[cover_switch->close_button_id] = (device_button_config_t){
        .pin                = close_pin,
        .pull               = pull,
        .debounce_ms        = debounce_ms,
        .hold_ms            = 800,
        .multi_click_gap_ms = gap_ms,
        .role               = DEVICE_BUTTON_COVER_SWITCH,
    };
    return true;
}

static config_parse_result_t parse_token(const char *token,
                                         device_composition_t *out,
                                         uint16_t *debounce_ms,
                                         uint16_t *gap_ms) {
    const char *   end;
    hal_gpio_pin_t pin;
    uint16_t       value;

    if (strcmp(token, "SLP") == 0) {
        out->simultaneous_latching_pulses = true;
    } else if (token[0] == 'D') {
        if (!parse_uint16(token + 1, debounce_ms)) {
            return CONFIG_PARSE_SYNTAX;
        }
    } else if (token[0] == 'G') {
        if (!parse_uint16(token + 1, gap_ms)) {
            return CONFIG_PARSE_SYNTAX;
        }
    } else if (strcmp(token, "M") == 0) {
        out->momentary_switches = true;
    } else if (token[0] == 'i') {
        if (!parse_uint16(token + 1, &value)) {
            return CONFIG_PARSE_SYNTAX;
        }
    } else if (token[0] == 'K') {
        uint16_t mask;
        uint16_t dead_time_ms = 0;

        if (!parse_hex_mask(token + 1, &mask, &end) ||
            (*end != '\0' && (*end != ':' || !parse_uint16(end + 1, &dead_time_ms)))) {
            return CONFIG_PARSE_SYNTAX;
        }
        if (out->interlock_count >= DEVICE_COMPOSITION_MAX_INTERLOCKS) {
            return CONFIG_PARSE_LIMIT;
        }
        if (mask != 0) {
            out->interlocks[out->interlock_count++] = (device_interlock_config_t){
                .relay_mask   = mask,
                .dead_time_ms = dead_time_ms,
            };
        }
    } else if (token[0] == 'B' && token[1] == 'T') {
        if (!parse_pin(token + 2, &end, &out->battery_pin) || *end != '\0') {
            return CONFIG_PARSE_SYNTAX;
        }
    } else if (token[0] == 'B' || token[0] == 'S') {
        if (out->button_count >= DEVICE_COMPOSITION_MAX_BUTTONS ||
            (token[0] == 'S' &&
             out->switch_count >= DEVICE_COMPOSITION_MAX_SWITCHES)) {
            return CONFIG_PARSE_LIMIT;
        }
        if (!parse_button_token(token, out, *debounce_ms, *gap_ms)) {
            return CONFIG_PARSE_SYNTAX;
        }
    } else if (token[0] == 'L' || token[0] == 'I') {
        device_indicator_config_t *indicator;

        if (out->indicator_count >= DEVICE_COMPOSITION_MAX_INDICATORS ||
            !parse_pin(token + 1, &end, &pin) ||
            (*end != '\0' && strcmp(end, "i") != 0)) {
            return CONFIG_PARSE_LIMIT;
        }
        indicator  = &out->indicators[out->indicator_count++];
        *indicator = (device_indicator_config_t){
            .pin       = pin,
            .on_high   = strcmp(end, "i") != 0,
            .dedicated = token[0] == 'L',
        };
    } else if (token[0] == 'R') {
        device_relay_config_t *relay;

        if (out->relay_count >= DEVICE_COMPOSITION_MAX_RELAYS ||
            out->relay_endpoint_count >= DEVICE_COMPOSITION_MAX_RELAYS ||
            !parse_pin(token + 1, &end, &pin)) {
            return CONFIG_PARSE_LIMIT;
        }
        relay  = &out->relays[out->relay_count];
        *relay = (device_relay_config_t){ .on_pin  = pin,
                                          .off_pin = HAL_INVALID_PIN };
        if (*end != '\0') {
            if (!parse_pin(end, &end, &relay->off_pin) || *end != '\0') {
                return CONFIG_PARSE_SYNTAX;
            }
            relay->is_latching = true;
        }
        out->relay_endpoint_ids[out->relay_endpoint_count++] = out->relay_count++;
    } else if (token[0] == 'X') {
        if (out->cover_switch_count >= DEVICE_COMPOSITION_MAX_COVER_SWITCHES ||
            out->button_count + 2 > DEVICE_COMPOSITION_MAX_BUTTONS) {
            return CONFIG_PARSE_LIMIT;
        }
        if (!parse_cover_switch_token(token, out, *debounce_ms, *gap_ms)) {
            return CONFIG_PARSE_SYNTAX;
        }
    } else if (token[0] == 'C') {
        device_cover_config_t *cover;
        hal_gpio_pin_t         open_pin;
        hal_gpio_pin_t         close_pin;

        if (out->cover_count >= DEVICE_COMPOSITION_MAX_COVERS ||
            out->relay_count + 2 > DEVICE_COMPOSITION_MAX_RELAYS ||
            !parse_pin(token + 1, &end, &open_pin) ||
            !parse_pin(end, &end, &close_pin) || *end != '\0') {
            return CONFIG_PARSE_LIMIT;
        }
        cover = &out->covers[out->cover_count++];
        cover->open_relay_id              = out->relay_count++;
        cover->close_relay_id             = out->relay_count++;
        out->relays[cover->open_relay_id] = (device_relay_config_t){
            .on_pin  = open_pin,
            .off_pin = HAL_INVALID_PIN,
        };
        out->relays[cover->close_relay_id] = (device_relay_config_t){
            .on_pin  = close_pin,
            .off_pin = HAL_INVALID_PIN,
        };
    } else {
        return CONFIG_PARSE_SYNTAX;
    }
    return CONFIG_PARSE_OK;
}

config_parse_result_t config_parser_parse(const char *text,
                                          device_composition_t *out) {
    const char *cursor = text;
    const char *end;
    uint16_t    debounce_ms = CONFIG_DEFAULT_DEBOUNCE_MS;
    uint16_t    gap_ms      = CONFIG_DEFAULT_MULTI_CLICK_GAP_MS;

    if (out == NULL) {
        return CONFIG_PARSE_SYNTAX;
    }
    memset(out, 0, sizeof(*out));
    out->battery_pin = HAL_INVALID_PIN;
    if (text == NULL) {
        goto syntax;
    }
    for (uint8_t field = 0; field < 2; field++) {
        size_t length;
        char * destination = field == 0 ? out->manufacturer : out->model;
        size_t capacity    = field == 0 ? sizeof(out->manufacturer) :
                             sizeof(out->model);

        end = cursor;
        while (*end != '\0' && *end != ';') {
            end++;
        }
        length = (size_t)(end - cursor);
        if (*end != ';' || length >= capacity) {
            goto syntax;
        }
        memcpy(destination, cursor, length);
        cursor = end + 1;
    }
    while (*cursor != '\0') {
        char   token[32];
        size_t length;
        config_parse_result_t result;

        end = cursor;
        while (*end != '\0' && *end != ';') {
            end++;
        }
        length = (size_t)(end - cursor);
        if (length == 0 || length >= sizeof(token)) {
            goto syntax;
        }
        memcpy(token, cursor, length);
        token[length] = '\0';
        cursor        = *end == ';' ? end + 1 : end;
        result        = parse_token(token, out, &debounce_ms, &gap_ms);
        if (result != CONFIG_PARSE_OK) {
            memset(out, 0, sizeof(*out));
            return result;
        }
    }
    for (uint8_t i = 0; i < out->button_count; i++) {
        out->buttons[i].debounce_ms        = debounce_ms;
        out->buttons[i].multi_click_gap_ms = gap_ms;
    }
    out->endpoint_count = (uint8_t)(out->switch_count +
                                    out->relay_endpoint_count +
                                    out->cover_switch_count + out->cover_count);
    if (out->endpoint_count == 0) {
        out->endpoint_count = 1;
    }
    {
        config_parse_result_t result = device_composition_validate(out);

        if (result != CONFIG_PARSE_OK) {
            memset(out, 0, sizeof(*out));
        }
        return result;
    }

syntax:
    memset(out, 0, sizeof(*out));
    return CONFIG_PARSE_SYNTAX;
}
