#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#if defined(_MSC_VER) && defined(DEBUG)
#include <crtdbg.h>
#endif
#include "soc_core.h"
#include "three_mm/codec.h"
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
class MaterialAccess : public Material_MTRL
{
  public:
    static MaterialAccess *Create()
    {
        return NewObj MaterialAccess;
    }
    bool Initialize(PChunkyResourceFile c, ChunkNumber n)
    {
        return _FInit(c, kctgMtrl, n);
    }
};
class CustomAccess : public CustomMaterial_CMTL
{
  public:
    static CustomAccess *Create()
    {
        return NewObj CustomAccess;
    }
    bool Initialize(PChunkyResourceFile c, ChunkNumber n)
    {
        return _FInit(c, kctgCmtl, n);
    }
    static PMaterial_MTRL MaterialAt(PCustomMaterial_CMTL c, int i)
    {
#ifdef THREE_MM_ENGINE_MATERIAL_RESOURCE_H
        auto member = &CustomAccess::_custom;
        auto &s = c->*member;
        return s.materials ? static_cast<PMaterial_MTRL>(s.materials[i]) : nullptr;
#else
        auto member = &CustomAccess::_prgpmtrl;
        auto p = c->*member;
        return p ? p[i] : nullptr;
#endif
    }
    static PModel ModelAt(PCustomMaterial_CMTL c, int i)
    {
#ifdef THREE_MM_ENGINE_MATERIAL_RESOURCE_H
        auto member = &CustomAccess::_custom;
        auto &s = c->*member;
        return s.models ? static_cast<PModel>(s.models[i]) : nullptr;
#else
        auto member = &CustomAccess::_prgpmodl;
        auto p = c->*member;
        return p ? p[i] : nullptr;
#endif
    }
};
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
    auto b = m->Pbmtl();
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
                      Material_MTRL::PmtrlFromBmtl(b) == m};
    Record(v, sizeof(v));
    Record(&b->map_transform, sizeof(b->map_transform));
    if (complete)
    {
        auto f = ChunkyFile::PcflCreateTemp();
        CHECK(f);
        ChunkNumber n;
        CHECK(m->FWrite(f, kctgMtrl, &n));
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
    auto t = TextureMap::PtmapNew(p, w, h);
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
        CHECK(t->FWrite(f, kctgTmap, &child));
        CHECK(f->FAdoptChild(kctgMtrl, *number, kctgTmap, child, 0));
        ReleasePpo(&t);
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
                    PMaterial_MTRL m = factory ? nullptr : MaterialAccess::Create();
                    CHECK(factory || m);
                    LegacyLong estimate = -1;
                    CHECK(Material_MTRL::FReadMtrl(cache, kctgMtrl, n, &block, nullptr, &estimate));
                    Value(estimate);
                    Inject(kind, failure);
                    bool ok = factory ? Material_MTRL::FReadMtrl(cache, kctgMtrl, n, &block, (PBaseCacheableObject *)&m,
                                                                 &estimate)
                                      : static_cast<MaterialAccess *>(m)->Initialize(cache, n);
                    Inject(kind, 0);
                    Value(ok);
                    if (factory && !ok)
                        m = nullptr;
                    if (factory && ok)
                        Value(estimate);
                    State(m, ok);
                    ReleasePpo(&m);
                    block.Free();
                    ReleasePpo(&cache);
                    ReleasePpo(&file);
                }
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
        auto m = Material_MTRL::PmtrlNew(i + 1, 12);
        CHECK(m);
        ChunkNumber child;
        CHECK(m->FWrite(f, kctgMtrl, &child) && f->FAdoptChild(kctgCmtl, *number, kctgMtrl, child, i));
        ReleasePpo(&m);
        if (variant >= 2 && i == 1)
        {
            BRV v[3] = {};
            BRF face = {};
            v[1].p.v[0] = 65536;
            v[2].p.v[1] = 65536;
            face.vertices[1] = 1;
            face.vertices[2] = 2;
            auto model = Model::PmodlNew(3, v, 1, &face);
            CHECK(model);
            ChunkNumber modelNumber;
            DataBlock b;
            CHECK(f->FAdd(0, kctgBmdl, &modelNumber, &b));
            b.Free();
            CHECK(model->FWrite(f, kctgBmdl, modelNumber));
            CHECK(f->FAdoptChild(kctgCmtl, *number, kctgBmdl, modelNumber, i));
            ReleasePpo(&model);
        }
    }
    if (variant == 3)
    {
        unsigned char packed[256] = {'K', 'C', 'D', 'C', 0, 0, 0, 8};
        ThreeMMI32 bytes;
        CHECK(three_mm_codec_encode(THREE_MM_CODEC_KCDC, &h, sizeof(h), packed + 8, sizeof(packed) - 8, &bytes,
                                    nullptr) == THREE_MM_DATA_OK);
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
                    PCustomMaterial_CMTL c = factory ? nullptr : CustomAccess::Create();
                    CHECK(factory || c);
                    LegacyLong estimate = -1;
                    CHECK(CustomMaterial_CMTL::FReadCmtl(cache, kctgCmtl, n, &block, nullptr, &estimate));
                    Value(estimate);
                    Inject(kind, failure);
                    bool ok = factory ? CustomMaterial_CMTL::FReadCmtl(cache, kctgCmtl, n, &block,
                                                                       (PBaseCacheableObject *)&c, &estimate)
                                      : static_cast<CustomAccess *>(c)->Initialize(cache, n);
                    Inject(kind, 0);
                    Value(ok);
                    if (factory && !ok)
                        c = nullptr;
                    Value(c != nullptr);
                    if (c)
                    {
                        Value(c->Ibset());
                        Value(c->Cbprt());
                        if (factory && ok)
                            Value(estimate);
                        for (int i = 0; i < c->Cbprt(); ++i)
                        {
                            auto m = CustomAccess::MaterialAt(c, i);
                            auto model = CustomAccess::ModelAt(c, i);
                            Value(m != nullptr);
                            Value(model != nullptr);
                            if (m)
                                State(m, true);
                        }
                        if (ok)
                            Value(c->FHasModels());
                    }
                    ReleasePpo(&c);
                    block.Free();
                    ReleasePpo(&cache);
                    ReleasePpo(&file);
                }
}
static void MoreCases()
{
    auto a = Material_MTRL::PmtrlNew(), b = Material_MTRL::PmtrlNew(3, 5);
    CHECK(a && b);
    PMaterial_MTRL materials[] = {a, b, a};
    for (int kind = 0; kind < 2; ++kind)
        for (int failure = 0; failure < 7; ++failure)
        {
            Inject(kind, failure);
            auto c = CustomMaterial_CMTL::PcmtlNew(-3, 3, materials);
            Inject(kind, 0);
            Value(c != nullptr);
            Value(a->CactRef());
            Value(b->CactRef());
            if (c)
            {
                Value(c->Ibset());
                Value(c->Cbprt());
                Value(c->FHasModels());
            }
            ReleasePpo(&c);
            Value(a->CactRef());
            Value(b->CactRef());
        }
    ReleasePpo(&a);
    ReleasePpo(&b);
    ChunkNumber n;
    auto source = MaterialFile(7, &n);
    auto cache = ChunkyResourceFile::PcrfNew(source, 0);
    CHECK(cache);
    auto m = static_cast<PMaterial_MTRL>(cache->PbacoFetch(kctgMtrl, n, Material_MTRL::FReadMtrl));
    CHECK(m);
    for (int kind = 0; kind < 2; ++kind)
        for (int failure = 0; failure < 18; ++failure)
        {
            auto f = ChunkyFile::PcflCreateTemp();
            CHECK(f);
            ChunkNumber output = 0;
            Inject(kind, failure);
            bool ok = m->FWrite(f, kctgMtrl, &output);
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
    ReleasePpo(&m);
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
    m = Material_MTRL::PmtrlNewFromPix(&path);
    CHECK(m);
    State(m, true);
    // TextureMap's legacy imported-pixelmap lifetime is a later migration input.
    // Release that SDK owner explicitly after observing material disposal.
    raw = m->Pbmtl()->colour_map;
    ReleasePpo(&m);
    BrPixelmapFree(raw);
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
    if (auto path = std::getenv("THREE_MM_MATERIAL_RESOURCE_SNAPSHOT"))
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
    CHECK(shade->FWrite(file, kctgTmap, &number) && Material_MTRL::FSetShadeTable(file, kctgTmap, number));
    ReleasePpo(&shade);
    ReleasePpo(&file);
    for (int i = 0; i < 6; ++i)
    {
        auto m = Material_MTRL::PmtrlNew(i % 2 ? -1 : 14, i % 3 ? -1 : 10);
        CHECK(m);
        State(m, true);
        ReleasePpo(&m);
    }
    MaterialLoads();
    CustomLoads();
    MoreCases();
    Material_MTRL::ReleaseShadeTable();
    BrZbEnd();
    BrEnd();
    if (snapshot)
        CHECK(std::fclose(snapshot) == 0);
    std::printf("Material resources: bytes=%u fingerprint=%08X\n", bytesRecorded, hash);
#ifdef DEBUG
    unsigned short one = 1;
    if (*reinterpret_cast<const byte *>(&one))
        CHECK(bytesRecorded == 102316 && hash == 0x113D98C1U);
#endif
}
