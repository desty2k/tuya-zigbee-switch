#include "hal/system.h"
#pragma pack(push, 1)
#include "tl_common.h"
#include "zb_api.h"
#pragma pack(pop)
#include <stdbool.h>
#include <stdint.h>

void hal_system_reset(void) {
    mcu_reset();
    // mcu_reset() does not return but is not declared noreturn.
    for (;;) {
    }
}

void hal_factory_reset(void) {
    zb_factoryReset();
}
