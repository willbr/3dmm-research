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

    For each source, TagManager maintains (in an SourceFileStructure) a ChunkyResourceManager (Chunky Resource
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

// Source File Structure...keeps track of known sources and caches
struct SourceFileStructure
{
  public:
    LegacyLong sid;             // ID for this source
    Filename fniHD;            // Filename of the HD directory
    Filename fniCD;            // Filename of the CD directory
    PChunkyResourceManager pcrmSource;      // ChunkyResourceManager of files on the CD (or possibly HD)
    tribool tContentOnHD; // Is the content on the HD or CD?

  public:
    void Clear(void) // Zeros out an SourceFileStructure
    {
        sid = ksidInvalid;
        fniHD.SetNil();
        fniCD.SetNil();
        pcrmSource = pvNil;
        tContentOnHD = tMaybe;
    }
};

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

    ptagm->_pglsfs = DynamicArray::PglNew(size(SourceFileStructure));
    if (pvNil == ptagm->_pglsfs)
        goto LFail;

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

    LegacyLong isfs;
    SourceFileStructure sfs;

    if (pvNil != _pglsfs)
    {
        for (isfs = 0; isfs < _pglsfs->IvMac(); isfs++)
        {
            _pglsfs->Get(isfs, &sfs);
            ReleasePpo(&sfs.pcrmSource);
        }
        ReleasePpo(&_pglsfs);
    }
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
    for (LegacyLong i = _pglsfs->IvMac(); i-- > 0;)
    {
        SourceFileStructure source;
        _pglsfs->Get(i, &source);
        if (source.sid != sid)
            continue;
        ReleasePpo(&source.pcrmSource);
        _pglsfs->Delete(i);
    }
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
PChunkyResourceManager TagManager::_PcrmSourceNew(LegacyLong sid, PFilename pfniDir)
{
    AssertThis(0);
    Assert(sid >= 0, "Invalid sid");
    AssertPo(pfniDir, ffniDir);

    String stn;
    Filename fni;
    PChunkyResourceManager pcrmSource = pvNil;
    FileNameEnumerator fne;
    FileType ftgChk = kftgContent;
    PChunkyFile pcfl = pvNil;

    pcrmSource = ChunkyResourceManager::PcrmNew(0);
    if (pvNil == pcrmSource)
        goto LFail;

    if (HasArchiveSource(sid))
    {
        for (LegacyLong index = 0; SourceArchive(sid, ftgChk, index, &fni); ++index)
        {
            pcfl = ChunkyFile::PcflOpen(&fni, fcflNil);
            if (!pcfl || !pcrmSource->FAddCfl(pcfl, _cbCache))
                goto LFail;
            ReleasePpo(&pcfl);
        }
        return pcrmSource;
    }
    // Add all chunky files in content directory to pcrmSource
    if (!fne.FInit(pfniDir, &ftgChk, 1))
        goto LFail;
    while (fne.FNextFni(&fni))
    {
        pcfl = ChunkyFile::PcflOpen(&fni, fcflNil);
        if (pvNil == pcfl)
            goto LFail;
        if (!pcrmSource->FAddCfl(pcfl, _cbCache))
            goto LFail;
        ReleasePpo(&pcfl);
    }
    return pcrmSource;
LFail:
    ReleasePpo(&pcfl);
    ReleasePpo(&pcrmSource);
    return pvNil;
}

/***************************************************************************
    Returns the source ChunkyResourceManager for the given sid, creating (and remembering) a
    new one if there isn't one already.  It verifies that the CD is still
    in the drive, unless fDontHitCD is fTrue.
***************************************************************************/
PChunkyResourceManager TagManager::_PcrmSourceGet(LegacyLong sid, bool fDontHitCD, const TAG *asset)
{
    const auto access = SourceDirectory(sid);
    if (access == TagSourceAccess::Disabled || access == TagSourceAccess::Missing)
    {
        if (!fDontHitCD)
            _FRetry(sid, asset);
        return pvNil;
    }
    AssertThis(0);
    Assert(sid >= 0, "Invalid sid");

    LegacyLong isfs;
    SourceFileStructure sfs;
    bool fContentOnFni;
    bool fFniChanged;
    bool fExists;

    for (isfs = 0; isfs < _pglsfs->IvMac(); isfs++)
    {
        _pglsfs->Get(isfs, &sfs);
        if (sid == sfs.sid)
            goto LSetupSfs;
    }
    // SourceFileStructure for this sid doesn't exist in _pglsfs, so make one
    sfs.Clear();
    sfs.sid = sid;
    if (!_pglsfs->FAdd(&sfs, &isfs))
        return pvNil;
LSetupSfs:
    if (sfs.tContentOnHD == tMaybe)
    {
        if (sfs.fniHD.Ftg() == ftgNil)
        {
            if (!_FBuildFniHD(sid, &sfs.fniHD, &fExists))
                return pvNil;
            if (!fExists)
                sfs.tContentOnHD = tNo;
            _pglsfs->Put(isfs, &sfs);
        }
        if (sfs.tContentOnHD == tMaybe)
        {
            if (HasArchiveSource(sid))
                fContentOnFni = true;
            else if (!_FDetermineIfContentOnFni(&sfs.fniHD, &fContentOnFni))
                return pvNil;
            sfs.tContentOnHD = (fContentOnFni ? tYes : tNo);
            _pglsfs->Put(isfs, &sfs);
        }
    }
    if (tYes == sfs.tContentOnHD)
    {
        if (pvNil == sfs.pcrmSource)
        {
            sfs.pcrmSource = _PcrmSourceNew(sid, &sfs.fniHD);
            if (pvNil == sfs.pcrmSource)
                return pvNil;
            _pglsfs->Put(isfs, &sfs);
        }
        return sfs.pcrmSource;
    }

    // Else content is not on HD, so look at CD
    if (!fDontHitCD)
    {
        // Verify that CD is where we thought it was, or find
        // it if we haven't found it before
        while (!_FFindFniCD(sid, &sfs.fniCD, &fFniChanged))
        {
            // Ask user to insert the CD
            if (!_FRetry(sid, asset))
                return pvNil;
            // A newly installed local source must invalidate the cached HD
            // miss too. An already-open CD manager retains its original path.
            if (sfs.pcrmSource == pvNil)
            {
                sfs.tContentOnHD = tMaybe;
                sfs.fniHD.SetNil();
                goto LSetupSfs;
            }
        }
        if (fFniChanged)
        {
            Assert(sfs.pcrmSource == pvNil, "fniCD can't change once pcrm is opened!");
            _pglsfs->Put(isfs, &sfs); // update sfs.fniCD
        }
    }
    if (pvNil == sfs.pcrmSource)
    {
        if (fDontHitCD)
        {
            // A document or portfolio preview may refer to an expansion
            // that is not installed. A local-only lookup is allowed to
            // miss, even when no source manager has been opened yet.
            return pvNil;
        }
        else
        {
            sfs.pcrmSource = _PcrmSourceNew(sid, &sfs.fniCD);
            if (pvNil == sfs.pcrmSource)
                return pvNil;
            _pglsfs->Put(isfs, &sfs);
        }
    }
    return sfs.pcrmSource;
}

/***************************************************************************
    Determines whether source is on HD (if it is, don't cache its stuff to
    HD!)  Note that the function return value is whether the function
    completed without error, not whether the source is on HD.
***************************************************************************/
bool TagManager::_FDetermineIfSourceHD(LegacyLong sid, bool *pfIsOnHD)
{
    AssertThis(0);
    Assert(sid >= 0, "Invalid sid");
    AssertVarMem(pfIsOnHD);

    LegacyLong isfs;
    SourceFileStructure sfs;
    bool fContentOnFni;
    bool fExists;

    for (isfs = 0; isfs < _pglsfs->IvMac(); isfs++)
    {
        _pglsfs->Get(isfs, &sfs);
        if (sid == sfs.sid)
            goto LSetupSfs;
    }
    // SourceFileStructure for this sid doesn't exist in _pglsfs, so make one
    sfs.Clear();
    sfs.sid = sid;
    if (!_pglsfs->FAdd(&sfs, &isfs))
        return fFalse;
LSetupSfs:
    if (sfs.tContentOnHD == tMaybe)
    {
        if (sfs.fniHD.Ftg() == ftgNil)
        {
            if (!_FBuildFniHD(sid, &sfs.fniHD, &fExists))
                return fFalse;
            if (!fExists)
                sfs.tContentOnHD = tNo;
            _pglsfs->Put(isfs, &sfs);
        }
        if (sfs.tContentOnHD == tMaybe)
        {
            if (HasArchiveSource(sid))
                fContentOnFni = true;
            else if (!_FDetermineIfContentOnFni(&sfs.fniHD, &fContentOnFni))
                return fFalse;
            sfs.tContentOnHD = (fContentOnFni ? tYes : tNo);
            _pglsfs->Put(isfs, &sfs);
        }
    }
    *pfIsOnHD = (sfs.tContentOnHD == tYes);
    return fTrue;
}

/***************************************************************************
    Get the Filename for the HD directory
***************************************************************************/
bool TagManager::_FGetFniHD(LegacyLong sid, PFilename pfniHD)
{
    const auto access = SourceDirectory(sid, pfniHD);
    if (access != TagSourceAccess::Legacy)
        return access == TagSourceAccess::Available;
    AssertThis(0);
    AssertVarMem(pfniHD);

    LegacyLong isfs;
    SourceFileStructure sfs;
    bool fExists;

    for (isfs = 0; isfs < _pglsfs->IvMac(); isfs++)
    {
        _pglsfs->Get(isfs, &sfs);
        if (sid == sfs.sid)
            goto LSetupSFS;
    }
    // SourceFileStructure for this sid doesn't exist in _pglsfs, so make one
    sfs.Clear();
    sfs.sid = sid;
    if (!_pglsfs->FAdd(&sfs, &isfs))
        return fFalse;
LSetupSFS:
    if (sfs.fniHD.Ftg() == ftgNil)
    {
        if (!_FBuildFniHD(sid, &sfs.fniHD, &fExists) || !fExists)
            return fFalse;
    }
    _pglsfs->Put(isfs, &sfs);
    *pfniHD = sfs.fniHD;
    return fTrue;
}

/***************************************************************************
    Get the Filename for the CD directory
***************************************************************************/
bool TagManager::_FGetFniCD(LegacyLong sid, PFilename pfniCD, bool fAskForCD)
{
    const auto access = SourceDirectory(sid, pfniCD);
    if (access != TagSourceAccess::Legacy)
    {
        if (access != TagSourceAccess::Available && fAskForCD)
            _FRetry(sid);
        return access == TagSourceAccess::Available;
    }
    AssertThis(0);
    AssertVarMem(pfniCD);

    LegacyLong isfs;
    SourceFileStructure sfs;
    bool fFniChanged;

    for (isfs = 0; isfs < _pglsfs->IvMac(); isfs++)
    {
        _pglsfs->Get(isfs, &sfs);
        if (sid == sfs.sid)
            goto LSetupSFS;
    }
    // SourceFileStructure for this sid doesn't exist in _pglsfs, so make one
    sfs.Clear();
    sfs.sid = sid;
    if (!_pglsfs->FAdd(&sfs, &isfs))
        return fFalse;
LSetupSFS:
    while (!_FFindFniCD(sid, &sfs.fniCD, &fFniChanged))
    {
        if (!fAskForCD)
            return fFalse;
        if (!_FRetry(sid))
            return fFalse;
    }
    if (fFniChanged)
        _pglsfs->Put(isfs, &sfs);
    *pfniCD = sfs.fniCD;
    return fTrue;
}

/***************************************************************************
    Finds the file with name pstn on the HD or CD.
***************************************************************************/
bool TagManager::FFindFile(LegacyLong sid, PString pstn, PFilename pfni, bool fAskForCD)
{
    AssertThis(0);
    Assert(sid >= 0, "Invalid sid");
    AssertPo(pstn, 0);
    AssertVarMem(pfni);

    FileType ftg;

    if (!pfni->FBuildFromPath(pstn))
        return fFalse;
    ftg = pfni->Ftg();

    // First, look on the HD
    if (!_FGetFniHD(sid, pfni))
        return fFalse;
    if (pfni->FSetLeaf(pstn, ftg) && pfni->FResolveCase())
        return fTrue;

    // Now look on the CD, asking for it if fAskForCD
    if (!_FGetFniCD(sid, pfni, fAskForCD))
        return fFalse;
    if (pfni->FSetLeaf(pstn, ftg) && pfni->FResolveCase())
        return fTrue;

    pfni->SetNil();
    return fFalse; // file not found
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
        [](void *c) -> ThreeMMU32 { return static_cast<PTagManager>(c)->_pglsfs->IvMac(); },
        [](void *c, ThreeMMU32 i, ThreeMMI32 *sid, void **manager) {
            SourceFileStructure source;
            static_cast<PTagManager>(c)->_pglsfs->Get(i, &source);
            *sid = source.sid;
            *manager = source.pcrmSource;
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
void TagManager::AssertValid(LegacyUlong grf)
{
    LegacyLong isfs;
    SourceFileStructure sfs;

    TagManager_PAR::AssertValid(fobjAllocated);
    AssertPo(_pglsfs, 0);
    AssertPo(_pgstSource, 0);
    Assert(pvNil != _pfninscd, "bad _pfninscd");

    for (isfs = 0; isfs < _pglsfs->IvMac(); isfs++)
    {
        _pglsfs->Get(isfs, &sfs);
        AssertPo(&sfs.fniHD, ffniDir | ffniEmpty);
        AssertPo(&sfs.fniCD, ffniDir | ffniEmpty);
        AssertNilOrPo(sfs.pcrmSource, 0);
    }
}

/***************************************************************************
    Mark memory used by the TagManager.
***************************************************************************/
void TagManager::MarkMem(void)
{
    AssertThis(0);

    LegacyLong isfs;
    SourceFileStructure sfs;

    TagManager_PAR::MarkMem();
    MarkMemObj(_pglsfs);
    MarkMemObj(_pgstSource);
    for (isfs = 0; isfs < _pglsfs->IvMac(); isfs++)
    {
        _pglsfs->Get(isfs, &sfs);
        MarkMemObj(sfs.pcrmSource);
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
