#include "base_components/timer_service.h"
#include "hal/tick_clock.h"
#include "support/fake_clock.h"
#include "support/fake_tasks.h"
#include "support/runner.h"
#include <stdint.h>

static uint32_t callback_count;

static void count_callback(void *arg) {
    uint32_t *count = (uint32_t *)arg;

    (*count)++;
}

static void reset_fakes(void) {
    callback_count = 0;
    fake_clock_reset();
    fake_tasks_reset();
}

TEST(start_keeps_active_deadline) {
    app_timer_t timer;

    reset_fakes();
    timer_init(&timer, count_callback, &callback_count);
    app_timer_start(&timer, 100);
    fake_clock_advance_ms(25);
    app_timer_start(&timer, 200);

    ASSERT_EQ(75, timer_remaining_ms(&timer));
    fake_clock_advance_ms(74);
    fake_tasks_poll();
    ASSERT_EQ(0, callback_count);
    fake_clock_advance_ms(1);
    fake_tasks_poll();
    ASSERT_EQ(1, callback_count);
}

TEST(restart_replaces_deadline) {
    app_timer_t timer;

    reset_fakes();
    timer_init(&timer, count_callback, &callback_count);
    app_timer_start(&timer, 100);
    fake_clock_advance_ms(25);
    timer_restart(&timer, 200);

    ASSERT_EQ(200, timer_remaining_ms(&timer));
    fake_clock_advance_ms(199);
    fake_tasks_poll();
    ASSERT_EQ(0, callback_count);
    fake_clock_advance_ms(1);
    fake_tasks_poll();
    ASSERT_EQ(1, callback_count);
}

TEST(cancel_prevents_callback) {
    app_timer_t timer;

    reset_fakes();
    timer_init(&timer, count_callback, &callback_count);
    app_timer_start(&timer, 10);
    timer_cancel(&timer);
    timer_cancel(&timer);
    fake_clock_advance_ms(10);
    fake_tasks_poll();

    ASSERT_EQ(0, callback_count);
    ASSERT_TRUE(!timer_is_active(&timer));
    ASSERT_EQ(0, timer_remaining_ms(&timer));
}

TEST(remaining_decreases_and_wraps_safely) {
    app_timer_t timer;

    reset_fakes();
    fake_clock_set(UINT32_MAX - 4);
    timer_init(&timer, count_callback, &callback_count);
    app_timer_start(&timer, 10);
    ASSERT_EQ(10, timer_remaining_ms(&timer));
    fake_clock_advance_ms(6);
    ASSERT_EQ(4, timer_remaining_ms(&timer));
    fake_clock_advance_ms(4);
    ASSERT_EQ(0, timer_remaining_ms(&timer));
}

static void restart_callback(void *arg) {
    app_timer_t *timer = (app_timer_t *)arg;

    callback_count++;
    if (callback_count == 1) {
        app_timer_start(timer, 20);
    }
}

TEST(callback_may_restart_own_timer) {
    app_timer_t timer;

    reset_fakes();
    timer_init(&timer, restart_callback, &timer);
    app_timer_start(&timer, 10);
    fake_clock_advance_ms(10);
    fake_tasks_poll();

    ASSERT_EQ(1, callback_count);
    ASSERT_TRUE(timer_is_active(&timer));
    ASSERT_EQ(20, timer_remaining_ms(&timer));
    fake_clock_advance_ms(20);
    fake_tasks_poll();
    ASSERT_EQ(2, callback_count);
    ASSERT_TRUE(!timer_is_active(&timer));
}

TEST(expiry_runs_at_exact_deadline) {
    app_timer_t timer;

    reset_fakes();
    timer_init(&timer, count_callback, &callback_count);
    app_timer_start(&timer, 50);
    fake_clock_advance_ms(49);
    fake_tasks_poll();
    ASSERT_EQ(0, callback_count);
    fake_clock_advance_ms(1);
    fake_tasks_poll();
    ASSERT_EQ(1, callback_count);
    ASSERT_TRUE(!timer_is_active(&timer));
}

TEST(tick_clock_is_exact_and_continuous_across_tick_wraps) {
    hal_tick_clock_t clock = { 0, 0 };
    uint64_t         ticks = 0;

    /* 16 MHz tick sampled every 59999.9375 ms for over 40 tick wraps. */
    for (uint32_t i = 1; i <= 200; i++) {
        uint32_t ms;

        ticks += 959999000u;
        ms     = hal_tick_clock_update(&clock, (uint32_t)ticks, 16000);
        ASSERT_EQ((uint32_t)(ticks / 16000), ms);
    }
}

TEST(tick_clock_milliseconds_wrap_at_2_pow_32) {
    hal_tick_clock_t clock = { 0, UINT32_MAX - 2 };

    ASSERT_EQ(UINT32_MAX - 2, hal_tick_clock_update(&clock, 15999, 16000));
    ASSERT_EQ(UINT32_MAX, hal_tick_clock_update(&clock, 3 * 16000 - 1, 16000));
    ASSERT_EQ(2, hal_tick_clock_update(&clock, 5 * 16000, 16000));
    ASSERT_EQ(6, hal_tick_clock_update(&clock, 9 * 16000, 16000));
}

TEST(failed_schedule_leaves_timer_inactive_and_counts) {
    app_timer_t timer;
    uint32_t    failures_before = timer_schedule_failures();

    reset_fakes();
    timer_init(&timer, count_callback, &callback_count);
    fake_tasks_reject_next(1);
    app_timer_start(&timer, 100);
    ASSERT_TRUE(!timer_is_active(&timer));
    ASSERT_EQ(0, timer_remaining_ms(&timer));
    ASSERT_EQ(failures_before + 1, timer_schedule_failures());

    app_timer_start(&timer, 100);
    ASSERT_TRUE(timer_is_active(&timer));
    fake_clock_advance_ms(100);
    fake_tasks_poll();
    ASSERT_EQ(1, callback_count);
}

int main(void) {
    RUN_TEST(failed_schedule_leaves_timer_inactive_and_counts);
    RUN_TEST(tick_clock_is_exact_and_continuous_across_tick_wraps);
    RUN_TEST(tick_clock_milliseconds_wrap_at_2_pow_32);
    RUN_TEST(start_keeps_active_deadline);
    RUN_TEST(restart_replaces_deadline);
    RUN_TEST(cancel_prevents_callback);
    RUN_TEST(remaining_decreases_and_wraps_safely);
    RUN_TEST(callback_may_restart_own_timer);
    RUN_TEST(expiry_runs_at_exact_deadline);

    return test_runner_failures == 0 ? 0 : 1;
}
