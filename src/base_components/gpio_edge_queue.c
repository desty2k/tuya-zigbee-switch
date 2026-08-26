#include "gpio_edge_queue.h"

#define GPIO_EDGE_QUEUE_MASK    (GPIO_EDGE_QUEUE_SIZE - 1)

void gpio_edge_queue_init(gpio_edge_queue_t *queue) {
    queue->head    = 0;
    queue->tail    = 0;
    queue->dropped = 0;
}

bool gpio_edge_queue_push(gpio_edge_queue_t *queue,
                          const hal_gpio_edge_t *edge) {
    uint8_t head = queue->head;

    if ((uint8_t)(head - queue->tail) >= GPIO_EDGE_QUEUE_SIZE) {
        queue->dropped++;
        return false;
    }

    queue->items[head & GPIO_EDGE_QUEUE_MASK] = *edge;
    queue->head = (uint8_t)(head + 1);
    return true;
}

bool gpio_edge_queue_pop(gpio_edge_queue_t *queue, hal_gpio_edge_t *out) {
    uint8_t tail = queue->tail;

    if (tail == queue->head) {
        return false;
    }

    *out        = queue->items[tail & GPIO_EDGE_QUEUE_MASK];
    queue->tail = (uint8_t)(tail + 1);
    return true;
}

uint8_t gpio_edge_queue_used(const gpio_edge_queue_t *queue) {
    return (uint8_t)(queue->head - queue->tail);
}
