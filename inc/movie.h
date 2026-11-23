/* Copyright (c) Microsoft Corporation.
   Licensed under the MIT License. */

/***************************************************************************

    Status: All changes must be code reviewed.

    Movie Stuff

        A single view on a movie (MovieView)

                DocumentDisplayGraphicsObject  	--->	MovieView

        Callbacks to client (MovieClientCallbacks)

                BASE 	--->	MovieClientCallbacks

        A single movie (Movie)

                DocumentBase	--->	Movie

***************************************************************************/

#ifndef MOVIE_H
#define MOVIE_H
#include <cstdint>
#include "mm/movie_sound.h"
#include "mm/movie_scene.h"
#include "mm/movie_runtime.h"
#include "mm/movie_save.h"
#include "mm/movie_edit.h"
#include "mm/actor_placement.h"
#include "mm/actor_record.h"
#include "mm/movie_content.h"
#include "mm/movie_compose.h"
#include "mm/scene_selection.h"

//
// Tools that can be "loaded" on the mouse cursor
//
enum
{
    toolPlace, // (to position an actor when you first add it)
    toolCompose,
    toolAction,
    toolTweak,
    toolRotateX,
    toolRotateY,
    toolRotateZ,
    toolCostumeCmid, // cmid is in _cmidTool
    toolSquashStretch,
    toolSoonerLater,
    toolResize,
    toolNormalizeRot,
    toolCopyObject,
    toolCutObject,
    toolPasteObject,
    toolCopyRte,
    toolDefault,
    toolTboxMove,
    toolTboxUpDown,
    toolTboxLeftRight,
    toolTboxFalling,
    toolTboxRising,
    toolSceneNuke,
    toolIBeam,
    toolSceneChop,
    toolSceneChopBack,
    toolCutText,
    toolCopyText,
    toolPasteText,
    toolPasteRte,
    toolActorEasel,
    toolActorSelect,
    toolActorNuke,
    toolTboxPaintText,
    toolTboxFillBkgd,
    toolTboxStory,
    toolTboxCredit,
    toolNormalizeSize,
    toolRecordSameAction,
    toolSounder,
    toolLooper,
    toolMatcher,
    toolTboxFont,
    toolTboxStyle,
    toolTboxSize,
    toolListener,
    toolAddAFrame,
    toolFWAFrame,
    toolRWAFrame,
    toolComposeAll,
    toolUndo,     // For playing UI Sound only
    toolRedo,     // For playing UI Sound only
    toolFWAScene, // For playing UI Sound only
    toolRWAScene, // For playing UI Sound only

    toolLimMvie
};

//
// Used to tell the client the change the state of the undo UI
//
enum
{
    undoDisabled = 1,
    undoUndo,
    undoRedo
};

//
// grfbrws flags
//
enum
{
    fbrwsNil = 0,
    fbrwsProp = 1, // fTrue implies prop or 3d
    fbrwsTdt = 2   // fTrue means this is a 3-D Text object
};

//
//
// A class for handling a single Movie view
//
//

//
// The class definition
//
#define MovieView_PAR DocumentDisplayGraphicsObject

typedef class MovieView *PMovieView;
#define kclsMovieView 'MVU'
class MovieView : public MovieView_PAR
{
    RTCLASS_DEC
    ASSERT
    MARKMEM
    CMD_MAP_DEC(MovieView)

  protected:
    /* Make these static; we want to be able to set and restore without having
        an actual MovieView, and they shouldn't be getting set per-MovieView anyway */
    static bool _fKbdDelayed;  // fTrue == we have delayed the keyboard
    static long _dtsKbdDelay;  // System keyboard delay before first repeat
    static long _dtsKbdRepeat; // System keyboard delay between repeats

    static bool _fShowPoseReadout; // Off by default; UI-2 actor pose overlay.

  public:
    static bool FShowPoseReadout(void)
    {
        return _fShowPoseReadout;
    }
    static void SetShowPoseReadout(bool fOn)
    {
        _fShowPoseReadout = fOn;
    }

  protected:

    long _dxp;
    long _dyp; // width and height rendered area.
#ifdef KAUAI_SDL3
    PGraphicsPort _pgptSuperSize = nullptr, _pgptSuperSizeIndexed = nullptr;
    RC _rcSuperSize;
    bool _fDrawingSuperSize = false;
    LegacyUlong _superSizeFrameSerial = 0;
    LegacyUlong _superSizeRgbSerial = 0;
#endif

    bool _fTrackingMouse; // Is the mouse currently being tracked?
    long _xpPrev;         // X location of the mouse.
    long _ypPrev;         // Y location of the mouse.
    BRS _dzrPrev;         // Z motion of the "mouse" (arrow keys)
    long _grfcust;        // Options in effect when mouse was down.
    PCursor _pcursDefault;  // Default cursor for when a tool is not applicable.
    PActor _pactrListener; // Pactr of the actor being auditioned

    TAG _tagTool;       // Tag associated with current tool.
    PTemplate _ptmplTool;   // Template associated with the current tool.
    long _cmidTool;     // Costume id associated with current tool.
    long _tool;         // Current tool loaded on cursor

    MMMovieActorDrag _actorDrag{};
    MMActorPlacementState _placement{};
    MMActorRecordGesture _recordGesture{};
    ulong _tsLastSample; // Last time mouse/kbd sampled
    RC _rcFrame;         // Frame for creating a text box.

    MMActorMatrix _mouseAxes{{{rOne, rZero, rZero}, {rZero, rZero, rOne}, {rZero, -rOne, rZero}}};
    bool _fPause : 1;             // fTrue if pausing play until a click.
    bool _fTextMode : 1;          // fTrue if Text boxes are active.
    bool _fRespectGround : 1;     // fTrue if Y=0 is enforced.
    bool _fMouseOn : 1;
    bool _fMouseDownSeen; // Was the mouse depressed during a place.
    MMSceneActorPointer _pointerSelection{};

    AbstractColor _acr;         // Color for painting text.
    long _onn;        // Font for text
    long _dypFont;    // Font size for text
    ulong _grfont;    // Font style for text

    MovieView(PDocumentBase pdocb, PGraphicsObjectBlock pgcb) : DocumentDisplayGraphicsObject(pdocb, pgcb)
    {
    }

    struct ClipboardHost
    {
        MovieView *view;
        PCommand command;
    };
    static MMMovieClipboardServices _ClipboardServices(ClipboardHost *host);
    void _ClipboardClick(int tool, bool hitActor);

    void _PositionActr(const MMActorPoint &delta);
    void _SampleActorDrag(const CMD_MOUSE &pointer, const MMActorPoint &mouse, const MMActorPoint &world);
    void _MouseDown(CMD_MOUSE *pcmd);
    void _MouseDrag(CMD_MOUSE *pcmd);
    void _MouseUp(CMD_MOUSE *pcmd);

    void _DiscardMoveUndo();
    MMActorRecordGestureServices _RecordGestureServices();
    bool _FRecordTemplateMatches(PActor actor);

    void _ActorClicked(PActor pactr, bool fDown);
    MMSceneActorPointerView _ActorPointerView();

  public:
    static void SlowKeyboardRepeat(void);
    static void RestoreKeyboardRepeat(void);

    //
    // Constructors and desctructors
    //
    static MovieView *PmvuNew(PMovie pmvie, PGraphicsObjectBlock pgcb, long dxy, long dyp);
    ~MovieView(void);

    //
    // Accessor for getting the owning movie
    //
    PMovie Pmvie()
    {
        return (PMovie)_pdocb;
    }

    //
    // Command handlers
    //
    virtual bool FCmdTrackMouse(PCMD_MOUSE pcmd);
    virtual bool FCmdMouseMove(PCMD_MOUSE pcmd);
    virtual bool FCmdKey(PCMD_KEY pcmd);
    virtual bool FCmdClip(Command *pcmd);
    virtual bool FCmdUndo(PCommand pcmd);
    virtual bool FCloseDoc(bool fAssumeYes, bool fSaveDDG = fFalse);
    virtual bool FCmdSave(PCommand pcmd);
    bool FCmdIdle(Command *pcmd); // Called whenever an idle loop is seen.
    bool FCmdRollOff(Command *pcmd);

    //
    // View specific functions.
    //
#ifdef KAUAI_SDL3
    bool FDrawSubtree(PGraphicsEnvironment pgnv, RC *prcClip, ulong grfgob) override;
    PGraphicsPort PgptSuperSizeFrame(bool redraw = false, bool indexed = false);
    LegacyUlong SuperSizeFrameSerial() const
    {
        return _superSizeFrameSerial;
    }
#endif
    void SetTool(long tool);
    long Tool(void)
    {
        return _tool;
    }
    long AnidTool(void)
    {
        return _recordGesture.action;
    }
    long CmidTool(void)
    {
        return _cmidTool;
    }
    PTAG PtagTool(void)
    {
        return &_tagTool;
    }
    void SetAnidTool(long anid)
    {
        _recordGesture.action = anid;
    }
    void SetTagTool(PTAG ptag);
    void SetCmidTool(long cmid)
    {
        _cmidTool = cmid;
    }
    void StartPlaceActor(bool fEntireScene = fFalse);
    void EndPlaceActor(void);
    void WarpCursToCenter(void);
    void WarpCursToActor(PActor pactr);
    void AdjustCursor(long xp, long yp);
    void MouseToWorld(MMActorPoint &delta, bool recording);
    void SetAxis(BRS rgrAxis[3][3])
    {
        BltPb(rgrAxis, _mouseAxes.m, size(BRS) * 9);
    }
    void SetFRecordDefault(bool f)
    {
        _recordGesture.replace = f;
    }
    void SetFRespectGround(bool f)
    {
        _fRespectGround = f;
    }
    bool FRecordDefault()
    {
        return _recordGesture.replace != 0;
    }
    void PauseUntilClick(bool fPause)
    {
        _fPause = fPause;
    }
    bool FPausing(void)
    {
        return _fPause;
    }
    bool FTextMode(void)
    {
        return _fTextMode;
    }
    bool FActrMode(void)
    {
        return !_fTextMode;
    }
    bool FRespectGround(void)
    {
        return _fRespectGround;
    }
    void SetActrUndo(PActor pactr);
    void SetPaintAcr(AbstractColor acr)
    {
        _acr = acr;
    }
    AbstractColor AcrPaint(void)
    {
        return _acr;
    }
    void SetOnnTextCur(long onn)
    {
        _onn = onn;
    }
    long OnnTextCur(void)
    {
        return _onn;
    }
    void SetDypFontTextCur(long dypFont)
    {
        _dypFont = dypFont;
    }
    long DypFontTextCur(void)
    {
        return _dypFont;
    }
    void SetStyleTextCur(ulong grfont)
    {
        _grfont = grfont;
    }
    ulong GrfontStyleTextCur(void)
    {
        return _grfont;
    }

    //
    // Routines for communicating with the framework
    //
    void Draw(PGraphicsEnvironment pgnv, RC *prcClip);
};

//
//
// Movie Client Callbacks.  Used for filling in
// parameters for this movie, and for notifying
// client of state changes.
//
//

#define MovieClientCallbacks_PAR BASE

class MovieClientCallbacks;

typedef MovieClientCallbacks *PMovieClientCallbacks;
#define kclsMovieClientCallbacks 'MCC'
class MovieClientCallbacks : public MovieClientCallbacks_PAR
{
  protected:
    long _dxp;
    long _dyp;
    long _cbCache;

  public:
    MovieClientCallbacks(long dxp, long dyp, long cbCache)
    {
        _dxp = dxp;
        _dyp = dyp;
        _cbCache = cbCache;
    }
    virtual long Dxp(void)
    {
        return _dxp;
    } // Width of the rendering area
    virtual long Dyp(void)
    {
        return _dyp;
    } // Height of the rendering area
    virtual long CbCache(void)
    {
        return _cbCache;
    } // Number of bytes to use for caching.
    virtual void SetCurs(long tool)
    {
    } // Sets the cursor based on the tool, may be pvNil.
    virtual void UpdateRollCall(void)
    {
    } // Tells the client to update its roll call.
    virtual void UpdateAction(void)
    {
    } // Tells the client to update its action menu.
    virtual void UpdateScrollbars(void)
    {
    } // Tells the client to update its scrollbars.
    virtual void PlayStopped(void)
    {
    } // Tells the client that playback was stopped internally.
    virtual void ChangeTool(long tool)
    {
    } // Tells the client that the tool was changed internally.
    virtual void SceneNuked(void)
    {
    } // Tells the client that a scene was nuked.
    virtual void SceneUnnuked(void)
    {
    } // Tells the client that a scene was nuked and now undone.
    virtual void ActorNuked(void)
    {
    } // Tells the client that an actor was nuked.
    virtual void EnableActorTools(void)
    {
    } // Tells the client that the first actor was added.
    virtual void EnableTboxTools(void)
    {
    } // Tells the client that the first textbox was added.
    virtual void TboxSelected(void)
    {
    } // Tells the client that a new text box was selected.
    virtual void ActorSelected(long arid)
    {
    } // Tells the client that an actor was selected
    virtual void ActorEasel(bool *pfActrChanged)
    {
    } // Lets client edit 3-D Text or costume
    virtual void SetUndo(long undo)
    {
    } // Tells the client the state of the undo buffer.
    virtual void SceneChange(void)
    {
    } // Tells the client that a different scene is the current one
    virtual void PauseType(WaitReason wit)
    {
    } // Tells the client of a new pause type for this frame.
    virtual void Recording(bool fRecording, bool fRecord)
    {
    } // Tells the client that the movie engine is recording or not.
    virtual void StartSoonerLater(void)
    {
    } // Tells the client that an actor is selected for sooner/latering.
    virtual void EndSoonerLater(void)
    {
    } // Tells the client that an actor is done for sooner/latering.
    virtual void NewActor(void)
    {
    } // Tells the client that an actor has just been placed.
    virtual void StartActionBrowser(void)
    {
    } // Tells the client to start up the action browser.
    virtual void StartListenerEasel(void)
    {
    } // Tells the client to start up the listener easel.
    virtual bool GetFniSave(Filename *pfni, long lFilterLabel, long lFilterExt, long lTitle, achar *lpstrDefExt,
                            PString pstnDefFileName)
    {
        return fFalse;
    } // Tells the client to start up the save portfolio.
    virtual void PlayUISound(long tool, long grfcust = 0)
    {
    } // Tells the client to play sound associated with use of tool.
    virtual void StopUISound(void)
    {
    } // Tells the client to stop sound associated with use of tools.
    virtual void UpdateTitle(PString pstnTitle)
    {
    } // Tells the client that the movie name has changed.
    virtual void EnableAccel(void)
    {
    } // Tells the client to enable keyboard accelerators.
    virtual void DisableAccel(void)
    {
    } // Tells the client to disable keyboard accelerators.
    virtual void GetStn(long ids, PString pstn)
    {
    } // Requests the client to fetch the given ids string.
    virtual long DypTextDef(void)
    {
        return vpappb->DypTextDef();
    }
    virtual long DypTboxDef(void)
    {
        return 14;
    }
    virtual void SetSndFrame(bool fSoundInFrame)
    {
    }
    virtual bool FMinimized(void)
    {
        return fFalse;
    }
    virtual bool FQueryPurgeSounds(void)
    {
        return fFalse;
    }
};

struct CompositeMovie : MMCompositeMovie
{
    void Empty(void);
#ifdef DEBUG
    void MarkMem(void);
#endif
};
typedef CompositeMovie *PCompositeMovie;

//
//
// Movie Class.
//
//

const long kccamMax = 9;

typedef class Movie *PMovie;

#if defined(DEBUG) || defined(STUDIO_TEST_TOOLS)
struct MoviePlaybackTestProgress
{
    bool active = false, complete = false, failed = false;
    const char *error = "";
    std::uint64_t frames = 0, expectedFrames = 0, framesInScene = 0, steps = 0, scrollSteps = 0;
    LegacyLong scenes = 0, scene = -1, frame = 0, first = 0, last = 0;
    LegacyLong failureCommand = -1, originalVolume = 0;
    LegacyUlong started = 0, elapsed = 0;
};
#endif

#define Movie_PAR DocumentBase
#define kclsMovie 'MVIE'
class Movie : public Movie_PAR
{
    friend class MovieSaveProbe;
    friend class MovieSceneUndo;
    friend class MovieView;
    friend class SceneUndoChop;
    RTCLASS_DEC
    MARKMEM
    ASSERT
    CMD_MAP_DEC(Movie)

  protected:
    MMPlaybackState _playback; // Playback values, independent of views and clocks.
#if defined(DEBUG) || defined(STUDIO_TEST_TOOLS)
    MoviePlaybackTestProgress _playbackTest;
    void (*_playbackFrameObserver)(Movie *) = nullptr;
    void _RecordPlaybackTestFrame();
    void _FinishPlaybackTest();
#endif
    MMMovieState _data; // Archive state and roll-call ownership, independent of views.

    MMMovieSaveState _save; // Save flags and saved-file reference owned by C.
    Filename _containerName;
    bool _FNeedsContainer();

    MMMovieScenes _scenes; // Current scene reference and index owned by C control.
    bool _fGCSndsOnClose : 1;    // Garbage collection of sounds on close

    MMMovieRuntime _runtime{};
    Clock _clok; // Clock for playing the film

#ifdef DEBUG
    bool _fWriteBmps;
    LegacyLong _lwBmp;
#endif // DEBUG

  private:
    Movie(void);
    Movie(const Movie &) = delete;
    Movie &operator=(const Movie &) = delete;
    struct AlarmHost;
    static MMAlarmPlaybackServices _AlarmServices(AlarmHost *);
    bool _FCloseCurrentScene(void);               // Closes and releases current scene, if any
    bool _FMakeCrfValid(void);                    // Makes sure there is a file to work with.
    bool _FUseTempFile(void);                     // Switches to using a temp file.
    void _DoSndGarbageCollection(bool fPurgeAll); // Remove unused user sounds from movie
    MMMovieSceneServices _SceneServices();
    MMMovieRollCallServices _RollCallServices();
    MMMovieUndoServices _UndoServices();
    MMMovieEditServices _EditServices();
    MMMovieContentServices _ContentServices();
    MMMovieComposeServices _ComposeServices();
    struct SaveHost;
    MMMovieSaveServices _SaveServices(SaveHost *);
    struct RuntimeHost;
    static MMMovieRuntimeServices _RuntimeServices(RuntimeHost *);
    void _SetTitle(PFilename pfni = pvNil);            // Set the title of the movie based on given file name.

  public:
#if defined(DEBUG) || defined(STUDIO_TEST_TOOLS)
    bool FBeginPlaybackTest();
    void StepPlaybackTest(LegacyLong frames, LegacyUlong milliseconds);
    void StopPlaybackTest();
    const MoviePlaybackTestProgress &PlaybackTestProgress() const { return _playbackTest; }
    LegacyLong TestSkippedClickPauses() const { return _playback.skippedClickPauses; }
    LegacyLong TestSkippedTimedPauses() const { return _playback.skippedTimedPauses; }
    LegacyLong TestSkippedSoundPauses() const { return _playback.skippedSoundPauses; }
#endif
    //
    // Begin client useable functions
    //

#ifdef DEBUG
    void SetFWriteBmps(bool fWriteBmps)
    {
        if (fWriteBmps && !_fWriteBmps)
            _lwBmp = 0;
        _fWriteBmps = fWriteBmps;
    }
    bool FWriteBmps(void)
    {
        return _fWriteBmps;
    }
#endif // DEBUG

    //
    // Getting views
    //
    PMovieView PmvuCur(void);
    PMovieView PmvuFirst(void);

    //
    // Create and Destroy
    //
    static PMovie PmvieNew(bool fHalfMode, PMovieClientCallbacks pmcc, Filename *pfni = pvNil, ChunkNumber cno = cnoNil);
    // Create a movie and read it if
    //   pfni != pvNil
    // reads roll call for a given movie
    void ForceSaveAs(void);
    void Flush(void);
    bool FReadOnly(void)
    {
        return FPure(_save.readOnly);
    }

    ~Movie(void);

    //
    // MovieClientCallbacks maintenance
    //
    PMovieClientCallbacks Pmcc(void)
    {
        return static_cast<PMovieClientCallbacks>(static_cast<BASE *>(_runtime.client));
    } // Accessor for getting to client callbacks.
    void SetMcc(PMovieClientCallbacks pmcc);

    //
    // Title stuff
    //
    void GetName(PString pstnTitle); // Gets the title of the movie.
    void ResetTitle(void);

    //
    // Scene stuff
    //
    long Cscen(void) // Returns number of scenes in movie
    {
        return _data.sceneCount;
    }
    long Iscen(void) // Returns the current scene number
    {
        return _scenes.index;
    }
    bool FSwitchScen(long iscen);                                // Loads and returns pointer to scene iscen,
                                                                 //   saving any current scene.
    bool FRemScen(long iscen);                                   // Removes a scene from the movie, and undo
    bool FChangeCam(long camid);                                 // Change the camera view in the scene.
    bool FInsTbox(RC *prc, bool fStory);                         // Insert a text box into the scene.
    bool FHideTbox(void);                                        // Hide selected text box from the scene at this fram.
    bool FNukeTbox(void);                                        // Remove selected text box from the scene.
    void SelectTbox(long itbox);                                 // Select the itbox'th text box in the frame.
    void SetPaintAcr(AbstractColor acr);                                   // Sets color that painting will occur with.
    void SetOnnTextCur(long onn);                                // Sets font that text will be in
    void SetDypFontTextCur(long dypFont);                        // Sets font size that text will be in
    void SetStyleTextCur(ulong grfont);                          // Sets font style that text will be in
    bool FInsActr(PTAG ptag);                                    // Insert an actor into the scene.
    bool FRemActr(void);                                         // Remove selected actor from scene.
    bool FAddOnstage(long arid);                                 // Bring this actor onto the stage.
    bool FRotateActr(BRA xa, BRA ya, BRA za, bool fFromHereFwd); // Rotate selected actor by degrees
    bool FSquashStretchActr(BRS brs);                            // Squash/Stretch selected actor
    bool FSoonerLaterActr(long nfrm);                            // Set the selected actor's destination frame
    bool FStepActorTime(long frames);                            // Sooner/Later selected actor
    bool FScaleActr(BRS brs);                                    // Scale selected actor
    bool FCostumeActr(long ibprt, PTAG ptag, long cmid, tribool fCustom);
    bool FAddScen(PTAG ptag);         // Add a scene after the current one, or change
                                      //   change bkgd if scene is empty
    bool FSetTransition(TRANS trans); // Set the transition type for the current scene.
    void Play(void);                  // Start/Stop a movie playing.
    void SetUserPaused(bool paused);  // Freeze the live clock/audio without restarting the scene.
    bool FUserPaused() const
    {
        return _runtime.transport.paused != 0;
    }
    bool FPause(WaitReason wit, long dts);   // Insert a pause here.

    bool FAddToCmvi(PCompositeMovie pcmvi, long *piscendIns);
    // Add this movie to the CompositeMovie
    bool FSetCmvi(PCompositeMovie pcmvi); // Re-build the movie from the CompositeMovie
    bool _FAddMvieToRollCall(ChunkNumber cno, long aridMin);
    // Updates roll call for an imported movie
    bool _FAdoptMsndInMvie(MMResourceCache *, ChunkNumber cnoScen); // Adopt msnd chunks as children of the movie

    bool FAddBkgdSnd(PTAG ptag, tribool fLoop, tribool fQueue, long vlm = vlmNil,
                     long sty = styNil);                                                              // Adds a sound
    bool FAddActrSnd(PTAG ptag, tribool fLoop, tribool fQueue, tribool fActnCel, long vlm, long sty); // Adds a sound

    //
    // Auto save stuff
    //
    bool FAutoSave(PFilename pfni = pvNil, bool fCleanRollCall = fFalse); // Save movie in temp file
    MMMovieSoundServices SoundServices();
    bool FSaveTagSnd(TAG *ptag);
    bool FCopySndFileToMvie(PFileObject pfil, long sty, ChunkNumber *pcno, PString pstn = pvNil);
    bool FEnsureAutosave(MMResourceCache **pcrf = pvNil);
    bool FCopyMsndFromCrf(MMResourceCache *source, ChunkNumber cnoSrc, ChunkNumber *pcnoDest);
    bool FResolveSndTag(PTAG ptag, ChildChunkID chid, ChunkNumber cnoScen = cnoNil, MMResourceCache *pcrf = pvNil);
    bool FResolveSndTag(MMTag &tag, ChildChunkID chid, ChunkNumber cnoScen = cnoNil,
                        MMResourceCache *pcrf = pvNil);
    bool FChidFromUserSndCno(ChunkNumber cno, ChildChunkID *pchid);
    void SetDocClosing(bool fClose)
    {
        _save.closing = fClose;
    }
    bool FQueryDocClosing(void)
    {
        return _save.closing != 0;
    }
    bool FQueryGCSndsOnClose(void)
    {
        return _fGCSndsOnClose;
    }
    bool FUnusedSndsUser(bool *pfHaveValid = pvNil);

    //
    // Roll call
    //
    bool FGetArid(long iarid, long *parid, PString pstn, long *pcactRef,
                  PTAG ptagTmpl = pvNil); // return actors one by one
    bool FChooseArid(long arid);          // user chose arid in roll call
    long AridSelected(void);
    bool FGetName(long arid, PString pstn);      // Return the name of a specific actor.
    // Borrows the name until the roll call changes.
    bool FGetName(long arid, const void **name, MMU32 *count);
    bool FNameActr(long arid, PString pstn);     // Set the name of this actor.
    bool FNameActr(long arid, const achar *name, LegacyLong count);
    void ChangeActrTag(long arid, PTAG ptag); // Change an actor's Template tag
    long CmactrMac(void)
    {
        AssertThis(0);
        return _data.actors.count;
    }
    bool FIsPropBrwsIarid(long iarid); // Identify the roll call browser iarids
    bool FIsIaridTdt(long iarid);      // 3d spletter

    //
    // Overridden DocumentBase functions
    //
    bool FGetFni(Filename *pfni);                  // For saving to a file
    bool FSave(long cid);                     // For saving to a file, (calls FGetFni and FSaveToFni)
    bool FSaveToFni(Filename *pfni, bool fSetFni); // For doing a Save As or Save
    static void SetContainerPrepare(MMMovieContainerPrepare);
    bool CommitContainer() { return mm_movie_container_commit(&_save.container) != 0; }
    void *ContainerDocument(const MMMovieContainerServices *services) const
    {
        return services == _save.container.services ? _save.container.document : nullptr;
    }
#if defined(DEBUG) || defined(STUDIO_TEST_TOOLS)
    void ObservePlaybackFrames(void (*observer)(Movie *))
    {
        _playbackFrameObserver = observer;
    }
#endif
    PDocumentMDIWindow PdmdNew(void);                       // Do not use!
    bool FGetFniSave(Filename *pfni);              // For saving via the portfolio.

    //
    // Drawing stuff
    //
    void InvalViews(void);       // Invalidates all views on the movie.
    void InvalViewsAndScb(void); // Invalidates all views of movie and scroll bars.
    void MarkViews(void);        // Marks all views on the movie.
    TRANS Trans(void)
    {
        return static_cast<TRANS>(_playback.transition);
    } // Current transition in effect.
    void SetTrans(TRANS trans)
    {
        _playback.transition = trans;
    }                                                                 // Set transition
    void DoTrans(PGraphicsEnvironment pgnvDst, PGraphicsEnvironment pgnvSrc, RC *prcDst, RC *prcSrc); // Draw current transition into GNVs

    //
    // Sound stuff
    //
    bool FSoundsEnabled(void)
    {
        return (_playback.soundsEnabled);
    }
    void SetFSoundsEnabled(bool f)
    {
        _playback.soundsEnabled = f;
    }

    //
    // End client callable functions
    //

    //
    // Begin internal movie engine functions
    //

    //
    // Command handlers
    //
    bool FCmdAlarm(Command *pcmd);  // Called at timer expiration (playback).
    bool FCmdRender(Command *pcmd); // Called to render a frame during playback.

    //
    // Automated test APIs
    //
    long LwQueryExists(long lwType, long lwId);     // Called by other apps to find if an actor/tbox exists.
    long LwQueryLocation(long lwType, long lwId);   // Called by other apps to find where actor/tbox exists.
    long LwSetMoviePos(long lwScene, long lwFrame); // Called by other apps to set the movie position.

    //
    // Scene stuff
    //
    PScene Pscen(void)
    {
        return static_cast<PScene>(_scenes.open);
    }                                           // The currently open scene.
    MMMovieSession *MovieSession() const { return _data.session; }
    void RenderWorld();
    bool FNewScenInsCore(long iscen);           // Inserts a blank scene before iscen
    bool FRemScenCore(long iscen);              // Removes a scene from the movie
    bool FPasteActr(PActor pactr);               // Pastes an actor into current scene.
    bool FPasteActrPath(PActor pactr);           // Pastes the path onto selected actor.
    MMFrameSoundQueue *SoundQueue() { return &_runtime.sounds; }

    //
    // Runtime Pausing
    //
    void DoPause(WaitReason wit, long dts) // Make the movie pause during run.
    {
        _playback.wait = wit;
        _playback.pauseTicks = dts;
    }

    //
    // Material stuff
    //
    bool FInsertMtrl(PMaterial_MTRL pmtrl, PTAG ptag); // Inserts a material into this movie.

    //
    // 3-D Text stuff
    //
    bool FInsTdt(PString pstn, long tdts, PTAG ptagTdf); // Inserts a ThreeDText into this movie.
    bool FChangeActrTdt(PActor pactr, PString pstn, long tdts, PTAG ptagTdf);

    //
    // Marking (overridden DocumentBase methods)
    //
    virtual bool FDirty(void) // Has the movie changed since last saved?
    {
        return _save.autosaveDirty || _fDirty;
    }
    virtual void SetDirty(bool fDirty = fTrue) // Mark the movie as changed.
    {
        _save.autosaveDirty = fDirty;
    }

    //
    // Roll call
    //
    bool FAddToRollCall(Actor *pactr, PString pstn);                    // Add an actor to the roll call
    void RemFromRollCall(Actor *pactr, bool fDelIfOnlyRef = fFalse); // Remove an actor from the roll call.
    void BuildActionMenu(void);                                     // Called when the selected actor has changed.

    //
    // Overridden DocumentBase functions
    //
    PDocumentDisplayGraphicsObject PddgNew(PGraphicsObjectBlock pgcb);    // For creating a view on a movie.
    bool FAddUndo(PMovieUndo pmunb); // Add an item to the undo list
    void ClearUndo(void);

    //
    // Accessors for MVUs only.
    //
#ifdef DEBUG
    void SetFPlaying(bool f)
    {
        _playback.playing = f;
        if (!f)
            SetFWriteBmps(fFalse);
    }  // Set the playing flag.
#else  // DEBUG
    void SetFPlaying(bool f)
    {
        _playback.playing = f;
    } // Set the playing flag.
#endif // !DEBUG
    void SetFStopPlaying(bool f)
    {
        _playback.stopRequested = f;
    } // INTERNAL USE ONLY
    bool FStopPlaying(void)
    {
        return (_playback.stopRequested);
    } // INTERNAL USE ONLY
    bool FPlaying(void)
    {
        return _playback.playing;
    } // Query the playing flag.
    PClock Pclok()
    {
        return &_clok;
    } // For getting the clock for playing
    void SetFIdleSeen(bool fIdle);
    bool FIdleSeen(void)
    {
        return _playback.idleSeen;
    }

    //
    // Accessor for getting to the Brender world.
    //
    PWorld Pbwld(void)
    {
        return static_cast<PWorld>(static_cast<BASE *>(_runtime.world));
    }

    //
    // Frame rate information
    //
    long Cnfrm(void)
    {
        return _playback.renderedFrames;
    }
    ulong TsStart(void)
    {
        return _playback.startTimestamp;
    }

    //
    // Thumbnail stuff
    //
    PDynamicArray PglclrThumbPalette(void)
    {
        AssertThis(0);
        return static_cast<PDynamicArray>(static_cast<BASE *>(_runtime.thumbnail_palette));
    }
    void SetThumbPalette(PDynamicArray pglclr);
};

#endif // !MOVIE_H
