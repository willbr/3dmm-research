#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
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
            std::fprintf(stderr, "depth bitmap: %d\n", __LINE__);                                                      \
            std::exit(1);                                                                                              \
        }                                                                                                              \
    } while (0)
static FILE *snapshot;
static unsigned hash = 2166136261U, recorded;
static void Record(const void *p, size_t n)
{
    auto b = static_cast<const byte *>(p);
    for (size_t i = 0; i < n; ++i)
        hash = (hash ^ b[i]) * 16777619U;
    recorded += unsigned(n);
    if (snapshot)
        CHECK(std::fwrite(p, 1, n, snapshot) == n);
}
static void Value(LegacyLong n)
{
    Record(&n, 4);
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
static void State(MMDepthOwner *z)
{
    Value(z != nullptr);
    if (!z)
        return;
    Value(mm_depth_owner_bitmap(z)->row_bytes);
    auto f = ChunkyFile::PcflCreateTemp();
    CHECK(f);
    ChunkNumber n = 0;
    CHECK(WriteDepthBitmap(z, f->WriteOwnerIndex(), kctgZbmp, &n));
    DataBlock b;
    CHECK(f->FFind(kctgZbmp, n, &b));
    std::vector<byte> bytes(b.Cb());
    CHECK(b.FReadRgb(bytes.data(), bytes.size(), 0));
    Value(bytes.size());
    Record(bytes.data(), bytes.size());
    b.Free();
    ReleasePpo(&f);
}
static void Loads()
{
    for (int variant = 0; variant < 8; ++variant)
        for (int factory = 0; factory < 2; ++factory)
            for (int kind = 0; kind < 2; ++kind)
                for (int failure = 0; failure < 8; ++failure)
                {
                    struct DepthHeader
                    {
                        short bo, osk, xpLeft, ypTop, dxp, dyp;
                    };
                    DepthHeader h = {kboCur, koskCur, 0, 0, 5, 3};
                    if (variant == 1)
                        h.bo = kboOther;
                    if (variant == 2)
                        h.bo = 0x1234;
                    if (variant == 3)
                    {
                        h.xpLeft = 2;
                        h.ypTop = -1;
                    }
                    if (variant == 7)
                        h.dxp = 0;
                    if (variant == 6)
                        h.dyp = 35;
                    std::vector<byte> bytes(sizeof(h) + h.dxp * h.dyp * 2);
                    std::memcpy(bytes.data(), &h, sizeof(h));
                    for (size_t i = 12; i < bytes.size(); ++i)
                        bytes[i] = variant == 6 ? 0 : byte(i * 13);
                    if (variant == 4)
                        bytes.push_back(0xaf);
                    if (variant == 5)
                        bytes.resize(bytes.size() - 3);
                    auto file = ChunkyFile::PcflCreateTemp();
                    CHECK(file);
                    ChunkNumber n = 0;
                    CHECK(file->FAddPv(bytes.data(), bytes.size(), kctgZbmp, &n));
                    if (variant == 6)
                        CHECK(file->FPackData(kctgZbmp, n));
                    auto cache = ChunkyResourceFile::PcrfNew(file, 0);
                    CHECK(cache);
                    DataBlock block;
                    CHECK(file->FFind(kctgZbmp, n, &block));
                    MMI32 estimate = -1;
                    CHECK(ReadCachedDepthBitmap(cache->CoreCache(), kctgZbmp, n, block.CoreBlock(), nullptr, &estimate));
                    Value(estimate);
                    void *object = nullptr;
                    MMDepthOwner *z = nullptr;
                    Inject(kind, failure);
                    bool ok = factory ? ReadCachedDepthBitmap(cache->CoreCache(), kctgZbmp, n, block.CoreBlock(), &object, &estimate)
                                      : (z = ReadDepthBitmap(&block)) != nullptr;
                    Inject(kind, 0);
                    Value(ok);
                    if (factory && ok)
                    {
                        z = static_cast<MMDepthOwner *>(object);
                        Value(estimate);
                    }
                    State(z);
                    mm_depth_owner_release(z);
                    block.Free();
                    ReleasePpo(&cache);
                    ReleasePpo(&file);
                    vpers->Clear();
                }
}
static void Constructors()
{
    for (int w : {1, 3, 5, 8})
        for (int h : {1, 2, 5})
            for (int copied = 0; copied < 2; ++copied)
                for (int kind = 0; kind < 2; ++kind)
                    for (int failure = 0; failure < 6; ++failure)
                    {
                        std::vector<byte> data(w * h * 2);
                        for (size_t i = 0; i < data.size(); ++i)
                            data[i] = byte(i * 7);
                        Inject(kind, failure);
                        auto z = NewDepthBitmap(w, h, copied ? data.data() : nullptr);
                        Inject(kind, 0);
                        State(z);
                        mm_depth_owner_release(z);
                        vpers->Clear();
                    }
}
static void Draws()
{
    auto z = NewDepthBitmap(8, 8);
    CHECK(z);
    const auto bitmap = mm_depth_owner_bitmap(z);
    for (int i = 0; i < 128; ++i)
        static_cast<byte *>(bitmap->pixels)[i] = byte(i * 11);
    RC a(1, 0, 3, 3), b(4, 2, 8, 7), full(0, 0, 8, 8), partial(2, 1, 7, 6), empty(0, 0, 0, 0);
    auto region = Region::PregnNew(&a);
    CHECK(region && region->FUnionRc(&b));
    auto none = Region::PregnNew();
    CHECK(none);
    for (int half = 0; half < 2; ++half)
        for (int x : {-2, 0, 1})
            for (int y : {-1, 0, 1})
                for (auto clip : {static_cast<RC *>(nullptr), &full, &partial, &empty})
                    for (auto r : {static_cast<PRegion>(nullptr), region, none})
                    {
                        byte data[160];
                        std::memset(data, 0xcc, sizeof(data));
                        MMDepthRect rect;
                        if (clip)
                            rect = {clip->xpLeft, clip->ypTop, clip->xpRight, clip->ypBottom};
                        mm_depth_draw(bitmap, data, 20, 8, x, y, clip ? &rect : nullptr,
                                      r ? r->Geometry() : nullptr, half);
                        Record(data, sizeof(data));
                    }
    ReleasePpo(&none);
    ReleasePpo(&region);
    mm_depth_owner_release(z);
}
static void CacheOwnership()
{
    auto file = ChunkyFile::PcflCreateTemp();
    CHECK(file);
    auto created = NewDepthBitmap(8, 8);
    CHECK(created);
    ChunkNumber number = 0;
    CHECK(WriteDepthBitmap(created, file->WriteOwnerIndex(), kctgZbmp, &number));
    mm_depth_owner_release(created);

    auto resourceFile = ChunkyResourceFile::PcrfNew(file, 4096);
    CHECK(resourceFile);
    ReleasePpo(&file);
    auto cache = resourceFile->CoreCache();
    auto services = mm_depth_owner_services();
    auto first = static_cast<MMDepthOwner *>(
        mm_resource_cache_fetch_owned_chunk(cache, kctgZbmp, number, ReadCachedDepthBitmap, services));
    CHECK(first && cache->count == 1);
    auto second = mm_resource_cache_fetch_owned_chunk(cache, kctgZbmp, number, ReadCachedDepthBitmap, services);
    CHECK(second == first && services->references(first)->count == 2);
    MMResourceEntry entry;
    CHECK(mm_resource_cache_at(cache, 0, &entry) == MM_STORAGE_OK && entry.objects == services);
    const auto binding = mm_depth_owner_binding(first);
    CHECK(binding->cache == cache && binding->attached && binding->priority == MM_RESOURCE_NORMAL_PRIORITY);
    mm_depth_owner_priority(first, MM_RESOURCE_TOSS_PRIORITY);
    CHECK(binding->priority == MM_RESOURCE_TOSS_PRIORITY);
    mm_depth_owner_release(first);
    CHECK(services->references(first)->count == 1);
    mm_depth_owner_detach(first);
    CHECK(!binding->attached && cache->count == 0);

    auto replacement = static_cast<MMDepthOwner *>(
        mm_resource_cache_fetch_owned_chunk(cache, kctgZbmp, number, ReadCachedDepthBitmap, services));
    CHECK(replacement && replacement != first && cache->count == 1);
    mm_depth_owner_release(replacement);
    mm_resource_cache_budget(cache, 0);
    CHECK(cache->count == 0);
    ReleasePpo(&resourceFile);
    CHECK(mm_depth_owner_bitmap(first)->bytes == 128 && binding->cache == cache);
    mm_depth_owner_release(first);
}
int main()
{
    if (auto path = std::getenv("MM_DEPTH_BITMAP_SNAPSHOT"))
    {
        snapshot = std::fopen(path, "wb");
        CHECK(snapshot);
    }
    Loads();
    Constructors();
    Draws();
    CacheOwnership();
    if (snapshot)
        CHECK(!std::fclose(snapshot));
    std::printf("Depth bitmap: bytes=%u fingerprint=%08X\n", recorded, hash);
#ifdef DEBUG
    const unsigned short one = 1;
    if (*reinterpret_cast<const byte *>(&one))
    {
        // Factory snapshots include the architecture-dependent native cache charge.
        const unsigned expected = sizeof(void *) == 8 ? 0x1BE82F14U : 0xAEAC95B4U;
        CHECK(recorded == 65880 && hash == expected);
    }
#endif
}
