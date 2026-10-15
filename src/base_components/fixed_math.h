#ifndef BASE_COMPONENTS_FIXED_MATH_H_
#define BASE_COMPONENTS_FIXED_MATH_H_

#include <stdint.h>

/*
 * 64-bit products and quotients from 32-bit operations only. The Telink TC32
 * image links without libgcc, so a 64-bit `*`, `/` or `%` leaves __muldi3 or
 * __udivdi3 unresolved; 64-bit add, subtract, compare and constant shifts are
 * emitted inline. Every caller runs at most a few times per meter sample.
 */

static inline uint64_t fixed_mul_u32(uint32_t a, uint32_t b) {
    uint32_t a_lo = a & 0xFFFFu;
    uint32_t a_hi = a >> 16;
    uint32_t b_lo = b & 0xFFFFu;
    uint32_t b_hi = b >> 16;
    uint64_t mid  = (uint64_t)(a_hi * b_lo) + (uint64_t)(a_lo * b_hi);

    return (uint64_t)(a_lo * b_lo) + (mid << 16) + ((uint64_t)(a_hi * b_hi) << 32);
}

/* Restoring division; `divisor` must not be 0. `remainder` may be NULL. */
static inline uint64_t fixed_div_u64(uint64_t dividend, uint32_t divisor,
                                     uint32_t *remainder) {
    uint64_t quotient = 0;
    uint64_t partial  = 0;

    for (uint8_t bit = 0; bit < 64; bit++) {
        partial    = (partial << 1) | (dividend >> 63);
        dividend <<= 1;
        quotient <<= 1;
        if (partial >= divisor) {
            partial  -= divisor;
            quotient |= 1u;
        }
    }
    if (remainder != 0) {
        *remainder = (uint32_t)partial;
    }
    return quotient;
}

/* a * b / divisor, saturated to UINT32_MAX; `divisor` must not be 0. */
static inline uint32_t fixed_muldiv_u32(uint32_t a, uint32_t b, uint32_t divisor) {
    uint64_t quotient = fixed_div_u64(fixed_mul_u32(a, b), divisor, 0);

    return quotient > UINT32_MAX ? UINT32_MAX : (uint32_t)quotient;
}

#endif /* BASE_COMPONENTS_FIXED_MATH_H_ */
