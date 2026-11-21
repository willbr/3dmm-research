/*
 * movie-save-load-test  --  reproduce + regression-test memory access errors
 * in the Movie save/load round-trip.
 *
 * Steps:
 *   1. Initialize the bare minimum app environment (vpappb, vptagm).
 *   2. PmvieNew(pmcc, pvNil)             -- create a blank movie.
 *   3. _FMakeCrfValid()                  -- build the temp autosave file.
 *      (called indirectly via the first FNewScenInsCore.)
 *   4. FNewScenInsCore(0)                -- give it one empty scene.
 *   5. FSaveToFni(<temp>.3mm, fTrue)     -- save to a real on-disk movie.
 *   6. ReleasePpo(&pmvie).
 *   7. PmvieNew(pmcc, &fni)              -- reopen.
 *   8. ReleasePpo(&pmvie) again.
 *
 * If any step segfaults or fires an assert, we print where and exit non-zero.
 *
 * Links engine + kauai (gui side too -- ApplicationBase lives there).
 */

#include <vector>
#include "soc.h"
#include "handle_services.h"
#include "engine_action_tables_fixture.h"
#include "actor_frame_fixture.h"
#include "mm/actor_placement.h"
ASSERTNAME

#include <stdio.h>
#include <cstring>
#include <cstdlib>
#include "movie_graph_frame_fixture.h"
#include "text_live_fixture.h"
#include "text_document_fixture.h"
#include "text_materialize_fixture.h"
#include "text_properties_fixture.h"
#include "text_format_fixture.h"
#include "text_access_fixture.h"
#include "text_copy_fixture.h"
#include "text_undo_fixture.h"
#include "document_undo_fixture.h"
#include "document_lifecycle_fixture.h"
#include "document_views_fixture.h"
#include "kidhelp.h"
#include "help_text_fixture.h"
#include "help_display_fixture.h"
#include "help_interaction_fixture.h"
#include "help_balloon_fixture.h"
#include "help_topic_fixture.h"
#include "text_box_runtime_fixture.h"
#include "text_box_edit_fixture.h"

/* WinMain (in kauai/src/appbwin.cpp) references FrameMain -- but we
 * never run WinMain since we provide our own main() and link as a
 * console subsystem. The symbol must still resolve. */
void __cdecl FrameMain(void) {}

/* TagManager wants a callback for asking the user to insert a CD when
 * source content is on a missing volume. For a blank movie there is
 * no source content, so this callback is never actually invoked. */
static bool FInsertCdStub(const MissingSourceRequest &)
{
    return fFalse;
}

/* Minimal MovieClientCallbacks. All virtuals on the base class have
 * default no-op implementations, so we only need to construct it. */
class TestMcc : public MovieClientCallbacks
{
  public:
    long stopped = 0;
    long disabled = 0;
    WaitReason pause = witNil;
    bool testingTitle = false;
    bool testingClipboard = false;
    String clipboardPrefix;
    long titleUpdates = 0;
    void GetStn(long ids, PString value) override
    {
        if (testingTitle && ids == idsEngineDefaultTitle) value->SetSzs(PszLit("Default movie"));
        if (testingClipboard && ids == idsEngineCopyOf) *value=clipboardPrefix;
    }
    void UpdateTitle(PString value) override
    {
        if (testingTitle) { ++titleUpdates; }
    }
    bool GetFniSave(Filename *, long, long, long, achar *, PString title) override
    {
        if (testingTitle) title->SetSzs(PszLit("Changed by client"));
        return false;
    }
    void PauseType(WaitReason reason) override
    {
        pause = reason;
    }
    void PlayStopped() override
    {
        ++stopped;
    }
    void DisableAccel() override
    {
        ++disabled;
    }
    TestMcc() : MovieClientCallbacks(640, 480, 0) {}
    /* GetFniSave is the one virtual the save path consults: it asks
     * the client for a filename when the user does Save-As. We never
     * hit Save-As here (we pass an explicit pfni to FSaveToFni), so
     * the inherited fFalse return is fine. */
};

/* Minimal ApplicationBase: BeginLongOp/EndLongOp is the only thing
 * Movie save/load reaches into. ApplicationBase's own ctor sets
 * vpappb = this, which is what Movie code uses. */
class TestApp : public ApplicationBase
{
  public:
    TestApp() : ApplicationBase() {}
#ifdef DEBUG
    bool FAssertProcApp(PSZS file, long line, PSZS message, void *, long) override
    {
        fprintf(stderr, "[ASSERT] %s:%ld: %s\n", file, line, message);
        std::exit(1);
    }
#endif
};

#include "movie_scene_fixture.h"
#include "movie_save_fixture.h"

#define LOG(msg) do { fprintf(stderr, "[step] %s\n", msg); fflush(stderr); } while (0)
#define BAIL(msg) do { fprintf(stderr, "[FAIL] %s\n", msg); fflush(stderr); rc = 1; goto LDone; } while (0)

// Minimal valid in-memory template: duplication uses its ownership, not assets.
class TrackTestTemplate : public Template
{
    MMActionResource *_testAction = pvNil;
    bool _failNextFetch = false;
    int _failAfterFetch = 0;

  protected:
    MMActionResource *_FetchAction(long anid) override
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
    static bool FWriteFixture(const MovieArchiveFixture &archive,ChunkNumber *number)
    {
        auto tmpl=NewObj TrackTestTemplate();
        auto cels=GeneralGroup::PggNew(sizeof(AnimationCel)); auto matrices=DynamicArray::PglNew(sizeof(BMAT34));
        auto costumeWire = GeneralGroup::PggNew(sizeof(int32_t));
        auto indexWire = DynamicArray::PglNew(sizeof(short));
        bool ok=tmpl && tmpl->FInit() && cels && matrices && costumeWire && indexWire;
        if (ok)
        {
            tmpl->_stn.SetSzs(PszLit("Fixture template"));
            byte wire[kcbMaxDataStn];
            const auto name = ChunkyFile::EncodeName(&tmpl->_stn, wire);
            ok = mm_template_write_header(&tmpl->_template, &name, kctgTmpl, number, &archive.writer) != 0;
        }
        AnimationCel cel={chidNil,rOne}; BMAT34 matrix; BrMatrix34Identity(&matrix);
        for (int i=0;ok && i<4;++i) { cel.dwr=BR_SCALAR(i+1); ok=cels->FInsert(i,0,nullptr,&cel); }
        ok=ok && matrices->FAdd(&matrix);
        auto table=[&](ChunkTagOrType parent,ChunkNumber root,ChunkTagOrType type,auto *data) {
            ChunkNumber child; DataBlock block;
            return archive.AddChild(parent,root,0,data->CbOnFile(),type,&child,&block) && data->FWrite(&block);
        };
        ChunkNumber action=0; ActionChunkOnFile header={kboCur,koskCur,factnRotateY};
        ok=ok && table(kctgTmpl,*number,kctgGlpi,indexWire) && table(kctgTmpl,*number,kctgGlbs,indexWire) &&
            table(kctgTmpl,*number,kctgGgcm,costumeWire) &&
            archive.AddChildBytes(kctgTmpl,*number,0,&header,sizeof(header),kctgActn,&action) &&
            table(kctgActn,action,kctgGgcl,cels) && table(kctgActn,action,kctgGlxf,matrices);
        ReleasePpo(&indexWire); ReleasePpo(&costumeWire); ReleasePpo(&tmpl); ReleasePpo(&cels); ReleasePpo(&matrices); return ok;
    }
    static int FReadCostumeFixture(MMResourceCache *, MMU32, MMU32, MMOwnedBlock *, void **out, MMI32 *bytes)
    {
        *bytes=sizeof(TrackTestTemplate);
        if (!out) return true;
        auto tmpl=NewObj TrackTestTemplate();
        *out=tmpl;
        if (!tmpl || !tmpl->FInit()) { ReleasePpo(&tmpl); *out = nullptr; return false; }
        tmpl->_template.custom_count=3;
        return true;
    }
    static int FReadSubrouteFixture(MMResourceCache *cache, MMU32 type, MMU32 number, MMOwnedBlock *block,
                                    void **out, MMI32 *bytes)
    {
        if (!FReadCostumeFixture(cache, type, number, block, out, bytes)) return false;
        auto tmpl = out ? static_cast<TrackTestTemplate *>(*out) : nullptr;
        if (tmpl && !tmpl->FInitAction()) { ReleasePpo(&tmpl); *out = nullptr; return false; }
        return true;
    }
    void SetTestText(bool text) { _template.flags=text ? ftmplTdt : 0; }
    void FailAfter(int calls) { _failAfterFetch = calls; }
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

class StageTestBackground : public Background
{
  public:
    bool Init()
    {
        _camera.count=1; _resource.light_count=1;
        _camera.place.dxr=BR_SCALAR(100); _camera.place.dyr=BR_SCALAR(2); _camera.place.dzr=-BR_SCALAR(30);
        _camera.yaw=0x1234;
        return FAllocPv(&_resource.actors,sizeof(BACT),fmemClear,mprNormal) &&
               FAllocPv(&_resource.lights,sizeof(BLIT),fmemClear,mprNormal);
    }
};
class StageTestScene : public Scene
{
  public:
    StageTestScene(PMovie movie) : Scene(movie) {}
    bool TrackActor(PActor actor,long frame)
    {
        _owner.data.currentFrame=frame; _owner.data.firstFrame=1; _owner.data.lastFrame=100;
        _owner.runtime.next_event=0;
        mm_scene_member_clear(&_owner, MM_SCENE_ACTORS);
        return mm_scene_member_append(&_owner, MM_SCENE_ACTORS, actor);
    }
    bool Init()
    {
        auto background=NewObj StageTestBackground(); _owner.owned[MM_SCENE_BACKGROUND]=background;
        _owner.data.firstFrame=1; _owner.data.lastFrame=20; _owner.data.currentFrame=3;
        return background && background->Init();
    }
};

class TrackTestActor : public Actor
{
  public:
    bool FInitClipboard(PChunkyResourceFile resource,ChunkNumber number)
    {
        using namespace ActorEvent;
        auto tmpl=static_cast<PTemplate>(resource->PbacoFetch(kctgTmpl,number,TrackTestTemplate::FReadSubrouteFixture));
        if (!tmpl) return false;
        if (_Template()) { _Template()->Release(); _owner.runtime.template_object = nullptr; } _owner.runtime.template_object = tmpl; mm_tag_close(&_owner.runtime.template_tag, &EngineActorTagServices());
        _owner.runtime.template_tag={ksidUseCrf,resource,kctgTmpl,number}; mm_tag_dup(&_owner.runtime.template_tag, &EngineActorTagServices());
        ActorTrack track; if (!track.FCreate()) return false;
        RouteDistancePoint first={{rZero,rZero,rZero},BR_SCALAR(10)},last={{BR_SCALAR(10),rZero,rZero},rZero};
        ActorEvent::Base event={aetAdd,1,{0,rZero,0}}; ActorEvent::Add add={}; ActorEvent::Action action={0,0}; LegacyLong step=rOne;
        if (!(mm_array_add(&track.Route()->rows, &first, nullptr) == MM_STORAGE_OK) || !(mm_array_add(&track.Route()->rows, &last, nullptr) == MM_STORAGE_OK) || !mm_actor_events_insert(track.Events(), 0,sizeof(add),&add,&event)) return false;
        event.aet=aetActn; if (!mm_actor_events_insert(track.Events(), 1,sizeof(action),&action,&event)) return false;
        event.aet=aetStep; if (!mm_actor_events_insert(track.Events(), 2,sizeof(step),&step,&event)) return false;
        mm_actor_track_swap(&_owner.track, &track.Core()); mm_actor_sounds_clear(&_owner.sounds); _owner.runtime.state=ActorFrameState(); ActorFrameTest::Rewind(_owner.runtime.state, _owner.track);
        _owner.runtime.state.frame=1; _owner.runtime.state.nextEvent=3; _owner.runtime.state.firstEventInFrame=0; _owner.runtime.state.addEvent=0; _owner.runtime.state.actionEvent=1;
        _owner.runtime.state.celCount=4; _owner.runtime.state.actionFlags=factnRotateY; _owner.runtime.first=1; _owner.runtime.last=10; _owner.runtime.life_dirty=false;
        return true;
    }
    static bool RecordClipboardActor(PActor actor,FILE *snapshot)
    {
        using namespace ActorEvent;
        auto ownerMember = &TrackTestActor::_owner;
        auto &track = (actor->*ownerMember).track; const auto &runtime = (actor->*ownerMember).runtime; const auto &state=runtime.state;
        const auto &tag=runtime.template_tag;
        auto record=[&](const void *p,size_t n) { return !snapshot || std::fwrite(p,1,n,snapshot)==n; };
        LegacyLong values[]={actor->Arid(),runtime.first,runtime.last,state.frame,state.nextEvent,state.firstEventInFrame,
            state.actionEvent,state.addEvent,state.location.irpt,state.location.dwrOffset,state.location.dnfrm,state.step,state.frozen,
            mm_actor_path_count(track.route),mm_actor_events_count(track.events),static_cast<PTemplate>(runtime.template_object)->CactRef(),static_cast<PChunkyResourceFile>(tag.cache)->CactRef()};
        bool ok=record(values,sizeof(values)) && record(&state.position,sizeof(state.position)) && record(&state.transform,sizeof(state.transform));
        for (long i=0;ok && i<mm_actor_path_count(track.route);++i) { RouteDistancePoint point; mm_actor_path_get(track.route, i,&point); ok=record(&point,sizeof(point)); }
        for (long i=0;ok && i<mm_actor_events_count(track.events);++i)
        {
            ActorEvent::Base event; mm_actor_events_get_fixed(track.events, i,&event); LegacyLong bytes=mm_actor_events_size(track.events, i);
            unsigned char data[48]={}; if (bytes>sizeof(data)) return false;
            mm_actor_events_get_payload(track.events, i,data); if (event.aet==aetAdd) data[18]=data[19]=0;
            ok=record(&event,sizeof(event)) && record(&bytes,sizeof(bytes)) && record(data,bytes);
        }
        return ok;
    }
    bool FTestOwner(int mode,FILE *snapshot)
    {
        using namespace ActorEvent;
        auto tmpl=static_cast<TrackTestTemplate *>(_Template());
        tmpl->FailAfter(0);
        if (!FGotoFrame(2)) return false;
        if (mode&2) { _Hide(); if (_Body()) { mm_body_owner_release(_Body()); _owner.runtime.body = nullptr; } }
        Base soundEvent={aetSnd,100,{2,rZero,5}};
        MMActorSound sound={}; sound.tag = _owner.runtime.template_tag; sound.tag.type=kctgMsnd;
        if (!mm_actor_events_insert(_owner.track.events, mm_actor_events_count(_owner.track.events),sizeof(sound),&sound,&soundEvent)) return false;
        mm_tag_dup(&sound.tag, &EngineActorTagServices());
        _owner.runtime.life_dirty=(mode&8)!=0; _owner.runtime.prerendered=(mode&32)!=0; _owner.runtime.time_frozen=true;
        _owner.runtime.recording.recording=(mode&4)!=0; _owner.runtime.recording.rejoining=(mode&16)!=0; _owner.runtime.recording.inserted=true;
        _owner.runtime.recording.insertion={1,123,4}; _owner.runtime.recording.timestamp=0xfffffff0UL; _owner.runtime.recording.gap=-17; _owner.runtime.recording.raw={11,-22,33};
        _owner.runtime.origin={BR_SCALAR(1),BR_SCALAR(2),BR_SCALAR(3)}; _owner.runtime.last=88; _owner.runtime.actor=100+mode;
        _owner.runtime.sound_tag.sid=(mode&16)?ksidInvalid:7; _owner.runtime.sound_tag.type=kctgMsnd; _owner.runtime.sound_tag.number=17;
        const bool reset=(mode&1) && !(mode&2);
        PActor copy=nullptr;
#ifdef DEBUG
        if (mode&64)
        {
            vmutxMem.Enter(); vdmglob.dmaglBase.cactDo=(mode/4)%4; vdmglob.dmaglBase.cactFail=1; vmutxMem.Leave();
        }
#endif
        bool ok=FDup(&copy,reset);
#ifdef DEBUG
        vmutxMem.Enter(); vdmglob.dmaglBase.cactFail=0; vmutxMem.Leave();
#endif
        bool independent=copy && copy->Pbody()==(reset?nullptr:_Body()) && copy->Pscen()==(reset?nullptr:_Scene());
        long templateRefs=_Template()->CactRef(),bodyRefs=_Body()?mm_body_owner_references(_Body()):0,cacheRefs=static_cast<PChunkyResourceFile>(_owner.runtime.template_tag.cache)->CactRef();
        if (copy)
        {
            if (reset)
            {
                if (mode&32) tmpl->FailAfter(1);
                const auto owners = _OwnerServices();
                const auto timeline = _TimelineServices();
                const auto playback = _PlaybackEffects();
                mm_actor_owner_restore_undo(this, copy, &owners, &timeline, &playback);
                independent=independent && !copy->Pscen() && !copy->Pbody();
            }
            else Restore(copy);
        }
        tmpl->FailAfter(0); vpers->Clear(); ReleasePpo(&copy);
        auto record=[&](const void *p,size_t n) { return !snapshot || std::fwrite(p,1,n,snapshot)==n; };
        LegacyLong values[]={mode,ok,independent,templateRefs,bodyRefs,cacheRefs,_Template()->CactRef(),_Body()?mm_body_owner_references(_Body()):0,
            static_cast<PChunkyResourceFile>(_owner.runtime.template_tag.cache)->CactRef(),_owner.runtime.state.frame,_owner.runtime.state.nextEvent,_owner.runtime.state.firstEventInFrame,_owner.runtime.state.actionEvent,_owner.runtime.state.addEvent,
            _owner.runtime.state.location.irpt,_owner.runtime.state.location.dwrOffset,_owner.runtime.state.location.dnfrm,_owner.runtime.state.onStage,_owner.runtime.state.frozen,_owner.runtime.state.action,_owner.runtime.state.cel,
            _owner.runtime.first,_owner.runtime.last,_owner.runtime.actor,_owner.runtime.life_dirty,_owner.runtime.prerendered,_owner.runtime.time_frozen,_owner.runtime.recording.recording,_owner.runtime.recording.rejoining,_owner.runtime.recording.inserted,
            _owner.runtime.recording.insertion.irpt,_owner.runtime.recording.insertion.dwrOffset,_owner.runtime.recording.insertion.dnfrm,static_cast<LegacyLong>(_owner.runtime.recording.timestamp),_owner.runtime.recording.gap,
            _owner.runtime.sound_tag.sid,static_cast<LegacyLong>(_owner.runtime.sound_tag.type),static_cast<LegacyLong>(_owner.runtime.sound_tag.number),mm_actor_path_count(_owner.track.route),mm_actor_events_count(_owner.track.events)};
        bool saved=record(values,sizeof(values)) && record(&_owner.runtime.state.transform,sizeof(_owner.runtime.state.transform)) &&
            record(&_owner.runtime.state.position,sizeof(_owner.runtime.state.position)) && record(&_owner.runtime.origin,sizeof(_owner.runtime.origin)) && record(&_owner.runtime.recording.raw,sizeof(_owner.runtime.recording.raw));
        for (long i=0;saved && i<mm_actor_path_count(_owner.track.route);++i)
        { RouteDistancePoint point; mm_actor_path_get(_owner.track.route, i,&point); saved=record(&point,sizeof(point)); }
        unsigned char payload[100];
        for (long i=0;saved && i<mm_actor_events_count(_owner.track.events);++i)
        {
            Base event; mm_actor_events_get_fixed(_owner.track.events, i,&event); LegacyLong bytes=mm_actor_events_size(_owner.track.events, i);
            if (bytes>sizeof(payload)) return false; mm_actor_events_get_payload(_owner.track.events, i,payload);
            if (event.aet==aetSnd)
            { MMActorSound clean; std::memcpy(&clean,payload,sizeof(clean)); clean.tag.cache=nullptr; std::memcpy(payload,&clean,sizeof(clean)); }
            saved=record(&event,sizeof(event)) && record(&bytes,sizeof(bytes)) && record(payload,bytes);
        }
        return saved;
    }
    bool FTestPaste(int mode,FILE *snapshot,StageTestScene *scene,PChunkyResourceFile source,PChunkyResourceFile target,
                    ChunkNumber soundNumber,PTemplate textTemplate,ChunkNumber templateNumber)
    {
        using namespace ActorEvent;
        if (textTemplate)
        {
            textTemplate->AddRef(); if (_Template()) { _Template()->Release(); _owner.runtime.template_object = nullptr; } _owner.runtime.template_object = textTemplate;
            mm_tag_close(&_owner.runtime.template_tag, &EngineActorTagServices()); _owner.runtime.template_tag={ksidUseCrf,source,kctgTmpl,templateNumber}; mm_tag_dup(&_owner.runtime.template_tag, &EngineActorTagServices());
        }
        ActorTrack track; if (!track.FCreate()) return false;
        RouteDistancePoint point={{BR_SCALAR(7),BR_SCALAR(2),-BR_SCALAR(3)},rZero};
        Add entrance={BR_SCALAR(4),BR_SCALAR(5),BR_SCALAR(6),111,222,333};
        Base event={(mode&8)?aetRem:aetAdd,1,{0,rZero,0}};
        if (!(mm_array_add(&track.Route()->rows, &point, nullptr) == MM_STORAGE_OK) || !mm_actor_events_insert(track.Events(), 0,sizeof(entrance),&entrance,&event)) return false;
        for (int i=0;i<3;++i)
        {
            MMActorSound sound={}; sound.cel=-1; sound.volume=100; sound.style=2; sound.child=55;
            sound.tag={((mode&2) && i==2)?1:ksidUseCrf,source,kctgMsnd,((mode&1) && i<2)?999999:soundNumber};
            mm_tag_dup(&sound.tag, &EngineActorTagServices()); event={aetSnd,2+i,{0,rZero,i}};
            if (!mm_actor_events_insert(track.Events(), mm_actor_events_count(track.Events()),sizeof(sound),&sound,&event))
            { mm_tag_close(&sound.tag, &EngineActorTagServices()); return false; }
        }
        mm_actor_track_swap(&_owner.track, &track.Core()); mm_actor_sounds_clear(&_owner.sounds); _owner.runtime.state=ActorFrameState();
        _owner.runtime.state.frame=3; _owner.runtime.state.nextEvent=3; _owner.runtime.state.firstEventInFrame=2; _owner.runtime.state.actionEvent=1; _owner.runtime.state.addEvent=0;
        _owner.runtime.state.subrouteTranslation={BR_SCALAR(8),BR_SCALAR(9),BR_SCALAR(10)};
        _owner.runtime.state.routeTranslation={BR_SCALAR(20),BR_SCALAR(21),BR_SCALAR(22)};
        _owner.runtime.origin={BR_SCALAR(30),BR_SCALAR(31),BR_SCALAR(32)}; _owner.runtime.first=1; _owner.runtime.last=20; _owner.runtime.life_dirty=(mode&4)!=0;
        if (!scene->TrackActor(this,3)) return false;
        const long frame=(mode%3)==0?1:(mode%3)==1?10:-3;
        const auto services = _PasteServices();
        bool pasted=mm_actor_paste(&_owner,frame,scene,&services)!=0; vpers->Clear();
        auto record=[&](const void *p,size_t n) { return !snapshot || std::fwrite(p,1,n,snapshot)==n; };
        LegacyLong values[]={mode,pasted,_owner.runtime.first,_owner.runtime.last,_owner.runtime.life_dirty,_owner.runtime.state.frame,_owner.runtime.state.nextEvent,
            _owner.runtime.state.firstEventInFrame,_owner.runtime.state.actionEvent,_owner.runtime.state.addEvent,mm_actor_events_count(_owner.track.events),
            _owner.runtime.template_tag.sid,static_cast<LegacyLong>(_owner.runtime.template_tag.number),_owner.runtime.template_tag.cache==target,_Template()->FIsTdt(),source->CactRef(),target->CactRef()};
        bool ok=record(values,sizeof(values)) && record(&_owner.runtime.origin,sizeof(_owner.runtime.origin)) &&
            record(&_owner.runtime.state.routeTranslation,sizeof(_owner.runtime.state.routeTranslation)) && record(&_owner.runtime.state.subrouteTranslation,sizeof(_owner.runtime.state.subrouteTranslation));
        for (long i=0;ok && i<mm_actor_events_count(_owner.track.events);++i)
        {
            Base header; mm_actor_events_get_fixed(_owner.track.events, i,&header); LegacyLong bytes=mm_actor_events_size(_owner.track.events, i);
            unsigned char payload[sizeof(MMActorSound)]={}; mm_actor_events_get_payload(_owner.track.events, i,payload);
            if (i==0) payload[18]=payload[19]=0;
            else { MMActorSound sound; std::memcpy(&sound,payload,sizeof(sound)); sound.tag.cache=nullptr; std::memcpy(payload,&sound,sizeof(sound)); }
            ok=record(&header,sizeof(header)) && record(&bytes,sizeof(bytes)) && record(payload,bytes);
        }
        return ok;
    }
    bool FTestRoutePaste(int mode,FILE *snapshot,StageTestScene *scene,TrackTestActor *clipboard,
                        PChunkyResourceFile resource,ChunkNumber material,ChunkNumber soundNumber)
    {
        using namespace ActorEvent;
        ActorTrack track,clip; if (!track.FCreate() || !clip.FCreate()) return false;
        const bool later=(mode&16)!=0; const int current=(mode&64)?2:(mode&32)?1:0;
        const int positions[]={0,4,10,20,30},distances[]={4,6,0,10,0};
        for (int i=0;i<(later?5:3);++i)
        {
            RouteDistancePoint point={{BR_SCALAR(positions[i]),rZero,rZero},BR_SCALAR(distances[i])};
            if (!mm_actor_path_insert(track.Route(), i,&point)) return false;
        }
        for (int i=0;i<((mode>>2)&3);++i)
        {
            RouteDistancePoint point={{BR_SCALAR(100+i*4),BR_SCALAR(5+((mode&2)?i:0)),BR_SCALAR(-10)},i+1==((mode>>2)&3)?rZero:BR_SCALAR(4)};
            if (!mm_actor_path_insert(clip.Route(), i,&point)) return false;
        }
        auto append=[&](int kind,int frame,int node) {
            Base event={kind,frame,{node,rZero,0}};
            union { Add add; Action action; Costume costume; MMActorSound sound; int32_t scalar; } data;
            std::memset(&data,0,sizeof(data)); int bytes=4;
            switch (kind)
            {
            case aetAdd: bytes=sizeof(Add); break;
            case aetActn: bytes=sizeof(Action); break;
            case aetSize: case aetStep: data.scalar=rOne; break;
            case aetCost: bytes=sizeof(Costume); data.costume.ibset=0; data.costume.fCmtl=tNo;
                data.costume.tag={ksidUseCrf,resource,kctgMtrl,material}; TagManager::DupTag(&data.costume.tag); break;
            case aetSnd: bytes=sizeof(MMActorSound); data.sound.cel=-1; data.sound.style=2; data.sound.volume=100;
                data.sound.tag={ksidUseCrf,resource,kctgMsnd,soundNumber}; mm_tag_dup(&data.sound.tag, &EngineActorTagServices()); break;
            }
            if (mm_actor_events_insert(track.Events(), mm_actor_events_count(track.Events()),bytes,&data,&event)) return true;
            if (kind==aetCost) TagManager::CloseTag(&data.costume.tag);
            if (kind==aetSnd) mm_tag_close(&data.sound.tag, &EngineActorTagServices());
            return false;
        };
        bool ok=append(aetAdd,1,0) && append(aetActn,1,0) && append(aetStep,2,0) && append(aetSize,2,0) &&
            append(aetCost,3,current) && append(aetSnd,3,current<2?current+1:current) && append(aetStep,4,2);
        if (later) ok=ok && append(aetAdd,8,3) && append(aetActn,8,3) && append(aetCost,8,3) && append(aetSnd,9,4) && append(aetStep,10,4);
        if (!ok) return false;
        mm_actor_track_swap(&_owner.track, &track.Core());
        mm_actor_track_swap(&clipboard->_owner.track, &clip.Core());
        mm_actor_sounds_clear(&_owner.sounds);
        _owner.runtime.state=ActorFrameState();
        _owner.runtime.state.frame=3; _owner.runtime.state.onStage=true; _owner.runtime.state.celCount=4; _owner.runtime.state.actionFlags=factnRotateY;
        _owner.runtime.state.addEvent=0; _owner.runtime.state.actionEvent=1; _owner.runtime.state.nextEvent=_owner.runtime.state.firstEventInFrame=4;
        _owner.runtime.state.location={current,(current<2 && (mode&1))?BR_SCALAR(2):rZero,(mode&2)?2:0};
        _GetXyzFromRtel(&_owner.runtime.state.location,&_owner.runtime.state.position); _owner.runtime.first=1; _owner.runtime.last=10; _owner.runtime.life_dirty=(mode&128)!=0;
        if (!scene->TrackActor(this,3)) return false;
        const auto services = _RoutePasteServices();
        bool changed=mm_actor_route_paste(&_owner,clipboard->_owner.track.route,&services)!=0; vpers->Clear();
        auto record=[&](const void *p,size_t n) { return !snapshot || std::fwrite(p,1,n,snapshot)==n; };
        LegacyLong values[]={mode,changed,_owner.runtime.state.frame,_owner.runtime.state.nextEvent,_owner.runtime.state.firstEventInFrame,_owner.runtime.state.addEvent,_owner.runtime.state.actionEvent,
            _owner.runtime.state.location.irpt,_owner.runtime.state.location.dwrOffset,_owner.runtime.state.location.dnfrm,_owner.runtime.state.step,_owner.runtime.state.frozen,
            _owner.runtime.first,_owner.runtime.last,_owner.runtime.life_dirty,resource->CactRef(),mm_actor_path_count(_owner.track.route),mm_actor_events_count(_owner.track.events),mm_actor_path_count(clipboard->_owner.track.route)};
        bool saved=record(values,sizeof(values)) && record(&_owner.runtime.state.position,sizeof(_owner.runtime.state.position)) && record(&_owner.runtime.state.transform,sizeof(_owner.runtime.state.transform));
        for (int source=0;saved && source<2;++source)
        {
            auto route=source?clipboard->_owner.track.route:_owner.track.route;
            for (long i=0;saved && i<mm_actor_path_count(route);++i) { RouteDistancePoint point; mm_actor_path_get(route, i, &point); saved=record(&point,sizeof(point)); }
        }
        unsigned char payload[100];
        for (long i=0;saved && i<mm_actor_events_count(_owner.track.events);++i)
        {
            Base event; mm_actor_events_get_fixed(_owner.track.events, i,&event); LegacyLong bytes=mm_actor_events_size(_owner.track.events, i);
            if (bytes>sizeof(payload)) return false; mm_actor_events_get_payload(_owner.track.events, i,payload);
            if (event.aet==aetAdd) payload[18]=payload[19]=0;
            if (event.aet==aetCost) { Costume value; std::memcpy(&value,payload,sizeof(value)); value.tag.pcrf=nullptr; std::memcpy(payload,&value,sizeof(value)); }
            if (event.aet==aetSnd) { MMActorSound value; std::memcpy(&value,payload,sizeof(value)); value.tag.cache=nullptr; std::memcpy(payload,&value,sizeof(value)); }
            saved=record(&event,sizeof(event)) && record(&bytes,sizeof(bytes)) && record(payload,bytes);
        }
        return saved;
    }
    bool FTestCopy(int mode,FILE *snapshot,PChunkyResourceFile resource,ChunkNumber templateNumber,
                   ChunkNumber materialNumber,ChunkNumber soundNumber,ChildChunkID child)
    {
        using namespace ActorEvent;
        auto tmpl=static_cast<PTemplate>(resource->PbacoFetch(kctgTmpl,templateNumber,TrackTestTemplate::FReadSubrouteFixture));
        if (!tmpl) return false;
        if (_Template()) { _Template()->Release(); _owner.runtime.template_object = nullptr; } _owner.runtime.template_object = tmpl; mm_tag_close(&_owner.runtime.template_tag, &EngineActorTagServices());
        _owner.runtime.template_tag={ksidUseCrf,resource,kctgTmpl,templateNumber}; mm_tag_dup(&_owner.runtime.template_tag, &EngineActorTagServices());
        ActorTrack track; if (!track.FCreate()) return false;
        for (int i=0;i<5;++i)
        {
            RouteDistancePoint point={{BR_SCALAR(i*10),BR_SCALAR(i),-BR_SCALAR(i)},(i==1 || i==4)?rZero:BR_SCALAR(10)};
            if (!mm_actor_path_insert(track.Route(), i,&point)) return false;
        }
        auto append=[&](int kind,int frame,int node,int scalar=0) {
            Base event={kind,frame,{node,rZero,0}};
            union { Add add; Action action; Costume costume; MMActorSound sound; RoutePoint point; BMAT34 matrix; int32_t scalar; } data={};
            int bytes=4;
            switch (kind)
            {
            case aetAdd: bytes=sizeof(Add); break;
            case aetActn: bytes=sizeof(Action); data.action={0,0}; break;
            case aetCost: bytes=sizeof(Costume); data.costume.ibset=0; data.costume.fCmtl=tNo;
                data.costume.tag={ksidUseCrf,resource,kctgMtrl,materialNumber}; TagManager::DupTag(&data.costume.tag); break;
            case aetSnd: bytes=sizeof(MMActorSound); data.sound.cel=scalar; data.sound.style=2; data.sound.volume=100;
                data.sound.child=mode>=256?999999:child; data.sound.tag={ksidUseCrf,resource,kctgMsnd,soundNumber};
                mm_tag_dup(&data.sound.tag, &EngineActorTagServices()); break;
            case aetRotF: case aetRotH: bytes=sizeof(BMAT34); BrMatrix34Identity(&data.matrix); break;
            case aetPull: case aetMove: case aetTweak: bytes=sizeof(RoutePoint); data.point={rOne,rOne,rOne}; break;
            case aetRem: bytes=0; break;
            default: data.scalar=scalar; break;
            }
            if (mm_actor_events_insert(track.Events(), mm_actor_events_count(track.Events()),bytes,&data,&event)) return true;
            if (kind==aetSnd) mm_tag_close(&data.sound.tag, &EngineActorTagServices());
            if (kind==aetCost) TagManager::CloseTag(&data.costume.tag);
            return false;
        };
        bool ok=append(aetAdd,1,0) && append(aetActn,1,0) && append(aetCost,1,0) && append(aetSize,2,0,rOne) &&
            append(aetPull,2,0) && append(aetRotF,2,0) && append(aetMove,2,0) && append(aetFreeze,2,0,1) &&
            append(aetStep,2,0,rOne) && append(aetSnd,2,0,-1) && append(aetRotH,2,0) && append(aetTweak,2,0) &&
            append(aetSnd,3,0,2) && append(aetSnd,3,0,-1) && append(aetTweak,3,0) && append(aetRotH,3,0) &&
            append(aetActn,4,1) && append(aetRem,4,1) && append(aetAdd,8,2) && append(aetActn,8,2) &&
            append(aetSnd,9,3,-1) && append(aetStep,10,4,0) && append(aetFreeze,10,4,1);
        if (!ok) return false;
        mm_actor_track_swap(&_owner.track, &track.Core()); mm_actor_sounds_clear(&_owner.sounds); _owner.runtime.state=ActorFrameState();
        _owner.runtime.state.cel=3; _owner.runtime.state.celCount=4; _owner.runtime.state.action=0; _owner.runtime.state.actionFlags=factnRotateY;
        _owner.runtime.state.location={(mode&64)?4:(mode&32)?1:0,(!(mode&96) && (mode&4))?BR_SCALAR(3):rZero,(mode&8)?3:0};
        _owner.runtime.state.addEvent=(mode&64)?18:0; _owner.runtime.state.actionEvent=(mode&64)?19:1;
        _owner.runtime.state.nextEvent=(mode&64)?23:16; _owner.runtime.state.firstEventInFrame=(mode&64)?21:(mode&16)?13:9;
        _owner.runtime.state.frame=(mode&64)?10:3; _owner.runtime.first=1; _owner.runtime.last=10; _owner.runtime.life_dirty=false;
        PActor copy=nullptr;
#ifdef DEBUG
        auto allocator=&vdmglob.dmaglBase;
        if (mode>=128 && mode<256)
        {
            const int site=(mode/16)%3;
            // Exercise native object creation and the C collection allocation path.
            if (site==2) allocator=&vdmglob.dmaglHq;
            vmutxMem.Enter(); allocator->cactDo=site==2?0:site+1; allocator->cactFail=1; vmutxMem.Leave();
        }
#endif
        bool changed=(mode&1)?FCopyRte(&copy,(mode&2)!=0):FCopy(&copy,(mode&2)!=0);
#ifdef DEBUG
        vmutxMem.Enter(); allocator->cactFail=0; vmutxMem.Leave();
#endif
        bool expected=(mode<128 || mode>=256) && (mode<256 || (mode&1));
#ifndef DEBUG
        if (mode>=128 && mode<256) expected=true;
#endif
        if (changed!=expected || (copy!=nullptr)!=expected)
        { std::fprintf(stderr,"Copy fixture outcome mismatch mode %d: %d expected %d\n",mode,changed,expected); ReleasePpo(&copy); return false; }
        vpers->Clear();
        auto record=[&](const void *p,size_t n) { return !snapshot || std::fwrite(p,1,n,snapshot)==n; };
        LegacyLong values[]={mode,changed,copy!=nullptr,_Template()->CactRef(),resource->CactRef(),_owner.runtime.state.frame,_owner.runtime.state.cel};
        bool saved=record(values,sizeof(values));
        auto ownerMember = &TrackTestActor::_owner;
        auto dump=[&](PActor actor) {
            auto &track = (actor->*ownerMember).track; auto &runtime = (actor->*ownerMember).runtime; auto &state=runtime.state;
            LegacyLong data[]={mm_actor_path_count(track.route),mm_actor_events_count(track.events),state.frame,state.nextEvent,
                state.actionEvent,state.addEvent,state.location.irpt,state.location.dwrOffset,
                state.location.dnfrm,state.position.dxr,state.position.dyr,state.position.dzr,
                runtime.first,runtime.last,actor->FIsModeRecord(),actor->Pbody()!=nullptr,actor->Pscen()!=nullptr};
            if (!record(data,sizeof(data))) return false;
            for (long i=0;i<mm_actor_path_count(track.route);++i)
            { RouteDistancePoint point; mm_actor_path_get(track.route, i,&point); if (!record(&point,sizeof(point))) return false; }
            unsigned char payload[100];
            for (long i=0;i<mm_actor_events_count(track.events);++i)
            {
                Base event; mm_actor_events_get_fixed(track.events, i,&event); LegacyLong bytes=mm_actor_events_size(track.events, i);
                if (bytes>sizeof(payload)) return false; mm_actor_events_get_payload(track.events, i,payload);
                if (event.aet==aetAdd) payload[18]=payload[19]=0;
                if (event.aet==aetSnd) { MMActorSound value; std::memcpy(&value,payload,sizeof(value)); value.tag.cache=nullptr; std::memcpy(payload,&value,sizeof(value)); }
                if (event.aet==aetCost) { Costume value; std::memcpy(&value,payload,sizeof(value)); value.tag.pcrf=nullptr; std::memcpy(payload,&value,sizeof(value)); }
                if (!record(&event,sizeof(event)) || !record(&bytes,sizeof(bytes)) || !record(payload,bytes)) return false;
            }
            return true;
        };
        saved=dump(this) && saved; if (copy) saved=dump(copy) && saved;
        ReleasePpo(&copy); return saved;
    }
    bool FTestEditTransaction(int mode,FILE *snapshot,StageTestScene *scene)
    {
        using namespace ActorEvent;
        const int operation=mode/16;
        auto tmpl=static_cast<TrackTestTemplate *>(_Template()); tmpl->FailAfter(0);
        long frame=(mode&2)?2:1;
        if (!scene->TrackActor(this,frame) || !FGotoFrame(frame)) return false;
        if (operation==1 && !FScale(BR_SCALAR(1.5))) return false;
        if (operation==0 && (mode&4) && !(mode&8)) tmpl->FailAfter(1);
        PActor output=nullptr; bool alive=true,changed=false;
#ifdef DEBUG
        if (operation!=5 && (mode&8))
        { vmutxMem.Enter(); vdmglob.dmaglBase.cactDo=(mode&4)?4:1; vdmglob.dmaglBase.cactFail=1; vmutxMem.Leave(); }
#endif
        switch (operation)
        {
        case 0: changed=FSetAction(0,(mode&1)?2:0,(mode&2)!=0,(mode&1)?&output:nullptr); break;
        case 1: changed=FNormalize(mode&3); break;
        case 2:
        case 4:
        {
            const auto transaction = _TransactionServices();
            const auto edit = _StageEditServices();
            changed = mm_actor_transaction_stage(this, operation == 4, &edit, &transaction) != 0;
            break;
        }
        case 3: changed=FDelete((mode&4)?nullptr:&alive,(mode&1)!=0); break;
        }
#ifdef DEBUG
        vmutxMem.Enter(); vdmglob.dmaglBase.cactFail=0; vmutxMem.Leave();
#endif
        tmpl->FailAfter(0); vpers->Clear();
        unsigned char aliveByte=0; std::memcpy(&aliveByte,&alive,sizeof(aliveByte));
        auto record=[&](const void *p,size_t n) { return !snapshot || std::fwrite(p,1,n,snapshot)==n; };
        LegacyLong values[]={mode,changed,aliveByte,output!=nullptr,output && output->Pbody(),output && output->Pscen(),
            output?output->CactRef():0,_Template()->CactRef(),_Body()?mm_body_owner_references(_Body()):0,static_cast<PChunkyResourceFile>(_owner.runtime.template_tag.cache)->CactRef(),
            _owner.runtime.state.frame,_owner.runtime.state.onStage,_owner.runtime.state.action,_owner.runtime.state.cel,_owner.runtime.state.nextEvent,_owner.runtime.state.addEvent,_owner.runtime.state.actionEvent,_owner.runtime.state.firstEventInFrame,
            _owner.runtime.state.location.irpt,_owner.runtime.state.location.dwrOffset,_owner.runtime.state.location.dnfrm,_owner.runtime.first,_owner.runtime.last,_owner.runtime.life_dirty,
            mm_actor_path_count(_owner.track.route),mm_actor_events_count(_owner.track.events)};
        bool saved=record(values,sizeof(values)) && record(&_owner.runtime.state.transform,sizeof(_owner.runtime.state.transform)) && record(&_owner.runtime.state.position,sizeof(_owner.runtime.state.position));
        for (long i=0;saved && i<mm_actor_path_count(_owner.track.route);++i)
        { RouteDistancePoint point; mm_actor_path_get(_owner.track.route, i,&point); saved=record(&point,sizeof(point)); }
        unsigned char payload[100];
        for (long i=0;saved && i<mm_actor_events_count(_owner.track.events);++i)
        {
            Base event; mm_actor_events_get_fixed(_owner.track.events, i,&event); LegacyLong bytes=mm_actor_events_size(_owner.track.events, i);
            if (bytes>sizeof(payload)) return false; mm_actor_events_get_payload(_owner.track.events, i,payload);
            if (event.aet==aetAdd) payload[18]=payload[19]=0;
            saved=record(&event,sizeof(event)) && record(&bytes,sizeof(bytes)) && record(payload,bytes);
        }
        ReleasePpo(&output); return saved;
    }
    bool FTestTransformEdit(int mode,FILE *snapshot,StageTestScene *scene)
    {
        using namespace ActorEvent;
        const int group=mode/128;
        auto tmpl=static_cast<TrackTestTemplate *>(_Template()); tmpl->FailAfter(0); tmpl->SetTestText((mode&4)!=0);
        if (_Body()) { mm_body_owner_release(_Body()); _owner.runtime.body = nullptr; } _owner.runtime.body = _Template()->PbodyCreate(); if (!_Body()) return false;
        ActorTrack track; if (!track.FCreate()) return false;
        const int coordinates[]={0,4,10,14,30};
        for (int i=0;i<5;++i)
        {
            RouteDistancePoint point={{BR_SCALAR(coordinates[i]),rZero,rZero},(i==0 || i==2)?BR_SCALAR(4):rZero};
            if (!mm_actor_path_insert(track.Route(), i,&point)) return false;
        }
        auto append=[&](int type,int frame,int node,int offset,int hold,const void *payload,long bytes) {
            Base event={type,frame,{node,BR_SCALAR(offset),hold}};
            return mm_actor_events_insert(track.Events(), mm_actor_events_count(track.Events()),bytes,const_cast<void *>(payload),&event);
        };
        Add entrance={}; Action action={0,0}; int32_t freeze=1; BRS step=0,scale=BR_SCALAR(2);
        BMAT34 forward,current; BrMatrix34Identity(&forward); BrMatrix34PreRotateX(&forward,0x1000);
        BrMatrix34PreRotateY(&forward,0x2345); BrMatrix34Identity(&current); BrMatrix34PostRotateY(&current,0x4567);
        forward.m[3][0]=123; current.m[3][2]=-456;
        RoutePoint tweak={BR_SCALAR(10),(mode&8)?-rOne:rOne,rZero};
        Stretch pull={BR_SCALAR(2),rOneHalf,rOne};
        if (!append(aetAdd,5,0,0,0,&entrance,sizeof(entrance)) || !append(aetActn,5,0,0,0,&action,sizeof(action)) ||
            !append(aetFreeze,5,0,0,0,&freeze,sizeof(freeze)) || !append(aetStep,5,0,0,0,&step,sizeof(step)) ||
            !append(aetAdd,20,2,0,0,&entrance,sizeof(entrance)) || !append(aetActn,20,2,0,0,&action,sizeof(action)) ||
            !append(aetRotF,20,2,0,0,&forward,sizeof(forward)) || !append(aetRotH,20,2,0,0,&current,sizeof(current)) ||
            !append(aetTweak,20,2,0,0,&tweak,sizeof(tweak)) || !append(aetSize,22,2,2,0,&scale,sizeof(scale)) ||
            !append(aetPull,23,3,0,0,&pull,sizeof(pull)) || !append(aetFreeze,24,3,0,1,&freeze,sizeof(freeze)) ||
            !append(aetAdd,40,4,0,0,&entrance,sizeof(entrance)) || !append(aetActn,40,4,0,0,&action,sizeof(action))) return false;
        mm_actor_track_swap(&_owner.track, &track.Core()); mm_actor_sounds_clear(&_owner.sounds); _owner.runtime.state=ActorFrameState(); ActorFrameTest::Rewind(_owner.runtime.state, _owner.track);
        _owner.runtime.state.frame=20; _owner.runtime.state.location={2,rZero,0}; _owner.runtime.state.position=tweak; _owner.runtime.state.onStage=true; EngineBody::Show(_Body());
        _owner.runtime.state.addEvent=4; _owner.runtime.state.actionEvent=5; _owner.runtime.state.firstEventInFrame=4; _owner.runtime.state.nextEvent=9;
        _owner.runtime.state.celCount=4; _owner.runtime.state.actionFlags=(mode&32)?factnRotateX|factnRotateY|factnRotateZ:factnRotateY;
        std::memcpy(&_owner.runtime.state.transform.bmat34Fwd, &forward, sizeof(forward));
        std::memcpy(&_owner.runtime.state.transform.bmat34Cur, &current, sizeof(current));
        _owner.runtime.state.useCurrentRotation=(mode&2)!=0;
        _owner.runtime.state.transform.xaPath=(mode&8)?0:0x1234; _owner.runtime.state.transform.yaPath=0x4000; _owner.runtime.state.transform.zaPath=(mode&16)?0xFFF0:0;
        const BRS oldScales[]={rOne,BR_SCALAR(2),krScaleMin,krScaleMax-rEps};
        _owner.runtime.state.transform.rScaleStep=oldScales[(mode/8)%4]; _owner.runtime.state.transform.aevpull=pull;
        _owner.runtime.state.routeTranslation={BR_SCALAR(3),rOne,BR_SCALAR(2)}; _owner.runtime.origin=_owner.runtime.state.routeTranslation;
        _owner.runtime.first=5; _owner.runtime.last=50; _owner.runtime.life_dirty=group==8 && (mode&16); _owner.runtime.recording.recording=_owner.runtime.recording.rejoining=false;
        if (!scene->TrackActor(this,_owner.runtime.state.frame)) return false;
        if (_owner.runtime.life_dirty && (mode&32)) tmpl->FailAfter(1);
        const BRA angles[]={0,1,0x1234,0x4000,0x8000,0xFFF0,0x1000,0};
        BRA x=angles[(mode/4)%8],y=angles[(mode/8)%8],z=angles[(mode/16)%8];
        const BRS factors[]={-rOne,0,krScaleMinFactor-1,krScaleMinFactor,krScaleMinFactor+1,rOne,rOneHalf,
                             BR_SCALAR(1.5),krScaleMaxFactor-1,krScaleMaxFactor,krScaleMaxFactor+1,0x7fffffff};
        const long count=sizeof(factors)/sizeof(factors[0]); BRS factor=factors[mode%count];
        BRS dx=(mode&16)?0:rOne,dy=(mode&32)?-BR_SCALAR(4):rOneHalf,dz=(mode&4)?0:rOneHalf;
        if (mode&64) dx=dy=dz=0;
        ulong flags=((mode&1)?fmafEntireScene:0)|((mode&2)?fmafEntireSubrte:0)|((mode&8)?fmafGround:0)|((group==7)?fmafOrient:0);
        bool moved=false,ok=false;
        std::fprintf(stderr,"[transform fixture] mode=%d\n",mode);
        switch (group)
        {
        case 0: ok=FRotate(x,y,z,(mode&1)!=0); break;
        case 1: ok=FRotateAroundWorld(x,y,z,(mode&1)!=0); break;
        case 2: ok=FNormalizeCore(mode%4); break;
        case 3: ok=FScale(factor); break;
        case 4: ok=FPull(factor,factors[(mode/3)%count],factors[(mode/5)%count]); break;
        case 5: ok=FTweakRoute(dx,dy,dz,flags); break;
        case 6: case 7: ok=FMoveRoute(dx,dy,dz,&moved,flags); break;
        case 8: { const long shifts[]={0,1,-1,4,-3,12,25,-18}; ok=FSoonerLater(shifts[mode%8]); break; }
        }
        tmpl->FailAfter(0); tmpl->SetTestText(false); vpers->Clear();
        auto record=[&](const void *p,size_t n) { return !snapshot || std::fwrite(p,1,n,snapshot)==n; };
        LegacyLong values[]={mode,ok,moved,_owner.runtime.state.nextEvent,_owner.runtime.state.firstEventInFrame,_owner.runtime.state.actionEvent,_owner.runtime.state.addEvent,
            _owner.runtime.state.location.irpt,_owner.runtime.state.location.dwrOffset,_owner.runtime.state.location.dnfrm,_owner.runtime.state.frame,_owner.runtime.state.onStage,_owner.runtime.state.frozen,
            _owner.runtime.state.useCurrentRotation,_owner.runtime.state.action,_owner.runtime.state.cel,_owner.runtime.state.step,_owner.runtime.first,_owner.runtime.last,_owner.runtime.life_dirty,scene->Nfrm(),
            mm_actor_path_count(_owner.track.route),mm_actor_events_count(_owner.track.events)};
        bool saved=record(values,sizeof(values)) && record(&_owner.runtime.state.position,sizeof(_owner.runtime.state.position)) &&
            record(&_owner.runtime.state.transform,sizeof(_owner.runtime.state.transform)) && record(&_owner.runtime.state.subrouteTranslation,sizeof(_owner.runtime.state.subrouteTranslation)) &&
            record(&_owner.runtime.state.routeTranslation,sizeof(_owner.runtime.state.routeTranslation)) && record(&_owner.runtime.origin,sizeof(_owner.runtime.origin));
        for (long i=0;saved && i<mm_actor_path_count(_owner.track.route);++i)
        { RouteDistancePoint point; mm_actor_path_get(_owner.track.route, i,&point); saved=record(&point,sizeof(point)); }
        unsigned char payload[100];
        for (long i=0;saved && i<mm_actor_events_count(_owner.track.events);++i)
        {
            Base event; mm_actor_events_get_fixed(_owner.track.events, i,&event); LegacyLong bytes=mm_actor_events_size(_owner.track.events, i);
            if (bytes>sizeof(payload)) return false; mm_actor_events_get_payload(_owner.track.events, i,payload);
            saved=record(&event,sizeof(event)) && record(&bytes,sizeof(bytes)) && record(payload,bytes);
        }
        return saved;
    }
    bool FTestRecording(int mode,FILE *snapshot,StageTestScene *scene,bool aliasOutputs=false)
    {
        using namespace ActorEvent;
        const int group=mode/128;
        auto tmpl=static_cast<TrackTestTemplate *>(_Template());
        tmpl->FailAfter(0); tmpl->SetTestText(false);
        if (_Body()) { mm_body_owner_release(_Body()); _owner.runtime.body = nullptr; } _owner.runtime.body = _Template()->PbodyCreate(); if (!_Body()) return false;
        ActorTrack track; if (!track.FCreate()) return false;
        for (int i=0;i<5;++i)
        {
            RouteDistancePoint point={{BR_SCALAR(i*4),rZero,rZero},i<3?BR_SCALAR(4):rZero};
            if (!mm_actor_path_insert(track.Route(), i,&point)) return false;
        }
        auto append=[&](int type,int frame,int node,int hold,const void *payload,long bytes) {
            Base event={type,frame,{node,rZero,hold}};
            return mm_actor_events_insert(track.Events(), mm_actor_events_count(track.Events()),bytes,const_cast<void *>(payload),&event);
        };
        Add entrance={}; Action action={0,0}; BRS step=kdwrNil,scale=BR_SCALAR(2); int32_t freeze=1;
        RoutePoint tweak={BR_SCALAR((group==2 && (mode&4))?12:4),rZero,rZero};
        if (!append(aetAdd,1,0,0,&entrance,sizeof(entrance)) || !append(aetActn,1,0,0,&action,sizeof(action)) ||
            !append(aetStep,1,0,0,&step,sizeof(step)) || !append(aetTweak,3,1,0,&tweak,sizeof(tweak)) ||
            !append(aetSize,5,2,0,&scale,sizeof(scale))) return false;
        scale=BR_SCALAR(3);
        if (!append(aetSize,6,2,1,&scale,sizeof(scale)) || !append(aetFreeze,6,2,1,&freeze,sizeof(freeze)) ||
            !append(aetActn,8,3,0,&action,sizeof(action)) ||
            !append(aetRem,9,(group==0 && (mode&8))?2:3,1,nullptr,0) ||
            !append(aetAdd,30,4,0,&entrance,sizeof(entrance)) || !append(aetActn,30,4,0,&action,sizeof(action))) return false;
        // An early Remove belongs before the later-node events.
        if (group==0 && (mode&8))
        {
            Base early; mm_actor_events_get_fixed(track.Events(), 8,&early); early.nfrm=7;
            mm_actor_events_delete(track.Events(), 8); if (!mm_actor_events_insert(track.Events(), 7,0,nullptr,&early)) return false;
        }
        mm_actor_track_swap(&_owner.track, &track.Core()); mm_actor_sounds_clear(&_owner.sounds); _owner.runtime.state=ActorFrameState(); ActorFrameTest::Rewind(_owner.runtime.state, _owner.track);
        _owner.runtime.state.frame=3; _owner.runtime.state.location={1,rZero,0}; _owner.runtime.state.position=tweak;
        _owner.runtime.state.onStage=!(group==2 && (mode&32) && (mode&2)); _owner.runtime.state.frozen=(mode&4)!=0;
        if (_owner.runtime.state.onStage) EngineBody::Show(_Body());
        _owner.runtime.state.action=0; _owner.runtime.state.cel=(mode&64)?1:0; _owner.runtime.state.celCount=4; _owner.runtime.state.actionFlags=factnRotateY;
        _owner.runtime.state.step=(group==2 && (mode&64))?rZero:kdwrNil;
        _owner.runtime.state.addEvent=0; _owner.runtime.state.actionEvent=1; _owner.runtime.state.firstEventInFrame=3; _owner.runtime.state.nextEvent=4;
        _owner.runtime.state.routeTranslation={rZero,rOne,rZero}; _owner.runtime.origin={rZero,rOne,rZero};
        _owner.runtime.first=1; _owner.runtime.last=40; _owner.runtime.life_dirty=group==0 && (mode&32);
        _owner.runtime.recording.recording=group==1 ? !(mode&1) : group==2 ? !(mode&2) : false;
        _owner.runtime.recording.rejoining=!(mode&16); _owner.runtime.recording.inserted=!(mode&8); _owner.runtime.recording.gap=-2;
        _owner.runtime.recording.insertion={0,rZero,0}; _owner.runtime.recording.raw={3,-5,7}; _owner.runtime.recording.timestamp=(mode&64)?0xffffff00UL:1000;
        if (group==0 && (mode&2)) _owner.runtime.state.location.dwrOffset=BR_SCALAR(2);
        if (group==2) { RouteDistancePoint point; mm_actor_path_get(_owner.track.route, 1,&point); point.dwr=0; mm_actor_path_put(_owner.track.route, 1,&point); }
        if (!scene->TrackActor(this,_owner.runtime.state.frame)) return false;
        PActor restore=nullptr; if (!FDup(&restore,true)) return false;
        if ((group==0 && (mode&32) && (mode&64)) || (group==1 && (mode&32))) tmpl->FailAfter((mode&64)?2:1);
        bool frame=true,route=true,ok=true;
        const ulong now=_owner.runtime.recording.timestamp+((mode&16)?750:749);
        const ulong flags=((mode&2)?fmafFreeze:0)|((mode&4)?fmafGround:0);
        BRS dx=(mode&2)?rEps:BR_SCALAR(6),dy=(mode&8)?-BR_SCALAR(9):rZero;
        const auto owners = _OwnerServices();
        const auto services = _RecordServices(&owners);
        const MMActorPoint delta{dx, dy, rEps};
        struct Outputs { bool *frame, *route; } outputs{&frame, aliasOutputs ? &frame : &route};
        MMActorRecordResult result{};
        result.context = &outputs;
        result.publish = [](void *p, int frame, int route, unsigned changed) {
            auto out = static_cast<Outputs *>(p);
            if (changed & 1) *out->frame = frame != 0;
            if (changed & 2) *out->route = route != 0;
        };
        const bool replace = (mode & 1) != 0;
        std::fprintf(stderr,"[record fixture] mode=%d\n",mode);
        if (group==0) ok=mm_actor_owner_record_begin(replace,restore,&services)!=0;
        else if (group==1) ok=mm_actor_owner_record_move(&delta,flags,now,&result,restore,&services)!=0;
        else if (group==2) ok=mm_actor_owner_record_end(replace,restore,&services)!=0;
        else if (group==3)
        {
            const MMActorPoint threshold{(mode&2)?kdwrThreshRte:kdwrThreshRte-rEps,rZero,rZero};
            ok=mm_actor_record_valid(&_owner.runtime.recording,&threshold,now,&services)!=0;
        }
        else
        {
            ok=mm_actor_owner_record_begin(replace,restore,&services)!=0;
            if (ok) ok=mm_actor_owner_record_move(&delta,flags,now,&result,restore,&services)!=0;
            if (ok) ok=mm_actor_owner_record_end(replace,restore,&services)!=0;
        }
        tmpl->FailAfter(0); vpers->Clear(); ReleasePpo(&restore);
        auto record=[&](const void *p,size_t n) { return !snapshot || std::fwrite(p,1,n,snapshot)==n; };
        LegacyLong values[]={mode,ok,frame,route,_owner.runtime.state.nextEvent,_owner.runtime.state.firstEventInFrame,_owner.runtime.state.actionEvent,_owner.runtime.state.addEvent,
            _owner.runtime.state.location.irpt,_owner.runtime.state.location.dwrOffset,_owner.runtime.state.location.dnfrm,_owner.runtime.state.frame,_owner.runtime.state.onStage,_owner.runtime.state.frozen,
            _owner.runtime.state.action,_owner.runtime.state.cel,_owner.runtime.state.step,_owner.runtime.first,_owner.runtime.last,_owner.runtime.life_dirty,_owner.runtime.recording.recording,_owner.runtime.recording.rejoining,_owner.runtime.recording.inserted,_owner.runtime.recording.gap,
            _owner.runtime.recording.insertion.irpt,_owner.runtime.recording.insertion.dwrOffset,_owner.runtime.recording.insertion.dnfrm,static_cast<LegacyLong>(_owner.runtime.recording.timestamp),scene->Nfrm(),
            mm_actor_path_count(_owner.track.route),mm_actor_events_count(_owner.track.events)};
        bool saved=record(values,sizeof(values)) && record(&_owner.runtime.state.position,sizeof(_owner.runtime.state.position)) &&
            record(&_owner.runtime.state.transform,sizeof(_owner.runtime.state.transform)) && record(&_owner.runtime.recording.raw,sizeof(_owner.runtime.recording.raw));
        for (long i=0;saved && i<mm_actor_path_count(_owner.track.route);++i)
        { RouteDistancePoint point; mm_actor_path_get(_owner.track.route, i,&point); saved=record(&point,sizeof(point)); }
        unsigned char payload[100];
        for (long i=0;saved && i<mm_actor_events_count(_owner.track.events);++i)
        {
            Base event; mm_actor_events_get_fixed(_owner.track.events, i,&event); LegacyLong bytes=mm_actor_events_size(_owner.track.events, i);
            if (bytes>sizeof(payload)) return false;
            mm_actor_events_get_payload(_owner.track.events, i,payload);
            saved=record(&event,sizeof(event)) && record(&bytes,sizeof(bytes)) && record(payload,bytes);
        }
        return saved && (!aliasOutputs || (ok && frame && route));
    }
    bool FTestStageEdit(int mode,FILE *snapshot)
    {
        using namespace ActorEvent;
        const int kind=mode/32;
        auto tmpl=static_cast<TrackTestTemplate *>(_Template());
        tmpl->FailAfter(0); tmpl->SetTestText((mode&1)!=0);
        if (_Body()) { mm_body_owner_release(_Body()); _owner.runtime.body = nullptr; } _owner.runtime.body = _Template()->PbodyCreate();
        if (!_Body()) return false;
        ActorTrack track; if (!track.FCreate()) return false;
        auto append=[&](long type,long frame,int node,const void *payload,long bytes) {
            Base event={static_cast<int32_t>(type),static_cast<int32_t>(frame),{node,rZero,0}};
            return mm_actor_events_insert(track.Events(), mm_actor_events_count(track.Events()),bytes,const_cast<void *>(payload),&event);
        };
        Add addition={}; Action action={0,0}; int32_t step=0; BRS scale=BR_SCALAR(2);
        if (kind)
        {
            RouteDistancePoint p={{rZero,rZero,rZero},kind==3?BR_SCALAR(5):rZero};
            if (!mm_actor_path_insert(track.Route(), 0,&p)) return false;
            if (kind==3) { p={{BR_SCALAR(5),rZero,rZero},rZero}; if (!mm_actor_path_insert(track.Route(), 1,&p)) return false; }
            long first=kind==1?5:1;
            if (!append(aetAdd,first,0,&addition,sizeof(addition)) || !append(aetActn,first,0,&action,sizeof(action)) ||
                !append(aetStep,first,0,&step,sizeof(step)) || !append(aetSize,first,0,&scale,sizeof(scale))) return false;
            if (mode&16) if (!append(aetRem,5,0,nullptr,0)) return false;
            p={{BR_SCALAR(20),rZero,rZero},rZero};
            int node=mm_actor_path_count(track.Route());
            if (!mm_actor_path_insert(track.Route(), node,&p) || !append(aetAdd,10,node,&addition,sizeof(addition)) ||
                !append(aetActn,10,node,&action,sizeof(action)) || !append(aetStep,10,node,&step,sizeof(step))) return false;
        }
        mm_actor_track_swap(&_owner.track, &track.Core()); mm_actor_sounds_clear(&_owner.sounds); _owner.runtime.state=ActorFrameState(); ActorFrameTest::Rewind(_owner.runtime.state, _owner.track);
        _owner.runtime.state.action=0; _owner.runtime.state.celCount=4; _owner.runtime.state.actionFlags=factnRotateY;
        _owner.runtime.state.frame=kind==0?3:kind==1?1:kind==2?7:kind==6?1:kind==7?10:3;
        _owner.runtime.state.onStage=kind==3 || kind>=5;
        if (_owner.runtime.state.onStage) EngineBody::Show(_Body());
        _owner.runtime.state.addEvent=kind<=1?-1:kind==7?((mode&16)?5:4):0;
        _owner.runtime.state.nextEvent=kind<=1?0:kind==7?mm_actor_events_count(_owner.track.events):4;
        _owner.runtime.state.firstEventInFrame=kind==7?_owner.runtime.state.addEvent:0;
        _owner.runtime.state.actionEvent=kind<=1?-1:kind==7?_owner.runtime.state.addEvent+1:1;
        _owner.runtime.state.location.irpt=kind==7?1:0;
        if (kind==3) _owner.runtime.state.location.dwrOffset=BR_SCALAR(1);
        _owner.runtime.state.routeTranslation={BR_SCALAR(2),BR_SCALAR(3),BR_SCALAR(4)};
        _owner.runtime.origin={BR_SCALAR(11),BR_SCALAR(12),BR_SCALAR(13)};
        _owner.runtime.first=(mode&2)?4:1; _owner.runtime.last=(mode&2)?5:20;
        _owner.runtime.life_dirty=false; _owner.runtime.recording.recording=(mode&4)!=0; _owner.runtime.recording.rejoining=false;
        tmpl->FailAfter((mode&8)?((mode&16)?3:1):0);
        const auto services = _StageEditServices();
        bool ok=mm_actor_owner_stage_edit(&_owner,kind<4,&services)!=0;
        tmpl->FailAfter(0); tmpl->SetTestText(false); vpers->Clear();
        auto record=[&](const void *p,size_t n) { return !snapshot || std::fwrite(p,1,n,snapshot)==n; };
        LegacyLong values[]={mode,ok,_owner.runtime.state.nextEvent,_owner.runtime.state.firstEventInFrame,_owner.runtime.state.actionEvent,_owner.runtime.state.addEvent,
            _owner.runtime.state.location.irpt,_owner.runtime.state.location.dwrOffset,_owner.runtime.state.location.dnfrm,_owner.runtime.state.frame,_owner.runtime.state.onStage,_owner.runtime.state.frozen,
            _owner.runtime.state.action,_owner.runtime.state.cel,_owner.runtime.state.step,_owner.runtime.first,_owner.runtime.last,_owner.runtime.life_dirty,_owner.runtime.recording.recording,
            mm_actor_path_count(_owner.track.route),mm_actor_events_count(_owner.track.events)};
        bool saved=record(values,sizeof(values)) && record(&_owner.runtime.state.transform,sizeof(_owner.runtime.state.transform)) &&
            record(&_owner.runtime.state.position,sizeof(_owner.runtime.state.position)) && record(&_owner.runtime.state.subrouteTranslation,sizeof(_owner.runtime.state.subrouteTranslation));
        for (long i=0;saved && i<mm_actor_path_count(_owner.track.route);++i)
        { RouteDistancePoint point; mm_actor_path_get(_owner.track.route, i,&point); saved=record(&point,sizeof(point)); }
        unsigned char payload[100];
        for (long i=0;saved && i<mm_actor_events_count(_owner.track.events);++i)
        {
            Base event; mm_actor_events_get_fixed(_owner.track.events, i,&event);
            LegacyLong bytes=mm_actor_events_size(_owner.track.events, i); if (bytes>sizeof(payload)) return false;
            mm_actor_events_get_payload(_owner.track.events, i,payload);
            // The native Add's final two bytes are uninitialized ABI padding.
            if (event.aet==aetAdd) payload[18]=payload[19]=0;
            saved=record(&event,sizeof(event)) && record(&bytes,sizeof(bytes)) && record(payload,bytes);
        }
        return saved;
    }
    bool FTestSubrouteEdit(int mode,FILE *snapshot,PChunkyResourceFile resource,ChunkNumber templateNumber,ChunkNumber materialNumber,bool owningCopy=false)
    {
        using namespace ActorEvent;
        auto tmpl=static_cast<TrackTestTemplate *>(resource->PbacoFetch(kctgTmpl,templateNumber,TrackTestTemplate::FReadSubrouteFixture));
        if (!tmpl) return false;
        if (_Template()) { _Template()->Release(); _owner.runtime.template_object = nullptr; } _owner.runtime.template_object = tmpl;
        auto material=EngineMaterial::Fetch(resource, kctgMtrl, materialNumber);
        PDynamicArray parents=DynamicArray::PglNew(sizeof(short)),sets=DynamicArray::PglNew(sizeof(short));
        short parent=-1,set=0;
        bool ok=material && parents && sets && parents->FAdd(&parent) && sets->FAdd(&set);
        if (_Body()) { mm_body_owner_release(_Body()); _owner.runtime.body = nullptr; } _owner.runtime.body = ok ? EngineBody::Create(parents,sets) : pvNil;
        ReleasePpo(&parents); ReleasePpo(&sets);
        if (!_Body()) { mm_material_owner_release(material); return false; }
        EngineBody::SetPartSetMtrl(_Body(), 0,material); mm_material_owner_release(material);
        ActorTrack track; if (!track.FCreate()) return false;
        for (int i=0;i<3;++i)
        {
            RouteDistancePoint point={{BR_SCALAR(i*10),rZero,rZero},(i==0 && mode<256)?BR_SCALAR(5):rZero};
            if (!mm_actor_path_insert(track.Route(), i,&point)) return false;
        }
        auto append=[&](int kind,int frame,int node,int offset,int hold,int scalar=0) {
            Base event={kind,frame,{node,BR_SCALAR(offset),hold}};
            union { Add add; Action action; Costume costume; MMActorSound sound; RoutePoint point; BMAT34 matrix; int32_t scalar; } data={};
            int bytes=4;
            switch (kind)
            {
            case aetAdd: bytes=sizeof(Add); data.add.xa=0xFFF0; data.add.ya=0x8000; data.add.za=0x0010; break;
            case aetActn: bytes=sizeof(Action); data.action={0,1}; break;
            case aetCost:
                bytes=sizeof(Costume); data.costume.ibset=0; data.costume.cmid=scalar; data.costume.fCmtl=owningCopy?tNo:tYes;
                if (owningCopy) { data.costume.tag={ksidUseCrf,resource,kctgMtrl,materialNumber}; TagManager::DupTag(&data.costume.tag); }
                break;
            case aetSnd: bytes=sizeof(MMActorSound); data.sound.cel=-1; data.sound.style=2; data.sound.volume=100; data.sound.tag={2,pvNil,kctgMsnd,0}; break;
            case aetRotF: case aetRotH: bytes=sizeof(BMAT34); BrMatrix34Identity(&data.matrix); data.matrix.m[3][0]=BR_SCALAR(2); break;
            case aetTweak: case aetPull: bytes=sizeof(RoutePoint); data.point={BR_SCALAR(2),rOne,rOne}; break;
            default: data.scalar=scalar; break;
            }
            if (mm_actor_events_insert(track.Events(), mm_actor_events_count(track.Events()),bytes,&data,&event)) return true;
            if (kind==aetCost && owningCopy) TagManager::CloseTag(&data.costume.tag);
            return false;
        };
        const bool backward=mode<256,later=mode>=256 && mode<320,previous=mode>=320 && mode<448;
        if (backward)
            ok=append(aetAdd,1,0,0,0) && append(aetActn,1,0,0,0) && append(aetRotH,2,0,0,0) &&
               append(aetTweak,3,0,2,0) && append(aetSnd,3,0,2,0) && append(aetSize,3,0,2,0,BR_SCALAR(2)) &&
               append(aetFreeze,4,0,2,2,1) && append(aetAdd,8,2,0,0) && append(aetStep,8,2,0,0);
        else if (later)
        {
            ok=append(aetAdd,1,0,0,0);
            if (!(mode&16)) ok=ok && append(aetAdd,5,1,0,0);
            ok=ok && append(aetCost,5,1,0,0,(mode&8)?2:0) &&
               append(aetSize,5,1,0,(mode&1)?1:0,BR_SCALAR(2)) && append(aetPull,5,1,0,0) && append(aetRotF,5,1,0,0);
        }
        else if (previous)
            ok=append(aetAdd,(mode&32)?5:1,0,0,0) && append(aetCost,1,0,0,0) &&
               append(aetSize,(mode&16)?5:2,0,0,0,BR_SCALAR(2)) && append(aetPull,2,0,0,0) &&
               append(aetRotF,3,0,0,0) && append(aetActn,1,0,0,0) && append(aetStep,3,0,0,1) &&
               append(aetAdd,5,1,0,0) && append(aetStep,6,1,0,1);
        else ok=append(aetAdd,1,0,0,0);
        if (!ok) return false;
        mm_actor_track_swap(&_owner.track, &track.Core()); mm_actor_sounds_clear(&_owner.sounds); _owner.runtime.state=ActorFrameState(); ActorFrameTest::Rewind(_owner.runtime.state, _owner.track);
        _owner.runtime.state.onStage=true; _owner.runtime.state.action=0; _owner.runtime.state.cel=2; _owner.runtime.state.celCount=4; _owner.runtime.state.actionFlags=factnRotateY;
        _owner.runtime.state.frame=backward?3:previous?5:1; _owner.runtime.state.location={0,backward?BR_SCALAR(2):rZero,0};
        _owner.runtime.state.nextEvent=backward?6:previous?8:1; _owner.runtime.state.firstEventInFrame=backward?3:previous?7:0;
        _owner.runtime.state.actionEvent=backward?1:previous?5:ivNil; _owner.runtime.state.addEvent=previous?7:0;
        if (previous) _owner.runtime.state.location.irpt=1;
        _owner.runtime.state.transform.xaPath=0x1234; _owner.runtime.state.transform.yaPath=0x5678; _owner.runtime.state.transform.zaPath=0x9ABC;
        _owner.runtime.first=1; _owner.runtime.last=20; _owner.runtime.life_dirty=false; _owner.runtime.recording.recording=false; _owner.runtime.recording.rejoining=false;
        tmpl->SetTestText(backward && (mode&128));
        bool alive=true; LegacyLong averaged=-1;
        if (backward)
        {
            if (mode&1) _owner.runtime.state.location.dwrOffset=rZero;
            if (mode&2) { _owner.runtime.state.location={1,rZero,0}; }
            if (mode&4) { _owner.runtime.state.onStage=false; _owner.runtime.state.nextEvent=7; }
            if (mode&8) _owner.runtime.recording.recording=true;
            if (mode&16) _owner.runtime.life_dirty=true;
            if (mode&32) _owner.runtime.state.frame=1;
            tmpl->FailAfter((mode&64)?1:0);
            DeleteBackCore(&alive);
        }
        else if (later)
        {
            tmpl->FailAfter((mode&4)?1:0);
            ok=_FAddAevFromLater();
        }
        else if (previous)
        {
            tmpl->FailAfter((mode&8)?1:0);
            ulong mask=((mode&1)?faetActn:0)|((mode&2)?faetCost:0)|((mode&4)?faetPull|faetSize|faetRotF:0);
            ok=_FAddAevFromPrev(7,mask);
        }
        else
        {
            RoutePoint delta={BR_SCALAR((mode%16)*.25),BR_SCALAR((mode%16)*.333),rZero};
            SetAddOrient(0x0010,0xFFFF,0x8000,mode%8,(mode&8)?pvNil:&delta);
            const BRS weights[]={rZero,rEps,rOneHalf,rOne-rEps,rOne};
            averaged=_BraAvgAngle(0xFFF0,0x0010,weights[mode%5]);
        }
        tmpl->FailAfter(0); tmpl->SetTestText(false); vpers->Clear();
        auto record=[&](const void *p,size_t n) { return !snapshot || std::fwrite(p,1,n,snapshot)==n; };
        LegacyLong values[]={mode,ok,alive,averaged,_owner.runtime.state.nextEvent,_owner.runtime.state.firstEventInFrame,_owner.runtime.state.actionEvent,_owner.runtime.state.addEvent,
            _owner.runtime.state.location.irpt,_owner.runtime.state.location.dwrOffset,_owner.runtime.state.location.dnfrm,_owner.runtime.state.frame,_owner.runtime.state.onStage,_owner.runtime.state.frozen,
            _owner.runtime.state.cel,_owner.runtime.state.step,_owner.runtime.first,_owner.runtime.last,_owner.runtime.life_dirty,_owner.runtime.recording.recording,_owner.runtime.state.transform.xaPath,_owner.runtime.state.transform.yaPath,_owner.runtime.state.transform.zaPath,
            mm_actor_path_count(_owner.track.route),mm_actor_events_count(_owner.track.events)};
        bool saved=record(values,sizeof(values)) && record(&_owner.runtime.state.transform,sizeof(_owner.runtime.state.transform));
        for (long i=0;saved && i<mm_actor_path_count(_owner.track.route);++i)
        { RouteDistancePoint point; mm_actor_path_get(_owner.track.route, i,&point); saved=record(&point,sizeof(point)); }
        unsigned char payload[100];
        for (long i=0;saved && i<mm_actor_events_count(_owner.track.events);++i)
        {
            Base event; mm_actor_events_get_fixed(_owner.track.events, i,&event); saved=record(&event,sizeof(event));
            long bytes=mm_actor_events_size(_owner.track.events, i);
            if (event.aet==aetCost)
            { Costume c; mm_actor_events_get_payload(_owner.track.events, i,&c); LegacyLong fields[]={c.ibset,c.cmid,c.fCmtl}; saved=saved && record(fields,sizeof(fields)); }
            else if (event.aet==aetSnd)
            { MMActorSound s; mm_actor_events_get_payload(_owner.track.events, i,&s); LegacyLong fields[]={s.cel,s.style,s.volume,s.tag.sid}; saved=saved && record(fields,sizeof(fields)); }
            else { if (bytes>sizeof(payload)) return false; mm_actor_events_get_payload(_owner.track.events, i,payload); saved=saved && record(payload,bytes); }
        }
        EngineBody::Hide(_Body()); if (_Body()) { mm_body_owner_release(_Body()); _owner.runtime.body = nullptr; } mm_actor_track_dispose(&_owner.track, &EngineActorTagServices());
        return saved;
    }
    bool FTestRouteEdit(int mode,FILE *snapshot)
    {
        using namespace ActorEvent;
        ActorTrack track; if (!track.FCreate()) return false;
        const long distances[]={4,6,0,5,0,0};
        const long positions[]={0,4,10,20,25,30};
        for (int i=0;i<6;++i)
        {
            RouteDistancePoint point={{BR_SCALAR(positions[i]),rZero,rZero},BR_SCALAR(distances[i])};
            if (!mm_actor_path_insert(track.Route(), i,&point)) return false;
        }
        auto append=[&](long kind,long frame,long node,long offset,long hold) {
            Base e={kind,frame,{node,BR_SCALAR(offset),hold}};
            union { Add add; Action action; RoutePoint point; int32_t scalar; } value={};
            long bytes=4;
            if (kind==aetAdd) bytes=sizeof(Add);
            else if (kind==aetActn) { value.action={0,0}; bytes=sizeof(Action); }
            else if (kind==aetTweak) { value.point={rOne,rZero,rZero}; bytes=sizeof(RoutePoint); }
            else if (kind==aetRem) bytes=0;
            else value.scalar=kind==aetFreeze ? 1 : 0;
            return mm_actor_events_insert(track.Events(), mm_actor_events_count(track.Events()),bytes,&value,&e);
        };
        if (!append(aetAdd,1,0,0,0) || !append(aetActn,1,0,0,0) || !append(aetTweak,3,0,2,0) ||
            !append(aetFreeze,4,1,0,0) || !append(aetStep,5,2,0,0) || !append(aetAdd,8,3,0,0) ||
            !append(aetStep,10,4,0,0) || !append(aetRem,11,4,0,1) || !append(aetAdd,15,5,0,0) ||
            !append(aetFreeze,15,5,0,0)) return false;
        mm_actor_track_swap(&_owner.track, &track.Core()); mm_actor_sounds_clear(&_owner.sounds);
        _owner.runtime.state=ActorFrameState(); ActorFrameTest::Rewind(_owner.runtime.state, _owner.track);
        _owner.runtime.state.frame=3; _owner.runtime.state.location={0,BR_SCALAR(2),0}; _owner.runtime.state.onStage=true; _owner.runtime.state.celCount=4;
        _owner.runtime.state.nextEvent=3; _owner.runtime.state.firstEventInFrame=2; _owner.runtime.state.actionEvent=1; _owner.runtime.state.addEvent=0;
        _owner.runtime.first=1; _owner.runtime.last=15; _owner.runtime.life_dirty=false; _owner.runtime.recording.recording=true;
        bool ok=true,alive=true; long found=-99;
        RouteDistancePoint supplied={{BR_SCALAR(99),rZero,rZero},rOne};
        if (mode<128)
        {
            long node=(mode%64)%6,first=((mode%64)/6)%4;
            if (mode&32)
            {
                Base e; mm_actor_events_get_fixed(_owner.track.events, 2,&e); e.rtel.dwrOffset=klwMax-1; mm_actor_events_set_fixed(_owner.track.events, 2,&e);
            }
            if (mode<64) _AdjustAevForRteIns(node,first);
            else _AdjustAevForRteDel(node,first);
        }
        else if (mode<160)
        {
            const int nodes[]={0,1,2,3,4,6}; long node=nodes[(mode-128)%6];
            bool moving=node==1 || node==2 || node==4;
            supplied.dwr=moving ? rOne : rZero;
            BRS prior=moving ? ((mode&8)?rZero:((mode&16)?BR_SCALAR(8):BR_SCALAR(2))) : rZero;
            ok=_FInsertGgRpt(node,&supplied,prior);
        }
        else if (mode<192)
        {
            long add=(mode&1)?5:8,limit=(mode&1)?8:10;
            if (mode&2)
            {
                Base e; mm_actor_events_get_fixed(_owner.track.events, add-1,&e);
                Base entrance; mm_actor_events_get_fixed(_owner.track.events, add,&entrance);
                e.rtel.irpt=entrance.rtel.irpt-((mode&4)?1:0); e.rtel.dwrOffset=(mode&4)?rOne:rZero;
                mm_actor_events_set_fixed(_owner.track.events, add-1,&e);
            }
            _owner.runtime.state.location.irpt=(mode&8)?5:0;
            _DelAddFrame(add,limit);
        }
        else if (mode<224)
        {
            _owner.runtime.state.location.dwrOffset=(mode&1)?rZero:BR_SCALAR(2);
            _owner.runtime.state.nextEvent=(mode&2)?3:5;
            _TruncateSubRte((mode&4)?3:5);
        }
        else if (mode<288)
        {
            long kind=(mode-224)%aetLim,first=(mode-224)%10;
            ok=(mode&1)?_FFindPrevAevAet(kind,first,&found):_FFindNextAevAet(kind,first,&found);
        }
        else if (mode<416)
        {
            if (mode&1) { _owner.runtime.state.location.dwrOffset=rZero; _owner.runtime.state.location.irpt=1; }
            if (mode&2) _owner.runtime.state.onStage=false;
            if (mode&4) { _owner.runtime.state.frame=0; _owner.runtime.state.nextEvent=0; _owner.runtime.state.firstEventInFrame=0; _owner.runtime.state.location={0,rZero,0}; }
            long start=(mode&8)?6:ivNil;
            if (mode<352) _DeleteFwdCore((mode&16)!=0,&alive,start);
            else DeleteFwdCore((mode&16)!=0,&alive,start);
        }
        else
        {
            long first=(mode&1)?5:0,limit=(mode&2)?ivNil:((mode&1)?8:5);
            _FindAevLastSub(first,limit,&found);
        }
        auto record=[&](const void *p,size_t n) { return !snapshot || std::fwrite(p,1,n,snapshot)==n; };
        LegacyLong state[]={mode,ok,alive,found,_owner.runtime.state.nextEvent,_owner.runtime.state.firstEventInFrame,_owner.runtime.state.actionEvent,_owner.runtime.state.addEvent,
            _owner.runtime.state.location.irpt,_owner.runtime.state.location.dwrOffset,_owner.runtime.state.location.dnfrm,_owner.runtime.state.frame,_owner.runtime.state.onStage,_owner.runtime.state.frozen,
            _owner.runtime.state.step,_owner.runtime.first,_owner.runtime.last,_owner.runtime.life_dirty,_owner.runtime.recording.recording,supplied.dwr,mm_actor_path_count(_owner.track.route),mm_actor_events_count(_owner.track.events)};
        bool saved=record(state,sizeof(state));
        for (long i=0;saved && i<mm_actor_path_count(_owner.track.route);++i)
        { RouteDistancePoint point; mm_actor_path_get(_owner.track.route, i,&point); saved=record(&point,sizeof(point)); }
        for (long i=0;saved && i<mm_actor_events_count(_owner.track.events);++i)
        { Base e; mm_actor_events_get_fixed(_owner.track.events, i,&e); saved=record(&e,sizeof(e)); }
        return saved;
    }
    bool FTestCostumeEdit(int mode,FILE *snapshot,PChunkyResourceFile resource,ChunkNumber templateNumber,
                          const ChunkNumber *materials,const ChunkNumber *customs,bool transaction=false)
    {
        using namespace ActorEvent;
        auto tmpl=static_cast<PTemplate>(resource->PbacoFetch(kctgTmpl,templateNumber,TrackTestTemplate::FReadCostumeFixture));
        if (!tmpl) return false;
        if (_Template()) { _Template()->Release(); _owner.runtime.template_object = nullptr; } _owner.runtime.template_object = tmpl;
        auto a=EngineMaterial::Fetch(resource, kctgMtrl, materials[0]);
        auto b=EngineMaterial::Fetch(resource, kctgMtrl, materials[1]);
        auto ca=EngineCostume::Fetch(resource, kctgCmtl, customs[0]);
        auto cb=EngineCostume::Fetch(resource, kctgCmtl, customs[1]);
        if (!a || !b || !ca || !cb) return false;
        PDynamicArray parents=DynamicArray::PglNew(sizeof(short)),sets=DynamicArray::PglNew(sizeof(short));
        short parent=-1,set=0;
        bool ok=parents && sets && parents->FAdd(&parent) && sets->FAdd(&set);
        set=1; ok=ok && parents->FAdd(&parent) && sets->FAdd(&set);
        if (_Body()) { mm_body_owner_release(_Body()); _owner.runtime.body = nullptr; } _owner.runtime.body = ok ? EngineBody::Create(parents,sets) : pvNil;
        ReleasePpo(&parents); ReleasePpo(&sets);
        if (!_Body()) return false;
        EngineBody::SetPartSetMtrl(_Body(), 0,a); EngineBody::SetPartSetMtrl(_Body(), 1,a);
        if (mode&4) EngineBody::SetPartSetCmtl(_Body(), ca);
        ActorTrack track; ok=track.FCreate();
        RouteDistancePoint point={{rZero,rZero,rZero},rZero};
        Base event={aetAdd,1,{0,rZero,0}}; Add add={};
        ok=ok && mm_actor_path_insert(track.Route(), 0,&point) && mm_actor_events_insert(track.Events(), 0,sizeof(add),&add,&event);
        auto append=[&](int type,int material,int bodySet,int frame) {
            Costume costume={}; costume.ibset=bodySet; costume.fCmtl=static_cast<tribool>(type); costume.cmid=material;
            costume.tag={ksidUseCrf,resource,kctgMtrl,materials[material%2]};
            event={aetCost,frame,{0,rZero,frame-1}};
            if (!type) TagManager::DupTag(&costume.tag);
            if (mm_actor_events_insert(track.Events(), mm_actor_events_count(track.Events()),sizeof(costume),&costume,&event)) return true;
            if (!type) TagManager::CloseTag(&costume.tag);
            return false;
        };
        int oldType=(mode&8)?1:0;
        ok=ok && append(oldType,0,0,2) && append(oldType,0,1,3) && append((mode&16)?1:0,1,0,4) &&
            append((mode&16)?1:0,1,0,5) && append((mode&32)?1:0,0,0,6);
        event={aetAdd,7,{0,rZero,6}};
        ok=ok && mm_actor_events_insert(track.Events(), mm_actor_events_count(track.Events()),sizeof(add),&add,&event) && append(oldType,0,0,8);
        if (!ok) return false;
        mm_actor_track_swap(&_owner.track, &track.Core()); mm_actor_sounds_clear(&_owner.sounds); _owner.runtime.state=ActorFrameState();
        _owner.runtime.state.frame=2; _owner.runtime.state.location={0,rZero,1}; _owner.runtime.state.onStage=true;
        _owner.runtime.state.firstEventInFrame=_owner.runtime.state.nextEvent=1; _owner.runtime.state.actionEvent=-1; _owner.runtime.state.addEvent=0;
        _owner.runtime.first=1; _owner.runtime.last=8;
        Costume fresh={}; fresh.ibset=(mode&64)?1:0; fresh.fCmtl=static_cast<tribool>(mode%3);
        fresh.cmid=(mode>=128 && (mode&8))?2:1; fresh.tag={ksidUseCrf,resource,kctgMtrl,materials[1]};
        bool snapshotFailureSafe = true;
#ifdef DEBUG
        if (transaction && mode == 128)
        {
            auto *events = _owner.track.events;
            const auto eventCount = mm_actor_events_count(events);
            const auto state = _owner.runtime.state;
            vmutxMem.Enter();
            vdmglob.dmaglBase.cactDo = 0;
            vdmglob.dmaglBase.cactFail = 1;
            vmutxMem.Leave();
            const bool changed = FSetCostume(fresh.ibset, &fresh.tag, fresh.cmid, fresh.fCmtl);
            vmutxMem.Enter();
            const bool failureUsed = vdmglob.dmaglBase.cactFail == 0;
            vdmglob.dmaglBase.cactFail = 0;
            vmutxMem.Leave();
            snapshotFailureSafe = failureUsed && !changed && _owner.track.events == events &&
                                  mm_actor_events_count(_owner.track.events) == eventCount &&
                                  ActorFrameTest::SameState(_owner.runtime.state, state);
        }
#endif
        if (mode>=128) ok=transaction ? FSetCostume(fresh.ibset,&fresh.tag,fresh.cmid,fresh.fCmtl) :
            FSetCostumeCore(fresh.ibset,&fresh.tag,fresh.cmid,fresh.fCmtl);
        else _PrepCostFill(1,&fresh);
        auto record=[&](const void *p,size_t n) { return !snapshot || std::fwrite(p,1,n,snapshot)==n; };
        LegacyLong state[]={mode,ok,_owner.runtime.state.nextEvent,_owner.runtime.state.firstEventInFrame,_owner.runtime.state.actionEvent,_owner.runtime.state.addEvent,_owner.runtime.first,_owner.runtime.last,mm_actor_events_count(_owner.track.events)};
        bool saved=record(state,sizeof(state));
        for (long i=0;saved && i<mm_actor_events_count(_owner.track.events);++i)
        {
            Base fixed; mm_actor_events_get_fixed(_owner.track.events, i,&fixed); saved=record(&fixed,sizeof(fixed));
            if (fixed.aet==aetCost)
            {
                Costume costume; mm_actor_events_get_payload(_owner.track.events, i,&costume);
                LegacyLong fields[]={costume.ibset,costume.fCmtl,costume.cmid,static_cast<LegacyLong>(costume.tag.cno)};
                saved=saved && record(fields,sizeof(fields));
            }
        }
        bool ordinary; PMaterial_MTRL material; PCustomMaterial_CMTL custom;
        EngineBody::GetPartSetMaterial(_Body(), 0,&ordinary,&material,&custom);
        LegacyLong body[]={ordinary,ordinary ? (material==a?0:material==b?1:2) : (custom==ca?0:custom==cb?1:2)};
        saved=saved && record(body,sizeof(body));
        mm_actor_track_dispose(&_owner.track, &EngineActorTagServices()); if (_Body()) { mm_body_owner_release(_Body()); _owner.runtime.body = nullptr; }
        LegacyLong refs[]={mm_material_owner_references(a),mm_material_owner_references(b),
                           mm_material_owner_references(ca),mm_material_owner_references(cb)};
        saved=saved && record(refs,sizeof(refs));
        mm_material_owner_release(a); mm_material_owner_release(b);
        mm_material_owner_release(ca); mm_material_owner_release(cb);
        return saved && snapshotFailureSafe;
    }
    bool FTestFillEdit(int mode, FILE *snapshot)
    {
        using namespace ActorEvent;
        ActorTrack track;
        if (!track.FCreate()) return false;
        RouteDistancePoint point={{rZero,rZero,rZero},BR_SCALAR(10)};
        if (!mm_actor_path_insert(track.Route(), 0,&point)) return false;
        point.dwr=rZero; if (!mm_actor_path_insert(track.Route(), 1,&point)) return false;
        auto append=[&](long kind,long value,long offset) {
            Base event={kind,offset+1,{0,BR_SCALAR(offset),0}};
            union { Add add; Action action; RoutePoint point; BMAT34 matrix; int32_t scalar; } payload={};
            long bytes=4;
            if (kind==aetAdd) bytes=sizeof(Add);
            else if (kind==aetActn) { payload.action={static_cast<int32_t>(value),0}; bytes=sizeof(Action); }
            else if (kind==aetRotF || kind==aetRotH) { BrMatrix34Identity(&payload.matrix); payload.matrix.m[3][0]=BR_SCALAR(value); bytes=sizeof(BMAT34); }
            else if (kind==aetPull || kind==aetTweak) { payload.point={BR_SCALAR(value),rOne,rOne}; bytes=sizeof(RoutePoint); }
            else payload.scalar=static_cast<int32_t>(value);
            return mm_actor_events_insert(track.Events(), mm_actor_events_count(track.Events()),bytes,&payload,&event);
        };
        const bool actionFill=mode>=384 && mode<512, actionSet=mode>=512;
        int bits=mode%128;
        long kind=actionFill ? aetActn : (mode/128==0 ? aetSize : mode/128==1 ? aetPull : aetRotF);
        if (!append(aetAdd,0,0)) return false;
        if (actionSet)
        {
            if (!append(aetActn,0,0) || !append(aetStep,0,10)) return false;
        }
        else
        {
            if (!append(kind,1,0) || !append(aetTweak,3,(bits&1)?1:3) ||
                !append(kind,(bits&2)?2:1,3) || !append(aetFreeze,1,(bits&4)?1:3) ||
                !append(aetRotH,3,2) || !append(aetStep,0,3) ||
                !append(kind,(bits&8)?3:2,4) || !append(aetFreeze,1,10) ||
                !append(aetAdd,0,10) || !append(kind,1,10)) return false;
        }
        mm_actor_track_swap(&_owner.track, &track.Core()); mm_actor_sounds_clear(&_owner.sounds);
        _owner.runtime.state=ActorFrameState(); ActorFrameTest::Rewind(_owner.runtime.state, _owner.track); _owner.runtime.state.frame=3; _owner.runtime.state.location={0,BR_SCALAR(2),0};
        _owner.runtime.state.onStage=true; _owner.runtime.state.action=0; _owner.runtime.state.celCount=4; _owner.runtime.state.actionFlags=factnRotateY;
        _owner.runtime.state.actionEvent=1; _owner.runtime.state.addEvent=0; _owner.runtime.state.firstEventInFrame=2; _owner.runtime.state.nextEvent=2;
        _owner.runtime.first=1; _owner.runtime.last=20; _owner.runtime.life_dirty=false;
        bool ok=true;
        auto tmpl=static_cast<TrackTestTemplate *>(_Template());
        if (actionSet)
        {
            int variant=mode-512;
            if (variant&1) { _owner.runtime.state.action=-1; _owner.runtime.state.actionEvent=-1; }
            if (variant&2) _owner.runtime.state.location={0,BR_SCALAR(10),0};
            tmpl->FailAfter(variant>=8 ? (variant-8)/2+1 : 0);
            const auto services = _ActionEditServices();
            const auto motion = mm_actor_track_motion_services();
            ChildChunkID external=0; TrashVar(&external);
            ok=mm_actor_owner_set_action(&_owner,0,variant%4,(variant&4)!=0,external,&motion,&services)!=0;
            tmpl->FailAfter(0);
        }
        else if (actionFill)
        {
            ulong flags=((bits&16)?faetActn:0)|((bits&32)?faetTweak:0)|((bits&64)?faetFreeze:0);
            _PrepActnFill(2,1,2,flags);
        }
        else
        {
            union { int32_t scalar; RoutePoint point; BMAT34 matrix; } value={};
            long bytes=4;
            if (kind==aetRotF) { BrMatrix34Identity(&value.matrix); value.matrix.m[3][0]=BR_SCALAR(2); bytes=sizeof(BMAT34); }
            else if (kind==aetPull) { value.point={BR_SCALAR(2),rOne,rOne}; bytes=sizeof(RoutePoint); }
            else value.scalar=2;
            ulong flags=((bits&16)?faetTweak|faetRotF:0)|((bits&32)?faetFreeze:0)|((bits&64)?faetStep:0);
            _PrepXfrmFill(kind,&value,bytes,2,(bits&1)?1:ivNil,flags);
        }
        auto record=[&](const void *p,size_t n) { return !snapshot || std::fwrite(p,1,n,snapshot)==n; };
        LegacyLong state[]={mode,ok,_owner.runtime.state.nextEvent,_owner.runtime.state.firstEventInFrame,_owner.runtime.state.actionEvent,_owner.runtime.state.addEvent,
            _owner.runtime.state.action,_owner.runtime.state.cel,_owner.runtime.state.frozen,_owner.runtime.state.step,_owner.runtime.first,_owner.runtime.last,_owner.runtime.life_dirty,mm_actor_events_count(_owner.track.events)};
        bool saved=record(state,sizeof(state));
        unsigned char data[100];
        for (long i=0; saved && i<mm_actor_events_count(_owner.track.events); ++i)
        {
            Base event; mm_actor_events_get_fixed(_owner.track.events, i,&event);
            long bytes=mm_actor_events_size(_owner.track.events, i); if (bytes>sizeof(data)) return false;
            mm_actor_events_get_payload(_owner.track.events, i,data); saved=record(&event,sizeof(event)) && record(data,bytes);
        }
        return saved;
    }
    bool FTestCostumeMergeOwnership(PChunkyResourceFile replacement)
    {
        auto original=static_cast<PChunkyResourceFile>(_owner.runtime.template_tag.cache);
        const long originalRefs=original->CactRef(), replacementRefs=replacement->CactRef();
        ActorTrack track;
        if (!track.FCreate()) return false;
        RouteDistancePoint point={{rZero,rZero,rZero},rZero};
        ActorEvent::Base event={ActorEvent::aetAdd,1,{0,rZero,0}};
        ActorEvent::Add add={};
        if (!mm_actor_path_insert(track.Route(), 0,&point) || !mm_actor_events_insert(track.Events(), 0,sizeof(add),&add,&event)) return false;
        for (int i=0; i<2; ++i)
        {
            ActorEvent::Costume costume={};
            costume.ibset=1; costume.tag={0,i ? replacement : original,kctgMtrl,0};
            event.aet=ActorEvent::aetCost; event.nfrm=4;
            TagManager::DupTag(&costume.tag);
            if (!mm_actor_events_insert(track.Events(), i+1,sizeof(costume),&costume,&event))
            { TagManager::CloseTag(&costume.tag); return false; }
        }
        mm_actor_track_swap(&_owner.track, &track.Core()); mm_actor_sounds_clear(&_owner.sounds);
        _owner.runtime.state.firstEventInFrame=1; _owner.runtime.state.nextEvent=2; _owner.runtime.state.actionEvent=-1; _owner.runtime.state.addEvent=0;
        long result=-1; _MergeAev(1,2,&result);
        if (result!=1 || original->CactRef()!=originalRefs || replacement->CactRef()!=replacementRefs+1)
        {
            std::fprintf(stderr,"Costume merge lost owning TAG references\n");
            std::exit(1);
        }
        mm_actor_track_dispose(&_owner.track, &EngineActorTagServices());
        return replacement->CactRef()==replacementRefs;
    }
    bool FTestEventEdit(int mode, FILE *snapshot)
    {
        ActorTrack created;
        if (!created.FCreate()) return false;
        RouteDistancePoint points[]={{{rZero,rZero,rZero},BR_SCALAR(10)},{{BR_SCALAR(10),rZero,rZero},rZero}};
        if (!mm_actor_path_insert(created.Route(), 0,&points[0]) || !mm_actor_path_insert(created.Route(), 1,&points[1])) return false;
        union Payload { ActorEvent::Add add; ActorEvent::Action action; ActorEvent::Costume costume;
            MMActorSound sound; BMAT34 matrix; RoutePoint move; int32_t scalar; } value;
        auto fill=[&](int kind,int seed) {
            std::memset(&value,0,sizeof(value));
            switch (kind)
            {
            case ActorEvent::aetAdd: return int(sizeof(value.add));
            case ActorEvent::aetActn: return int(sizeof(value.action));
            case ActorEvent::aetCost:
                value.costume.ibset=1; value.costume.tag={2,pvNil,kctgMtrl,static_cast<ChunkNumber>(seed)};
                return int(sizeof(value.costume));
            case ActorEvent::aetSnd:
                value.sound.cel=seed; value.sound.style=2; value.sound.volume=100+seed;
                value.sound.tag={2,pvNil,kctgMsnd,static_cast<ChunkNumber>(seed)};
                return int(sizeof(value.sound));
            case ActorEvent::aetRotH: case ActorEvent::aetRotF:
                BrMatrix34Identity(&value.matrix); value.matrix.m[3][0]=BR_SCALAR(seed); return int(sizeof(value.matrix));
            case ActorEvent::aetMove: case ActorEvent::aetTweak: case ActorEvent::aetPull:
                value.move={BR_SCALAR(seed),BR_SCALAR(seed+1),BR_SCALAR(seed+2)}; return int(sizeof(value.move));
            case ActorEvent::aetRem: return 0;
            default: value.scalar=seed; return int(sizeof(value.scalar));
            }
        };
        auto add=[&](int kind,int seed,int frame,int hold) {
            int bytes=fill(kind,seed);
            ActorEvent::Base event={kind,frame,{0,rZero,hold}};
            return mm_actor_events_insert(created.Events(), mm_actor_events_count(created.Events()),bytes,&value,&event);
        };
        int oldKind=mode<26 ? mode/2 : ActorEvent::aetFreeze;
        int newKind=oldKind;
        if (mode==26 || mode==27) { oldKind=ActorEvent::aetRem; newKind=ActorEvent::aetAdd; }
        if (mode==28 || mode==29) { oldKind=ActorEvent::aetRotH; newKind=ActorEvent::aetRotF; }
        if (mode==30) oldKind=newKind=ActorEvent::aetRotF;
        if (mode>=31 && mode<=33) oldKind=newKind=ActorEvent::aetSnd;
        if (mode==34) oldKind=newKind=ActorEvent::aetCost;
        if (mode==35 || mode==41) oldKind=newKind=ActorEvent::aetMove;
        if (!add(ActorEvent::aetAdd,0,1,0) || !add(oldKind,1,(mode==26 || mode==29) ? 3 : 4,0)) return false;
        if (mode==30 && !add(ActorEvent::aetRotH,1,4,0)) return false;
        if (!add(newKind,2,4,mode<26 ? mode%2 : 0)) return false;
        if (mode==31) { value.sound.queue=tYes; mm_actor_events_set_payload(created.Events(), 2,&value.sound); }
        if (mode==32 || mode==33) { value.sound.style=3; mm_actor_events_set_payload(created.Events(), 2,&value.sound); }
        if (mode==34) { value.costume.ibset=2; mm_actor_events_set_payload(created.Events(), 2,&value.costume); }
        if (mode==35)
        {
            RoutePoint old={klwMax,0,0}, fresh={1,0,0};
            mm_actor_events_set_payload(created.Events(), 1,&old); mm_actor_events_set_payload(created.Events(), 2,&fresh);
        }
        mm_actor_track_swap(&_owner.track, &created.Core()); mm_actor_sounds_clear(&_owner.sounds);
        _owner.runtime.state=ActorFrameState(); ActorFrameTest::Rewind(_owner.runtime.state, _owner.track);
        _owner.runtime.state.onStage=true; _owner.runtime.state.frame=4; _owner.runtime.state.firstEventInFrame=1; _owner.runtime.state.nextEvent=mm_actor_events_count(_owner.track.events)-1;
        _owner.runtime.state.actionEvent=-1; _owner.runtime.state.addEvent=0; _owner.runtime.state.celCount=4;
        _owner.runtime.state.subrouteTranslation={BR_SCALAR(7),BR_SCALAR(8),BR_SCALAR(9)};
        _owner.runtime.state.routeTranslation={BR_SCALAR(42),BR_SCALAR(43),BR_SCALAR(44)};
        _owner.runtime.actor=mode==33 ? 32768 : 23; _owner.runtime.first=1; _owner.runtime.last=30; _owner.runtime.life_dirty=false;
        if (oldKind==ActorEvent::aetSnd)
        {
            MMActorMotionSound match;
            mm_actor_events_get_fixed(_owner.track.events, 1,&match.event); mm_actor_events_get_payload(_owner.track.events, 1,&match.sound);
            if (!mm_actor_sounds_insert(&_owner.sounds,&match.event,&match.sound)) return false;
        }
        LegacyLong result=-99; bool ok=true;
        if (mode>=38 && mode<=40)
        {
            int32_t freeze=1; ActorEvent::Base event={ActorEvent::aetFreeze,4,{0,rZero,0}};
            _owner.runtime.state.actionEvent=1;
            ok=_FInsertAev(1,sizeof(freeze),mode==39 ? pvNil : &freeze,&event,mode!=40);
            if (ok && mode==39) mm_actor_events_set_payload(_owner.track.events, 1,&freeze);
        }
        else if (mode>=41)
        {
            _RemoveAev(_owner.runtime.state.nextEvent,false);
            if (mode==41) { RoutePoint move={BR_SCALAR(2),BR_SCALAR(3),BR_SCALAR(4)}; ok=_FAddDoAev(ActorEvent::aetMove,sizeof(move),&move); }
            else
            {
                int32_t scalar=1;
                if (mode==43) { _owner.runtime.state.location.dwrOffset=BR_SCALAR(20); scalar=0; }
                if (mode==44) { _owner.runtime.first=10; _owner.runtime.last=-5; }
                ok=_FAddDoAev(mode==43 ? ActorEvent::aetStep : ActorEvent::aetFreeze,sizeof(scalar),&scalar);
            }
        }
        else _MergeAev(mode==36 ? _owner.runtime.state.nextEvent : 1,_owner.runtime.state.nextEvent,mode==37 ? pvNil : &result);
        auto record=[&](const void *p,size_t n) { return !snapshot || std::fwrite(p,1,n,snapshot)==n; };
        LegacyLong state[]={mode,ok,result,_owner.runtime.state.nextEvent,_owner.runtime.state.firstEventInFrame,_owner.runtime.state.actionEvent,_owner.runtime.state.addEvent,
            _owner.runtime.state.subrouteTranslation.dxr,_owner.runtime.state.subrouteTranslation.dyr,_owner.runtime.state.subrouteTranslation.dzr,
            _owner.runtime.state.routeTranslation.dxr,_owner.runtime.state.routeTranslation.dyr,_owner.runtime.state.routeTranslation.dzr,_owner.runtime.state.frozen,
            _owner.runtime.first,_owner.runtime.last,mm_actor_events_count(_owner.track.events),static_cast<LegacyLong>(_owner.sounds.count)};
        bool saved=record(state,sizeof(state));
        for (long i=0; i<mm_actor_events_count(_owner.track.events); ++i)
        {
            ActorEvent::Base event; mm_actor_events_get_fixed(_owner.track.events, i,&event); saved=saved && record(&event,sizeof(event));
            if (event.aet==ActorEvent::aetSnd)
            {
                MMActorSound sound; mm_actor_events_get_payload(_owner.track.events, i,&sound);
                LegacyLong fields[]={sound.cel,sound.style,sound.volume,sound.queue,sound.tag.sid,static_cast<LegacyLong>(sound.tag.number)};
                saved=saved && record(fields,sizeof(fields));
            }
            else if (event.aet==ActorEvent::aetCost)
            {
                ActorEvent::Costume costume; mm_actor_events_get_payload(_owner.track.events, i,&costume);
                LegacyLong fields[]={costume.ibset,costume.cmid,costume.fCmtl,costume.tag.sid,static_cast<LegacyLong>(costume.tag.cno)};
                saved=saved && record(fields,sizeof(fields));
            }
            else
            {
                LegacyLong bytes=mm_actor_events_size(_owner.track.events, i); std::memset(&value,0,sizeof(value));
                mm_actor_events_get_payload(_owner.track.events, i,&value); saved=saved && record(&value,bytes);
            }
        }
        return saved && ok==(mode!=43);
    }
    bool FTestSoundEdit(int mode, FILE *snapshot)
    {
        ActorTrack created;
        if (!created.FCreate()) return false;
        RouteDistancePoint point = {{rZero, rZero, rZero}, rZero};
        if (!mm_actor_path_insert(created.Route(), 0, &point)) return false;
        const int kinds[] = {ActorEvent::aetAdd,ActorEvent::aetActn,ActorEvent::aetSnd,ActorEvent::aetSnd,
            ActorEvent::aetSnd,ActorEvent::aetSnd,ActorEvent::aetSnd,ActorEvent::aetFreeze,ActorEvent::aetSnd,
            ActorEvent::aetActn,ActorEvent::aetSnd,ActorEvent::aetAdd};
        const int frames[] = {1,1,2,2,3,3,4,4,4,5,6,7};
        const int cels[] = {0,0,1,1,-1,1,-1,0,-1,0,1,0};
        const int styles[] = {0,0,2,3,2,2,2,0,2,0,2,0};
        for (int i = 0; i < 12; ++i)
        {
            ActorEvent::Base event = {kinds[i],frames[i],{0,rZero,i}};
            ActorEvent::Add add = {};
            ActorEvent::Action action = {0,0};
            MMActorSound sound = {};
            int32_t frozen = 1;
            sound.cel = cels[i]; sound.style = styles[i]; sound.volume = 100 + i;
            sound.loop = i & 1 ? tYes : tNo;
            sound.tag = {2,pvNil,kctgMsnd,static_cast<ChunkNumber>(30+i)};
            void *data = &sound;
            LegacyLong bytes = sizeof(sound);
            if (kinds[i] == ActorEvent::aetAdd) { data=&add; bytes=sizeof(add); }
            if (kinds[i] == ActorEvent::aetActn) { data=&action; bytes=sizeof(action); }
            if (kinds[i] == ActorEvent::aetFreeze) { data=&frozen; bytes=sizeof(frozen); }
            if (!mm_actor_events_insert(created.Events(), i,bytes,data,&event)) return false;
        }
        mm_actor_track_swap(&_owner.track, &created.Core());
        mm_actor_sounds_clear(&_owner.sounds);
        for (int i : {2,3,10})
        {
            MMActorMotionSound record;
            mm_actor_events_get_fixed(_owner.track.events, i,&record.event);
            mm_actor_events_get_payload(_owner.track.events, i,&record.sound);
            if (!mm_actor_sounds_insert(&_owner.sounds,&record.event,&record.sound)) return false;
        }
        _owner.runtime.state.frame=4; _owner.runtime.state.action=0; _owner.runtime.state.cel=5; _owner.runtime.state.celCount=4;
        _owner.runtime.first=1; _owner.runtime.last=30; _owner.runtime.life_dirty=false;
        _owner.runtime.state.firstEventInFrame=6; _owner.runtime.state.nextEvent=9; _owner.runtime.state.actionEvent=1; _owner.runtime.state.addEvent=0;
        PDynamicArray tags = pvNil;
        LegacyLong volume=12345;
        bool loop=false, ok=false;
        if (mode == 9) ((TrackTestTemplate *)_Template())->FailAfter(2);
        if (mode == 10 || mode == 11 || mode == 12) ((TrackTestTemplate *)_Template())->FailNextFetch();
        switch (mode)
        {
        case 0: ok=FQuerySnd(2,false,&tags,&volume,&loop); break;
        case 1: case 10: ok=FQuerySnd(2,true,&tags,&volume,&loop); break;
        case 2: case 11: ok=FSetVlmSnd(2,false,333); break;
        case 3: ok=FSetVlmSnd(2,true,333); break;
        case 4: case 12: ok=FDeleteSndCore(2,false); break;
        case 5: case 9: ok=FDeleteSndCore(2,true); break;
        case 6: ok=_FRemoveAevMm(0); break;
        case 7: ok=FResolveAllSndTags(0); break;
        case 8: _owner.runtime.state.frame=_Scene()->Nfrm(); ok=FSoundInFrm(); break;
        case 13: _RemoveAev(3,false); ok=true; break;
        case 14: _RemoveAev(1,true); ok=true; break;
        }
        auto record = [&](const void *p, size_t n) { return !snapshot || std::fwrite(p,1,n,snapshot)==n; };
        LegacyLong values[] = {mode,ok,volume,loop,_owner.runtime.state.frame,_owner.runtime.state.action,_owner.runtime.state.cel,_owner.runtime.state.firstEventInFrame,
            _owner.runtime.state.nextEvent,_owner.runtime.state.actionEvent,_owner.runtime.state.addEvent,mm_actor_events_count(_owner.track.events),static_cast<LegacyLong>(_owner.sounds.count),
            tags ? tags->IvMac() : -1};
        bool saved=record(values,sizeof(values));
        for (long i=0; tags && i<tags->IvMac(); ++i)
        {
            TAG tag; tags->Get(i,&tag);
            LegacyUlong v[] = {static_cast<LegacyUlong>(tag.sid),tag.ctg,tag.cno};
            saved=saved && record(v,sizeof(v));
        }
        ReleasePpo(&tags);
        for (long i=0; i<mm_actor_events_count(_owner.track.events); ++i)
        {
            ActorEvent::Base event; mm_actor_events_get_fixed(_owner.track.events, i,&event);
            saved=saved && record(&event,sizeof(event));
            if (event.aet == ActorEvent::aetSnd)
            {
                MMActorSound sound; mm_actor_events_get_payload(_owner.track.events, i,&sound);
                LegacyUlong v[] = {static_cast<LegacyUlong>(sound.cel),static_cast<LegacyUlong>(sound.style),
                    static_cast<LegacyUlong>(sound.volume),static_cast<LegacyUlong>(sound.loop),
                    static_cast<LegacyUlong>(sound.tag.sid),sound.tag.type,sound.tag.number};
                saved=saved && record(v,sizeof(v));
            }
        }
        for (long i=0; i<static_cast<LegacyLong>(_owner.sounds.count); ++i)
        {
            MMActorMotionSound sound; if (!mm_actor_sounds_get(&_owner.sounds, i, &sound)) return false;
            LegacyLong v[] = {sound.sound.cel, sound.sound.style, sound.sound.volume};
            saved=saved && record(v,sizeof(v));
        }
        return saved && (ok == !(mode == 9 || mode == 10 || mode == 11 || mode == 12));
    }
    bool FInitPlayback(PScene scene, PChunkyResourceFile resource)
    {
        if (!FInit(scene, resource) || !((TrackTestTemplate *)_Template())->FInitAction() ||
            !ActorFrameTest::FCreateTrack(_owner.track, false))
            return false;
        _owner.runtime.body =  _Template()->PbodyCreate();
        if (_Body() == pvNil)
            return false;
        mm_actor_sounds_clear(&_owner.sounds);
        _owner.runtime.state = ActorFrameState();
        ActorFrameTest::Rewind(_owner.runtime.state, _owner.track);
        _owner.runtime.state.celCount = 4;
        _owner.runtime.state.actionFlags = factnRotateY;
        _owner.runtime.first = 1;
        _owner.runtime.last = 0; // Entrance lookup must refresh lifetime before final effects.
        _owner.runtime.life_dirty = true;
        return true;
    }
    MMActorState State() const
    {
        return _owner.runtime.state;
    }
    long LastFrame() const
    {
        return _owner.runtime.last;
    }
    bool FTestTimeline()
    {
        if (!ActorFrameTest::FCreateMetadataTrack(_owner.track, true))
            return false;
        MMActorState before = _owner.runtime.state;
        _owner.runtime.first = -2;
        _owner.runtime.life_dirty = true;
        auto tmpl = (TrackTestTemplate *)_Template();
        tmpl->FailNextFetch();
        long reported = 88;
        if (_FComputeLifetime(&reported) || reported != 88 || _owner.runtime.last != -1 || !_owner.runtime.life_dirty ||
            !ActorFrameTest::SameState(_owner.runtime.state, before))
            return false;
        if (!_FComputeLifetime(&reported) || reported != 0 || _owner.runtime.last != 0 || _owner.runtime.life_dirty ||
            !ActorFrameTest::SameState(_owner.runtime.state, before))
            return false;
        _owner.runtime.recording.recording = true;
        _owner.runtime.last = 42;
        tmpl->FailNextFetch();
        if (!_FComputeLifetime(&reported) || reported != 42 || !_owner.runtime.life_dirty)
            return false;
        _owner.runtime.recording.recording = false;
        reported = 88;
        // The failure is still pending: recording did not consult the resolver.
        if (_FComputeLifetime(&reported) || reported != 88 || !_owner.runtime.life_dirty)
            return false;
        return _FComputeLifetime(&reported) && reported == 0 && !_owner.runtime.life_dirty &&
               ActorFrameTest::SameState(_owner.runtime.state, before);
    }
    bool FInit(PScene scene, PChunkyResourceFile resource)
    {
        auto tmpl = NewObj TrackTestTemplate();
        _owner.runtime.template_object =  tmpl;
        if (tmpl == pvNil || !tmpl->FInit() || !_FCreateGroups())
            return false;
        _owner.runtime.template_tag.sid = ksidUseCrf;
        _owner.runtime.template_tag.type = kctgTmpl;
        _owner.runtime.template_tag.number = 1;
        _owner.runtime.template_tag.cache = resource;
        mm_tag_dup(&_owner.runtime.template_tag, &EngineActorTagServices());
        _owner.runtime.scene =  scene;
        RouteDistancePoint point = {{BR_SCALAR(3), rZero, rZero}, rZero};
        ActorEvent::Base event = {};
        event.aet = ActorEvent::aetAdd;
        ActorEvent::Add addition = {};
        if (!mm_actor_path_insert(_owner.track.route, 0, &point) || !mm_actor_events_insert(_owner.track.events, 0, sizeof(addition), &addition, &event))
            return false;
        event.aet = ActorEvent::aetSnd;
        MMActorSound sound = {};
        sound.tag = _owner.runtime.template_tag;
        sound.tag.type = kctgMsnd;
        if (!mm_actor_events_insert(_owner.track.events, 1, sizeof(sound), &sound, &event))
            return false;
        mm_tag_dup(&sound.tag, &EngineActorTagServices());
        MMActorMotionSound match = {};
        if (!mm_actor_sounds_insert(&_owner.sounds, &match.event, &match.sound))
            return false;
        _SetStateRewound();
        _owner.runtime.first = _owner.runtime.state.frame = 1;
        _owner.runtime.last = 8;
        _owner.runtime.actor = 23;
        _owner.runtime.state.nextEvent = 1;
        _owner.runtime.state.transform.rScaleStep = BR_SCALAR(2);
        return true;
    }
    bool FTestRestore(PActor snapshot)
    {
        auto changedEvents = _owner.track.events;
        MMActorPath *changedRoute = _owner.track.route;
        RouteDistancePoint point = {{BR_SCALAR(99), rZero, rZero}, rZero};
        mm_actor_path_put(_owner.track.route, 0, &point);
        mm_actor_events_delete(_owner.track.events, 0); // Add has no owned TAG.
        _owner.runtime.state.frame = 7;
        _owner.runtime.actor = 99;
        _owner.runtime.state.transform.rScaleStep = BR_SCALAR(3);
        const long refs = CactRef();
        Restore(snapshot);
        mm_actor_path_get(_owner.track.route, 0, &point);
        return CactRef() == refs && _owner.track.events != changedEvents && _owner.track.route != changedRoute &&
               mm_actor_events_count(_owner.track.events) == 2 && point.xyz.dxr == BR_SCALAR(3) && _owner.runtime.state.frame == 1 && _owner.runtime.actor == 23 &&
               _owner.runtime.state.transform.rScaleStep == BR_SCALAR(2) && _owner.runtime.state.nextEvent == 1 && static_cast<LegacyLong>(_owner.sounds.count) == 1;
    }
    bool FTestRoute()
    {
        RouteDistancePoint start = {{BR_SCALAR(3), rZero, rZero}, BR_SCALAR(4)};
        RouteDistancePoint finish = {{BR_SCALAR(7), rZero, rZero}, rZero};
        mm_actor_path_put(_owner.track.route, 0, &start);
        if (!mm_actor_path_insert(_owner.track.route, 1, &finish))
            return false;
        ActorEvent::Base event = {};
        event.aet = ActorEvent::aetStep;
        event.rtel.dwrOffset = BR_SCALAR(2);
        BRS step = rZero;
        if (!mm_actor_events_insert(_owner.track.events, 2, sizeof(step), &step, &event))
            return false;
        for (long mode = 0; mode < 3; ++mode)
        {
            _owner.runtime.recording.recording = mode == 1;
            _owner.runtime.recording.rejoining = mode == 2;
            RouteLocation location = {0, rZero, 0};
            RoutePoint position;
            bool end = true;
            _AdvanceRtel(BR_SCALAR(3), &location, 0, 2, &end);
            _GetXyzFromRtel(&location, &position);
            if (end || position.dxr != (mode == 0 ? BR_SCALAR(5) : BR_SCALAR(6)))
                return false;
        }
        return true;
    }
    bool FTestValueEvents()
    {
        _owner.runtime.origin = {BR_SCALAR(10), BR_SCALAR(20), BR_SCALAR(30)};
        _owner.runtime.state.subrouteTranslation = {};
        _owner.runtime.state.frame = 17;
        auto apply = [&](long type, void *payload, long bytes) {
            ActorEvent::Base event = {static_cast<int32_t>(type), -1, {0, BR_SCALAR(2), 3}};
            long index = mm_actor_events_count(_owner.track.events);
            if (!mm_actor_events_insert(_owner.track.events, index, bytes, payload, &event) || !_FDoAevCore(index))
                return false;
            mm_actor_events_get_fixed(_owner.track.events, index, &event);
            return event.nfrm == 17;
        };
        RoutePoint delta = {BR_SCALAR(2), BR_SCALAR(3), BR_SCALAR(4)};
        BRS step = rZero;
        int32_t frozen = -1;
        if (!apply(ActorEvent::aetMove, &delta, sizeof(delta)) || !apply(ActorEvent::aetStep, &step, sizeof(step)) ||
            !apply(ActorEvent::aetFreeze, &frozen, sizeof(frozen)))
            return false;
        if (_owner.runtime.state.routeTranslation.dxr != BR_SCALAR(12) || _owner.runtime.state.routeTranslation.dyr != BR_SCALAR(23) ||
            _owner.runtime.state.position.dxr != BR_SCALAR(5) || _owner.runtime.state.location.dnfrm != 3 || !_owner.runtime.state.frozen)
            return false;
        PActor snapshot = pvNil;
        if (!FDup(&snapshot, false))
            return false;
        _owner.runtime.state = ActorFrameState();
        Restore(snapshot);
        ReleasePpo(&snapshot);
        return _owner.runtime.state.frame == 17 && _owner.runtime.state.routeTranslation.dzr == BR_SCALAR(34) && _owner.runtime.state.frozen &&
               _owner.runtime.state.position.dxr == BR_SCALAR(5) && _owner.runtime.state.location.dnfrm == 3;
    }
    bool FTestPose()
    {
        mm_actor_state_init_transform(&_owner.runtime.state);
        _owner.runtime.state.useCurrentRotation = false;
        _owner.runtime.state.actionFlags = factnRotateY;
        _owner.runtime.state.addEvent = 0;
        _owner.runtime.state.location = {0, BR_SCALAR(2), 0};
        RoutePoint position = {BR_SCALAR(5), rZero, rZero};
        BMAT34 matrix;
        _MatrixRotUpdate(&position, &matrix);
        // Route yaw is 90 degrees; the template contributes another 90.
        if (_owner.runtime.state.position.dxr != BR_SCALAR(5) || _owner.runtime.state.transform.yaPath != static_cast<BRA>(0x4000) ||
            BrsAbs(BrsAdd(matrix.m[0][0], rOne)) > BR_SCALAR(0.002) || BrsAbs(matrix.m[0][2]) > BR_SCALAR(0.002))
            return false;
        _owner.runtime.state.actionFlags = factnRotateX;
        RoutePoint direction = {rOne, rOne, rZero};
        BRA x = 123, y = 123, z = 123;
        _ApplyRotFromVec(&direction, pvNil, &x, &y, &z);
        return x == static_cast<BRA>(0xe000) && y == aZero && z == aZero;
    }
};

static bool TestActorTrackRuntime(PScene scene)
{
    PChunkyFile file = ChunkyFile::PcflCreateTemp();
    PChunkyResourceFile resource = pvNil;
    TrackTestActor *actor = pvNil;
    PActor snapshot = pvNil;
    bool ok = false;
    if (file == pvNil || (resource = ChunkyResourceFile::PcrfNew(file, 0)) == pvNil)
        goto done;
    for (long order = 0; order < 2; ++order)
    {
        long refs = resource->CactRef();
        actor = NewObj TrackTestActor();
        if (actor == pvNil || !actor->FInit(scene, resource))
            goto done;
#ifdef DEBUG
        // The C path clone allocates its handle and rows before the event table.
        // A failure allocating the event table must release the partial path.
        vmutxMem.Enter();
        vdmglob.dmaglHq.cactDo = 2;
        vdmglob.dmaglHq.cactFail = 1;
        vmutxMem.Leave();
        bool copied = actor->FDup(&snapshot, false);
        vmutxMem.Enter();
        vdmglob.dmaglHq.cactFail = 0;
        vmutxMem.Leave();
        vpers->Clear();
        if (copied || snapshot != pvNil || resource->CactRef() != refs + 2)
            goto done;
#endif
        if (!actor->FDup(&snapshot, false) || resource->CactRef() != refs + 4 || !actor->FTestRestore(snapshot))
            goto done;
        if (order == 0)
        {
            ReleasePpo(&snapshot);
            ReleasePpo(&actor);
        }
        else
        {
            ReleasePpo(&actor);
            ReleasePpo(&snapshot);
        }
        if (resource->CactRef() != refs)
            goto done;
    }
    actor = NewObj TrackTestActor();
    if (actor == pvNil || !actor->FInit(scene, resource) || !actor->FDup(&snapshot, true) || snapshot->Pscen() != pvNil)
        goto done;
    if (!actor->FTestRoute() || !actor->FTestValueEvents() || !actor->FTestPose())
        goto done;
    ok = true;
done:
    ReleasePpo(&snapshot);
    ReleasePpo(&actor);
    ReleasePpo(&resource);
    ReleasePpo(&file);
    return ok;
}

static bool TestActorFrameRuntime(PScene scene)
{
    PChunkyFile file = ChunkyFile::PcflCreateTemp();
    PChunkyResourceFile resource = pvNil;
    TrackTestActor *sequential = pvNil, *seeking = pvNil;
    MMActorState expected[14];
    bool ok = false;
    if (file == pvNil || (resource = ChunkyResourceFile::PcrfNew(file, 0)) == pvNil)
        goto done;
    sequential = NewObj TrackTestActor();
    seeking = NewObj TrackTestActor();
    if (sequential == pvNil || seeking == pvNil || !sequential->FInitPlayback(scene, resource) ||
        !seeking->FInitPlayback(scene, resource))
        goto done;
    for (long frame = 1; frame <= 13; ++frame)
    {
        if (!sequential->FGotoFrame(frame))
            goto done;
        expected[frame] = sequential->State();
        if (sequential->LastFrame() != 9)
            goto done;
    }
    {
        long order[] = {13, 12, 4, 8, 3, 2, 7, 1, 9, 13};
        for (long target : order)
        {
            if (!seeking->FGotoFrame(target) || !ActorFrameTest::SameState(seeking->State(), expected[target]))
            {
                fprintf(stderr, "[FAIL] runtime actor seek to %ld differs from sequential playback\n", target);
                goto done;
            }
        }
    }
    if (!seeking->FTestTimeline())
        goto done;
    ok = true;
done:
    ReleasePpo(&seeking);
    ReleasePpo(&sequential);
    ReleasePpo(&resource);
    ReleasePpo(&file);
    return ok;
}

static bool TestActorSoundEditing(PScene scene)
{
    FILE *snapshot = nullptr;
    if (const char *path=std::getenv("MM_ACTOR_SOUND_EDIT_SNAPSHOT"))
        if (!(snapshot=std::fopen(path,"wb"))) return false;
    PChunkyFile file=ChunkyFile::PcflCreateTemp();
    PChunkyResourceFile resource=file ? ChunkyResourceFile::PcrfNew(file,0) : pvNil;
    auto actor=NewObj TrackTestActor();
    bool ok=actor && resource && actor->FInitPlayback(scene,resource);
    for (int mode=0; ok && mode<15; ++mode) ok=actor->FTestSoundEdit(mode,snapshot);
    ReleasePpo(&actor); ReleasePpo(&resource); ReleasePpo(&file);
    if (snapshot) ok=std::fclose(snapshot)==0 && ok;
    return ok;
}

static bool TestActorEventEditing(PScene scene)
{
    FILE *snapshot=nullptr;
    if (const char *path=std::getenv("MM_ACTOR_EVENT_EDIT_SNAPSHOT"))
        if (!(snapshot=std::fopen(path,"wb"))) return false;
    PChunkyFile file=ChunkyFile::PcflCreateTemp();
    PChunkyResourceFile resource=file ? ChunkyResourceFile::PcrfNew(file,0) : pvNil;
    auto actor=NewObj TrackTestActor();
    bool ok=actor && resource && actor->FInitPlayback(scene,resource);
    for (int mode=0; ok && mode<45; ++mode) ok=actor->FTestEventEdit(mode,snapshot);
    PChunkyFile second=ChunkyFile::PcflCreateTemp();
    PChunkyResourceFile replacement=second ? ChunkyResourceFile::PcrfNew(second,0) : pvNil;
    if (ok) ok=replacement && actor->FTestCostumeMergeOwnership(replacement);
    ReleasePpo(&actor); ReleasePpo(&resource); ReleasePpo(&file);
    ReleasePpo(&replacement); ReleasePpo(&second);
    if (snapshot) ok=std::fclose(snapshot)==0 && ok;
    return ok;
}

static bool TestActorFillEditing(PScene scene)
{
    FILE *snapshot=nullptr;
    if (const char *path=std::getenv("MM_ACTOR_FILL_EDIT_SNAPSHOT"))
        if (!(snapshot=std::fopen(path,"wb"))) return false;
    PChunkyFile file=ChunkyFile::PcflCreateTemp();
    PChunkyResourceFile resource=file ? ChunkyResourceFile::PcrfNew(file,0) : pvNil;
    auto actor=NewObj TrackTestActor();
    bool ok=actor && resource && actor->FInitPlayback(scene,resource);
    for (int mode=0; ok && mode<528; ++mode) ok=actor->FTestFillEdit(mode,snapshot);
    ReleasePpo(&actor); ReleasePpo(&resource); ReleasePpo(&file);
    if (snapshot) ok=std::fclose(snapshot)==0 && ok;
    return ok;
}

static bool TestActorCostumeEditing(PScene scene)
{
    FILE *snapshot=nullptr;
    if (const char *path=std::getenv("MM_ACTOR_COSTUME_EDIT_SNAPSHOT"))
        if (!(snapshot=std::fopen(path,"wb"))) return false;
    PChunkyFile file=ChunkyFile::PcflCreateTemp();
    PChunkyResourceFile resource=file ? ChunkyResourceFile::PcrfNew(file,1024*1024) : pvNil;
    ChunkNumber materials[2]={},customs[2]={},templateNumber=0;
    byte dummy=0;
    bool ok=resource && file->FAddPv(&dummy,1,kctgTmpl,&templateNumber);
    for (int i=0;ok && i<2;++i)
    {
        MaterialOnFile material={kboCur,koskCur,0xffffff,0,0,0,static_cast<byte>(32+i*16),15,rOne};
        CustomMaterialOnFile custom={kboCur,koskCur,0};
        ok=file->FAddPv(&material,sizeof(material),kctgMtrl,&materials[i]) &&
            file->FAddPv(&custom,sizeof(custom),kctgCmtl,&customs[i]) &&
            file->FAdoptChild(kctgCmtl,customs[i],kctgMtrl,materials[i],0) &&
            file->FAdoptChild(kctgTmpl,templateNumber,kctgCmtl,customs[i],i);
    }
    auto actor=NewObj TrackTestActor();
    ok=ok && actor && actor->FInit(scene,resource);
    for (int mode=0;ok && mode<160;++mode) ok=actor->FTestCostumeEdit(mode,snapshot,resource,templateNumber,materials,customs);
    ReleasePpo(&actor); ReleasePpo(&resource); ReleasePpo(&file);
    if (snapshot) ok=std::fclose(snapshot)==0 && ok;
    return ok;
}

static bool TestActorRouteEditing(PScene scene)
{
    FILE *snapshot=nullptr;
    if (const char *path=std::getenv("MM_ACTOR_ROUTE_EDIT_SNAPSHOT"))
        if (!(snapshot=std::fopen(path,"wb"))) return false;
    PChunkyFile file=ChunkyFile::PcflCreateTemp();
    PChunkyResourceFile resource=file ? ChunkyResourceFile::PcrfNew(file,0) : pvNil;
    auto actor=NewObj TrackTestActor();
    bool ok=actor && resource && actor->FInitPlayback(scene,resource);
    for (int mode=0;ok && mode<480;++mode) ok=actor->FTestRouteEdit(mode,snapshot);
    ReleasePpo(&actor); ReleasePpo(&resource); ReleasePpo(&file);
    if (snapshot) ok=std::fclose(snapshot)==0 && ok;
    return ok;
}

static bool TestActorSubrouteEditing(PScene scene)
{
    FILE *snapshot=nullptr;
    if (const char *path=std::getenv("MM_ACTOR_SUBROUTE_EDIT_SNAPSHOT"))
        if (!(snapshot=std::fopen(path,"wb"))) return false;
    PChunkyFile file=ChunkyFile::PcflCreateTemp();
    PChunkyResourceFile resource=file ? ChunkyResourceFile::PcrfNew(file,1024*1024) : pvNil;
    byte dummy=0; ChunkNumber templateNumber=0,materialNumber=0,customNumber=0;
    MaterialOnFile material={kboCur,koskCur,0xffffff,0,0,0,32,15,rOne};
    CustomMaterialOnFile custom={kboCur,koskCur,0};
    bool ok=resource && file->FAddPv(&dummy,1,kctgTmpl,&templateNumber) &&
        file->FAddPv(&material,sizeof(material),kctgMtrl,&materialNumber) &&
        file->FAddPv(&custom,sizeof(custom),kctgCmtl,&customNumber) &&
        file->FAdoptChild(kctgCmtl,customNumber,kctgMtrl,materialNumber,0) &&
        file->FAdoptChild(kctgTmpl,templateNumber,kctgCmtl,customNumber,0);
    auto actor=NewObj TrackTestActor();
    ok=ok && actor && actor->FInitPlayback(scene,resource);
    for (int mode=0;ok && mode<576;++mode) ok=actor->FTestSubrouteEdit(mode,snapshot,resource,templateNumber,materialNumber);
    const int owningModes[]={256,260,327,335};
    for (int mode:owningModes)
    {
        if (!ok) break;
        const long refs=resource->CactRef();
        ok=actor->FTestSubrouteEdit(mode,nullptr,resource,templateNumber,materialNumber,true);
        if (resource->CactRef()!=refs)
        { std::fprintf(stderr,"Subroute propagation lost owning TAG references\n"); std::exit(1); }
    }
    ReleasePpo(&actor); ReleasePpo(&resource); ReleasePpo(&file);
    if (snapshot) ok=std::fclose(snapshot)==0 && ok;
    return ok;
}

class TestActorUndo : public ActorUndo
{
  public:
    TestActorUndo() {}
    PActor SavedActor() const { return static_cast<PActor>(_undoState.actor); }
    long SavedId() const { return _undoState.actor_id; }
};
static bool TestActorUndoRuntime(PMovie parent)
{
    FILE *snapshot=nullptr;
    if (const char *path=std::getenv("MM_ACTOR_UNDO_SNAPSHOT"))
        if (!(snapshot=std::fopen(path,"wb"))) return false;
    PChunkyFile file=ChunkyFile::PcflCreateTemp();
    PChunkyResourceFile resource=file?ChunkyResourceFile::PcrfNew(file,0):pvNil;
    byte dummy=0; ChunkNumber number=0;
    bool ok=resource && file->FAddPv(&dummy,1,kctgTmpl,&number) && file->FAddPv(&dummy,1,kctgTmpl,&number) && number==1;
    auto record=[&](const void *p,size_t n) { return !snapshot || std::fwrite(p,1,n,snapshot)==n; };
    for (int mode=0;ok && mode<32;++mode)
    {
        const long refs=resource->CactRef();
        PMovie movie=Movie::PmvieNew(fFalse,parent->Pmcc(),pvNil,cnoNil);
        auto actor=NewObj TrackTestActor(); PActor copy=nullptr; auto undo=NewObj TestActorUndo();
        String before,after; before.SetSzs(PszLit("before")); after.SetSzs(PszLit("after"));
        ok=movie && movie->FNewScenInsCore(0) && actor && undo && actor->FInitPlayback(movie->Pscen(),resource);
        if (ok)
        {
            actor->SetArid(aridNil);
            ok=movie->Pscen()->FAddActrCore(actor) && actor->FDup(&copy,false) && movie->FNameActr(actor->Arid(),&after);
        }
        if (ok && (mode&16))
        {
            ok=actor->FCreateUndo(copy,(mode&1)!=0,(mode&4)?&before:nullptr);
            LegacyLong values[]={mode,ok,copy->Pbody()!=nullptr,copy->Pscen()!=nullptr,copy->CactRef(),resource->CactRef()};
            ok=record(values,sizeof(values)); movie->ClearUndo();
        }
        else if (ok)
        {
            copy->Reset();
            if (!(mode&8)) undo->SetPactr(copy);
            undo->SetPmvie(movie); undo->SetIscen(0); undo->SetNfrm(movie->Pscen()->Nfrm());
            undo->SetNfrmLast(2); undo->SetSoonerLater((mode&2)!=0); undo->SetSndUndo((mode&1)!=0);
            undo->SetArid(actor->Arid()); if (mode&4) undo->SetStn(&before);
            ok=actor->FScale(BR_SCALAR(1.5));
            for (int phase=0;ok && phase<2;++phase)
            {
                bool changed=phase?undo->FDo(movie):undo->FUndo(movie);
                PActor live=movie->Pscen()->PactrFromArid(actor->Arid());
                String name; bool named=movie->FGetName(actor->Arid(),&name);
                BRS x=0,y=0,z=0; if (live) live->GetXyzWorld(&x,&y,&z);
                LegacyLong values[]={mode,phase,changed,movie->Pscen()->Nfrm(),undo->Nfrm(),undo->SavedId(),
                    live==actor,live==copy,live!=nullptr,live && live->Pbody(),undo->SavedActor()==actor,undo->SavedActor()==copy,
                    undo->SavedActor()!=nullptr,actor->CactRef(),copy->CactRef(),resource->CactRef(),x,y,z,named,name.Cch()};
                ok=record(values,sizeof(values)) && record(name.Psz(),name.Cch()*sizeof(achar)) && changed;
            }
        }
        ReleasePpo(&undo); ReleasePpo(&copy); ReleasePpo(&actor); ReleasePpo(&movie);
        if (resource->CactRef()!=refs) { std::fprintf(stderr,"Actor undo reference leak at mode %d\n",mode); ok=false; }
    }
    ReleasePpo(&resource); ReleasePpo(&file);
    if (snapshot) ok=std::fclose(snapshot)==0 && ok; return ok;
}

#include "engine_group_undo_fixture.h"
#include "engine_edit_transaction_fixture.h"
#include "engine_actor_copy_fixture.h"
#include "engine_route_paste_fixture.h"
#include "engine_actor_paste_fixture.h"
#include "engine_actor_clipboard_fixture.h"
#include "engine_actor_create_fixture.h"
#include "engine_body_create_fixture.h"
#include "engine_body_edit_fixture.h"
#include "engine_body_runtime_fixture.h"
#include "engine_body_geometry_fixture.h"
#include "engine_template_create_fixture.h"
#include "engine_template_runtime_fixture.h"
#include "engine_action_definition_fixture.h"
#include "engine_font_resource_fixture.h"
#include "engine_text_generation_fixture.h"
#include "engine_text_resource_fixture.h"
#include "engine_sound_resource_fixture.h"
#include "engine_movie_sound_fixture.h"
#include "movie_edit_fixture.h"
#include "movie_content_fixture.h"
#include "movie_compose_fixture.h"
static bool TestActorOwner(PMovie movie)
{
    FILE *snapshot=nullptr;
    if (const char *path=std::getenv("MM_ACTOR_OWNER_SNAPSHOT"))
        if (!(snapshot=std::fopen(path,"wb"))) return false;
    auto scene=NewObj StageTestScene(movie); PChunkyFile file=ChunkyFile::PcflCreateTemp();
    PChunkyResourceFile resource=file?ChunkyResourceFile::PcrfNew(file,0):pvNil;
    bool ok=scene && scene->Init() && resource;
    for (int mode=0;ok && mode<128;++mode)
    {
        const long refs=resource->CactRef(); auto actor=NewObj TrackTestActor();
        ok=actor && actor->FInitPlayback(scene,resource) && actor->FTestOwner(mode,snapshot);
        ReleasePpo(&actor);
        if (resource->CactRef()!=refs) { std::fprintf(stderr,"Actor owner reference leak at mode %d\n",mode); return false; }
    }
    ReleasePpo(&resource); ReleasePpo(&file); ReleasePpo(&scene);
    if (snapshot) ok=std::fclose(snapshot)==0 && ok; return ok;
}

static bool TestActorTransformEditing(PMovie movie)
{
    FILE *snapshot=nullptr;
    if (const char *path=std::getenv("MM_ACTOR_TRANSFORM_EDIT_SNAPSHOT"))
        if (!(snapshot=std::fopen(path,"wb"))) return false;
    auto scene=NewObj StageTestScene(movie); PChunkyFile file=ChunkyFile::PcflCreateTemp();
    PChunkyResourceFile resource=file?ChunkyResourceFile::PcrfNew(file,0):pvNil; auto actor=NewObj TrackTestActor();
    bool ok=scene && scene->Init() && resource && actor && actor->FInitPlayback(scene,resource);
    for (int mode=0;ok && mode<1152;++mode) ok=actor->FTestTransformEdit(mode,snapshot,scene);
    ReleasePpo(&actor); ReleasePpo(&resource); ReleasePpo(&file); ReleasePpo(&scene);
    if (snapshot) ok=std::fclose(snapshot)==0 && ok; return ok;
}

static bool TestActorRecording(PMovie movie)
{
    FILE *snapshot=nullptr;
    if (const char *path=std::getenv("MM_ACTOR_RECORD_SNAPSHOT"))
        if (!(snapshot=std::fopen(path,"wb"))) return false;
    auto scene=NewObj StageTestScene(movie);
    PChunkyFile file=ChunkyFile::PcflCreateTemp();
    PChunkyResourceFile resource=file?ChunkyResourceFile::PcrfNew(file,0):pvNil;
    auto actor=NewObj TrackTestActor();
    bool ok=scene && scene->Init() && resource && actor && actor->FInitPlayback(scene,resource);
    for (int mode=0;ok && mode<640;++mode) ok=actor->FTestRecording(mode,snapshot,scene);
    if (ok) ok=actor->FTestRecording(146,nullptr,scene,true);
    ReleasePpo(&actor); ReleasePpo(&resource); ReleasePpo(&file); ReleasePpo(&scene);
    if (snapshot) ok=std::fclose(snapshot)==0 && ok;
    return ok;
}

static bool TestActorStageEditing(PMovie movie)
{
    FILE *snapshot=nullptr;
    if (const char *path=std::getenv("MM_ACTOR_STAGE_EDIT_SNAPSHOT"))
        if (!(snapshot=std::fopen(path,"wb"))) return false;
    auto scene=NewObj StageTestScene(movie);
    PChunkyFile file=ChunkyFile::PcflCreateTemp();
    PChunkyResourceFile resource=file?ChunkyResourceFile::PcrfNew(file,0):pvNil;
    auto actor=NewObj TrackTestActor();
    bool ok=scene && scene->Init() && resource && actor && actor->FInitPlayback(scene,resource);
    for (int mode=0;ok && mode<256;++mode) ok=actor->FTestStageEdit(mode,snapshot);
    ReleasePpo(&actor); ReleasePpo(&resource); ReleasePpo(&file); ReleasePpo(&scene);
    if (snapshot) ok=std::fclose(snapshot)==0 && ok;
    return ok;
}

/* Independent native BRender oracle for the original _GetNewOrigin policy.
 * Exercise clipping boundaries and non-cardinal, fractional camera matrices. */
static bool TestActorPlacement()
{
    struct Camera { BMAT34 matrix; BRS hither, yon; int calls; } camera{};
    MMActorPlacementServices services{};
    services.context = &camera;
    services.camera = [](void *context, MMActorMatrix *matrix, MMI32 *hither, MMI32 *yon) {
        auto *camera = static_cast<Camera *>(context);
        std::memcpy(matrix, &camera->matrix, sizeof(*matrix));
        *hither = camera->hither; *yon = camera->yon; ++camera->calls;
    };
    const BRS clips[] = {0, 1, 3, BR_SCALAR(25)-1, BR_SCALAR(25), BR_SCALAR(25)+1, BR_SCALAR(50)};
    unsigned seed = 0x7183U;
    for (int mode = 0; mode < 4096; ++mode)
    {
        for (int row = 0; row < 4; ++row)
            for (int column = 0; column < 3; ++column)
            {
                seed = seed * 1664525U + 1013904223U;
                camera.matrix.m[row][column] = static_cast<BRS>(seed % 262145U) - 131072;
            }
        camera.yon = clips[(mode / 16) % (sizeof(clips)/sizeof(clips[0]))];
        camera.hither = camera.yon / ((mode % 3) + 1);
        camera.calls = 0;
        BRS x = (mode & 2) ? -17 : 0;
        BRS y = (mode & 4) ? -BR_SCALAR(3) : 0;
        BRS z = (mode & 8) ? BR_SCALAR(7) : 0;
        MMActorPoint point{x,y,z};
        bool absent = x == 0 && y == 0 && z == 0;
        if (absent)
        {
            BRS distance = kzrDefault;
            if (BR_ABS(distance) > camera.yon)
                distance = BR_CONST_DIV(BR_ADD(camera.yon, camera.hither), -2);
            x = BR_MAC3(rZero,camera.matrix.m[0][0],rZero,camera.matrix.m[1][0],distance,camera.matrix.m[2][0]) + camera.matrix.m[3][0];
            y = rZero;
            z = BR_MAC3(rZero,camera.matrix.m[0][2],rZero,camera.matrix.m[1][2],distance,camera.matrix.m[2][2]) + camera.matrix.m[3][2];
        }
        if (mode & 1) y += BR_SCALAR(10);
        mm_actor_placement(&point, mode & 1, &services);
        if (point.dxr != x || point.dyr != y || point.dzr != z || camera.calls != (absent ? 1 : 0))
        {
            std::fprintf(stderr,"actor placement differs from native BRender at mode %d\n",mode);
            return false;
        }
    }
    std::fprintf(stderr,"[ OK ] 4096 actor placement comparisons against native BRender\n");
    return true;
}

class QueueTestClock : public Clock
{
  public:
    QueueTestClock() : Clock(khidMsqClock + 13)
    {
    }
    long AlarmCount() const
    {
        return _clock.count;
    }
    ulong FirstAlarm() const
    {
        MMClockAlarm alarm = {};
        AssertDo(mm_alarm_clock_at(&_clock, 0, &alarm), "missing alarm");
        return alarm.time;
    }
};

class QueueTestAdapter : public MovieSoundQueue
{
  public:
    QueueTestAdapter() : MovieSoundQueue(khidMsq + 13)
    {
    }
    bool FInit()
    {
        auto output = _Output();
        output.create_clock = [](void *) -> void * { return static_cast<PClock>(NewObj QueueTestClock()); };
        return mm_frame_sound_queue_open(&_queue, KauaiCopyAllocator(),
            [](void *p) { mm_sound_owner_retain(static_cast<PMovieSoundMSND>(p)); },
            [](void *p) { mm_sound_owner_release(static_cast<PMovieSoundMSND>(p)); }, 0, &output) != 0;
    }
    long Pending() const
    {
        return _queue.count;
    }
    long Alarms() const
    {
        return static_cast<QueueTestClock *>(static_cast<PClock>(_queue.clock))->AlarmCount();
    }
    ulong FirstAlarm() const
    {
        return static_cast<QueueTestClock *>(static_cast<PClock>(_queue.clock))->FirstAlarm();
    }
    bool Take(MMFrameSoundRequest *request)
    {
        return mm_frame_sound_queue_dequeue(&_queue, request) != 0;
    }
};

static bool TestSoundQueueRuntime()
{
    QueueTestAdapter *queue = NewObj QueueTestAdapter();
    auto sound = mm_sound_owner_create(&EngineSound::Host());
    if (sound)
    {
        auto state = mm_sound_owner_state(sound);
        state->resource.style = stySfx;
        state->resource.volume = 123;
        state->silent = tYes; // Exercise dispatch without an audio file/device.
    }
    QueueTestAdapter *failedAlarm = pvNil;
    MMFrameSoundRequest request = {};
    ulong firstAlarm = 0;
    bool ok = false;
    if (queue == pvNil || sound == pvNil || !queue->FInit() || queue->DtimSnd() != kdtim2Msq)
        goto done;
    if (!queue->FEnqueue(sound, 37, true, false, 123, 5, true, 456) || mm_sound_owner_references(sound) != 2 || !queue->Take(&request))
        goto done;
    if (request.sound != sound || request.style != stySfx || request.object != 37 || !request.actor ||
        !request.loop || request.queued || request.volume != 123 || request.priority != 5 || request.offset != 456)
        goto done;
    mm_sound_owner_release(static_cast<PMovieSoundMSND>(request.sound));
    request.sound = nullptr;
    if (mm_sound_owner_references(sound) != 1 || mm_sound_queue_number(stySfx, 37, 1) != 0x10020025)
        goto done;
    if (!queue->FEnqueue(sound, 37, false, false, 123, 5))
        goto done;
    queue->PlayMsq();
    if (queue->Pending() != 0 || mm_sound_owner_references(sound) != 1 || queue->Alarms() != 1)
        goto done;
    firstAlarm = queue->FirstAlarm();
    queue->SndOnLong();
    if (!queue->FEnqueue(sound, 37, false, false, 123, 5))
        goto done;
    queue->PlayMsq();
    if (queue->Pending() != 0 || mm_sound_owner_references(sound) != 1 || queue->Alarms() != 1 || queue->FirstAlarm() != firstAlarm)
        goto done; // Long mode adds no alarm and does not cancel the existing preview alarm.
    if (!queue->FEnqueue(sound, 37, false, false, 123, 5))
        goto done;
    queue->SndOff();
    queue->PlayMsq();
    if (queue->Pending() != 0 || mm_sound_owner_references(sound) != 1 || queue->Alarms() != 1)
        goto done;
#ifdef DEBUG
    failedAlarm = NewObj QueueTestAdapter();
    if (failedAlarm == pvNil || !failedAlarm->FInit() || !failedAlarm->FEnqueue(sound, 37, false, false, 123, 5))
        goto done;
    {
        vmutxMem.Enter();
        vdmglob.dmaglHq.cactDo = 0;
        vdmglob.dmaglHq.cactFail = 2;
        vmutxMem.Leave();
        failedAlarm->PlayMsq(); // Fail both growth attempts for the first alarm after the batch drains.
        vmutxMem.Enter();
        bool consumed = vdmglob.dmaglHq.cactFail == 0;
        vdmglob.dmaglHq.cactFail = 0;
        vmutxMem.Leave();
        vpers->Clear();
        if (!consumed || failedAlarm->Alarms() != 0 || failedAlarm->Pending() != 0 || mm_sound_owner_references(sound) != 1)
            goto done;
    }
#endif
    ok = true;
done:
    if (request.sound)
        mm_sound_owner_release(static_cast<PMovieSoundMSND>(request.sound));
    ReleasePpo(&failedAlarm);
    ReleasePpo(&queue);
    mm_sound_owner_release(sound);
    return ok;
}

static bool TestPlaybackAlarmRuntime(PMovie movie)
{
    bool oldIdle = movie->FIdleSeen();
    bool ok = false;
    Command alarm = {};
    alarm.cid = cidAlarm;
    vpcex->FlushCid(cidRender);
    movie->SetFIdleSeen(false);
    if (!movie->FCmdAlarm(&alarm) || vpcex->FCidIn(cidRender))
        goto done;
    movie->SetFIdleSeen(true);
    if (!movie->FCmdAlarm(&alarm) || movie->FIdleSeen() || !vpcex->FCidIn(cidRender))
        goto done;
    ok = true;
done:
    vpcex->FlushCid(cidRender); // Verify the handoff without invoking a window-dependent render callback.
    movie->Pclok()->RemoveCmh(movie);
    movie->SetFIdleSeen(oldIdle);
    return ok;
}

// Access an existing Movie's protected playback value through a member pointer;
// no replacement Movie or window-dependent render loop is needed for this check.
class PlaybackProbe : public Movie
{
  public:
    static const MMPlaybackState &State(const Movie &movie)
    {
        return movie.*(&PlaybackProbe::_playback);
    }
};

static bool TestPauseEventRuntime(PMovie movie, TestMcc *client)
{
    auto scene = movie->Pscen();
    const long durations[] = {1, 0x11223344};
    for (long duration : durations)
    {
        WaitReason reason = witForTime;
        long ticks = duration;
        if (!scene->FPauseCore(&reason, &ticks) || reason != witNil)
            return false;
        client->pause = witNil;
        if (!scene->FReplayFrm(fscenPauses) || client->pause != witForTime ||
            PlaybackProbe::State(*movie).wait != witForTime ||
            PlaybackProbe::State(*movie).pauseTicks != duration)
            return false;
        reason = witNil;
        ticks = 0;
        if (!scene->FPauseCore(&reason, &ticks) || reason != witForTime || ticks != duration)
            return false;
    }
    movie->DoPause(witNil, 0);
    return true;
}

static bool TestPlayStartupRuntime(PMovie movie, TestMcc *client)
{
    if (movie->Pscen() == pvNil || movie->Pscen()->Cactr() != 0 || movie->Pscen()->Ctbox() != 0 || movie->FPlaying())
        return false;
    bool oldSounds = movie->FSoundsEnabled();
    ulong oldSceneFlags = movie->Pscen()->GrfScen();
    long stopped = client->stopped;
    long disabled = client->disabled;
    bool ok = false;
    movie->Play();
    if (!movie->FPlaying() || movie->FStopPlaying() || !movie->FSoundsEnabled() || movie->Cnfrm() != 0 ||
        movie->Pmsq()->DtimSnd() != kdtimLongMsq || client->disabled != disabled + 1 || client->stopped != stopped ||
        !vpcex->FCidIn(cidMviePlaying))
        goto done;
    movie->Play();
    if (!movie->FPlaying() || !movie->FStopPlaying() || client->stopped != stopped)
        goto done;
    movie->Play(); // Already stopping: no duplicate client notification or startup.
    if (!movie->FStopPlaying() || client->stopped != stopped || client->disabled != disabled + 1)
        goto done;
    ok = true;
done:
    // Isolate startup/toggle from the window-dependent render/stop callback.
    // Restore the test environment directly; this does not test native stop cleanup.
    movie->Pclok()->Stop();
    movie->Pclok()->RemoveCmh(movie);
    vpcex->FlushCid(cidRender);
    vpcex->FlushCid(cidMviePlaying);
    movie->SetFStopPlaying(false);
    movie->SetFPlaying(false);
    movie->SetFSoundsEnabled(oldSounds);
    movie->Pmsq()->SndOnShort();
    if (movie->Pscen() != pvNil)
    {
        // This empty fixture never prerenders actors. StopPlaying would try
        // to restore an authored background which the fixture does not have.
        movie->Pscen()->Enable(~0UL);
        movie->Pscen()->Disable(oldSceneFlags);
    }
    client->EnableAccel();
    return ok;
}

static bool TestCaptionPersistence(PScene scene)
{
    PChunkyFile file = pvNil;
    PChunkyResourceFile resources = pvNil;
    PTBOX caption = pvNil;
    RichTextData content, exported;
    TextBoxData metadata, saved;
    ChunkIdentification richText, child;
    ChunkNumber number, second;
    bool ok = false;
    byte text[] = {'H', 'e', 'l', 'l', 'o'};
    achar actual[5];
    file = ChunkyFile::PcflCreateTemp();
    content.properties = DynamicArray::PglNew(size(RichTextPropertyEntry));
    content.fontName.SetSzs(PszLit("Arial"));
    content.text.pfil = FileObject::PfilCreateTemp();
    content.text.cb = sizeof(text);
    if (file == pvNil || content.properties == pvNil || content.text.pfil == pvNil ||
        !content.text.pfil->FSetFpMac(content.text.cb) || !content.text.FWriteRgb(text, sizeof(text), 0))
        goto done;
    if (!content.FWrite(file, &richText) || !file->FAdd(0, kctgTbox, &number))
        goto done;
    metadata.state.first = 1;
    metadata.state.end = klwMax;
    metadata.state.story = true;
    metadata.SetBounds(10, 20, 180, 100);
    if (!metadata.FWrite(file, number) || !file->FAdoptChild(kctgTbox, number, richText.ctg, richText.cno, 0))
        goto done;
    resources = ChunkyResourceFile::PcrfNew(file, 0);
    if (resources == pvNil)
        goto done;
    caption = TextBox::PtboxRead(resources, number, scene);
    if (caption == pvNil || caption->CpMac() != 6)
        goto done;
    caption->FetchRgch(0, 5, actual);
    if (std::memcmp(actual, text, sizeof(text)) != 0)
        goto done;
    if (!file->FAdd(0, kctgTbox, &second) || !caption->FWrite(file, second))
        goto done;
    if (!saved.FRead(file, second, &child) || !exported.FRead(file, child.ctg, child.cno))
        goto done;
    if (!saved.state.story || saved.state.first != 1 || exported.text.cb != 5)
        goto done;
    if (!exported.text.FReadRgb(actual, 5, 0) || std::memcmp(actual, text, 5) != 0)
        goto done;
    if (!TestLiveCaptionSaving(scene, file))
        goto done;
    if (!TestTextDocumentFiles(scene, file))
        goto done;
    if (!TestByteDocumentFiles())
    {
        std::fprintf(stderr, "[document] byte-stream save or reopen changed the source bytes\n");
        goto done;
    }
    if (!TestTextMaterialization())
        goto done;
    if (!TestTextProperties())
        goto done;
    if (!TestTextFormat())
        goto done;
    if (!TestTextAccess())
        goto done;
    if (!TestTextCopy())
        goto done;
    if (!TestTextUndo())
        goto done;
    if (!TestDocumentUndo())
        goto done;
    if (!TestDocumentLifecycle())
        goto done;
    if (!TestDocumentViews())
        goto done;
    if (!TestHelpText())
        goto done;
    if (!TestHelpDisplay())
        goto done;
    if (!TestHelpInteraction())
        goto done;
    if (!TestHelpBalloon())
        goto done;
    if (!TestHelpTopic())
        goto done;
    if (!TestTextBoxRuntime(scene, file, richText))
        goto done;
    if (!TestTextBoxEdit(scene, file))
        goto done;
    ok = true;
done:
    ReleasePpo(&caption);
    ReleasePpo(&resources);
    ReleasePpo(&file);
    return ok;
}

int __cdecl main(int argc, char **argv)
{
    int rc = 0;
    PMovie pmvie = pvNil;
    TestMcc *pmcc = pvNil;
    TestApp *papp = pvNil;
    Filename fniMovie;
    Filename fniHDRoot;
    String stnPath;
    bool fSaved = fFalse;

    LOG("constructing TestApp (sets vpappb)");
    papp = NewObj TestApp();
    if (papp == pvNil)
        BAIL("new TestApp failed");
    if (argc > 1 && std::strcmp(argv[1], "--graph-frame") == 0)
    {
        rc = GraphFrameFixture(argc, argv);
        ReleasePpo(&papp);
        return rc;
    }
#if defined(KAUAI_SDL3) && !defined(WIN)
    {
        auto palette = DynamicArray::PglNew(sizeof(Color), 256);
        if (!palette || !palette->FSetIvMac(256)) BAIL("palette fixture failed");
        for (int i = 0; i < 256; ++i)
        {
            const auto level = static_cast<byte>(i);
            Color color{level, level, level, 0};
            palette->Put(i, &color);
        }
        GraphicsPort::SetActiveColors(palette, 0);
        ReleasePpo(&palette);
    }
#endif

    LOG("initializing vpcex (CommandExecutionManager) -- needed by Clock::Stop");
    /* vpcex is normally created by ApplicationBase::_FInit. Clock::Stop
     * (called transitively from MovieSoundQueue::~MovieSoundQueue ->
     * StopAll -> Clock::Stop) does vpcex->RemoveCmh(this, ...) so we
     * have to provide a real CommandExecutionManager. */
    vpcex = CommandExecutionManager::PcexNew(20, 20);
    if (vpcex == pvNil)
        BAIL("CommandExecutionManager::PcexNew failed");

    LOG("initializing vpsndm (SoundManager) -- needed by FSwitchScen");
    /* vpsndm is normally created by ApplicationBase::_FInit. Our test
     * skips _FInit, so we have to set it up by hand. Movie::FSwitchScen
     * calls vpsndm->StopAll() unconditionally; without this the scene
     * insert path null-derefs. */
    vpsndm = SoundManager::PsndmNew();
    if (vpsndm == pvNil)
        BAIL("SoundManager::PsndmNew failed");

    LOG("initializing vptagm");
    /* TagManager wants a directory that exists. Use the current
     * directory; on Windows that's wherever the test is invoked
     * from. */
    if (argc >= 2)
    {
        stnPath.SetSzs(argv[1]);
    }
    else
    {
        stnPath.SetSzs(PszLit("."));
    }
    if (!fniHDRoot.FBuildFromPath(&stnPath))
        BAIL("FBuildFromPath(hd-root) failed");
    vptagm = TagManager::PtagmNew(&fniHDRoot, FInsertCdStub, 0);
    if (vptagm == pvNil)
        BAIL("PtagmNew failed");

    LOG("constructing TestMcc");
    pmcc = NewObj TestMcc();
    if (pmcc == pvNil)
        BAIL("new TestMcc failed");

    LOG("PmvieNew(pmcc, pvNil) -- blank movie");
    pmvie = Movie::PmvieNew(fFalse, pmcc, pvNil, cnoNil);
    if (pmvie == pvNil)
        BAIL("PmvieNew(blank) returned pvNil");
    {
        String title;
        Filename unused;
        pmcc->testingTitle = true;
        pmvie->ResetTitle(); pmvie->GetName(&title);
        if (!title.FEqualSz(PszLit("Default movie")) || pmcc->titleUpdates != 1)
            BAIL("default title notification failed");
        if (pmvie->FGetFniSave(&unused)) BAIL("save-dialog stub unexpectedly accepted");
        pmvie->GetName(&title);
        if (!title.FEqualSz(PszLit("Changed by client"))) BAIL("cancelled save title mutation lost");
        pmcc->testingTitle = false;
        pmvie->ResetTitle(); // Restore the original empty-title fixture.
    }

    LOG("Play without a scene notifies the client without starting playback");
    {
        long stopped = pmcc->stopped;
        pmvie->Play();
        if (pmcc->stopped != stopped + 1 || pmvie->FPlaying())
            BAIL("empty-movie playback startup failed");
    }

    LOG("FNewScenInsCore(0) -- give the movie one empty scene");
    if (!TestMovieEdit())
        BAIL("movie actor/roll-call edit control failed");
    if (!TestMovieContent())
        BAIL("movie material/text/caption control failed");
    if (!TestMovieCompose())
        BAIL("movie composition control failed");
    if (!TestPrivateMovieCompose())
        BAIL("private archive movie composition failed");
    if (!TestMovieSave(pmvie))
        BAIL("movie save control failed");
    if (!TestPrivateMovieSave(pmvie))
        BAIL("private archive movie save failed");
    if (!TestMovieScenes())
        BAIL("movie scene container control failed");
    if (!pmvie->FNewScenInsCore(0))
        BAIL("FNewScenInsCore(0) failed");

    LOG("real empty-scene playback startup and stop-request toggle");
    if (!TestPlayStartupRuntime(pmvie, pmcc))
        BAIL("playback startup adapter failed");

    LOG("computing temp output path");
    {
        String stnOut;
        if (argc >= 3)
        {
            stnOut.SetSzs(argv[2]);
        }
        else
        {
            stnOut.SetSzs(PszLit("movie-save-load-test.3mm"));
        }
        if (!fniMovie.FBuildFromPath(&stnOut))
            BAIL("FBuildFromPath(out) failed");
    }

    LOG("FSaveToFni(<out>, fTrue) -- save");
    if (!pmvie->FSaveToFni(&fniMovie, fTrue))
        BAIL("FSaveToFni failed");
    fSaved = fTrue;

    LOG("ReleasePpo(&pmvie) -- close");
    ReleasePpo(&pmvie);

    LOG("editing the saved scene through the core persistence API");
    {
        MovieData stored;
        if (!stored.FLoad(&fniMovie))
            BAIL("core movie load failed");
        const MovieArchiveFixture archive(stored.Resources());
        if (stored.Resources()->Pcfl())
            BAIL("core movie opened through the native archive wrapper");
        ChunkNumber scene;
        if (!archive.Link(kctgMvie, stored.movieNumber, 0, kctgScen, &scene))
            BAIL("saved movie has no scene");
        const auto titles = KauaiChunkOwnerTitleIO();
        const auto io = mm_scene_file_io(archive.reader, &archive.writer, &titles);
        SceneData metadata;
        if (!mm_scene_read(&metadata, scene, &io))
            BAIL("core scene read failed");
        metadata.transition = transFadeToBlack;
        if (!mm_scene_write(&metadata, scene, &io) || !stored.FSave(&fniMovie))
            BAIL("core scene edit/save failed");
    }

    LOG("PmvieNew(pmcc, &fni) -- reopen");
    pmvie = Movie::PmvieNew(fFalse, pmcc, &fniMovie, cnoNil);
    if (pmvie == pvNil)
        BAIL("PmvieNew(reopen) returned pvNil");

    if (!pmvie->FSwitchScen(0) || pmvie->Pscen()->Trans() != transFadeToBlack)
        BAIL("runtime failed to read the scene edited by core");

    LOG("fixed-width scene pause events reach playback without adjacent reads");
    if (!TestPauseEventRuntime(pmvie, pmcc))
        BAIL("scene pause event runtime boundary failed");

    LOG("actor track duplication, failure cleanup and undo restoration");
    if (!TestActorTrackRuntime(pmvie->Pscen()))
        BAIL("actor track runtime ownership failed");

    LOG("actor frame sequencing and direct/backward/forward seek equivalence");
    if (!TestActorFrameRuntime(pmvie->Pscen()))
        BAIL("actor frame runtime evaluation failed");

    LOG("sound queue ownership, dispatch and preview alarms");
    if (!TestActorSoundEditing(pmvie->Pscen()))
        BAIL("actor sound editing failed");
    if (!TestActorEventEditing(pmvie->Pscen()))
        BAIL("actor event editing failed");
    if (!TestActorFillEditing(pmvie->Pscen()))
        BAIL("actor fill editing failed");
    if (!TestActorCostumeEditing(pmvie->Pscen()))
        BAIL("actor costume editing failed");
    if (!TestActorRouteEditing(pmvie->Pscen()))
        BAIL("actor route editing failed");
    if (!TestActorSubrouteEditing(pmvie->Pscen()))
        BAIL("actor subroute editing failed");
    if (!TestActorPlacement())
        BAIL("actor placement failed");
    if (!TestActorStageEditing(pmvie))
        BAIL("actor stage editing failed");
    if (!TestActorRecording(pmvie))
        BAIL("actor recording failed");
    if (!TestActorTransformEditing(pmvie))
        BAIL("actor transform editing failed");
    if (!TestActorOwner(pmvie))
        BAIL("actor owner failed");
    if (!TestActorUndoRuntime(pmvie))
        BAIL("actor undo runtime failed");
    if (!TestActorGroupUndo(pmvie))
        BAIL("actor group undo failed");
    if (!TestActorEditTransactions(pmvie))
        BAIL("actor edit transactions failed");
    if (!TestActorCopy(pmvie))
        BAIL("actor copy failed");
    if (!TestActorRoutePaste(pmvie))
        BAIL("actor route paste failed");
    if (!TestActorPaste(pmvie))
        BAIL("actor paste failed");
    if (!TestActorClipboard(pmvie))
        BAIL("actor clipboard failed");
    if (!TestBodyCreation())
        BAIL("body creation failed");
    if (!TestBodyEditing())
        BAIL("body editing failed");
    if (!TestBodyRuntime())
        BAIL("body runtime failed");
    if (!TestBodyGeometry())
        BAIL("body geometry failed");
    if (!TestTemplateConstruction())
        BAIL("template construction failed");
    if (!TestTemplateRuntime())
        BAIL("template runtime failed");
    if (!TestActionDefinition())
        BAIL("action definition failed");
    if (!TestFontResources())
        BAIL("font resources failed");
    if (!TestSoundResources())
        BAIL("sound resources failed");
    if (!TestSoundResourceDispatch())
        BAIL("sound resource dispatch failed");
#if defined(WIN) && !defined(KAUAI_SDL3)
    if (!TestAudioManStream())
        BAIL("AudioMan stream boundary failed");
#endif
    if (!TestTextResources())
        BAIL("text resources failed");
    if (!TestTextGeneration())
        BAIL("text generation failed");
    if (!TestActorCreation(pmvie))
        BAIL("actor creation failed");
    if (!TestMovieSoundIntegration(pmvie))
        BAIL("movie sound integration failed");
    if (!TestSoundQueueRuntime())
        BAIL("sound queue runtime boundary failed");

    LOG("playback alarm/idle handoff to render commands");
    if (!TestPlaybackAlarmRuntime(pmvie))
        BAIL("playback alarm boundary failed");

    LOG("core -> runtime -> core caption persistence");
    {
#ifdef WIN
        vwig.hdcApp = GetDC(NULL);
        bool captionOK = vwig.hdcApp != NULL && vntl.FInit() && TestCaptionPersistence(pmvie->Pscen());
        if (vwig.hdcApp != NULL)
            ReleaseDC(NULL, vwig.hdcApp);
        vwig.hdcApp = NULL;
#else
        bool captionOK = vntl.FInit() && TestCaptionPersistence(pmvie->Pscen());
#endif
        if (!captionOK)
            BAIL("caption persistence failed");
    }

    LOG("ReleasePpo(&pmvie) -- close (after reopen)");
    ReleasePpo(&pmvie);

    fprintf(stderr, "[ OK ] save -> close -> reopen -> close round-trip clean\n");

LDone:
    ReleasePpo(&pmvie);
    if (pmcc != pvNil)
        ReleasePpo(&pmcc);
    ReleasePpo(&vptagm);
    ReleasePpo(&vpsndm);
    ReleasePpo(&vpcex);
    if (papp != pvNil)
        delete papp;
    /* If save succeeded but the test failed later, leave the file on
     * disk so the user can inspect it. Otherwise clean up. */
    if (fSaved && rc == 0)
    {
        String stnPathDelete;
        fniMovie.GetStnPath(&stnPathDelete);
        fprintf(stderr, "[note] saved movie left at %s\n", stnPathDelete.Psz());
    }
    return rc;
}
