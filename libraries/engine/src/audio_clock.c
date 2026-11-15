#include "mm/audio_clock.h"
MMAudioCounter mm_audio_counter(MMU32 low, MMU32 high)
{
    MMAudioCounter r;
    r.low = low;
    r.high = high;
    return r;
}
MMAudioCounter mm_audio_count_size(size_t n)
{
    return mm_audio_counter((MMU32)n, (MMU32)((n / 65536U) / 65536U));
}
size_t mm_audio_count_to_size(MMAudioCounter n)
{
    return (size_t)n.high * 65536U * 65536U + n.low;
}
MMAudioCounter mm_audio_count_add(MMAudioCounter a, MMAudioCounter b)
{
    MMAudioCounter r;
    r.low = a.low + b.low;
    r.high = a.high + b.high + (r.low < a.low);
    return r;
}
MMAudioCounter mm_audio_count_sub(MMAudioCounter a, MMAudioCounter b)
{
    return mm_audio_counter(a.low - b.low, a.high - b.high - (a.low < b.low));
}
int mm_audio_count_compare(MMAudioCounter a, MMAudioCounter b)
{
    if (a.high != b.high)
        return a.high < b.high ? -1 : 1;
    return a.low == b.low ? 0 : a.low < b.low ? -1 : 1;
}
MMAudioCounter mm_audio_count_multiply(MMU32 a, MMU32 b)
{
    MMU32 lo = (a & 65535U) * (b & 65535U), mid;
    MMU32 hi = (a >> 16) * (b >> 16);
    mid = (a >> 16) * (b & 65535U) + (lo >> 16);
    hi += mid >> 16;
    mid = (b >> 16) * (a & 65535U) + (mid & 65535U);
    return mm_audio_counter((mid << 16) | (lo & 65535U), hi + (mid >> 16));
}
MMAudioCounter mm_audio_count_divide(MMAudioCounter a, MMAudioCounter b, MMAudioCounter *rest)
{
    MMAudioCounter q = {0, 0}, r = {0, 0};
    int i, carry;
    MMU32 bit;
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
            if (carry || mm_audio_count_compare(r, b) >= 0)
            {
                r = mm_audio_count_sub(r, b);
                q.low |= 1U;
            }
        }
    }
    if (rest)
        *rest = r;
    return q;
}
