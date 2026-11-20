#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#include "soc.h"
#include "scene_sound.h"
#include "scene_edit_undo.h"
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
    void SceneChange() override;
    void PauseType(WaitReason) override;
    Client() : MovieClientCallbacks(32, 24, 0)
    {
    }
};
#define CHECK(c)                                                                                                       \
    do                                                                                                                 \
    {                                                                                                                  \
        if (!(c))                                                                                                      \
        {                                                                                                              \
            std::fprintf(stderr, "scene edit: %d\n", __LINE__);                                                        \
            std::exit(1);                                                                                              \
        }                                                                                                              \
    } while (0)
static void Value(LegacyLong);
static FILE *snapshot;
static bool recording = true;
static unsigned hash = 2166136261U, recorded;
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

static void Reverse(void *p, long bytes)
{
    auto b = static_cast<byte *>(p);
    for (long i = 0; i < bytes; i += 4)
    {
        auto x = b[i];
        b[i] = b[i + 3];
        b[i + 3] = x;
        x = b[i + 1];
        b[i + 1] = b[i + 2];
        b[i + 2] = x;
    }
}
void Client::SceneChange()
{
    Value(701);
}
void Client::PauseType(WaitReason w)
{
    Value(702);
    Value(w);
}

struct Access : Scene
{
    static void OwnedSound(PScene p, PChunkyResourceFile cache)
    {
        MMSceneSoundTag tag = {0, {0, cache, kctgMsnd, 123}};
        auto services = ScenePacketServices();
        auto packet = mm_scene_sound_make(80, stySfx, false, 1, &tag, &services);
        CHECK(packet);
        auto g = (p->*&Access::_FrameTrack)();
        SceneEvent event = {1, sevtPlaySnd};
        CHECK(InsertSceneTrackEvent(g, g->rows.count, mm_scene_sound_size(packet->ctagc), packet, event));
        FreePpv(reinterpret_cast<void **>(&packet));
        (p->*&Access::_owner).runtime.next_event = g->rows.count;
    }
    static void State(PScene p)
    {
        auto &d = (p->*&Access::_owner).data;
        Value(d.currentFrame);
        Value(d.firstFrame);
        Value(d.lastFrame);
        Value(d.name.count);
        Record(mm_movie_title_text(&d.name), d.name.count * d.name.unit);
        auto &r = (p->*&Access::_owner).runtime;
        Value(r.next_event);
        Value(r.disabled);
        auto bg = p->Pbkgd();
        Value(bg != nullptr);
        if (bg)
            Value(bg->Icam());
        auto &t = (p->*&Access::_owner).background_tag;
        Value(t.sid);
        Value(t.type);
        Value(t.number);
        Value(t.cache != nullptr);
        for (auto g : {(p->*&Access::_FrameTrack)(), (p->*&Access::_StartTrack)()})
        {
            Value(g->rows.count);
            for (long i = 0; i < g->rows.count; ++i)
            {
                auto e = ReadSceneTrackEvent(g, i);
                Value(e.sevt);
                if (e.sevt != sevtSetBkgd)
                    Value(e.nfrm);
                MMI32 bytes;
                auto payload = mm_scene_track_payload(g, i, &bytes);
                Value(bytes);
                if (e.sevt == sevtSetBkgd)
                {
                    auto t = static_cast<PTAG>(payload);
                    Value(t->sid);
                    Value(t->ctg);
                    Value(t->cno);
                }
                else
                    Record(payload, bytes);
            }
        }
        Value(observed->CactRef());
        Value(vpcex->FCidIn(cidSceneLoaded));
    }
    static void Reset(PScene p)
    {
        auto g = (p->*&Access::_FrameTrack)();
        while (g->rows.count)
            CHECK(mm_group_delete(&g->rows, g->rows.count - 1) == MM_STORAGE_OK);
        (p->*&Access::_owner).runtime.next_event = 0;
        p->SetNfrmCur(1);
    }
};
static void BackgroundFixture(PChunkyFile file, ChunkNumber *number, int id)
{
    BackgroundFile header = {kboCur, koskCur, 0, 0, 0};
    CHECK(file->FAddPv(&header, sizeof(header), kctgBkgd, number));
    String name;
    name.SetSzs(id ? PszLit("Background B") : PszLit("Background A"));
    CHECK(file->FSetName(kctgBkgd, *number, &name));
    auto lights = DynamicArray::PglNew(sizeof(LightPosition));
    CHECK(lights);
    LightPosition light = {};
    BrMatrix34Identity(&light.bmat34);
    light.rIntensity = BR_SCALAR(1);
    light.lt = BR_LIGHT_DIRECT;
    CHECK(lights->FAdd(&light));
    ChunkNumber child;
    DataBlock b;
    CHECK(file->FAddChild(kctgBkgd, *number, 0, lights->CbOnFile(), kctgGllt, &child, &b) && lights->FWrite(&b));
    ReleasePpo(&lights);
    b.Free();
    byte pixels[32 * 24];
    std::memset(pixels, 17 + id, sizeof(pixels));
    RC r(0, 0, 32, 24);
    auto bitmap = MaskedBitmapMBMP::PmbmpNew(pixels, 32, 24, &r, 0, 0, 0);
    CHECK(bitmap);
    ChunkNumber rgb, z;
    CHECK(file->FAdd(bitmap->CbOnFile(), kctgMbmp, &rgb, &b) && bitmap->FWrite(&b));
    b.Free();
    ReleasePpo(&bitmap);
    auto depth = ZBMP::PzbmpNew(32, 24);
    CHECK(depth);
    std::memset(depth->Prgb(), 0xff, depth->CbRow() * 24);
    CHECK(depth->FWrite(file, kctgZbmp, &z));
    ReleasePpo(&depth);
    for (int i = 0; i < 4; ++i)
    {
        CameraPosition cam = {};
        cam.bo = kboCur;
        cam.osk = koskCur;
        cam.zrHither = BR_SCALAR(1);
        cam.zrYon = BR_SCALAR(1000);
        cam.aFov = BR_ANGLE_DEG(45);
        BrMatrix34Identity(&cam.bmat34Cam);
        cam.bmat34Cam.m[3][0] = BR_SCALAR(i);
        CHECK(file->FAddChildPv(kctgBkgd, *number, i, &cam, sizeof(cam), kctgCam, &child));
        CHECK(file->FAdoptChild(kctgCam, child, kctgMbmp, rgb, 0) && file->FAdoptChild(kctgCam, child, kctgZbmp, z, 0));
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
    if (auto name = std::getenv("MM_SCENE_EDIT_SNAPSHOT"))
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
    auto scene = movie->Pscen();
    auto file = ChunkyFile::PcflCreateTemp();
    CHECK(file);
    observed = ChunkyResourceFile::PcrfNew(file, 0);
    CHECK(observed);
    ChunkNumber a, b;
    BackgroundFixture(file, &a, 0);
    BackgroundFixture(file, &b, 1);
    TAG tags[] = {{0, observed, kctgBkgd, a}, {0, observed, kctgBkgd, b}};
    TAG old;
    CHECK(scene->FSetBkgdCore(&tags[0], &old));
    vpcex->FlushCid(cidSceneLoaded);
    movie->ClearUndo();
    Access::State(scene);
    auto pause = [&](WaitReason w, long d) {
        Value(scene->FPauseCore(&w, &d));
        Value(w);
        Value(d);
        Access::State(scene);
    };
    pause(witNil, 0);
    pause(witForTime, 13);
    pause(witUntilClick, 27);
    pause(witNil, 0);
    pause(witForTime, 39);
    auto camera = [&](long n) {
        long old = 99;
        Value(scene->FChangeCamCore(n, &old));
        Value(old);
        Access::State(scene);
    };
    camera(0);
    camera(1);
    camera(2);
    camera(0);
    scene->SetNfrmCur(3);
    camera(1);
    scene->SetNfrmCur(6);
    camera(2);
    scene->SetNfrmCur(3);
    camera(2);
    camera(1);
    for (int n = 0; n < 4; ++n)
    {
        Value(scene->FSetBkgd(&tags[n % 2]));
        Access::State(scene);
        vpcex->FlushCid(cidSceneLoaded);
        movie->ClearUndo();
    }
    Access::Reset(scene);
    Value(scene->FPause(witForTime, 66));
    Access::State(scene);
    movie->ClearUndo();
    auto undo = SceneUndoPause::PsunpNew();
    CHECK(undo);
    undo->SetPmvie(movie);
    undo->SetIscen(0);
    undo->SetNfrm(1);
    undo->SetWit(witNil);
    undo->SetDts(0);
    for (int i = 0; i < 4; ++i)
    {
        Value(undo->FDo(movie));
        Access::State(scene);
    }
    ReleasePpo(&undo);
    Access::Reset(scene);
    auto cameraUndo = SceneUndoBackground::PsunkNew();
    CHECK(cameraUndo);
    cameraUndo->SetPmvie(movie);
    cameraUndo->SetIscen(0);
    cameraUndo->SetNfrm(1);
    cameraUndo->SetIcam(2);
    cameraUndo->SetFBkgd(false);
    for (int i = 0; i < 4; ++i)
    {
        Value(cameraUndo->FDo(movie));
        Access::State(scene);
    }
    ReleasePpo(&cameraUndo);
    auto backgroundUndo = SceneUndoBackground::PsunkNew();
    CHECK(backgroundUndo);
    backgroundUndo->SetPmvie(movie);
    backgroundUndo->SetIscen(0);
    backgroundUndo->SetNfrm(1);
    backgroundUndo->SetIcam(1);
    backgroundUndo->SetFBkgd(true);
    backgroundUndo->SetTag(&tags[1]);
    for (int i = 0; i < 4; ++i)
    {
        Value(backgroundUndo->FDo(movie));
        Access::State(scene);
    }
    ReleasePpo(&backgroundUndo);
    vpcex->FlushCid(cidSceneLoaded);
    for (int kind = 0; kind < 3; ++kind)
        for (int failure = 0; failure <= 18; ++failure)
        {
            Access::Reset(scene);
            CHECK(scene->FSetBkgdCore(&tags[0], &old));
            movie->ClearUndo();
            vpcex->FlushCid(cidSceneLoaded);
#ifdef DEBUG
            vmutxMem.Enter();
            auto &alloc = kind == 0 ? vdmglob.dmaglBase : kind == 1 ? vdmglob.dmaglHq : vdmglob.dmaglPv;
            alloc.cactDo = failure ? failure - 1 : 0;
            alloc.cactFail = failure ? 1 : 0;
            vmutxMem.Leave();
#endif
            bool ok = scene->FSetBkgdCore(&tags[1], &old);
#ifdef DEBUG
            vmutxMem.Enter();
            alloc.cactDo = alloc.cactFail = 0;
            vmutxMem.Leave();
#endif
            Value(ok);
            Access::State(scene);
            vpers->Clear();
            vpcex->FlushCid(cidSceneLoaded);
        }
#ifdef MM_ENGINE_SCENE_EDIT_H
    // Resource ownership corrections are outside the frozen behavior trace.
    recording = false;
    auto references = observed->CactRef();
    CHECK(scene->FChangeCam(1));
    movie->ClearUndo();
    CHECK(observed->CactRef() == references);
    references = observed->CactRef();
    Access::OwnedSound(scene, observed);
    CHECK(observed->CactRef() == references + 1);
    CHECK(scene->FSetBkgdCore(&tags[1], &old));
    CHECK(observed->CactRef() == references);
    recording = true;
#endif
    movie->ClearUndo();
    ReleasePpo(&movie);
    ReleasePpo(&observed);
    ReleasePpo(&file);
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
    std::printf("Scene edit: bytes=%u fingerprint=%08X\n", recorded, hash);
    const unsigned short one = 1;
    if (*reinterpret_cast<const byte *>(&one) && sizeof(void *) == 8 && sizeof(achar) == 1)
#ifdef DEBUG
        CHECK(recorded == 9900 && hash == 0x116589AFU);
#else
        CHECK(recorded == 9928 && hash == 0x1DF02C29U);
#endif
}
