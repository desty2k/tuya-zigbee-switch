#include "base_components/button_dispatcher.h"
#include "base_components/button_input.h"
#include "support/fake_clock.h"
#include "support/fake_gpio.h"
#include "support/fake_tasks.h"
#include "support/runner.h"

#define TEST_PIN    ((hal_gpio_pin_t)1)
#define EVENT_MAX   32

static button_event_t events[EVENT_MAX];
static uint8_t        event_count;

static void record_event(const button_event_t *event, void *arg) {
    (void)arg;
    if (event_count < EVENT_MAX) {
        events[event_count++] = *event;
    }
}

static uint8_t setup_button(uint8_t initial_level, bool active_high) {
    button_config_t config = {
        .pin         = TEST_PIN,
        .active_high = active_high,
        .debounce_ms = 8,
    };

    fake_clock_reset();
    fake_tasks_reset();
    fake_gpio_reset();
    fake_gpio_set_input(TEST_PIN, initial_level);
    event_count = 0;
    button_input_init();
    button_dispatcher_register(record_event, NULL);
    return button_input_add(&config);
}

static void poll_at(uint32_t timestamp_ms) {
    fake_clock_set(timestamp_ms);
    fake_tasks_poll();
}

TEST(bounce_on_press) {
    setup_button(1, false);
    fake_gpio_inject_edge(TEST_PIN, 0, 100);
    fake_gpio_inject_edge(TEST_PIN, 1, 101);
    fake_gpio_inject_edge(TEST_PIN, 0, 102);
    fake_gpio_inject_edge(TEST_PIN, 1, 103);
    fake_gpio_inject_edge(TEST_PIN, 0, 104);
    poll_at(120);
    ASSERT_EQ(1, event_count);
    ASSERT_EQ(BUTTON_EVENT_DOWN, events[0].type);
    ASSERT_EQ(104, events[0].timestamp_ms);
}

TEST(very_short_valid_press) {
    setup_button(1, false);
    fake_gpio_inject_edge(TEST_PIN, 0, 100);
    fake_gpio_inject_edge(TEST_PIN, 1, 125);
    poll_at(140);
    ASSERT_EQ(2, event_count);
    ASSERT_EQ(100, events[0].timestamp_ms);
    ASSERT_EQ(125, events[1].timestamp_ms);
}

TEST(worker_delayed_preserves_four_events) {
    setup_button(1, false);
    fake_gpio_inject_edge(TEST_PIN, 0, 0);
    fake_gpio_inject_edge(TEST_PIN, 1, 20);
    fake_gpio_inject_edge(TEST_PIN, 0, 40);
    fake_gpio_inject_edge(TEST_PIN, 1, 60);
    poll_at(100);
    ASSERT_EQ(4, event_count);
    ASSERT_EQ(BUTTON_EVENT_DOWN, events[0].type);
    ASSERT_EQ(BUTTON_EVENT_UP, events[1].type);
    ASSERT_EQ(BUTTON_EVENT_DOWN, events[2].type);
    ASSERT_EQ(BUTTON_EVENT_UP, events[3].type);
    ASSERT_EQ(1, events[0].press_id);
    ASSERT_EQ(1, events[1].press_id);
    ASSERT_EQ(2, events[2].press_id);
    ASSERT_EQ(2, events[3].press_id);
}

TEST(release_blip_is_absorbed) {
    uint8_t button_id = setup_button(1, false);
    fake_gpio_inject_edge(TEST_PIN, 0, 100);
    fake_gpio_inject_edge(TEST_PIN, 1, 110);
    fake_gpio_inject_edge(TEST_PIN, 0, 111);
    poll_at(130);
    ASSERT_EQ(1, event_count);
    ASSERT_TRUE(button_input_is_down(button_id));
}

TEST(sub_debounce_spike_is_filtered) {
    setup_button(1, false);
    fake_gpio_inject_edge(TEST_PIN, 0, 100);
    fake_gpio_inject_edge(TEST_PIN, 1, 103);
    poll_at(200);
    ASSERT_EQ(0, event_count);
}

TEST(press_id_pairs_three_cycles) {
    setup_button(1, false);
    for (uint8_t i = 0; i < 3; i++) {
        uint32_t at = (uint32_t)i * 40 + 10;
        fake_gpio_inject_edge(TEST_PIN, 0, at);
        fake_gpio_inject_edge(TEST_PIN, 1, at + 20);
    }
    poll_at(140);
    ASSERT_EQ(6, event_count);
    for (uint8_t i = 0; i < 6; i++) {
        ASSERT_EQ((i / 2) + 1, events[i].press_id);
    }
}

TEST(boot_pressed_emits_only_release) {
    uint8_t button_id = setup_button(0, false);
    fake_gpio_inject_edge(TEST_PIN, 1, 50);
    poll_at(60);
    ASSERT_TRUE(button_input_boot_press(button_id));
    ASSERT_EQ(1, event_count);
    ASSERT_EQ(BUTTON_EVENT_UP, events[0].type);
    ASSERT_EQ(0, events[0].press_id);
}

TEST(active_high_flip_reseeds_state) {
    uint8_t button_id = setup_button(1, false);
    button_input_set_active_high(button_id, true);
    ASSERT_TRUE(button_input_is_down(button_id));
    ASSERT_EQ(0, event_count);
}

TEST(millis_wraparound) {
    setup_button(1, false);
    fake_gpio_inject_edge(TEST_PIN, 0, 0xFFFFFFF0u);
    fake_gpio_inject_edge(TEST_PIN, 1, 0x00000010u);
    poll_at(0x00000020u);
    ASSERT_EQ(2, event_count);
    ASSERT_EQ(BUTTON_EVENT_DOWN, events[0].type);
    ASSERT_EQ(BUTTON_EVENT_UP, events[1].type);
}

TEST(two_buttons_interleave_with_global_sequence) {
    button_config_t second = {
        .pin = 2, .active_high = false, .debounce_ms = 8,
    };
    setup_button(1, false);
    fake_gpio_set_input(2, 1);
    button_input_add(&second);
    fake_gpio_inject_edge(TEST_PIN, 0, 10);
    fake_gpio_inject_edge(2, 0, 20);
    fake_gpio_inject_edge(TEST_PIN, 1, 30);
    fake_gpio_inject_edge(2, 1, 40);
    poll_at(60);
    ASSERT_EQ(4, event_count);
    for (uint8_t i = 1; i < event_count; i++) {
        ASSERT_EQ(events[i - 1].seq + 1, events[i].seq);
    }
}

int main(void) {
    RUN_TEST(bounce_on_press);
    RUN_TEST(very_short_valid_press);
    RUN_TEST(worker_delayed_preserves_four_events);
    RUN_TEST(release_blip_is_absorbed);
    RUN_TEST(sub_debounce_spike_is_filtered);
    RUN_TEST(press_id_pairs_three_cycles);
    RUN_TEST(boot_pressed_emits_only_release);
    RUN_TEST(active_high_flip_reseeds_state);
    RUN_TEST(millis_wraparound);
    RUN_TEST(two_buttons_interleave_with_global_sequence);
    return test_runner_failures == 0 ? 0 : 1;
}
