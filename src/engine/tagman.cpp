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
    _pgstSource keeps track of these names.  Rather than have one StringTable_GST for
    short names and one for long names, each string in _pgstSource is the
    "merged name", which is the long name followed by a slash character
    (/) followed by the short name, e.g., "3D Movie Maker/3DMOVIE".  The
    SplitString() function splits the merged name into the long and
    short names.

***************************************************************************/
#include "soc_core.h"
PTagManager vptagm;

ASSERTNAME

RTCLASS(TagManager)

const ByteOrderMask kbomSid = 0xc0000000;

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
} // namespace
ThreeMMSourceOwners TagManager::_SourceOwners()
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
ThreeMMSourceServices TagManager::_SourceServices(void *context)
{
    static_assert(static_cast<int>(TagSourceAccess::Legacy) == THREE_MM_SOURCE_LEGACY &&
                      static_cast<int>(TagSourceAccess::Available) == THREE_MM_SOURCE_AVAILABLE &&
                      static_cast<int>(TagSourceAccess::Disabled) == THREE_MM_SOURCE_DISABLED &&
                      static_cast<int>(TagSourceAccess::Missing) == THREE_MM_SOURCE_MISSING,
                  "source access encoding");
    ThreeMMSourceServices s = {};
    s.context = context;
    s.access = [](void *c, ThreeMMI32 sid, void *, int kind) -> int {
        auto h = static_cast<NativeSourceHost *>(c);
        return static_cast<int>(h->manager->SourceDirectory(sid, kind ? h->output : nullptr));
    };
    s.path_empty = [](void *p, int media) -> int {
        auto paths = static_cast<NativeSourcePaths *>(p);
        return (media ? paths->media : paths->local).Ftg() == ftgNil;
    };
    s.clear_local = [](void *p) { static_cast<NativeSourcePaths *>(p)->local.SetNil(); };
    s.build_local = [](void *c, ThreeMMI32 sid, void *p, int *exists) -> int {
        bool v = false;
        const bool ok = static_cast<NativeSourceHost *>(c)->manager->_FBuildFniHD(
            sid, &static_cast<NativeSourcePaths *>(p)->local, &v);
        *exists = v;
        return ok;
    };
    s.archive_source = [](void *c, ThreeMMI32 sid) -> int {
        return static_cast<NativeSourceHost *>(c)->manager->HasArchiveSource(sid);
    };
    s.content = [](void *c, void *p, int *exists) -> int {
        bool v = false;
        const bool ok = static_cast<NativeSourceHost *>(c)->manager->_FDetermineIfContentOnFni(
            &static_cast<NativeSourcePaths *>(p)->local, &v);
        *exists = v;
        return ok;
    };
    s.find_media = [](void *c, ThreeMMI32 sid, void *p, int *changed) -> int {
        bool v = false;
        const bool ok = static_cast<NativeSourceHost *>(c)->manager->_FFindFniCD(
            sid, &static_cast<NativeSourcePaths *>(p)->media, &v);
        *changed = v;
        return ok;
    };
    s.retry = [](void *c, ThreeMMI32 sid) -> int {
        auto h = static_cast<NativeSourceHost *>(c);
        return h->manager->_FRetry(sid, h->asset);
    };
    s.new_manager = [](void *c, ThreeMMI32 sid, void *p, int media) -> void * {
        auto paths = static_cast<NativeSourcePaths *>(p);
        return static_cast<NativeSourceHost *>(c)->manager->_PcrmSourceNew(sid, media ? &paths->media : &paths->local);
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
        if (three_mm_source_init(&ptagm->_sources, &owners, &allocator) != THREE_MM_STORAGE_OK)
            goto LFail;
    }

    ptagm->_pgstSource = StringTable_GST::PgstNew(size(LegacyLong)); // extra data is sid
    if (pvNil == ptagm->_pgstSource)
        goto LFail;

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
        AssertDo(three_mm_source_dispose(&_sources) == THREE_MM_STORAGE_OK, "invalid source disposal");
    ReleasePpo(&_pgstSource);
}

/***************************************************************************
    Split a merged string into its long and short components
***************************************************************************/
void TagManager::SplitString(PString pstnMerged, PString pstnLong, PString pstnShort)
{
    AssertPo(pstnMerged, 0);
    AssertVarMem(pstnLong);
    AssertVarMem(pstnShort);

    achar *pchStart = pstnMerged->Psz();
    achar *pch;

    for (pch = pchStart; *pch != chNil; pch++)
    {
        if (*pch == ChLit('/'))
        {
            pstnLong->SetRgch(pchStart, (pch - pchStart));
            pstnShort->SetSz(pch + 1);
            return;
        }
    }
    // no slash, so set both pstnLong and pstnShort to pstnMerged
    *pstnLong = *pstnMerged;
    *pstnShort = *pstnMerged;
}

/***************************************************************************
    Return source title string table so it can be embedded in documents
***************************************************************************/
PStringTable_GST TagManager::PgstSource(void)
{
    AssertThis(0);
    return _pgstSource;
}

void TagManager::InvalidateSource(LegacyLong sid)
{
    AssertDo(three_mm_source_invalidate(&_sources, sid) == THREE_MM_STORAGE_OK, "invalid source invalidation");
}

/***************************************************************************
    If there is an stn for the given sid in _pgstSource, return the
    location of the stn.
***************************************************************************/
bool TagManager::_FFindSid(LegacyLong sid, LegacyLong *pistn)
{
    AssertThis(0);
    Assert(sid >= 0, "Invalid sid");
    AssertNilOrVarMem(pistn);

    LegacyLong istn;
    LegacyLong sidT;

    for (istn = 0; istn < _pgstSource->IvMac(); istn++)
    {
        _pgstSource->GetExtra(istn, &sidT);
        if (sid == sidT) // it's already in there
        {
            if (pvNil != pistn)
                *pistn = istn;
            return fTrue;
        }
    }
    TrashVar(pistn);
    return fFalse;
}

/***************************************************************************
    Add source title string table entries to tag manager, if it doesn't
    already know them.
***************************************************************************/
bool TagManager::FMergeGstSource(PStringTable_GST pgst, short bo, short osk)
{
    AssertThis(0);
    AssertPo(pgst, 0);
    Assert(size(LegacyLong) == pgst->CbExtra(), "bad pgstSource");

    LegacyLong istn;
    String stn;
    LegacyLong sid;

    for (istn = 0; istn < pgst->IvMac(); istn++)
    {
        pgst->GetStn(istn, &stn);
        pgst->GetExtra(istn, &sid);
        if (kboOther == bo)
            SwapBytesBom(&sid, kbomSid);
        if (!FAddStnSource(&stn, sid))
            return fFalse;
    }

    return fTrue;
}

/***************************************************************************
    Add source title string to tag manager, if it's not already there
***************************************************************************/
bool TagManager::FAddStnSource(PString pstn, LegacyLong sid)
{
    AssertThis(0);
    AssertPo(pstn, 0);
    Assert(sid >= 0, "Invalid sid");

    if (_FFindSid(sid))
        return fTrue; // String is already there

    return _pgstSource->FAddStn(pstn, &sid); // Try to add it
}

/***************************************************************************
    Find the sid with the given string as its source name.  pstn can be
    the merged name, the short name, or the long name.
***************************************************************************/
bool TagManager::FGetSid(PString pstn, LegacyLong *psid)
{
    AssertThis(0);
    AssertPo(pstn, 0);
    AssertVarMem(psid);

    LegacyLong istn;
    String stnMerged;
    String stnLong;
    String stnShort;

    for (istn = 0; istn < _pgstSource->IvMac(); istn++)
    {
        _pgstSource->GetStn(istn, &stnMerged);
        if (stnMerged.FEqualUser(pstn, fstnIgnoreCase))
        {
            _pgstSource->GetExtra(istn, psid);
            return fTrue;
        }
        SplitString(&stnMerged, &stnLong, &stnShort);
        if (stnLong.FEqualUser(pstn, fstnIgnoreCase) || stnShort.FEqualUser(pstn, fstnIgnoreCase))
        {
            _pgstSource->GetExtra(istn, psid);
            return fTrue;
        }
    }
    return fFalse;
}

/***************************************************************************
    Find the string of the source with the given sid
***************************************************************************/
bool TagManager::_FGetStnMergedOfSid(LegacyLong sid, PString pstn)
{
    AssertThis(0);
    AssertVarMem(pstn);

    LegacyLong istn;
    LegacyLong sidT;

    for (istn = 0; istn < _pgstSource->IvMac(); istn++)
    {
        _pgstSource->GetStn(istn, pstn);
        _pgstSource->GetExtra(istn, &sidT);
        if (sid == sidT)
            return fTrue;
    }
    return fFalse;
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
    if (!_sourceResolver)
        return TagSourceAccess::Legacy;
    String longName, shortName;
    Filename unused;
    if (!_FGetStnSplitOfSid(sid, &longName, &shortName))
        return TagSourceAccess::Legacy;
    return _sourceResolver(&longName, &shortName, path ? path : &unused);
}

bool TagManager::_FBuildFniHD(LegacyLong sid, PFilename pfniHD, bool *pfExists)
{
    AssertThis(0);
    Assert(sid >= 0, "Invalid sid");
    AssertVarMem(pfniHD);
    AssertVarMem(pfExists);

    String stnLong;
    String stnShort;
    Filename fni;

    *pfExists = fFalse;
    const auto access = SourceDirectory(sid, pfniHD);
    if (access != TagSourceAccess::Legacy)
    {
        *pfExists = access == TagSourceAccess::Available;
        return fTrue;
    }
    if (!_FGetStnSplitOfSid(sid, &stnLong, &stnShort))
    {
        return fFalse; // can't even determine if fniHD exists or not
    }

    fni = _fniHDRoot;
    if (!fni.FDownDir(&stnLong, ffniMoveToDir) && !fni.FDownDir(&stnShort, ffniMoveToDir))
    {
        return fTrue; // fniHD doesn't exist
    }
    *pfniHD = fni;
    *pfExists = fTrue;
    return fTrue;
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
    const auto access = SourceDirectory(sid, pfniCD);
    if (access != TagSourceAccess::Legacy)
    {
        *pfFniChanged = access == TagSourceAccess::Available;
        return access == TagSourceAccess::Available;
    }
    AssertThis(0);
    Assert(sid >= 0, "Invalid sid");
    AssertPo(pfniCD, ffniEmpty | ffniDir); // could be a blank Filename
    AssertVarMem(pfFniChanged);

    FileNameEnumerator fne;
    Filename fni;
    String stnLong;
    String stnShort;
    Filename fniCD;

    *pfFniChanged = fFalse;

    // If pfniCD's ftg is not ftgNil, we've opened this source before,
    // so look for it where it was last time.
    if (ftgNil != pfniCD->Ftg())
    {
        if (_FEnsureFniCD(sid, pfniCD))
        {
            // The source is where we thought it was
            return fTrue;
        }
        else
        {
            // With the way the ChunkyResourceManager stuff works now, the ChunkyResourceManager can't
            // move to another path.  So fail if the CD isn't exactly
            // where it was before.
            return fFalse;
        }
    }

    if (!_FGetStnSplitOfSid(sid, &stnLong, &stnShort))
        return fFalse;
    // The source has moved/disappeared, or we're opening it for the first
    // time, so search all drives for it.
    if (!fne.FInit(pvNil, pvNil, 0))
        return fFalse;
    while (fne.FNextFni(&fni))
    {
        if (fni.Grfvk() & fvkFloppy) // don't buzz floppies
            continue;
        fniCD = fni;
        if (!_FEnsureFniCD(sid, &fniCD, &stnShort) && !_FEnsureFniCD(sid, &fniCD, &stnLong))
        {
            continue;
        }
        *pfFniChanged = fTrue;
        *pfniCD = fniCD;
        return fTrue;
    }
    // Couldn't find the source
    return fFalse;
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
        Bug("_pgstSource has no string for this sid!");
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
PChunkyResourceManager TagManager::_PcrmSourceNew(LegacyLong sid, PFilename directory)
{
    AssertThis(0);
    Assert(sid >= 0, "Invalid sid");
    AssertPo(directory, ffniDir);
    struct Host
    {
        PTagManager manager;
        PFilename directory;
        Filename current;
        FileNameEnumerator files;
        FileType type = kftgContent;
    } host = {this, directory};
    const ThreeMMSourceBuildServices services = {
        &host,
        [](void *) -> void * { return ChunkyResourceManager::PcrmNew(0); },
        [](void *c, ThreeMMI32 sid) -> int { return static_cast<Host *>(c)->manager->HasArchiveSource(sid); },
        [](void *c) -> int {
            auto h = static_cast<Host *>(c);
            return h->files.FInit(h->directory, &h->type, 1);
        },
        [](void *c, ThreeMMI32 sid, ThreeMMU32 i, int archive) -> int {
            auto h = static_cast<Host *>(c);
            return archive ? h->manager->SourceArchive(sid, h->type, i, &h->current) : h->files.FNextFni(&h->current);
        },
        [](void *c) -> void * { return ChunkyFile::PcflOpen(&static_cast<Host *>(c)->current, fcflNil); },
        [](void *m, void *f, ThreeMMI32 budget) -> int {
            return static_cast<PChunkyResourceManager>(m)->FAddCfl(static_cast<PChunkyFile>(f), budget);
        },
        [](void *f) { static_cast<PChunkyFile>(f)->Release(); },
        [](void *m) { static_cast<PChunkyResourceManager>(m)->Release(); }};
    return static_cast<PChunkyResourceManager>(three_mm_source_build(sid, _cbCache, &services));
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
    return static_cast<PChunkyResourceManager>(three_mm_source_manager(&_sources, sid, localOnly, &paths, &services));
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
    const bool ok = three_mm_source_local_content(&_sources, sid, &paths, &value, &services) != 0;
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
    return three_mm_source_local_path(&_sources, sid, &paths, &services) != 0;
}

/***************************************************************************
    Get the Filename for the CD directory
***************************************************************************/
bool TagManager::_FGetFniCD(LegacyLong sid, PFilename output, bool ask)
{
    NativeSourceHost host = {this, output, nullptr, nullptr};
    NativeSourcePaths paths;
    const auto services = _SourceServices(&host);
    return three_mm_source_media_path(&_sources, sid, ask, &paths, &services) != 0;
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
    return three_mm_source_find_file(&_sources, sid, ask, &paths, &services) != 0;
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
ThreeMMTagServices TagManager::_Services(void *context)
{
    ThreeMMTagServices s = {};
    s.context = context;
    s.file = [](void *c) -> void * { return static_cast<PChunkyResourceFile>(c)->Pcfl(); };
    s.retain = [](void *c) { static_cast<PChunkyResourceFile>(c)->AddRef(); };
    s.release = [](void *c) { static_cast<PChunkyResourceFile>(c)->Release(); };
    s.compare_cache = [](void *a, void *b) -> int {
        auto x = static_cast<PChunkyResourceFile>(a), y = static_cast<PChunkyResourceFile>(b);
        return x == y ? 0 : x < y ? -1 : 1;
    };
    s.copy = [](void *a, ThreeMMU32 type, ThreeMMU32 number, void *b, ThreeMMU32 *out) -> int {
        return static_cast<PChunkyFile>(a)->FCopy(type, number, static_cast<PChunkyFile>(b), out);
    };
    s.source = [](void *c, ThreeMMI32 sid, int local, const ThreeMMTag *) -> void * {
        auto h = static_cast<NativeTagHost *>(c);
        return h->manager->_PcrmSourceGet(sid, local != 0, h->asset);
    };
    s.find_cache = [](void *m, ThreeMMU32 type, ThreeMMU32 number) -> void * {
        return static_cast<PChunkyResourceManager>(m)->PcrfFindChunk(type, number);
    };
    s.child = [](void *c, const ThreeMMTag *parent, ThreeMMU32 slot, ThreeMMU32 type, ThreeMMU32 *number) -> int {
        ChildChunkIdentification kid;
        if (!static_cast<PChunkyResourceFile>(c)->Pcfl()->FGetKidChidCtg(parent->type, parent->number, slot, type,
                                                                         &kid))
            return 0;
        *number = kid.cki.cno;
        return 1;
    };
    s.fetch = [](void *c, int manager, const ThreeMMTag *tag, ThreeMMResourceReader reader) -> void * {
        auto r = reinterpret_cast<PFNRPO>(reader);
        return manager ? static_cast<PChunkyResourceManager>(c)->PbacoFetch(tag->type, tag->number, r)
                       : static_cast<PChunkyResourceFile>(c)->PbacoFetch(tag->type, tag->number, r);
    };
    s.missing = [](void *c, const ThreeMMTag *) {
#if defined(DEBUG) || defined(STUDIO_TEST_TOOLS)
        auto h = static_cast<NativeTagHost *>(c);
        if (h->manager->_missingTagTest)
            h->manager->_missingTagTest(const_cast<PTAG>(h->asset));
#endif
    };
    s.publish = [](void *c, const ThreeMMTag *value) {
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
    ThreeMMTag child = {};
    return three_mm_tag_child(&input, slot, type, &child, &services) != 0;
}
bool TagManager::FCacheTagToHD(PTAG tag, bool children)
{
    AssertThis(0);
    AssertVarMem(tag);
    Assert(tag->sid >= 0, "Invalid sid");
    const ThreeMMTag value = {tag->sid, nullptr, 0, 0};
    return three_mm_tag_cache_file(&value, children) != 0;
}
PBaseCacheableObject TagManager::PbacoFetch(PTAG tag, PFNRPO reader, bool useMedia)
{
    AssertThis(0);
    AssertVarMem(tag);
    Assert(tag->sid >= 0, "Invalid sid");
    Assert(reader != nullptr, "bad rpo");
    NativeTagHost host = {this, nullptr, tag};
    const auto services = _Services(&host);
    auto value = TagToCore(*tag);
    return static_cast<PBaseCacheableObject>(
        three_mm_tag_fetch(&value, reinterpret_cast<ThreeMMResourceReader>(reader), useMedia, &services));
}
void TagManager::ClearCache(LegacyLong sid, LegacyUlong flags)
{
    AssertThis(0);
    Assert(sid >= 0, "Invalid sid");
    const ThreeMMTagCacheServices services = {
        this,
        [](void *c) -> ThreeMMU32 { return static_cast<PTagManager>(c)->_sources.count; },
        [](void *c, ThreeMMU32 i, ThreeMMI32 *sid, void **manager) {
            ThreeMMSourceRecord source;
            AssertDo(three_mm_source_at(&static_cast<PTagManager>(c)->_sources, i, &source) == THREE_MM_STORAGE_OK,
                     "invalid source index");
            *sid = source.sid;
            *manager = source.manager;
        },
        [](void *c, ThreeMMI32 sid) -> void * { return static_cast<PTagManager>(c)->_PcrmSourceGet(sid); },
        [](void *m) -> ThreeMMU32 { return static_cast<PChunkyResourceManager>(m)->Ccrf(); },
        [](void *m, ThreeMMU32 i) -> void * { return static_cast<PChunkyResourceManager>(m)->PcrfGet(i); },
        [](void *c) -> void * { return static_cast<PChunkyResourceFile>(c)->Pcfl(); },
        [](void *f) { static_cast<PChunkyFile>(f)->FReopen(); },
        [](void *c) -> ThreeMMI32 { return static_cast<PChunkyResourceFile>(c)->CbMax(); },
        [](void *c, ThreeMMI32 budget) { static_cast<PChunkyResourceFile>(c)->SetCbMax(budget); }};
    three_mm_tag_clear_cache(sid, flags, &services);
}
bool TagManager::FOpenTag(PTAG tag, PChunkyResourceFile destination, PChunkyFile source)
{
    AssertVarMem(tag);
    Assert(tag->sid >= 0, "Invalid sid");
    AssertPo(destination, 0);
    AssertNilOrPo(source, 0);
    NativeTagHost host = {nullptr, tag, tag};
    const auto services = _Services(&host);
    ThreeMMTag value = {tag->sid, nullptr, tag->ctg, tag->cno};
    return three_mm_tag_open(&value, destination, source, &services) != 0;
}
bool TagManager::FSaveTag(PTAG tag, PChunkyResourceFile destination, bool redirect)
{
    AssertVarMem(tag);
    Assert(tag->sid >= 0, "Invalid sid");
    AssertPo(destination, 0);
    NativeTagHost host = {nullptr, tag, tag};
    const auto services = _Services(&host);
    auto value = TagToCore(*tag);
    return three_mm_tag_save(&value, destination, redirect, &services) != 0;
}
void TagManager::DupTag(PTAG tag)
{
    AssertVarMem(tag);
    Assert(tag->sid >= 0, "Invalid sid");
    const auto services = _Services(nullptr);
    auto value = TagToCore(*tag);
    three_mm_tag_dup(&value, &services);
}
void TagManager::CloseTag(PTAG tag)
{
    AssertVarMem(tag);
    Assert(tag->sid == ksidInvalid || tag->sid >= 0, "Invalid sid");
    NativeTagHost host = {nullptr, tag, tag};
    const auto services = _Services(&host);
    auto value = TagToCore(*tag);
    three_mm_tag_close(&value, &services);
}
LegacyUlong TagManager::FcmpCompareTags(PTAG a, PTAG b)
{
    AssertVarMem(a);
    AssertVarMem(b);
    Assert(a->sid >= 0 && b->sid >= 0, "Invalid sid");
    const auto services = _Services(nullptr);
    auto x = TagToCore(*a), y = TagToCore(*b);
    const int order = three_mm_tag_compare(&x, &y, &services);
    return order < 0 ? fcmpLt : order > 0 ? fcmpGt : fcmpEq;
}

#ifdef DEBUG
/***************************************************************************
    Assert the validity of the TagManager.
***************************************************************************/
void TagManager::AssertValid(LegacyUlong flags)
{
    TagManager_PAR::AssertValid(fobjAllocated);
    AssertPo(_pgstSource, 0);
    Assert(_pfninscd != nullptr, "bad source callback");
    Assert(_sources.count <= _sources.storage.buffers[0].capacity / sizeof(ThreeMMSourceRecord), "bad source count");
    for (ThreeMMU32 i = 0; i < _sources.count; ++i)
    {
        ThreeMMSourceRecord r;
        AssertDo(three_mm_source_at(&_sources, i, &r) == THREE_MM_STORAGE_OK, "bad source index");
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
    MarkMemObj(_pgstSource);
    for (ThreeMMU32 i = 0; i < _sources.count; ++i)
    {
        ThreeMMSourceRecord r;
        AssertDo(three_mm_source_at(&_sources, i, &r) == THREE_MM_STORAGE_OK, "bad source index");
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
