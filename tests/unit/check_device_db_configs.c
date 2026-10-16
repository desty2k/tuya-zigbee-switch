/*
 * Parses config strings with the firmware's own parser, built with every
 * feature. Input on stdin: one "<board>\t<config|alt>\t<config string>" per
 * line, as printed by `helper_scripts/board_features.py configs`. A rejected
 * config string would boot the recovery graph on the device and fails the
 * check; a rejected alternative string is only reported, because
 * device_db.yaml may list alternatives the firmware does not run yet.
 */
#include "device_config/config_parser.h"
#include "support/fake_gpio_counter.h"

#include <stdio.h>
#include <string.h>

static const char *result_name(config_parse_result_t result) {
    switch (result) {
    case CONFIG_PARSE_OK:              return "OK";
    case CONFIG_PARSE_SYNTAX:          return "SYNTAX";
    case CONFIG_PARSE_LIMIT:           return "LIMIT";
    case CONFIG_PARSE_REFERENCE:       return "REFERENCE";
    case CONFIG_PARSE_PIN_CONFLICT:    return "PIN_CONFLICT";
    case CONFIG_PARSE_RESOURCE:        return "RESOURCE";
    case CONFIG_PARSE_FEATURE_MISSING: return "FEATURE_MISSING";
    }
    return "UNKNOWN";
}

int main(void) {
    static char          line[1024];
    device_composition_t composition;
    unsigned int         checked  = 0;
    unsigned int         rejected = 0;

    fake_gpio_counter_reset();
    while (fgets(line, sizeof(line), stdin) != NULL) {
        char *kind = strchr(line, '\t');
        char *config;

        line[strcspn(line, "\r\n")] = '\0';
        if (kind == NULL || (config = strchr(kind + 1, '\t')) == NULL) {
            continue;
        }
        *kind++   = '\0';
        *config++ = '\0';
        checked++;

        config_parse_result_t result = config_parser_parse(config, &composition);
        if (result == CONFIG_PARSE_OK) {
            continue;
        }
        if (strcmp(kind, "config") == 0) {
            fprintf(stderr, "%s: config_str rejected (%s): %s\n", line,
                    result_name(result), config);
            rejected++;
        } else {
            fprintf(stderr, "warning: %s: alt_config_str rejected (%s): %s\n",
                    line, result_name(result), config);
        }
    }
    printf("%u config strings parsed, %u config_str rejected\n", checked, rejected);
    return rejected == 0 ? 0 : 1;
}
