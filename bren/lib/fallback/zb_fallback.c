/*
 * C fallbacks for the BRender ZB and FW asm-only routines.
 * Used on non-x86 builds where the .ASM files cannot be assembled.
 *
 * The triangle/trapezoid scan-converters (TrapezoidRenderPIZ2TIA,
 * TrapezoidRenderPIZ2TIANT, TriangleRenderPIZ2I) are STUBBED -- they return
 * without rendering anything. This means 3D content will not appear on x64
 * builds. Linking succeeds, the application launches, and the rest of the
 * UI works. Porting the rasterizers proper is the scope of
 * docs/superpowers/specs/2026-05-01-brender-x64-enablement.md.
 *
 * The math/blit helpers (_sar16, SafeFixedMac2Div, _GetSysQual,
 * _MemFill_A, _MemRectFill_A, _MemCopyBits_A) are real C ports that
 * preserve their original semantics.
 */

#include <stdint.h>
#include <string.h>

/* Trapezoid scan-converters are called by the auto-generated triangle
 * renderers in awtmz.c. The renderers prepare per-edge interpolation state
 * and then call the trapezoid routine to fill the spans. Without it,
 * triangles are silently skipped. */
void TrapezoidRenderPIZ2TIA(void)
{
}

void TrapezoidRenderPIZ2TIANT(void)
{
}

/* TriangleRenderPIZ2I is the no-texture/intensity-only renderer used for
 * smooth/flat shading paths in zbsetup.c's mat_types_index_8 table. */
struct br_vertex;
void TriangleRenderPIZ2I(struct br_vertex *pvertex_0, struct br_vertex *pvertex_1, struct br_vertex *pvertex_2)
{
    (void)pvertex_0;
    (void)pvertex_1;
    (void)pvertex_2;
}

/* sar16: arithmetic shift right by 16 bits (sign-extending). The x86 asm
 * uses `sar eax,16` directly. */
int _sar16(int a)
{
    return a >> 16;
}

/* SafeFixedMac2Div: result = (a*b + c*d) / e, returns 0 on overflow.
 * The asm uses the safediv macro which checks for overflow during the
 * 64-bit / 32-bit divide. We use int64_t and trust modern C division
 * behavior, returning 0 if the divisor is zero. */
int SafeFixedMac2Div(int a, int b, int c, int d, int e)
{
    if (e == 0)
        return 0;
    int64_t numer = ((int64_t)a * b) + ((int64_t)c * d);
    int64_t result = numer / e;
    /* Check that result fits in int32_t. */
    if (result > INT32_MAX || result < INT32_MIN)
        return 0;
    return (int)result;
}

/* GetSysQual: returns the system data segment selector. On flat 32/64-bit
 * Windows there's no segmentation, so the result is unused -- return 0. */
uint16_t _GetSysQual(void)
{
    return 0;
}

/* _MemFill_A: fill `pixels` pixels of `bpp` bytes each with `colour`.
 * `dest_qual` is a far-pointer qualifier ignored on flat memory models. */
void _MemFill_A(char *dest, uint32_t dest_qual, uint32_t pixels, uint32_t bpp, uint32_t colour)
{
    (void)dest_qual;
    uint32_t i;
    switch (bpp)
    {
    case 1: {
        uint8_t v = (uint8_t)colour;
        memset(dest, v, pixels);
        break;
    }
    case 2: {
        uint16_t *p = (uint16_t *)dest;
        uint16_t v = (uint16_t)colour;
        for (i = 0; i < pixels; i++)
            p[i] = v;
        break;
    }
    case 3: {
        uint8_t *p = (uint8_t *)dest;
        uint8_t b0 = (uint8_t)colour;
        uint8_t b1 = (uint8_t)(colour >> 8);
        uint8_t b2 = (uint8_t)(colour >> 16);
        for (i = 0; i < pixels; i++)
        {
            *p++ = b0;
            *p++ = b1;
            *p++ = b2;
        }
        break;
    }
    case 4: {
        uint32_t *p = (uint32_t *)dest;
        for (i = 0; i < pixels; i++)
            p[i] = colour;
        break;
    }
    }
}

/* _MemRectFill_A: fill a rectangular block of pixels.
 * dest points to the top-left, d_stride is the destination row pitch. */
void _MemRectFill_A(char *dest, uint32_t dest_qual, uint32_t pwidth, uint32_t pheight, uint32_t d_stride, uint32_t bpp,
                    uint32_t colour)
{
    uint32_t row;
    for (row = 0; row < pheight; row++)
    {
        _MemFill_A(dest, dest_qual, pwidth, bpp, colour);
        dest += d_stride;
    }
}

/* _MemRectCopy_A: copy a rectangular pixel block, src -> dest. */
void _MemRectCopy_A(char *dest, uint32_t dest_qual, char *src, uint32_t src_qual, uint32_t pwidth, uint32_t pheight,
                    int32_t d_stride, int32_t s_stride, uint32_t bpp)
{
    (void)dest_qual;
    (void)src_qual;
    uint32_t row;
    uint32_t row_bytes = pwidth * bpp;
    for (row = 0; row < pheight; row++)
    {
        memcpy(dest, src, row_bytes);
        dest += d_stride;
        src += s_stride;
    }
}

/* _MemCopy_A: copy a single row of `pwidth` pixels at `bpp` bytes each. */
void _MemCopy_A(char *dest, uint32_t dest_qual, char *src, uint32_t src_qual, uint32_t pwidth, uint32_t bpp)
{
    (void)dest_qual;
    (void)src_qual;
    memcpy(dest, src, (size_t)pwidth * bpp);
}

/* _MemPixelSet: write one pixel of `bytes` bytes at `dest`. */
void _MemPixelSet(char *dest, uint32_t dest_qual, uint32_t bytes, uint32_t colour)
{
    (void)dest_qual;
    switch (bytes)
    {
    case 1:
        ((uint8_t *)dest)[0] = (uint8_t)colour;
        break;
    case 2:
        *(uint16_t *)dest = (uint16_t)colour;
        break;
    case 3:
        ((uint8_t *)dest)[0] = (uint8_t)colour;
        ((uint8_t *)dest)[1] = (uint8_t)(colour >> 8);
        ((uint8_t *)dest)[2] = (uint8_t)(colour >> 16);
        break;
    case 4:
        *(uint32_t *)dest = colour;
        break;
    }
}

/* _MemPixelGet: read one pixel of `bytes` bytes from `dest`, return as uint32. */
uint32_t _MemPixelGet(char *dest, uint32_t dest_qual, uint32_t bytes)
{
    (void)dest_qual;
    switch (bytes)
    {
    case 1:
        return ((uint8_t *)dest)[0];
    case 2:
        return *(uint16_t *)dest;
    case 3:
        return ((uint8_t *)dest)[0] | (((uint8_t *)dest)[1] << 8) | (((uint8_t *)dest)[2] << 16);
    case 4:
        return *(uint32_t *)dest;
    }
    return 0;
}

/* _MemCopyBits_A: bit-level pixel blit. Copies a rectangular region from
 * src (1 bit per pixel) into dest (bpp bytes per pixel) where set bits map
 * to `colour` and clear bits are skipped (transparent). Used for font
 * glyph rendering. */
void _MemCopyBits_A(char *dest, uint32_t dest_qual, int32_t d_stride, uint8_t *src, uint32_t s_stride,
                    uint32_t start_bit, uint32_t end_bit, uint32_t n_rows, uint32_t bpp, uint32_t colour)
{
    (void)dest_qual;
    uint32_t row;
    uint32_t bit_count = end_bit - start_bit;

    for (row = 0; row < n_rows; row++)
    {
        uint8_t *src_row = src + (start_bit >> 3);
        uint8_t *dst_byte = (uint8_t *)dest;
        uint32_t bit_offset = start_bit & 7;
        uint32_t bit;

        for (bit = 0; bit < bit_count; bit++)
        {
            /* Bit ordering in the source: MSB first within each byte. */
            uint32_t b = bit_offset + bit;
            uint8_t mask = (uint8_t)(0x80 >> (b & 7));
            if (src_row[b >> 3] & mask)
            {
                /* Set pixel `bit` to colour, in the destination's bpp. */
                uint8_t *p = dst_byte + bit * bpp;
                switch (bpp)
                {
                case 1:
                    p[0] = (uint8_t)colour;
                    break;
                case 2:
                    *(uint16_t *)p = (uint16_t)colour;
                    break;
                case 3:
                    p[0] = (uint8_t)colour;
                    p[1] = (uint8_t)(colour >> 8);
                    p[2] = (uint8_t)(colour >> 16);
                    break;
                case 4:
                    *(uint32_t *)p = colour;
                    break;
                }
            }
        }

        src += s_stride;
        dest += d_stride;
    }
}
