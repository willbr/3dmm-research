/*
 * C fallbacks for the BRender ZB and FW asm-only routines.
 * Used on non-x86 builds where the .ASM files cannot be assembled.
 *
 * Status:
 *   - TriangleRenderPIZ2I: integer Gouraud scanner in triangle_i.h.
 *     Verified against asm path via tests/bren_rasterizer_test.cpp.
 *   - TrapezoidRenderPIZ2TIA, TrapezoidRenderPIZ2TIANT: portable textured
 *     scan converters, with viewport clipping for rounded edge vertices.
 *
 * The math/blit helpers (_sar16, SafeFixedMac2Div, _GetSysQual,
 * _MemFill_A, _MemRectFill_A, _MemCopyBits_A) are real C ports that
 * preserve their original semantics.
 */

#include <stdint.h>
#include <string.h>

#include "brender.h"
/* zb.h is in the brender_zb internal include path, not brender_fw's, so
 * use a relative include. The rasterizer fallback lives in brender_fw
 * because the FW pmmemops TU references _MemFill_A / _MemRectFill_A /
 * _MemCopyBits_A and needs them at FW link time -- see CMakeLists.txt
 * comment in bren/lib/. */
#include "../zb/zb.h"

/* Naive shared body for the textured-trapezoid scan-converters
 * TrapezoidRenderPIZ2TIA / TrapezoidRenderPIZ2TIANT (zb/t_piza.asm,
 * t_piza2.asm). The C wrapper in awtmz.c populates zb.{main,top,bot} edge
 * walkers, zb.p{z,i,u,v} parameter walkers, and zb.awsl pointer state for
 * the first scanline, then calls us to walk one half of the triangle.
 *
 * What we do per scanline:
 *   - Walk pixels from zb.awsl.start to zb.awsl.end (forward or backward
 *     depending on the long edge's direction relative to the minor edge).
 *   - Per pixel: interp z, intensity, u, v from zb.pz/pi/pu/pv .current,
 *     using grad_x as the per-pixel delta. Wrap (u,v) into the texture
 *     and sample. If transparent (texel == 0 for BPP=1, transp variant
 *     only) skip. Z-test against the 16-bit depth buffer; on pass, write
 *     z and shade_table[intensity*256 + texel].
 *
 * What we do per scanline boundary:
 *   - Long edge: zb.main.f += zb.main.d_f. Detect 32-bit unsigned overflow
 *     to know whether to use d_carry or d_nocarry on the parameter walkers.
 *   - Minor edge (zb.awsl.edge): same thing for the trapezoid's minor side.
 *   - Frame buffer pointer (zb.awsl.start) advances by main.d_i (+1 if
 *     main carried). End pointer advances by edge->d_i (+1 if edge carried).
 *     zstart advances by row_width worth of uint16_t (depth_row_width /
 *     sizeof(uint16_t)).
 *   - Per-parameter walker advances by d_carry if main carried, else
 *     d_nocarry.
 *
 * Differences from the asm:
 *   - We don't maintain zb.awsl's incremental u_int_current /
 *     source_current; we recompute texel position per pixel from
 *     zb.pu.current / zb.pv.current (slower but trivial to get right).
 *   - We use C arithmetic for the edge fraction overflow rather than
 *     CPU carry flags.
 */
static void render_trapezoid_tia(int transparent)
{
    struct scan_edge *edge = zb.awsl.edge;
    int count = edge->count;
    if (count <= 0 || !fw.output)
        return;

    int row_width = zb.row_width;
    int scan_y = edge->start;
    /* Keep edge cursors as addresses until clipping has put them inside the
     * image. A rounded vertex may start before the first pixel/scanline. */
    intptr_t fb_start = (intptr_t)zb.awsl.start;
    intptr_t fb_end = (intptr_t)zb.awsl.end;
    intptr_t zp_start = (intptr_t)zb.awsl.zstart;

    uint8_t *tex_buf = (uint8_t *)zb.texture_buffer;
    uint8_t *shade = (uint8_t *)zb.shade_table;
    int tex_w = zb.awsl.texture_width;
    int tex_stride = zb.awsl.texture_stride;
    int tex_h = zb.material->colour_map->height;

    br_fixed_ls dz_x = zb.pz.grad_x;
    br_fixed_ls di_x = zb.pi.grad_x;
    br_fixed_ls du_x = zb.pu.grad_x;
    br_fixed_ls dv_x = zb.pv.grad_x;

    while (count-- > 0)
    {
        /* These walkers mirror 32-bit assembly registers and wrap on carry. */
        uint32_t cur_z = (uint32_t)zb.pz.current;
        uint32_t cur_i = (uint32_t)zb.pi.current;
        uint32_t cur_u = (uint32_t)zb.pu.current;
        uint32_t cur_v = (uint32_t)zb.pv.current;

        /* Clip each span to its actual scanline, including subpixel maps.
         * Skipped pixels still advance Z/light/UV by the original gradients. */
        if (scan_y >= 0 && scan_y < fw.output->height)
        {
            intptr_t row_begin = (intptr_t)zb.colour_buffer + (intptr_t)scan_y * row_width + fw.output->base_x;
            intptr_t row_end = row_begin + fw.output->width;
            int direction = fb_start < fb_end ? 1 : -1;
            intptr_t first, limit;
            if (direction > 0)
            {
                first = fb_start > row_begin ? fb_start : row_begin;
                limit = fb_end < row_end ? fb_end : row_end;
            }
            else
            {
                first = (fb_start < row_end ? fb_start : row_end) - 1;
                limit = fb_end > row_begin ? fb_end : row_begin;
            }
            uint32_t skipped = (uint32_t)(first - fb_start);
            cur_z += skipped * (uint32_t)dz_x;
            cur_i += skipped * (uint32_t)di_x;
            cur_u += skipped * (uint32_t)du_x;
            cur_v += skipped * (uint32_t)dv_x;
            for (intptr_t pixel = first; direction > 0 ? pixel < limit : pixel >= limit; pixel += direction)
            {
                uint8_t *fb = (uint8_t *)pixel;
                uint16_t *zp = (uint16_t *)(zp_start + (pixel - fb_start) * 2);
                int u = (int)((int32_t)cur_u >> 16);
                int v = (int)((int32_t)cur_v >> 16);
                u %= tex_w;
                if (u < 0)
                    u += tex_w;
                v %= tex_h;
                if (v < 0)
                    v += tex_h;
                uint8_t texel = tex_buf[v * tex_stride + u];
                if (!transparent || texel != 0)
                {
                    uint16_t z16 = (uint16_t)((uint32_t)cur_z >> 16);
                    /* Asm uses cmp/sbb + jb, which skips only on strict
                     * depth < current; ties write. Use <= so a later
                     * triangle at the same Z overdraws (painter-style). */
                    if (z16 <= *zp)
                    {
                        *zp = z16;
                        int intensity = (int)(((uint32_t)cur_i >> 16) & 0xFF);
                        *fb = shade[intensity * 256 + texel];
                    }
                }
                cur_z += (uint32_t)direction * (uint32_t)dz_x;
                cur_i += (uint32_t)direction * (uint32_t)di_x;
                cur_u += (uint32_t)direction * (uint32_t)du_x;
                cur_v += (uint32_t)direction * (uint32_t)dv_x;
            }
        }

        /* Advance long edge: detect unsigned overflow from f += d_f. The
         * asm's `add zb.main.f, eax / jc` is exactly this carry test. */
        uint32_t main_f_old = (uint32_t)zb.main.f;
        zb.main.f = (br_int_32)(main_f_old + (uint32_t)zb.main.d_f);
        int main_carry = ((uint32_t)zb.main.f < main_f_old) ? 1 : 0;

        uint32_t edge_f_old = (uint32_t)edge->f;
        edge->f = (br_int_32)(edge_f_old + (uint32_t)edge->d_f);
        int edge_carry = ((uint32_t)edge->f < edge_f_old) ? 1 : 0;

        /* Frame buffer start/end advance. main.d_i / edge->d_i already
         * include row_width baked into them at setup time (see SETUP_PI:
         * d_i = sar16(grad) + row_width). +1 pixel on carry. */
        fb_start += zb.main.d_i + main_carry;
        fb_end += edge->d_i + edge_carry;
        /* Depth pointer must track the start pointer's diagonal motion --
         * the asm advances zstart (byte ptr) by 2*(main.d_i + main_carry).
         * Just adding row_width drops the X component, so the z-buffer ends
         * up sampled from neighbouring rows; that goes unnoticed for a
         * single triangle (depth buffer is uniformly clear there) but
         * fails spuriously when an earlier triangle has painted z-values
         * in the misaligned slot. */
        zp_start += 2 * (zb.main.d_i + main_carry);
        ++scan_y;

        /* Parameter walkers. d_carry is "delta when long edge carried",
         * d_nocarry the alternative. */
        if (main_carry)
        {
            zb.pz.current = (br_fixed_ls)((uint32_t)zb.pz.current + (uint32_t)zb.pz.d_carry);
            zb.pi.current = (br_fixed_ls)((uint32_t)zb.pi.current + (uint32_t)zb.pi.d_carry);
            zb.pu.current = (br_fixed_ls)((uint32_t)zb.pu.current + (uint32_t)zb.pu.d_carry);
            zb.pv.current = (br_fixed_ls)((uint32_t)zb.pv.current + (uint32_t)zb.pv.d_carry);
        }
        else
        {
            zb.pz.current = (br_fixed_ls)((uint32_t)zb.pz.current + (uint32_t)zb.pz.d_nocarry);
            zb.pi.current = (br_fixed_ls)((uint32_t)zb.pi.current + (uint32_t)zb.pi.d_nocarry);
            zb.pu.current = (br_fixed_ls)((uint32_t)zb.pu.current + (uint32_t)zb.pu.d_nocarry);
            zb.pv.current = (br_fixed_ls)((uint32_t)zb.pv.current + (uint32_t)zb.pv.d_nocarry);
        }
    }

    /* Persist running state for the next caller (the awtmz wrapper kicks
     * off a second TNAME() for the bottom half of the triangle that
     * continues from where we left off). */
    zb.awsl.start = (char *)fb_start;
    zb.awsl.end = (char *)fb_end;
    zb.awsl.zstart = (char *)zp_start;
    edge->start = scan_y;
    edge->count = 0; /* asm exits with count == 0; mirror that */
}

void TrapezoidRenderPIZ2TIA(void) /* transparent */
{
    render_trapezoid_tia(1);
}

void TrapezoidRenderPIZ2TIANT(void) /* non-transparent */
{
    render_trapezoid_tia(0);
}

/* Integer scan conversion shared with the original x86 algorithm. */
#include "triangle_i.h"

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
void _MemRectFill_A(char *dest, uint32_t dest_qual, uint32_t pwidth, uint32_t pheight, int32_t d_stride, uint32_t bpp,
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
