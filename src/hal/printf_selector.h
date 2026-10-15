#ifndef HAL_PRINTF_H
#define HAL_PRINTF_H

#ifdef HAL_TELINK

/*
 * The SDK's tl_printf only knows %c, %d, %s, %o and %x with an optional
 * width: no %u and no length modifiers. Shared code passes 32-bit values as
 * (int) with %d, which is also exact for newlib on Silabs.
 */
extern int tl_printf(const char *format, ...);

#define printf    tl_printf

#elif defined(HAL_SILABS)

#include "stdio.h"

#else
#include <stdio.h>
#endif

#endif
