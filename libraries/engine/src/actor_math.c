/* Matrix and angle policy adapted from BRender matrix34.c and fixed386.asm.
 * Copyright (c) 1992,1993-1995 Argonaut Technologies Limited. All rights reserved. */
#include "three_mm/actor_math.h"
#include "three_mm/wide.h"
#include "actor_math_tables.h"
static ThreeMMU32 magnitude(ThreeMMI32 n)
{
    return n < 0 ? 0U - (ThreeMMU32)n : (ThreeMMU32)n;
}
static ThreeMMWide product(ThreeMMI32 a, ThreeMMI32 b)
{
    ThreeMMU32 x = magnitude(a), y = magnitude(b), part, old, high;
    ThreeMMWide result, zero;
    result.low = (x & 65535U) * (y & 65535U);
    high = (x >> 16) * (y >> 16);
    part = (x >> 16) * (y & 65535U);
    old = result.low;
    result.low += part << 16;
    high += (part >> 16) + (result.low < old);
    part = (x & 65535U) * (y >> 16);
    old = result.low;
    result.low += part << 16;
    high += (part >> 16) + (result.low < old);
    result.high = three_mm_signed_word(high);
    if ((a < 0) != (b < 0))
    {
        zero.low = 0;
        zero.high = 0;
        result = three_mm_wide_subtract(zero, result);
    }
    return result;
}
ThreeMMI32 three_mm_fixed_mac3(ThreeMMI32 a, ThreeMMI32 b, ThreeMMI32 c, ThreeMMI32 d, ThreeMMI32 e, ThreeMMI32 f)
{
    ThreeMMWide value = three_mm_wide_add(three_mm_wide_add(product(a, b), product(c, d)), product(e, f));
    return three_mm_signed_word((value.low >> 16) | ((ThreeMMU32)value.high << 16));
}
static ThreeMMI32 signed16(ThreeMMU32 value)
{
    value &= 65535U;
    return value < 32768U ? (ThreeMMI32)value : (ThreeMMI32)value - 65536L;
}
static ThreeMMI32 sincos(unsigned short angle, unsigned offset)
{
    unsigned index = (angle >> 8) + offset;
    ThreeMMI32 base = sin_table[index], delta = signed16((ThreeMMU32)sin_table[index + 1] - (ThreeMMU32)base);
    ThreeMMI32 value = (ThreeMMI32)(angle & 255U) * delta, interpolated;
    interpolated = value < 0 ? -1 - ((-1 - value) / 256L) : value / 256L;
    return signed16((ThreeMMU32)base + (ThreeMMU32)interpolated) * 2;
}
ThreeMMI32 three_mm_angle_sin(unsigned short angle)
{
    return sincos(angle, 0);
}
ThreeMMI32 three_mm_angle_cos(unsigned short angle)
{
    return sincos(angle, 64);
}
unsigned short three_mm_angle_atan2(ThreeMMI32 x, ThreeMMI32 y)
{
    ThreeMMU32 ax = magnitude(x), ay = magnitude(y), small = ax < ay ? ax : ay, large = ax < ay ? ay : ax;
    ThreeMMU32 angle, ratio = 0, index;
    unsigned i;
    if (!large)
        return 0;
    if (ax == ay)
        angle = 0x2000;
    else
    {
        /* small<large<=2^31: doubling the remainder loses no unsigned carry. */
        for (i = 0; i < 16; ++i)
        {
            small <<= 1;
            ratio <<= 1;
            if (small >= large)
            {
                small -= large;
                ratio |= 1U;
            }
        }
        index = ratio >> 8;
        angle = atan_table[index] + ((atan_table[index + 1] - atan_table[index]) * (ratio & 255U) >> 8);
        if (ax > ay)
            angle = 0x4000 - angle;
    }
    if (y < 0)
        angle = 0x8000 - angle;
    if (x < 0)
        angle = 0U - angle;
    return (unsigned short)angle;
}
void three_mm_matrix_identity(ThreeMMActorMatrix *m)
{
    unsigned i, j;
    for (i = 0; i < 4; ++i)
        for (j = 0; j < 3; ++j)
            m->m[i][j] = i == j ? 65536L : 0;
}
void three_mm_matrix_multiply(ThreeMMActorMatrix *out, const ThreeMMActorMatrix *b, const ThreeMMActorMatrix *c)
{
    unsigned i, j;
    ThreeMMI32 value;
    for (i = 0; i < 4; ++i)
        for (j = 0; j < 3; ++j)
        {
            value = three_mm_fixed_mac3(b->m[i][0], c->m[0][j], b->m[i][1], c->m[1][j], b->m[i][2], c->m[2][j]);
            if (i == 3)
                value = three_mm_fixed_add(value, c->m[3][j]);
            out->m[i][j] = value;
        }
}
void three_mm_matrix_rotate(ThreeMMActorMatrix *m, int axis, unsigned short angle, int pre)
{
    ThreeMMActorMatrix rotation, result;
    ThreeMMI32 sine = three_mm_angle_sin(angle), cosine = three_mm_angle_cos(angle);
    three_mm_matrix_identity(&rotation);
    if (axis == THREE_MM_MATRIX_X)
    {
        rotation.m[1][1] = rotation.m[2][2] = cosine;
        rotation.m[1][2] = sine;
        rotation.m[2][1] = -sine;
    }
    else if (axis == THREE_MM_MATRIX_Y)
    {
        rotation.m[0][0] = rotation.m[2][2] = cosine;
        rotation.m[0][2] = -sine;
        rotation.m[2][0] = sine;
    }
    else
    {
        rotation.m[0][0] = rotation.m[1][1] = cosine;
        rotation.m[0][1] = sine;
        rotation.m[1][0] = -sine;
    }
    if (pre)
        three_mm_matrix_multiply(&result, &rotation, m);
    else
        three_mm_matrix_multiply(&result, m, &rotation);
    *m = result;
}
void three_mm_matrix_scale(ThreeMMActorMatrix *m, ThreeMMI32 x, ThreeMMI32 y, ThreeMMI32 z)
{
    ThreeMMActorMatrix scale, result;
    three_mm_matrix_identity(&scale);
    scale.m[0][0] = x;
    scale.m[1][1] = y;
    scale.m[2][2] = z;
    three_mm_matrix_multiply(&result, m, &scale);
    *m = result;
}
