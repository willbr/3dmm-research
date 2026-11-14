#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#if defined(_MSC_VER) && defined(DEBUG)
#include <crtdbg.h>
#endif
#include "bren_core.h"
#undef size
using namespace BRender;
ASSERTNAME
void WarnProc(schar *f, LegacyLong n, schar *s)
{
    std::fprintf(stderr, "%s:%ld: %s\n", f, (long)n, s);
}
bool FAssertProc(schar *f, LegacyLong n, schar *s, void *, LegacyLong)
{
    WarnProc(f, n, s);
    std::exit(1);
}
#define CHECK(c)                                                                                                       \
    do                                                                                                                 \
    {                                                                                                                  \
        if (!(c))                                                                                                      \
        {                                                                                                              \
            std::fprintf(stderr, "texture resource: %d\n", __LINE__);                                                  \
            std::exit(1);                                                                                              \
        }                                                                                                              \
    } while (0)
static FILE *snapshot;
static unsigned hash = 2166136261U, traceBytes;
static void Record(const void *p, size_t n)
{
    auto b = static_cast<const byte *>(p);
    for (size_t i = 0; i < n; ++i)
        hash = (hash ^ b[i]) * 16777619U;
    traceBytes += unsigned(n);
    if (snapshot)
        CHECK(std::fwrite(p, 1, n, snapshot) == n);
}
static void Value(LegacyLong n)
{
    Record(&n, 4);
}
static void State(PTextureMap t)
{
    Value(t != nullptr);
    if (!t)
        return;
    auto p = t->Pbpmp();
    LegacyLong fields[] = {p->row_bytes,
                           p->type,
                           p->flags,
                           p->base_x,
                           p->base_y,
                           p->width,
                           p->height,
                           p->origin_x,
                           p->origin_y,
                           p->map != nullptr,
                           p->identifier == reinterpret_cast<char *>(t),
                           t->CbOnFile()};
    Record(fields, sizeof(fields));
    auto f = ChunkyFile::PcflCreateTemp();
    CHECK(f);
    ChunkNumber n;
    CHECK(t->FWrite(f, kctgTmap, &n));
    DataBlock b;
    CHECK(f->FFind(kctgTmap, n, &b));
    std::vector<byte> data(b.Cb());
    CHECK(b.FReadRgb(data.data(), data.size(), 0));
    Value(data.size());
    Record(data.data(), data.size());
    b.Free();
    ReleasePpo(&f);
}
static void Inject(int kind, int n)
{
#ifdef DEBUG
    vmutxMem.Enter();
    auto &a = kind ? vdmglob.dmaglPv : vdmglob.dmaglBase;
    a.cactDo = n ? n - 1 : 0;
    a.cactFail = n ? 1 : 0;
    vmutxMem.Leave();
#else
    (void)kind;
    (void)n;
#endif
}
static void Loads()
{
    for (int variant = 0; variant < 6; ++variant)
        for (int factory = 0; factory < 2; ++factory)
            for (int kind = 0; kind < 2; ++kind)
                for (int failure = 0; failure < 8; ++failure)
                {
                    TextureMapFile h = {};
                    h.bo = kboCur;
                    h.osk = koskCur;
                    h.cbRow = 8;
                    h.dxp = 5;
                    h.dyp = 3;
                    h.type = BR_PMT_INDEX_8;
                    h.grftmap = BR_PMF_LINEAR;
                    h.xpLeft = 3;
                    h.ypTop = 7;
                    h.xpOrigin = -4;
                    h.ypOrigin = 11;
                    if (variant == 1)
                        SwapBytesBom(&h, kbomTmapf);
                    std::vector<byte> data(sizeof(h) + 24);
                    std::memcpy(data.data(), &h, sizeof(h));
                    for (int i = 0; i < 24; ++i)
                        data[20 + i] = byte(i * 7);
                    if (variant == 2)
                        data.insert(data.end(), 7, 0xa9);
                    if (variant == 3)
                        data.insert(data.end(), 160, 0);
                    if (variant == 4)
                        data[6] = BR_PMT_ALPHA_8;
                    if (variant == 5)
                        data[7] = 0x81;
                    auto f = ChunkyFile::PcflCreateTemp();
                    CHECK(f);
                    ChunkNumber n;
                    CHECK(f->FAddPv(data.data(), data.size(), kctgTmap, &n));
                    if (variant == 3)
                        CHECK(f->FPackData(kctgTmap, n));
                    auto cache = ChunkyResourceFile::PcrfNew(f, 0);
                    CHECK(cache);
                    DataBlock block;
                    CHECK(f->FFind(kctgTmap, n, &block));
                    LegacyLong estimate = -1;
                    CHECK(TextureMap::FReadTmap(cache, kctgTmap, n, &block, nullptr, &estimate));
                    Value(estimate);
                    PTextureMap t = nullptr;
                    Inject(kind, failure);
                    bool ok = factory ? TextureMap::FReadTmap(cache, kctgTmap, n, &block, (PBaseCacheableObject *)&t,
                                                              &estimate)
                                      : (t = TextureMap::PtmapRead(f, kctgTmap, n)) != nullptr;
                    Inject(kind, 0);
                    Value(ok);
                    if (factory && !ok)
                        t = nullptr;
                    if (factory && ok)
                        Value(estimate);
                    State(t);
                    ReleasePpo(&t);
                    block.Free();
                    ReleasePpo(&cache);
                    ReleasePpo(&f);
                }
}
static void Standalone(PTextureMap t, bool compress)
{
    Filename name;
    CHECK(name.FGetTemp());
    CHECK(t->FWriteTmapChkFile(&name, compress));
    auto file = FileObject::PfilOpen(&name);
    CHECK(file);
    std::vector<byte> bytes(file->FpMac());
    CHECK(file->FReadRgb(bytes.data(), bytes.size(), 0));
    Value(bytes.size());
    Record(bytes.data(), bytes.size());
    ReleasePpo(&file);
    CHECK(name.FDelete());
}
static void BitmapImports()
{
    auto palette = DynamicArray::PglNew(sizeof(Color));
    CHECK(palette);
    for (int i = 0; i < 256; ++i)
    {
        Color c = {};
        c.bRed = byte(i);
        c.bGreen = byte(i * 3);
        c.bBlue = byte(i * 5);
        CHECK(palette->FAdd(&c));
    }
    const int widths[] = {3, 4, 5, 8};
    for (int width : widths)
    {
        int height = 3;
        std::vector<byte> pixels(((width + 3) & ~3) * height);
        for (size_t i = 0; i < pixels.size(); ++i)
            pixels[i] = byte(i * 19 + 7);
        Filename name;
        CHECK(name.FGetTemp());
        CHECK(FWriteBitmap(&name, pixels.data(), palette, width, height, false));
        for (int count : {-1, 0, 1, 2, 7, 256})
        {
            auto target = DynamicArray::PglNew(sizeof(Color));
            CHECK(target);
            for (int i = 0; i < count; ++i)
            {
                Color c = {};
                c.bRed = byte((i / 2) * 17);
                c.bGreen = byte(i * 5);
                c.bBlue = byte(i * 11);
                if (i == 1)
                    c = {};
                CHECK(target->FAdd(&c));
            }
            auto t = TextureMap::PtmapReadNative(&name, count == -1 ? nullptr : target);
            CHECK(t);
            State(t);
            ReleasePpo(&t);
            ReleasePpo(&target);
        }
        CHECK(name.FDelete());
    }
    ReleasePpo(&palette);
}
static void Constructors()
{
    for (int width : {1, 3, 4, 8, 16})
    {
        byte *pixels = nullptr;
        CHECK(FAllocPv((void **)&pixels, width * 3, fmemClear, mprNormal));
        for (int i = 0; i < width * 3; ++i)
            pixels[i] = byte(i * 11);
        auto t = TextureMap::PtmapNew(pixels, width, 3);
        CHECK(t);
        State(t);
        Standalone(t, false);
        Standalone(t, true);
        ReleasePpo(&t);
    }
    auto raw = BrPixelmapAllocate(BR_PMT_INDEX_8, 5, 3, nullptr, 0);
    CHECK(raw);
    std::memset(raw->pixels, 7, raw->row_bytes * raw->height);
    raw->origin_x = -7;
    raw->base_y = 3;
    auto t = TextureMap::PtmapNewFromBpmp(raw);
    CHECK(t);
    State(t);
    ReleasePpo(&t);
#ifndef THREE_MM_ENGINE_TEXTURE_RESOURCE_H
    BrPixelmapFree(raw); // Baseline repair only: the original wrapper leaked this SDK owner.
#endif
}
static void PackedRoundTrip()
{
    byte *pixels = nullptr;
    CHECK(FAllocPv(reinterpret_cast<void **>(&pixels), 256, fmemClear, mprNormal));
    auto t = TextureMap::PtmapNew(pixels, 16, 16);
    CHECK(t);
    t->Pbpmp()->origin_x = -17; // The legacy renderer view remains writable.
    Filename name;
    CHECK(name.FGetTemp() && t->FWriteTmapChkFile(&name, true));
    auto file = FileObject::PfilOpen(&name);
    CHECK(file);
    LegacyLong signature;
    CHECK(file->FReadRgb(&signature, 4, 0) && signature == klwSigPackedFile);
    DataBlock block(file, 4, file->FpMac() - 4, true);
    CHECK(block.FUnpackData() && block.Cb() == 276);
    std::vector<byte> bytes(276);
    CHECK(block.FReadRgb(bytes.data(), bytes.size(), 0));
    TextureMapFile expected = {};
    expected.bo = kboCur;
    expected.osk = koskCur;
    expected.cbRow = 16;
    expected.dxp = expected.dyp = 16;
    expected.type = BR_PMT_INDEX_8;
    expected.grftmap = BR_PMF_LINEAR;
    expected.xpOrigin = -17;
    CHECK(!std::memcmp(bytes.data(), &expected, 20));
    for (size_t i = 20; i < bytes.size(); ++i)
        CHECK(bytes[i] == 0);
    block.Free();
    ReleasePpo(&file);
    ReleasePpo(&t);
    CHECK(name.FDelete());
}
#ifdef THREE_MM_ENGINE_TEXTURE_RESOURCE_H
static br_allocator *underlying;
static std::vector<void *> sdkBlocks;
static void *BR_CALLBACK TrackAllocate(br_size_t bytes, br_uint_8 type)
{
    auto p = underlying->allocate(bytes, type);
    if (p)
        sdkBlocks.push_back(p);
    return p;
}
static void BR_CALLBACK TrackFree(void *p)
{
    for (auto i = sdkBlocks.begin(); i != sdkBlocks.end(); ++i)
        if (*i == p)
        {
            sdkBlocks.erase(i);
            break;
        }
    underlying->free(p);
}
static br_size_t BR_CALLBACK TrackInquire(br_uint_8 type)
{
    return underlying->inquire(type);
}
static void ImportedLifetime()
{
    br_allocator tracker = {"Imported texture lifetime", TrackAllocate, TrackFree, TrackInquire};
    underlying = BrAllocatorSet(&tracker);
    auto raw = BrPixelmapAllocate(BR_PMT_INDEX_8, 8, 3, nullptr, 0);
    CHECK(raw && !sdkBlocks.empty());
    auto t = TextureMap::PtmapNewFromBpmp(raw);
    CHECK(t);
    ReleasePpo(&t);
    CHECK(sdkBlocks.empty());
    raw = BrPixelmapAllocate(BR_PMT_INDEX_8, 8, 3, nullptr, 0);
    CHECK(raw);
#ifdef DEBUG
    Inject(0, 1);
    t = TextureMap::PtmapNewFromBpmp(raw);
    Inject(0, 0);
    CHECK(!t && !sdkBlocks.empty());
#endif
    BrPixelmapFree(raw);
    CHECK(sdkBlocks.empty());
    BrAllocatorSet(underlying);
}
#endif
#if defined(_MSC_VER) && defined(DEBUG)
static int Report(int type, char *message, int *)
{
    std::fprintf(stderr, "CRT: %s\n", message);
    if (type != _CRT_WARN)
        std::exit(1);
    return 0;
}
#endif
int main()
{
#if defined(_MSC_VER) && defined(DEBUG)
    _CrtSetReportHook(Report);
#endif
    if (auto name = std::getenv("THREE_MM_TEXTURE_RESOURCE_SNAPSHOT"))
    {
        snapshot = std::fopen(name, "wb");
        CHECK(snapshot);
    }
    BrBegin();
    Loads();
    Constructors();
#ifdef THREE_MM_ENGINE_TEXTURE_RESOURCE_H
    ImportedLifetime();
#endif
    BitmapImports();
    PackedRoundTrip();
    BrEnd();
    if (snapshot)
        CHECK(!std::fclose(snapshot));
    std::printf("Texture resources: bytes=%u fingerprint=%08X\n", traceBytes, hash);
#ifdef DEBUG
    unsigned short one = 1;
    if (*reinterpret_cast<const byte *>(&one))
        CHECK(traceBytes == 22000 && hash == 0xC5DC711AU);
#endif
}
