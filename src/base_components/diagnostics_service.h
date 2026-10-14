#ifndef BASE_COMPONENTS_DIAGNOSTICS_SERVICE_H_
#define BASE_COMPONENTS_DIAGNOSTICS_SERVICE_H_

#include <stdint.h>

typedef struct {
    uint32_t uptime_ms;
    uint32_t gpio_edges_captured;
    uint32_t gpio_edges_dropped;
    uint32_t gpio_rearm_limit_hits;
    uint32_t button_events_emitted;
    uint32_t gestures_emitted;
    uint32_t zb_submitted;
    uint32_t zb_dropped;
    uint32_t zb_expired;
    uint32_t zb_send_failed;
    uint32_t zb_high_water;
    uint32_t network_transitions;
    uint32_t network_losses;
    uint32_t network_joins;
    uint32_t announce_attempts;
    uint32_t announce_failures;
    uint32_t steering_attempts;
    uint32_t descriptor_validation_failures;
    uint8_t  last_descriptor_error;
} diagnostics_snapshot_t;

typedef void (*diagnostics_provider_t)(diagnostics_snapshot_t *snapshot,
                                       void *context);

void diagnostics_service_init(void);
void diagnostics_service_set_provider(diagnostics_provider_t provider,
                                      void *context);
void diagnostics_service_refresh(void);
const diagnostics_snapshot_t *diagnostics_service_get_snapshot(void);

#endif
