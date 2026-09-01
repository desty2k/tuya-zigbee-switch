#ifndef DEVICE_CONFIG_CONFIG_PARSER_H_
#define DEVICE_CONFIG_CONFIG_PARSER_H_

#include "device_config/device_composition.h"

config_parse_result_t config_parser_parse(const char *text,
                                          device_composition_t *out);

#endif
