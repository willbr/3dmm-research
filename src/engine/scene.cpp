/* Copyright (c) Microsoft Corporation.
   Licensed under the MIT License. */

/***************************************************************************

  scene.cpp

  Author: Sean Selitrennikoff

  Date: August, 1994

  This file contains all functionality for scene manipulation.

  THIS IS A CODE REVIEWED FILE

    Basic scene private classes:

        Scene Chop Undo Object (SceneUndoChop)

            BASE ---> UndoBase ---> MovieUndo ---> SceneUndoChop

        Scene Background Undo Object (SceneUndoBackground)

            BASE ---> UndoBase ---> MovieUndo ---> SceneUndoBackground

        Scene Pause Undo Object (SceneUndoPause)

            BASE ---> UndoBase ---> MovieUndo ---> SceneUndoPause

        Scene Text box Undo Object (SceneUndoText)

            BASE ---> UndoBase ---> MovieUndo ---> SceneUndoText

        Scene Sound Undo Object (SceneUndoSound)

            BASE ---> UndoBase ---> MovieUndo ---> SceneUndoSound

        Scene Title Undo Object (SceneUndoTitle)

            BASE ---> UndoBase ---> MovieUndo ---> SceneUndoTitle

        Scene Transition Undo Object (SceneUndoTransition)

            BASE ---> UndoBase ---> MovieUndo ---> SceneUndoTransition

***************************************************************************/

#include "soc.h"
#include "body_world.h"
#include "movie_archive_host.h"
#include "engine_movie_graph.h"
#include "scene_sound.h"
#include "scene_sound_undo.h"
#include "scene_edit_undo.h"
#include "scene_content_undo.h"
#include "scene_chop_undo.h"
#include "scene_control_undo.h"
#include "mm/chunk_copy.h"
#include "mm/private_archive.h"
#include <new>
#include <cstring>

ASSERTNAME
static_assert(kctgActr == MM_SCENE_ACTOR_CHUNK, "Scene actor link type");

//
// Scene event types
//
//
// Scene thumbnails
//




//
// Scene event
//

MMSceneSoundPacketServices ScenePacketServices(PMovie movie)
{
    MMSceneSoundPacketServices s = {};
    s.context = movie;
    s.allocate = [](void *, MMI32 bytes) -> void * {
        void *p = nullptr;
        return FAllocPv(&p, bytes, fmemNil, mprNormal) ? p : nullptr;
    };
    s.retain = [](void *, const MMSceneSoundTag *record) {
        Assert(record->tag.sid >= 0, "Invalid sid");
        mm_tag_dup(&record->tag, &TagManager::CoreServices());
    };
    s.resolve = [](void *p, MMSceneSoundTag *record) -> int {
        return static_cast<PMovie>(p)->FResolveSndTag(record->tag, record->child);
    };
    s.fetch = [](void *, const MMSceneSoundTag *record) -> void * {
        TAG tag;
        TagFromCore(&tag, record->tag);
        return vptagm->PbacoFetch(&tag, MovieSoundMSND::FReadMsnd);
    };
    s.priority = [](void *, void *sound, int loopTool) -> MMI32 {
        return static_cast<PMovieSoundMSND>(sound)->Spr(loopTool ? toolLooper : toolSounder);
    };
    s.enqueue = [](void *p, void *sound, int loop, int queue, MMI32 volume, MMI32 priority,
                   MMU32 start) {
        static_cast<PMovie>(p)->Pmsq()->FEnqueue(static_cast<PMovieSoundMSND>(sound), 0, loop != 0, queue != 0, volume,
                                                 priority, fFalse, start);
    };
    s.release = [](void *, void *sound) {
        auto p = static_cast<PMovieSoundMSND>(sound);
        ReleasePpo(&p);
    };
    return s;
}

static MMSceneSoundValueServices SceneSoundValues(PMovie movie = nullptr)
{
    MMSceneSoundValueServices v = {};
    v.packets = ScenePacketServices(movie);
    v.release = [](void *, MMSceneSound *sound) { ReleasePpsse(&sound); };
    return v;
}

//
// Undo object for chopping operation.
//
//
// Undo object for background operations
//


//
// Undo object for transition operations
//
//
// Undo object for pause operations
//


//
// Undo object for text box operations
//


//
// Undo object for sound operations
//
//
// Undo object for title operations
//
RTCLASS(Scene)
RTCLASS(SceneUndoTitle)
RTCLASS(SceneUndoSound)
RTCLASS(SceneActorUndo)
RTCLASS(SceneUndoBackground)
RTCLASS(SceneUndoPause)
RTCLASS(SceneUndoText)
RTCLASS(SceneUndoChop)
RTCLASS(SceneUndoTransition)

static void SceneOwnerDiagnostic(void *, int issue)
{
    switch (issue)
    {
    case MM_SCENE_BAD_FRAME:
        Assert(false, "Invalid event in event stream.");
        break;
    case MM_SCENE_BAD_START:
        Bug("Invalid event in event stream.");
        break;
    case MM_SCENE_UNKNOWN_EVENT:
        Bug("Unknown event type");
        break;
    case MM_SCENE_BAD_SOUND:
        Assert(false, "Invalid SceneSoundEvent");
        break;
    case MM_SCENE_BAD_PAYLOAD:
        Bug("Wrong size for scene event payload");
        break;
    case MM_SCENE_TEXT_HIDE_FAILED:
        Assert(false, "Could not remove a text box");
        break;
    }
}
static MMSceneSoundServices SceneSoundServices()
{
    static_assert(int(styNil) == 0 && int(styLim) == 5, "Scene sound style bounds");
    MMSceneSoundServices s = {};
    s.close = [](void *, MMSceneSoundTag *record) {
        Assert(record->tag.sid == ksidInvalid || record->tag.sid >= 0, "Invalid sid");
        mm_tag_close(&record->tag, &TagManager::CoreServices());
    };
    s.free = [](void *, void *p) { FreePpv(&p); };
    s.diagnostic = SceneOwnerDiagnostic;
    return s;
}
MMSceneOwnerServices Scene::_OwnerServices()
{
    MMSceneOwnerServices s = {};
    s.create_scene = [](void *, void *movie) -> void * { return NewObj Scene(static_cast<PMovie>(movie)); };
    s.state = [](void *, void *p) { return &static_cast<PScene>(p)->_owner; };
    s.can_notify = [](void *) -> int { return vpcex != nullptr; };
    s.loaded = [](void *, void *) { vpcex->EnqueueCid(cidSceneLoaded); };
    s.release = [](void *, void *p) { static_cast<BASE *>(p)->Release(); };
    s.actor_visibility = [](void *, void *p, int show) {
        auto actor = static_cast<PActor>(p);
        AssertPo(actor, 0);
        if (show)
            actor->Show();
        else
            actor->Hide();
    };
    s.lights_off = [](void *, void *p) { static_cast<PBackground>(p)->TurnOffLights(); };
    s.text_lifetime = [](void *, void *p, MMI32 *first) -> int {
        auto text = static_cast<PTBOX>(p);
        AssertPo(text, 0);
        long start, last;
        bool ok = text->FGetLifetime(&start, &last);
        if (ok)
            *first = start;
        return ok;
    };
    s.text_seek = [](void *, void *p, MMI32 frame) -> int { return static_cast<PTBOX>(p)->FGotoFrame(frame); };
    s.diagnostic = SceneOwnerDiagnostic;
    s.sound = SceneSoundServices();
    return s;
}

/****************************************************
 *
 * Constructor for scenes.  This function is private, use PscenNew()
 * for public construction.
 *
 * Parameters:
 *  pmvie - The movie this scene belongs to.
 *
 * Returns:
 *  None.
 *
 ****************************************************/
Scene::Scene(PMovie pmvie)
{
    AssertNilOrPo(pmvie, 0);
    const auto allocator = CollectionAllocator();
    AssertDo(mm_scene_owner_init(&_owner, pmvie, sizeof(achar), &allocator), "invalid scene title width");
#ifdef DEBUG
    for (auto &track : _owner.tracks)
        track.rows.trash_unused = 1;
#endif
}

/****************************************************
 *
 * Exported constructor for scenes.
 *
 * Parameters:
 *	pmvie - The movie this scene belongs to.
 *
 *
 * Returns:
 *  pvNil, on failure, else a pointer to an allocated Scene object.
 *
 ****************************************************/
PScene Scene::PscenNew(PMovie pmvie)
{
    AssertNilOrPo(pmvie, 0);
    auto services = _OwnerServices();
    auto scene = static_cast<PScene>(mm_scene_owner_new(pmvie, &services));
    AssertNilOrPo(scene, 0);
    return scene;
}

/****************************************************
 *
 * Destructor for scenes.
 *
 * Parameters:
 *  None.
 *
 * Returns:
 *  None.
 *
 ****************************************************/
Scene::~Scene(void)
{
    AssertBaseThis(0);
    auto services = _OwnerServices();
    mm_scene_owner_dispose(&_owner, &services);
}

/****************************************************
 *
 * Destructor for scenes.  This method is used to not only
 * destruct a scene, but to remove all lights, actors and
 * text boxes from the rendering area.  This is necessary
 * because Undo will hold references to actors, etc, causing
 * their destructors to not be called.
 *
 * Parameters:
 *  ppscen - A pointer to the scene to destroy.
 *
 * Returns:
 *  None.
 *
 ****************************************************/

void Scene::Close(PScene *ppscen)
{
    AssertPo(*ppscen, 0);
    void *scene = *ppscen;
    auto services = _OwnerServices();
    mm_scene_owner_close(&scene, &services);
    *ppscen = static_cast<PScene>(scene);
}

#ifdef DEBUG

/****************************************************
 * Mark memory used by the Scene
 *
 * Parameters:
 * 	None.
 *
 * Returns:
 *  None.
 *
 ****************************************************/
void Scene::MarkMem(void)
{
    AssertThis(0);

    Scene_PAR::MarkMem();

    for (const auto &track : _owner.tracks)
    {
        MarkPv(track.storage.buffers[0].data);
        MarkPv(track.storage.buffers[1].data);
    }
    MarkMemObj(static_cast<PMaskedBitmapMBMP>(_owner.owned[MM_SCENE_THUMBNAIL]));
    MarkMemObj(static_cast<PChunkyResourceFile>(_owner.owned[MM_SCENE_BACKGROUND_CACHE]));

    for (const auto &list : _owner.members)
        MarkPv(list.storage.buffers[0].data);
    for (long i = 0; i < Cactr(); ++i)
        MarkMemObj(PactrAt(i));
    for (long i = 0; i < Ctbox(); ++i)
        MarkMemObj(static_cast<PTBOX>(mm_scene_member_at(&_owner, MM_SCENE_TEXTS, i)));

    MarkPv(_owner.owned[MM_SCENE_BACKGROUND_SOUND]);
}

/***************************************************************************
    Assert the validity of the Scene.
***************************************************************************/
void Scene::AssertValid(LegacyUlong grf)
{
    MMU32 isev;
    SceneEvent sev;

    Scene_PAR::AssertValid(fobjAllocated);

    Assert(_owner.data.name.count <= 255 && _owner.data.name.unit == sizeof(achar), "bad scene name");
    AssertNilOrPo(PactrSelected(), 0);
    for (MMI32 i = 0; i < mm_scene_member_count(&_owner, MM_SCENE_SELECTION); ++i)
        AssertPo(static_cast<PActor>(mm_scene_member_at(&_owner, MM_SCENE_SELECTION, i)), 0);
    AssertNilOrPo(Pbkgd(), 0);
    AssertNilOrPo(static_cast<PMaskedBitmapMBMP>(_owner.owned[MM_SCENE_THUMBNAIL]), 0);
    for (const auto &track : _owner.tracks)
        Assert(track.rows.storage == &track.storage, "bad scene track storage");
    AssertPo(Pmvie(), 0);

    for (isev = 0; isev < _FrameTrack()->rows.count; isev++)
    {
        sev = ReadSceneTrackEvent(_FrameTrack(), isev);
        switch (sev.sevt)
        {
        case sevtPlaySnd:
        case sevtPause:
        case sevtChngCamera:
            break;
        default:
            Bug("Unknown event type");
        }
    }

    for (isev = 0; isev < _StartTrack()->rows.count; isev++)
    {
        sev = ReadSceneTrackEvent(_StartTrack(), isev);
        switch (sev.sevt)
        {
        case sevtAddActr:
        case sevtSetBkgd:
        case sevtAddTbox:
            break;
        default:
            Bug("Unknown event type");
        }
    }
}

#endif

/****************************************************
 *
 * Sets the transition of the scene and creates an undo object.
 *
 * Parameters:
 *	trans - Transition type.
 *
 * Returns:
 *  fTrue if successful, else fFalse.
 *
 ****************************************************/
MMScenePropertyServices Scene::_PropertyServices()
{
    MMScenePropertyServices s = {};
    s.context = this;
    s.data = &_owner.data;
    s.new_undo = [](void *, int kind) -> void * {
        return kind == MM_SCENE_CONTROL_NAME ? static_cast<void *>(SceneUndoTitle::PsuntNew())
                                                   : static_cast<void *>(SceneUndoTransition::PsunrNew());
    };
    s.title = [](void *, void *undo) { return static_cast<PSceneUndoTitle>(undo)->CoreState(); };
    s.transition = [](void *, void *undo) { return static_cast<PSceneUndoTransition>(undo)->CoreState(); };
    s.commit = [](void *p, void *undo) {
        return int(static_cast<Scene *>(p)->Pmvie()->FAddUndo(static_cast<MovieUndo *>(undo)));
    };
    s.release = [](void *, void *undo) { static_cast<BASE *>(undo)->Release(); };
    return s;
}

bool Scene::FSetTransition(TRANS trans)
{
    AssertThis(0);
    auto s = _PropertyServices();
    return mm_scene_transition_edit(trans, &s) != 0;
}

/****************************************************
 *
 * Sets the name of the scene and creates an undo object.
 *
 * Parameters:
 *	psz - Null terminated string.
 *
 * Returns:
 *  fTrue if successful, else fFalse.
 *
 ****************************************************/
void SceneUndoTitle::SetName(PString pstn)
{
    AssertPo(pstn, 0);
    AssertDo(mm_movie_title_set(&_title.name, pstn->Prgch(), pstn->Cch()), "bad Scene undo name");
}

bool Scene::FSetName(PString pstn)
{
    AssertThis(0);
    AssertPo(pstn, 0);
    MMMovieTitle name;
    mm_movie_title_init(&name, sizeof(achar));
    AssertDo(mm_movie_title_set(&name, pstn->Prgch(), pstn->Cch()), "bad Scene name");
    auto s = _PropertyServices();
    return mm_scene_name_edit(&name, &s) != 0;
}

MMSceneRuntimeServices Scene::_RuntimeServices()
{
    static_assert(fscenSounds == MM_SCENE_EFFECT_SOUNDS && fscenPauses == MM_SCENE_EFFECT_PAUSES &&
                      fscenActrs == MM_SCENE_EFFECT_ACTORS && fscenTboxes == MM_SCENE_EFFECT_TEXTS &&
                      fscenCams == MM_SCENE_EFFECT_CAMERAS,
                  "Scene effect flags");
    MMSceneRuntimeServices s = {};
    s.context = this;
    s.events = _EventServices();
    s.seek = [](void *, int text, void *object, MMI32 frame, int *sound) -> int {
        if (text)
            return static_cast<PTBOX>(object)->FGotoFrame(frame);
        bool value = sound ? *sound != 0 : false;
        bool result = static_cast<PActor>(object)->FGotoFrame(frame, sound ? &value : nullptr);
        if (sound)
            *sound = value;
        return result;
    };
    s.lifetime = [](void *, int text, void *object, MMI32 *first, MMI32 *last) -> int {
        long begin, end;
        bool result = text ? static_cast<PTBOX>(object)->FGetLifetime(&begin, &end)
                           : static_cast<PActor>(object)->FGetLifetime(&begin, &end);
        if (result)
        {
            *first = begin;
            *last = end;
        }
        return result;
    };
    s.replay_actor = [](void *, void *actor, MMU32 enabled) -> int {
        return static_cast<PActor>(actor)->FReplayFrame(enabled);
    };
    s.playing = [](void *p) -> int { return static_cast<PScene>(p)->Pmvie()->FPlaying(); };
    s.recording = [](void *p) -> int {
        auto view = static_cast<PMovieView>(static_cast<PScene>(p)->Pmvie()->PddgGet(0));
        return view && view->Tool() == toolRecordSameAction;
    };
    s.must_render = [](void *, void *actor, MMI32 until) -> int {
        return static_cast<PActor>(actor)->FMustRender(until);
    };
    s.prerendered = [](void *, void *actor) -> int { return static_cast<PActor>(actor)->FPrerendered(); };
    s.show = [](void *, void *actor, int visible) {
        if (visible)
            static_cast<PActor>(actor)->Show();
        else
            static_cast<PActor>(actor)->Hide();
    };
    s.set_prerendered = [](void *, void *actor, int value) { static_cast<PActor>(actor)->SetPrerendered(value != 0); };
    s.snapshot = [](void *p, int begin) {
        auto scene = static_cast<PScene>(p);
        auto movie = scene->Pmvie();
        auto world = movie->Pbwld();
        if (begin)
            world->Prerender(movie->MovieSession(), scene->PlaybackFrame());
        else
            world->Unprerender();
    };
    s.notice = [](void *p, int notice, int value) {
        auto scene = static_cast<PScene>(p);
        switch (notice)
        {
        case MM_SCENE_RESET_PAUSE:
            scene->Pmvie()->Pmcc()->PauseType(witNil);
            break;
        case MM_SCENE_RECORD_SOUND:
            scene->Pmvie()->Pmcc()->PlayUISound(toolRecordSameAction);
            break;
        case MM_SCENE_DIRTY:
            scene->_MarkMovieDirty();
            break;
        case MM_SCENE_INVALIDATE:
            scene->Pmvie()->InvalViews();
            break;
        case MM_SCENE_SOUND_INDICATOR:
            scene->Pmvie()->Pmcc()->SetSndFrame(value != 0);
            break;
        case MM_SCENE_GOTO_FAILURE:
            PushErc(ercSocGotoFrameFailure);
            break;
        case MM_SCENE_PRERENDER_ASSERT:
            Assert(false, "no actor should be marked prerendered here");
            break;
        }
    };
    return s;
}

/****************************************************
 *
 * This routine jumps to an arbitrary frame, updating actors, and adding
 * new frames to the scene if needed.
 *
 * Parameters:
 *	nFrm - The frame number to jump to.  This may be positive or negative
 *
 * Returns:
 *  fTrue if successful, else fFalse.
 *
 ****************************************************/


bool Scene::FGotoFrm(long nfrm)
{
    AssertThis(0);
    auto services = _RuntimeServices();
    services.movie = Pmvie()->MovieSession();
    return mm_scene_goto(nfrm, &services) != 0;
}

/****************************************************
 *
 * This function is an optimization which could be completely
 * skipped if desired.  The idea is to not render actors frame
 * after frame if they're not moving or changing at all.  So
 * when we hit a camera change, we look ahead to see if any
 * actors are unchanging before the next camera change.  If
 * there are, we hide the changing actors (so only the unchanging
 * ones are visible), then "take a snapshot" of the world with
 * the unchanging actors (via World::Prerender()), and use the
 * snapshot as the background RGB and Z buffer until the next
 * camera view change.  We only prerender if the movie is
 * playing.
 *
 * Parameters:
 *	fStartNow - if fTrue, start prerendering even if there's no
 *              camera change in this frame.  If fFalse, only
 *              start prerendering if there is a camera view
 *              change in this frame, or if this is the first
 *              frame of the scene.
 *
 * Returns:
 *  none
 *
 ****************************************************/
void Scene::_DoPrerenderingWork(bool fStartNow)
{
    AssertThis(0);
    auto services = _RuntimeServices();
    mm_scene_prerender(fStartNow, &services);
}

/****************************************************
 *
 * 	Ends any current prerendering by restoring the background
 *  RGB and Z buffers of the World, showing all previously
 *  hidden actors, and marking all actors as not prerendered.
 *
 * Parameters:
 *	none
 *
 * Returns:
 *  none
 *
 ****************************************************/
void Scene::_EndPrerendering(void)
{
    AssertThis(0);
    auto services = _RuntimeServices();
    mm_scene_end_prerender(&services);
}

/****************************************************
 *
 * This routine replays all the events, filtered by
 * grfscen, in the current frame.
 *
 * Parameters:
 *	grfscen - Events to play.
 *
 * Returns:
 *  fTrue if successful, else fFalse.
 *
 ****************************************************/
bool Scene::FReplayFrm(ulong grfscen)
{
    AssertThis(0);
    auto services = _RuntimeServices();
    return mm_scene_replay(grfscen, &services) != 0;
}

/****************************************************
 *
 * This routine recalculates the length of the movie.
 *
 * Parameters:
 *	None.
 *
 * Returns:
 *	None.
 *
 ****************************************************/
void Scene::InvalFrmRange(void)
{
    AssertThis(0);
    auto services = _RuntimeServices();
    mm_scene_extend_range(&services);
}

MMSceneEventServices Scene::_EventServices()
{
    static_assert(styMidi == MM_SCENE_EVENT_MIDI && transNil == MM_SCENE_EVENT_NIL_TRANSITION &&
                      transCut == MM_SCENE_EVENT_CUT_TRANSITION,
                  "Scene event constants");
    static_assert(sevtAddActr == MM_SCENE_ADD_ACTOR && sevtPlaySnd == MM_SCENE_PLAY_SOUND &&
                      sevtAddTbox == MM_SCENE_ADD_TEXT && sevtChngCamera == MM_SCENE_CHANGE_CAMERA &&
                      sevtSetBkgd == MM_SCENE_SET_BACKGROUND && sevtPause == MM_SCENE_PAUSE,
                  "Scene event kinds");
    MMSceneEventServices s = {};
    s.context = this;
    s.owner = &_owner;
    s.owners = _OwnerServices();
    s.duplicate_sound = [](void *, void *payload) -> void * {
        auto services = ScenePacketServices();
        return mm_scene_sound_duplicate(static_cast<MMSceneSound *>(payload), &services);
    };
    s.play_sound = [](void *p, void *payload) {
        auto services = ScenePacketServices(static_cast<PScene>(p)->Pmvie());
        mm_scene_sound_play(static_cast<MMSceneSound *>(payload), 0, &services);
    };
    s.fetch_background = [](void *, MMTag *value) -> void * {
        TAG tag;
        TagFromCore(&tag, *value);
        auto background = vptagm->PbacoFetch(&tag, Background::FReadBkgd);
        *value = {tag.sid, tag.pcrf, tag.ctg, tag.cno};
        return background;
    };
    s.set_camera = [](void *p, void *background, MMI32 camera) -> int {
        return static_cast<PBackground>(background)->FSetCamera(static_cast<PScene>(p)->Pmvie()->Pbwld(), camera);
    };
    s.transition = [](void *p) -> int { return static_cast<PScene>(p)->Pmvie()->Trans(); };
    s.set_transition = [](void *p, int value) { static_cast<PScene>(p)->Pmvie()->SetTrans(static_cast<TRANS>(value)); };
    s.bind_actor = [](void *p, void *actor) { static_cast<PActor>(actor)->SetPscen(static_cast<PScene>(p)); };
    s.seek = [](void *, int text, void *object, MMI32 frame) -> int {
        return text ? static_cast<PTBOX>(object)->FGotoFrame(frame) : static_cast<PActor>(object)->FGotoFrame(frame);
    };
    s.pause_type = [](void *p, int reason) {
        static_cast<PScene>(p)->Pmvie()->Pmcc()->PauseType(static_cast<WaitReason>(reason));
    };
    s.pause = [](void *p, int reason, MMI32 delay) {
        static_cast<PScene>(p)->Pmvie()->DoPause(static_cast<WaitReason>(reason), delay);
    };
    s.diagnostic = [](void *, int issue) {
        switch (issue)
        {
        case MM_SCENE_EVENT_NO_BACKGROUND:
            Assert(false, "No background in the scene");
            break;
        case MM_SCENE_EVENT_UNKNOWN:
            Bug("Unhandled sevt");
            break;
        case MM_SCENE_EVENT_RESTORE_FAILED:
            Assert(false, "Should not fail.");
            break;
        case MM_SCENE_EVENT_FIRST_CAMERA_FAILED:
            Assert(false, "Should never happen");
            break;
        case MM_SCENE_EVENT_OWNER_OCCUPIED:
            Assert(false, "Scene owner slot must be empty");
            break;
        }
    };
    return s;
}

/****************************************************
 *
 * This routine moves back stuff that is currently
 * in the first frame of the movie to the given frame.
 *
 * Parameters:
 *	nfrm - New first frame.
 *
 * Returns:
 *	None.
 *
 ****************************************************/
void Scene::_MoveBackFirstFrame(long nfrm)
{
    AssertThis(0);
    Assert(nfrm < _owner.data.firstFrame, "Can only be called to extend scene back.");
    mm_scene_move_first(&_owner, nfrm);
}

static MMMovieSoundServices BindSceneMovieSound(void *p)
{
    return static_cast<PScene>(p)->Pmvie()->SoundServices();
}

MMSceneSoundEditServices Scene::_SoundEditServices()
{
    MMSceneSoundEditServices s = {};
    s.context = this;
    s.events = _EventServices();
    s.values = SceneSoundValues(Pmvie());
    s.allocate_records = [](void *, MMI32 bytes) -> void * {
        void *p = nullptr;
        return FAllocPv(&p, bytes, fmemClear, mprNormal) ? p : nullptr;
    };
    s.free_raw = [](void *, void *p) { FreePpv(&p); };
    s.movie_sound = BindSceneMovieSound;
    s.unused_child = [](void *) -> MMU32 {
        ChildChunkID value = 0;
        TrashVar(&value);
        return value;
    };
    s.fetch_input = [](void *, const MMTag *value) -> void * {
        TAG tag;
        TagFromCore(&tag, *value);
        return vptagm->PbacoFetch(&tag, MovieSoundMSND::FReadMsnd);
    };
    s.create_tag_list = [](void *, MMI32 count) -> void * { return DynamicArray::PglNew(size(TAG), count); };
    s.resize_tag_list = [](void *, void *list, MMI32 count) -> int {
        return static_cast<PDynamicArray>(list)->FSetIvMac(count);
    };
    s.put_list_tag = [](void *, void *list, MMI32 index, const MMTag *value) {
        TAG tag;
        TagFromCore(&tag, *value);
        static_cast<PDynamicArray>(list)->Put(index, &tag);
    };
    s.release_list = [](void *, void *list) {
        auto p = static_cast<PDynamicArray>(list);
        ReleasePpo(&p);
    };
    s.actor_count = [](void *p) -> MMI32 { return static_cast<PScene>(p)->Cactr(); };
    s.actor_sound = [](void *p, MMI32 index) -> int {
        auto actor = static_cast<PScene>(p)->PactrAt(index);
        AssertPo(actor, 0);
        return actor->FSoundInFrm();
    };
    s.dirty = [](void *p) { static_cast<PScene>(p)->_MarkMovieDirty(); };
    s.sound_frame = [](void *p, int value) { static_cast<PScene>(p)->Pmvie()->Pmcc()->SetSndFrame(value != 0); };
    s.new_undo = [](void *) -> void * { return SceneUndoSound::PsunsNew(); };
    s.undo_state = [](void *, void *undo) { return static_cast<PSceneUndoSound>(undo)->CoreState(); };
    s.commit_undo = [](void *p, void *undo) -> int {
        return static_cast<PScene>(p)->Pmvie()->FAddUndo(static_cast<PSceneUndoSound>(undo));
    };
    s.release_undo = [](void *, void *undo) {
        auto p = static_cast<PSceneUndoSound>(undo);
        ReleasePpo(&p);
    };
    s.clear_undo = [](void *p) { static_cast<PScene>(p)->Pmvie()->ClearUndo(); };
    s.diagnostic = [](void *, int issue) {
        switch (issue)
        {
        case MM_SCENE_SOUND_STYLE_MISSING:
            Bug("sty not found");
            break;
        case MM_SCENE_SOUND_MISSING:
            Bug("No such sound");
            break;
        case MM_SCENE_SOUND_QUEUE_RESTORE:
            Bug("Should never queue when undoing");
            break;
        case MM_SCENE_SOUND_OFFSET_OVERFLOW:
            Assert(false, "overflow in background sound offset");
            break;
        case MM_SCENE_SOUND_LIST_RESIZE:
            Assert(false, "PglNew should have ensured space");
            break;
        }
    };
    return s;
}
MMSceneSoundValueServices SceneUndoSound::_Values()
{
    return SceneSoundValues();
}
MMSceneSoundUndoServices SceneUndoSound::_Services()
{
    MMSceneSoundUndoServices s = {};
    s.context = this;
    s.values = SceneSoundValues(Pmvie());
    s.switch_scene = [](void *p) -> int {
        auto u = static_cast<PSceneUndoSound>(p);
        return u->Pmvie()->FSwitchScen(u->Iscen());
    };
    s.seek_frame = [](void *p) -> int {
        auto u = static_cast<PSceneUndoSound>(p);
        return u->Pmvie()->Pscen()->FGotoFrm(u->Nfrm());
    };
    s.flush = [](void *p) { static_cast<PSceneUndoSound>(p)->Pmvie()->Pmsq()->FlushMsq(); };
    s.play = [](void *p) { static_cast<PSceneUndoSound>(p)->Pmvie()->Pmsq()->PlayMsq(); };
    s.clear = [](void *p) { static_cast<PSceneUndoSound>(p)->Pmvie()->ClearUndo(); };
    s.get = [](void *p, MMI32 style, int *found, MMSceneSound **out) -> int {
        auto u = static_cast<PSceneUndoSound>(p);
        bool hit;
        PSceneSoundEvent sound = nullptr;
        bool ok = u->Pmvie()->Pscen()->FGetSnd(style, &hit, &sound);
        *found = hit;
        if (!ok || hit)
            *out = sound;
        return ok;
    };
    s.remove = [](void *p, MMI32 style) { static_cast<PSceneUndoSound>(p)->Pmvie()->Pscen()->RemSndCore(style); };
    s.restore = [](void *p, const MMSceneSound *sound) -> int {
        auto u = static_cast<PSceneUndoSound>(p);
        // Undo owns a separately allocated packet with aligned records.
        auto tags = reinterpret_cast<const MMSceneSoundTag *>(sound + 1);
        return u->Pmvie()->Pscen()->FAddSndCoreTagc(sound->fLoop != 0, false, sound->vlm, sound->sty, sound->ctagc,
                                                   tags);
    };
    s.diagnostic = [](void *, int) { Bug("where's the sound event?"); };
    return s;
}

/****************************************************
 *
 * This routine adds a sound to the event list for this frame.
 * Note: The chid's in the tagc are current
 *
 * Parameters:
 *  fLoop - should the sound loop?
 *  fQueue - queue after existing sounds, or replace them?
 *  vlm - volume to play this sound at
 *  sty - sound type (midi, speech, or SFX)
 *  ctag - number of sounds
 *  prgtagc - array of MovieSoundMSND tags
 *
 *
 * Returns:
 *	fTrue, if successful, else fFalse.
 *
 ****************************************************/
bool Scene::FAddSndCoreTagc(bool fLoop, bool fQueue, long vlm, long sty, long ctagc, const MMSceneSoundTag *prgtagc)
{
    AssertThis(0);
    AssertPvCb(const_cast<MMSceneSoundTag *>(prgtagc), LwMul(ctagc, size(MMSceneSoundTag)));
    Assert(!fQueue || ctagc == 1, "if queueing, add only one sound");
    Assert(!fQueue || !fLoop, "can't both queue and loop");
    AssertIn(sty, 0, styLim);
    auto services = _SoundEditServices();
    return mm_scene_sound_restore(fLoop, fQueue, vlm, sty, ctagc, prgtagc, &services) != 0;
}

/****************************************************
 *
 * This routine removes a sound from the event list for this frame.
 *
 * Parameters:
 *	sty - the type of sound to remove
 *
 *
 * Returns:
 *  None
 *
 ****************************************************/
void Scene::RemSndCore(long sty)
{
    AssertThis(0);
    AssertIn(sty, 0, styLim);
    auto services = _SoundEditServices();
    mm_scene_sound_remove(sty, &services);
}

/****************************************************
 *
 * This routine removes a sound from the event list for this frame
 * and creates an undo object for the action.
 *
 * Parameters:
 *	sty - the type of sound to remove
 *
 *
 * Returns:
 *  fTrue if successful, else fFalse.
 *
 ****************************************************/
bool Scene::FRemSnd(long sty)
{
    AssertThis(0);
    AssertIn(sty, 0, styLim);
    auto services = _SoundEditServices();
    return mm_scene_sound_remove_undo(sty, &services) != 0;
}

/****************************************************
 *
 * This routine finds a specific sound in the current frame.
 *
 * Parameters:
 *	sty - sound type to search for
 *  pfFound - set to fTrue if a sound is found, else fFalse
 *  ppsse - gets a pointer to the SceneSoundEvent if it is found
 *
 * Returns:
 *  fTrue if successful, else fFalse.
 *
 ****************************************************/
bool Scene::FGetSnd(long sty, bool *pfFound, PSceneSoundEvent *ppsse)
{
    AssertThis(0);
    AssertIn(sty, 0, styLim);
    AssertVarMem(ppsse);
    *pfFound = false;
    auto services = _SoundEditServices();
    int found;
    MMSceneSound *sound = nullptr;
    bool ok = mm_scene_sound_get(sty, &found, &sound, &services) != 0;
    if (!ok || found)
        *ppsse = sound;
    *pfFound = found != 0;
    return ok;
}

/****************************************************
 *
 * This routine plays the background sound, if any.
 * If the sound really started at an earlier frame,
 * this function tries to resume the sound at the
 * appropriate offset into the sound (dtsStart).
 *
 * Parameters:
 *  none
 *
 * Returns:
 *  none
 *
 ****************************************************/
void Scene::PlayBkgdSnd(void)
{
    AssertThis(0);
    static_assert(kdtsSecond == 1000 && kfps == 6, "Scene background clock");
    auto services = _SoundEditServices();
    mm_scene_sound_background_play(&services);
}

/****************************************************
 *
 * This routine queries what sounds are attched to the
 * scene at the current frame
 *
 * Parameters:
 *  sty - the sound type to query
 *
 * Returns:
 *  fFalse if an error occurs
 *
 ****************************************************/
bool Scene::FQuerySnd(long sty, PDynamicArray *ppgltagSnd, long *pvlm, bool *pfLoop)
{
    AssertThis(0);
    AssertVarMem(ppgltagSnd);
    AssertVarMem(pvlm);
    AssertVarMem(pfLoop);
    auto services = _SoundEditServices();
    struct Output
    {
        PDynamicArray *list;
        long *volume;
        bool *loop;
    } output = {ppgltagSnd, pvlm, pfLoop};
    services.query_context = &output;
    services.publish_query = [](void *p, void *list, int metadata, MMI32 volume, int loop) {
        auto out = static_cast<Output *>(p);
        *out->list = static_cast<PDynamicArray>(list);
        if (metadata)
        {
            *out->volume = volume;
            *out->loop = loop != 0;
        }
    };
    void *list = nullptr;
    MMI32 volume = 0;
    int loop = 0;
    return mm_scene_sound_query(sty, &list, &volume, &loop, &services) != 0;
}

/****************************************************
 *
 * This routine changes the volume of the sound of
 * type sty at the current frame to vlmNew
 *
 * Parameters:
 *  sty - the sound type to query
 *  vlmNew - the new volume
 *
 * Returns:
 *  none
 *
 ****************************************************/
void Scene::SetSndVlmCore(long sty, long vlmNew)
{
    AssertThis(0);
    AssertIn(sty, 0, styLim);
    AssertIn(vlmNew, 0, kvlmFull + 1);
    auto services = _SoundEditServices();
    mm_scene_sound_volume(sty, vlmNew, &services);
}

/******************************************************************************
    UpdateSndFrame
        Enumerates all scene events for the current frame, and asks all actors
        to enumerate all of their actor events for the current frame, looking
        for a sound event.  Has the movie's MovieClientCallbacks update the frame-sound state
        based on the results of the search.
************************************************************ PETED ***********/
void Scene::UpdateSndFrame(void)
{
    auto services = _SoundEditServices();
    mm_scene_sound_update_indicator(&services);
}

/****************************************************
 *
 * This routine adds an event to the current frame of the current scene.
 *
 * Parameters:
 *  psev - Pointer to the event to add.
 *	cbVar- Size of pvVar buffer, in bytes.
 *  pvVar- Pointer to the variable part of the event.
 *
 * Returns:
 *	fTrue, if successful, else fFalse.
 *
 ****************************************************/
bool Scene::_FAddSev(PSEV psev, long cbVar, void *pvVar)
{
    AssertThis(0);
    AssertVarMem(psev);
    AssertIn(cbVar, 0, klwMax);
    auto services = _SoundEditServices();
    return mm_scene_edit_insert(psev->nfrm, psev->sevt, cbVar, pvVar, &services) != 0;
}

/****************************************************
 *
 * This routine sets the selected actor to the given one.
 *
 * Parameters:
 *  pactr - Pointer to the actr to select.  pvNil is a
 *  	valid value and deselects the current actor and tbox.
 *
 * Returns:
 *	None
 *
 ****************************************************/
MMSceneSelectionServices Scene::_SelectionServices()
{
    MMSceneSelectionServices s = {};
    s.context = this;
    s.owner = &_owner;
    s.view_mode = [](void *p) {
        auto view = static_cast<PMovieView>(static_cast<Scene *>(p)->Pmvie()->PddgGet(0));
        return view ? int(view->FTextMode()) : -1;
    };
    s.hilite = [](void *, void *object, int selected) {
        if (selected)
            static_cast<PActor>(object)->Hilite();
        else
            static_cast<PActor>(object)->Unhilite();
    };
    s.text_selected = [](void *, void *object) { return int(static_cast<PTBOX>(object)->FSelected()); };
    s.select_text = [](void *, void *object, int selected) { static_cast<PTBOX>(object)->Select(selected != 0); };
    s.invalidate = [](void *p) { static_cast<Scene *>(p)->Pmvie()->InvalViews(); };
    s.action_menu = [](void *p) { static_cast<Scene *>(p)->Pmvie()->BuildActionMenu(); };
    s.text_notice = [](void *p) { static_cast<Scene *>(p)->Pmvie()->Pmcc()->TboxSelected(); };
    s.name = [](void *, void *object, MMMovieTitle *out) {
        String name;
        static_cast<PActor>(object)->GetName(&name);
        mm_movie_title_init(out, sizeof(achar));
        mm_movie_title_set(out, name.Prgch(), name.Cch());
    };
    s.visible_position = [](void *, void *object, MMActorPoint *out) {
        auto actor = static_cast<PActor>(object);
        if (!actor->Pbody() || !actor->FIsInView())
            return 0;
        BRS x, y, z;
        actor->Pbody()->GetPosition(&x, &y, &z);
        *out = {x, y, z};
        return 1;
    };
    return s;
}

void Scene::SelectActr(Actor *pactr)
{
    AssertThis(0);
    AssertNilOrPo(pactr, 0);
    auto s = _SelectionServices();
    mm_scene_select_actor(pactr, &s);
}

/****************************************************
 *
 * Clears the entire selection (primary and extras).
 *
 ****************************************************/
void Scene::ClearSelection(void)
{
    AssertThis(0);
    SelectActr(pvNil);
}

/****************************************************
 *
 * Selects every actor in the current scene's actor list.
 * The first actor becomes the primary; the rest are added
 * as extras. Empty scene clears any existing selection.
 *
 * Returns fFalse if a mid-add allocation fails (selection
 * may end up partial in that case).
 *
 ****************************************************/
bool Scene::FSelectAllActrs(void)
{
    AssertThis(0);
    auto s = _SelectionServices();
    return mm_scene_selection_all(&s) != 0;
}

/****************************************************
 *
 * Select pactrSeed and every other scene actor that shares any #tag
 * with it. If pactrSeed has no tags, this is just SelectActr(pactrSeed).
 *
 ****************************************************/
bool Scene::FSelectActrsSharingTagsWith(PActor pactrSeed)
{
    AssertThis(0);
    AssertPo(pactrSeed, 0);
    auto s = _SelectionServices();
    return mm_scene_selection_shared(pactrSeed, &s) != 0;
}

/****************************************************
 *
 * Replace the current selection with every actor in the current scene
 * whose display name contains '#pszTag' as a whole tag token.
 *
 ****************************************************/
bool Scene::FSelectActrsByTag(PZString pszTag)
{
    AssertThis(0);
    auto s = _SelectionServices();
    return mm_scene_selection_tag(pszTag, CchSz(pszTag), sizeof(achar) == 2, &s) != 0;
}

void Scene::EditActorSelectionTags(bool strip)
{
    AssertThis(0);
    const auto selection = _SelectionServices();
    MMSceneTagEditServices edit{};
    edit.context = Pmvie();
    edit.create_undo = [](void *, void **handle) -> MMActorRenameGroup * {
        auto undo = ActorRenameGroupUndo::PargNew();
        *handle = undo;
        return undo ? &undo->_group : nullptr;
    };
    edit.actor_id = [](void *, void *actor) -> MMI32 { return static_cast<PActor>(actor)->Arid(); };
    edit.rename = [](void *movie, MMI32 id, const void *text, MMU32 count) {
        return int(static_cast<PMovie>(movie)->FNameActr(id, static_cast<const achar *>(text), count));
    };
    edit.commit_undo = [](void *movie, void *undo) {
        return int(static_cast<PMovie>(movie)->FAddUndo(static_cast<PActorRenameGroupUndo>(undo)));
    };
    edit.release_undo = [](void *, void *undo) { static_cast<PActorRenameGroupUndo>(undo)->Release(); };
    edit.clear_undo = [](void *movie) { static_cast<PMovie>(movie)->ClearUndo(); };
    edit.not_undoable = [](void *) { PushErc(ercSocNotUndoable); };
    edit.dirty = [](void *movie) { static_cast<PMovie>(movie)->SetDirty(); };
    mm_scene_selection_edit_tags(strip, &selection, &edit);
}

void Scene::CaptureActorDragUndo(MMActorDragUndo *drag, PActor actor, bool useGroup, bool freezePivot)
{
    AssertThis(0);
    const auto selection = _SelectionServices();
    const auto actors = Actor::_OwnerServices();
    const auto undo = ActorMoveGroupUndo::DragUndoServices(Pmvie());
    mm_actor_drag_undo_begin(drag, actor, freezePivot, useGroup ? &selection : nullptr, &actors, &undo);
}

MMActorGroupTransformServices Scene::_GroupTransformServices()
{
    return {
        nullptr,
        [](void *, void *actor) { return &static_cast<PActor>(actor)->_owner; },
        [](void *, void *actor, MMActorTransformEditServices *effects) {
            *effects = static_cast<PActor>(actor)->_TransformEditServices();
        },
        [](void *, void *body, MMActorPoint *point) {
            BRS x, y, z;
            static_cast<PBody>(body)->GetPosition(&x, &y, &z);
            *point = {x, y, z};
        }};
}

bool Scene::FTransformSelectedActors(const MMActorGroupTransform &edit)
{
    AssertThis(0);
    const auto selection = _SelectionServices();
    const auto services = _GroupTransformServices();
    return mm_actor_group_transform(&edit, &selection, &services) != 0;
}

MMActorPlacementEditServices Scene::_PlacementServices()
{
    MMActorPlacementEditServices services{};
    services.context = Pmvie();
    services.remove_roll = [](void *p, void *actor) {
        static_cast<PMovie>(p)->RemFromRollCall(static_cast<PActor>(actor), false);
    };
    services.add_roll = [](void *p, void *actor) {
        AssertDo(static_cast<PMovie>(p)->FAddToRollCall(static_cast<PActor>(actor), nullptr),
                 "Cannot restore actor roll-call reference");
    };
    services.visible = [](void *, void *actor) { return int(static_cast<PActor>(actor)->FIsInView()); };
    services.out_of_view = [](void *, MMI32 id) {
        vpcex->EnqueueCid(cidActorPlacedOutOfView, pvNil, pvNil, id);
    };
    services.enable_tools = [](void *movie) { static_cast<PMovie>(movie)->Pmcc()->EnableActorTools(); };
    return services;
}

void Scene::BeginActorPlacement(MMActorPlacementState *state, PActor actor, bool entireScene)
{
    const auto actors = Actor::_OwnerServices();
    const auto edit = _PlacementServices();
    mm_actor_placement_begin(state, actor, entireScene, &actors, &edit);
}

void Scene::EndActorPlacement(MMActorPlacementState *state)
{
    const auto actors = Actor::_OwnerServices();
    const auto edit = _PlacementServices();
    mm_actor_placement_end(state, &actors, &edit);
}

bool Scene::FMoveActorPlacement(const MMActorPlacementState &state, const MMActorPoint &delta, bool ground)
{
    auto actor = static_cast<PActor>(state.actor);
    AssertPo(actor, 0);
    const auto actors = Actor::_OwnerServices();
    const auto effects = actor->_TransformEditServices();
    return mm_actor_placement_move(&state, &delta, ground, &actors, &effects) != 0;
}

bool Scene::FFinishActorPlacement(MMActorPlacementState *state)
{
    const auto actors = Actor::_OwnerServices();
    const auto content = _ContentServices();
    const auto edit = _PlacementServices();
    return mm_actor_placement_finish(state, &actors, &content, &edit) != 0;
}

/****************************************************
 *
 * Mean of currently-visible selected actors' world positions.
 * Group rotate / scale tools freeze this at mousedown as the
 * pivot. Skips actors that aren't FIsInView() at the current
 * frame so a stale offstage position can't drag the pivot
 * away from what the user sees.
 *
 * Returns fFalse (and leaves outputs untouched) if no selected
 * actor is currently visible.
 *
 ****************************************************/
bool Scene::FXyzSelectionCentroid(BRS *pxr, BRS *pyr, BRS *pzr)
{
    AssertThis(0);
    AssertVarMem(pxr);
    AssertVarMem(pyr);
    AssertVarMem(pzr);
    auto s = _SelectionServices();
    MMActorPoint point;
    if (!mm_scene_selection_centroid(&point, &s))
        return fFalse;
    *pxr = point.dxr;
    *pyr = point.dyr;
    *pzr = point.dzr;
    return fTrue;
}

/****************************************************
 *
 * Returns the total number of selected actors:
 * 0 if primary is pvNil, else 1 + extras count.
 *
 ****************************************************/
long Scene::CactrSelected(void)
{
    AssertThis(0);
    auto s = _SelectionServices();
    return mm_scene_selection_count(&s);
}

/****************************************************
 *
 * Returns the iactr-th selected actor: 0 = primary,
 * 1..N = extras in insertion order.
 *
 ****************************************************/
PActor Scene::PactrSelectedAt(long iactr)
{
    AssertThis(0);
    AssertIn(iactr, 0, CactrSelected());
    auto s = _SelectionServices();
    return static_cast<PActor>(mm_scene_selection_at(iactr, &s));
}

/****************************************************
 *
 * Returns fTrue if pactr is the primary or in the extras list.
 *
 ****************************************************/
bool Scene::FIsActrSelected(PActor pactr)
{
    AssertThis(0);
    AssertNilOrPo(pactr, 0);
    auto s = _SelectionServices();
    return mm_scene_selection_contains(pactr, &s) != 0;
}

/****************************************************
 *
 * Shift-click entry point. Toggles pactr's membership in the
 * selection set: adds it if absent, removes it if present.
 * Returns fFalse on allocation failure.
 *
 ****************************************************/
bool Scene::FToggleActrSelected(PActor pactr)
{
    AssertThis(0);
    AssertPo(pactr, 0);
    auto s = _SelectionServices();
    return mm_scene_selection_toggle(pactr, &s) != 0;
}

/****************************************************
 *
 * This routine sets the selected text box to the given one.
 *
 * Parameters:
 *  ptbox - Pointer to the tbox to select.  pvNil is a
 *  	valid value and deselects the current actor and tbox.
 *
 * Returns:
 *	None
 *
 ****************************************************/
void Scene::SelectTbox(PTBOX ptbox)
{
    AssertThis(0);
    AssertNilOrPo(ptbox, 0);
    auto s = _SelectionServices();
    mm_scene_select_text(ptbox, &s);
}

/****************************************************
 *
 * This routine adds an actor to the scene at the current frame,
 * if the arid is aridNil, else it replaces the actor in the
 * scene with the same arid.
 *
 * Parameters:
 *  pactr - Pointer to the actr to add.
 *
 *
 * Returns:
 *	fTrue, if successful, else fFalse.
 *
 ****************************************************/
MMSceneContentServices Scene::_ContentServices()
{
    MMSceneContentServices s = {};
    s.context = this;
    s.events = _EventServices();
    s.selection = _SelectionServices();
    s.actors = Actor::_OwnerServices();
    s.retain = [](void *, void *p) { static_cast<BASE *>(p)->AddRef(); };
    s.actor_id = [](void *, void *p) -> MMI32 { return static_cast<PActor>(p)->Arid(); };
    s.set_actor_id = [](void *, void *p, MMI32 id) { static_cast<PActor>(p)->SetArid(id); };
    s.actor_ready = [](void *, void *p) -> int { return static_cast<PActor>(p)->Pbody() != nullptr; };
    s.stage = Actor::_BindStageEdit;
    s.actor_paste = [](void *p) { return static_cast<PActor>(p)->_PasteServices(); };
    s.hide_actor = [](void *, void *p) {
        auto actor = static_cast<PActor>(p);
        if (actor->Pbody())
            actor->Hide();
    };
    s.end_placement = [](void *p) {
        auto view = static_cast<PMovieView>(static_cast<PScene>(p)->Pmvie()->PddgGet(0));
        if (view)
            view->EndPlaceActor();
    };
    s.add_roll = [](void *p, void *object, int templateName) -> int {
        auto a = static_cast<PActor>(object);
        String name;
        if (templateName)
            a->Ptmpl()->GetName(&name);
        else
            a->GetName(&name);
        return static_cast<PScene>(p)->Pmvie()->FAddToRollCall(a, &name);
    };
    s.remove_roll = [](void *p, void *a, int only) {
        static_cast<PScene>(p)->Pmvie()->RemFromRollCall(static_cast<PActor>(a), only != 0);
    };
    s.dirty = [](void *p) { static_cast<PScene>(p)->_MarkMovieDirty(); };
    s.invalidate = [](void *p) { static_cast<PScene>(p)->InvalFrmRange(); };
    s.indicator = [](void *p) { static_cast<PScene>(p)->UpdateSndFrame(); };
    s.update_roll = [](void *p) { static_cast<PScene>(p)->Pmvie()->Pmcc()->UpdateRollCall(); };
    s.bind_text = [](void *p, void *text) { static_cast<PTBOX>(text)->SetScen(static_cast<PScene>(p)); };
    s.text_lifetime = [](void *, void *text, MMI32 *first, MMI32 *last) -> int {
        long a, b;
        bool ok = static_cast<PTBOX>(text)->FGetLifetime(&a, &b);
        if (ok)
        {
            *first = a;
            *last = b;
        }
        return ok;
    };
    s.hide_text = [](void *, void *text) { static_cast<PTBOX>(text)->HideCore(); };
    s.new_undo = [](void *, int text) -> void * {
        return text ? static_cast<void *>(SceneUndoText::PsunxNew()) : static_cast<void *>(SceneActorUndo::PsunaNew());
    };
    s.actor_state = [](void *, void *u) { return static_cast<PSceneActorUndo>(u)->CoreState(); };
    s.text_state = [](void *, void *u) { return static_cast<PSceneUndoText>(u)->CoreState(); };
    s.commit_undo = [](void *p, void *u) -> int {
        return static_cast<PScene>(p)->Pmvie()->FAddUndo(static_cast<PMovieUndo>(u));
    };
    s.release_undo = [](void *, void *u) { static_cast<PMovieUndo>(u)->Release(); };
    s.clear_undo = [](void *p) { static_cast<PScene>(p)->Pmvie()->ClearUndo(); };
    s.not_undoable = [](void *) { PushErc(ercSocNotUndoable); };
    s.diagnostic = [](void *, int issue) {
        switch (issue)
        {
        case MM_SCENE_CONTENT_ACTOR_MISSING:
            Bug("Actor does not exist in scene");
            break;
        case MM_SCENE_CONTENT_ACTOR_ROLL_MISSING:
            Bug("Actor does not exist in roll call");
            break;
        case MM_SCENE_CONTENT_TEXT_MISSING:
            Bug("Could not find text box");
            break;
        case MM_SCENE_CONTENT_TEXT_ROLL_MISSING:
            Bug("Text box not found in DynamicArray");
            break;
        case MM_SCENE_CONTENT_TEXT_HIDE_FAILED:
            Bug("Could not hide text box");
            break;
        default:
            Bug("Invalid scene content state");
            break;
        }
    };
    return s;
}
bool Scene::FAddActrCore(Actor *actor)
{
    AssertThis(0);
    auto s = _ContentServices();
    return mm_scene_actor_add(actor, &s) != 0;
}

/****************************************************
 *
 * This routine adds an actor to the scene at the current frame.
 * This auto magically selects the actor and places them on stage.
 *
 * Parameters:
 *  pactr - Pointer to the actr to add.
 *
 *
 * Returns:
 *	fTrue, if successful, else fFalse.
 *
 ****************************************************/
bool Scene::FAddActr(Actor *actor)
{
    AssertThis(0);
    auto s = _ContentServices();
    return mm_scene_actor_add_onstage(actor, &s) != 0;
}

/****************************************************
 *
 * This routine removes an actor from the scene.
 *
 * Parameters:
 *  arid - The actor id to remove.
 *
 *
 * Returns:
 *  None
 *
 ****************************************************/
void Scene::RemActrCore(long id)
{
    AssertThis(0);
    auto s = _ContentServices();
    mm_scene_actor_remove(id, &s);
}

/****************************************************
 *
 * This routine removes an actor from the scene, and
 * creates an undo object for the action.
 *
 * Parameters:
 *  arid - The actor id to remove.
 *
 *
 * Returns:
 *  fTrue if successful, else fFalse.
 *
 ****************************************************/
bool Scene::FRemActr(long id)
{
    AssertThis(0);
    auto s = _ContentServices();
    return mm_scene_actor_remove_undo(id, &s) != 0;
}

/****************************************************
 *
 * This routine returns the actor pointed at by the mouse.
 *
 * Parameters:
 *	xp - X position of the mouse within the display space.
 *	yp - Y position of the mouse within the display space.
 *  pibset - Place to store the index of the group of body parts hit.
 *
 * Returns:
 *  Pointer to the actor, pvNil if none.
 *
 ****************************************************/
Actor *Scene::PactrFromPt(long xp, long yp, long *pibset)
{
    AssertThis(0);
    AssertVarMem(pibset);
    struct Host
    {
        Scene *scene;
        long *part;
    } host = {this, pibset};
    const MMScenePickServices services = {
        &host,
        [](void *p, MMI32 x, MMI32 y) -> void * {
            auto h = static_cast<Host *>(p);
            return BodyWorld::PbodyClicked(x, y, h->scene->Pmvie()->Pbwld(), h->part);
        },
        [](void *p) -> MMI32 { return static_cast<Host *>(p)->scene->Cactr(); },
        [](void *p, MMI32 index) -> void * { return static_cast<Host *>(p)->scene->PactrAt(index); },
        [](void *, void *actor, void *body) {
            return int(static_cast<PActor>(actor)->FIsMyBody(static_cast<Body *>(body)));
        },
        [](void *) { Bug("weird...we clicked a pbody, but we don't know whose it is!"); }};
    return static_cast<Actor *>(mm_scene_pick_actor(xp, yp, &services));
}

/****************************************************
 *
 * This routine adds a text box to the scene at the current frame
 *
 * Parameters:
 *  ptbox - Pointer to the text box to add.
 *
 *
 * Returns:
 *  fTrue if successful, else fFalse.
 *
 ****************************************************/
bool Scene::FAddTboxCore(PTBOX text)
{
    AssertThis(0);
    auto s = _ContentServices();
    return mm_scene_text_add(text, &s) != 0;
}

/****************************************************
 *
 * This routine adds a text box to the scene at the current frame
 * creates an undo object for the action.
 *
 * Parameters:
 *  ptbox - Pointer to the text box to add.
 *
 *
 * Returns:
 *  fTrue if successful, else fFalse.
 *
 ****************************************************/
bool Scene::FAddTbox(PTBOX text)
{
    AssertThis(0);
    auto s = _ContentServices();
    return mm_scene_text_add_undo(text, &s) != 0;
}

/****************************************************
 *
 * This routine removes a text box from the scene.
 *
 * Parameters:
 *  ptbox - Pointer to the text box to remove.
 *
 *
 * Returns:
 *  fTrue if successful, else fFalse.
 *
 ****************************************************/
bool Scene::FRemTboxCore(PTBOX text)
{
    AssertThis(0);
    auto s = _ContentServices();
    return mm_scene_text_remove(text, &s) != 0;
}

/****************************************************
 *
 * This routine removes a text box from the scene,
 * creates an undo object for the action.
 *
 * Parameters:
 *  ptbox - Pointer to the text box to remove.
 *
 *
 * Returns:
 *  fTrue if successful, else fFalse.
 *
 ****************************************************/
bool Scene::FRemTbox(PTBOX text)
{
    AssertThis(0);
    auto s = _ContentServices();
    return mm_scene_text_remove_undo(text, &s) != 0;
}

/****************************************************
 *
 * This routine gets the ith text box.
 *
 * Parameters:
 *  itbox - Index of the text box to get.
 *
 * Returns:
 *  Pointer to the text box if itbox is valid, else pvNil.
 *
 ****************************************************/
TextBox *Scene::PtboxFromItbox(long i)
{
    AssertThis(0);
    auto s = _ContentServices();
    return static_cast<PTBOX>(mm_scene_text_at(i, &s));
}

/****************************************************
 *
 * This routine adds/removes a pause to the scene at the current frame.
 *
 * Parameters:
 *	pwit - The pause type, witNil removes a pause, returns old pause type.
 *  pdts - Valid only if wit==witForTime, dts is in clock ticks, returns
 *		old wait time if old wit was witForTime.
 *
 * Returns:
 *  fTrue if successful, else fFalse.
 *
 ****************************************************/
static MMTag SceneEditTag(const TAG &t)
{
    return {t.sid, t.pcrf, t.ctg, t.cno};
}
static void SceneEditPublishTag(void *p, const MMTag *tag, int first)
{
    auto out = static_cast<PTAG>(p);
    TagFromCore(out, *tag);
    if (first)
        TrashVar(out);
}
MMSceneEditServices Scene::_EditServices()
{
    MMSceneEditServices s = {};
    s.context = this;
    s.sound = _SoundEditServices();
    s.background_name = [](void *p) {
        auto scene = static_cast<PScene>(p);
        String name;
        scene->Pbkgd()->GetName(&name);
        scene->SetNameCore(&name);
    };
    s.scene_change = [](void *p) { static_cast<PScene>(p)->Pmvie()->Pmcc()->SceneChange(); };
    s.can_notify = [](void *) -> int { return vpcex != nullptr; };
    s.loaded = [](void *) { vpcex->EnqueueCid(cidSceneLoaded); };
    s.current_camera = [](void *p) -> MMI32 { return static_cast<PScene>(p)->Pbkgd()->Icam(); };
    s.new_undo = [](void *, int kind) -> void * {
        return kind == MM_SCENE_UNDO_PAUSE ? static_cast<void *>(SceneUndoPause::PsunpNew())
                                                 : static_cast<void *>(SceneUndoBackground::PsunkNew());
    };
    s.pause_state = [](void *, void *u) { return static_cast<PSceneUndoPause>(u)->CoreState(); };
    s.background_state = [](void *, void *u) { return static_cast<PSceneUndoBackground>(u)->CoreState(); };
    s.commit_undo = [](void *p, void *u) -> int {
        return static_cast<PScene>(p)->Pmvie()->FAddUndo(static_cast<PMovieUndo>(u));
    };
    s.release_undo = [](void *, void *u) { static_cast<PMovieUndo>(u)->Release(); };
    s.clear_undo = [](void *p) { static_cast<PScene>(p)->Pmvie()->ClearUndo(); };
    s.not_undoable = [](void *) { PushErc(ercSocNotUndoable); };
    s.camera_tag = [](void *p, MMI32 camera, MMTag *out) -> int {
        TAG parent, tag = {};
        TagFromCore(&parent, static_cast<PScene>(p)->_owner.background_tag);
        bool ok = vptagm->FBuildChildTag(&parent, camera, kctgCam, &tag);
        if (ok)
            *out = SceneEditTag(tag);
        return ok;
    };
    s.cache_tag = [](void *, const MMTag *value) -> int {
        TAG tag;
        TagFromCore(&tag, *value);
        return vptagm->FCacheTagToHD(&tag);
    };
    s.close_tag = [](void *, MMTag *value) {
        TAG tag;
        TagFromCore(&tag, *value);
        TagManager::CloseTag(&tag);
        *value = SceneEditTag(tag);
    };
    s.diagnostic = [](void *, int issue) {
        if (issue == MM_SCENE_EDIT_FIRST_BACKGROUND)
            Bug("Use CORE function to set first background");
        else
            Bug("No background event found");
    };
    return s;
}
bool Scene::FPauseCore(WaitReason *pwit, long *pdts)
{
    AssertThis(0);
    AssertIn(*pwit, witNil, witLim);
    AssertIn(*pdts, 0, klwMax);
    MMI32 reason = *pwit, delay = *pdts;
    auto s = _EditServices();
    bool ok = mm_scene_pause_edit(&reason, &delay, &s) != 0;
    *pwit = static_cast<WaitReason>(reason);
    *pdts = delay;
    return ok;
}

/****************************************************
 *
 * This routine adds/removes a pause to the scene at the current frame
 * and creates an undo object for the action.
 *
 * Parameters:
 *	wit - The pause type, removes pause if wit==witNil.
 *  dts - Valid only if wit==witForTime, dts is in clock ticks.
 *
 *
 * Returns:
 *  fTrue if successful, else fFalse.
 *
 ****************************************************/
bool Scene::FPause(WaitReason wit, long dts)
{
    auto s = _EditServices();
    return mm_scene_pause_undo_edit(wit, dts, &s) != 0;
}

/****************************************************
 *
 * This routine sets the background for the scene.
 *
 * Parameters:
 *  ptag - Pointer to the background tag to put on the scene.
 *  ptagOld - Place to store the old background tag.
 *
 * Returns:
 *  fTrue if successful, else fFalse.
 *
 ****************************************************/
bool Scene::FSetBkgdCore(PTAG ptag, PTAG ptagOld)
{
    AssertThis(0);
    AssertVarMem(ptag);
    AssertVarMem(ptagOld);
    auto value = SceneEditTag(*ptag);
    MMTag old;
    auto s = _EditServices();
    s.output_context = ptagOld;
    s.publish_tag = SceneEditPublishTag;
    return mm_scene_background_edit(&value, &old, &s) != 0;
}

/****************************************************
 *
 * This routine sets the background for the scene.
 *
 * Parameters:
 *  pbkgd - Pointer to the background to put on the scene.
 *
 *
 * Returns:
 *  fTrue if successful, else fFalse.
 *
 ****************************************************/
bool Scene::FSetBkgd(PTAG ptag)
{
    AssertThis(0);
    AssertVarMem(ptag);
    auto value = SceneEditTag(*ptag);
    auto s = _EditServices();
    s.output_context = ptag;
    s.publish_tag = SceneEditPublishTag;
    return mm_scene_background_undo_edit(&value, &s) != 0;
}

/****************************************************
 *
 * This routine changes the camera view point at this frame.
 *
 * Parameters:
 *  icam - The camera number in the Background to switch to.
 *
 *
 * Returns:
 *  fTrue if successful, else fFalse.
 *
 ****************************************************/
bool Scene::FChangeCamCore(long icam, long *picamOld)
{
    AssertThis(0);
    AssertIn(icam, 0, 500);
    AssertVarMem(picamOld);
    MMI32 old;
    auto s = _EditServices();
    s.output_context = picamOld;
    s.publish_camera = [](void *p, MMI32 value) { *static_cast<long *>(p) = value; };
    return mm_scene_camera_edit(icam, &old, &s) != 0;
}

/****************************************************
 *
 * This routine changes the camera view point at this frame
 * and creates an undo object for the action.
 *
 * Parameters:
 *  icam - The camera number in the Background to switch to.
 *
 *
 * Returns:
 *  fTrue if successful, else fFalse.
 *
 ****************************************************/
bool Scene::FChangeCam(long icam)
{
    auto s = _EditServices();
    return mm_scene_camera_undo_edit(icam, &s) != 0;
}

/****************************************************
 *
 * This routine reads in a scene from a chunky file.
 *
 * Parameters:
 *  pcrf - Pointer to the chunky file to read from.
 *  cno  - Cno within the chunky file to read.
 *
 * Returns:
 *  pvNil, if failure, else a pointer to the scene.
 *
 ****************************************************/
Scene *Scene::PscenRead(PMovie pmvie, PChunkyResourceFile pcrf, ChunkNumber cno, const MMMovieSession *movie)
{
    AssertPo(pmvie, 0);
    AssertPo(pcrf, 0);
    const auto reader = pcrf->ReadOwner();
    if (!reader)
        return nullptr;
    struct Host
    {
        PChunkyResourceFile resources;
        MMMovieSceneSource source;
    } host = {pcrf, {movie, mm_movie_owner_graph(reader)}};
    MMSceneLoadServices s = {};
    s.context = &host;
    s.owners = _OwnerServices();
    s.packets = ScenePacketServices(pmvie);
    s.read = mm_movie_scene_reader(&host.source);
    s.resources = pcrf;
    s.tags = &TagManager::CoreServices();
    s.title_file = reader;
    s.titles = KauaiChunkOwnerTitleIO();
    s.thumbnail = [](void *p, MMU32 number) -> void * {
        const auto &graph = static_cast<Host *>(p)->source.graph;
        MMChunkEntry entry = {};
        PMaskedBitmapMBMP bitmap = nullptr;
        entry.type = kctgThumbMbmp;
        entry.number = number;
        graph.payload(graph.context, &entry, [](void *out, const MMReadStream *stream) -> int {
            *static_cast<PMaskedBitmapMBMP *>(out) = MaskedBitmapMBMP::PmbmpReadStream(stream);
            return MM_MOVIE_OK;
        }, &bitmap);
        return bitmap;
    };
    s.object = [](void *p, MMU32 number, int kind, void *scene) -> void * {
        auto resources = static_cast<Host *>(p)->resources;
        return kind == MM_SCENE_ADD_ACTOR
                   ? static_cast<void *>(Actor::PactrRead(resources, number))
                   : static_cast<void *>(TextBox::PtboxRead(resources, number, static_cast<PScene>(scene)));
    };
    return static_cast<PScene>(mm_scene_load(pmvie, cno, &s));
}

/****************************************************
 *
 * This routine plays all the starting events for a scene.
 *
 * Parameters:
 *	None.
 *
 * Returns:
 *  fTrue if successful, else fFalse.
 *
 ****************************************************/
bool Scene::FPlayStartEvents(bool fActorsOnly)
{
    AssertThis(0);
    auto services = _EventServices();
    return mm_scene_start_events(fActorsOnly, &services) != 0;
}

/****************************************************
 *
 * This routine returns the bkgd tag in *ptag
 *
 ****************************************************/
bool Scene::FGetTagBkgd(PTAG ptag)
{
    AssertThis(0);
    AssertVarMem(ptag);

    auto services = _EventServices();
    MMTag tag;
    if (!mm_scene_background_tag(&tag, &services))
        return fFalse;
    TagFromCore(ptag, tag);
    return fTrue;
}

/****************************************************
 *
 * This routine writes a scene into a chunky file.
 *
 * Parameters:
 *  pcrf - Pointer to the chunky file to write to.
 *  pcno  - Cno within the chunky file written to.
 *
 * Returns:
 *  fFalse if it fails, else fTrue.
 *
 ****************************************************/
bool Scene::FWrite(PChunkyResourceFile pcrf, ChunkNumber *pcno)
{
    AssertPo(pcrf, 0);
    const auto reader = pcrf->ReadOwner();
    if (!reader)
        return false;
    MovieArchiveHost archive{reader, pcrf->ArchiveWriter()};
    MMResourceChunkCopy binding;
    const auto destination = pcrf->CopySource(binding);
    MMMovieArchiveAssets assets{&destination, &TagManager::CoreServices()};
    return FWrite(archive.Editor(), archive.Writer(), mm_movie_archive_assets(&assets), pcno);
}

bool Scene::FWrite(const MMMovieArchiveEditor &editor, const MMArchiveWriter &writer,
                   const MMMovieEmbeddedArchive &assets, ChunkNumber *pcno)
{
    AssertThis(0);
    struct Host
    {
        PScene scene;
        const MMMovieArchiveEditor &editor;
        const MMArchiveWriter &writer;
        const MMMovieEmbeddedArchive &assets;
    } host = {this, editor, writer, assets};
    MMSceneStoreServices s = {};
    s.context = &host;
    s.owner = &_owner;
    s.editor = &editor;
    s.values = SceneSoundValues();
    s.assets = assets;
    s.free_sound_bytes = [](void *, void *p) { FreePpv(&p); };
    s.write_object = [](void *p, void *object, int kind, MMU32 n, MMU32 scene) -> int {
        const auto &host = *static_cast<Host *>(p);
        return kind == MM_SCENE_ADD_ACTOR ? static_cast<PActor>(object)->FWrite(host.writer, host.assets, n, scene)
                                        : static_cast<PTBOX>(object)->FWrite(host.editor, host.writer, n);
    };
    s.update_thumbnail = [](void *p) { static_cast<Host *>(p)->scene->_UpdateThumbnail(); };
    s.thumbnail_size = [](void *, void *p) -> MMI32 { return static_cast<PMaskedBitmapMBMP>(p)->CbOnFile(); };
    s.write_thumbnail = [](void *, void *p, const MMStorageWriter *writer) -> int {
        return static_cast<PMaskedBitmapMBMP>(p)->FWriteStream(*writer);
    };
    s.diagnostic = [](void *, int) { Bug("Bad event in frame event list"); };
    return mm_scene_store(pcno, &s) != 0;
}

/****************************************************
 *
 * This routine resolves all sound tags
 *
 * Parameters:
 *  None
 * Returns:
 *  None
 *
 ****************************************************/
bool Scene::FResolveAllSndTags(ChunkNumber cnoScen)
{
    AssertThis(0);
    MMSceneResolveServices s = {};
    s.context = this;
    s.owner = &_owner;
    s.actor = [](void *, void *actor, MMU32 scene) -> int {
        return static_cast<PActor>(actor)->FResolveAllSndTags(scene);
    };
    s.movie_sound = BindSceneMovieSound;
    return mm_scene_resolve_sounds(cnoScen, &s) != 0;
}

/****************************************************
 *
 * This routine removes all actors from movie's roll call.
 *
 * Parameters:
 *  fDelIfOnlyRef  --  fTrue indicates to remove the roll-call entry
 *        if a given actr is only referenced by this scene.
 *
 * Returns:
 *  None
 *
 ****************************************************/
void Scene::RemActrsFromRollCall(bool only)
{
    AssertThis(0);
    auto s = _ContentServices();
    mm_scene_roll_remove(only, &s);
}

/****************************************************
 *
 * This routine adds all actors to movie's roll call.
 *
 * Parameters:
 *  None
 *
 * Returns:
 *  fTrue if successful, else fFalse.
 *
 ****************************************************/
bool Scene::FAddActrsToRollCall(void)
{
    AssertThis(0);
    auto s = _ContentServices();
    return mm_scene_roll_add(&s) != 0;
}

/****************************************************
 *
 * This routine returns the scene's thumbnail.
 *
 * Parameters:
 *  None
 *
 * Returns:
 *  None
 *
 ****************************************************/
namespace
{
struct SceneThumbnailGraphics
{
    alignas(GraphicsEnvironment) unsigned char source[sizeof(GraphicsEnvironment)];
    alignas(GraphicsEnvironment) unsigned char target[sizeof(GraphicsEnvironment)];
    GraphicsEnvironment *source_env, *target_env;
};
RC ThumbnailRect(const MMMaskedRect &r)
{
    return RC(r.left, r.top, r.right, r.bottom);
}
MMSceneDirtyServices SceneDirtyServices()
{
    return {nullptr, [](void *, void *movie) { static_cast<PMovie>(movie)->SetDirty(); }};
}
} // namespace
MMSceneThumbnailServices Scene::_ThumbnailServices(void *graphics)
{
    MMSceneThumbnailServices s = {};
    s.context = this;
    s.owner = &_owner;
    s.owners = _OwnerServices();
    s.selection = _SelectionServices();
    s.sound_time = [](void *p) -> MMI32 { return static_cast<Scene *>(p)->Pmvie()->Pmsq()->DtimSnd(); };
    s.mute = [](void *p) { static_cast<Scene *>(p)->Pmvie()->Pmsq()->SndOff(); };
    s.resume = [](void *p, MMI32 time) { static_cast<Scene *>(p)->Pmvie()->Pmsq()->SndOnDtim(time); };
    s.view = [](void *p) -> void * { return static_cast<Scene *>(p)->Pmvie()->PddgGet(0); };
    s.dirty = [](void *p) { return int(static_cast<Scene *>(p)->Pmvie()->FDirty()); };
    s.dimensions = [](void *p, MMI32 *width, MMI32 *height) {
        auto client = static_cast<Scene *>(p)->Pmvie()->Pmcc();
        *width = client->Dxp();
        *height = client->Dyp();
    };
    s.new_port = [](void *, const MMMaskedRect *r) -> void * {
        auto bounds = ThumbnailRect(*r);
        return GraphicsPort::PgptNewOffscreen(&bounds, 8);
    };
    s.palette = [](void *p, void *port) {
        static_cast<PGraphicsPort>(port)->SetOffscreenColors(static_cast<Scene *>(p)->Pmvie()->PglclrThumbPalette());
    };
    s.release_port = [](void *, void *port) { static_cast<PGraphicsPort>(port)->Release(); };
    s.seek = [](void *p, MMI32 frame) { return int(static_cast<Scene *>(p)->FGotoFrm(frame)); };
    s.text_mode = [](void *, void *view) { return int(static_cast<PMovieView>(view)->FTextMode()); };
    s.draw = [](void *, void *view, void *port, const MMMaskedRect *r) {
        auto bounds = ThumbnailRect(*r);
        static_cast<PMovieView>(view)->DrawTree(static_cast<PGraphicsPort>(port), nullptr, &bounds, fgobNoVis);
    };
    s.copy_context = graphics;
    s.copy = [](void *p, void *source, void *target, const MMMaskedRect *from, const MMMaskedRect *to) {
        auto graphics = static_cast<SceneThumbnailGraphics *>(p);
        auto a = graphics->source_env =
            ::new (graphics->source) GraphicsEnvironment(static_cast<PGraphicsPort>(source));
        auto b = graphics->target_env =
            ::new (graphics->target) GraphicsEnvironment(static_cast<PGraphicsPort>(target));
        auto src = ThumbnailRect(*from), dst = ThumbnailRect(*to);
        b->CopyPixels(a, &src, &dst);
    };
    s.end_copy = [](void *p) {
        auto graphics = static_cast<SceneThumbnailGraphics *>(p);
        graphics->target_env->~GraphicsEnvironment();
        graphics->source_env->~GraphicsEnvironment();
    };
    s.row_bytes = [](void *, void *port) -> MMI32 { return static_cast<PGraphicsPort>(port)->CbRow(); };
    s.lock = [](void *, void *port) -> void * { return static_cast<PGraphicsPort>(port)->PrgbLockPixels(); };
    s.bitmap = [](void *, const void *pixels, MMI32 stride, const MMMaskedRect *r,
                  int transparent) -> void * {
        auto bounds = ThumbnailRect(*r);
        return MaskedBitmapMBMP::PmbmpNew(const_cast<byte *>(static_cast<const byte *>(pixels)), stride, r->bottom,
                                          &bounds, 0, 0, transparent);
    };
    s.unlock = [](void *, void *port) { static_cast<PGraphicsPort>(port)->Unlock(); };
    return s;
}

PMaskedBitmapMBMP Scene::PmbmpThumbnail(void)
{
    AssertThis(0);
    SceneThumbnailGraphics graphics;
    auto s = _ThumbnailServices(&graphics);
    return static_cast<PMaskedBitmapMBMP>(mm_scene_thumbnail(&s));
}

/****************************************************
 *
 * This routine updates the mbmp associated with the
 * thumbnail for the scene.
 *
 * Parameters:
 *  None
 *
 * Returns:
 *  None
 *
 ****************************************************/
void Scene::_UpdateThumbnail(void)
{
    AssertThis(0);
    SceneThumbnailGraphics graphics;
    auto s = _ThumbnailServices(&graphics);
    mm_scene_update_thumbnail(&s);
}

/****************************************************
 *
 * This routine marks the movie as dirty.
 *
 * Parameters:
 *  fDirty - Set dirty or not.
 *
 * Returns:
 *  None
 *
 ****************************************************/
void Scene::MarkDirty(bool fDirty)
{
    AssertThis(0);
    auto s = SceneDirtyServices();
    mm_scene_mark_dirty(&_owner, fDirty, &s);
}

/****************************************************
 *
 * This routine pastes an actor into the current scene
 *
 * Parameters:
 *  pactr - Pointer to the actor to paste
 *
 * Returns:
 *  fTrue if successful, else fFalse.
 *
 ****************************************************/
bool Scene::FPasteActrCore(PActor actor)
{
    AssertThis(0);
    auto s = _ContentServices();
    return mm_scene_actor_paste(actor, &s) != 0;
}

/****************************************************
 *
 * This routine pastes an actor into the current scene
 * and creates an undo object for the action.
 *
 * NOTE: This does not need an undo object, since the
 * tool is getting set to a "place" tool, which will
 * create the appropriate undo type.
 *
 * Parameters:
 *  pactr - Pointer to the actor to paste
 *
 * Returns:
 *  fTrue if successful, else fFalse.
 *
 ****************************************************/
bool Scene::FPasteActr(PActor pactr)
{
    AssertThis(0);
    AssertPo(pactr, 0);

    if (!FPasteActrCore(pactr))
    {
        return (fFalse);
    }

    return (fTrue);
}

/****************************************************
 *
 * This routine makes all actors go to a specific frame.
 *
 * Parameters:
 *  nfrm - Frame number to go to.
 *
 * Returns:
 *  fTrue if successful, else fFalse.
 *
 ****************************************************/
bool Scene::_FForceActorsToFrm(long nfrm, bool *pfSoundInFrame)
{
    AssertThis(0);
    auto services = _RuntimeServices();
    int sound = pfSoundInFrame ? *pfSoundInFrame : 0;
    bool result = mm_scene_force(nfrm, 0, pfSoundInFrame ? &sound : nullptr, &services) != 0;
    if (pfSoundInFrame) *pfSoundInFrame = sound != 0;
    return result;
}

/****************************************************
 *
 * This routine makes all text boxes go to a specific frame.
 *
 * Parameters:
 *  nfrm - Frame number to go to.
 *
 * Returns:
 *  fTrue if successful, else fFalse.
 *
 ****************************************************/
bool Scene::_FForceTboxesToFrm(long nfrm)
{
    AssertThis(0);
    auto services = _RuntimeServices();
    return mm_scene_force(nfrm, 1, nullptr, &services) != 0;
}

/****************************************************
 *
 * Hides all text boxes in this scene
 *
 * Parameters:
 *  None.
 *
 * Returns:
 *  None.
 *
 ****************************************************/
void Scene::HideTboxes(void)
{
    AssertThis(0);
    auto services = _OwnerServices();
    mm_scene_owner_hide_texts(&_owner, &services);
}

/****************************************************
 *
 * Hides all actors in this scene
 *
 * Parameters:
 *
 * Returns:
 *  None.
 *
 ****************************************************/
void Scene::HideActors(void)
{
    AssertThis(0);
    auto services = _OwnerServices();
    mm_scene_owner_actors(&_owner, 0, &services);
}

/****************************************************
 *
 * Shows all actors in this scene
 *
 * Parameters:
 *
 * Returns:
 *  None.
 *
 ****************************************************/
void Scene::ShowActors(void)
{
    AssertThis(0);
    auto services = _OwnerServices();
    mm_scene_owner_actors(&_owner, 1, &services);
}

/****************************************************
 *
 * This routine marks the movie dirty.
 *
 * Parameters:
 *  None
 *
 * Returns:
 *  None.
 *
 ****************************************************/
void Scene::_MarkMovieDirty()
{
    AssertThis(0);
    auto s = SceneDirtyServices();
    mm_scene_mark_dirty(&_owner, 1, &s);
}

/****************************************************
 *
 * This routine sets the scene to the given movie
 *
 * Parameters:
 *  pmvie - The new parent movie
 *
 * Returns:
 *  None.
 *
 ****************************************************/
void Scene::SetMvie(PMovie movie)
{
    AssertThis(0);
    _owner.movie = movie;
}

/****************************************************
 *
 * This returns the actor in the current scene with
 * the given arid.
 *
 * Parameters:
 *   Arid to look for.
 *
 * Returns:
 *   pvNil if failure, else the actor.
 *
 ****************************************************/
PActor Scene::PactrFromArid(long id)
{
    AssertThis(0);
    auto s = _ContentServices();
    return static_cast<PActor>(mm_scene_actor_find(id, &s));
}

/****************************************************
 *
 * This routine chops off the rest of the scene.
 *
 * Parameters:
 *   None.
 *
 * Returns:
 *   fTrue is successful, else fFalse if failure.
 *
 ****************************************************/
MMSceneChopServices Scene::_ChopServices()
{
    MMSceneChopServices s = {};
    s.context = this;
    s.events = _EventServices();
    s.trim_actor = [](void *, void *object, int back) {
        bool alive;
        auto actor = static_cast<PActor>(object);
        if (back)
            actor->DeleteBackCore(&alive);
        else
            actor->DeleteFwdCore(fTrue, &alive);
        return int(alive);
    };
    s.remove_actor = [](void *p, void *object) {
        static_cast<Scene *>(p)->RemActrCore(static_cast<PActor>(object)->Arid());
    };
    s.remove_text = [](void *p, void *object) {
        return int(static_cast<Scene *>(p)->FRemTboxCore(static_cast<PTBOX>(object)));
    };
    s.lifetime = [](void *, void *object, MMI32 *first, MMI32 *last) {
        long a, b;
        if (!static_cast<PTBOX>(object)->FGetLifetime(&a, &b))
            return 0;
        *first = a;
        *last = b;
        return 1;
    };
    s.show_text = [](void *, void *object) { return int(static_cast<PTBOX>(object)->FShowCore()); };
    s.start_text = [](void *, void *object, MMI32 frame) {
        auto text = static_cast<PTBOX>(object);
        Assert(text->FIsVisible(), "Bad tbox");
        text->SetStartFrame(frame);
    };
    s.dirty = [](void *p) { static_cast<Scene *>(p)->Pmvie()->SetDirty(); };
    s.discard_background_frame = [](void *p) { TrashVar(&static_cast<Scene *>(p)->_owner.runtime.background_frame); };
    s.bad_lifetime = [](void *) { Bug("Bad caption lifetime while chopping Scene"); };
    s.new_undo = [](void *) -> void * { return SceneUndoChop::PsuncNew(); };
    s.save_undo = [](void *p, void *undo) {
        return int(static_cast<PSceneUndoChop>(undo)->FSave(static_cast<Scene *>(p)));
    };
    s.commit_undo = [](void *p, void *undo) {
        return int(static_cast<Scene *>(p)->Pmvie()->FAddUndo(static_cast<PSceneUndoChop>(undo)));
    };
    s.release_undo = [](void *, void *undo) { static_cast<PSceneUndoChop>(undo)->Release(); };
    s.clear_undo = [](void *p) { static_cast<Scene *>(p)->Pmvie()->ClearUndo(); };
    return s;
}

bool Scene::FChopCore()
{
    AssertThis(0);
    auto s = _ChopServices();
    return mm_scene_chop(0, &s) != 0;
}

/****************************************************
 *
 * This routine chops off the rest of the scene and
 * creates an undo object as well.
 *
 * Parameters:
 *   None.
 *
 * Returns:
 *   fTrue is successful, else fFalse if failure.
 *
 ****************************************************/
bool Scene::FChop()
{
    AssertThis(0);
    auto s = _ChopServices();
    return mm_scene_chop_edit(0, &s) != 0;
}

/****************************************************
 *
 * This routine chops off the rest of the scene, backwards
 *
 * Parameters:
 *   None.
 *
 * Returns:
 *   fTrue is successful, else fFalse if failure.
 *
 ****************************************************/
bool Scene::FChopBackCore()
{
    AssertThis(0);
    auto s = _ChopServices();
    return mm_scene_chop(1, &s) != 0;
}

/****************************************************
 *
 * This routine chops off the rest of the scene, backwards,
 * and creates an undo object as well.
 *
 * Parameters:
 *   None.
 *
 * Returns:
 *   fTrue is successful, else fFalse if failure.
 *
 ****************************************************/
bool Scene::FChopBack()
{
    AssertThis(0);
    auto s = _ChopServices();
    return mm_scene_chop_edit(1, &s) != 0;
}

/****************************************************
 *
 * This routine does any startup for playback
 *
 * Parameters:
 *   None.
 *
 * Returns:
 *   None.
 *
 ****************************************************/
MMScenePlaybackServices Scene::_PlaybackServices()
{
    MMScenePlaybackServices s = {};
    s.context = this;
    s.owner = &_owner;
    s.lifetime = [](void *, int text, void *object, MMI32 *first, MMI32 *last) {
        long a, b;
        bool ok =
            text ? static_cast<PTBOX>(object)->FGetLifetime(&a, &b) : static_cast<PActor>(object)->FGetLifetime(&a, &b);
        if (!ok)
            return 0;
        *first = a;
        *last = b;
        return 1;
    };
    s.story = [](void *, void *object) { return int(static_cast<PTBOX>(object)->FStory()); };
    s.visible = [](void *, void *object) { return int(static_cast<PTBOX>(object)->FIsVisible()); };
    s.view = [](void *, void *object) -> void * { return static_cast<PTBOX>(object)->PddgGet(0); };
    s.scroll = [](void *, void *view) { static_cast<PTextBoxGobject>(view)->Scroll(scaNil); };
    s.missing_view = [](void *) { Bug("Missing caption view at Scene playback startup"); };
    s.prerender = [](void *p, int start) {
        auto scene = static_cast<Scene *>(p);
        if (start)
            scene->_DoPrerenderingWork(fTrue);
        else
            scene->_EndPrerendering();
    };
    s.clean_text = [](void *, void *object) { static_cast<PTBOX>(object)->CleanDdg(); };
    return s;
}

bool Scene::FStartPlaying()
{
    AssertThis(0);
    auto s = _PlaybackServices();
    return mm_scene_start_playing(&s) != 0;
}

/****************************************************
 *
 * This routine cleans up after a playback has stopped.
 *
 * Parameters:
 *   None.
 *
 * Returns:
 *   None.
 *
 ****************************************************/
void Scene::StopPlaying()
{
    AssertThis(0);
    auto s = _PlaybackServices();
    mm_scene_stop_playing(&s);
}

/******************************************************************************
    FTransOnFile
        For a given Scene chunk on a given ChunkyResourceFile, get the scene transition
        state for the scene.

    Arguments:
        PChunkyResourceFile pcrf     -- the chunky resource file the Scene lives on
        ChunkNumber cno       -- the ChunkNumber of the Scene chunk
        TRANS *ptrans -- pointer to memory to take the transition setting

    Returns: fTrue if it was able to set *ptrans, fFalse if something failed

************************************************************ PETED ***********/
bool Scene::FTransOnFile(PChunkyResourceFile pcrf, ChunkNumber cno, TRANS *ptrans)
{
    MMI32 value;
    if (!SceneData::FReadTransition(pcrf, cno, &value))
        return fFalse;
    *ptrans = static_cast<TRANS>(value);
    return fTrue;
}

/******************************************************************************
    FSetTransOnFile
        For a given Scene chunk on a given ChunkyResourceFile, set the scene transition
        state for the scene.

    Arguments:
        PChunkyResourceFile pcrf   -- the chunky resource file the Scene lives on
        ChunkNumber cno     -- the ChunkNumber of the Scene chunk
        TRANS trans -- the transition state to use

    Returns: fTrue if the routine could guarantee that the scene has the
        given transition state.

************************************************************ PETED ***********/
bool Scene::FSetTransOnFile(PChunkyResourceFile pcrf, ChunkNumber cno, TRANS trans)
{
    return SceneData::FWriteTransition(pcrf, cno, trans);
}

//
//
//
// UNDO STUFF
//
//
//

/****************************************************
 *
 * Public constructor for scene undo objects for name
 * related commands.
 *
 * Parameters:
 *	None.
 *
 * Returns:
 *  pvNil if failure, else a pointer to the movie undo.
 *
 ****************************************************/
PSceneUndoTitle SceneUndoTitle::PsuntNew()
{
    PSceneUndoTitle psunt;
    psunt = NewObj SceneUndoTitle();
    return (psunt);
}

/****************************************************
 *
 * Destructor for scene naming undo objects
 *
 ****************************************************/
SceneUndoTitle::~SceneUndoTitle(void)
{
    AssertBaseThis(0);
}

/****************************************************
 *
 * Does a command stored in an undo object.
 *
 * Parameters:
 *	None.
 *
 * Returns:
 *  fTrue if successful, else fFalse.
 *
 ****************************************************/
namespace {
struct ScenePropertyUndoHost { PMovie movie; long index, frame; };
MMScenePropertyUndoServices ScenePropertyUndoServices(ScenePropertyUndoHost *host)
{
    MMScenePropertyUndoServices s = {};
    s.context = host;
    s.switch_scene = [](void *p) {
        auto h = static_cast<ScenePropertyUndoHost *>(p);
        return int(h->movie->FSwitchScen(h->index));
    };
    s.seek = [](void *p) {
        auto h = static_cast<ScenePropertyUndoHost *>(p);
        return int(h->movie->Pscen()->FGotoFrm(h->frame));
    };
    s.flush = [](void *p) { static_cast<ScenePropertyUndoHost *>(p)->movie->Pmsq()->FlushMsq(); };
    s.clear = [](void *p) { static_cast<ScenePropertyUndoHost *>(p)->movie->ClearUndo(); };
    return s;
}
}

bool SceneUndoTitle::FDo(PDocumentBase pdocb)
{
    AssertThis(0);
    ScenePropertyUndoHost host = {Pmvie(), Iscen(), Nfrm()};
    auto s = ScenePropertyUndoServices(&host);
    s.scene = [](void *p) -> MMSceneState * {
        return &static_cast<ScenePropertyUndoHost *>(p)->movie->Pscen()->_owner.data;
    };
    return mm_scene_title_swap(&_title, &s) != 0;
}

/****************************************************
 *
 * Undoes a command stored in an undo object.
 *
 * Parameters:
 *	None.
 *
 * Returns:
 *  fTrue if successful, else fFalse.
 *
 ****************************************************/
bool SceneUndoTitle::FUndo(PDocumentBase pdocb)
{
    AssertThis(0);

    return (FDo(pdocb));
}

#ifdef DEBUG
/****************************************************
 * Mark memory used by the SceneUndoTitle
 *
 * Parameters:
 * 	None.
 *
 * Returns:
 *  None.
 *
 ****************************************************/
void SceneUndoTitle::MarkMem(void)
{
    AssertThis(0);
    SceneUndoTitle_PAR::MarkMem();
}

/***************************************************************************
    Assert the validity of the SceneUndoTitle.
***************************************************************************/
void SceneUndoTitle::AssertValid(LegacyUlong grf)
{
}
#endif

/****************************************************
 *
 * Public constructor for scene undo objects for sound
 * related commands.
 *
 * Parameters:
 *	None.
 *
 * Returns:
 *  pvNil if failure, else a pointer to the movie undo.
 *
 ****************************************************/
PSceneUndoSound SceneUndoSound::PsunsNew()
{
    PSceneUndoSound psuns;
    psuns = NewObj SceneUndoSound();
    return (psuns);
}

/****************************************************
 *
 * Destructor for scene sound undo objects
 *
 ****************************************************/
SceneUndoSound::~SceneUndoSound(void)
{
    AssertBaseThis(0);
    auto values = _Values();
    mm_scene_sound_undo_dispose(&_sound, &values);
}

/****************************************************
 *
 * Does a command stored in an undo object.
 *
 * Parameters:
 *	None.
 *
 * Returns:
 *  fTrue if successful, else fFalse.
 *
 ****************************************************/
bool SceneUndoSound::FDo(PDocumentBase pdocb)
{
    AssertThis(0);
    AssertPo(pdocb, 0);
    auto services = _Services();
    return mm_scene_sound_undo_swap(&_sound, &services) != 0;
}

/****************************************************
 *
 * Undoes a command stored in an undo object.
 *
 * Parameters:
 *	None.
 *
 * Returns:
 *  fTrue if successful, else fFalse.
 *
 ****************************************************/
bool SceneUndoSound::FUndo(PDocumentBase pdocb)
{
    AssertThis(0);
    AssertPo(pdocb, 0);

    return FDo(pdocb);
}

#ifdef DEBUG
/****************************************************
 * Mark memory used by the SceneUndoSound
 *
 * Parameters:
 * 	None.
 *
 * Returns:
 *  None.
 *
 ****************************************************/
void SceneUndoSound::MarkMem(void)
{
    AssertThis(0);
    SceneUndoSound_PAR::MarkMem();
    if (_sound.sound != pvNil)
        MarkPv(_sound.sound);
}

/***************************************************************************
    Assert the validity of the SceneUndoSound.
***************************************************************************/
void SceneUndoSound::AssertValid(LegacyUlong grf)
{
    SceneUndoSound_PAR::AssertValid(grf);
    if (_sound.sound != pvNil)
    {
        AssertPvCb(_sound.sound, size(MMSceneSound));
        AssertPvCb(_sound.sound, mm_scene_sound_size(_sound.sound->ctagc));
        Assert(_sound.style == _sound.sound->sty, "sty's don't match");
    }
}
#endif

/****************************************************
 *
 * Public constructor for scene undo objects for actor
 * related commands.
 *
 * Parameters:
 *	None.
 *
 * Returns:
 *  pvNil if failure, else a pointer to the movie undo.
 *
 ****************************************************/
PSceneActorUndo SceneActorUndo::PsunaNew()
{
    PSceneActorUndo psuna;
    psuna = NewObj SceneActorUndo();
    return (psuna);
}

/****************************************************
 *
 * Destructor for scene actor undo objects
 *
 ****************************************************/
static MMSceneContentServices SceneContentReleaseServices()
{
    MMSceneContentServices s = {};
    s.events.owners.release = [](void *, void *p) { static_cast<BASE *>(p)->Release(); };
    return s;
}
struct SceneContentUndoHost {PMovie movie;long scene,frame;};
static MMSceneContentUndoServices SceneContentUndoServices(SceneContentUndoHost *h)
{
    MMSceneContentUndoServices s = {};
    s.context = h;
    s.frame = h->frame;
    s.switch_scene = [](void *p) -> int {
        auto h = static_cast<SceneContentUndoHost *>(p);
        return h->movie->FSwitchScen(h->scene);
    };
    s.seek = [](void *p, MMI32 frame) -> int {
        return static_cast<SceneContentUndoHost *>(p)->movie->Pscen()->FGotoFrm(frame);
    };
    s.flush = [](void *p) { static_cast<SceneContentUndoHost *>(p)->movie->Pmsq()->FlushMsq(); };
    s.clear = [](void *p) { static_cast<SceneContentUndoHost *>(p)->movie->ClearUndo(); };
    s.update_roll = [](void *p) { static_cast<SceneContentUndoHost *>(p)->movie->Pmcc()->UpdateRollCall(); };
    return s;
}
SceneActorUndo::~SceneActorUndo(void)
{
    AssertBaseThis(0);
    auto s = SceneContentReleaseServices();
    mm_scene_actor_undo_dispose(&_content, &s);
}

/****************************************************
 *
 * Does a command stored in an undo object.
 *
 * Parameters:
 *	None.
 *
 * Returns:
 *  fTrue if successful, else fFalse.
 *
 ****************************************************/
bool SceneActorUndo::FDo(PDocumentBase pdocb)
{
    AssertThis(0);
    SceneContentUndoHost host = {Pmvie(), Iscen(), Nfrm()};
    auto s = SceneContentUndoServices(&host);
    s.scene = [](void *p) { return static_cast<SceneContentUndoHost *>(p)->movie->Pscen()->_ContentServices(); };
    return mm_scene_actor_undo(&_content, 1, &s) != 0;
}

/****************************************************
 *
 * Undoes a command stored in an undo object.
 *
 * Parameters:
 *	None.
 *
 * Returns:
 *  fTrue if successful, else fFalse.
 *
 ****************************************************/
bool SceneActorUndo::FUndo(PDocumentBase pdocb)
{
    AssertThis(0);
    SceneContentUndoHost host = {Pmvie(), Iscen(), Nfrm()};
    auto s = SceneContentUndoServices(&host);
    s.scene = [](void *p) { return static_cast<SceneContentUndoHost *>(p)->movie->Pscen()->_ContentServices(); };
    return mm_scene_actor_undo(&_content, 0, &s) != 0;
}

#ifdef DEBUG
/****************************************************
 * Mark memory used by the SceneActorUndo
 *
 * Parameters:
 * 	None.
 *
 * Returns:
 *  None.
 *
 ****************************************************/
void SceneActorUndo::MarkMem(void)
{
    AssertThis(0);
    SceneActorUndo_PAR::MarkMem();
    MarkMemObj(static_cast<PActor>(_content.actor));
}

/***************************************************************************
    Assert the validity of the SceneActorUndo.
***************************************************************************/
void SceneActorUndo::AssertValid(LegacyUlong grf)
{
    AssertPo(static_cast<PActor>(_content.actor), 0);
}
#endif

/****************************************************
 *
 * Public constructor for scene undo objects for text box
 * related commands.
 *
 * Parameters:
 *	None.
 *
 * Returns:
 *  pvNil if failure, else a pointer to the movie undo.
 *
 ****************************************************/
PSceneUndoText SceneUndoText::PsunxNew()
{
    PSceneUndoText psunx;
    psunx = NewObj SceneUndoText();
    return (psunx);
}

/****************************************************
 *
 * Destructor for scene text box undo objects
 *
 ****************************************************/
SceneUndoText::~SceneUndoText(void)
{
    AssertBaseThis(0);
    auto s = SceneContentReleaseServices();
    mm_scene_text_undo_dispose(&_content, &s);
}

/****************************************************
 *
 * Does a command stored in an undo object.
 *
 * Parameters:
 *	None.
 *
 * Returns:
 *  fTrue if successful, else fFalse.
 *
 ****************************************************/
bool SceneUndoText::FDo(PDocumentBase pdocb)
{
    AssertThis(0);
    SceneContentUndoHost host = {Pmvie(), Iscen(), Nfrm()};
    auto s = SceneContentUndoServices(&host);
    s.scene = [](void *p) { return static_cast<SceneContentUndoHost *>(p)->movie->Pscen()->_ContentServices(); };
    return mm_scene_text_undo(&_content, 1, &s) != 0;
}

/****************************************************
 *
 * Undoes a command stored in an undo object.
 *
 * Parameters:
 *	None.
 *
 * Returns:
 *  fTrue if successful, else fFalse.
 *
 ****************************************************/
bool SceneUndoText::FUndo(PDocumentBase pdocb)
{
    AssertThis(0);
    SceneContentUndoHost host = {Pmvie(), Iscen(), Nfrm()};
    auto s = SceneContentUndoServices(&host);
    s.scene = [](void *p) { return static_cast<SceneContentUndoHost *>(p)->movie->Pscen()->_ContentServices(); };
    return mm_scene_text_undo(&_content, 0, &s) != 0;
}
#ifdef DEBUG
/****************************************************
 * Mark memory used by the SceneUndoText
 *
 * Parameters:
 * 	None.
 *
 * Returns:
 *  None.
 *
 ****************************************************/
void SceneUndoText::MarkMem(void)
{
    AssertThis(0);
    SceneUndoText_PAR::MarkMem();
    MarkMemObj(static_cast<PTBOX>(_content.text));
}

/***************************************************************************
    Assert the validity of the SceneUndoText.
***************************************************************************/
void SceneUndoText::AssertValid(LegacyUlong grf)
{
    AssertPo(static_cast<PTBOX>(_content.text), 0);
}
#endif

/****************************************************
 *
 * Public constructor for scene undo objects for transition
 * related commands.
 *
 * Parameters:
 *	None.
 *
 * Returns:
 *  pvNil if failure, else a pointer to the movie undo.
 *
 ****************************************************/
PSceneUndoTransition SceneUndoTransition::PsunrNew()
{
    PSceneUndoTransition psunr;
    psunr = NewObj SceneUndoTransition();
    return (psunr);
}

/****************************************************
 *
 * Destructor for scene transition undo objects
 *
 ****************************************************/
SceneUndoTransition::~SceneUndoTransition(void)
{
    AssertBaseThis(0);
}

/****************************************************
 *
 * Does a command stored in an undo object.
 *
 * Parameters:
 *	None.
 *
 * Returns:
 *  fTrue if successful, else fFalse.
 *
 ****************************************************/
bool SceneUndoTransition::FDo(PDocumentBase pdocb)
{
    AssertThis(0);
    ScenePropertyUndoHost host = {Pmvie(), Iscen(), Nfrm()};
    auto s = ScenePropertyUndoServices(&host);
    s.scene = [](void *p) -> MMSceneState * {
        return &static_cast<ScenePropertyUndoHost *>(p)->movie->Pscen()->_owner.data;
    };
    return mm_scene_transition_swap(&_transition, &s) != 0;
}

/****************************************************
 *
 * Undoes a command stored in an undo object.
 *
 * Parameters:
 *	None.
 *
 * Returns:
 *  fTrue if successful, else fFalse.
 *
 ****************************************************/
bool SceneUndoTransition::FUndo(PDocumentBase pdocb)
{
    AssertThis(0);
    return (FDo(pdocb));
}

#ifdef DEBUG
/****************************************************
 * Mark memory used by the SceneUndoTransition
 *
 * Parameters:
 * 	None.
 *
 * Returns:
 *  None.
 *
 ****************************************************/
void SceneUndoTransition::MarkMem(void)
{
    AssertThis(0);
    SceneUndoTransition_PAR::MarkMem();
}

/***************************************************************************
    Assert the validity of the SceneUndoTransition.
***************************************************************************/
void SceneUndoTransition::AssertValid(LegacyUlong grf)
{
}
#endif

/****************************************************
 *
 * Public constructor for scene undo objects for pause
 * related commands.
 *
 * Parameters:
 *	None.
 *
 * Returns:
 *  pvNil if failure, else a pointer to the movie undo.
 *
 ****************************************************/
PSceneUndoPause SceneUndoPause::PsunpNew()
{
    PSceneUndoPause psunp;
    psunp = NewObj SceneUndoPause();
    return (psunp);
}

/****************************************************
 *
 * Destructor for scene pause undo objects
 *
 ****************************************************/
SceneUndoPause::~SceneUndoPause(void)
{
    AssertBaseThis(0);
}

/****************************************************
 *
 * Does a command stored in an undo object.
 *
 * Parameters:
 *	None.
 *
 * Returns:
 *  fTrue if successful, else fFalse.
 *
 ****************************************************/
struct SceneEditUndoHost
{
    PMovie movie;
    long scene, frame;

    MMSceneEditUndoServices Services();
};

MMSceneEditUndoServices SceneEditUndoHost::Services()
{
    MMSceneEditUndoServices s = {};
    s.context = this;
    s.switch_scene = [](void *p) -> int {
        auto h = static_cast<SceneEditUndoHost *>(p);
        return h->movie->FSwitchScen(h->scene);
    };
    s.seek_frame = [](void *p) -> int {
        auto h = static_cast<SceneEditUndoHost *>(p);
        return h->movie->Pscen()->FGotoFrm(h->frame);
    };
    s.scene_edit = [](void *p) -> MMSceneEditServices {
        return static_cast<SceneEditUndoHost *>(p)->movie->Pscen()->_EditServices();
    };
    s.flush = [](void *p) { static_cast<SceneEditUndoHost *>(p)->movie->Pmsq()->FlushMsq(); };
    s.clear = [](void *p) { static_cast<SceneEditUndoHost *>(p)->movie->ClearUndo(); };
    return s;
}
bool SceneUndoPause::FDo(PDocumentBase pdocb)
{
    AssertThis(0);
    SceneEditUndoHost host = {Pmvie(), Iscen(), Nfrm()};
    auto s = host.Services();
    return mm_scene_pause_undo(&_edit, &s) != 0;
}

/****************************************************
 *
 * Undoes a command stored in an undo object.
 *
 * Parameters:
 *	None.
 *
 * Returns:
 *  fTrue if successful, else fFalse.
 *
 ****************************************************/
bool SceneUndoPause::FUndo(PDocumentBase pdocb)
{
    return (FDo(pdocb));
}

#ifdef DEBUG
/****************************************************
 * Mark memory used by the SceneUndoPause
 *
 * Parameters:
 * 	None.
 *
 * Returns:
 *  None.
 *
 ****************************************************/
void SceneUndoPause::MarkMem(void)
{
    AssertThis(0);
    SceneUndoPause_PAR::MarkMem();
}

/***************************************************************************
    Assert the validity of the SceneUndoPause.
***************************************************************************/
void SceneUndoPause::AssertValid(LegacyUlong grf)
{
}
#endif

/****************************************************
 *
 * Public constructor for scene undo objects for background
 * related commands.
 *
 * Parameters:
 *	None.
 *
 * Returns:
 *  pvNil if failure, else a pointer to the movie undo.
 *
 ****************************************************/
PSceneUndoBackground SceneUndoBackground::PsunkNew()
{
    PSceneUndoBackground psunk;
    psunk = NewObj SceneUndoBackground();
    return (psunk);
}

/****************************************************
 *
 * Destructor for scene background undo objects
 *
 ****************************************************/
SceneUndoBackground::~SceneUndoBackground(void)
{
    AssertBaseThis(0);
}

/****************************************************
 *
 * Does a command stored in an undo object.
 *
 * Parameters:
 *	None.
 *
 * Returns:
 *  fTrue if successful, else fFalse.
 *
 ****************************************************/
bool SceneUndoBackground::FDo(PDocumentBase pdocb)
{
    AssertThis(0);
    SceneEditUndoHost host = {Pmvie(), Iscen(), Nfrm()};
    auto s = host.Services();
    return mm_scene_background_undo(&_edit, &s) != 0;
}

/****************************************************
 *
 * Undoes a command stored in an undo object.
 *
 * Parameters:
 *	None.
 *
 * Returns:
 *  fTrue if successful, else fFalse.
 *
 ****************************************************/
bool SceneUndoBackground::FUndo(PDocumentBase pdocb)
{
    AssertThis(0);
    return (FDo(pdocb));
}

#ifdef DEBUG
/****************************************************
 * Mark memory used by the SceneUndoBackground
 *
 * Parameters:
 * 	None.
 *
 * Returns:
 *  None.
 *
 ****************************************************/
void SceneUndoBackground::MarkMem(void)
{
    AssertThis(0);
    SceneUndoBackground_PAR::MarkMem();
}

/***************************************************************************
    Assert the validity of the SceneUndoBackground.
***************************************************************************/
void SceneUndoBackground::AssertValid(LegacyUlong grf)
{
}
#endif

/****************************************************
 *
 * Public constructor for scene undo objects for background
 * chop commands.
 *
 * Parameters:
 *	None.
 *
 * Returns:
 *  pvNil if failure, else a pointer to the movie undo.
 *
 ****************************************************/
MMSceneSnapshotServices Scene::SnapshotServices()
{
    MMSceneSnapshotServices s = {};
    s.open = [](void *) { return KauaiTemporaryArchive(); };
    s.begin = [](void *) { vpappb->BeginLongOp(); };
    s.end = [](void *) { vpappb->EndLongOp(); };
    s.write = [](void *, void *scene, MMPrivateArchive *archive, MMU32 *number) -> int {
        MMChunkFileCopy binding;
        const auto destination = mm_private_archive_copy_target(archive, &binding);
        MMMovieArchiveAssets assets{&destination, &TagManager::CoreServices()};
        return static_cast<PScene>(scene)->FWrite(mm_private_archive_editor(archive),
                                                mm_private_archive_writer(archive),
                                                mm_movie_archive_assets(&assets), number);
    };
    return s;
}

PSceneUndoChop SceneUndoChop::PsuncNew()
{
    PSceneUndoChop psunc;
    psunc = NewObj SceneUndoChop();
    return (psunc);
}

/****************************************************
 *
 * Destructor for scene pause undo objects
 *
 ****************************************************/
SceneUndoChop::~SceneUndoChop(void)
{
    mm_scene_snapshot_dispose(&_snapshot);
}

/****************************************************
 *
 * This function saves away a copy of the scene.
 *
 * Parameters:
 *  pscen - The scene to save away.
 *
 * Returns:
 *  fTrue if successful, else fFalse.
 *
 ****************************************************/
bool SceneUndoChop::FSave(PScene pscen)
{
    const auto s = Scene::SnapshotServices();
    return mm_scene_snapshot_save(&_snapshot, pscen, &s) != 0;
}

/****************************************************
 *
 * Does a command stored in an undo object.
 *
 * Parameters:
 *	None.
 *
 * Returns:
 *  fTrue if successful, else fFalse.
 *
 ****************************************************/
bool SceneUndoChop::FDo(PDocumentBase pdocb)
{
    AssertThis(0);
    const auto scenes = Pmvie()->_SceneServices();
    MMSceneChopSwapServices s = {};
    s.snapshot = Scene::SnapshotServices();
    s.delete_failed = [](void *) { Bug("chunk not deletable or missing"); };
    s.rewind = [](void *, void *scene) {
        auto value = static_cast<PScene>(scene);
        value->SetNfrmCur(mm_fixed_sub(value->NfrmFirst(), 1));
    };
    s.seek = [](void *scene, MMI32 frame) { static_cast<PScene>(scene)->FGotoFrm(frame); };
    return mm_scene_chop_swap(&_snapshot, Iscen(), Nfrm(), &s, &scenes) != 0;
}

/****************************************************
 *
 * Undoes a command stored in an undo object.
 *
 * Parameters:
 *	None.
 *
 * Returns:
 *  fTrue if successful, else fFalse.
 *
 ****************************************************/
bool SceneUndoChop::FUndo(PDocumentBase pdocb)
{
    AssertThis(0);
    return (FDo(pdocb));
}

#ifdef DEBUG
/****************************************************
 * Mark memory used by the SceneUndoChop
 *
 * Parameters:
 * 	None.
 *
 * Returns:
 *  None.
 *
 ****************************************************/
void SceneUndoChop::MarkMem(void)
{
    AssertThis(0);
    SceneUndoChop_PAR::MarkMem();
    if (_snapshot.archive)
    {
        const auto &files = mm_private_archive_reader(_snapshot.archive)->storage->owner->files;
        MarkMemObj(static_cast<PFileObject>(files.main_file));
        MarkMemObj(static_cast<PFileObject>(files.extra_file));
    }
}

/***************************************************************************
    Assert the validity of the SceneUndoChop.
***************************************************************************/
void SceneUndoChop::AssertValid(LegacyUlong grf)
{
    if (_snapshot.archive)
    {
        const auto &files = mm_private_archive_reader(_snapshot.archive)->storage->owner->files;
        AssertPo(static_cast<PFileObject>(files.main_file), grf);
        AssertNilOrPo(static_cast<PFileObject>(files.extra_file), grf);
    }
}
#endif

void ReleasePpsse(MMSceneSound **ppsse)
{
    AssertVarMem(ppsse);
    void *sound = *ppsse;
    auto services = SceneSoundServices();
    mm_scene_sound_release(&sound, &services);
    *ppsse = static_cast<MMSceneSound *>(sound);
}
