#include "app.h"
#include "device_config/config_parser.h"
#include "device_config/device_type.h"
#include "device_config/nvm_items.h"
#include "device_config/reset.h"
#include "base_components/button_input.h"
#include "hal/gpio.h"
#include "hal/nvm.h"
#include "hal/printf_selector.h"
#include "hal/system.h"
#include "hal/timer.h"
#include "hal/zigbee.h"
#include "zigbee/battery_cluster.h"
#include "zigbee/basic_cluster.h"
#include "zigbee/general_commands.h"
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

void app_init(void) {
    handle_version_changes();
    parse_config(); // Does most of the setup, including all callbacks
                    // registration
    init_global_attr_write_callback();

    process_device_type_change();
}

static bool     boot_announce_sent = false;
static uint32_t boot_announce_retry_ms;
static bool     network_status_initialized;
static hal_zigbee_network_status_t last_network_status;
static app_network_diagnostics_t   network_diagnostics;

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
    app_track_network_status(network_status);
    network_diagnostics.uptime_ms = hal_millis();
    basic_cluster_update_diagnostics();
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
