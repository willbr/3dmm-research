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
    RegionScanner scanner;
    PBaseCacheableObject *object = nullptr;
    LegacyLong *bytes = nullptr;
};
ThreeMMDepthRect Rect(const RC &r)
{
    return {r.xpLeft, r.ypTop, r.xpRight, r.ypBottom};
}
RC Rect(const ThreeMMDepthRect &r)
{
    return RC(r.left, r.top, r.right, r.bottom);
}
} // namespace
ThreeMMDepthServices ZBMP::_Services(void *context)
{
    ThreeMMDepthServices s = {};
    s.context = context;
    const unsigned short one = 1;
    s.host_big_endian = !*reinterpret_cast<const byte *>(&one);
    s.system = koskCur;
    s.object_bytes = sizeof(ZBMP);
    s.create = [](void *) -> void * { return NewObj ZBMP; };
    s.state = [](void *, void *p) -> ThreeMMDepthBitmap * { return &static_cast<PZBMP>(p)->_depth; };
    s.release = [](void *, void *p) { static_cast<PZBMP>(p)->Release(); };
    s.allocate = [](void *, ThreeMMI32 n, void **p) -> int { return FAllocPv(p, n, fmemNil, mprNormal); };
    s.free_memory = [](void *, void **p) { FreePpv(p); };
    s.warning = [](void *) { Warn("Couldn't load ZBMP"); };
    s.unpack = [](void *, void *p) -> int { return static_cast<PDataBlock>(p)->FUnpackData(); };
    s.size = [](void *, void *p, int packed) -> ThreeMMI32 { return static_cast<PDataBlock>(p)->Cb(packed != 0); };
    s.read = [](void *, void *p, void *data, ThreeMMI32 n, ThreeMMI32 at) -> int {
        return static_cast<PDataBlock>(p)->FReadRgb(data, n, at);
    };
    s.write = [](void *, void *p, const void *data, ThreeMMI32 n, ThreeMMI32 at) -> int {
        return static_cast<PDataBlock>(p)->FWriteRgb(const_cast<void *>(data), n, at);
    };
    s.add = [](void *p, void *file, ThreeMMU32 type, ThreeMMI32 bytes, ThreeMMU32 *number) -> void * {
        auto &block = static_cast<DepthContext *>(p)->block;
        ChunkNumber n;
        std::memcpy(&n, number, sizeof(n));
        bool ok = static_cast<PChunkyFile>(file)->FAdd(bytes, type, &n, &block);
        std::memcpy(number, &n, sizeof(n));
        return ok ? &block : nullptr;
    };
    s.publish_fetch = [](void *p, int valid, void *object, ThreeMMI32 bytes) {
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
    s.region_bounds = [](void *, void *p, ThreeMMDepthRect *out) -> int {
        RC r;
        bool empty = static_cast<PRegion>(p)->FEmpty(&r);
        *out = Rect(r);
        return !empty;
    };
    s.scan_init = [](void *p, void *region, const ThreeMMDepthRect *clip, const ThreeMMDepthRect *bounds) {
        auto &scanner = static_cast<DepthContext *>(p)->scanner;
        RC b = Rect(*bounds);
        if (region)
            scanner.Init(static_cast<PRegion>(region), &b);
        else
        {
            RC r = Rect(*clip);
            scanner.InitRc(&r, &b);
        }
    };
    s.scan_pair = [](void *p, ThreeMMI32 *left, ThreeMMI32 *right) -> int {
        auto &scanner = static_cast<DepthContext *>(p)->scanner;
        if (scanner.XpCur() == klwMax)
            return 0;
        *left = scanner.XpCur();
        *right = scanner.XpFetch();
        scanner.XpFetch();
        return 1;
    };
    s.scan_next = [](void *p, ThreeMMI32 rows) { static_cast<DepthContext *>(p)->scanner.ScanNext(rows); };
    return s;
}
PZBMP ZBMP::PzbmpNew(LegacyLong w, LegacyLong h)
{
    AssertIn(w, 1, klwMax);
    AssertIn(h, 1, klwMax);
    auto s = _Services();
    auto object = static_cast<PZBMP>(three_mm_depth_new(w, h, nullptr, &s));
    AssertNilOrPo(object, 0);
    return object;
}
PZBMP ZBMP::PzbmpNewFromBpmp(BPMP *p)
{
    AssertVarMem(p);
    auto s = _Services();
    return static_cast<PZBMP>(three_mm_depth_new(p->width, p->height, p->pixels, &s));
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
    return three_mm_depth_fetch(block, out != nullptr, &s) != 0;
}
PZBMP ZBMP::PzbmpRead(PDataBlock block)
{
    AssertPo(block, 0);
    auto s = _Services();
    auto object = static_cast<PZBMP>(three_mm_depth_read(block, &s));
    AssertNilOrPo(object, 0);
    return object;
}
ZBMP::~ZBMP()
{
    AssertBaseThis(0);
    auto s = _Services();
    three_mm_depth_dispose(&_depth, &s);
}
void ZBMP::Draw(byte *pixels, LegacyLong row, LegacyLong height, LegacyLong x, LegacyLong y, RC *clip, PRegion region)
{
    AssertThis(0);
    AssertPvCb(pixels, LwMul(row, height));
    AssertNilOrVarMem(clip);
    AssertNilOrVarMem(region);
    DepthContext context;
    auto s = _Services(&context);
    ThreeMMDepthRect c;
    if (clip)
        c = Rect(*clip);
    three_mm_depth_draw(&_depth, pixels, row, height, x, y, clip ? &c : nullptr, region, 0, &s);
}
void ZBMP::DrawHalf(byte *pixels, LegacyLong row, LegacyLong height, LegacyLong x, LegacyLong y, RC *clip,
                    PRegion region)
{
    AssertThis(0);
    AssertPvCb(pixels, LwMul(row, height / 2));
    AssertNilOrVarMem(clip);
    AssertNilOrVarMem(region);
    DepthContext context;
    auto s = _Services(&context);
    ThreeMMDepthRect c;
    if (clip)
        c = Rect(*clip);
    three_mm_depth_draw(&_depth, pixels, row, height, x, y, clip ? &c : nullptr, region, 1, &s);
}
bool ZBMP::FWrite(PChunkyFile file, ChunkTagOrType type, ChunkNumber *number)
{
    AssertThis(0);
    AssertPo(file, 0);
    AssertVarMem(number);
    DepthContext context;
    auto s = _Services(&context);
    ThreeMMU32 n;
    std::memcpy(&n, number, sizeof(n));
    bool ok = three_mm_depth_write(&_depth, file, type, &n, &s) != 0;
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
