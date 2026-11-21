#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#if defined(_MSC_VER) && defined(DEBUG)
#include <crtdbg.h>
#endif
#include "soc_core.h"
#undef size
ASSERTNAME
void WarnProc(schar *file, LegacyLong line, schar *message)
{
    std::fprintf(stderr, "%s:%ld: %s\n", file, static_cast<long>(line), message);
}
bool FAssertProc(schar *file, LegacyLong line, schar *message, void *, LegacyLong)
{
    WarnProc(file, line, message);
    std::exit(1);
}
#define CHECK(c)                                                                                                       \
    do                                                                                                                 \
    {                                                                                                                  \
        if (!(c))                                                                                                      \
        {                                                                                                              \
            std::fprintf(stderr, "model resource: %d\n", __LINE__);                                                    \
            std::exit(1);                                                                                              \
        }                                                                                                              \
    } while (0)
static bool Prelight(PModel model)
{
    BVEC3 light = {};
    return EngineModel::Prelight(model, 1, &light);
}
static FILE *snapshot;
static std::uint32_t fingerprint = 2166136261u, traceBytes = 0;
static void Record(const void *p, std::size_t count)
{
    auto bytes = static_cast<const unsigned char *>(p);
    for (std::size_t i = 0; i < count; ++i)
        fingerprint = (fingerprint ^ bytes[i]) * 16777619u;
    traceBytes += static_cast<std::uint32_t>(count);
    if (snapshot)
        CHECK(std::fwrite(p, 1, count, snapshot) == count);
}
static void Value(LegacyLong n)
{
    Record(&n, sizeof(n));
}
static std::vector<byte> Wire(PModel model)
{
    auto file = ChunkyFile::PcflCreateTemp();
    CHECK(file);
    CHECK(EngineModel::Write(model, file, 'MODL', 7));
    DataBlock block;
    CHECK(file->FFind('MODL', 7, &block));
    std::vector<byte> bytes(block.Cb());
    CHECK(block.FReadRgb(bytes.data(), static_cast<LegacyLong>(bytes.size()), 0));
    block.Free();
    ReleasePpo(&file);
    return bytes;
}
static void State(PModel model, bool complete)
{
    Value(model != nullptr);
    if (!model)
        return;
    auto m = EngineModel::Handle(model);
    Value(m != nullptr);
    if (!m)
        return;
    LegacyLong fields[] = {m->flags,
                           m->nvertices,
                           m->nfaces,
                           m->nprepared_vertices,
                           m->nprepared_faces,
                           m->nvertex_groups,
                           m->nface_groups,
                           m->prepared_vertices != nullptr,
                           m->prepared_faces != nullptr,
                           m->vertex_groups != nullptr,
                           m->face_groups != nullptr,
                           EngineModel::FromHandle(m) == model};
    Record(fields, sizeof(fields));
    if (complete)
    {
        Value(EngineModel::Extent(model, 0));
        Value(EngineModel::Extent(model, 1));
        Value(EngineModel::Extent(model, 2));
        auto wire = Wire(model);
        Value(static_cast<LegacyLong>(wire.size()));
        Record(wire.data(), wire.size());
    }
}
static PModel Source(int pattern)
{
    BRV vertices[4] = {};
    BRF faces[2] = {};
    vertices[0].p.v[0] = BR_SCALAR(pattern ? -1.125 : 0.25);
    vertices[0].p.v[1] = BR_SCALAR(-2);
    vertices[0].p.v[2] = BR_SCALAR(-3);
    vertices[1].p.v[0] = BR_SCALAR(4.125);
    vertices[2].p.v[1] = BR_SCALAR(5.25);
    vertices[3].p.v[2] = BR_SCALAR(6.5);
    for (int i = 0; i < 4; ++i)
    {
        vertices[i].map.v[0] = 12345 + i;
        vertices[i].map.v[1] = -23456 - i;
        vertices[i].index = static_cast<byte>(17 + i);
    }
    faces[0].vertices[1] = 1;
    faces[0].vertices[2] = 2;
    faces[0].smoothing = 1;
    faces[0]._pad0 = 37;
    faces[1].vertices[0] = 1;
    faces[1].vertices[1] = 2;
    faces[1].vertices[2] = 3;
    faces[1].smoothing = 2;
    return EngineModel::New(4, vertices, 2, faces);
}
static void Inject(int kind, int n)
{
#ifdef DEBUG
    vmutxMem.Enter();
    auto &f = kind ? vdmglob.dmaglPv : vdmglob.dmaglBase;
    f.cactDo = n ? n - 1 : 0;
    f.cactFail = n ? 1 : 0;
    vmutxMem.Leave();
#else
    (void)kind;
    (void)n;
#endif
}
static br_allocator *underlyingAllocator;
static void *trackedBlocks[256];
static unsigned trackedCount;
static void *BR_CALLBACK TrackAllocate(br_size_t bytes, br_uint_8 type)
{
    void *p = underlyingAllocator->allocate(bytes, type);
    if (p)
    {
        CHECK(trackedCount < 256);
        trackedBlocks[trackedCount++] = p;
    }
    return p;
}
static void BR_CALLBACK TrackFree(void *p)
{
    for (unsigned i = 0; i < trackedCount; ++i)
        if (trackedBlocks[i] == p)
        {
            trackedBlocks[i] = trackedBlocks[--trackedCount];
            break;
        }
    underlyingAllocator->free(p);
}
static br_size_t BR_CALLBACK TrackInquire(br_uint_8 type)
{
    return underlyingAllocator->inquire(type);
}
static void CheckPrelightLifetime(PModel model)
{
    // The first prelight above warms any renderer-wide caches. Track every
    // SDK allocation in a second call and require all of them to be freed.
    br_allocator tracker = {"Model lifetime fixture", TrackAllocate, TrackFree, TrackInquire};
    underlyingAllocator = BrAllocatorSet(&tracker);
    CHECK(Prelight(model));
    BrAllocatorSet(underlyingAllocator);
    CHECK(!trackedCount);
}
static void Dat(const char *identifier)
{
    auto source = Source(0);
    CHECK(source);
    auto prepared = EngineModel::Handle(source);
    auto raw = BrModelAllocate(const_cast<char *>(identifier), prepared->nprepared_vertices, prepared->nprepared_faces);
    CHECK(raw);
    std::memcpy(raw->vertices, prepared->prepared_vertices, prepared->nprepared_vertices * sizeof(BRV));
    std::memcpy(raw->faces, prepared->prepared_faces, prepared->nprepared_faces * sizeof(BRF));
    Filename filename;
    CHECK(filename.FGetTemp());
    String path;
    filename.GetStnPath(&path);
    CHECK(BrModelSave(path.Psz(), raw));
    BrModelFree(raw);
    auto loaded = EngineModel::FromDat(&filename);
    CHECK(loaded && EngineModel::FromHandle(EngineModel::Handle(loaded)) == loaded &&
          (EngineModel::Handle(loaded)->flags & BR_MODF_KEEP_ORIGINAL) &&
          EngineModel::Handle(loaded)->nprepared_faces == prepared->nprepared_faces);
    mm_model_owner_release(loaded);
    CHECK(filename.FDelete());
    mm_model_owner_release(source);
}
#if defined(_MSC_VER) && defined(DEBUG)
static int RuntimeReport(int type, char *message, int *)
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
    _CrtSetReportHook(RuntimeReport);
#endif
    if (const char *path = std::getenv("MM_MODEL_RESOURCE_SNAPSHOT"))
    {
        snapshot = std::fopen(path, "wb");
        CHECK(snapshot);
    }
    BrBegin();
    br_diaghandler diagnostics = {"Model resource test",
                                  [](char *m) { std::fprintf(stderr, "BRender warning: %s\n", m); },
                                  [](char *m) {
                                      std::fprintf(stderr, "BRender failure: %s\n", m);
                                      std::exit(1);
                                  }};
    BrDiagHandlerSet(&diagnostics);
    BrZbBegin(BR_PMT_INDEX_8, BR_PMT_DEPTH_16);
    int attempts = 0, loaded = 0;
    for (int pattern = 0; pattern < 2; ++pattern)
    {
        auto source = Source(pattern);
        CHECK(source);
        auto original = Wire(source);
        State(source, true);
        EngineModel::AdjustGlyph(source);
        State(source, true);
        EngineModel::AdjustGlyph(source);
        State(source, true);
        mm_model_owner_release(source);
        for (int variant = 0; variant < 9; ++variant)
            for (int factory = 0; factory < 2; ++factory)
                for (int kind = 0; kind < 2; ++kind)
                    for (int failure = 0; failure < 7; ++failure)
                    {
#ifdef DEBUG
                        const LegacyLong base = vdmglob.dmaglBase.cv, pv = vdmglob.dmaglPv.cv, hq = vdmglob.dmaglHq.cv;
#endif
                        auto bytes = original;
                        ModelOnFile nativeHeader;
                        std::memcpy(&nativeHeader, bytes.data(), sizeof(nativeHeader));
                        if (variant == 1 || variant == 3)
                            std::memset(bytes.data() + 8, 0, 4);
                        if (variant == 2 || variant == 3 || variant == 4)
                            SwapBytesBom(bytes.data(), kbomModlf);
                        if (variant == 4)
                        {
                            for (int i = 0; i < nativeHeader.cver; ++i)
                                SwapBytesBom(bytes.data() + 48 + i * 32, kbomBrv);
                            for (int i = 0; i < nativeHeader.cfac; ++i)
                                SwapBytesBom(bytes.data() + 48 + nativeHeader.cver * 32 + i * 32, kbomBrf);
                        }
                        if (variant == 5)
                            bytes.insert(bytes.end(), 17, 0x73);
                        if (variant == 6)
                            bytes.resize(47);
                        if (variant == 8)
                        {
                            bytes.resize(48);
                            std::memset(bytes.data() + 4, 0, 4);
                        }
                        auto file = ChunkyFile::PcflCreateTemp();
                        CHECK(file);
                        ChunkNumber number;
                        CHECK(file->FAddPv(bytes.data(), static_cast<LegacyLong>(bytes.size()), 'MODL', &number));
                        if (variant == 7)
                            CHECK(file->FPackData('MODL', number));
                        auto cache = ChunkyResourceFile::PcrfNew(file, 0);
                        CHECK(cache);
                        DataBlock block;
                        CHECK(file->FFind('MODL', number, &block));
                        MMI32 estimate = -1;
                        CHECK(EngineModel::Read(cache->CoreCache(), 'MODL', number, block.CoreBlock(), nullptr, &estimate));
                        Value(estimate);
                        PModel model = factory ? nullptr : mm_model_owner_create(&EngineModel::Host());
                        CHECK(factory || model);
                        bool ok;
                        Inject(kind, failure);
                        if (factory)
                        {
                            void *output = nullptr;
                            ok = EngineModel::Read(cache->CoreCache(), 'MODL', number, block.CoreBlock(), &output, &estimate);
                            model = ok ? static_cast<PModel>(output) : nullptr;
                        }
                        else
                            ok = mm_model_read(model, block.CoreBlock());
                        Inject(kind, 0);
                        ++attempts;
                        loaded += ok;
                        LegacyLong fields[] = {pattern, variant, factory, kind, failure, ok, estimate};
                        Record(fields, sizeof(fields));
                        State(model, ok);
                        if (ok && variant != 4)
                        {
                            EngineModel::AdjustGlyph(model);
                            State(model, true);
                        }
                        mm_model_owner_release(model);
                        block.Free();
                        ReleasePpo(&cache);
                        ReleasePpo(&file);
                        vpers->Clear();
#ifdef DEBUG
                        CHECK(vdmglob.dmaglBase.cv == base && vdmglob.dmaglPv.cv == pv && vdmglob.dmaglHq.cv == hq);
#endif
                    }
    }
    // The private routine ignores direction input; capture its actual default
    // lighting result. The original leaked its temporary material/pixelmaps.
    auto lit = Source(0);
    CHECK(lit);
    CHECK(Prelight(lit));
    State(lit, true);
    CheckPrelightLifetime(lit);
    mm_model_owner_release(lit);
    // This checksum excludes DAT's external file representation.
    const unsigned endian = 1;
    if (*reinterpret_cast<const byte *>(&endian))
    {
#ifdef DEBUG
        CHECK(traceBytes == 277360 && fingerprint == 0x78c575e5u);
#endif
    }
    Dat("Model resource DAT fixture");
    Dat("x");
    BrZbEnd();
    BrEnd();
    if (snapshot)
        CHECK(!std::fclose(snapshot));
    std::printf("Model resources: attempts=%d loaded=%d bytes=%u fingerprint=%08x\n", attempts, loaded, traceBytes,
                fingerprint);
    CHECK(attempts == 504);
}
