#include "base_components/timer_service.h"
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

int main(void) {
    RUN_TEST(start_keeps_active_deadline);
    RUN_TEST(restart_replaces_deadline);
    RUN_TEST(cancel_prevents_callback);
    RUN_TEST(remaining_decreases_and_wraps_safely);
    RUN_TEST(callback_may_restart_own_timer);
    RUN_TEST(expiry_runs_at_exact_deadline);

    return test_runner_failures == 0 ? 0 : 1;
}
