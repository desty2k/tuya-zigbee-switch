#include "hal/timer.h"
#include "stub/hal/stub.h"
#include "stub/machine_io.h"
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/time.h>

/*
 * Virtual time is a 64-bit millisecond count that never wraps. hal_millis()
 * derives the firmware-visible 32-bit value from it according to the selected
 * platform model:
 *  - native: the low 32 bits, i.e. a true 2^32 ms wrap (Silabs);
 *  - telink: the TLSR8258 32-bit system tick at 16 MHz, converted to
 *    milliseconds the way src/telink/hal/timer.c does.
 */
#define STUB_TELINK_TICKS_PER_MS    16000u

static uint64_t          start_ms;
static int               initialized   = 0;
static int               time_frozen   = 0;
static uint64_t          frozen_millis = 0;
static stub_time_model_t time_model    = STUB_TIME_MODEL_NATIVE;

void stub_millis_init() {
    struct timeval current_time;

    if (gettimeofday(&current_time, NULL) != 0) {
        io_log("TIMER", "Error: Failed to get initial time");
        exit(1);
    }
    start_ms    = (uint64_t)current_time.tv_sec * 1000 + current_time.tv_usec / 1000;
    initialized = 1;
    io_log("TIMER", "Timer initialized with start time %ld.%06ld",
           current_time.tv_sec, current_time.tv_usec);
}

uint64_t stub_millis_virtual(void) {
    struct timeval current_time;

    if (time_frozen) {
        return frozen_millis;
    }
    if (!initialized) {
        stub_millis_init();
    }
    if (gettimeofday(&current_time, NULL) != 0) {
        io_log("TIMER", "Error: Failed to get current time");
        exit(1);
    }
    return (uint64_t)current_time.tv_sec * 1000 + current_time.tv_usec / 1000 -
           start_ms;
}

void stub_millis_freeze() {
    frozen_millis = initialized || time_frozen ? stub_millis_virtual() : 0;
    time_frozen   = 1;
    io_log("TIMER", "Time frozen at %llu ms", (unsigned long long)frozen_millis);
}

void stub_millis_unfreeze() {
    time_frozen = 0;
    io_log("TIMER", "Time unfrozen");
}

void stub_millis_step(uint64_t step) {
    frozen_millis += step;
}

int stub_millis_set(uint64_t virtual_ms) {
    if (!time_frozen || virtual_ms < frozen_millis) {
        return -1;
    }
    stub_millis_step(virtual_ms - frozen_millis);
    return 0;
}

void stub_millis_set_model(stub_time_model_t model) {
    time_model = model;
}

stub_time_model_t stub_millis_model(void) {
    return time_model;
}

uint32_t hal_millis() {
    uint64_t virtual_ms = stub_millis_virtual();

    if (time_model == STUB_TIME_MODEL_TELINK) {
        uint32_t tick = (uint32_t)(virtual_ms * STUB_TELINK_TICKS_PER_MS);

        return tick / STUB_TELINK_TICKS_PER_MS;
    }
    return (uint32_t)virtual_ms;
}
