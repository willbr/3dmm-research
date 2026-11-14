#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#include "soc.h"
#undef size
ASSERTNAME
void FrameMain(void)
{
}
class TestApp : public ApplicationBase
{
  public:
#ifdef DEBUG
    void WarnProcApp(PSZS f, long n, PSZS s) override
    {
        std::fprintf(stderr, "%s:%ld: %s\n", f, n, s);
    }
    bool FAssertProcApp(PSZS f, long n, PSZS s, void *, long) override
    {
        WarnProcApp(f, n, s);
        std::exit(1);
    }
#endif
};
class Client : public MovieClientCallbacks
{
  public:
    Client() : MovieClientCallbacks(32, 24, 0)
    {
    }
};
#define CHECK(c)                                                                                                       \
    do                                                                                                                 \
    {                                                                                                                  \
        if (!(c))                                                                                                      \
        {                                                                                                              \
            std::fprintf(stderr, "scene owner: %d\n", __LINE__);                                                       \
            std::exit(1);                                                                                              \
        }                                                                                                              \
    } while (0)
static FILE *snapshot;
static unsigned hash = 2166136261U, recorded;
static PChunkyResourceFile observed;
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
class Probe : public Actor
{
    int id;

  public:
    Probe(int value) : id(value)
    {
    }
    ~Probe() override
    {
        Value(10 + id);
        Value(observed->CactRef());
    }
};
struct SoundHeader
{
    int32_t volume, style, loop, count;
};
struct SoundTag
{
    ChildChunkID child;
    TAG tag;
};
static std::vector<byte> Sound(int count)
{
    std::vector<byte> b(sizeof(SoundHeader) + count * sizeof(SoundTag), 0);
    auto h = reinterpret_cast<SoundHeader *>(b.data());
    h->volume = 100;
    h->style = styMidi;
    h->count = count;
    auto t = reinterpret_cast<SoundTag *>(b.data() + sizeof(SoundHeader));
    for (int i = 0; i < count; ++i)
    {
        t[i].tag.sid = i % 2 ? ksidInvalid : 0;
        if (!(i % 2))
        {
            t[i].tag.pcrf = observed;
            observed->AddRef();
        }
    }
    return b;
}
struct Access : Scene
{
#ifdef THREE_MM_ENGINE_SCENE_OWNER_H
    static void FailedRead(PScene scene, int count)
    {
        auto packet = Sound(count);
        SceneEvent event = {2, sevtPlaySnd};
        auto frames = scene->*&Access::_pggsevFrm;
        CHECK(frames->FInsert(0, packet.size(), packet.data(), &event));
        auto services = _OwnerServices();
        three_mm_scene_owner_unwind(&(scene->*&Access::_owner), scene, 0, 1, &services);
    }
#endif
    static void State(PScene s)
    {
        Value((s->*&Access::_runtime).disabled);
        Value((s->*&Access::_runtime).next_event);
        auto &data = s->*&Access::_data;
        Value(data.currentFrame);
        Value(data.firstFrame);
        Value(data.lastFrame);
        Value(data.transition);
        Value(data.name.count);
        Value(data.name.unit);
        for (auto f : {&Access::_pggsevFrm, &Access::_pggsevStart})
        {
            Value((s->*f)->CbFixed());
            Value((s->*f)->IvMac());
        }
        for (auto f : {&Access::_pglpactr, &Access::_pglptbox})
        {
            Value((s->*f)->CbEntry());
            Value((s->*f)->IvMac());
        }
        Value(s->*&Access::_pglpactrSelExtra != nullptr);
        Value(s->*&Access::_pmbmp != nullptr);
        Value(s->*&Access::_psseBkgd != nullptr);
        Value(s->CactRef());
    }
    static void Populate(PScene s, int count)
    {
        auto starts = s->*&Access::_pggsevStart, frames = s->*&Access::_pggsevFrm;
        for (int i = 1; i <= 3; ++i)
        {
            auto actor = NewObj Probe(i);
            CHECK(actor);
            SceneEvent event = {i, sevtAddActr};
            PActor p = actor;
            CHECK(starts->FInsert(starts->IvMac(), sizeof(p), &p, &event));
        }
        auto extra = DynamicArray::PglNew(sizeof(PActor));
        CHECK(extra);
        byte pixels[8] = {1, 2, 3, 4, 5, 6, 7, 8};
        RC rect(0, 0, 4, 2);
        auto bitmap = MaskedBitmapMBMP::PmbmpNew(pixels, 4, 2, &rect, 0, 0, 0);
        CHECK(bitmap);
        auto frame = Sound(count);
        SceneEvent event = {2, sevtPlaySnd};
        CHECK(frames->FInsert(0, frame.size(), frame.data(), &event));
        auto background = Sound(count + 1);
        void *sound = nullptr;
        CHECK(FAllocPv(&sound, background.size(), fmemNil, mprNormal));
        std::memcpy(sound, background.data(), background.size());
#ifdef THREE_MM_ENGINE_SCENE_OWNER_H
        auto services = _OwnerServices();
        auto &owner = s->*&Access::_owner;
        three_mm_scene_owner_set(&owner, s, THREE_MM_SCENE_SELECTION, extra, &services);
        three_mm_scene_owner_set(&owner, s, THREE_MM_SCENE_THUMBNAIL, bitmap, &services);
        three_mm_scene_owner_set(&owner, s, THREE_MM_SCENE_BACKGROUND_SOUND, sound, &services);
#else
        s->*&Access::_pglpactrSelExtra = extra;
        s->*&Access::_pmbmp = bitmap;
        s->*&Access::_psseBkgd = static_cast<PSceneSoundEvent>(sound);
#endif
    }
};
static bool Missing(const MissingSourceRequest &)
{
    return false;
}
int main()
{
    auto app = NewObj TestApp;
    CHECK(app);
    if (auto name = std::getenv("THREE_MM_SCENE_OWNER_SNAPSHOT"))
    {
        snapshot = std::fopen(name, "wb");
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
    vpcex = CommandExecutionManager::PcexNew(20, 20);
    CHECK(vpcex);
    vpsndm = SoundManager::PsndmNew();
    CHECK(vpsndm);
    String path;
    path.SetSzs(PszLit("."));
    Filename root;
    CHECK(root.FBuildFromPath(&path));
    vptagm = TagManager::PtagmNew(&root, Missing, 0);
    CHECK(vptagm);
    auto client = NewObj Client;
    CHECK(client);
    auto movie = Movie::PmvieNew(false, client, nullptr, cnoNil);
    CHECK(movie);
    for (int available = 0; available < 2; ++available)
        for (int kind = 0; kind < 3; ++kind)
            for (int failure = 0; failure <= 12; ++failure)
            {
                auto commands = vpcex;
                commands->FlushCid(cidSceneLoaded);
#ifdef DEBUG
                auto base = vdmglob.dmaglBase.cv, hq = vdmglob.dmaglHq.cv, pv = vdmglob.dmaglPv.cv;
#endif
                if (!available)
                    vpcex = nullptr;
#ifdef DEBUG
                auto inject = [&](bool enabled) {
                    vmutxMem.Enter();
                    auto &a = kind == 0 ? vdmglob.dmaglBase : kind == 1 ? vdmglob.dmaglHq : vdmglob.dmaglPv;
                    a.cactDo = enabled && failure ? failure - 1 : 0;
                    a.cactFail = enabled && failure ? 1 : 0;
                    vmutxMem.Leave();
                };
                inject(true);
#endif
                auto scene = Scene::PscenNew(movie);
#ifdef DEBUG
                inject(false);
#endif
                Value(scene != nullptr);
                Value(commands->FCidIn(cidSceneLoaded));
                if (scene)
                {
                    Access::State(scene);
                    Scene::Close(&scene);
                    CHECK(!scene);
                }
                vpcex = commands;
                commands->FlushCid(cidSceneLoaded);
                vpers->Clear();
#ifdef DEBUG
                CHECK(vdmglob.dmaglBase.cv == base && vdmglob.dmaglHq.cv == hq && vdmglob.dmaglPv.cv == pv);
#endif
            }
    auto file = ChunkyFile::PcflCreateTemp();
    CHECK(file);
    observed = ChunkyResourceFile::PcrfNew(file, 1 << 20);
    CHECK(observed);
    for (int count = 0; count < 6; ++count)
        for (int retained = 0; retained < 2; ++retained)
        {
            auto scene = Scene::PscenNew(movie);
            CHECK(scene);
            Access::Populate(scene, count);
#ifdef DEBUG
            // These partial actors are destruction probes, never staged/rendered.
            SuspendAssertValid();
#endif
            Access::State(scene);
            Value(observed->CactRef());
            if (retained)
            {
                auto alias = scene;
                alias->AddRef();
                Scene::Close(&scene);
                CHECK(!scene);
                Access::State(alias);
                ReleasePpo(&alias);
            }
            else
                Scene::Close(&scene);
#ifdef DEBUG
            ResumeAssertValid();
#endif
            Value(observed->CactRef());
            CHECK(observed->CactRef() == 1);
        }
#ifdef THREE_MM_ENGINE_SCENE_OWNER_H
    // Exercise the exact read-failure service bridge, outside the frozen trace.
    for (int count = 0; count < 6; ++count)
    {
        auto scene = Scene::PscenNew(movie);
        CHECK(scene);
        Access::FailedRead(scene, count);
        CHECK(observed->CactRef() == 1);
        ReleasePpo(&scene);
    }
#endif
    ReleasePpo(&observed);
    ReleasePpo(&file);
    ReleasePpo(&movie);
    ReleasePpo(&client);
    ReleasePpo(&vptagm);
    ReleasePpo(&vpsndm);
    ReleasePpo(&vpcex);
    BRender::World::CloseBRender();
#ifndef KAUAI_SDL3
    ReleaseDC(nullptr, vwig.hdcApp);
    vwig.hdcApp = nullptr;
#endif
    delete app;
    if (snapshot)
        CHECK(!std::fclose(snapshot));
    std::printf("Scene owner: bytes=%u fingerprint=%08X\n", recorded, hash);
    const unsigned short one = 1;
    if (*reinterpret_cast<const byte *>(&one))
#ifdef DEBUG
        CHECK(recorded == 7888 && hash == 0x0EE1BC65U);
#else
        CHECK(recorded == 8688 && hash == 0x2A12D054U);
#endif
}
