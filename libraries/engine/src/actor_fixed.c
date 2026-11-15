#include "mm/actor_motion.h"
#include "mm/wide.h"
static MMU32 magnitude(MMI32 n)
{
    return n < 0 ? 0U - (MMU32)n : (MMU32)n;
}
MMI32 mm_fixed_add(MMI32 a, MMI32 b)
{
    return mm_signed_word((MMU32)a + (MMU32)b);
}
MMI32 mm_fixed_sub(MMI32 a, MMI32 b)
{
    return mm_signed_word((MMU32)a - (MMU32)b);
}
MMI32 mm_fixed_mul(MMI32 a, MMI32 b)
{
    MMU32 x = magnitude(a), y = magnitude(b), low, result;
    low = (x & 65535U) * (y & 65535U);
    result = (low >> 16) + (x >> 16) * (y & 65535U) + (x & 65535U) * (y >> 16) + (((x >> 16) * (y >> 16)) << 16);
    if ((a < 0) != (b < 0))
        result = 0U - result - ((low & 65535U) != 0);
    return mm_signed_word(result);
}
/* Only the quotient's low word is required. Remainder stays below a signed
 * magnitude (<= 2^31), so its unsigned left shift never loses a carry. */
static MMU32 quotient(MMU32 high, MMU32 low, MMU32 divisor)
{
    MMU32 remainder = 0, result = 0, bit;
    int i;
    for (i = 63; i >= 0; --i)
    {
        bit = i >= 32 ? (high >> (i - 32)) & 1U : (low >> i) & 1U;
        remainder = (remainder << 1) | bit;
        result <<= 1;
        if (remainder >= divisor)
        {
            remainder -= divisor;
            result |= 1U;
        }
    }
    return result;
}
static MMI32 raw_div(MMI32 a, MMI32 b)
{
    MMU32 x = magnitude(a), q = quotient(x >> 16, x << 16, magnitude(b));
    return mm_signed_word((a < 0) != (b < 0) ? 0U - q : q);
}
MMI32 mm_fixed_div(MMI32 a, MMI32 b)
{
    MMI32 reciprocal, abs_a, abs_b;
    MMU32 q, product;
    abs_a = mm_signed_word(magnitude(a));
    abs_b = mm_signed_word(magnitude(b));
    if (b >= 65536L || b < -65536L || abs_a < abs_b)
        return raw_div(a, b);
    if (abs_b < 3)
        return (a > 0) == (b > 0) ? 2147483647L : (-2147483647L - 1);
    q = quotient(1, 0, magnitude(b));
    reciprocal = mm_signed_word(b < 0 ? 0U - q : q);
    product = (magnitude(a) >> 16) * (magnitude(reciprocal) >> 16);
    if (product < 65536UL)
        return mm_fixed_mul(a, reciprocal);
    return (a > 0) == (b > 0) ? 2147483647L : (-2147483647L - 1);
}
