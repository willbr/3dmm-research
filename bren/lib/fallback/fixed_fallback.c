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

/* FMac variants compute a*b + c*d (+ e*f (+ g*h)) where the a/c/e/g args
 * are 1.15 signed FRACTIONS (br_fraction = short) and b/d/f/h are 16.16
 * fixed (br_fixed_ls). Per the asm signatures (`pa: _FF, pb: _F, ...`
 * in fw/fixed386.asm) the asm uses `movsx` to widen each fraction and
 * ends with `shrd eax,edx,15`. The 15 -- not 16 -- comes from the
 * fraction having only 15 fractional bits, so the product (1.15 * 16.16)
 * needs to lose 15 bits to land back in 16.16.
 *
 * The C signature still takes br_fixed_ls (32-bit) for every arg so the
 * fwiproto.h prototype matches the asm's stack layout. Callers pass the
 * fractions as sign-extended shorts; we just truncate-and-extend through
 * int16_t to mirror what `movsx` does in the asm. The off-by-one shift
 * was the cause of x64 face culling rendering models inside-out: every
 * BrFVector3Dot(&face->n, &eye) result was half the correct value, so
 * the `< face->d` cull test flipped for any face near grazing. */
br_fixed_ls BrFixedFMac2(br_fixed_ls a, br_fixed_ls b, br_fixed_ls c, br_fixed_ls d)
{
    return (br_fixed_ls)((((int64_t)(int16_t)a * b) + ((int64_t)(int16_t)c * d)) >> 15);
}

br_fixed_ls BrFixedFMac3(br_fixed_ls a, br_fixed_ls b, br_fixed_ls c, br_fixed_ls d, br_fixed_ls e, br_fixed_ls f)
{
    return (br_fixed_ls)((((int64_t)(int16_t)a * b) + ((int64_t)(int16_t)c * d) + ((int64_t)(int16_t)e * f)) >> 15);
}

br_fixed_ls BrFixedFMac4(br_fixed_ls a, br_fixed_ls b, br_fixed_ls c, br_fixed_ls d, br_fixed_ls e, br_fixed_ls f,
                         br_fixed_ls g, br_fixed_ls h)
{
    return (br_fixed_ls)((((int64_t)(int16_t)a * b) + ((int64_t)(int16_t)c * d) + ((int64_t)(int16_t)e * f) +
                          ((int64_t)(int16_t)g * h)) >> 15);
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

/* Sin/cos via the same lookup table the asm uses (extracted byte-for-byte
 * from fw/fixed386.asm's `sin_table` / `cos_table` data block, which is
 * one contiguous 322-entry int16 array; sin_table starts at offset 0,
 * cos_table at offset 64 -- the cos = sin(theta + pi/2) shift).
 *
 * Replacing the previous libm-based fallback was load-bearing for x64
 * actor rendering: the libm version diverged from the asm by a few LSBs
 * at most angles, which was enough to flip per-pixel z-test ties at
 * overlap boundaries between separately-transformed actors -- the
 * "hair over face / arms over body" symptom the user reported. The
 * tests/golden/props-overlap scene reproduces it (5 intersecting real
 * 3DMM props in distinct colours; 68 pixels swap colour at the overlap
 * boundary with libm sin/cos, 0 pixels with this LUT). */
static const int16_t g_sin_lut[322] = {
    0x0000, 0x0324, 0x0647, 0x096A, 0x0C8B, 0x0FAB, 0x12C7, 0x15E1, 0x18F8, 0x1C0B, 0x1F19, 0x2223, 0x2527, 0x2826,
    0x2B1E, 0x2E10, 0x30FB, 0x33DE, 0x36B9, 0x398C, 0x3C56, 0x3F16, 0x41CD, 0x447A, 0x471C, 0x49B3, 0x4C3F, 0x4EBF,
    0x5133, 0x539A, 0x55F4, 0x5842, 0x5A81, 0x5CB3, 0x5ED6, 0x60EB, 0x62F1, 0x64E7, 0x66CE, 0x68A5, 0x6A6C, 0x6C23,
    0x6DC9, 0x6F5E, 0x70E1, 0x7254, 0x73B5, 0x7503, 0x7640, 0x776B, 0x7883, 0x7989, 0x7A7C, 0x7B5C, 0x7C29, 0x7CE2,
    0x7D89, 0x7E1C, 0x7E9C, 0x7F08, 0x7F61, 0x7FA6, 0x7FD7, 0x7FF5, 0x7FFF, 0x7FF5, 0x7FD7, 0x7FA6, 0x7F61, 0x7F08,
    0x7E9C, 0x7E1C, 0x7D89, 0x7CE2, 0x7C29, 0x7B5C, 0x7A7C, 0x7989, 0x7883, 0x776B, 0x7640, 0x7503, 0x73B5, 0x7254,
    0x70E1, 0x6F5E, 0x6DC9, 0x6C23, 0x6A6C, 0x68A5, 0x66CE, 0x64E7, 0x62F1, 0x60EB, 0x5ED6, 0x5CB3, 0x5A81, 0x5842,
    0x55F4, 0x539A, 0x5133, 0x4EBF, 0x4C3F, 0x49B3, 0x471C, 0x447A, 0x41CD, 0x3F16, 0x3C56, 0x398C, 0x36B9, 0x33DE,
    0x30FB, 0x2E10, 0x2B1E, 0x2826, 0x2527, 0x2223, 0x1F19, 0x1C0B, 0x18F8, 0x15E1, 0x12C7, 0x0FAB, 0x0C8B, 0x096A,
    0x0647, 0x0324,
    /* second half (negative) */
    0x0000, (int16_t)0xFCDC, (int16_t)0xF9B9, (int16_t)0xF696, (int16_t)0xF375, (int16_t)0xF055, (int16_t)0xED39,
    (int16_t)0xEA1F, (int16_t)0xE708, (int16_t)0xE3F5, (int16_t)0xE0E7, (int16_t)0xDDDD, (int16_t)0xDAD9, (int16_t)0xD7DA,
    (int16_t)0xD4E2, (int16_t)0xD1F0, (int16_t)0xCF05, (int16_t)0xCC22, (int16_t)0xC947, (int16_t)0xC674, (int16_t)0xC3AA,
    (int16_t)0xC0EA, (int16_t)0xBE33, (int16_t)0xBB86, (int16_t)0xB8E4, (int16_t)0xB64D, (int16_t)0xB3C1, (int16_t)0xB141,
    (int16_t)0xAECD, (int16_t)0xAC66, (int16_t)0xAA0C, (int16_t)0xA7BE, (int16_t)0xA57F, (int16_t)0xA34D, (int16_t)0xA12A,
    (int16_t)0x9F15, (int16_t)0x9D0F, (int16_t)0x9B19, (int16_t)0x9932, (int16_t)0x975B, (int16_t)0x9594, (int16_t)0x93DD,
    (int16_t)0x9237, (int16_t)0x90A2, (int16_t)0x8F1F, (int16_t)0x8DAC, (int16_t)0x8C4B, (int16_t)0x8AFD, (int16_t)0x89C0,
    (int16_t)0x8895, (int16_t)0x877D, (int16_t)0x8677, (int16_t)0x8584, (int16_t)0x84A4, (int16_t)0x83D7, (int16_t)0x831E,
    (int16_t)0x8277, (int16_t)0x81E4, (int16_t)0x8164, (int16_t)0x80F8, (int16_t)0x809F, (int16_t)0x805A, (int16_t)0x8029,
    (int16_t)0x800B, (int16_t)0x8001, (int16_t)0x800B, (int16_t)0x8029, (int16_t)0x805A, (int16_t)0x809F, (int16_t)0x80F8,
    (int16_t)0x8164, (int16_t)0x81E4, (int16_t)0x8277, (int16_t)0x831E, (int16_t)0x83D7, (int16_t)0x84A4, (int16_t)0x8584,
    (int16_t)0x8677, (int16_t)0x877D, (int16_t)0x8895, (int16_t)0x89C0, (int16_t)0x8AFD, (int16_t)0x8C4B, (int16_t)0x8DAC,
    (int16_t)0x8F1F, (int16_t)0x90A2, (int16_t)0x9237, (int16_t)0x93DD, (int16_t)0x9594, (int16_t)0x975B, (int16_t)0x9932,
    (int16_t)0x9B19, (int16_t)0x9D0F, (int16_t)0x9F15, (int16_t)0xA12A, (int16_t)0xA34D, (int16_t)0xA57F, (int16_t)0xA7BE,
    (int16_t)0xAA0C, (int16_t)0xAC66, (int16_t)0xAECD, (int16_t)0xB141, (int16_t)0xB3C1, (int16_t)0xB64D, (int16_t)0xB8E4,
    (int16_t)0xBB86, (int16_t)0xBE33, (int16_t)0xC0EA, (int16_t)0xC3AA, (int16_t)0xC674, (int16_t)0xC947, (int16_t)0xCC22,
    (int16_t)0xCF05, (int16_t)0xD1F0, (int16_t)0xD4E2, (int16_t)0xD7DA, (int16_t)0xDAD9, (int16_t)0xDDDD, (int16_t)0xE0E7,
    (int16_t)0xE3F5, (int16_t)0xE708, (int16_t)0xEA1F, (int16_t)0xED39, (int16_t)0xF055, (int16_t)0xF375, (int16_t)0xF696,
    (int16_t)0xF9B9, (int16_t)0xFCDC,
    /* one quarter wave wrapping */
    0x0000, 0x0324, 0x0647, 0x096A, 0x0C8B, 0x0FAB, 0x12C7, 0x15E1, 0x18F8, 0x1C0B, 0x1F19, 0x2223, 0x2527, 0x2826,
    0x2B1E, 0x2E10, 0x30FB, 0x33DE, 0x36B9, 0x398C, 0x3C56, 0x3F16, 0x41CD, 0x447A, 0x471C, 0x49B3, 0x4C3F, 0x4EBF,
    0x5133, 0x539A, 0x55F4, 0x5842, 0x5A81, 0x5CB3, 0x5ED6, 0x60EB, 0x62F1, 0x64E7, 0x66CE, 0x68A5, 0x6A6C, 0x6C23,
    0x6DC9, 0x6F5E, 0x70E1, 0x7254, 0x73B5, 0x7503, 0x7640, 0x776B, 0x7883, 0x7989, 0x7A7C, 0x7B5C, 0x7C29, 0x7CE2,
    0x7D89, 0x7E1C, 0x7E9C, 0x7F08, 0x7F61, 0x7FA6, 0x7FD7, 0x7FF5, 0x7FFF, 0x7FFF,
};

/* Replicates the asm INTERP_FUNCTION sequence on the LUT: high byte of
 * input is the table index, low byte is the linear-interp fraction. The
 * raw 16-bit value at the chosen lerp position is sign-extended and
 * doubled to land in 16.16 fixed (the asm's `cwde; add eax, eax` tail). */
static br_fixed_ls sincos_lookup(br_angle input, int table_offset)
{
    int high = (input >> 8) & 0xFF;
    int low = input & 0xFF;
    int idx = high + table_offset;
    int16_t base = g_sin_lut[idx];
    int16_t next = g_sin_lut[idx + 1];
    int16_t delta = (int16_t)(next - base); /* may wrap; matches asm 16-bit sub */
    int product = low * (int)delta;
    int16_t interp = (int16_t)(product >> 8); /* asm grabs bits 8..23 of dx:ax */
    int16_t sum = (int16_t)(base + interp);
    return ((br_fixed_ls)sum) << 1;
}

br_fixed_ls BrFixedSin(br_angle input)
{
    return sincos_lookup(input, 0);
}

br_fixed_ls BrFixedCos(br_angle input)
{
    return sincos_lookup(input, 64); /* cos_table is sin_table shifted 1/4 wave (64 entries) */
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
