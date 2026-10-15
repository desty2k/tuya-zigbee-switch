#ifndef RESET_H
#define RESET_H

#include <stdint.h>

#define DEFAULT_RESET_DELAY_MS    300

/** Reboot after delay_ms, or DEFAULT_RESET_DELAY_MS when 0. */
void schedule_reboot(uint16_t delay_ms);

#endif // RESET_H
