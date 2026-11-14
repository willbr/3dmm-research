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
#define CHECK(c)                                                                                                       \
    do                                                                                                                 \
    {                                                                                                                  \
        if (!(c))                                                                                                      \
        {                                                                                                              \
            std::fprintf(stderr, "scene events: %d\n", __LINE__);                                                      \
            std::exit(1);                                                                                              \
        }                                                                                                              \
    } while (0)
static FILE *snapshot;
static unsigned hash = 2166136261U, recorded;
static bool recording = true;
static void Record(const void *p, size_t n)
{
    if (!recording)
        return;
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
class Client : public MovieClientCallbacks
{
  public:
    Client() : MovieClientCallbacks(32, 24, 0)
    {
    }
    void PauseType(WaitReason reason) override
    {
        Value(100);
        Value(reason);
    }
    void SetSndFrame(bool sound) override
    {
        Value(101);
        Value(sound);
    }
};

struct Header
{
    int32_t volume, style, loop, count;
};
struct Access : Scene
{
    static bool PauseWithoutFrame(PScene s)
    {
        SceneEvent event;
        event.sevt = sevtPause; // Match native callers that leave irrelevant fields unset.
        int32_t pause[2] = {witForTime, 3};
        return (s->*&Access::_FPlaySev)(&event, pause, 0);
    }
    static bool Play(PScene s, int kind, long frame, void *payload, unsigned mask)
    {
        SceneEvent event = {int32_t(frame), SceneEventType(kind)};
        return (s->*&Access::_FPlaySev)(&event, payload, mask);
    }
    static bool Unplay(PScene s, int kind)
    {
        SceneEvent event = {0, SceneEventType(kind)};
        return (s->*&Access::_FUnPlaySev)(&event, nullptr);
    }
    static void Populate(PScene s)
    {
        auto frames = s->*&Access::_pggsevFrm;
        const int times[] = {1, 1, 3, 4, 4, 7};
        for (int i = 0; i < 6; ++i)
        {
            SceneEvent event = {times[i], i == 1 || i == 3 || i == 5 ? sevtPlaySnd : sevtPause};
            Header sound = {100 - i * 5, i == 5 ? stySfx : styMidi, 0, 0};
            int32_t pause[2] = {i == 2 ? witUntilClick : witForTime, i + 1};
            CHECK(frames->FInsert(frames->IvMac(), event.sevt == sevtPlaySnd ? sizeof(sound) : sizeof(pause),
                                  event.sevt == sevtPlaySnd ? static_cast<void *>(&sound) : pause, &event));
        }
    }
    static void State(PScene s)
    {
        auto &d = s->*&Access::_data;
        Value(d.currentFrame);
        Value(d.firstFrame);
        Value(d.lastFrame);
#ifdef THREE_MM_ENGINE_SCENE_RUNTIME_H
        auto &r = s->*&Access::_runtime;
        Value(r.next_event);
        Value(r.disabled);
        Value(r.background_frame);
#else
        Value(s->*&Access::_isevFrmLim);
        Value(s->*&Access::_grfscen);
        Value(s->*&Access::_nfrmSseBkgd);
#endif
        auto sound = s->*&Access::_psseBkgd;
        Value(sound != nullptr);
        if (sound)
            Record(sound, sizeof(Header));
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
    if (auto name = std::getenv("THREE_MM_SCENE_EVENTS_SNAPSHOT"))
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

    for (unsigned mask : {unsigned(fscenPauses), unsigned(fscenAll)})
    {
        auto scene = Scene::PscenNew(movie);
        CHECK(scene);
        Access::Populate(scene);
        scene->Disable(mask);
        for (long frame : {0, 1, 3, 5, 4, 1, -2, 7, 8, 1})
        {
            Value(frame);
            Value(scene->FGotoFrm(frame));
            Access::State(scene);
            for (unsigned flags : {0U, unsigned(fscenSounds), unsigned(fscenActrs), unsigned(fscenAll)})
            {
                Value(flags);
                Value(scene->FReplayFrm(flags));
                Access::State(scene);
            }
        }
        ReleasePpo(&scene);
    }
    for (unsigned mask : {0U, unsigned(fscenAll)})
    {
        auto scene = Scene::PscenNew(movie);
        CHECK(scene);
        Header sound = {91, styMidi, 0, 0};
        Value(Access::Play(scene, sevtPlaySnd, 5, &sound, mask));
        Access::State(scene);
        for (int failure = 0; failure < 3; ++failure)
        {
            sound.volume = 42 + failure;
#ifdef DEBUG
            vmutxMem.Enter();
            vdmglob.dmaglPv.cactDo = failure ? failure - 1 : 0;
            vdmglob.dmaglPv.cactFail = failure ? 1 : 0;
            vmutxMem.Leave();
#endif
            bool result = Access::Play(scene, sevtPlaySnd, 9, &sound, mask);
#ifdef DEBUG
            vmutxMem.Enter();
            vdmglob.dmaglPv.cactDo = vdmglob.dmaglPv.cactFail = 0;
            vmutxMem.Leave();
#endif
            Value(result);
            Access::State(scene);
            vpers->Clear();
        }
        sound.style = stySfx;
        Value(Access::Play(scene, sevtPlaySnd, 12, &sound, mask));
        Access::State(scene);
        for (int32_t reason : {0, 1, 2, 3})
        {
            int32_t pause[2] = {reason, -7};
            Value(Access::Play(scene, sevtPause, 12, pause, mask));
            Access::State(scene);
        }
        for (int kind : {int(sevtPlaySnd), int(sevtPause), int(sevtAddActr), int(sevtAddTbox), int(sevtSetBkgd)})
        {
            Value(Access::Unplay(scene, kind));
            Access::State(scene);
        }
        Value(scene->FPlayStartEvents(false));
        Value(scene->FPlayStartEvents(true));
        ReleasePpo(&scene);
    }
    recording = false;
    auto supplemental = Scene::PscenNew(movie);
    CHECK(supplemental && Access::PauseWithoutFrame(supplemental));
    ReleasePpo(&supplemental);
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

    std::printf("Scene events: bytes=%u fingerprint=%08X\n", recorded, hash);
    const unsigned short one = 1;
    if (*reinterpret_cast<const byte *>(&one))
    {
        CHECK(recorded == 6928);
#ifdef DEBUG
        CHECK(hash == 0x2B9558EDU);
#else
        CHECK(hash == 0x17EBE56DU);
#endif
    }
}
