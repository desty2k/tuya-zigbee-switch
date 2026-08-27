#include "network_indicator.h"
#include <stddef.h>

void network_indicator_connected(network_indicator_t *indicator) {
    indicator_feedback_t **feedback = indicator->indicators;

    while (*feedback != NULL && (feedback - indicator->indicators) < 4) {
        indicator_feedback_network_connected(*feedback);
        feedback++;
    }
    network_indicator_from_manual_state(indicator);
}

void network_indicator_from_manual_state(network_indicator_t *indicator) {
    indicator_feedback_t **feedback = indicator->indicators;

    while (*feedback != NULL && (feedback - indicator->indicators) < 4) {
        if (indicator->has_dedicated_led) {
            indicator_feedback_set_base_state(
                *feedback, indicator->manual_state_when_connected);
        }
        feedback++;
    }
}

void network_indicator_commission_success(network_indicator_t *indicator) {
    indicator_feedback_t **feedback = indicator->indicators;

    while (*feedback != NULL && (feedback - indicator->indicators) < 4) {
        indicator_feedback_commission_success(*feedback);
        feedback++;
    }
}

void network_indicator_not_connected(network_indicator_t *indicator) {
    indicator_feedback_t **feedback = indicator->indicators;

    while (*feedback != NULL && (feedback - indicator->indicators) < 4) {
        indicator_feedback_network_not_connected(*feedback);
        feedback++;
    }
}
