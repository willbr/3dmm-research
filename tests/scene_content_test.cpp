#include <cstdio>
#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <vector>
#include "soc.h"
#include "scene_sound.h"
#include "scene_content_undo.h"
#include "scene_chop_undo.h"
#include "scene_control_undo.h"
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

class TrackTestTemplate : public Template
{
    PActionDefinition _testAction = pvNil;
    bool _failNextFetch = false;
    int _failAfterFetch = 0;

  protected:
    PActionDefinition _PactnFetch(long anid) override
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
        _testAction->AddRef();
        return _testAction;
    }

  public:
    static bool FWriteFixture(PChunkyFile file, ChunkNumber *number)
    {
        auto tmpl = NewObj TrackTestTemplate();
        auto cels = GeneralGroup::PggNew(sizeof(AnimationCel));
        auto matrices = DynamicArray::PglNew(sizeof(BMAT34));
        bool ok = tmpl && tmpl->FInit() && cels && matrices;
        if (ok)
        {
            tmpl->_stn.SetSzs(PszLit("Fixture template"));
            ok = tmpl->_FWriteTmplf(file, kctgTmpl, number);
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
        ok = ok && table(kctgTmpl, *number, kctgGlpi, tmpl->_pglibactPar) &&
             table(kctgTmpl, *number, kctgGlbs, tmpl->_pglibset) &&
             table(kctgTmpl, *number, kctgGgcm, tmpl->_pggcmid) &&
             file->FAddChild(kctgTmpl, *number, 0, sizeof(header), kctgActn, &action) &&
             file->FPutPv(&header, sizeof(header), kctgActn, action) && table(kctgActn, action, kctgGgcl, cels) &&
             table(kctgActn, action, kctgGlxf, matrices);
        ReleasePpo(&tmpl);
        ReleasePpo(&cels);
        ReleasePpo(&matrices);
        return ok;
    }
    static bool FReadCostumeFixture(PChunkyResourceFile, ChunkTagOrType, ChunkNumber, PDataBlock,
                                    PBaseCacheableObject *out, LegacyLong *bytes)
    {
        *bytes = sizeof(TrackTestTemplate);
        if (!out)
            return true;
        auto tmpl = NewObj TrackTestTemplate();
        *out = tmpl;
        if (!tmpl || !tmpl->FInit())
        {
            ReleasePpo(out);
            return false;
        }
        tmpl->_ccmid = 3;
        return true;
    }
    static bool FReadSubrouteFixture(PChunkyResourceFile file, ChunkTagOrType type, ChunkNumber number,
                                     PDataBlock block, PBaseCacheableObject *out, LegacyLong *bytes)
    {
        if (!FReadCostumeFixture(file, type, number, block, out, bytes))
            return false;
        if (out && !static_cast<TrackTestTemplate *>(*out)->FInitAction())
        {
            ReleasePpo(out);
            return false;
        }
        return true;
    }
    void SetTestText(bool text)
    {
        _grftmpl = text ? ftmplTdt : 0;
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
        ReleasePpo(&_testAction);
    }
    bool FInit()
    {
        _yaRest = BR_ANGLE_DEG(90);
        _pglibactPar = DynamicArray::PglNew(sizeof(short));
        _pglibset = DynamicArray::PglNew(sizeof(short));
        _pggcmid = GeneralGroup::PggNew(sizeof(int32_t));
        return _pglibactPar != pvNil && _pglibset != pvNil && _pggcmid != pvNil;
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
            _testAction = ActionDefinition::PactnNew(cels, matrices, factnRotateY);
        ReleasePpo(&cels);
        ReleasePpo(&matrices);
        _cactn = 1;
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
            static_cast<PTemplate>(resource->PbacoFetch(kctgTmpl, number, TrackTestTemplate::FReadSubrouteFixture));
        if (!tmpl)
            return false;
        ReleasePpo(&_ptmpl);
        _ptmpl = tmpl;
        TagManager::CloseTag(&_tagTmpl);
        _tagTmpl = {ksidUseCrf, resource, kctgTmpl, number};
        TagManager::DupTag(&_tagTmpl);
        ActorTrack track;
        if (!track.FCreate())
            return false;
        RouteDistancePoint first = {{rZero, rZero, rZero}, BR_SCALAR(10)},
                           last = {{BR_SCALAR(10), rZero, rZero}, rZero};
        ActorEvent::Base event = {aetAdd, 1, {0, rZero, 0}};
        ActorEvent::Add add = {};
        ActorEvent::Action action = {0, 0};
        LegacyLong step = rOne;
        if (!track.Route()->FAdd(&first) || !track.Route()->FAdd(&last) ||
            !track.Events()->FInsert(0, sizeof(add), &add, &event))
            return false;
        event.aet = aetActn;
        if (!track.Events()->FInsert(1, sizeof(action), &action, &event))
            return false;
        event.aet = aetStep;
        if (!track.Events()->FInsert(2, sizeof(step), &step, &event))
            return false;
        _track.Swap(track);
        _motionSounds.Clear();
        _state = ActorFrameState();
        _state.RewindRoute(_track);
        _state.frame = 1;
        _state.nextEvent = 3;
        _state.firstEventInFrame = 0;
        _state.addEvent = 0;
        _state.actionEvent = 1;
        _state.celCount = 4;
        _state.actionFlags = factnRotateY;
        _nfrmFirst = 1;
        _nfrmLast = 10;
        _fLifeDirty = false;
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
        auto &d = p->*&Access::_data;
        Value(d.currentFrame);
        Value(d.transition);
        Value(d.name.count);
        Record(three_mm_movie_title_text(&d.name), d.name.count * d.name.unit);
    }
    static void ChopSound(PScene p, PChunkyResourceFile cache, bool back)
    {
        TagChildPair tag = {0, {ksidUseCrf, cache, kctgMsnd, 123}};
        auto packet = SceneSoundEvent::PsseNew(80, stySfx, false, 1, &tag);
        CHECK(packet);
        auto events = p->*&Access::_pggsevFrm;
        SceneEvent e = {back ? 1 : 7, sevtPlaySnd};
        CHECK(events->FInsert(back ? 0 : events->IvMac(), packet->Cb(), packet, &e));
        FreePpv(reinterpret_cast<void **>(&packet));
        if (back)
            ++(p->*&Access::_runtime).next_event;
    }
    static void ChopSetup(PScene p, bool cameras = true)
    {
        auto &d = p->*&Access::_data;
        d.firstFrame = 1;
        d.lastFrame = 40;
        d.currentFrame = 5;
        auto events = p->*&Access::_pggsevFrm;
        while (events->IvMac())
            events->Delete(events->IvMac() - 1);
        if (!cameras)
        {
            (p->*&Access::_runtime).next_event = 0;
            return;
        }
        for (long frame : {1, 3, 5, 5, 7})
        {
            SceneEvent e = {frame, sevtChngCamera};
            long camera = 0;
            CHECK(events->FInsert(events->IvMac(), sizeof(camera), &camera, &e));
        }
        (p->*&Access::_runtime).next_event = 4;
    }
    static void ChopState(PScene p)
    {
        State(p);
        auto events = p->*&Access::_pggsevFrm;
        Value((p->*&Access::_runtime).next_event);
        Value(events->IvMac());
        for (long i = 0; i < events->IvMac(); ++i)
        {
            SceneEvent e;
            events->GetFixed(i, &e);
            Value(e.nfrm);
            Value(e.sevt);
        }
    }
    static ThreeMMSceneContentServices Services(PScene p)
    {
        return (p->*&Access::_ContentServices)();
    }
    static void CheckMovie(PScene p, PMovie movie)
    {
        CHECK((p->*&Access::_owner).movie == movie && p->Pmvie() == movie);
        (p->*&Access::_SetOwner)(THREE_MM_SCENE_THUMBNAIL, nullptr);
        CHECK(p->Pmvie() == movie);
    }
    static void Extra(PScene p, PActor actor)
    {
        auto list = p->*&Access::_pglpactrSelExtra;
        if (!list)
        {
            list = DynamicArray::PglNew(sizeof(PActor));
            CHECK(list);
            (p->*&Access::_SetOwner)(THREE_MM_SCENE_SELECTION, list);
        }
        CHECK(list->FAdd(&actor));
    }
    static long Extras(PScene p)
    {
        return (p->*&Access::_pglpactrSelExtra)->IvMac();
    }
    static void FreshActorLists(PScene p)
    {
        (p->*&Access::_DropOwner)(THREE_MM_SCENE_STARTS);
        (p->*&Access::_DropOwner)(THREE_MM_SCENE_ACTORS);
        (p->*&Access::_SetOwner)(THREE_MM_SCENE_STARTS, GeneralGroup::PggNew(sizeof(SceneEvent)));
        (p->*&Access::_SetOwner)(THREE_MM_SCENE_ACTORS, DynamicArray::PglNew(sizeof(PActor)));
    }
    static void State(PScene p)
    {
        CHECK(p->PactrSelected() == (p->*&Access::_pactrSelected));
        CHECK(p->PtboxSelected() == (p->*&Access::_ptboxSelected));
        auto &d = p->*&Access::_data;
        Value(d.currentFrame);
        Value(d.firstFrame);
        Value(d.lastFrame);
        Value((p->*&Access::_pactrSelected) != nullptr);
        Value((p->*&Access::_ptboxSelected) != nullptr);
        auto starts = p->*&Access::_pggsevStart;
        Value(starts->IvMac());
        for (long i = 0; i < starts->IvMac(); ++i)
        {
            SceneEvent e;
            starts->GetFixed(i, &e);
            Value(e.sevt);
            if (e.sevt == sevtAddActr)
            {
                PActor a;
                starts->Get(i, &a);
                Value(a->Arid());
                Value(a->CactRef());
            }
            else if (e.sevt == sevtAddTbox)
            {
                PTBOX box;
                starts->Get(i, &box);
                Value(box->CactRef());
                Value(box->Pscen() == p);
            }
        }
        auto actors = p->*&Access::_pglpactr;
        Value(actors->IvMac());
        for (long i = 0; i < actors->IvMac(); ++i)
        {
            PActor a;
            actors->Get(i, &a);
            Value(a->Arid());
            Value(a->CactRef());
            Value(a->Pscen() == p);
        }
        auto texts = p->*&Access::_pglptbox;
        Value(texts->IvMac());
        for (long i = 0; i < texts->IvMac(); ++i)
        {
            PTBOX box;
            texts->Get(i, &box);
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
    auto file = resources->Pcfl();
    RichTextData text;
    text.properties = DynamicArray::PglNew(sizeof(RichTextPropertyEntry));
    text.fontName.SetSzs(PszLit("Arial"));
    text.text.pfil = FileObject::PfilCreateTemp();
    text.text.cb = 2;
    byte chars[] = {'H', 'i'};
    CHECK(text.properties && text.text.pfil && text.text.pfil->FSetFpMac(2) && text.text.FWriteRgb(chars, 2, 0));
    ChunkIdentification rich;
    CHECK(text.FWrite(file, &rich));
    ChunkNumber n;
    CHECK(file->FAdd(0, kctgTbox, &n));
    TextBoxData data;
    data.firstFrame = 20;
    data.maxFrame = 30;
    data.story = true;
    data.bounds.Set(1, 2, 100, 80);
    CHECK(data.FWrite(file, n) && file->FAdoptChild(kctgTbox, n, rich.ctg, rich.cno, 0));
    return TextBox::PtboxRead(resources, n, scene);
}
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
        std::vector<byte> camera(sizeof(cam) + (i == 1 ? 2 * sizeof(APOS) : 0));
        std::memcpy(camera.data(), &cam, sizeof(cam));
        if (i == 1)
        {
            APOS points[2] = {};
            points[0].xrPlace = BR_SCALAR(2); points[0].yrPlace = BR_SCALAR(-3); points[0].zrPlace = BR_SCALAR(4);
            points[1].xrPlace = BR_SCALAR(5); points[1].yrPlace = BR_SCALAR(6); points[1].zrPlace = BR_SCALAR(-7);
            std::memcpy(camera.data() + sizeof(cam), points, sizeof(points));
        }
        CHECK(file->FAddChildPv(kctgBkgd, *number, i, camera.data(), camera.size(), kctgCam, &child));
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
    if (auto name = std::getenv("THREE_MM_SCENE_CONTENT_SNAPSHOT"))
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
    auto file = observed->Pcfl();
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
    BackgroundFixture(file, &background, 0);
    TAG tag = {ksidUseCrf, observed, kctgBkgd, background}, old;
    CHECK(scene->FSetBkgdCore(&tag, &old));
    {
        auto background = static_cast<PBackground>(observed->PbacoFetch(kctgBkgd, tag.cno, Background::FReadBkgd));
        CHECK(background);
        for (long camera : {0, 1, 2, 1, 0})
        {
            Value(background->FSetCamera(movie->Pbwld(), camera));
            Value(background->Ccam());
            Value(background->Icam());
            BRS x, y, z;
            background->GetCameraPos(&x, &y, &z);
            Value(x);
            Value(y);
            Value(z);
            BMAT34 mouse;
            background->GetMouseMatrix(&mouse);
            Record(&mouse, sizeof(mouse));
            Value(background->BraRotYCamera());
            for (int i = 0; i < 5; ++i)
            {
                background->GetActorPlacePoint(&x, &y, &z);
                Value(x);
                Value(y);
                Value(z);
                if (i == 1)
                    background->ReuseActorPlacePoint();
            }
        }
        /* Foreign-order appended points used to be swapped past a stack header.
         * Exercise the real block/list bridge without changing the native trace. */
        ChildChunkIdentification child;
        DataBlock block;
        CHECK(file->FGetKidChidCtg(kctgBkgd, tag.cno, 1, kctgCam, &child));
        CHECK(file->FFind(kctgCam, child.cki.cno, &block));
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
        CHECK(file->FPutPv(foreign.data(), foreign.size(), kctgCam, child.cki.cno));
        CHECK(background->FSetCamera(movie->Pbwld(), 1));
        BRS x, y, z;
        background->GetCameraPos(&x, &y, &z);
        CHECK(x == BR_SCALAR(1) && y == 0 && z == 0 && background->Icam() == 1);
        background->GetActorPlacePoint(&x, &y, &z);
        CHECK(x == 0 && y == 0 && z == 0);
        background->GetActorPlacePoint(&x, &y, &z);
        CHECK(x == BR_SCALAR(2) && y == BR_SCALAR(-3) && z == BR_SCALAR(4));
        background->ReuseActorPlacePoint();
        background->GetActorPlacePoint(&x, &y, &z);
        CHECK(x == BR_SCALAR(2) && y == BR_SCALAR(-3) && z == BR_SCALAR(4));
        background->GetActorPlacePoint(&x, &y, &z);
        CHECK(x == BR_SCALAR(5) && y == BR_SCALAR(6) && z == BR_SCALAR(-7));
        background->GetActorPlacePoint(&x, &y, &z);
        CHECK(x == 0 && y == 0 && z == 0);
        CHECK(file->FPutPv(native.data(), native.size(), kctgCam, child.cki.cno));
        CHECK(background->FSetCamera(movie->Pbwld(), 0));
        ReleasePpo(&background);
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
                CHECK(file->FAdd(group->CbOnFile(), kctgFrmGg, &n, &block) && group->FWrite(&block, order));
                block.Free();
                CHECK(file->FFind(kctgFrmGg, n, &block));
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
                file->Delete(kctgFrmGg, n);
            }
        byte invalid = 0;
        ChunkNumber n;
        DataBlock block;
        CHECK(file->FAddPv(&invalid, 1, kctgFrmGg, &n) && file->FFind(kctgFrmGg, n, &block));
        short order = 0x7777;
        auto failed = SceneData::PggReadEvents(&block, &order, false);
        CHECK(!failed && order == 0x7777);
        Value(failed != nullptr);
        Value(order);
        block.Free();
        file->Delete(kctgFrmGg, n);
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
        CHECK(metadata.FCreate(file, &number));
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
        file->Delete(kctgScen, number);
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
    CHECK(file->FAdd(0, kctgTmpl, &number));
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
#ifdef THREE_MM_ENGINE_SCENE_CONTENT_H
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
    services.events.seek = [](void *, int, void *, ThreeMMI32) -> int { return 0; };
    CHECK(!three_mm_scene_text_add(box, &services) && box->CactRef() == 1);
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
        CHECK(recorded == 30976 && hash == 0x34CB17AFU);
}
