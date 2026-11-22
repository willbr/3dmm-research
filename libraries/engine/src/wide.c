#include "mm/wide.h"
MMI32 mm_signed_word(MMU32 n)
{
    return n <= 0x7fffffffUL ? (MMI32)n : -1 - (MMI32)(0xffffffffUL - n);
}
int mm_word_divide(MMI32 left, MMI32 right, int remainder, MMI32 *result)
{
    MMU32 a, b, value;
    int negative;
    if (!right)
        return MM_WORD_DIVIDE_ZERO;
    if (left == (-2147483647 - 1) && right == -1)
        return MM_WORD_DIVIDE_OVERFLOW;
    a = left < 0 ? 0U - (MMU32)left : (MMU32)left;
    b = right < 0 ? 0U - (MMU32)right : (MMU32)right;
    value = remainder ? a % b : a / b;
    negative = remainder ? left < 0 : (left < 0) != (right < 0);
    *result = mm_signed_word(negative ? 0U - value : value);
    return MM_WORD_DIVIDED;
}
MMWide mm_wide_i32(MMI32 n)
{
    MMWide w;
    w.low = (MMU32)n;
    w.high = n < 0 ? -1 : 0;
    return w;
}
MMWide mm_wide_add(MMWide a, MMWide b)
{
    MMWide w;
    w.low = a.low + b.low;
    w.high = mm_signed_word((MMU32)a.high + (MMU32)b.high + (w.low < a.low));
    return w;
}
MMWide mm_wide_subtract(MMWide a, MMWide b)
{
    MMWide n;
    n.low = 0 - b.low;
    n.high = mm_signed_word(~(MMU32)b.high + (n.low == 0));
    return mm_wide_add(a, n);
}
int mm_wide_compare(MMWide a, MMWide b)
{
    if (a.high != b.high)
        return a.high < b.high ? -1 : 1;
    return a.low == b.low ? 0 : a.low < b.low ? -1 : 1;
}
