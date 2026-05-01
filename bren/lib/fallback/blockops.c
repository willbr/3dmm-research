/*
 * C fallbacks for the BLOCKOPS.ASM fast-path routines.
 * Used on non-x86 builds where the MASM file cannot be assembled.
 *
 * The .ASM versions use `rep stosd`/`rep movsd` -- DWORD-stride.
 * These C versions are equivalent for DWORD-aligned destinations,
 * which is what BRender's pixmap and mesh code always provides.
 */

#include <stdint.h>
#include <string.h>

void BrBlockFill(void *dest_ptr, int value, int dwords)
{
    uint32_t *dest = (uint32_t *)dest_ptr;
    int i;
    for (i = 0; i < dwords; i++) {
        dest[i] = (uint32_t)value;
    }
}

void BrBlockCopy(void *dest_ptr, void *src_ptr, int dwords)
{
    memcpy(dest_ptr, src_ptr, (size_t)dwords * 4);
}

void BrFarBlockCopy(void *dest_ptr, void *src_ptr, int dwords)
{
    /* No segmented memory on flat 32/64-bit targets -- same as BrBlockCopy. */
    memcpy(dest_ptr, src_ptr, (size_t)dwords * 4);
}
