#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#if defined(_MSC_VER) && defined(DEBUG)
#include <crtdbg.h>
#endif
#include "soc_core.h"
#include "texture_renderer.h"
#include "mm/codec.h"
#undef size
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
            std::fprintf(stderr, "material resource: %d\n", __LINE__);                                                 \
            std::exit(1);                                                                                              \
        }                                                                                                              \
    } while (0)
static PMaterial_MTRL MaterialAt(PCustomMaterial_CMTL costume, int index)
{
    const auto data = mm_material_owner_custom(costume);
    return data->materials ? static_cast<PMaterial_MTRL>(data->materials[index]) : nullptr;
}

static PModel ModelAt(PCustomMaterial_CMTL costume, int index)
{
    const auto data = mm_material_owner_custom(costume);
    return data->models ? static_cast<PModel>(data->models[index]) : nullptr;
}

static FILE *snapshot;
static unsigned hash = 2166136261U, bytesRecorded;
static void Record(const void *p, std::size_t n)
{
    auto b = static_cast<const unsigned char *>(p);
    for (std::size_t i = 0; i < n; ++i)
        hash = (hash ^ b[i]) * 16777619U;
    bytesRecorded += unsigned(n);
    if (snapshot)
        CHECK(std::fwrite(p, 1, n, snapshot) == n);
}
static void Value(LegacyLong n)
{
    Record(&n, 4);
}
static void Chunk(PChunkyFile f, ChunkTagOrType t, ChunkNumber n)
{
    DataBlock b;
    CHECK(f->FFind(t, n, &b));
    std::vector<byte> bytes(b.Cb());
    CHECK(b.FReadRgb(bytes.data(), bytes.size(), 0));
    Value(bytes.size());
    Record(bytes.data(), bytes.size());
}
static void State(PMaterial_MTRL m, bool complete)
{
    Value(m != nullptr);
    if (!m)
        return;
    auto b = mm_brender_material_handle(m);
    Value(b != nullptr);
    if (!b)
        return;
    LegacyLong v[] = {static_cast<LegacyLong>(b->colour),
                      b->ka,
                      b->kd,
                      b->ks,
                      b->power,
                      b->index_base,
                      b->index_range,
                      b->opacity,
                      static_cast<LegacyLong>(b->flags),
                      b->colour_map != nullptr,
                      b->index_shade != nullptr,
                      mm_brender_material_owner(b) == m};
    Record(v, sizeof(v));
    Record(&b->map_transform, sizeof(b->map_transform));
    if (complete)
    {
        auto f = ChunkyFile::PcflCreateTemp();
        CHECK(f);
        ChunkNumber n;
        const auto writer = f->WriteIndex();
        CHECK(mm_material_owner_write(m, &writer, kctgMtrl, &n));
        Chunk(f, kctgMtrl, n);
        ChildChunkIdentification child;
        bool texture = f->FGetKidChidCtg(kctgMtrl, n, 0, kctgTmap, &child);
        Value(texture);
        if (texture)
            Chunk(f, child.cki.ctg, child.cki.cno);
        ReleasePpo(&f);
    }
}
static PTextureMap Map(int w, int h)
{
    byte *p = nullptr;
    CHECK(FAllocPv((void **)&p, w * h, fmemClear, mprNormal));
    for (int i = 0; i < w * h; ++i)
        p[i] = byte((i * 17) % 256);
    auto t = EngineTexture::New(p, w, h);
    CHECK(t);
    return t;
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
static PChunkyFile MaterialFile(int variant, ChunkNumber *number)
{
    auto f = ChunkyFile::PcflCreateTemp();
    CHECK(f);
    MaterialOnFile h = {};
    h.bo = kboCur;
    h.osk = koskCur;
    h.brc = 0x123456;
    h.brufKa = 1234;
    h.brufKd = 34567;
    h.brufKs = 23456;
    h.bIndexBase = 13;
    h.cIndexRange = 19;
    h.rPower = 1234567;
    if (variant == 1)
        SwapBytesBom(&h, kbomMtrlf);
    if (variant == 2)
    {
        h.bo = 0;
        h.osk = koskSbWin;
    }
    std::vector<byte> raw(sizeof(h));
    std::memcpy(raw.data(), &h, sizeof(h));
    if (variant == 3)
        raw.insert(raw.end(), 7, 0xab);
    if (variant == 4)
        raw.insert(raw.end(), 160, 0);
    if (variant == 5)
        raw.resize(19);
    CHECK(f->FAddPv(raw.data(), raw.size(), kctgMtrl, number));
    if (variant == 4)
        CHECK(f->FPackData(kctgMtrl, *number));
    if (variant >= 6 && variant <= 9)
    {
        auto t = Map(4, 4);
        ChunkNumber child;
        CHECK(EngineTexture::Write(t, f, kctgTmap, &child));
        CHECK(f->FAdoptChild(kctgMtrl, *number, kctgTmap, child, 0));
        mm_texture_owner_release(t);
    }
    if (variant >= 7)
    {
        TXXFF transform = {};
        transform.bo = kboCur;
        transform.osk = koskCur;
        for (int i = 0; i < 6; ++i)
            reinterpret_cast<BRS *>(&transform.bmat23)[i] = i * 123456 - 345678;
        if (variant == 8)
            SwapBytesBom(&transform, kbomTxxff);
        ChunkNumber child;
        CHECK(f->FAddPv(&transform, variant == 9 ? 27 : sizeof(transform), kctgTxxf, &child));
        CHECK(f->FAdoptChild(kctgMtrl, *number, kctgTxxf, child, 0));
    }
    return f;
}
static void MaterialLoads()
{
    for (int variant = 0; variant < 11; ++variant)
        for (int factory = 0; factory < 2; ++factory)
            for (int kind = 0; kind < 2; ++kind)
                for (int failure = 0; failure < 9; ++failure)
                {
                    ChunkNumber n;
                    auto file = MaterialFile(variant, &n);
                    auto cache = ChunkyResourceFile::PcrfNew(file, 0);
                    CHECK(cache);
                    DataBlock block;
                    CHECK(file->FFind(kctgMtrl, n, &block));
                    PMaterial_MTRL m = factory ? nullptr : mm_material_owner_create(mm_kauai_material_host(), MM_MATERIAL);
                    CHECK(factory || m);
                    MMI32 estimate = -1;
                    CHECK(mm_brender_material_read(cache->CoreCache(), kctgMtrl, n, block.CoreBlock(), nullptr, &estimate));
                    Value(estimate);
                    Inject(kind, failure);
                    void *output = nullptr;
                    bool ok = factory ? mm_brender_material_read(cache->CoreCache(), kctgMtrl, n, block.CoreBlock(), &output,
                                                                 &estimate)
                                      : mm_material_read(m, cache->CoreCache(), kctgMtrl, n, nullptr);
                    Inject(kind, 0);
                    Value(ok);
                    if (factory)
                        m = ok ? static_cast<PMaterial_MTRL>(output) : nullptr;
                    if (factory && ok)
                        Value(estimate);
                    State(m, ok);
                    mm_material_owner_release(m);
                    block.Free();
                    ReleasePpo(&cache);
                    ReleasePpo(&file);
                }
}
static void CachedTextureOwnership()
{
    ChunkNumber number;
    auto file = MaterialFile(6, &number);
    auto cache = ChunkyResourceFile::PcrfNew(file, 1024);
    auto material = mm_material_owner_create(mm_kauai_material_host(), MM_MATERIAL);
    CHECK(cache && material && mm_material_read(material, cache->CoreCache(), kctgMtrl, number, nullptr));
    auto texture = static_cast<PTextureMap>(mm_material_owner_texture(material));
    CHECK(texture && texture->binding.cache == cache->CoreCache() && texture->binding.attached);

    const auto child = texture->binding.number;
    auto peer = static_cast<PTextureMap>(mm_resource_cache_fetch_owned_chunk(
        cache->CoreCache(), kctgTmap, child, EngineTexture::Read, mm_texture_owner_services()));
    CHECK(peer == texture && texture->references.count == 2);
    mm_material_owner_release(material);
    CHECK(texture->references.count == 1 && EngineTexture::Pixelmap(texture)->width == 4);
    mm_texture_owner_release(peer);
    cache->SetCbMax(0);
    CHECK(!mm_resource_cache_find(cache->CoreCache(), kctgTmap, child, EngineTexture::Read, 0));
    ReleasePpo(&cache);
    ReleasePpo(&file);
}

static PChunkyFile CustomFile(int variant, ChunkNumber *number)
{
    auto f = ChunkyFile::PcflCreateTemp();
    CHECK(f);
    CustomMaterialOnFile h = {kboCur, koskCur, variant % 2 ? -7 : 3};
    if (variant == 1)
        SwapBytesBom(&h, kbomCmtlf);
    CHECK(f->FAddPv(&h, sizeof(h), kctgCmtl, number));
    for (int i = 0; i < 3; ++i)
    {
        auto m = mm_brender_material_new(i + 1, 12);
        CHECK(m);
        ChunkNumber child;
        const auto writer = f->WriteIndex();
        CHECK(mm_material_owner_write(m, &writer, kctgMtrl, &child) && f->FAdoptChild(kctgCmtl, *number, kctgMtrl, child, i));
        mm_material_owner_release(m);
        if (variant >= 2 && i == 1)
        {
            BRV v[3] = {};
            BRF face = {};
            v[1].p.v[0] = 65536;
            v[2].p.v[1] = 65536;
            face.vertices[1] = 1;
            face.vertices[2] = 2;
            auto model = mm_model_new(3, v, 1, &face, mm_kauai_model_host());
            CHECK(model);
            ChunkNumber modelNumber;
            DataBlock b;
            CHECK(f->FAdd(0, kctgBmdl, &modelNumber, &b));
            b.Free();
            const auto writer = f->WriteOwnerIndex();
            CHECK(mm_model_owner_write(model, &writer, kctgBmdl, modelNumber));
            CHECK(f->FAdoptChild(kctgCmtl, *number, kctgBmdl, modelNumber, i));
            mm_model_owner_release(model);
        }
    }
    if (variant == 3)
    {
        unsigned char packed[256] = {'K', 'C', 'D', 'C', 0, 0, 0, 8};
        MMI32 bytes;
        CHECK(mm_codec_encode(MM_CODEC_KCDC, &h, sizeof(h), packed + 8, sizeof(packed) - 8, &bytes,
                                    nullptr) == MM_DATA_OK);
        CHECK(f->FPutPv(packed, bytes + 8, kctgCmtl, *number));
        f->SetPacked(kctgCmtl, *number, true);
    }
    return f;
}
static void CustomLoads()
{
    for (int variant = 0; variant < 4; ++variant)
        for (int factory = 0; factory < 2; ++factory)
            for (int kind = 0; kind < 2; ++kind)
                for (int failure = 0; failure < 12; ++failure)
                {
                    ChunkNumber n;
                    auto file = CustomFile(variant, &n);
                    auto cache = ChunkyResourceFile::PcrfNew(file, 0);
                    CHECK(cache);
                    DataBlock block;
                    CHECK(file->FFind(kctgCmtl, n, &block));
                    PCustomMaterial_CMTL c = factory ? nullptr : mm_material_owner_create(mm_kauai_material_host(), MM_CUSTOM_MATERIAL);
                    CHECK(factory || c);
                    MMI32 estimate = -1;
                    CHECK(mm_brender_costume_read(cache->CoreCache(), kctgCmtl, n, block.CoreBlock(), nullptr, &estimate));
                    Value(estimate);
                    Inject(kind, failure);
                    void *output = nullptr;
                    bool ok = factory ? mm_brender_costume_read(cache->CoreCache(), kctgCmtl, n, block.CoreBlock(),
                                                                       &output, &estimate)
                                      : mm_custom_material_read(c, cache->CoreCache(), kctgCmtl, n, nullptr);
                    Inject(kind, 0);
                    Value(ok);
                    if (factory)
                        c = ok ? static_cast<PCustomMaterial_CMTL>(output) : nullptr;
                    Value(c != nullptr);
                    if (c)
                    {
                        const auto costume = mm_material_owner_custom(c);
                        Value(costume->set);
                        Value(costume->count);
                        if (factory && ok)
                            Value(estimate);
                        for (int i = 0; i < costume->count; ++i)
                        {
                            auto m = MaterialAt(c, i);
                            auto model = ModelAt(c, i);
                            Value(m != nullptr);
                            Value(model != nullptr);
                            if (m)
                                State(m, true);
                        }
                        if (ok)
                            Value(mm_custom_material_has_models(costume));
                    }
                    mm_material_owner_release(c);
                    block.Free();
                    ReleasePpo(&cache);
                    ReleasePpo(&file);
                }
}
static void MoreCases()
{
    auto a = mm_brender_material_new(-1, -1), b = mm_brender_material_new(3, 5);
    CHECK(a && b);
    PMaterial_MTRL materials[] = {a, b, a};
    for (int kind = 0; kind < 2; ++kind)
        for (int failure = 0; failure < 7; ++failure)
        {
            Inject(kind, failure);
            auto c = mm_custom_material_new(-3, 3, materials, mm_kauai_material_host());
            Inject(kind, 0);
            Value(c != nullptr);
            Value(mm_material_owner_references(a));
            Value(mm_material_owner_references(b));
            if (c)
            {
                const auto costume = mm_material_owner_custom(c);
                Value(costume->set);
                Value(costume->count);
                Value(mm_custom_material_has_models(costume));
            }
            mm_material_owner_release(c);
            Value(mm_material_owner_references(a));
            Value(mm_material_owner_references(b));
        }
    mm_material_owner_release(a);
    mm_material_owner_release(b);
    ChunkNumber n;
    auto source = MaterialFile(7, &n);
    auto cache = ChunkyResourceFile::PcrfNew(source, 0);
    CHECK(cache);
    auto m = mm_brender_material_fetch(cache->CoreCache(), kctgMtrl, n);
    CHECK(m);
    for (int kind = 0; kind < 2; ++kind)
        for (int failure = 0; failure < 18; ++failure)
        {
            auto f = ChunkyFile::PcflCreateTemp();
            CHECK(f);
            ChunkNumber output = 0;
            Inject(kind, failure);
            const auto writer = f->WriteIndex();
            bool ok = mm_material_owner_write(m, &writer, kctgMtrl, &output);
            Inject(kind, 0);
            Value(ok);
            Value(output);
            Value(f->Ccki());
            for (int i = 0; i < f->Ccki(); ++i)
            {
                ChunkIdentification id;
                LegacyLong children;
                DataBlock block;
                CHECK(f->FGetCki(i, &id, &children, &block));
                Value(id.ctg);
                Value(id.cno);
                Value(block.Cb());
                Value(children);
                block.Free();
                if (ok)
                    Chunk(f, id.ctg, id.cno);
            }
            ReleasePpo(&f);
        }
    mm_material_owner_release(m);
    ReleasePpo(&cache);
    ReleasePpo(&source);
    auto raw = BrPixelmapAllocate(BR_PMT_INDEX_8, 4, 4, nullptr, 0);
    CHECK(raw);
    std::memset(raw->pixels, 7, raw->row_bytes * raw->height);
    Filename path;
    CHECK(path.FGetTemp());
    String text;
    path.GetStnPath(&text);
    CHECK(BrPixelmapSave(text.Psz(), raw));
    BrPixelmapFree(raw);
    m = mm_material_import(text.Psz(), nullptr, 0, mm_kauai_material_host());
    CHECK(m);
    State(m, true);
    // The original texture wrapper needed this explicit SDK-owner cleanup.
    // The C-owned texture releases it during material disposal.
    raw = mm_brender_material_handle(m)->colour_map;
    mm_material_owner_release(m);
#ifndef MM_ENGINE_TEXTURE_RESOURCE_H
    BrPixelmapFree(raw);
#endif
    CHECK(path.FDelete());
}
#if defined(_MSC_VER) && defined(DEBUG)
static int Report(int type, char *m, int *)
{
    std::fprintf(stderr, "CRT: %s\n", m);
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
    if (auto path = std::getenv("MM_MATERIAL_RESOURCE_SNAPSHOT"))
    {
        snapshot = std::fopen(path, "wb");
        CHECK(snapshot);
    }
    BrBegin();
    BrZbBegin(BR_PMT_INDEX_8, BR_PMT_DEPTH_16);
    auto shade = Map(256, 8);
    auto file = ChunkyFile::PcflCreateTemp();
    CHECK(file);
    ChunkNumber number;
    CHECK(EngineTexture::Write(shade, file, kctgTmap, &number) &&
          mm_brender_material_shade_take(mm_brender_texture_from_chunk(file, kctgTmap, number)));
    mm_texture_owner_release(shade);
    ReleasePpo(&file);
    for (int i = 0; i < 6; ++i)
    {
        auto m = mm_brender_material_new(i % 2 ? -1 : 14, i % 3 ? -1 : 10);
        CHECK(m);
        State(m, true);
        mm_material_owner_release(m);
    }
    MaterialLoads();
    CachedTextureOwnership();
    CustomLoads();
    MoreCases();
    mm_brender_material_shade_clear();
    BrZbEnd();
    BrEnd();
    if (snapshot)
        CHECK(std::fclose(snapshot) == 0);
    std::printf("Material resources: bytes=%u fingerprint=%08X\n", bytesRecorded, hash);
#ifdef DEBUG
    unsigned short one = 1;
    if (*reinterpret_cast<const byte *>(&one))
    {
        // The trace includes native object sizes in resource cache estimates.
        const auto expected = sizeof(void *) == 4 ? 0x552F8BFDU : 0x113D98C1U;
        CHECK(bytesRecorded == 102316 && hash == expected);
    }
#endif
}
