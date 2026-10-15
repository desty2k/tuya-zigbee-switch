#include "base_components/meter_fake_driver.h"
#include "base_components/meter_service.h"
#include "device_config/nvm_items.h"
#include "hal/nvm.h"
#include "support/fake_clock.h"
#include "support/fake_tasks.h"
#include "support/runner.h"

#include <stdbool.h>
#include <string.h>

#define NVM_ITEM_SIZE    64

static uint8_t nvm_data[256][NVM_ITEM_SIZE];
static uint8_t nvm_size[256];
static bool nvm_write_fails;
static uint8_t notifications;

hal_nvm_status_t hal_nvm_write(uint8_t item_id, uint16_t size, uint8_t *data) {
    if (nvm_write_fails || data == NULL || size > NVM_ITEM_SIZE) {
        return HAL_NVM_ERROR;
    }
    memcpy(nvm_data[item_id], data, size);
    nvm_size[item_id] = (uint8_t)size;
    return HAL_NVM_SUCCESS;
}

hal_nvm_status_t hal_nvm_read(uint8_t item_id, uint16_t size, uint8_t *data) {
    if (data == NULL || nvm_size[item_id] != size) {
        return HAL_NVM_NOT_FOUND;
    }
    memcpy(data, nvm_data[item_id], size);
    return HAL_NVM_SUCCESS;
}

hal_nvm_status_t hal_nvm_delete(uint8_t item_id) {
    nvm_size[item_id] = 0;
    return HAL_NVM_SUCCESS;
}

hal_nvm_status_t hal_nvm_clear_all(void) {
    memset(nvm_size, 0, sizeof(nvm_size));
    nvm_write_fails = false;
    return HAL_NVM_SUCCESS;
}

static void meter_changed(uint8_t meter_id, const meter_service_snapshot_t *snapshot,
                          void *context) {
    (void)meter_id;
    (void)snapshot;
    (void)context;
    notifications++;
}

static meter_service_config_t valid_config(meter_fake_driver_t *driver) {
    return (meter_service_config_t){
        .driver = meter_fake_driver_as_meter(driver),
        .profile_calibration = {
            .voltage_gain_ppm = 1100000,
            .current_gain_ppm = 1200000,
            .power_gain_ppm = 1500000,
        },
        .sample_interval_ms = 10,
        .checkpoint_interval_ms = 100,
        .checkpoint_energy_uwh = 100,
        .nvm_item = NV_ITEM_METER_STATE(0),
    };
}

static uint8_t reset_service(meter_fake_driver_t *driver) {
    meter_service_config_t config;

    memset(nvm_data, 0, sizeof(nvm_data));
    memset(nvm_size, 0, sizeof(nvm_size));
    nvm_write_fails = false;
    notifications = 0;
    fake_clock_reset();
    fake_tasks_reset();
    meter_fake_driver_init(driver);
    meter_service_init();
    config = valid_config(driver);
    return meter_service_add(&config);
}

TEST(applies_profile_then_runtime_calibration_and_preserves_validity) {
    meter_fake_driver_t driver;
    meter_service_snapshot_t snapshot;
    meter_calibration_t runtime = {
        .voltage_gain_ppm = 1000000,
        .current_gain_ppm = 1000000,
        .power_gain_ppm = 500000,
    };
    uint8_t meter_id = reset_service(&driver);
    meter_sample_t sample = {
        .voltage_mv = 1000,
        .current_ma = 200,
        .active_power_mw = -200,
        .apparent_power_mva = 200,
        .reactive_power_mvar = -100,
        .power_factor_milli = -900,
        .energy_delta_uwh = 2,
        .valid_mask = METER_SAMPLE_VOLTAGE | METER_SAMPLE_CURRENT |
                      METER_SAMPLE_ACTIVE_POWER | METER_SAMPLE_APPARENT_POWER |
                      METER_SAMPLE_REACTIVE_POWER | METER_SAMPLE_POWER_FACTOR |
                      METER_SAMPLE_ENERGY,
    };

    ASSERT_EQ(0, meter_id);
    meter_fake_driver_set_sample(&driver, &sample);
    ASSERT_TRUE(meter_service_subscribe(meter_id, meter_changed, NULL));
    ASSERT_TRUE(meter_service_poll(meter_id));
    ASSERT_TRUE(meter_service_snapshot(meter_id, &snapshot));
    ASSERT_EQ(1100, snapshot.sample.voltage_mv);
    ASSERT_EQ(240, snapshot.sample.current_ma);
    ASSERT_EQ(-300, snapshot.sample.active_power_mw);
    ASSERT_EQ(300, snapshot.sample.apparent_power_mva);
    ASSERT_EQ(-150, snapshot.sample.reactive_power_mvar);
    ASSERT_EQ(-900, snapshot.sample.power_factor_milli);
    ASSERT_EQ(3, snapshot.accumulated_energy_uwh);
    ASSERT_EQ(1, notifications);
    ASSERT_EQ(METER_SERVICE_OK, meter_service_set_calibration(meter_id, &runtime));
    ASSERT_TRUE(meter_service_poll(meter_id));
    ASSERT_TRUE(meter_service_snapshot(meter_id, &snapshot));
    ASSERT_EQ(-100, snapshot.sample.active_power_mw);
    ASSERT_EQ(4, snapshot.accumulated_energy_uwh);
    ASSERT_TRUE(snapshot.runtime_calibration_active);
}

TEST(checkpoints_restores_and_resets_energy_with_one_owner) {
    meter_fake_driver_t driver;
    meter_service_snapshot_t snapshot;
    meter_sample_t sample = {
        .energy_delta_uwh = 100,
        .valid_mask = METER_SAMPLE_ENERGY,
    };
    uint8_t meter_id = reset_service(&driver);

    meter_fake_driver_set_sample(&driver, &sample);
    ASSERT_TRUE(meter_service_poll(meter_id));
    ASSERT_TRUE(nvm_size[NV_ITEM_METER_STATE(0)] != 0);
    meter_service_init();
    {
        meter_service_config_t config = valid_config(&driver);

        meter_id = meter_service_add(&config);
    }
    ASSERT_TRUE(meter_service_snapshot(meter_id, &snapshot));
    ASSERT_EQ(150, snapshot.accumulated_energy_uwh);
    ASSERT_EQ(METER_SERVICE_OK, meter_service_reset_energy(meter_id));
    ASSERT_TRUE(meter_service_snapshot(meter_id, &snapshot));
    ASSERT_EQ(0, snapshot.accumulated_energy_uwh);
    ASSERT_EQ(0, driver.sample.energy_delta_uwh);
}

TEST(rejects_invalid_configuration_and_keeps_state_on_nvm_failure) {
    meter_fake_driver_t driver;
    meter_service_snapshot_t snapshot;
    meter_calibration_t invalid = {
        .voltage_gain_ppm = 1,
        .current_gain_ppm = 1,
        .power_gain_ppm = 1,
    };
    meter_sample_t sample = {
        .energy_delta_uwh = 100,
        .valid_mask = METER_SAMPLE_ENERGY,
    };
    uint8_t meter_id = reset_service(&driver);

    ASSERT_EQ(METER_SERVICE_INVALID, meter_service_set_calibration(meter_id, &invalid));
    meter_fake_driver_set_sample(&driver, &sample);
    ASSERT_TRUE(meter_service_poll(meter_id));
    ASSERT_TRUE(meter_service_snapshot(meter_id, &snapshot));
    nvm_write_fails = true;
    ASSERT_EQ(METER_SERVICE_NVM_ERROR, meter_service_reset_energy(meter_id));
    ASSERT_TRUE(meter_service_snapshot(meter_id, &snapshot));
    ASSERT_EQ(150, snapshot.accumulated_energy_uwh);
    ASSERT_EQ(100, driver.sample.energy_delta_uwh);
    meter_fake_driver_set_available(&driver, false);
    ASSERT_TRUE(!meter_service_poll(meter_id));
    {
        meter_service_config_t config = valid_config(&driver);

        ASSERT_EQ(1, meter_service_add(&config));
        ASSERT_EQ(UINT8_MAX, meter_service_add(&config));
    }
}

TEST(sampling_timer_uses_virtual_time) {
    meter_fake_driver_t driver;
    meter_service_snapshot_t snapshot;
    meter_sample_t sample = {
        .energy_delta_uwh = 2,
        .valid_mask = METER_SAMPLE_ENERGY,
    };
    uint8_t meter_id = reset_service(&driver);

    meter_fake_driver_set_sample(&driver, &sample);
    ASSERT_TRUE(meter_service_start(meter_id));
    fake_clock_advance_ms(10);
    fake_tasks_poll();
    ASSERT_TRUE(meter_service_snapshot(meter_id, &snapshot));
    ASSERT_EQ(3, snapshot.accumulated_energy_uwh);
    meter_service_stop(meter_id);
    fake_clock_advance_ms(10);
    fake_tasks_poll();
    ASSERT_TRUE(meter_service_snapshot(meter_id, &snapshot));
    ASSERT_EQ(3, snapshot.accumulated_energy_uwh);
}

TEST(energy_scaling_and_accumulation_saturate_without_wrapping) {
    meter_fake_driver_t driver;
    meter_service_snapshot_t snapshot;
    meter_sample_t sample = {
        .energy_delta_uwh = UINT64_MAX,
        .valid_mask       = METER_SAMPLE_ENERGY,
    };
    uint8_t meter_id = reset_service(&driver);

    meter_fake_driver_set_sample(&driver, &sample);
    ASSERT_TRUE(meter_service_poll(meter_id));
    ASSERT_TRUE(meter_service_snapshot(meter_id, &snapshot));
    ASSERT_EQ(UINT64_MAX, snapshot.accumulated_energy_uwh);
}

int main(void) {
    RUN_TEST(applies_profile_then_runtime_calibration_and_preserves_validity);
    RUN_TEST(checkpoints_restores_and_resets_energy_with_one_owner);
    RUN_TEST(rejects_invalid_configuration_and_keeps_state_on_nvm_failure);
    RUN_TEST(sampling_timer_uses_virtual_time);
    RUN_TEST(energy_scaling_and_accumulation_saturate_without_wrapping);
    return test_runner_failures == 0 ? 0 : 1;
}
