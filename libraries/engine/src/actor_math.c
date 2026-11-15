/* Matrix and angle policy adapted from BRender matrix34.c and fixed386.asm.
 * Copyright (c) 1992,1993-1995 Argonaut Technologies Limited. All rights reserved. */
#include "mm/actor_math.h"
#include "mm/wide.h"
#include "actor_math_tables.h"
static MMU32 magnitude(MMI32 n)
{
    return n < 0 ? 0U - (MMU32)n : (MMU32)n;
}
MMI32 mm_fixed_mac3(MMI32 a, MMI32 b, MMI32 c, MMI32 d, MMI32 e, MMI32 f)
{
    /* Retail's scalar macro truncates each product before adding, unlike the
     * standalone BrFixedMac3 assembly primitive. Unsigned addition wraps. */
    return mm_signed_word((MMU32)mm_fixed_mul(a,b) +
                               (MMU32)mm_fixed_mul(c,d) +
                               (MMU32)mm_fixed_mul(e,f));
}
static MMI32 signed16(MMU32 value)
{
    value &= 65535U;
    return value < 32768U ? (MMI32)value : (MMI32)value - 65536L;
}
static MMI32 sincos(unsigned short angle, unsigned offset)
{
    unsigned index = (angle >> 8) + offset;
    MMI32 base = sin_table[index], delta = signed16((MMU32)sin_table[index + 1] - (MMU32)base);
    MMI32 value = (MMI32)(angle & 255U) * delta, interpolated;
    interpolated = value < 0 ? -1 - ((-1 - value) / 256L) : value / 256L;
    return signed16((MMU32)base + (MMU32)interpolated) * 2;
}
MMI32 mm_angle_sin(unsigned short angle)
{
    return sincos(angle, 0);
}
MMI32 mm_angle_cos(unsigned short angle)
{
    return sincos(angle, 64);
}
unsigned short mm_angle_atan2(MMI32 x, MMI32 y)
{
    MMU32 ax = magnitude(x), ay = magnitude(y), small = ax < ay ? ax : ay, large = ax < ay ? ay : ax;
    MMU32 angle, ratio = 0, index;
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
void mm_matrix_identity(MMActorMatrix *m)
{
    unsigned i, j;
    for (i = 0; i < 4; ++i)
        for (j = 0; j < 3; ++j)
            m->m[i][j] = i == j ? 65536L : 0;
}
void mm_matrix_multiply(MMActorMatrix *out, const MMActorMatrix *b, const MMActorMatrix *c)
{
    unsigned i, j;
    MMI32 value;
    for (i = 0; i < 4; ++i)
        for (j = 0; j < 3; ++j)
        {
            value = mm_fixed_mac3(b->m[i][0], c->m[0][j], b->m[i][1], c->m[1][j], b->m[i][2], c->m[2][j]);
            if (i == 3)
                value = mm_fixed_add(value, c->m[3][j]);
            out->m[i][j] = value;
        }
}
void mm_matrix_rotate(MMActorMatrix *m, int axis, unsigned short angle, int pre)
{
    MMActorMatrix rotation, result;
    MMI32 sine = mm_angle_sin(angle), cosine = mm_angle_cos(angle);
    mm_matrix_identity(&rotation);
    if (axis == MM_MATRIX_X)
    {
        rotation.m[1][1] = rotation.m[2][2] = cosine;
        rotation.m[1][2] = sine;
        rotation.m[2][1] = -sine;
    }
    else if (axis == MM_MATRIX_Y)
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
        mm_matrix_multiply(&result, &rotation, m);
    else
        mm_matrix_multiply(&result, m, &rotation);
    *m = result;
}
void mm_matrix_scale(MMActorMatrix *m, MMI32 x, MMI32 y, MMI32 z)
{
    MMActorMatrix scale, result;
    mm_matrix_identity(&scale);
    scale.m[0][0] = x;
    scale.m[1][1] = y;
    scale.m[2][2] = z;
    mm_matrix_multiply(&result, m, &scale);
    *m = result;
}
