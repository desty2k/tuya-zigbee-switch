#include "base_components/button_dispatcher.h"
#include "base_components/button_input.h"
#include "support/fake_clock.h"
#include "support/fake_gpio.h"
#include "support/fake_tasks.h"
#include "support/runner.h"

#define FUZZ_PIN     ((hal_gpio_pin_t)3)
#define FUZZ_SEEDS   32

static uint16_t event_count;
static uint32_t last_press_id;
static button_event_type_t last_type;
static bool event_invariant_failed;

static void fuzz_event(const button_event_t *event, void *arg) {
    (void)arg;
    if (event_count != 0 && event->type == last_type) {
        event_invariant_failed = true;
    }
    if (event->type == BUTTON_EVENT_DOWN) {
        if (event->press_id != last_press_id + 1) {
            event_invariant_failed = true;
        }
        last_press_id = event->press_id;
    } else if (event->press_id != last_press_id) {
        event_invariant_failed = true;
    }
    last_type = event->type;
    event_count++;
}

TEST(deterministic_bounce_sequences) {
    for (uint32_t seed = 1; seed <= FUZZ_SEEDS; seed++) {
        button_config_t config = {
            .pin = FUZZ_PIN, .active_high = false, .debounce_ms = 8,
        };
        uint32_t now = seed * 100;
        uint8_t press_count = (uint8_t)(1 + seed % 12);

        fake_clock_reset();
        fake_tasks_reset();
        fake_gpio_reset();
        fake_gpio_set_input(FUZZ_PIN, 1);
        event_count            = 0;
        last_press_id          = 0;
        last_type              = BUTTON_EVENT_UP;
        event_invariant_failed = false;
        button_input_init();
        button_dispatcher_register(fuzz_event, NULL);
        button_input_add(&config);

        for (uint8_t press = 0; press < press_count; press++) {
            uint32_t release_at = now + 22 + seed % 30;

            fake_gpio_inject_edge(FUZZ_PIN, 0, now);
            fake_gpio_inject_edge(FUZZ_PIN, 1, now + 1);
            fake_gpio_inject_edge(FUZZ_PIN, 0, now + 2);
            fake_gpio_inject_edge(FUZZ_PIN, 1, release_at);
            now = release_at + 20 + seed % 40;
            if (press % 4 == 3) {
                fake_clock_set(now);
                fake_tasks_poll();
            }
        }
        fake_clock_set(now + 20);
        fake_tasks_poll();
        ASSERT_TRUE(!event_invariant_failed);
        ASSERT_EQ((uint16_t)press_count * 2, event_count);
        ASSERT_TRUE(!button_input_is_down(0));
    }
}

int main(void) {
    RUN_TEST(deterministic_bounce_sequences);
    return test_runner_failures == 0 ? 0 : 1;
}
