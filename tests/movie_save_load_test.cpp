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

  protected:
    PActionDefinition _PactnFetch(long anid) override
    {
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
    bool FInitPlayback(PScene scene, PChunkyResourceFile resource)
    {
        if (!FInit(scene, resource) || !((TrackTestTemplate *)_ptmpl)->FInitAction() ||
            !ActorFrameTest::FCreateTrack(_track, false))
            return false;
        _pbody = _ptmpl->PbodyCreate();
        if (_pbody == pvNil)
            return false;
        _pglsmm->FSetIvMac(0);
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
        if (!_track.route->FInsert(0, &point) || !_track.events->FInsert(0, sizeof(addition), &addition, &event))
            return false;
        event.aet = ActorEvent::aetSnd;
        ActorEvent::Sound sound = {};
        sound.tag = _tagTmpl;
        sound.tag.ctg = kctgMsnd;
        if (!_track.events->FInsert(1, sizeof(sound), &sound, &event))
            return false;
        TagManager::DupTag(&sound.tag);
        ActorEvent::SoundMotionMatch match = {};
        if (!_pglsmm->FInsert(0, &match))
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
        PGeneralGroup changedEvents = _track.events;
        PDynamicArray changedRoute = _track.route;
        RouteDistancePoint point = {{BR_SCALAR(99), rZero, rZero}, rZero};
        _track.route->Put(0, &point);
        _track.events->Delete(0); // Add has no owned TAG.
        _state.frame = 7;
        _arid = 99;
        _state.transform.rScaleStep = BR_SCALAR(3);
        const long refs = CactRef();
        Restore(snapshot);
        _track.route->Get(0, &point);
        return CactRef() == refs && _track.events != changedEvents && _track.route != changedRoute &&
               _track.events->IvMac() == 2 && point.xyz.dxr == BR_SCALAR(3) && _state.frame == 1 && _arid == 23 &&
               _state.transform.rScaleStep == BR_SCALAR(2) && _state.nextEvent == 1 && _pglsmm->IvMac() == 1;
    }
    bool FTestRoute()
    {
        RouteDistancePoint start = {{BR_SCALAR(3), rZero, rZero}, BR_SCALAR(4)};
        RouteDistancePoint finish = {{BR_SCALAR(7), rZero, rZero}, rZero};
        _track.route->Put(0, &start);
        if (!_track.route->FInsert(1, &finish))
            return false;
        ActorEvent::Base event = {};
        event.aet = ActorEvent::aetStep;
        event.rtel.dwrOffset = BR_SCALAR(2);
        BRS step = rZero;
        if (!_track.events->FInsert(2, sizeof(step), &step, &event))
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
            long index = _track.events->IvMac();
            if (!_track.events->FInsert(index, bytes, payload, &event) || !_FDoAevCore(index))
                return false;
            _track.events->GetFixed(index, &event);
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
        // The fourth BASE allocation is motion-match storage, after Actor,
        // route and event allocation. Failure must release only the clone.
        vmutxMem.Enter();
        vdmglob.dmaglBase.cactDo = 3;
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
