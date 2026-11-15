#include "mm/wide.h"
MMI32 mm_signed_word(MMU32 n)
{
    return n <= 0x7fffffffUL ? (MMI32)n : -1 - (MMI32)(0xffffffffUL - n);
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
