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

#include "soc.h"
#include "actor_frame_fixture.h"
ASSERTNAME

#include <stdio.h>
#include <cstring>
#include <cstdlib>

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
    long titleUpdates = 0;
    void GetStn(long ids, PString value) override
    {
        if (testingTitle && ids == idsEngineDefaultTitle) value->SetSzs(PszLit("Default movie"));
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

#define LOG(msg) do { fprintf(stderr, "[step] %s\n", msg); fflush(stderr); } while (0)
#define BAIL(msg) do { fprintf(stderr, "[FAIL] %s\n", msg); fflush(stderr); rc = 1; goto LDone; } while (0)

// Minimal valid in-memory template: duplication uses its ownership, not assets.
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
    static bool FReadCostumeFixture(PChunkyResourceFile,ChunkTagOrType,ChunkNumber,PDataBlock,
                                    PBaseCacheableObject *out,LegacyLong *bytes)
    {
        *bytes=sizeof(TrackTestTemplate);
        if (!out) return true;
        auto tmpl=NewObj TrackTestTemplate();
        *out=tmpl;
        if (!tmpl || !tmpl->FInit()) { ReleasePpo(out); return false; }
        tmpl->_ccmid=3;
        return true;
    }
    void FailAfter(int calls) { _failAfterFetch = calls; }
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

class TrackTestActor : public Actor
{
  public:
    bool FTestRouteEdit(int mode,FILE *snapshot)
    {
        using namespace ActorEvent;
        ActorTrack track; if (!track.FCreate()) return false;
        const long distances[]={4,6,0,5,0,0};
        const long positions[]={0,4,10,20,25,30};
        for (int i=0;i<6;++i)
        {
            RouteDistancePoint point={{BR_SCALAR(positions[i]),rZero,rZero},BR_SCALAR(distances[i])};
            if (!track.Route()->FInsert(i,&point)) return false;
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
            return track.Events()->FInsert(track.Events()->IvMac(),bytes,&value,&e);
        };
        if (!append(aetAdd,1,0,0,0) || !append(aetActn,1,0,0,0) || !append(aetTweak,3,0,2,0) ||
            !append(aetFreeze,4,1,0,0) || !append(aetStep,5,2,0,0) || !append(aetAdd,8,3,0,0) ||
            !append(aetStep,10,4,0,0) || !append(aetRem,11,4,0,1) || !append(aetAdd,15,5,0,0) ||
            !append(aetFreeze,15,5,0,0)) return false;
        _track.Swap(track); _motionSounds.Clear();
        _state=ActorFrameState(); _state.RewindRoute(_track);
        _state.frame=3; _state.location={0,BR_SCALAR(2),0}; _state.onStage=true; _state.celCount=4;
        _state.nextEvent=3; _state.firstEventInFrame=2; _state.actionEvent=1; _state.addEvent=0;
        _nfrmFirst=1; _nfrmLast=15; _fLifeDirty=false; _fModeRecord=true;
        bool ok=true,alive=true; long found=-99;
        RouteDistancePoint supplied={{BR_SCALAR(99),rZero,rZero},rOne};
        if (mode<128)
        {
            long node=(mode%64)%6,first=((mode%64)/6)%4;
            if (mode&32)
            {
                Base e; _track.Events()->GetFixed(2,&e); e.rtel.dwrOffset=klwMax-1; _track.Events()->PutFixed(2,&e);
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
                Base e; _track.Events()->GetFixed(add-1,&e);
                Base entrance; _track.Events()->GetFixed(add,&entrance);
                e.rtel.irpt=entrance.rtel.irpt-((mode&4)?1:0); e.rtel.dwrOffset=(mode&4)?rOne:rZero;
                _track.Events()->PutFixed(add-1,&e);
            }
            _state.location.irpt=(mode&8)?5:0;
            _DelAddFrame(add,limit);
        }
        else if (mode<224)
        {
            _state.location.dwrOffset=(mode&1)?rZero:BR_SCALAR(2);
            _state.nextEvent=(mode&2)?3:5;
            _TruncateSubRte((mode&4)?3:5);
        }
        else if (mode<288)
        {
            long kind=(mode-224)%aetLim,first=(mode-224)%10;
            ok=(mode&1)?_FFindPrevAevAet(kind,first,&found):_FFindNextAevAet(kind,first,&found);
        }
        else if (mode<416)
        {
            if (mode&1) { _state.location.dwrOffset=rZero; _state.location.irpt=1; }
            if (mode&2) _state.onStage=false;
            if (mode&4) { _state.frame=0; _state.nextEvent=0; _state.firstEventInFrame=0; _state.location={0,rZero,0}; }
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
        LegacyLong state[]={mode,ok,alive,found,_state.nextEvent,_state.firstEventInFrame,_state.actionEvent,_state.addEvent,
            _state.location.irpt,_state.location.dwrOffset,_state.location.dnfrm,_state.frame,_state.onStage,_state.frozen,
            _state.step,_nfrmFirst,_nfrmLast,_fLifeDirty,_fModeRecord,supplied.dwr,_track.Route()->IvMac(),_track.Events()->IvMac()};
        bool saved=record(state,sizeof(state));
        for (long i=0;saved && i<_track.Route()->IvMac();++i)
        { RouteDistancePoint point; _track.Route()->Get(i,&point); saved=record(&point,sizeof(point)); }
        for (long i=0;saved && i<_track.Events()->IvMac();++i)
        { Base e; _track.Events()->GetFixed(i,&e); saved=record(&e,sizeof(e)); }
        return saved;
    }
    bool FTestCostumeEdit(int mode,FILE *snapshot,PChunkyResourceFile resource,ChunkNumber templateNumber,
                          const ChunkNumber *materials,const ChunkNumber *customs)
    {
        using namespace ActorEvent;
        auto tmpl=static_cast<PTemplate>(resource->PbacoFetch(kctgTmpl,templateNumber,TrackTestTemplate::FReadCostumeFixture));
        if (!tmpl) return false;
        ReleasePpo(&_ptmpl); _ptmpl=tmpl;
        auto a=static_cast<PMaterial_MTRL>(resource->PbacoFetch(kctgMtrl,materials[0],Material_MTRL::FReadMtrl));
        auto b=static_cast<PMaterial_MTRL>(resource->PbacoFetch(kctgMtrl,materials[1],Material_MTRL::FReadMtrl));
        auto ca=static_cast<PCustomMaterial_CMTL>(resource->PbacoFetch(kctgCmtl,customs[0],CustomMaterial_CMTL::FReadCmtl));
        auto cb=static_cast<PCustomMaterial_CMTL>(resource->PbacoFetch(kctgCmtl,customs[1],CustomMaterial_CMTL::FReadCmtl));
        if (!a || !b || !ca || !cb) return false;
        PDynamicArray parents=DynamicArray::PglNew(sizeof(short)),sets=DynamicArray::PglNew(sizeof(short));
        short parent=-1,set=0;
        bool ok=parents && sets && parents->FAdd(&parent) && sets->FAdd(&set);
        set=1; ok=ok && parents->FAdd(&parent) && sets->FAdd(&set);
        ReleasePpo(&_pbody); _pbody=ok ? Body::PbodyNew(parents,sets) : pvNil;
        ReleasePpo(&parents); ReleasePpo(&sets);
        if (!_pbody) return false;
        _pbody->SetPartSetMtrl(0,a); _pbody->SetPartSetMtrl(1,a);
        if (mode&4) _pbody->SetPartSetCmtl(ca);
        ActorTrack track; ok=track.FCreate();
        RouteDistancePoint point={{rZero,rZero,rZero},rZero};
        Base event={aetAdd,1,{0,rZero,0}}; Add add={};
        ok=ok && track.Route()->FInsert(0,&point) && track.Events()->FInsert(0,sizeof(add),&add,&event);
        auto append=[&](int type,int material,int bodySet,int frame) {
            Costume costume={}; costume.ibset=bodySet; costume.fCmtl=static_cast<tribool>(type); costume.cmid=material;
            costume.tag={ksidUseCrf,resource,kctgMtrl,materials[material%2]};
            event={aetCost,frame,{0,rZero,frame-1}};
            if (!type) TagManager::DupTag(&costume.tag);
            if (track.Events()->FInsert(track.Events()->IvMac(),sizeof(costume),&costume,&event)) return true;
            if (!type) TagManager::CloseTag(&costume.tag);
            return false;
        };
        int oldType=(mode&8)?1:0;
        ok=ok && append(oldType,0,0,2) && append(oldType,0,1,3) && append((mode&16)?1:0,1,0,4) &&
            append((mode&16)?1:0,1,0,5) && append((mode&32)?1:0,0,0,6);
        event={aetAdd,7,{0,rZero,6}};
        ok=ok && track.Events()->FInsert(track.Events()->IvMac(),sizeof(add),&add,&event) && append(oldType,0,0,8);
        if (!ok) return false;
        _track.Swap(track); _motionSounds.Clear(); _state=ActorFrameState();
        _state.frame=2; _state.location={0,rZero,1}; _state.onStage=true;
        _state.firstEventInFrame=_state.nextEvent=1; _state.actionEvent=-1; _state.addEvent=0;
        _nfrmFirst=1; _nfrmLast=8;
        Costume fresh={}; fresh.ibset=(mode&64)?1:0; fresh.fCmtl=static_cast<tribool>(mode%3);
        fresh.cmid=(mode>=128 && (mode&8))?2:1; fresh.tag={ksidUseCrf,resource,kctgMtrl,materials[1]};
        if (mode>=128) ok=FSetCostumeCore(fresh.ibset,&fresh.tag,fresh.cmid,fresh.fCmtl);
        else _PrepCostFill(1,&fresh);
        auto record=[&](const void *p,size_t n) { return !snapshot || std::fwrite(p,1,n,snapshot)==n; };
        LegacyLong state[]={mode,ok,_state.nextEvent,_state.firstEventInFrame,_state.actionEvent,_state.addEvent,_nfrmFirst,_nfrmLast,_track.Events()->IvMac()};
        bool saved=record(state,sizeof(state));
        for (long i=0;saved && i<_track.Events()->IvMac();++i)
        {
            Base fixed; _track.Events()->GetFixed(i,&fixed); saved=record(&fixed,sizeof(fixed));
            if (fixed.aet==aetCost)
            {
                Costume costume; _track.Events()->Get(i,&costume);
                LegacyLong fields[]={costume.ibset,costume.fCmtl,costume.cmid,static_cast<LegacyLong>(costume.tag.cno)};
                saved=saved && record(fields,sizeof(fields));
            }
        }
        bool ordinary; PMaterial_MTRL material; PCustomMaterial_CMTL custom;
        _pbody->GetPartSetMaterial(0,&ordinary,&material,&custom);
        LegacyLong body[]={ordinary,ordinary ? (material==a?0:material==b?1:2) : (custom==ca?0:custom==cb?1:2)};
        saved=saved && record(body,sizeof(body));
        _track.Clear(); ReleasePpo(&_pbody);
        LegacyLong refs[]={a->CactRef(),b->CactRef(),ca->CactRef(),cb->CactRef()};
        saved=saved && record(refs,sizeof(refs));
        ReleasePpo(&a); ReleasePpo(&b); ReleasePpo(&ca); ReleasePpo(&cb);
        return saved;
    }
    bool FTestFillEdit(int mode, FILE *snapshot)
    {
        using namespace ActorEvent;
        ActorTrack track;
        if (!track.FCreate()) return false;
        RouteDistancePoint point={{rZero,rZero,rZero},BR_SCALAR(10)};
        if (!track.Route()->FInsert(0,&point)) return false;
        point.dwr=rZero; if (!track.Route()->FInsert(1,&point)) return false;
        auto append=[&](long kind,long value,long offset) {
            Base event={kind,offset+1,{0,BR_SCALAR(offset),0}};
            union { Add add; Action action; RoutePoint point; BMAT34 matrix; int32_t scalar; } payload={};
            long bytes=4;
            if (kind==aetAdd) bytes=sizeof(Add);
            else if (kind==aetActn) { payload.action={static_cast<int32_t>(value),0}; bytes=sizeof(Action); }
            else if (kind==aetRotF || kind==aetRotH) { BrMatrix34Identity(&payload.matrix); payload.matrix.m[3][0]=BR_SCALAR(value); bytes=sizeof(BMAT34); }
            else if (kind==aetPull || kind==aetTweak) { payload.point={BR_SCALAR(value),rOne,rOne}; bytes=sizeof(RoutePoint); }
            else payload.scalar=static_cast<int32_t>(value);
            return track.Events()->FInsert(track.Events()->IvMac(),bytes,&payload,&event);
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
        _track.Swap(track); _motionSounds.Clear();
        _state=ActorFrameState(); _state.RewindRoute(_track); _state.frame=3; _state.location={0,BR_SCALAR(2),0};
        _state.onStage=true; _state.action=0; _state.celCount=4; _state.actionFlags=factnRotateY;
        _state.actionEvent=1; _state.addEvent=0; _state.firstEventInFrame=2; _state.nextEvent=2;
        _nfrmFirst=1; _nfrmLast=20; _fLifeDirty=false;
        bool ok=true;
        auto tmpl=static_cast<TrackTestTemplate *>(_ptmpl);
        if (actionSet)
        {
            int variant=mode-512;
            if (variant&1) { _state.action=-1; _state.actionEvent=-1; }
            if (variant&2) _state.location={0,BR_SCALAR(10),0};
            tmpl->FailAfter(variant>=8 ? (variant-8)/2+1 : 0);
            ok=FSetActionCore(0,variant%4,(variant&4)!=0);
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
        LegacyLong state[]={mode,ok,_state.nextEvent,_state.firstEventInFrame,_state.actionEvent,_state.addEvent,
            _state.action,_state.cel,_state.frozen,_state.step,_nfrmFirst,_nfrmLast,_fLifeDirty,_track.Events()->IvMac()};
        bool saved=record(state,sizeof(state));
        unsigned char data[100];
        for (long i=0; saved && i<_track.Events()->IvMac(); ++i)
        {
            Base event; _track.Events()->GetFixed(i,&event);
            long bytes=_track.Events()->Cb(i); if (bytes>sizeof(data)) return false;
            _track.Events()->Get(i,data); saved=record(&event,sizeof(event)) && record(data,bytes);
        }
        return saved;
    }
    bool FTestCostumeMergeOwnership(PChunkyResourceFile replacement)
    {
        auto original=_tagTmpl.pcrf;
        const long originalRefs=original->CactRef(), replacementRefs=replacement->CactRef();
        ActorTrack track;
        if (!track.FCreate()) return false;
        RouteDistancePoint point={{rZero,rZero,rZero},rZero};
        ActorEvent::Base event={ActorEvent::aetAdd,1,{0,rZero,0}};
        ActorEvent::Add add={};
        if (!track.Route()->FInsert(0,&point) || !track.Events()->FInsert(0,sizeof(add),&add,&event)) return false;
        for (int i=0; i<2; ++i)
        {
            ActorEvent::Costume costume={};
            costume.ibset=1; costume.tag={0,i ? replacement : original,kctgMtrl,0};
            event.aet=ActorEvent::aetCost; event.nfrm=4;
            TagManager::DupTag(&costume.tag);
            if (!track.Events()->FInsert(i+1,sizeof(costume),&costume,&event))
            { TagManager::CloseTag(&costume.tag); return false; }
        }
        _track.Swap(track); _motionSounds.Clear();
        _state.firstEventInFrame=1; _state.nextEvent=2; _state.actionEvent=-1; _state.addEvent=0;
        long result=-1; _MergeAev(1,2,&result);
        if (result!=1 || original->CactRef()!=originalRefs || replacement->CactRef()!=replacementRefs+1)
        {
            std::fprintf(stderr,"Costume merge lost owning TAG references\n");
            std::exit(1);
        }
        _track.Clear();
        return replacement->CactRef()==replacementRefs;
    }
    bool FTestEventEdit(int mode, FILE *snapshot)
    {
        ActorTrack created;
        if (!created.FCreate()) return false;
        RouteDistancePoint points[]={{{rZero,rZero,rZero},BR_SCALAR(10)},{{BR_SCALAR(10),rZero,rZero},rZero}};
        if (!created.Route()->FInsert(0,&points[0]) || !created.Route()->FInsert(1,&points[1])) return false;
        union Payload { ActorEvent::Add add; ActorEvent::Action action; ActorEvent::Costume costume;
            ActorEvent::Sound sound; BMAT34 matrix; RoutePoint move; int32_t scalar; } value;
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
                value.sound.celn=seed; value.sound.sty=2; value.sound.vlm=100+seed;
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
            return created.Events()->FInsert(created.Events()->IvMac(),bytes,&value,&event);
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
        if (mode==31) { value.sound.fQueue=tYes; created.Events()->Put(2,&value.sound); }
        if (mode==32 || mode==33) { value.sound.sty=3; created.Events()->Put(2,&value.sound); }
        if (mode==34) { value.costume.ibset=2; created.Events()->Put(2,&value.costume); }
        if (mode==35)
        {
            RoutePoint old={klwMax,0,0}, fresh={1,0,0};
            created.Events()->Put(1,&old); created.Events()->Put(2,&fresh);
        }
        _track.Swap(created); _motionSounds.Clear();
        _state=ActorFrameState(); _state.RewindRoute(_track);
        _state.onStage=true; _state.frame=4; _state.firstEventInFrame=1; _state.nextEvent=_track.Events()->IvMac()-1;
        _state.actionEvent=-1; _state.addEvent=0; _state.celCount=4;
        _state.subrouteTranslation={BR_SCALAR(7),BR_SCALAR(8),BR_SCALAR(9)};
        _state.routeTranslation={BR_SCALAR(42),BR_SCALAR(43),BR_SCALAR(44)};
        _arid=mode==33 ? 32768 : 23; _nfrmFirst=1; _nfrmLast=30; _fLifeDirty=false;
        if (oldKind==ActorEvent::aetSnd)
        {
            ThreeMMActorMotionSound match;
            _track.Events()->GetFixed(1,&match.event); _track.Events()->Get(1,&match.sound);
            if (!three_mm_actor_sounds_insert(_motionSounds.Core(),&match.event,&match.sound)) return false;
        }
        LegacyLong result=-99; bool ok=true;
        if (mode>=38 && mode<=40)
        {
            int32_t freeze=1; ActorEvent::Base event={ActorEvent::aetFreeze,4,{0,rZero,0}};
            _state.actionEvent=1;
            ok=_FInsertAev(1,sizeof(freeze),mode==39 ? pvNil : &freeze,&event,mode!=40);
            if (ok && mode==39) _track.Events()->Put(1,&freeze);
        }
        else if (mode>=41)
        {
            _RemoveAev(_state.nextEvent,false);
            if (mode==41) { RoutePoint move={BR_SCALAR(2),BR_SCALAR(3),BR_SCALAR(4)}; ok=_FAddDoAev(ActorEvent::aetMove,sizeof(move),&move); }
            else
            {
                int32_t scalar=1;
                if (mode==43) { _state.location.dwrOffset=BR_SCALAR(20); scalar=0; }
                if (mode==44) { _nfrmFirst=10; _nfrmLast=-5; }
                ok=_FAddDoAev(mode==43 ? ActorEvent::aetStep : ActorEvent::aetFreeze,sizeof(scalar),&scalar);
            }
        }
        else _MergeAev(mode==36 ? _state.nextEvent : 1,_state.nextEvent,mode==37 ? pvNil : &result);
        auto record=[&](const void *p,size_t n) { return !snapshot || std::fwrite(p,1,n,snapshot)==n; };
        LegacyLong state[]={mode,ok,result,_state.nextEvent,_state.firstEventInFrame,_state.actionEvent,_state.addEvent,
            _state.subrouteTranslation.dxr,_state.subrouteTranslation.dyr,_state.subrouteTranslation.dzr,
            _state.routeTranslation.dxr,_state.routeTranslation.dyr,_state.routeTranslation.dzr,_state.frozen,
            _nfrmFirst,_nfrmLast,_track.Events()->IvMac(),_motionSounds.IvMac()};
        bool saved=record(state,sizeof(state));
        for (long i=0; i<_track.Events()->IvMac(); ++i)
        {
            ActorEvent::Base event; _track.Events()->GetFixed(i,&event); saved=saved && record(&event,sizeof(event));
            if (event.aet==ActorEvent::aetSnd)
            {
                ActorEvent::Sound sound; _track.Events()->Get(i,&sound);
                LegacyLong fields[]={sound.celn,sound.sty,sound.vlm,sound.fQueue,sound.tag.sid,static_cast<LegacyLong>(sound.tag.cno)};
                saved=saved && record(fields,sizeof(fields));
            }
            else if (event.aet==ActorEvent::aetCost)
            {
                ActorEvent::Costume costume; _track.Events()->Get(i,&costume);
                LegacyLong fields[]={costume.ibset,costume.cmid,costume.fCmtl,costume.tag.sid,static_cast<LegacyLong>(costume.tag.cno)};
                saved=saved && record(fields,sizeof(fields));
            }
            else
            {
                LegacyLong bytes=_track.Events()->Cb(i); std::memset(&value,0,sizeof(value));
                _track.Events()->Get(i,&value); saved=saved && record(&value,bytes);
            }
        }
        return saved && ok==(mode!=43);
    }
    bool FTestSoundEdit(int mode, FILE *snapshot)
    {
        ActorTrack created;
        if (!created.FCreate()) return false;
        RouteDistancePoint point = {{rZero, rZero, rZero}, rZero};
        if (!created.Route()->FInsert(0, &point)) return false;
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
            ActorEvent::Sound sound = {};
            int32_t frozen = 1;
            sound.celn = cels[i]; sound.sty = styles[i]; sound.vlm = 100 + i;
            sound.fLoop = i & 1 ? tYes : tNo;
            sound.tag = {2,pvNil,kctgMsnd,static_cast<ChunkNumber>(30+i)};
            void *data = &sound;
            LegacyLong bytes = sizeof(sound);
            if (kinds[i] == ActorEvent::aetAdd) { data=&add; bytes=sizeof(add); }
            if (kinds[i] == ActorEvent::aetActn) { data=&action; bytes=sizeof(action); }
            if (kinds[i] == ActorEvent::aetFreeze) { data=&frozen; bytes=sizeof(frozen); }
            if (!created.Events()->FInsert(i,bytes,data,&event)) return false;
        }
        _track.Swap(created);
        _motionSounds.Clear();
        for (int i : {2,3,10})
        {
            ThreeMMActorMotionSound record;
            _track.Events()->GetFixed(i,&record.event);
            _track.Events()->Get(i,&record.sound);
            if (!three_mm_actor_sounds_insert(_motionSounds.Core(),&record.event,&record.sound)) return false;
        }
        _state.frame=4; _state.action=0; _state.cel=5; _state.celCount=4;
        _nfrmFirst=1; _nfrmLast=30; _fLifeDirty=false;
        _state.firstEventInFrame=6; _state.nextEvent=9; _state.actionEvent=1; _state.addEvent=0;
        PDynamicArray tags = pvNil;
        LegacyLong volume=12345;
        bool loop=false, ok=false;
        if (mode == 9) ((TrackTestTemplate *)_ptmpl)->FailAfter(2);
        if (mode == 10 || mode == 11 || mode == 12) ((TrackTestTemplate *)_ptmpl)->FailNextFetch();
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
        case 8: _state.frame=_pscen->Nfrm(); ok=FSoundInFrm(); break;
        case 13: _RemoveAev(3,false); ok=true; break;
        case 14: _RemoveAev(1,true); ok=true; break;
        }
        auto record = [&](const void *p, size_t n) { return !snapshot || std::fwrite(p,1,n,snapshot)==n; };
        LegacyLong values[] = {mode,ok,volume,loop,_state.frame,_state.action,_state.cel,_state.firstEventInFrame,
            _state.nextEvent,_state.actionEvent,_state.addEvent,_track.Events()->IvMac(),_motionSounds.IvMac(),
            tags ? tags->IvMac() : -1};
        bool saved=record(values,sizeof(values));
        for (long i=0; tags && i<tags->IvMac(); ++i)
        {
            TAG tag; tags->Get(i,&tag);
            LegacyUlong v[] = {static_cast<LegacyUlong>(tag.sid),tag.ctg,tag.cno};
            saved=saved && record(v,sizeof(v));
        }
        ReleasePpo(&tags);
        for (long i=0; i<_track.Events()->IvMac(); ++i)
        {
            ActorEvent::Base event; _track.Events()->GetFixed(i,&event);
            saved=saved && record(&event,sizeof(event));
            if (event.aet == ActorEvent::aetSnd)
            {
                ActorEvent::Sound sound; _track.Events()->Get(i,&sound);
                LegacyUlong v[] = {static_cast<LegacyUlong>(sound.celn),static_cast<LegacyUlong>(sound.sty),
                    static_cast<LegacyUlong>(sound.vlm),static_cast<LegacyUlong>(sound.fLoop),
                    static_cast<LegacyUlong>(sound.tag.sid),sound.tag.ctg,sound.tag.cno};
                saved=saved && record(v,sizeof(v));
            }
        }
        for (long i=0; i<_motionSounds.IvMac(); ++i)
        {
            ActorEvent::SoundMotionMatch sound; _motionSounds.Get(i,&sound);
            LegacyLong v[] = {sound.aevsnd.celn,sound.aevsnd.sty,sound.aevsnd.vlm};
            saved=saved && record(v,sizeof(v));
        }
        return saved && (ok == !(mode == 9 || mode == 10 || mode == 11 || mode == 12));
    }
    bool FInitPlayback(PScene scene, PChunkyResourceFile resource)
    {
        if (!FInit(scene, resource) || !((TrackTestTemplate *)_ptmpl)->FInitAction() ||
            !ActorFrameTest::FCreateTrack(_track, false))
            return false;
        _pbody = _ptmpl->PbodyCreate();
        if (_pbody == pvNil)
            return false;
        _motionSounds.Clear();
        _state = ActorFrameState();
        _state.RewindRoute(_track);
        _state.celCount = 4;
        _state.actionFlags = factnRotateY;
        _nfrmFirst = 1;
        _nfrmLast = 0; // Entrance lookup must refresh lifetime before final effects.
        _fLifeDirty = true;
        return true;
    }
    ActorFrameState State() const
    {
        return _state;
    }
    long LastFrame() const
    {
        return _nfrmLast;
    }
    bool FTestTimeline()
    {
        if (!ActorFrameTest::FCreateMetadataTrack(_track, true))
            return false;
        ActorFrameState before = _state;
        _nfrmFirst = -2;
        _fLifeDirty = true;
        auto tmpl = (TrackTestTemplate *)_ptmpl;
        tmpl->FailNextFetch();
        long reported = 88;
        if (_FComputeLifetime(&reported) || reported != 88 || _nfrmLast != -1 || !_fLifeDirty ||
            !ActorFrameTest::SameState(_state, before))
            return false;
        if (!_FComputeLifetime(&reported) || reported != 0 || _nfrmLast != 0 || _fLifeDirty ||
            !ActorFrameTest::SameState(_state, before))
            return false;
        _fModeRecord = true;
        _nfrmLast = 42;
        tmpl->FailNextFetch();
        if (!_FComputeLifetime(&reported) || reported != 42 || !_fLifeDirty)
            return false;
        _fModeRecord = false;
        reported = 88;
        // The failure is still pending: recording did not consult the resolver.
        if (_FComputeLifetime(&reported) || reported != 88 || !_fLifeDirty)
            return false;
        return _FComputeLifetime(&reported) && reported == 0 && !_fLifeDirty &&
               ActorFrameTest::SameState(_state, before);
    }
    bool FInit(PScene scene, PChunkyResourceFile resource)
    {
        auto tmpl = NewObj TrackTestTemplate();
        _ptmpl = tmpl;
        if (tmpl == pvNil || !tmpl->FInit() || !_FCreateGroups())
            return false;
        _tagTmpl.sid = ksidUseCrf;
        _tagTmpl.ctg = kctgTmpl;
        _tagTmpl.cno = 1;
        _tagTmpl.pcrf = resource;
        TagManager::DupTag(&_tagTmpl);
        _pscen = scene;
        RouteDistancePoint point = {{BR_SCALAR(3), rZero, rZero}, rZero};
        ActorEvent::Base event = {};
        event.aet = ActorEvent::aetAdd;
        ActorEvent::Add addition = {};
        if (!_track.Route()->FInsert(0, &point) || !_track.Events()->FInsert(0, sizeof(addition), &addition, &event))
            return false;
        event.aet = ActorEvent::aetSnd;
        ActorEvent::Sound sound = {};
        sound.tag = _tagTmpl;
        sound.tag.ctg = kctgMsnd;
        if (!_track.Events()->FInsert(1, sizeof(sound), &sound, &event))
            return false;
        TagManager::DupTag(&sound.tag);
        ThreeMMActorMotionSound match = {};
        if (!three_mm_actor_sounds_insert(_motionSounds.Core(), &match.event, &match.sound))
            return false;
        _SetStateRewound();
        _nfrmFirst = _state.frame = 1;
        _nfrmLast = 8;
        _arid = 23;
        _state.nextEvent = 1;
        _state.transform.rScaleStep = BR_SCALAR(2);
        return true;
    }
    bool FTestRestore(PActor snapshot)
    {
        PGeneralGroup changedEvents = _track.Events();
        PDynamicArray changedRoute = _track.Route();
        RouteDistancePoint point = {{BR_SCALAR(99), rZero, rZero}, rZero};
        _track.Route()->Put(0, &point);
        _track.Events()->Delete(0); // Add has no owned TAG.
        _state.frame = 7;
        _arid = 99;
        _state.transform.rScaleStep = BR_SCALAR(3);
        const long refs = CactRef();
        Restore(snapshot);
        _track.Route()->Get(0, &point);
        return CactRef() == refs && _track.Events() != changedEvents && _track.Route() != changedRoute &&
               _track.Events()->IvMac() == 2 && point.xyz.dxr == BR_SCALAR(3) && _state.frame == 1 && _arid == 23 &&
               _state.transform.rScaleStep == BR_SCALAR(2) && _state.nextEvent == 1 && _motionSounds.IvMac() == 1;
    }
    bool FTestRoute()
    {
        RouteDistancePoint start = {{BR_SCALAR(3), rZero, rZero}, BR_SCALAR(4)};
        RouteDistancePoint finish = {{BR_SCALAR(7), rZero, rZero}, rZero};
        _track.Route()->Put(0, &start);
        if (!_track.Route()->FInsert(1, &finish))
            return false;
        ActorEvent::Base event = {};
        event.aet = ActorEvent::aetStep;
        event.rtel.dwrOffset = BR_SCALAR(2);
        BRS step = rZero;
        if (!_track.Events()->FInsert(2, sizeof(step), &step, &event))
            return false;
        for (long mode = 0; mode < 3; ++mode)
        {
            _fModeRecord = mode == 1;
            _fRejoin = mode == 2;
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
        _dxyzFullRte = {BR_SCALAR(10), BR_SCALAR(20), BR_SCALAR(30)};
        _state.subrouteTranslation = {};
        _state.frame = 17;
        auto apply = [&](long type, void *payload, long bytes) {
            ActorEvent::Base event = {static_cast<int32_t>(type), -1, {0, BR_SCALAR(2), 3}};
            long index = _track.Events()->IvMac();
            if (!_track.Events()->FInsert(index, bytes, payload, &event) || !_FDoAevCore(index))
                return false;
            _track.Events()->GetFixed(index, &event);
            return event.nfrm == 17;
        };
        RoutePoint delta = {BR_SCALAR(2), BR_SCALAR(3), BR_SCALAR(4)};
        BRS step = rZero;
        int32_t frozen = -1;
        if (!apply(ActorEvent::aetMove, &delta, sizeof(delta)) || !apply(ActorEvent::aetStep, &step, sizeof(step)) ||
            !apply(ActorEvent::aetFreeze, &frozen, sizeof(frozen)))
            return false;
        if (_state.routeTranslation.dxr != BR_SCALAR(12) || _state.routeTranslation.dyr != BR_SCALAR(23) ||
            _state.position.dxr != BR_SCALAR(5) || _state.location.dnfrm != 3 || !_state.frozen)
            return false;
        PActor snapshot = pvNil;
        if (!FDup(&snapshot, false))
            return false;
        _state = ActorFrameState();
        Restore(snapshot);
        ReleasePpo(&snapshot);
        return _state.frame == 17 && _state.routeTranslation.dzr == BR_SCALAR(34) && _state.frozen &&
               _state.position.dxr == BR_SCALAR(5) && _state.location.dnfrm == 3;
    }
    bool FTestPose()
    {
        _state.InitTransform();
        _state.useCurrentRotation = false;
        _state.actionFlags = factnRotateY;
        _state.addEvent = 0;
        _state.location = {0, BR_SCALAR(2), 0};
        RoutePoint position = {BR_SCALAR(5), rZero, rZero};
        BMAT34 matrix;
        _MatrixRotUpdate(&position, &matrix);
        // Route yaw is 90 degrees; the template contributes another 90.
        if (_state.position.dxr != BR_SCALAR(5) || _state.transform.yaPath != static_cast<BRA>(0x4000) ||
            BrsAbs(BrsAdd(matrix.m[0][0], rOne)) > BR_SCALAR(0.002) || BrsAbs(matrix.m[0][2]) > BR_SCALAR(0.002))
            return false;
        _state.actionFlags = factnRotateX;
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
        // The third BASE allocation is the final native track table. Motion
        // storage now uses the C allocator (fault-injected in its standalone test).
        // Failure must release only the clone.
        vmutxMem.Enter();
        vdmglob.dmaglBase.cactDo = 2;
        vdmglob.dmaglBase.cactFail = 1;
        vmutxMem.Leave();
        bool copied = actor->FDup(&snapshot, false);
        vmutxMem.Enter();
        vdmglob.dmaglBase.cactFail = 0;
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
    ActorFrameState expected[14];
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
    if (const char *path=std::getenv("THREE_MM_ACTOR_SOUND_EDIT_SNAPSHOT"))
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
    if (const char *path=std::getenv("THREE_MM_ACTOR_EVENT_EDIT_SNAPSHOT"))
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
    if (const char *path=std::getenv("THREE_MM_ACTOR_FILL_EDIT_SNAPSHOT"))
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
    if (const char *path=std::getenv("THREE_MM_ACTOR_COSTUME_EDIT_SNAPSHOT"))
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
    if (const char *path=std::getenv("THREE_MM_ACTOR_ROUTE_EDIT_SNAPSHOT"))
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

class QueueTestSound : public MovieSoundMSND
{
  public:
    QueueTestSound(long style)
    {
        _sty = style;
        _vlm = 123;
        _fNoSound = tYes; // Exercise dispatch without an audio file/device.
        _fInvalid = false;
    }
};

class QueueTestClock : public Clock
{
  public:
    QueueTestClock() : Clock(khidMsqClock + 13)
    {
    }
    long AlarmCount() const
    {
        return _pglalad == pvNil ? 0 : _pglalad->IvMac();
    }
    ulong FirstAlarm() const
    {
        AlarmDescriptor alarm;
        _pglalad->Get(0, &alarm);
        return alarm.tim;
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
        if (!_queue.FInit())
            return false;
        _pclok = NewObj QueueTestClock();
        return _pclok != pvNil;
    }
    long Pending() const
    {
        return _queue.Count();
    }
    long Alarms() const
    {
        return ((QueueTestClock *)_pclok)->AlarmCount();
    }
    ulong FirstAlarm() const
    {
        return ((QueueTestClock *)_pclok)->FirstAlarm();
    }
    bool Take(SoundPlaybackRequest *request)
    {
        return _queue.FDequeue(request);
    }
};

static bool TestSoundQueueRuntime()
{
    QueueTestAdapter *queue = NewObj QueueTestAdapter();
    QueueTestSound *sound = NewObj QueueTestSound(stySfx);
    QueueTestAdapter *failedAlarm = pvNil;
    SoundPlaybackRequest request;
    ulong firstAlarm = 0;
    bool ok = false;
    if (queue == pvNil || sound == pvNil || !queue->FInit() || queue->DtimSnd() != kdtim2Msq)
        goto done;
    if (!queue->FEnqueue(sound, 37, true, false, 123, 5, true, 456) || sound->CactRef() != 2 || !queue->Take(&request))
        goto done;
    if (request.sound != sound || request.style != stySfx || request.objectId != 37 || !request.actor ||
        !request.loop || request.queue || request.volume != 123 || request.priority != 5 || request.startOffset != 456)
        goto done;
    ReleasePpo(&request.sound);
    if (sound->CactRef() != 1 || MovieSoundMSND::SqnActr(stySfx, 37) != 0x10020025)
        goto done;
    if (!queue->FEnqueue(sound, 37, false, false, 123, 5))
        goto done;
    queue->PlayMsq();
    if (queue->Pending() != 0 || sound->CactRef() != 1 || queue->Alarms() != 1)
        goto done;
    firstAlarm = queue->FirstAlarm();
    queue->SndOnLong();
    if (!queue->FEnqueue(sound, 37, false, false, 123, 5))
        goto done;
    queue->PlayMsq();
    if (queue->Pending() != 0 || sound->CactRef() != 1 || queue->Alarms() != 1 || queue->FirstAlarm() != firstAlarm)
        goto done; // Long mode adds no alarm and does not cancel the existing preview alarm.
    if (!queue->FEnqueue(sound, 37, false, false, 123, 5))
        goto done;
    queue->SndOff();
    queue->PlayMsq();
    if (queue->Pending() != 0 || sound->CactRef() != 1 || queue->Alarms() != 1)
        goto done;
#ifdef DEBUG
    failedAlarm = NewObj QueueTestAdapter();
    if (failedAlarm == pvNil || !failedAlarm->FInit() || !failedAlarm->FEnqueue(sound, 37, false, false, 123, 5))
        goto done;
    {
        vmutxMem.Enter();
        vdmglob.dmaglBase.cactDo = 0;
        vdmglob.dmaglBase.cactFail = 1;
        vmutxMem.Leave();
        failedAlarm->PlayMsq(); // Clock's first alarm-table allocation fails after the batch drains.
        vmutxMem.Enter();
        bool consumed = vdmglob.dmaglBase.cactFail == 0;
        vdmglob.dmaglBase.cactFail = 0;
        vmutxMem.Leave();
        vpers->Clear();
        if (!consumed || failedAlarm->Alarms() != 0 || failedAlarm->Pending() != 0 || sound->CactRef() != 1)
            goto done;
    }
#endif
    ok = true;
done:
    ReleasePpo(&request.sound);
    ReleasePpo(&failedAlarm);
    ReleasePpo(&queue);
    ReleasePpo(&sound);
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
    static const MoviePlaybackState &State(const Movie &movie)
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
    metadata.firstFrame = 1;
    metadata.maxFrame = klwMax;
    metadata.story = true;
    metadata.bounds.Set(10, 20, 180, 100);
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
    if (!saved.story || saved.firstFrame != 1 || exported.text.cb != 5)
        goto done;
    if (!exported.text.FReadRgb(actual, 5, 0) || std::memcmp(actual, text, 5) != 0)
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
     * StopAll -> _pclok->Stop) does vpcex->RemoveCmh(this, ...) so we
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
        ChildChunkIdentification scene;
        if (!stored.Resources()->Pcfl()->FGetKidChidCtg(kctgMvie, stored.movieNumber, 0, kctgScen, &scene))
            BAIL("saved movie has no scene");
        SceneData metadata;
        if (!metadata.FRead(stored.Resources()->Pcfl(), scene.cki.cno))
            BAIL("core scene read failed");
        metadata.transition = transFadeToBlack;
        if (!metadata.FWrite(stored.Resources()->Pcfl(), scene.cki.cno) || !stored.FSave(&fniMovie))
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
