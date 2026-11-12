#ifndef THREE_MM_ENGINE_WIDE_H
#define THREE_MM_ENGINE_WIDE_H
#include "types.h"
#ifdef __cplusplus
extern "C"
{
#endif
    /* Signed two-word arithmetic without C99 integer types. Arithmetic wraps at
     * 64 bits; callers bound mathematical values when overflow is not intended. */
    typedef struct ThreeMMWide
    {
        ThreeMMU32 low;
        ThreeMMI32 high;
    } ThreeMMWide;
    ThreeMMI32 three_mm_signed_word(ThreeMMU32);
    ThreeMMWide three_mm_wide_i32(ThreeMMI32);
    ThreeMMWide three_mm_wide_add(ThreeMMWide, ThreeMMWide);
    ThreeMMWide three_mm_wide_subtract(ThreeMMWide, ThreeMMWide);
    int three_mm_wide_compare(ThreeMMWide, ThreeMMWide);
#ifdef __cplusplus
}
#endif
#endif
