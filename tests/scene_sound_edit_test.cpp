#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#include "soc.h"
#include "scene_sound.h"
#include "scene_sound_undo.h"
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
static void Value(LegacyLong);
class Client : public MovieClientCallbacks
{
  public:
    void SetSndFrame(bool value) override
    {
        Value(901);
        Value(value);
    }
    Client() : MovieClientCallbacks(32, 24, 0)
    {
    }
};
#define CHECK(c)                                                                                                       \
    do                                                                                                                 \
    {                                                                                                                  \
        if (!(c))                                                                                                      \
        {                                                                                                              \
            std::fprintf(stderr, "scene sound edit: %d\n", __LINE__);                                                  \
            std::exit(1);                                                                                              \
        }                                                                                                              \
    } while (0)
static FILE *snapshot;
static unsigned hash = 2166136261U, recorded;
static bool recording = true;
static bool put_called;
static PChunkyResourceFile observed;
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

struct MovieAccess : Movie
{
    static PChunkyResourceFile Cache(PMovie p)
    {
        return (p->*&MovieAccess::_data).Resources();
    }
};
struct Access : Scene
{
#ifdef MM_ENGINE_SCENE_SOUND_EDIT_H
    static void FailPut(PScene scene, PTAG tag)
    {
        auto services = (scene->*&Access::_SoundEditServices)();
        auto track = (scene->*&Access::_FrameTrack)();
        auto &allocator = track->storage.allocator;
        auto resize = allocator.resize;
        CHECK(mm_storage_resize_exact(&track->storage, 0, track->rows.bytes, MM_STORAGE_NORMAL) == MM_STORAGE_OK);
        allocator.resize = [](void *, void *, MMU32, MMU32, int) -> void * {
            put_called = true;
            return nullptr;
        };
        put_called = false;
        bool result = mm_scene_sound_add(0, 1, 29, stySfx, 1, tag, &services) != 0;
        allocator.resize = resize;
        CHECK(!result && put_called);
    }
    static long Count(PScene p)
    {
        return (p->*&Access::_FrameTrack)()->rows.count;
    }
#endif
    static void State(PScene p)
    {
        auto &data = p->*&Access::_data;
        auto &r = p->*&Access::_runtime;
        auto group = (p->*&Access::_FrameTrack)();
        Value(data.currentFrame);
        Value(data.firstFrame);
        Value(data.lastFrame);
        Value(r.next_event);
        Value(r.disabled);
        Value(r.background_frame);
        Value(group->rows.count);
        Value(observed->CactRef());
        for (int i = 0; i < group->rows.count; ++i)
        {
            auto e = ReadSceneTrackEvent(group, i);
            Value(e.nfrm);
            Value(e.sevt);
            if (e.sevt != sevtPlaySnd)
                continue;
            Packet(reinterpret_cast<PSceneSoundEvent>(mm_scene_track_payload(group, i, nullptr)));
        }
        auto background = p->*&Access::_psseBkgd;
        Value(background != nullptr);
        if (background)
            Packet(background);
    }
    static void Packet(PSceneSoundEvent p)
    {
        Value(p->vlm);
        Value(p->sty);
        Value(p->fLoop);
        Value(p->ctagc);
        for (int i = 0; i < p->ctagc; ++i)
        {
            auto tag = p->Ptag(i);
            Value(tag->sid);
            Value(tag->ctg);
            Value(tag->cno);
            Value(tag->pcrf != nullptr);
            Value(tag->sid == 0 ? *p->Pchid(i) : 0); // Ignore undefined unused built-in child words.
        }
    }
};
struct UndoAccess : SceneUndoSound
{
    static void State(PSceneUndoSound p)
    {
#ifdef MM_ENGINE_SCENE_SOUND_EDIT_H
        auto &state = p->*&UndoAccess::_sound;
        Value(state.style);
        Value(state.sound != nullptr);
        if (state.sound)
            Access::Packet(static_cast<PSceneSoundEvent>(state.sound));
#else
        Value(p->*&UndoAccess::_sty);
        auto sound = p->*&UndoAccess::_psse;
        Value(sound != nullptr);
        if (sound)
            Access::Packet(sound);
#endif
    }
};
static void Query(PScene scene, int style)
{
    bool found = false;
    auto sentinel = reinterpret_cast<PSceneSoundEvent>(uintptr_t(1));
    auto p = sentinel;
    Value(scene->FGetSnd(style, &found, &p));
    Value(found);
    Value(p == sentinel);
    if (found)
    {
        Access::Packet(p);
        ReleasePpsse(&p);
    }
    PDynamicArray tags = nullptr;
    long volume = -17;
    bool loop = true;
    Value(scene->FQuerySnd(style, &tags, &volume, &loop));
    Value(tags != nullptr);
    Value(volume);
    Value(loop);
    if (tags)
    {
        Value(tags->IvMac());
        ReleasePpo(&tags);
    }
}
static bool Missing(const MissingSourceRequest &)
{
    return false;
}
int main()
{
    auto app = NewObj TestApp;
    CHECK(app);
    if (auto name = std::getenv("MM_SCENE_SOUND_EDIT_SNAPSHOT"))
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

    CHECK(movie->FNewScenInsCore(0));
    observed = MovieAccess::Cache(movie);
    CHECK(observed);
    observed->AddRef();
    ChunkNumber number;
    CHECK(observed->Pcfl()->FAdd(0, kctgMsnd, &number));
    auto scene = movie->Pscen();
    CHECK(scene);
    TAG tag = {0, observed, kctgMsnd, number};
    for (int style : {int(stySfx), int(stySpeech), int(styMidi)})
    {
        Query(scene, style);
        Value(scene->FAddSndCore(false, false, 83, style, 1, &tag));
        Access::State(scene);
        vpers->Clear();
        Value(scene->FAddSndCore(false, true, 29, style, 1, &tag));
        Access::State(scene);
        vpers->Clear();
        scene->SetSndVlmCore(style, 67);
        Access::State(scene);
        Query(scene, style);
        Value(scene->FAddSnd(&tag, true, false, 56, style));
        Access::State(scene);
        vpers->Clear();
        scene->RemSndCore(style);
        Access::State(scene);
        movie->ClearUndo();
    }
    for (int failure = 0; failure < 7; ++failure)
    {
        movie->ClearUndo();
#ifdef DEBUG
        vmutxMem.Enter();
        vdmglob.dmaglPv.cactDo = failure ? failure - 1 : 0;
        vdmglob.dmaglPv.cactFail = failure ? 1 : 0;
        vmutxMem.Leave();
#endif
        bool ok = scene->FAddSndCore(false, false, 44, styMidi, 1, &tag);
#ifdef DEBUG
        vmutxMem.Enter();
        vdmglob.dmaglPv.cactDo = vdmglob.dmaglPv.cactFail = 0;
        vmutxMem.Leave();
#endif
        Value(ok);
        Access::State(scene);
        vpers->Clear();
    }
    Value(scene->FRemSnd(styMidi));
    Access::State(scene);
    movie->ClearUndo();
    for (int preset : {0, 1})
    {
        CHECK(scene->FAddSndCore(false, false, 31, stySfx, 1, &tag));
        vpers->Clear();
        auto undo = SceneUndoSound::PsunsNew();
        CHECK(undo);
        undo->SetPmvie(movie);
        undo->SetIscen(0);
        undo->SetNfrm(1);
        undo->SetSty(stySfx);
        if (preset)
        {
            bool found;
            PSceneSoundEvent old = nullptr;
            CHECK(scene->FGetSnd(stySfx, &found, &old) && found && undo->FSetSnd(old));
            ReleasePpsse(&old);
            CHECK(scene->FAddSndCore(false, false, 72, stySfx, 1, &tag));
            vpers->Clear();
        }
        for (int repeat = 0; repeat < 4; ++repeat)
        {
            Value(undo->FDo(movie));
            Access::State(scene);
            UndoAccess::State(undo);
            vpers->Clear();
        }
        ReleasePpo(&undo);
        scene->RemSndCore(stySfx);
        vpers->Clear();
    }
#ifdef MM_ENGINE_SCENE_SOUND_EDIT_H
    // Corrected failure paths are outside the frozen native behavior trace.
    recording = false;
    CHECK(scene->FAddSndCore(false, false, 31, stySfx, 1, &tag));
    vpers->Clear();
    auto references = observed->CactRef();
    Access::FailPut(scene, &tag);
    CHECK(observed->CactRef() == references && Access::Count(scene) == 1);
#ifdef DEBUG
    auto failedUndo = SceneUndoSound::PsunsNew();
    CHECK(failedUndo);
    failedUndo->SetPmvie(movie);
    failedUndo->SetIscen(0);
    failedUndo->SetNfrm(1);
    failedUndo->SetSty(stySfx);
    vmutxMem.Enter();
    vdmglob.dmaglPv.cactDo = 1;
    vdmglob.dmaglPv.cactFail = 1;
    vmutxMem.Leave();
    bool undone = failedUndo->FDo(movie);
    vmutxMem.Enter();
    vdmglob.dmaglPv.cactDo = vdmglob.dmaglPv.cactFail = 0;
    vmutxMem.Leave();
    CHECK(!undone && observed->CactRef() == references && Access::Count(scene) == 1);
    ReleasePpo(&failedUndo);
    vpers->Clear();
#endif
    CHECK(scene->FGotoFrm(2));
    CHECK(scene->FAddSndCore(false, false, 64, stySfx, 1, &tag));
    vpers->Clear();
    CHECK(Access::Count(scene) == 2 && scene->FRemSnd(stySfx) && Access::Count(scene) == 1);
    movie->ClearUndo();
    CHECK(scene->FGotoFrm(1));
    scene->RemSndCore(stySfx);
    vpers->Clear();
#endif
    ReleasePpo(&observed);
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

    std::printf("Scene sound edit: bytes=%u fingerprint=%08X\n", recorded, hash);
    const unsigned short endian = 1;
    if (*reinterpret_cast<const byte *>(&endian))
    {
#ifdef DEBUG
        CHECK(recorded == 3796 && hash == 0xD3F41FF2U);
#else
        CHECK(recorded == 3812 && hash == 0x52AE7716U);
#endif
    }
}
