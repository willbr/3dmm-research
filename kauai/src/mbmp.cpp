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
#include "three_mm/bitmap_file.h"
ASSERTNAME

RTCLASS(MaskedBitmapMBMP)

namespace {
struct MaskedContext
{
    PBaseCacheableObject *object = nullptr;
    LegacyLong *bytes = nullptr;
};
ThreeMMMaskedRect MaskedRect(const RC &r)
{
    return {r.xpLeft, r.ypTop, r.xpRight, r.ypBottom};
}
} // namespace
ThreeMMMaskedServices MaskedBitmapMBMP::_Services(void *context)
{
    static_assert(sizeof(MaskedBitmapOnFile) == 28, "masked bitmap header layout");
    ThreeMMMaskedServices s = {};
    s.context = context;
    const unsigned short one = 1;
    s.host_big_endian = !*reinterpret_cast<const byte *>(&one);
    s.system = koskCur;
    s.create = [](void *) -> void * { return NewObj MaskedBitmapMBMP; };
    s.state = [](void *, void *p) -> ThreeMMMaskedBitmap * { return &static_cast<PMaskedBitmapMBMP>(p)->_bitmap; };
    s.release = [](void *, void *p) { static_cast<PMaskedBitmapMBMP>(p)->Release(); };
    s.allocate = [](void *, ThreeMMI32 n, void **out) -> int { return FAllocPv(out, n, fmemNil, mprNormal); };
    s.free_memory = [](void *, void **p) { FreePpv(p); };
    s.diagnostic = [](void *, int code) {
        if (code == THREE_MM_MASKED_EMPTY)
        {
            Warn("Empty source bitmap for MaskedBitmapMBMP");
        }
        else
        {
            Bug("Wrong sized block");
        }
    };
    s.unpack = [](void *, void *p) -> int { return static_cast<PDataBlock>(p)->FUnpackData(); };
    s.size = [](void *, void *p, int packed) -> ThreeMMI32 { return static_cast<PDataBlock>(p)->Cb(packed != 0); };
    s.read = [](void *, void *p, void *out, ThreeMMI32 n) -> int {
        return static_cast<PDataBlock>(p)->FReadRgb(out, n, 0);
    };
    s.write = [](void *, void *p, const void *data, ThreeMMI32 n) -> int {
        return static_cast<PDataBlock>(p)->FWriteRgb(const_cast<void *>(data), n, 0);
    };
    s.is_memory = [](void *, void *p) -> int { return static_cast<PDataBlock>(p)->CbMem() != 0; };
    s.consume = [](void *, void *p) { static_cast<PDataBlock>(p)->Free(); };
    s.publish_fetch = [](void *p, int valid, void *object, ThreeMMI32 bytes) {
        auto c = static_cast<MaskedContext *>(p);
        if (valid)
        {
            if (c->object)
                *c->object = static_cast<PMaskedBitmapMBMP>(object);
            *c->bytes = bytes;
        }
        else
        {
            TrashVar(c->object);
            TrashVar(c->bytes);
        }
    };
    s.bitmap_read = [](void *, void *filename, unsigned char transparent, ThreeMMMaskedSource *out) -> int {
        byte *pixels = nullptr;
        LegacyLong w, h;
        bool up;
        if (!FReadBitmap(static_cast<PFilename>(filename), &pixels, nullptr, &w, &h, &up, transparent))
            return 0;
        *out = {pixels, w, h, up};
        return 1;
    };
    return s;
}
MaskedBitmapMBMP::~MaskedBitmapMBMP()
{
    AssertBaseThis(0);
    auto s = _Services();
    three_mm_masked_dispose(&_bitmap, &s);
}
PMaskedBitmapMBMP MaskedBitmapMBMP::PmbmpNew(byte *pixels, LegacyLong row, LegacyLong height, RC *extract, LegacyLong x,
                                             LegacyLong y, byte transparent, LegacyUlong flags, byte fill)
{
    AssertIn(row, 1, kcbMax);
    AssertIn(height, 1, kcbMax);
    AssertPvCb(pixels, LwMul(row, height));
    AssertVarMem(extract);
    auto r = MaskedRect(*extract);
    auto s = _Services();
    return static_cast<PMaskedBitmapMBMP>(
        three_mm_masked_new(pixels, row, height, &r, x, y, transparent, flags, fill, &s));
}
bool MaskedBitmapMBMP::_FInit(byte *pixels, LegacyLong row, LegacyLong height, RC *extract, LegacyLong x, LegacyLong y,
                              byte transparent, LegacyUlong flags, byte fill)
{
    AssertVarMem(extract);
    auto r = MaskedRect(*extract);
    auto s = _Services();
    return three_mm_masked_init(&_bitmap, pixels, row, height, &r, x, y, transparent, flags, fill, &s) != 0;
}
PMaskedBitmapMBMP MaskedBitmapMBMP::PmbmpReadNative(Filename *filename, byte transparent, LegacyLong x, LegacyLong y,
                                                    LegacyUlong flags, byte fill)
{
    AssertPo(filename, ffniFile);
    auto s = _Services();
    return static_cast<PMaskedBitmapMBMP>(three_mm_masked_import(filename, transparent, x, y, flags, fill, &s));
}
PMaskedBitmapMBMP MaskedBitmapMBMP::PmbmpRead(PDataBlock block)
{
    AssertPo(block, 0);
    auto s = _Services();
    return static_cast<PMaskedBitmapMBMP>(three_mm_masked_read(block, &s));
}
LegacyLong MaskedBitmapMBMP::CbOnFile()
{
    AssertThis(0);
    return _bitmap.bytes;
}
bool MaskedBitmapMBMP::FWrite(PDataBlock block)
{
    AssertThis(0);
    AssertPo(block, 0);
    auto s = _Services();
    return three_mm_masked_write(&_bitmap, block, &s) != 0;
}
void MaskedBitmapMBMP::GetRc(RC *out)
{
    AssertThis(0);
    AssertVarMem(out);
    auto s = _Services();
    ThreeMMMaskedRect r;
    three_mm_masked_bounds(&_bitmap, s.host_big_endian, &r);
    out->Set(r.left, r.top, r.right, r.bottom);
}
bool MaskedBitmapMBMP::FPtIn(LegacyLong x, LegacyLong y)
{
    AssertThis(0);
    auto s = _Services();
    return three_mm_masked_hit(&_bitmap, s.host_big_endian, x, y) != 0;
}
#ifdef DEBUG
void MaskedBitmapMBMP::AssertValid(LegacyUlong grf)
{
    MaskedBitmapMBMP_PAR::AssertValid(0);
    AssertPvCb(_bitmap.data, _bitmap.bytes);
    RC r = _Qmbmph()->rc;
    Assert(_bitmap.row_bytes == LwMul(r.Dyp(), sizeof(short)), "wrong row table size");
    LegacyLong total = 28 + _bitmap.row_bytes;
    auto rows = _Qrgcb();
    for (LegacyLong i = 0; i < r.Dyp(); ++i)
    {
        AssertIn(rows[i], 0, kcbMax);
        total += rows[i];
    }
    Assert(total == _bitmap.bytes, "wrong bitmap size");
    (void)grf;
}
void MaskedBitmapMBMP::MarkMem()
{
    AssertThis(0);
    MaskedBitmapMBMP_PAR::MarkMem();
    MarkPv(_bitmap.data);
}
#endif
bool MaskedBitmapMBMP::FReadMbmp(PChunkyResourceFile cache, ChunkTagOrType type, ChunkNumber number, PDataBlock block,
                                 PBaseCacheableObject *out, LegacyLong *bytes)
{
    AssertPo(cache, 0);
    AssertPo(block, fblckReadable);
    AssertNilOrVarMem(out);
    AssertVarMem(bytes);
    (void)type;
    (void)number;
    MaskedContext context;
    context.object = out;
    context.bytes = bytes;
    auto s = _Services(&context);
    return three_mm_masked_fetch(block, out != nullptr, &s) != 0;
}
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
ThreeMMBitmapServices BitmapServices(BitmapContext *context)
{
    ThreeMMBitmapServices s = {};
    s.context = context;
    s.open = [](void *, void *p) -> void * { return FileObject::PfilOpen(static_cast<PFilename>(p)); };
    s.create = [](void *, void *p) -> void * { return FileObject::PfilCreate(static_cast<PFilename>(p)); };
    s.release = [](void *, void *p) { static_cast<BASE *>(p)->Release(); };
    s.size = [](void *, void *p) -> ThreeMMI32 { return static_cast<PFileObject>(p)->FpMac(); };
    s.read = [](void *, void *p, void *out, ThreeMMI32 n, ThreeMMI32 at) -> int {
        return static_cast<PFileObject>(p)->FReadRgb(out, n, at);
    };
    s.write = [](void *, void *p, const void *data, ThreeMMI32 n, ThreeMMI32 at) -> int {
        return static_cast<PFileObject>(p)->FWriteRgb(const_cast<void *>(data), n, at);
    };
    s.allocate = [](void *, ThreeMMI32 n, void **out) -> int { return FAllocPv(out, n, fmemNil, mprNormal); };
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
        if (kind == THREE_MM_BITMAP_PIXELS)
            *c->pixels = static_cast<byte *>(object);
        else
            *c->palette = static_cast<PDynamicArray>(object);
    };
    s.dimensions = [](void *p, int valid, ThreeMMI32 w, ThreeMMI32 h, int up) {
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
    return three_mm_bitmap_read(filename, pixels != nullptr, palette != nullptr, transparent, &s) != 0;
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
    return three_mm_bitmap_write(filename, pixels, palette, width, height, upside, &s) != 0;
#else
    RawRtn();
    return false;
#endif
}
