#include "base_components/button_dispatcher.h"
#include "base_components/gesture_fsm.h"
#include "support/runner.h"
#include "zigbee/button_event_cluster.h"
#include "zigbee/consts.h"
#include <string.h>

#define SENT_MAX    32

typedef struct {
    uint8_t endpoint;
    uint8_t payload[8];
} sent_event_t;

static uint32_t                    now_ms;
static hal_zigbee_network_status_t network_status;
static bool                        send_fails;
static sent_event_t                sent[SENT_MAX];
static uint8_t                     sent_count;
static zigbee_button_event_cluster test_cluster;
static hal_zigbee_cluster          endpoint_clusters[2];
static hal_zigbee_endpoint         endpoint;

uint32_t hal_millis(void) {
    return now_ms;
}

hal_zigbee_network_status_t hal_zigbee_get_network_status(void) {
    return network_status;
}

hal_zigbee_status_t
hal_zigbee_send_cmd_to_coordinator(const hal_zigbee_cmd *cmd) {
    if (send_fails) {
        return HAL_ZIGBEE_ERR_SEND_FAILED;
    }
    if (sent_count >= SENT_MAX) {
        return HAL_ZIGBEE_ERR_SEND_FAILED;
    }
    sent[sent_count].endpoint = cmd->endpoint;
    memcpy(sent[sent_count].payload, cmd->payload, 8);
    sent_count++;
    return HAL_ZIGBEE_OK;
}

void hal_zigbee_notify_attribute_changed(uint8_t endpoint_id,
                                         uint16_t cluster_id,
                                         uint16_t attribute_id) {
    (void)endpoint_id;
    (void)cluster_id;
    (void)attribute_id;
}

bool button_input_is_down(uint8_t button_id) {
    (void)button_id;
    return false;
}

void button_input_set_debounce_ms(uint8_t button_id, uint16_t debounce_ms) {
    (void)button_id;
    (void)debounce_ms;
}

void gesture_fsm_set_multi_click_gap_ms(uint8_t button_id, uint16_t gap_ms) {
    (void)button_id;
    (void)gap_ms;
}

void button_dispatcher_register(button_event_sink_t sink, void *arg) {
    (void)sink;
    (void)arg;
}

void gesture_fsm_register_sink(gesture_sink_t sink, void *arg) {
    (void)sink;
    (void)arg;
}

static uint16_t decode_u16(const uint8_t *bytes) {
    return (uint16_t)(bytes[0] | ((uint16_t)bytes[1] << 8));
}

static void reset_queue(void) {
    uint8_t button_id = 0;

    now_ms        = 0;
    network_status = HAL_ZIGBEE_NETWORK_JOINED;
    send_fails    = true;
    sent_count    = 0;
    memset(sent, 0, sizeof(sent));
    memset(&test_cluster, 0, sizeof(test_cluster));
    memset(endpoint_clusters, 0, sizeof(endpoint_clusters));
    memset(&endpoint, 0, sizeof(endpoint));
    endpoint.endpoint = 1;
    endpoint.clusters = endpoint_clusters;
    button_event_cluster_init();
    button_event_cluster_add_to_endpoint(&test_cluster, &endpoint, &button_id,
                                         1, 350, 8, NULL, NULL);
}

static void emit_button(uint32_t press_id) {
    button_event_t event = {
        .button_id    = 0,
        .type         = BUTTON_EVENT_DOWN,
        .timestamp_ms = now_ms,
        .seq          = press_id,
        .press_id     = press_id,
    };

    button_event_cluster_on_button_event(&event, NULL);
}

TEST(events_drain_in_fifo_order) {
    reset_queue();
    emit_button(10);
    emit_button(11);
    emit_button(12);
    ASSERT_EQ(3, button_event_cluster_queue_used());

    send_fails = false;
    button_event_cluster_drain();
    ASSERT_EQ(0, button_event_cluster_queue_used());
    ASSERT_EQ(3, sent_count);
    ASSERT_EQ(0, decode_u16(sent[0].payload));
    ASSERT_EQ(1, decode_u16(sent[1].payload));
    ASSERT_EQ(2, decode_u16(sent[2].payload));
    ASSERT_EQ(10, decode_u16(&sent[0].payload[2]));
    ASSERT_EQ(12, decode_u16(&sent[2].payload[2]));
}

TEST(overflow_drops_oldest_and_keeps_newest) {
    reset_queue();
    for (uint8_t i = 0; i < ZB_BUTTON_EVENT_QUEUE_SIZE + 2; i++) {
        emit_button(i + 1);
    }
    ASSERT_EQ(ZB_BUTTON_EVENT_QUEUE_SIZE, button_event_cluster_queue_used());
    ASSERT_EQ(2, button_event_cluster_dropped());

    send_fails = false;
    button_event_cluster_drain();
    ASSERT_EQ(ZB_BUTTON_EVENT_QUEUE_SIZE, sent_count);
    ASSERT_EQ(2, decode_u16(sent[0].payload));
    ASSERT_EQ(17, decode_u16(sent[ZB_BUTTON_EVENT_QUEUE_SIZE - 1].payload));
}

TEST(sequence_advances_while_not_joined) {
    reset_queue();
    network_status = HAL_ZIGBEE_NETWORK_NOT_JOINED;
    emit_button(1);
    emit_button(2);
    emit_button(3);
    ASSERT_EQ(0, button_event_cluster_queue_used());

    network_status = HAL_ZIGBEE_NETWORK_JOINED;
    send_fails     = false;
    emit_button(4);
    ASSERT_EQ(1, sent_count);
    ASSERT_EQ(3, decode_u16(sent[0].payload));
}

TEST(expired_entries_are_not_transmitted) {
    reset_queue();
    emit_button(1);
    now_ms = ZB_BUTTON_EVENT_TTL_MS + 1;
    send_fails = false;
    button_event_cluster_drain();
    ASSERT_EQ(0, sent_count);
    ASSERT_EQ(0, button_event_cluster_queue_used());
}

TEST(queued_payload_is_an_immutable_snapshot) {
    button_event_t event = {
        .button_id    = 0,
        .type         = BUTTON_EVENT_DOWN,
        .timestamp_ms = 0,
        .seq          = 1,
        .press_id     = 0x1234,
    };

    reset_queue();
    button_event_cluster_on_button_event(&event, NULL);
    event.press_id = 0x9999;
    event.type     = BUTTON_EVENT_UP;
    send_fails     = false;
    button_event_cluster_drain();
    ASSERT_EQ(0x1234, decode_u16(&sent[0].payload[2]));
    ASSERT_EQ(ZB_BUTTON_DOWN, sent[0].payload[4]);
}

int main(void) {
    RUN_TEST(events_drain_in_fifo_order);
    RUN_TEST(overflow_drops_oldest_and_keeps_newest);
    RUN_TEST(sequence_advances_while_not_joined);
    RUN_TEST(expired_entries_are_not_transmitted);
    RUN_TEST(queued_payload_is_an_immutable_snapshot);
    return test_runner_failures == 0 ? 0 : 1;
}
