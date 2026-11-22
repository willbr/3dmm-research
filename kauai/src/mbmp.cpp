/* Copyright (c) Microsoft Corporation.
   Licensed under the MIT License. */

/***************************************************************************
    Author: ShonK
    Project: Kauai
    Reviewed:
    Copyright (c) Microsoft Corporation

    Masked bitmap routines that a tool might use.

***************************************************************************/
#include "kauai_core.h"
#include "region.h"
#include "mbmp.h"
#include "bitmap_format.h"
#include "handle_services.h"
#include "mm/bitmap_file.h"
ASSERTNAME

namespace
{
const MMMaskedOwnerHost &MaskedHost()
{
    static const MMMaskedOwnerHost host = {
        {nullptr, mm_masked_native_big_endian(), koskCur,
         [](void *, MMI32 n, void **out) -> int { return FAllocPv(out, n, fmemNil, mprNormal); },
         [](void *, void **p) { FreePpv(p); },
         [](void *, int code) {
             if (code == MM_MASKED_EMPTY)
             {
                 Warn("Empty source bitmap for MaskedBitmapMBMP");
             }
             else
             {
                 Bug("Wrong sized block");
             }
         },
         KauaiDecodeBlockHandle,
         [](void *, void *filename, unsigned char transparent, MMMaskedSource *out) -> int {
             byte *pixels = nullptr;
             LegacyLong width, height;
             bool upsideDown;
             if (!FReadBitmap(static_cast<PFilename>(filename), &pixels, nullptr, &width, &height, &upsideDown,
                              transparent))
                 return 0;
             *out = {pixels, width, height, upsideDown};
             return 1;
         }},
        KauaiResourceObjectAllocator(),
#ifdef DEBUG
        MarkMaskedBitmap
#else
        nullptr
#endif
    };
    return host;
}
} // namespace

PMaskedBitmapMBMP NewMaskedBitmap(const byte *pixels, LegacyLong row, LegacyLong height, const RC *extract,
                                 LegacyLong x, LegacyLong y, byte transparent, LegacyUlong flags, byte fill)
{
    AssertIn(row, 1, kcbMax);
    AssertIn(height, 1, kcbMax);
    AssertPvCb(const_cast<byte *>(pixels), LwMul(row, height));
    AssertVarMem(const_cast<RC *>(extract));
    if (!extract)
        return nullptr;
    const MMMaskedRect rect = {extract->xpLeft, extract->ypTop, extract->xpRight, extract->ypBottom};
    return mm_masked_owner_new(pixels, row, height, &rect, x, y, transparent, flags, fill, &MaskedHost());
}

PMaskedBitmapMBMP ImportMaskedBitmap(PFilename filename, byte transparent, LegacyLong x, LegacyLong y,
                                    LegacyUlong flags, byte fill)
{
    AssertPo(filename, ffniFile);
    return mm_masked_owner_import(filename, transparent, x, y, flags, fill, &MaskedHost());
}

PMaskedBitmapMBMP ReadMaskedBitmap(PDataBlock block)
{
    AssertPo(block, 0);
    return mm_masked_owner_read_block(block->CoreBlock(), &MaskedHost());
}

PMaskedBitmapMBMP ReadMaskedBitmap(const MMReadStream *stream)
{
    return mm_masked_owner_read_stream(stream, &MaskedHost());
}

PMaskedBitmapMBMP FetchMaskedBitmap(PResourceCache cache, ChunkTagOrType type, ChunkNumber number)
{
    return static_cast<PMaskedBitmapMBMP>(
        cache->FetchOwned(type, number, ReadCachedMaskedBitmap, mm_masked_owner_services()));
}

PMaskedBitmapMBMP FetchMaskedBitmap(MMResourceCache *cache, ChunkTagOrType type, ChunkNumber number)
{
    return static_cast<PMaskedBitmapMBMP>(
        mm_resource_cache_fetch_owned_chunk(cache, type, number, ReadCachedMaskedBitmap, mm_masked_owner_services()));
}

int ReadCachedMaskedBitmap(MMResourceCache *, MMU32, MMU32, MMOwnedBlock *block, void **out, MMI32 *bytes)
{
    return mm_masked_owner_fetch(block, out, bytes, &MaskedHost());
}

LegacyLong MaskedBitmapSize(PMaskedBitmapMBMP owner)
{
    AssertMaskedBitmap(owner);
    return mm_masked_owner_bitmap(owner)->bytes;
}

bool WriteMaskedBitmap(PMaskedBitmapMBMP owner, PDataBlock block)
{
    AssertMaskedBitmap(owner);
    AssertPo(block, 0);
    MMBlockIO io;
    MMStorageWriter writer;
    return mm_owned_block_io(block->CoreBlock(), &io) == MM_DATA_OK &&
           mm_block_io_writer(&io, &writer) == MM_DATA_OK && mm_masked_owner_write(owner, &writer);
}

void GetMaskedBitmapBounds(PMaskedBitmapMBMP owner, RC *out)
{
    AssertMaskedBitmap(owner);
    AssertVarMem(out);
    MMMaskedRect r;
    mm_masked_bounds(mm_masked_owner_bitmap(owner), mm_masked_native_big_endian(), &r);
    out->Set(r.left, r.top, r.right, r.bottom);
}

bool MaskedBitmapContains(PMaskedBitmapMBMP owner, LegacyLong x, LegacyLong y)
{
    AssertMaskedBitmap(owner);
    return mm_masked_hit(mm_masked_owner_bitmap(owner), mm_masked_native_big_endian(), x, y) != 0;
}

#ifdef DEBUG
void AssertMaskedBitmap(PMaskedBitmapMBMP owner, bool allowNull)
{
    Assert(owner || allowNull, "missing masked bitmap");
    if (!owner)
        return;
    const auto bitmap = mm_masked_owner_bitmap(owner);
    AssertPvCb(bitmap->data, bitmap->bytes);
    MMMaskedRle view = {};
    AssertDo(mm_masked_rle_view(bitmap, mm_masked_native_big_endian(), &view), "wrong bitmap bounds");
    size_t total = 28 + bitmap->row_bytes;
    const auto rows = static_cast<const short *>(view.row_bytes);
    for (LegacyLong i = 0; i < view.height; ++i)
    {
        AssertIn(rows[i], 0, kcbMax);
        total += rows[i];
    }
    Assert(total == size_t(bitmap->bytes), "wrong bitmap size");
}

void MarkMaskedBitmap(PMaskedBitmapMBMP owner)
{
    if (!owner)
        return;
    AssertMaskedBitmap(owner);
    MarkPv(owner);
    MarkPv(mm_masked_owner_bitmap(owner)->data);
    const auto binding = mm_masked_owner_binding(owner);
    if (binding->cache && !binding->attached)
        mm_resource_cache_mark(binding->cache);
}
#endif

/***************************************************************************
    Given an Filename refering to a bitmap file, returns the interesting parts of
    the header and the pixel data and palette.  Fails if the bitmap is not
    8 bits, uncompressed.  Any or all of the output pointers may be nil.

    input:
        pfni	--	the file from which to read the data

    output:
        The following variable parameters are valid only if FReadBitmap
        returns true (*pprgb and *ppglclr will be nil if this fails):
            pprgb			-- pointer to the allocated memory for pixel data
            ppglclr			-- the palette
            pdxp			-- the width of the bitmap
            pdyp			-- the height of the bitmap
            pfUpsideDown	--	fTrue if the bitmap is upside down
        returns fTrue if it succeeds
***************************************************************************/
namespace
{
struct BitmapContext
{
    byte **pixels = nullptr;
    PDynamicArray *palette = nullptr;
    LegacyLong *width = nullptr, *height = nullptr;
    bool *upside = nullptr;
};
MMBitmapServices BitmapServices(BitmapContext *context)
{
    MMBitmapServices s = {};
    s.context = context;
    s.open = [](void *, void *p) -> void * { return FileObject::PfilOpen(static_cast<PFilename>(p)); };
    s.create = [](void *, void *p) -> void * { return FileObject::PfilCreate(static_cast<PFilename>(p)); };
    s.release = [](void *, void *p) { static_cast<BASE *>(p)->Release(); };
    s.size = [](void *, void *p) -> MMI32 { return static_cast<PFileObject>(p)->FpMac(); };
    s.read = [](void *, void *p, void *out, MMI32 n, MMI32 at) -> int {
        return static_cast<PFileObject>(p)->FReadRgb(out, n, at);
    };
    s.write = [](void *, void *p, const void *data, MMI32 n, MMI32 at) -> int {
        return static_cast<PFileObject>(p)->FWriteRgb(const_cast<void *>(data), n, at);
    };
    s.allocate = [](void *, MMI32 n, void **out) -> int { return FAllocPv(out, n, fmemNil, mprNormal); };
    s.free_memory = [](void *, void **p) { FreePpv(p); };
    s.palette_create = [](void *) -> void * {
        auto palette = DynamicArray::PglNew(sizeof(Color), 256);
        if (palette)
            AssertDo(palette->FSetIvMac(256), "Ensured by creation");
        return palette;
    };
    s.palette_lock = [](void *, void *p) -> void * { return static_cast<PDynamicArray>(p)->PvLock(0); };
    s.palette_unlock = [](void *, void *p) { static_cast<PDynamicArray>(p)->Unlock(); };
    s.publish = [](void *p, int kind, void *object) {
        auto c = static_cast<BitmapContext *>(p);
        if (kind == MM_BITMAP_PIXELS)
            *c->pixels = static_cast<byte *>(object);
        else
            *c->palette = static_cast<PDynamicArray>(object);
    };
    s.dimensions = [](void *p, int valid, MMI32 w, MMI32 h, int up) {
        auto c = static_cast<BitmapContext *>(p);
        if (valid)
        {
            if (c->width)
                *c->width = w;
            if (c->height)
                *c->height = h;
            if (c->upside)
                *c->upside = up != 0;
        }
        else
        {
            TrashVar(c->width);
            TrashVar(c->height);
            TrashVar(c->upside);
        }
    };
    s.warning = [](void *, int reason) {
        PZString messages[] = {PszLit("bad bitmap file"),
                               PszLit("not an 8-bit bitmap"),
                               PszLit("bad compression type or bitmap file is wrong length"),
                               PszLit("Empty bitmap rectangle"),
                               PszLit("Bitmap data wrong size"),
                               PszLit("palette is wrong size"),
                               PszLit("compressed bitmap is bad")};
        Warn(messages[reason]);
    };
    s.bad_bitmap = [](void *) { PushErc(ercMbmpCantOpenBitmap); };
    s.make_temporary = [](void *, void *p) { static_cast<PFileObject>(p)->SetTemp(); };
    return s;
}
} // namespace
bool FReadBitmap(Filename *filename, byte **pixels, PDynamicArray *palette, LegacyLong *width, LegacyLong *height,
                 bool *upside, byte transparent)
{
    AssertPo(filename, ffniFile);
    AssertNilOrVarMem(pixels);
    AssertNilOrVarMem(palette);
    AssertNilOrVarMem(width);
    AssertNilOrVarMem(height);
    AssertNilOrVarMem(upside);
#ifndef MAC
    BitmapContext context;
    context.pixels = pixels;
    context.palette = palette;
    context.width = width;
    context.height = height;
    context.upside = upside;
    auto s = BitmapServices(&context);
    return mm_bitmap_read(filename, pixels != nullptr, palette != nullptr, transparent, &s) != 0;
#else
    if (pixels)
        *pixels = nullptr;
    if (palette)
        *palette = nullptr;
    TrashVar(width);
    TrashVar(height);
    TrashVar(upside);
    RawRtn();
    return false;
#endif
}

/***************************************************************************
    Writes a given bitmap to a given file.

    Arguments:
        Filename *pfni         -- the name of the file to write
        byte *prgb        -- the bits in the bitmap
        PDynamicArray pglclr        -- the palette of the bitmap
        long dxp          -- the width of the bitmap
        long dyp          -- the height of the bitmap
        bool fUpsideDown  -- indicates if the rows should be inverted

    Returns: fTrue if it could write the file
***************************************************************************/
bool FWriteBitmap(Filename *filename, byte *pixels, PDynamicArray palette, LegacyLong width, LegacyLong height,
                  bool upside)
{
    AssertPo(filename, ffniFile);
    AssertVarMem(pixels);
    AssertPo(palette, 0);
    Assert(palette->IvMac() == 256, "Invalid color palette");
    Assert(width >= 0, "Invalid width");
    Assert(height >= 0, "Invalid height");
#ifndef MAC
    static_assert(sizeof(Color) == 4, "BMP palette layout");
    Assert(palette->CbEntry() == sizeof(Color), "Palette has different format from Windows");
    BitmapContext context;
    auto s = BitmapServices(&context);
    return mm_bitmap_write(filename, pixels, palette, width, height, upside, &s) != 0;
#else
    RawRtn();
    return false;
#endif
}
