#include "hal/gpio_counter.h"
#include "stub/hal/gpio_counter.h"
#include "support/fake_clock.h"
#include "support/fake_gpio.h"
#include "support/runner.h"

TEST(snapshots_are_cumulative_and_non_destructive) {
    hal_gpio_counter_snapshot_t snapshot;
    hal_gpio_counter_id_t id;

    fake_clock_reset();
    fake_gpio_reset();
    id = hal_gpio_counter_open(12, HAL_GPIO_COUNTER_EDGE_RISING, HAL_GPIO_PULL_UP);
    ASSERT_EQ(0, id);
    stub_gpio_counter_capture_edge(12, 0, 1, 10);
    stub_gpio_counter_capture_edge(12, 1, 0, 11);
    ASSERT_TRUE(hal_gpio_counter_snapshot(id, &snapshot));
    ASSERT_EQ(1, snapshot.total);
    ASSERT_EQ(10, snapshot.timestamp_ms);
    ASSERT_TRUE(snapshot.running);
    stub_gpio_counter_capture_edge(12, 0, 1, 20);
    ASSERT_TRUE(hal_gpio_counter_snapshot(id, &snapshot));
    ASSERT_EQ(2, snapshot.total);
    ASSERT_EQ(20, snapshot.timestamp_ms);
    hal_gpio_counter_close(id);
}

TEST(edges_are_isolated_and_resources_are_bounded) {
    hal_gpio_counter_snapshot_t snapshot;
    hal_gpio_counter_id_t rising;
    hal_gpio_counter_id_t falling;

    fake_clock_reset();
    fake_gpio_reset();
    rising = hal_gpio_counter_open(12, HAL_GPIO_COUNTER_EDGE_RISING, HAL_GPIO_PULL_NONE);
    falling = hal_gpio_counter_open(13, HAL_GPIO_COUNTER_EDGE_FALLING, HAL_GPIO_PULL_NONE);
    ASSERT_EQ(0, rising);
    ASSERT_EQ(1, falling);
    ASSERT_EQ(HAL_GPIO_COUNTER_INVALID_ID,
              hal_gpio_counter_open(14, HAL_GPIO_COUNTER_EDGE_RISING, HAL_GPIO_PULL_NONE));
    stub_gpio_counter_capture_edge(12, 1, 0, 10);
    stub_gpio_counter_capture_edge(13, 0, 1, 11);
    stub_gpio_counter_capture_edge(13, 1, 0, 12);
    ASSERT_TRUE(hal_gpio_counter_snapshot(rising, &snapshot));
    ASSERT_EQ(0, snapshot.total);
    ASSERT_TRUE(hal_gpio_counter_snapshot(falling, &snapshot));
    ASSERT_EQ(1, snapshot.total);
    ASSERT_TRUE(!hal_gpio_counter_snapshot(HAL_GPIO_COUNTER_INVALID_ID, &snapshot));
    ASSERT_TRUE(!hal_gpio_counter_can_route(2, 12));
    hal_gpio_counter_close(rising);
    ASSERT_EQ(0, hal_gpio_counter_open(14, HAL_GPIO_COUNTER_EDGE_RISING,
                                       HAL_GPIO_PULL_NONE));
}

int main(void) {
    RUN_TEST(snapshots_are_cumulative_and_non_destructive);
    RUN_TEST(edges_are_isolated_and_resources_are_bounded);
    return test_runner_failures == 0 ? 0 : 1;
}
