#include "basic_cluster.h"
#include "app.h"
#include "base_components/button_input.h"
#include "base_components/gesture_fsm.h"
#include "base_components/network_indicator.h"
#include "build_date.h"
#include "cluster_common.h"
#include "consts.h"
#include "device_config/config_nv.h"
#include "device_config/feature_wiring.h"
#include "device_config/device_params_nv.h"
#include "device_config/nvm_items.h"
#include "device_config/reset.h"
#include "hal/nvm.h"
#include "hal/gpio.h"
#include "zigbee/button_event_cluster.h"
#include <stddef.h>

#ifdef HAL_SILABS
#include "silabs_config.h"
#endif

const uint8_t zclVersion   = 0x03;
const uint8_t appVersion   = 0x03;
const uint8_t stackVersion = 0x02;
const uint8_t hwVersion    = 0x00;

// Power source - set at runtime based on battery config
uint8_t powerSource = POWER_SOURCE_MAINS_1_PHASE; // 0x01 default

const uint16_t cluster_revision = 0x01;
DEF_STR(STRINGIFY_VALUE(VERSION_STR), swBuildId);
extern network_indicator_t network_indicator;

static uint32_t gpio_edges_captured;
static uint32_t gpio_edges_dropped;
static uint32_t button_events_emitted;
static uint32_t gestures_emitted;
static uint32_t zb_button_events_dropped;
static uint32_t gpio_rearm_limit_hits;
static uint32_t zb_button_events_expired;
static uint32_t zb_button_events_send_failed;
static uint32_t zb_button_events_high_water;
static uint32_t zb_button_events_submitted;
static uint32_t network_transitions;
static uint32_t network_losses;
static uint32_t network_joins;
static uint32_t announce_attempts;
static uint32_t announce_failures;
static uint32_t steering_attempts;
static uint32_t uptime_ms;
static uint32_t descriptor_validation_failures;
static uint8_t  last_descriptor_error;

void basic_cluster_store_attrs_to_nv();
void basic_cluster_load_attrs_from_nv();

void basic_cluster_callback_attr_write_trampoline(uint16_t attribute_id) {
    basic_cluster_store_attrs_to_nv();
    if (attribute_id == ZCL_ATTR_BASIC_DEVICE_CONFIG) {
        device_config_str.data[device_config_str.size] =
            0;              // NULL terminate the string
        device_config_write_to_nv();
        schedule_reboot(0); // Use default delay
    }
    if (attribute_id == ZCL_ATTR_BASIC_STATUS_LED_STATE) {
        network_indicator_from_manual_state(&network_indicator);
    }
    if (attribute_id == ZCL_ATTR_BASIC_MULTI_PRESS_RESET_COUNT) {
        device_params_set_multi_press_reset_count(g_multi_press_reset_count);
    }
}

void basic_cluster_add_to_endpoint(zigbee_basic_cluster *cluster,
                                   hal_zigbee_endpoint *endpoint) {
    uint8_t attr_index = 13;

    if (!hal_zigbee_endpoint_reserve_clusters(endpoint,
                                              endpoint->cluster_capacity, 1)) {
        return;
    }

    // Set power source based on runtime battery configuration
    if (battery.pin != HAL_INVALID_PIN) {
        powerSource = POWER_SOURCE_BATTERY;
    }

    // Initialize build date buffer
    zb_build_date_init(ZB_BUILD_DATE_YYYYMMDD);

    // Fill Attrs

    SETUP_ATTR(0, ZCL_ATTR_BASIC_ZCL_VER, ZCL_DATA_TYPE_UINT8, ATTR_READONLY,
               zclVersion);

    SETUP_ATTR(1, ZCL_ATTR_BASIC_APP_VER, ZCL_DATA_TYPE_UINT8, ATTR_READONLY,
               appVersion);
    SETUP_ATTR(2, ZCL_ATTR_BASIC_STACK_VER, ZCL_DATA_TYPE_UINT8, ATTR_READONLY,
               stackVersion);
    SETUP_ATTR(3, ZCL_ATTR_BASIC_HW_VER, ZCL_DATA_TYPE_UINT8, ATTR_READONLY,
               hwVersion);
    SETUP_ATTR(4, ZCL_ATTR_BASIC_MFR_NAME, ZCL_DATA_TYPE_CHAR_STR, ATTR_READONLY,
               cluster->manuName);
    SETUP_ATTR(5, ZCL_ATTR_BASIC_MODEL_ID, ZCL_DATA_TYPE_CHAR_STR, ATTR_READONLY,
               cluster->modelId);
    SETUP_ATTR(6, ZCL_ATTR_BASIC_POWER_SOURCE, ZCL_DATA_TYPE_ENUM8, ATTR_READONLY,
               powerSource);
    SETUP_ATTR(7, ZCL_ATTR_BASIC_DEV_ENABLED, ZCL_DATA_TYPE_BOOLEAN,
               ATTR_WRITABLE, cluster->deviceEnable);
    SETUP_ATTR(8, ZCL_ATTR_BASIC_SW_BUILD_ID, ZCL_DATA_TYPE_CHAR_STR,
               ATTR_READONLY, swBuildId);
    SETUP_ATTR(9, ZCL_ATTR_BASIC_DATE_CODE, ZCL_DATA_TYPE_CHAR_STR, ATTR_READONLY,
               ZB_BUILD_DATE_YYYYMMDD);
    SETUP_ATTR(10, ZCL_ATTR_GLOBAL_CLUSTER_REVISION, ZCL_DATA_TYPE_UINT16,
               ATTR_READONLY, cluster_revision);
    SETUP_ATTR(11, ZCL_ATTR_BASIC_DEVICE_CONFIG, ZCL_DATA_TYPE_LONG_CHAR_STR,
               ATTR_WRITABLE, device_config_str);
    SETUP_ATTR(12, ZCL_ATTR_BASIC_MULTI_PRESS_RESET_COUNT, ZCL_DATA_TYPE_UINT8,
               ATTR_WRITABLE, g_multi_press_reset_count);
    if (network_indicator.has_dedicated_led) {
        SETUP_ATTR_FOR_TABLE(
            cluster->attr_infos, attr_index, ZCL_ATTR_BASIC_STATUS_LED_STATE,
            ZCL_DATA_TYPE_BOOLEAN, ATTR_WRITABLE,
            network_indicator.manual_state_when_connected);
        attr_index++;
    }
    SETUP_ATTR_FOR_TABLE(cluster->attr_infos, attr_index,
                         ZCL_ATTR_BASIC_GPIO_EDGES_CAPTURED,
                         ZCL_DATA_TYPE_UINT32, ATTR_READONLY,
                         gpio_edges_captured);
    attr_index++;
    SETUP_ATTR_FOR_TABLE(cluster->attr_infos, attr_index,
                         ZCL_ATTR_BASIC_GPIO_EDGES_DROPPED,
                         ZCL_DATA_TYPE_UINT32, ATTR_READONLY,
                         gpio_edges_dropped);
    attr_index++;
    SETUP_ATTR_FOR_TABLE(cluster->attr_infos, attr_index,
                         ZCL_ATTR_BASIC_BUTTON_EVENTS_EMITTED,
                         ZCL_DATA_TYPE_UINT32, ATTR_READONLY,
                         button_events_emitted);
    attr_index++;
    SETUP_ATTR_FOR_TABLE(cluster->attr_infos, attr_index,
                         ZCL_ATTR_BASIC_GESTURES_EMITTED,
                         ZCL_DATA_TYPE_UINT32, ATTR_READONLY,
                         gestures_emitted);
    attr_index++;
    SETUP_ATTR_FOR_TABLE(cluster->attr_infos, attr_index,
                         ZCL_ATTR_BASIC_ZB_BUTTON_EVENTS_DROPPED,
                         ZCL_DATA_TYPE_UINT32, ATTR_READONLY,
                         zb_button_events_dropped);
    attr_index++;
    SETUP_ATTR_FOR_TABLE(cluster->attr_infos, attr_index,
                         ZCL_ATTR_BASIC_GPIO_REARM_LIMIT_HITS,
                         ZCL_DATA_TYPE_UINT32, ATTR_READONLY,
                         gpio_rearm_limit_hits);
    attr_index++;
    SETUP_ATTR_FOR_TABLE(cluster->attr_infos, attr_index,
                         ZCL_ATTR_BASIC_ZB_BUTTON_EVENTS_EXPIRED,
                         ZCL_DATA_TYPE_UINT32, ATTR_READONLY,
                         zb_button_events_expired);
    attr_index++;
    SETUP_ATTR_FOR_TABLE(cluster->attr_infos, attr_index,
                         ZCL_ATTR_BASIC_ZB_BUTTON_EVENTS_SEND_FAILED,
                         ZCL_DATA_TYPE_UINT32, ATTR_READONLY,
                         zb_button_events_send_failed);
    attr_index++;
    SETUP_ATTR_FOR_TABLE(cluster->attr_infos, attr_index,
                         ZCL_ATTR_BASIC_ZB_BUTTON_EVENTS_HIGH_WATER,
                         ZCL_DATA_TYPE_UINT32, ATTR_READONLY,
                         zb_button_events_high_water);
    attr_index++;
    SETUP_ATTR_FOR_TABLE(cluster->attr_infos, attr_index,
                         ZCL_ATTR_BASIC_ZB_BUTTON_EVENTS_SUBMITTED,
                         ZCL_DATA_TYPE_UINT32, ATTR_READONLY,
                         zb_button_events_submitted);
    attr_index++;
    SETUP_ATTR_FOR_TABLE(cluster->attr_infos, attr_index,
                         ZCL_ATTR_BASIC_NETWORK_TRANSITIONS,
                         ZCL_DATA_TYPE_UINT32, ATTR_READONLY,
                         network_transitions);
    attr_index++;
    SETUP_ATTR_FOR_TABLE(cluster->attr_infos, attr_index,
                         ZCL_ATTR_BASIC_NETWORK_LOSSES, ZCL_DATA_TYPE_UINT32,
                         ATTR_READONLY, network_losses);
    attr_index++;
    SETUP_ATTR_FOR_TABLE(cluster->attr_infos, attr_index,
                         ZCL_ATTR_BASIC_NETWORK_JOINS, ZCL_DATA_TYPE_UINT32,
                         ATTR_READONLY, network_joins);
    attr_index++;
    SETUP_ATTR_FOR_TABLE(cluster->attr_infos, attr_index,
                         ZCL_ATTR_BASIC_ANNOUNCE_ATTEMPTS, ZCL_DATA_TYPE_UINT32,
                         ATTR_READONLY, announce_attempts);
    attr_index++;
    SETUP_ATTR_FOR_TABLE(cluster->attr_infos, attr_index,
                         ZCL_ATTR_BASIC_ANNOUNCE_FAILURES, ZCL_DATA_TYPE_UINT32,
                         ATTR_READONLY, announce_failures);
    attr_index++;
    SETUP_ATTR_FOR_TABLE(cluster->attr_infos, attr_index,
                         ZCL_ATTR_BASIC_STEERING_ATTEMPTS, ZCL_DATA_TYPE_UINT32,
                         ATTR_READONLY, steering_attempts);
    attr_index++;
    SETUP_ATTR_FOR_TABLE(cluster->attr_infos, attr_index,
                         ZCL_ATTR_BASIC_UPTIME_MS, ZCL_DATA_TYPE_UINT32,
                         ATTR_READONLY, uptime_ms);
    attr_index++;
    SETUP_ATTR_FOR_TABLE(cluster->attr_infos, attr_index,
                         ZCL_ATTR_BASIC_DESCRIPTOR_VALIDATION_FAILURES,
                         ZCL_DATA_TYPE_UINT32, ATTR_READONLY,
                         descriptor_validation_failures);
    attr_index++;
    SETUP_ATTR_FOR_TABLE(cluster->attr_infos, attr_index,
                         ZCL_ATTR_BASIC_LAST_DESCRIPTOR_ERROR,
                         ZCL_DATA_TYPE_ENUM8, ATTR_READONLY,
                         last_descriptor_error);
    attr_index++;

    hal_zigbee_cluster endpoint_cluster = {
        .cluster_id      = ZCL_CLUSTER_BASIC,
        .is_server       =                   1,
        .attribute_count = attr_index,
        .attributes      = cluster->attr_infos,
    };

    hal_zigbee_endpoint_add_cluster(endpoint, endpoint->cluster_capacity,
                                    &endpoint_cluster);

    device_params_load_from_nv();
    basic_cluster_load_attrs_from_nv();
    if (hal_zigbee_get_network_status() == HAL_ZIGBEE_NETWORK_JOINED &&
        network_indicator.has_dedicated_led) {
        network_indicator_from_manual_state(&network_indicator);
    }
}

void basic_cluster_update_diagnostics(void) {
    hal_gpio_diagnostics_t    gpio_diagnostics   = hal_gpio_get_diagnostics();
    app_network_diagnostics_t app_diagnostics    = app_network_get_diagnostics();
    hal_zigbee_diagnostics_t  zigbee_diagnostics = hal_zigbee_get_diagnostics();

    gpio_edges_captured          = gpio_diagnostics.gpio_edges_captured;
    gpio_edges_dropped           = button_input_gpio_edges_dropped();
    button_events_emitted        = button_input_events_emitted();
    gestures_emitted             = gesture_fsm_events_emitted();
    zb_button_events_dropped     = button_event_cluster_dropped();
    gpio_rearm_limit_hits        = gpio_diagnostics.gpio_rearm_limit_hits;
    zb_button_events_expired     = button_event_cluster_expired();
    zb_button_events_send_failed = button_event_cluster_send_failed();
    zb_button_events_high_water  = button_event_cluster_high_water();
    zb_button_events_submitted   = button_event_cluster_submitted();
    network_transitions          = app_diagnostics.network_transitions;
    network_losses    = app_diagnostics.network_losses;
    network_joins     = app_diagnostics.network_joins;
    announce_attempts = app_diagnostics.announce_attempts;
    announce_failures = app_diagnostics.announce_failures;
    steering_attempts = app_diagnostics.steering_attempts;
    uptime_ms         = app_diagnostics.uptime_ms;
    descriptor_validation_failures =
        zigbee_diagnostics.descriptor_validation_failures;
    last_descriptor_error = zigbee_diagnostics.last_descriptor_error;
}

typedef struct {
    uint8_t network_led_on;
} zigbee_basic_cluster_config;

static zigbee_basic_cluster_config nv_config_buffer;

void basic_cluster_store_attrs_to_nv() {
    nv_config_buffer.network_led_on =
        network_indicator.manual_state_when_connected;

    hal_nvm_write(NV_ITEM_BASIC_CLUSTER_DATA, sizeof(zigbee_basic_cluster_config),
                  (uint8_t *)&nv_config_buffer);
}

void basic_cluster_load_attrs_from_nv() {
    hal_nvm_status_t st = hal_nvm_read(NV_ITEM_BASIC_CLUSTER_DATA,
                                       sizeof(zigbee_basic_cluster_config),
                                       (uint8_t *)&nv_config_buffer);

    if (st != HAL_NVM_SUCCESS) {
        return;
    }
    network_indicator.manual_state_when_connected =
        nv_config_buffer.network_led_on;
}
