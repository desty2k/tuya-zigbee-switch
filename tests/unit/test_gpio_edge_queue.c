#include "base_components/gpio_edge_queue.h"
#include "support/runner.h"
#include <stdbool.h>
#include <stdint.h>

static hal_gpio_edge_t test_edge(uint32_t value) {
    hal_gpio_edge_t edge = {
        .pin          = (hal_gpio_pin_t)(value + 1),
        .level        = (uint8_t)(value & 1),
        .timestamp_ms = value * 10,
        .seq          = value,
    };

    return edge;
}

static void assert_edge(uint32_t expected, const hal_gpio_edge_t *actual) {
    ASSERT_EQ(expected + 1, actual->pin);
    ASSERT_EQ(expected & 1, actual->level);
    ASSERT_EQ(expected * 10, actual->timestamp_ms);
    ASSERT_EQ(expected, actual->seq);
}

TEST(push_pop_fifo_order) {
    gpio_edge_queue_t queue;
    hal_gpio_edge_t   out;

    gpio_edge_queue_init(&queue);
    for (uint32_t i = 0; i < 6; i++) {
        hal_gpio_edge_t edge = test_edge(i);
        ASSERT_TRUE(gpio_edge_queue_push(&queue, &edge));
    }
    for (uint32_t i = 0; i < 6; i++) {
        ASSERT_TRUE(gpio_edge_queue_pop(&queue, &out));
        assert_edge(i, &out);
    }
    ASSERT_TRUE(!gpio_edge_queue_pop(&queue, &out));
}

TEST(all_entries_are_usable) {
    gpio_edge_queue_t queue;

    gpio_edge_queue_init(&queue);
    for (uint32_t i = 0; i < GPIO_EDGE_QUEUE_SIZE; i++) {
        hal_gpio_edge_t edge = test_edge(i);
        ASSERT_TRUE(gpio_edge_queue_push(&queue, &edge));
    }
    ASSERT_EQ(GPIO_EDGE_QUEUE_SIZE, gpio_edge_queue_used(&queue));

    hal_gpio_edge_t overflow = test_edge(GPIO_EDGE_QUEUE_SIZE);
    ASSERT_TRUE(!gpio_edge_queue_push(&queue, &overflow));
    ASSERT_EQ(1, queue.dropped);
}

TEST(overflow_preserves_oldest_entries) {
    gpio_edge_queue_t queue;
    hal_gpio_edge_t   out;

    gpio_edge_queue_init(&queue);
    for (uint32_t i = 0; i < GPIO_EDGE_QUEUE_SIZE; i++) {
        hal_gpio_edge_t edge = test_edge(i);
        ASSERT_TRUE(gpio_edge_queue_push(&queue, &edge));
    }
    hal_gpio_edge_t overflow = test_edge(99);
    ASSERT_TRUE(!gpio_edge_queue_push(&queue, &overflow));

    for (uint32_t i = 0; i < GPIO_EDGE_QUEUE_SIZE; i++) {
        ASSERT_TRUE(gpio_edge_queue_pop(&queue, &out));
        assert_edge(i, &out);
    }
    ASSERT_TRUE(!gpio_edge_queue_pop(&queue, &out));
}

TEST(wraparound_preserves_order) {
    gpio_edge_queue_t queue;
    hal_gpio_edge_t   out;

    gpio_edge_queue_init(&queue);
    for (uint32_t i = 0; i < GPIO_EDGE_QUEUE_SIZE * 4; i++) {
        hal_gpio_edge_t edge = test_edge(i);
        ASSERT_TRUE(gpio_edge_queue_push(&queue, &edge));
        ASSERT_TRUE(gpio_edge_queue_pop(&queue, &out));
        assert_edge(i, &out);
    }
    ASSERT_EQ(0, gpio_edge_queue_used(&queue));
    ASSERT_EQ(0, queue.dropped);
}

TEST(interleaved_push_pop_has_complete_ordered_entries) {
    gpio_edge_queue_t queue;
    hal_gpio_edge_t   out;
    uint32_t          next_out = 0;

    gpio_edge_queue_init(&queue);
    for (uint32_t i = 0; i < GPIO_EDGE_QUEUE_SIZE * 4; i++) {
        hal_gpio_edge_t edge = test_edge(i);
        ASSERT_TRUE(gpio_edge_queue_push(&queue, &edge));
        if ((i % 5) != 0) {
            ASSERT_TRUE(gpio_edge_queue_pop(&queue, &out));
            assert_edge(next_out++, &out);
        }
    }
    while (gpio_edge_queue_pop(&queue, &out)) {
        assert_edge(next_out++, &out);
    }
    ASSERT_EQ(GPIO_EDGE_QUEUE_SIZE * 4, next_out);
}

int main(void) {
    RUN_TEST(push_pop_fifo_order);
    RUN_TEST(all_entries_are_usable);
    RUN_TEST(overflow_preserves_oldest_entries);
    RUN_TEST(wraparound_preserves_order);
    RUN_TEST(interleaved_push_pop_has_complete_ordered_entries);

    return test_runner_failures == 0 ? 0 : 1;
}
