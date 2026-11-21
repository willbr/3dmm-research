/* Copyright (c) Microsoft Corporation.
   Licensed under the MIT License. */

/***************************************************************************

    THIS IS A CODE REVIEWED FILE

    Basic scene classes:
        Scene (Scene)

            BASE ---> Scene

        Scene Actor Undo Object (SceneActorUndo)

            BASE ---> UndoBase ---> MovieUndo ---> SceneActorUndo

***************************************************************************/

#ifndef SCEN_H
#define SCEN_H
#include "mm/scene_owner.h"
#include "mm/scene_runtime.h"
#include "mm/scene_events.h"
#include "mm/scene_sound_edit.h"
#include "mm/scene_load.h"
#include "mm/scene_store.h"
#include "mm/scene_edit.h"
#include "mm/scene_content.h"
#include "mm/scene_selection.h"
#include "mm/scene_chop.h"
#include "mm/scene_control.h"
#include "mm/scene_thumbnail.h"
#include "mm/actor_placement.h"

//
// Undo object for actor operations
//
typedef class SceneActorUndo *PSceneActorUndo;

#define SceneActorUndo_PAR MovieUndo

// Undo types
enum
{
    utAdd = 0x1,
    utDel,
    utRep,
};

#define kclsSceneActorUndo 'SUNA'
class SceneActorUndo : public SceneActorUndo_PAR
{
    RTCLASS_DEC
    MARKMEM
    ASSERT

  protected:
    MMSceneActorUndo _content = {};
    SceneActorUndo(void)
    {
    }

  public:
    static PSceneActorUndo PsunaNew(void);
    ~SceneActorUndo(void);

    void SetActr(PActor pactr)
    {
        _content.actor = pactr;
    }
    void SetType(long ut)
    {
        _content.operation = ut;
    }

    MMSceneActorUndo *CoreState()
    {
        return &_content;
    }
    virtual bool FDo(PDocumentBase pdocb);
    virtual bool FUndo(PDocumentBase pdocb);
};

//
// Different reasons for pausing in a scene
//

//
// Functionality that can be turned off and on.  If the bit
// is set, it is disabled.
//
enum
{
    fscenSounds = 0x1,
    fscenPauses = 0x2,
    fscenTboxes = 0x4,
    fscenActrs = 0x8,
    fscenPosition = 0x10,
    fscenAction = 0x20,
    fscenCams = 0x40,
    fscenAll = 0xFFFF
};

typedef MMSceneSound *PSceneSoundEvent;

typedef class Scene *PScene;
struct MMActorGroupTransformServices;

//
// Notes:
//
//	This assumes that struct SND contains at least,
//		- Everything necessary to play the sound.
//
//	This assumes that struct TextBox contains at least,
//		- Everything necessary to display the text.
//		- Enumerating through text boxes in a scene is not necessary.
//

#define Scene_PAR BASE
#define kclsScene 'SCEN'
class Scene : public Scene_PAR
{
    friend class Movie;
    friend class MovieSceneUndo;
    friend class SceneActorUndo;
    friend class MovieView;
    friend class SceneUndoText;
    friend class SceneUndoTitle;
    friend class SceneUndoTransition;
    friend struct SceneEditUndoHost;
    RTCLASS_DEC
    MARKMEM
    ASSERT

  protected:
    typedef struct SceneEvent *PSEV;
    MMSceneOwner _owner;
    static MMSceneOwnerServices _OwnerServices();

    MMSceneRuntimeServices _RuntimeServices();
    MMSceneEventServices _EventServices();
    MMSceneSoundEditServices _SoundEditServices();
    MMSceneEditServices _EditServices();
    MMSceneContentServices _ContentServices();
    MMSceneSelectionServices _SelectionServices();
    static MMActorGroupTransformServices _GroupTransformServices();
    MMActorPlacementEditServices _PlacementServices();
    MMSceneChopServices _ChopServices();
    MMScenePropertyServices _PropertyServices();
    MMScenePlaybackServices _PlaybackServices();
    MMSceneThumbnailServices _ThumbnailServices(void *graphics);

    //
    // Global information
    //
    MMSceneTrack *_FrameTrack() { return &_owner.tracks[MM_SCENE_FRAMES]; }
    MMSceneTrack *_StartTrack() { return &_owner.tracks[MM_SCENE_STARTS]; }

  protected:
    Scene(PMovie pmvie);
    ~Scene(void);

    //
    // Event stuff
    //
    bool _FAddSev(PSEV psev, long cbVar, void *pvVar);     // Adds scene event to the current frame.
    void _MoveBackFirstFrame(long nfrm);

    //
    // Dirtying stuff
    //
    void _MarkMovieDirty(void);
    void _DoPrerenderingWork(bool fStartNow); // Does any prerendering for _owner.data.currentFrame
    void _EndPrerendering(void);              // Stops prerendering

    //
    // Make actors go to a specific frame
    //
    bool _FForceActorsToFrm(long nfrm, bool *pfSoundInFrame = pvNil);
    bool _FForceTboxesToFrm(long nfrm);

    //
    // Thumbnail routines
    //
    void _UpdateThumbnail(void);

  public:
    //
    // Create and destroy
    //
    static Scene *PscenNew(PMovie pmvie);                      // Returns pvNil if it fails.
    static Scene *PscenRead(PMovie pmvie, PChunkyResourceFile pcrf, ChunkNumber cno,
                           const MMMovieSession *movie = nullptr); // Returns pvNil if it fails.
    bool FWrite(PChunkyResourceFile pcrf, ChunkNumber *pcno);                       // Returns fFalse if it fails, else the cno written.
    bool FWrite(const struct MMMovieArchiveEditor &, const MMArchiveWriter &,
                const struct MMMovieEmbeddedArchive &, ChunkNumber *pcno);
    static MMSceneSnapshotServices SnapshotServices();
    static void Close(PScene *ppscen);                        // Public destructor
    void RemActrsFromRollCall(bool fDelIfOnlyRef = fFalse);  // Removes actors from movie roll call.
    bool FAddActrsToRollCall(void);                          // Adds actors from movie roll call.

    //
    // Tag collection
    //

    //
    // Frame functions
    //
    bool FPlayStartEvents(bool fActorsOnly = fFalse); // Play all one-time starting scene events.
    void InvalFrmRange(void);                         // Mark the frame count dirty
    bool FGotoFrm(long nfrm);                         // Jumps to an arbitrary frame.
    const MMMovieLiveFrame *PlaybackFrame() const
    {
        const auto &frame = _owner.runtime.publication;
        return frame.phase && frame.frame == _owner.data.currentFrame ? &frame : nullptr;
    }
    long Nfrm(void)                                   // Returns the current frame number
    {
        return (_owner.data.currentFrame);
    }
    long NfrmFirst(void) // Returns the number of the first frame in the scene.
    {
        return (_owner.data.firstFrame);
    }
    long NfrmLast(void) // Returns the number of the last frame in the scene.
    {
        return (_owner.data.lastFrame);
    }
    bool FReplayFrm(ulong grfscen); // Replay events in this scene.

    //
    // Undo accessor functions
    //
    void SetNfrmCur(long nfrm)
    {
        _owner.data.currentFrame = nfrm;
    }

    //
    // Edit functions
    //
    void SetMvie(PMovie pmvie); // Sets the associated movie.
    void GetName(PString pstn)    // Gets name of current scene.
    {
        GetSceneName(_owner.data.name, pstn);
    }
    void SetNameCore(PString pstn) // Sets name of current scene.
    {
        SetSceneName(_owner.data.name, pstn);
    }
    bool FSetName(PString pstn); // Sets name of current scene, and undo
    bool FChopCore(void);     // Chops off the rest of the scene.
    bool FChop(void);         // Chops off the rest of the scene and undo
    bool FChopBackCore(void); // Chops off the rest of the scene, backwards.
    bool FChopBack(void);     // Chops off the rest of the scene, backwards, and undo.

    //
    // Transition functions
    //
    void SetTransitionCore(TRANS trans) // Set the final transition to be.
    {
        _owner.data.transition = trans;
    }
    bool FSetTransition(TRANS trans); // Set the final transition to be and undo.
    TRANS Trans(void)
    {
        return static_cast<TRANS>(_owner.data.transition);
    } // Returns the transition setting.
    // These two operate a specific Scene chunk rather than a Scene in memory
    static bool FTransOnFile(PChunkyResourceFile pcrf, ChunkNumber cno, TRANS *ptrans);
    static bool FSetTransOnFile(PChunkyResourceFile pcrf, ChunkNumber cno, TRANS trans);

    //
    // State functions
    //
    void Disable(ulong grfscen) // Disables functionality.
    {
        _owner.runtime.disabled |= grfscen;
    }
    void Enable(ulong grfscen) // Enables functionality.
    {
        _owner.runtime.disabled &= ~grfscen;
    }
    long GrfScen(void) // Currently disabled functionality.
    {
        return _owner.runtime.disabled;
    }

    //
    // Actor functions
    //
    bool FAddActrCore(Actor *pactr); // Adds an actor to the scene at current frame.
    bool FAddActr(Actor *pactr);     // Adds an actor to the scene at current frame, and undo
    void RemActrCore(long arid);    // Removes an actor from the scene.
    bool FRemActr(long arid);       // Removes an actor from the scene, and undo
    PActor PactrSelected(void)       // Returns selected actor
    {
        return static_cast<PActor>(_owner.selected_actor);
    }
    void SelectActr(Actor *pactr);                      // Sets the selected actor
    bool FIsActrSelected(PActor pactr); // primary OR in extras
    long CactrSelected(void);           // total count: 0 if primary is pvNil, else 1 + extras count
    PActor PactrSelectedAt(long iactr); // 0 = primary, 1..N = extras in insertion order
    bool FToggleActrSelected(PActor pactr); // shift-click entry: add if absent, remove if present. fFalse on alloc failure.
    void ClearSelection(void);          // empty primary AND extras; drop all hilites
    bool FSelectAllActrs(void); // select every actor in the current scene; fFalse on alloc failure mid-add
    // Mean of currently-visible selected actors' world positions. fFalse if zero qualify.
    bool FXyzSelectionCentroid(BRS *pxr, BRS *pyr, BRS *pzr);
    // Replace the current selection with every actor in the current scene
    // whose display name contains the tag '#pszTag' (ASCII, case-insensitive,
    // matches '#pszTag' as a whole token terminated by whitespace, end-of-
    // string, or another '#'). Returns fFalse on alloc failure mid-build (in
    // which case the selection ends up empty).
    bool FSelectActrsByTag(PZString pszTag);
    void EditActorSelectionTags(bool strip);
    void CaptureActorDragUndo(MMActorDragUndo *, PActor, bool useGroup, bool freezePivot);
    bool FTransformSelectedActors(const struct MMActorGroupTransform &);
    void BeginActorPlacement(MMActorPlacementState *, PActor, bool entireScene);
    void EndActorPlacement(MMActorPlacementState *);
    bool FMoveActorPlacement(const MMActorPlacementState &, const MMActorPoint &, bool ground);
    bool FFinishActorPlacement(MMActorPlacementState *);
    // Select pactrSeed and every other actor in the current scene that
    // shares at least one #tag with it (set union across pactrSeed's tags).
    // If pactrSeed has no tags this collapses to SelectActr(pactrSeed) --
    // single-actor selection.  Used by plain click in _MouseDown so that
    // clicking a tagged actor recalls its whole group.
    bool FSelectActrsSharingTagsWith(PActor pactrSeed);
    PActor PactrFromPt(long xp, long yp, long *pibset); // Gets actor pointed at by the mouse.
    PActor PactrAt(long index) const
    {
        return static_cast<PActor>(mm_scene_member_at(&_owner, MM_SCENE_ACTORS, index));
    }
    void HideActors(void);
    void ShowActors(void);
    PActor PactrFromArid(long arid); // Finds a current actor in this scene.
    long Cactr(void) const
    {
        return mm_scene_member_count(&_owner, MM_SCENE_ACTORS);
    }

    //
    // Sound functions
    //
    bool FAddSndCoreTagc(bool fLoop, bool fQueue, long vlm, long sty, long ctagc, const MMSceneSoundTag *prgtagc);
    void RemSndCore(long sty);                                            // Removes the sound from current frame.
    bool FRemSnd(long sty);                             // Removes the sound from current frame, and undo
    bool FGetSnd(long sty, bool *pfFound, PSceneSoundEvent *ppsse); // Allows for retrieval of sounds.
    void PlayBkgdSnd(void);
    bool FQuerySnd(long sty, PDynamicArray *pgltagSnd, long *pvlm, bool *pfLoop);
    void SetSndVlmCore(long sty, long vlmNew);
    void UpdateSndFrame(void);
    bool FResolveAllSndTags(ChunkNumber cnoScen);

    //
    // Text box functions
    //
    bool FAddTboxCore(PTBOX ptbox);   // Adds a text box to the current frame.
    bool FAddTbox(PTBOX ptbox);       // Adds a text box to the current frame.
    bool FRemTboxCore(PTBOX ptbox);   // Removes a text box from the scene.
    bool FRemTbox(PTBOX ptbox);       // Removes a text box from the scene.
    PTBOX PtboxFromItbox(long itbox); // Returns the ith tbox in this frame.
    PTBOX PtboxSelected(void)         // Returns the tbox currently selected.
    {
        return static_cast<PTBOX>(_owner.selected_text);
    }
    void SelectTbox(PTBOX ptbox); // Selects this tbox.
    void HideTboxes(void);        // Hides all text boxes.
    long Ctbox(void) const
    {
        return mm_scene_member_count(&_owner, MM_SCENE_TEXTS);
    }

    //
    // Pause functions
    //
    bool FPauseCore(WaitReason *pwit, long *pdts); // Adds\Removes a pause to the current frame.
    bool FPause(WaitReason wit, long dts);         // Adds\Removes a pause to the current frame, and undo

    //
    // Background functions
    //
    bool FSetBkgdCore(PTAG ptag, PTAG ptagOld); // Sets the background for this scene.
    bool FSetBkgd(PTAG ptag);                   // Sets the background for this scene, and undo
    Background *Pbkgd(void)
    {
        return static_cast<PBackground>(_owner.owned[MM_SCENE_BACKGROUND]);
    }                                               // Gets the background for this scene.
    bool FChangeCamCore(long icam, long *picamOld); // Changes camera viewpoint at current frame.
    bool FChangeCam(long icam);                     // Changes camera viewpoint at current frame, and undo
    PMaskedBitmapMBMP PmbmpThumbnail(void);                     // Returns the thumbnail.
    bool FGetTagBkgd(PTAG ptag);                    // Returns the tag for the background for this scene

    //
    // Movie functions
    //
    PMovie Pmvie()
    {
        return static_cast<PMovie>(_owner.movie);
    } // Get the parent movie

    //
    // Mark scene as dirty
    //
    void MarkDirty(bool fDirty = fTrue); // Mark the scene as changed.

    //
    // Clipboard type functions
    //
    bool FPasteActrCore(PActor pactr); // Pastes actor into current frame
    bool FPasteActr(PActor pactr);     // Pastes actor into current frame and undo

    //
    // Playing functions
    //
    bool FStartPlaying(void); // For special behavior when playback starts
    void StopPlaying(void);   // Used to clean up after playback has stopped.
};

#endif //! SCEN_H
