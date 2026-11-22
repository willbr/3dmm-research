#ifndef MM_ENGINE_WIDE_H
#define MM_ENGINE_WIDE_H
#include "types.h"
#ifdef __cplusplus
extern "C"
{
#endif
    /* Signed two-word arithmetic without C99 integer types. Arithmetic wraps at
     * 64 bits; callers bound mathematical values when overflow is not intended. */
    typedef struct MMWide
    {
        MMU32 low;
        MMI32 high;
    } MMWide;
    MMI32 mm_signed_word(MMU32);
    enum MMWordDivision
    {
        MM_WORD_DIVIDED,
        MM_WORD_DIVIDE_ZERO,
        MM_WORD_DIVIDE_OVERFLOW
    };
    /* Quotient truncates toward zero. Failure leaves the result untouched. */
    int mm_word_divide(MMI32 left, MMI32 right, int remainder, MMI32 *result);
    MMWide mm_wide_i32(MMI32);
    MMWide mm_wide_add(MMWide, MMWide);
    MMWide mm_wide_subtract(MMWide, MMWide);
    int mm_wide_compare(MMWide, MMWide);
#ifdef __cplusplus
}
#endif
#endif
