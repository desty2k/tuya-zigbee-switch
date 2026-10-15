#ifndef HAL_PRINTF_H
#define HAL_PRINTF_H

#ifdef HAL_TELINK

/*
 * The SDK's tl_printf only knows %c, %d, %s, %o and %x with an optional
 * width: no %u and no length modifiers. Shared code passes 32-bit values as
 * (int) with %d, which is also exact for newlib on Silabs.
 */
extern int tl_printf(const char *format, ...);

/*
 * tl_printf writes through drv_putchar, which only outputs with
 * UART_PRINTF_MODE (DEBUG=1 in src/telink/Makefile). Without it the calls
 * compile away so the formatter and the format strings stay out of the image;
 * the arguments are type-checked but never evaluated, so they must be free of
 * side effects.
 */
#if UART_PRINTF_MODE
#define printf    tl_printf
#else
#define printf(...)    do { if (0) { tl_printf(__VA_ARGS__); } } while (0)
#endif

#elif defined(HAL_SILABS)

#include "stdio.h"

#else
#include <stdio.h>
#endif

#endif
