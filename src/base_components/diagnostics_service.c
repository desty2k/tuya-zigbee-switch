#include "diagnostics_service.h"

#include <string.h>

static diagnostics_snapshot_t snapshot;
static diagnostics_provider_t provider;
static void *provider_context;

void diagnostics_service_init(void) {
    memset(&snapshot, 0, sizeof(snapshot));
    provider         = NULL;
    provider_context = NULL;
}

void diagnostics_service_set_provider(diagnostics_provider_t new_provider,
                                      void *context) {
    provider         = new_provider;
    provider_context = context;
}

void diagnostics_service_refresh(void) {
    if (provider != NULL) provider(&snapshot, provider_context);
}

const diagnostics_snapshot_t *diagnostics_service_get_snapshot(void) {
    return &snapshot;
}
