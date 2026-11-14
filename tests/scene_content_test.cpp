#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#include "soc.h"
#include "scene_sound.h"
#include "scene_content_undo.h"
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
    BackgroundFixture(file, &background, 0);
    TAG tag = {0, observed, kctgBkgd, background}, old;
    CHECK(scene->FSetBkgdCore(&tag, &old));
    ChunkNumber number;
    CHECK(file->FAdd(0, kctgTmpl, &number));
    auto makeActor = [&]() {
        auto a = NewObj ContentActor;
        CHECK(a && a->FInitClipboard(observed, number) && a->PrepareBody(scene));
        return a;
    };
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
#ifdef THREE_MM_ENGINE_SCENE_CONTENT_H
    recording = false;
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
        CHECK(recorded == 5020 && hash == 0x2F3EEF19U);
}
