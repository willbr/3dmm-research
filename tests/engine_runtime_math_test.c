#include "mm/runtime_math.h"
#include <assert.h>
#include <stdio.h>
static void scaled(MMU32 low, MMU32 high, MMU32 scale, MMU32 freq_low, MMU32 freq_high,
    int ok, MMU32 expected_low, MMU32 expected_high)
{
    MMRuntimeCounter value, frequency, output;
    value.low = low; value.high = high; frequency.low = freq_low; frequency.high = freq_high;
    output.low = 42; output.high = 77;
    assert(mm_runtime_counter_scale(value, scale, frequency, &output) == ok);
    assert(output.low == (ok ? expected_low : 42) && output.high == (ok ? expected_high : 77));
}
int main(void)
{
    MMRuntimeCounter a, b, difference;
    scaled(0xffffffffUL, 0xffffffffUL, 0, 1, 0, 1, 0, 0);
    scaled(1, 0, 1, 0, 0, 0, 0, 0);
    scaled(0xffffffffUL, 0xffffffffUL, 1, 1, 0, 1, 0xffffffffUL, 0xffffffffUL);
    scaled(0xffffffffUL, 0xffffffffUL, 2, 1, 0, 0, 0, 0);
    scaled(0xffffffffUL, 0xffffffffUL, 0xffffffffUL, 0xffffffffUL, 0, 1, 0xffffffffUL, 0xffffffffUL);
    scaled(0xffffffffUL, 0xffffffffUL, 0xffffffffUL, 0xffffffffUL, 0xffffffffUL, 1, 0xffffffffUL, 0);
    scaled(0, 1, 1000, 10000000, 0, 1, 429496, 0);
    scaled(24000, 0, 1000, 24000000, 0, 1, 1, 0);
    scaled(23999, 0, 1000, 24000000, 0, 1, 0, 0);
    scaled(123456789, 0, 1000, 10000000, 0, 1, 12345, 0);
    scaled(0, 1, 1, 0xffffffffUL, 0, 1, 1, 0);
    a.low = 0; a.high = 1; b.low = 1; b.high = 0;
    difference = mm_runtime_counter_subtract(a, b);
    assert(difference.low == 0xffffffffUL && difference.high == 0);
    a.high = 0; difference = mm_runtime_counter_subtract(a, b);
    assert(difference.low == 0xffffffffUL && difference.high == 0xffffffffUL);
    puts("C89 runtime counters: exact scaling, fraction truncation, carry, wrap and failure publication passed.");
    return 0;
}
