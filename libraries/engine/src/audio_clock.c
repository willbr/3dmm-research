#include "three_mm/audio_clock.h"
ThreeMMAudioCounter three_mm_audio_counter(ThreeMMU32 low, ThreeMMU32 high)
{
    ThreeMMAudioCounter r;
    r.low = low;
    r.high = high;
    return r;
}
ThreeMMAudioCounter three_mm_audio_count_size(size_t n)
{
    return three_mm_audio_counter((ThreeMMU32)n, (ThreeMMU32)((n / 65536U) / 65536U));
}
size_t three_mm_audio_count_to_size(ThreeMMAudioCounter n)
{
    return (size_t)n.high * 65536U * 65536U + n.low;
}
ThreeMMAudioCounter three_mm_audio_count_add(ThreeMMAudioCounter a, ThreeMMAudioCounter b)
{
    ThreeMMAudioCounter r;
    r.low = a.low + b.low;
    r.high = a.high + b.high + (r.low < a.low);
    return r;
}
ThreeMMAudioCounter three_mm_audio_count_sub(ThreeMMAudioCounter a, ThreeMMAudioCounter b)
{
    return three_mm_audio_counter(a.low - b.low, a.high - b.high - (a.low < b.low));
}
int three_mm_audio_count_compare(ThreeMMAudioCounter a, ThreeMMAudioCounter b)
{
    if (a.high != b.high)
        return a.high < b.high ? -1 : 1;
    return a.low == b.low ? 0 : a.low < b.low ? -1 : 1;
}
ThreeMMAudioCounter three_mm_audio_count_multiply(ThreeMMU32 a, ThreeMMU32 b)
{
    ThreeMMU32 lo = (a & 65535U) * (b & 65535U), mid;
    ThreeMMU32 hi = (a >> 16) * (b >> 16);
    mid = (a >> 16) * (b & 65535U) + (lo >> 16);
    hi += mid >> 16;
    mid = (b >> 16) * (a & 65535U) + (mid & 65535U);
    return three_mm_audio_counter((mid << 16) | (lo & 65535U), hi + (mid >> 16));
}
ThreeMMAudioCounter three_mm_audio_count_divide(ThreeMMAudioCounter a, ThreeMMAudioCounter b, ThreeMMAudioCounter *rest)
{
    ThreeMMAudioCounter q = {0, 0}, r = {0, 0};
    int i, carry;
    ThreeMMU32 bit;
    if (!a.high && !b.high && b.low)
    {
        q.low = a.low / b.low;
        r.low = a.low % b.low;
    }
    else if (b.low || b.high)
    {
        for (i = 63; i >= 0; --i)
        {
            bit = i >= 32 ? (a.high >> (i - 32)) & 1U : (a.low >> i) & 1U;
            carry = (r.high & 0x80000000UL) != 0;
            r.high = (r.high << 1) | (r.low >> 31);
            r.low = (r.low << 1) | bit;
            q.high = (q.high << 1) | (q.low >> 31);
            q.low <<= 1;
            if (carry || three_mm_audio_count_compare(r, b) >= 0)
            {
                r = three_mm_audio_count_sub(r, b);
                q.low |= 1U;
            }
        }
    }
    if (rest)
        *rest = r;
    return q;
}
