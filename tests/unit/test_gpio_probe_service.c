#include "base_components/gpio_probe_service.h"
#include "stub/hal/gpio_probe.h"
#include "support/fake_clock.h"
#include "support/fake_tasks.h"
#include "support/runner.h"

#define PA0    HAL_GPIO_PROBE_ID(0, 0)
#define PA1    HAL_GPIO_PROBE_ID(0, 1)
#define PA2    HAL_GPIO_PROBE_ID(0, 2)
#define PA3    HAL_GPIO_PROBE_ID(0, 3)
#define PA4    HAL_GPIO_PROBE_ID(0, 4)
#define PA7    HAL_GPIO_PROBE_ID(0, 7)
#define PC3    HAL_GPIO_PROBE_ID(2, 3)

static void setup(void) {
    fake_clock_reset();
    fake_tasks_reset();
    stub_gpio_probe_reset();
    gpio_probe_service_init();
    gpio_probe_service_enable();
}

/* Main loop and timer tasks, one millisecond at a time. */
static void run_ms(uint32_t ms) {
    for (uint32_t i = 0; i < ms; i++) {
        fake_clock_advance_ms(1);
        fake_tasks_poll();
        gpio_probe_service_poll();
    }
}

static int8_t position_of(hal_gpio_probe_id_t pin) {
    for (uint8_t i = 0; i < gpio_probe_service_pin_count(); i++) {
        if (gpio_probe_service_pin(i) == pin) {
            return (int8_t)i;
        }
    }
    return -1;
}

static gpio_probe_class_t class_of(hal_gpio_probe_id_t pin) {
    return gpio_probe_service_class((uint8_t)position_of(pin));
}

static bool released(hal_gpio_probe_id_t pin) {
    stub_probe_pin_info_t info;

    return stub_gpio_probe_info(pin, &info) && !info.output_enabled &&
           info.pull == HAL_GPIO_PULL_NONE;
}

static uint32_t drives(hal_gpio_probe_id_t pin) {
    stub_probe_pin_info_t info;

    stub_gpio_probe_info(pin, &info);
    return info.drives;
}

static uint32_t contention(hal_gpio_probe_id_t pin) {
    stub_probe_pin_info_t info;

    stub_gpio_probe_info(pin, &info);
    return info.contention;
}

static void classify_all(void) {
    gpio_probe_service_pull_test(NULL, 0);
    run_ms(4 * GPIO_PROBE_PULL_PHASE_MS + 1);
}

TEST(boot_touches_no_pin) {
    const hal_gpio_probe_id_t *ids;
    uint8_t count;

    setup();
    count = hal_gpio_probe_pins(&ids);
    ASSERT_EQ(31, count);
    run_ms(1000);
    for (uint8_t i = 0; i < count; i++) {
        ASSERT_EQ(0, drives(ids[i]));
        ASSERT_TRUE(released(ids[i]));
    }
    ASSERT_EQ(0, gpio_probe_service_state());
}

TEST(platform_and_claimed_pins_are_not_probeable) {
    gpio_probe_watch_pin_t watch = { PC3, GPIO_PROBE_PULL_NONE };

    fake_clock_reset();
    fake_tasks_reset();
    stub_gpio_probe_reset();
    gpio_probe_service_init();
    gpio_probe_service_claim(2, 3);
    gpio_probe_service_enable();
    ASSERT_EQ(30, gpio_probe_service_pin_count());
    ASSERT_EQ(-1, position_of(PC3));
    ASSERT_EQ(-1, position_of(PA7));
    ASSERT_EQ(GPIO_PROBE_STATUS_BAD_PIN, gpio_probe_service_arm(PC3));
    ASSERT_EQ(GPIO_PROBE_STATUS_BAD_PIN, gpio_probe_service_arm(PA7));
    ASSERT_EQ(GPIO_PROBE_STATUS_BAD_PIN, gpio_probe_service_watch(&watch, 1, 1));
    ASSERT_EQ(GPIO_PROBE_STATUS_BAD_PIN, gpio_probe_service_pull_test(&watch.pin, 1));
    classify_all();
    ASSERT_EQ(0, drives(PC3));
    ASSERT_TRUE(released(PC3));
}

TEST(disabled_probe_refuses_every_operation) {
    uint32_t levels;
    gpio_probe_watch_pin_t watch = { PA0, GPIO_PROBE_PULL_NONE };

    fake_clock_reset();
    fake_tasks_reset();
    stub_gpio_probe_reset();
    gpio_probe_service_init();
    ASSERT_EQ(GPIO_PROBE_STATUS_DISABLED, gpio_probe_service_snapshot(&levels));
    ASSERT_EQ(GPIO_PROBE_STATUS_DISABLED, gpio_probe_service_pull_test(NULL, 0));
    ASSERT_EQ(GPIO_PROBE_STATUS_DISABLED, gpio_probe_service_watch(&watch, 1, 1));
    ASSERT_EQ(GPIO_PROBE_STATUS_DISABLED, gpio_probe_service_arm(PA0));
    ASSERT_EQ(GPIO_PROBE_STATUS_DISABLED, gpio_probe_service_pulse(PA0, 1, 10));
    ASSERT_EQ(0, drives(PA0));
}

TEST(snapshot_reads_levels_without_changing_modes) {
    uint32_t levels = 0;

    setup();
    stub_gpio_probe_set_driven(PA1, 1);
    ASSERT_EQ(GPIO_PROBE_STATUS_OK, gpio_probe_service_snapshot(&levels));
    ASSERT_EQ(1, (levels >> position_of(PA1)) & 1u);
    ASSERT_EQ(0, (levels >> position_of(PA0)) & 1u);
    ASSERT_TRUE(released(PA1));
    ASSERT_TRUE(released(PA0));
}

TEST(pull_test_classifies_floating_driven_and_active_pins) {
    setup();
    stub_gpio_probe_set_driven(PA1, 1);
    stub_gpio_probe_set_driven(PA2, 0);
    stub_gpio_probe_set_train(PA3, 1300, 600);
    ASSERT_EQ(GPIO_PROBE_STATUS_OK, gpio_probe_service_pull_test(NULL, 0));
    ASSERT_TRUE(gpio_probe_service_state() & GPIO_PROBE_STATE_PULL_TEST);
    ASSERT_EQ(GPIO_PROBE_STATUS_BUSY, gpio_probe_service_pull_test(NULL, 0));
    run_ms(4 * GPIO_PROBE_PULL_PHASE_MS + 1);
    ASSERT_EQ(0, gpio_probe_service_state());
    ASSERT_EQ(GPIO_PROBE_CLASS_FLOATING, class_of(PA0));
    ASSERT_EQ(GPIO_PROBE_CLASS_DRIVEN_HIGH, class_of(PA1));
    ASSERT_EQ(GPIO_PROBE_CLASS_DRIVEN_LOW, class_of(PA2));
    ASSERT_EQ(GPIO_PROBE_CLASS_ACTIVE, class_of(PA3));
    ASSERT_TRUE(released(PA0));
    ASSERT_TRUE(released(PA1));
    ASSERT_TRUE(released(PA3));
    ASSERT_EQ(0, drives(PA0));
}

TEST(pull_test_of_selected_pins_leaves_others_untested) {
    hal_gpio_probe_id_t pins[] = { PA1 };

    setup();
    stub_gpio_probe_set_driven(PA1, 0);
    ASSERT_EQ(GPIO_PROBE_STATUS_OK, gpio_probe_service_pull_test(pins, 1));
    run_ms(4 * GPIO_PROBE_PULL_PHASE_MS + 1);
    ASSERT_EQ(GPIO_PROBE_CLASS_DRIVEN_LOW, class_of(PA1));
    ASSERT_EQ(GPIO_PROBE_CLASS_UNTESTED, class_of(PA0));
}

TEST(pulse_is_refused_on_untested_driven_high_and_active_pins) {
    setup();
    ASSERT_EQ(GPIO_PROBE_STATUS_NOT_FLOATING, gpio_probe_service_arm(PA0));
    stub_gpio_probe_set_driven(PA1, 1);
    stub_gpio_probe_set_train(PA3, 1300, 600);
    classify_all();
    ASSERT_EQ(GPIO_PROBE_STATUS_NOT_FLOATING, gpio_probe_service_arm(PA1));
    ASSERT_EQ(GPIO_PROBE_STATUS_NOT_FLOATING, gpio_probe_service_arm(PA3));
    ASSERT_EQ(GPIO_PROBE_STATUS_NOT_ARMED, gpio_probe_service_pulse(PA1, 1, 10));
    ASSERT_EQ(GPIO_PROBE_STATUS_NOT_ARMED, gpio_probe_service_pulse(PA3, 1, 10));
    ASSERT_EQ(GPIO_PROBE_STATUS_NOT_ARMED, gpio_probe_service_pulse(PA0, 1, 10));
    ASSERT_EQ(0, drives(PA0));
    ASSERT_EQ(0, drives(PA1));
    ASSERT_EQ(0, drives(PA3));
}

TEST(driven_low_pin_takes_only_a_short_high_pulse) {
    stub_probe_pin_info_t info;

    setup();
    stub_gpio_probe_set_driven(PA2, 0);
    classify_all();
    ASSERT_EQ(GPIO_PROBE_CLASS_DRIVEN_LOW, class_of(PA2));
    ASSERT_EQ(GPIO_PROBE_STATUS_OK, gpio_probe_service_arm(PA2));
    ASSERT_EQ(GPIO_PROBE_STATUS_RANGE, gpio_probe_service_pulse(PA2, 0, 10));
    ASSERT_EQ(GPIO_PROBE_STATUS_OK, gpio_probe_service_arm(PA2));
    ASSERT_EQ(GPIO_PROBE_STATUS_RANGE,
              gpio_probe_service_pulse(PA2, 1, GPIO_PROBE_DRIVEN_LOW_PULSE_MAX_MS + 1));
    ASSERT_EQ(0, drives(PA2));
    ASSERT_EQ(GPIO_PROBE_STATUS_OK, gpio_probe_service_arm(PA2));
    ASSERT_EQ(GPIO_PROBE_STATUS_OK,
              gpio_probe_service_pulse(PA2, 1, GPIO_PROBE_DRIVEN_LOW_PULSE_MAX_MS));
    ASSERT_TRUE(stub_gpio_probe_info(PA2, &info));
    ASSERT_TRUE(info.output_enabled);
    ASSERT_EQ(1, info.output_level);
    ASSERT_EQ(1, contention(PA2));
    run_ms(GPIO_PROBE_DRIVEN_LOW_PULSE_MAX_MS);
    ASSERT_TRUE(released(PA2));
    ASSERT_TRUE(stub_gpio_probe_info(PA2, &info));
    ASSERT_EQ(0, info.output_level);
    ASSERT_EQ(1, drives(PA2));
}

TEST(driven_low_pin_must_read_driven_low_right_before_drive) {
    setup();
    stub_gpio_probe_set_driven(PA2, 0);
    classify_all();
    ASSERT_EQ(GPIO_PROBE_STATUS_OK, gpio_probe_service_arm(PA2));
    stub_gpio_probe_set_floating(PA2);
    ASSERT_EQ(GPIO_PROBE_STATUS_VERIFY_FAILED, gpio_probe_service_pulse(PA2, 1, 10));
    ASSERT_EQ(GPIO_PROBE_CLASS_FLOATING, class_of(PA2));
    ASSERT_TRUE(released(PA2));
    ASSERT_EQ(0, drives(PA2));
}

TEST(each_pulse_needs_its_own_arm) {
    setup();
    classify_all();
    ASSERT_EQ(GPIO_PROBE_STATUS_OK, gpio_probe_service_arm(PA0));
    ASSERT_EQ(GPIO_PROBE_STATUS_NOT_ARMED, gpio_probe_service_pulse(PA4, 1, 10));
    ASSERT_EQ(GPIO_PROBE_STATUS_NOT_ARMED, gpio_probe_service_pulse(PA0, 1, 10));
    ASSERT_EQ(GPIO_PROBE_STATUS_OK, gpio_probe_service_arm(PA0));
    ASSERT_EQ(GPIO_PROBE_STATUS_RANGE, gpio_probe_service_pulse(PA0, 1, 0));
    ASSERT_EQ(GPIO_PROBE_STATUS_NOT_ARMED, gpio_probe_service_pulse(PA0, 1, 10));
    ASSERT_EQ(GPIO_PROBE_STATUS_OK, gpio_probe_service_arm(PA0));
    ASSERT_EQ(GPIO_PROBE_STATUS_RANGE,
              gpio_probe_service_pulse(PA0, 1, GPIO_PROBE_PULSE_MAX_MS + 1));
    ASSERT_EQ(GPIO_PROBE_STATUS_OK, gpio_probe_service_arm(PA0));
    ASSERT_EQ(GPIO_PROBE_STATUS_OK, gpio_probe_service_pulse(PA0, 1, 10));
    run_ms(20);
    ASSERT_EQ(1, drives(PA0));
    ASSERT_EQ(GPIO_PROBE_STATUS_NOT_ARMED, gpio_probe_service_pulse(PA0, 1, 10));
    ASSERT_EQ(1, drives(PA0));
}

TEST(arm_expires) {
    setup();
    classify_all();
    ASSERT_EQ(GPIO_PROBE_STATUS_OK, gpio_probe_service_arm(PA0));
    ASSERT_TRUE(gpio_probe_service_state() & GPIO_PROBE_STATE_ARMED);
    run_ms(GPIO_PROBE_ARM_TIMEOUT_MS + 1);
    ASSERT_EQ(0, gpio_probe_service_state() & GPIO_PROBE_STATE_ARMED);
    ASSERT_EQ(GPIO_PROBE_STATUS_NOT_ARMED, gpio_probe_service_pulse(PA0, 1, 10));
    ASSERT_EQ(0, drives(PA0));
}

TEST(pin_must_follow_both_pulls_right_before_drive) {
    setup();
    classify_all();
    ASSERT_EQ(GPIO_PROBE_CLASS_FLOATING, class_of(PA0));
    stub_gpio_probe_set_driven(PA0, 0);
    ASSERT_EQ(GPIO_PROBE_STATUS_OK, gpio_probe_service_arm(PA0));
    ASSERT_EQ(GPIO_PROBE_STATUS_VERIFY_FAILED, gpio_probe_service_pulse(PA0, 1, 10));
    ASSERT_EQ(GPIO_PROBE_CLASS_DRIVEN_LOW, class_of(PA0));
    ASSERT_EQ(0, drives(PA0));
    ASSERT_EQ(0, contention(PA0));
    ASSERT_TRUE(released(PA0));
}

TEST(pulse_drives_requested_level_then_timer_cuts_off) {
    stub_probe_pin_info_t info;

    setup();
    classify_all();
    ASSERT_EQ(GPIO_PROBE_STATUS_OK, gpio_probe_service_arm(PA0));
    ASSERT_EQ(GPIO_PROBE_STATUS_OK, gpio_probe_service_pulse(PA0, 1, 100));
    ASSERT_TRUE(stub_gpio_probe_info(PA0, &info));
    ASSERT_TRUE(info.output_enabled);
    ASSERT_EQ(1, info.output_level);
    ASSERT_TRUE(gpio_probe_service_state() & GPIO_PROBE_STATE_PULSE);
    /* Timer only: the main loop does not run. */
    fake_clock_advance_ms(99);
    fake_tasks_poll();
    ASSERT_TRUE(stub_gpio_probe_info(PA0, &info));
    ASSERT_TRUE(info.output_enabled);
    fake_clock_advance_ms(1);
    fake_tasks_poll();
    ASSERT_TRUE(released(PA0));
    ASSERT_EQ(0, gpio_probe_service_state() & GPIO_PROBE_STATE_PULSE);
    ASSERT_EQ(0, fake_tasks_pending());
}

TEST(main_loop_cuts_off_a_pulse_whose_timer_is_late) {
    setup();
    classify_all();
    ASSERT_EQ(GPIO_PROBE_STATUS_OK, gpio_probe_service_arm(PA0));
    ASSERT_EQ(GPIO_PROBE_STATUS_OK, gpio_probe_service_pulse(PA0, 0, 50));
    /* Main loop only: the timer task does not run. */
    fake_clock_advance_ms(49);
    gpio_probe_service_poll();
    ASSERT_TRUE(!released(PA0));
    fake_clock_advance_ms(1);
    gpio_probe_service_poll();
    ASSERT_TRUE(released(PA0));
    ASSERT_EQ(0, fake_tasks_pending());
}

TEST(pulse_is_refused_when_cutoff_timer_cannot_be_armed) {
    setup();
    classify_all();
    ASSERT_EQ(GPIO_PROBE_STATUS_OK, gpio_probe_service_arm(PA0));
    fake_tasks_reject_next(1);
    ASSERT_EQ(GPIO_PROBE_STATUS_RESOURCE, gpio_probe_service_pulse(PA0, 1, 100));
    ASSERT_EQ(0, drives(PA0));
    ASSERT_TRUE(released(PA0));
}

TEST(only_one_pin_pulses_at_a_time) {
    setup();
    classify_all();
    ASSERT_EQ(GPIO_PROBE_STATUS_OK, gpio_probe_service_arm(PA0));
    ASSERT_EQ(GPIO_PROBE_STATUS_OK, gpio_probe_service_pulse(PA0, 1, 200));
    ASSERT_EQ(GPIO_PROBE_STATUS_BUSY, gpio_probe_service_arm(PA4));
    ASSERT_EQ(GPIO_PROBE_STATUS_BUSY, gpio_probe_service_pull_test(NULL, 0));
    ASSERT_EQ(GPIO_PROBE_STATUS_BUSY, gpio_probe_service_pulse(PA4, 1, 10));
    ASSERT_EQ(0, drives(PA4));
    run_ms(200);
    ASSERT_TRUE(released(PA0));
    ASSERT_EQ(GPIO_PROBE_STATUS_OK, gpio_probe_service_arm(PA4));
}

TEST(stop_ends_every_operation_and_restores_pins) {
    gpio_probe_watch_pin_t watch = { PA3, GPIO_PROBE_PULL_UP };

    setup();
    classify_all();
    ASSERT_EQ(GPIO_PROBE_STATUS_OK, gpio_probe_service_watch(&watch, 1, 60));
    ASSERT_EQ(GPIO_PROBE_STATUS_OK, gpio_probe_service_arm(PA0));
    ASSERT_EQ(GPIO_PROBE_STATUS_OK, gpio_probe_service_pulse(PA0, 1, 500));
    ASSERT_TRUE(!released(PA0));
    ASSERT_TRUE(!released(PA3));
    gpio_probe_service_stop();
    ASSERT_EQ(0, gpio_probe_service_state());
    ASSERT_TRUE(released(PA0));
    ASSERT_TRUE(released(PA3));
    ASSERT_EQ(0, fake_tasks_pending());
    ASSERT_EQ(GPIO_PROBE_CLASS_FLOATING, class_of(PA0));
    ASSERT_EQ(0, gpio_probe_service_watch_result(0)->flags & GPIO_PROBE_WATCH_RUNNING);

    gpio_probe_service_pull_test(NULL, 0);
    run_ms(10);
    gpio_probe_service_stop();
    ASSERT_EQ(0, gpio_probe_service_state());
    ASSERT_TRUE(released(PA1));
}

TEST(watch_counts_a_pulse_train_and_ends_after_its_duration) {
    gpio_probe_watch_pin_t watch[] = {
        { PA3, GPIO_PROBE_PULL_NONE },
        { PA4, GPIO_PROBE_PULL_UP   },
    };
    const gpio_probe_watch_result_t *train;
    const gpio_probe_watch_result_t *button;

    setup();
    stub_gpio_probe_set_train(PA3, 2000, 1000);
    stub_gpio_probe_set_driven(PA4, 1);
    ASSERT_EQ(GPIO_PROBE_STATUS_OK, gpio_probe_service_watch(watch, 2, 3));
    ASSERT_EQ(GPIO_PROBE_STATUS_BUSY, gpio_probe_service_watch(watch, 1, 1));
    run_ms(500);
    stub_gpio_probe_set_driven(PA4, 0);
    run_ms(100);
    stub_gpio_probe_set_driven(PA4, 1);
    run_ms(1000);
    stub_gpio_probe_set_driven(PA4, 0);
    run_ms(200);
    stub_gpio_probe_set_driven(PA4, 1);
    run_ms(1199);
    ASSERT_TRUE(gpio_probe_service_state() & GPIO_PROBE_STATE_WATCH);
    ASSERT_EQ(GPIO_PROBE_WATCH_RUNNING, gpio_probe_service_watch_result(0)->flags);
    run_ms(1);
    ASSERT_EQ(0, gpio_probe_service_state() & GPIO_PROBE_STATE_WATCH);
    ASSERT_EQ(2, gpio_probe_service_watch_count());
    ASSERT_EQ(3000, gpio_probe_service_watch_elapsed_ms());
    train  = gpio_probe_service_watch_result(0);
    button = gpio_probe_service_watch_result(1);
    ASSERT_EQ(PA3, train->pin);
    ASSERT_EQ(1500, train->edges);
    ASSERT_EQ(2000, train->min_interval_us);
    ASSERT_EQ(2000, train->max_interval_us);
    ASSERT_EQ(0, train->flags);
    ASSERT_EQ(2, button->edges);
    ASSERT_EQ(1200000, button->min_interval_us);
    ASSERT_EQ(1200000, button->max_interval_us);
    ASSERT_EQ(1, button->level);
    ASSERT_TRUE(released(PA3));
    ASSERT_TRUE(released(PA4));
}

TEST(watch_rejects_bad_requests) {
    gpio_probe_watch_pin_t watch[] = {
        { PA0, GPIO_PROBE_PULL_NONE },
        { PA1, GPIO_PROBE_PULL_NONE },
        { PA2, GPIO_PROBE_PULL_NONE },
    };
    gpio_probe_watch_pin_t twice[] = {
        { PA0, GPIO_PROBE_PULL_NONE },
        { PA0, GPIO_PROBE_PULL_UP   },
    };
    gpio_probe_watch_pin_t bad_pull = { PA0, 3 };

    setup();
    ASSERT_EQ(GPIO_PROBE_STATUS_RANGE, gpio_probe_service_watch(watch, 1, 0));
    ASSERT_EQ(GPIO_PROBE_STATUS_RANGE,
              gpio_probe_service_watch(watch, 1, GPIO_PROBE_WATCH_MAX_S + 1));
    ASSERT_EQ(GPIO_PROBE_STATUS_RANGE, gpio_probe_service_watch(watch, 0, 1));
    ASSERT_EQ(GPIO_PROBE_STATUS_RESOURCE, gpio_probe_service_watch(watch, 3, 1));
    ASSERT_EQ(GPIO_PROBE_STATUS_RANGE, gpio_probe_service_watch(twice, 2, 1));
    ASSERT_EQ(GPIO_PROBE_STATUS_RANGE, gpio_probe_service_watch(&bad_pull, 1, 1));
    ASSERT_EQ(0, gpio_probe_service_state());
}

TEST(pins_under_watch_cannot_be_pulsed_or_pull_tested) {
    gpio_probe_watch_pin_t watch = { PA0, GPIO_PROBE_PULL_NONE };

    setup();
    classify_all();
    ASSERT_EQ(GPIO_PROBE_STATUS_OK, gpio_probe_service_watch(&watch, 1, 5));
    ASSERT_EQ(GPIO_PROBE_STATUS_BUSY, gpio_probe_service_arm(PA0));
    ASSERT_EQ(GPIO_PROBE_STATUS_BUSY, gpio_probe_service_pull_test(NULL, 0));
    /* Another floating pin may pulse while a watch runs (SEL against CF1). */
    ASSERT_EQ(GPIO_PROBE_STATUS_OK, gpio_probe_service_arm(PA4));
    ASSERT_EQ(GPIO_PROBE_STATUS_OK, gpio_probe_service_pulse(PA4, 1, 10));
    ASSERT_EQ(0, drives(PA0));
}

TEST(rate_ceiling_stops_and_flags_a_fast_pin) {
    gpio_probe_watch_pin_t watch[] = {
        { PA3, GPIO_PROBE_PULL_NONE },
        { PA4, GPIO_PROBE_PULL_NONE },
    };
    const gpio_probe_watch_result_t *fast;
    const gpio_probe_watch_result_t *slow;

    setup();
    stub_gpio_probe_set_train(PA3, 40, 20);    /* 25 kHz */
    stub_gpio_probe_set_train(PA4, 100, 50);   /* 10 kHz */
    ASSERT_EQ(GPIO_PROBE_STATUS_OK, gpio_probe_service_watch(watch, 2, 1));
    run_ms(20);
    fast = gpio_probe_service_watch_result(0);
    slow = gpio_probe_service_watch_result(1);
    ASSERT_EQ(GPIO_PROBE_WATCH_OVER_RATE, fast->flags);
    ASSERT_EQ(GPIO_PROBE_WATCH_RUNNING, slow->flags);
    run_ms(100);
    ASSERT_TRUE(fast->edges <= 20 * 25 + 1);
    ASSERT_TRUE(released(PA3));
    run_ms(1000);
    ASSERT_EQ(0, slow->flags);
    ASSERT_EQ(10000, slow->edges);
}

TEST(burst_limit_stops_a_counter_when_the_main_loop_is_late) {
    gpio_probe_watch_pin_t watch = { PA3, GPIO_PROBE_PULL_NONE };

    setup();
    stub_gpio_probe_set_train(PA3, 10, 5);     /* 100 kHz */
    ASSERT_EQ(GPIO_PROBE_STATUS_OK, gpio_probe_service_watch(&watch, 1, 1));
    fake_clock_advance_ms(100);
    gpio_probe_service_poll();
    ASSERT_EQ(GPIO_PROBE_WATCH_OVER_RATE, gpio_probe_service_watch_result(0)->flags);
    ASSERT_EQ(HAL_GPIO_PROBE_BURST_LIMIT, gpio_probe_service_watch_result(0)->edges);
    ASSERT_TRUE(released(PA3));
}

TEST(init_forgets_classes_and_touches_no_pin) {
    setup();
    classify_all();
    ASSERT_EQ(GPIO_PROBE_CLASS_FLOATING, class_of(PA0));
    gpio_probe_service_init();
    gpio_probe_service_enable();
    ASSERT_EQ(GPIO_PROBE_CLASS_UNTESTED, class_of(PA0));
    ASSERT_EQ(GPIO_PROBE_STATUS_NOT_FLOATING, gpio_probe_service_arm(PA0));
}

int main(void) {
    RUN_TEST(boot_touches_no_pin);
    RUN_TEST(platform_and_claimed_pins_are_not_probeable);
    RUN_TEST(disabled_probe_refuses_every_operation);
    RUN_TEST(snapshot_reads_levels_without_changing_modes);
    RUN_TEST(pull_test_classifies_floating_driven_and_active_pins);
    RUN_TEST(pull_test_of_selected_pins_leaves_others_untested);
    RUN_TEST(pulse_is_refused_on_untested_driven_high_and_active_pins);
    RUN_TEST(driven_low_pin_takes_only_a_short_high_pulse);
    RUN_TEST(driven_low_pin_must_read_driven_low_right_before_drive);
    RUN_TEST(each_pulse_needs_its_own_arm);
    RUN_TEST(arm_expires);
    RUN_TEST(pin_must_follow_both_pulls_right_before_drive);
    RUN_TEST(pulse_drives_requested_level_then_timer_cuts_off);
    RUN_TEST(main_loop_cuts_off_a_pulse_whose_timer_is_late);
    RUN_TEST(pulse_is_refused_when_cutoff_timer_cannot_be_armed);
    RUN_TEST(only_one_pin_pulses_at_a_time);
    RUN_TEST(stop_ends_every_operation_and_restores_pins);
    RUN_TEST(watch_counts_a_pulse_train_and_ends_after_its_duration);
    RUN_TEST(watch_rejects_bad_requests);
    RUN_TEST(pins_under_watch_cannot_be_pulsed_or_pull_tested);
    RUN_TEST(rate_ceiling_stops_and_flags_a_fast_pin);
    RUN_TEST(burst_limit_stops_a_counter_when_the_main_loop_is_late);
    RUN_TEST(init_forgets_classes_and_touches_no_pin);
    return test_runner_failures == 0 ? 0 : 1;
}
