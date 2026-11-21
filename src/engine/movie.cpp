/* Copyright (c) Microsoft Corporation.
   Licensed under the MIT License. */

/***************************************************************************

  movie.cpp

  Author: Sean Selitrennikoff

  Date: August, 1994

  This file contains all functionality for movie manipulation.

  THIS IS A CODE REVIEWED FILE

    Basic movie private classes:

        Movie Scene actions Undo Object (MovieSceneUndo)

            BASE ---> UndoBase ---> MovieUndo ---> MovieSceneUndo


Note: The client of the movie engine should always do all actions through
Movie level APIs, it should never use accessor functions to maniplate Scenes,
Actors, Text boxes, etc.

***************************************************************************/

#include <cstdio>
#include <exception>
#include "soc.h"
#include "engine_movie_graph.h"
#include "studio_preferences.h"
#include "mm/indexed_display.h"
#include "mm/actor_archive.h"
#include "mm/movie_archive_edit.h"
#include "mm/movie_sound_archive.h"
ASSERTNAME

static_assert(kdtsSecond / kfps == MM_RECORD_FRAME_TICKS && ivNil == -1, "Actor recording gesture ABI");
static_assert(BASED_FIXED && BR_SCALAR(0.001) == 65 && kdtsSecond == 1000 && sizeof(BRA) == 2,
              "Actor transform drag ABI");

static unsigned RecordGestureModifiers(long modifiers)
{
    return ((modifiers & fcustShift) ? MM_RECORD_GESTURE_SHIFT : 0) |
           ((modifiers & fcustCmd) ? MM_RECORD_GESTURE_COMMAND : 0);
}

static_assert(kdtimSecond == MM_PLAYBACK_TICKS_PER_SECOND && kdtimSecond / kfps == MM_PLAYBACK_FRAME_TICKS &&
                  kdtimSecond / 4 == MM_PLAYBACK_FADE_TICKS && kdtsSecond == 1000,
              "Playback clock contract");
static_assert(int(witNil) == MM_PLAYBACK_WAIT_NONE && int(witUntilClick) == MM_PLAYBACK_WAIT_CLICK &&
                  int(witUntilSnd) == MM_PLAYBACK_WAIT_SOUND && int(witForTime) == MM_PLAYBACK_WAIT_TIME &&
                  transNil == -1,
              "Playback enum contract");

const ChildChunkID kchidGstSource = 1;

//
// How many pixels from edge to warp cursor back to center
//
const long kdpInset = 50;

//
//
// UNDO object for scene related actions:  Ins, New, and Rem
//
//
typedef class MovieSceneUndo *PMovieSceneUndo;

#define MovieSceneUndo_PAR MovieUndo

enum MUNST
{
    munstInsScen,
    munstRemScen,
    munstSetBkgd
};

#define kclsMovieSceneUndo 'MUNS'
class MovieSceneUndo : public MovieSceneUndo_PAR
{
    RTCLASS_DEC
    MARKMEM
    ASSERT

  protected:
    long _iscen;
    TAG _tag;
    PScene _pscen;
    MUNST _munst;
    bool _Apply(bool redo);
    MovieSceneUndo(void)
    {
    }

  public:
    static PMovieSceneUndo PmunsNew(void);
    ~MovieSceneUndo(void);

    void SetIscen(long iscen)
    {
        _iscen = iscen;
    }
    void SetPscen(PScene pscen)
    {
        _pscen = pscen;
        _pscen->AddRef();
    }
    void SetMunst(MUNST munst)
    {
        _munst = munst;
    }
    void SetTag(PTAG ptag)
    {
        _tag = *ptag;
    }

    virtual bool FDo(PDocumentBase pdocb);
    virtual bool FUndo(PDocumentBase pdocb);
};

//
//
//
//  BEGIN MOVIE
//
//
//

RTCLASS(Movie)
RTCLASS(MovieUndo)
RTCLASS(MovieSceneUndo)

BEGIN_CMD_MAP(Movie, CommandHandler)
ON_CID_ME(cidAlarm, &Movie::FCmdAlarm, pvNil)
ON_CID_ME(cidRender, &Movie::FCmdRender, pvNil)
ON_CID_GEN(cidSaveAndClose, pvNil, pvNil)
END_CMD_MAP_NIL()

struct Movie::SaveHost
{
    PMovie movie;
    Filename temporary, unique;
    explicit SaveHost(PMovie value) : movie(value) {}
};

static MMChunkEdit EditMovieArchive(void *file)
{
    return static_cast<PChunkyFile>(file)->EditIndex();
}

static void MovieArchiveIssue(void *, int issue)
{
    switch (issue)
    {
    case MM_MOVIE_ARCHIVE_MISSING_CHILD: Bug("parent or child not there"); break;
    case MM_MOVIE_ARCHIVE_UNLINK_FAILED: Bug("chunk or child link missing"); break;
    case MM_MOVIE_ARCHIVE_DELETE_FAILED: Bug("chunk not deletable or missing"); break;
    case MM_MOVIE_ARCHIVE_MOVE_FAILED: Bug("src not a child or dest already a child"); break;
    }
}

MMMovieSaveServices Movie::_SaveServices(SaveHost *host)
{
    static_assert(kctgScen == MM_SAVE_SCENE && kctgGst == MM_SAVE_TABLE &&
        kctgMtrl == MM_SAVE_MATERIAL && kctgTmpl == MM_SAVE_TEMPLATE &&
        int(crepToss) == MM_RESOURCE_TOSS_PRIORITY &&
        ksidUseCrf == 0 && kchidGstSource == 1 && cnoNil == 0xffffffffU,
        "Movie save ABI");
    MMMovieSaveServices s = {};
    s.context = host;
    s.data = &_data;
    s.scenes = &_scenes;
    s.state = &_save;
    s.allocator = CollectionAllocator();
#ifdef BUG1848
    s.retry_enabled = 1;
#endif
    s.temporary_path = [](void *p) -> void * {
        auto h = static_cast<SaveHost *>(p);
        return h->temporary.FGetTemp() ? &h->temporary : nullptr;
    };
    s.unique_path = [](void *p, void *path) -> void * {
        auto h = static_cast<SaveHost *>(p);
        auto destination = static_cast<PFilename>(path);
        h->unique = *destination;
        return h->unique.FGetUnique(destination->Ftg()) ? &h->unique : nullptr;
    };
    s.create_file = [](void *, void *path) -> void * {
        auto file = ChunkyFile::PcflCreate(static_cast<PFilename>(path), fcflTemp);
        Assert(!file || file->FTemp(), "Bad ChunkyFile");
        return file;
    };
    s.release_archive = [](void *p) { auto file = static_cast<PChunkyFile>(p); ReleasePpo(&file); };
    s.new_resources = [](void *, void *p, MMU32 cache) -> void * {
        return ChunkyResourceFile::PcrfNew(static_cast<PChunkyFile>(p), cache);
    };
    s.is_temp = [](void *p) -> int { return static_cast<PChunkyFile>(p)->FTemp(); };
    s.save = [](void *p, void *path) -> int { return static_cast<PChunkyFile>(p)->FSave(kctgSoc, static_cast<PFilename>(path)); };
    s.set_temp = [](void *p, int value) { static_cast<PChunkyFile>(p)->SetTemp(value != 0); };
    s.mark_temporary = [](void *p) { AssertDo(static_cast<PChunkyFile>(p)->FSetGrfcfl(fcflTemp, fcflTemp), 0); };
    s.persistent = [](void *p) {
        AssertDo(static_cast<PChunkyFile>(p)->FSetGrfcfl(fcflAddToExtra, fcflAddToExtra | fcflTemp), 0);
    };
    s.archive = [](void *file) { return static_cast<PChunkyFile>(file)->WriteIndex(); };
    s.find_file = [](void *, void *path) -> void * { return FileObject::PfilFromFni(static_cast<PFilename>(path)); };
    s.retain_saved = [](void *p) { static_cast<PFileObject>(p)->AddRef(); };
    s.release_saved = [](void *p) { auto file = static_cast<PFileObject>(p); ReleasePpo(&file); };
    s.flush_saved = [](void *p) {
        auto file = static_cast<PFileObject>(p);
        AssertPo(file, 0);
        file->Flush();
    };
    s.read_only = [](void *, void *p) -> int {
        String path;
        static_cast<PFilename>(p)->GetStnPath(&path);
        KauaiFiles::PathInfo info;
        return KauaiFiles::Info(path.Psz(), &info) == KauaiFiles::Status::Found && info.readOnly;
    };
    s.get_name = [](void *p, void *out) { static_cast<PFileObject>(p)->GetFni(static_cast<PFilename>(out)); };
    s.copy_path = [](void *p, void *out) { *static_cast<PFilename>(out) = *static_cast<PFilename>(p); };
    s.container_name = [](void *p) -> void * { return &static_cast<SaveHost *>(p)->movie->_containerName; };
    s.set_container_name = [](void *p, void *path) { static_cast<SaveHost *>(p)->movie->_containerName = *static_cast<PFilename>(path); };
    s.long_op = [](void *, int begin) { if (begin) vpappb->BeginLongOp(); else vpappb->EndLongOp(); };
    s.document_dirty = [](void *p, int dirty) { static_cast<SaveHost *>(p)->movie->_fDirty = dirty != 0; };
    s.write_scene = [](void *p, void *scene, MMU32 *number) -> int {
        return static_cast<PScene>(scene)->FWrite(static_cast<SaveHost *>(p)->movie->_data.Resources(), number);
    };
    s.archive_issue = MovieArchiveIssue;
    s.tags = &TagManager::CoreServices();
    s.rollcall_changed = [](void *p) { static_cast<SaveHost *>(p)->movie->Pmcc()->UpdateRollCall(); };
    s.source_names = vptagm ? vptagm->CoreSourceNames() : nullptr;
    s.titles = KauaiChunkTitleIO();
    s.has_error = [](void *p) -> int { return static_cast<PChunkyFile>(p)->ElError() != elNil; };
    s.reset_error = [](void *p) { static_cast<PChunkyFile>(p)->ResetEl(); };
    s.clear_errors = [](void *) { vpers->Clear(); };
    s.save_error = [](void *) { PushErc(ercSocSaveFailure); };
    s.bad_file = [](void *) { PushErc(ercSocBadFile); };
    s.update_title = [](void *p, void *path) { static_cast<SaveHost *>(p)->movie->_SetTitle(static_cast<PFilename>(path)); };
    s.clear_undo = [](void *p) { static_cast<SaveHost *>(p)->movie->ClearUndo(); };
    s.has_container = [](void *p) -> int { return static_cast<SaveHost *>(p)->movie->_container != nullptr; };
    s.bundled = [](void *p) -> int { return static_cast<SaveHost *>(p)->movie->_container->Bundled(); };
    s.is_vmm = [](void *path) -> int { return MovieContainer::IsVmm(static_cast<PFilename>(path)); };
    s.package = [](void *p, void *working, void *destination) -> int {
        return static_cast<SaveHost *>(p)->movie->_container->Save(static_cast<PFilename>(working), static_cast<PFilename>(destination));
    };
    s.source_archive = [](void *, MMI32 source) -> int {
        Filename path;
        return vptagm && vptagm->SourceArchive(source, kftgContent, 0, &path);
    };
    s.tag_graph = [](void *p) { return EngineMovieGraph::FromFile(static_cast<PChunkyFile>(p)); };
    s.actor_tag_issue = [](void *, int issue) {
        if (issue == MM_ACTOR_TAG_TEMPLATE_LINK)
            Bug("where's the child Template?");
        else
            Assert(false, "bad ThreeDTextF");
    };
    s.scene_tag_issue = [](void *, int start, int missing) {
        if (missing)
            Bug("no background event!?");
        else if (start)
            Bug("Bad event in start event list");
        else
            Bug("Bad event in frame event list");
    };
    s.material_reader = Material_MTRL::FReadMtrl;
    s.template_reader = Template::FReadTmpl;
    s.purge_sounds = [](void *p) -> int { return static_cast<SaveHost *>(p)->movie->Pmcc()->FQueryPurgeSounds(); };
    s.bad_child = [](void *) { Bug("ChunkyFile returned bogus Ckid()"); };
    s.bad_sound_index = [](void *) { Bug("Should never fail"); };
    s.missing_scene = [](void *) { AssertDo(false, "Should never fail"); };
    s.missing_root = [](void *) { AssertDo(false, "Missing saved movie root"); };
    return s;
}

/****************************************************
 *
 * Constructor for movies.  This function is private, use PmvieNew()
 * for public construction.
 *
 ****************************************************/
Movie::Movie(void) : _clok(khidMvieClock)
{
    mm_movie_scenes_init(&_scenes);
    mm_movie_save_init(&_save);
    mm_playback_state_init(&_playback);
    mm_alarm_playback_init(&_runtime.transport);

    SetCundbMax(1);
}

struct Movie::RuntimeHost
{
    SaveHost saveHost;
    MMMovieSaveServices save;
    MMMovieOwnerServices owner;
    MMMovieSceneServices scenes;
    Filename working;

    explicit RuntimeHost(PMovie movie)
        : saveHost(movie), save(movie->_SaveServices(&saveHost)), owner(MovieData::_OwnerServices(nullptr)),
          scenes(movie->_SceneServices())
    {
    }
};

static void RetainRuntimeRef(void *p) { static_cast<BASE *>(p)->AddRef(); }
static void ReleaseRuntimeRef(void *p) { static_cast<BASE *>(p)->Release(); }
static int MovieFadesDisabled(void *) { return CurrentStudioPreferences().disableAudioFades; }
static void SetMovieVolume(void *, MMI32 volume) { vpsndm->SetVlm(volume); }

MMMovieRuntimeServices Movie::_RuntimeServices(RuntimeHost *host)
{
    MMMovieRuntimeServices s = {};
    s.context = host;
    s.owner = &host->owner;
    s.save = &host->save;
    s.scenes = &host->scenes;
    s.prepare = [](void *p, void *original, void **working) -> int {
        auto h = static_cast<RuntimeHost *>(p);
        auto path = static_cast<PFilename>(original);
        const bool bundled = MovieContainer::IsVmm(path);
        if (!MovieContainer::prepare)
        {
            if (bundled)
                PushErc(ercSocBadFile);
            return !bundled;
        }
        if (!MovieContainer::prepare(path, &h->working, h->saveHost.movie->_container))
            return 0;
        if (bundled)
            *working = &h->working;
        return 1;
    };
    s.init_undo = [](void *p) -> int { return static_cast<RuntimeHost *>(p)->saveHost.movie->_FInitUndo(); };
    s.create_world = [](void *, void *p, int half) -> void * {
        auto client = static_cast<PMovieClientCallbacks>(static_cast<BASE *>(p));
        return static_cast<BASE *>(World::PbwldNew(client->Dxp(), client->Dyp(), half != 0, half != 0));
    };
    s.create_sounds = [](void *) -> void * { return static_cast<BASE *>(MovieSoundQueue::PmsqNew()); };
    s.retain = RetainRuntimeRef;
    s.release = ReleaseRuntimeRef;
    s.discard_document = [](void *p) { static_cast<RuntimeHost *>(p)->saveHost.movie->Release(); };
    s.retain_archive = [](void *p) { static_cast<PChunkyFile>(p)->AddRef(); };
    s.missing_saved_file = [](void *) { Bug("Hey, we just opened this file!"); };
    s.merge_sources = [](void *file, MMU32 root) { vptagm->FMergeMovieSources(static_cast<PChunkyFile>(file), root); };
    s.cache_tags = [](void *, MMTagList *tags) -> int { return vptagm->FCacheTagList(tags); };
    s.resume_audio = [](void *) { if (vpsndm) vpsndm->ResumeAll(); };
    s.stop_playback = [](void *p) {
        auto client = static_cast<PMovieClientCallbacks>(static_cast<BASE *>(p));
        AssertPo(client, 0);
        client->EnableAccel();
        client->PlayStopped();
        vpsndm->StopAll();
    };
    s.audio_fades_disabled = MovieFadesDisabled;
    s.set_volume = SetMovieVolume;
    s.update_roll_call = [](void *p) { static_cast<PMovieClientCallbacks>(static_cast<BASE *>(p))->UpdateRollCall(); };
    return s;
}

void Movie::SetMcc(PMovieClientCallbacks pmcc)
{
    mm_movie_runtime_set_ref(&_runtime.client, static_cast<BASE *>(pmcc), RetainRuntimeRef, ReleaseRuntimeRef);
}

void Movie::SetThumbPalette(PDynamicArray palette)
{
    mm_movie_runtime_set_ref(&_runtime.thumbnail_palette, static_cast<BASE *>(palette), RetainRuntimeRef, ReleaseRuntimeRef);
}

/****************************************************
 *
 * Reads a movie from a file.
 *
 * Parameters:
 *  pmcc - Pointer to the movie client class block to use.
 *  pfni - File to read from.
 *  cno - ChunkNumber of the movie chunk, cnoNil if using
 *        the first one in the file.
 *
 * Returns:
 *  pvNil if failure, else a pointer to the movie object.
 *
 ****************************************************/
PMovie Movie::PmvieNew(bool fHalfMode, PMovieClientCallbacks pmcc, Filename *pfni, ChunkNumber cno)
{
    AssertNilOrPo(pfni, 0);
    AssertPo(pmcc, 0);

    auto movie = NewObj Movie;
    if (movie == pvNil)
        return pvNil;
    RuntimeHost host(movie);
    const auto services = _RuntimeServices(&host);
    return mm_movie_runtime_open(&movie->_runtime, static_cast<BASE *>(pmcc), pfni, cno, fHalfMode, &services)
               ? movie : pvNil;
}

/******************************************************************************
    Flush
        Ensures that the data has been written to disk.

************************************************************ PETED ***********/
void Movie::Flush(void)
{
    SaveHost host(this);
    auto services = _SaveServices(&host);
    mm_movie_save_flush(&services);
}

void Movie::ForceSaveAs(void)
{
    SaveHost host(this);
    auto services = _SaveServices(&host);
    mm_movie_save_release(1, &services);
}

/****************************************************
 *
 * Nuke unused sounds from the file
 * Msnd chunks exist as children of the movie chunk.
 * They are also children of any scene which uses
 * them.
 * Note: The ref count does not reflect how many
 * scene or actor events reference the sound.
 *
 * Parms:
 *    bool fPurgeAll -- if fFalse, only purge invalid sounds
 *
 ****************************************************/
void Movie::_DoSndGarbageCollection(bool fPurgeAll)
{
    AssertThis(0);
    SaveHost host(this);
    auto services = _SaveServices(&host);
    mm_movie_save_sounds(fPurgeAll, &services);
}

/****************************************************
 *
 * Destructor for movies.
 *
 ****************************************************/
Movie::~Movie(void)
{
    AssertBaseThis(0);
    RuntimeHost host(this);
    const auto services = _RuntimeServices(&host);
    mm_movie_runtime_dispose(&_runtime, &_playback, &services);
}

#ifdef DEBUG

/****************************************************
 * Mark memory used by the Movie
 *
 * Parameters:
 * 	None.
 *
 * Returns:
 *  None.
 *
 ****************************************************/
void Movie::MarkMem(void)
{
    AssertThis(0);

    Movie_PAR::MarkMem();

    MarkMemObj(_data.Resources());

    MarkMemObj(static_cast<PFileObject>(_save.file));

    MarkPv(_data.actors.storage.buffers[0].data);
    MarkPv(_data.actors.storage.buffers[1].data);

    MarkMemObj(Pscen());

    MarkMemObj(Pbwld());

    MarkMemObj(Pmcc());

    MarkMemObj(Pmsq());

    MarkMemObj(static_cast<BASE *>(_runtime.thumbnail_palette));
}

/***************************************************************************
 *
 * Assert the validity of the Movie.
 *
 * Parameters:
 *	grf - Bit field of options
 *
 * Returns:
 *  None.
 *
 **************************************************************************/
void Movie::AssertValid(LegacyUlong grf)
{
    Movie_PAR::AssertValid(fobjAllocated);

    AssertNilOrPo(_data.Resources(), 0);
    Assert(_data.actors.stride == sizeof(MMRollCallActor) + 4 &&
               _data.actors.count <= _data.actors.storage.buffers[1].capacity / _data.actors.stride,
           "bad movie roll call");
    AssertNilOrPo(Pbwld(), 0);
    AssertPo(&_clok, 0);
    AssertPo(Pmcc(), 0);
}

#endif // DEBUG

/****************************************************
 *
 * Fetches the iarid'th actor in the movie.
 *
 * Parameters:
 *   iarid - The arid index to fetch.
 *   parid - Pointer to storage for the found arid.
 *	 pstn  - Pointer to the actors name.
 *	 pcactRef - Number of scenes the actor is in.
 *	 *ptagTmpl - Template tag
 * Returns:
 *	 fTrue if successful, else fFalse if out of range.
 *
 ****************************************************/
MMMovieRollCallServices Movie::_RollCallServices()
{
    static_assert(fbrwsProp == MM_MOVIE_PROP && fbrwsTdt == MM_MOVIE_TEXT && aridNil == -1, "Roll-call ABI");
    MMMovieRollCallServices s = {};
    s.context = this;
    s.data = &_data;
    s.actor_id = [](void *p) -> MMI32 { return static_cast<PActor>(p)->Arid(); };
    s.set_actor_id = [](void *p, MMI32 id) { static_cast<PActor>(p)->SetArid(id); };
    s.actor_tag = [](void *p, MMTag *out) {
        *out = static_cast<PActor>(p)->_owner.runtime.template_tag;
        if (out->sid != ksidUseCrf)
            out->cache = nullptr;
    };
    s.is_prop = [](void *p) -> int { return static_cast<PActor>(p)->FIsPropBrws(); };
    s.is_text = [](void *p) -> int { return static_cast<PActor>(p)->FIsTdt(); };
    s.tags = &TagManager::CoreServices();
    s.changed = [](void *p) { static_cast<PMovie>(p)->Pmcc()->UpdateRollCall(); };
    s.diagnostic = [](void *, int issue) {
        switch (issue)
        {
        case MM_ROLLCALL_NO_ACTOR: Bug("no such actor"); break;
        case MM_ROLLCALL_BAD_REMOVE: Bug("Tried to remove an invalid actor"); break;
        case MM_ROLLCALL_UNDERFLOW: Assert(false, "Too many removes"); break;
        }
    };
    return s;
}

MMMovieUndoServices Movie::_UndoServices()
{
    static_assert(undoDisabled == MM_MOVIE_UNDO_DISABLED && undoUndo == MM_MOVIE_UNDO_READY, "Movie undo ABI");
    MMMovieUndoServices s = {};
    s.context = this;
    s.movie = this;
    s.state = [](void *, void *u) { return static_cast<PMovieUndo>(u)->CoreContext(); };
    s.validate = [](void *, void *u) { AssertPo(static_cast<PMovieUndo>(u), 0); };
    s.scene_index = [](void *p) -> MMI32 { return static_cast<PMovie>(p)->Iscen(); };
    s.frame = [](void *p) -> MMI32 { return static_cast<PMovie>(p)->Pscen()->Nfrm(); };
    s.commit = [](void *p, void *u) -> int { return static_cast<PMovie>(p)->DocumentBase::FAddUndo(static_cast<PMovieUndo>(u)); };
    s.clear = [](void *p) { static_cast<PMovie>(p)->Movie_PAR::ClearUndo(); };
    s.notify = [](void *p, int mode) { static_cast<PMovie>(p)->Pmcc()->SetUndo(mode); };
    return s;
}

MMMovieEditServices Movie::_EditServices()
{
    static_assert(rOne == 65536 && vlmNil == -1 && styNil == 0 && styMidi == 4, "Movie edit scalar/sound ABI");
    MMMovieEditServices s = {};
    s.context = this; s.data = &_data; s.scenes = &_scenes;
    s.rollcall = _RollCallServices(); s.undo = _UndoServices();
    s.selected_actor = [](void *p) -> void * { return static_cast<PScene>(p)->PactrSelected(); };
    s.actor_from_id = [](void *p, MMI32 id) -> void * { return static_cast<PScene>(p)->PactrFromArid(id); };
    s.select_actor = [](void *p, void *actor) { static_cast<PScene>(p)->SelectActr(static_cast<PActor>(actor)); };
    s.first_view = [](void *p) -> void * { return static_cast<PMovieView>(static_cast<PMovie>(p)->PddgGet(0)); };
    s.new_actor = [](void *, const MMTag *value) -> void * { TAG tag; TagFromCore(&tag, *value); return Actor::PactrNew(&tag); };
    s.actor_visible = [](void *p) -> int { return static_cast<PActor>(p)->FIsInView(); };
    s.stage = Actor::_BindStageEdit;
    s.actors = Actor::_OwnerServices();
    s.actor_undo = [](void *p) { return static_cast<PActor>(p)->_UndoCreateServices(); };
    s.transform = [](void *p) { return static_cast<PActor>(p)->_TransformEditServices(); };
    s.costume = [](void *p) { return static_cast<PActor>(p)->_CostumeEditServices(); };
    s.actor_sound = [](void *p) { return static_cast<PActor>(p)->_SoundEditServices(); };
    TrashVar(&s.sound_external_child);
    s.add_actor = [](void *p, void *actor) -> int { return static_cast<PScene>(p)->FAddActr(static_cast<PActor>(actor)); };
    s.remove_actor = [](void *p, MMI32 id) -> int { return static_cast<PScene>(p)->FRemActr(id); };
    s.scene_edit = [](void *p) { return static_cast<PScene>(p)->_EditServices(); };
    s.scene_properties = [](void *p) { return static_cast<PScene>(p)->_PropertyServices(); };
    s.frame = [](void *p) -> MMI32 { return static_cast<PScene>(p)->Nfrm(); };
    s.last_actor_undo = [](void *p) -> MMActorUndoState * {
        auto movie = static_cast<PMovie>(p);
        if (movie->CundbUndo() <= 0)
            return nullptr;
        auto undo = movie->_PundbGet(movie->_undoList.done - 1);
        return undo->FIs(kclsActorUndo) ? &static_cast<PActorUndo>(undo)->_undoState : nullptr;
    };
    s.place_actor = [](void *p, int entire) { static_cast<PMovieView>(p)->StartPlaceActor(entire != 0); };
    s.placement_undo = [](void *p, void *owned) { static_cast<PMovieView>(p)->SetActrUndo(static_cast<PActor>(owned)); };
    s.paste_actor = [](void *p, void *actor) -> int { return static_cast<PScene>(p)->FPasteActr(static_cast<PActor>(actor)); };
    s.route_paste = [](void *p) { return static_cast<PActor>(p)->_RoutePasteServices(); };
    s.long_op = [](void *, int begin) { if (begin) vpappb->BeginLongOp(); else vpappb->EndLongOp(); };
    s.cache_tag = [](void *, const MMTag *value, int background) -> int {
        TAG tag; TagFromCore(&tag, *value); return background ? Background::FCacheToHD(&tag) : vptagm->FCacheTagToHD(&tag);
    };
    s.default_title = [](void *p) { static_cast<PMovie>(p)->_SetTitle(); };
    s.new_scene_undo = [](void *) -> void * { return MovieSceneUndo::PmunsNew(); };
    s.configure_scene_undo = [](void *, void *p, MMI32 index, const MMTag *value) {
        auto undo = static_cast<PMovieSceneUndo>(p); TAG tag; TagFromCore(&tag, *value);
        undo->SetMunst(munstInsScen); undo->SetIscen(index); undo->SetTag(&tag);
    };
    s.release_undo = [](void *p) { auto undo = static_cast<PMovieSceneUndo>(p); ReleasePpo(&undo); };
    s.sounds_enabled = [](void *p) -> int { return static_cast<PMovie>(p)->FSoundsEnabled(); };
    s.play_sound = [](void *p) { static_cast<PMovie>(p)->Pmsq()->PlayMsq(); };
    s.flush_sound = [](void *p) { static_cast<PMovie>(p)->Pmsq()->FlushMsq(); };
    s.fetch_sound = [](void *, const MMTag *value) -> void * { TAG tag; TagFromCore(&tag, *value); return vptagm->PbacoFetch(&tag, MovieSoundMSND::FReadMsnd); };
    s.sound_volume = [](void *p) -> MMI32 { return static_cast<PMovieSoundMSND>(p)->Vlm(); };
    s.sound_style = [](void *p) -> MMI32 { return static_cast<PMovieSoundMSND>(p)->Sty(); };
    s.release_sound = [](void *p) { auto sound = static_cast<PMovieSoundMSND>(p); ReleasePpo(&sound); };
    s.dirty = [](void *p) { static_cast<PMovie>(p)->SetDirty(); };
    s.invalidate = [](void *p, int bars) { if (bars) static_cast<PMovie>(p)->InvalViewsAndScb(); else static_cast<PMovie>(p)->InvalViews(); };
    s.actor_nuked = [](void *p) { static_cast<PMovie>(p)->Pmcc()->ActorNuked(); };
    s.diagnostic = [](void *, int issue) {
        switch (issue)
        {
        case MM_MOVIE_EDIT_NO_ACTOR: PushErc(ercSocNoActrSelected); break;
        case MM_MOVIE_EDIT_ROLLCALL_FAILED: AssertDo(false, "Should never fail"); break;
        case MM_MOVIE_EDIT_BACKGROUND_FAILED: Bug("warning: set background failed."); break;
        }
    };
    return s;
}

bool Movie::FGetArid(long iarid, long *parid, PString pstn, long *pcactRef, PTAG ptagTmpl)
{
    AssertThis(0);
    AssertPvCb(parid, size(long));
    AssertVarMem(pcactRef);
    MMI32 id, refs;
    const void *name;
    MMU32 count;
    MMTag tag;
    if (!mm_movie_rollcall_get(&_data.actors, iarid, &id, &name, &count, &refs,
                                        ptagTmpl ? &tag : nullptr))
        return false;
    pstn->SetRgch(const_cast<achar *>(static_cast<const achar *>(name)), count);
    *parid = id; *pcactRef = refs;
    if (ptagTmpl) TagFromCore(ptagTmpl, tag);
    return true;
}

/****************************************************
 *
 * User chose arid in the roll call.  If actor
 * exists in this scene and is onstage, select it.
 * Else if actor is offstage, bring it onstage.
 * If actor is not in this scene, create and add it.
 *
 * Parameters:
 *	arid - The arid to search for, or create.
 *
 * Returns:
 *  fTrue if successful, else fFalse.
 *
 ****************************************************/
bool Movie::FChooseArid(long arid)
{
    AssertThis(0); AssertPo(Pscen(), 0);
    const auto services = _EditServices();
    return mm_movie_choose_actor(arid, &services) != 0;
}

/****************************************************
 *
 * Return the arid of the selected actor.
 *
 * Parameters:
 *   None.
 *
 * Returns:
 *   Arid of the selected actor, else aridNil.
 *
 ****************************************************/
long Movie::AridSelected(void)
{
    AssertThis(0);
    const auto services = _EditServices();
    return mm_movie_selected_actor(&services);
}

/****************************************************
 *
 * Fetches the name of the actor with arid.
 *
 * Parameters:
 *   arid - The arid to fetch.
 *   pstn - Pointer to storage for the found name.
 *
 * Returns:
 *	 fTrue if successful, else fFalse if failure.
 *
 ****************************************************/
bool Movie::FGetName(long arid, PString pstn)
{
    AssertThis(0); AssertPo(pstn, 0);
    const void *name;
    MMU32 count;
    if (!FGetName(arid, &name, &count))
        return false;
    pstn->SetRgch(const_cast<achar *>(static_cast<const achar *>(name)), count);
    return true;
}

bool Movie::FGetName(long arid, const void **name, MMU32 *count)
{
    AssertThis(0);
    return mm_movie_rollcall_name(&_data.actors, arid, name, count) != 0;
}

/****************************************************
 *
 * Sets the name of the actor with arid.
 *
 * Parameters:
 *   arid - The arid to set.
 *   pstn - Pointer to the name.
 *
 * Returns:
 *	 fTrue if successful, else fFalse.
 *
 ****************************************************/
bool Movie::FNameActr(long arid, PString pstn)
{
    AssertPo(pstn, 0);
    return FNameActr(arid, pstn->Prgch(), pstn->Cch());
}

bool Movie::FNameActr(long arid, const achar *name, LegacyLong count)
{
    AssertThis(0); AssertIn(arid, 0, 500);
    const auto services = _RollCallServices();
    return mm_movie_rollcall_rename(arid, name, count, &services) != 0;
}

/****************************************************
 *
 * Identifies whether the mactr is in prop browser
 *
 * Parameters:
 *   imactr - index in the roll call
 *
 * Returns:
 *	 bool
 *
 ****************************************************/
bool Movie::FIsPropBrwsIarid(long iarid)
{
    AssertThis(0); AssertIn(iarid, 0, _data.actors.count);
    return mm_movie_rollcall_has(&_data.actors, iarid, MM_MOVIE_PROP) != 0;
}

/****************************************************
 *
 * Identifies whether the mactr is in 3dtext object
 *
 * Parameters:
 *   imactr - index in the roll call
 *
 * Returns:
 *	 bool
 *
 ****************************************************/
bool Movie::FIsIaridTdt(long iarid)
{
    AssertThis(0); AssertIn(iarid, 0, _data.actors.count);
    return mm_movie_rollcall_has(&_data.actors, iarid, MM_MOVIE_TEXT) != 0;
}

/****************************************************
 *
 * Sets the tag of the actor with arid.
 *
 * Parameters:
 *   arid - The arid to set.
 *   ptag - Pointer to the Template tag.
 *
 * Returns:
 *	 none
 *
 ****************************************************/
void Movie::ChangeActrTag(long arid, PTAG ptag)
{
    AssertThis(0); AssertIn(arid, 0, 500); AssertVarMem(ptag);
    const auto services = _RollCallServices();
    const auto tag = TagToCore(*ptag);
    mm_movie_rollcall_change_tag(arid, &tag, &services);
}

/****************************************************
 *
 * This adds an actor to the roll call, if not already there, or
 * adds a reference count if it already exists.
 *
 * Parameters:
 *   Pointer to the actor to add.
 *
 * Returns:
 *   fTrue if successful, else fFalse indicating out of resources.
 *
 ****************************************************/
bool Movie::FAddToRollCall(Actor *pactr, PString pstn)
{
    AssertThis(0); AssertPo(pactr, 0); AssertNilOrPo(pstn, 0);
    const auto services = _RollCallServices();
    return mm_movie_rollcall_add(pactr, pstn ? pstn->Prgch() : nullptr, pstn ? pstn->Cch() : 0, &services) != 0;
}

/****************************************************
 *
 * This removes an actor from the roll call, if the reference count
 * drops to zero, else it decrements the reference count.
 *
 * Parameters:
 *   Pointer to the actor to remove.
 *
 * Returns:
 *   None.
 *
 ****************************************************/
void Movie::RemFromRollCall(Actor *pactr, bool fDelIfOnlyRef)
{
    AssertThis(0); AssertPo(pactr, 0);
    const auto services = _RollCallServices();
    mm_movie_rollcall_remove(pactr, fDelIfOnlyRef, &services);
}

/****************************************************
 *
 * Changes the scene in the movie currently being referenced.
 *
 * Parameters:
 *	iscen - Scene number to go to.
 *
 * Returns:
 *
 *  fTrue, if successful, else fFalse, indicating the
 *  same scene is still open (if possible).
 *
 ****************************************************/
MMMovieSceneServices Movie::_SceneServices()
{
    static_assert(munstInsScen == MM_MOVIE_INSERT_SCENE && munstRemScen == MM_MOVIE_REMOVE_SCENE &&
        ivNil == -1, "Movie scene ABI");
    MMMovieSceneServices s = {};
    s.context = this;
    s.state = &_scenes;
    s.movie = &_data;
    s.autosave = [](void *p) -> int { return static_cast<PMovie>(p)->FAutoSave(pvNil, fFalse); };
    s.make_resources = [](void *p) -> int { return static_cast<PMovie>(p)->_FMakeCrfValid(); };
    s.use_temporary = [](void *p) -> int { return static_cast<PMovie>(p)->_FUseTempFile(); };
    s.stop_loops = [](void *) { vpsndm->StopAll(sqnNil, sclLoopWav); };
    s.memory_cleanup = [](void *) {
#ifdef WIN
        MEMORYSTATUS ms;
        ms.dwLength = size(MEMORYSTATUS);
        GlobalMemoryStatus(&ms);
        if (ms.dwMemoryLoad == 100)
        {
            vpappb->BeginLongOp();
            vptagm->ClearCache(sidNil, ftagmMemory);
            vpappb->EndLongOp();
        }
#endif
    };
    s.session = MovieData::_SessionBinding();
    s.read = [](void *p, void *resources, MMU32 number, const MMMovieSession *session) -> void * {
        return Scene::PscenRead(static_cast<PMovie>(p), static_cast<PChunkyResourceFile>(resources), number, session);
    };
    s.create = [](void *p) -> void * { return Scene::PscenNew(static_cast<PMovie>(p)); };
    s.start = [](void *p) -> int { return static_cast<PScene>(p)->FPlayStartEvents(); };
    s.seek_first = [](void *p) -> int {
        auto scene = static_cast<PScene>(p);
        return scene->FGotoFrm(scene->NfrmFirst());
    };
    s.close = [](void *p) { auto scene = static_cast<PScene>(p); Scene::Close(&scene); };
    s.retain = [](void *p) { static_cast<PScene>(p)->AddRef(); };
    s.release = [](void *p) { auto scene = static_cast<PScene>(p); ReleasePpo(&scene); };
    s.sound_frame = [](void *p) { static_cast<PScene>(p)->UpdateSndFrame(); };
    s.hide = [](void *p) {
        auto scene = static_cast<PScene>(p);
        scene->HideActors();
        scene->HideTboxes();
    };
    s.scene_change = [](void *p) { static_cast<PMovie>(p)->Pmcc()->SceneChange(); };
    s.invalidate = [](void *p) { static_cast<PMovie>(p)->InvalViewsAndScb(); };
    s.file = [](void *p) -> void * { return static_cast<PChunkyResourceFile>(p)->Pcfl(); };
    s.edit = EditMovieArchive;
    s.write = [](void *p, void *scene, MMU32 *number) -> int {
        return static_cast<PScene>(scene)->FWrite(static_cast<PMovie>(p)->_data.Resources(), number);
    };
    s.archive_issue = MovieArchiveIssue;
    s.save = [](void *file) { static_cast<PChunkyFile>(file)->FSave(kctgSoc); };
    s.missing_link = [](void *) { Bug("Missing movie scene link"); };
    s.remove_actors = [](void *scene) { static_cast<PScene>(scene)->RemActrsFromRollCall(); };
    s.add_actors = [](void *scene) { static_cast<PScene>(scene)->FAddActrsToRollCall(); };
    s.dirty = [](void *p) { static_cast<PMovie>(p)->SetDirty(); };
    s.rollcall_changed = [](void *p) { static_cast<PMovie>(p)->Pmcc()->UpdateRollCall(); };
    s.switch_error = [](void *) { PushErc(ercSocSceneSwitch); };
    s.sounds_enabled = [](void *p) -> int { return static_cast<PMovie>(p)->FSoundsEnabled(); };
    s.play_sound = [](void *p) { static_cast<PMovie>(p)->Pmsq()->PlayMsq(); };
    s.flush_sound = [](void *p) { static_cast<PMovie>(p)->Pmsq()->FlushMsq(); };
    s.new_undo = [](void *) -> void * { return MovieSceneUndo::PmunsNew(); };
    s.configure_undo = [](void *, void *p, MMI32 index, void *scene) {
        auto undo = static_cast<PMovieSceneUndo>(p);
        undo->SetIscen(index);
        undo->SetPscen(static_cast<PScene>(scene));
        undo->SetMunst(munstRemScen);
    };
    s.add_undo = [](void *p, void *undo) -> int { return static_cast<PMovie>(p)->FAddUndo(static_cast<PMovieSceneUndo>(undo)); };
    s.release_undo = [](void *p) { auto undo = static_cast<PMovieSceneUndo>(p); ReleasePpo(&undo); };
    s.clear_undo = [](void *p) { static_cast<PMovie>(p)->ClearUndo(); };
    return s;
}

bool Movie::FSwitchScen(long iscen)
{
    AssertThis(0);
    Assert(iscen == ivNil || FIn(iscen, 0, Cscen()), "iscen out of range");
    Assert(iscen == ivNil || _data.Resources(), "Invalid save file");
    auto services = _SceneServices();
    return mm_movie_scene_switch(iscen, &services) != 0;
}

/****************************************************
 *
 * Creates a new scene and inserts it as scene number iscen.
 *
 * Parameters:
 *	iscen - Scene number to insert the new scene as.
 *
 * Returns:
 *  fTrue, if successful, else fFalse.
 *
 ****************************************************/
bool Movie::FNewScenInsCore(long iscen)
{
    AssertThis(0);
    AssertIn(iscen, 0, Cscen() + 1);
    auto services = _SceneServices();
    return mm_movie_scene_new(iscen, &services) != 0;
}

/******************************************************************************
    Native resource primitives for the shared C movie sound controller.

************************************************************ PETED ***********/
static void MovieSoundIssue(void *, int fault)
{
    switch (fault)
    {
    case MM_SOUND_BAD_CHILD_ENUM: Bug("ChunkyFile returned bogus ckid"); break;
    case MM_SOUND_NO_CURRENT_SCENE: Bug("Scene chunk doesn't exist!"); break;
    case MM_SOUND_CANNOT_COPY: PushErc(ercSocCantCopyMsnd); break;
    case MM_SOUND_BAD_CHUNK_ENUM: Bug("Should never fail"); break;
    case MM_SOUND_MISSING_CHUNK: Bug("parent or child not there"); break;
    case MM_SOUND_DELETE_FAILED: Bug("chunk not deletable or missing"); break;
    default: Bug("Unknown movie sound diagnostic"); break;
    }
}

MMMovieSoundServices Movie::SoundServices()
{
    static_assert(kctgMvie==MM_SOUND_MOVIE && kctgScen==MM_SOUND_SCENE &&
        kctgMsnd==MM_SOUND_CHUNK && kctgSoc==MM_SOUND_CREATOR && kchidSnd==MM_SOUND_PAYLOAD_SLOT && ksidUseCrf==0,
        "Movie sound ABI");
    static_assert(sizeof(TAG)==sizeof(MMTag) && sizeof(ChunkNumber)==sizeof(MMU32) &&
        sizeof(ChildChunkID)==sizeof(MMU32),"Movie sound record ABI");
    MMMovieSoundServices s{}; s.context=this;
    s.read=[](void *p,MMMovieSoundState *state) {
        auto movie=static_cast<PMovie>(p); state->cache=movie->_data.Resources(); state->movie=movie->_data.movieNumber; state->scene=movie->_scenes.index;
    };
    s.tags = &TagManager::CoreServices();
    s.edit = EditMovieArchive;
    s.ensure_autosave=[](void *p) { return int(static_cast<PMovie>(p)->FEnsureAutosave()); };
    s.use_temp=[](void *p) { return int(static_cast<PMovie>(p)->_FUseTempFile()); };
    s.inspect_source=[](void *p) { Filename filename; static_cast<PChunkyFile>(p)->GetFni(&filename); };
    s.invalidate_number = [](MMU32 *number) { (void)number; TrashVar(number); };
    s.save_file=[](void *p,MMU32 creator) { return int(static_cast<PChunkyFile>(p)->FSave(creator)); };
    s.is_midi=[](void *p) { Filename filename; static_cast<PFileObject>(p)->GetFni(&filename); return int(filename.Ftg()==kftgMidi); };
    s.copy_midi=[](void *p,void *destination,MMU32 *out,void *name) {
        ChunkNumber value; std::memcpy(&value,out,sizeof(value)); bool ok=MovieSoundMSND::FCopyMidi(static_cast<PFileObject>(p),static_cast<PChunkyFile>(destination),&value,static_cast<PString>(name));
        std::memcpy(out,&value,sizeof(value)); return int(ok);
    };
    s.copy_wave=[](void *p,void *destination,MMI32 style,MMU32 *out,void *name) {
        ChunkNumber value; std::memcpy(&value,out,sizeof(value)); bool ok=MovieSoundMSND::FCopyWave(static_cast<PFileObject>(p),static_cast<PChunkyFile>(destination),style,&value,static_cast<PString>(name));
        std::memcpy(out,&value,sizeof(value)); return int(ok);
    };
    s.has_bad_sound_error=[](void *) { return int(vpers->FIn(ercSocBadSoundFile)); };
    s.mark_gc=[](void *p) { static_cast<PMovie>(p)->_fGCSndsOnClose=true; };
    s.diagnostic = MovieSoundIssue;
    return s;
}

/****************************************************
 *
 * Adopt the scene user sounds as children of the movie.
 * Msnds are children of the movie unless they are
 * unused and deleted when the movie closes.
 *
 * Parameters
 * pcfl
 * cnoScen
 *
 *	Returns:
 * success or failure
 *
 ****************************************************/
bool Movie::_FAdoptMsndInMvie(PChunkyFile pcfl, ChunkNumber cnoScen)
{
    AssertThis(0); AssertPo(pcfl,0); const auto services=SoundServices();
    return mm_movie_sound_adopt(pcfl,cnoScen,&services)!=0;
}

/****************************************************
 *
 * Resolves a sound tag & chid to be a current tag
 *
 * Note: Msnds are children of the current scene.
 * Due to sound import, the cno can change.
 *
 * Parameters
 * ptag (with *ptag.cno possibly out of date)
 * chid (valid only for user snd)
 * cnoScen (if cnoNil, use current scene's cno
 *
 *	Returns:
 * Updated *ptag
 *
 ****************************************************/
bool Movie::FResolveSndTag(PTAG ptag, ChildChunkID chid, ChunkNumber cnoScen, PChunkyResourceFile pcrf)
{
    AssertVarMem(ptag);
    MMTag tag = {ptag->sid, ptag->pcrf, ptag->ctg, ptag->cno};
    const bool ok = FResolveSndTag(tag, chid, cnoScen, pcrf);
    TagFromCore(ptag, tag);
    return ok;
}
bool Movie::FResolveSndTag(MMTag &tag, ChildChunkID chid, ChunkNumber cnoScen, PChunkyResourceFile pcrf)
{
    AssertThis(0);
    AssertNilOrVarMem(pcrf);
    const auto services = SoundServices();
    return mm_movie_sound_resolve(&tag, chid, cnoScen, pcrf, &services) != 0;
}
bool Movie::FSaveTagSnd(TAG *ptag)
{
    AssertVarMem(ptag);
    MMTag tag;
    std::memcpy(&tag, ptag, sizeof(tag));
    const auto services = SoundServices();
    const bool ok = mm_movie_sound_save(&tag, &services) != 0;
    std::memcpy(ptag, &tag, sizeof(tag));
    return ok;
}

/****************************************************
 *
 * Finds the chid (child of the current scene) for a
 * given sound tag.  Adopt it if not found.
 * Meaningful for user sounds only
 *
 * Parameters
 * cno
 *
 *	Returns:
 * Updated *pchid
 *
 ****************************************************/
bool Movie::FChidFromUserSndCno(ChunkNumber cno, ChildChunkID *pchid)
{
    AssertThis(0); AssertVarMem(pchid); const auto services=SoundServices();
    MMU32 child; std::memcpy(&child,pchid,sizeof(child)); bool ok=mm_movie_sound_child(cno,&child,&services)!=0;
    std::memcpy(pchid,&child,sizeof(child)); return ok;
}

/****************************************************
 *
 * Copies a sound file to the movie. (Importing snd)
 * Sounds are written as MovieSoundMSND children of the current
 * scene chunk
 *
 * Parameters:
 *	pfilSrc, sty
 *
 * Returns:
 *  *pcno = cno of chunk written
 *  fFalse if there was a failure, else fTrue.
 *
 ****************************************************/
bool Movie::FCopySndFileToMvie(PFileObject pfilSrc, long sty, ChunkNumber *pcno, PString pstn)
{
    AssertThis(0); AssertVarMem(pfilSrc); AssertVarMem(pcno); AssertNilOrPo(pstn,0); Assert(_data.Resources()!=nullptr,"Bad working file.");
    const auto services=SoundServices(); MMU32 number; std::memcpy(&number,pcno,sizeof(number));
    bool ok=mm_movie_sound_import(pfilSrc,sty,&number,pstn,&services)!=0; std::memcpy(pcno,&number,sizeof(number)); return ok;
}

/****************************************************
 *
 * Copy Msnd chunk from specified movie *pcfl to
 * current movie
 * Parameters:
 *	pcfl : Source file
 *
 * Returns:
 *  fFalse if there was a failure, else fTrue.
 *	*pcnoDest
 *
 ****************************************************/
bool Movie::FCopyMsndFromPcfl(PChunkyFile pcflSrc, ChunkNumber cnoSrc, ChunkNumber *pcnoDest)
{
    AssertBaseThis(0); AssertPo(pcflSrc,0); AssertVarMem(pcnoDest); const auto services=SoundServices();
    MMU32 number; std::memcpy(&number,pcnoDest,sizeof(number)); bool ok=mm_movie_sound_copy(pcflSrc,cnoSrc,&number,&services)!=0;
    std::memcpy(pcnoDest,&number,sizeof(number)); return ok;
}

/****************************************************
 *
 * Verify the version number of a file
 *
 * Parameters:
 *	pfni
 *	*pcno == cnoNil if using the first chunk in the file
 *
 * Returns:
 *  fFalse if there was a failure, else fTrue.
 *  *pcno updated
 *
 ****************************************************/
bool Movie::FVerifyVersion(PChunkyFile pcfl, ChunkNumber *pcno)
{
    return MovieData::FVerifyVersion(pcfl, pcno);
}

/****************************************************
 *
 * Removes a scene from the movie.
 *
 * Parameters:
 *	iscen - Scene number to insert the new scene as.
 *
 * Returns:
 *  fFalse if there was a failure, else fTrue.
 *
 ****************************************************/
bool Movie::FRemScenCore(long iscen)
{
    AssertThis(0);
    AssertIn(iscen, 0, Cscen());
    Assert(_data.Resources(), "Bad working file.");
    auto services = _SceneServices();
    return mm_movie_scene_remove(iscen, &services) != 0;
}

/****************************************************
 *
 * Removes a scene from the movie and creates an
 * undo object for the action.  If it was the
 * currently open scene, then the scene is not open.
 * The next scene is opened.
 *
 * Parameters:
 *	iscen - Scene number to remove.
 *
 * Returns:
 *  fFalse if there was a failure, else fTrue.
 *
 ****************************************************/
bool Movie::FRemScen(long iscen)
{
    AssertThis(0);
    AssertIn(iscen, 0, Cscen());
    auto services = _SceneServices();
    return mm_movie_scene_remove_undoable(iscen, &services) != 0;
}

/****************************************************
 *
 * Adds a new material to the user's document.
 *
 * Parameters:
 *	pmtrl - Pointer to the material to add.
 *  ptag - Pointer to the tag for the file.
 *
 * Returns:
 *  fTrue if success, fFalse if couldn't add the material
 *
 ****************************************************/
MMMovieContentServices Movie::_ContentServices()
{
    MMMovieContentServices s = {};
    s.edit = _EditServices();
    s.ensure = [](void *p, void **out) -> int {
        PChunkyResourceFile resources = nullptr;
        bool ok = static_cast<PMovie>(p)->FEnsureAutosave(&resources);
        if (ok) *out = resources;
        return ok;
    };
    s.make_valid = [](void *p) -> int { return static_cast<PMovie>(p)->_FMakeCrfValid(); };
    s.use_temp = [](void *p) -> int { return static_cast<PMovie>(p)->_FUseTempFile(); };
    s.file = [](void *p) -> void * { return static_cast<PChunkyResourceFile>(p)->Pcfl(); };
    s.write_material = [](void *p, void *file, MMU32 *out) -> int {
        ChunkNumber number; bool ok = static_cast<PMaterial_MTRL>(p)->FWrite(static_cast<PChunkyFile>(file), kctgMtrl, &number);
        if (ok) *out = number;
        return ok;
    };
    s.new_text = [](const void *name, MMI32 shape, const void *font) -> void * {
        return ThreeDText::PtdtNew(const_cast<PString>(static_cast<const String *>(name)), shape,
                                  const_cast<PTAG>(static_cast<const TAG *>(font)));
    };
    s.write_text = [](void *p, void *file, MMU32 *out) -> int {
        ChunkNumber number; bool ok = static_cast<PThreeDText>(p)->FWrite(static_cast<PChunkyFile>(file), kctgTmpl, &number);
        if (ok) *out = number;
        return ok;
    };
    s.release_text = [](void *p) { auto text = static_cast<PThreeDText>(p); ReleasePpo(&text); };
    s.tags = &TagManager::CoreServices();
    s.name_units = [](const void *p, MMU32 *count) -> const void * {
        auto name = const_cast<PString>(static_cast<const String *>(p));
        *count = name->Cch();
        return name->Prgch();
    };
    s.character = [](const void *name, MMI32 i) -> MMI32 {
        return static_cast<unsigned char>(const_cast<PString>(static_cast<const String *>(name))->Psz()[i]);
    };
    s.change_template = [](void *p, const MMTag *value) -> int {
        return static_cast<PActor>(p)->FChangeTagTmpl(*value);
    };
    s.restore_template_edit = [](void *p, void *snapshot) {
        static_cast<PActor>(p)->Restore(static_cast<PActor>(snapshot));
    };
    s.new_caption = [](void *scene, const void *bounds, int story) -> void * {
        return TextBox::PtboxNew(static_cast<PScene>(scene), const_cast<RC *>(static_cast<const RC *>(bounds)), story != 0);
    };
    s.default_font = [](void *p) -> MMI32 { return static_cast<PMovie>(p)->Pmcc()->DypTboxDef(); };
    s.caption_font = [](void *p, MMI32 font) { static_cast<PTBOX>(p)->SetDypFontDef(font); };
    s.add_caption = [](void *p, void *caption) -> int { return static_cast<PScene>(p)->FAddTbox(static_cast<PTBOX>(caption)); };
    s.remove_caption = [](void *p, void *caption) -> int { return static_cast<PScene>(p)->FRemTbox(static_cast<PTBOX>(caption)); };
    s.select_caption = [](void *p, void *caption) { static_cast<PScene>(p)->SelectTbox(static_cast<PTBOX>(caption)); };
    s.attach_caption = [](void *p) { static_cast<PTBOX>(p)->AttachToMouse(); };
    s.release_caption = [](void *p) { auto caption = static_cast<PTBOX>(p); ReleasePpo(&caption); };
    s.selected_caption = [](void *p) -> void * { return static_cast<PScene>(p)->PtboxSelected(); };
    s.caption_at = [](void *p, MMI32 i) -> void * { return static_cast<PScene>(p)->PtboxFromItbox(i); };
    s.hide_caption = [](void *p) -> int { return static_cast<PTBOX>(p)->FHide(); };
    s.visible_caption = [](void *p) -> int { return static_cast<PTBOX>(p)->FIsVisible(); };
    s.missing_caption = [](void *) { PushErc(ercSocNoTboxSelected); };
    return s;
}

bool Movie::FInsertMtrl(PMaterial_MTRL pmtrl, PTAG ptag)
{
    AssertThis(0); AssertPo(pmtrl, 0); AssertVarMem(ptag);
    const auto services = _ContentServices();
    MMTag tag = {}; int written;
    bool ok = mm_movie_insert_material(pmtrl, &tag, &written, &services) != 0;
    if (written) TagFromCore(ptag, tag); else TrashVar(ptag);
    return ok;
}

/****************************************************
 *
 * Ensure an autosave file exists to use
 *
 * Parameters:
 *	Return the pcrf
 *
 * Returns:
 *  fTrue if success, fFalse if couldn't add the material
 *
 ****************************************************/
bool Movie::FEnsureAutosave(PChunkyResourceFile *ppcrf)
{
    AssertThis(0);
    SaveHost host(this);
    auto services = _SaveServices(&host);
    return mm_movie_save_ensure(ppcrf, ppcrf ? +[](void *output, void *resources) {
        *static_cast<PChunkyResourceFile *>(output) = static_cast<PChunkyResourceFile>(resources);
    } : nullptr, &services) != 0;
}

/****************************************************
 *
 * Adds a new 3-D Text object to the user's document.
 *
 * Parameters:
 *  pstn - ThreeDText text
 *  tdts - the ThreeDText shape
 *  ptagTdf - a tag to the ThreeDText's font
 *
 * Returns:
 *  fTrue if success, fFalse if couldn't add the ThreeDText
 *
 ****************************************************/
bool Movie::FInsTdt(PString pstn, long tdts, PTAG ptagTdf)
{
    AssertThis(0); AssertPo(pstn, 0); Assert(pstn->Cch() > 0, "can't insert 0-length ThreeDText");
    AssertIn(tdts, 0, tdtsLim); AssertVarMem(ptagTdf);
    const auto services = _ContentServices();
    return mm_movie_insert_text(pstn, tdts, ptagTdf, &services) != 0;
}

/****************************************************
 *
 * Edits the ThreeDText attached to pactr.
 *
 * Parameters:
 *  pactr - Pointer to actor to change
 *  pstn - New ThreeDText text
 *	tdts - New ThreeDText shape
 *  ptagTdf - New ThreeDText font
 *
 * Returns:
 *  fTrue if success, fFalse if couldn't change the ThreeDText
 *
 ****************************************************/
bool Movie::FChangeActrTdt(PActor pactr, PString pstn, long tdts, PTAG ptagTdf)
{
    AssertThis(0); AssertPo(pactr, 0); Assert(pactr->Ptmpl()->FIsTdt(), "actor must be a ThreeDText");
    AssertPo(pstn, 0); AssertIn(tdts, 0, tdtsLim); AssertVarMem(ptagTdf);
    Assert(pactr == Pscen()->PactrSelected(), 0);
    const auto services = _ContentServices();
    return mm_movie_change_text(pactr, pstn, tdts, ptagTdf, &services) != 0;
}

/****************************************************
 *
 * Closes and releases the current scene, if any
 *
 * Parameters:
 *	None.
 *
 * Returns:
 *  fTrue if success, fFalse if couldn't autosave
 *
 ****************************************************/
bool Movie::_FCloseCurrentScene(void)
{
    AssertThis(0);
    auto services = _SceneServices();
    return mm_movie_scene_close(&services) != 0;
}

/****************************************************
 *
 * Makes sure that the current file in use is a temp file.
 *
 * Parameters:
 *	None.
 *
 * Returns:
 *  fTrue if success, fFalse if couldn't switch
 *
 ****************************************************/
bool Movie::_FUseTempFile(void)
{
    AssertThis(0);
    SaveHost host(this);
    auto services = _SaveServices(&host);
    return mm_movie_save_temporary(&services) != 0;
}

/****************************************************
 *
 * Makes sure that there is a file to work with.
 *
 * Parameters:
 *	None.
 *
 * Returns:
 *  fTrue if success, fFalse if couldn't switch
 *
 ****************************************************/
bool Movie::_FMakeCrfValid(void)
{
    AssertThis(0);
    SaveHost host(this);
    auto services = _SaveServices(&host);
    return mm_movie_save_resources(&services) != 0;
}

/****************************************************
 *
 * Saves a movie to the temp file assigned to the movie.
 *
 * Parameters:
 *	pfni - File to save to, pvNil if to use a temp file.
 *	pCleanRollCall - Should actors that are not used be removed?
 *
 * Returns:
 *  fFalse if there was a failure, else fTrue.
 *
 ****************************************************/
bool Movie::FAutoSave(PFilename pfni, bool fCleanRollCall)
{
    AssertThis(0);
    AssertNilOrPo(_data.Resources(), 0);
    AssertNilOrPo(pfni, ffniFile);
    SaveHost host(this);
    auto services = _SaveServices(&host);
    return mm_movie_autosave(pfni, fCleanRollCall, &services) != 0;
}

/****************************************************
 *
 * Gets the file name to save the document to.
 *
 * Parameters:
 *	pfni - A pointer to a place to store the name
 *
 * Returns:
 *  fFalse if the fni is valid, else fTrue *and*
 *  pfni filled in.
 *
 ****************************************************/
bool Movie::FGetFni(Filename *pfni)
{
    AssertThis(0);
    AssertPo(pfni, 0);
    SaveHost host(this);
    auto services = _SaveServices(&host);
    return mm_movie_save_name(pfni, &services) != 0;
}

/***************************************************************************
 *
 * Saves a movie.
 *
 * Parameters:
 *  cid - type of save command issued for save
 *
 * Returns:
 *  fFalse if there was a failure, else fTrue.
 *
 ****************************************************/
bool Movie::FSave(long cid)
{
    AssertThis(0);
    return Movie_PAR::FSave(mm_movie_save_command(cid, cidSave, cidSaveAs, &_save));
}

/****************************************************
 *
 * Saves a movie to the given fni.
 *
 * Parameters:
 *	pfni - File to write to.
 *  fSetFni - Should the file name be remembered.
 *
 * Returns:
 *  fFalse if there was a failure, else fTrue.
 *
 ****************************************************/
bool Movie::FSaveToFni(Filename *pfni, bool fSetFni)
{
    AssertThis(0);
    AssertNilOrPo(pfni, ffniFile);
    SaveHost host(this);
    auto services = _SaveServices(&host);
    return mm_movie_save_to(pfni, fSetFni, &services) != 0;
}

/***************************************************************************
 *
 * Called by docb.cpp to get an fni for a selected movie file using
 * the save portfolio.
 *
 * Parameters:
 *	pfni - fni for selected file
 *
 * Returns:
 *  TRUE  - User selected a file
 *  FALSE - User canceled, (or other error).
 *
 ***************************************************************************/
bool Movie::FGetFniSave(Filename *pfni)
{
    AssertThis(0);
    AssertVarMem(pfni);

    String title; _data.GetTitle(&title);
    const bool selected = Pmcc()->GetFniSave(pfni, idsPortfMovieFilterLabel, idsPortfMovieFilterExt, idsPortfSaveMovieTitle,
                              (achar *)(_FNeedsContainer() ? PszLit("vmm") : ksz3mm), &title);
    _data.SetTitle(&title);
    return selected;
}

bool Movie::_FNeedsContainer()
{
    SaveHost host(this);
    auto services = _SaveServices(&host);
    return mm_movie_needs_container(&services) != 0;
}

/****************************************************
 *
 * Creates a new view on a movie.
 *
 * Parameters:
 *	pgcb - The creation block describing the gob placement
 *
 * Returns:
 *  A pointer to the view, otw pvNil on failure
 *
 ****************************************************/
PDocumentDisplayGraphicsObject Movie::PddgNew(PGraphicsObjectBlock pgcb)
{
    AssertThis(0);
    AssertVarMem(pgcb);
    return (MovieView::PmvuNew(this, pgcb, Pmcc()->Dxp(), Pmcc()->Dyp()));
}

/****************************************************
 *
 * Is not used.  Not supported.  Stubbed out here for
 * debugging.
 *
 * Parameters:
 *	None
 *
 * Returns:
 *  pvNil
 *
 ****************************************************/
PDocumentMDIWindow Movie::PdmdNew(void)
{
    Bug("Movie does not support DMDs, use multiple DDGs.");
    return (pvNil);
}

/****************************************************
 *
 * Adds a single item to the undo list
 *
 * Parameters:
 *	pmund - A pointer to a movie undo item.
 *
 * Returns:
 *  fTrue if successful, else fFalse.
 *
 ****************************************************/
bool Movie::FAddUndo(PMovieUndo pmunb)
{
    AssertThis(0);
    const auto services = _UndoServices();
    return mm_movie_undo_add(pmunb, &services) != 0;
}

/****************************************************
 *
 * Clears out the undo buffer
 *
 * Parameters:
 *	None.
 *
 * Returns:
 *  None.
 *
 ****************************************************/
void Movie::ClearUndo(void)
{
    AssertThis(0);
    const auto services = _UndoServices();
    mm_movie_undo_clear(&services);
}

/***************************************************************************
 *
 * This routine changes the current camera view
 *
 * Parameters:
 *	icam - The new camera to use.
 *
 * Returns:
 *  fTrue if successful, else fFalse.
 *
 **************************************************************************/
bool Movie::FChangeCam(long icam)
{
    AssertThis(0); AssertPo(Pscen(), 0); AssertIn(icam, 0, kccamMax);
    const auto services = _EditServices();
    MMMovieEditRequest q = {};
    q.camera = icam;
    return mm_movie_edit_scene(MM_MOVIE_EDIT_CAMERA, &q, &services) != 0;
}

/***************************************************************************
 *
 * This command inserts a new text box into the open scene.
 *
 * Parameters:
 *	prc - The placement within the movie's view of the text box.
 *  fStory - Is this supposed to be a story text box?
 *
 * Returns:
 *  fTrue if successful, else fFalse.
 *
 **************************************************************************/
bool Movie::FInsTbox(RC *prc, bool fStory)
{
    AssertThis(0); AssertPvCb(prc, size(RC)); AssertPo(Pscen(), 0);
    const auto services = _ContentServices();
    return mm_movie_insert_caption(prc, fStory, &services) != 0;
}

/***************************************************************************
 *
 * This command removes the currently selected text box.
 *
 * Parameters:
 *	None.
 *
 * Returns:
 *  fTrue if successful, else fFalse.
 *
 **************************************************************************/
bool Movie::FNukeTbox(void)
{
    AssertThis(0); AssertPo(Pscen(), 0);
    const auto services = _ContentServices();
    return mm_movie_remove_caption(0, &services) != 0;
}

/***************************************************************************
 *
 * This command hides the currently selected text box at the current frame.
 *
 * Parameters:
 *	None.
 *
 * Returns:
 *  fTrue if successful, else fFalse.
 *
 **************************************************************************/
bool Movie::FHideTbox(void)
{
    AssertThis(0); AssertPo(Pscen(), 0);
    const auto services = _ContentServices();
    return mm_movie_remove_caption(1, &services) != 0;
}

/***************************************************************************
 *
 * This command selects the itbox'th text box in the current frame.
 *
 * Parameters:
 *	itbox - Index value of the text box to select.
 *
 * Returns:
 *  None.
 *
 **************************************************************************/
void Movie::SelectTbox(long itbox)
{
    AssertThis(0); AssertPo(Pscen(), 0);
    const auto services = _ContentServices();
    mm_movie_select_caption(itbox, &services);
}

/***************************************************************************
 *
 * This sets the color to apply to a text box.
 *
 * Parameters:
 *  acr - The destination color.
 *
 * Returns:
 *  fTrue if successful, else fFalse.
 *
 **************************************************************************/
void Movie::SetPaintAcr(AbstractColor acr)
{
    AssertThis(0);

    PMovieView pmvu;

    pmvu = (PMovieView)PddgGet(0);
    AssertPo(pmvu, 0);
    pmvu->SetPaintAcr(acr);
}

/******************************************************************************
    SetDypFontTextCur
        Sets the current textbox font size

    Arguments:
        long dypFont  --  the new textbox font size

************************************************************ PETED ***********/
void Movie::SetDypFontTextCur(long dypFont)
{
    AssertThis(0);

    PmvuFirst()->SetDypFontTextCur(dypFont);
}

/******************************************************************************
    SetStyleTextCur
        Sets the current textbox font style

    Arguments:
        long grfont  --  the new textbox font style

************************************************************ PETED ***********/
void Movie::SetStyleTextCur(ulong grfont)
{
    AssertThis(0);

    PmvuFirst()->SetStyleTextCur(grfont);
}

/******************************************************************************
    SetOnnTextCur
        Sets the current textbox font face

    Arguments:
        long onn  -- the new textbox font face

************************************************************ PETED ***********/
void Movie::SetOnnTextCur(long onn)
{
    AssertThis(0);

    PmvuFirst()->SetOnnTextCur(onn);
}

/******************************************************************************
    PmvuCur
        Returns the active MovieView for this movie

************************************************************ PETED ***********/
PMovieView Movie::PmvuCur(void)
{
    AssertThis(0);
    PMovieView pmvu = (PMovieView)PddgActive();

    AssertPo(pmvu, 0);
    Assert(pmvu->FIs(kclsMovieView), "Current DocumentDisplayGraphicsObject isn't an MovieView");
    return pmvu;
}

/******************************************************************************
    PmvuFirst
        Returns the first MovieView for this movie

************************************************************ PETED ***********/
PMovieView Movie::PmvuFirst(void)
{
    AssertThis(0);
    PMovieView pmvu = (PMovieView)PddgGet(0);

    AssertPo(pmvu, 0);
    Assert(pmvu->FIs(kclsMovieView), "First DocumentDisplayGraphicsObject isn't an MovieView");
    return pmvu;
}

/***************************************************************************
 *
 * This command inserts a new actor.
 *
 * Parameters:
 *	ptag - The tag of the actor to create.
 *
 * Returns:
 *  fTrue if successful, else fFalse.
 *
 **************************************************************************/
bool Movie::FInsActr(PTAG ptag)
{
    AssertThis(0); AssertPvCb(ptag, size(TAG)); AssertPo(Pscen(), 0);
    const auto services = _EditServices();
    const auto tag = TagToCore(*ptag);
    return mm_movie_insert_actor(&tag, &services) != 0;
}

/***************************************************************************
 *
 * Brings an actor on to the stage.
 *
 * Parameters:
 *	arid - Arid of the actor to bring on.  aridNil implies the
 *		currently selected actor.
 *
 * Returns:
 *  fTrue if successful, else fFalse.
 *
 **************************************************************************/
bool Movie::FAddOnstage(long arid)
{
    AssertThis(0); AssertPo(Pscen(), 0);
    const auto services = _EditServices();
    return mm_movie_onstage(arid, &services) != 0;
}

/***************************************************************************
 *
 * Removes the selected actor from the current scene
 *
 * Parameters:
 *	None.
 *
 * Returns:
 *  fTrue if successful, else fFalse.
 *
 **************************************************************************/
bool Movie::FRemActr()
{
    AssertThis(0); AssertPo(Pscen(), 0);
    const auto services = _EditServices();
    MMMovieEditRequest q = {};

    return mm_movie_edit_actor(MM_MOVIE_EDIT_REMOVE, &q, &services) != 0;
}

/***************************************************************************
 *
 * Rotates selected actor by degrees around an axis.
 *
 * Parameters:
 *  axis - Brender axis to rotate around.
 *	xa - Degrees to rotate by in X.
 *	ya - Degrees to rotate by in Y.
 *	za - Degrees to rotate by in Z.
 *
 * Returns:
 *  fTrue if successful, else fFalse.
 *
 **************************************************************************/
bool Movie::FRotateActr(BRA xa, BRA ya, BRA za, bool fFromHereFwd)
{
    AssertThis(0); AssertPo(Pscen(), 0);
    const auto services = _EditServices();
    MMMovieEditRequest q = {};
    q.rotation[0] = xa; q.rotation[1] = ya; q.rotation[2] = za; q.forward = fFromHereFwd;
    return mm_movie_edit_actor(MM_MOVIE_EDIT_ROTATE, &q, &services) != 0;
}

/***************************************************************************
 *
 * Squashes/Stretches the selected actor by a scalar.
 *
 * Parameters:
 *  brs - The scalar for squashing.
 *
 * Returns:
 *  fTrue if successful, else fFalse.
 *
 **************************************************************************/
bool Movie::FSquashStretchActr(BRS brs)
{
    AssertThis(0); AssertPo(Pscen(), 0);
    const auto services = _EditServices();
    MMMovieEditRequest q = {};
    q.factor = brs;
    return mm_movie_edit_actor(MM_MOVIE_EDIT_SQUASH, &q, &services) != 0;
}

/***************************************************************************
 *
 * Drags the actor in time
 *
 * Parameters:
 *  nfrm - The destination frame number.
 *
 * Returns:
 *  fTrue if successful, else fFalse.
 *
 **************************************************************************/
bool Movie::FSoonerLaterActr(long nfrm)
{
    AssertThis(0);
    const auto services = _EditServices();
    MMMovieEditRequest request{};
    request.frame = nfrm;
    return mm_movie_edit_actor(MM_MOVIE_EDIT_TIME, &request, &services) != 0;
}

bool Movie::FStepActorTime(long frames)
{
    AssertThis(0);
    const auto services = _EditServices();
    return mm_movie_step_actor_time(frames, &services) != 0;
}

/***************************************************************************
 *
 * Scales the selected actor by a scalar.
 *
 * Parameters:
 *  brs - The scalar for scaling.
 *
 * Returns:
 *  fTrue if successful, else fFalse.
 *
 **************************************************************************/
bool Movie::FScaleActr(BRS brs)
{
    AssertThis(0); AssertPo(Pscen(), 0);
    const auto services = _EditServices();
    MMMovieEditRequest q = {};
    q.factor = brs;
    return mm_movie_edit_actor(MM_MOVIE_EDIT_SCALE, &q, &services) != 0;
}

/****************************************************
 *
 * Adds a sound to the background
 *
 * Parameters:
 *  ptag - tag to the MovieSoundMSND to insert
 *  fLoop - play snd over and over?
 *  fQueue - replace existing sounds, or queue afterwards?
 *  vlm - volume to play sound
 *  sty - sound type
 *
 * Returns:
 *  fFalse if there was a failure, else fTrue.
 *
 ****************************************************/
bool Movie::FAddBkgdSnd(PTAG ptag, tribool fLoop, tribool fQueue, long vlm, long sty)
{
    AssertThis(0); AssertPo(Pscen(), 0);
    const auto services = _EditServices();
    MMMovieEditRequest q = {};
    MMTag tag = {}; if (ptag) tag = TagToCore(*ptag); q.tag = ptag ? &tag : nullptr; q.loop = fLoop; q.queue = fQueue; q.volume = vlm; q.style = sty;
    return mm_movie_edit_scene(MM_MOVIE_EDIT_BACKGROUND_SOUND, &q, &services) != 0;
}

/****************************************************
 *
 * Adds a sound to an actor
 *
 * Parameters:
 *  pactr - actor to attach sound to
 *  ptag - tag to the MovieSoundMSND to insert
 *  fLoop - play snd over and over?
 *  fQueue - replace existing sounds, or queue afterwards?
 *  vlm - volume to use (vlmNil -> use pmsnd volume)
 *  sty - type to use (styNil -> use pmsnd sty)
 *
 * Returns:
 *  fFalse if there was a failure, else fTrue.
 *
 ****************************************************/
bool Movie::FAddActrSnd(PTAG ptag, tribool fLoop, tribool fQueue, tribool fActnCel, long vlm, long sty)
{
    AssertThis(0); AssertPo(Pscen(), 0);
    const auto services = _EditServices();
    MMMovieEditRequest q = {};
    MMTag tag = {}; if (ptag) tag = TagToCore(*ptag); q.tag = ptag ? &tag : nullptr; q.loop = fLoop; q.queue = fQueue; q.cel = fActnCel; q.volume = vlm; q.style = sty;
    return mm_movie_edit_actor(MM_MOVIE_EDIT_ACTOR_SOUND, &q, &services) != 0;
}

/***************************************************************************
 *
 * Adds a new scene after the current scene.
 *
 * Parameters:
 *	ptag - The tag of the scene to add.
 *
 * Returns:
 *  fTrue if successful, else fFalse.
 *
 **************************************************************************/
bool Movie::FAddScen(PTAG ptag)
{
    AssertThis(0); AssertPvCb(ptag, size(TAG));
    const auto services = _EditServices();
    const auto scenes = _SceneServices();
    const auto tag = TagToCore(*ptag);
    return mm_movie_add_background(&tag, &services, &scenes) != 0;
}

struct Movie::AlarmHost
{
    PMovie movie;
    PMovieView view;
};

MMAlarmPlaybackServices Movie::_AlarmServices(AlarmHost *host)
{
    MMAlarmPlaybackServices s = {};
    s.context = host;
    s.wall_time = [](void *) -> MMPlaybackU32 { return TsCurrent(); };
    s.clock_time = [](void *p) -> MMPlaybackU32 { return static_cast<AlarmHost *>(p)->movie->_clok.TimCur(); };
    s.start_clock = [](void *p, MMPlaybackU32 time) { static_cast<AlarmHost *>(p)->movie->_clok.Start(time); };
    s.stop_clock = [](void *p) { static_cast<AlarmHost *>(p)->movie->_clok.Stop(); };
    s.set_alarm = [](void *p, MMPlaybackI32 delay, int adjust) -> int {
        auto movie = static_cast<AlarmHost *>(p)->movie;
        return movie->_clok.FSetAlarm(delay, movie, 0, adjust != 0);
    };
    s.tracking = [](void *) -> int { return vpcex->PgobTracking() != pvNil; };
    s.pause_audio = [](void *, int paused) {
        if (vpsndm)
        {
            if (paused)
                vpsndm->PauseAll();
            else
                vpsndm->ResumeAll();
        }
    };
    s.audio_fades_disabled = MovieFadesDisabled;
    s.set_volume = SetMovieVolume;
    s.effect = [](void *p, MMAlarmPlaybackEffect effect, int value) {
        auto host = static_cast<AlarmHost *>(p);
        auto movie = host->movie;
        switch (effect)
        {
        case MM_ALARM_QUEUE_RENDER:
        {
            Command cmd = {};
            cmd.pcmh = movie;
            cmd.cid = cidRender;
            vpcex->EnqueueCmd(&cmd);
            break;
        }
        case MM_ALARM_INVALIDATE_VIEWS: movie->InvalViews(); break;
        case MM_ALARM_ENABLE_ACCELERATORS: movie->Pmcc()->EnableAccel(); break;
        case MM_ALARM_REMOVE: movie->_clok.RemoveCmh(movie); break;
        case MM_ALARM_CLEAR_PLAYING:
#ifdef DEBUG
            movie->SetFWriteBmps(false);
#endif
            break;
        case MM_ALARM_CLEAR_CLICK_PAUSE:
        {
            auto view = host->view ? host->view : static_cast<PMovieView>(movie->PddgGet(0));
            view->PauseUntilClick(false);
            break;
        }
        case MM_ALARM_RESET_SCENE:
        {
            auto scene = movie->Pscen();
            if (scene || value)
            {
                AssertPo(scene, 0);
                scene->Enable(fscenTboxes);
                scene->Disable(fscenPauses);
            }
            else
                Assert(movie->_scenes.index == ivNil, "Bogus scene state");
            break;
        }
        case MM_ALARM_REFRESH_VIEWS: movie->InvalViewsAndScb(); break;
        case MM_ALARM_STOP_SCENE:
            if (movie->Pscen() || value)
                movie->Pscen()->StopPlaying();
            break;
        case MM_ALARM_PLAYING_NOTICE: vpcex->EnqueueCid(cidMviePlaying, pvNil, pvNil, value); break;
        case MM_ALARM_SHORT_SOUNDS: movie->Pmsq()->SndOnShort(); break;
        case MM_ALARM_NOTIFY_STOPPED: movie->Pmcc()->PlayStopped(); break;
        case MM_ALARM_STOP_AUDIO: vpsndm->StopAll(); break;
        }
    };
    return s;
}

/***************************************************************************
 *
 * Plays a movie.
 *
 * Parameters:
 *	None
 *
 * Returns:
 *  None.
 *
 **************************************************************************/
void Movie::Play()
{
    AssertThis(0);
    if (mm_alarm_playback_stop_frozen(&_runtime.transport, &_playback))
    {
        FCmdRender(nullptr); // The frozen clock cannot deliver the ordinary stop alarm.
        return;
    }
    AlarmHost host{this, nullptr};
    const auto alarm = _AlarmServices(&host);
    MMPlayEvaluator evaluator;
    mm_play_init(&evaluator, &_playback);
    for (;;)
    {
        MMPlayCommand command{MM_PLAY_FINISHED, 0};
        AssertDo(mm_play_next(&evaluator, &command), "Unacknowledged play command");
        if (command.kind == MM_PLAY_FINISHED)
            return;
        MMPlaybackReply reply;
        mm_playback_reply_init(&reply);
        switch (command.kind)
        {
        case MM_PLAY_RENDER_STOP:
            FCmdRender(pvNil);
            break;
        case MM_PLAY_CHECK_SCENE:
            reply.value = Pscen() != pvNil;
            break;
        case MM_PLAY_RESET_SOUNDS:
            Pmsq()->StopAll();
            Pmsq()->FlushMsq();
            break;
        case MM_PLAY_QUERY_END:
            reply.value = Iscen() + 1 == Cscen() && Pscen()->Nfrm() == Pscen()->NfrmLast();
            break;
        case MM_PLAY_REWIND:
            reply.success = FSwitchScen(0);
            if (reply.success)
                reply.success = Pscen()->FGotoFrm(Pscen()->NfrmFirst());
            break;
        case MM_PLAY_READ_TIME:
            reply.timestamp = TsCurrent();
            break;
        case MM_PLAY_START_CLOCK:
            reply.success = mm_alarm_playback_start(&_runtime.transport, &_playback,
                                                    CurrentStudioPreferences().PlaybackFps(), &alarm);
            break;
        case MM_PLAY_PREPARE_SCENE:
            Pmsq()->SndOnLong();
            Pscen()->Enable(fscenPauses);
            Pscen()->SelectActr(pvNil);
            Pscen()->SelectTbox(pvNil);
            // Replay must precede prerendering: replayed camera changes invalidate it.
            reply.success = Pscen()->FReplayFrm(fscenPauses | fscenSounds | fscenActrs) && Pscen()->FStartPlaying();
            break;
        case MM_PLAY_NOTIFY_STOPPED:
            Pmcc()->PlayStopped();
            break;
        case MM_PLAY_STARTUP_FAILED:
            Pmcc()->PlayStopped();
            if (command.flag)
                SetFStopPlaying(true);
            else
                SetFPlaying(false);
            Pmsq()->PlayMsq();
            SetFSoundsEnabled(_playback.oldSoundsEnabled);
            break;
        case MM_PLAY_DISABLE_ACCELERATORS:
            Pmcc()->DisableAccel();
            break;
        case MM_PLAY_QUERY_OPENING:
            reply.value = Iscen() == 0 && Pscen()->Nfrm() == Pscen()->NfrmFirst();
            break;
        case MM_PLAY_REFRESH_OPENING:
            Pmcc()->UpdateScrollbars();
            InvalViews();
            vpappb->UpdateMarked();
            break;
        case MM_PLAY_BACKGROUND_SOUND:
            Pscen()->PlayBkgdSnd();
            break;
        case MM_PLAY_DISPATCH_SOUNDS:
            if (command.flag)
                Pmsq()->PlayMsq();
            else
                Pmsq()->FlushMsq();
            break;
        case MM_PLAY_FINISHED:
            break;
        }
        AssertDo(mm_play_complete(&evaluator, &reply), "No play command to complete");
    }
}

/***************************************************************************
 *
 * Handle an alarm going off.
 *
 * This routine simply enqueues a command to render a frame.  We do it this
 * way because the clock pre-empts commands in the command queue.  If the
 * user mouse clicks, we need to process those clicks.  The best way to do
 * that is by doing rendering via the command queue.
 *
 * Parameters:
 *	pcmd - Pointer to the command to process.
 *
 * Returns:
 *  fTrue if it handled the command, else fFalse.
 *
 ***************************************************************************/
void Movie::SetUserPaused(bool paused)
{
    AlarmHost host{this, nullptr};
    const auto alarm = _AlarmServices(&host);
    mm_alarm_playback_set_paused(&_runtime.transport, &_playback, paused, &alarm);
}

void Movie::SetFIdleSeen(bool idle)
{
    AlarmHost host{this, nullptr};
    const auto alarm = _AlarmServices(&host);
    mm_alarm_playback_idle(&_runtime.transport, &_playback, idle, &alarm);
}

bool Movie::FCmdAlarm(PCommand pcmd)
{
    AssertThis(0);
    AssertVarMem(pcmd);
    AlarmHost host{this, nullptr};
    const auto alarm = _AlarmServices(&host);
    mm_alarm_playback_alarm(&_runtime.transport, &_playback, &alarm);
    return true;
}

/***************************************************************************
 *
 * Handle rendering a frame.
 *
 * This routine gets a little busy.  The basic premise is to render a
 * frame one frame ahead of the one currently displayed.  This means
 * that only actor stuff gets done first, then when the frame is to be
 * displayed, textboxes and sounds get started.  If there is a scrolling
 * text box, then we play nothing else until the scrolling is done, and
 * (finally) pauses in the frame.
 *
 * Parameters:
 *	pcmd - Pointer to the command to process.
 *
 * Returns:
 *  fTrue if it handled the command, else fFalse.
 *
 ***************************************************************************/
bool Movie::FCmdRender(PCommand pcmd)
{
    AssertThis(0);
    AssertNilOrVarMem(pcmd);
    if (mm_alarm_playback_defer_render(&_runtime.transport, &_playback))
        return true;
    const auto started = TsCurrent();
    PMovieView view = (PMovieView)PddgGet(0);
    AssertPo(view, 0);
    _playback.suppressAudioFades = MovieFadesDisabled(nullptr);
    AlarmHost host{this, view};
    const auto alarm = _AlarmServices(&host);
    MMPlaybackEvaluator evaluator;
    mm_playback_init(&evaluator, &_playback, started);
    for (;;)
    {
        MMPlaybackCommand command{MM_PLAYBACK_FINISHED, 0, 0};
        AssertDo(mm_playback_next(&evaluator, &command), "Unacknowledged playback command");
        if (command.kind == MM_PLAYBACK_FINISHED)
            return true;
        MMPlaybackReply reply;
        mm_playback_reply_init(&reply);
        switch (command.kind)
        {
        case MM_PLAYBACK_STOP:
            mm_alarm_playback_stop(&_runtime.transport, &_playback, &alarm);
            break;
        case MM_PLAYBACK_READ_VOLUME:
            reply.number = vpsndm->VlmCur();
            break;
        case MM_PLAYBACK_SET_VOLUME:
            vpsndm->SetVlm(command.value);
            break;
        case MM_PLAYBACK_SCROLL_TEXT:
            for (long index = 0;; ++index)
            {
                PTBOX caption = Pscen()->PtboxFromItbox(index);
                AssertNilOrPo(caption, 0);
                if (caption == pvNil)
                    break;
                if (caption->FNeedToScroll())
                {
                    caption->Scroll();
                    _playback.scrolling = true;
                    reply.value = true;
                }
            }
            if (_playback.unpaced)
            {
                vpappb->UpdateMarked();
#if defined(DEBUG) || defined(STUDIO_TEST_TOOLS)
                ++_playbackTest.scrollSteps;
#endif
            }
            break;
        case MM_PLAYBACK_PRESENT_FRAME:
            Pscen()->Enable(fscenTboxes);
            Pscen()->Disable(fscenActrs);
            reply.success = Pscen()->FGotoFrm(Pscen()->Nfrm());
            Pscen()->Enable(fscenActrs);
            break;
        case MM_PLAYBACK_DISPLAY_FRAME:
            MarkViews();
#if defined(DEBUG) || defined(STUDIO_TEST_TOOLS)
            if (_playbackFrameObserver)
                _playbackFrameObserver(this);
#endif
            Pmcc()->UpdateScrollbars();
            if (FSoundsEnabled())
                Pmsq()->PlayMsq();
            else
                Pmsq()->FlushMsq();
            vpappb->UpdateMarked();
#if defined(DEBUG) || defined(STUDIO_TEST_TOOLS)
            if (_playbackTest.active)
                _RecordPlaybackTestFrame();
#endif
            break;
        case MM_PLAYBACK_QUERY_SCROLLING:
            for (long index = 0;; ++index)
            {
                PTBOX caption = Pscen()->PtboxFromItbox(index);
                AssertNilOrPo(caption, 0);
                if (caption == pvNil)
                    break;
                if (caption->FNeedToScroll())
                {
                    reply.value = true;
                    break;
                }
            }
            break;
        case MM_PLAYBACK_SET_CLICK_PAUSE:
            view->PauseUntilClick(command.flag);
            break;
        case MM_PLAYBACK_QUERY_SOUND:
            reply.value = Pmsq()->FPlaying(false);
            break;
        case MM_PLAYBACK_READ_TIME:
            reply.timestamp = TsCurrent();
            break;
        case MM_PLAYBACK_SET_ALARM:
            reply.success = mm_alarm_playback_schedule(&_runtime.transport, &_playback, started,
                                                       command.value, command.flag, &alarm);
            break;
        case MM_PLAYBACK_READ_POSITION:
            Pscen()->Disable(fscenTboxes);
            reply.position = {static_cast<LegacyLong>(Iscen()), static_cast<LegacyLong>(Cscen()),
                              static_cast<LegacyLong>(Pscen()->Nfrm()), static_cast<LegacyLong>(Pscen()->NfrmLast()),
                              Pscen()->Trans()};
            break;
        case MM_PLAYBACK_SWITCH_SCENE:
            if (_playback.unpaced)
                std::fprintf(stderr, "BATCH_SCENE_LOAD scene=%ld\n", static_cast<long>(command.value));
            reply.success = FSwitchScen(command.value);
            break;
        case MM_PLAYBACK_REPLAY_SCENE:
            AssertPo(Pscen(), 0);
            Pscen()->Disable(fscenTboxes);
            Pscen()->Enable(fscenPauses);
            reply.success = Pscen()->FReplayFrm(fscenPauses | fscenSounds | fscenActrs);
            if (reply.success)
                Pscen()->PlayBkgdSnd();
            break;
        case MM_PLAYBACK_ADVANCE_FRAME:
            if (_playback.unpaced)
                std::fprintf(stderr, "BATCH_FRAME scene=%ld frame=%ld\n", Iscen(), static_cast<long>(command.value));
            reply.success = Pscen()->FGotoFrm(command.value);
            break;
        case MM_PLAYBACK_RENDER_WORLD:
            RenderWorld();
            break;
        case MM_PLAYBACK_FINISHED:
            break;
        }
        if (_playback.unpaced && !reply.success)
        {
#if defined(DEBUG) || defined(STUDIO_TEST_TOOLS)
            _playbackTest.failed = true;
            _playbackTest.error = "playback_command_failed";
            _playbackTest.failureCommand = static_cast<LegacyLong>(command.kind);
#endif
        }
        AssertDo(mm_playback_complete(&evaluator, &reply), "No playback command to complete");
    }
}

#if defined(DEBUG) || defined(STUDIO_TEST_TOOLS)
bool Movie::FBeginPlaybackTest()
{
    if (FPlaying() || _playbackTest.active)
        return false;
    _playbackTest = MoviePlaybackTestProgress();
    _playbackTest.active = true;
    _playbackTest.started = TsCurrentSystem();
    _playbackTest.originalVolume = vpsndm->VlmCur();
    _playback.unpaced = true;
    _playback.skippedClickPauses = _playback.skippedTimedPauses = _playback.skippedSoundPauses = 0;
    vpsndm->SetVlm(0); // Keep asset loading/decoding, without audible playback.
    std::fprintf(stderr, "BATCH_START scenes=%ld\n", Cscen());
    if (!Cscen() || !FSwitchScen(0) || !Pscen()->FGotoFrm(Pscen()->NfrmFirst()))
    {
        _playbackTest.failed = true;
        _playbackTest.error = "cannot_prepare_first_scene";
        _FinishPlaybackTest();
        return false;
    }
    std::fprintf(stderr, "BATCH_FRAME scene=0 frame=%ld\n", Pscen()->NfrmFirst());
    Play();
    if (!FPlaying())
    {
        _playbackTest.failed = true;
        _playbackTest.error = "cannot_start_playback";
        _FinishPlaybackTest();
        return false;
    }
    return true;
}
void Movie::_RecordPlaybackTestFrame()
{
    auto &test = _playbackTest;
    if (test.scene != Iscen())
    {
        if (Iscen() != test.scene + 1 || (test.scene >= 0 && test.framesInScene !=
            static_cast<std::uint64_t>(std::int64_t(test.last) - test.first + 1)))
        {
            test.failed = true;
            test.error = "incomplete_scene_coverage";
            return;
        }
        test.scene = static_cast<LegacyLong>(Iscen());
        test.first = static_cast<LegacyLong>(Pscen()->NfrmFirst());
        test.last = static_cast<LegacyLong>(Pscen()->NfrmLast());
        test.framesInScene = 0;
        if (test.last < test.first)
        {
            test.failed = true;
            test.error = "invalid_scene_frame_range";
            return;
        }
        ++test.scenes;
        test.expectedFrames += std::uint64_t(std::int64_t(test.last) - test.first + 1);
        std::fprintf(stderr, "BATCH_SCENE scene=%ld first=%ld last=%ld\n", static_cast<long>(test.scene),
                     static_cast<long>(test.first), static_cast<long>(test.last));
    }
    if (std::int64_t(Pscen()->Nfrm()) != std::int64_t(test.first) + test.framesInScene)
    {
        test.failed = true;
        test.error = "nonsequential_movie_frame";
        return;
    }
    test.frame = static_cast<LegacyLong>(Pscen()->Nfrm());
    ++test.framesInScene;
    ++test.frames;
}
void Movie::_FinishPlaybackTest()
{
    if (FPlaying())
    {
        SetFStopPlaying(true);
        FCmdRender(pvNil);
    }
    _playbackTest.elapsed = TsCurrentSystem() - _playbackTest.started;
    _playbackTest.active = false;
    _playback.unpaced = false;
    vpsndm->StopAll();
    vpsndm->SetVlm(_playbackTest.originalVolume);
}
void Movie::StepPlaybackTest(LegacyLong frameBudget, LegacyUlong milliseconds)
{
    if (!_playbackTest.active)
        return;
    const auto start = TsCurrentSystem();
    const auto initial = _playbackTest.frames;
    do
    {
        ++_playbackTest.steps;
        FCmdRender(pvNil);
        if (vpers && vpers->Cerc())
        {
            _playbackTest.failed = true;
            _playbackTest.error = "playback_error_stack";
        }
        if (_playbackTest.failed || !FPlaying())
        {
            _playbackTest.complete = !_playbackTest.failed && _playbackTest.scenes == Cscen() &&
                _playbackTest.scene + 1 == Cscen() && _playbackTest.frame == _playbackTest.last &&
                _playbackTest.frames == _playbackTest.expectedFrames;
            if (!_playbackTest.complete && !_playbackTest.failed)
            {
                _playbackTest.failed = true;
                _playbackTest.error = "playback_stopped_before_end";
            }
            _FinishPlaybackTest();
            return;
        }
    } while (_playbackTest.frames - initial < static_cast<unsigned>(frameBudget) &&
             TsCurrentSystem() - start < milliseconds);
    _playbackTest.elapsed = TsCurrentSystem() - _playbackTest.started;
}
void Movie::StopPlaybackTest()
{
    if (!_playbackTest.active)
        return;
    _playbackTest.failed = true;
    _playbackTest.error = "cancelled";
    _FinishPlaybackTest();
}
#endif

/***************************************************************************
 *
 * This sets the costume of an actor.
 *
 * Parameters:
 *	ibprt - Id of the body part to set.
 *  ptag - Pointer to the tag of the costume, if fCustom != fTrue.
 *  cmid - The cmid of the costume, if fCustom == fTrue.
 *  fCustom - Is this a custom costume.
 *
 * Returns:
 *  fTrue if successful, else fFalse.
 *
 **************************************************************************/
bool Movie::FCostumeActr(long ibprt, PTAG ptag, long cmid, tribool fCustom)
{
    AssertThis(0); AssertPo(Pscen(), 0);
    const auto services = _EditServices();
    MMMovieEditRequest q = {};
    MMTag tag = {}; if (ptag) tag = TagToCore(*ptag); q.tag = ptag ? &tag : nullptr; q.part = ibprt; q.costume = cmid; q.custom = fCustom;
    return mm_movie_edit_actor(MM_MOVIE_EDIT_COSTUME, &q, &services) != 0;
}

/***************************************************************************
 *
 * This inserts a pause into the scene right now.
 *
 * Parameters:
 *	wit - The type of pause to insert, or witNil to clear pauses.
 *  dts - Number of clock ticks to pause.
 *
 * Returns:
 *  fTrue if successful, else fFalse.
 *
 **************************************************************************/
bool Movie::FPause(WaitReason wit, long dts)
{
    AssertThis(0); AssertPo(Pscen(), 0);
    const auto services = _EditServices();
    MMMovieEditRequest q = {};
    q.reason = wit; q.delay = dts;
    return mm_movie_edit_scene(MM_MOVIE_EDIT_PAUSE, &q, &services) != 0;
}

static void ReleaseComposeObject(void *object)
{
    static_cast<BASE *>(object)->Release();
}

MMMovieComposeServices Movie::_ComposeServices()
{
    static_assert(kctgMvie==0x4d564945U && kctgActr==0x41435452U && cnoNil==0xffffffffU, "Movie composition ABI");
    MMMovieComposeServices s = {};
    s.context = this;
    s.data = &_data;
    s.scenes = &_scenes;
    s.scene_control = _SceneServices();
    s.allocator = CollectionAllocator();
    s.archive = [](void *file) { return static_cast<PChunkyFile>(file)->WriteIndex(); };
    s.titles = KauaiChunkTitleIO();
    s.rollcall = MovieData::_RollCallBinding();
    s.retain = [](void *object) { static_cast<BASE *>(object)->AddRef(); };
    s.release = ReleaseComposeObject;
#ifdef BUG1929
    s.temporary_before_save=1;
#endif
    s.use_temporary=[](void *p)->int { return static_cast<PMovie>(p)->_FUseTempFile(); };
    s.autosave=[](void *p)->int { return static_cast<PMovie>(p)->FAutoSave(nullptr,false); };
    s.transition=[](void *p)->MMI32 { return static_cast<PScene>(p)->Trans(); };
    s.decode_thumbnail = [](void *, const MMReadStream *stream) -> void * {
        return MaskedBitmapMBMP::PmbmpReadStream(stream);
    };
    s.thumbnail=[](void *p)->void* { return static_cast<PScene>(p)->PmbmpThumbnail(); };
    s.sound_issue = MovieSoundIssue;
    s.start_scene=[](void *p)->int { return static_cast<PScene>(p)->FPlayStartEvents(true); };
    s.add_actors=[](void *p)->int { return static_cast<PScene>(p)->FAddActrsToRollCall(); };
    s.remove_actors=[](void *p) { static_cast<PScene>(p)->RemActrsFromRollCall(true); };
    s.actor_tools=[](void *p) { static_cast<PMovie>(p)->Pmcc()->EnableActorTools(); };
    s.text_tools=[](void *p) { static_cast<PMovie>(p)->Pmcc()->EnableTboxTools(); };
    s.set_transition=[](void *p,MMI32 transition) { static_cast<PScene>(p)->SetTransitionCore(TRANS(transition)); };
    s.dirty=[](void *p) { static_cast<PMovie>(p)->SetDirty(); };
    s.scene_nuked=[](void *p) { static_cast<PMovie>(p)->Pmcc()->SceneNuked(); };
    s.invalidate=[](void *p) { static_cast<PMovie>(p)->InvalViewsAndScb(); };
    s.rollcall_changed=[](void *p) { static_cast<PMovie>(p)->Pmcc()->UpdateRollCall(); };
    s.diagnostic=[](void *,int issue) {
        switch(issue) {
        case MM_COMPOSE_MISSING_SCENE: AssertDo(false,"Not enough scene chunks for movie"); break;
        case MM_COMPOSE_BAD_BASE: Assert(false,"Invalid base movie descriptor"); break;
        case MM_COMPOSE_IMPORT_ACTORS: PushErc(ercSocNoImportRollCall); break;
        case MM_COMPOSE_REMOVE_ACTORS: PushErc(ercSocNoNukeRollCall); break;
        case MM_COMPOSE_MISSING_THUMBNAIL: Bug("Scene doesn't have a thumbnail"); break;
        case MM_COMPOSE_SCENE_ENUM: Bug("Can't guarantee validity of Movie's Scene children"); break;
        case MM_COMPOSE_SCENE_ORDER: Assert(false, "Found duplicate ChildChunkID in scene children"); break;
        case MM_COMPOSE_MISSING_COPY_ROOT: Bug("chunk not found"); break;
        }
    };
#ifdef DEBUG
    s.validate_children = 1;
#endif
    return s;
}


#ifdef DEBUG
/******************************************************************************
    MarkMem
        Marks memory used by the CompositeMovie

************************************************************ PETED ***********/
void CompositeMovie::MarkMem(void)
{
    for (MMU32 i = 0; i < movie_count; ++i)
    {
        MMComposeMovie movie;
        mm_composite_movie(this, i, &movie);
        MarkMemObj(static_cast<PChunkyResourceFile>(movie.resources));
    }
    for (MMU32 i = 0; i < scene_count; ++i)
    {
        MMComposeScene scene;
        mm_composite_scene(this, i, &scene);
        MarkMemObj(static_cast<PMaskedBitmapMBMP>(scene.thumbnail));
    }
    MarkPv(rows.buffers[0].data);
    MarkPv(rows.buffers[1].data);
}
#endif // DEBUG

/******************************************************************************
    FAddToCmvi
        Adds the movie and its scene descriptors to a composition.
        The caller can rearrange scenes and add other movies before applying it.

    Arguments:
        PCompositeMovie pcmvi     --  the CompositeMovie to add the movie to
        long iscendIns  --  the point at which to start inserting scenes

    Returns: fTrue if it was successful, fFalse otherwise

************************************************************ PETED ***********/
bool Movie::FAddToCmvi(PCompositeMovie pcmvi, long *piscendIns)
{
    AssertThis(0); AssertVarMem(pcmvi); AssertVarMem(piscendIns);
    MMI32 insertion = *piscendIns;
    const auto services = _ComposeServices();
    bool ok = mm_movie_compose_add(pcmvi, &insertion, &services) != 0;
    *piscendIns = insertion;
    return ok;
}

/******************************************************************************
    FSetCmvi
        Rebuilds the movie based on the given CompositeMovie.  Any scenes
        marked for deletion are disowned by their Movie chunk.  Any scenes that
        refer to a movie file other than this Movie's auto save file are
        copied into this Movie's auto save file.  Scene chunks are given new
        CHIDs reflecting their new position within the movie.  The non-nuked
        scenes must appear in the composition in the order that they appear in the
        movie; other than that, there is no restriction on the order of the
        scenes (ie, nuked scenes can appear anywhere in the composition, even though
        currently the only client of this API keeps the nuked scenes at the
        end).

    Arguments:
        PCompositeMovie pcmvi -- the CompositeMovie that describes the new movie structure

    Returns: fTrue if it could accomplish all of the above, fFalse otherwise

************************************************************ PETED ***********/
bool Movie::FSetCmvi(PCompositeMovie pcmvi)
{
    AssertThis(0);
    AssertVarMem(pcmvi);
    const auto services = _ComposeServices();
    return mm_movie_compose_apply(pcmvi, &services) != 0;
}

/******************************************************************************
    _FAddMvieToRollCall
        Updates roll call (including remapping arids for the actors found in
        the new movie) for a given Movie that's just been copied into this
        movie's file.

    Arguments:
        ChunkNumber cno       -- the ChunkNumber of the copied movie
        long aridMin  -- the new base arid for this movie's actors

    Returns: fTrue if it succeeds, fFalse otherwise

************************************************************ PETED ***********/
bool Movie::_FAddMvieToRollCall(ChunkNumber cno, long aridMin)
{
    AssertThis(0); const auto services=_ComposeServices();
    return mm_movie_compose_merge(cno,aridMin,&services)!=0;
}

/******************************************************************************
    EmptyCmvi
        Releases the retained thumbnails, resources and descriptor storage.

************************************************************ PETED ***********/
void CompositeMovie::Empty(void)
{
    mm_composite_empty(this, ReleaseComposeObject);
}

/***************************************************************************
 *
 * This sets the transition type for the current scene.
 *
 * Parameters:
 *	trans - The transition type.
 *
 * Returns:
 *  fTrue if successful, else fFalse.
 *
 **************************************************************************/
bool Movie::FSetTransition(TRANS trans)
{
    AssertThis(0); AssertPo(Pscen(), 0); AssertIn(trans, 0, transLim);
    const auto services = _EditServices();
    MMMovieEditRequest q = {};
    q.transition = trans;
    return mm_movie_edit_scene(MM_MOVIE_EDIT_TRANSITION, &q, &services) != 0;
}

/***************************************************************************
 *
 * This pastes an actor into the movie.
 *
 * Parameters:
 *	pactr - A pointer to the actor to paste.
 *
 * Returns:
 *  fTrue if successful, else fFalse.
 *
 **************************************************************************/
bool Movie::FPasteActr(PActor pactr)
{
    AssertThis(0); AssertPo(pactr, 0); AssertPo(Pscen(), 0);
    const auto services = _EditServices();
    return mm_movie_paste_actor(pactr, &services) != 0;
}

/***************************************************************************
 *
 * This pastes an actor path onto the selected actor.
 *
 * Parameters:
 *	pactr - A pointer to the actor to paste from.
 *
 * Returns:
 *  fTrue if successful, else fFalse.
 *
 **************************************************************************/
bool Movie::FPasteActrPath(PActor pactr)
{
    AssertThis(0); AssertPo(pactr, 0); AssertPo(Pscen(), 0);
    const auto services = _EditServices();
    return mm_movie_paste_route(pactr, &services) != 0;
}

/***************************************************************************
 *
 * This invalidates all the views on the movie.
 *
 * Parameters:
 *	None.
 *
 * Returns:
 *  None.
 *
 **************************************************************************/
void Movie::InvalViews(void)
{
    AssertThis(0);

    long ipddg;
    PDocumentDisplayGraphicsObject pddg;

    for (ipddg = 0; pvNil != (pddg = PddgGet(ipddg)); ipddg++)
    {
        pddg->InvalRc(pvNil, pddg == PddgActive() ? kginMark : kginSysInval);
    }
}

/***************************************************************************
 *
 * This invalidates scrollbars and all the views on the movie.
 *
 * Parameters:
 *	None.
 *
 * Returns:
 *  None.
 *
 **************************************************************************/
void Movie::InvalViewsAndScb(void)
{
    AssertThis(0);

    Pmcc()->UpdateScrollbars();
    InvalViews();
}

void Movie::RenderWorld()
{
    auto scene = Pscen();
    Pbwld()->Render(MovieSession(), scene ? scene->PlaybackFrame() : nullptr);
}

/***************************************************************************
 *
 * This updates all the views on the movie, updates will happen
 * asap.
 *
 * Parameters:
 *	None.
 *
 * Returns:
 *  None.
 *
 **************************************************************************/
void Movie::MarkViews(void)
{
    AssertThis(0);

    long ipddg;
    PDocumentDisplayGraphicsObject pddg;

    //
    // Need this call in order to mark correctly the changed regions.
    //
    RenderWorld();

#ifdef DEBUG
    if (FWriteBmps())
    {
        Filename fni;
        String stn;

        if (stn.FFormatSz(PszLit("cel%04d.dib"), _lwBmp++))
        {
            if (fni.FBuildFromPath(&stn))
            {
                if (!Pbwld()->FWriteBmp(&fni))
                    SetFWriteBmps(fFalse);
            }
        }
    }
#endif // DEBUG

    for (ipddg = 0; pvNil != (pddg = PddgGet(ipddg)); ipddg++)
    {
        Pbwld()->MarkRenderedRegn(pddg, 0, 0);
    }
}

/***************************************************************************
 *
 * This returns the current name of the movie.
 *
 * Parameters:
 *	pstnTitle - An stn to copy the name into.
 *
 * Returns:
 *  None.
 *
 **************************************************************************/
void Movie::GetName(PString pstnTitle)
{
    AssertThis(0);
    AssertPo(pstnTitle, 0);

    _data.GetTitle(pstnTitle);
}

/******************************************************************************
    ResetTitle
        Resets the movie title to whatever it's normal default would be (either
        from the filename or from the MovieClientCallbacks string table).
************************************************************ PETED ***********/
void Movie::ResetTitle(void)
{
    AssertThis(0);

    Filename fni;

    mm_movie_title_set(&_data.title, nullptr, 0);
    _SetTitle(FGetFni(&fni) ? &fni : pvNil);
}

/***************************************************************************
 *
 * Updates the external action menu.
 *
 * Parameters:
 *	None.
 *
 * Returns:
 *  None.
 *
 **************************************************************************/
void Movie::BuildActionMenu()
{
    AssertThis(0);
    long arid = aridNil;

    if (pvNil != _scenes.open && pvNil != Pscen()->PactrSelected())
    {
        arid = Pscen()->PactrSelected()->Arid();
    }
    Pmcc()->ActorSelected(arid);
    Pmcc()->UpdateAction();
}

static_assert(static_cast<int>(transCut) == MM_GRAPHICS_MOVIE_CUT &&
              static_cast<int>(transFadeToBlack) == MM_GRAPHICS_MOVIE_FADE_BLACK &&
              static_cast<int>(transFadeToWhite) == MM_GRAPHICS_MOVIE_FADE_WHITE &&
              static_cast<int>(transDissolve) == MM_GRAPHICS_MOVIE_DISSOLVE &&
              static_cast<int>(transBlack) == MM_GRAPHICS_MOVIE_BLACK, "Authored movie transition values");

/***************************************************************************
 *
 * Does a transition.  Note that the rectangles should be the exact size
 * of the rendered area in order to get pushing, dissolve, etc to look perfect.
 *
 * Note: A cool fun number fact.  If you take a number from 1 - (klwPrime - 1),
 * multiply by klwPrimeRoot and modulo the result by klwPrime, you will get
 * a sequence of numbers which hits every number 1->(klwPrime-1) w/o repeating
 * until every number is hit.  This fact is used to create the dissolve effect.
 *
 * Parameters:
 *	pgnvDst - The destination GraphicsEnvironment.
 *	pgnvSrc - The source GraphicsEnvironment.
 *	prcDst - The clipping rectangle in the destination.
 *	prcSrc - The clipping rectangle in the source.
 *
 * Returns:
 *  None.
 *
 **************************************************************************/
void Movie::DoTrans(PGraphicsEnvironment pgnvDst, PGraphicsEnvironment pgnvSrc, RC *prcDst, RC *prcSrc)
{
    AssertThis(0);
    AssertPo(pgnvDst, 0);
    AssertPo(pgnvSrc, 0);
    AssertVarMem(prcDst);
    AssertVarMem(prcSrc);

    PDynamicArray pglclrSystem = pvNil;
    PDynamicArray pglclrBkgd = pvNil;
    long iclrMin;

    pglclrSystem = GraphicsPort::PglclrGetPalette();
    if (Pscen() == pvNil || !Pscen()->Pbkgd()->FGetPalette(&pglclrBkgd, &iclrMin))
    {
        pglclrBkgd = pvNil;
    }
    if (pvNil != pglclrSystem && pvNil != pglclrBkgd)
    {
        Assert(pglclrBkgd->IvMac() + iclrMin <= pglclrSystem->IvMac(), "Background palette too large");
        static_assert(sizeof(Color) == 4, "Transition palettes use BGRX");
        if (pglclrBkgd->IvMac() && pglclrSystem->IvMac())
            mm_display_palette_overlay(static_cast<unsigned char *>(pglclrSystem->QvGet(0)), pglclrSystem->IvMac(),
                                       iclrMin, static_cast<const unsigned char *>(pglclrBkgd->QvGet(0)),
                                       pglclrBkgd->IvMac());
    }

    struct TransitionHost
    {
        PGraphicsEnvironment dst, src;
        RC *dstRect, *srcRect;
        PDynamicArray palette;
        std::exception_ptr failure;
    } host{pgnvDst, pgnvSrc, prcDst, prcSrc, pglclrSystem, {}};
    const int result = mm_graphics_movie_transition_indexed(
        static_cast<MMI32>(_playback.transition), _playback.unpaced || CurrentStudioPreferences().cutTransitions, &host,
        [](void *p, MMGraphicsMovieIndexedOperation op, const MMGraphicsMovieTransition *effect) -> int {
            auto &h = *static_cast<TransitionHost *>(p);
            try
            {
                switch (op)
                {
                case MM_MOVIE_TRANSITION_FILL_BLACK: h.dst->FillRc(h.dstRect, kacrBlack); break;
                case MM_MOVIE_TRANSITION_SET_PALETTE: GraphicsPort::SetActiveColors(h.palette, fpalIdentity); break;
                case MM_MOVIE_TRANSITION_COPY: h.dst->CopyPixels(h.src, h.srcRect, h.dstRect); break;
                case MM_MOVIE_TRANSITION_DISSOLVE:
                    const auto shade = static_cast<byte>(effect->fill);
                    const auto fill = effect->fill < 0 ? kacrClear : AbstractColor(shade, shade, shade);
                    h.dst->Dissolve(0, 0, fill, h.src, h.srcRect, h.dstRect, effect->dissolve_phase_ms, h.palette);
                    break;
                }
                return 0;
            }
            catch (...) { h.failure = std::current_exception(); return -1; }
        });
    if (!result)
        Bug("bad trans");

    ReleasePpo(&pglclrSystem);
    ReleasePpo(&pglclrBkgd);
    if (host.failure)
        std::rethrow_exception(host.failure);

    _playback.transition = transNil;
}

/***************************************************************************
 *
 * Used to query if an actor or tbox exists.
 *
 * Parameters:
 *	lwType - 1 if searching for an actor, else 0.
 *  lwId - Arid or Itbox to search for.
 *
 * Returns:
 *  1 if exists, else 0.
 *
 **************************************************************************/
long Movie::LwQueryExists(long lwType, long lwId)
{
    AssertThis(0);
    AssertIn(lwType, 0, 2);

    if (Pscen() == pvNil)
    {
        return (0);
    }
    AssertPo(Pscen(), 0);

    if (lwType == 1)
    {
        return (Pscen()->PactrFromArid(lwId) != pvNil ? 1 : 0);
    }

    return (Pscen()->PtboxFromItbox(lwId) != pvNil ? 1 : 0);
}

/***************************************************************************
 *
 * Used to query where an actor or tbox exists.
 *
 * Parameters:
 *	lwType - 1 if searching for an actor, else 0.
 *  lwId - Arid or Itbox to search for.
 *
 * Returns:
 *  -1 if nonexistent or non-visible, else x in high word, y in low word.
 *
 **************************************************************************/
long Movie::LwQueryLocation(long lwType, long lwId)
{
    AssertThis(0);
    AssertIn(lwType, 0, 2);

    PActor pactr;
    PTBOX ptbox;
    LegacyLong xp, yp;
    RC rc;

    if (Pscen() == pvNil)
    {
        return (-1);
    }
    AssertPo(Pscen(), 0);

    if (lwType == 1)
    {
        RC rcBounds;
        long cactGuessPt = 0;
        Random rnd;
        long ibset;

        pactr = Pscen()->PactrFromArid(lwId);
        if (pactr == pvNil)
        {
            return (-1);
        }

        AssertPo(pactr, 0);
        if (!pactr->FIsInView())
        {
            return (-1);
        }

        pactr->GetCenter(&xp, &yp);
        pactr->GetRcBounds(&rcBounds);
        // The center of the actor may not be a selectable point (as in
        // an arched spletter), so try some random points if the center
        // point doesn't select the actor.
        while (pactr != Pscen()->PactrFromPt(xp, yp, &ibset) && cactGuessPt < 1000)
        {
            cactGuessPt++;
            xp = rnd.LwNext(rcBounds.Dxp() + rcBounds.xpLeft);
            yp = rnd.LwNext(rcBounds.Dyp() + rcBounds.ypTop);
        }
        if (cactGuessPt == 1000)
        {
            xp = -1;
            yp = -1;
        }
        return ((xp << 16) | yp);
    }

    ptbox = Pscen()->PtboxFromItbox(lwId);
    if (ptbox == pvNil)
    {
        return (-1);
    }

    AssertPo(ptbox, 0);
    if (!ptbox->FIsVisible())
    {
        return (-1);
    }

    ptbox->GetRc(&rc);
    return ((rc.xpLeft << 16) | rc.ypTop);
}

/***************************************************************************
 *
 * Used to set the position within the movie.
 *
 * Parameters:
 *	lwScene - Scene to go to.
 *  lwFrame - Frame to go to.
 *
 * Returns:
 *  0 if successful, else -1.
 *
 **************************************************************************/
long Movie::LwSetMoviePos(long lwScene, long lwFrame)
{
    AssertThis(0);

    if (!FSwitchScen(lwScene))
    {
        return (-1);
    }

    if (!Pscen()->FGotoFrm(lwFrame))
    {
        return (-1);
    }

    InvalViewsAndScb();

    return (0);
}

/***************************************************************************
 *
 * Unused user sounds in this movie?
 *
 * Parms:
 *     bool pfHaveValid -- bool to take whether any of the unused sounds were
 *                         actually usable
 **************************************************************************/

bool Movie::FUnusedSndsUser(bool *pfHaveValid)
{
    AssertThis(0); AssertNilOrVarMem(pfHaveValid); const auto services=SoundServices(); int valid=0;
    bool unused=mm_movie_sound_unused(pfHaveValid?&valid:nullptr,&services)!=0;
    if (pfHaveValid) *pfHaveValid=valid!=0; return unused;
}

/***************************************************************************
 *
 * Used to set the title of the movie, based on a file name.
 *
 * Parameters:
 *  pfni - File name.
 *
 * Returns:
 *  None.
 *
 **************************************************************************/
void Movie::_SetTitle(PFilename filename)
{
    struct Host { PMovie movie; PFilename filename; } host = {this, filename};
    const MMMovieTitleServices services = {&host,
        [](void *c, MMMovieTitle *title) {
            String value; static_cast<Host *>(c)->filename->GetLeaf(&value);
            AssertDo(mm_movie_title_set(title, value.Prgch(), value.Cch()), "bad leaf title");
        },
        [](void *c, MMMovieTitle *title) {
            String value; static_cast<Host *>(c)->movie->Pmcc()->GetStn(idsEngineDefaultTitle, &value);
            AssertDo(mm_movie_title_set(title, value.Prgch(), value.Cch()), "bad default title");
        },
        [](void *c, MMMovieTitle *title) {
            String value; value.SetRgch(const_cast<achar *>(static_cast<const achar *>(mm_movie_title_text(title))), title->count);
            static_cast<Host *>(c)->movie->Pmcc()->UpdateTitle(&value);
            AssertDo(mm_movie_title_set(title, value.Prgch(), value.Cch()), "bad notified title");
        }};
    mm_movie_title_choose(&_data.title, filename != nullptr, &services);
}

//
//
//
//  BEGIN MovieView GOODIES
//
//
//

BEGIN_CMD_MAP(MovieView, DocumentDisplayGraphicsObject)
ON_CID_GEN(cidCopyRoute, &MovieView::FCmdClip, pvNil)
ON_CID_GEN(cidCutTool, &MovieView::FCmdClip, pvNil)
ON_CID_GEN(cidShiftCut, &MovieView::FCmdClip, pvNil)
ON_CID_GEN(cidCopyTool, &MovieView::FCmdClip, pvNil)
ON_CID_GEN(cidShiftCopy, &MovieView::FCmdClip, pvNil)
ON_CID_GEN(cidPasteTool, &MovieView::FCmdClip, pvNil)
ON_CID_GEN(cidClose, pvNil, pvNil)
ON_CID_GEN(cidSave, &MovieView::FCmdSave, pvNil)
ON_CID_GEN(cidSaveAs, &MovieView::FCmdSave, pvNil)
ON_CID_GEN(cidSaveCopy, &MovieView::FCmdSave, pvNil)
ON_CID_GEN(cidIdle, &MovieView::FCmdIdle, pvNil)
ON_CID_GEN(cidRollOff, &MovieView::FCmdRollOff, pvNil)
ON_CID_GEN(cidKey, &MovieView::FCmdKeyCore, pvNil)
END_CMD_MAP_NIL()

RTCLASS(MovieView)

/****************************************************
 *
 * Destructor for movie view objects
 *
 ****************************************************/
MovieView::~MovieView(void)
{
#ifdef KAUAI_SDL3
    ReleasePpo(&_pgptSuperSize);
    ReleasePpo(&_pgptSuperSizeIndexed);
#endif
    if (_tagTool.sid != ksidInvalid)
        TagManager::CloseTag(&_tagTool);
    _DiscardMoveUndo();
    const auto actors = Actor::_OwnerServices();
    mm_scene_actor_pointer_reset(&_pointerSelection, &actors);
    mm_actor_placement_discard(&_placement, &actors);
    mm_actor_record_gesture_discard(&_recordGesture, &actors);
}

/***************************************************************************
 *
 * Create a new mvu.
 *
 * Parameters:
 *	pmvie - Pointer to the creating movie.
 *  pgcb - Pointer to the creation block describing placement, etc.
 *	dxp - Width of the rendered area.
 *	dyp - Height of the rendered area.
 *
 * Returns:
 *  A pointer to the view, otw pvNil on failure
 *
 ***************************************************************************/
MovieView *MovieView::PmvuNew(PMovie pmvie, PGraphicsObjectBlock pgcb, long dxp, long dyp)
{
    AssertPo(pmvie, 0);
    AssertVarMem(pgcb);

    MovieView *pmvu;

    //
    // Create the new view
    //
    if ((pmvu = NewObj MovieView(pmvie, pgcb)) == pvNil)
        return pvNil;

    //
    // Init it
    //
    if (!pmvu->_FInit())
    {
        ReleasePpo(&pmvu);
        return (pvNil);
    }

    pmvu->SetFRecordDefault(fTrue);
    pmvu->_fRespectGround = fFalse;
    pmvu->_dxp = dxp;
    pmvu->_dyp = dyp;
    pmvu->_tool = toolCompose;
    pmvu->_tagTool.sid = ksidInvalid;

    //
    // Make this the active view
    //
    pmvu->Activate(fTrue);
    return pmvu;
}

/***************************************************************************
 *
 * Set the tool type
 *
 * Parameters:
 *	tool - The new tool to use.
 *
 * Returns:
 *  None.
 *
 ***************************************************************************/
void MovieView::SetTool(long tool)
{
    AssertThis(0);
    AssertPo(Pmvie(), 0);
    AssertNilOrPo(Pmvie()->Pscen(), 0);

    long lwMode; // -1 = Textbox mode, 0 = either mode, 1 = Actor mode
    PTBOX ptbox = pvNil;
    PActor pactr = pvNil;

    if (Pmvie()->Pscen() != pvNil)
    {

        AssertNilOrPo(Pmvie()->Pscen()->PactrSelected(), 0);
        AssertNilOrPo(Pmvie()->Pscen()->PtboxSelected(), 0);

        pactr = Pmvie()->Pscen()->PactrSelected();
        ptbox = Pmvie()->Pscen()->PtboxSelected();

        if (pactr != pvNil)
        {
            _ptmplTool = pactr->Ptmpl();
            AssertPo(_ptmplTool, 0);
        }
    }
    else
    {
        _ptmplTool = pvNil;
    }

    //
    // Get old tool type
    //
    switch (_tool)
    {
    case toolSoonerLater:

        lwMode = 1;
        if (tool != toolSoonerLater)
        {
            Pmvie()->Pmcc()->EndSoonerLater();

            if (pactr && mm_actor_owner_freeze_time(&pactr->_owner, 0))
            {
                pactr->Hilite();
                Pmvie()->InvalViewsAndScb();
            }
        }
        break;

    case toolActorNuke:
    case toolCopyObject:
    case toolCopyRte:
    case toolCutObject:
    case toolPasteObject:
        lwMode = 0;
        break;

    case toolSceneNuke:
    case toolDefault:
    case toolSounder:
    case toolLooper:
    case toolMatcher:
    case toolListener:
    case toolSceneChop:
    case toolSceneChopBack:
    case toolAction:
    case toolActorSelect:
    case toolPlace:
    case toolCompose:
    case toolRecordSameAction:
    case toolRotateX:
    case toolRotateY:
    case toolRotateZ:
    case toolCostumeCmid:
    case toolSquashStretch:
    case toolResize:
    case toolNormalizeRot:
    case toolNormalizeSize:
    case toolActorEasel:

        lwMode = 1;
        break;

    case toolTboxMove:
    case toolTboxUpDown:
    case toolTboxLeftRight:
    case toolTboxFalling:
    case toolTboxRising:
    case toolTboxPaintText:
    case toolTboxFillBkgd:
    case toolTboxStory:
    case toolTboxCredit:
    case toolTboxFont:
    case toolTboxStyle:
    case toolTboxSize:
        lwMode = -1;
        break;

    default:
        Bug("Unknown tool type");
    }

    //
    // Check if we've changed primary tool type
    //
    switch (tool)
    {

    case toolActorNuke:
    case toolCopyObject:
    case toolCopyRte:
    case toolCutObject:
    case toolPasteObject:
        break;

    case toolSoonerLater:
        _fMouseDownSeen = fFalse;

    case toolSceneNuke:
    case toolDefault:
    case toolSounder:
    case toolLooper:
    case toolMatcher:
    case toolListener:
    case toolSceneChop:
    case toolSceneChopBack:
    case toolAction:
    case toolActorSelect:
    case toolPlace:
    case toolCompose:
    case toolRecordSameAction:
    case toolRotateX:
    case toolRotateY:
    case toolRotateZ:
    case toolCostumeCmid:
    case toolSquashStretch:
    case toolResize:
    case toolNormalizeRot:
    case toolNormalizeSize:
    case toolActorEasel:

        _fTextMode = fFalse;

        if (lwMode > 0)
        {
            break;
        }

        if (Pmvie()->Pscen() != pvNil)
        {
            Pmvie()->Pscen()->SelectActr(pactr);
        }
        break;

    case toolTboxMove:
    case toolTboxUpDown:
    case toolTboxLeftRight:
    case toolTboxFalling:
    case toolTboxRising:
    case toolTboxStory:
    case toolTboxCredit:
    case toolTboxFillBkgd:

        _fTextMode = fTrue;

        if (lwMode < 0)
        {
            break;
        }

    LSelectTbox:
        if (Pmvie()->Pscen() != pvNil)
        {
            Pmvie()->Pscen()->SelectTbox(ptbox);
        }
        break;

    case toolTboxPaintText:

        _fTextMode = fTrue;

        if ((lwMode < 0) && (ptbox != pvNil))
        {
            ptbox->FSetAcrText(AcrPaint());
            ptbox->Pscen()->Pmvie()->Pmcc()->PlayUISound(tool);
            break;
        }

        goto LSelectTbox;

    case toolTboxFont:
        _fTextMode = fTrue;
        if ((lwMode < 0) && (ptbox != pvNil))
        {
            ptbox->FSetOnnText(OnnTextCur());
            ptbox->Pscen()->Pmvie()->Pmcc()->PlayUISound(tool);
            break;
        }
        goto LSelectTbox;

    case toolTboxSize:
        _fTextMode = fTrue;
        if ((lwMode < 0) && (ptbox != pvNil))
        {
            ptbox->FSetDypFontText(DypFontTextCur());
            ptbox->Pscen()->Pmvie()->Pmcc()->PlayUISound(tool);
            break;
        }
        goto LSelectTbox;

    case toolTboxStyle:
        _fTextMode = fTrue;
        if ((lwMode < 0) && (ptbox != pvNil))
        {
            ptbox->FSetStyleText(GrfontStyleTextCur());
            ptbox->Pscen()->Pmvie()->Pmcc()->PlayUISound(tool);
            break;
        }
        goto LSelectTbox;

    default:
        Bug("Unknown tool type");
    }

    _tool = tool;
}

/***************************************************************************
 *
 * Change the current loaded tag attached to the cursor.  If the previous
 * tag was a "ksidUseCrf" tag, it must be closed to release its refcount on
 * the tag's pcrf.
 *
 * Parameters:
 *	ptag - The new tag to attach to the cursor
 *
 * Returns:
 *  None.
 *
 ***************************************************************************/
void MovieView::SetTagTool(PTAG ptag)
{
    AssertThis(0);
    AssertVarMem(ptag);

    if (_tagTool.sid != ksidInvalid)
    {
        TagManager::CloseTag(&_tagTool);
    }

#ifdef DEBUG
    // Make sure the new tag has been opened, if it's a "ksidUseCrf" tag
    if (ptag->sid == ksidUseCrf)
    {
        AssertPo(ptag->pcrf, 0);
    }
#endif

    _tagTool = *ptag;
    if (_tagTool.sid != ksidInvalid)
        TagManager::DupTag(ptag);
}

#ifdef KAUAI_SDL3
PGraphicsPort MovieView::PgptSuperSizeFrame(bool redraw, bool indexed)
{
    const auto scale = Pmvie()->Pbwld()->RenderScale();
    if (_fDrawingSuperSize)
        return nullptr;
    if (scale == 1)
    {
        if (redraw)
        {
            ReleasePpo(&_pgptSuperSize);
            ReleasePpo(&_pgptSuperSizeIndexed);
        }
        return nullptr;
    }
    RC native;
    GetRc(&native, cooLocal);
    RC high(0, 0, native.Dxp() * scale, native.Dyp() * scale);
    if (native.FEmpty())
        return nullptr;
    if (!_pgptSuperSizeIndexed || _rcSuperSize != high)
    {
        if (!redraw)
            return nullptr;
        auto indexedPort = GraphicsPort::PgptNewOffscreen(&high, 8);
        if (!indexedPort)
        {
            PushErc(ercGfxCantDraw);
            return nullptr;
        }
        ReleasePpo(&_pgptSuperSize);
        ReleasePpo(&_pgptSuperSizeIndexed);
        _pgptSuperSizeIndexed = indexedPort;
        _rcSuperSize = high;
    }
    if (redraw)
    {
        // Retain the movie palette while drawing the entire subtree, including
        // text boxes. Studio and movie-only playback share the same pixels;
        // the RGB copy is for SDL presentation, not another text rendering pass.
        _fDrawingSuperSize = true;
        DrawTree(_pgptSuperSizeIndexed, &high, nullptr, fgobNoVis);
        _fDrawingSuperSize = false;
        ++_superSizeFrameSerial;
    }
    if (indexed)
        return _pgptSuperSizeIndexed;
    if (!_pgptSuperSize)
    {
        _pgptSuperSize = GraphicsPort::PgptNewOffscreen(&high, 32);
        if (!_pgptSuperSize)
        {
            PushErc(ercGfxCantDraw);
            return nullptr;
        }
        _superSizeRgbSerial = _superSizeFrameSerial - 1;
    }
    if (_superSizeRgbSerial != _superSizeFrameSerial)
    {
        GraphicsEnvironment source(_pgptSuperSizeIndexed), color(_pgptSuperSize);
        color.CopyPixels(&source, &high, &high);
        _superSizeRgbSerial = _superSizeFrameSerial;
    }
    return _pgptSuperSize;
}

bool MovieView::FDrawSubtree(PGraphicsEnvironment pgnv, RC *prcClip, ulong grfgob)
{
    auto imagePort = PgptSuperSizeFrame(!(grfgob & fgobUseCache), pgnv->Pgpt()->CbitPixel() == 8);
    if (!imagePort)
        return false;
    GraphicsEnvironment image(imagePort);
    RC native;
    GetRc(&native, cooLocal);
    RC source = *prcClip;
    source.Map(&native, &_rcSuperSize);
    pgnv->CopyPixels(&image, &source, prcClip);
    return true;
}
#endif

/***************************************************************************
 *
 * Draw this view.
 *
 * Parameters:
 *	pgnv - The environment to write to.
 *  prcClip - The clipping rectangle.
 *
 * Returns:
 *  None.
 *
 ***************************************************************************/
void MovieView::Draw(PGraphicsEnvironment pgnv, RC *prcClip)
{
    AssertThis(0);
    AssertPo(pgnv, 0);
    AssertVarMem(prcClip);

    RC rcDest;

    //
    // Clear non-rendering areas
    //
    if (prcClip->xpRight > _dxp)
    {
        rcDest = *prcClip;
        rcDest.xpLeft = _dxp;
        pgnv->FillRc(&rcDest, kacrWhite);
    }
    if (prcClip->ypBottom > _dyp)
    {
        rcDest = *prcClip;
        rcDest.ypTop = _dyp;
        pgnv->FillRc(&rcDest, kacrWhite);
    }

    //
    // Render
    //
    if (Pmvie()->Pscen() != pvNil)
    {
        Pmvie()->RenderWorld();
        Pmvie()->Pbwld()->Draw(pgnv, prcClip, 0, 0);

        //
        // This draws a currently being dragged out text box frame.
        //
        if (!_rcFrame.FEmpty())
        {
            pgnv->FrameRcApt(&_rcFrame, &vaptLtGray, kacrBlack, kacrWhite);
        }

        //
        // Numeric pose readout for selected actor (UI-2). Shows world X/Y/Z
        // of the primary selection in the top-left of the viewport when the
        // toggle is on (Product Info dialog).
        //
        if (_fShowPoseReadout)
        {
            PActor pactrSel = Pmvie()->Pscen()->PactrSelected();
            if (pactrSel != pvNil && pactrSel->Pbody() != pvNil)
            {
                // Read live BRender transform so the readout tracks during
                // drag. Actor::GetXyzWorld reads committed path data and is
                // stale until mouseup.
                BRS xr, yr, zr;
                String stn;
                pactrSel->Pbody()->GetPosition(&xr, &yr, &zr);
                stn.FFormatSz(PszLit("X=%d Y=%d Z=%d"), BrScalarToInt(xr), BrScalarToInt(yr), BrScalarToInt(zr));
                pgnv->DrawStn(&stn, 4, 4, kacrBlack, kacrWhite);
            }
        }
    }
    else
    {
        rcDest.Set(0, 0, _dxp, _dyp);
        pgnv->FillRc(&rcDest, kacrBlack);
    }
}

/***************************************************************************
 *
 * Warps the cursor to the center of this gob.  Also sets _xpPrev and
 * _ypPrev so that future mouse deltas are from the center.
 *
 * Parameters:
 *	None.
 *
 * Returns:
 *	None.
 *
 **************************************************************************/
void MovieView::WarpCursToCenter(void)
{
    AssertThis(0);

    PT pt;

    _xpPrev = _dxp / 2;
    _ypPrev = _dyp / 2;
    _dzrPrev = rZero;
    pt.xp = _xpPrev;
    pt.yp = _ypPrev;
    vpappb->PositionCurs(this, pt.xp, pt.yp);
}

/***************************************************************************
 *
 * Warps the cursor to the center of the given actor.
 *
 * Parameters:
 *	None.
 *
 * Returns:
 *	None.
 *
 **************************************************************************/
void MovieView::WarpCursToActor(PActor pactr)
{
    AssertThis(0);
    AssertPo(pactr, 0);

    PT pt;

    pactr->GetCenter(&pt.xp, &pt.yp);
    vpappb->PositionCurs(this, pt.xp, pt.yp);
}

/***************************************************************************
 *
 * Call this function when you have the cursor hidden and you want to
 * "reset" its position after using the mouse position to adjust an actor.
 * It updates _xpPrev and _ypPrev.  Then, if the cursor has gone outside
 * the gob, it warps the cursor to the center of the gob.  This way, the
 * actor doesn't seem to hit an invisible "wall" just because the (hidden)
 * cursor has hit the edge of the screen.
 *
 * Parameters:
 *	(xp, yp): current cursor position
 *
 * Returns:
 *	None.
 *
 **************************************************************************/
void MovieView::AdjustCursor(long xp, long yp)
{
    AssertThis(0);

    RC rc;

    GetRc(&rc, cooLocal);
    rc.Inset(kdpInset, kdpInset); // warp before the cursor gets close to the gob's edge
    if (rc.FPtIn(xp, yp))
    {
        _xpPrev = xp;
        _ypPrev = yp;
        _dzrPrev = rZero;
    }
    else
    {
        WarpCursToCenter();
    }
}

/***************************************************************************
 *
 * Converts from mouse coordinates to world coordinates
 *
 * The delta uses signed 16.16 pixels on input and world units on return.
 *
 **************************************************************************/
void MovieView::MouseToWorld(MMActorPoint &delta, bool recording)
{
    AssertThis(0);

    BMAT34 native;
    MMActorMatrix camera;
    Pmvie()->Pscen()->Pbkgd()->GetMouseMatrix(&native);
    static_assert(sizeof(camera) == sizeof(native), "Mouse matrix ABI");
    std::memcpy(&camera, &native, sizeof(camera));
    mm_actor_mouse_to_world(&delta, &_mouseAxes, &camera, recording);
}

bool MovieView::_fKbdDelayed = fFalse;
long MovieView::_dtsKbdDelay;
long MovieView::_dtsKbdRepeat;
bool MovieView::_fShowPoseReadout = fFalse;

/***************************************************************************
 *
 * Slows down keyboard auto-repeat as much as possible.  Saves user's
 * previous setting so it can be restored in RestoreKeyboardRepeat.
 *
 * Parameters:
 *  none
 *
 * Returns
 *  none
 *
 **************************************************************************/
void MovieView::SlowKeyboardRepeat(void)
{
    if (CurrentStudioPreferences().normalKeyRepeat)
        return;
#ifdef KAUAI_SDL3
    vpappb->SetSlowKeyRepeat(true);
#else
    if (_fKbdDelayed)
        return;
#ifdef WIN
    if (!SystemParametersInfo(SPI_GETKEYBOARDDELAY, 0, &_dtsKbdDelay, fFalse))
    {
        Bug("why could this fail?");
        return;
    }
    if (!SystemParametersInfo(SPI_SETKEYBOARDDELAY, klwMax, pvNil, fFalse))
    {
        Bug("why could this fail?");
        return;
    }
    if (!SystemParametersInfo(SPI_GETKEYBOARDSPEED, 0, &_dtsKbdRepeat, fFalse))
    {
        Bug("why could this fail?");
        return;
    }
    if (!SystemParametersInfo(SPI_SETKEYBOARDSPEED, 0, pvNil, fFalse))
    {
        Bug("why could this fail?");
        return;
    }
#endif
#ifdef MAC
    RawRtn();
#endif
    _fKbdDelayed = fTrue;
#endif
}

/***************************************************************************
 *
 * Restores the keyboard auto-repeat to the user's previous setting.
 *
 * Parameters:
 *  none
 *
 * Returns
 *  none
 *
 **************************************************************************/
void MovieView::RestoreKeyboardRepeat(void)
{
#ifdef KAUAI_SDL3
    vpappb->SetSlowKeyRepeat(false);
#else
    if (!_fKbdDelayed)
        return;
#ifdef WIN
    if (!SystemParametersInfo(SPI_SETKEYBOARDDELAY, _dtsKbdDelay, pvNil, fFalse))
    {
        Bug("why could this fail?");
        return;
    }
    if (!SystemParametersInfo(SPI_SETKEYBOARDSPEED, _dtsKbdRepeat, pvNil, fFalse))
    {
        Bug("why could this fail?");
        return;
    }
#endif
#ifdef MAC
    RawRtn();
#endif
    _fKbdDelayed = fFalse;
#endif
}

/***************************************************************************
 *
 * An actor has just been added, so enter "place actor" mode, where the
 * actor floats with the cursor.
 *
 * Parameters:
 *  fEntireScene flags whether the whole scene's route is to be translated on
 *  positioning, or whether only the current subroute is to be translated.
 *
 * Returns
 *  None.
 *
 **************************************************************************/
void MovieView::StartPlaceActor(bool fEntireScene)
{
    AssertThis(0);
    AssertPo(Pmvie()->Pscen(), 0);

    PActor pactr = Pmvie()->Pscen()->PactrSelected();

    AssertPo(pactr, 0);

    vpappb->HideCurs();
    WarpCursToCenter();

    SetTool(toolPlace);
    Pmvie()->Pscen()->BeginActorPlacement(&_placement, pactr, fEntireScene);
    Pmvie()->Pmcc()->NewActor();
    vpcex->TrackMouse(this);

    // While tracking the mouse, don't allow the cursor out of capture window.
    // If we don't do this, then the user can click on another window in the
    // middle of the cursor tracking, (if tracking with mouse btn up). Note,
    // this call clips the cursor movement to an area on the screen, so we are
    // assuming there is no way for the capture window to move during tracking.
    vpappb->ConfineCurs(this);

    _fMouseDownSeen = fFalse;
    _tsLastSample = TsCurrent();
    SlowKeyboardRepeat();

    return;
}

/***************************************************************************
 *
 * Undoes the place tool.
 *
 * Parameters:
 *  None.
 *
 * Returns
 *  fTrue if successful, else fFalse.
 *
 **************************************************************************/
void MovieView::EndPlaceActor()
{
    AssertThis(0);
    AssertPo(Pmvie()->Pscen(), 0);

    if (Tool() != toolPlace)
    {
        return;
    }

    vpappb->ShowCurs();
    WarpCursToCenter();
    SetTool(toolCompose);
    Pmvie()->Pscen()->EndActorPlacement(&_placement);
    Pmvie()->Pmcc()->ChangeTool(toolCompose);
    vpcex->EndMouseTracking();

    _fMouseDownSeen = fFalse;

    return;
}

void MovieView::SetActrUndo(PActor actor)
{
    const auto actors = Actor::_OwnerServices();
    mm_actor_placement_snapshot(&_placement, actor, &actors);
}

/***************************************************************************
 *
 * Track the mouse moves and set the cursor appropriately.
 *
 * Parameters:
 *	pcmd - The command information.
 *
 * Returns:
 *  fTrue - indicating that the command was processed.
 *
 ***************************************************************************/
bool MovieView::FCmdMouseMove(PCMD_MOUSE pcmd)
{
    AssertThis(0);
    AssertVarMem(pcmd);

    PActor pactr;
    long ibset;
    PDocumentBase pdocb;

    AssertPo(Pmvie(), 0);
    if (Pmvie()->Pscen() == pvNil)
    {
        Pmvie()->Pmcc()->SetCurs(toolDefault);
        return (fTrue);
    }

    if (Pmvie()->FPlaying())
    {
        Pmvie()->Pmcc()->SetCurs(toolDefault);
        return (fTrue);
    }

    switch (Tool())
    {
    case toolTboxStory:
    case toolTboxCredit:
    case toolTboxPaintText:
    case toolTboxFillBkgd:
    case toolTboxMove:
    case toolTboxFont:
    case toolTboxSize:
    case toolTboxStyle:
        Pmvie()->Pmcc()->SetCurs(toolDefault);
        break;

    case toolPlace:
        return (fFalse);

    case toolSceneNuke:
    case toolSceneChop:
    case toolSceneChopBack:
        Pmvie()->Pmcc()->SetCurs(Tool());
        break;

    case toolSounder:
    case toolLooper:
    case toolMatcher:
        if (_tagTool.sid == ksidInvalid)
            Pmvie()->Pmcc()->SetCurs(toolDefault);
        else
            Pmvie()->Pmcc()->SetCurs(Tool());
        break;

    case toolListener:
        Pmvie()->Pmcc()->SetCurs(Tool());
        // Audition sound if over an actor
        pactr = Pmvie()->Pscen()->PactrFromPt(pcmd->xp, pcmd->yp, &ibset);
        if (pvNil != pactr)
        {
            if (pactr != _pactrListener)
            {
                Pmvie()->Pmsq()->StopAll();
                pactr->FReplayFrame(fscenSounds); // Ignore audition error; non-fatal
                // Play outstanding sounds
                Pmvie()->Pmsq()->PlayMsq();
            }
            _fMouseOn = fFalse;
        }
        else if (!_fMouseOn)
        {
            _fMouseOn = fTrue;
            Pmvie()->Pmsq()->StopAll();
            Pmvie()->Pscen()->FReplayFrm(fscenSounds);
            // Play outstanding sounds
            Pmvie()->Pmsq()->PlayMsq();
        }

        _pactrListener = pactr;
        break;

    case toolSoonerLater:
        if (!_fTextMode)
        {
            AssertPo(Pmvie()->Pscen(), 0);
            pactr = Pmvie()->Pscen()->PactrFromPt(pcmd->xp, pcmd->yp, &ibset);
            AssertNilOrPo(pactr, 0);
            if (pactr == pvNil)
            {
                Pmvie()->Pmcc()->SetCurs(toolDefault);
                break;
            }
            else if (_fMouseDownSeen)
            {
                Pmvie()->Pmcc()->SetCurs(toolCompose);
            }
            else
            {
                Pmvie()->Pmcc()->SetCurs(Tool());
            }
        }
        else
        {
            Pmvie()->Pmcc()->SetCurs(toolDefault);
        }
        break;

    case toolAction:
    case toolActorNuke:
    case toolActorSelect:
    case toolCompose:
    case toolRecordSameAction:
    case toolRotateX:
    case toolRotateY:
    case toolRotateZ:
    case toolCostumeCmid:
    case toolSquashStretch:
    case toolResize:
    case toolNormalizeRot:
    case toolNormalizeSize:
    case toolCopyObject:
    case toolPasteObject:
    case toolCopyRte:
    case toolCutObject:
    case toolActorEasel:

        if (!_fTextMode)
        {
            AssertPo(Pmvie()->Pscen(), 0);
            pactr = Pmvie()->Pscen()->PactrFromPt(pcmd->xp, pcmd->yp, &ibset);
            AssertNilOrPo(pactr, 0);
            if (pactr == pvNil)
            {
                Pmvie()->Pmcc()->SetCurs(toolDefault);
                break;
            }
            else if ((Tool() == toolCompose) && (pcmd->grfcust & fcustCmd))
            {
                Pmvie()->Pmcc()->SetCurs(toolTweak);
            }
            else if ((Tool() == toolCompose) && (pcmd->grfcust & fcustShift))
            {
                Pmvie()->Pmcc()->SetCurs(toolComposeAll);
            }
            else if ((Tool() == toolPasteObject) && vpclip->FGetFormat(kclsActorClipboard, &pdocb))
            {
                if (((PActorClipboard)pdocb)->FRouteOnly())
                    Pmvie()->Pmcc()->SetCurs(toolPasteRte);
                else
                    Pmvie()->Pmcc()->SetCurs(Tool());
                ReleasePpo(&pdocb);
            }
            else
            {
                Pmvie()->Pmcc()->SetCurs(Tool());
            }
        }
        else
        {
            Pmvie()->Pmcc()->SetCurs(toolDefault);
        }
        break;

    case toolDefault:
        Pmvie()->Pmcc()->SetCurs(toolDefault);
        break;

    default:
        Bug("Unknown tool type");
    }

    return (fTrue);
}

/***************************************************************************
 *
 * Track the mouse and do the appropriate command.
 *
 * Parameters:
 *	pcmdTrack - The command information.
 *
 * Returns:
 *  fTrue - indicating that the command was processed.
 *
 ***************************************************************************/
bool MovieView::FCmdTrackMouse(PCMD_MOUSE pcmd)
{
    AssertThis(0);
    AssertVarMem(pcmd);

    RC rc;

    if (pcmd->cid == cidMouseDown)
    {
        Assert(vpcex->PgobTracking() == pvNil, "mouse already being tracked!");
        vpcex->TrackMouse(this);
    }
    else
    {
        Assert(vpcex->PgobTracking() == this, "not tracking mouse!");
        Assert(pcmd->cid == cidTrackMouse, 0);
    }

    if ((pcmd->cid == cidMouseDown) || ((pcmd->grfcust & fcustMouse) && !_fMouseDownSeen))
    {
        _MouseDown(pcmd);
    }
    else
    {
        _MouseDrag(pcmd);
    }

    if (!(pcmd->grfcust & fcustMouse))
    {
        _MouseUp(pcmd);
    }

    return fTrue;
}

/***************************************************************************
 *
 * Handle positioning an actor when the place tool is in effect.
 *
 * Parameters:
 *  delta - the change in position in worldspace
 *
 * Returns:
 *  None.
 *
 **************************************************************************/
void MovieView::_PositionActr(const MMActorPoint &delta)
{
    AssertThis(0);
    Assert(Tool() == toolPlace, "Wrong tool in effect");

    auto scene = Pmvie()->Pscen();
    if (scene->FMoveActorPlacement(_placement, delta, FRespectGround()))
    {
        Pmvie()->Pbwld()->MarkDirty();
        Pmvie()->MarkViews();
        scene->Pbkgd()->ReuseActorPlacePoint();
    }
}

/***************************************************************************
 *
 * Notify script that an actor was clicked.  Note that we sometimes call
 * this function with fDown fFalse even though the user hasn't mouseup'ed
 * yet, because of bringing up easels on mousedown.
 *
 * Parameters:
 *	pactr - the actor that was clicked
 *  fDown - fTrue if we're mousedown'ing the actor, fFalse if mouseup
 *
 * Returns:
 *  None.
 *
 **************************************************************************/
void MovieView::_ActorClicked(PActor pactr, bool fDown)
{
    AssertThis(0);
    AssertPo(pactr, 0);

    ulong grftmpl = 0;

    if (pactr->Ptmpl()->FIsTdt())
    {
        grftmpl |= ftmplTdt;
    }

    if (pactr->Ptmpl()->FIsProp())
    {
        grftmpl |= ftmplProp;
    }

    if (fDown)
    {
        vpcex->EnqueueCid(cidActorClickedDown, pvNil, pvNil, pactr->Arid(), pactr->Ptmpl()->Cno(), grftmpl);
    }
    else
    {
        vpcex->EnqueueCid(cidActorClicked, pvNil, pvNil, pactr->Arid(), pactr->Ptmpl()->Cno(), grftmpl);
    }
}

static int ActorPointerTool(long tool)
{
    switch (tool)
    {
    case toolDefault: return MM_SCENE_POINTER_DEFAULT;
    case toolActorSelect: return MM_SCENE_POINTER_SELECT;
    case toolCompose:
    case toolRotateX:
    case toolRotateY:
    case toolRotateZ:
    case toolResize:
    case toolSquashStretch: return MM_SCENE_POINTER_TRANSFORM;
    default: return MM_SCENE_POINTER_OTHER;
    }
}

MMSceneActorPointerView MovieView::_ActorPointerView()
{
    return {
        this,
        [](void *p, MMI32 x, MMI32 y, MMI32 *part) -> void * {
            long native = *part;
            auto hit = static_cast<PMovieView>(p)->Pmvie()->Pscen()->PactrFromPt(x, y, &native);
            *part = native;
            return hit;
        },
        [](void *, void *p, MMI32 x, MMI32 y, MMI32 *part) {
            auto actor = static_cast<PActor>(p);
            long native = *part;
            const bool hit = actor->FIsInView() && actor->FPtIn(x, y, &native);
            *part = native;
            return int(hit);
        },
        [](void *p, void *actor) { static_cast<PMovieView>(p)->_ActorClicked(static_cast<PActor>(actor), true); },
        [](void *p) { static_cast<PMovieView>(p)->Pmvie()->Pbwld()->MarkDirty(); },
        [](void *p) { static_cast<PMovieView>(p)->Pmvie()->MarkViews(); }};
}

/***************************************************************************
 *
 * Handle Mousedown
 *
 * Parameters:
 *	pcmd - The mouse command
 *
 * Returns:
 *  None.
 *
 **************************************************************************/
void MovieView::_MouseDown(CMD_MOUSE *pcmd)
{
    AssertThis(0);
    AssertVarMem(pcmd);

    PActor pactr = pvNil;
    PTBOX ptbox;
    PT pt;
    MMI32 ibset = 0;

    SlowKeyboardRepeat();
    const auto actors = Actor::_OwnerServices();
    mm_scene_actor_pointer_reset(&_pointerSelection, &actors);

    if (Pmvie()->FPlaying())
    {
        //
        // If we are in the middle of playing, ignore mouse down.
        //
        return;
    }

    AssertPo(Pmvie(), 0);
    if (pvNil == Pmvie()->Pscen())
    {
        return;
    }
    AssertPo(Pmvie()->Pscen(), 0);

    if (_fTextMode)
    {
        ptbox = Pmvie()->Pscen()->PtboxSelected();
    }
    else if ((Tool() != toolPlace) && (Tool() != toolSceneChop) && (Tool() != toolSceneChopBack))
    {
        const MMSceneActorPointerInput input{pcmd->xp, pcmd->yp, ActorPointerTool(Tool()),
                                             (pcmd->grfcust & fcustShift) != 0};
        const auto selection = Pmvie()->Pscen()->_SelectionServices();
        const auto view = _ActorPointerView();
        pactr = static_cast<PActor>(mm_scene_actor_pointer_down(&_pointerSelection, &input, &ibset,
                                                               &selection, &actors, &view));
    }

#ifdef DEBUG
    // Authoring hack to write out background starting pos data

    if (pvNil != pactr &&
        ((vpappb->GrfcustCur(fFalse) & (fcustShift | fcustCmd | fcustOption)) == (fcustShift | fcustCmd | fcustOption)))
    {
        BRS xr;
        BRS yr;
        BRS zr;

        pactr->Pbody()->GetPosition(&xr, &yr, &zr);
        if (!Pmvie()->Pscen()->Pbkgd()->FWritePlaceFile(xr, yr, zr))
        {
            Bug("ouch.  bkgd write failed.");
        }
        else
        {
            Warn("Wrote bkgd actor start point.");
        }
    }
#endif // DEBUG

    _xpPrev = pcmd->xp;
    _ypPrev = pcmd->yp;
    _dzrPrev = rZero;
    _grfcust = pcmd->grfcust;
    _tsLastSample = TsCurrent();

    switch (Tool())
    {

    // Sound tools get handled together:
    case toolSounder:
    case toolLooper:
    case toolMatcher:
        if (pactr == pvNil) // scene sound
        {
            tribool fLoop = (tribool)(Tool() == toolLooper);
            tribool fQueue = (tribool)FPure(_grfcust & fcustCmd);

            if ((Tool() == toolMatcher) && (ksidInvalid != _tagTool.sid))
            {
                PushErc(ercSocBadSceneSound);
                break;
            }
            if (ksidInvalid != _tagTool.sid)
            {
                Pmvie()->FAddBkgdSnd(&_tagTool, fLoop, fQueue, vlmNil, styNil);
            }
        }
        else // actor sound
        {
            tribool fLoop = (tribool)(Tool() == toolLooper);
            tribool fQueue = (tribool)FPure(_grfcust & fcustCmd);
            tribool fActnCel = (tribool)FPure(Tool() == toolMatcher);

            if (ksidInvalid != _tagTool.sid)
            {
                Pmvie()->FAddActrSnd(&_tagTool, fLoop, fQueue, fActnCel, vlmNil, styNil);
            }
        }
        break;

    case toolListener:
        // Start the listener easel
        vpcex->EndMouseTracking();
        RestoreKeyboardRepeat();
        if (pvNil != pactr)
        {
            _ActorClicked(pactr, fFalse);
        }
        Pmvie()->Pmcc()->StartListenerEasel();
        break;

    case toolActorSelect:
        Pmvie()->Pmcc()->PlayUISound(Tool());
        break;

    case toolSceneNuke:
        if (Pmvie()->FRemScen(Pmvie()->Iscen()))
        {
            Pmvie()->Pmcc()->UpdateRollCall();
            Pmvie()->Pmcc()->SceneNuked();
            Pmvie()->InvalViewsAndScb();
            Pmvie()->Pmcc()->PlayUISound(Tool());
        }
        break;

    case toolActorNuke:
        if (pactr == pvNil)
        {
            break;
        }

        Pmvie()->FRemActr();
        Pmvie()->Pmcc()->PlayUISound(Tool());
        break;

    case toolSceneChop:
        if (Pmvie()->Pscen()->FChop())
        {
            Pmvie()->InvalViewsAndScb();
            Pmvie()->Pmcc()->PlayUISound(Tool());
        }
        break;

    case toolSceneChopBack:
        if (Pmvie()->Pscen()->FChopBack())
        {
            Pmvie()->InvalViewsAndScb();
            Pmvie()->Pmcc()->PlayUISound(Tool());
        }
        break;

    case toolCutObject:
        _ClipboardClick(MM_MOVIE_CLIP_CUT, false);
        break;
    case toolCopyObject:
        _ClipboardClick(MM_MOVIE_CLIP_COPY, false);
        break;
    case toolCopyRte:
        _ClipboardClick(MM_MOVIE_CLIP_ROUTE, false);
        break;

    case toolPasteObject:
        _ClipboardClick(MM_MOVIE_CLIP_PASTE, pactr != pvNil);
        break;

    case toolTboxStory:
    case toolTboxCredit:
    case toolTboxPaintText:
    case toolTboxFillBkgd:
    case toolTboxMove:
    case toolTboxFont:
    case toolTboxSize:
    case toolTboxStyle:
        Pmvie()->Pscen()->SelectTbox(pvNil);
        break;

    case toolPlace:
        _fMouseDownSeen = fTrue;
        break;

    case toolSoonerLater:
        if ((pactr != pvNil) && !_fMouseDownSeen)
        {
            mm_actor_owner_freeze_time(&pactr->_owner, 1);
            pactr->Hilite();
            _fMouseDownSeen = fTrue;
            Pmvie()->Pmcc()->PlayUISound(Tool());
        }
        else
        {
            goto LEnd;
        }
        break;

    case toolCompose:
    case toolRotateX:
    case toolRotateY:
    case toolRotateZ:
    case toolResize:
    case toolSquashStretch:
        if (pactr != pvNil)
        {
            vpappb->HideCurs();

            Pmvie()->Pscen()->CaptureActorDragUndo(&_actorDrag.undo, pactr, true, Tool() != toolCompose);
            if ((Tool() != toolResize) && (Tool() != toolSquashStretch))
            {
                Pmvie()->Pmcc()->PlayUISound(Tool(), _grfcust);
            }
            else
            {
                _actorDrag.size_direction = 0;
            }
        }
        break;

    case toolNormalizeRot:
        if (pactr != pvNil)
        {
            Pmvie()->Pmcc()->PlayUISound(Tool());
            pactr->FNormalize(fnormRotate);
        }
        break;
    case toolNormalizeSize:
        if (pactr != pvNil)
        {
            Pmvie()->Pmcc()->PlayUISound(Tool());
            pactr->FNormalize(fnormSize);
        }
        break;

    case toolRecordSameAction:
        if (pactr != pvNil)
        {
            _ptmplTool = pactr->Ptmpl();
            const auto recording = _RecordGestureServices();
            const auto transaction = pactr->_TransactionServices();
            const auto actorRecording = pactr->_RecordServices(&transaction.actors);
            if (mm_actor_record_gesture_start(&_recordGesture, RecordGestureModifiers(pcmd->grfcust),
                                              &actorRecording, &transaction, &recording))
                Pmvie()->Pmcc()->PlayUISound(Tool());
        }
        break;

    case toolAction:
        //
        // Start the action browser
        //
        vpcex->EndMouseTracking();
        RestoreKeyboardRepeat();

        if (pactr != pvNil)
        {
            _ActorClicked(pactr, fFalse);
            Pmvie()->Pscen()->SelectActr(pactr);
            _ptmplTool = pactr->Ptmpl();
            Pmvie()->Pmcc()->StartActionBrowser();
        }
        break;

    case toolCostumeCmid:
        if (pactr != pvNil)
        {
            TAG tag; // unused
            TrashVar(&tag);
            TrashVar(&ibset);
            Pmvie()->FCostumeActr(ibset, &tag, CmidTool(), tribool::tYes);
        }
        break;
    case toolActorEasel:
        if (pactr != pvNil)
        {
            bool fActrChanged;

            _ActorClicked(pactr, fFalse);
            Pmvie()->Pmcc()->ActorEasel(&fActrChanged);
            if (fActrChanged)
            {
                Pmvie()->SetDirty();
                Pmvie()->ClearUndo();
            }
        }
        break;

    case toolDefault:
        /* Do nothing */
        break;

    default:
        Bug("Tool unknown on mouse down");
    }

    if (Pmvie()->FSoundsEnabled())
    {
        Pmvie()->Pmsq()->PlayMsq();
    }
    else
    {
        Pmvie()->Pmsq()->FlushMsq();
    }
    _fMouseDownSeen = fTrue;

LEnd:
    Pmvie()->MarkViews();
}

void MovieView::_DiscardMoveUndo()
{
    const auto undo = ActorMoveGroupUndo::DragUndoServices(nullptr);
    mm_actor_drag_undo_discard(&_actorDrag.undo, &undo);
}

void MovieView::_SampleActorDrag(const CMD_MOUSE &pointer, const MMActorPoint &mouse, const MMActorPoint &world)
{
    int tool;
    switch (Tool())
    {
    case toolRotateX: tool = MM_MOVIE_DRAG_ROTATE_X; break;
    case toolRotateY: tool = MM_MOVIE_DRAG_ROTATE_Y; break;
    case toolRotateZ: tool = MM_MOVIE_DRAG_ROTATE_Z; break;
    case toolResize: tool = MM_MOVIE_DRAG_RESIZE; break;
    case toolSquashStretch: tool = MM_MOVIE_DRAG_SQUASH; break;
    default: tool = MM_MOVIE_DRAG_COMPOSE; break;
    }
    const MMMovieActorDragInput sample{tool, (_grfcust & fcustCmd) != 0, (_grfcust & fcustShift) != 0,
                                      _fRespectGround, mouse, world};
    auto movie = Pmvie();
    const auto edit = movie->_EditServices();
    const auto undo = ActorMoveGroupUndo::DragUndoServices(movie);
    const auto selection = movie->Pscen()->_SelectionServices();
    const auto actors = Scene::_GroupTransformServices();
    struct DragFeedback
    {
        PMovieView view;
        long x, y;
    } feedback{this, pointer.xp, pointer.yp};
    const MMMovieActorDragServices services{
        &selection, &actors, &edit, &undo, &feedback,
        [](void *p) {
            const auto &feedback = *static_cast<DragFeedback *>(p);
            feedback.view->AdjustCursor(feedback.x, feedback.y);
        },
        [](void *p, int alternate) {
            auto view = static_cast<DragFeedback *>(p)->view;
            auto client = view->Pmvie()->Pmcc();
            client->StopUISound();
            client->PlayUISound(view->Tool(), alternate ? fcustShift : 0);
        },
        [](void *p) { static_cast<DragFeedback *>(p)->view->Pmvie()->Pbwld()->MarkDirty(); },
        [](void *p) { static_cast<DragFeedback *>(p)->view->Pmvie()->MarkViews(); }};
    mm_movie_actor_drag_sample(&_actorDrag, &sample, &services);
}

MMActorRecordGestureServices MovieView::_RecordGestureServices()
{
    MMActorRecordGestureServices services{};
    services.context = this;
    services.clock_now = [](void *) { return MMU32(TsCurrent()); };
    services.hide_cursor = [](void *) { vpappb->HideCurs(); };
    services.recording_started = [](void *p, int replace) {
        static_cast<PMovieView>(p)->Pmvie()->Pmcc()->Recording(true, replace != 0);
    };
    return services;
}

bool MovieView::_FRecordTemplateMatches(PActor actor)
{
    auto tmpl = actor->Ptmpl();
    return tmpl == _ptmplTool || (_ptmplTool && _ptmplTool->FIsTdt() && tmpl->FIsTdt());
}

/***************************************************************************
 *
 * Handle Mouse drag (mouse move while button down)
 *
 * Parameters:
 *  pcmd - The mouse command
 *
 * Returns:
 *  None.
 *
 **************************************************************************/
void MovieView::_MouseDrag(CMD_MOUSE *pcmd)
{
    AssertThis(0);
    AssertVarMem(pcmd);

    PMovie pmvie;
    PScene pscen;
    PActor pactr = pvNil;
    BRS zrActr, zrCam;

    if (Pmvie()->FPlaying() || Pmvie()->Pmcc()->FMinimized())
    {
        //
        // If we are in the middle of playing, or been minimized, ignore mouse dragging.
        //
        return;
    }

    pmvie = Pmvie();
    AssertPo(pmvie, 0);
    pscen = pmvie->Pscen();
    AssertNilOrPo(pscen, 0);

    if (pvNil == pscen)
    {
        return;
    }

    if (_pointerSelection.target || _pointerSelection.cancelled)
    {
        const MMSceneActorPointerInput input{pcmd->xp, pcmd->yp, ActorPointerTool(Tool()), 0};
        const auto selection = pscen->_SelectionServices();
        const auto actors = Actor::_OwnerServices();
        const auto undo = ActorMoveGroupUndo::DragUndoServices(pmvie);
        if (mm_scene_actor_pointer_drag(&_pointerSelection, &input, &_actorDrag.undo, &selection, &actors, &undo))
            return;
    }

    pactr = pscen->PactrSelected();

    AssertNilOrPo(pactr, 0);

    if (pactr == pvNil)
    {
        return;
    }

    auto mouse = mm_actor_mouse_delta(pcmd->xp, pcmd->yp, _xpPrev, _ypPrev, _dzrPrev);
#ifdef WIN
    const auto elapsed = TsCurrent() - _tsLastSample;
    const bool up = GetKeyState(VK_UP) < 0;
    const bool down = GetKeyState(VK_DOWN) < 0;
    mouse.dzr = mm_actor_mouse_key_depth(mouse.dzr, elapsed, up, down);
    if ((up || down) && Tool() == toolRecordSameAction)
        mm_actor_record_gesture_filter_axes(&_recordGesture, pactr->FIsModeRecord(), &mouse);
    _dzrPrev = mouse.dzr;
    _tsLastSample = TsCurrent();
#endif
#ifdef MAC
    RawRtn();
#endif

    auto world = mouse;
    MouseToWorld(world, Tool() == toolRecordSameAction);

    //
    // Scale movement based on selected actor
    //
    pactr->GetXyzWorld(pvNil, pvNil, &zrActr);
    pscen->Pbkgd()->GetCameraPos(pvNil, pvNil, &zrCam);
    mm_actor_scale_drag_depth(&world, zrActr, zrCam);

    switch (Tool())
    {
    default:
        Bug("Tool unknown on mouse move");
        break;

    case toolDefault:
    case toolActorSelect:
        break;

    case toolSceneNuke:
    case toolActorNuke:
    case toolSceneChop:
    case toolSceneChopBack:
    case toolCutObject:
    case toolCopyObject:
    case toolPasteObject:
    case toolCopyRte:
    case toolTboxPaintText:
    case toolTboxFillBkgd:
    case toolTboxMove:
    case toolTboxFont:
    case toolTboxSize:
    case toolTboxStory:
    case toolTboxCredit:
    case toolTboxStyle:
    case toolActorEasel:
    case toolSounder:
    case toolLooper:
    case toolMatcher:
        break;

    case toolPlace:
        _PositionActr(world);
        AdjustCursor(pcmd->xp, pcmd->yp);
        break;

    case toolCompose:
    case toolRotateX:
    case toolRotateY:
    case toolRotateZ:
    case toolResize:
    case toolSquashStretch:
        _SampleActorDrag(*pcmd, mouse, world);
        break;

    case toolSoonerLater:
    case toolNormalizeRot:
    case toolNormalizeSize:
    case toolCostumeCmid:
        break;

    case toolRecordSameAction: {
        const auto now = TsCurrent();
        if (!_FRecordTemplateMatches(pactr))
            break;
        const auto recording = _RecordGestureServices();
        const auto owners = Actor::_OwnerServices();
        const auto actorRecording = pactr->_RecordServices(&owners);
        const int result = mm_actor_record_gesture_sample(&_recordGesture,
            &world, RecordGestureModifiers(pcmd->grfcust), _fRespectGround, now, &actorRecording, &recording);
        if (result == MM_RECORD_GESTURE_FRAME || result == MM_RECORD_GESTURE_POINT)
        {
            Pmvie()->Pmcc()->UpdateScrollbars();
            if (result == MM_RECORD_GESTURE_POINT)
                AdjustCursor(pcmd->xp, pcmd->yp);
        }
        if (result != MM_RECORD_GESTURE_IDLE)
            Pmvie()->MarkViews();
    }
    break;
    }
}

/***************************************************************************
 *
 * Handle Mouseup
 *
 * Parameters:
 *  pcmd - The mouse command.
 *
 * Returns:
 *  None.
 *
 **************************************************************************/
void MovieView::_MouseUp(CMD_MOUSE *pcmd)
{
    AssertThis(0);
    AssertVarMem(pcmd);

    PMovie pmvie;
    PScene pscen;
    PActor pactr = pvNil;
    bool fJustToggledSel = fFalse; // user shift-click-released without dragging

    if (_pointerSelection.target || _pointerSelection.cancelled)
    {
        auto scene = Pmvie()->Pscen();
        const auto selection = scene ? scene->_SelectionServices() : MMSceneSelectionServices{};
        const auto actors = Actor::_OwnerServices();
        const auto view = _ActorPointerView();
        fJustToggledSel = mm_scene_actor_pointer_up(&_pointerSelection, pcmd->xp, pcmd->yp,
                                                   scene ? &selection : nullptr, &actors, &view) != 0;
    }

    pmvie = Pmvie();
    AssertPo(pmvie, 0);

    _grfcust = fcustNil;

    if (Tool() != toolPlace || _fMouseDownSeen)
        RestoreKeyboardRepeat();

    if (_fPause)
    {

        Assert(Pmvie()->FPlaying(), "Bad Pause type");

        //
        // If we are pausing in the middle of playing, restart playing
        //
        if (!pmvie->Pclok()->FSetAlarm(0, pmvie))
        {
            Command cmd;

            pmvie->SetFStopPlaying(fTrue);
            cmd.pcmh = pmvie;
            cmd.cid = cidAlarm;
            pmvie->FCmdAlarm(&cmd);
        }

        goto LEndTracking;
    }

    if (Pmvie()->FPlaying())
    {
        goto LEndTracking;
    }

    Pmvie()->Pmcc()->StopUISound();

    pscen = pmvie->Pscen();
    AssertNilOrPo(pscen, 0);
    if (pvNil == pscen)
    {
        goto LEndTracking;
    }

    pactr = pscen->PactrSelected();
    AssertNilOrPo(pactr, 0);
    if (pvNil != pactr && Tool() != toolPlace)
    {
        _ActorClicked(pactr, fFalse);
    }

    switch (Tool())
    {
    case toolDefault:
    case toolActorSelect:
        break;

    case toolPlace:

        if (!_fMouseDownSeen)
        {
            return;
        }

        pactr = static_cast<PActor>(_placement.actor);
        AssertPo(pactr, 0);
        SetTool(toolCompose);
        Pmvie()->Pmcc()->ChangeTool(toolCompose);
        if (!pscen->FFinishActorPlacement(&_placement))
        {
            WarpCursToCenter();
            vpappb->ShowCurs();
            break;
        }
        WarpCursToActor(pactr);
        vpappb->ShowCurs();
        vpcex->EnqueueCid(cidActorPlaced, pvNil, pvNil, pactr->Arid());
        break;

    case toolCompose:
    case toolRotateX:
    case toolRotateY:
    case toolRotateZ:
    case toolResize:
    case toolSquashStretch:
        if (pactr != pvNil)
        {
            // Don't snap the cursor to the primary actor when the user just
            // shift-clicked an actor without dragging -- they were modifying
            // the selection set, not moving anything, and warping the cursor
            // away from the click point feels jarring.
            if (!fJustToggledSel)
            {
                WarpCursToActor(pactr);
            }
            vpappb->ShowCurs();
        }
        break;

    case toolRecordSameAction: {
        if (pactr != pvNil && !_FRecordTemplateMatches(pactr))
            break;
        const auto owners = Actor::_OwnerServices();
        const auto actorRecording = pactr ? pactr->_RecordServices(&owners) : MMActorRecordServices{};
        mm_actor_record_gesture_end(&_recordGesture, pactr ? &actorRecording : nullptr, &owners);
        if (pvNil != pactr)
        {
            Pmvie()->InvalViewsAndScb();
            WarpCursToActor(pactr);
            vpappb->ShowCurs();
        }
        Pmvie()->Pmcc()->Recording(fFalse, FRecordDefault());
        break;
    }

    case toolSoonerLater:
        if (pactr != pvNil)
        {
            Assert(pactr->FTimeFrozen(), "Something odd is going on");
            const auto actors = Actor::_OwnerServices();
            const auto undo = ActorMoveGroupUndo::DragUndoServices(Pmvie());
            mm_actor_time_undo_begin(pactr, pscen->Nfrm(), &actors, &undo);

            Pmvie()->Pbwld()->MarkDirty();
            Pmvie()->MarkViews();

            Pmvie()->Pmcc()->StartSoonerLater();
        }

        break;

    case toolNormalizeRot:
    case toolNormalizeSize:
    case toolCostumeCmid:
    case toolSceneNuke:
    case toolActorNuke:
    case toolSceneChop:
    case toolSceneChopBack:
    case toolCutObject:
    case toolCopyObject:
    case toolPasteObject:
    case toolCopyRte:
    case toolTboxPaintText:
    case toolTboxFillBkgd:
    case toolTboxMove:
    case toolTboxFont:
    case toolTboxSize:
    case toolTboxStyle:
    case toolActorEasel:
    case toolTboxStory:
    case toolTboxCredit:
    case toolSounder:
    case toolLooper:
    case toolMatcher:
        break;

    default:
        Bug("Tool unknown on mouse up");
    }

LEndTracking:
    _DiscardMoveUndo();
    vpcex->EndMouseTracking();

    // Remove any cursor clipping we may have begun when mouse tracking started.
    vpappb->ConfineCurs(nullptr);

    // Refresh the pose readout once at mouseup. BRender's per-frame dirty
    // region only covers the actor bounds, so the top-left readout strip
    // stays clean through the drag and would otherwise display the
    // pre-drag position until the next selection change. One inval per
    // mouseup is safe (no paint-storm).
    if (_fShowPoseReadout)
    {
        RC rcReadout;
        rcReadout.Set(0, 0, 200, 24);
        InvalRc(&rcReadout, kginSysInval);
    }
}

static long NativeClipboardTool(int tool)
{
    switch (tool)
    {
    case MM_MOVIE_CLIP_CUT: return toolCutObject;
    case MM_MOVIE_CLIP_COPY: return toolCopyObject;
    case MM_MOVIE_CLIP_PASTE: return toolPasteObject;
    case MM_MOVIE_CLIP_ROUTE: return toolCopyRte;
    default: Bug("Bad clipboard tool"); return toolDefault;
    }
}

static int MovieClipboardCommand(long cid)
{
    switch (cid)
    {
    case cidCut: return MM_MOVIE_CLIP_CUT;
    case cidCopy: return MM_MOVIE_CLIP_COPY;
    case cidPaste: return MM_MOVIE_CLIP_PASTE;
    case cidCopyRoute: return MM_MOVIE_CLIP_ROUTE;
    case cidCutTool: return MM_MOVIE_CLIP_CUT_TOOL;
    case cidCopyTool: return MM_MOVIE_CLIP_COPY_TOOL;
    case cidPasteTool: return MM_MOVIE_CLIP_PASTE_TOOL;
    case cidShiftCut: return MM_MOVIE_CLIP_SHIFT_CUT;
    case cidShiftCopy: return MM_MOVIE_CLIP_SHIFT_COPY;
    default: return MM_MOVIE_CLIP_UNKNOWN;
    }
}

MMMovieClipboardServices MovieView::_ClipboardServices(ClipboardHost *host)
{
    MMMovieClipboardServices s = {};
    s.context = host;
    s.scene = [](void *p) -> void * { return static_cast<ClipboardHost *>(p)->view->Pmvie()->Pscen(); };
    s.text_mode = [](void *p) -> int { return static_cast<ClipboardHost *>(p)->view->FTextMode(); };
    s.shift = [](void *p) -> int { return (static_cast<ClipboardHost *>(p)->view->_grfcust & fcustShift) != 0; };
    s.set_shift = [](void *p, int shift) { static_cast<ClipboardHost *>(p)->view->_grfcust = shift ? fcustShift : fcustNil; };
    s.selected_text = [](void *scene) -> void * { return static_cast<PScene>(scene)->PtboxSelected(); };
    s.selected_actor = [](void *scene) -> void * { return static_cast<PScene>(scene)->PactrSelected(); };
    s.text_visible = [](void *text) -> int { return static_cast<PTBOX>(text)->FIsVisible(); };
    s.text_selected = [](void *text) -> int { return static_cast<PTBOX>(text)->FTextSelected(); };
    s.actor_visible = [](void *actor) -> int { return static_cast<PActor>(actor)->FIsInView(); };
    s.empty = [](void *) -> int { return vpclip->FDocIsClip(pvNil); };
    s.format = [](void *, int kind, void **owned) -> int {
        long cls = kind == MM_MOVIE_CLIP_ACTOR ? kclsActorClipboard : kclsTextBoxClipboard;
        PDocumentBase document;
        if (!vpclip->FGetFormat(cls, owned ? &document : pvNil))
            return 0;
        if (owned)
            *owned = document;
        return 1;
    };
    s.route_only = [](void *document) -> int {
        return static_cast<PActorClipboard>(static_cast<PDocumentBase>(document))->FRouteOnly();
    };
    s.copy_actor = [](void *actor, int route, int throughEnd) -> void * {
        return static_cast<PDocumentBase>(ActorClipboard::PaclpNew(static_cast<PActor>(actor), route != 0, throughEnd != 0));
    };
    s.publish = [](void *, void *document) { vpclip->Set(static_cast<PDocumentBase>(document)); };
    s.release = [](void *document) { static_cast<PDocumentBase>(document)->Release(); };
    s.scene_sounds_enabled = [](void *scene) -> int { return !(static_cast<PScene>(scene)->GrfScen() & fscenSounds); };
    s.scene_sounds = [](void *scene, int enabled) {
        if (enabled)
            static_cast<PScene>(scene)->Enable(fscenSounds);
        else
            static_cast<PScene>(scene)->Disable(fscenSounds);
    };
    s.delete_actor = [](void *actor, int throughEnd, int *alive) -> int {
        bool stillOnScene;
        if (!static_cast<PActor>(actor)->FDelete(&stillOnScene, throughEnd != 0))
            return 0;
        *alive = stillOnScene;
        return 1;
    };
    s.remove_actor = [](void *scene, void *actor) { static_cast<PScene>(scene)->RemActrCore(static_cast<PActor>(actor)->Arid()); };
    s.stage = Actor::_BindStageEdit;
    s.dirty = [](void *scene) { static_cast<PScene>(scene)->MarkDirty(); };
    s.invalidate = [](void *p) { static_cast<ClipboardHost *>(p)->view->Pmvie()->InvalViews(); };
    s.paste_actor = [](void *p, void *document) {
        auto actor = static_cast<PActorClipboard>(static_cast<PDocumentBase>(document));
        actor->FPaste(static_cast<ClipboardHost *>(p)->view->Pmvie());
    };
    s.paste_text = [](void *scene, void *document) -> int {
        auto text = static_cast<PTextBoxClipboard>(static_cast<PDocumentBase>(document));
        return text->FPaste(static_cast<PScene>(scene));
    };
    s.enable_text = [](void *p) { static_cast<ClipboardHost *>(p)->view->Pmvie()->Pmcc()->EnableTboxTools(); };
    s.cue = [](void *p, int tool) {
        static_cast<ClipboardHost *>(p)->view->Pmvie()->Pmcc()->PlayUISound(NativeClipboardTool(tool));
    };
    s.set_tool = [](void *p, int tool) { static_cast<ClipboardHost *>(p)->view->SetTool(NativeClipboardTool(tool)); };
    s.queue_text = [](void *p, void *text) {
        Command cmd = *static_cast<ClipboardHost *>(p)->command;
        cmd.pcmh = static_cast<PTBOX>(text)->PddgGet(0);
        vpcex->EnqueueCmd(&cmd);
    };
    s.queue_generic = [](void *p, int cid) {
        Command cmd = *static_cast<ClipboardHost *>(p)->command;
        switch (cid)
        {
        case MM_MOVIE_CLIP_CUT: cmd.cid = cidCut; break;
        case MM_MOVIE_CLIP_COPY: cmd.cid = cidCopy; break;
        case MM_MOVIE_CLIP_PASTE: cmd.cid = cidPaste; break;
        default: Bug("Bad generic clipboard command"); return;
        }
        vpcex->EnqueueCmd(&cmd);
    };
    s.diagnostic = [](void *, int code) {
        switch (code)
        {
        case MM_MOVIE_CLIP_NO_ACTOR: PushErc(ercSocNoActrSelected); break;
        case MM_MOVIE_CLIP_NO_SCENE: PushErc(ercSocNoScene); break;
        case MM_MOVIE_CLIP_UNSUPPORTED: PushErc(ercSocCannotPasteThatHere); break;
        default: Bug("Unknown command"); break;
        }
    };
    return s;
}

bool MovieView::FCmdClip(PCommand pcmd)
{
    AssertThis(0);
    AssertVarMem(pcmd);
    ClipboardHost host = {this, pcmd};
    auto services = _ClipboardServices(&host);
    mm_movie_clipboard_command(MovieClipboardCommand(pcmd->cid), &services);
    return fTrue;
}

void MovieView::_ClipboardClick(int tool, bool hitActor)
{
    ClipboardHost host = {this, pvNil};
    auto services = _ClipboardServices(&host);
    mm_movie_clipboard_click(tool, hitActor, &services);
}

/***************************************************************************
 *
 * Handles the Undo and Redo commands.
 *
 * Parameters:
 *  pcmd - Pointer to the command to process.
 *
 * Returns:
 *  fTrue if it processed the command, else fFalse.
 *
 **************************************************************************/
bool MovieView::FCmdUndo(PCommand pcmd)
{
    AssertThis(0);
    AssertVarMem(pcmd);

    bool fRet;

    if (pcmd->cid == cidUndo)
    {
        Pmvie()->Pmcc()->PlayUISound(toolUndo);
    }
    else
    {
        Pmvie()->Pmcc()->PlayUISound(toolRedo);
    }

    fRet = MovieView_PAR::FCmdUndo(pcmd);
    Pmvie()->Pmcc()->SetUndo(Pmvie()->CundbUndo() != 0   ? undoUndo
                             : Pmvie()->CundbRedo() != 0 ? undoRedo
                                                         : undoDisabled);

    Pmvie()->InvalViewsAndScb();
    return (fRet);
}

/***************************************************************************
 *
 * Handle a close command.
 *
 * Parameters:
 *	fAssumeYes - Should the dialog assume yes.
 *
 * Returns:
 * 	fTrue if the client should close this document.
 *
 **************************************************************************/
bool MovieView::FCloseDoc(bool fAssumeYes, bool fSaveDDG)
{
    AssertThis(0);
    bool fRet;
    Filename fni;

    //
    // FQueryClose calls FAutosave depending on the result of the query.
    // FAutosave needs to know whether the doc is closing as unused user
    // sounds are to be deleted from the movie only upon close.
    //
    Pmvie()->SetDocClosing(fTrue);
    // If not dirty, flush snds on close without user query
    // Irrelevant if there are no user sounds in the movie or if
    // the file is read-only (can't save to the original file)
    if (!Pmvie()->FDirty() && Pmvie()->FUnusedSndsUser() && !Pmvie()->FReadOnly() && Pmvie()->FGetFni(&fni))
    {
        vpappb->BeginLongOp();
        fRet = _pdocb->FSave(); // Flush sounds
        goto LSaved;
    }

    if (Pmvie()->Cscen() > 0)
    {
        fRet = _pdocb->FQueryClose(fAssumeYes ? fdocAssumeYes : fdocNil);
    }
    else
    {
        fRet = fTrue;
    }
    vpappb->BeginLongOp();

LSaved:
    Pmvie()->SetDocClosing(fFalse);
    if (fRet && !fSaveDDG)
    {
        // Beware: the following line destroys the this pointer!
        _pdocb->CloseAllDdg();
    }
    vpappb->EndLongOp();
    return fRet;
}

/***************************************************************************
 *
 * Handle a save, save as or save a copy command.
 *
 * Parameters:
 *	pcmd - The command to process
 *
 * Returns:
 *  fTrue.
 *
 **************************************************************************/
bool MovieView::FCmdSave(PCommand pcmd)
{
    if (Pmvie()->Cscen() < 1)
    {
        PushErc(ercSocSaveFailure);
    }
    else
    {
        _pdocb->FSave(pcmd->cid);
    }

    return fTrue;
}

/***************************************************************************
 *
 * Note that we have had an idle loop.
 *
 * Parameters:
 *	pcmd - Pointer to the command to process.
 *
 * Returns:
 *  fFalse.
 *
 ***************************************************************************/
bool MovieView::FCmdIdle(PCommand pcmd)
{
    AssertThis(0);
    AssertVarMem(pcmd);

    Pmvie()->SetFIdleSeen(fTrue);
    return (fFalse);
}

/***************************************************************************
 *
 * Note that the mouse is no longer on the view.
 *
 * Parameters:
 *	pcmd - Pointer to the command to process.
 *
 * Returns:
 *  fFalse.
 *
 ***************************************************************************/
bool MovieView::FCmdRollOff(PCommand pcmd)
{
    AssertThis(0);
    AssertVarMem(pcmd);

    if (_fMouseOn && (Tool() == toolListener))
    {
        Pmvie()->Pmsq()->StopAll();
    }

    _fMouseOn = fFalse;

    return (fFalse);
}

/***************************************************************************
 *
 * Handle a key command. Esc clears the entire selection (primary actor +
 * extras). All other keys defer to the base class.
 *
 * Parameters:
 *	pcmd - Pointer to the key command to process.
 *
 * Returns:
 *  fTrue if the key was consumed (Esc); otherwise the base class result.
 *
 ***************************************************************************/
bool MovieView::FCmdKey(PCMD_KEY pcmd)
{
    AssertThis(0);
    AssertVarMem(pcmd);

    if (pcmd->vk == kvkEscape)
    {
        if (Pmvie()->Pscen() != pvNil)
        {
            Pmvie()->Pscen()->ClearSelection(); // already calls InvalViews internally
            Pmvie()->Pbwld()->MarkDirty();
        }
        return fTrue; // command consumed
    }

    // Ctrl+A (Cmd+A on Mac) selects every actor in the current scene.
    // In text mode, defer to the base class so the focused TBOX edit can
    // interpret it as "select all text" instead.
    if (pcmd->vk == 'A' && (pcmd->grfcust & fcustCmd) && !FTextMode())
    {
        if (Pmvie()->Pscen() != pvNil)
        {
            if (Pmvie()->Pscen()->FSelectAllActrs())
            {
                Pmvie()->Pbwld()->MarkDirty();
            }
        }
        return fTrue; // command consumed
    }

    // Ctrl+G: tag the current selection as group #N (auto-numbered).
    // Ctrl+Shift+G: strip the rightmost #tag from each selected actor.
    // In text mode defer to the base.
    if (pcmd->vk == 'G' && (pcmd->grfcust & fcustCmd) && !FTextMode())
    {
        PScene pscen = Pmvie()->Pscen();
        if (pscen != pvNil)
            pscen->EditActorSelectionTags((pcmd->grfcust & fcustShift) != 0);
        return fTrue;
    }

    // Ctrl+1..Ctrl+9: replace the current selection with all actors in the
    // current scene whose name has the matching numeric #tag, and warp the
    // cursor to the (newly-primary) actor so it's clear which group was
    // recalled. Silent no-op if the tag isn't bound to any actor.
    if (pcmd->vk >= '1' && pcmd->vk <= '9' && (pcmd->grfcust & fcustCmd) && !FTextMode())
    {
        PScene pscen = Pmvie()->Pscen();
        if (pscen != pvNil)
        {
            achar szTag[2];
            szTag[0] = (achar)pcmd->vk;
            szTag[1] = (achar)0;
            if (pscen->FSelectActrsByTag(szTag) && pscen->CactrSelected() >= 1)
            {
                PActor pactr = pscen->PactrSelectedAt(0);
                if (pactr != pvNil)
                    WarpCursToActor(pactr);
                Pmvie()->Pbwld()->MarkDirty();
            }
        }
        return fTrue;
    }

    return MovieView_PAR::FCmdKey(pcmd);
}

#ifdef DEBUG
/***************************************************************************
 *
 * Assert the validity of the MovieView.
 *
 * Parameters:
 *  grf - Bit field of options
 *
 * Returns:
 *  None.
 *
 **************************************************************************/
void MovieView::AssertValid(LegacyUlong grf)
{
    MovieView_PAR::AssertValid(fobjAllocated);
}

/***************************************************************************
 *
 * Mark memory used by the MovieView
 *
 * Parameters:
 *  None.
 *
 * Returns:
 *  None.
 *
 **************************************************************************/
void MovieView::MarkMem(void)
{
    AssertThis(0);
    MovieView_PAR::MarkMem();
    MarkMemObj(Pmvie());
    MarkMemObj(static_cast<PUndoBase>(_actorDrag.undo.single));
    MarkMemObj(static_cast<PUndoBase>(_actorDrag.undo.group));
    MarkMemObj(static_cast<PActor>(_pointerSelection.target));
    MarkMemObj(static_cast<PActor>(_placement.actor));
    MarkMemObj(static_cast<PActor>(_placement.snapshot));
    MarkMemObj(static_cast<PActor>(_recordGesture.restore));
#ifdef KAUAI_SDL3
    MarkMemObj(_pgptSuperSize);
    MarkMemObj(_pgptSuperSizeIndexed);
#endif
}
#endif // DEBUG

//
//
//
// UNDO STUFF
//
//
//

/****************************************************
 *
 * Public constructor for movie undo objects for scene
 * related commands.
 *
 * Parameters:
 *	None.
 *
 * Returns:
 *  pvNil if failure, else a pointer to the movie undo.
 *
 ****************************************************/
PMovieSceneUndo MovieSceneUndo::PmunsNew()
{
    PMovieSceneUndo pmuns;
    pmuns = NewObj MovieSceneUndo();
    return (pmuns);
}

/****************************************************
 *
 * Destructor for movies undo objects
 *
 ****************************************************/
MovieSceneUndo::~MovieSceneUndo(void)
{
    AssertBaseThis(0);
    ReleasePpo(&_pscen);
}

/****************************************************
 *
 * Does a command stored in an undo object.
 *
 * Parameters:
 *	pdocb - The owning document.
 *
 * Returns:
 *  fTrue if successful, else fFalse.
 *
 ****************************************************/
bool MovieSceneUndo::_Apply(bool redo)
{
    const auto scenes = Pmvie()->_SceneServices();
    MMMovieSceneUndoServices s = {};
    s.scenes = &scenes;
    s.scene_edit = [](void *p) -> MMSceneEditServices {
        return static_cast<PScene>(p)->_EditServices();
    };
    s.notify = [](void *p, int removed) {
        auto client = static_cast<PMovie>(p)->Pmcc();
        if (removed) client->SceneNuked();
        else client->SceneUnnuked();
    };
    s.unknown = [](void *) { Bug("Unknown munst"); };
    const MMTag tag = redo && _munst == munstInsScen ? TagToCore(_tag) : MMTag{};
    return mm_movie_scene_undo(_munst, _iscen, _pscen, &tag, redo, &s) != 0;
}

bool MovieSceneUndo::FDo(PDocumentBase pdocb)
{
    AssertThis(0);
    return _Apply(true);
}

/****************************************************
 *
 * Undoes a command stored in an undo object.
 *
 * Parameters:
 *	pdocb - The owning document.
 *
 * Returns:
 *  fTrue if successful, else fFalse.
 *
 ****************************************************/
bool MovieSceneUndo::FUndo(PDocumentBase pdocb)
{
    AssertThis(0);
    return _Apply(false);
}

#ifdef DEBUG
/****************************************************
 * Mark memory used by the MovieSceneUndo
 *
 * Parameters:
 * 	None.
 *
 * Returns:
 *  None.
 *
 ****************************************************/
void MovieSceneUndo::MarkMem(void)
{
    AssertThis(0);
    MovieSceneUndo_PAR::MarkMem();
    MarkMemObj(_pscen);
}

/***************************************************************************
 *
 * Assert the validity of the MovieSceneUndo.
 *
 * Parameters:
 *  grf - Bit field of options
 *
 * Returns:
 *  None.
 *
 **************************************************************************/
void MovieSceneUndo::AssertValid(LegacyUlong grf)
{
    AssertNilOrPo(_pscen, 0);
}
#endif // DEBUG

#ifdef DEBUG
/***************************************************************************
 *
 * Assert the validity of the MovieUndo.
 *
 * Parameters:
 *  grf - Bit field of options
 *
 * Returns:
 *  None.
 *
 **************************************************************************/
void MovieUndo::AssertValid(LegacyUlong grf)
{
    MovieUndo_PAR::AssertValid(fobjAllocated);
    AssertPo(Pmvie(), 0);
}
#endif // DEBUG
