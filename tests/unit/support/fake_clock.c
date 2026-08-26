#include "fake_clock.h"
#include "hal/timer.h"

static uint32_t fake_clock_now_ms;

void fake_clock_reset(void) {
    fake_clock_now_ms = 0;
}

void fake_clock_set(uint32_t now_ms) {
    fake_clock_now_ms = now_ms;
}

void fake_clock_advance_ms(uint32_t amount_ms) {
    fake_clock_now_ms += amount_ms;
}

uint32_t hal_millis(void) {
    return fake_clock_now_ms;
}
