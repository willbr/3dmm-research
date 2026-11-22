#ifndef MM_RUNTIME_MATH_H
#define MM_RUNTIME_MATH_H
#include "types.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct MMRuntimeCounter { MMU32 low, high; } MMRuntimeCounter;
MMRuntimeCounter mm_runtime_counter_subtract(MMRuntimeCounter, MMRuntimeCounter);
MMRuntimeCounter mm_runtime_counter_multiply(MMU32, MMU32);
/* Unsigned floor(counter * scale / frequency), with a 96-bit intermediate.
 * Zero frequency or a quotient exceeding 64 bits fails without publication.
 * No C99 integers, floating point or rounding through nanoseconds. */
int mm_runtime_counter_scale(MMRuntimeCounter counter, MMU32 scale,
    MMRuntimeCounter frequency, MMRuntimeCounter *result);
#ifdef __cplusplus
}
#endif
#endif
