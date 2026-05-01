/*
 * C fallbacks for the FIXED386.ASM fixed-point math routines.
 * Used on non-x86 builds where the MASM file cannot be assembled.
 *
 * `br_fixed_ls` is BRender's 16.16 fixed-point type — a 32-bit signed
 * integer where the low 16 bits are the fractional part. The asm versions
 * exploit x86's 32x32 -> 64-bit imul + shrd. The portable C version casts
 * to int64_t, multiplies, and shifts; equivalent on every architecture that
 * has an int64_t.
 *
 * Trig functions (BrFixedSin/Cos/ASin/ACos/ATan2) are not yet ported — they
 * use lookup tables defined in fixed386.asm; that's a separate piece of work.
 */

#include <stdint.h>
#include <math.h>

typedef int32_t br_fixed_ls;
typedef uint16_t br_angle; /* 16-bit BAM: 0x10000 == 360 degrees == 2*PI radians */

#define BR_FIXED_ONE 0x10000
#define BR_PI 3.14159265358979323846

br_fixed_ls BrFixedAbs(br_fixed_ls a)
{
    return a < 0 ? -a : a;
}

br_fixed_ls BrFixedMul(br_fixed_ls a, br_fixed_ls b)
{
    return (br_fixed_ls)(((int64_t)a * (int64_t)b) >> 16);
}

br_fixed_ls BrFixedDiv(br_fixed_ls a, br_fixed_ls b)
{
    return (br_fixed_ls)(((int64_t)a << 16) / b);
}

br_fixed_ls BrFixedDivR(br_fixed_ls a, br_fixed_ls b)
{
    /* Rounded divide: add 0.5 to the high half before the divide. */
    int64_t num = ((int64_t)a << 16);
    return (br_fixed_ls)((num + (b >> 1)) / b);
}

br_fixed_ls BrFixedDivF(br_fixed_ls a, br_fixed_ls b)
{
    /* Floor divide: same as plain divide for fixed point in this codebase. */
    return (br_fixed_ls)(((int64_t)a << 16) / b);
}

br_fixed_ls BrFixedRcp(br_fixed_ls a)
{
    return (br_fixed_ls)(((int64_t)1 << 32) / a);
}

br_fixed_ls BrFixedSqr(br_fixed_ls a)
{
    return (br_fixed_ls)(((int64_t)a * (int64_t)a) >> 16);
}

br_fixed_ls BrFixedSqr2(br_fixed_ls a, br_fixed_ls b)
{
    return (br_fixed_ls)((((int64_t)a * a) + ((int64_t)b * b)) >> 16);
}

br_fixed_ls BrFixedSqr3(br_fixed_ls a, br_fixed_ls b, br_fixed_ls c)
{
    return (br_fixed_ls)((((int64_t)a * a) + ((int64_t)b * b) + ((int64_t)c * c)) >> 16);
}

br_fixed_ls BrFixedSqr4(br_fixed_ls a, br_fixed_ls b, br_fixed_ls c, br_fixed_ls d)
{
    return (br_fixed_ls)((((int64_t)a * a) + ((int64_t)b * b) + ((int64_t)c * c) + ((int64_t)d * d)) >> 16);
}

br_fixed_ls BrFixedMac2(br_fixed_ls a, br_fixed_ls b, br_fixed_ls c, br_fixed_ls d)
{
    return (br_fixed_ls)((((int64_t)a * b) + ((int64_t)c * d)) >> 16);
}

br_fixed_ls BrFixedMac3(br_fixed_ls a, br_fixed_ls b, br_fixed_ls c, br_fixed_ls d, br_fixed_ls e, br_fixed_ls f)
{
    return (br_fixed_ls)((((int64_t)a * b) + ((int64_t)c * d) + ((int64_t)e * f)) >> 16);
}

br_fixed_ls BrFixedMac4(br_fixed_ls a, br_fixed_ls b, br_fixed_ls c, br_fixed_ls d, br_fixed_ls e, br_fixed_ls f,
                        br_fixed_ls g, br_fixed_ls h)
{
    return (br_fixed_ls)((((int64_t)a * b) + ((int64_t)c * d) + ((int64_t)e * f) + ((int64_t)g * h)) >> 16);
}

br_fixed_ls BrFixedMulDiv(br_fixed_ls a, br_fixed_ls b, br_fixed_ls c)
{
    /* (a*b)/c, all in 16.16. Compute a*b in 32.32, then divide by c (16.16). */
    return (br_fixed_ls)(((int64_t)a * b) / c);
}

br_fixed_ls BrFixedMac2Div(br_fixed_ls a, br_fixed_ls b, br_fixed_ls c, br_fixed_ls d, br_fixed_ls e)
{
    return (br_fixed_ls)((((int64_t)a * b) + ((int64_t)c * d)) / e);
}

br_fixed_ls BrFixedMac3Div(br_fixed_ls a, br_fixed_ls b, br_fixed_ls c, br_fixed_ls d, br_fixed_ls e, br_fixed_ls f,
                           br_fixed_ls g)
{
    return (br_fixed_ls)((((int64_t)a * b) + ((int64_t)c * d) + ((int64_t)e * f)) / g);
}

br_fixed_ls BrFixedMac4Div(br_fixed_ls a, br_fixed_ls b, br_fixed_ls c, br_fixed_ls d, br_fixed_ls e, br_fixed_ls f,
                           br_fixed_ls g, br_fixed_ls h, br_fixed_ls i)
{
    return (br_fixed_ls)((((int64_t)a * b) + ((int64_t)c * d) + ((int64_t)e * f) + ((int64_t)g * h)) / i);
}

/* FMac variants: pa/pc/pe/pg are pre-multiplied accumulators (32.32 layout
 * stored as two 32-bit halves in the asm). For C portability we treat them as
 * a single int64_t accumulator and add b/d/f/h products. The asm packs them
 * as fraction:fixed pairs; we just use int64_t directly — equivalent value,
 * simpler representation. */
br_fixed_ls BrFixedFMac2(br_fixed_ls a, br_fixed_ls b, br_fixed_ls c, br_fixed_ls d)
{
    return (br_fixed_ls)((((int64_t)a * b) + ((int64_t)c * d)) >> 16);
}

br_fixed_ls BrFixedFMac3(br_fixed_ls a, br_fixed_ls b, br_fixed_ls c, br_fixed_ls d, br_fixed_ls e, br_fixed_ls f)
{
    return (br_fixed_ls)((((int64_t)a * b) + ((int64_t)c * d) + ((int64_t)e * f)) >> 16);
}

br_fixed_ls BrFixedFMac4(br_fixed_ls a, br_fixed_ls b, br_fixed_ls c, br_fixed_ls d, br_fixed_ls e, br_fixed_ls f,
                         br_fixed_ls g, br_fixed_ls h)
{
    return (br_fixed_ls)((((int64_t)a * b) + ((int64_t)c * d) + ((int64_t)e * f) + ((int64_t)g * h)) >> 16);
}

/* Length: sqrt(a^2 + b^2 + ...). The asm uses a lookup table; the portable
 * version uses libm sqrt. For 16.16 inputs the squared sum fits in int64_t
 * for any practical magnitude. */
br_fixed_ls BrFixedLength2(br_fixed_ls a, br_fixed_ls b)
{
    int64_t sumSq = ((int64_t)a * a) + ((int64_t)b * b);
    /* Result is sqrt(sumSq) where sumSq is in 32.32; sqrt yields 16.16. */
    return (br_fixed_ls)sqrt((double)sumSq);
}

br_fixed_ls BrFixedLength3(br_fixed_ls a, br_fixed_ls b, br_fixed_ls c)
{
    int64_t sumSq = ((int64_t)a * a) + ((int64_t)b * b) + ((int64_t)c * c);
    return (br_fixed_ls)sqrt((double)sumSq);
}

br_fixed_ls BrFixedLength4(br_fixed_ls a, br_fixed_ls b, br_fixed_ls c, br_fixed_ls d)
{
    int64_t sumSq = ((int64_t)a * a) + ((int64_t)b * b) + ((int64_t)c * c) + ((int64_t)d * d);
    return (br_fixed_ls)sqrt((double)sumSq);
}

/* Reciprocal length: 1 / sqrt(sumSq). Result is in 16.16 fixed. */
br_fixed_ls BrFixedRLength2(br_fixed_ls a, br_fixed_ls b)
{
    double len = sqrt((double)(((int64_t)a * a) + ((int64_t)b * b)));
    if (len == 0.0)
        return 0;
    return (br_fixed_ls)((double)((int64_t)BR_FIXED_ONE * BR_FIXED_ONE) / len);
}

br_fixed_ls BrFixedRLength3(br_fixed_ls a, br_fixed_ls b, br_fixed_ls c)
{
    double len = sqrt((double)(((int64_t)a * a) + ((int64_t)b * b) + ((int64_t)c * c)));
    if (len == 0.0)
        return 0;
    return (br_fixed_ls)((double)((int64_t)BR_FIXED_ONE * BR_FIXED_ONE) / len);
}

br_fixed_ls BrFixedRLength4(br_fixed_ls a, br_fixed_ls b, br_fixed_ls c, br_fixed_ls d)
{
    double len = sqrt((double)(((int64_t)a * a) + ((int64_t)b * b) + ((int64_t)c * c) + ((int64_t)d * d)));
    if (len == 0.0)
        return 0;
    return (br_fixed_ls)((double)((int64_t)BR_FIXED_ONE * BR_FIXED_ONE) / len);
}

/* Trig: BAM angles wrap at 0x10000 = 2*PI. The asm uses a precomputed sine
 * table; the portable version uses libm sin/cos. */
br_fixed_ls BrFixedSin(br_angle input)
{
    double rad = (double)input * (2.0 * BR_PI / 65536.0);
    return (br_fixed_ls)(sin(rad) * BR_FIXED_ONE);
}

br_fixed_ls BrFixedCos(br_angle input)
{
    double rad = (double)input * (2.0 * BR_PI / 65536.0);
    return (br_fixed_ls)(cos(rad) * BR_FIXED_ONE);
}

br_angle BrFixedASin(br_fixed_ls input)
{
    double r = (double)input / BR_FIXED_ONE;
    if (r > 1.0)
        r = 1.0;
    if (r < -1.0)
        r = -1.0;
    return (br_angle)(asin(r) * (65536.0 / (2.0 * BR_PI)));
}

br_angle BrFixedACos(br_fixed_ls input)
{
    double r = (double)input / BR_FIXED_ONE;
    if (r > 1.0)
        r = 1.0;
    if (r < -1.0)
        r = -1.0;
    return (br_angle)(acos(r) * (65536.0 / (2.0 * BR_PI)));
}

/* Note: BRender's asm signature is `BrFixedATan2(_F x, _F y)` -- x then y.
 * The asm body matches the convention atan2(y/x), but takes them swapped at
 * the API level. Mirror the asm signature exactly. */
br_angle BrFixedATan2(br_fixed_ls x, br_fixed_ls y)
{
    return (br_angle)(atan2((double)y, (double)x) * (65536.0 / (2.0 * BR_PI)));
}

br_angle BrFixedATan2Fast(br_fixed_ls x, br_fixed_ls y)
{
    return BrFixedATan2(x, y);
}
