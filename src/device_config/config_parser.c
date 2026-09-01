#include "device_config/config_parser.h"

#include <string.h>

#define CONFIG_DEFAULT_DEBOUNCE_MS    8

static bool parse_pin(const char *s, hal_gpio_pin_t *pin) {
    uint16_t number = 0; char port;

    if (!s || s[0] < 'A' || s[0] > 'Z' || s[1] < '0' || s[1] > '9') return false;

    port = *s++;
    while (*s >= '0' && *s <= '9') number = (uint16_t)(number * 10U + (uint16_t)(*s++ - '0'));
    if (number > 15) return false;

    *pin = (hal_gpio_pin_t)(((uint16_t)(port - 'A') << 4) | number);
    return true;
}

static bool parse_pull(char c, hal_gpio_pull_t *pull) {
    if (c == 'u') *pull = HAL_GPIO_PULL_UP;
    else if (c == 'd') *pull = HAL_GPIO_PULL_DOWN;
    else if (c == 'f' || c == '\0') *pull = HAL_GPIO_PULL_NONE;
    else return false;

    return true;
}

static uint32_t parse_uint(const char *s) {
    uint32_t n = 0;

    while (*s >= '0' && *s <= '9') {
        n = n * 10U + (uint32_t)(*s++ - '0');
    }
    return n;
}

static uint16_t parse_hex(const char *s) {
    uint16_t n = 0;

    while ((*s >= '0' && *s <= '9') || (*s >= 'a' && *s <= 'f') || (*s >= 'A' && *s <= 'F')) {
        uint8_t d = *s <= '9' ? (uint8_t)(*s - '0') : (uint8_t)((*s | 0x20) - 'a' + 10);
        n = (uint16_t)((n << 4) | d); s++;
    }

    return n;
}

static config_parse_result_t validate(device_composition_t *c) {
    uint16_t pins[37]; uint8_t roles[37]; uint8_t count = 0;

    for (uint8_t i = 0; i < c->button_count; i++) {
        pins[count] = c->buttons[i].pin; roles[count++] = 1;
    }
    for (uint8_t i = 0; i < c->relay_count; i++) {
        pins[count] = c->relays[i].on_pin; roles[count++] = 2; if (c->relays[i].is_latching) {
            pins[count] = c->relays[i].off_pin; roles[count++] = 2;
        }
    }
    for (uint8_t i = 0; i < c->indicator_count; i++) {
        pins[count] = c->indicators[i].pin; roles[count++] = 3;
    }
    if (c->battery_pin != HAL_INVALID_PIN) {
        pins[count] = c->battery_pin; roles[count++] = 1;
    }
    for (uint8_t i = 0; i < count;
         i++) for (uint8_t j = 0; j < i;
                   j++) if (pins[i] == pins[j] && roles[i] != roles[j] &&
                            !(roles[i] == 3 || roles[j] == 3)) return CONFIG_PARSE_PIN_CONFLICT;

    for (uint8_t i = 0; i < c->interlock_count;
         i++) if ((c->interlocks[i].relay_mask >> c->relay_count) !=
                  0) return CONFIG_PARSE_REFERENCE;

    for (uint8_t i = 0; i < c->cover_count;
         i++) if (c->covers[i].open_relay_id >= c->relay_count ||
                  c->covers[i].close_relay_id >= c->relay_count ||
                  c->covers[i].open_relay_id ==
                  c->covers[i].close_relay_id) return CONFIG_PARSE_REFERENCE;

    c->endpoint_count = (uint8_t)(c->switch_count + c->relay_endpoint_count +
                                  c->cover_switch_count +
                                  c->cover_count);
    if (!c->endpoint_count) c->endpoint_count = 1;
    return c->endpoint_count >
           DEVICE_COMPOSITION_MAX_ENDPOINTS ? CONFIG_PARSE_RESOURCE : CONFIG_PARSE_OK;
}

config_parse_result_t config_parser_parse(const char *text, device_composition_t *out) {
    const char *cursor = text, *end; uint16_t debounce = CONFIG_DEFAULT_DEBOUNCE_MS, gap = 350;

    if (!out) {
        return CONFIG_PARSE_SYNTAX;
    }
    memset(out, 0, sizeof(*out));
    out->battery_pin = HAL_INVALID_PIN;
    if (!text) goto syntax;
    for (uint8_t field = 0; field < 2; field++) {
        size_t len;

        end = cursor;
        while (*end && *end != ';') {
            end++;
        }
        len = (size_t)(end - cursor);
        if (*end != ';' ||
            len >= (field ? sizeof(out->model) : sizeof(out->manufacturer))) goto syntax;
        memcpy(field ? out->model : out->manufacturer, cursor, len); cursor = end + 1;
    }
    while (*cursor) {
        char token[32]; size_t len;

        end = cursor;
        while (*end && *end != ';') {
            end++;
        }
        len = (size_t)(end - cursor);
        if (!len || len >= sizeof(token)) {
            goto syntax;
        }
        memcpy(token, cursor, len);
        token[len] = '\0'; cursor = *end ? end + 1 : end;
        if (!strcmp(token, "SLP")) out->simultaneous_latching_pulses = true;
        else if (token[0] == 'D') debounce = (uint16_t)parse_uint(token + 1);
        else if (token[0] == 'G') gap = (uint16_t)parse_uint(token + 1);
        else if (!strcmp(token, "M")) out->momentary_switches = true;
        else if (token[0] == 'i') {
            (void)parse_uint(token + 1);
        }else if (token[0] == 'K') {
            char *sep = strchr(token, ':');
            if (out->interlock_count >= DEVICE_COMPOSITION_MAX_INTERLOCKS) goto limit;
            out->interlocks[out->interlock_count] = (device_interlock_config_t){ parse_hex(token +
                                                                                           1),
                                                                                 sep ?
                                                                                 (uint16_t)
                                                                                 parse_uint(sep +
                                                                                            1) :
                                                                                 0 };
            if (out->interlocks[out->interlock_count].relay_mask) out->interlock_count++;
        }else if (!strncmp(token, "BT", 2)) {
            if (!parse_pin(token + 2, &out->battery_pin)) goto syntax;
        }else if (token[0] == 'B' || token[0] == 'S') {
            device_button_config_t *b; hal_gpio_pull_t pull;
            if (out->button_count >= DEVICE_COMPOSITION_MAX_BUTTONS || len < 4 ||
                !parse_pull(token[len - 1], &pull)) goto limit;
            b = &out->buttons[out->button_count];
            if (!parse_pin(token + 1, &b->pin)) goto syntax;
            *b = (device_button_config_t){ b->pin, pull, debounce, token[0] == 'S' ? 800 : 2000,
                                           gap,
                                           token[0] ==
                                           'S' ? DEVICE_BUTTON_SWITCH : DEVICE_BUTTON_ONBOARD };
            if (token[0] == 'S') {
                if (out->switch_count >= DEVICE_COMPOSITION_MAX_SWITCHES) goto limit;
                out->switches[out->switch_count] = (device_switch_config_t){ out->button_count,
                                                                             out->switch_count };
                out->switch_count++;
            }
            out->button_count++;
        }else if (token[0] == 'L' || token[0] == 'I') {
            device_indicator_config_t *i;
            if (out->indicator_count >= DEVICE_COMPOSITION_MAX_INDICATORS) goto limit;
            i = &out->indicators[out->indicator_count++];
            if (!parse_pin(token + 1, &i->pin)) goto syntax;
            i->on_high = len < 4 || token[3] != 'i'; i->dedicated = token[0] == 'L';
        }else if (token[0] == 'R') {
            device_relay_config_t *r;
            if (out->relay_count >= DEVICE_COMPOSITION_MAX_RELAYS ||
                out->relay_endpoint_count >= DEVICE_COMPOSITION_MAX_RELAYS) goto limit;
            r = &out->relays[out->relay_count];
            if (!parse_pin(token + 1, &r->on_pin)) {
                goto syntax;
            }
            r->off_pin = HAL_INVALID_PIN;
            if (len > 3) {
                if (!parse_pin(token + 3, &r->off_pin)) {
                    goto syntax;
                }
                r->is_latching = true;
            }
            out->relay_endpoint_ids[out->relay_endpoint_count++] = out->relay_count++;
        }else if (token[0] == 'X') {
            device_cover_switch_config_t *x; hal_gpio_pin_t p; hal_gpio_pull_t pull;
            if (out->cover_switch_count >= DEVICE_COMPOSITION_MAX_COVER_SWITCHES ||
                out->button_count + 2 > DEVICE_COMPOSITION_MAX_BUTTONS ||
                !parse_pull(token[len - 1], &pull)) goto limit;
            x = &out->cover_switches[out->cover_switch_count++];
            x->open_button_id = out->button_count++; x->close_button_id = out->button_count++;
            if (!parse_pin(token + 1, &p)) goto syntax;
            out->buttons[x->open_button_id] = (device_button_config_t){ p, pull, debounce, 800, gap,
                                                                        DEVICE_BUTTON_COVER_SWITCH };
            if (!parse_pin(token + 3, &p)) goto syntax;
            out->buttons[x->close_button_id] = (device_button_config_t){ p, pull, debounce, 800,
                                                                         gap,
                                                                         DEVICE_BUTTON_COVER_SWITCH };
        }else if (token[0] == 'C') {
            device_cover_config_t *cover;
            if (out->cover_count >= DEVICE_COMPOSITION_MAX_COVERS ||
                out->relay_count + 2 > DEVICE_COMPOSITION_MAX_RELAYS) goto limit;
            cover = &out->covers[out->cover_count++]; cover->open_relay_id = out->relay_count++;
            cover->close_relay_id = out->relay_count++;
            if (!parse_pin(token + 1,
                           &out->relays[cover->open_relay_id].on_pin) || !parse_pin(token + 3,
                                                                                    &out->relays[
                                                                                        cover->
                                                                                        close_relay_id]
                                                                                    .on_pin)) goto
                syntax;
        }else goto syntax;
    }
    return validate(out);

limit: memset(out, 0, sizeof(*out)); return CONFIG_PARSE_LIMIT;

syntax: memset(out, 0, sizeof(*out)); return CONFIG_PARSE_SYNTAX;
}
