#ifndef _GPIO_EDGE_QUEUE_H_
#define _GPIO_EDGE_QUEUE_H_

#include "hal/gpio.h"
#include <stdbool.h>
#include <stdint.h>

#define GPIO_EDGE_QUEUE_SIZE    32

typedef struct {
    hal_gpio_edge_t   items[GPIO_EDGE_QUEUE_SIZE];
    volatile uint8_t  head;
    volatile uint8_t  tail;
    volatile uint32_t dropped;
} gpio_edge_queue_t;

void gpio_edge_queue_init(gpio_edge_queue_t *queue);
bool gpio_edge_queue_push(gpio_edge_queue_t *queue,
                          const hal_gpio_edge_t *edge);
bool gpio_edge_queue_pop(gpio_edge_queue_t *queue, hal_gpio_edge_t *out);
uint8_t gpio_edge_queue_used(const gpio_edge_queue_t *queue);

#endif
