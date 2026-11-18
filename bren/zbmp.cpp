/***************************************************************************

    zbmp.cpp: Z-buffer bitmap class

    Primary Author: ******
    Review Status: REVIEWED - any changes to this file must be reviewed!

***************************************************************************/
#include "bren_core.h"
#include <cstring>
ASSERTNAME
namespace BRender
{
RTCLASS(ZBMP)
namespace
{
struct DepthContext
{
    DataBlock block;
    PBaseCacheableObject *object = nullptr;
    LegacyLong *bytes = nullptr;
};
MMDepthRect Rect(const RC &r)
{
    return {r.xpLeft, r.ypTop, r.xpRight, r.ypBottom};
}
} // namespace
MMDepthServices ZBMP::_Services(void *context)
{
    MMDepthServices s = {};
    s.context = context;
    const unsigned short one = 1;
    s.host_big_endian = !*reinterpret_cast<const byte *>(&one);
    s.system = koskCur;
    s.object_bytes = sizeof(ZBMP);
    s.create = [](void *) -> void * { return NewObj ZBMP; };
    s.state = [](void *, void *p) -> MMDepthBitmap * { return &static_cast<PZBMP>(p)->_depth; };
    s.release = [](void *, void *p) { static_cast<PZBMP>(p)->Release(); };
    s.allocate = [](void *, MMI32 n, void **p) -> int { return FAllocPv(p, n, fmemNil, mprNormal); };
    s.free_memory = [](void *, void **p) { FreePpv(p); };
    s.warning = [](void *) { Warn("Couldn't load ZBMP"); };
    s.unpack = [](void *, void *p) -> int { return static_cast<PDataBlock>(p)->FUnpackData(); };
    s.size = [](void *, void *p, int packed) -> MMI32 { return static_cast<PDataBlock>(p)->Cb(packed != 0); };
    s.read = [](void *, void *p, void *data, MMI32 n, MMI32 at) -> int {
        return static_cast<PDataBlock>(p)->FReadRgb(data, n, at);
    };
    s.write = [](void *, void *p, const void *data, MMI32 n, MMI32 at) -> int {
        return static_cast<PDataBlock>(p)->FWriteRgb(const_cast<void *>(data), n, at);
    };
    s.add = [](void *p, void *file, MMU32 type, MMI32 bytes, MMU32 *number) -> void * {
        auto &block = static_cast<DepthContext *>(p)->block;
        ChunkNumber n;
        std::memcpy(&n, number, sizeof(n));
        bool ok = static_cast<PChunkyFile>(file)->FAdd(bytes, type, &n, &block);
        std::memcpy(number, &n, sizeof(n));
        return ok ? &block : nullptr;
    };
    s.publish_fetch = [](void *p, int valid, void *object, MMI32 bytes) {
        auto c = static_cast<DepthContext *>(p);
        if (valid)
        {
            if (c->object)
                *c->object = static_cast<PZBMP>(object);
            *c->bytes = bytes;
        }
        else
        {
            TrashVar(c->object);
            TrashVar(c->bytes);
        }
    };
    return s;
}
PZBMP ZBMP::PzbmpNew(LegacyLong w, LegacyLong h)
{
    AssertIn(w, 1, klwMax);
    AssertIn(h, 1, klwMax);
    auto s = _Services();
    auto object = static_cast<PZBMP>(mm_depth_new(w, h, nullptr, &s));
    AssertNilOrPo(object, 0);
    return object;
}
PZBMP ZBMP::PzbmpNewFromBpmp(BPMP *p)
{
    AssertVarMem(p);
    auto s = _Services();
    return static_cast<PZBMP>(mm_depth_new(p->width, p->height, p->pixels, &s));
}
bool ZBMP::FReadZbmp(PChunkyResourceFile cache, ChunkTagOrType type, ChunkNumber number, PDataBlock block,
                     PBaseCacheableObject *out, LegacyLong *bytes)
{
    AssertPo(cache, 0);
    AssertPo(block, 0);
    AssertNilOrVarMem(out);
    AssertVarMem(bytes);
    (void)type;
    (void)number;
    DepthContext context;
    context.object = out;
    context.bytes = bytes;
    auto s = _Services(&context);
    return mm_depth_fetch(block, out != nullptr, &s) != 0;
}
PZBMP ZBMP::PzbmpRead(PDataBlock block)
{
    AssertPo(block, 0);
    auto s = _Services();
    auto object = static_cast<PZBMP>(mm_depth_read(block, &s));
    AssertNilOrPo(object, 0);
    return object;
}
ZBMP::~ZBMP()
{
    AssertBaseThis(0);
    auto s = _Services();
    mm_depth_dispose(&_depth, &s);
}
void ZBMP::Draw(byte *pixels, LegacyLong row, LegacyLong height, LegacyLong x, LegacyLong y, RC *clip, PRegion region)
{
    AssertThis(0);
    AssertPvCb(pixels, LwMul(row, height));
    AssertNilOrVarMem(clip);
    AssertNilOrVarMem(region);
    MMDepthRect c;
    if (clip)
        c = Rect(*clip);
    mm_depth_draw(&_depth, pixels, row, height, x, y, clip ? &c : nullptr, region ? region->Geometry() : nullptr, 0);
}
void ZBMP::DrawHalf(byte *pixels, LegacyLong row, LegacyLong height, LegacyLong x, LegacyLong y, RC *clip,
                    PRegion region)
{
    AssertThis(0);
    AssertPvCb(pixels, LwMul(row, height / 2));
    AssertNilOrVarMem(clip);
    AssertNilOrVarMem(region);
    MMDepthRect c;
    if (clip)
        c = Rect(*clip);
    mm_depth_draw(&_depth, pixels, row, height, x, y, clip ? &c : nullptr, region ? region->Geometry() : nullptr, 1);
}
bool ZBMP::FWrite(PChunkyFile file, ChunkTagOrType type, ChunkNumber *number)
{
    AssertThis(0);
    AssertPo(file, 0);
    AssertVarMem(number);
    DepthContext context;
    auto s = _Services(&context);
    MMU32 n;
    std::memcpy(&n, number, sizeof(n));
    bool ok = mm_depth_write(&_depth, file, type, &n, &s) != 0;
    std::memcpy(number, &n, sizeof(n));
    return ok;
}
#ifdef DEBUG
void ZBMP::AssertValid(LegacyUlong grf)
{
    ZBMP_PAR::AssertValid(fobjAllocated);
    AssertPvCb(_depth.pixels, _depth.bytes);
    Assert(_depth.row_bytes == LwMul(_depth.bounds.right - _depth.bounds.left, kcbPixelZbmp), "bad _cbRow");
    Assert(_depth.bytes == LwMul(_depth.bounds.bottom - _depth.bounds.top, _depth.row_bytes), "bad _cb");
    (void)grf;
}
void ZBMP::MarkMem()
{
    AssertThis(0);
    ZBMP_PAR::MarkMem();
    MarkPv(_depth.pixels);
}
#endif
} // namespace BRender
