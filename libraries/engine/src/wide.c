#include "three_mm/wide.h"
ThreeMMI32 three_mm_signed_word(ThreeMMU32 n)
{
    return n <= 0x7fffffffUL ? (ThreeMMI32)n : -1 - (ThreeMMI32)(0xffffffffUL - n);
}
ThreeMMWide three_mm_wide_i32(ThreeMMI32 n)
{
    ThreeMMWide w;
    w.low = (ThreeMMU32)n;
    w.high = n < 0 ? -1 : 0;
    return w;
}
ThreeMMWide three_mm_wide_add(ThreeMMWide a, ThreeMMWide b)
{
    ThreeMMWide w;
    w.low = a.low + b.low;
    w.high = three_mm_signed_word((ThreeMMU32)a.high + (ThreeMMU32)b.high + (w.low < a.low));
    return w;
}
ThreeMMWide three_mm_wide_subtract(ThreeMMWide a, ThreeMMWide b)
{
    ThreeMMWide n;
    n.low = 0 - b.low;
    n.high = three_mm_signed_word(~(ThreeMMU32)b.high + (n.low == 0));
    return three_mm_wide_add(a, n);
}
int three_mm_wide_compare(ThreeMMWide a, ThreeMMWide b)
{
    if (a.high != b.high)
        return a.high < b.high ? -1 : 1;
    return a.low == b.low ? 0 : a.low < b.low ? -1 : 1;
}
