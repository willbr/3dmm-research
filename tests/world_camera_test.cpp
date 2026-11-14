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
            std::fprintf(stderr, "world camera: %d\n", __LINE__);                                                      \
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
static void Rendered(PBACT actor, RC *rect)
{
    Value(actor->identifier[0]);
    Record(rect, sizeof(*rect));
}
struct Access : World
{
    static void Dirty(PWorld w)
    {
        Value(w->*&Access::_fWorldChanged);
        RC r;
        Value((w->*&Access::_pregnDirtyWorking)->FEmpty(&r));
        Record(&r, sizeof(r));
    }
    static void Clear(PWorld w)
    {
        w->*&Access::_fWorldChanged = false;
        (w->*&Access::_pregnDirtyWorking)->SetRc(nullptr);
    }
    static void Origin(PWorld w, int x, int y)
    {
        (w->*&Access::_bpmpRGB).origin_x = x;
        (w->*&Access::_bpmpRGB).origin_y = y;
    }
    static void Begin(PWorld w)
    {
#ifdef THREE_MM_ENGINE_WORLD_CAMERA_H
        three_mm_world_pick_begin(&(w->*&Access::_camera));
#endif
        w->*&Access::_pbactClosestClicked = nullptr;
        w->*&Access::_dzpClosestClicked = BR_SCALAR_MAX;
    }
    static void Filter(PWorld w, PBACT a, BRS depth)
    {
        BVEC3 zero = {};
        Value(_FFilter(a, nullptr, nullptr, &zero, &zero, depth, depth, w));
        auto picked = w->*&Access::_pbactClosestClicked;
        Value(picked ? picked->identifier[0] : 0);
        Value(w->*&Access::_dzpClosestClicked);
    }
    static void Bounds(PBACT actor, br_int_32 bounds[4])
    {
        _ActorRendered(actor, nullptr, nullptr, 0, nullptr, bounds);
    }
};
int main()
{
    auto app = NewObj TestApp;
    CHECK(app);
    if (auto path = std::getenv("THREE_MM_WORLD_CAMERA_SNAPSHOT"))
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
    auto model = BrModelAllocate("camera-test", 3, 1);
    CHECK(model);
    model->vertices[0].p = BVEC3{{BR_SCALAR(-1), BR_SCALAR(-1), 0}};
    model->vertices[1].p = BVEC3{{BR_SCALAR(1), BR_SCALAR(-1), 0}};
    model->vertices[2].p = BVEC3{{0, BR_SCALAR(1), 0}};
    model->faces[0].vertices[0] = 0;
    model->faces[0].vertices[1] = 1;
    model->faces[0].vertices[2] = 2;
    BrModelAdd(model);
    for (int mode = 0; mode < 6; ++mode)
    {
        auto w = World::PbwldNew(32, 24, mode == 1 || mode == 3, mode == 2 || mode == 3);
        CHECK(w);
        if (mode > 3)
            CHECK(w->FSetRenderScale(mode == 4 ? 2 : 4));
        BMAT34 matrix;
        BRS nearZ, farZ;
        BRA fov;
        w->GetCamera(&matrix, &nearZ, &farZ, &fov);
        Record(&matrix, sizeof(matrix));
        Value(nearZ);
        Value(farZ);
        Value(fov);
        for (int camera = 0; camera < 3; ++camera)
        {
            BrMatrix34Identity(&matrix);
            matrix.m[3][2] = BR_SCALAR(5 + camera);
            Access::Clear(w);
            w->SetCamera(&matrix, BR_SCALAR(1), BR_SCALAR(100), BR_ANGLE_DEG(45 + camera * 10));
            Access::Dirty(w);
            for (int mask = 0; mask < 8; ++mask)
            {
                BMAT34 read;
                nearZ = farZ = -123;
                fov = 123;
                w->GetCamera(&read, mask & 1 ? &nearZ : nullptr, mask & 2 ? &farZ : nullptr, mask & 4 ? &fov : nullptr);
                Record(&read, sizeof(read));
                Value(nearZ);
                Value(farZ);
                Value(fov);
            }
        }
        auto root = BrActorAllocate(BR_ACTOR_NONE, nullptr);
        CHECK(root);
        root->identifier = const_cast<char *>("R");
        auto a = BrActorAllocate(BR_ACTOR_MODEL, model);
        CHECK(a);
        a->identifier = const_cast<char *>("A");
        auto b = BrActorAllocate(BR_ACTOR_MODEL, model);
        CHECK(b);
        b->identifier = const_cast<char *>("B");
        BrActorAdd(root, a);
        BrActorAdd(root, b);
        w->AddActor(root);
        Access::Begin(w);
        Access::Filter(w, a, BR_SCALAR_MAX);
        Access::Filter(w, a, BR_SCALAR(2));
        Access::Filter(w, b, BR_SCALAR(2));
        Access::Filter(w, b, -1);
        w->SetActorRenderedCallback(Rendered);
        br_int_32 bounds[][4] = {{-5, -3, 8, 7}, {0, 0, 0, 0}, {-8, -8, -3, -3}, {3, 7, 30, 23}};
        for (auto &r : bounds)
            Access::Bounds(a, r);
        w->SetActorRenderedCallback(nullptr);
        Access::Bounds(a, bounds[0]);
        for (int origin = 0; origin < 2; ++origin)
        {
            Access::Origin(w, origin ? 3 : 0, origin ? -2 : 0);
            for (int y = -4; y < 29; y += 2)
                for (int x = -4; x < 37; x += 2)
                {
                    PBACT picked = nullptr;
                    bool hit = w->FClickedActor(x, y, &picked);
                    Value(hit);
                    Value(hit ? picked->identifier[0] : 0);
                }
        }
        BrActorRemove(root);
        BrActorFree(root);
        ReleasePpo(&w);
    }
    BrModelRemove(model);
    BrModelFree(model);
    World::CloseBRender();
#ifndef KAUAI_SDL3
    ReleaseDC(nullptr, vwig.hdcApp);
    vwig.hdcApp = nullptr;
#endif
    delete app;
    if (snapshot)
        CHECK(!std::fclose(snapshot));
    std::printf("World camera: bytes=%u fingerprint=%08X\n", recorded, hash);
    const unsigned short one = 1;
    if (*reinterpret_cast<const byte *>(&one))
        CHECK(recorded == 44472 && hash == 0xAD744449U);
}
