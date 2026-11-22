#include "mm/runtime_math.h"
MMRuntimeCounter mm_runtime_counter_subtract(MMRuntimeCounter a, MMRuntimeCounter b)
{
    MMRuntimeCounter result;
    result.low = a.low - b.low;
    result.high = a.high - b.high - (a.low < b.low);
    return result;
}
MMRuntimeCounter mm_runtime_counter_multiply(MMU32 a, MMU32 b)
{
    MMRuntimeCounter result;
    MMU32 first, second, third, before;
    first = (a & 65535) * (b & 65535);
    second = (a >> 16) * (b & 65535);
    third = (a & 65535) * (b >> 16);
    result.low = first + (second << 16);
    result.high = (a >> 16) * (b >> 16) + (second >> 16) + (result.low < first);
    before = result.low; result.low += third << 16;
    result.high += (third >> 16) + (result.low < before);
    return result;
}
int mm_runtime_counter_scale(MMRuntimeCounter counter, MMU32 scale,
    MMRuntimeCounter frequency, MMRuntimeCounter *result)
{
    MMRuntimeCounter lower, upper, remainder, quotient;
    MMU32 numerator[3], before, carry;
    unsigned bit, input;
    int overflow;
    if (!frequency.low && !frequency.high) return 0;
    if (!scale) { quotient.low = quotient.high = 0; *result = quotient; return 1; }
    /* QPC's common 10/24 MHz profiles reduce to division by 10/24 thousand.
     * Four base-65536 divisions preserve exactness without a wide divide. */
    if (!frequency.high && frequency.low % scale == 0 && frequency.low / scale <= 65535) {
        MMU32 divisor, part, rest;
        unsigned i;
        divisor = frequency.low / scale; rest = 0;
        quotient.low = quotient.high = 0;
        for (i = 0; i < 4; ++i) {
            part = i < 2 ? counter.high : counter.low;
            part = (part >> ((i & 1) ? 0 : 16)) & 65535;
            part |= rest << 16;
            if (i < 2) quotient.high = (quotient.high << 16) | (part / divisor);
            else quotient.low = (quotient.low << 16) | (part / divisor);
            rest = part % divisor;
        }
        *result = quotient; return 1;
    }
    lower = mm_runtime_counter_multiply(counter.low, scale);
    upper = mm_runtime_counter_multiply(counter.high, scale);
    numerator[0] = lower.low;
    numerator[1] = lower.high + upper.low;
    numerator[2] = upper.high + (numerator[1] < lower.high);
    remainder.low = remainder.high = quotient.low = quotient.high = 0;
    overflow = 0;
    for (bit = 96; bit > 0; --bit) {
        input = (unsigned)((numerator[(bit - 1) / 32] >> ((bit - 1) % 32)) & 1);
        carry = remainder.high >> 31;
        remainder.high = (remainder.high << 1) | (remainder.low >> 31);
        remainder.low = (remainder.low << 1) | input;
        if (carry || remainder.high > frequency.high ||
            (remainder.high == frequency.high && remainder.low >= frequency.low)) {
            before = remainder.low;
            remainder.low -= frequency.low;
            remainder.high -= frequency.high + (before < frequency.low);
            if (bit > 64) overflow = 1;
            else if (bit > 32) quotient.high |= (MMU32)1 << (bit - 33);
            else quotient.low |= (MMU32)1 << (bit - 1);
        }
    }
    if (overflow) return 0;
    *result = quotient;
    return 1;
}
