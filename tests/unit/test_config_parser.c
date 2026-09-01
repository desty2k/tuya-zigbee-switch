#include "device_config/config_parser.h"
#include "support/runner.h"

#include <string.h>

static void append_token(char *text, const char *token) {
    strcat(text, token);
    strcat(text, ";");
}

static device_composition_t valid_composition(void) {
    device_composition_t composition;

    memset(&composition, 0, sizeof(composition));
    composition.battery_pin = HAL_INVALID_PIN;
    return composition;
}

TEST(parses_every_legacy_token_without_mutating_input) {
    const char text[] =
        "Tuya;TS0726;SLP;D12;G400;K3:25;BTC5;BA0;SB1u;LC2i;ID3;RE4F4;"
        "XG5H6d;CI7J8;i43627;M;";
    char source[sizeof(text)];
    device_composition_t composition;

    memcpy(source, text, sizeof(source));
    ASSERT_EQ(CONFIG_PARSE_OK, config_parser_parse(source, &composition));
    ASSERT_EQ(0, strcmp("Tuya", composition.manufacturer));
    ASSERT_EQ(0, strcmp("TS0726", composition.model));
    ASSERT_TRUE(composition.simultaneous_latching_pulses);
    ASSERT_TRUE(composition.momentary_switches);
    ASSERT_EQ(4, composition.button_count);
    ASSERT_EQ(3, composition.relay_count);
    ASSERT_EQ(2, composition.indicator_count);
    ASSERT_EQ(1, composition.interlock_count);
    ASSERT_EQ(1, composition.switch_count);
    ASSERT_EQ(1, composition.cover_switch_count);
    ASSERT_EQ(1, composition.cover_count);
    ASSERT_EQ(4, composition.endpoint_count);
    ASSERT_EQ(12, composition.buttons[0].debounce_ms);
    ASSERT_EQ(400, composition.buttons[3].multi_click_gap_ms);
    ASSERT_EQ(HAL_GPIO_PULL_NONE, composition.buttons[0].pull);
    ASSERT_EQ(HAL_GPIO_PULL_UP, composition.buttons[1].pull);
    ASSERT_TRUE(composition.relays[0].is_latching);
    ASSERT_EQ(3, composition.interlocks[0].relay_mask);
    ASSERT_EQ(25, composition.interlocks[0].dead_time_ms);
    ASSERT_EQ(0, memcmp(source, text, sizeof(text)));
}

TEST(global_debounce_and_gap_apply_regardless_of_token_position) {
    device_composition_t composition;

    ASSERT_EQ(CONFIG_PARSE_OK,
              config_parser_parse("Tuya;TS0726;SA0u;D19;XB1C1u;G777;BA2d;",
                                  &composition));
    ASSERT_EQ(19, composition.buttons[0].debounce_ms);
    ASSERT_EQ(19, composition.buttons[3].debounce_ms);
    ASSERT_EQ(777, composition.buttons[0].multi_click_gap_ms);
    ASSERT_EQ(777, composition.buttons[3].multi_click_gap_ms);
}

TEST(preserves_endpoint_order_and_detaches_missing_relays) {
    device_composition_t composition;

    ASSERT_EQ(CONFIG_PARSE_OK,
              config_parser_parse("Tuya;TS0726;SA0u;SB1u;RC0;", &composition));
    ASSERT_EQ(3, composition.endpoint_count);
    ASSERT_EQ(0, composition.switches[0].relay_id);
    ASSERT_EQ(1, composition.switches[1].relay_id);
    ASSERT_EQ(0, composition.relay_endpoint_ids[0]);
    ASSERT_EQ(CONFIG_PARSE_OK, device_composition_validate(&composition));
}

TEST(parses_empty_and_relayless_compositions) {
    device_composition_t composition;

    ASSERT_EQ(CONFIG_PARSE_OK, config_parser_parse("Tuya;Empty;", &composition));
    ASSERT_EQ(1, composition.endpoint_count);
    ASSERT_EQ(0, composition.relay_count);
    ASSERT_EQ(CONFIG_PARSE_OK,
              config_parser_parse("Tuya;Remote;SA0u;", &composition));
    ASSERT_EQ(1, composition.endpoint_count);
    ASSERT_EQ(1, composition.switch_count);
    ASSERT_EQ(0, composition.relay_count);
}

TEST(rejects_each_bounded_array_overflow) {
    char text[256] = "Tuya;Bounds;";
    device_composition_t composition;

    for (uint8_t i = 0; i <= DEVICE_COMPOSITION_MAX_BUTTONS; i++) {
        char token[8];
        snprintf(token, sizeof(token), "BA%u", i);
        append_token(text, token);
    }
    ASSERT_EQ(CONFIG_PARSE_LIMIT, config_parser_parse(text, &composition));

    strcpy(text, "Tuya;Bounds;");
    for (uint8_t i = 0; i <= DEVICE_COMPOSITION_MAX_RELAYS; i++) {
        char token[8];
        snprintf(token, sizeof(token), "RB%u", i);
        append_token(text, token);
    }
    ASSERT_EQ(CONFIG_PARSE_LIMIT, config_parser_parse(text, &composition));

    strcpy(text, "Tuya;Bounds;");
    for (uint8_t i = 0; i <= DEVICE_COMPOSITION_MAX_INDICATORS; i++) {
        char token[8];
        snprintf(token, sizeof(token), "IC%u", i);
        append_token(text, token);
    }
    ASSERT_EQ(CONFIG_PARSE_LIMIT, config_parser_parse(text, &composition));

    ASSERT_EQ(CONFIG_PARSE_LIMIT,
              config_parser_parse("Tuya;Bounds;CA0B0;CC0D0;CE0F0;CG0H0;",
                                  &composition));
    ASSERT_EQ(CONFIG_PARSE_LIMIT,
              config_parser_parse("Tuya;Bounds;XA0B0;XC0D0;XE0F0;XG0H0;",
                                  &composition));

    strcpy(text, "Tuya;Bounds;");
    for (uint8_t i = 0; i <= DEVICE_COMPOSITION_MAX_SWITCHES; i++) {
        char token[8];
        snprintf(token, sizeof(token), "SD%uu", i);
        append_token(text, token);
    }
    ASSERT_EQ(CONFIG_PARSE_LIMIT, config_parser_parse(text, &composition));

    strcpy(text, "Tuya;Bounds;");
    for (uint8_t i = 0; i < DEVICE_COMPOSITION_MAX_RELAYS; i++) {
        char token[8];
        snprintf(token, sizeof(token), "RE%u", i);
        append_token(text, token);
    }
    for (uint8_t i = 0; i <= DEVICE_COMPOSITION_MAX_INTERLOCKS; i++) {
        append_token(text, "K1");
    }
    ASSERT_EQ(CONFIG_PARSE_LIMIT, config_parser_parse(text, &composition));
}

TEST(rejects_endpoint_and_cluster_capacity_overflow) {
    device_composition_t composition;

    ASSERT_EQ(CONFIG_PARSE_RESOURCE,
              config_parser_parse("Tuya;Bounds;SA0u;SA1u;SA2u;SA3u;SA4u;"
                                  "SA5u;SA6u;SA7u;SA8u;SA9u;", &composition));
}

TEST(rejects_conflicting_pins_and_invalid_references) {
    device_composition_t composition = valid_composition();

    ASSERT_EQ(CONFIG_PARSE_PIN_CONFLICT,
              config_parser_parse("Tuya;Pins;SA0u;SA0u;", &composition));
    ASSERT_EQ(CONFIG_PARSE_PIN_CONFLICT,
              config_parser_parse("Tuya;Pins;SA0u;RA0;", &composition));
    ASSERT_EQ(CONFIG_PARSE_REFERENCE,
              config_parser_parse("Tuya;Interlock;RA0;K4;", &composition));

    composition = valid_composition();
    composition.button_count = 1;
    composition.switch_count = 1;
    composition.switches[0].button_id = 1;
    composition.endpoint_count = 1;
    ASSERT_EQ(CONFIG_PARSE_REFERENCE, device_composition_validate(&composition));

    composition = valid_composition();
    composition.relay_count = 3;
    composition.relays[0].on_pin = 0;
    composition.relays[1].on_pin = 1;
    composition.relays[2].on_pin = 2;
    composition.cover_count = 2;
    composition.endpoint_count = 2;
    composition.covers[0] = (device_cover_config_t){ .open_relay_id = 0,
                                                       .close_relay_id = 1 };
    composition.covers[1] = (device_cover_config_t){ .open_relay_id = 1,
                                                       .close_relay_id = 2 };
    ASSERT_EQ(CONFIG_PARSE_REFERENCE, device_composition_validate(&composition));
}

TEST(parser_failure_zeroes_the_output) {
    device_composition_t composition;
    uint8_t *bytes = (uint8_t *)&composition;

    memset(&composition, 0xa5, sizeof(composition));
    ASSERT_EQ(CONFIG_PARSE_SYNTAX,
              config_parser_parse("Tuya;Broken;SA;", &composition));
    for (size_t i = 0; i < sizeof(composition); i++) {
        ASSERT_EQ(0, bytes[i]);
    }
}

int main(void) {
    RUN_TEST(parses_every_legacy_token_without_mutating_input);
    RUN_TEST(global_debounce_and_gap_apply_regardless_of_token_position);
    RUN_TEST(preserves_endpoint_order_and_detaches_missing_relays);
    RUN_TEST(parses_empty_and_relayless_compositions);
    RUN_TEST(rejects_each_bounded_array_overflow);
    RUN_TEST(rejects_endpoint_and_cluster_capacity_overflow);
    RUN_TEST(rejects_conflicting_pins_and_invalid_references);
    RUN_TEST(parser_failure_zeroes_the_output);

    return test_runner_failures == 0 ? 0 : 1;
}
