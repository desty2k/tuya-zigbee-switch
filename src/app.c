#include "app.h"
#include "device_config/feature_wiring.h"
#include "device_config/device_config_service.h"
#include "device_config/config_nv.h"
#include "device_config/device_type.h"
#include "device_config/nvm_items.h"
#include "device_config/reset.h"
#include "base_components/button_input.h"
#include "base_components/diagnostics_service.h"
#include "device_config/build_features.h"
#if FEATURE_PROBE
#include "base_components/gpio_probe_service.h"
#endif
#include "base_components/gesture_fsm.h"
#include "base_components/relay_controller.h"
#include "hal/gpio.h"
#include "hal/nvm.h"
#include "hal/printf_selector.h"
#include "hal/system.h"
#include "hal/timer.h"
#include "hal/zigbee.h"
#include "zigbee/battery_cluster.h"
#include "zigbee/button_event_cluster.h"
#include "zigbee/device_diagnostics_cluster.h"
#ifdef END_DEVICE
#include "zigbee/poll_control_cluster.h"
#endif

void process_device_type_change() {
    // If device was updated from router to end device or vice versa,
    // we need to do a reset, as the network settings stored by SDK in NVM
    // are not compatible between these device types.
    // Read device type from NVM and compare with current configuration.
    enum device_type_t stored_device_type;
    hal_nvm_status_t   st =
        hal_nvm_read(NV_ITEM_DEVICE_TYPE, sizeof(stored_device_type),
                     (uint8_t *)&stored_device_type);

    if (st != HAL_NVM_SUCCESS) {
        // Unable to read device type from NVM, possibly first boot.
        stored_device_type = CURRENT_DEVICE_TYPE;
        hal_nvm_write(NV_ITEM_DEVICE_TYPE, sizeof(stored_device_type),
                      (uint8_t *)&stored_device_type);
        return;
    }
    if (stored_device_type != CURRENT_DEVICE_TYPE) {
        printf("Device type change detected: %d -> %d\r\n", stored_device_type,
               CURRENT_DEVICE_TYPE);
        // Device type has changed, update NVM and reset device.
        stored_device_type = CURRENT_DEVICE_TYPE;
        hal_nvm_write(NV_ITEM_DEVICE_TYPE, sizeof(stored_device_type),
                      (uint8_t *)&stored_device_type);
        // Perform a factory reset to clear incompatible network settings.
        hal_factory_reset();
        schedule_reboot(2000);
    }
}

static bool     boot_announce_sent;
static uint32_t boot_announce_retry_ms;
static bool     network_status_initialized;
static hal_zigbee_network_status_t last_network_status;
static app_network_diagnostics_t   network_diagnostics;

void app_init(void) {
    handle_version_changes();
    device_config_service_init();
    diagnostics_service_init();
    parse_config(); // Does most of the setup, including all callbacks
                    // registration

    process_device_type_change();
}

static void app_diagnostics_provider(diagnostics_snapshot_t *snapshot,
                                     void *context) {
    hal_gpio_diagnostics_t   gpio   = hal_gpio_get_diagnostics();
    hal_zigbee_diagnostics_t zigbee = hal_zigbee_get_diagnostics();

    (void)context;
    snapshot->uptime_ms                      = network_diagnostics.uptime_ms;
    snapshot->gpio_edges_captured            = gpio.gpio_edges_captured;
    snapshot->gpio_edges_dropped             = button_input_gpio_edges_dropped();
    snapshot->gpio_rearm_limit_hits          = gpio.gpio_rearm_limit_hits;
    snapshot->button_events_emitted          = button_input_events_emitted();
    snapshot->gestures_emitted               = gesture_fsm_events_emitted();
    snapshot->zb_submitted                   = button_event_cluster_submitted();
    snapshot->zb_dropped                     = button_event_cluster_dropped();
    snapshot->zb_expired                     = button_event_cluster_expired();
    snapshot->zb_send_failed                 = button_event_cluster_send_failed();
    snapshot->zb_high_water                  = button_event_cluster_high_water();
    snapshot->network_transitions            = network_diagnostics.network_transitions;
    snapshot->network_losses                 = network_diagnostics.network_losses;
    snapshot->network_joins                  = network_diagnostics.network_joins;
    snapshot->announce_attempts              = network_diagnostics.announce_attempts;
    snapshot->announce_failures              = network_diagnostics.announce_failures;
    snapshot->steering_attempts              = network_diagnostics.steering_attempts;
    snapshot->descriptor_validation_failures =
        zigbee.descriptor_validation_failures;
    snapshot->last_descriptor_error = zigbee.last_descriptor_error;
}

static void app_track_network_status(hal_zigbee_network_status_t status) {
    if (!network_status_initialized) {
        network_status_initialized = true;
        last_network_status        = status;
        return;
    }
    if (status == last_network_status) {
        return;
    }
    network_diagnostics.network_transitions++;
    network_diagnostics.last_transition_ms = hal_millis();
    if (status == HAL_ZIGBEE_NETWORK_JOINED) {
        network_diagnostics.network_joins++;
    } else if (last_network_status == HAL_ZIGBEE_NETWORK_JOINED) {
        network_diagnostics.network_losses++;
    }
    printf("Network transition %d -> %d at %u ms\r\n", last_network_status,
           status, network_diagnostics.last_transition_ms);
    last_network_status = status;
}

void app_task() {
    hal_zigbee_network_status_t network_status = hal_zigbee_get_network_status();

    hal_gpio_process_pending();
    button_input_process_pending();
    button_event_cluster_process_pending();
    relay_ctrl_process_pending();
#if FEATURE_PROBE
    gpio_probe_service_poll();
#endif
    app_track_network_status(network_status);
    network_diagnostics.uptime_ms = hal_millis();
    diagnostics_service_set_provider(app_diagnostics_provider, NULL);
    diagnostics_service_refresh();
    device_diagnostics_cluster_refresh();
#ifdef END_DEVICE
    poll_control_cluster_update();
#endif

    // TODO: add jitter to avoid all devices trying to join at once
    if (network_status != HAL_ZIGBEE_NETWORK_JOINED &&
        network_status != HAL_ZIGBEE_NETWORK_JOINING) {
        network_diagnostics.steering_attempts++;
        hal_zigbee_start_network_steering();
    }
    if (!boot_announce_sent && network_status == HAL_ZIGBEE_NETWORK_JOINED &&
        (uint32_t)(hal_millis() - boot_announce_retry_ms) < 0x80000000u) {
        network_diagnostics.announce_attempts++;
        if (hal_zigbee_send_announce() == HAL_ZIGBEE_OK) {
            boot_announce_sent = true;
        } else {
            network_diagnostics.announce_failures++;
            boot_announce_retry_ms = hal_millis() + 1000;
        }
    }
}

app_network_diagnostics_t app_network_get_diagnostics(void) {
    return network_diagnostics;
}
