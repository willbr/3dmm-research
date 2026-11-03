/* Copyright (c) Microsoft Corporation.
   Licensed under the MIT License. */

/***************************************************************************
    Author: ShonK
    Project: Kauai
    Reviewed:
    Copyright (c) Microsoft Corporation

    Shared filename navigation and enumeration over native path services.

***************************************************************************/
#include "util.h"
ASSERTNAME

// This is the FileType to use for temp files - clients may set this to whatever
// they want.
FileType vftgTemp = kftgTemp;

// maximal number of short characters in an extension is 4 (so it fits in
// a long).
const LegacyLong kcchsMaxExt = 4;

priv void _CleanFtg(FileType *pftg, PString pstnExt = pvNil);
Filename _fniTemp;

RTCLASS(Filename)
RTCLASS(FileNameEnumerator)

/***************************************************************************
    Sets the fni to nil values.
***************************************************************************/
void Filename::SetNil(void)
{
    _ftg = ftgNil;
    _stnFile.SetNil();
    AssertThis(ffniEmpty);
}

/***************************************************************************
    Constructor for fni class.
***************************************************************************/
Filename::Filename(void)
{
    SetNil();
}

/***************************************************************************
    Builds the fni from the path.
***************************************************************************/
bool Filename::FBuildFromPath(PString pstn, FileType ftgDef)
{
    AssertThis(0);
    AssertPo(pstn, 0);

    LegacyLong cch;
    achar *pchT;
    ZString sz;

    if (kftgDir != ftgDef)
    {
        // if the path ends with a slash or only has periods after the last
        // slash, force the fni to be a directory.

        cch = pstn->Cch();
        for (;;)
        {
            if (cch == 0 || KauaiFiles::IsSeparator(pstn->Prgch()[cch - 1]))
            {
                ftgDef = kftgDir;
                break;
            }
            if (pstn->Prgch()[--cch] != ChLit('.'))
                break;
        }
    }

    // Resolve '.' and '..' lexically, including names not yet on disk.
    if ((cch = KauaiFiles::FullPath(pstn->Psz(), sz, kcchMaxSz, &pchT)) == 0 || cch > kcchMaxSz)
    {
        goto LFail;
    }
    Assert(cch <= kcchMaxSz, 0);
    _stnFile = sz;

    if (ftgDef == kftgDir)
    {
        achar ch = _stnFile.Prgch()[_stnFile.Cch() - 1];
        if (!KauaiFiles::IsSeparator(ch))
        {
            if (!_stnFile.FAppendCh(KauaiFiles::Separator()))
            {
                goto LFail;
            }
        }
        _ftg = kftgDir;
    }
    else
    {
        _SetFtgFromName();
        if (_ftg == 0 && ftgDef != ftgNil && pstn->Prgch()[pstn->Cch() - 1] != ChLit('.') && !FChangeFtg(ftgDef))
        {
        LFail:
            SetNil();
            PushErc(ercFniGeneral);
            return fFalse;
        }
    }

    AssertThis(ffniFile | ffniDir);
    return fTrue;
}

/******************************************************************************
    Will attempt to build an Filename with the given filename.  Uses the
    Native path-search rules (SearchPath on Windows, PATH-style on POSIX).

    Arguments:
        PString pstn ** the filename to look for

    Returns: fTrue if it could find the file
******************************************************************************/
bool Filename::FSearchInPath(PString pstn, PString pstnEnv)
{
    AssertThis(0);
    AssertPo(pstn, 0);
    AssertNilOrPo(pstnEnv, 0);

    LegacyLong cch;
    ZString sz;
    achar *pchT;
    PZString psz = (pstnEnv == pvNil) ? pvNil : pstnEnv->Psz();

    if ((cch = KauaiFiles::FindInPath(pstn->Psz(), psz, sz, kcchMaxSz)) == 0 || cch > kcchMaxSz)
    {
        SetNil();
        PushErc(ercFniGeneral);
        return fFalse;
    }

    Assert(cch <= kcchMaxSz, 0);
    _stnFile = sz;
    _SetFtgFromName();

    AssertThis(ffniFile | ffniDir);
    return fTrue;
}

/***************************************************************************
    Get a unique filename in the directory currently indicated by the fni.
***************************************************************************/
bool Filename::FGetUnique(FileType ftg)
{
    AssertThis(ffniFile | ffniDir);
    static short _dsw = 0;
    String stn;
    String stnOld;
    short sw;
    LegacyLong cact;

    if (Ftg() == kftgDir)
        stnOld.SetNil();
    else
        GetLeaf(&stnOld);

    sw = (short)TsCurrentSystem() + ++_dsw;
    for (cact = 20; cact != 0; cact--, sw += ++_dsw)
    {
        stn.FFormatSz(PszLit("Temp%04x"), (LegacyLong)sw);
        if (stn.FEqual(&stnOld))
            continue;
        if (FSetLeaf(&stn, ftg) && TExists() == tNo)
            return fTrue;
    }
    SetNil();
    PushErc(ercFniGeneral);
    return fFalse;
}

/***************************************************************************
    Get a temporary fni.
***************************************************************************/
bool Filename::FGetTemp(void)
{
    AssertThis(0);

    if (_fniTemp._ftg != kftgDir)
    {
        // get the temp directory
        ZString sz;

        if (KauaiFiles::TempPath(sz, kcchMaxSz) == 0)
        {
            PushErc(ercFniGeneral);
            return fFalse;
        }
        _fniTemp._stnFile = sz;
        _fniTemp._ftg = kftgDir;
        AssertPo(&_fniTemp, ffniDir);
    }
    *this = _fniTemp;
    return FGetUnique(vftgTemp);
}

/***************************************************************************
    Return the file type of the fni.
***************************************************************************/
FileType Filename::Ftg(void)
{
    AssertThis(0);
    return _ftg;
}

/***************************************************************************
    Return the volume kind for the given fni.
***************************************************************************/
LegacyUlong Filename::Grfvk(void)
{
    AssertThis(ffniDir | ffniFile);
    return KauaiFiles::VolumeKinds(_stnFile.Psz());
}

/***************************************************************************
    Set the leaf to the given string and type.
***************************************************************************/
bool Filename::FSetLeaf(PString pstn, FileType ftg)
{
    AssertThis(ffniFile | ffniDir);
    AssertNilOrPo(pstn, 0);

    _CleanFtg(&ftg);
    Assert(FPure(ftg == kftgDir) == FPure(pstn == pvNil || pstn->Cch() == 0), "ftg doesn't match pstn");
    if (!_FChangeLeaf(pstn))
        goto LFail;

    if ((kftgDir != ftg) && (ftgNil != ftg) && !FChangeFtg(ftg))
        goto LFail;

    AssertThis(ffniFile | ffniDir);
    return fTrue;

LFail:
    SetNil();
    PushErc(ercFniGeneral);
    return fFalse;
}

/******************************************************************************
    Changes just the FileType of the Filename, leaving the file path and filename alone
    (but does change the extension). Returns: fTrue if it succeeds
******************************************************************************/
bool Filename::FChangeFtg(FileType ftg)
{
    AssertThis(ffniFile);
    Assert(ftg != ftgNil && ftg != kftgDir, "Bad FileType");
    String stnFtg;
    LegacyLong cchBase;

    _CleanFtg(&ftg, &stnFtg);
    if (_ftg == ftg)
        return fTrue;

    // set the extension
    cchBase = _stnFile.Cch() - _CchExt();

    // use >= to leave room for the '.'
    if (cchBase + stnFtg.Cch() >= kcchMaxStn)
        return fFalse;

    _stnFile.Delete(cchBase);
    _ftg = ftg;
    if (stnFtg.Cch() > 0)
    {
        _stnFile.FAppendCh(ChLit('.'));
        _stnFile.FAppendStn(&stnFtg);
    }
    return fTrue;
}

/***************************************************************************
    Get the leaf name.
***************************************************************************/
void Filename::GetLeaf(PString pstn)
{
    AssertThis(0);
    AssertPo(pstn, 0);
    achar *pch;
    PZString psz = _stnFile.Psz();

    for (pch = psz + _stnFile.Cch(); pch-- > psz && !KauaiFiles::IsSeparator(*pch);)
    {
    }
    Assert(pch >= psz, "bad fni");

    pstn->SetSz(pch + 1);
}

/***************************************************************************
    Get a string representing the path of the fni.
***************************************************************************/
void Filename::GetStnPath(PString pstn)
{
    AssertThis(0);
    AssertPo(pstn, 0);
    *pstn = _stnFile;
}

/***************************************************************************
    Determines if the file/directory exists.  Returns tMaybe on error or
    if the fni type (file or dir) doesn't match the disk object of the
    same name.
***************************************************************************/
tribool Filename::TExists(void)
{
    AssertThis(ffniFile | ffniDir);
    String stn;
    PString pstn;
    KauaiFiles::PathInfo info;
    KauaiFiles::Status status;

    // strip off the trailing slash (if a directory).
    pstn = &_stnFile;
    if (_ftg == kftgDir)
    {
        LegacyLong cch;

        stn = _stnFile;
        pstn = &stn;
        cch = stn.Cch();
        Assert(cch > 0 && (KauaiFiles::IsSeparator(stn.Psz()[cch - 1])), 0);
        if (cch > 1)
            stn.Delete(cch - 1);
    }

    status = KauaiFiles::Info(pstn->Psz(), &info);
    if (status != KauaiFiles::Status::Found)
    {
        if (status == KauaiFiles::Status::Missing)
            return tNo;
        PushErc(ercFniGeneral);
        return tMaybe;
    }
    if ((_ftg == kftgDir) != info.directory)
    {
        PushErc(ercFniMismatch);
        return tMaybe;
    }
    if (info.hidden || info.system)
    {
        PushErc(ercFniHidden);
        return tMaybe;
    }
    return tYes;
}

/***************************************************************************
    Delete the physical file.  Should not be open.
***************************************************************************/
bool Filename::FDelete(void)
{
    AssertThis(ffniFile);
    Assert(FileObject::PfilFromFni(this) == pvNil, "file is open");

    if (KauaiFiles::Delete(_stnFile.Psz()))
        return fTrue;
    PushErc(ercFniDelete);
    return fFalse;
}

/***************************************************************************
    Renames the file indicated by this to *pfni.
***************************************************************************/
bool Filename::FRename(Filename *pfni)
{
    AssertThis(ffniFile);
    AssertPo(pfni, ffniFile);

    if (KauaiFiles::Rename(_stnFile.Psz(), pfni->_stnFile.Psz()))
    {
        return fTrue;
    }
    PushErc(ercFniRename);
    return fFalse;
}

/***************************************************************************
    Compare two fni's for equality.
***************************************************************************/
bool Filename::FEqual(Filename *pfni)
{
    AssertThis(ffniFile | ffniDir);
    AssertPo(pfni, ffniFile | ffniDir);

    return KauaiFiles::EqualPaths(pfni->_stnFile.Psz(), _stnFile.Psz());
}

/***************************************************************************
    Return whether the fni refers to a directory.
***************************************************************************/
bool Filename::FDir(void)
{
    AssertThis(0);
    return _ftg == kftgDir;
}

/***************************************************************************
    Return whether the directory portions of the fni's are the same.
***************************************************************************/
bool Filename::FSameDir(Filename *pfni)
{
    AssertThis(ffniFile | ffniDir);
    AssertPo(pfni, ffniFile | ffniDir);
    Filename fni1, fni2;

    fni1 = *this;
    fni2 = *pfni;
    fni1._FChangeLeaf(pvNil);
    fni2._FChangeLeaf(pvNil);
    return fni1.FEqual(&fni2);
}

/***************************************************************************
    Determine if the directory pstn in fni exists, optionally creating it
    and/or moving into it.  Specify ffniCreateDir to create it if it
    doesn't exist.  Specify ffniMoveTo to make the fni refer to it.
***************************************************************************/
bool Filename::FDownDir(PString pstn, LegacyUlong grffni)
{
    AssertThis(ffniDir);
    AssertPo(pstn, 0);

    Filename fniT;

    fniT = *this;
    // the +1 is for the \ character
    if (fniT._stnFile.Cch() + pstn->Cch() + 1 > kcchMaxStn)
    {
        PushErc(ercFniGeneral);
        return fFalse;
    }
    AssertDo(fniT._stnFile.FAppendStn(pstn), 0);
    AssertDo(fniT._stnFile.FAppendCh(KauaiFiles::Separator()), 0);
    fniT._ftg = kftgDir;
    AssertPo(&fniT, ffniDir);

    if (fniT.TExists() != tYes)
    {
        if (!(grffni & ffniCreateDir))
            return fFalse;
        // try to create it
        if (!KauaiFiles::MakeDirectory(fniT._stnFile.Psz()))
        {
            PushErc(ercFniDirCreate);
            return fFalse;
        }
    }
    if (grffni & ffniMoveToDir)
        *this = fniT;

    return fTrue;
}

/***************************************************************************
    Gets the lowest directory name (if pstn is not nil) and optionally
    moves the fni up a level (if ffniMoveToDir is specified).
***************************************************************************/
bool Filename::FUpDir(PString pstn, LegacyUlong grffni)
{
    AssertThis(ffniDir);
    AssertNilOrPo(pstn, 0);

    LegacyLong cch;
    achar *pchT;
    ZString sz;
    String stn;

    stn = _stnFile;
    if (!stn.FAppendSz(PszLit("..")))
        return fFalse;

    if ((cch = KauaiFiles::FullPath(stn.Psz(), sz, kcchMaxSz, &pchT)) == 0 || cch >= _stnFile.Cch() - 1)
    {
        return fFalse;
    }
    Assert(cch <= kcchMaxSz, 0);
    Assert(cch < _stnFile.Cch() + 2, 0);
    stn = sz;
    if (!KauaiFiles::IsSeparator(stn.Psz()[cch - 1]))
    {
        AssertDo(stn.FAppendCh(KauaiFiles::Separator()), 0);
        cch++;
    }

    if (pvNil != pstn)
    {
        // copy the tail and delete the trailing slash
        pstn->SetSz(_stnFile.Psz() + cch);
        pstn->Delete(pstn->Cch() - 1);
    }

    if (grffni & ffniMoveToDir)
    {
        _stnFile = stn;
        AssertThis(ffniDir);
    }
    return fTrue;
}

#ifdef DEBUG
/***************************************************************************
    Assert validity of the Filename.
***************************************************************************/
void Filename::AssertValid(LegacyUlong grffni)
{
    Filename_PAR::AssertValid(0);
    AssertPo(&_stnFile, 0);

    ZString szT;
    LegacyLong cch;
    PZString pszT;

    if (grffni == 0)
        grffni = ffniEmpty | ffniDir | ffniFile;

    if (_ftg == ftgNil)
    {
        Assert(grffni & ffniEmpty, "unexpected empty");
        Assert(_stnFile.Cch() == 0, "named empty?");
        return;
    }

    if ((cch = KauaiFiles::FullPath(_stnFile.Psz(), szT, kcchMaxSz, &pszT)) == 0 || cch > kcchMaxSz ||
        !KauaiFiles::EqualPaths(_stnFile.Psz(), szT))
    {
        Bug("bad fni");
        return;
    }

    if (_ftg == kftgDir)
    {
        Assert(grffni & ffniDir, "unexpected dir");
        Assert(KauaiFiles::IsSeparator(szT[cch - 1]), "expected trailing slash");
        Assert(pszT == NULL, "unexpected filename");
    }
    else
    {
        Assert(grffni & ffniFile, "unexpected file");
        Assert(pszT >= szT && pszT < szT + cch, "expected filename");
    }
}
#endif // DEBUG

/***************************************************************************
    Find the length of the file extension on the fni (including the period).
    Allow up to kcchsMaxExt characters for the extension (plus one for the
    period).
***************************************************************************/
LegacyLong Filename::_CchExt(void)
{
    AssertBaseThis(0);
    LegacyLong cch;
    PZString psz = _stnFile.Psz();
    achar *pch = psz + _stnFile.Cch() - 1;

    for (cch = 1; cch <= kcchsMaxExt + 1 && pch >= psz; cch++, pch--)
    {
        if ((achar)(schar)*pch != *pch)
        {
            // not an ANSI character - so doesn't qualify for our
            // definition of an extension
            return 0;
        }

        if (*pch == ChLit('.'))
            return cch;
        if (KauaiFiles::IsSeparator(*pch))
            return 0;
    }

    return 0;
}

/***************************************************************************
    Set the ftg from the file name.
***************************************************************************/
void Filename::_SetFtgFromName(void)
{
    AssertBaseThis(0);
    Assert(_stnFile.Cch() > 0, 0);
    LegacyLong cch, ich;
    achar *pchLim = _stnFile.Psz() + _stnFile.Cch();

    if (KauaiFiles::IsSeparator(pchLim[-1]))
        _ftg = kftgDir;
    else
    {
        _ftg = 0;
        cch = _CchExt() - 1;
        AssertIn(cch, -1, kcchsMaxExt + 1);
        pchLim -= cch;
        for (ich = 0; ich < cch; ich++)
            _ftg = (_ftg << 8) | (LegacyLong)(byte)ChsUpper((schar)pchLim[ich]);
    }
    AssertThis(ffniFile | ffniDir);
}

/***************************************************************************
    Change the leaf of the fni.
***************************************************************************/
bool Filename::_FChangeLeaf(PString pstn)
{
    AssertThis(ffniFile | ffniDir);
    AssertNilOrPo(pstn, 0);

    achar *pch;
    PZString psz;
    LegacyLong cchBase, cch;

    psz = _stnFile.Psz();
    for (pch = psz + _stnFile.Cch(); pch-- > psz && !KauaiFiles::IsSeparator(*pch);)
    {
    }
    Assert(pch >= psz, "bad fni");

    cchBase = pch - psz + 1;
    _stnFile.Delete(cchBase);
    _ftg = kftgDir;
    if (pstn != pvNil && (cch = pstn->Cch()) > 0)
    {
        if (cchBase + cch > kcchMaxStn)
            return fFalse;
        AssertDo(_stnFile.FAppendStn(pstn), 0);
        _SetFtgFromName();
    }
    AssertThis(ffniFile | ffniDir);
    return fTrue;
}

/***************************************************************************
    Make sure the ftg is all uppercase and has no characters after a zero.
***************************************************************************/
priv void _CleanFtg(FileType *pftg, PString pstnExt)
{
    AssertVarMem(pftg);
    AssertNilOrPo(pstnExt, 0);

    LegacyLong ichs;
    schar chs;
    bool fZero;
    FileType ftgNew;

    if (pvNil != pstnExt)
        pstnExt->SetNil();

    if (*pftg == kftgDir || *pftg == ftgNil)
        return;

    fZero = fFalse;
    ftgNew = 0;
    for (ichs = 0; ichs < kcchsMaxExt; ichs++)
    {
        chs = (schar)((LegacyUlong)*pftg >> (ichs * 8));
        fZero |= (chs == 0);
        if (!fZero)
        {
            chs = ChsUpper(chs);
            ftgNew |= (LegacyLong)(byte)chs << (8 * ichs);
            if (pvNil != pstnExt)
                pstnExt->FInsertCh(0, (achar)(byte)chs);
        }
    }

    *pftg = ftgNew;
}

/***************************************************************************
    Constructor for a File Name Enumerator.
***************************************************************************/
FileNameEnumerator::FileNameEnumerator(void)
{
    AssertBaseThis(0);
    _prgftg = _rgftg;
    _pglfes = pvNil;
    _fesCur.hn = nullptr;
    _fInited = fFalse;
    AssertThis(0);
}

/***************************************************************************
    Destructor for an FileNameEnumerator.
***************************************************************************/
FileNameEnumerator::~FileNameEnumerator(void)
{
    AssertBaseThis(0);
    _Free();
}

/***************************************************************************
    Free all the memory associated with the FileNameEnumerator.
***************************************************************************/
void FileNameEnumerator::_Free(void)
{
    if (_prgftg != _rgftg)
    {
        FreePpv((void **)&_prgftg);
        _prgftg = _rgftg;
    }
    do
    {
        if (nullptr != _fesCur.hn)
            KauaiFiles::CloseDirectory(_fesCur.hn);
    } while (pvNil != _pglfes && _pglfes->IvMac() > 0 && _pglfes->FPop(&_fesCur));
    _fesCur.hn = nullptr;
    _fInited = fFalse;
    ReleasePpo(&_pglfes);
    AssertThis(0);
}

/***************************************************************************
    Initialize the fne to do an enumeration.
***************************************************************************/
bool FileNameEnumerator::FInit(Filename *pfniDir, FileType *prgftg, LegacyLong cftg, LegacyUlong grffne)
{
    AssertThis(0);
    AssertNilOrVarMem(pfniDir);
    AssertIn(cftg, 0, kcbMax);
    AssertPvCb(prgftg, LwMul(cftg, size(FileType)));
    FileType *pftg;

    // free the old stuff
    _Free();

    if (0 >= cftg)
        _cftg = 0;
    else
    {
        LegacyLong cb = LwMul(cftg, size(FileType));

        if (cftg > kcftgFneBase && !FAllocPv((void **)&_prgftg, cb, fmemNil, mprNormal))
        {
            _prgftg = _rgftg;
            PushErc(ercFneGeneral);
            AssertThis(0);
            return fFalse;
        }
        CopyPb(prgftg, _prgftg, cb);
        _cftg = cftg;
        for (pftg = _prgftg + _cftg; pftg-- > _prgftg;)
            _CleanFtg(pftg);
    }

    if (pfniDir == pvNil)
    {
        _fesCur.chVol = 'A';
        _fesCur.grfvol = KauaiFiles::RootMask();
    }
    else
    {
        String stn;

        _fesCur.grfvol = 0;
        _fesCur.chVol = 0;
        _fesCur.fni = *pfniDir;
        stn = PszLit("*");
        if (!_fesCur.fni._FChangeLeaf(&stn))
        {
            PushErc(ercFneGeneral);
            _Free();
            AssertThis(0);
            return fFalse;
        }
    }
    _fesCur.hn = nullptr;
    _fRecurse = FPure(grffne & ffneRecurse);
    _fInited = fTrue;
    AssertThis(0);
    return fTrue;
}

/***************************************************************************
    Get the next Filename in the enumeration.
***************************************************************************/
bool FileNameEnumerator::FNextFni(Filename *pfni, LegacyUlong *pgrffneOut, LegacyUlong grffneIn)
{
    AssertThis(0);
    AssertVarMem(pfni);
    AssertNilOrVarMem(pgrffneOut);
    String stn;
    bool fT;
    LegacyLong fvol;
    KauaiFiles::Iteration result;
    FileType *pftg;

    if (!_fInited)
    {
        Bug("must initialize the FileNameEnumerator before using it!");
        return fFalse;
    }

    if (grffneIn & ffneSkipDir)
    {
        // skip the rest of the stuff in this dir
        if (!_FPop())
            goto LDone;
    }

    if (_fesCur.chVol != 0)
    {
        // volume
        for (fvol = 1L << (_fesCur.chVol - 'A'); _fesCur.chVol <= 'Z' && (_fesCur.grfvol & fvol) == 0;
             _fesCur.chVol++, fvol <<= 1)
        {
        }

        if (_fesCur.chVol > 'Z')
            goto LDone;
        // we've got one
        ZString root;
        if (!KauaiFiles::RootPath(static_cast<unsigned>(_fesCur.chVol++ - 'A'), root, kcchMaxSz))
            goto LDone;
        stn = root;
        AssertDo(pfni->FBuildFromPath(&stn, kftgDir), 0);
        goto LGotOne;
    }

    // directory or file
    for (;;)
    {
        if (nullptr == _fesCur.hn)
            result = KauaiFiles::First(_fesCur.fni._stnFile.Psz(), &_fesCur.hn, &_fesCur.entry);
        else
            result = KauaiFiles::Next(_fesCur.hn, &_fesCur.entry);
        if (result != KauaiFiles::Iteration::Entry)
        {
            if (result == KauaiFiles::Iteration::Error)
                PushErc(ercFneGeneral);
            goto LPop;
        }
        if (_fesCur.entry.info.hidden || _fesCur.entry.info.system)
            continue;
        stn.SetSz(_fesCur.entry.name);
        *pfni = _fesCur.fni;
        if (_fesCur.entry.info.directory)
        {
            if (stn.FEqualSz(PszLit(".")) || stn.FEqualSz(PszLit("..")))
                continue;
            AssertDo(pfni->_FChangeLeaf(pvNil), 0);
            fT = pfni->FDownDir(&stn, ffniMoveToDir);
        }
        else
            fT = pfni->_FChangeLeaf(&stn);
        if (!fT)
        {
            PushErc(ercFneGeneral);
            continue;
        }

        if (_cftg == 0)
            goto LGotOne;
        for (pftg = _prgftg + _cftg; pftg-- > _prgftg;)
        {
            if (*pftg == pfni->_ftg)
                goto LGotOne;
        }
    }
    Bug("How did we fall through to here?");

LPop:
    if (pvNil == _pglfes || _pglfes->IvMac() == 0)
    {
    LDone:
        _Free();
        AssertThis(0);
        return fFalse;
    }

    // we're about to pop a directory, so send the current directory back
    // with ffnePost
    if (pvNil != pgrffneOut)
        *pgrffneOut = ffnePost;
    *pfni = _fesCur.fni;
    AssertDo(pfni->_FChangeLeaf(pvNil), 0);
    AssertDo(_FPop(), 0);
    AssertPo(pfni, ffniDir);
    AssertThis(0);
    return fTrue;

LGotOne:
    AssertPo(pfni, ffniFile | ffniDir);
    if (pvNil != pgrffneOut)
        *pgrffneOut = ffnePre | ffnePost;

    if (_fRecurse && pfni->_ftg == kftgDir && (nullptr == _fesCur.hn || !_fesCur.entry.info.link))
    {
        if ((pvNil != _pglfes || pvNil != (_pglfes = DynamicArray::PglNew(size(FES), 5))) && _pglfes->FPush(&_fesCur))
        {
            // set up the new fes
            _fesCur.fni = *pfni;
            stn = PszLit("*");
            if (!_fesCur.fni._FChangeLeaf(&stn))
            {
                AssertDo(_pglfes->FPop(&_fesCur), 0);
            }
            else
            {
                _fesCur.hn = nullptr;
                _fesCur.grfvol = 0;
                _fesCur.chVol = 0;
                if (pvNil != pgrffneOut)
                    *pgrffneOut = ffnePre;
            }
        }
        else
            PushErc(ercFneGeneral);
    }
    AssertThis(0);
    return fTrue;
}

/***************************************************************************
    Pop a state in the FileNameEnumerator.
***************************************************************************/
bool FileNameEnumerator::_FPop(void)
{
    AssertBaseThis(0);
    if (nullptr != _fesCur.hn)
    {
        KauaiFiles::CloseDirectory(_fesCur.hn);
        _fesCur.hn = nullptr;
    }
    return pvNil != _pglfes && _pglfes->IvMac() > 0 && _pglfes->FPop(&_fesCur);
}

#ifdef DEBUG
/***************************************************************************
    Assert the validity of a FileNameEnumerator.
***************************************************************************/
void FileNameEnumerator::AssertValid(LegacyUlong grf)
{
    FileNameEnumerator_PAR::AssertValid(0);
    if (_fInited)
    {
        AssertNilOrPo(_pglfes, 0);
        AssertIn(_cftg, 0, kcbMax);
        AssertPvCb(_prgftg, LwMul(size(FileType), _cftg));
        Assert((_cftg <= kcftgFneBase) == (_prgftg == _rgftg), "wrong _prgftg");
    }
    else
        Assert(_pglfes == pvNil, 0);
}

/***************************************************************************
    Mark memory for the FileNameEnumerator.
***************************************************************************/
void FileNameEnumerator::MarkMem(void)
{
    AssertValid(0);
    FileNameEnumerator_PAR::MarkMem();
    if (_prgftg != _rgftg)
        MarkPv(_prgftg);
    MarkMemObj(_pglfes);
}
#endif // DEBUG
