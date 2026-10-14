#include "meter_service.h"

#include "base_components/timer_service.h"
#include "device_config/nvm_items.h"
#include "hal/nvm.h"
#include "hal/timer.h"
#include <limits.h>
#include <string.h>

#define METER_SERVICE_RECORD_VERSION    1

typedef struct {
    uint8_t             version;
    uint8_t             runtime_calibration_active;
    uint16_t            reserved;
    meter_calibration_t calibration;
    uint64_t            accumulated_energy_uwh;
    uint32_t            energy_remainder_ppm;
} meter_service_record_t;

typedef struct {
    meter_service_subscriber_t callback;
    void *                     context;
} meter_service_subscriber_slot_t;

typedef struct {
    meter_service_config_t          config;
    meter_service_snapshot_t        snapshot;
    meter_calibration_t             runtime_calibration;
    uint32_t                        energy_remainder_ppm;
    uint64_t                        energy_since_checkpoint_uwh;
    uint32_t                        last_checkpoint_ms;
    app_timer_t                     sample_timer;
    meter_service_subscriber_slot_t subscribers[METER_SERVICE_MAX_SUBSCRIBERS];
    uint8_t                         in_use;
} meter_service_instance_t;

static meter_service_instance_t meter_services[METER_SERVICE_MAX_INSTANCES];

static meter_calibration_t unity_calibration(void) {
    return (meter_calibration_t){
               .voltage_gain_ppm = METER_CALIBRATION_UNITY_PPM,
               .current_gain_ppm = METER_CALIBRATION_UNITY_PPM,
               .power_gain_ppm   = METER_CALIBRATION_UNITY_PPM,
    };
}

static bool calibration_is_valid(const meter_calibration_t *calibration) {
    return calibration != NULL &&
           calibration->voltage_gain_ppm >= METER_CALIBRATION_MIN_PPM &&
           calibration->voltage_gain_ppm <= METER_CALIBRATION_MAX_PPM &&
           calibration->current_gain_ppm >= METER_CALIBRATION_MIN_PPM &&
           calibration->current_gain_ppm <= METER_CALIBRATION_MAX_PPM &&
           calibration->power_gain_ppm >= METER_CALIBRATION_MIN_PPM &&
           calibration->power_gain_ppm <= METER_CALIBRATION_MAX_PPM;
}

static bool calibration_is_zero(const meter_calibration_t *calibration) {
    return calibration->voltage_gain_ppm == 0 && calibration->current_gain_ppm == 0 &&
           calibration->power_gain_ppm == 0;
}

static meter_calibration_t effective_calibration(
    const meter_service_instance_t *service) {
    if (service->snapshot.runtime_calibration_active) {
        return service->runtime_calibration;
    }
    if (calibration_is_valid(&service->config.profile_calibration)) {
        return service->config.profile_calibration;
    }
    return unity_calibration();
}

static uint32_t scale_u32(uint32_t value, uint32_t gain_ppm) {
    uint64_t whole    = (uint64_t)(value / METER_CALIBRATION_UNITY_PPM) * gain_ppm;
    uint64_t fraction =
        ((uint64_t)(value % METER_CALIBRATION_UNITY_PPM) * gain_ppm) /
        METER_CALIBRATION_UNITY_PPM;

    return whole + fraction > UINT32_MAX ? UINT32_MAX : (uint32_t)(whole + fraction);
}

static int32_t scale_i32(int32_t value, uint32_t gain_ppm) {
    uint64_t magnitude = value < 0 ? (uint64_t)-(int64_t)value : (uint64_t)value;
    uint64_t scaled    = (uint64_t)scale_u32(
        magnitude > UINT32_MAX ? UINT32_MAX : (uint32_t)magnitude, gain_ppm);

    if (value < 0) {
        if (scaled >= (uint64_t)INT32_MAX + 1U) {
            return INT32_MIN;
        }
        return -(int32_t)scaled;
    }
    return scaled > INT32_MAX ? INT32_MAX : (int32_t)scaled;
}

static uint64_t add_u64_saturated(uint64_t left, uint64_t right) {
    return UINT64_MAX - left < right ? UINT64_MAX : left + right;
}

static uint64_t scale_energy_delta(meter_service_instance_t *service,
                                   uint64_t delta_uwh, uint32_t gain_ppm) {
    uint64_t whole_units = delta_uwh / METER_CALIBRATION_UNITY_PPM;
    uint64_t whole       = whole_units > UINT64_MAX / gain_ppm ? UINT64_MAX :
                           whole_units * gain_ppm;
    uint64_t fractional =
        (delta_uwh % METER_CALIBRATION_UNITY_PPM) * gain_ppm +
        service->energy_remainder_ppm;

    service->energy_remainder_ppm =
        (uint32_t)(fractional % METER_CALIBRATION_UNITY_PPM);
    return add_u64_saturated(whole, fractional / METER_CALIBRATION_UNITY_PPM);
}

static void scale_sample(meter_service_instance_t *service, meter_sample_t *sample,
                         const meter_calibration_t *calibration) {
    if (sample->valid_mask & METER_SAMPLE_VOLTAGE) {
        sample->voltage_mv = scale_u32(sample->voltage_mv,
                                       calibration->voltage_gain_ppm);
    }
    if (sample->valid_mask & METER_SAMPLE_CURRENT) {
        sample->current_ma = scale_u32(sample->current_ma,
                                       calibration->current_gain_ppm);
    }
    if (sample->valid_mask & METER_SAMPLE_ACTIVE_POWER) {
        sample->active_power_mw = scale_i32(sample->active_power_mw,
                                            calibration->power_gain_ppm);
    }
    if (sample->valid_mask & METER_SAMPLE_APPARENT_POWER) {
        sample->apparent_power_mva = scale_u32(sample->apparent_power_mva,
                                               calibration->power_gain_ppm);
    }
    if (sample->valid_mask & METER_SAMPLE_REACTIVE_POWER) {
        sample->reactive_power_mvar = scale_i32(sample->reactive_power_mvar,
                                                calibration->power_gain_ppm);
    }
    if (sample->valid_mask & METER_SAMPLE_ENERGY) {
        sample->energy_delta_uwh = scale_energy_delta(service,
                                                      sample->energy_delta_uwh,
                                                      calibration->power_gain_ppm);
    }
}

static uint8_t nvm_item(uint8_t meter_id) {
    return NV_ITEM_METER_STATE(meter_id);
}

static bool persist_record(const meter_service_instance_t *service,
                           const meter_calibration_t *runtime_calibration,
                           uint8_t runtime_calibration_active,
                           uint64_t accumulated_energy_uwh,
                           uint32_t energy_remainder_ppm) {
    meter_service_record_t record = {
        .version                    = METER_SERVICE_RECORD_VERSION,
        .runtime_calibration_active = runtime_calibration_active,
        .calibration            = *runtime_calibration,
        .accumulated_energy_uwh = accumulated_energy_uwh,
        .energy_remainder_ppm   = energy_remainder_ppm,
    };
    uint8_t meter_id = (uint8_t)(service - meter_services);

    return hal_nvm_write(nvm_item(meter_id), sizeof(record),
                         (uint8_t *)&record) == HAL_NVM_SUCCESS;
}

static void restore_record(meter_service_instance_t *service) {
    meter_service_record_t record;
    uint8_t meter_id = (uint8_t)(service - meter_services);

    if (hal_nvm_read(nvm_item(meter_id), sizeof(record), (uint8_t *)&record) !=
        HAL_NVM_SUCCESS ||
        record.version != METER_SERVICE_RECORD_VERSION ||
        (record.runtime_calibration_active &&
         !calibration_is_valid(&record.calibration))) {
        hal_nvm_delete(nvm_item(meter_id));
        return;
    }
    service->snapshot.runtime_calibration_active =
        record.runtime_calibration_active != 0;
    service->runtime_calibration             = record.calibration;
    service->snapshot.accumulated_energy_uwh = record.accumulated_energy_uwh;
    service->energy_remainder_ppm            = record.energy_remainder_ppm;
}

static void notify_subscribers(uint8_t meter_id,
                               const meter_service_instance_t *service) {
    for (uint8_t i = 0; i < METER_SERVICE_MAX_SUBSCRIBERS; i++) {
        if (service->subscribers[i].callback != NULL) {
            service->subscribers[i].callback(meter_id, &service->snapshot,
                                             service->subscribers[i].context);
        }
    }
}

static void checkpoint_if_due(meter_service_instance_t *service) {
    uint32_t elapsed = hal_millis() - service->last_checkpoint_ms;

    if (elapsed < service->config.checkpoint_interval_ms &&
        service->energy_since_checkpoint_uwh < service->config.checkpoint_energy_uwh) {
        return;
    }
    if (persist_record(service, &service->runtime_calibration,
                       service->snapshot.runtime_calibration_active,
                       service->snapshot.accumulated_energy_uwh,
                       service->energy_remainder_ppm)) {
        service->energy_since_checkpoint_uwh = 0;
        service->last_checkpoint_ms          = hal_millis();
    }
}

static void sample_timer_elapsed(void *arg) {
    uint8_t meter_id = (uint8_t)(uintptr_t)arg;

    meter_service_poll(meter_id);
    if (meter_id < METER_SERVICE_MAX_INSTANCES && meter_services[meter_id].in_use) {
        timer_restart(&meter_services[meter_id].sample_timer,
                      meter_services[meter_id].config.sample_interval_ms);
    }
}

void meter_service_init(void) {
    memset(meter_services, 0, sizeof(meter_services));
}

uint8_t meter_service_add(const meter_service_config_t *config) {
    meter_service_instance_t *service = NULL;
    uint8_t meter_id;

    if (config == NULL || config->driver.ops == NULL ||
        config->driver.ops->sample == NULL || config->sample_interval_ms == 0 ||
        config->checkpoint_interval_ms == 0 || config->checkpoint_energy_uwh == 0 ||
        (!calibration_is_zero(&config->profile_calibration) &&
         !calibration_is_valid(&config->profile_calibration))) {
        return UINT8_MAX;
    }
    for (meter_id = 0; meter_id < METER_SERVICE_MAX_INSTANCES; meter_id++) {
        if (!meter_services[meter_id].in_use) {
            service = &meter_services[meter_id];
            break;
        }
    }
    if (service == NULL) {
        return UINT8_MAX;
    }
    memset(service, 0, sizeof(*service));
    service->config = *config;
    service->runtime_calibration  = unity_calibration();
    service->snapshot.calibration = effective_calibration(service);
    service->in_use             = 1;
    service->last_checkpoint_ms = hal_millis();
    timer_init(&service->sample_timer, sample_timer_elapsed,
               (void *)(uintptr_t)meter_id);
    restore_record(service);
    service->snapshot.calibration = effective_calibration(service);
    return meter_id;
}

bool meter_service_start(uint8_t meter_id) {
    meter_service_instance_t *service;

    if (meter_id >= METER_SERVICE_MAX_INSTANCES || !meter_services[meter_id].in_use) {
        return false;
    }
    service = &meter_services[meter_id];
    app_timer_start(&service->sample_timer, service->config.sample_interval_ms);
    return true;
}

void meter_service_stop(uint8_t meter_id) {
    if (meter_id < METER_SERVICE_MAX_INSTANCES && meter_services[meter_id].in_use) {
        timer_cancel(&meter_services[meter_id].sample_timer);
    }
}

bool meter_service_poll(uint8_t meter_id) {
    meter_service_instance_t *service;
    meter_sample_t            sample;
    meter_calibration_t       calibration;

    if (meter_id >= METER_SERVICE_MAX_INSTANCES || !meter_services[meter_id].in_use ||
        !energy_meter_sample(&meter_services[meter_id].config.driver, &sample)) {
        return false;
    }
    service     = &meter_services[meter_id];
    calibration = effective_calibration(service);
    scale_sample(service, &sample, &calibration);
    service->snapshot.sample      = sample;
    service->snapshot.calibration = calibration;
    if (sample.valid_mask & METER_SAMPLE_ENERGY) {
        uint64_t before = service->snapshot.accumulated_energy_uwh;

        service->snapshot.accumulated_energy_uwh += sample.energy_delta_uwh;
        if (service->snapshot.accumulated_energy_uwh < before) {
            service->snapshot.accumulated_energy_uwh = UINT64_MAX;
        }
        service->energy_since_checkpoint_uwh += sample.energy_delta_uwh;
        if (service->energy_since_checkpoint_uwh < sample.energy_delta_uwh) {
            service->energy_since_checkpoint_uwh = UINT64_MAX;
        }
        checkpoint_if_due(service);
    }
    notify_subscribers(meter_id, service);
    return true;
}

bool meter_service_snapshot(uint8_t meter_id, meter_service_snapshot_t *out) {
    if (out == NULL || meter_id >= METER_SERVICE_MAX_INSTANCES ||
        !meter_services[meter_id].in_use) {
        return false;
    }
    *out = meter_services[meter_id].snapshot;
    return true;
}

bool meter_service_subscribe(uint8_t meter_id, meter_service_subscriber_t callback,
                             void *context) {
    meter_service_instance_t *service;

    if (callback == NULL || meter_id >= METER_SERVICE_MAX_INSTANCES ||
        !meter_services[meter_id].in_use) {
        return false;
    }
    service = &meter_services[meter_id];
    for (uint8_t i = 0; i < METER_SERVICE_MAX_SUBSCRIBERS; i++) {
        if (service->subscribers[i].callback == callback &&
            service->subscribers[i].context == context) {
            return true;
        }
        if (service->subscribers[i].callback == NULL) {
            service->subscribers[i] = (meter_service_subscriber_slot_t){
                .callback = callback,
                .context  = context,
            };
            return true;
        }
    }
    return false;
}

meter_service_result_t meter_service_set_calibration(
    uint8_t meter_id, const meter_calibration_t *calibration) {
    meter_service_instance_t *service;

    if (meter_id >= METER_SERVICE_MAX_INSTANCES || !meter_services[meter_id].in_use ||
        !calibration_is_valid(calibration)) {
        return METER_SERVICE_INVALID;
    }
    service = &meter_services[meter_id];
    if (!persist_record(service, calibration, 1, service->snapshot.accumulated_energy_uwh,
                        service->energy_remainder_ppm)) {
        return METER_SERVICE_NVM_ERROR;
    }
    service->runtime_calibration = *calibration;
    service->snapshot.runtime_calibration_active = 1;
    service->snapshot.calibration        = effective_calibration(service);
    service->last_checkpoint_ms          = hal_millis();
    service->energy_since_checkpoint_uwh = 0;
    notify_subscribers(meter_id, service);
    return METER_SERVICE_OK;
}

meter_service_result_t meter_service_clear_calibration(uint8_t meter_id) {
    meter_service_instance_t *service;
    meter_calibration_t       unity = unity_calibration();

    if (meter_id >= METER_SERVICE_MAX_INSTANCES || !meter_services[meter_id].in_use) {
        return METER_SERVICE_INVALID;
    }
    service = &meter_services[meter_id];
    if (!persist_record(service, &unity, 0, service->snapshot.accumulated_energy_uwh,
                        service->energy_remainder_ppm)) {
        return METER_SERVICE_NVM_ERROR;
    }
    service->runtime_calibration = unity;
    service->snapshot.runtime_calibration_active = 0;
    service->snapshot.calibration        = effective_calibration(service);
    service->last_checkpoint_ms          = hal_millis();
    service->energy_since_checkpoint_uwh = 0;
    notify_subscribers(meter_id, service);
    return METER_SERVICE_OK;
}

meter_service_result_t meter_service_reset_energy(uint8_t meter_id) {
    meter_service_instance_t *service;

    if (meter_id >= METER_SERVICE_MAX_INSTANCES || !meter_services[meter_id].in_use) {
        return METER_SERVICE_INVALID;
    }
    service = &meter_services[meter_id];
    if (!persist_record(service, &service->runtime_calibration,
                        service->snapshot.runtime_calibration_active, 0, 0)) {
        return METER_SERVICE_NVM_ERROR;
    }
    energy_meter_reset_remainder(&service->config.driver);
    service->snapshot.accumulated_energy_uwh = 0;
    service->energy_remainder_ppm            = 0;
    service->energy_since_checkpoint_uwh     = 0;
    service->last_checkpoint_ms = hal_millis();
    notify_subscribers(meter_id, service);
    return METER_SERVICE_OK;
}
