/* Copyright (c) Microsoft Corporation.
   Licensed under the MIT License. */

/***************************************************************************

    tagman.cpp: Tag Manager class (TagManager)

    Primary Author: ******
    Review Status: REVIEWED - any changes to this file must be reviewed!

    It is important to keep in mind that there are two layers of caching
    going on in TagManager: Caching content from the CD (or other slow source)
    to the local hard disk, and caching resources in RAM using chunky
    resources (the ChunkyResourceFile and ChunkyResourceManager classes).

    For each source, TagManager maintains (in the C source catalogue) a ChunkyResourceManager (Chunky Resource
    Manager) of all the content	files on the source and a ChunkyResourceFile (Chunky
    Resource File), which is a single file on the HD which can be used
    for faster access to the source.  Both CRFs and CRMs can cache
    resources in RAM.  Since Socrates copies *all* content from the CD
    to the cache file, the ChunkyResourceManager is told not to cache its resources in
    RAM.  However, if the source is actually on the HD, TagManager notices and
    doesn't copy any content to a cache file, since that would be a waste
    of time.  Instead, the content is read directly from the ChunkyResourceManager.  In this
    case, TagManager does tell the ChunkyResourceManager to cache its resources in RAM.

    Source names: every source has a long and short name.  This is so we
    can use long names for the source directory on the HD (e.g., "3D Movie
    Maker"), and short names for the source directory on the CD
    ("3DMOVIE").  We have to support short names because CD-ROMs currently
    do not allow long filenames.  So everywhere that we look for the
    source directory, we accept either the long or short name.
    The C source-name table keeps track of these names. Each string is the
    "merged name", which is the long name followed by a slash character
    (/) followed by the short name, e.g., "3D Movie Maker/3DMOVIE".  The
    SplitString() function splits the merged name into the long and
    short names.

***************************************************************************/
#include "soc_core.h"
#include "engine_movie_source.h"
#include "mm/movie_session.h"
#include "mm/archive_reader.h"
#include "mm/chunk_copy.h"
#include "mm/native_archive_file.h"
PTagManager vptagm;

ASSERTNAME

RTCLASS(TagManager)

namespace
{
class NativeSourcePaths : public BASE
{
  public:
    Filename local, media;
};
struct NativeSourceHost
{
    PTagManager manager;
    PFilename output;
    PString leaf;
    const TAG *asset;
    FileType type = ftgNil;
};
struct NativeDiscoveryHost
{
    PTagManager manager;
    LegacyLong sid;
    PFilename output;
    bool media;
    String longName, shortName;
    Filename candidate, drive;
    FileNameEnumerator drives;
};

PChunkyResourceFile OpenSourceArchive(Filename &filename, LegacyLong budget, bool media)
{
    const auto existing = ChunkyFile::PcflFromFni(&filename);
    if (existing)
        return existing->FSetGrfcfl(fcflNil) ? ChunkyResourceFile::PcrfNew(existing, budget) : nullptr;
    const auto existingFile = FileObject::PfilFromFni(&filename);
    if (const auto owner = KauaiFindWorkingArchive(existingFile))
        return ChunkyResourceFile::PcrfNewPrivate(owner, budget);
    if (media || (existingFile && (existingFile->GrffilCur() & (ffilWriteEnable | ffilDenyRead))) ||
        (filename.Grfvk() & (fvkCD | fvkFloppy | fvkRemovable)))
        return ChunkyResourceFile::PcrfOpenWorking(&filename, false, budget);

    String path;
    filename.GetStnPath(&path);
    auto file = mm_native_archive_file_open(reinterpret_cast<const MMNativePathChar *>(path.Psz()));
    MMReadStream stream;
    PChunkyResourceFile cache = nullptr;
    if (mm_native_archive_file_stream(file, &stream))
    {
        const unsigned short order = 1;
        MMChunkPolicy policy = {};
#ifdef CHUNK_BIG_INDEX
        policy.expanded_runtime = 1;
#endif
        policy.native_big_endian = *reinterpret_cast<const byte *>(&order) == 0;
        policy.max_name_bytes = kcbMaxDataStn;
        MMChunkIndex *index;
        if (mm_archive_read_index(&stream, &policy, &index) == MM_ARCHIVE_READ_OK)
        {
            cache = ChunkyResourceFile::PcrfNewIndex(index, file, *mm_native_archive_file_blocks(), path, budget);
            mm_chunk_index_close(index);
        }
    }
    mm_native_archive_file_release(file);
    return cache;
}
} // namespace
MMSourceOwners TagManager::_SourceOwners()
{
    return {this, [](void *) -> void * { return NewObj NativeSourcePaths(); },
            [](void *d, const void *s) {
                auto to = static_cast<NativeSourcePaths *>(d);
                auto from = const_cast<NativeSourcePaths *>(static_cast<const NativeSourcePaths *>(s));
                to->local = from->local;
                to->media = from->media;
            },
            [](void *p) { static_cast<NativeSourcePaths *>(p)->Release(); },
            [](void *p) { static_cast<PChunkyResourceManager>(p)->Release(); }};
}
MMSourceServices TagManager::_SourceServices(void *context)
{
    static_assert(static_cast<int>(TagSourceAccess::Legacy) == MM_SOURCE_LEGACY &&
                      static_cast<int>(TagSourceAccess::Available) == MM_SOURCE_AVAILABLE &&
                      static_cast<int>(TagSourceAccess::Disabled) == MM_SOURCE_DISABLED &&
                      static_cast<int>(TagSourceAccess::Missing) == MM_SOURCE_MISSING,
                  "source access encoding");
    MMSourceServices s = {};
    s.context = context;
    s.access = [](void *c, MMI32 sid, void *, int kind) -> int {
        auto h = static_cast<NativeSourceHost *>(c);
        return static_cast<int>(h->manager->SourceDirectory(sid, kind ? h->output : nullptr));
    };
    s.path_empty = [](void *p, int media) -> int {
        auto paths = static_cast<NativeSourcePaths *>(p);
        return (media ? paths->media : paths->local).Ftg() == ftgNil;
    };
    s.clear_local = [](void *p) { static_cast<NativeSourcePaths *>(p)->local.SetNil(); };
    s.build_local = [](void *c, MMI32 sid, void *p, int *exists) -> int {
        bool v = false;
        const bool ok = static_cast<NativeSourceHost *>(c)->manager->_FBuildFniHD(
            sid, &static_cast<NativeSourcePaths *>(p)->local, &v);
        *exists = v;
        return ok;
    };
    s.archive_source = [](void *c, MMI32 sid) -> int {
        return static_cast<NativeSourceHost *>(c)->manager->HasArchiveSource(sid);
    };
    s.content = [](void *c, void *p, int *exists) -> int {
        bool v = false;
        const bool ok = static_cast<NativeSourceHost *>(c)->manager->_FDetermineIfContentOnFni(
            &static_cast<NativeSourcePaths *>(p)->local, &v);
        *exists = v;
        return ok;
    };
    s.find_media = [](void *c, MMI32 sid, void *p, int *changed) -> int {
        bool v = false;
        const bool ok = static_cast<NativeSourceHost *>(c)->manager->_FFindFniCD(
            sid, &static_cast<NativeSourcePaths *>(p)->media, &v);
        *changed = v;
        return ok;
    };
    s.retry = [](void *c, MMI32 sid) -> int {
        auto h = static_cast<NativeSourceHost *>(c);
        return h->manager->_FRetry(sid, h->asset);
    };
    s.new_manager = [](void *c, MMI32 sid, void *p, int media) -> void * {
        auto paths = static_cast<NativeSourcePaths *>(p);
        return static_cast<NativeSourceHost *>(c)->manager->_PcrmSourceNew(sid, media ? &paths->media : &paths->local,
                                                                         media != 0);
    };
    s.output = [](void *c, void *p, int media) {
        auto paths = static_cast<NativeSourcePaths *>(p);
        *static_cast<NativeSourceHost *>(c)->output = media ? paths->media : paths->local;
    };
    s.moved_open_source = [](void *) { Bug("fniCD can't change once pcrm is opened!"); };
    s.prepare_file = [](void *c) -> int {
        auto h = static_cast<NativeSourceHost *>(c);
        if (!h->output->FBuildFromPath(h->leaf))
            return 0;
        h->type = h->output->Ftg();
        return 1;
    };
    s.find_leaf = [](void *c) -> int {
        auto h = static_cast<NativeSourceHost *>(c);
        return h->output->FSetLeaf(h->leaf, h->type) && h->output->FResolveCase();
    };
    s.clear_output = [](void *c) { static_cast<NativeSourceHost *>(c)->output->SetNil(); };
    return s;
}

MMSourceDiscovery TagManager::_DiscoveryServices(void *context)
{
    return {context,
            [](void *c, MMI32 sid) -> int {
                auto h = static_cast<NativeDiscoveryHost *>(c);
                return static_cast<int>(h->manager->SourceDirectory(sid, h->output));
            },
            [](void *c, MMI32 sid) -> int {
                auto h = static_cast<NativeDiscoveryHost *>(c);
                return h->manager->_FGetStnSplitOfSid(sid, &h->longName, &h->shortName);
            },
            [](void *c) {
                auto h = static_cast<NativeDiscoveryHost *>(c);
                h->candidate = h->manager->_fniHDRoot;
            },
            [](void *c, int shortName) -> int {
                auto h = static_cast<NativeDiscoveryHost *>(c);
                auto name = shortName ? &h->shortName : &h->longName;
                return h->media ? h->manager->_FEnsureFniCD(h->sid, &h->candidate, name)
                                : h->candidate.FDownDir(name, ffniMoveToDir);
            },
            [](void *c) {
                auto h = static_cast<NativeDiscoveryHost *>(c);
                *h->output = h->candidate;
            },
            [](void *c) -> int { return static_cast<NativeDiscoveryHost *>(c)->output->Ftg() != ftgNil; },
            [](void *c) -> int {
                auto h = static_cast<NativeDiscoveryHost *>(c);
                return h->manager->_FEnsureFniCD(h->sid, h->output);
            },
            [](void *c) -> int { return static_cast<NativeDiscoveryHost *>(c)->drives.FInit(nullptr, nullptr, 0); },
            [](void *c) -> int {
                auto h = static_cast<NativeDiscoveryHost *>(c);
                return h->drives.FNextFni(&h->drive);
            },
            [](void *c) -> int { return (static_cast<NativeDiscoveryHost *>(c)->drive.Grfvk() & fvkFloppy) != 0; },
            [](void *c) {
                auto h = static_cast<NativeDiscoveryHost *>(c);
                h->candidate = h->drive;
            }};
}

/***************************************************************************
    Initialize the tag manager
***************************************************************************/
PTagManager TagManager::PtagmNew(PFilename pfniHDRoot, PFNINSCD pfninscd, LegacyLong cbCache)
{
    AssertPo(pfniHDRoot, ffniDir);
    Assert(pvNil != pfninscd, "bad pfninscd");
    AssertIn(cbCache, 0, kcbMax);

    PTagManager ptagm;

    ptagm = NewObj TagManager;
    if (pvNil == ptagm)
        goto LFail;

    ptagm->_fniHDRoot = *pfniHDRoot;
    ptagm->_cbCache = cbCache;
    ptagm->_pfninscd = pfninscd;

    {
        const auto owners = ptagm->_SourceOwners();
        const auto allocator = CollectionAllocator();
        if (mm_source_init(&ptagm->_sources, &owners, &allocator) != MM_STORAGE_OK)
            goto LFail;
    }

    {
        const auto allocator = CollectionAllocator();
        if (mm_source_names_init(&ptagm->_names, sizeof(achar), koskCur,
#ifdef DEBUG
                                       1,
#else
                                       0,
#endif
                                       &allocator) != MM_STORAGE_OK)
            goto LFail;
    }

    AssertPo(ptagm, 0);
    return ptagm;
LFail:
    ReleasePpo(&ptagm);
    return pvNil;
}

/***************************************************************************
    Tag Manager destructor
***************************************************************************/
TagManager::~TagManager(void)
{
    AssertBaseThis(0);
    if (_sources.storage.allocator.release)
        AssertDo(mm_source_dispose(&_sources) == MM_STORAGE_OK, "invalid source disposal");
    if (_names.storage.allocator.release)
        AssertDo(mm_source_names_dispose(&_names) == MM_STORAGE_OK, "invalid names disposal");
}

/***************************************************************************
    Split a merged string into its long and short components
***************************************************************************/
void TagManager::SplitString(PString pstnMerged, PString pstnLong, PString pstnShort)
{
    const auto parts = mm_source_name_split(pstnMerged->Prgch(), pstnMerged->Cch(), sizeof(achar));
    pstnLong->SetRgch(pstnMerged->Prgch(), parts.long_count);
    pstnShort->SetRgch(pstnMerged->Prgch() + parts.short_offset, parts.short_count);
}

bool TagManager::FGetSourceSid(LegacyLong index, LegacyLong *sid)
{
    const void *text;
    MMU32 count;
    MMI32 value;
    if (!mm_source_names_at(&_names, index, &text, &count, &value))
        return false;
    *sid = value;
    return true;
}

void TagManager::InvalidateSource(LegacyLong sid)
{
    AssertDo(mm_source_invalidate(&_sources, sid) == MM_STORAGE_OK, "invalid source invalidation");
}

/***************************************************************************
    Add source title string table entries to tag manager, if it doesn't
    already know them.
***************************************************************************/
bool TagManager::FMergeGstSource(PStringTable_GST pgst, short bo, short osk)
{
    AssertPo(pgst, 0);
    Assert(size(LegacyLong) == pgst->CbExtra(), "bad pgstSource");
    struct Host
    {
        PStringTable_GST table;
        String text;
    } host = {pgst};
    const MMSourceNameInput input = {
        &host, static_cast<MMU32>(pgst->IvMac()),
        [](void *c, MMU32 i, const void **text, MMU32 *count, MMI32 *sid) {
            auto h = static_cast<Host *>(c);
            h->table->GetStn(i, &h->text);
            h->table->GetExtra(i, sid);
            *text = h->text.Prgch();
            *count = h->text.Cch();
        },
        [](void *, MMI32) { Bug("Invalid sid"); }};
    return mm_source_names_merge(&_names, &input, bo == kboOther) != 0;
}

bool TagManager::FMergeMovieSources(PChunkyResourceFile resources, ChunkNumber movie)
{
    const auto graph = mm_movie_owner_graph(resources->ReadOwner());
    return mm_source_names_merge_movie(&_names, &graph, movie, EngineMovieSource::ConvertWidth, nullptr,
                                       [](void *, MMI32) { Bug("Invalid sid"); }) != 0;
}

/***************************************************************************
    Add source title string to tag manager, if it's not already there
***************************************************************************/
bool TagManager::FAddStnSource(PString pstn, LegacyLong sid)
{
    Assert(sid >= 0, "Invalid sid");
    return mm_source_names_add(&_names, sid, pstn->Prgch(), pstn->Cch()) != 0;
}

/***************************************************************************
    Find the sid with the given string as its source name.  pstn can be
    the merged name, the short name, or the long name.
***************************************************************************/
bool TagManager::FGetSid(PString pstn, LegacyLong *psid)
{
    MMI32 value;
    const auto equal = [](void *, const void *a, MMU32 ac, const void *b, MMU32 bc) -> int {
        return ::FEqualUserRgch(const_cast<achar *>(static_cast<const achar *>(a)), ac,
                                const_cast<achar *>(static_cast<const achar *>(b)), bc, fstnIgnoreCase);
    };
    if (!mm_source_names_find_name(&_names, pstn->Prgch(), pstn->Cch(), equal, nullptr, &value))
        return false;
    *psid = value;
    return true;
}

/***************************************************************************
    Find the string of the source with the given sid
***************************************************************************/
bool TagManager::_FGetStnMergedOfSid(LegacyLong sid, PString pstn)
{
    const void *text = nullptr;
    MMU32 count = 0;
    const bool found = mm_source_names_merged(&_names, sid, &text, &count) != 0;
    if (text)
        pstn->SetRgch(const_cast<achar *>(static_cast<const achar *>(text)), count);
    return found;
}

/***************************************************************************
    Find the string of the source with the given sid
***************************************************************************/
bool TagManager::_FGetStnSplitOfSid(LegacyLong sid, PString pstnLong, PString pstnShort)
{
    AssertThis(0);
    AssertVarMem(pstnLong);
    AssertVarMem(pstnShort);

    String stnMerged;

    if (!_FGetStnMergedOfSid(sid, &stnMerged))
        return fFalse;
    SplitString(&stnMerged, pstnLong, pstnShort);
    return fTrue;
}

/***************************************************************************
    Builds the Filename to the HD files for a given sid
    - If we don't even know the string for the sid, return fFalse
    - If there is no fniHD, set *pfExists to fFalse and return fTrue
    - If we find the fniHD, put it in *pfniHD, set *pfExists to fTrue,
      and return fTrue
***************************************************************************/
TagSourceAccess TagManager::SourceDirectory(LegacyLong sid, PFilename path)
{
    const auto provider = [](void *c, const void *longText, MMU32 longCount, const void *shortText,
                             MMU32 shortCount, void *path) -> int {
        String longName, shortName;
        Filename unused;
        longName.SetRgch(const_cast<achar *>(static_cast<const achar *>(longText)), longCount);
        shortName.SetRgch(const_cast<achar *>(static_cast<const achar *>(shortText)), shortCount);
        return static_cast<int>(static_cast<PTagManager>(c)->_sourceResolver(
            &longName, &shortName, path ? static_cast<PFilename>(path) : &unused));
    };
    return static_cast<TagSourceAccess>(mm_source_names_resolve(
        &_names, sid, path, _sourceResolver ? +provider : nullptr, this));
}

bool TagManager::_FBuildFniHD(LegacyLong sid, PFilename pfniHD, bool *pfExists)
{
    NativeDiscoveryHost host = {this, sid, pfniHD, false};
    const auto services = _DiscoveryServices(&host);
    int exists;
    const bool ok = mm_source_discover_local(sid, &services, &exists) != 0;
    *pfExists = exists != 0;
    return ok;
}

/***************************************************************************
    See if there are any content files in the directory specified by pfni
***************************************************************************/
bool TagManager::_FDetermineIfContentOnFni(PFilename pfni, bool *pfContentOnFni)
{
    AssertThis(0);
    AssertPo(pfni, ffniDir);
    AssertVarMem(pfContentOnFni);

    FileNameEnumerator fne;
    FileType ftgContent = kftgContent;
    Filename fni;

    if (!fne.FInit(pfni, &ftgContent, 1))
        return fFalse;
    if (fne.FNextFni(&fni))
        *pfContentOnFni = fTrue;
    else
        *pfContentOnFni = fFalse;
    return fTrue;
}

/***************************************************************************
    Returns whether the directory pointed to by pfniCD exists where we
    think it does.  Or, if pstn is non-nil, try to go down from pfniCD
    to pstn.
***************************************************************************/
bool TagManager::_FEnsureFniCD(LegacyLong sid, Filename *pfniCD, PString pstn)
{
    AssertThis(0);
    Assert(sid >= 0, "Invalid sid");
    AssertPo(pfniCD, ffniDir);
    AssertNilOrPo(pstn, 0);

    ErrorStack ersT;
    ErrorStack *pers;
    bool fRet;

    pers = vpers;
    vpers = &ersT;

    // Block the Windows "There is no disk in the drive" message
    auto em = KauaiFiles::SuppressMediaErrors();

    if (pvNil != pstn)
        fRet = pfniCD->FDownDir(pstn, ffniMoveToDir);
    else
        fRet = (pfniCD->TExists() == tYes);

    KauaiFiles::RestoreMediaErrors(em);

    vpers = pers;
    return fRet;
}

/***************************************************************************
    This function verifies that the source (e.g., CD) is where we think it
    is, and searches for it otherwise.  Pass the previously determined Filename
    of the CD directory file in pfniCD.  Or if this is the first time
    looking for this source, pass in any Filename with a FileType of ftgNil.	If it
    can't find the CD directory, it returns fFalse with pfniInfo untouched.
***************************************************************************/
bool TagManager::_FFindFniCD(LegacyLong sid, PFilename pfniCD, bool *pfFniChanged)
{
    NativeDiscoveryHost host = {this, sid, pfniCD, true};
    const auto services = _DiscoveryServices(&host);
    int changed;
    const bool ok = mm_source_discover_media(sid, &services, &changed) != 0;
    *pfFniChanged = changed != 0;
    return ok;
}

/***************************************************************************
    Calls the client-supplied callback, which should tell the user that
    the source named stn cannot be found.  Returns fTrue if the user wants
    to retry, else fFalse.
***************************************************************************/
bool TagManager::_FRetry(LegacyLong sid, const TAG *asset)
{
    AssertThis(0);
    Assert(sid >= 0, "Invalid sid");

    String stnLong;
    String stnShort;

    if (!_FGetStnSplitOfSid(sid, &stnLong, &stnShort))
    {
        Bug("the source-name table has no string for this sid!");
        return fFalse;
    }
    String root;
    _fniHDRoot.GetStnPath(&root);
    return _pfninscd({sid, &stnLong, &stnShort, &root, asset, SourceDirectory(sid) == TagSourceAccess::Disabled});
}

/***************************************************************************
    Builds the ChunkyResourceManager for the given sid's source.  pfniDir tells where the
    content files are.
***************************************************************************/
PChunkyResourceManager TagManager::_PcrmSourceNew(LegacyLong sid, PFilename directory, bool media)
{
    AssertThis(0);
    Assert(sid >= 0, "Invalid sid");
    AssertPo(directory, ffniDir);
    struct Host
    {
        PTagManager manager;
        PFilename directory;
        bool media;
        Filename current;
        FileNameEnumerator files;
        FileType type = kftgContent;
    } host = {this, directory, media};
    const MMSourceBuildServices services = {
        &host,
        [](void *) -> void * { return ChunkyResourceManager::PcrmNew(0); },
        [](void *c, MMI32 sid) -> int { return static_cast<Host *>(c)->manager->HasArchiveSource(sid); },
        [](void *c) -> int {
            auto h = static_cast<Host *>(c);
            return h->files.FInit(h->directory, &h->type, 1);
        },
        [](void *c, MMI32 sid, MMU32 i, int archive) -> int {
            auto h = static_cast<Host *>(c);
            return archive ? h->manager->SourceArchive(sid, h->type, i, &h->current) : h->files.FNextFni(&h->current);
        },
        [](void *c, MMI32 budget) -> void * {
            const auto h = static_cast<Host *>(c);
            return OpenSourceArchive(h->current, budget, h->media);
        },
        [](void *m, void *f) -> int {
            auto manager = static_cast<PChunkyResourceManager>(m)->CoreManager();
            auto cache = static_cast<PChunkyResourceFile>(f)->CoreCache();
            return mm_resource_manager_add_cache(manager, cache, nullptr) == MM_STORAGE_OK;
        },
        [](void *f) { static_cast<PChunkyResourceFile>(f)->Release(); },
        [](void *m) { static_cast<PChunkyResourceManager>(m)->Release(); }};
    return static_cast<PChunkyResourceManager>(mm_source_build(sid, _cbCache, &services));
}

/***************************************************************************
    Returns the source ChunkyResourceManager for the given sid, creating (and remembering) a
    new one if there isn't one already.  It verifies that the CD is still
    in the drive, unless fDontHitCD is fTrue.
***************************************************************************/
PChunkyResourceManager TagManager::_PcrmSourceGet(LegacyLong sid, bool localOnly, const TAG *asset)
{
    NativeSourceHost host = {this, nullptr, nullptr, asset};
    NativeSourcePaths paths;
    const auto services = _SourceServices(&host);
    return static_cast<PChunkyResourceManager>(mm_source_manager(&_sources, sid, localOnly, &paths, &services));
}

/***************************************************************************
    Determines whether source is on HD (if it is, don't cache its stuff to
    HD!)  Note that the function return value is whether the function
    completed without error, not whether the source is on HD.
***************************************************************************/
bool TagManager::_FDetermineIfSourceHD(LegacyLong sid, bool *exists)
{
    AssertThis(0);
    NativeSourceHost host = {this, nullptr, nullptr, nullptr};
    NativeSourcePaths paths;
    const auto services = _SourceServices(&host);
    int value = 0;
    const bool ok = mm_source_local_content(&_sources, sid, &paths, &value, &services) != 0;
    if (ok)
        *exists = value != 0;
    return ok;
}

/***************************************************************************
    Get the Filename for the HD directory
***************************************************************************/
bool TagManager::_FGetFniHD(LegacyLong sid, PFilename output)
{
    NativeSourceHost host = {this, output, nullptr, nullptr};
    NativeSourcePaths paths;
    const auto services = _SourceServices(&host);
    return mm_source_local_path(&_sources, sid, &paths, &services) != 0;
}

/***************************************************************************
    Get the Filename for the CD directory
***************************************************************************/
bool TagManager::_FGetFniCD(LegacyLong sid, PFilename output, bool ask)
{
    NativeSourceHost host = {this, output, nullptr, nullptr};
    NativeSourcePaths paths;
    const auto services = _SourceServices(&host);
    return mm_source_media_path(&_sources, sid, ask, &paths, &services) != 0;
}

/***************************************************************************
    Finds the file with name pstn on the HD or CD.
***************************************************************************/
bool TagManager::FFindFile(LegacyLong sid, PString leaf, PFilename output, bool ask)
{
    AssertThis(0);
    Assert(sid >= 0, "Invalid sid");
    AssertPo(leaf, 0);
    AssertVarMem(output);
    NativeSourceHost host = {this, output, leaf, nullptr};
    NativeSourcePaths paths;
    const auto services = _SourceServices(&host);
    return mm_source_find_file(&_sources, sid, ask, &paths, &services) != 0;
}

/***************************************************************************
    Build a tag for a child of another tag.  Note that this may hit the
    CD if _PcrmSourceGet has not yet been called for ptagPar->sid.
***************************************************************************/
namespace
{
struct NativeTagHost
{
    PTagManager manager;
    PTAG target;
    const TAG *asset;
};
} // namespace
const MMTagServices &TagManager::CoreServices()
{
    static const auto services = [] {
        MMTagServices s = {};
        s.file = [](void *c) -> void * { return static_cast<PChunkyResourceFile>(c)->Pcfl(); };
        s.retain = [](void *c) { static_cast<PChunkyResourceFile>(c)->AddRef(); };
        s.release = [](void *c) { static_cast<PChunkyResourceFile>(c)->Release(); };
        s.compare_cache = [](void *a, void *b) -> int {
            auto x = static_cast<PChunkyResourceFile>(a), y = static_cast<PChunkyResourceFile>(b);
            return x == y ? 0 : x < y ? -1 : 1;
        };
        s.bind_file = [](void *file, MMChunkFileCopy *binding) {
            return static_cast<PChunkyFile>(file)->CopyEndpoint(*binding);
        };
        s.bind_cache = [](void *cache, MMResourceChunkCopy *binding) {
            return static_cast<PChunkyResourceFile>(cache)->CopySource(*binding);
        };
        s.bind_target = [](void *cache, MMChunkFileCopy *binding) {
            return static_cast<PChunkyResourceFile>(cache)->CopyTarget(*binding);
        };
        s.cache = [](void *c) -> MMResourceCache * {
            return c ? static_cast<PChunkyResourceFile>(c)->CoreCache() : nullptr;
        };
        s.missing_chunk = [](void *) { Bug("chunk not found"); };
        return s;
    }();
    return services;
}

MMTagServices TagManager::_Services(void *context)
{
    auto s = CoreServices();
    s.context = context;
    s.source = [](void *c, MMI32 sid, int local, const MMTag *) -> MMResourceManager * {
        auto h = static_cast<NativeTagHost *>(c);
        auto manager = h->manager->_PcrmSourceGet(sid, local != 0, h->asset);
        return manager ? manager->CoreManager() : nullptr;
    };
    s.missing = [](void *c, const MMTag *) {
#if defined(DEBUG) || defined(STUDIO_TEST_TOOLS)
        auto h = static_cast<NativeTagHost *>(c);
        if (h->manager->_missingTagTest)
            h->manager->_missingTagTest(const_cast<PTAG>(h->asset));
#endif
    };
    s.publish = [](void *c, const MMTag *value) {
        auto h = static_cast<NativeTagHost *>(c);
        if (h->target)
        {
            h->target->sid = value->sid;
            h->target->ctg = value->type;
            h->target->cno = value->number;
            if (value->sid == ksidUseCrf)
                h->target->pcrf = static_cast<PChunkyResourceFile>(value->cache);
        }
    };
    return s;
}
bool TagManager::FBuildChildTag(PTAG parent, ChildChunkID slot, ChunkTagOrType type, PTAG out)
{
    AssertThis(0);
    AssertVarMem(parent);
    Assert(parent->sid >= 0, "Invalid sid");
    AssertVarMem(out);
    TrashVar(out);
    NativeTagHost host = {this, out, parent};
    const auto services = _Services(&host);
    auto input = TagToCore(*parent);
    MMTag child = {};
    return mm_tag_child(&input, slot, type, &child, &services) != 0;
}
bool TagManager::FCacheTagToHD(PTAG tag, bool children)
{
    AssertThis(0);
    AssertVarMem(tag);
    Assert(tag->sid >= 0, "Invalid sid");
    const MMTag value = {tag->sid, nullptr, 0, 0};
    return mm_tag_cache_file(&value, children) != 0;
}
bool TagManager::FCacheTagList(MMTagList *list)
{
    const MMTagListServices services = {
        this,
        [](void *p, const MMTag *value, int children) -> int {
            TAG tag;
            TagFromCore(&tag, *value);
            return static_cast<PTagManager>(p)->FCacheTagToHD(&tag, children != 0);
        },
        [](void *p, const MMTag *value, MMU32 slot, MMU32 type, MMTag *out) -> int {
            TAG parent, child;
            TagFromCore(&parent, *value);
            if (!static_cast<PTagManager>(p)->FBuildChildTag(&parent, slot, type, &child))
                return 0;
            *out = TagToCore(child);
            return 1;
        }};
    return mm_tag_list_cache(list, &services) != 0;
}
PBaseCacheableObject TagManager::PbacoFetch(PTAG tag, ResourceReader reader, bool useMedia)
{
    AssertThis(0);
    AssertVarMem(tag);
    Assert(tag->sid >= 0, "Invalid sid");
    Assert(reader != nullptr, "bad rpo");
    NativeTagHost host = {this, nullptr, tag};
    const auto services = _Services(&host);
    auto value = TagToCore(*tag);
    return static_cast<PBaseCacheableObject>(
        mm_tag_fetch(&value, reader, useMedia, &services));
}
void TagManager::ClearCache(LegacyLong sid, LegacyUlong flags)
{
    AssertThis(0);
    Assert(sid >= 0, "Invalid sid");
    const MMTagCacheServices services = {
        this,
        [](void *c) -> MMU32 { return static_cast<PTagManager>(c)->_sources.count; },
        [](void *c, MMU32 i, MMI32 *sid, MMResourceManager **manager) {
            MMSourceRecord source;
            AssertDo(mm_source_at(&static_cast<PTagManager>(c)->_sources, i, &source) == MM_STORAGE_OK,
                     "invalid source index");
            *sid = source.sid;
            auto native = static_cast<PChunkyResourceManager>(source.manager);
            *manager = native ? native->CoreManager() : nullptr;
        },
        [](void *c, MMI32 sid) -> MMResourceManager * {
            auto manager = static_cast<PTagManager>(c)->_PcrmSourceGet(sid);
            return manager ? manager->CoreManager() : nullptr;
        },
        [](MMResourceCache *cache) { static_cast<PChunkyResourceFile>(cache->owner)->FReopenArchive(); }};
    mm_tag_clear_cache(sid, flags, &services);
}
bool TagManager::FOpenTag(PTAG tag, PChunkyResourceFile destination, PChunkyFile source)
{
    AssertVarMem(tag);
    Assert(tag->sid >= 0, "Invalid sid");
    AssertPo(destination, 0);
    AssertNilOrPo(source, 0);
    NativeTagHost host = {nullptr, tag, tag};
    const auto services = _Services(&host);
    MMTag value = {tag->sid, nullptr, tag->ctg, tag->cno};
    return mm_tag_open(&value, destination, source, &services) != 0;
}
bool TagManager::FSaveTag(PTAG tag, PChunkyResourceFile destination, bool redirect)
{
    AssertVarMem(tag);
    Assert(tag->sid >= 0, "Invalid sid");
    AssertPo(destination, 0);
    NativeTagHost host = {nullptr, tag, tag};
    const auto services = _Services(&host);
    auto value = TagToCore(*tag);
    return mm_tag_save(&value, destination, redirect, &services) != 0;
}
void TagManager::DupTag(PTAG tag)
{
    AssertVarMem(tag);
    Assert(tag->sid >= 0, "Invalid sid");
    const auto services = _Services(nullptr);
    auto value = TagToCore(*tag);
    mm_tag_dup(&value, &services);
}
void TagManager::CloseTag(PTAG tag)
{
    AssertVarMem(tag);
    Assert(tag->sid == ksidInvalid || tag->sid >= 0, "Invalid sid");
    NativeTagHost host = {nullptr, tag, tag};
    const auto services = _Services(&host);
    auto value = TagToCore(*tag);
    mm_tag_close(&value, &services);
}
LegacyUlong TagManager::FcmpCompareTags(PTAG a, PTAG b)
{
    AssertVarMem(a);
    AssertVarMem(b);
    Assert(a->sid >= 0 && b->sid >= 0, "Invalid sid");
    const auto services = _Services(nullptr);
    auto x = TagToCore(*a), y = TagToCore(*b);
    const int order = mm_tag_compare(&x, &y, &services);
    return order < 0 ? fcmpLt : order > 0 ? fcmpGt : fcmpEq;
}

#ifdef DEBUG
/***************************************************************************
    Assert the validity of the TagManager.
***************************************************************************/
void TagManager::AssertValid(LegacyUlong flags)
{
    TagManager_PAR::AssertValid(fobjAllocated);
    Assert(_names.count <= _names.storage.buffers[1].capacity / 8, "bad source names");
    Assert(_pfninscd != nullptr, "bad source callback");
    Assert(_sources.count <= _sources.storage.buffers[0].capacity / sizeof(MMSourceRecord), "bad source count");
    for (MMU32 i = 0; i < _sources.count; ++i)
    {
        MMSourceRecord r;
        AssertDo(mm_source_at(&_sources, i, &r) == MM_STORAGE_OK, "bad source index");
        auto paths = static_cast<NativeSourcePaths *>(r.paths);
        AssertPo(&paths->local, ffniDir | ffniEmpty);
        AssertPo(&paths->media, ffniDir | ffniEmpty);
        AssertNilOrPo(static_cast<PChunkyResourceManager>(r.manager), 0);
    }
}

/***************************************************************************
    Mark memory used by the TagManager.
***************************************************************************/
void TagManager::MarkMem(void)
{
    AssertThis(0);
    TagManager_PAR::MarkMem();
    MarkPv(_sources.storage.buffers[0].data);
    MarkPv(_names.storage.buffers[0].data);
    MarkPv(_names.storage.buffers[1].data);
    for (MMU32 i = 0; i < _sources.count; ++i)
    {
        MMSourceRecord r;
        AssertDo(mm_source_at(&_sources, i, &r) == MM_STORAGE_OK, "bad source index");
        MarkMemObj(static_cast<NativeSourcePaths *>(r.paths));
        MarkMemObj(static_cast<PChunkyResourceManager>(r.manager));
    }
}
#endif // DEBUG

#ifdef DEBUG
/***************************************************************************
    Mark memory used by the TAG.
***************************************************************************/
void TAG::MarkMem(void)
{
    if (sid == ksidUseCrf)
    {
        AssertPo(pcrf, 0);
        MarkMemObj(pcrf);
    }
}
#endif // DEBUG
