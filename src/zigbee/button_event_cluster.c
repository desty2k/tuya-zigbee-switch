#include "button_event_cluster.h"

#include "base_components/button_dispatcher.h"
#include "cluster_common.h"
#include "consts.h"
#include "hal/timer.h"
#include <stddef.h>

typedef struct {
    zb_button_event_entry_t entries[ZB_BUTTON_EVENT_QUEUE_SIZE];
    uint8_t                 head;
    uint8_t                 tail;
    uint8_t                 used;
    uint32_t                dropped;
} button_event_queue_t;

static zigbee_button_event_cluster *button_event_clusters[ZB_BUTTON_EVENT_MAX_CLUSTERS];
static uint8_t              button_event_cluster_count;
static uint16_t             button_event_seq;
static button_event_queue_t button_event_queue;

static bool button_event_cluster_has_button(
    const zigbee_button_event_cluster *cluster, uint8_t button_id) {
    for (uint8_t i = 0; i < cluster->button_count; i++) {
        if (cluster->button_ids[i] == button_id) {
            return true;
        }
    }
    return false;
}

static zigbee_button_event_cluster *button_event_cluster_find_by_endpoint(
    uint8_t endpoint) {
    for (uint8_t i = 0; i < button_event_cluster_count; i++) {
        if (button_event_clusters[i]->endpoint == endpoint) {
            return button_event_clusters[i];
        }
    }
    return NULL;
}

static zigbee_button_event_cluster *button_event_cluster_find_by_button(
    uint8_t button_id) {
    for (uint8_t i = 0; i < button_event_cluster_count; i++) {
        if (button_event_cluster_has_button(button_event_clusters[i], button_id)) {
            return button_event_clusters[i];
        }
    }
    return NULL;
}

static void button_event_cluster_encode_payload(
    const zb_button_event_payload_t *payload, uint8_t bytes[8]) {
    bytes[0] = (uint8_t)payload->seq;
    bytes[1] = (uint8_t)(payload->seq >> 8);
    bytes[2] = (uint8_t)payload->press_id;
    bytes[3] = (uint8_t)(payload->press_id >> 8);
    bytes[4] = payload->event_type;
    bytes[5] = payload->count;
    bytes[6] = (uint8_t)payload->duration_ms;
    bytes[7] = (uint8_t)(payload->duration_ms >> 8);
}

static void button_event_cluster_pop(void) {
    button_event_queue.tail =
        (uint8_t)((button_event_queue.tail + 1) % ZB_BUTTON_EVENT_QUEUE_SIZE);
    button_event_queue.used--;
}

static void button_event_cluster_enqueue(
    uint8_t endpoint, const zb_button_event_payload_t *payload) {
    zb_button_event_entry_t *entry;

    if (hal_zigbee_get_network_status() != HAL_ZIGBEE_NETWORK_JOINED) {
        return;
    }
    if (button_event_queue.used == ZB_BUTTON_EVENT_QUEUE_SIZE) {
        button_event_cluster_pop();
        button_event_queue.dropped++;
    }
    entry                   = &button_event_queue.entries[button_event_queue.head];
    entry->endpoint         = endpoint;
    entry->enqueued_at_ms   = hal_millis();
    entry->payload          = *payload;
    button_event_queue.head =
        (uint8_t)((button_event_queue.head + 1) % ZB_BUTTON_EVENT_QUEUE_SIZE);
    button_event_queue.used++;
    button_event_cluster_drain();
}

static void button_event_cluster_emit(uint8_t endpoint,
                                      zb_button_event_type_t type,
                                      uint32_t press_id, uint8_t count,
                                      uint16_t duration_ms) {
    zb_button_event_payload_t payload = {
        .seq         = button_event_seq++,
        .press_id    = (uint16_t)press_id,
        .event_type  = (uint8_t)type,
        .count       = count,
        .duration_ms = duration_ms,
    };

    button_event_cluster_enqueue(endpoint, &payload);
}

void button_event_cluster_init(void) {
    button_event_cluster_count = 0;
    button_event_seq           = 0;
    button_event_queue.head    = 0;
    button_event_queue.tail    = 0;
    button_event_queue.used    = 0;
    button_event_queue.dropped = 0;
}

void button_event_cluster_add_to_endpoint(
    zigbee_button_event_cluster *cluster, hal_zigbee_endpoint *endpoint,
    const uint8_t *button_ids, uint8_t button_count,
    uint16_t multi_click_gap_ms, uint16_t debounce_ms,
    button_event_config_changed_t config_changed, void *config_changed_arg) {
    if (cluster == NULL || endpoint == NULL || button_ids == NULL ||
        button_count == 0 || button_count > ZB_BUTTON_EVENT_MAX_BUTTONS ||
        button_event_cluster_count >= ZB_BUTTON_EVENT_MAX_CLUSTERS) {
        return;
    }
    cluster->endpoint           = endpoint->endpoint;
    cluster->button_count       = button_count;
    cluster->button_state       = 0;
    cluster->last_event_seq     = 0;
    cluster->multi_click_gap_ms = multi_click_gap_ms;
    cluster->debounce_ms        = debounce_ms;
    cluster->config_changed     = config_changed;
    cluster->config_changed_arg = config_changed_arg;
    for (uint8_t i = 0; i < button_count; i++) {
        cluster->button_ids[i] = button_ids[i];
        if (button_input_is_down(button_ids[i])) {
            cluster->button_state = 1;
        }
    }

    SETUP_ATTR_FOR_TABLE(cluster->attr_infos, 0, ZCL_ATTR_BUTTON_EVENT_STATE,
                         ZCL_DATA_TYPE_ENUM8, ATTR_READONLY,
                         cluster->button_state);
    SETUP_ATTR_FOR_TABLE(cluster->attr_infos, 1,
                         ZCL_ATTR_BUTTON_EVENT_LAST_EVENT_SEQ,
                         ZCL_DATA_TYPE_UINT16, ATTR_READONLY,
                         cluster->last_event_seq);
    SETUP_ATTR_FOR_TABLE(cluster->attr_infos, 2,
                         ZCL_ATTR_BUTTON_EVENT_MULTI_CLICK_GAP,
                         ZCL_DATA_TYPE_UINT16, ATTR_WRITABLE,
                         cluster->multi_click_gap_ms);
    SETUP_ATTR_FOR_TABLE(cluster->attr_infos, 3,
                         ZCL_ATTR_BUTTON_EVENT_DEBOUNCE_MS,
                         ZCL_DATA_TYPE_UINT16, ATTR_WRITABLE,
                         cluster->debounce_ms);
    endpoint->clusters[endpoint->cluster_count].cluster_id =
        ZCL_CLUSTER_BUTTON_EVENT;
    endpoint->clusters[endpoint->cluster_count].attribute_count = 4;
    endpoint->clusters[endpoint->cluster_count].attributes      = cluster->attr_infos;
    endpoint->clusters[endpoint->cluster_count].is_server       = 1;
    endpoint->cluster_count++;
    button_event_clusters[button_event_cluster_count++] = cluster;
}

void button_event_cluster_register_input(void) {
    button_dispatcher_register(button_event_cluster_on_button_event, NULL);
}

void button_event_cluster_register_gestures(void) {
    gesture_fsm_register_sink(button_event_cluster_on_gesture_event, NULL);
}

void button_event_cluster_sync_states(void) {
    for (uint8_t cluster_index = 0;
         cluster_index < button_event_cluster_count; cluster_index++) {
        zigbee_button_event_cluster *cluster =
            button_event_clusters[cluster_index];

        cluster->button_state = 0;
        for (uint8_t i = 0; i < cluster->button_count; i++) {
            if (button_input_is_down(cluster->button_ids[i])) {
                cluster->button_state = 1;
                break;
            }
        }
    }
}

void button_event_cluster_on_button_event(const button_event_t *event,
                                          void *arg) {
    zigbee_button_event_cluster *cluster;

    (void)arg;
    cluster = button_event_cluster_find_by_button(event->button_id);
    if (cluster == NULL) {
        return;
    }
    if (event->type == BUTTON_EVENT_DOWN) {
        cluster->button_state = 1;
    } else {
        cluster->button_state = 0;
        for (uint8_t i = 0; i < cluster->button_count; i++) {
            if (button_input_is_down(cluster->button_ids[i])) {
                cluster->button_state = 1;
                break;
            }
        }
    }
    hal_zigbee_notify_attribute_changed(cluster->endpoint,
                                        ZCL_CLUSTER_BUTTON_EVENT,
                                        ZCL_ATTR_BUTTON_EVENT_STATE);
    button_event_cluster_emit(
        cluster->endpoint,
        event->type == BUTTON_EVENT_DOWN ? ZB_BUTTON_DOWN : ZB_BUTTON_UP,
        event->press_id, 0, 0);
}

void button_event_cluster_on_gesture_event(const gesture_event_t *event,
                                           void *arg) {
    zigbee_button_event_cluster *cluster;
    zb_button_event_type_t       type;

    (void)arg;
    cluster = button_event_cluster_find_by_button(event->button_id);
    if (cluster == NULL) {
        return;
    }
    if (event->type == GESTURE_HOLD_START) {
        type = ZB_BUTTON_HOLD_START;
    } else if (event->type == GESTURE_HOLD_END) {
        type = ZB_BUTTON_HOLD_END;
    } else {
        type = ZB_BUTTON_N_CLICK;
    }
    button_event_cluster_emit(cluster->endpoint, type, event->press_id,
                              type == ZB_BUTTON_N_CLICK ? event->count : 0,
                              type == ZB_BUTTON_HOLD_END ? event->duration_ms : 0);
}

void button_event_cluster_on_write_attr(uint8_t endpoint,
                                        uint16_t attribute_id) {
    zigbee_button_event_cluster *cluster =
        button_event_cluster_find_by_endpoint(endpoint);

    if (cluster == NULL) {
        return;
    }
    if (attribute_id == ZCL_ATTR_BUTTON_EVENT_MULTI_CLICK_GAP) {
        if (cluster->multi_click_gap_ms < 100 ||
            cluster->multi_click_gap_ms > 2000) {
            cluster->multi_click_gap_ms = 350;
        }
        for (uint8_t i = 0; i < cluster->button_count; i++) {
            gesture_fsm_set_multi_click_gap_ms(cluster->button_ids[i],
                                               cluster->multi_click_gap_ms);
        }
    } else if (attribute_id == ZCL_ATTR_BUTTON_EVENT_DEBOUNCE_MS) {
        if (cluster->debounce_ms < 1 || cluster->debounce_ms > 200) {
            cluster->debounce_ms = BUTTON_DEBOUNCE_DEFAULT_MS;
        }
        for (uint8_t i = 0; i < cluster->button_count; i++) {
            button_input_set_debounce_ms(cluster->button_ids[i],
                                         cluster->debounce_ms);
        }
    } else {
        return;
    }
    if (cluster->config_changed != NULL) {
        cluster->config_changed(cluster->config_changed_arg);
    }
}

void button_event_cluster_on_network_status_change(
    hal_zigbee_network_status_t status) {
    if (status == HAL_ZIGBEE_NETWORK_JOINED) {
        button_event_cluster_drain();
    }
}

void button_event_cluster_drain(void) {
    while (button_event_queue.used != 0 &&
           hal_zigbee_get_network_status() == HAL_ZIGBEE_NETWORK_JOINED) {
        zb_button_event_entry_t *entry =
            &button_event_queue.entries[button_event_queue.tail];
        zigbee_button_event_cluster *cluster;
        hal_zigbee_cmd cmd;
        uint8_t        payload[8];

        if (hal_millis() - entry->enqueued_at_ms > ZB_BUTTON_EVENT_TTL_MS) {
            button_event_cluster_pop();
            continue;
        }
        button_event_cluster_encode_payload(&entry->payload, payload);
        cmd.endpoint            = entry->endpoint;
        cmd.profile_id          = ZCL_HA_PROFILE;
        cmd.cluster_id          = ZCL_CLUSTER_BUTTON_EVENT;
        cmd.command_id          = ZCL_CMD_BUTTON_EVENT;
        cmd.cluster_specific    = 1;
        cmd.direction           = HAL_ZIGBEE_DIR_SERVER_TO_CLIENT;
        cmd.disable_default_rsp = 1;
        cmd.manufacturer_code   = 0;
        cmd.payload             = payload;
        cmd.payload_len         = sizeof(payload);
        if (hal_zigbee_send_cmd_to_coordinator(&cmd) != HAL_ZIGBEE_OK) {
            return;
        }
        cluster = button_event_cluster_find_by_endpoint(entry->endpoint);
        if (cluster != NULL) {
            cluster->last_event_seq = entry->payload.seq;
            hal_zigbee_notify_attribute_changed(
                cluster->endpoint, ZCL_CLUSTER_BUTTON_EVENT,
                ZCL_ATTR_BUTTON_EVENT_LAST_EVENT_SEQ);
        }
        button_event_cluster_pop();
    }
}

uint32_t button_event_cluster_dropped(void) {
    return button_event_queue.dropped;
}

uint8_t button_event_cluster_queue_used(void) {
    return button_event_queue.used;
}
