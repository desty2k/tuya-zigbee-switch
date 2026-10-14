#include "commands.h"
#include "machine_io.h"

#include "hal/gpio.h"
#include "hal/timer.h"
#include "hal/zigbee.h"
#include "app.h"

#include "stub/hal/stub.h"

#include "stub/stub_app.h"
#include "device_config/build_features.h"
#include "device_config/config_parser.h"
#include "device_config/device_config_service.h"
#include "device_config/feature_wiring.h"
#include "base_components/button_input.h"
#include "base_components/gesture_fsm.h"
#include "base_components/timer_service.h"
#include "zigbee/zcl_cluster_defs.h"
#include "zigbee/button_event_cluster.h"
#if FEATURE_PROBE
#include "stub/hal/gpio_probe.h"
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int parse_u8_dec(const char *s, uint8_t *out) {
    char *e = NULL;
    long  v = strtol(s, &e, 10);

    if (*s == '\0' || *e || v < 0 || v > 255)
        return -1;

    *out = (uint8_t)v;
    return 0;
}

static int parse_u16_hex(const char *s, uint16_t *out) {
    char *e = NULL;
    long  v = strtol(s, &e, 16);

    if (*s == '\0' || *e || v < 0 || v > 0xFFFF)
        return -1;

    *out = (uint16_t)v;
    return 0;
}

static int hex_digit_value(char digit) {
    if (digit >= '0' && digit <= '9') return digit - '0';

    if (digit >= 'a' && digit <= 'f') return digit - 'a' + 10;

    if (digit >= 'A' && digit <= 'F') return digit - 'A' + 10;

    return -1;
}

static int parse_hex_bytes(const char *text, uint8_t *bytes,
                           size_t capacity, uint16_t *length) {
    size_t text_length = strlen(text);

    if ((text_length & 1) != 0 || text_length / 2 > capacity) return -1;

    for (size_t i = 0; i < text_length / 2; i++) {
        int high = hex_digit_value(text[i * 2]);
        int low  = hex_digit_value(text[i * 2 + 1]);

        if (high < 0 || low < 0) return -1;

        bytes[i] = (uint8_t)((high << 4) | low);
    }
    *length = (uint16_t)(text_length / 2);
    return 0;
}

extern volatile sig_atomic_t g_should_exit;

static int cmd_machine(int argc, char **argv) {
    if (argc != 2) {
        fprintf(stderr, "Usage: machine on|off\n");
        io_res_err("usage");
        return -1;
    }
    if (strcmp(argv[1], "on") == 0) {
        g_machine_mode = true;
        io_res_ok(NULL);
        return 0;
    }
    if (strcmp(argv[1], "off") == 0) {
        io_res_ok(NULL);
        g_machine_mode = false;
        fprintf(stderr, "machine: off\n");
        return 0;
    }
    io_res_err("arg");
    return -1;
}

static int cmd_help(int argc, char **argv) {
    (void)argc;
    (void)argv;
    stub_app_print_help();
    io_res_ok(NULL);
    return 0;
}

static int cmd_status(int argc, char **argv) {
    (void)argc;
    (void)argv;
    stub_app_show_status();
    io_res_ok("uptime_ms=%u joined=%d poll_rate_ms=%u", hal_millis(),
              hal_zigbee_get_network_status(), hal_zigbee_get_poll_rate_ms());
    return 0;
}

static int cmd_diag(int argc, char **argv) {
    hal_gpio_diagnostics_t    diag         = hal_gpio_get_diagnostics();
    app_network_diagnostics_t network_diag = app_network_get_diagnostics();

    (void)argc;
    (void)argv;
    io_res_ok("gpio_init_count=%u gpio_write_count=%u tasks_active=%u "
              "gpio_irq_count=%u gpio_edges_captured=%u gpio_edges_dropped=%u "
              "button_events_emitted=%u gestures_emitted=%u "
              "zb_button_events_dropped=%u zb_button_events_expired=%u "
              "zb_button_events_send_failed=%u zb_button_events_high_water=%u "
              "zb_button_events_submitted=%u gpio_rearm_limit_hits=%u "
              "network_transitions=%u network_losses=%u network_joins=%u "
              "steering_attempts=%u announce_attempts=%u announce_failures=%u "
              "uptime_ms=%u last_network_transition_ms=%u",
              stub_gpio_init_count(), stub_gpio_write_count(),
              stub_tasks_active_count(), diag.gpio_irq_count,
              diag.gpio_edges_captured,
              button_input_gpio_edges_dropped(),
              button_input_events_emitted(), gesture_fsm_events_emitted(),
              button_event_cluster_dropped(), button_event_cluster_expired(),
              button_event_cluster_send_failed(), button_event_cluster_high_water(),
              button_event_cluster_submitted(),
              diag.gpio_rearm_limit_hits, network_diag.network_transitions,
              network_diag.network_losses, network_diag.network_joins,
              network_diag.steering_attempts, network_diag.announce_attempts,
              network_diag.announce_failures, network_diag.uptime_ms,
              network_diag.last_transition_ms);
    return 0;
}

static void print_graph_usage(const char *prefix,
                              const hal_zigbee_graph_usage_t *usage) {
    printf("%s endpoints=%u server_clusters=%u client_clusters=%u attributes=%u "
           "zcl_registrations=%u\n", prefix, usage->endpoints,
           usage->server_clusters, usage->client_clusters, usage->attributes,
           usage->zcl_registrations);
}

/* Resources of the registered graph and of the stored config's preflight,
 * next to the shared platform limits they are checked against. */
static int cmd_zcl_graph_usage(int argc, char **argv) {
    hal_zigbee_graph_usage_t actual    = stub_zigbee_get_graph_usage();
    hal_zigbee_graph_usage_t preflight = { 0 };
    device_composition_t     composition;
    int preflight_ok;

    (void)argc;
    (void)argv;
    preflight_ok = config_parser_parse(device_config_service_config(),
                                       &composition) == CONFIG_PARSE_OK;
    if (preflight_ok) {
        feature_wiring_graph_usage(&composition, &preflight);
    }
    print_graph_usage("actual", &actual);
    print_graph_usage("preflight", &preflight);
    io_res_ok("endpoints=%u server_clusters=%u client_clusters=%u attributes=%u "
              "zcl_registrations=%u preflight_parsed=%d "
              "preflight_endpoints=%u preflight_server_clusters=%u "
              "preflight_client_clusters=%u preflight_attributes=%u "
              "preflight_zcl_registrations=%u max_endpoints=%u "
              "max_server_clusters=%u max_client_clusters=%u max_clusters=%u "
              "max_attributes=%u max_zcl_registrations=%u",
              actual.endpoints, actual.server_clusters, actual.client_clusters,
              actual.attributes, actual.zcl_registrations, preflight_ok,
              preflight.endpoints, preflight.server_clusters,
              preflight.client_clusters, preflight.attributes,
              preflight.zcl_registrations, HAL_ZIGBEE_MAX_ENDPOINTS,
              HAL_ZIGBEE_MAX_SERVER_CLUSTERS, HAL_ZIGBEE_MAX_CLIENT_CLUSTERS,
              HAL_ZIGBEE_MAX_CLUSTERS, HAL_ZIGBEE_MAX_ATTRIBUTES,
              HAL_ZIGBEE_MAX_ZCL_REGISTRATIONS);
    return 0;
}

static int cmd_quit(int argc, char **argv) {
    (void)argc;
    (void)argv;
    g_should_exit = 1;
    io_res_ok(NULL);
    return 0;
}

static int cmd_net(int argc, char **argv) {
    if (argc != 2) {
        fprintf(stderr, "Usage: net <0|1|2>\n");
        io_res_err("usage");
        return -1;
    }
    char *e   = NULL;
    long  val = strtol(argv[1], &e, 10);
    if (*argv[1] == '\0' || *e || (val < 0 || val > 2)) {
        fprintf(stderr,
                "Value must be 0 (NOT_JOINED), 1 (JOINED), or 2 (JOINING)\n");
        io_res_err("bad_value=%s", argv[1]);
        return -1;
    }
    stub_zigbee_set_network_status((int)val);
    const char *status_str;
    switch ((hal_zigbee_network_status_t)val) {
    case HAL_ZIGBEE_NETWORK_NOT_JOINED:
        status_str = "NOT_JOINED";
        break;
    case HAL_ZIGBEE_NETWORK_JOINED:
        status_str = "JOINED";
        break;
    case HAL_ZIGBEE_NETWORK_JOINING:
        status_str = "JOINING";
        break;
    default:
        status_str = "UNKNOWN";
        break;
    }
    printf("Network status: %s\n", status_str);
    io_res_ok("joined=%ld status=%s", val, status_str);
    return 0;
}

static int cmd_tx_fail(int argc, char **argv) {
    char *        e;
    unsigned long count;

    if (argc != 2) {
        io_res_err("usage");
        return -1;
    }
    count = strtoul(argv[1], &e, 10);
    if (*argv[1] == '\0' || *e || count > UINT32_MAX) {
        io_res_err("bad_value");
        return -1;
    }
    stub_zigbee_reject_next_coordinator_sends((uint32_t)count);
    io_res_ok("count=%lu", count);
    return 0;
}

static int cmd_announce_fail(int argc, char **argv) {
    char *        e;
    unsigned long count;

    if (argc != 2) {
        io_res_err("usage");
        return -1;
    }
    count = strtoul(argv[1], &e, 10);
    if (*argv[1] == '\0' || *e || count > UINT32_MAX) {
        io_res_err("bad_value");
        return -1;
    }
    stub_zigbee_reject_next_announces((uint32_t)count);
    io_res_ok("count=%lu", count);
    return 0;
}

static int cmd_nvm_fail(int argc, char **argv) {
    char *        e;
    unsigned long count;

    if (argc != 2) {
        io_res_err("usage");
        return -1;
    }
    count = strtoul(argv[1], &e, 10);
    if (*argv[1] == '\0' || *e || count > UINT32_MAX) {
        io_res_err("bad_value");
        return -1;
    }
    stub_nvm_reject_next_writes((uint32_t)count);
    io_res_ok("count=%lu", count);
    return 0;
}

static int cmd_pin(int argc, char **argv) {
    if (argc != 3) {
        fprintf(stderr, "Usage: p <pin> <0|1>\n");
        io_res_err("usage");
        return -1;
    }
    char *e   = NULL;
    long  pin = strtol(argv[1], &e, 10);
    if (*argv[1] == '\0' || *e) {
        fprintf(stderr, "Bad pin\n");
        io_res_err("bad_pin=%s", argv[1]);
        return -1;
    }
    long val = strtol(argv[2], &e, 10);
    if (*argv[2] == '\0' || *e || (val != 0 && val != 1)) {
        fprintf(stderr, "Value must be 0 or 1\n");
        io_res_err("bad_value=%s", argv[2]);
        return -1;
    }
    stub_gpio_simulate_input((int)pin, (int)val);
    printf("Pin %ld <= %ld\n", pin, val);
    io_res_ok("pin=%ld value=%ld", pin, val);
    return 0;
}

static int cmd_zcl_list_attrs(int argc, char **argv) {
    (void)argc;
    (void)argv;
    stub_app_list_attrs();
    io_res_ok(NULL);
    return 0;
}

/* Registered descriptor as `ep:cluster:role:manufacturer` entries, where role
 * is S (server, receives commands) or C (client, sends commands). */
static int cmd_zcl_clusters(int argc, char **argv) {
    uint8_t count;
    hal_zigbee_endpoint *eps = stub_zigbee_get_endpoints(&count);
    char   out[1024];
    size_t used = 0;

    (void)argc;
    (void)argv;
    out[0] = '\0';
    for (uint8_t i = 0; i < count; i++) {
        for (uint8_t j = 0; j < eps[i].cluster_count; j++) {
            const hal_zigbee_cluster *cluster = &eps[i].clusters[j];
            int written = snprintf(out + used, sizeof(out) - used,
                                   "%s%u:0x%04X:%c:0x%04X", used ? "," : "",
                                   eps[i].endpoint, cluster->cluster_id,
                                   cluster->role == HAL_ZIGBEE_ROLE_SERVER ? 'S' : 'C',
                                   cluster->manufacturer_code);

            if (written < 0 || (size_t)written >= sizeof(out) - used) {
                io_res_err("too_many_clusters");
                return -1;
            }
            used += (size_t)written;
        }
    }
    io_res_ok("clusters=%s", out);
    return 0;
}

static int cmd_zcl_read(int argc, char **argv) {
    if (argc != 4) {
        fprintf(stderr, "Usage: zcl_read <ep:dec> <cluster:hex> <attr:hex>\n");
        io_res_err("usage");
        return -1;
    }
    uint8_t  ep;
    uint16_t cl, at;
    if (parse_u8_dec(argv[1], &ep) || parse_u16_hex(argv[2], &cl) ||
        parse_u16_hex(argv[3], &at)) {
        fprintf(stderr, "Bad args\n");
        io_res_err("bad_args");
        return -1;
    }
    hal_zigbee_attribute *attr = stub_app_find_attr(ep, cl, at);
    if (attr) {
        char        buf[256];
        const char *val_str =
            stub_app_attribute_value_to_string(attr, buf, sizeof(buf));
        stub_app_print_attribute_value(attr);
        io_res_ok("ep=%d cluster=0x%04X attr=0x%04X value=%s", ep, cl, at, val_str);
    } else {
        puts("Attribute not found");
        io_res_err("attr_not_found ep=%d cluster=0x%04X attr=0x%04X", ep, cl, at);
    }
    return 0;
}

static int cmd_zcl_write(int argc, char **argv) {
    if (argc != 5) {
        fprintf(stderr,
                "Usage: zcl_write <ep:dec> <cluster:hex> <attr:hex> <value>\n");
        io_res_err("usage");
        return -1;
    }
    uint8_t  ep;
    uint16_t cl, at;
    if (parse_u8_dec(argv[1], &ep) || parse_u16_hex(argv[2], &cl) ||
        parse_u16_hex(argv[3], &at)) {
        fprintf(stderr, "Bad args\n");
        io_res_err("bad_args");
        return -1;
    }

    // Find attribute to determine its type
    hal_zigbee_attribute *attr = stub_app_find_attr(ep, cl, at);
    if (!attr) {
        fprintf(stderr,
                "Attribute not found (ep=%u, cluster=0x%04X, attr=0x%04X)\n", ep,
                cl, at);
        io_res_err("attr_not_found ep=%u cluster=0x%04X attr=0x%04X", ep, cl, at);
        return -1;
    }
    if ((attr->flag & HAL_ATTR_WRITE) == 0) {
        io_res_err("read_only ep=%u cluster=0x%04X attr=0x%04X", ep, cl, at);
        return -1;
    }

    /* hal_zigbee_attribute.size is uint8_t, so every attribute fits. */
    uint8_t candidate[UINT8_MAX + 1];
    hal_zigbee_attribute candidate_attr = *attr;

    memcpy(candidate, attr->value, attr->size);
    candidate_attr.value = candidate;
    int ret = stub_app_string_to_attribute_value(&candidate_attr, argv[4]);
    if (ret != 0) {
        fprintf(stderr, "Failed to parse value for attribute (code %d)\n", ret);
        io_res_err("bad_value ep=%u cluster=0x%04X attr=0x%04X", ep, cl, at);
        return -1;
    }

    if (stub_simulate_zigbee_attribute_write(
            ep, cl, at, candidate,
            hal_zigbee_type_is_string(attr->data_type_id) ?
            hal_zigbee_attr_encoded_size(attr->data_type_id, candidate, attr->size) :
            attr->size) !=
        HAL_ZIGBEE_CMD_PROCESSED) {
        io_res_err("rejected ep=%u cluster=0x%04X attr=0x%04X", ep, cl, at);
        return -1;
    }
    io_res_ok("ep=%d cluster=0x%04X attr=0x%04X value=%s", ep, cl, at, argv[4]);

    return 0;
}

static int cmd_zcl_write_raw(int argc, char **argv) {
    uint8_t  ep;
    uint16_t cl;
    uint16_t at;
    uint16_t candidate_len;
    uint8_t  candidate[256];
    hal_zigbee_attribute *attr;

    if (argc != 5 || parse_u8_dec(argv[1], &ep) ||
        parse_u16_hex(argv[2], &cl) || parse_u16_hex(argv[3], &at) ||
        parse_hex_bytes(argv[4], candidate, sizeof(candidate), &candidate_len)) {
        io_res_err("bad_args");
        return -1;
    }
    attr = stub_app_find_attr(ep, cl, at);
    if (attr == NULL) {
        io_res_err("attr_not_found ep=%u cluster=0x%04X attr=0x%04X", ep, cl, at);
        return -1;
    }
    if (stub_simulate_zigbee_attribute_write(ep, cl, at, candidate, candidate_len) !=
        HAL_ZIGBEE_CMD_PROCESSED) {
        io_res_err("rejected ep=%u cluster=0x%04X attr=0x%04X", ep, cl, at);
        return -1;
    }
    io_res_ok("ep=%u cluster=0x%04X attr=0x%04X", ep, cl, at);
    return 0;
}

/* zcl_write_undivided <ep> <cluster> <attr>=<hex bytes> ... */
static int cmd_zcl_write_undivided(int argc, char **argv) {
    uint8_t        ep;
    uint16_t       cl;
    uint8_t        count = (uint8_t)(argc - 3);
    uint16_t       attribute_ids[STUB_ZIGBEE_MAX_WRITE_RECORDS];
    uint8_t        values[STUB_ZIGBEE_MAX_WRITE_RECORDS][64];
    const uint8_t *candidates[STUB_ZIGBEE_MAX_WRITE_RECORDS];
    uint16_t       candidate_lens[STUB_ZIGBEE_MAX_WRITE_RECORDS];

    if (argc < 4 || argc - 3 > STUB_ZIGBEE_MAX_WRITE_RECORDS ||
        parse_u8_dec(argv[1], &ep) || parse_u16_hex(argv[2], &cl)) {
        io_res_err("usage");
        return -1;
    }
    for (uint8_t i = 0; i < count; i++) {
        char *separator = strchr(argv[3 + i], '=');

        if (separator == NULL) {
            io_res_err("bad_args");
            return -1;
        }
        *separator = '\0';
        if (parse_u16_hex(argv[3 + i], &attribute_ids[i]) ||
            parse_hex_bytes(separator + 1, values[i], sizeof(values[i]),
                            &candidate_lens[i])) {
            io_res_err("bad_args");
            return -1;
        }
        candidates[i] = values[i];
    }
    if (stub_simulate_zigbee_attribute_write_undivided(
            ep, cl, count, attribute_ids, candidates, candidate_lens) !=
        HAL_ZIGBEE_CMD_PROCESSED) {
        io_res_err("rejected ep=%u cluster=0x%04X", ep, cl);
        return -1;
    }
    io_res_ok("ep=%u cluster=0x%04X records=%u", ep, cl, count);
    return 0;
}

static int cmd_zcl_attr_callback(int argc, char **argv) {
    uint8_t  ep;
    uint16_t cl;
    uint16_t at;

    if (argc != 4 || parse_u8_dec(argv[1], &ep) ||
        parse_u16_hex(argv[2], &cl) || parse_u16_hex(argv[3], &at)) {
        io_res_err("usage");
        return -1;
    }
    hal_zigbee_attribute *attr = stub_app_find_attr(ep, cl, at);
    if (attr != NULL)
        (void)stub_simulate_zigbee_attribute_write(
            ep, cl, at, attr->value,
            hal_zigbee_type_is_string(attr->data_type_id) ?
            hal_zigbee_attr_encoded_size(attr->data_type_id, attr->value, attr->size) :
            attr->size);
    io_res_ok("ep=%u cluster=0x%04X attr=0x%04X", ep, cl, at);
    return 0;
}

static int cmd_zcl_configure_reporting(int argc, char **argv) {
    uint8_t  ep;
    uint16_t cl;
    uint16_t at;
    stub_zigbee_report_config_status_t status;

    if (argc != 4 || parse_u8_dec(argv[1], &ep) ||
        parse_u16_hex(argv[2], &cl) || parse_u16_hex(argv[3], &at)) {
        io_res_err("usage");
        return -1;
    }
    status = stub_zigbee_configure_reporting(ep, cl, at);
    if (status == STUB_ZIGBEE_REPORT_CONFIG_UNSUPPORTED_ATTRIBUTE) {
        io_res_err("unsupported_attribute ep=%u cluster=0x%04X attr=0x%04X", ep, cl, at);
        return -1;
    }
    io_res_ok("ep=%u cluster=0x%04X attr=0x%04X", ep, cl, at);
    return 0;
}

static const char *cmd_result_name(hal_zigbee_cmd_result_t result) {
    switch (result) {
    case HAL_ZIGBEE_CMD_PROCESSED:    return "PROCESSED";

    case HAL_ZIGBEE_INVALID_VALUE:    return "INVALID_VALUE";

    case HAL_ZIGBEE_MALFORMED_COMMAND: return "MALFORMED_COMMAND";

    case HAL_ZIGBEE_ACTION_DENIED:    return "ACTION_DENIED";

    case HAL_ZIGBEE_CMD_SKIPPED:      return "SKIPPED";

    default:                          return "UNKNOWN";
    }
}

/* zcl_foundation <ep> <cluster:hex> <cmd:hex> [payload hex] */
static int cmd_zcl_foundation(int argc, char **argv) {
    uint8_t  ep;
    uint16_t cl;
    uint16_t command_id;
    uint8_t  payload[256];
    uint16_t payload_len = 0;
    hal_zigbee_cmd_result_t result;

    if ((argc != 4 && argc != 5) || parse_u8_dec(argv[1], &ep) ||
        parse_u16_hex(argv[2], &cl) || parse_u16_hex(argv[3], &command_id) ||
        command_id > UINT8_MAX ||
        (argc == 5 && parse_hex_bytes(argv[4], payload, sizeof(payload), &payload_len))) {
        io_res_err("usage");
        return -1;
    }
    result = stub_zigbee_foundation_frame(ep, cl, (uint8_t)command_id,
                                          payload_len > 0 ? payload : NULL, payload_len);
    io_res_ok("ep=%u cluster=0x%04X cmd=0x%02X result=%s payload_len=%u", ep, cl,
              command_id, cmd_result_name(result), payload_len);
    return 0;
}

static int cmd_read_pin(int argc, char **argv) {
    if (argc != 2) {
        fprintf(stderr, "Usage: read_pin <pin>\n");
        io_res_err("usage");
        return -1;
    }
    char *e   = NULL;
    int   pin = strtol(argv[1], &e, 10);
    if (*argv[1] == '\0' || *e) {
        fprintf(stderr, "Bad pin\n");
        io_res_err("bad_pin=%s", argv[1]);
        return -1;
    }
    int val = stub_gpio_get_output(pin);
    printf("Pin %d => %d\n", pin, val);
    io_res_ok("pin=%d value=%d", pin, val);
    return 0;
}

static int cmd_zcl_cmd_impl(int argc, char **argv, bool trigger_activity) {
    if (argc < 4) {
        fprintf(stderr, "Usage: zcl_cmd <ep:dec> <cluster:hex> <cmd:hex> "
                "[payload:hex_bytes...]\n");
        io_res_err("usage");
        return -1;
    }

    uint8_t  ep;
    uint16_t cluster, cmd_id;

    if (parse_u8_dec(argv[1], &ep) || parse_u16_hex(argv[2], &cluster) ||
        parse_u16_hex(argv[3], &cmd_id)) {
        fprintf(stderr, "Bad args\n");
        io_res_err("bad_args");
        return -1;
    }

    // Parse optional payload bytes
    uint8_t payload[64] = { 0 }; // Max payload size
    int     payload_len = 0;

    for (int i = 4; i < argc && payload_len < sizeof(payload); i++) {
        char *e        = NULL;
        long  byte_val = strtol(argv[i], &e, 16);
        if (*argv[i] == '\0' || *e || byte_val < 0 || byte_val > 0xFF) {
            fprintf(stderr, "Bad payload byte: %s\n", argv[i]);
            io_res_err("bad_payload_byte=%s", argv[i]);
            return -1;
        }
        payload[payload_len++] = (uint8_t)byte_val;
    }

    hal_zigbee_cmd_result_t result;
    if (trigger_activity) {
        result = stub_zigbee_simulate_command(ep, cluster, cmd_id,
                                              payload_len > 0 ? payload : NULL,
                                              payload_len);
    } else {
        result = stub_zigbee_simulate_command_without_activity(
            ep, cluster, cmd_id, payload_len > 0 ? payload : NULL,
            payload_len);
    }

    const char *result_str = cmd_result_name(result);

    printf("ZCL command result: %s (ep=%d, cluster=0x%04X, cmd=0x%02X, "
           "payload_len=%d)\n",
           result_str, ep, cluster, cmd_id, payload_len);
    io_res_ok("ep=%d cluster=0x%04X cmd=0x%02X result=%s payload_len=%d", ep,
              cluster, cmd_id, result_str, payload_len);

    return 0;
}

static int cmd_zcl_cmd(int argc, char **argv) {
    return cmd_zcl_cmd_impl(argc, argv, true);
}

static int cmd_zcl_cmd_no_activity(int argc, char **argv) {
    return cmd_zcl_cmd_impl(argc, argv, false);
}

static int cmd_freeze_time(int argc, char **argv) {
    if (argc != 2) {
        fprintf(stderr, "Usage: freeze_time <0|1>\n");
        io_res_err("usage");
        return -1;
    }
    int freeze = atoi(argv[1]);
    if (freeze != 0 && freeze != 1) {
        fprintf(stderr, "Invalid argument: %s\n", argv[1]);
        io_res_err("bad_args");
        return -1;
    }
    if (freeze) {
        stub_millis_freeze();
    } else {
        stub_millis_unfreeze();
    }
    io_res_ok("freeze_time=%d", freeze);
    return 0;
}

static int cmd_step_time(int argc, char **argv) {
    if (argc != 2) {
        fprintf(stderr, "Usage: step_time <milliseconds>\n");
        io_res_err("usage");
        return -1;
    }
    char *e    = NULL;
    long  step = strtol(argv[1], &e, 10);
    if (*argv[1] == '\0' || *e || step < 0) {
        fprintf(stderr, "Bad step value: %s\n", argv[1]);
        io_res_err("bad_step=%s", argv[1]);
        return -1;
    }
    stub_millis_step((uint64_t)step);
    stub_tasks_resume();
    io_res_ok("stepped_ms=%ld", step);
    return 0;
}

static int cmd_time_advance(int argc, char **argv) {
    char *e       = NULL;
    long  advance = argc == 2 ? strtol(argv[1], &e, 10) : -1;

    if (argc != 2 || *argv[1] == '\0' || *e || advance < 0) {
        io_res_err("usage");
        return -1;
    }
    stub_tasks_pause();
    stub_millis_step((uint64_t)advance);
    io_res_ok("advanced_ms=%ld", advance);
    return 0;
}

static int cmd_tasks_poll(int argc, char **argv) {
    (void)argc;
    (void)argv;
    stub_tasks_resume();
    stub_tasks_poll();
    io_res_ok(NULL);
    return 0;
}

static int cmd_pin_edge(int argc, char **argv) {
    char *e = NULL;
    long  pin;
    long  value;
    long  after_ms = 0;

    if (argc != 3 && argc != 4) {
        io_res_err("usage");
        return -1;
    }
    pin = strtol(argv[1], &e, 10);
    if (*argv[1] == '\0' || *e || pin < 0 || pin > 255) {
        io_res_err("bad_pin=%s", argv[1]);
        return -1;
    }
    value = strtol(argv[2], &e, 10);
    if (*argv[2] == '\0' || *e || (value != 0 && value != 1)) {
        io_res_err("bad_value=%s", argv[2]);
        return -1;
    }
    if (argc == 4) {
        after_ms = strtol(argv[3], &e, 10);
        if (*argv[3] == '\0' || *e || after_ms < 0) {
            io_res_err("bad_after_ms=%s", argv[3]);
            return -1;
        }
    }
    stub_tasks_pause();
    stub_millis_step((uint64_t)after_ms);
    stub_gpio_simulate_input((hal_gpio_pin_t)pin, (uint8_t)value);
    io_res_ok("pin=%ld value=%ld t=%u", pin, value, hal_millis());
    return 0;
}

static int cmd_set_battery_voltage(int argc, char **argv) {
    if (argc != 2) {
        fprintf(stderr, "Usage: set_battery_voltage <millivolts>\n");
        io_res_err("usage");
        return -1;
    }
    char *e   = NULL;
    long  val = strtol(argv[1], &e, 10);
    if (*argv[1] == '\0' || *e || val < 0 || val > 0xFFFF) {
        fprintf(stderr, "Bad voltage value: %s\n", argv[1]);
        io_res_err("bad_value=%s", argv[1]);
        return -1;
    }
    stub_set_battery_voltage_mv((uint16_t)val);
    printf("Battery voltage set to %ld mV\n", val);
    io_res_ok("voltage_mv=%ld", val);
    return 0;
}

/* Input pipeline and timing fidelity commands. */

static int parse_ulong_arg(const char *text, unsigned long long max,
                           unsigned long long *out) {
    char *end = NULL;
    unsigned long long value;

    if (*text == '\0' || *text == '-') {
        return -1;
    }
    value = strtoull(text, &end, 0);
    if (*end != '\0' || value > max) {
        return -1;
    }
    *out = value;
    return 0;
}

static int cmd_time_model(int argc, char **argv) {
    if (argc != 2) {
        io_res_err("usage");
        return -1;
    }
    if (strcmp(argv[1], "native") == 0) {
        stub_millis_set_model(STUB_TIME_MODEL_NATIVE);
    } else if (strcmp(argv[1], "telink") == 0) {
        stub_millis_set_model(STUB_TIME_MODEL_TELINK);
    } else {
        io_res_err("bad_model=%s", argv[1]);
        return -1;
    }
    io_res_ok("model=%s millis=%u", argv[1], hal_millis());
    return 0;
}

static int cmd_time_set(int argc, char **argv) {
    unsigned long long virtual_ms;

    if (argc != 2 || parse_ulong_arg(argv[1], UINT64_MAX / 16000u,
                                     &virtual_ms) != 0) {
        io_res_err("usage");
        return -1;
    }
    if (stub_millis_set((uint64_t)virtual_ms) != 0) {
        io_res_err("not_frozen_or_backwards");
        return -1;
    }
    io_res_ok("virtual_ms=%llu millis=%u", virtual_ms, hal_millis());
    return 0;
}

static int cmd_pin_burst(int argc, char **argv) {
    unsigned long long pin;
    unsigned long long count;

    if (argc != 3 || parse_ulong_arg(argv[1], 255, &pin) != 0 ||
        parse_ulong_arg(argv[2], 1000, &count) != 0) {
        io_res_err("usage");
        return -1;
    }
    stub_gpio_burst((hal_gpio_pin_t)pin, (uint32_t)count);
    io_res_ok("pin=%llu count=%llu", pin, count);
    return 0;
}

static int cmd_pin_late(int argc, char **argv) {
    unsigned long long pin;
    unsigned long long value;
    unsigned long long lag_ms;

    if (argc != 4 || parse_ulong_arg(argv[1], 255, &pin) != 0 ||
        parse_ulong_arg(argv[2], 1, &value) != 0 ||
        parse_ulong_arg(argv[3], 60000, &lag_ms) != 0) {
        io_res_err("usage");
        return -1;
    }
    if (stub_millis_set(stub_millis_virtual()) != 0) {
        io_res_err("not_frozen");
        return -1;
    }
    stub_gpio_simulate_late_input((hal_gpio_pin_t)pin, (uint8_t)value,
                                  (uint32_t)lag_ms);
    io_res_ok("pin=%llu value=%llu t=%u", pin, value, hal_millis());
    return 0;
}

static int cmd_task_fail(int argc, char **argv) {
    unsigned long long count;

    if (argc != 2 || parse_ulong_arg(argv[1], UINT32_MAX, &count) != 0) {
        io_res_err("usage");
        return -1;
    }
    stub_tasks_reject_next((uint32_t)count);
    io_res_ok("count=%llu", count);
    return 0;
}

static int cmd_input_diag(int argc, char **argv) {
    (void)argc;
    (void)argv;
    io_res_ok("button_schedule_failures=%u timer_schedule_failures=%u "
              "gpio_edges_dropped=%u zb_button_event_schedule_failures=%u",
              button_input_schedule_failures(), timer_schedule_failures(),
              button_input_gpio_edges_dropped(),
              button_event_cluster_schedule_failures());
    return 0;
}

#if FEATURE_PROBE
/* probe_pin <pin> float | high | low | train <period_us> <high_us> */
static int cmd_probe_pin(int argc, char **argv) {
    hal_gpio_probe_id_t id;
    unsigned long long  period_us;
    unsigned long long  high_us;
    bool ok = false;

    if (argc < 3 || !stub_gpio_probe_parse(argv[1], &id)) {
        io_res_err("usage");
        return -1;
    }
    if (argc == 3 && strcmp(argv[2], "float") == 0) {
        ok = stub_gpio_probe_set_floating(id);
    } else if (argc == 3 && strcmp(argv[2], "high") == 0) {
        ok = stub_gpio_probe_set_driven(id, 1);
    } else if (argc == 3 && strcmp(argv[2], "low") == 0) {
        ok = stub_gpio_probe_set_driven(id, 0);
    } else if (argc == 5 && strcmp(argv[2], "train") == 0 &&
               parse_ulong_arg(argv[3], UINT32_MAX, &period_us) == 0 &&
               parse_ulong_arg(argv[4], UINT32_MAX, &high_us) == 0) {
        ok = stub_gpio_probe_set_train(id, (uint32_t)period_us, (uint32_t)high_us);
    }
    if (!ok) {
        io_res_err("usage");
        return -1;
    }
    io_res_ok("pin=%s", argv[1]);
    return 0;
}

static int cmd_probe_pin_info(int argc, char **argv) {
    hal_gpio_probe_id_t   id;
    stub_probe_pin_info_t info;

    if (argc != 2 || !stub_gpio_probe_parse(argv[1], &id) ||
        !stub_gpio_probe_info(id, &info)) {
        io_res_err("usage");
        return -1;
    }
    io_res_ok("pin=%s level=%u output=%u output_level=%u pull=%u drives=%u "
              "contention=%u", argv[1], info.level, info.output_enabled,
              info.output_level, info.pull, info.drives, info.contention);
    return 0;
}

#endif

/* Command table */
static const SimpleReplCommand kCmds[] = {
    { "machine",                 cmd_machine                 },
    { "help",                    cmd_help                    },
    { "s",                       cmd_status                  },
    { "status",                  cmd_status                  },
    { "diag",                    cmd_diag                    },
    { "net",                     cmd_net                     },
    { "tx_fail",                 cmd_tx_fail                 },
    { "announce_fail",           cmd_announce_fail           },
    { "nvm_fail",                cmd_nvm_fail                },
    { "set_pin",                 cmd_pin                     },
    { "pin_edge",                cmd_pin_edge                },
    { "read_pin",                cmd_read_pin                },
    { "zcl_read",                cmd_zcl_read                },
    { "zcl_write",               cmd_zcl_write               },
    { "zcl_write_raw",           cmd_zcl_write_raw           },
    { "zcl_write_undivided",     cmd_zcl_write_undivided     },
    { "zcl_attr_callback",       cmd_zcl_attr_callback       },
    { "zcl_list_attrs",          cmd_zcl_list_attrs          },
    { "zcl_graph_usage",         cmd_zcl_graph_usage         },
    { "zcl_clusters",            cmd_zcl_clusters            },
    { "zcl_configure_reporting", cmd_zcl_configure_reporting },
    { "zcl_foundation",          cmd_zcl_foundation          },
    { "zcl_cmd",                 cmd_zcl_cmd                 },
    { "zcl_cmd_no_activity",     cmd_zcl_cmd_no_activity     },
    { "freeze_time",             cmd_freeze_time             },
    { "step_time",               cmd_step_time               },
    { "time_advance",            cmd_time_advance            },
    { "tasks_poll",              cmd_tasks_poll              },
    { "set_battery_voltage",     cmd_set_battery_voltage     },
    { "time_model",              cmd_time_model              },
    { "time_set",                cmd_time_set                },
    { "pin_burst",               cmd_pin_burst               },
    { "pin_late",                cmd_pin_late                },
    { "task_fail",               cmd_task_fail               },
    { "input_diag",              cmd_input_diag              },
#if FEATURE_PROBE
    { "probe_pin",               cmd_probe_pin               },
    { "probe_pin_info",          cmd_probe_pin_info          },
#endif
    { "q",                       cmd_quit                    },
    { "quit",                    cmd_quit                    },
};

const SimpleReplCommand *commands_table(void) {
    return kCmds;
}

size_t commands_count(void) {
    return sizeof(kCmds) / sizeof(kCmds[0]);
}

void commands_print_help(void) {
    stub_app_print_help();
}
