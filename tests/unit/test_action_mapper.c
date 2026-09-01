#include "base_components/action_mapper.h"
#include "support/fake_clock.h"
#include "support/fake_relay_hw.h"
#include "support/fake_tasks.h"
#include "support/runner.h"
#include <stdint.h>

uint8_t allow_simultaneous_latching_pulses;
uint8_t g_multi_press_reset_count;

static gesture_sink_t registered_gesture_sink;

typedef struct {
    uint8_t cover_count, binding_count, button_count, system_count;
    uint8_t target;
    uint16_t value;
    action_type_t binding_type;
} sink_log_t;

static sink_log_t log;

void gesture_fsm_register_sink(gesture_sink_t sink, void *context) {
    (void)context;
    registered_gesture_sink = sink;
}

static void cover_sink(void *context, uint8_t target, uint16_t command) {
    sink_log_t *sink_log = context;
    sink_log->cover_count++;
    sink_log->target = target;
    sink_log->value = command;
}

static void binding_sink(void *context, uint8_t target, action_type_t type,
                         uint16_t intent) {
    sink_log_t *sink_log = context;
    sink_log->binding_count++;
    sink_log->target = target;
    sink_log->binding_type = type;
    sink_log->value = intent;
}

static void button_sink(void *context, uint8_t target, uint16_t event) {
    sink_log_t *sink_log = context;
    sink_log->button_count++;
    sink_log->target = target;
    sink_log->value = event;
}

static void system_sink(void *context, uint8_t action, uint16_t delay_ms) {
    sink_log_t *sink_log = context;
    sink_log->system_count++;
    sink_log->target = action;
    sink_log->value = delay_ms;
}

static void reset_mapper(void) {
    relay_driver_t driver = { .on_pin = 10, .off_pin = HAL_INVALID_PIN,
                              .on_high = 1, .is_latching = 0 };
    static relay_driver_t persistent_driver;
    static relay_config_t config;

    fake_clock_reset();
    fake_tasks_reset();
    fake_relay_hw_reset();
    persistent_driver = driver;
    config = (relay_config_t){ 0 };
    relay_ctrl_init();
    ASSERT_EQ(0, relay_ctrl_add(&persistent_driver, &config));
    log = (sink_log_t){ 0 };
    action_mapper_init();
    ASSERT_TRUE(registered_gesture_sink != NULL);
    action_mapper_set_sinks(&(action_mapper_sinks_t){
        .cover = cover_sink, .binding = binding_sink, .button_event = button_sink,
        .system = system_sink, .context = &log,
    });
}

TEST(dispatches_typed_targets_to_fake_sinks) {
    reset_mapper();
    ASSERT_TRUE(action_mapper_add(&(action_mapping_t){
        .trigger = { .button_id = 0, .type = ACTION_TRIGGER_DOWN },
        .target = { .type = ACTION_RELAY, .target_id = 0,
                    .argument = RELAY_REQUEST_ON },
    }));
    ASSERT_TRUE(action_mapper_add(&(action_mapping_t){
        .trigger = { .button_id = 1, .type = ACTION_TRIGGER_UP },
        .target = { .type = ACTION_COVER, .target_id = 2, .argument = 7 },
    }));
    ASSERT_TRUE(action_mapper_add(&(action_mapping_t){
        .trigger = { .button_id = 2, .type = ACTION_TRIGGER_HOLD },
        .target = { .type = ACTION_BIND_LEVEL, .target_id = 3, .argument = 9 },
    }));
    ASSERT_TRUE(action_mapper_add(&(action_mapping_t){
        .trigger = { .button_id = 3, .type = ACTION_TRIGGER_N_CLICK, .count = 2 },
        .target = { .type = ACTION_BUTTON_EVENT, .target_id = 4, .argument = 11 },
    }));
    ASSERT_TRUE(action_mapper_add(&(action_mapping_t){
        .trigger = { .button_id = 4, .type = ACTION_TRIGGER_HOLD },
        .target = { .type = ACTION_SYSTEM, .target_id = 1, .argument = 12 },
    }));

    action_mapper_dispatch(&(action_trigger_t){ .button_id = 0, .type = ACTION_TRIGGER_DOWN });
    ASSERT_TRUE(relay_ctrl_is_on(0));
    action_mapper_dispatch(&(action_trigger_t){ .button_id = 1, .type = ACTION_TRIGGER_UP });
    ASSERT_EQ(1, log.cover_count); ASSERT_EQ(2, log.target); ASSERT_EQ(7, log.value);
    action_mapper_dispatch(&(action_trigger_t){ .button_id = 2, .type = ACTION_TRIGGER_HOLD });
    ASSERT_EQ(1, log.binding_count); ASSERT_EQ(ACTION_BIND_LEVEL, log.binding_type);
    action_mapper_dispatch(&(action_trigger_t){ .button_id = 3, .type = ACTION_TRIGGER_N_CLICK, .count = 1 });
    ASSERT_EQ(0, log.button_count);
    action_mapper_dispatch(&(action_trigger_t){ .button_id = 3, .type = ACTION_TRIGGER_N_CLICK, .count = 2 });
    ASSERT_EQ(1, log.button_count); ASSERT_EQ(4, log.target); ASSERT_EQ(11, log.value);
    action_mapper_dispatch(&(action_trigger_t){ .button_id = 4, .type = ACTION_TRIGGER_HOLD });
    ASSERT_EQ(1, log.system_count); ASSERT_EQ(1, log.target); ASSERT_EQ(12, log.value);
}

TEST(mapping_capacity_and_gesture_reset_are_bounded) {
    reset_mapper();
    for (uint8_t i = 0; i < ACTION_MAPPER_MAX_RECORDS; i++) {
        ASSERT_TRUE(action_mapper_add(&(action_mapping_t){
            .trigger = { .button_id = i % BUTTON_INPUT_MAX, .type = ACTION_TRIGGER_DOWN },
            .target = { .type = ACTION_NONE },
        }));
    }
    ASSERT_TRUE(!action_mapper_add(&(action_mapping_t){
        .trigger = { .button_id = 0, .type = ACTION_TRIGGER_DOWN },
    }));
    action_mapper_clear();
    action_mapper_add_reset_button(1, true);
    registered_gesture_sink(&(gesture_event_t){ .button_id = 1, .type = GESTURE_HOLD_START }, NULL);
    ASSERT_EQ(1, log.system_count);
}

int main(void) {
    RUN_TEST(dispatches_typed_targets_to_fake_sinks);
    RUN_TEST(mapping_capacity_and_gesture_reset_are_bounded);
    return test_runner_failures == 0 ? 0 : 1;
}
