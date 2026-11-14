#include <cstdio>
#include <cstdlib>
#include <cstring>
#include "bren.h"
#undef size
using namespace BRender;
ASSERTNAME
void FrameMain(void)
{
}
class TestApp : public ApplicationBase
{
  public:
#ifdef DEBUG
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
            std::fprintf(stderr, "world render: %d\n", __LINE__);                                                      \
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
        Value(w->*&Access::_fWorldChanged);
        Value(begins);
        Value(rects);
        Value(rendered);
        for (auto f : {&Access::_pregnDirtyWorking, &Access::_pregnDirtyScreen})
        {
            RC r;
            Value((w->*f)->FEmpty(&r));
            Record(&r, sizeof(r));
        }
        auto rgb = w->*&Access::_pgptWorking;
        auto p = rgb->PrgbLockPixels();
        Record(p, rgb->CbRow() * (w->*&Access::_rcBuffer).Dyp());
        rgb->Unlock();
        auto z = w->*&Access::_pzbmpWorking;
        Record(z->Prgb(), z->CbRow() * (w->*&Access::_rcBuffer).Dyp());
        Materials();
    }
};
int main()
{
    auto app = NewObj TestApp;
    CHECK(app);
    if (auto path = std::getenv("THREE_MM_WORLD_RENDER_SNAPSHOT"))
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
    auto bitmap = MaskedBitmapMBMP::PmbmpNew(pixels, 32, 24, &view, 0, 0, 0);
    CHECK(bitmap);
    ChunkNumber rgb = 0, z = 0;
    DataBlock block;
    CHECK(file->FAdd(bitmap->CbOnFile(), kctgMbmp, &rgb, &block) && bitmap->FWrite(&block));
    block.Free();
    ReleasePpo(&bitmap);
    auto depth = ZBMP::PzbmpNew(32, 24);
    CHECK(depth);
    CHECK(depth->FWrite(file, kctgZbmp, &z));
    ReleasePpo(&depth);
    for (int mode = 0; mode < 6; ++mode)
    {
        auto w = World::PbwldNew(32, 24, mode == 1 || mode == 3, mode == 2 || mode == 3);
        CHECK(w);
        if (mode > 3)
            CHECK(w->FSetRenderScale(mode == 4 ? 2 : 4));
        CHECK(w->FSetBackground(cache, kctgMbmp, rgb, kctgZbmp, z));
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
            w->Render();
            Value(begins);
            Value(rects);
            Value(rendered);
        }
        w->Prerender();
        Access::State(w);
        w->Unprerender();
        w->Render();
        Access::State(w);
        auto target = GraphicsPort::PgptNewOffscreen(&view, 8);
        CHECK(target);
        auto p = target->PrgbLockPixels();
        std::memset(p, 77, target->CbRow() * 24);
        target->Unlock();
        {
            GraphicsEnvironment env(target);
            RC clip(1, 1, 4, 4);
            w->Draw(&env, &clip, 2, -1);
        }
        GraphicsPort::Flush();
        p = target->PrgbLockPixels();
        Record(p, target->CbRow() * 24);
        target->Unlock();
        ReleasePpo(&target);
        BrActorRemove(root);
        BrActorFree(root);
        BrActorRemove(sibling);
        BrActorFree(sibling);
        ReleasePpo(&w);
    }
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
    std::printf("World render: bytes=%u fingerprint=%08X\n", recorded, hash);
    const unsigned short one = 1;
    if (*reinterpret_cast<const byte *>(&one))
        CHECK(recorded == 528048 && hash == 0x4FC7CD7DU);
}
