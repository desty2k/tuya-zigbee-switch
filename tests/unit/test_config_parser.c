#include "device_config/config_parser.h"
#include "support/fake_gpio_counter.h"
#include "support/fake_gpio_parse.h"
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
    ASSERT_TRUE(composition.has_image_type);
    ASSERT_EQ(43627, composition.image_type);
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

TEST(debounce_defaults_to_25_ms_without_d_token) {
    device_composition_t composition;

    ASSERT_EQ(CONFIG_PARSE_OK,
              config_parser_parse("Tuya;TS0726;SA0u;BA2d;", &composition));
    ASSERT_EQ(25, composition.buttons[0].debounce_ms);
    ASSERT_EQ(25, composition.buttons[1].debounce_ms);
    ASSERT_TRUE(!composition.has_image_type);
}

TEST(interlock_dead_time_defaults_without_duration) {
    device_composition_t composition;

    ASSERT_EQ(CONFIG_PARSE_OK,
              config_parser_parse("Tuya;TS0726;RA0;RA1;K3;K3:0;", &composition));
    ASSERT_EQ(2, composition.interlock_count);
    ASSERT_EQ(200, composition.interlocks[0].dead_time_ms);
    ASSERT_EQ(0, composition.interlocks[1].dead_time_ms);
}

/* A config that fails to parse yields 0, which no assertion below expects. */
static uint32_t relay_topology_of(const char *config) {
    device_composition_t composition;

    if (config_parser_parse(config, &composition) != CONFIG_PARSE_OK) {
        test_runner_failures++;
        return 0;
    }
    return device_composition_relay_topology(&composition);
}

TEST(relay_topology_tracks_only_relay_endpoints_and_groups) {
    uint32_t base   = relay_topology_of("A;B;SA0u;RB0;RB1;");
    uint32_t paired = relay_topology_of("A;B;SA0u;RB0;RB1;K3;");

    ASSERT_EQ(base, relay_topology_of("Other;Model;SA0u;RB0;RB1;G400;LC0;"));
    ASSERT_EQ(base, relay_topology_of("A;B;SA0u;RB1;RB0;"));
    ASSERT_EQ(base, relay_topology_of("A;B;SA0u;RB0C0;RB1;"));
    ASSERT_EQ(paired, relay_topology_of("A;B;SA0u;RB0;RB1;K3:0;"));
    ASSERT_EQ(paired, relay_topology_of("A;B;SA0u;RB0;RB1;K3:500;"));
    ASSERT_TRUE(base != paired);
    ASSERT_TRUE(base != relay_topology_of("A;B;SA0u;RB0;"));
    ASSERT_EQ(base, relay_topology_of("A;B;SA0u;RB0;RB1;CC1C2;"));
    ASSERT_TRUE(paired != relay_topology_of("A;B;SA0u;RB0;RB1;RB2;K5;"));
}

TEST(debounce_outside_attribute_range_is_rejected) {
    device_composition_t composition;

    ASSERT_EQ(CONFIG_PARSE_SYNTAX,
              config_parser_parse("Tuya;TS0726;D0;SA0u;", &composition));
    ASSERT_EQ(CONFIG_PARSE_SYNTAX,
              config_parser_parse("Tuya;TS0726;D201;SA0u;", &composition));
    ASSERT_EQ(CONFIG_PARSE_OK,
              config_parser_parse("Tuya;TS0726;D1;SA0u;", &composition));
    ASSERT_EQ(1, composition.buttons[0].debounce_ms);
    ASSERT_EQ(CONFIG_PARSE_OK,
              config_parser_parse("Tuya;TS0726;D200;SA0u;", &composition));
    ASSERT_EQ(200, composition.buttons[0].debounce_ms);
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

TEST(rejects_unavailable_or_conflicting_meter_counter_routes) {
    device_composition_t composition = valid_composition();

    composition.endpoint_count = 1;
    composition.meter_count = 1;
    composition.meters[0] = (device_meter_config_t){
        .ic = DEVICE_METER_BL0937,
        .cf_pin = 8,
        .cf1_pin = 9,
        .sel_pin = 10,
        .sel_high_selects_voltage = true,
    };
    fake_gpio_counter_reset();
    ASSERT_EQ(CONFIG_PARSE_OK, device_composition_validate(&composition));
    fake_gpio_counter_set_route(1, false);
    ASSERT_EQ(CONFIG_PARSE_RESOURCE, device_composition_validate(&composition));
    fake_gpio_counter_set_route(1, true);
    composition.meters[0].cf1_pin = 8;
    ASSERT_EQ(CONFIG_PARSE_PIN_CONFLICT, device_composition_validate(&composition));
    composition.meters[0].cf1_pin = 9;
    composition.meters[0].sel_pin = 9;
    ASSERT_EQ(CONFIG_PARSE_PIN_CONFLICT, device_composition_validate(&composition));
    composition.meters[0].sel_pin = 10;
    fake_gpio_counter_set_capacity(1);
    ASSERT_EQ(CONFIG_PARSE_RESOURCE, device_composition_validate(&composition));
    fake_gpio_counter_reset();
    composition.meter_count = 2;
    composition.meters[1] = composition.meters[0];
    ASSERT_EQ(CONFIG_PARSE_RESOURCE, device_composition_validate(&composition));
}

TEST(parses_the_bl0937_meter_token) {
    device_composition_t composition;

    fake_gpio_counter_reset();
    ASSERT_EQ(CONFIG_PARSE_OK,
              config_parser_parse("drzlxjne;PM;SB4u;RB5;IC4;EBC2C3D2;M;", &composition));
    ASSERT_EQ(1, composition.meter_count);
    ASSERT_EQ(DEVICE_METER_BL0937, composition.meters[0].ic);
    ASSERT_EQ(FAKE_GPIO_PIN('C', 2), composition.meters[0].cf_pin);
    ASSERT_EQ(FAKE_GPIO_PIN('C', 3), composition.meters[0].cf1_pin);
    ASSERT_EQ(FAKE_GPIO_PIN('D', 2), composition.meters[0].sel_pin);
    ASSERT_TRUE(composition.meters[0].sel_high_selects_voltage);
    ASSERT_EQ(CONFIG_PARSE_OK,
              config_parser_parse("M;PM;EBC2C3D2i;", &composition));
    ASSERT_TRUE(!composition.meters[0].sel_high_selects_voltage);
    ASSERT_EQ(1, composition.endpoint_count);
    ASSERT_EQ(CONFIG_PARSE_SYNTAX, config_parser_parse("M;PM;EHC2C3D2;", &composition));
    ASSERT_EQ(CONFIG_PARSE_SYNTAX, config_parser_parse("M;PM;EBC2C3;", &composition));
    ASSERT_EQ(CONFIG_PARSE_SYNTAX, config_parser_parse("M;PM;EBC2C3D2x;", &composition));
    /* Meter pins are claimed against buttons, relays and indicators. */
    ASSERT_EQ(CONFIG_PARSE_PIN_CONFLICT,
              config_parser_parse("M;PM;SC2u;EBC2C3D2;", &composition));
    ASSERT_EQ(CONFIG_PARSE_PIN_CONFLICT,
              config_parser_parse("M;PM;RD2;EBC2C3D2;", &composition));
    ASSERT_EQ(CONFIG_PARSE_PIN_CONFLICT,
              config_parser_parse("M;PM;IC3;EBC2C3D2;", &composition));
    fake_gpio_counter_set_capacity(0);
    ASSERT_EQ(CONFIG_PARSE_RESOURCE, config_parser_parse("M;PM;EBC2C3D2;", &composition));
    fake_gpio_counter_reset();
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

TEST(pins_are_the_hal_pin_ids) {
    device_composition_t composition;

    ASSERT_EQ(CONFIG_PARSE_OK,
              config_parser_parse("Tuya;Pins;SB7u;RC2;RD3E4;LA1i;BTA5;XB0B1d;",
                                  &composition));
    ASSERT_EQ(FAKE_GPIO_PIN('B', 7), composition.buttons[0].pin);
    ASSERT_EQ(FAKE_GPIO_PIN('C', 2), composition.relays[0].on_pin);
    ASSERT_EQ(FAKE_GPIO_PIN('D', 3), composition.relays[1].on_pin);
    ASSERT_EQ(FAKE_GPIO_PIN('E', 4), composition.relays[1].off_pin);
    ASSERT_EQ(FAKE_GPIO_PIN('A', 1), composition.indicators[0].pin);
    ASSERT_EQ(FAKE_GPIO_PIN('A', 5), composition.battery_pin);
    ASSERT_EQ(FAKE_GPIO_PIN('B', 0), composition.buttons[1].pin);
    ASSERT_EQ(FAKE_GPIO_PIN('B', 1), composition.buttons[2].pin);
    ASSERT_EQ(HAL_GPIO_PULL_DOWN, composition.buttons[2].pull);
}

TEST(pins_the_hal_rejects_fail_the_parse) {
    device_composition_t composition;

    ASSERT_EQ(CONFIG_PARSE_SYNTAX,
              config_parser_parse("Tuya;Pins;SZ1u;", &composition));
    ASSERT_EQ(CONFIG_PARSE_SYNTAX,
              config_parser_parse("Tuya;Pins;XA0Z1u;", &composition));
    ASSERT_EQ(CONFIG_PARSE_SYNTAX,
              config_parser_parse("Tuya;Pins;BTZ5;", &composition));
    ASSERT_TRUE(config_parser_parse("Tuya;Pins;RZ1;", &composition) !=
                CONFIG_PARSE_OK);
    ASSERT_EQ(CONFIG_PARSE_SYNTAX,
              config_parser_parse("Tuya;Pins;SA16u;", &composition));
    ASSERT_EQ(CONFIG_PARSE_SYNTAX,
              config_parser_parse("Tuya;Pins;SA100u;", &composition));
}

int main(void) {
    RUN_TEST(parses_every_legacy_token_without_mutating_input);
    RUN_TEST(debounce_defaults_to_25_ms_without_d_token);
    RUN_TEST(interlock_dead_time_defaults_without_duration);
    RUN_TEST(relay_topology_tracks_only_relay_endpoints_and_groups);
    RUN_TEST(debounce_outside_attribute_range_is_rejected);
    RUN_TEST(global_debounce_and_gap_apply_regardless_of_token_position);
    RUN_TEST(preserves_endpoint_order_and_detaches_missing_relays);
    RUN_TEST(parses_empty_and_relayless_compositions);
    RUN_TEST(rejects_each_bounded_array_overflow);
    RUN_TEST(rejects_endpoint_and_cluster_capacity_overflow);
    RUN_TEST(rejects_conflicting_pins_and_invalid_references);
    RUN_TEST(rejects_unavailable_or_conflicting_meter_counter_routes);
    RUN_TEST(parses_the_bl0937_meter_token);
    RUN_TEST(parser_failure_zeroes_the_output);
    RUN_TEST(pins_are_the_hal_pin_ids);
    RUN_TEST(pins_the_hal_rejects_fail_the_parse);

    return test_runner_failures == 0 ? 0 : 1;
}
