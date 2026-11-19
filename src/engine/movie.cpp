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
#include "studio_preferences.h"
#include "mm/indexed_display.h"
ASSERTNAME

#define krScaleMouseRecord BR_SCALAR(0.20)
#define krScaleMouseNonRecord BR_SCALAR(0.02)
#define kdwrMousePerSecond BR_SCALAR(100.0) // "mouse drag" speed for arrow keys

#define kdtsFrame (kdtsSecond / kfps) // milliseconds per frame
#define kdtsCycleCels (kdtsFrame * 3) // delay when cycling cels
#define kzrMouseScalingFactor BR_SCALAR(100.0)

const ChildChunkID kchidGstSource = 1;

//
// How many pixels from edge to warp cursor back to center
//
const long kdpInset = 50;

//
// Mouse scaling factor when rotating
//
const long krRotateScaleFactor = BR_SCALAR(0.001);

//
// Mouse scaling factor when sooner/latering.
//
const long krSoonerScaleFactor = BR_SCALAR(0.05);

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
    MMMovieSceneUndoServices _Services();
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
    DataBlock sources;
    explicit SaveHost(PMovie value) : movie(value) {}
};

MMMovieSaveServices Movie::_SaveServices(SaveHost *host)
{
    static_assert(kctgScen == MM_SAVE_SCENE && kctgGst == MM_SAVE_TABLE &&
        kctgMtrl == MM_SAVE_MATERIAL && kctgTmpl == MM_SAVE_TEMPLATE &&
        kctgMsnd == MM_SAVE_SOUND && ksidUseCrf == 0 && kchidGstSource == 1 && cnoNil == 0xffffffffU,
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
    s.file = [](void *p) -> void * { return static_cast<PChunkyResourceFile>(p)->Pcfl(); };
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
    s.first_root = [](void *p) -> MMU32 {
        ChunkIdentification chunk;
        AssertDo(static_cast<PChunkyFile>(p)->FGetCkiCtg(kctgMvie, 0, &chunk), "Missing saved movie root");
        return chunk.cno;
    };
    s.create_root = [](void *, void *p, MMU32 *number) -> int { return MovieData::FCreateRoot(static_cast<PChunkyFile>(p), number); };
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
    s.child = [](void *p, MMU32 root, MMU32 type, MMU32 slot, MMU32 *number) -> int {
        ChildChunkIdentification child;
        bool found = static_cast<PChunkyFile>(p)->FGetKidChidCtg(kctgMvie, root, slot, type, &child);
        if (found) *number = child.cki.cno;
        return found;
    };
    s.adopt = [](void *p, MMU32 root, MMU32 type, MMU32 number, MMU32 slot) -> int {
        return static_cast<PChunkyFile>(p)->FAdoptChild(kctgMvie, root, type, number, slot);
    };
    s.delete_child = [](void *p, MMU32 root, MMU32 type, MMU32 number, MMU32 slot) {
        static_cast<PChunkyFile>(p)->DeleteChild(kctgMvie, root, type, number, slot);
    };
    s.delete_chunk = [](void *p, MMU32 type, MMU32 number) { static_cast<PChunkyFile>(p)->Delete(type, number); };
    s.actor_count = [](void *p) -> MMI32 { return static_cast<PRollCallTable>(p)->IvMac(); };
    s.actor_refs = [](void *p, MMI32 index, MMTag *tag) -> MMI32 {
        RollCallActorEntry actor;
        static_cast<PRollCallTable>(p)->GetExtra(index, &actor);
        *tag = TagToCore(actor.tagTmpl);
        return actor.cactRef;
    };
    s.erase_actor = [](void *p, MMI32 index) { static_cast<PRollCallTable>(p)->Delete(index); };
    s.close_tag = [](void *, MMTag *value) { TAG tag; TagFromCore(&tag, *value); vptagm->CloseTag(&tag); };
    s.rollcall_changed = [](void *p) { static_cast<SaveHost *>(p)->movie->_pmcc->UpdateRollCall(); };
    s.write_rollcall = [](void *, void *p, MMU32 root, void *table, MMU32 *number) -> int {
        return MovieData::FWriteRollCall(static_cast<PChunkyFile>(p), root, static_cast<PRollCallTable>(table), number);
    };
    s.source_bytes = [](void *) -> MMU32 { return vptagm->CbSourceNames(); };
    s.add_sources = [](void *p, void *file, MMU32 bytes, MMU32 *number) -> int {
        return static_cast<PChunkyFile>(file)->FAdd(bytes, kctgGst, number, &static_cast<SaveHost *>(p)->sources);
    };
    s.write_sources = [](void *p) -> int { return vptagm->FWriteSourceNames(&static_cast<SaveHost *>(p)->sources); };
    s.release_sources = [](void *p) { static_cast<SaveHost *>(p)->sources.Free(); };
    s.write_title = [](void *p, void *file) -> int { return static_cast<SaveHost *>(p)->movie->_data.FWriteTitle(static_cast<PChunkyFile>(file)); };
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
    s.new_tags = [](void *) -> void * { return TagList::PtaglNew(); };
    s.scene_tags = [](void *p, MMU32 scene, void *tags) -> int {
        return Scene::FAddTagsToTagl(static_cast<PChunkyFile>(p), scene, static_cast<PTagList>(tags));
    };
    s.tag_count = [](void *p) -> MMI32 { return static_cast<PTagList>(p)->Ctag(); };
    s.tag = [](void *p, MMI32 index, MMTag *value) { TAG tag; static_cast<PTagList>(p)->GetTag(index, &tag); *value = TagToCore(tag); };
    s.release_tags = [](void *p) { auto tags = static_cast<PTagList>(p); ReleasePpo(&tags); };
    s.chunk = [](void *p, MMU32 type, MMI32 index, MMU32 *number) -> int {
        ChunkIdentification chunk;
        if (!static_cast<PChunkyFile>(p)->FGetCkiCtg(type, index, &chunk)) return 0;
        *number = chunk.cno;
        return 1;
    };
    s.toss_cache = [](void *p, MMU32 type, MMU32 number) {
        auto reader = type == kctgMtrl ? Material_MTRL::FReadMtrl : Template::FReadTmpl;
        static_cast<SaveHost *>(p)->movie->_data.Resources()->FSetCrep(crepToss, type, number, reader);
    };
    s.child_count = [](void *p, MMU32 root) -> MMI32 { return static_cast<PChunkyFile>(p)->Ckid(kctgMvie, root); };
    s.references = [](void *p, MMU32 type, MMU32 number) -> MMI32 { return static_cast<PChunkyFile>(p)->CckiRef(type, number); };
    s.child_at = [](void *p, MMU32 root, MMI32 index, MMChunkChild *value) -> int {
        ChildChunkIdentification child;
        if (!static_cast<PChunkyFile>(p)->FGetKid(kctgMvie, root, index, &child)) return 0;
        value->type = child.cki.ctg; value->number = child.cki.cno; value->slot = child.chid;
        return 1;
    };
    s.sound_child = [](void *p, MMU32 number) -> int {
        ChildChunkIdentification child;
        return static_cast<PChunkyFile>(p)->FGetKidChid(kctgMsnd, number, kchidSnd, &child);
    };
    s.unused_sounds = [](void *p, int *valid) -> int {
        bool haveValid;
        bool unused = static_cast<SaveHost *>(p)->movie->FUnusedSndsUser(&haveValid);
        *valid = haveValid;
        return unused;
    };
    s.purge_sounds = [](void *p) -> int { return static_cast<SaveHost *>(p)->movie->Pmcc()->FQueryPurgeSounds(); };
    s.bad_child = [](void *) { Bug("ChunkyFile returned bogus Ckid()"); };
    s.missing_scene = [](void *) { AssertDo(false, "Should never fail"); };
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
    _playback.wait = witNil;
    _playback.transition = transNil;
    _playback.fadeOriginalVolume = 0;

    SetCundbMax(1);
}

/******************************************************************************
    _FSetPfilSave
        Given an Filename, looks for and remembers if found the FileObject associated with
        it.  If the FileObject was found, will also check to see if it's read-only.

    Returns:
        fFalse if the FileObject wasn't found.

************************************************************ PETED ***********/
bool Movie::_FSetPfilSave(PFilename pfni)
{
    AssertBaseThis(0);
    AssertPo(pfni, 0);
    SaveHost host(this);
    auto services = _SaveServices(&host);
    return mm_movie_save_bind(pfni, &services) != 0;
}

/****************************************************
 *
 * Reads a movie from a file.
 *
 * Parameters:
 *  pmcc - Pointer to the movie client class block to use.
 *	pfni - File to read from.
 *	cno - ChunkNumber of the movie chunk, cnoNil if using the
 *		the first one in the file.
 *
 * Returns:
 *  pvNil if failure, else a pointer to the movie object.
 *
 ****************************************************/
PMovie Movie::PmvieNew(bool fHalfMode, PMovieClientCallbacks pmcc, Filename *pfni, ChunkNumber cno)
{
    AssertNilOrPo(pfni, 0);
    AssertPo(pmcc, 0);

    bool fSuccess = fFalse, fBeganLongOp = fFalse;
    PMovie pmvie;
    TagList *ptagl;
    PChunkyFile pcfl = pvNil;
    Filename containerWorking;

    //
    // Create the movie object
    //
    pmvie = NewObj Movie;
    if (pmvie == pvNil)
    {
        goto LFail;
    }
    if (MovieContainer::prepare)
    {
        if (!MovieContainer::prepare(pfni, &containerWorking, pmvie->_container))
            goto LFail;
        if (MovieContainer::IsVmm(pfni))
        {
            pmvie->_containerName = *pfni;
            pmvie->_save.containerNamed = true;
            pfni = &containerWorking;
        }
    }
    else if (MovieContainer::IsVmm(pfni))
    {
        PushErc(ercSocBadFile);
        goto LFail;
    }

    //
    // Prepare the C-owned undo collection.
    //
    if (!pmvie->_FInitUndo())
    {
        goto LFail;
    }

    //
    // Create DynamicArray of actors in the movie
    //
    if (pvNil == pfni)
    {
        pmvie->_data.actors = RollCallTable::New();
        if (pmvie->_data.Actors() == pvNil)
        {
            goto LFail;
        }
    }

    //
    // Create the brender world
    //
    pmvie->_pbwld = World::PbwldNew(pmcc->Dxp(), pmcc->Dyp(), fHalfMode, fHalfMode);
    if (pvNil == pmvie->_pbwld)
    {
        goto LFail;
    }

    //
    // Create the movie sound queue
    //
    pmvie->_pmsq = MovieSoundQueue::PmsqNew();
    if (pvNil == pmvie->_pmsq)
    {
        goto LFail;
    }

    //
    // Save other variables
    //
    pmvie->_pmcc = pmcc;
    pmcc->AddRef();

    //
    // Do we initialize from a file?
    //
    if (pfni == pvNil)
    {
        return (pmvie);
    }

    /* Don't bother putting up the wait cursor unless we're reading a movie */
    vpappb->BeginLongOp();
    fBeganLongOp = fTrue;

    //
    // Get file to read from
    //
    if (!pmvie->_data.FLoad(pfni, cno))
        goto LFail;
    cno = pmvie->_data.movieNumber;
    pcfl = pmvie->_data.Resources()->Pcfl();
    pcfl->AddRef();

    if (!pmvie->_FSetPfilSave(pfni))
    {
        Bug("Hey, we just opened this file!");
        goto LFail;
    }

    //
    // Merge this document's source title list
    //
    vptagm->FMergeMovieSources(pcfl, cno); // Source titles are best-effort.

    //
    // Get all the content tags
    //
    ptagl = pmvie->_PtaglFetch();
    if (ptagl == pvNil)
    {
        goto LFail;
    }

    //
    // Now bring all the tags into cache
    //
    if (!ptagl->FCacheTags())
    {
        ReleasePpo(&ptagl);
        goto LFail;
    }

    ReleasePpo(&ptagl);

    //
    // Set the movie title
    //
    pmvie->_SetTitle(pmvie->_save.containerNamed ? &pmvie->_containerName : pfni);
    if (pmvie->_save.containerNamed)
    {
        String path;
        pmvie->_containerName.GetStnPath(&path);
        KauaiFiles::PathInfo info;
        pmvie->_save.readOnly = KauaiFiles::Info(path.Psz(), &info) == KauaiFiles::Status::Found && info.readOnly;
    }

    fSuccess = fTrue;

LFail:
    ReleasePpo(&pcfl);
    if (!fSuccess)
        ReleasePpo(&pmvie);
    if (fBeganLongOp)
        vpappb->EndLongOp();

    return (pmvie);
}

/******************************************************************************
    FReadRollCall
        Reads the roll call off file for a given movie.  Will swapbytes the
        extra data in the StringTable_GST if necessary, and will report back on the
        highest arid found.

    Arguments:
        PChunkyFile pcfl       -- the file the movie is on
        PChunkyResourceFile pcrf       -- the autosave ChunkyResourceFile for the movie's Actor tags
        ChunkNumber cno         -- the cno of the movie
        PStringTable_GST *ppgst     -- the PStringTable_GST to fill in
        long *paridLim  -- the max arid to update

    Returns: fTrue if there were no failures, fFalse otherwise

************************************************************ PETED ***********/
bool Movie::FReadRollCall(PChunkyResourceFile pcrf, ChunkNumber cno, PRollCallTable *ppgst, LegacyLong *paridLim)
{
    return MovieData::FReadRollCall(pcrf, cno, ppgst, paridLim);
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

    if (_fUserPaused && vpsndm)
        vpsndm->ResumeAll();
    _fUserPaused = false;

    _data.ReleaseResources();
    SaveHost saveHost(this);
    auto saveServices = _SaveServices(&saveHost);
    mm_movie_save_release(0, &saveServices);

    if (FPlaying())
    {
        AssertPo(Pmcc(), 0);
        Pmcc()->EnableAccel();
        Pmcc()->PlayStopped();
        vpsndm->StopAll();

        // if we were fading, then restore sound volume
        if (_playback.fadeOriginalVolume)
        {
            if (_playback.fadeChangedVolume || !CurrentStudioPreferences().disableAudioFades)
                vpsndm->SetVlm(_playback.fadeOriginalVolume);
            _playback.fadeOriginalVolume = 0;
        }
    }

    if (Pmcc() != pvNil)
    {
        Pmcc()->UpdateRollCall();
    }

    if (Pscen() != pvNil)
    {

        //
        // Release open scene.
        //
        auto services = _SceneServices();
        mm_movie_scene_close_open(&services);
    }

    //
    // Release the roll call
    //
    _data.ClearActors();

    //
    // Release the call back class
    //
    ReleasePpo(&_pmcc);

    //
    // Release the brender world
    //
    ReleasePpo(&_pbwld);

    //
    // Release the sound queue
    //
    ReleasePpo(&_pmsq);

    ReleasePpo(&_pglclrThumbPalette);

    return;
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

    MarkMemObj(_data.Actors());

    MarkMemObj(Pscen());

    MarkMemObj(Pbwld());

    MarkMemObj(_pmcc);

    MarkMemObj(_pmsq);

    MarkMemObj(_pglclrThumbPalette);
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
    AssertPo(_data.Actors(), 0);
    AssertNilOrPo(Pbwld(), 0);
    AssertPo(&_clok, 0);
    AssertPo(_pmcc, 0);
}

#endif // DEBUG

/***************************************************************************
 *
 * Returns a list of all tags being used by this Movie
 *
 * Parameters:
 *	None.
 *
 * Returns:
 *  A TagList (list of tags that the movie uses)
 *
 **************************************************************************/
PTagList Movie::_PtaglFetch(void)
{
    AssertThis(0);
    Assert(_data.Resources(), "need pcrfAutosave");
    SaveHost host(this);
    auto services = _SaveServices(&host);
    return static_cast<PTagList>(mm_movie_save_tags(&services));
}

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
    s.count = [](void *p) -> MMI32 { return static_cast<PRollCallTable>(p)->storage.count; };
    s.row = [](void *p, MMI32 index, MMRollCallActor *out) {
        AssertDo(mm_rollcall_table_extra(&static_cast<PRollCallTable>(p)->storage, index, out), "bad actor row");
    };
    s.put = [](void *p, MMI32 index, const MMRollCallActor *value) {
        AssertDo(mm_rollcall_table_set_extra(&static_cast<PRollCallTable>(p)->storage, index, value), "bad actor row");
    };
    s.name = [](void *p, MMI32 index, void *out) { static_cast<PRollCallTable>(p)->GetStn(index, static_cast<PString>(out)); };
    s.rename = [](void *p, MMI32 index, const void *name) -> int {
        auto value = const_cast<PString>(static_cast<const String *>(name));
        return mm_rollcall_table_rename(&static_cast<PRollCallTable>(p)->storage, index, value->Prgch(), value->Cch());
    };
    s.append = [](void *p, const void *name, const MMRollCallActor *row) -> int {
        auto value = const_cast<PString>(static_cast<const String *>(name));
        AssertPo(value, 0);
        return mm_rollcall_table_add(&static_cast<PRollCallTable>(p)->storage, value->Prgch(), value->Cch(), row);
    };
    s.erase = [](void *p, MMI32 index) { static_cast<PRollCallTable>(p)->Delete(index); };
    s.actor_id = [](void *p) -> MMI32 { return static_cast<PActor>(p)->Arid(); };
    s.set_actor_id = [](void *p, MMI32 id) { static_cast<PActor>(p)->SetArid(id); };
    s.actor_tag = [](void *p, MMTag *out) { TAG tag; static_cast<PActor>(p)->GetTagTmpl(&tag); *out = TagToCore(tag); };
    s.is_prop = [](void *p) -> int { return static_cast<PActor>(p)->FIsPropBrws(); };
    s.is_text = [](void *p) -> int { return static_cast<PActor>(p)->FIsTdt(); };
    s.same_tag = [](const MMTag *a, const MMTag *b) -> int {
        TAG first, second; TagFromCore(&first, *a); TagFromCore(&second, *b);
        return TagManager::FcmpCompareTags(&first, &second) == fcmpEq;
    };
    s.open_tag = [](void *, MMTag *value, void *resources) {
        TAG tag; TagFromCore(&tag, *value);
        AssertDo(TagManager::FOpenTag(&tag, static_cast<PChunkyResourceFile>(resources)), "Should never fail when not copying the tag");
        *value = TagToCore(tag);
    };
    s.duplicate_tag = [](const MMTag *value) { TAG tag; TagFromCore(&tag, *value); TagManager::DupTag(&tag); };
    s.close_tag = [](MMTag *value) { TAG tag; TagFromCore(&tag, *value); TagManager::CloseTag(&tag); *value = TagToCore(tag); };
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
    static_assert(rOne == 65536 && vlmNil == -1 && styNil == 0, "Movie edit scalar/sound ABI");
    MMMovieEditServices s = {};
    s.context = this; s.data = &_data; s.scenes = &_scenes;
    s.rollcall = _RollCallServices(); s.undo = _UndoServices();
    s.selected_actor = [](void *p) -> void * { return static_cast<PScene>(p)->PactrSelected(); };
    s.actor_from_id = [](void *p, MMI32 id) -> void * { return static_cast<PScene>(p)->PactrFromArid(id); };
    s.select_actor = [](void *p, void *actor) { static_cast<PScene>(p)->SelectActr(static_cast<PActor>(actor)); };
    s.first_view = [](void *p) -> void * { return static_cast<PMovieView>(static_cast<PMovie>(p)->PddgGet(0)); };
    s.new_actor = [](void *, const MMTag *value) -> void * { TAG tag; TagFromCore(&tag, *value); return Actor::PactrNew(&tag); };
    s.actor_visible = [](void *p) -> int { return static_cast<PActor>(p)->FIsInView(); };
    s.core_onstage = [](void *p) -> int { return static_cast<PActor>(p)->FAddOnStageCore(); };
    s.duplicate_actor = [](void *p, int reset) -> void * {
        PActor copy = nullptr; if (!static_cast<PActor>(p)->FDup(&copy, reset != 0)) return nullptr; return copy;
    };
    s.release_actor = [](void *p) { auto actor = static_cast<PActor>(p); ReleasePpo(&actor); };
    s.restore_actor = [](void *p, void *copy) { static_cast<PActor>(p)->Restore(static_cast<PActor>(copy)); };
    s.add_actor = [](void *p, void *actor) -> int { return static_cast<PScene>(p)->FAddActr(static_cast<PActor>(actor)); };
    s.remove_actor = [](void *p, MMI32 id) -> int { return static_cast<PScene>(p)->FRemActr(id); };
    s.apply_actor = [](void *p, int kind, const MMMovieEditRequest *q) -> int {
        auto actor = static_cast<PActor>(p); AssertPo(actor, 0);
        TAG tag; if (q->tag) TagFromCore(&tag, *q->tag);
        switch (kind)
        {
        case MM_MOVIE_EDIT_ROTATE: return actor->FRotate(q->rotation[0], q->rotation[1], q->rotation[2], q->forward != 0);
        case MM_MOVIE_EDIT_SQUASH: return actor->FPull(q->pull[0], q->pull[1], q->pull[2]);
        case MM_MOVIE_EDIT_TIME: return actor->FSoonerLater(q->shift);
        case MM_MOVIE_EDIT_SCALE: return actor->FScale(q->factor);
        case MM_MOVIE_EDIT_COSTUME: return actor->FSetCostume(q->part, q->tag ? &tag : nullptr, q->costume, tribool(q->custom));
        case MM_MOVIE_EDIT_ACTOR_SOUND: return actor->FSetSnd(q->tag ? &tag : nullptr, tribool(q->loop), tribool(q->queue), tribool(q->cel), q->volume, q->style);
        case MM_MOVIE_EDIT_ONSTAGE: return actor->FAddOnStage();
        default: Bug("Invalid Movie actor primitive"); return 0;
        }
    };
    s.apply_scene = [](void *p, int kind, const MMMovieEditRequest *q) -> int {
        auto scene = static_cast<PScene>(p);
        TAG tag; if (q->tag) TagFromCore(&tag, *q->tag);
        switch (kind)
        {
        case MM_MOVIE_EDIT_CAMERA: return scene->FChangeCam(q->camera);
        case MM_MOVIE_EDIT_PAUSE: return scene->FPause(WaitReason(q->reason), q->delay);
        case MM_MOVIE_EDIT_TRANSITION: return scene->FSetTransition(TRANS(q->transition));
        case MM_MOVIE_EDIT_BACKGROUND_SOUND: return scene->FAddSnd(q->tag ? &tag : nullptr, tribool(q->loop), tribool(q->queue), q->volume, q->style);
        default: Bug("Invalid Movie scene primitive"); return 0;
        }
    };
    s.frame = [](void *p) -> MMI32 { return static_cast<PScene>(p)->Nfrm(); };
    s.undo_count = [](void *p) -> MMI32 { return static_cast<PMovie>(p)->CundbUndo(); };
    s.last_undo = [](void *p) -> void * {
        auto movie = static_cast<PMovie>(p);
        return movie->_PundbGet(movie->_undoList.done - 1);
    };
    s.actor_undo = [](void *p) -> int { return static_cast<PUndoBase>(p)->FIs(kclsActorUndo); };
    s.time_undo = [](void *p) -> int { return static_cast<PActorUndo>(p)->FSoonerLater(); };
    s.time_undo_frame = [](void *p, MMI32 frame) { static_cast<PActorUndo>(p)->SetNfrmLast(frame); };
    s.place_actor = [](void *p, int entire) { static_cast<PMovieView>(p)->StartPlaceActor(entire != 0); };
    s.placement_undo = [](void *p, void *owned) { static_cast<PMovieView>(p)->SetActrUndo(static_cast<PActor>(owned)); };
    s.paste_actor = [](void *p, void *actor) -> int { return static_cast<PScene>(p)->FPasteActr(static_cast<PActor>(actor)); };
    s.paste_route = [](void *p, void *source) -> int { return static_cast<PActor>(p)->FPasteRte(static_cast<PActor>(source)); };
    s.create_actor_undo = [](void *p, void *copy) -> int { return static_cast<PActor>(p)->FCreateUndo(static_cast<PActor>(copy)); };
    s.long_op = [](void *, int begin) { if (begin) vpappb->BeginLongOp(); else vpappb->EndLongOp(); };
    s.cache_tag = [](void *, const MMTag *value, int background) -> int {
        TAG tag; TagFromCore(&tag, *value); return background ? Background::FCacheToHD(&tag) : vptagm->FCacheTagToHD(&tag);
    };
    s.default_title = [](void *p) { static_cast<PMovie>(p)->_SetTitle(); };
    s.empty_scene = [](void *p) -> int { return static_cast<PScene>(p)->FIsEmpty(); };
    s.new_scene = [](void *p, MMI32 index) -> int { return static_cast<PMovie>(p)->FNewScenInsCore(index); };
    s.remove_scene = [](void *p, MMI32 index) -> int { return static_cast<PMovie>(p)->FRemScenCore(index); };
    s.background = [](void *p, const MMTag *value, int undoable) -> int {
        TAG tag, old; TagFromCore(&tag, *value); auto scene = static_cast<PMovie>(p)->Pscen();
        return undoable ? scene->FSetBkgd(&tag) : scene->FSetBkgdCore(&tag, &old);
    };
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
    const auto services = _RollCallServices();
    MMI32 id, refs;
    MMTag tag;
    if (!mm_movie_rollcall_get(iarid, &id, pstn, &refs, ptagTmpl ? &tag : nullptr, &services)) return false;
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
    const auto services = _RollCallServices();
    return mm_movie_rollcall_name(arid, pstn, &services) != 0;
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
    AssertThis(0); AssertIn(arid, 0, 500); AssertPo(pstn, 0);
    const auto services = _RollCallServices();
    return mm_movie_rollcall_rename(arid, pstn, &services) != 0;
}

/****************************************************
 *
 * Identifies whether the mactr is in prop browser
 *
 * Parameters:
 *   imactr - index in _data.Actors()
 *
 * Returns:
 *	 bool
 *
 ****************************************************/
bool Movie::FIsPropBrwsIarid(long iarid)
{
    AssertThis(0); AssertIn(iarid, 0, _data.Actors()->IvMac());
    const auto services = _RollCallServices();
    return mm_movie_rollcall_has(iarid, MM_MOVIE_PROP, &services) != 0;
}

/****************************************************
 *
 * Identifies whether the mactr is in 3dtext object
 *
 * Parameters:
 *   imactr - index in _data.Actors()
 *
 * Returns:
 *	 bool
 *
 ****************************************************/
bool Movie::FIsIaridTdt(long iarid)
{
    AssertThis(0); AssertIn(iarid, 0, _data.Actors()->IvMac());
    const auto services = _RollCallServices();
    return mm_movie_rollcall_has(iarid, MM_MOVIE_TEXT, &services) != 0;
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
    return mm_movie_rollcall_add(pactr, pstn, &services) != 0;
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
    s.count = &_data.sceneCount;
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
    s.read = [](void *p, MMI32 index) -> void * {
        auto movie = static_cast<PMovie>(p);
        auto resources = movie->_data.Resources();
        ChunkNumber number;
        if (!movie->_data.FResolveScene(index, &number))
            return nullptr;
        return Scene::PscenRead(movie, resources, number, movie->MovieSession());
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
    s.scene_change = [](void *p) { static_cast<PMovie>(p)->_pmcc->SceneChange(); };
    s.invalidate = [](void *p) { static_cast<PMovie>(p)->InvalViewsAndScb(); };
    s.file = [](void *p) -> void * { return static_cast<PMovie>(p)->_data.Resources()->Pcfl(); };
    s.write = [](void *p, void *scene, MMU32 *number) -> int {
        return static_cast<PScene>(scene)->FWrite(static_cast<PMovie>(p)->_data.Resources(), number);
    };
    s.scene_number = [](void *p, void *file, MMI32 index) -> MMU32 {
        auto &data = static_cast<PMovie>(p)->_data;
        ChunkNumber number = cnoNil;
        Assert(file == data.Resources()->Pcfl(), "Scene lookup should use the current movie graph");
        (void)file;
        AssertDo(data.FResolveScene(index, &number), "Should never fail");
        return number;
    };
    s.move_child = [](void *p, void *file, MMI32 from, MMI32 to) {
        auto movie = static_cast<PMovie>(p);
        auto archive = static_cast<PChunkyFile>(file);
        ChildChunkIdentification kid;
        AssertDo(archive->FGetKidChidCtg(kctgMvie, movie->_data.movieNumber, from, kctgScen, &kid), "Should never fail");
        archive->ChangeChid(kctgMvie, movie->_data.movieNumber, kctgScen, kid.cki.cno, from, to);
    };
    s.adopt = [](void *p, void *file, MMU32 number, MMI32 index) -> int {
        return static_cast<PChunkyFile>(file)->FAdoptChild(kctgMvie, static_cast<PMovie>(p)->_data.movieNumber,
            kctgScen, number, index);
    };
    s.delete_child = [](void *p, void *file, MMU32 number, MMI32 index) {
        static_cast<PChunkyFile>(file)->DeleteChild(kctgMvie, static_cast<PMovie>(p)->_data.movieNumber,
            kctgScen, number, index);
    };
    s.delete_chunk = [](void *, void *file, MMU32 number) { static_cast<PChunkyFile>(file)->Delete(kctgScen, number); };
    s.save = [](void *file) { static_cast<PChunkyFile>(file)->FSave(kctgSoc); };
    s.remove_actors = [](void *scene) { static_cast<PScene>(scene)->RemActrsFromRollCall(); };
    s.add_actors = [](void *scene) { static_cast<PScene>(scene)->FAddActrsToRollCall(); };
    s.dirty = [](void *p) { static_cast<PMovie>(p)->SetDirty(); };
    s.rollcall_changed = [](void *p) { static_cast<PMovie>(p)->_pmcc->UpdateRollCall(); };
    s.switch_error = [](void *) { PushErc(ercSocSceneSwitch); };
    s.sounds_enabled = [](void *p) -> int { return static_cast<PMovie>(p)->FSoundsEnabled(); };
    s.play_sound = [](void *p) { static_cast<PMovie>(p)->_pmsq->PlayMsq(); };
    s.flush_sound = [](void *p) { static_cast<PMovie>(p)->_pmsq->FlushMsq(); };
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
MMMovieSoundServices Movie::_SoundServices()
{
    static_assert(kctgMvie==MM_SOUND_MOVIE && kctgScen==MM_SOUND_SCENE &&
        kctgMsnd==MM_SOUND_CHUNK && kctgSoc==MM_SOUND_CREATOR && kchidSnd==0 && ksidUseCrf==0,"Movie sound ABI");
    static_assert(sizeof(TAG)==sizeof(MMTag) && sizeof(ChunkNumber)==sizeof(MMU32) &&
        sizeof(ChildChunkID)==sizeof(MMU32),"Movie sound record ABI");
    MMMovieSoundServices s{}; s.context=this;
    s.read=[](void *p,MMMovieSoundState *state) {
        auto movie=static_cast<PMovie>(p); state->cache=movie->_data.Resources(); state->movie=movie->_data.movieNumber; state->scene=movie->_scenes.index;
    };
    s.file=[](void *p)->void * { return static_cast<PChunkyResourceFile>(p)->Pcfl(); };
    s.chunks=[](void *p) { return static_cast<PChunkyFile>(p)->ReadChunkAccess(); };
    s.adopt=[](void *p,MMU32 type,MMU32 number,MMU32 childType,MMU32 child,MMU32 slot) {
        return int(static_cast<PChunkyFile>(p)->FAdoptChild(type,number,childType,child,slot));
    };
    s.open_tag=[](void *,MMTag *v,void *cache) {
        TAG tag; std::memcpy(&tag,v,sizeof(tag)); bool ok=TagManager::FOpenTag(&tag,static_cast<PChunkyResourceFile>(cache));
        std::memcpy(v,&tag,sizeof(tag)); return int(ok);
    };
    s.close_tag=[](void *,MMTag *v) { TAG tag; std::memcpy(&tag,v,sizeof(tag)); TagManager::CloseTag(&tag); std::memcpy(v,&tag,sizeof(tag)); };
    s.save_tag=[](void *,MMTag *v,void *cache,int redirect) {
        TAG tag; std::memcpy(&tag,v,sizeof(tag)); bool ok=TagManager::FSaveTag(&tag,static_cast<PChunkyResourceFile>(cache),redirect!=0);
        std::memcpy(v,&tag,sizeof(tag)); return int(ok);
    };
    s.ensure_autosave=[](void *p) { return int(static_cast<PMovie>(p)->FEnsureAutosave()); };
    s.use_temp=[](void *p) { return int(static_cast<PMovie>(p)->_FUseTempFile()); };
    s.inspect_source=[](void *p) { Filename filename; static_cast<PChunkyFile>(p)->GetFni(&filename); };
    s.copy_chunk=[](void *p,MMU32 type,MMU32 number,void *destination,MMU32 *out) {
        ChunkNumber value; std::memcpy(&value,out,sizeof(value)); bool ok=static_cast<PChunkyFile>(p)->FCopy(type,number,static_cast<PChunkyFile>(destination),&value);
        std::memcpy(out,&value,sizeof(value)); return int(ok);
    };
    s.delete_chunk=[](void *p,MMU32 type,MMU32 number) { static_cast<PChunkyFile>(p)->Delete(type,number); };
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
    s.diagnostic=[](void *,int fault) {
        switch (fault) {
        case MM_SOUND_BAD_CHILD_ENUM: Bug("ChunkyFile returned bogus ckid"); break;
        case MM_SOUND_NO_CURRENT_SCENE: Bug("Scene chunk doesn't exist!"); break;
        case MM_SOUND_CANNOT_COPY: PushErc(ercSocCantCopyMsnd); break;
        case MM_SOUND_BAD_CHUNK_ENUM: Bug("Should never fail"); break;
        default: Bug("Unknown movie sound diagnostic"); break;
        }
    };
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
    AssertThis(0); AssertPo(pcfl,0); const auto services=_SoundServices();
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
    AssertThis(0); AssertVarMem(ptag); AssertNilOrVarMem(pcrf); const auto services=_SoundServices();
    MMTag tag; std::memcpy(&tag,ptag,sizeof(tag)); bool ok=mm_movie_sound_resolve(&tag,chid,cnoScen,pcrf,&services)!=0;
    std::memcpy(ptag,&tag,sizeof(tag)); return ok;
}
bool Movie::FSaveTagSnd(TAG *ptag)
{
    AssertVarMem(ptag);
    MMTag tag; std::memcpy(&tag,ptag,sizeof(tag)); const auto services=_SoundServices();
    bool ok=mm_movie_sound_save(&tag,&services)!=0; std::memcpy(ptag,&tag,sizeof(tag)); return ok;
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
    AssertThis(0); AssertVarMem(pchid); const auto services=_SoundServices();
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
    const auto services=_SoundServices(); MMU32 number; std::memcpy(&number,pcno,sizeof(number));
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
    AssertBaseThis(0); AssertPo(pcflSrc,0); AssertVarMem(pcnoDest); const auto services=_SoundServices();
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
    s.open_tag = [](MMTag *value, void *resources) -> int {
        TAG tag; TagFromCore(&tag, *value);
        bool ok = TagManager::FOpenTag(&tag, static_cast<PChunkyResourceFile>(resources));
        *value = TagToCore(tag); return ok;
    };
    s.close_tag = [](MMTag *value) { TAG tag; TagFromCore(&tag, *value); TagManager::CloseTag(&tag); *value = TagToCore(tag); };
    s.length = [](const void *name) -> MMI32 { return const_cast<PString>(static_cast<const String *>(name))->Cch(); };
    s.character = [](const void *name, MMI32 i) -> MMI32 {
        return static_cast<unsigned char>(const_cast<PString>(static_cast<const String *>(name))->Psz()[i]);
    };
    s.change_template = [](void *p, const MMTag *value) -> int {
        TAG tag; TagFromCore(&tag, *value); return static_cast<PActor>(p)->FChangeTagTmpl(&tag);
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
    const bool selected = _pmcc->GetFniSave(pfni, idsPortfMovieFilterLabel, idsPortfMovieFilterExt, idsPortfSaveMovieTitle,
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
    return (MovieView::PmvuNew(this, pgcb, _pmcc->Dxp(), _pmcc->Dyp()));
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
    AssertThis(0); AssertPo(Pscen(), 0);
    const auto services = _EditServices();
    MMMovieEditRequest q = {};
    q.frame = nfrm;
    return mm_movie_edit_actor(MM_MOVIE_EDIT_TIME, &q, &services) != 0;
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

/****************************************************
 *
 * Takes a scene and inserts it as scene number iscen,
 * and switches to the scene.
 *
 * Parameters:
 *	iscen - Scene number to insert the new scene as.
 *	pscen - Pointer to the scene to insert.
 *
 * Returns:
 *  fFalse if there was a failure, else fTrue.
 *
 ****************************************************/
bool Movie::FInsScenCore(long iscen, Scene *pscen)
{
    AssertThis(0);
    AssertIn(iscen, 0, Cscen() + 1);
    AssertPo(pscen, 0);
    Assert(pscen->Pmvie() == this, "Cannot insert a scene from another movie");
    auto services = _SceneServices();
    return mm_movie_scene_insert(iscen, pscen, &services) != 0;
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
    const auto tag = TagToCore(*ptag);
    return mm_movie_add_background(&tag, &services) != 0;
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
    if (_fUserPaused && FPlaying())
    {
        _playback.stopRequested = true;
        FCmdRender(nullptr); // The frozen clock cannot deliver the ordinary stop alarm.
        return;
    }
    MoviePlayEvaluator evaluator(_playback);
    for (;;)
    {
        MoviePlayCommand command = evaluator.Next();
        if (command.kind == MoviePlayCommandKind::Finished)
            return;
        MoviePlaybackReply reply;
        switch (command.kind)
        {
        case MoviePlayCommandKind::RenderStop:
            FCmdRender(pvNil);
            break;
        case MoviePlayCommandKind::CheckScene:
            reply.value = Pscen() != pvNil;
            break;
        case MoviePlayCommandKind::ResetSounds:
            _pmsq->StopAll();
            _pmsq->FlushMsq();
            break;
        case MoviePlayCommandKind::QueryEnd:
            reply.value = Iscen() + 1 == Cscen() && Pscen()->Nfrm() == Pscen()->NfrmLast();
            break;
        case MoviePlayCommandKind::Rewind:
            reply.success = FSwitchScen(0);
            if (reply.success)
                reply.success = Pscen()->FGotoFrm(Pscen()->NfrmFirst());
            break;
        case MoviePlayCommandKind::ReadTime:
            reply.timestamp = TsCurrent();
            break;
        case MoviePlayCommandKind::StartClock:
            _playback.framePacer.Reset(CurrentStudioPreferences().PlaybackFps());
            if (!_playback.unpaced)
                _clok.Start(0);
            SetFPlaying(true);
            vpcex->EnqueueCid(cidMviePlaying, pvNil, pvNil, true);
            reply.success = _playback.unpaced || _clok.FSetAlarm(0, this);
            break;
        case MoviePlayCommandKind::PrepareScene:
            _pmsq->SndOnLong();
            Pscen()->Enable(fscenPauses);
            Pscen()->SelectActr(pvNil);
            Pscen()->SelectTbox(pvNil);
            // Replay must precede prerendering: replayed camera changes invalidate it.
            reply.success = Pscen()->FReplayFrm(fscenPauses | fscenSounds | fscenActrs) && Pscen()->FStartPlaying();
            break;
        case MoviePlayCommandKind::NotifyStopped:
            Pmcc()->PlayStopped();
            break;
        case MoviePlayCommandKind::StartupFailed:
            Pmcc()->PlayStopped();
            if (command.flag)
                SetFStopPlaying(true);
            else
                SetFPlaying(false);
            _pmsq->PlayMsq();
            SetFSoundsEnabled(_playback.oldSoundsEnabled);
            break;
        case MoviePlayCommandKind::DisableAccelerators:
            Pmcc()->DisableAccel();
            break;
        case MoviePlayCommandKind::QueryOpening:
            reply.value = Iscen() == 0 && Pscen()->Nfrm() == Pscen()->NfrmFirst();
            break;
        case MoviePlayCommandKind::RefreshOpening:
            _pmcc->UpdateScrollbars();
            InvalViews();
            vpappb->UpdateMarked();
            break;
        case MoviePlayCommandKind::BackgroundSound:
            Pscen()->PlayBkgdSnd();
            break;
        case MoviePlayCommandKind::DispatchSounds:
            if (command.flag)
                _pmsq->PlayMsq();
            else
                _pmsq->FlushMsq();
            break;
        case MoviePlayCommandKind::Finished:
            break;
        }
        evaluator.Complete(reply);
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
    if (_fUserPaused == paused || (paused && !FPlaying()))
        return;
    _fUserPaused = paused;
    if (paused)
    {
        _playback.framePacer.Pause(TsCurrent());
        _timUserPause = _clok.TimCur();
        _clok.Stop(); // Registered alarms keep their remaining movie-clock time.
        if (vpsndm)
            vpsndm->PauseAll();
    }
    else
    {
        _playback.framePacer.Resume(TsCurrent());
        if (vpsndm)
            vpsndm->ResumeAll();
        if (FPlaying())
        {
            _clok.Start(_timUserPause);
            // A render already queued before pausing may have consumed its alarm.
            if (_fUserResumeRender && !_clok.FSetAlarm(0, this))
                SetFStopPlaying(true);
        }
        _fUserResumeRender = false;
    }
    InvalViews();
}
void Movie::SetFIdleSeen(bool fIdle)
{
    _playback.idleSeen = fIdle;
    if (fIdle && FPlaying() && !_fUserPaused && _playback.framePacer.TakeDue(TsCurrent()))
    {
        Command alarm = {};
        alarm.cid = cidAlarm;
        FCmdAlarm(&alarm); // Queue through the same fair handoff as clock alarms.
    }
}

bool Movie::FCmdAlarm(PCommand pcmd)
{
    AssertThis(0);
    AssertVarMem(pcmd);

    if (_fUserPaused && !_playback.stopRequested)
    {
        _fUserResumeRender = true;
        return true;
    }

    Command cmd;

    if (_playback.FShouldRender(FIdleSeen() ? false : vpcex->PgobTracking() != pvNil))
    {

        ClearPb(&cmd, size(Command));

        cmd.pcmh = this;
        cmd.cid = cidRender;
        vpcex->EnqueueCmd(&cmd);
    }
    else
    {

        //
        // Check again, asap.
        //
        if (!_clok.FSetAlarm(0, this))
        {
            PMovieView pmvu;

            //
            // Things are in a bad way.
            //
            Pmcc()->EnableAccel();
            SetFPlaying(fFalse);
            SetUserPaused(false);
            SetFStopPlaying(fFalse);
            SetFSoundsEnabled(_playback.oldSoundsEnabled);
            _clok.RemoveCmh(this);
            _playback.pausing = fFalse;
            _playback.scrolling = fFalse;
            _playback.wait = witNil;
            pmvu = (PMovieView)PddgGet(0);
            pmvu->PauseUntilClick(fFalse);
            Pscen()->Enable(fscenTboxes);
            Pscen()->Disable(fscenPauses);

            //
            // Update views and scroll bars
            //
            InvalViewsAndScb();

            //
            // Clean up anything else
            //
            Pscen()->StopPlaying();
            vpcex->EnqueueCid(cidMviePlaying, pvNil, pvNil, fFalse);

            //
            // Set sound queue state
            //
            _pmsq->SndOnShort();

            Pmcc()->PlayStopped();
            vpsndm->StopAll();
        }
    }

    return (fTrue);
}

// Runtime stop effects retain their original cleanup and restoration order.
void Movie::_StopRenderPlayback(PMovieView view)
{
    AssertThis(0);
    Pmcc()->EnableAccel();
    _clok.Stop();
    _clok.RemoveCmh(this);
    _playback.framePacer.pending = false;
    _playback.pausing = fFalse;
    _playback.scrolling = fFalse;
    _playback.wait = witNil;
    SetFStopPlaying(fFalse);
    SetFPlaying(fFalse);
    SetUserPaused(false);
    SetFSoundsEnabled(_playback.oldSoundsEnabled);
    view->PauseUntilClick(fFalse);
    if (Pscen() != pvNil)
    {
        Pscen()->Enable(fscenTboxes);
        Pscen()->Disable(fscenPauses);
    }
    else
        Assert(_scenes.index == ivNil, "Bogus scene state");

    //
    // Update views and scroll bars
    //
    InvalViewsAndScb();

    //
    // Clean up anything else
    //
    if (Pscen() != pvNil)
        Pscen()->StopPlaying();
    vpcex->EnqueueCid(cidMviePlaying, pvNil, pvNil, fFalse);

    //
    // Set sound queue state
    //
    _pmsq->SndOnShort();

    Pmcc()->PlayStopped();
    vpsndm->StopAll();

    // if we were fading, then restore sound volume
    if (_playback.fadeOriginalVolume)
    {
        if (_playback.fadeChangedVolume || !CurrentStudioPreferences().disableAudioFades)
            vpsndm->SetVlm(_playback.fadeOriginalVolume);
        _playback.fadeOriginalVolume = 0;
    }
    _playback.fadeVolume = -1;
    _playback.fadeChangedVolume = false;
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
    if (_fUserPaused && !_playback.stopRequested)
    {
        _fUserResumeRender = true;
        return true;
    }
    ulong started = TsCurrent();
    PMovieView view = (PMovieView)PddgGet(0);
    AssertPo(view, 0);
    _playback.suppressAudioFades = CurrentStudioPreferences().disableAudioFades;
    MoviePlaybackEvaluator evaluator(_playback, started);
    for (;;)
    {
        MoviePlaybackCommand command = evaluator.Next();
        if (command.kind == MoviePlaybackCommandKind::Finished)
            return true;
        MoviePlaybackReply reply;
        switch (command.kind)
        {
        case MoviePlaybackCommandKind::Stop:
            _StopRenderPlayback(view);
            break;
        case MoviePlaybackCommandKind::ReadVolume:
            reply.number = vpsndm->VlmCur();
            break;
        case MoviePlaybackCommandKind::SetVolume:
            vpsndm->SetVlm(command.value);
            break;
        case MoviePlaybackCommandKind::ScrollText:
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
        case MoviePlaybackCommandKind::PresentFrame:
            Pscen()->Enable(fscenTboxes);
            Pscen()->Disable(fscenActrs);
            reply.success = Pscen()->FGotoFrm(Pscen()->Nfrm());
            Pscen()->Enable(fscenActrs);
            break;
        case MoviePlaybackCommandKind::DisplayFrame:
            MarkViews();
#if defined(DEBUG) || defined(STUDIO_TEST_TOOLS)
            if (_playbackFrameObserver)
                _playbackFrameObserver(this);
#endif
            _pmcc->UpdateScrollbars();
            if (FSoundsEnabled())
                _pmsq->PlayMsq();
            else
                _pmsq->FlushMsq();
            vpappb->UpdateMarked();
#if defined(DEBUG) || defined(STUDIO_TEST_TOOLS)
            if (_playbackTest.active)
                _RecordPlaybackTestFrame();
#endif
            break;
        case MoviePlaybackCommandKind::QueryScrolling:
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
        case MoviePlaybackCommandKind::SetClickPause:
            view->PauseUntilClick(command.flag);
            break;
        case MoviePlaybackCommandKind::QuerySound:
            reply.value = _pmsq->FPlaying(false);
            break;
        case MoviePlaybackCommandKind::ReadTime:
            reply.timestamp = TsCurrent();
            break;
        case MoviePlaybackCommandKind::SetAlarm:
            if (!_playback.unpaced && command.flag && _playback.framePacer.fps >= 0)
                _playback.framePacer.Schedule(started);
            else
                reply.success = _playback.unpaced || _clok.FSetAlarm(command.value, this, 0, command.flag);
            break;
        case MoviePlaybackCommandKind::ReadPosition:
            Pscen()->Disable(fscenTboxes);
            reply.position = {static_cast<LegacyLong>(Iscen()), static_cast<LegacyLong>(Cscen()),
                              static_cast<LegacyLong>(Pscen()->Nfrm()), static_cast<LegacyLong>(Pscen()->NfrmLast()),
                              Pscen()->Trans()};
            break;
        case MoviePlaybackCommandKind::SwitchScene:
            if (_playback.unpaced)
                std::fprintf(stderr, "BATCH_SCENE_LOAD scene=%ld\n", static_cast<long>(command.value));
            reply.success = FSwitchScen(command.value);
            break;
        case MoviePlaybackCommandKind::ReplayScene:
            AssertPo(Pscen(), 0);
            Pscen()->Disable(fscenTboxes);
            Pscen()->Enable(fscenPauses);
            reply.success = Pscen()->FReplayFrm(fscenPauses | fscenSounds | fscenActrs);
            if (reply.success)
                Pscen()->PlayBkgdSnd();
            break;
        case MoviePlaybackCommandKind::AdvanceFrame:
            if (_playback.unpaced)
                std::fprintf(stderr, "BATCH_FRAME scene=%ld frame=%ld\n", Iscen(), static_cast<long>(command.value));
            reply.success = Pscen()->FGotoFrm(command.value);
            break;
        case MoviePlaybackCommandKind::RenderWorld:
            RenderWorld();
            break;
        case MoviePlaybackCommandKind::Finished:
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
        evaluator.Complete(reply);
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

static SceneDescriptor NativeComposeScene(const MMComposeScene &value)
{
    SceneDescriptor scene = {};
    scene.imvied=value.movie; scene.cno=value.number; scene.chid=value.child;
    scene.pmbmp=static_cast<PMaskedBitmapMBMP>(value.thumbnail);
    scene.trans=TRANS(value.transition); scene.fNuked=value.deleted!=0;
    return scene;
}
static MovieDescriptor NativeComposeMovie(const MMComposeMovie &value)
{
    return {static_cast<PChunkyResourceFile>(value.resources),value.number,value.actor_limit};
}
static MMMovieComposeServices ComposeListServices()
{
    MMMovieComposeServices s = {};
    s.new_list=[](int movies)->void* { return DynamicArray::PglNew(movies?sizeof(MovieDescriptor):sizeof(SceneDescriptor)); };
    s.duplicate_movies=[](void *p)->void* { return static_cast<PDynamicArray>(p)->PglDup(); };
    s.count=[](void *p)->MMI32 { return static_cast<PDynamicArray>(p)->IvMac(); };
    s.scene=[](void *p,MMI32 index,MMComposeScene *out) {
        SceneDescriptor scene; static_cast<PDynamicArray>(p)->Get(index,&scene);
        *out={static_cast<MMI32>(scene.imvied),scene.cno,scene.chid,scene.pmbmp,scene.trans,scene.fNuked};
    };
    s.movie=[](void *p,MMI32 index,MMComposeMovie *out) {
        MovieDescriptor movie; static_cast<PDynamicArray>(p)->Get(index,&movie);
        *out={movie.pcrf,movie.cno,static_cast<MMI32>(movie.aridLim)};
    };
    s.insert_scene=[](void *p,MMI32 index,const MMComposeScene *value)->int {
        auto scene=NativeComposeScene(*value); return static_cast<PDynamicArray>(p)->FInsert(index,&scene);
    };
    s.append_movie=[](void *p,const MMComposeMovie *value,MMI32 *out)->int {
        auto movie=NativeComposeMovie(*value); LegacyLong index;
        bool ok=static_cast<PDynamicArray>(p)->FAdd(&movie,&index); if(ok) *out=index; return ok;
    };
    s.put_movie=[](void *p,MMI32 index,const MMComposeMovie *value) {
        auto movie=NativeComposeMovie(*value); static_cast<PDynamicArray>(p)->Put(index,&movie);
    };
    s.erase=[](void *p,MMI32 index) { static_cast<PDynamicArray>(p)->Delete(index); };
    s.retain=[](void *p) { static_cast<BASE *>(p)->AddRef(); };
    s.release=[](void *p) { static_cast<BASE *>(p)->Release(); };
    return s;
}
MMMovieComposeServices Movie::_ComposeServices()
{
    static_assert(kctgMvie==0x4d564945U && kctgActr==0x41435452U && cnoNil==0xffffffffU, "Movie composition ABI");
    auto s=ComposeListServices(); s.context=this; s.data=&_data; s.scenes=&_scenes;
    s.scene_control=_SceneServices(); s.allocator=CollectionAllocator();
#ifdef BUG1929
    s.temporary_before_save=1;
#endif
    s.use_temporary=[](void *p)->int { return static_cast<PMovie>(p)->_FUseTempFile(); };
    s.autosave=[](void *p)->int { return static_cast<PMovie>(p)->FAutoSave(nullptr,false); };
    s.file=[](void *p)->void* { return static_cast<PChunkyResourceFile>(p)->Pcfl(); };
    s.scene_child=[](void *p,MMU32 movie,MMU32 child,MMU32 *out)->int {
        ChildChunkIdentification kid; bool ok=static_cast<PChunkyFile>(p)->FGetKidChidCtg(kctgMvie,movie,child,kctgScen,&kid);
        if(ok) *out=kid.cki.cno; return ok;
    };
    s.read_transition=[](void *p,MMU32 number,MMI32 *out)->int {
        TRANS value; bool ok=Scene::FTransOnFile(static_cast<PChunkyResourceFile>(p),number,&value);
        if(ok) *out=value; return ok;
    };
    s.transition=[](void *p)->MMI32 { return static_cast<PScene>(p)->Trans(); };
    s.read_thumbnail=[](void *p,MMU32 number)->void* {
        auto file = static_cast<PChunkyFile>(p);
        const auto chunks = file->ReadChunkAccess();
        MMU32 thumbnail;
        if (mm_scene_thumbnail_link(&chunks, number, &thumbnail) != MM_CHUNK_OK)
        {
            Bug("Scene doesn't have a thumbnail");
            return nullptr;
        }
        return MaskedBitmapMBMP::PmbmpReadChunk(file, kctgThumbMbmp, thumbnail);
    };
    s.thumbnail=[](void *p)->void* { return static_cast<PScene>(p)->PmbmpThumbnail(); };
    s.clone_movie=[](void *p,MMU32 number,void *out,MMU32 *copied)->int {
        ChunkNumber value; bool ok=static_cast<PChunkyFile>(p)->FClone(kctgMvie,number,static_cast<PChunkyFile>(out),&value);
        if(ok) *copied=value; return ok;
    };
    s.delete_movie=[](void *p,MMU32 number) { static_cast<PChunkyFile>(p)->Delete(kctgMvie,number); };
    s.adopt_scene=[](void *p,MMU32 movie,MMU32 scene,MMU32 child)->int {
        return static_cast<PChunkyFile>(p)->FAdoptChild(kctgMvie,movie,kctgScen,scene,child);
    };
    s.adopt_sounds=[](void *p,void *file,MMU32 scene)->int { return static_cast<PMovie>(p)->_FAdoptMsndInMvie(static_cast<PChunkyFile>(file),scene); };
    s.read_scene=[](void *p,void *resources,MMU32 scene)->void* {
        return Scene::PscenRead(static_cast<PMovie>(p),static_cast<PChunkyResourceFile>(resources),scene);
    };
    s.start_scene=[](void *p)->int { return static_cast<PScene>(p)->FPlayStartEvents(true); };
    s.add_actors=[](void *p)->int { return static_cast<PScene>(p)->FAddActrsToRollCall(); };
    s.remove_actors=[](void *p) { static_cast<PScene>(p)->RemActrsFromRollCall(true); };
    s.actor_tools=[](void *p) { static_cast<PMovie>(p)->Pmcc()->EnableActorTools(); };
    s.text_tools=[](void *p) { static_cast<PMovie>(p)->Pmcc()->EnableTboxTools(); };
    s.delete_scene_child=[](void *p,MMU32 movie,MMU32 scene,MMU32 child) { static_cast<PChunkyFile>(p)->DeleteChild(kctgMvie,movie,kctgScen,scene,child); };
    s.change_child=[](void *p,MMU32 movie,MMU32 scene,MMU32 before,MMU32 after) { static_cast<PChunkyFile>(p)->ChangeChid(kctgMvie,movie,kctgScen,scene,before,after); };
    s.write_transition=[](void *p,MMU32 number,MMI32 transition)->int { return Scene::FSetTransOnFile(static_cast<PChunkyResourceFile>(p),number,TRANS(transition)); };
    s.set_transition=[](void *p,MMI32 transition) { static_cast<PScene>(p)->SetTransitionCore(TRANS(transition)); };
    s.dirty=[](void *p) { static_cast<PMovie>(p)->SetDirty(); };
    s.scene_nuked=[](void *p) { static_cast<PMovie>(p)->Pmcc()->SceneNuked(); };
    s.invalidate=[](void *p) { static_cast<PMovie>(p)->InvalViewsAndScb(); };
    s.rollcall_changed=[](void *p) { static_cast<PMovie>(p)->Pmcc()->UpdateRollCall(); };
    s.diagnostic=[](void *,int issue) {
        switch(issue) {
        case MM_COMPOSE_MISSING_SCENE: AssertDo(false,"Not enough scene chunks for movie"); break;
        case MM_COMPOSE_BAD_BASE: Assert(false,"Invalid DynamicArray of MVIEDs"); break;
        case MM_COMPOSE_IMPORT_ACTORS: PushErc(ercSocNoImportRollCall); break;
        case MM_COMPOSE_REMOVE_ACTORS: PushErc(ercSocNoNukeRollCall); break;
        }
    };
#ifdef DEBUG
    s.validate_children=[](void *p) {
        auto movie=static_cast<PMovie>(p); auto file=movie->_data.Resources()->Pcfl(); ChildChunkID last=chidNil;
        for(long i=0;i<file->Ckid(kctgMvie,movie->_data.movieNumber);++i) {
            ChildChunkIdentification child;
            if(!file->FGetKid(kctgMvie,movie->_data.movieNumber,i,&child)) { Bug("Can't guarantee validity of Movie's Scene children"); break; }
            if(child.cki.ctg==kctgScen) { Assert(last==chidNil || child.chid>last,"Found duplicate ChildChunkID in scene children"); last=child.chid; }
        }
    };
#endif
    s.read_rollcall=[](void *p,MMU32 number)->void* {
        PRollCallTable table=nullptr; return Movie::FReadRollCall(static_cast<PChunkyResourceFile>(p),number,&table)?table:nullptr;
    };
    s.actor_count=[](void *p)->MMI32 { return static_cast<PRollCallTable>(p)->storage.count; };
    s.actor=[](void *p,MMI32 index,MMRollCallActor *out) { AssertDo(mm_rollcall_table_extra(&static_cast<PRollCallTable>(p)->storage,index,out),"Bad roll-call row"); };
    s.append_actor=[](void *p,void *source,MMI32 index,const MMRollCallActor *row)->int {
        auto from=&static_cast<PRollCallTable>(source)->storage; auto to=&static_cast<PRollCallTable>(p)->storage;
        const void *name; MMU32 characters;
        if(!mm_rollcall_table_name(from,index,&name,&characters)) return 0;
        Assert(from->unit==to->unit && from->encoding==to->encoding,"Roll-call name domain changed");
        return mm_rollcall_table_add(to,name,characters,row);
    };
    s.erase_actor=[](void *p,MMI32 index) { static_cast<PRollCallTable>(p)->Delete(index); };
    s.close_tag=[](MMTag *value) { TAG tag; TagFromCore(&tag,*value); TagManager::CloseTag(&tag); *value=TagToCore(tag); };
    s.adjust_actor=[](void *p,MMU32 number,MMI32 offset)->int { return Actor::FAdjustAridOnFile(static_cast<PChunkyFile>(p),number,offset); };
    s.graph.context=_data.Resources()?_data.Resources()->Pcfl():nullptr;
    s.graph.children=[](void *p,MMU32 type,MMU32 number)->MMU32 { return static_cast<PChunkyFile>(p)->Ckid(type,number); };
    s.graph.child=[](void *p,MMU32 type,MMU32 number,MMU32 i,MMChunkChild *out)->int {
        ChildChunkIdentification child; if(!static_cast<PChunkyFile>(p)->FGetKid(type,number,i,&child)) return 0;
        *out={child.cki.ctg,child.cki.cno,child.chid}; return 1;
    };
    return s;
}


#ifdef DEBUG
/******************************************************************************
    MarkMem
        Marks memory used by the CompositeMovie

************************************************************ PETED ***********/
void CompositeMovie::MarkMem(void)
{
    long iv, ivMac;
    MovieDescriptor mvied;
    SceneDescriptor scend;

    ivMac = pglmvied->IvMac();
    for (iv = 0; iv < ivMac; iv++)
    {
        pglmvied->Get(iv, &mvied);
        MarkMemObj(mvied.pcrf);
    }
    MarkMemObj(pglmvied);

    ivMac = pglscend->IvMac();
    for (iv = 0; iv < ivMac; iv++)
    {
        pglscend->Get(iv, &scend);
        MarkMemObj(scend.pmbmp);
    }
    MarkMemObj(pglscend);
}
#endif // DEBUG

/******************************************************************************
    FAddToCmvi
        Generates a DynamicArray of SCENDs that describes this movie.  A movie client
        that wishes to makes wholesale changes to a movie may get this DynamicArray,
        rearrange it, including inserting references to new movie files, and
        pass it back to the movie via FSetCmvi to modify the movie.
        Adds the movie to the DynamicArray of movie descriptors.

    Arguments:
        PCompositeMovie pcmvi     --  the CompositeMovie to add the movie to
        long iscendIns  --  the point at which to start inserting scenes

    Returns: fTrue if it was successful, fFalse otherwise

************************************************************ PETED ***********/
bool Movie::FAddToCmvi(PCompositeMovie pcmvi, long *piscendIns)
{
    AssertThis(0); AssertVarMem(pcmvi); AssertVarMem(piscendIns);
    MMCompositeMovie state={pcmvi->pglscend,pcmvi->pglmvied};
    MMI32 insertion=*piscendIns; const auto services=_ComposeServices();
    bool ok=mm_movie_compose_add(&state,&insertion,&services)!=0;
    pcmvi->pglscend=static_cast<PDynamicArray>(state.scenes); pcmvi->pglmvied=static_cast<PDynamicArray>(state.movies);
    *piscendIns=insertion; return ok;
}

/******************************************************************************
    FSetCmvi
        Rebuilds the movie based on the given CompositeMovie.  Any scenes
        marked for deletion are disowned by their Movie chunk.  Any scenes that
        refer to a movie file other than this Movie's auto save file are
        copied into this Movie's auto save file.  Scene chunks are given new
        CHIDs reflecting their new position within the movie.  The non-nuked
        scenes must appear in the DynamicArray in the order that they appear in the
        movie; other than that, there is no restriction on the order of the
        scenes (ie, nuked scenes can appear anywhere in the DynamicArray, even though
        currently the only client of this API keeps the nuked scenes at the
        end).

    Arguments:
        PCompositeMovie pcmvi -- the CompositeMovie that describes the new movie structure

    Returns: fTrue if it could accomplish all of the above, fFalse otherwise

************************************************************ PETED ***********/
bool Movie::FSetCmvi(PCompositeMovie pcmvi)
{
    AssertThis(0); AssertVarMem(pcmvi); AssertPo(pcmvi->pglmvied,0); AssertPo(pcmvi->pglscend,0);
    const MMCompositeMovie state={pcmvi->pglscend,pcmvi->pglmvied}; const auto services=_ComposeServices();
    return mm_movie_compose_apply(&state,&services)!=0;
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
        Frees up the memory used by the CompositeMovie.  For each scene in the
        DynamicArray of SCENDs, releases memory that the SceneDescriptor referred to.  Likewise
        for each MovieDescriptor in the DynamicArray of MVIEDs.

    Arguments:
        PCompositeMovie pcmvi -- the CompositeMovie to empty

    Returns: Sets the client's pointer to pvNil when finished

************************************************************ PETED ***********/
void CompositeMovie::Empty(void)
{
    AssertNilOrPo(pglscend,0); AssertNilOrPo(pglmvied,0);
    MMCompositeMovie state={pglscend,pglmvied}; const auto services=ComposeListServices();
    mm_composite_empty(&state,&services);
    pglscend=static_cast<PDynamicArray>(state.scenes); pglmvied=static_cast<PDynamicArray>(state.movies);
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

    _pmcc->UpdateScrollbars();
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
    _pmcc->ActorSelected(arid);
    _pmcc->UpdateAction();
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
    AssertThis(0); AssertNilOrVarMem(pfHaveValid); const auto services=_SoundServices(); int valid=0;
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
    ReleasePpo(&_paundGroup);
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
    BRS rgr[3][3] = {{rOne, rZero, rZero}, {rZero, rZero, rOne}, {rZero, -rOne, rZero}};

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

    CopyPb(rgr, pmvu->_rgrAxis, size(rgr));
    pmvu->_fRecordDefault = fTrue;
    pmvu->_fRespectGround = fFalse;
    pmvu->_dxp = dxp;
    pmvu->_dyp = dyp;
    pmvu->_tool = toolCompose;
    pmvu->_tagTool.sid = ksidInvalid;
    pmvu->_fSelToggleArmed = fFalse;
    pmvu->_pactrSelToggle = pvNil;
    pmvu->_paundGroup = pvNil;
    pmvu->_xrPivot = rZero;
    pmvu->_yrPivot = rZero;
    pmvu->_zrPivot = rZero;

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

            if ((pactr != pvNil) && pactr->FTimeFrozen())
            {
                pactr->SetTimeFreeze(fFalse);
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
 * Parameters:
 *	dxrMouse - BRender scalar representation of mouse X coordinate
 *	dyrMouse - BRender scalar representation of mouse Y coordinate
 *	dzrMouse - BRender scalar representation of mouse Z coordinate
 *  pdxrWld  - Place to store world X coordinate.
 *  pdyrWld  - Place to store world Y coordinate.
 *  pdzrWld  - Place to store world Z coordinate.
 *  fRecord  - Is the conversion to be scaled according to the recording
 *				scaling factor, or the non-recording scaling factor.
 *
 * Returns:
 *  None.
 *
 **************************************************************************/
void MovieView::MouseToWorld(BRS dxrMouse, BRS dyrMouse, BRS dzrMouse, BRS *pdxrWld, BRS *pdyrWld, BRS *pdzrWld, bool fRecord)
{
    AssertThis(0);
    AssertVarMem(pdxrWld);
    AssertVarMem(pdyrWld);
    AssertVarMem(pdzrWld);

    BRS dxrScr, dyrScr, dzrScr;
    BMAT34 bmat34Cam;
    BRS rScaleMouse;

    dxrScr = BR_MAC3(dxrMouse, _rgrAxis[0][0], dyrMouse, _rgrAxis[0][1], dzrMouse, _rgrAxis[0][2]);
    dyrScr = BR_MAC3(dxrMouse, _rgrAxis[1][0], dyrMouse, _rgrAxis[1][1], dzrMouse, _rgrAxis[1][2]);
    dzrScr = BR_MAC3(dxrMouse, _rgrAxis[2][0], dyrMouse, _rgrAxis[2][1], dzrMouse, _rgrAxis[2][2]);

    rScaleMouse = fRecord ? krScaleMouseRecord : krScaleMouseNonRecord;

    //
    // apply some scaling so that 1 pixel of mouse movement is rScaleMouse world units
    //
    dxrScr = BrsMul(dxrScr, rScaleMouse);
    dyrScr = BrsMul(dyrScr, rScaleMouse);
    dzrScr = BrsMul(dzrScr, rScaleMouse);

    Pmvie()->Pscen()->Pbkgd()->GetMouseMatrix(&bmat34Cam);
    *pdxrWld = BR_MAC3(dxrScr, bmat34Cam.m[0][0], dyrScr, bmat34Cam.m[1][0], dzrScr, bmat34Cam.m[2][0]);
    *pdyrWld = BR_MAC3(dxrScr, bmat34Cam.m[0][1], dyrScr, bmat34Cam.m[1][1], dzrScr, bmat34Cam.m[2][1]);
    *pdzrWld = BR_MAC3(dxrScr, bmat34Cam.m[0][2], dyrScr, bmat34Cam.m[1][2], dzrScr, bmat34Cam.m[2][2]);
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

    _fEntireScene = fEntireScene;
    SetTool(toolPlace);
    Pmvie()->RemFromRollCall(pactr, fFalse);
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
    AssertDo(Pmvie()->FAddToRollCall(Pmvie()->Pscen()->PactrSelected(), pvNil), "Should never fail");
    Pmvie()->Pmcc()->ChangeTool(toolCompose);
    vpcex->EndMouseTracking();

    _fMouseDownSeen = fFalse;

    return;
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
 *  dxrWld, dyrWld, dzrWld - the change in position in worldspace
 *
 * Returns:
 *  None.
 *
 **************************************************************************/
void MovieView::_PositionActr(BRS dxrWld, BRS dyrWld, BRS dzrWld)
{
    AssertThis(0);
    Assert(Tool() == toolPlace, "Wrong tool in effect");

    PMovie pmvie;
    PScene pscen;
    bool fMoved;
    PActor pactr = pvNil;
    ulong grfmaf = fmafOrient;

    pmvie = Pmvie();
    AssertPo(pmvie, 0);

    pscen = pmvie->Pscen();
    AssertPo(pscen, 0);

    pactr = pscen->PactrSelected();
    AssertPo(pactr, 0);

    if (_fEntireScene)
    {
        grfmaf |= fmafEntireScene;
    }
    else
    {
        grfmaf |= fmafEntireSubrte;
    }

    if (FRespectGround())
    {
        grfmaf |= fmafGround;
    }

    // FMoveRouteCore cannot fail on fmafEntireSubrte as no events are added
    if (pactr->FMoveRoute(dxrWld, dyrWld, dzrWld, &fMoved, grfmaf) && fMoved)
    {
        Pmvie()->Pbwld()->MarkDirty();
        Pmvie()->MarkViews();
        pscen->Pbkgd()->ReuseActorPlacePoint();
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
    PActor pactrDup;
    PTBOX ptbox;
    PActorUndo paund;
    PT pt;
    long ibset;
    PDocumentBase pdocb;

    SlowKeyboardRepeat();

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
        //
        // Select the actor under the cursor.
        //
        pactr = Pmvie()->Pscen()->PactrSelected();
        AssertNilOrPo(pactr, 0);

        if ((pactr != pvNil) && pactr->FTimeFrozen())
        {
            pactr->SetTimeFreeze(fFalse);
        }

        pactrDup = Pmvie()->Pscen()->PactrFromPt(pcmd->xp, pcmd->yp, &ibset);

        // Shift-click on an actor with a selection-friendly tool: arm a toggle for mouseup.
        // Tools that support multi-actor drags participate (move, rotate, resize, squash),
        // plus the default and explicit-select tools. Other tools collapse to single-select
        // via the existing path below.
        bool fShiftSelectTool = (Tool() == toolCompose) || (Tool() == toolDefault) ||
                                (Tool() == toolActorSelect) || (Tool() == toolRotateX) ||
                                (Tool() == toolRotateY) || (Tool() == toolRotateZ) ||
                                (Tool() == toolResize) || (Tool() == toolSquashStretch);
        if (fShiftSelectTool && (pcmd->grfcust & fcustShift) && pactrDup != pvNil)
        {
            _fSelToggleArmed = fTrue;
            _pactrSelToggle = pactrDup;
            _xpSelToggleDown = pcmd->xp;
            _ypSelToggleDown = pcmd->yp;
            Pmvie()->Pbwld()->MarkDirty();
            // Do NOT call SelectActr here. Toggle fires at mouseup if we didn't drag.
            // Skip the rest of the mousedown selection flow; existing behavior resumes
            // at the switch on Tool() below if the user goes on to drag.
        }
        else if (fShiftSelectTool && pactrDup != pvNil &&
                 Pmvie()->Pscen()->CactrSelected() >= 2 && Pmvie()->Pscen()->FIsActrSelected(pactrDup))
        {
            // Plain click (no shift) on an actor that is already part of an existing
            // multi-selection: do NOT collapse the selection via SelectActr, so a
            // click-drag moves the whole group. Without this, SelectActr would clear
            // the extras list and the drag would only translate the clicked actor.
            Pmvie()->Pbwld()->MarkDirty();
        }
        else
        {
            //
            // Use previously selected actor if mouse in the actor.
            // Don't change the selected actor if we're using the default tool.
            //
            if (((pactr == pvNil) || !pactr->FIsInView() || !pactr->FPtIn(pcmd->xp, pcmd->yp, &ibset)) &&
                Tool() != toolDefault)
            {
                pactr = pactrDup;
                AssertNilOrPo(pactr, 0);
            }

            if (pvNil != pactr)
            {
                _ActorClicked(pactr, fTrue);
                // Plain click on a tagged actor recalls its whole group;
                // for actors with no tag this collapses to SelectActr, the
                // pre-tag-feature behaviour. Shift-click bypasses this --
                // shift-toggle is single-actor by design.
                Pmvie()->Pscen()->FSelectActrsSharingTagsWith(pactr);
            }
            else
            {
                Pmvie()->Pscen()->SelectActr(pvNil);
            }
            Pmvie()->Pbwld()->MarkDirty();
        }
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
    case toolCopyObject:
    case toolCopyRte:
        if (!_fTextMode)
        {
            FDoClip(Tool());
        }
        break;

    case toolPasteObject:
        if (!_fTextMode)
        {
            if (vpclip->FGetFormat(kclsActorClipboard, &pdocb) && ((PActorClipboard)pdocb)->FRouteOnly() && (pactr != pvNil))
            {
                FDoClip(Tool());
                ReleasePpo(&pdocb);
            }
        }
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
            pactr->SetTimeFreeze(fTrue);
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

            // For multi-selection drags (move / rotate / resize / squash):
            // snapshot every selected actor and bundle into an
            // ActorMoveGroupUndo. Single-select retains the _paund flow.
            // Group rotate / scale also freeze a pivot here.
            bool fGroupTool = (Tool() == toolCompose) || (Tool() == toolRotateX) ||
                              (Tool() == toolRotateY) || (Tool() == toolRotateZ) ||
                              (Tool() == toolResize) || (Tool() == toolSquashStretch);
            long cactrSel = (fGroupTool && Pmvie()->Pscen() != pvNil)
                                ? Pmvie()->Pscen()->CactrSelected()
                                : 1;

            if (cactrSel >= 2 && Tool() != toolCompose)
            {
                // Freeze the rotate / scale pivot at the selection centroid.
                // If no selected actor is currently visible the centroid is
                // undefined; default the pivot to origin -- every per-tick
                // FMoveRoute will no-op on the offstage actors anyway, so the
                // drag is harmlessly inert and the group undo entry is empty.
                if (!Pmvie()->Pscen()->FXyzSelectionCentroid(&_xrPivot, &_yrPivot, &_zrPivot))
                {
                    _xrPivot = _yrPivot = _zrPivot = rZero;
                }
            }

            if (cactrSel >= 2)
            {
                PActorMoveGroupUndo pamgu = ActorMoveGroupUndo::PamguNew();
                bool fOk = (pamgu != pvNil);
                for (long iactr = 0; fOk && iactr < cactrSel; iactr++)
                {
                    PActor pactrSel = Pmvie()->Pscen()->PactrSelectedAt(iactr);
                    PActorUndo paundChild = ActorUndo::PaundNew();
                    PActor pactrSnap = pvNil;
                    if (paundChild == pvNil || !pactrSel->FDup(&pactrSnap, fTrue))
                    {
                        ReleasePpo(&paundChild);
                        ReleasePpo(&pactrSnap);
                        fOk = fFalse;
                        break;
                    }
                    paundChild->SetPactr(pactrSnap);
                    ReleasePpo(&pactrSnap);
                    paundChild->SetArid(pactrSel->Arid());
                    if (!pamgu->FAddChild(paundChild))
                    {
                        ReleasePpo(&paundChild);
                        fOk = fFalse;
                        break;
                    }
                    ReleasePpo(&paundChild); // pamgu holds its own ref via FAddChild
                }

                if (!fOk)
                {
                    ReleasePpo(&pamgu);
                    Pmvie()->ClearUndo();
                    PushErc(ercSocNotUndoable);
                }
                else
                {
                    Assert(_paundGroup == pvNil, "leaking previous group undo");
                    _paundGroup = pamgu;
                }
            }
            else
            {
                //
                // Create an actor undo object (single-actor flow, unchanged).
                //
                paund = ActorUndo::PaundNew();
                if ((paund == pvNil) || !pactr->FDup(&pactrDup, fTrue))
                {
                    Pmvie()->ClearUndo();
                    PushErc(ercSocNotUndoable);
                }
                else
                {
                    paund->SetPactr(pactrDup);
                    ReleasePpo(&pactrDup);
                    paund->SetArid(pactr->Arid());

                    //
                    // Store it.  We will only add it if there is a change done
                    // to the actor.
                    //
                    _paund = paund;
                }
            }

            if ((Tool() != toolResize) && (Tool() != toolSquashStretch))
            {
                Pmvie()->Pmcc()->PlayUISound(Tool(), _grfcust);
            }
            else
            {
                _lwLastTime = 0;
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
            long anidTool = pactr->AnidCur();
            long anid = anidTool;
            long celn = 0;
            bool fFrozen;

            SetAnidTool(pactr->AnidCur());
            _ptmplTool = pactr->Ptmpl();

            if ((pcmd->grfcust & fcustShift) && (pcmd->grfcust & fcustCmd) && FRecordDefault())
            {
                fFrozen = fFalse;
                _fCyclingCels = fFalse;
                _fSetFRecordDefault = fTrue;
                SetFRecordDefault(fFalse);
            }
            else
            {
                fFrozen = FPure(pcmd->grfcust & fcustShift);
                _fCyclingCels = FPure(pcmd->grfcust & fcustCmd);
            }

            if ((pactr->Ptmpl() != _ptmplTool) && !(_ptmplTool->FIsTdt() && pactr->Ptmpl()->FIsTdt()))
            {
                PushErc(ercSocActionNotApplicable);
                return;
            }

            vpappb->HideCurs();

            // first, call FSetAction
            if (anidTool == ivNil)
            {
                anid = pactr->AnidCur();
            }
            if (pactr->AnidCur() == anid)
            {
                celn = pactr->CelnCur();
            }

            // note that FSetAction creates an undo object
            // NOTE:  FSetAction() must be called on each use
            // of toolRecordSameAction. (It is not redundant).
            // Otherwise, resizing can break wysiwyg, as the final
            // path point probably won't be a complete cel's distance
            // from the previous step.
            Assert(pvNil == _pactrRestore, "_pactrRestore should not require releasing");
            ReleasePpo(&_pactrRestore); // To be safe
            if (!pactr->FSetAction(anid, celn, fFrozen, &_pactrRestore))
            {
                break; // an Oom erc has already been pushed
            }
            SetAnidTool(ivNil);
            _tsLast = TsCurrent();
            pactr->SetTsInsert(_tsLast);
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

/***************************************************************************
 *
 * Commits whichever pending move-undo we have (group or single) to the
 * movie's undo stack. Releases the field afterwards. Nil-safe.
 *
 **************************************************************************/
void MovieView::_CommitMoveUndo(void)
{
    AssertThis(0);

    if (_paundGroup != pvNil)
    {
        if (!Pmvie()->FAddUndo(_paundGroup))
        {
            PushErc(ercSocNotUndoable);
            Pmvie()->ClearUndo();
        }
        ReleasePpo(&_paundGroup);
    }
    else if (_paund != pvNil)
    {
        if (!Pmvie()->FAddUndo(_paund))
        {
            PushErc(ercSocNotUndoable);
            Pmvie()->ClearUndo();
        }
        ReleasePpo(&_paund);
    }
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
    BRS dxrMouse, dyrMouse, dzrMouse;
    BRS dxrWld, dyrWld, dzrWld; // amount moved from previous point in world space
    BRS zrActr, zrCam, dzrActr;
    bool fArrowKey = fFalse;
    RC rc;
    PT pt;

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

    //
    // Shift-click selection toggle, retroactive arm:
    //   - If the user shift-clicked at mousedown and the mouse stays within
    //     a 3 px tolerance, leave the toggle armed -- _MouseUp will fire it.
    //   - Once the mouse moves past tolerance the click "becomes a drag".
    //     For parity with original 3DMM (where shift+mousedown on a
    //     non-selected actor would call SelectActr on it), retroactively
    //     collapse the selection to the clicked actor and allocate the
    //     drag-undo snapshot we skipped in _MouseDown. If the clicked actor
    //     is already part of the current selection, just disarm so the
    //     existing single-/multi-actor drag flow continues unchanged.
    //
    if (_fSelToggleArmed)
    {
        const long kdxpSelToggleTol = 3;
        const long kdypSelToggleTol = 3;
        long dxpSel = LwAbs(pcmd->xp - _xpSelToggleDown);
        long dypSel = LwAbs(pcmd->yp - _ypSelToggleDown);

        if (dxpSel <= kdxpSelToggleTol && dypSel <= kdypSelToggleTol)
        {
            // Still within tolerance. Suppress drag application this tick so
            // we don't translate the previous selection while the user might
            // still be aiming at a click.
            return;
        }

        if (_pactrSelToggle != pvNil && !pscen->FIsActrSelected(_pactrSelToggle))
        {
            // Clicked actor is not part of the current selection. Drop any
            // undo state allocated for the previous selection in _MouseDown
            // and rebuild it for the now-clicked actor (matches what the
            // pre-MVP non-shift-arm mousedown path would have done).
            ReleasePpo(&_paund);
            ReleasePpo(&_paundGroup);

            pscen->SelectActr(_pactrSelToggle);

            if (Tool() == toolCompose)
            {
                PActorUndo paundDrag = ActorUndo::PaundNew();
                PActor pactrDup = pvNil;
                if (paundDrag != pvNil && _pactrSelToggle->FDup(&pactrDup, fTrue))
                {
                    paundDrag->SetPactr(pactrDup);
                    ReleasePpo(&pactrDup);
                    paundDrag->SetArid(_pactrSelToggle->Arid());
                    _paund = paundDrag;
                }
                else
                {
                    ReleasePpo(&paundDrag);
                    ReleasePpo(&pactrDup);
                    Pmvie()->ClearUndo();
                    PushErc(ercSocNotUndoable);
                }
            }
        }

        _fSelToggleArmed = fFalse;
        _pactrSelToggle = pvNil;
    }

    pactr = pscen->PactrSelected();

    AssertNilOrPo(pactr, 0);

    if (pactr == pvNil)
    {
        return;
    }

    dxrMouse = BrsSub(BrIntToScalar(pcmd->xp), BrIntToScalar(_xpPrev));
    dyrMouse = BrsSub(BrIntToScalar(_ypPrev), BrIntToScalar(pcmd->yp));
    dzrMouse = _dzrPrev;
#ifdef WIN
    //
    // Get the "mouse Z" by sampling the arrow keys.  If an arrow key
    // is down, the number of pixels moved is the number of seconds
    // since the keyboard was last sampled times kdwrMousePerSecond.
    //
    ulong dts = LwMax(1, TsCurrent() - _tsLastSample);
    BRS drSec = BrsDiv(BrIntToScalar(dts), BR_SCALAR(kdtsSecond));
    if (GetKeyState(VK_UP) < 0)
    {
        fArrowKey = fTrue;
        dzrMouse += BrsMul(drSec, kdwrMousePerSecond);
    }
    if (GetKeyState(VK_DOWN) < 0)
    {
        fArrowKey = fTrue;
        dzrMouse -= BrsMul(drSec, kdwrMousePerSecond);
    }
    if (fArrowKey && Tool() == toolRecordSameAction && !_fCyclingCels && pactr->FIsModeRecord())
    {
        if ((BrsAbs(dxrMouse) * 4 < BrsAbs(dzrMouse)) && (BrsAbs(dyrMouse) * 4 < BrsAbs(dzrMouse)))
        {
            // When the up/down arrow keys are used, infinitesimal
            // mouse movement should not be observed or determine path direction.
            dxrMouse = rZero;
            dyrMouse = rZero;
        }
    }
    _dzrPrev = dzrMouse;
    _tsLastSample = TsCurrent();
#endif
#ifdef MAC
    RawRtn();
#endif

    MouseToWorld(dxrMouse, dyrMouse, dzrMouse, &dxrWld, &dyrWld, &dzrWld, Tool() == toolRecordSameAction);

    //
    // Scale movement based on selected actor
    //
    pactr->GetXyzWorld(pvNil, pvNil, &zrActr);
    pscen->Pbkgd()->GetCameraPos(pvNil, pvNil, &zrCam);
    dzrActr = BrsAbs(BrsSub(zrActr, zrCam));
    dzrActr = BrsDiv(dzrActr, kzrMouseScalingFactor);
    if (dzrActr < rOne)
    {
        dzrActr = rOne;
    }
    dxrWld = BrsMul(dxrWld, BrsMul(dzrActr, BR_SCALAR(1.1)));
    dyrWld = BrsMul(dyrWld, BrsMul(dzrActr, BR_SCALAR(1.1)));
    dzrWld = BrsMul(dzrWld, BrsMul(dzrActr, BR_SCALAR(1.1)));

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
        _PositionActr(dxrWld, dyrWld, dzrWld);
        AdjustCursor(pcmd->xp, pcmd->yp);
        break;

    case toolCompose: {
        ulong grfmaf = fmafNil;
        bool fAnyMoved = fFalse;
        long cactrSel = (Pmvie()->Pscen() != pvNil) ? Pmvie()->Pscen()->CactrSelected() : 0;

        if (_fRespectGround)
        {
            grfmaf |= fmafGround;
        }

        if (_grfcust & fcustCmd)
        {
            AdjustCursor(pcmd->xp, pcmd->yp);

            for (long iactr = 0; iactr < cactrSel; iactr++)
            {
                PActor pa = Pmvie()->Pscen()->PactrSelectedAt(iactr);
                if (pa == pvNil)
                {
                    continue;
                }
                if (pa->FTweakRoute(dxrWld, dyrWld, dzrWld, grfmaf))
                {
                    fAnyMoved = fTrue;
                }
            }

            // NOTE: Pre-Task-4 the Tweak (Cmd-drag) branch declared a local
            // bool fMoved{} that was never assigned, so its `if (fMoved)`
            // arm was dead code -- Tweak drags silently leaked their undo
            // snapshot at mouseup. Driving the commit off FTweakRoute's
            // return value here also makes single-actor Tweak drags
            // undoable for the first time. This is intentional; the Task 4
            // spec said "no single-select behavior change" but the prior
            // behavior was a latent bug that nobody noticed.
            if (fAnyMoved)
            {
                _CommitMoveUndo();
            }

            Pmvie()->MarkViews();
        }
        else
        {
            if (_grfcust & fcustShift)
            {
                grfmaf |= fmafEntireSubrte;
            }

            for (long iactr = 0; iactr < cactrSel; iactr++)
            {
                PActor pa = Pmvie()->Pscen()->PactrSelectedAt(iactr);
                if (pa == pvNil)
                {
                    continue;
                }
                bool fMovedThis = fFalse;
                if (pa->FMoveRoute(dxrWld, dyrWld, dzrWld, &fMovedThis, grfmaf) && fMovedThis)
                {
                    fAnyMoved = fTrue;
                }
            }

            if (fAnyMoved)
            {
                _CommitMoveUndo();

                AdjustCursor(pcmd->xp, pcmd->yp);
                Pmvie()->Pbwld()->MarkDirty();
                Pmvie()->MarkViews();
            }
        }
    }
    break;

    case toolRotateX:
    case toolRotateY:
    case toolRotateZ: {
        BRS brs;
        BRA xa, ya, za;

        brs = BrsMul(dxrMouse + dyrMouse, -krRotateScaleFactor);

        xa = aZero;
        ya = aZero;
        za = aZero;

        switch (Tool())
        {
        case toolRotateX:
            xa = BrScalarToAngle(brs);
            break;
        case toolRotateY:
            ya = -BrScalarToAngle(brs);
            break;
        case toolRotateZ:
            za = BrScalarToAngle(brs);
            break;
        }

        long cactrSel = (Pmvie()->Pscen() != pvNil) ? Pmvie()->Pscen()->CactrSelected() : 0;

        if (cactrSel >= 2)
        {
            // Multi-actor rigid group rotation: each actor's world position
            // orbits the frozen pivot AND each actor's body rotates around the
            // SAME world axis (via FRotateAroundWorld, which conjugates through
            // the actor's rest orientation -- otherwise actors facing different
            // directions tilt around different world axes and the group looks
            // chaotic). Only one of xa/ya/za is non-zero per tick.
            // Orbit world-axis matches the body world-axis used in
            // FRotateAroundWorld: tool X uses world Z, tool Z uses world X
            // (so the canonical face-camera actor's right/forward axes line
            // up with the user's tool labels).
            BMAT34 bmat;
            if (xa != aZero)
                BrMatrix34RotateZ(&bmat, xa);
            else if (ya != aZero)
                BrMatrix34RotateY(&bmat, ya);
            else if (za != aZero)
                BrMatrix34RotateX(&bmat, za);
            else
                BrMatrix34Identity(&bmat);

            bool fFwd = FPure(_grfcust & fcustCmd);
            bool fAnyMoved = fFalse;
            for (long iactr = 0; iactr < cactrSel; iactr++)
            {
                PActor pa = Pmvie()->Pscen()->PactrSelectedAt(iactr);
                if (pa == pvNil || pa->Pbody() == pvNil)
                {
                    continue;
                }
                BRS xr, yr, zr;
                pa->Pbody()->GetPosition(&xr, &yr, &zr);
                br_vector3 vIn, vOut;
                vIn.v[0] = BrsSub(xr, _xrPivot);
                vIn.v[1] = BrsSub(yr, _yrPivot);
                vIn.v[2] = BrsSub(zr, _zrPivot);
                BrMatrix34ApplyV(&vOut, &vIn, &bmat);
                BRS dxr = BrsSub(vOut.v[0], vIn.v[0]);
                BRS dyr = BrsSub(vOut.v[1], vIn.v[1]);
                BRS dzr = BrsSub(vOut.v[2], vIn.v[2]);
                bool fMovedThis = fFalse;
                bool fMovedRoute = pa->FMoveRoute(dxr, dyr, dzr, &fMovedThis, fmafNil);
                bool fRotated = pa->FRotateAroundWorld(xa, ya, za, fFwd);
                if (fMovedRoute || fRotated)
                {
                    fAnyMoved = fTrue;
                }
            }

            if (fAnyMoved)
            {
                _CommitMoveUndo();
                Pmvie()->SetDirty();
                Pmvie()->InvalViews();
            }
        }
        else if (pmvie->FRotateActr(xa, ya, za, FPure(_grfcust & fcustCmd)))
        {
            if ((_paund != pvNil) && !Pmvie()->FAddUndo(_paund))
            {
                PushErc(ercSocNotUndoable);
                Pmvie()->ClearUndo();
            }

            ReleasePpo(&_paund);
        }

        Pmvie()->Pbwld()->MarkDirty();
        Pmvie()->MarkViews();
        AdjustCursor(pcmd->xp, pcmd->yp);
    }
    break;

    case toolResize: {
        BRS brs;
        BRS brs2;

        brs = BrsMul(dxrMouse + dyrMouse, krRotateScaleFactor);
        brs2 = BrsAdd(brs, rOne);

        //
        // Play UI sound
        //
        if ((((dxrMouse + dyrMouse) < 0) && !(_lwLastTime < 0)) || (((dxrMouse + dyrMouse) > 0) && !(_lwLastTime > 0)))
        {
            _lwLastTime = dxrMouse + dyrMouse;
            Pmvie()->Pmcc()->StopUISound();
            Pmvie()->Pmcc()->PlayUISound(Tool(), (dxrMouse + dyrMouse > 0) ? 0 : fcustShift);
        }

        long cactrSel = (Pmvie()->Pscen() != pvNil) ? Pmvie()->Pscen()->CactrSelected() : 0;

        if (cactrSel >= 2)
        {
            BRS sm1 = BrsSub(brs2, rOne); // (s - 1)
            bool fAnyMoved = fFalse;
            for (long iactr = 0; iactr < cactrSel; iactr++)
            {
                PActor pa = Pmvie()->Pscen()->PactrSelectedAt(iactr);
                if (pa == pvNil || pa->Pbody() == pvNil)
                {
                    continue;
                }
                BRS xr, yr, zr;
                pa->Pbody()->GetPosition(&xr, &yr, &zr);
                BRS dxr = BrsMul(sm1, BrsSub(xr, _xrPivot));
                BRS dyr = BrsMul(sm1, BrsSub(yr, _yrPivot));
                BRS dzr = BrsMul(sm1, BrsSub(zr, _zrPivot));
                bool fMovedThis = fFalse;
                pa->FMoveRoute(dxr, dyr, dzr, &fMovedThis, fmafNil);
                if (pa->FScale(brs2))
                {
                    fAnyMoved = fTrue;
                }
            }

            if (fAnyMoved)
            {
                _CommitMoveUndo();
                Pmvie()->SetDirty();
                Pmvie()->InvalViewsAndScb();
            }
        }
        else if (pmvie->FScaleActr(brs2))
        {
            if ((_paund != pvNil) && !Pmvie()->FAddUndo(_paund))
            {
                PushErc(ercSocNotUndoable);
                Pmvie()->ClearUndo();
            }

            ReleasePpo(&_paund);
        }

        Pmvie()->Pbwld()->MarkDirty();
        Pmvie()->MarkViews();
        AdjustCursor(pcmd->xp, pcmd->yp);
    }
    break;

    case toolSquashStretch: {
        BRS brs;
        BRS brs2;

        brs = BrsMul(-dxrMouse - dyrMouse, krRotateScaleFactor);
        brs2 = BrsAdd(brs, rOne);

        //
        // Play UI sound
        //
        if ((((-dxrMouse - dyrMouse) < 0) && !(_lwLastTime < 0)) ||
            (((-dxrMouse - dyrMouse) > 0) && !(_lwLastTime > 0)))
        {
            _lwLastTime = -dxrMouse - dyrMouse;
            Pmvie()->Pmcc()->StopUISound();
            Pmvie()->Pmcc()->PlayUISound(Tool(), (-dxrMouse - dyrMouse < 0) ? 0 : fcustShift);
        }

        long cactrSel = (Pmvie()->Pscen() != pvNil) ? Pmvie()->Pscen()->CactrSelected() : 0;

        if (cactrSel >= 2)
        {
            // Mirror Movie::FSquashStretchActr's per-axis factors:
            // sx = brs2, sy = 1/brs2, sz = brs2.
            BRS sx = brs2;
            BRS sy = BrsDiv(rOne, brs2);
            BRS sz = brs2;
            BRS sxm1 = BrsSub(sx, rOne);
            BRS sym1 = BrsSub(sy, rOne);
            BRS szm1 = BrsSub(sz, rOne);
            bool fAnyMoved = fFalse;
            for (long iactr = 0; iactr < cactrSel; iactr++)
            {
                PActor pa = Pmvie()->Pscen()->PactrSelectedAt(iactr);
                if (pa == pvNil || pa->Pbody() == pvNil)
                {
                    continue;
                }
                BRS xr, yr, zr;
                pa->Pbody()->GetPosition(&xr, &yr, &zr);
                BRS dxr = BrsMul(sxm1, BrsSub(xr, _xrPivot));
                BRS dyr = BrsMul(sym1, BrsSub(yr, _yrPivot));
                BRS dzr = BrsMul(szm1, BrsSub(zr, _zrPivot));
                bool fMovedThis = fFalse;
                pa->FMoveRoute(dxr, dyr, dzr, &fMovedThis, fmafNil);
                if (pa->FPull(sx, sy, sz))
                {
                    fAnyMoved = fTrue;
                }
            }

            if (fAnyMoved)
            {
                _CommitMoveUndo();
                Pmvie()->SetDirty();
                Pmvie()->InvalViews();
            }
        }
        else if (pmvie->FSquashStretchActr(brs2))
        {
            if ((_paund != pvNil) && !Pmvie()->FAddUndo(_paund))
            {
                PushErc(ercSocNotUndoable);
                Pmvie()->ClearUndo();
            }

            ReleasePpo(&_paund);
        }

        Pmvie()->Pbwld()->MarkDirty();
        Pmvie()->MarkViews();
        AdjustCursor(pcmd->xp, pcmd->yp);
    }
    break;

    case toolSoonerLater:
    case toolNormalizeRot:
    case toolNormalizeSize:
    case toolCostumeCmid:
        break;

    case toolRecordSameAction: {
        bool fLonger;
        bool fStep;
        ulong tsCurrent = TsCurrent();
        ulong grfmaf = 0;
        bool fFrozen = FPure((pcmd->grfcust & fcustShift) && !(pcmd->grfcust & fcustCmd));

        if ((pactr->Ptmpl() != _ptmplTool) && !(_ptmplTool->FIsTdt() && pactr->Ptmpl()->FIsTdt()))
        {
            return;
        }

        // If have stopped cycling cels
        if (_fCyclingCels && !(pcmd->grfcust & fcustCmd))
        {
            _fCyclingCels = fFalse;
            _tsLast = tsCurrent;
        }

        // Start recording unless we're cycling cels
        if (!_fCyclingCels && !pactr->FIsModeRecord() && pactr->FIsRecordValid(dxrWld, dyrWld, dzrWld, tsCurrent))
        {
            if (!pactr->FBeginRecord(tsCurrent, FRecordDefault(), _pactrRestore))
                break; // an Oom erc has already been pushed

            // If rerecording, nuke the remainder of the subroute
            if (FRecordDefault())
                pactr->DeleteFwdCore(fFalse);

            Pmvie()->Pmcc()->Recording(fTrue, FRecordDefault());
        }

        if (_fCyclingCels)
        {
            if (pcmd->grfcust & fcustCmd) // still cycling
            {
                if ((tsCurrent - _tsLast) < kdtsCycleCels)
                {
                    break;
                }
                _tsLast = tsCurrent;
                if (!pactr->FSetActionCore(pactr->AnidCur(), pactr->CelnCur() + 1, fFrozen))
                {
                    break; // an Oom erc has already been pushed
                }
                Pmvie()->MarkViews();
            }
        }
        else if (pactr->FIsModeRecord()) // just recording
        {
            if ((tsCurrent - _tsLast) < kdtsFrame)
            {
                break;
            }
            _tsLast = tsCurrent;
            if (fFrozen)
            {
                grfmaf |= fmafFreeze;
            }

            if (_fRespectGround)
            {
                grfmaf |= fmafGround;
            }

            if (!pactr->FRecordMove(dxrWld, dyrWld, dzrWld, grfmaf, tsCurrent, &fLonger, &fStep, _pactrRestore))
            {
                // Oom erc already pushed
                break;
            }
            if (fLonger) // If a point was added to the path
            {
                // update scroll bars
                Pmvie()->Pmcc()->UpdateScrollbars();
                if (fStep)
                    AdjustCursor(pcmd->xp, pcmd->yp);
                Pmvie()->MarkViews();
            }
        }
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
    PActor pactrDup;
    PSceneActorUndo psuna;
    bool fJustToggledSel = fFalse; // user shift-click-released without dragging

    // Shift-click selection toggle: if the user shift-clicked at mousedown
    // and didn't drag past tolerance, fire the toggle now.
    if (_fSelToggleArmed)
    {
        const long kdxpSelToggleTol = 3;
        const long kdypSelToggleTol = 3;
        long dxp = LwAbs(pcmd->xp - _xpSelToggleDown);
        long dyp = LwAbs(pcmd->yp - _ypSelToggleDown);
        if (dxp <= kdxpSelToggleTol && dyp <= kdypSelToggleTol)
        {
            if (_pactrSelToggle != pvNil && Pmvie()->Pscen() != pvNil)
            {
                Pmvie()->Pscen()->FToggleActrSelected(_pactrSelToggle);
                Pmvie()->Pbwld()->MarkDirty();
                Pmvie()->MarkViews();
                fJustToggledSel = fTrue;
            }
        }
        _fSelToggleArmed = fFalse;
        _pactrSelToggle = pvNil;
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

        pactr = Pmvie()->Pscen()->PactrSelected();
        AssertPo(pactr, 0);
        pactrDup = _pactrUndo;
        AssertNilOrPo(pactrDup, 0);

        SetTool(toolCompose);
        Pmvie()->Pmcc()->ChangeTool(toolCompose);

        //
        // Now check if the actor is out of view
        //
        if (!pactr->FIsInView())
        {

            //
            // _pactrUndo is pvNil if this is a new actor, else it is
            // an actor from the roll call.
            //
            if (_pactrUndo != pvNil)
            {
                Pmvie()->Pscen()->FAddActrCore(_pactrUndo); // Replace old actor with saved version.
                vpcex->EnqueueCid(cidActorPlacedOutOfView, pvNil, pvNil, _pactrUndo->Arid());
                ReleasePpo(&_pactrUndo);
            }
            else
            {
                pactr->AddRef();
                AssertDo(Pmvie()->FAddToRollCall(pactr, pvNil), "Should never fail");
                Pmvie()->Pscen()->RemActrCore(pactr->Arid());
                vpcex->EnqueueCid(cidActorPlacedOutOfView, pvNil, pvNil, pactr->Arid());
                ReleasePpo(&pactr);
            }

            WarpCursToCenter();
            vpappb->ShowCurs();
            break;
        }
        else if (_pactrUndo == pvNil)
        {
            //
            // _pactrUndo is pvNil if this is a new actor, else it is
            // an actor from the roll call.
            //
            AssertDo(Pmvie()->FAddToRollCall(pactr, pvNil), "Should never fail");
        }

        //
        // Now build an undo object for the placing of the actor
        //
        psuna = SceneActorUndo::PsunaNew();

        if ((psuna == pvNil) || ((_pactrUndo == pvNil) && !pactr->FDup(&pactrDup, fTrue)))
        {

            PushErc(ercSocNotUndoable);
            ReleasePpo(&pactrDup);
            Pmvie()->ClearUndo();
        }
        else
        {

            pactrDup->SetArid(pactr->Arid());
            psuna->SetType(_pactrUndo == pvNil ? utAdd : utRep);
            psuna->SetActr(pactrDup);
            if (_pactrUndo != pvNil)
            {
                pactrDup->AddRef();
            }

            if (!Pmvie()->FAddUndo(psuna))
            {
                PushErc(ercSocNotUndoable);
                Pmvie()->ClearUndo();
            }

            Pmvie()->Pmcc()->EnableActorTools();
        }

        ReleasePpo(&_pactrUndo);
        ReleasePpo(&psuna);
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

    case toolRecordSameAction:

        if (pvNil != pactr)
        {

            if ((pactr->Ptmpl() != _ptmplTool) && !(_ptmplTool->FIsTdt() && pactr->Ptmpl()->FIsTdt()))
            {
                break;
            }

            pactr->FEndRecord(FRecordDefault(), _pactrRestore); // On error, Oom already pushed
            ReleasePpo(&_pactrRestore);
            Pmvie()->InvalViewsAndScb();
            WarpCursToActor(pactr);
            vpappb->ShowCurs();
        }

        if (_fSetFRecordDefault)
        {
            _fSetFRecordDefault = fFalse;
            SetFRecordDefault(fTrue);
        }

        Pmvie()->Pmcc()->Recording(fFalse, FRecordDefault());

        break;

    case toolSoonerLater:
        if (pactr != pvNil)
        {
            Assert(pactr->FTimeFrozen(), "Something odd is going on");

            PActor pactrDup;
            PActorUndo paund;

            paund = ActorUndo::PaundNew();
            if ((paund == pvNil) || !pactr->FDup(&pactrDup, fTrue))
            {
                Pmvie()->ClearUndo();
                PushErc(ercSocNotUndoable);
            }
            else
            {
                paund->SetPactr(pactrDup);
                ReleasePpo(&pactrDup);
                paund->SetArid(pactr->Arid());
                paund->SetSoonerLater(fTrue);
                paund->SetNfrmLast(Pmvie()->Pscen()->Nfrm());

                if (!Pmvie()->FAddUndo(paund))
                {
                    Pmvie()->ClearUndo();
                    PushErc(ercSocNotUndoable);
                }
            }

            ReleasePpo(&paund);

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
    AssertNilOrPo(_paund, 0);
    ReleasePpo(&_paund); // If you just did a mousedown then mouseup, we have a leftover
                         // undo object that we don't want.  So nuke it.
    AssertNilOrPo(_paundGroup, 0);
    ReleasePpo(&_paundGroup); // Same cleanup for the multi-select group-undo path.

LEndTracking:

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

/***************************************************************************
 *
 * Handles the Cut, Copy, Paste and Clear commands, by setting the appropriate
 * tool.
 *
 * Parameters:
 *  pcmd - Pointer to the command to process.
 *
 * Returns:
 *  fTrue if it processed the command, else fFalse.
 *
 **************************************************************************/
bool MovieView::FCmdClip(PCommand pcmd)
{
    AssertThis(0);
    AssertVarMem(pcmd);

    PTBOX ptbox;
    PDocumentBase pdocb;
    bool fOV = fFalse;
    Command cmd;

    //
    // Check for O-V model for text in text box.
    //
    if (Pmvie()->Pscen() != pvNil)
    {
        AssertPo(Pmvie()->Pscen(), 0);

        if (FTextMode())
        {
            ptbox = Pmvie()->Pscen()->PtboxSelected();

            if ((ptbox != pvNil) && !ptbox->FIsVisible())
            {
                ptbox = pvNil;
            }
        }
        else
        {
            ptbox = pvNil;
        }

        AssertNilOrPo(ptbox, 0);

        if (pcmd->cid == cidPasteTool && vpclip->FGetFormat(kclsActorClipboard, &pdocb))
        {
            fOV = !((PActorClipboard)pdocb)->FRouteOnly();
            ReleasePpo(&pdocb);
        }
        else if (((ptbox != pvNil) && ptbox->FTextSelected()) ||
                 ((pcmd->cid == cidPasteTool) && vpclip->FGetFormat(kclsTextBoxClipboard)))
        {
            if (ptbox != pvNil)
            {
                Command cmd = *pcmd;

                cmd.pcmh = ptbox->PddgGet(0);
                vpcex->EnqueueCmd(&cmd);
            }
            fOV = fTrue;
        }
    }

    cmd = *pcmd;

    switch (pcmd->cid)
    {
    case cidCutTool:
        if (fOV)
        {
            cmd.cid = cidCut;
            vpcex->EnqueueCmd(&cmd);
        }
        else
        {
            SetTool(toolCutObject);
        }
        break;

    case cidCopyTool:
        if (fOV)
        {
            cmd.cid = cidCopy;
            vpcex->EnqueueCmd(&cmd);
        }
        else
        {
            SetTool(toolCopyObject);
        }

        break;

    case cidPasteTool:
        if (fOV)
        {

            if (vpclip->FGetFormat(kclsTextBoxClipboard) || vpclip->FGetFormat(kclsActorClipboard))
            {
                FDoClip(toolPasteObject);
            }
            else
            {
                cmd.cid = cidPaste;
                vpcex->EnqueueCmd(&cmd);
            }
        }
        else
        {
            SetTool(toolPasteObject);
        }

        break;

    case cidCopyRoute:
        SetTool(toolCopyRte);
        break;

    case cidPaste:

        FDoClip(toolPasteObject);
        break;

    case cidShiftCut:
        _grfcust = fcustShift;
    case cidCut:
        FDoClip(toolCutObject);
        _grfcust = fcustNil;
        break;

    case cidShiftCopy:
        _grfcust = fcustShift;
    case cidCopy:
        FDoClip(toolCopyObject);
        _grfcust = fcustNil;
        break;

    default:
        Bug("Unknown command");
    }
    return (fTrue);
}

/***************************************************************************
 *
 * Handles the Cut, Copy, Paste and Clear commands.
 *
 * Parameters:
 *  tool - The tool to apply.
 *
 * Returns:
 *  fTrue if it processed the command, else fFalse.
 *
 **************************************************************************/
bool MovieView::FDoClip(long tool)
{
    AssertThis(0);

    PDocumentBase pdocb = pvNil;

    switch (tool)
    {
    case toolCutObject:
    case toolCopyObject:
    case toolCopyRte:

        //
        // copy the selection
        //
        if (!_FCopySel(&pdocb, tool == toolCopyRte))
        {
            return fTrue;
        }
        vpclip->Set(pdocb);
        ReleasePpo(&pdocb);

        if (tool == toolCutObject)
        {
            _ClearSel();
        }

        Pmvie()->Pmcc()->PlayUISound(tool);

        break;

    case toolPasteObject:
        if (!vpclip->FDocIsClip(pvNil))
        {
            PTextBoxClipboard ptclp;

            if (vpclip->FGetFormat(kclsTextBoxClipboard, (PDocumentBase *)&ptclp))
            {
                AssertPo(ptclp, 0);

                if (Pmvie()->Pscen() == pvNil)
                {
                    ReleasePpo(&ptclp);
                    return (fFalse);
                }

                if (ptclp->FPaste(Pmvie()->Pscen()))
                {
                    ReleasePpo(&ptclp);
                    Pmvie()->Pmcc()->EnableTboxTools();
                    Pmvie()->Pmcc()->PlayUISound(tool);
                    return (fTrue);
                }

                ReleasePpo(&ptclp);
                Pmvie()->Pmcc()->PlayUISound(tool);
                return (fFalse);
            }
            else
            {
                _FPaste(vpclip);
                Pmvie()->Pmcc()->PlayUISound(tool);
            }
        }
        break;

    default:
        Bug("Bad Tool");
    }

    return fTrue;
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
 * Handles the Copying whatever is currently selected.
 *
 * Don't worry about tboxes, because if a tbox is selected it will
 * get the cut/copy/paste command.
 *
 * Parameters:
 *  ppdocb - Pointer to a place to store a pointer to the resulting docb.
 *	fRteOnly - fTrue if to copy an actors route only, else fFalse.
 *
 * Returns:
 *  fTrue if it was successful, else fFalse.
 *
 **************************************************************************/
bool MovieView::_FCopySel(PDocumentBase *ppdocb, bool fRteOnly)
{
    AssertThis(0);

    PActor pactr;
    PActorClipboard paclp;

    if (FTextMode())
    {
        return (fFalse);
    }

    if (Pmvie()->Pscen() == pvNil)
    {
        return (fFalse);
    }

    pactr = Pmvie()->Pscen()->PactrSelected();
    AssertNilOrPo(pactr, 0);

    if ((pactr == pvNil) || !pactr->FIsInView())
    {
        PushErc(ercSocNoActrSelected);
        return (fFalse);
    }

    paclp = ActorClipboard::PaclpNew(pactr, fRteOnly, FPure(_grfcust & fcustShift));
    AssertNilOrPo(paclp, 0);

    *ppdocb = (PDocumentBase)paclp;

    return (paclp != pvNil);
}

/***************************************************************************
 *
 * Handles the deleting whatever is currently selected.
 *
 * Don't worry about tboxes, because if a tbox is selected it will
 * get the cut/copy/paste command.
 *
 * Parameters:
 *  None.
 *
 * Returns:
 *  None.
 *
 **************************************************************************/
void MovieView::_ClearSel()
{
    AssertThis(0);

    PActor pactr;
    bool fAlive;
    bool fEnableSounds;

    if (Pmvie()->Pscen() == pvNil)
    {
        return;
    }

    pactr = Pmvie()->Pscen()->PactrSelected();

    if (pactr == pvNil)
    {
        PushErc(ercSocNoActrSelected);
        return;
    }

    AssertPo(pactr, 0);

    fEnableSounds = !(FPure(Pmvie()->Pscen()->GrfScen() & fscenSounds));
    Pmvie()->Pscen()->Disable(fscenSounds);
    if (!pactr->FDelete(&fAlive, FPure(_grfcust & fcustShift)))
    {
        if (fEnableSounds)
            Pmvie()->Pscen()->Enable(fscenSounds);
        return;
    }
    if (fEnableSounds)
        Pmvie()->Pscen()->Enable(fscenSounds);

    if (!fAlive)
    {
        Pmvie()->Pscen()->RemActrCore(pactr->Arid());
    }
    else
    {
        //
        // According to design, if this fails, it is
        // ok to leave the actor on the stage.
        //
        pactr->FRemFromStageCore();
    }

    Pmvie()->Pscen()->MarkDirty();
    Pmvie()->InvalViews();
}

/***************************************************************************
 *
 * Handles the Pasting whatever is currently in the clipboard.
 *
 * Don't worry about tboxes, because if a tbox is selected it will
 * get the cut/copy/paste command.
 *
 * Parameters:
 *  pdocb - The pointer to the resulting docb.
 *
 * Returns:
 *  fTrue if it was successful, else fFalse.
 *
 **************************************************************************/
bool MovieView::_FPaste(PClipboardObject pclip)
{
    AssertThis(0);
    AssertPo(pclip, 0);

    PActorClipboard paclp;
    PTextBoxClipboard ptclp;
    PActor pactr;
    bool fRet;

    if (pclip->FGetFormat(kclsActorClipboard, (PDocumentBase *)&paclp))
    {
        AssertPo(paclp, 0);

        if (Pmvie()->Pscen() == pvNil)
        {
            PushErc(ercSocNoScene);
            return (fFalse);
        }

        if (paclp->FRouteOnly() && FTextMode())
        {
            PushErc(ercSocCannotPasteThatHere);
            ReleasePpo(&paclp);
            return (fTrue);
        }

        pactr = Pmvie()->Pscen()->PactrSelected();
        AssertNilOrPo(pactr, 0);

        if (paclp->FRouteOnly() && ((pactr == pvNil) || !pactr->FIsInView()))
        {
            PushErc(ercSocNoActrSelected);
            ReleasePpo(&paclp);
            return (fTrue);
        }

        fRet = paclp->FPaste(Pmvie());
        ReleasePpo(&paclp);
        return fRet;
    }

    if (pclip->FGetFormat(kclsTextBoxClipboard, (PDocumentBase *)&ptclp))
    {
        AssertPo(ptclp, 0);

        if (Pmvie()->Pscen() == pvNil)
        {
            PushErc(ercSocNoScene);
            return (fFalse);
        }

        fRet = ptclp->FPaste(Pmvie()->Pscen());
        ReleasePpo(&ptclp);
        return fRet;
    }

    PushErc(ercSocCannotPasteThatHere);
    return (fFalse);
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
// File-local helpers for the actor-tag-group hotkeys (Ctrl+G / Ctrl+Shift+G /
// Ctrl+1..9). Tag syntax: '#' followed by [A-Za-z0-9_]+, terminated by
// whitespace, end-of-string, or another '#'. ASCII-only.
static bool _FIsTagCharMv(achar ch)
{
    return (ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') ||
           (ch >= '0' && ch <= '9') || ch == '_';
}

// Walk pstn, find each '#N...' tag, parse the run of digits as an integer.
// Returns the largest integer seen, or 0 if no purely-numeric tag is found.
static long _LwMaxNumericTagInName(PString pstn)
{
    achar *prgch = pstn->Prgch();
    long cch = pstn->Cch();
    long lwMax = 0;
    for (long ich = 0; ich < cch; ich++)
    {
        if (prgch[ich] != '#')
            continue;
        long jch = ich + 1;
        long lw = 0;
        bool fAllDigits = (jch < cch && prgch[jch] >= '0' && prgch[jch] <= '9');
        while (jch < cch && _FIsTagCharMv(prgch[jch]))
        {
            if (!(prgch[jch] >= '0' && prgch[jch] <= '9'))
            {
                fAllDigits = fFalse;
                break;
            }
            lw = lw * 10 + (prgch[jch] - '0');
            jch++;
        }
        if (fAllDigits && jch > ich + 1 && lw > lwMax)
            lwMax = lw;
    }
    return lwMax;
}

// Walks every actor in the current scene's roll-call and returns
// max(numeric tags) + 1, or 1 if none. Auto-numbering for Ctrl+G.
static long _LwNextFreeNumericTagInScene(PScene pscen, PMovie pmvie)
{
    AssertPo(pscen, 0);
    AssertPo(pmvie, 0);

    long cactr = pscen->Cactr();
    long lwMax = 0;
    String stn;
    PDynamicArray pglpactr = pscen->PglRollCall();
    if (pglpactr == pvNil)
        return 1;
    for (long iactr = 0; iactr < cactr; iactr++)
    {
        PActor pactr;
        pglpactr->Get(iactr, &pactr);
        if (pactr == pvNil)
            continue;
        pactr->GetName(&stn);
        long lw = _LwMaxNumericTagInName(&stn);
        if (lw > lwMax)
            lwMax = lw;
    }
    return lwMax + 1;
}

// Strips the rightmost '#tag' from pstn (with any single preceding space).
// Sets *pfStripped to fTrue iff anything was removed. No-op + fFalse if
// the name has no tag.
static void _StripRightmostTag(PString pstn, bool *pfStripped)
{
    AssertPo(pstn, 0);
    AssertVarMem(pfStripped);

    *pfStripped = fFalse;
    achar *prgch = pstn->Prgch();
    long cch = pstn->Cch();

    for (long ich = cch - 1; ich >= 0; ich--)
    {
        if (prgch[ich] != '#')
            continue;
        // Verify this is a real tag: at least one tag char follows.
        if (ich + 1 >= cch || !_FIsTagCharMv(prgch[ich + 1]))
            continue;
        // Verify the run after '#' is all tag chars (no embedded space).
        long jch;
        for (jch = ich + 1; jch < cch && _FIsTagCharMv(prgch[jch]); jch++)
            ;
        if (jch != cch)
            continue; // there's non-tag content after this '#tag'; not the rightmost tag
        // Eat one preceding space so "Foo #1" -> "Foo".
        long ichDel = ich;
        if (ichDel > 0 && prgch[ichDel - 1] == ' ')
            ichDel--;
        pstn->Delete(ichDel, cch - ichDel);
        *pfStripped = fTrue;
        return;
    }
}

// Helper: append " #N" to a copy of pstnIn into pstnOut.
static bool _FAppendNumericTag(PString pstnIn, long lwTag, PString pstnOut)
{
    *pstnOut = *pstnIn;
    if (!pstnOut->FAppendCh((achar)' '))
        return fFalse;
    if (!pstnOut->FAppendCh((achar)'#'))
        return fFalse;
    achar rgch[16];
    long cch = 0;
    long lwTmp = lwTag;
    do
    {
        rgch[cch++] = (achar)('0' + (lwTmp % 10));
        lwTmp /= 10;
    } while (lwTmp > 0 && cch < 15);
    // rgch is little-endian; append in reverse.
    for (long ich = cch - 1; ich >= 0; ich--)
    {
        if (!pstnOut->FAppendCh(rgch[ich]))
            return fFalse;
    }
    return fTrue;
}

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
        if (pscen != pvNil && pscen->CactrSelected() >= 1)
        {
            bool fStrip = FPure(pcmd->grfcust & fcustShift);
            PActorRenameGroupUndo parg = ActorRenameGroupUndo::PargNew();
            if (parg == pvNil)
            {
                PushErc(ercSocNotUndoable);
                return fTrue;
            }
            long cactrSel = pscen->CactrSelected();
            long lwTag = fStrip ? 0 : _LwNextFreeNumericTagInScene(pscen, Pmvie());
            bool fOk = fTrue;
            bool fAnyChanged = fFalse;
            for (long iactr = 0; fOk && iactr < cactrSel; iactr++)
            {
                PActor pactr = pscen->PactrSelectedAt(iactr);
                if (pactr == pvNil)
                    continue;
                String stnOld, stnNew;
                pactr->GetName(&stnOld);
                if (fStrip)
                {
                    stnNew = stnOld;
                    bool fStripped = fFalse;
                    _StripRightmostTag(&stnNew, &fStripped);
                    if (!fStripped)
                        continue; // actor had no tag; skip without recording undo
                }
                else
                {
                    if (!_FAppendNumericTag(&stnOld, lwTag, &stnNew))
                    {
                        fOk = fFalse;
                        break;
                    }
                }
                if (!parg->FAddChild(pactr->Arid(), &stnOld))
                {
                    fOk = fFalse;
                    break;
                }
                if (!Pmvie()->FNameActr(pactr->Arid(), &stnNew))
                {
                    fOk = fFalse;
                    break;
                }
                fAnyChanged = fTrue;
            }
            if (!fOk)
            {
                ReleasePpo(&parg);
                Pmvie()->ClearUndo();
                PushErc(ercSocNotUndoable);
            }
            else if (fAnyChanged)
            {
                if (!Pmvie()->FAddUndo(parg))
                {
                    PushErc(ercSocNotUndoable);
                    Pmvie()->ClearUndo();
                }
                ReleasePpo(&parg);
                Pmvie()->SetDirty();
            }
            else
            {
                ReleasePpo(&parg);
            }
        }
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
MMMovieSceneUndoServices MovieSceneUndo::_Services()
{
    MMMovieSceneUndoServices s = {};
    s.context = Pmvie();
    s.create = [](void *p, MMI32 index) -> int { return static_cast<PMovie>(p)->FNewScenInsCore(index); };
    s.remove = [](void *p, MMI32 index) -> int { return static_cast<PMovie>(p)->FRemScenCore(index); };
    s.insert = [](void *p, MMI32 index, void *scene) -> int {
        return static_cast<PMovie>(p)->FInsScenCore(index, static_cast<PScene>(scene));
    };
    s.background = [](void *p, const MMTag *value) -> int {
        TAG tag, old;
        TagFromCore(&tag, *value);
        return static_cast<PMovie>(p)->Pscen()->FSetBkgdCore(&tag, &old);
    };
    s.notify = [](void *p, int removed) {
        auto client = static_cast<PMovie>(p)->Pmcc();
        if (removed) client->SceneNuked();
        else client->SceneUnnuked();
    };
    s.flush = [](void *p) { static_cast<PMovie>(p)->Pmsq()->FlushMsq(); };
    s.clear = [](void *p) { static_cast<PMovie>(p)->ClearUndo(); };
    s.unknown = [](void *) { Bug("Unknown munst"); };
    return s;
}

bool MovieSceneUndo::FDo(PDocumentBase pdocb)
{
    AssertThis(0);
    auto services = _Services();
    MMTag tag = {};
    if (_munst == munstInsScen) tag = TagToCore(_tag);
    return mm_movie_scene_undo(_munst, _iscen, _pscen, &tag, 1, &services) != 0;
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
    auto services = _Services();
    return mm_movie_scene_undo(_munst, _iscen, _pscen, nullptr, 0, &services) != 0;
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
