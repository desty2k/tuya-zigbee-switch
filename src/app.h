#ifndef APP_H
#define APP_H

#include <stdint.h>

typedef struct {
    uint32_t network_transitions;
    uint32_t network_losses;
    uint32_t network_joins;
    uint32_t steering_attempts;
    uint32_t announce_attempts;
    uint32_t announce_failures;
    uint32_t uptime_ms;
    uint32_t last_transition_ms;
} app_network_diagnostics_t;

void app_init(void);
void app_task(void);
app_network_diagnostics_t app_network_get_diagnostics(void);

#endif // APP_H
