#ifndef _TEST_FAKE_TASKS_H_
#define _TEST_FAKE_TASKS_H_

#include <stdint.h>

void fake_tasks_reset(void);
void fake_tasks_poll(void);
uint8_t fake_tasks_pending(void);

#endif
