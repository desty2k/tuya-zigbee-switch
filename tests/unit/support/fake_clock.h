#ifndef _TEST_FAKE_CLOCK_H_
#define _TEST_FAKE_CLOCK_H_

#include <stdint.h>

void fake_clock_reset(void);
void fake_clock_set(uint32_t now_ms);
void fake_clock_advance_ms(uint32_t amount_ms);

#endif
