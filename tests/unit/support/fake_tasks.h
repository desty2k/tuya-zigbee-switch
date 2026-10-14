#ifndef _TEST_FAKE_TASKS_H_
#define _TEST_FAKE_TASKS_H_

#include <stdint.h>

void fake_tasks_reset(void);
void fake_tasks_poll(void);
uint8_t fake_tasks_pending(void);
/** Make the next count hal_tasks_schedule() calls fail like a full pool. */
void fake_tasks_reject_next(uint8_t count);

#endif
