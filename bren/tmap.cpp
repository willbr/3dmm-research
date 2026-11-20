/***************************************************************************
    Texture map (br_pixelmap wrapper) host services.
***************************************************************************/
#include "bren_core.h"
#include "texture_renderer.h"
#include <cstring>
ASSERTNAME
namespace BRender
{
RTCLASS(TextureMap)
static_assert(sizeof(TextureMapFile) == 20, "texture header layout");
namespace
{
struct TextureContext
{
    DataBlock block;
};
} // namespace
void TextureMap::_Sync()
{
    mm_brender_texture_sync(&_texture, &_bpmp);
}
MMTextureServices TextureMap::_Services(void *context)
{
    MMTextureServices s = {};
    s.context = context;
    const unsigned short one = 1;
    s.host_big_endian = !*reinterpret_cast<const byte *>(&one);
    s.system = koskCur;
    s.packed_signature = klwSigPackedFile;
    s.unpacked_signature = klwSigUnpackedFile;
    mm_brender_texture_services(&s);
    s.create = [](void *) -> void * { return NewObj TextureMap; };
    s.release = [](void *, void *p) { static_cast<BASE *>(p)->Release(); };
    s.state = [](void *, void *p) -> MMTextureResource * { return &static_cast<PTextureMap>(p)->_texture; };
    s.publish = [](void *, void *object, const MMTextureResource *resource) {
        auto texture = static_cast<PTextureMap>(object);
        mm_brender_texture_publish(&texture->_bpmp, texture, resource);
    };
    s.allocate = [](void *, MMI32 n, int clear, void **out) -> int {
        return FAllocPv(out, n, clear ? fmemClear : fmemNil, mprNormal);
    };
    s.free_memory = [](void *, void **p) { FreePpv(p); };
    s.invalidate = [](void *, void *p, size_t n) { TrashPvCb(p, static_cast<LegacyLong>(n)); };
    s.diagnostic = [](void *, int code) {
        if (code == MM_TEXTURE_ORDER)
            Assert(false, "bad TextureMapFile");
        else
            Assert(false, "upside down bitmap");
    };
    s.find = [](void *p, void *file, MMU32 type, MMU32 number) -> void * {
        auto &b = static_cast<TextureContext *>(p)->block;
        return static_cast<PChunkyFile>(file)->FFind(type, number, &b) ? &b : nullptr;
    };
    s.unpack = [](void *, void *p) -> int { return static_cast<PDataBlock>(p)->FUnpackData(); };
    s.size = [](void *, void *p, int packed) -> MMI32 { return static_cast<PDataBlock>(p)->Cb(packed != 0); };
    s.read = [](void *, void *p, void *out, MMI32 n, MMI32 at) -> int {
        return static_cast<PDataBlock>(p)->FReadRgb(out, n, at);
    };
    s.write = [](void *, void *p, const void *data, MMI32 n, MMI32 at) -> int {
        return static_cast<PDataBlock>(p)->FWriteRgb(const_cast<void *>(data), n, at);
    };
    s.close_block = [](void *p) { static_cast<TextureContext *>(p)->block.Free(); };
    s.add = [](void *p, void *file, MMU32 type, MMI32 bytes, MMU32 *number) -> void * {
        auto &b = static_cast<TextureContext *>(p)->block;
        ChunkNumber n;
        std::memcpy(&n, number, sizeof(n));
        bool ok = static_cast<PChunkyFile>(file)->FAdd(bytes, type, &n, &b);
        std::memcpy(number, &n, sizeof(n));
        return ok ? &b : nullptr;
    };
#ifndef MAC
    s.read_bitmap = [](void *, void *filename, MMTextureBitmap *out) -> int {
        byte *pixels = nullptr;
        PDynamicArray palette = nullptr;
        LegacyLong w, h;
        bool upside;
        if (!FReadBitmap(static_cast<PFilename>(filename), &pixels, &palette, &w, &h, &upside))
            return 0;
        *out = {pixels, palette, w, h, upside};
        return 1;
    };
#else
    s.read_bitmap = [](void *, void *, MMTextureBitmap *) -> int {
        RawRtn();
        return 0;
    };
#endif
    s.palette_count = [](void *, void *p) -> MMI32 { return static_cast<PDynamicArray>(p)->IvMac(); };
    s.palette_colour = [](void *, void *p, MMI32 i, MMTextureColour *out) {
        Color c;
        static_cast<PDynamicArray>(p)->Get(i, &c);
        *out = {c.bRed, c.bGreen, c.bBlue};
    };
    s.create_file = [](void *, void *p) -> void * { return FileObject::PfilCreate(static_cast<PFilename>(p)); };
    s.temp_block = [](void *p, MMI32 n) -> void * {
        auto &b = static_cast<TextureContext *>(p)->block;
        return b.FSetTemp(n) ? &b : nullptr;
    };
    s.pack = [](void *, void *p) -> int { return static_cast<PDataBlock>(p)->FPackData(); };
    s.write_file = [](void *, void *p, const void *data, MMI32 n, MMI32 at) -> int {
        return static_cast<PFileObject>(p)->FWriteRgb(const_cast<void *>(data), n, at);
    };
    s.copy_to_file = [](void *, void *b, void *f, MMI32 at, MMI32 n) -> int {
        FileLocation loc = {static_cast<PFileObject>(f), at, n};
        return static_cast<PDataBlock>(b)->FWriteToFlo(&loc, true);
    };
    s.make_temporary = [](void *, void *p) { static_cast<PFileObject>(p)->SetTemp(); };
    s.report = [](void *, void *p, int error) {
        PZString message = error == MM_TEXTURE_FILE_CREATE   ? PszLit("Couldn't create destination file\n")
                           : error == MM_TEXTURE_FILE_MEMORY ? PszLit("allocation failure\n")
                                                                   : PszLit("writing to destination file failed\n");
        static_cast<PMSNK>(p)->ReportLine(message);
    };
    return s;
}
bool TextureMap::FReadTmap(PChunkyResourceFile cache, ChunkTagOrType, ChunkNumber, PDataBlock block,
                           PBaseCacheableObject *out, br_int_32 *bytes)
{
    AssertPo(cache, 0);
    AssertPo(block, 0);
    AssertNilOrVarMem(out);
    AssertVarMem(bytes);
    auto s = _Services();
    return mm_texture_fetch(block, out, bytes, &s) != 0;
}
PTextureMap TextureMap::PtmapRead(PChunkyFile file, ChunkTagOrType type, ChunkNumber number)
{
    TextureContext context;
    auto s = _Services(&context);
    auto object = static_cast<PTextureMap>(mm_texture_read(file, type, number, &s));
    AssertNilOrPo(object, 0);
    return object;
}
PTextureMap TextureMap::PtmapRead(const MMReadStream &stream)
{
    auto s = _Services();
    auto object = static_cast<PTextureMap>(mm_texture_read_stream(&stream, &s));
    AssertNilOrPo(object, 0);
    return object;
}
PTextureMap TextureMap::PtmapNewFromBpmp(BPMP *bpmp)
{
    auto s = _Services();
    return static_cast<PTextureMap>(mm_texture_wrap(bpmp, &s));
}
TextureMap::~TextureMap()
{
    _Sync();
    auto s = _Services();
    mm_texture_dispose(&_texture, &s);
    _bpmp.pixels = nullptr;
}
bool TextureMap::FWrite(PChunkyFile file, ChunkTagOrType type, ChunkNumber *number)
{
    AssertThis(0);
    TextureContext context;
    auto s = _Services(&context);
    _Sync();
    MMU32 n;
    std::memcpy(&n, number, sizeof(n));
    bool ok = mm_texture_write_chunk(&_texture, file, type, &n, &s) != 0;
    std::memcpy(number, &n, sizeof(n));
    return ok;
}
bool TextureMap::FWrite(PDataBlock block)
{
    _Sync();
    auto s = _Services();
    return mm_texture_write(&_texture, block, &s) != 0;
}
#ifndef MAC
PTextureMap TextureMap::PtmapReadNative(Filename *filename, PDynamicArray palette)
{
    AssertPo(filename, 0);
    auto s = _Services();
    return static_cast<PTextureMap>(mm_texture_bitmap(filename, palette, &s));
}
#else
PTextureMap TextureMap::PtmapReadNative(Filename *filename)
{
    auto s = _Services();
    return static_cast<PTextureMap>(mm_texture_bitmap(filename, nullptr, &s));
}
#endif
PTextureMap TextureMap::PtmapNew(byte *pixels, LegacyLong width, LegacyLong height)
{
    Assert(width <= ksuMax, "bitmap too wide");
    Assert(height <= ksuMax, "bitmap too high");
    auto s = _Services();
    auto object = static_cast<PTextureMap>(mm_texture_new(pixels, width, height, &s));
    AssertNilOrPo(object, 0);
    return object;
}
br_int_32 TextureMap::CbOnFile()
{
    _Sync();
    return mm_texture_size(&_texture);
}
bool TextureMap::FWriteTmapChkFile(PFilename filename, bool compress, PMSNK sink)
{
    AssertThis(0);
    AssertPo(filename, ffniFile);
    AssertNilOrPo(sink, 0);
    TextureContext context;
    auto s = _Services(&context);
    _Sync();
    return mm_texture_standalone(&_texture, filename, compress, sink, &s) != 0;
}
#ifdef NOT_YET_REVIEWED
byte *TextureMap::PrgbBuildInverseTable(void)
{
    byte *prgb, *prgbT, iclr;
    LegacyLong cbRgb;

    if (_pbpmp->type != BR_PMT_RGB_888)
        return pvNil;

    if (!FAllocPv((void **)&prgb, cbRgb = _pbpmp->height, fmemNil, mprNormal))
        return pvNil;

    for (prgbT = prgb, iclr = 0; iclr < cbRgb; prgbT++, iclr++)
        *prgbT = iclr;

    _SortInverseTable(prgb, cbRgb, BR_COLOUR_RGB(0, 0, 0), BR_COLOUR_RGB(0xFF, 0xFF, 0xFF));
    return prgb;
}

void TextureMap::_SortInverseTable(byte *prgb, LegacyLong cbRgb, BRCLR brclrLo, BRCLR brclrHi)
{
    LegacyLong cbRgb1 = 0, cbRgb2 = 0;
    byte *prgb2, *prgbRead, bT;
    BRCLR brclrPivot = brclrLo + (brclrHi - brclrLo) / 2;
    BRCLR *pbrclr;

    prgb2 = prgb + cbRgb;
    prgbRead = prgb;
    while (cbRgb--)
    {
        pbrclr = 0; // pbrclr from index *prgb;
        if (*pbrclr <= brclrPivot)
        {
            prgbRead++;
            cbRgb1++;
        }
        else
        {
            bT = *prgb2;
            *--prgb2 = *prgbRead;
            *prgbRead = bT;
            cbRgb2++;
        }
    }
    if (cbRgb1 > 1)
        _SortInverseTable(prgb, cbRgb1, brclrLo, brclrPivot);
    if (cbRgb2 > 1)
        _SortInverseTable(prgb2, cbRgb2, brclrPivot + 1, brclrHi);
}
#endif // NOT_YET_REVIEWED

#ifdef DEBUG
void TextureMap::AssertValid(LegacyUlong)
{
    TextureMap_PAR::AssertValid(fobjAllocated);
    if (!_texture.imported_owner)
        AssertPvCb(_bpmp.pixels, LwMul(_bpmp.row_bytes, _bpmp.height));
}
void TextureMap::MarkMem()
{
    AssertThis(0);
    TextureMap_PAR::MarkMem();
    if (!_texture.imported_owner)
        MarkPv(_bpmp.pixels);
}
#endif
} // namespace BRender
