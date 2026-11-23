#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#include "soc.h"
#include "text_box_archive_fixture.h"
#include "scene_archive_fixture.h"
#include "scene_sound.h"
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
            std::fprintf(stderr, "scene load: %d\n", __LINE__);                                                        \
            std::exit(1);                                                                                              \
        }                                                                                                              \
    } while (0)
struct Fingerprint
{
    unsigned hash = 2166136261U, bytes = 0;

    void Add(const void *data, size_t count)
    {
        auto b = static_cast<const byte *>(data);
        for (size_t i = 0; i < count; ++i)
            hash = (hash ^ b[i]) * 16777619U;
        bytes += unsigned(count);
    }
};
static FILE *snapshot;
static Fingerprint trace, attempt, unfaulted;
static bool faultRequested;
static PChunkyResourceFile observed;
static void Record(const void *p, size_t n)
{
    trace.Add(p, n);
    attempt.Add(p, n);
    if (!faultRequested)
        unfaulted.Add(p, n);
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
struct Access : Scene
{
    static MMU32 Locks(PScene p)
    {
        return (p->*&Access::_FrameTrack)()->storage.buffers[0].locks;
    }
    static void State(PScene p)
    {
        auto &d = (p->*&Access::_owner).data;
        Value(d.currentFrame);
        Value(d.firstFrame);
        Value(d.lastFrame);
        Value(d.transition);
        Value(d.name.count);
        Record(mm_movie_title_text(&d.name), d.name.count * d.name.unit);
        Value((p->*&Access::_owner).runtime.next_event);
        Value(p->Cactr());
        Value(p->Ctbox());
        Value((p->*&Access::_owner).owned[MM_SCENE_THUMBNAIL] != nullptr);
        for (auto g : {(p->*&Access::_FrameTrack)(), (p->*&Access::_StartTrack)()})
        {
            Value(g->rows.count);
            for (long i = 0; i < g->rows.count; ++i)
            {
                auto e = SceneArchiveFixture::ReadTrackEvent(g, i);
                Value(e.nfrm);
                Value(e.sevt);
                MMI32 bytes;
                auto payload = mm_scene_track_payload(g, i, &bytes);
                Value(bytes);
                if (e.sevt == sevtPlaySnd)
                {
                    auto p = static_cast<PSceneSoundEvent>(payload);
                    Value(p->vlm);
                    Value(p->sty);
                    Value(p->fLoop);
                    Value(p->ctagc);
                    for (long j = 0; j < p->ctagc; ++j)
                    {
                        MMSceneSoundTag entry;
                        mm_scene_sound_get_tag(p, j, &entry);
                        Value(entry.child);
                        Value(entry.tag.sid);
                        Value(entry.tag.cache != nullptr);
                        Value(entry.tag.type);
                        Value(entry.tag.number);
                    }
                }
                else if (e.sevt == sevtSetBkgd)
                {
                    auto t = static_cast<PTAG>(payload);
                    Value(t->sid);
                    Value(t->cache != nullptr);
                    Value(t->ctg);
                    Value(t->cno);
                }
                else if (e.sevt == sevtAddTbox)
                {
                    PTBOX t;
                    std::memcpy(&t, payload, sizeof(t));
                    Value(t->CpMac());
                }
                else
                    Record(payload, bytes);
            }
        }
    }
};
static ChunkNumber Fixture(PChunkyFile file, int scenario, bool swap)
{
    ChunkNumber number;
    CHECK(file->FAdd(0, kctgScen, &number));
    auto d = SceneArchiveFixture::State();
    d.firstFrame = 3;
    d.lastFrame = 19;
    d.transition = transDissolve;
    String name;
    name.SetSzs(PszLit("Load fixture"));
    CHECK(mm_movie_title_set(&d.name, name.Prgch(), name.Cch()));
    CHECK(SceneArchiveFixture::Write(file, number, d));
    auto frames = GeneralGroup::PggNew(sizeof(SceneEvent)), starts = GeneralGroup::PggNew(sizeof(SceneEvent));
    CHECK(frames && starts);
    auto add = [&](PGeneralGroup g, SceneEventType kind, void *p, long bytes) {
        SceneEvent e = {3, kind};
        std::vector<byte> v(bytes);
        std::memcpy(v.data(), p, bytes);
        if (swap)
        {
            Reverse(&e, sizeof(e));
            Reverse(v.data(), bytes);
        }
        CHECK(g->FAdd(bytes, nullptr, v.data(), &e));
    };
    int32_t sound[] = {
        73, 2, 1, 2, 7, 0, 0, static_cast<int32_t>(kctgMsnd), 123, 8, 1, 0, static_cast<int32_t>(kctgMsnd), 321};
    add(frames, sevtPlaySnd, sound, scenario == 5 ? 16 : sizeof(sound));
    int32_t camera = 0x12345678;
    add(frames, sevtChngCamera, &camera, 4);
    int32_t pause[] = {witForTime, 230};
    add(frames, sevtPause, pause, 8);
    int32_t background[] = {1, 0, static_cast<int32_t>(kctgBkgd), 15};
    add(starts, sevtSetBkgd, background, 16);
    add(starts, sevtChngCamera, &camera, 4);
    if (scenario == 3 || scenario == 4)
    {
        int32_t child = 17;
        add(starts, scenario == 3 ? sevtAddActr : sevtAddTbox, &child, 4);
    }
    if (scenario == 6)
    {
        RichTextData text;
        text.properties = mm_text_collection_new(MM_TEXT_PROPERTIES, nullptr);
        text.fontName.SetSzs(PszLit("Arial"));
        text.text.pfil = FileObject::PfilCreateTemp();
        text.text.cb = 2;
        byte chars[] = {'H', 'i'};
        CHECK(text.properties && text.text.pfil && text.text.pfil->FSetFpMac(2) && text.text.FWriteRgb(chars, 2, 0));
        ChunkIdentification rich;
        CHECK(text.FWrite(file, &rich));
        ChunkNumber child;
        CHECK(file->FAddChild(kctgScen, number, 4, 0, kctgTbox, &child));
        MMTextBoxState meta = {};
        meta.first = 3;
        meta.end = 19;
        meta.story = true;
        TextBoxArchiveFixture::SetBounds(meta, 1, 2, 100, 80);
        CHECK(TextBoxArchiveFixture::Write(file, child, meta) && file->FAdoptChild(kctgTbox, child, rich.ctg, rich.cno, 0));
        int32_t id = 4;
        add(starts, sevtAddTbox, &id, 4);
    }
    auto write = [&](PGeneralGroup g, ChunkTagOrType type, ChildChunkID id) {
        ChunkNumber child;
        DataBlock block;
        CHECK(file->FAddChild(kctgScen, number, id, g->CbOnFile(), type, &child, &block) &&
              g->FWrite(&block, swap ? kboOther : kboCur));
    };
    if (scenario != 1)
        write(frames, kctgFrmGg, 0);
    if (scenario != 2)
        write(starts, kctgStartGg, 1);
    ReleasePpo(&frames);
    ReleasePpo(&starts);
    return number;
}
static bool Missing(const MissingSourceRequest &)
{
    return false;
}
int main()
{
    auto app = NewObj TestApp;
    CHECK(app);
    if (auto name = std::getenv("MM_SCENE_LOAD_SNAPSHOT"))
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

    auto file = ChunkyFile::PcflCreateTemp();
    CHECK(file);
    observed = ChunkyResourceFile::PcrfNew(file, 1 << 20);
    CHECK(observed);
#ifdef DEBUG
    unsigned allocationFailures[3] = {};
#endif
    for (int swap = 0; swap < 2; ++swap)
        for (int scenario = 0; scenario < 7; ++scenario)
        {
            auto number = Fixture(file, scenario, swap != 0);
            const bool wellFormed = scenario == 0 || scenario == 6;
            Fingerprint expected;
            for (int kind = 0; kind < 3; ++kind)
                for (int failure = 0; failure <= 22; ++failure)
                {
                    attempt = {};
                    faultRequested = failure != 0;
#ifdef DEBUG
                    vmutxMem.Enter();
                    auto &a = kind == 0 ? vdmglob.dmaglBase : kind == 1 ? vdmglob.dmaglHq : vdmglob.dmaglPv;
                    a.cactDo = failure ? failure - 1 : 0;
                    a.cactFail = failure ? 1 : 0;
                    vmutxMem.Leave();
#endif
                    auto scene = Scene::PscenRead(movie, observed->CoreCache(), number);
#ifdef DEBUG
                    vmutxMem.Enter();
                    a.cactDo = a.cactFail = 0;
                    vmutxMem.Leave();
                    if (wellFormed && faultRequested && !scene)
                        ++allocationFailures[kind];
                    const bool canFail = faultRequested;
#else
                    const bool canFail = false;
#endif
                    CHECK(scene ? wellFormed : !wellFormed || canFail);
                    Value(scene != nullptr);
                    Value(observed->CactRef());
                    if (scene)
                    {
                        Access::State(scene);
                        if (kind == 0 && failure == 0)
                            expected = attempt;
                        CHECK(attempt.bytes == expected.bytes && attempt.hash == expected.hash);
                        ReleasePpo(&scene);
                    }
                    CHECK(observed->CactRef() == 1);
                    vpers->Clear();
                }
            file->Delete(kctgScen, number);
        }
#ifdef DEBUG
    for (auto failures : allocationFailures)
        CHECK(failures > 0);
#endif
#ifdef MM_ENGINE_SCENE_LOAD_H
    // Corrected resolution-failure lock cleanup is outside the frozen trace.
    CHECK(movie->FNewScenInsCore(0));
    auto number = Fixture(file, 0, false);
    auto scene = Scene::PscenRead(movie, observed->CoreCache(), number);
    CHECK(scene);
    CHECK(!scene->FResolveAllSndTags(number) && !Access::Locks(scene));
    ReleasePpo(&scene);
    file->Delete(kctgScen, number);
    CHECK(observed->CactRef() == 1);
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
    std::printf("Scene load: bytes=%u fingerprint=%08X\n", trace.bytes, trace.hash);
    const unsigned short one = 1;
    if (*reinterpret_cast<const byte *>(&one) && sizeof(void *) == 8 && sizeof(achar) == 1)
    {
        // Allocator fault ordinals shift as the loader changes storage owners.
        CHECK(unfaulted.bytes == 2880 && unfaulted.hash == 0x5E2C69F1U);
#ifndef DEBUG
        CHECK(trace.bytes == 66240 && trace.hash == 0x8940A7C9U);
#endif
    }
}
