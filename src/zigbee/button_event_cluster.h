#ifndef _BUTTON_EVENT_CLUSTER_H_
#define _BUTTON_EVENT_CLUSTER_H_

#include "base_components/button_input.h"
#include "base_components/gesture_fsm.h"
#include "hal/zigbee.h"
#include <stdint.h>

#define ZB_BUTTON_EVENT_QUEUE_SIZE        16
#define ZB_BUTTON_EVENT_TTL_MS            5000
#define ZB_BUTTON_EVENT_TX_INTERVAL_MS    40
#define ZB_BUTTON_EVENT_RETRY_MS          100
#define ZB_BUTTON_EVENT_MAX_CLUSTERS      7
#define ZB_BUTTON_EVENT_MAX_BUTTONS       2

typedef enum {
    ZB_BUTTON_DOWN       = 0,
    ZB_BUTTON_UP         = 1,
    ZB_BUTTON_HOLD_START = 2,
    ZB_BUTTON_HOLD_END   = 3,
    ZB_BUTTON_N_CLICK    = 4,
} zb_button_event_type_t;

typedef struct {
    uint16_t seq;
    uint16_t press_id;
    uint8_t  event_type;
    uint8_t  count;
    uint16_t duration_ms;
} zb_button_event_payload_t;

typedef struct {
    uint8_t                   endpoint;
    uint32_t                  enqueued_at_ms;
    zb_button_event_payload_t payload;
} zb_button_event_entry_t;

typedef void (*button_event_config_changed_t)(void *arg);

typedef struct {
    uint8_t                       endpoint;
    uint8_t                       button_ids[ZB_BUTTON_EVENT_MAX_BUTTONS];
    uint8_t                       button_count;
    uint8_t                       button_state;
    uint16_t                      last_event_seq;
    uint16_t                      multi_click_gap_ms;
    uint16_t                      debounce_ms;
    hal_zigbee_attribute          attr_infos[4];
    button_event_config_changed_t config_changed;
    void *                        config_changed_arg;
} zigbee_button_event_cluster;

void button_event_cluster_init(void);
void button_event_cluster_add_to_endpoint(
    zigbee_button_event_cluster *cluster, hal_zigbee_endpoint *endpoint,
    const uint8_t *button_ids, uint8_t button_count,
    uint16_t multi_click_gap_ms, uint16_t debounce_ms,
    button_event_config_changed_t config_changed, void *config_changed_arg);
void button_event_cluster_register_input(void);
void button_event_cluster_register_gestures(void);
void button_event_cluster_sync_states(void);
void button_event_cluster_on_button_event(const button_event_t *event,
                                          void *arg);
void button_event_cluster_on_gesture_event(const gesture_event_t *event,
                                           void *arg);
void button_event_cluster_on_write_attr(uint8_t endpoint,
                                        uint16_t attribute_id);
void button_event_cluster_on_network_status_change(
    hal_zigbee_network_status_t status);
void button_event_cluster_drain(void);
uint32_t button_event_cluster_dropped(void);
uint32_t button_event_cluster_expired(void);
uint32_t button_event_cluster_send_failed(void);
uint32_t button_event_cluster_high_water(void);
uint32_t button_event_cluster_submitted(void);
uint8_t button_event_cluster_queue_used(void);

#endif
