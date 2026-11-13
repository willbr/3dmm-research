/* Copyright (c) Microsoft Corporation.
   Licensed under the MIT License. */

/*******************************************************************

 Actor class

 Primary Author : *****
 Review Status: Reviewed

 BASE -------> Actor

 Each actor has a unique route which is defined by one or more nodes,
 and which may be a concatenation of one or more subroutes.
 Each actor also has an event list describing events which happen
 at a specified time and point along the route.  A point may coincide
 with a node or lie between nodes.

 *******************************************************************/
#ifndef ACTOR_H
#define ACTOR_H

#include <cstdint>

using namespace BRender;

//
//	RoutePoint : A point in x,y,z along an actor's RouTE.
//
#include "actor_data.h"
#include "actor_track.h"
#include "actor_sound.h"
#include "three_mm/actor_sound_edit.h"
#include "three_mm/actor_action_edit.h"
#include "three_mm/actor_costume_edit.h"
#include "three_mm/actor_route_edit.h"
#include "three_mm/actor_subroute_edit.h"
#include "three_mm/actor_stage_edit.h"
#include "three_mm/actor_record.h"
#include "three_mm/actor_transform_edit.h"
#include "three_mm/actor_owner.h"
#include "three_mm/actor_transaction.h"
#include "three_mm/actor_copy.h"
#include "actor_state.h"
#include "actor_timeline.h"

const long kcrptGrow = 32;                                         // quantum growth for rpt
const long kctagSndGrow = 2;                                       // quantum growth for tagSnd
const long smmNil = -1;                                            // Not motion match sound
const BRS kzrDefault = BR_SCALAR(-25.0);                           // initial default z position
const BRS kdwrMax = BR_SCALAR(32767.0);                            // large BRS value
const ulong kdtsThreshRte = (kdtsSecond >> 2) + (kdtsSecond >> 1); // time threshhold before record in place
const BRS kdwrThreshRte = BR_SCALAR(2.0);                          // distance threshhold before entering record mode
const long kcaevInit = 10;
const BRS kdwrFast = BR_SCALAR(3.0);       // delta world coord change for fast mouse move
const BRS krOriWeightMin = BR_SCALAR(0.1); // orientation weighting for slow mouse move
const BRS krAngleMin = BR_SCALAR(0.1);     // Min angle impacting amount of forward movement on placement
const BRS krAngleMinRcp = BR_RCP(krAngleMin);
const BRS krAngleMax = BR_SCALAR(0.4);         // Max angle impacting amount of forward movement on placement
const BRS krScaleMinFactor = (BR_SCALAR(0.1)); // Min scaling between Brender update
const auto krScaleMaxFactor = BrsDiv(rOne, krScaleMinFactor); // Max scaling between Brender update
const auto krScaleMin = (BR_SCALAR(0.25));                    // Min scaling
const auto krScaleMax = (BR_SCALAR(10.0));                    // Max scaling
const auto krPullMin = krScaleMin;
const auto krPullMax = BrsDiv(rOne, krPullMin);

#define aridNil ivNil

// Angle valid flags
enum
{
    fbraRotateX = factnRotateX,
    fbraRotateY = factnRotateY,
    fbraRotateZ = factnRotateZ
};

// Normalize flags
enum
{
    fnormSize = 1,
    fnormRotate = 2
};

// Mouse Actor flags
enum
{
    fmafNil = 0,
    fmafFreeze = 0x01,      // freeze cels
    fmafGround = 0x02,      // respect ground level
    fmafOrient = 0x04,      // orient actor during move
    fmafEntireScene = 0x08, // position over entire scene, vs subroute only
    fmafEntireSubrte = 0x10 // position over entire subroute
};

//
// Default hilite colors
//
#define kiclrNormalHilite 108
#define kiclrTimeFreezeHilite 43

/***********************************************
   Actor Class
***********************************************/
typedef class Actor *PActor;
#define Actor_PAR BASE
#define kclsActor 'ACTR'
class Actor : public Actor_PAR
{
    RTCLASS_DEC
    ASSERT
    MARKMEM

  protected:
    // Components of an actor
    // Note: In addition to these components, any complete actor must
    // have either fLifeDirty set or _nfrmLast current.
    // Note: _tagTmpl cannot be derived from _ptmpl
    ActorTrack _track;    // Authored route/events and their TAG references
    Template *_ptmpl;     // Actor body & action list template
    Body *_pbody;     // Actor's body
    TAG _tagTmpl;     // Note: The sid cannot be queried at save time
    TAG _tagSnd;      // Sound (played on entrance)
    Scene *_pscen;     // Underlying scene
    RoutePoint _dxyzFullRte; // Origin of the route
    long _nfrmFirst;  // klwMax -or- First frame : Set	when event created
    long _arid;       // Unique id assigned to this actor.

    ActorFrameState _state; // Core evaluated values

    // Runtime caches and sound state
    bool _fLifeDirty : 1;    // Set if _nfrmLast requires recomputation
    bool _fPrerendered : 1;  // Set if actor is prerendered
    LegacyLong _nfrmLast;          // Latest known frame for this actor.
    ActorSoundList _motionSounds;             // Current action motion match sounds

    // Path Recording State Information
    RouteLocation _rtelInsert;        // Joining information
    ulong _tsInsert;         // Starting time of route recording
    bool _fModeRecord : 1;   // Record a route mode
    bool _fRejoin : 1;       // Rerecord is extending a subpath from the end
    bool _fPathInserted : 1; // More path inserted
    bool _fTimeFrozen : 1;   // Is the actor frozen wrt time?
    long _dnfrmGap;          // Frames between subroutes
    RoutePoint _dxyzRaw;            // Raw mouse movement from previous frame

    //
    //	Protected functions
    //
    Actor(void);
    bool _FInit(TAG *ptmplTag);         // Constructor allocation & file I/O
    void _InitXfrmRot(BMAT34 *pbmat34); // Initialize rotation only
    void _InitXfrm(void);               // Initialize rotation & scaling
    bool _FCreateGroups(void);
    void _InitState(void);
    void _GetNewOrigin(BRS *pxr, BRS *pyr, BRS *pzr);
    void _SetStateRewound(void);

    bool _FApplyQuickSeekEffects();
    bool _FDoFrm(bool fPositionBody, bool *pfPositionDirty, bool *pfSoundInFrame = pvNil);
    bool _FGetStatic(long anid, bool *pfStatic);
    bool _FIsDoneAevSub(long iaev, RouteLocation rtel);
    bool _FIsAddNow(long iaev);
    ActorStepSource _StepSource() const;
    bool _FGetDwrPlay(BRS *pdwr);   // Step size if playing
    bool _FGetDwrRecord(BRS *pdwr); // Step size if recording
    bool _FDoAevCur(void);
    bool _FDoAevCore(long iaev);
    bool _FDoAetVar(long aet, void *pvVar, long cbVar);

    ThreeMMActorSoundServices _SoundServices();
    ThreeMMActorEditServices _EventEditServices();
    ThreeMMActorFillServices _FillEditServices();
    ThreeMMActorCostumeEditServices _CostumeEditServices();
    ThreeMMActorRouteEditServices _RouteEditServices();
    ThreeMMActorActionEditServices _ActionEditServices();
    ThreeMMActorSubrouteEditServices _SubrouteEditServices();
    ThreeMMActorStageEditServices _StageEditServices();
    ThreeMMActorRecording _RecordingState() const;
    void _PublishRecordingState(const ThreeMMActorRecording &);
    ThreeMMActorRecordServices _RecordServices();
    ThreeMMActorTransformEditServices _TransformEditServices();
    ThreeMMActorRuntime _Runtime() const;
    void _PublishRuntime(const ThreeMMActorRuntime &);
    static ThreeMMActorOwnerServices _OwnerServices();
    struct EditContext;
    static ThreeMMActorTransactionServices _TransactionServices(EditContext *);
    static ThreeMMActorDeleteServices _DeleteServices(EditContext *);
    ThreeMMActorCopyServices _CopyServices();
    ThreeMMActorSoundEditServices _SoundEditServices();
    bool _FEnqueueSnd(long iaev);
    bool _FEnqueueSmmInMsq(void);
    bool _FRemoveAevMm(long anid);
    bool _FAddAevDefMm(long anid);

    bool _FAddDoAev(long aetNew, long kcbNew, void *pvVar);
    void _MergeAev(long iaevFirst, long iaevLast, long *piaevNew = pvNil);
    bool _FFreeze(void);   // insert freeze event
    bool _FUnfreeze(void); // insert unfreeze event
    void _Hide(void);
    bool _FInsertGgRpt(long irpt, RouteDistancePoint *prpt, BRS dwrPrior = rZero);
    bool _FAddAevFromPrev(long iaevLim, ulong grfaet);
    bool _FAddAevFromLater(void);
    bool _FFindNextAevAet(long aet, long iaevCur, long *piaevAdd);
    bool _FFindPrevAevAet(long aet, long iaevCur, long *piaevAdd);
    void _FindAevLastSub(long iaevAdd, long iaevLim, long *piaevLast);
    void _DeleteFwdCore(bool fDeleteAll, bool *pfAlive = pvNil, long iaevCur = ivNil);
    bool _FDeleteEntireSubrte(void);
    void _DelAddFrame(long iaevAdd, long iaevLim);

    void _UpdateXyzRte(void);
    bool _FInsertAev(long iaev, long cbNew, void *pvVar, void *paev, bool fUpdateState = fTrue);
    void _RemoveAev(long iaev, bool fUpdateState = fTrue);
    void _PrepXfrmFill(long aet, void *pvVar, long cbVar, long iaevMin, long iaevCmp = ivNil, ulong grfaet = ActorEvent::faetNil);
    void _PrepActnFill(long iaevMin, long anidPrev, long anidNew, ulong grfaet);
    void _PrepCostFill(long iaevMin, ActorEvent::Costume *paevcost);
    void _AdjustAevForRteIns(long irptAdjust, long iaevMin);
    void _AdjustAevForRteDel(long irptAdjust, long iaevMin);
    bool _FInsertStop(void);
    void _ApplyRotFromVec(RoutePoint *pxyz, BMAT34 *pbmat34, BRA *pxa = pvNil, BRA *pya = pvNil, BRA *pza = pvNil,
                          LegacyUlong *grfbra = pvNil);
    void _SaveCurPathOrien(void);
    void _LoadAddOrien(ActorEvent::Add *paevadd, bool fNoReset = fFalse);
    BRA _BraAvgAngle(BRA a1, BRA a2, BRS rw);

    void _AdvanceRtel(BRS dwrStep, RouteLocation *prtel, long iaevCur, long nfrmCur, bool *pfEndRoute);
    void _GetXyzFromRtel(RouteLocation *prtel, PRoutePoint pxyz);
    void _GetXyzOnLine(PRoutePoint pxyzFirst, PRoutePoint pxyzSecond, BRS dwrOffset, PRoutePoint pxyz);
    void _PositionBody(PRoutePoint pxyz);
    void _MatrixRotUpdate(RoutePoint *pxyz, BMAT34 *pbmat34);
    void _TruncateSubRte(long irptDelLim);
    bool _FComputeLifetime(long *pnfrmLast = pvNil);
    bool _FIsStalled(long iaevFirst, RouteLocation *prtel, long *piaevLast = pvNil);

    void _RestoreFromUndo(PActor pactrRestore);
    void _CopyRuntimeStateFrom(const Actor &source);

    bool _FWriteTmpl(PChunkyFile pcfl, ChunkNumber cno);
    bool _FReadActor(PChunkyFile pcfl, ChunkNumber cno);
    bool _FReadRoute(PChunkyFile pcfl, ChunkNumber cno);
    bool _FReadEvents(PChunkyFile pcfl, ChunkNumber cno);
    bool _FOpenTags(PChunkyResourceFile pcrf);
    static bool _FIsIaevTag(PGeneralGroup pggaev, long iaev, PTAG *pptag, ActorEvent::PBase *pqaev = pvNil);
    void _CloseTags(void);

  public:
#if defined(DEBUG) || defined(STUDIO_TEST_TOOLS)
    const ActorFrameState &EvaluatedFrameState() const { return _state; }
#endif
    ~Actor(void);
    static PActor PactrNew(TAG *ptagTmpl);
    void SetPscen(Scene *pscen);
    void SetArid(long arid)
    {
        AssertBaseThis(0);
        _arid = arid;
    }
    void SetLifeDirty(void)
    {
        AssertBaseThis(0);
        _fLifeDirty = fTrue;
    }
    PScene Pscen(void)
    {
        AssertThis(0);
        return _pscen;
    }

    // ActrSave Routines
    static PActor PactrRead(PChunkyResourceFile pcrf, ChunkNumber cno);    // Construct from a document
    bool FWrite(PChunkyFile pcfl, ChunkNumber cno, ChunkNumber cnoScene); // Write to a document
    static PDynamicArray PgltagFetch(PChunkyFile pcfl, ChunkNumber cno, bool *pfError);
    static bool FAdjustAridOnFile(PChunkyFile pcfl, ChunkNumber cno, long darid);

    // Visibility
    void Hilite(void);
    void Unhilite(void)
    {
        AssertBaseThis(0);
        _pbody->Unhilite();
    }
    void Hide(void)
    {
        AssertBaseThis(0);
        _pbody->Hide();
    }
    void Show(void)
    {
        AssertBaseThis(0);
        _pbody->Show();
    }
    bool FIsInView(void)
    {
        AssertBaseThis(0);
        return _pbody->FIsInView();
    }
    void GetCenter(LegacyLong *pxp, LegacyLong *pyp)
    {
        AssertBaseThis(0);
        _pbody->GetCenter(pxp, pyp);
    }
    void GetRcBounds(RC *prc)
    {
        AssertBaseThis(0);
        _pbody->GetRcBounds(prc);
    }
    void SetPrerendered(bool fPrerendered)
    {
        AssertBaseThis(0);
        _fPrerendered = fPrerendered;
    }
    bool FPrerendered(void)
    {
        AssertBaseThis(0);
        return _fPrerendered;
    }

    // Actor Information
    long Arid(void)
    {
        AssertBaseThis(0);
        return (_arid);
    }
    bool FOnStage(void)
    {
        AssertBaseThis(0);
        return (_state.onStage);
    }
    bool FIsMyBody(Body *pbody)
    {
        AssertBaseThis(0);
        return pbody == _pbody;
    }
    bool FIsMyTmpl(Template *ptmpl)
    {
        AssertBaseThis(0);
        return _ptmpl == ptmpl;
    }
    bool FIsModeRecord(void)
    {
        AssertBaseThis(0);
        return FPure(_fModeRecord);
    }
    bool FIsRecordValid(BRS dxr, BRS dyr, BRS dzr, ulong tsCurrent);
    PTemplate Ptmpl(void)
    {
        AssertBaseThis(0);
        return _ptmpl;
    }
    PBody Pbody(void)
    {
        AssertBaseThis(0);
        return _pbody;
    }
    long AnidCur(void)
    {
        AssertBaseThis(0);
        return _state.action;
    }
    long CelnCur(void)
    {
        AssertBaseThis(0);
        return _state.cel;
    }
    bool FFrozen(void)
    {
        AssertBaseThis(0);
        return _state.frozen;
    }
    bool FGetLifetime(long *pnfrmFirst, long *pnfrmLast); // allows nil ptrs
    bool FPtIn(long xp, long yp, long *pibset);
    void GetTagTmpl(PTAG ptag)
    {
        AssertBaseThis(0);
        *ptag = _tagTmpl;
    }
    void GetName(PString pstn);
    bool FChangeTagTmpl(PTAG ptagTmplNew);
    bool FTimeFrozen(void)
    {
        AssertBaseThis(0);
        return _fTimeFrozen;
    }
    void SetTimeFreeze(bool fTimeFrozen)
    {
        AssertBaseThis(0);
        _fTimeFrozen = fTimeFrozen;
    }
    bool FIsPropBrws(void)
    {
        AssertThis(0);
        return FPure(_ptmpl->FIsProp() || _ptmpl->FIsTdt());
    }
    bool FIsTdt(void)
    {
        AssertThis(0);
        return FPure(_ptmpl->FIsTdt());
    }
    bool FMustRender(long nfrmRenderLast);
    void GetXyzWorld(BRS *pxr, BRS *pyr, BRS *pzr);

    // Animation
    bool FGotoFrame(long nfrm, bool *pfSoundInFrame = pvNil); // Prepare for display at frame nfrm
    bool FReplayFrame(long grfscen);                          // Replay a frame.

    // Event Editing
    bool FAddOnStageCore(void);
    bool FSetActionCore(long anid, long celn, bool fFreeze);
    bool FRemFromStageCore(void);
    bool FSetCostumeCore(long ibsetClicked, TAG *ptag, long cmid, tribool fCustom);
    bool FSetStep(BRS dwrStep);
    bool FRotate(BRA xa, BRA ya, BRA za, bool fFromHereFwd);
    // Like FRotate but treats (xa,ya,za) as a WORLD-axis rotation rather than
    // the actor's local-frame axis. Used for multi-actor rigid group rotation
    // so each selected actor tilts coherently around the same world axis
    // regardless of its rest orientation. Conjugates through the template's
    // rest orientation (TS): bmat34 := (TS^-1 * R * TS) * bmat34.
    bool FRotateAroundWorld(BRA xa, BRA ya, BRA za, bool fFromHereFwd);
    bool FNormalizeCore(ulong grfnorm);
    void SetAddOrient(BRA xa, BRA ya, BRA za, ulong grfbra, RoutePoint *pdxyz = pvNil);
    bool FScale(BRS rScaleStep);
    bool FPull(BRS rScaleX, BRS rScaleY, BRS rScaleZ);
    void DeleteFwdCore(bool fDeleteAll, bool *pfAlive = pvNil, long iaevCur = ivNil);
    void DeleteBackCore(bool *pfAlive = pvNil);
    bool FSoonerLater(long dnfrm);

    // ActrEdit Routines
    bool FDup(PActor *ppactr, bool fReset = fFalse); // Duplicate everything
    void Restore(PActor pactr);

    bool FCreateUndo(PActor pactr, bool fSndUndo = fFalse, PString pstn = pvNil); // Create undo object
    void Reset(void);
    bool FAddOnStage(void); // add actor to the stage, w/Undo
    bool FSetAction(long anid, long celn, bool fFreeze, PActor *ppactrDup = pvNil);
    bool FSetCostume(long ibset, TAG *ptag, long cmid, tribool fCustom);
    bool FRemFromStage(void);                                 // add event: rem actor from stage, w/Undo
    bool FCopy(PActor *ppactr, bool fEntireScene = fFalse);    // Duplicate actor from this frame on
    bool FCopyRte(PActor *ppactr, bool fEntireScene = fFalse); // Duplicate path from this frame on
    bool FPasteRte(PActor pactr);                              // Paste from clipboard from this frame on
    bool FNormalize(ulong grfnorm);
    bool FPaste(long nfrm, Scene *pscen);
    bool FDelete(bool *pfAlive, bool fDeleteAll);

    // ActrSnd Routines
    bool FSetSnd(PTAG ptag, tribool fLoop, tribool fQueue, tribool fActnCel, long vlm, long sty);
    bool FSetSndCore(PTAG ptag, tribool fLoop, tribool fQueue, tribool fActnCel, long vlm, long sty);
    bool FSetVlmSnd(long sty, bool fMotionMatch, long vlm); // Set the volume of a sound
    bool FQuerySnd(long sty, bool fMotionMatch, PDynamicArray *pglTagSnd, long *pvlm, bool *pfLoop);
    bool FDeleteSndCore(long sty, bool fMotionMatch);
    bool FSoundInFrm(void);
    bool FResolveAllSndTags(ChunkNumber cnoScen);

    // Route Definition
    void SetTsInsert(ulong tsCurrent)
    {
        AssertBaseThis(0);
        _tsInsert = tsCurrent;
    }
    bool FBeginRecord(ulong tsCurrent, bool fReplace, PActor pactrRestore);
    bool FRecordMove(BRS dxr, BRS dyr, BRS dzr, ulong grfmaf, ulong tsCurrent, bool *pfLonger, bool *pfStep,
                     PActor pactrRestore);
    bool FEndRecord(bool fReplace, PActor pactrRestore);
    bool FTweakRoute(BRS dxr, BRS dyr, BRS dzr, ulong grfmaf = fmafNil);
    bool FMoveRoute(BRS dxr, BRS dyr, BRS dzr, bool *pfMoved = pvNil, ulong grfmaf = fmafNil);
};

//
// Actor document for clipping
//
typedef class ActorClipboard *PActorClipboard;
#define ActorClipboard_PAR DocumentBase
#define kclsActorClipboard 'ACLP'
class ActorClipboard : public ActorClipboard_PAR
{
    RTCLASS_DEC
    MARKMEM
    ASSERT

  protected:
    PActor _pactr;
    bool _fRteOnly;
    String _stnName;

    ActorClipboard(void)
    {
    }

  public:
    //
    // Constructors and destructors
    //
    static PActorClipboard PaclpNew(PActor pactr, bool fRteOnly, bool fEndScene = fFalse);
    ~ActorClipboard(void);

    //
    // Pasting function
    //
    bool FPaste(PMovie pmvie);

    bool FRouteOnly(void)
    {
        return _fRteOnly;
    }
};

#endif //! ACTOR_H
