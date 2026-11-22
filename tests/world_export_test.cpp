#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#include "bren.h"
#undef size
#ifdef DEBUG
using namespace BRender;
ASSERTNAME
void FrameMain(void)
{
}
class TestApp : public ApplicationBase
{
  public:
#ifdef DEBUG
    void WarnProcApp(PSZS file, long line, PSZS message) override
    {
        std::fprintf(stderr, "%s:%ld: %s\n", file, line, message);
    }
    bool FAssertProcApp(PSZS file, long line, PSZS message, void *, long) override
    {
        std::fprintf(stderr, "%s:%ld: %s\n", file, line, message);
        std::exit(1);
    }
#endif
};
#define CHECK(c)                                                                                                       \
    do                                                                                                                 \
    {                                                                                                                  \
        if (!(c))                                                                                                      \
        {                                                                                                              \
            std::fprintf(stderr, "world export: %d\n", __LINE__);                                                      \
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
static br_material *solid, *textured;
static int begins, rects, rendered;
static void Materials()
{
    for (auto material : {solid, textured})
    {
        Value(material->flags);
        Value(material->colour_map != nullptr);
        Value(material->index_base);
        Value(material->index_range);
    }
}
static void Begin(PBACT a)
{
    Value(1);
    Value(a->identifier[0]);
    ++begins;
}
static void Bounds(PBACT a, RC *r)
{
    Value(2);
    Value(a->identifier[0]);
    ++rects;
    r->Set(-1, 1, 5, 4);
}
static void Rendered(PBACT a, RC *r)
{
    Value(3);
    Value(a->identifier[0]);
    ++rendered;
    Record(r, sizeof(*r));
    Materials();
}
struct Access : World
{
    static void State(PWorld w)
    {
        Value((w->*&Access::_render).changed);
        Value(begins);
        Value(rects);
        Value(rendered);
        const auto &buffers = w->*&Access::_buffers;
        for (auto region : {&buffers.dirty_working, &buffers.dirty_screen})
        {
            const auto &bounds = region->bounds;
            RC r(bounds.left, bounds.top, bounds.right, bounds.bottom);
            Value(mm_region_empty(region) != 0);
            Record(&r, sizeof(r));
        }
        const auto rgb = mm_world_buffer_pixels(&buffers.working);
        Record(rgb.pixels, rgb.bytes);
        auto z = mm_depth_owner_bitmap(buffers.depth_working);
        Record(z->pixels, z->row_bytes * buffers.buffer_height);
        Materials();
        BMAT34 matrix;
        BRS nearZ, farZ;
        BRA fov;
        w->GetCamera(&matrix, &nearZ, &farZ, &fov);
        Record(&matrix, sizeof(matrix));
        Value(nearZ);
        Value(farZ);
        Value(fov);
        RC view(0, 0, buffers.width, buffers.height), buffer(0, 0, buffers.buffer_width, buffers.buffer_height);
        Record(&view, sizeof(RC));
        Record(&buffer, sizeof(RC));
    }
    static void Seed(PWorld w)
    {
        const auto &buffers = w->*&Access::_buffers;
        const auto background = mm_world_buffer_pixels(&buffers.background);
        std::memset(background.pixels, 3, background.bytes);
        const auto working = mm_world_buffer_pixels(&buffers.working);
        std::memset(working.pixels, 3, working.bytes);
    }
};
static unsigned Live(PWorld w)
{
    auto savedHash = hash, savedRecorded = recorded;
    auto savedSnapshot = snapshot;
    hash = 2166136261U;
    recorded = 0;
    snapshot = nullptr;
    Access::State(w);
    auto result = hash;
    hash = savedHash;
    recorded = savedRecorded;
    snapshot = savedSnapshot;
    return result;
}
static void Export(PWorld w, int scale)
{
    const auto before = Live(w);
    Filename name;
    CHECK(name.FGetTemp());
    CHECK(scale ? w->FWriteBmpScaled(&name, scale) : w->FWriteBmp(&name));
    CHECK(before == Live(w));
    auto file = FileObject::PfilOpen(&name);
    CHECK(file);
    std::vector<byte> bytes(file->FpMac());
    CHECK(file->FReadRgb(bytes.data(), bytes.size(), 0));
    Value(bytes.size());
    Record(bytes.data(), bytes.size());
    ReleasePpo(&file);
    CHECK(name.FDelete());
    Access::State(w);
}
static void FailureCleanup(PWorld w)
{
    // Supplemental checks deliberately exclude the frozen successful trace.
    for (bool scaled : {false, true})
        for (int kind = 0; kind < 3; ++kind)
            for (int failure = 0; failure <= 10; ++failure)
            {
                Filename name;
                CHECK(name.FGetTemp());
                vpers->Clear();
                const auto live = Live(w);
                const auto base = vdmglob.dmaglBase.cv, hq = vdmglob.dmaglHq.cv, pv = vdmglob.dmaglPv.cv;
                auto inject = [&](bool enabled) {
                    vmutxMem.Enter();
                    auto &a = kind == 0 ? vdmglob.dmaglBase : kind == 1 ? vdmglob.dmaglHq : vdmglob.dmaglPv;
                    a.cactDo = enabled && failure ? failure - 1 : 0;
                    a.cactFail = enabled && failure ? 1 : 0;
                    vmutxMem.Leave();
                };
                inject(true);
                const bool ok = scaled ? w->FWriteBmpScaled(&name, 2) : w->FWriteBmp(&name);
                inject(false);
                if (!failure)
                    CHECK(ok);
                name.FDelete();
                vpers->Clear();
                CHECK(live == Live(w));
                if (vdmglob.dmaglBase.cv != base || vdmglob.dmaglHq.cv != hq || vdmglob.dmaglPv.cv != pv)
                    std::fprintf(stderr, "cleanup: scaled=%d kind=%d failure=%d base=%ld/%ld hq=%ld/%ld pv=%ld/%ld\n",
                                 scaled, kind, failure, long(vdmglob.dmaglBase.cv), long(base),
                                 long(vdmglob.dmaglHq.cv), long(hq), long(vdmglob.dmaglPv.cv), long(pv));
                CHECK(vdmglob.dmaglBase.cv == base && vdmglob.dmaglHq.cv == hq && vdmglob.dmaglPv.cv == pv);
            }
}
int main()
{
    auto app = NewObj TestApp;
    CHECK(app);
    if (auto path = std::getenv("MM_WORLD_EXPORT_SNAPSHOT"))
    {
        snapshot = std::fopen(path, "wb");
        CHECK(snapshot);
    }
#ifndef KAUAI_SDL3
    vwig.hdcApp = GetDC(nullptr);
    CHECK(vwig.hdcApp);
#endif
    CHECK(vntl.FInit());
    auto palette = DynamicArray::PglNew(sizeof(Color));
    CHECK(palette);
    for (int i = 0; i < 256; ++i)
    {
        Color c = {byte(i), byte(i), byte(i), 0};
        CHECK(palette->FAdd(&c));
    }
    GraphicsPort::SetActiveColors(palette, fpalIdentity);
    ReleasePpo(&palette);
    auto initial = World::PbwldNew(32, 24);
    CHECK(initial);
    ReleasePpo(&initial);
    auto tex = BrPixelmapAllocate(BR_PMT_INDEX_8, 4, 4, nullptr, 0);
    CHECK(tex);
    std::memset(tex->pixels, 17, 16);
    BrMapAdd(tex);
    solid = BrMaterialAllocate("solid");
    textured = BrMaterialAllocate("textured");
    CHECK(solid && textured);
    for (auto m : {solid, textured})
    {
        m->flags = BR_MATF_PRELIT | BR_MATF_GOURAUD | BR_MATF_TWO_SIDED;
        m->index_base = 11;
        m->index_range = 100;
    }
    textured->colour_map = tex;
    BrMaterialAdd(solid);
    BrMaterialAdd(textured);
    auto model = BrModelAllocate("render-test", 3, 1);
    CHECK(model);
    model->vertices[0].p = BVEC3{{BR_SCALAR(-1), BR_SCALAR(-1), 0}};
    model->vertices[1].p = BVEC3{{BR_SCALAR(1), BR_SCALAR(-1), 0}};
    model->vertices[2].p = BVEC3{{0, BR_SCALAR(1), 0}};
    for (int i = 0; i < 3; ++i)
    {
        model->vertices[i].index = 30 + i * 30;
        model->faces[0].vertices[i] = i;
    }
    BrModelAdd(model);
    auto file = ChunkyFile::PcflCreateTemp();
    CHECK(file);
    auto cache = ChunkyResourceFile::PcrfNew(file, 1 << 20);
    CHECK(cache);
    byte pixels[32 * 24];
    for (int i = 0; i < 32 * 24; ++i)
        pixels[i] = byte(1 + i % 14);
    RC view(0, 0, 32, 24);
    auto bitmap = NewMaskedBitmap(pixels, 32, 24, &view, 0, 0, 0);
    CHECK(bitmap);
    ChunkNumber rgb = 0, z = 0;
    DataBlock block;
    CHECK(file->FAdd(MaskedBitmapSize(bitmap), kctgMbmp, &rgb, &block) && WriteMaskedBitmap(bitmap, &block));
    block.Free();
    ReleaseMaskedBitmap(&bitmap);
    auto depth = NewDepthBitmap(32, 24);
    CHECK(depth);
    for (int y = 8; y < 12; ++y)
        for (int x = 12; x < 16; ++x)
            reinterpret_cast<ushort *>(static_cast<byte *>(mm_depth_owner_bitmap(depth)->pixels))[y * 32 + x] = 0;
    CHECK(WriteDepthBitmap(depth, file->WriteOwnerIndex(), kctgZbmp, &z));
    mm_depth_owner_release(depth);
    for (int mode = 0; mode < 6; ++mode)
    {
        auto w = World::PbwldNew(32, 24, mode == 1 || mode == 3, mode == 2 || mode == 3);
        CHECK(w);
        if (mode > 3)
            CHECK(w->FSetRenderScale(mode == 4 ? 2 : 4));
        CHECK(w->FSetBackground(cache->CoreCache(), kctgMbmp, rgb, kctgZbmp, z));
        BMAT34 camera;
        BrMatrix34Identity(&camera);
        camera.m[3][2] = BR_SCALAR(5);
        w->SetCamera(&camera, BR_SCALAR(1), BR_SCALAR(100), BR_ANGLE_DEG(45));
        auto root = BrActorAllocate(BR_ACTOR_NONE, nullptr);
        CHECK(root);
        root->identifier = const_cast<char *>("R");
        auto sibling = BrActorAllocate(BR_ACTOR_NONE, nullptr);
        CHECK(sibling);
        sibling->identifier = const_cast<char *>("S");
        auto actor = BrActorAllocate(BR_ACTOR_MODEL, model);
        CHECK(actor);
        actor->identifier = const_cast<char *>("M");
        actor->material = textured;
        BrActorAdd(root, actor);
        w->AddActor(root);
        w->AddActor(sibling);
        w->SetBeginRenderCallback(Begin);
        w->SetGetRcCallback(Bounds);
        w->SetActorRenderedCallback(Rendered);
        begins = rects = rendered = 0;
        for (int option = 0; option < 8; ++option)
        {
            World::SetRenderWireframe(option & 1);
            World::SetFlatShading(option & 2);
            World::SetNoTexture(option & 4);
            w->MarkDirty();
            w->Render();
            Access::State(w);
            Value(actor->render_style);
            Export(w, 0);
            for (int scale : {1, 2, 4, 16})
                Export(w, scale);
        }
        BrActorRemove(root);
        BrActorFree(root);
        BrActorRemove(sibling);
        BrActorFree(sibling);
        ReleasePpo(&w);
    }
    // No-cache export and the 4096-pixel scale clamp.
    auto cap = World::PbwldNew(640, 2);
    CHECK(cap);
    BMAT34 camera;
    BrMatrix34Identity(&camera);
    camera.m[3][2] = BR_SCALAR(5);
    cap->SetCamera(&camera, BR_SCALAR(1), BR_SCALAR(100), BR_ANGLE_DEG(45));
    Access::Seed(cap);
    cap->MarkDirty();
    cap->Render();
    Export(cap, 0);
    Export(cap, 1);
    Export(cap, 16);
    FailureCleanup(cap);
    ReleasePpo(&cap);
    ReleasePpo(&cache);
    ReleasePpo(&file);
    BrModelRemove(model);
    BrModelFree(model);
    BrMaterialRemove(solid);
    BrMaterialRemove(textured);
    BrMaterialFree(solid);
    BrMaterialFree(textured);
    BrMapRemove(tex);
    BrPixelmapFree(tex);
    World::SetRenderWireframe(false);
    World::SetFlatShading(false);
    World::SetNoTexture(false);
    World::CloseBRender();
#ifndef KAUAI_SDL3
    ReleaseDC(nullptr, vwig.hdcApp);
    vwig.hdcApp = nullptr;
#endif
    delete app;
    if (snapshot)
        CHECK(!std::fclose(snapshot));
    std::printf("World export: bytes=%u fingerprint=%08X\n", recorded, hash);
    const unsigned short one = 1;
    if (*reinterpret_cast<const byte *>(&one))
        CHECK(recorded == 13188586 && hash == 0xE235ADA9U);
}
#else
int main()
{
    std::puts("World export helpers are debug-only.");
    return 77;
}
#endif
