/*
 * Built once per feature set (Makefile UNIT_FEATURE_SETS); every expectation
 * follows the FEATURE_* values of the build.
 */
#include "device_config/build_features.h"
#include "device_config/config_parser.h"
#include "support/runner.h"

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

static config_parse_result_t parse(const char *text,
                                   device_composition_t *composition) {
    char source[128];

    strncpy(source, text, sizeof(source) - 1);
    source[sizeof(source) - 1] = '\0';
    return config_parser_parse(source, composition);
}

static bool output_is_zeroed(const device_composition_t *composition) {
    const uint8_t *bytes = (const uint8_t *)composition;

    for (size_t i = 0; i < sizeof(*composition); i++) {
        if (bytes[i] != 0) return false;
    }
    return true;
}

#define ASSERT_FEATURE_TOKEN(config, compiled)                                \
        do {                                                                  \
            device_composition_t composition;                                 \
            memset(&composition, 0xa5, sizeof(composition));                  \
            if (compiled) {                                                   \
                ASSERT_EQ(CONFIG_PARSE_OK, parse(config, &composition));      \
            } else {                                                          \
                ASSERT_EQ(CONFIG_PARSE_FEATURE_MISSING,                       \
                          parse(config, &composition));                       \
                ASSERT_TRUE(output_is_zeroed(&composition));                  \
            }                                                                 \
        } while (0)

TEST(feature_tokens_parse_only_when_the_feature_is_compiled) {
    ASSERT_FEATURE_TOKEN("M;T;RA0;", FEATURE_RELAY);
    ASSERT_FEATURE_TOKEN("M;T;SA0u;", FEATURE_SWITCH);
    ASSERT_FEATURE_TOKEN("M;T;CA0A1;", FEATURE_COVER);
    ASSERT_FEATURE_TOKEN("M;T;XA0A1u;", FEATURE_COVER);
    ASSERT_FEATURE_TOKEN("M;T;BTA5;", FEATURE_BATTERY);
    ASSERT_FEATURE_TOKEN("M;T;EBC2C3D2;", FEATURE_METER);
    ASSERT_FEATURE_TOKEN("M;T;K0:25;", FEATURE_INTERLOCK);
}

TEST(core_tokens_parse_in_every_build) {
    device_composition_t composition;

    ASSERT_EQ(CONFIG_PARSE_OK,
              parse("M;T;SLP;D12;G400;BA0u;LC2i;ID3;i43627;M;",
                    &composition));
    ASSERT_EQ(1, composition.button_count);
    ASSERT_EQ(2, composition.indicator_count);
    ASSERT_EQ(12, composition.buttons[0].debounce_ms);
}

TEST(excluded_feature_is_reported_after_earlier_valid_tokens) {
    device_composition_t composition;
    config_parse_result_t expected = FEATURE_COVER ? CONFIG_PARSE_OK :
                                     CONFIG_PARSE_FEATURE_MISSING;

    ASSERT_EQ(expected, parse("M;T;BA0u;LC2;CB0B1;", &composition));
}

TEST(excluded_feature_takes_precedence_over_token_syntax) {
    device_composition_t composition;
    config_parse_result_t expected = FEATURE_COVER ? CONFIG_PARSE_LIMIT :
                                     CONFIG_PARSE_FEATURE_MISSING;

    ASSERT_EQ(expected, parse("M;T;Cbad;", &composition));
}

int main(void) {
    RUN_TEST(feature_tokens_parse_only_when_the_feature_is_compiled);
    RUN_TEST(core_tokens_parse_in_every_build);
    RUN_TEST(excluded_feature_is_reported_after_earlier_valid_tokens);
    RUN_TEST(excluded_feature_takes_precedence_over_token_syntax);

    return test_runner_failures == 0 ? 0 : 1;
}
