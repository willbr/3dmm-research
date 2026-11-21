#include <cstdio>
#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <vector>
#include "soc.h"
#include "actor_track_fixture.h"
#include "engine_action_tables_fixture.h"
#include "scene_sound.h"
#include "scene_content_undo.h"
#include "scene_chop_undo.h"
#include "scene_control_undo.h"
#include "movie_archive_fixture.h"
#include "movie_archive_host.h"
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
    void UpdateRollCall() override;
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
            std::fprintf(stderr, "scene content: %d\n", __LINE__);                                                     \
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
void Client::UpdateRollCall()
{
    Value(703);
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

class TrackTestTemplate
{
    MMTemplateState &_template;
    explicit TrackTestTemplate(PTemplate owner) : _template(EngineTemplate::State(owner)) {}

    MMActionResource *_testAction = pvNil;
    bool _failNextFetch = false;
    int _failAfterFetch = 0;

  protected:
    MMActionResource *FetchAction(MMI32 anid)
    {
        if (_failAfterFetch > 0 && --_failAfterFetch == 0)
            return pvNil;
        if (_failNextFetch)
        {
            _failNextFetch = false;
            return pvNil;
        }
        if (anid != 0 || _testAction == pvNil)
            return pvNil;
        mm_action_resource_retain(_testAction);
        return _testAction;
    }

  public:
    static TrackTestTemplate *From(PTemplate owner)
    {
        return static_cast<TrackTestTemplate *>(mm_template_owner_provider(owner)->context);
    }
    static PTemplate Create()
    {
        auto owner = mm_template_owner_create(&EngineTemplate::Host(), 0);
        if (!owner)
            return nullptr;
        auto fixture = new (std::nothrow) TrackTestTemplate(owner);
        if (!fixture)
        {
            mm_template_owner_release(owner);
            return nullptr;
        }
        *mm_template_owner_provider(owner) = {
            fixture, [](void *p, MMI32 id) { return static_cast<TrackTestTemplate *>(p)->FetchAction(id); },
            nullptr, [](void *p) { delete static_cast<TrackTestTemplate *>(p); }
        };
        return owner;
    }
    static bool FWriteFixture(PChunkyFile file, ChunkNumber *number)
    {
        auto owner = Create();
        auto tmpl = owner ? From(owner) : nullptr;
        auto cels = GeneralGroup::PggNew(sizeof(AnimationCel));
        auto matrices = DynamicArray::PglNew(sizeof(BMAT34));
        auto costumeWire = GeneralGroup::PggNew(sizeof(int32_t));
        auto indexWire = DynamicArray::PglNew(sizeof(short));
        bool ok = tmpl && tmpl->FInit() && cels && matrices && costumeWire && indexWire;
        if (ok)
        {
            String title;
            title.SetSzs(PszLit("Fixture template"));
            const auto name = mm_template_owner_name(owner);
            mm_string_value_copy(&name, title.Pst());
            ok = EngineTemplate::WriteHeader(owner, file, kctgTmpl, number);
        }
        AnimationCel cel = {chidNil, rOne};
        BMAT34 matrix;
        BrMatrix34Identity(&matrix);
        for (int i = 0; ok && i < 4; ++i)
        {
            cel.dwr = BR_SCALAR(i + 1);
            ok = cels->FInsert(i, 0, nullptr, &cel);
        }
        ok = ok && matrices->FAdd(&matrix);
        auto table = [&](ChunkTagOrType parent, ChunkNumber root, ChunkTagOrType type, auto *data) {
            ChunkNumber child;
            DataBlock block;
            return file->FAddChild(parent, root, 0, data->CbOnFile(), type, &child, &block) && data->FWrite(&block);
        };
        ChunkNumber action = 0;
        ActionChunkOnFile header = {kboCur, koskCur, factnRotateY};
        ok = ok && table(kctgTmpl, *number, kctgGlpi, indexWire) &&
             table(kctgTmpl, *number, kctgGlbs, indexWire) &&
             table(kctgTmpl, *number, kctgGgcm, costumeWire) &&
             file->FAddChild(kctgTmpl, *number, 0, sizeof(header), kctgActn, &action) &&
             file->FPutPv(&header, sizeof(header), kctgActn, action) && table(kctgActn, action, kctgGgcl, cels) &&
             table(kctgActn, action, kctgGlxf, matrices);
        ReleasePpo(&indexWire); ReleasePpo(&costumeWire); mm_template_owner_release(owner);
        ReleasePpo(&cels);
        ReleasePpo(&matrices);
        return ok;
    }
    static int FReadCostumeFixture(MMResourceCache *, MMU32, MMU32, MMOwnedBlock *, void **out, MMI32 *bytes)
    {
        *bytes = sizeof(TrackTestTemplate);
        if (!out)
            return true;
        auto owner = Create();
        auto tmpl = owner ? From(owner) : nullptr;
        *out = owner;
        if (!tmpl || !tmpl->FInit())
        {
            mm_template_owner_release(owner);
            *out = nullptr;
            return false;
        }
        tmpl->_template.custom_count = 3;
        return true;
    }
    static int FReadSubrouteFixture(MMResourceCache *cache, MMU32 type, MMU32 number,
                                    MMOwnedBlock *block, void **out, MMI32 *bytes)
    {
        if (!FReadCostumeFixture(cache, type, number, block, out, bytes))
            return false;
        auto owner = out ? static_cast<PTemplate>(*out) : nullptr;
        auto tmpl = owner ? From(owner) : nullptr;
        if (tmpl && !tmpl->FInitAction())
        {
            mm_template_owner_release(owner);
            *out = nullptr;
            return false;
        }
        return true;
    }
    void SetTestText(bool text)
    {
        _template.flags = text ? ftmplTdt : 0;
    }
    void FailAfter(int calls)
    {
        _failAfterFetch = calls;
    }
    void FailNextFetch()
    {
        _failNextFetch = true;
    }
    ~TrackTestTemplate()
    {
        mm_action_resource_release(_testAction);
    }
    bool FInit()
    {
        _template.y = BR_ANGLE_DEG(90);
        const auto allocator = CollectionAllocator();
        _template.parents = mm_part_indices_new(&allocator);
        _template.sets = mm_part_indices_new(&allocator);
        _template.costumes = mm_template_costumes_new(&allocator);
        return _template.parents && _template.sets && _template.costumes;
    }
    bool FInitAction()
    {
        PGeneralGroup cels = GeneralGroup::PggNew(sizeof(AnimationCel));
        PDynamicArray matrices = DynamicArray::PglNew(sizeof(BMAT34));
        bool ok = cels != pvNil && matrices != pvNil;
        AnimationCel cel = {chidNil, rOne};
        BMAT34 matrix;
        BrMatrix34Identity(&matrix);
        for (long index = 0; ok && index < 4; ++index)
        {
            cel.dwr = BR_SCALAR(index + 1);
            ok = cels->FInsert(index, 0, pvNil, &cel);
        }
        if (ok)
            ok = matrices->FInsert(0, &matrix);
        if (ok)
            _testAction = MakeFixtureAction(cels, matrices, factnRotateY);
        ReleasePpo(&cels);
        ReleasePpo(&matrices);
        _template.action_count = 1;
        return _testAction != pvNil;
    }
};

class ContentActor : public Actor
{
  public:
    bool PrepareBody(PScene scene)
    {
        SetPscen(scene);
        return Pbody() != nullptr;
    }
    bool FInitClipboard(PChunkyResourceFile resource, ChunkNumber number)
    {
        using namespace ActorEvent;
        auto tmpl =
            static_cast<PTemplate>(mm_resource_cache_fetch_owned_chunk(resource->CoreCache(), kctgTmpl, number, TrackTestTemplate::FReadSubrouteFixture, mm_template_owner_services()));
        if (!tmpl)
            return false;
        if (_Template())
            mm_template_owner_release(_Template());
        _owner.runtime.template_object = tmpl;
        mm_tag_close(&_owner.runtime.template_tag, &EngineActorTagServices());
        _owner.runtime.template_tag = {ksidUseCrf, resource, kctgTmpl, number};
        mm_tag_dup(&_owner.runtime.template_tag, &EngineActorTagServices());
        ActorTrack track;
        if (!track.FCreate())
            return false;
        RouteDistancePoint first = {{rZero, rZero, rZero}, BR_SCALAR(10)},
                           last = {{BR_SCALAR(10), rZero, rZero}, rZero};
        ActorEvent::Base event = {aetAdd, 1, {0, rZero, 0}};
        ActorEvent::Add add = {};
        ActorEvent::Action action = {0, 0};
        LegacyLong step = rOne;
        if (!(mm_array_add(&track.Route()->rows, &first, nullptr) == MM_STORAGE_OK) || !(mm_array_add(&track.Route()->rows, &last, nullptr) == MM_STORAGE_OK) ||
            !mm_actor_events_insert(track.Events(), 0, sizeof(add), &add, &event))
            return false;
        event.aet = aetActn;
        if (!mm_actor_events_insert(track.Events(), 1, sizeof(action), &action, &event))
            return false;
        event.aet = aetStep;
        if (!mm_actor_events_insert(track.Events(), 2, sizeof(step), &step, &event))
            return false;
        mm_actor_track_swap(&_owner.track, &track.Core());
        mm_actor_sounds_clear(&_owner.sounds);
        _owner.runtime.state = ActorFrameState();
        const auto motion = mm_actor_track_motion_services();
        mm_actor_state_rewind(&_owner.runtime.state, _owner.track.route, &motion);
        _owner.runtime.state.frame = 1;
        _owner.runtime.state.nextEvent = 3;
        _owner.runtime.state.firstEventInFrame = 0;
        _owner.runtime.state.addEvent = 0;
        _owner.runtime.state.actionEvent = 1;
        _owner.runtime.state.celCount = 4;
        _owner.runtime.state.actionFlags = factnRotateY;
        _owner.runtime.first = 1;
        _owner.runtime.last = 10;
        _owner.runtime.life_dirty = false;
        return true;
    }
};

struct MovieAccess : Movie
{
    static PChunkyResourceFile Resources(PMovie m)
    {
        return (m->*&MovieAccess::_data).Resources();
    }
};
struct Access : Scene
{
    static void ControlState(PScene p)
    {
        auto &d = (p->*&Access::_owner).data;
        Value(d.currentFrame);
        Value(d.transition);
        Value(d.name.count);
        Record(mm_movie_title_text(&d.name), d.name.count * d.name.unit);
    }
    static void ChopSound(PScene p, PChunkyResourceFile cache, bool back)
    {
        MMSceneSoundTag tag = {0, {ksidUseCrf, cache, kctgMsnd, 123}};
        auto services = ScenePacketServices();
        auto packet = mm_scene_sound_make(80, stySfx, false, 1, &tag, &services);
        CHECK(packet);
        auto events = (p->*&Access::_FrameTrack)();
        SceneEvent e = {back ? 1 : 7, sevtPlaySnd};
        CHECK(InsertSceneTrackEvent(events, back ? 0 : events->rows.count, mm_scene_sound_size(packet->ctagc), packet, e));
        FreePpv(reinterpret_cast<void **>(&packet));
        if (back)
            ++(p->*&Access::_owner).runtime.next_event;
    }
    static void ChopSetup(PScene p, bool cameras = true)
    {
        auto &d = (p->*&Access::_owner).data;
        d.firstFrame = 1;
        d.lastFrame = 40;
        d.currentFrame = 5;
        auto events = (p->*&Access::_FrameTrack)();
        while (events->rows.count)
            CHECK(mm_group_delete(&events->rows, events->rows.count - 1) == MM_STORAGE_OK);
        if (!cameras)
        {
            (p->*&Access::_owner).runtime.next_event = 0;
            return;
        }
        for (long frame : {1, 3, 5, 5, 7})
        {
            SceneEvent e = {frame, sevtChngCamera};
            long camera = 0;
            CHECK(InsertSceneTrackEvent(events, events->rows.count, sizeof(camera), &camera, e));
        }
        (p->*&Access::_owner).runtime.next_event = 4;
    }
    static void ChopState(PScene p)
    {
        State(p);
        auto events = (p->*&Access::_FrameTrack)();
        Value((p->*&Access::_owner).runtime.next_event);
        Value(events->rows.count);
        for (long i = 0; i < events->rows.count; ++i)
        {
            auto e = ReadSceneTrackEvent(events, i);
            Value(e.nfrm);
            Value(e.sevt);
        }
    }
    static MMSceneContentServices Services(PScene p)
    {
        return (p->*&Access::_ContentServices)();
    }
    static void CheckMovie(PScene p, PMovie movie)
    {
        CHECK((p->*&Access::_owner).movie == movie && p->Pmvie() == movie);
    }
    static void Extra(PScene p, PActor actor)
    {
        CHECK(mm_scene_member_append(&(p->*&Access::_owner), MM_SCENE_SELECTION, actor));
    }
    static long Extras(PScene p)
    {
        return mm_scene_member_count(&(p->*&Access::_owner), MM_SCENE_SELECTION);
    }
    static void FreshActorLists(PScene p)
    {
        mm_scene_track_dispose((p->*&Access::_StartTrack)());
        mm_scene_member_clear(&(p->*&Access::_owner), MM_SCENE_ACTORS);
    }
    static void State(PScene p)
    {
        const auto &owner = p->*&Access::_owner;
        CHECK(p->PactrSelected() == owner.selected_actor);
        CHECK(p->PtboxSelected() == owner.selected_text);
        const auto &d = owner.data;
        Value(d.currentFrame);
        Value(d.firstFrame);
        Value(d.lastFrame);
        Value(p->PactrSelected() != nullptr);
        Value(p->PtboxSelected() != nullptr);
        auto starts = (p->*&Access::_StartTrack)();
        Value(starts->rows.count);
        for (long i = 0; i < starts->rows.count; ++i)
        {
            auto e = ReadSceneTrackEvent(starts, i);
            Value(e.sevt);
            if (e.sevt == sevtAddActr)
            {
                PActor a;
                std::memcpy(&a, mm_scene_track_payload(starts, i, nullptr), sizeof(a));
                Value(a->Arid());
                Value(a->CactRef());
            }
            else if (e.sevt == sevtAddTbox)
            {
                PTBOX box;
                std::memcpy(&box, mm_scene_track_payload(starts, i, nullptr), sizeof(box));
                Value(box->CactRef());
                Value(box->Pscen() == p);
            }
        }
        Value(p->Cactr());
        for (long i = 0; i < p->Cactr(); ++i)
        {
            PActor a = p->PactrAt(i);
            Value(a->Arid());
            Value(a->CactRef());
            Value(a->Pscen() == p);
        }
        Value(p->Ctbox());
        for (long i = 0; i < p->Ctbox(); ++i)
        {
            auto box = static_cast<PTBOX>(mm_scene_member_at(&(p->*&Access::_owner), MM_SCENE_TEXTS, i));
            long first, last;
            Value(box->CactRef());
            Value(box->FGetLifetime(&first, &last));
            Value(first);
            Value(last);
        }
        Value(observed->CactRef());
    }
};
static PTBOX Caption(PScene scene, PChunkyResourceFile resources)
{
    const MovieArchiveFixture archive(resources);
    MovieArchiveHost host{archive.reader, archive.writer};
    const auto editor = host.Editor();
    RichTextData text;
    text.properties = DynamicArray::PglNew(sizeof(RichTextPropertyEntry));
    text.fontName.SetSzs(PszLit("Arial"));
    text.text.pfil = FileObject::PfilCreateTemp();
    text.text.cb = 2;
    byte chars[] = {'H', 'i'};
    CHECK(text.properties && text.text.pfil && text.text.pfil->FSetFpMac(2) && text.text.FWriteRgb(chars, 2, 0));
    ChunkIdentification rich;
    CHECK(text.FWrite(editor, &rich));
    ChunkNumber n;
    CHECK(archive.Add(0, kctgTbox, &n));
    TextBoxData data;
    data.state.first = 20;
    data.state.end = 30;
    data.state.story = true;
    data.SetBounds(1, 2, 100, 80);
    CHECK(mm_text_box_write(&data.state, n, &editor) > 0 && archive.Adopt(kctgTbox, n, rich.ctg, rich.cno, 0));
    return TextBox::PtboxRead(resources, n, scene);
}
static void BackgroundFixture(const MovieArchiveFixture &archive, ChunkNumber *number, int id)
{
    BackgroundFile header = {kboCur, koskCur, byte(id ? 0 : 20), 0, 0};
    CHECK(archive.AddBytes(&header, sizeof(header), kctgBkgd, number));
    String name;
    name.SetSzs(id ? PszLit("Background B") : PszLit("Background A"));
    CHECK(archive.SetName(kctgBkgd, *number, &name));
    auto lights = DynamicArray::PglNew(sizeof(LightPosition));
    CHECK(lights);
    LightPosition light = {};
    BrMatrix34Identity(&light.bmat34);
    light.rIntensity = BR_SCALAR(1);
    light.lt = BR_LIGHT_DIRECT;
    CHECK(lights->FAdd(&light));
    if (!id)
    {
        light.rIntensity = BR_SCALAR(0.5);
        light.bmat34.m[3][0] = BR_SCALAR(-3);
        CHECK(lights->FAdd(&light));
    }
    ChunkNumber child;
    DataBlock b;
    CHECK(archive.AddChild(kctgBkgd, *number, 0, lights->CbOnFile(), kctgGllt, &child, &b) && lights->FWrite(&b));
    ReleasePpo(&lights);
    b.Free();
    if (!id)
    {
        BackgroundDefaultSoundOnFile sound = {};
        sound.bo = kboCur;
        sound.osk = koskCur;
        sound.vlm = 12345;
        sound.fLoop = true;
        sound.tagSnd = {-1, 0x1BADB002U, kctgMsnd, 0xFEDC1234U};
        SwapBytesBom(&sound, kbomBds);
        CHECK(archive.AddChildBytes(kctgBkgd, *number, 0, &sound, sizeof(sound), kctgBds, &child));
        auto colors = DynamicArray::PglNew(sizeof(Color));
        CHECK(colors);
        Color a = {1, 2, 3, 0}, second = {4, 5, 6, 0};
        CHECK(colors->FAdd(&a) && colors->FAdd(&second));
        CHECK(archive.AddChild(kctgBkgd, *number, 0, colors->CbOnFile(), kctgColorTable, &child, &b) &&
              colors->FWrite(&b, kboOther));
        b.Free();
        ReleasePpo(&colors);
    }
    byte pixels[32 * 24];
    std::memset(pixels, 17 + id, sizeof(pixels));
    RC r(0, 0, 32, 24);
    auto bitmap = MaskedBitmapMBMP::PmbmpNew(pixels, 32, 24, &r, 0, 0, 0);
    CHECK(bitmap);
    ChunkNumber rgb, z;
    CHECK(archive.Add(bitmap->CbOnFile(), kctgMbmp, &rgb, &b) && bitmap->FWrite(&b));
    b.Free();
    ReleasePpo(&bitmap);
    auto depth = NewDepthBitmap(32, 24);
    CHECK(depth);
    CHECK(WriteDepthBitmap(depth, archive.writer, kctgZbmp, &z));
    mm_depth_owner_release(depth);
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
        std::vector<byte> camera(sizeof(cam) + (i == 1 ? 2 * sizeof(APOS) : 0));
        std::memcpy(camera.data(), &cam, sizeof(cam));
        if (i == 1)
        {
            APOS points[2] = {};
            points[0].xrPlace = BR_SCALAR(2); points[0].yrPlace = BR_SCALAR(-3); points[0].zrPlace = BR_SCALAR(4);
            points[1].xrPlace = BR_SCALAR(5); points[1].yrPlace = BR_SCALAR(6); points[1].zrPlace = BR_SCALAR(-7);
            std::memcpy(camera.data() + sizeof(cam), points, sizeof(points));
        }
        CHECK(archive.AddChildBytes(kctgBkgd, *number, i, camera.data(), camera.size(), kctgCam, &child));
        CHECK(archive.Adopt(kctgCam, child, kctgMbmp, rgb, 0) && archive.Adopt(kctgCam, child, kctgZbmp, z, 0));
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
    if (auto name = std::getenv("MM_SCENE_CONTENT_SNAPSHOT"))
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
    observed = MovieAccess::Resources(movie);
    CHECK(observed);
    observed->AddRef();
    const MovieArchiveFixture archive(observed);
    ChunkNumber background;
    /* Actual snapshot save/swap and frame chopping, captured before extraction. */
    Access::ChopSetup(scene, false);
    auto chopUndo = SceneUndoChop::PsuncNew();
    CHECK(chopUndo);
    chopUndo->SetPmvie(movie);
    chopUndo->SetIscen(0);
    chopUndo->SetNfrm(5);
    Value(chopUndo->FSave(scene));
    Value(scene->FChopCore());
    Access::ChopState(scene);
    for (int i = 0; i < 4; ++i)
    {
        Value(i % 2 ? chopUndo->FUndo(movie) : chopUndo->FDo(movie));
        scene = movie->Pscen();
        Access::ChopState(scene);
    }
    ReleasePpo(&chopUndo);
    BackgroundFixture(archive, &background, 0);
    TAG tag = {ksidUseCrf, observed, kctgBkgd, background}, old;
    CHECK(scene->FSetBkgdCore(&tag, &old));
    {
        auto background = EngineBackground::Fetch(observed, kctgBkgd, tag.cno);
        CHECK(background);
        TAG sound;
        long volume;
        bool loop;
        EngineBackground::GetDefaultSound(background, &sound, &volume, &loop);
        Value(sound.sid);
        Value(sound.pcrf != nullptr);
        Value(sound.ctg);
        Value(sound.cno);
        Value(volume);
        Value(loop);
        for (int i = 0; i < 2; ++i)
        {
            PDynamicArray colors;
            long base;
            Value(EngineBackground::FGetPalette(background, &colors, &base));
            Value(base);
            Value(colors->IvMac());
            for (long j = 0; j < colors->IvMac(); ++j)
            {
                Color color;
                colors->Get(j, &color);
                Record(&color, sizeof(color));
            }
            if (!i)
            {
                Color changed = {9, 8, 7, 0};
                colors->Put(0, &changed);
            }
            ReleasePpo(&colors);
        }
        EngineBackground::TurnOffLights(background);
        EngineBackground::TurnOnLights(background, movie->Pbwld());
        EngineBackground::SetFLeaveLitesOn(background, true);
        Value(EngineBackground::FLeaveLitesOn(background));
        EngineBackground::TurnOffLights(background);
        EngineBackground::SetFLeaveLitesOn(background, false);
        Value(EngineBackground::FLeaveLitesOn(background));
        EngineBackground::TurnOffLights(background);
        EngineBackground::TurnOffLights(background);
        EngineBackground::TurnOnLights(background, movie->Pbwld());
        EngineBackground::TurnOnLights(background, movie->Pbwld());
        for (long camera : {0, 1, 2, 1, 0})
        {
            Value(EngineBackground::FSetCamera(background, movie->Pbwld(), camera));
            Value(EngineBackground::Ccam(background));
            Value(EngineBackground::Icam(background));
            BRS x, y, z;
            EngineBackground::GetCameraPos(background, &x, &y, &z);
            Value(x);
            Value(y);
            Value(z);
            BMAT34 mouse;
            EngineBackground::GetMouseMatrix(background, &mouse);
            Record(&mouse, sizeof(mouse));
            Value(EngineBackground::BraRotYCamera(background));
            for (int i = 0; i < 5; ++i)
            {
                EngineBackground::GetActorPlacePoint(background, &x, &y, &z);
                Value(x);
                Value(y);
                Value(z);
                if (i == 1)
                    EngineBackground::ReuseActorPlacePoint(background);
            }
        }
        /* Foreign-order appended points used to be swapped past a stack header.
         * Exercise the real block/list bridge without changing the native trace. */
        ChunkNumber child;
        DataBlock block;
        CHECK(archive.Link(kctgBkgd, tag.cno, 1, kctgCam, &child));
        CHECK(archive.Find(kctgCam, child, &block));
        std::vector<byte> native(block.Cb()), foreign(block.Cb());
        CHECK(block.FReadRgb(native.data(), native.size(), 0));
        block.Free();
        CameraPosition camera;
        APOS points[2];
        CHECK(native.size() == sizeof(camera) + sizeof(points));
        std::memcpy(&camera, native.data(), sizeof(camera));
        std::memcpy(points, native.data() + sizeof(camera), sizeof(points));
        SwapBytesBom(&camera, kbomCam);
        SwapBytesRglw(&camera.bmat34Cam, 12);
        SwapBytesRglw(points, 6);
        std::memcpy(foreign.data(), &camera, sizeof(camera));
        std::memcpy(foreign.data() + sizeof(camera), points, sizeof(points));
        CHECK(archive.PutBytes(foreign.data(), foreign.size(), kctgCam, child));
        CHECK(EngineBackground::FSetCamera(background, movie->Pbwld(), 1));
        BRS x, y, z;
        EngineBackground::GetCameraPos(background, &x, &y, &z);
        CHECK(x == BR_SCALAR(1) && y == 0 && z == 0 && EngineBackground::Icam(background) == 1);
        EngineBackground::GetActorPlacePoint(background, &x, &y, &z);
        CHECK(x == 0 && y == 0 && z == 0);
        EngineBackground::GetActorPlacePoint(background, &x, &y, &z);
        CHECK(x == BR_SCALAR(2) && y == BR_SCALAR(-3) && z == BR_SCALAR(4));
        EngineBackground::ReuseActorPlacePoint(background);
        EngineBackground::GetActorPlacePoint(background, &x, &y, &z);
        CHECK(x == BR_SCALAR(2) && y == BR_SCALAR(-3) && z == BR_SCALAR(4));
        EngineBackground::GetActorPlacePoint(background, &x, &y, &z);
        CHECK(x == BR_SCALAR(5) && y == BR_SCALAR(6) && z == BR_SCALAR(-7));
        EngineBackground::GetActorPlacePoint(background, &x, &y, &z);
        CHECK(x == 0 && y == 0 && z == 0);
        CHECK(archive.PutBytes(native.data(), native.size(), kctgCam, child));
        CHECK(EngineBackground::FSetCamera(background, movie->Pbwld(), 0));
        mm_background_owner_release(background);
    }
    {
        TAG found = {ksidInvalid, nullptr, 0, 0};
        auto references = observed->CactRef();
        Value(scene->FGetTagBkgd(&found));
        Value(found.sid);
        Value(found.pcrf == observed);
        Value(found.ctg == kctgBkgd);
        Value(found.cno == background);
        CHECK(observed->CactRef() == references);
        long part = 71;
        Value(scene->PactrFromPt(-7, 13, &part) != nullptr);
        Value(part);
        for (short order : {kboCur, kboOther})
            for (int valid = 0; valid < 2; ++valid)
            {
                auto group = GeneralGroup::PggNew(sizeof(SceneEvent));
                CHECK(group);
                SceneEvent e = {7, valid ? sevtChngCamera : static_cast<SceneEventType>(99)};
                LegacyLong camera = 0;
                if (order == kboOther)
                    Reverse(&e, sizeof(e));
                CHECK(group->FAdd(sizeof(camera), nullptr, &camera, &e));
                ChunkNumber n;
                DataBlock block;
                CHECK(archive.Add(group->CbOnFile(), kctgFrmGg, &n, &block) && group->FWrite(&block, order));
                block.Free();
                CHECK(archive.Find(kctgFrmGg, n, &block));
                short out = 0x7777;
                auto loaded = SceneData::PggReadEvents(&block, &out, false);
                Value(loaded != nullptr);
                Value(out);
                CHECK((loaded != nullptr) == bool(valid));
                CHECK(out == (valid ? order : 0x7777));
                if (loaded)
                {
                    loaded->GetFixed(0, &e);
                    Record(&e, sizeof(e));
                }
                auto optional = SceneData::PggReadEvents(&block, nullptr, false);
                CHECK((optional != nullptr) == bool(valid));
                ReleasePpo(&optional);
                ReleasePpo(&loaded);
                block.Free();
                ReleasePpo(&group);
                archive.Delete(kctgFrmGg, n);
            }
        byte invalid = 0;
        ChunkNumber n;
        DataBlock block;
        CHECK(archive.AddBytes(&invalid, 1, kctgFrmGg, &n) && archive.Find(kctgFrmGg, n, &block));
        short order = 0x7777;
        auto failed = SceneData::PggReadEvents(&block, &order, false);
        CHECK(!failed && order == 0x7777);
        Value(failed != nullptr);
        Value(order);
        block.Free();
        archive.Delete(kctgFrmGg, n);
    }
    {
        String name;
        name.SetSzs(PszLit("Original #scene"));
        scene->SetNameCore(&name);
        auto title = SceneUndoTitle::PsuntNew();
        CHECK(title);
        title->SetPmvie(movie);
        title->SetIscen(0);
        title->SetNfrm(1);
        name.SetSzs(PszLit("Saved title"));
        title->SetName(&name);
        for (int i = 0; i < 4; ++i)
        {
            Value(i % 2 ? title->FUndo(movie) : title->FDo(movie));
            Access::ControlState(scene);
        }
        ReleasePpo(&title);
        auto transition = SceneUndoTransition::PsunrNew();
        CHECK(transition);
        transition->SetPmvie(movie);
        transition->SetIscen(0);
        transition->SetNfrm(1);
        transition->SetTrans(transBlack);
        scene->SetTransitionCore(transFadeToWhite);
        for (int i = 0; i < 4; ++i)
        {
            Value(i % 2 ? transition->FUndo(movie) : transition->FDo(movie));
            Access::ControlState(scene);
        }
        ReleasePpo(&transition);
        name.SetSzs(PszLit("Public title"));
        Value(scene->FSetName(&name));
        Access::ControlState(scene);
        movie->ClearUndo();
        Value(scene->FSetTransition(transDissolve));
        Access::ControlState(scene);
        movie->ClearUndo();
        SceneData metadata;
        metadata.firstFrame = 3;
        metadata.lastFrame = 9;
        metadata.SetName(&name);
        ChunkNumber number;
        MovieArchiveHost host{archive.reader, archive.writer};
        const auto editor = host.Editor();
        CHECK(mm_scene_archive_create(&editor, &metadata, &number));
        for (int i = -1; i <= transLim; ++i)
        {
            Value(Scene::FSetTransOnFile(observed, number, static_cast<TRANS>(i)));
            TRANS out = transNil;
            Value(Scene::FTransOnFile(observed, number, &out));
            Value(out);
        }
        TRANS out = transBlack;
        Value(Scene::FTransOnFile(observed, cnoNil, &out));
        Value(out);
        archive.Delete(kctgScen, number);
        Value(scene->FStartPlaying());
        scene->StopPlaying();
    }
    Access::ChopSetup(scene);
    Value(scene->FChopBack());
    Access::ChopState(scene);
    movie->ClearUndo();
    Access::ChopSetup(scene);
    Value(scene->FChop());
    Access::ChopState(scene);
    movie->ClearUndo();
    Access::ChopSetup(scene);
    scene->SetNfrmCur(1);
    ChunkNumber number;
    CHECK(archive.Add(0, kctgTmpl, &number));
    auto makeActor = [&]() {
        auto a = NewObj ContentActor;
        CHECK(a && a->FInitClipboard(observed, number) && a->PrepareBody(scene));
        return a;
    };
    for (int back = 0; back < 2; ++back)
    {
        Access::ChopSetup(scene);
        if (back)
            scene->SetNfrmCur(35);
        auto testActor = makeActor();
        CHECK(scene->FAddActrCore(testActor));
        Value(scene->FStartPlaying());
        scene->StopPlaying();
        Value(back ? scene->FChopBackCore() : scene->FChopCore());
        Access::ChopState(scene);
        if (scene->PactrFromArid(testActor->Arid()))
            scene->RemActrCore(testActor->Arid());
        ReleasePpo(&testActor);
    }
    Access::ChopSetup(scene);
    scene->SetNfrmCur(1);
    auto actor = makeActor();
    Value(scene->FAddActrCore(actor));
    Access::State(scene);
    PActor replacement = nullptr;
    CHECK(actor->FDup(&replacement, true));
    replacement->SetArid(actor->Arid());
    Value(scene->FAddActrCore(replacement));
    Access::State(scene);
    ReleasePpo(&replacement);
    auto undo = SceneActorUndo::PsunaNew();
    CHECK(undo);
    undo->SetPmvie(movie);
    undo->SetIscen(0);
    undo->SetNfrm(1);
    actor->AddRef();
    undo->SetActr(actor);
    undo->SetType(utRep);
    for (int i = 0; i < 4; ++i)
    {
        Value(i % 2 ? undo->FUndo(movie) : undo->FDo(movie));
        Access::State(scene);
    }
    ReleasePpo(&undo);
    Value(scene->FRemActr(actor->Arid()));
    Access::State(scene);
    movie->ClearUndo();
    ReleasePpo(&actor);
    actor = makeActor();
    auto addUndo = SceneActorUndo::PsunaNew();
    CHECK(addUndo);
    addUndo->SetPmvie(movie);
    addUndo->SetIscen(0);
    addUndo->SetNfrm(1);
    addUndo->SetActr(actor);
    addUndo->SetType(utAdd);
    actor->AddRef();
    for (int i = 0; i < 4; ++i)
    {
        Value(i % 2 ? addUndo->FUndo(movie) : addUndo->FDo(movie));
        Access::State(scene);
    }
    ReleasePpo(&addUndo);
    ReleasePpo(&actor);
    for (int mode = 0; mode < 2; ++mode)
    {
        auto box = Caption(scene, observed);
        CHECK(box);
        Value(mode ? scene->FAddTbox(box) : scene->FAddTboxCore(box));
        Access::State(scene);
        Value(mode ? scene->FRemTbox(box) : scene->FRemTboxCore(box));
        Access::State(scene);
        movie->ClearUndo();
        ReleasePpo(&box);
    }
    for (int kind = 0; kind < 3; ++kind)
        for (int failure = 0; failure <= 15; ++failure)
        {
            Access::FreshActorLists(scene);
            actor = makeActor();
#ifdef DEBUG
            vmutxMem.Enter();
            auto &a = kind == 0 ? vdmglob.dmaglBase : kind == 1 ? vdmglob.dmaglHq : vdmglob.dmaglPv;
            a.cactDo = failure ? failure - 1 : 0;
            a.cactFail = failure ? 1 : 0;
            vmutxMem.Leave();
#endif
            bool ok = scene->FAddActrCore(actor);
#ifdef DEBUG
            vmutxMem.Enter();
            a.cactDo = a.cactFail = 0;
            vmutxMem.Leave();
#endif
            Value(ok);
            Value(actor->CactRef());
            Value(actor->Arid());
            Value(actor->Pscen() == scene);
            Access::State(scene);
            vpers->Clear();
            if (ok)
                scene->RemActrCore(actor->Arid());
            ReleasePpo(&actor);
        }
    /* Freeze the native headless selection policy before its C89 extraction.
     * In particular, a plain select without a view keeps the borrowed extras. */
    {
        auto a = makeActor(), b = makeActor(), c = makeActor();
        CHECK(scene->FAddActrCore(a) && scene->FAddActrCore(b) && scene->FAddActrCore(c));
        String name;
        name.SetSzs(PszLit("A #Team #first"));
        CHECK(movie->FNameActr(a->Arid(), &name));
        name.SetSzs(PszLit("B #TEAM #second"));
        CHECK(movie->FNameActr(b->Arid(), &name));
        name.SetSzs(PszLit("C #other"));
        CHECK(movie->FNameActr(c->Arid(), &name));
        auto state = [&]() {
            Value(scene->CactrSelected());
            for (long i = 0; i < scene->CactrSelected(); ++i)
            {
                auto selected = scene->PactrSelectedAt(i);
                Value(selected == a ? 1 : selected == b ? 2 : selected == c ? 3 : 0);
            }
            Value(scene->FIsActrSelected(a));
            Value(scene->FIsActrSelected(b));
            Value(scene->FIsActrSelected(c));
            Value(scene->FIsActrSelected(nullptr));
            BRS x = 17, y = 23, z = 31;
            Value(scene->FXyzSelectionCentroid(&x, &y, &z));
            Value(x);
            Value(y);
            Value(z);
        };
        scene->SelectActr(a);
        state();
        Value(scene->FToggleActrSelected(b));
        state();
        Value(scene->FToggleActrSelected(c));
        state();
        Value(scene->FToggleActrSelected(a));
        state();
        Value(scene->FToggleActrSelected(c));
        state();
        scene->ClearSelection();
        state();
        Value(scene->FSelectAllActrs());
        state();
        Value(scene->FSelectActrsByTag(PszLit("team")));
        state();
        Value(scene->FSelectActrsSharingTagsWith(a));
        state();
        Value(scene->FSelectActrsByTag(PszLit("missing")));
        state();
        scene->SelectTbox(nullptr);
        state();
        scene->RemActrCore(a->Arid());
        scene->RemActrCore(b->Arid());
        scene->RemActrCore(c->Arid());
        ReleasePpo(&a);
        ReleasePpo(&b);
        ReleasePpo(&c);
    }
#ifdef MM_ENGINE_SCENE_CONTENT_H
    recording = false;
    {
        const auto old = scene->Trans();
        scene->SetTransitionCore(transFadeToWhite);
        CHECK(scene->FSetTransition(transBlack));
        CHECK(movie->FUndo() && scene->Trans() == transFadeToWhite);
        CHECK(movie->FRedo() && scene->Trans() == transBlack);
        movie->ClearUndo();
        scene->SetTransitionCore(old);
    }
    for (int back = 0; back < 2; ++back)
    {
        Access::ChopSetup(scene);
        auto references = observed->CactRef();
        Access::ChopSound(scene, observed, back != 0);
        CHECK(observed->CactRef() == references + 1);
        CHECK(back ? scene->FChopBackCore() : scene->FChopCore());
        CHECK(observed->CactRef() == references);
    }
    Access::ChopSetup(scene);
    scene->SetNfrmCur(1);
    auto unprepared = NewObj ContentActor;
    CHECK(unprepared && unprepared->FInitClipboard(observed, number));
#ifdef DEBUG
    vmutxMem.Enter();
    vdmglob.dmaglBase.cactDo = 0;
    vdmglob.dmaglBase.cactFail = 1;
    vmutxMem.Leave();
    bool accepted = scene->FAddActrCore(unprepared);
    vmutxMem.Enter();
    vdmglob.dmaglBase.cactDo = vdmglob.dmaglBase.cactFail = 0;
    vmutxMem.Leave();
    CHECK(!accepted && !unprepared->Pbody() && unprepared->CactRef() == 1);
#endif
    ReleasePpo(&unprepared);
    auto box = Caption(scene, observed);
    CHECK(box);
    auto services = Access::Services(scene);
    services.events.seek = [](void *, int, void *, MMI32) -> int { return 0; };
    CHECK(!mm_scene_text_add(box, &services) && box->CactRef() == 1);
    ReleasePpo(&box);
    auto a = makeActor();
    CHECK(scene->FAddActrCore(a));
    scene->SelectActr(a);
    Access::Extra(scene, a);
    PActor b = nullptr;
    CHECK(a->FDup(&b, true));
    b->SetArid(a->Arid());
    CHECK(scene->FAddActrCore(b) && Access::Extras(scene) == 0);
    scene->RemActrCore(b->Arid());
    ReleasePpo(&a);
    ReleasePpo(&b);
    auto other = Movie::PmvieNew(false, client, nullptr, cnoNil);
    CHECK(other);
    scene->SetMvie(other);
    Access::CheckMovie(scene, other);
    scene->SetMvie(movie);
    ReleasePpo(&other);
    recording = true;
#endif
    {
        TAG absent = {91, observed, kctgMsnd, 17};
        Value(scene->FGetTagBkgd(&absent));
        Value(absent.sid);
        Value(absent.pcrf == observed);
        Value(absent.ctg == kctgMsnd);
        Value(absent.cno);
        movie->DocumentBase::SetDirty(false);
        movie->SetDirty(false);
        scene->MarkDirty(false);
        Value(movie->FDirty());
        scene->MarkDirty();
        Value(movie->FDirty());
        Value(scene->PmbmpThumbnail() != nullptr);
        auto colors = DynamicArray::PglNew(sizeof(Color));
        CHECK(colors);
        for (int i = 0; i < 256; ++i)
        {
            Color c = {byte(i), byte(i), byte(i), 0};
            CHECK(colors->FAdd(&c));
        }
        movie->SetThumbPalette(colors);
        ReleasePpo(&colors);
        RC bounds(0, 0, 32, 24);
        GraphicsObjectBlock block(7201, nullptr, fgobNil, ginNil, &bounds);
        auto view = MovieView::PmvuNew(movie, &block, 32, 24);
        CHECK(view);
        movie->SetDirty();
        auto thumbnail = scene->PmbmpThumbnail();
        Value(thumbnail != nullptr);
        CHECK(thumbnail);
        std::vector<byte> pixels(144 * 81, 0);
        thumbnail->Draw(pixels.data(), 144, 81, 0, 0);
        Record(pixels.data(), pixels.size());
        Value(thumbnail->CbOnFile());
        Access::ControlState(scene);
        Value(scene->GrfScen());
        thumbnail->AddRef();
        Value(thumbnail->CactRef());
        movie->DocumentBase::SetDirty(false);
        movie->SetDirty(false);
        Value(scene->PmbmpThumbnail() == thumbnail);
        CHECK(scene->FGotoFrm(5));
        movie->SetDirty();
        auto replacement = scene->PmbmpThumbnail();
        CHECK(replacement);
        Value(thumbnail->CactRef());
        ReleasePpo(&thumbnail);
        std::fill(pixels.begin(), pixels.end(), 0);
        replacement->Draw(pixels.data(), 144, 81, 0, 0);
        Record(pixels.data(), pixels.size());
        Value(replacement->CbOnFile());
        Access::ControlState(scene);
        Value(scene->GrfScen());
        ReleasePpo(&view);
    }
    movie->ClearUndo();
    ReleasePpo(&movie);
    ReleasePpo(&observed);
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
    std::printf("Scene content: bytes=%u fingerprint=%08X\n", recorded, hash);
    const unsigned short one = 1;
    if (*reinterpret_cast<const byte *>(&one) && sizeof(void *) == 8 && sizeof(achar) == 1)
        CHECK(recorded == 31048 && hash == 0x5E56C170U);
}
