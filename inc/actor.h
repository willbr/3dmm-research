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
#include "mm/actor_sound_edit.h"
#include "mm/actor_action_edit.h"
#include "mm/actor_costume_edit.h"
#include "mm/actor_route_edit.h"
#include "mm/actor_subroute_edit.h"
#include "mm/actor_stage_edit.h"
#include "mm/actor_record.h"
#include "mm/actor_transform_edit.h"
#include "mm/actor_owner.h"
#include "mm/actor_transaction.h"
#include "mm/actor_copy.h"
#include "mm/actor_paste.h"
#include "mm/actor_route_paste.h"
#include "mm/actor_clipboard.h"
#include "mm/actor_create.h"

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
    friend class ActorClipboard;
    friend class Movie;
    friend class Scene;
    friend class MovieView;

    RTCLASS_DEC
    ASSERT
    MARKMEM

  protected:
    MMActorOwner _owner;

    PTemplate _Template() const { return static_cast<PTemplate>(_owner.runtime.template_object); }
    PBody _Body() const { return static_cast<PBody>(_owner.runtime.body); }
    PScene _Scene() const { return static_cast<PScene>(_owner.runtime.scene); }

    Actor(void);
    bool _FInit(TAG *ptmplTag);         // Constructor allocation & file I/O
    void _InitXfrmRot(BMAT34 *pbmat34); // Initialize rotation only
    void _InitXfrm(void);               // Initialize rotation & scaling
    bool _FCreateGroups(void);
    void _InitState(void);
    void _GetNewOrigin(BRS *pxr, BRS *pyr, BRS *pzr);
    void _SetStateRewound(void);

    bool _FGetStatic(long anid, bool *pfStatic);
    bool _FIsDoneAevSub(long iaev, RouteLocation rtel);
    MMActorTimelineServices _TimelineServices();
    MMActorPlaybackEffects _PlaybackEffects();
    bool _FGetDwrRecord(BRS *pdwr); // Step size if recording
    bool _FDoAevCur(void);
    bool _FDoAevCore(long iaev);
    bool _FDoAetVar(long aet, void *pvVar, long cbVar);

    MMActorSoundServices _SoundServices();
    MMActorEditServices _EventEditServices();
    MMActorFillServices _FillEditServices();
    MMActorCostumeEditServices _CostumeEditServices();
    MMActorRouteEditServices _RouteEditServices();
    MMActorActionEditServices _ActionEditServices();
    MMActorSubrouteEditServices _SubrouteEditServices();
    MMActorStageEditServices _StageEditServices();
    static MMActorStageEditServices _BindStageEdit(void *);
    MMActorRecordServices _RecordServices(const MMActorOwnerServices *owners);
    MMActorTransformEditServices _TransformEditServices();
    static MMActorOwnerServices _OwnerServices();
    static MMActorCreateServices _CreateServices();
    static void *_FetchTemplate(void *, const MMTag *);
    MMActorUndoCreateServices _UndoCreateServices();
    MMActorTransactionServices _TransactionServices();
    MMActorDeleteServices _DeleteServices();
    MMActorCopyServices _CopyServices();
    MMActorPasteServices _PasteServices();
    MMActorRoutePasteServices _RoutePasteServices();
    MMActorSoundEditServices _SoundEditServices();
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
    void _SaveCurPathOrien(void);
    void _LoadAddOrien(ActorEvent::Add *paevadd, bool fNoReset = fFalse);
    BRA _BraAvgAngle(BRA a1, BRA a2, BRS rw);

    void _TruncateSubRte(long irptDelLim);


  public:
#if defined(DEBUG) || defined(STUDIO_TEST_TOOLS)
    const MMActorState &EvaluatedFrameState() const { return _owner.runtime.state; }
#endif
    ~Actor(void);
    static PActor PactrNew(TAG *ptagTmpl);
    void SetPscen(Scene *pscen);
    void SetArid(long arid)
    {
        AssertBaseThis(0);
        _owner.runtime.actor = arid;
    }
    void SetLifeDirty(void)
    {
        AssertBaseThis(0);
        _owner.runtime.life_dirty = fTrue;
    }
    PScene Pscen(void)
    {
        AssertThis(0);
        return _Scene();
    }

    // ActrSave Routines
    static PActor PactrRead(MMResourceCache *pcrf, ChunkNumber cno);    // Construct from a document
    bool FWrite(const MMArchiveWriter &, const struct MMMovieEmbeddedArchive &, ChunkNumber cno, ChunkNumber cnoScene);

    // Visibility
    void Hilite(void);
    void Unhilite(void)
    {
        AssertBaseThis(0);
        EngineBody::Unhilite(_Body());
    }
    void Hide(void)
    {
        AssertBaseThis(0);
        EngineBody::Hide(_Body());
    }
    void Show(void)
    {
        AssertBaseThis(0);
        EngineBody::Show(_Body());
    }
    bool FIsInView(void)
    {
        AssertBaseThis(0);
        return EngineBody::IsInView(_Body());
    }
    void GetCenter(LegacyLong *pxp, LegacyLong *pyp)
    {
        AssertBaseThis(0);
        EngineBody::GetCenter(_Body(), pxp, pyp);
    }
    void GetRcBounds(RC *prc)
    {
        AssertBaseThis(0);
        EngineBody::GetRcBounds(_Body(), prc);
    }
    void SetPrerendered(bool fPrerendered)
    {
        AssertBaseThis(0);
        _owner.runtime.prerendered = fPrerendered;
    }
    bool FPrerendered(void)
    {
        AssertBaseThis(0);
        return _owner.runtime.prerendered;
    }

    // Actor Information
    long Arid(void)
    {
        AssertBaseThis(0);
        return (_owner.runtime.actor);
    }
    bool FOnStage(void)
    {
        AssertBaseThis(0);
        return (_owner.runtime.state.onStage);
    }
    bool FIsMyBody(PBody pbody)
    {
        AssertBaseThis(0);
        return pbody == _Body();
    }
    bool FIsMyTmpl(PTemplate ptmpl)
    {
        AssertBaseThis(0);
        return _Template() == ptmpl;
    }
    bool FIsModeRecord(void)
    {
        AssertBaseThis(0);
        return FPure(_owner.runtime.recording.recording);
    }
    PTemplate Ptmpl(void)
    {
        AssertBaseThis(0);
        return _Template();
    }
    PBody Pbody(void)
    {
        AssertBaseThis(0);
        return _Body();
    }
    long AnidCur(void)
    {
        AssertBaseThis(0);
        return _owner.runtime.state.action;
    }
    long CelnCur(void)
    {
        AssertBaseThis(0);
        return _owner.runtime.state.cel;
    }
    bool FFrozen(void)
    {
        AssertBaseThis(0);
        return _owner.runtime.state.frozen;
    }
    bool FGetLifetime(long *pnfrmFirst, long *pnfrmLast); // allows nil ptrs
    bool FPtIn(long xp, long yp, long *pibset);
    void GetTagTmpl(PTAG ptag)
    {
        AssertBaseThis(0);
        TagFromCore(ptag, _owner.runtime.template_tag);
    }
    void GetName(PString pstn);
    bool FChangeTagTmpl(const MMTag &tag);
    bool FTimeFrozen(void)
    {
        AssertBaseThis(0);
        return _owner.runtime.time_frozen;
    }
    bool FIsPropBrws(void)
    {
        AssertThis(0);
        return FPure(EngineTemplate::FIsProp(_Template()) || EngineTemplate::FIsTdt(_Template()));
    }
    bool FIsTdt(void)
    {
        AssertThis(0);
        return FPure(EngineTemplate::FIsTdt(_Template()));
    }
    bool FMustRender(long nfrmRenderLast);
    void GetXyzWorld(BRS *pxr, BRS *pyr, BRS *pzr);

    // Animation
    bool FGotoFrame(long nfrm, bool *pfSoundInFrame = pvNil); // Prepare for display at frame nfrm
    bool FReplayFrame(long grfscen);                          // Replay a frame.

    // Event Editing
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
    bool FSetAction(long anid, long celn, bool fFreeze, PActor *ppactrDup = pvNil);
    bool FSetCostume(long ibset, TAG *ptag, long cmid, tribool fCustom);
    bool FCopy(PActor *ppactr, bool fEntireScene = fFalse);    // Duplicate actor from this frame on
    bool FCopyRte(PActor *ppactr, bool fEntireScene = fFalse); // Duplicate path from this frame on
    bool FNormalize(ulong grfnorm);
    bool FDelete(bool *pfAlive, bool fDeleteAll);

    // ActrSnd Routines
    bool FSetSnd(PTAG ptag, tribool fLoop, tribool fQueue, tribool fActnCel, long vlm, long sty);
    bool FSetVlmSnd(long sty, bool fMotionMatch, long vlm); // Set the volume of a sound
    bool FQuerySnd(long sty, bool fMotionMatch, PDynamicArray *pglTagSnd, long *pvlm, bool *pfLoop);
    bool FDeleteSndCore(long sty, bool fMotionMatch);
    bool FSoundInFrm(void);
    bool FResolveAllSndTags(ChunkNumber cnoScen);

    // Route Definition
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
    MMActorClipboard _clipboard;
    static MMActorClipboardServices _Services();

    ActorClipboard(void)
    {
        static_assert((sizeof(achar) == 1 || sizeof(achar) == 2) && kcchMaxStn == MM_STRING_MAX,
                      "Clipboard name ABI");
        mm_actor_clipboard_init(&_clipboard, sizeof(achar));
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
        return _clipboard.route_only!=0;
    }
};

#endif //! ACTOR_H
