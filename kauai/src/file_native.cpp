/* Copyright (c) Microsoft Corporation.
   Licensed under the MIT License. */

/***************************************************************************
    Author: ShonK
    Project: Kauai
    Reviewed:
    Copyright (c) Microsoft Corporation

    Shared file lifecycle and error policy over the native file backend.

***************************************************************************/
#include "util.h"
ASSERTNAME

namespace
{
KauaiFiles::FileHandle OpenHandle(const achar *path, bool create, LegacyUlong flags)
{
    KauaiFiles::OpenOptions options;
    options.write = (flags & ffilWriteEnable) != 0;
    options.denyRead = (flags & ffilDenyRead) != 0;
    options.denyWrite = (flags & ffilDenyWrite) != 0;
    options.creation = !create              ? KauaiFiles::Creation::OpenExisting
                       : (flags & ffilTemp) ? KauaiFiles::Creation::CreateExclusive
                                            : KauaiFiles::Creation::CreateAlways;
    return KauaiFiles::Open(path, options);
}
} // namespace

/***************************************************************************
    Open or create the file.  If the file is already open, sets the
    permissions according to grffil.
***************************************************************************/
bool FileObject::_FOpen(bool fCreate, LegacyUlong grffil)
{
    AssertBaseThis(0);
    bool fRet = fFalse;

    _mutx.Enter();

    if (_el >= kelCritical)
        goto LRet;

    grffil &= kgrffilPerm;
    if (_fOpen)
    {
        Assert(!fCreate, "can't create an open file");
        if ((~_grffil & grffil) == 0)
        {
            // permissions are already set high enough
            fRet = fTrue;
            goto LRet;
        }
        KauaiFiles::Close(_hfile);
        _hfile = nullptr;

        // maintain the permissions we had before
        grffil |= _grffil & kgrffilPerm;
    }

    _hfile = OpenHandle(_fni._stnFile.Psz(), fCreate, grffil | (_grffil & ffilTemp));

    if (nullptr == _hfile)
    {
        // if it was open, re-open it with old permissions
        if (_fOpen)
        {
            _hfile = OpenHandle(_fni._stnFile.Psz(), fFalse, _grffil);
            if (nullptr != _hfile)
                goto LRet;
        }
        _fOpen = fFalse;
        _el = kelCritical;
        goto LRet;
    }

    _fOpen = fTrue;
    _fEverOpen = fTrue;
    _grffil = (_grffil & ~kgrffilPerm) | grffil;
    fRet = fTrue;

LRet:
    _mutx.Leave();

    return fRet;
}

/***************************************************************************
    Close the file.
***************************************************************************/
void FileObject::_Close(bool fFinal)
{
    AssertBaseThis(0);

    _mutx.Enter();

    if (_fOpen)
    {
        Flush();
        KauaiFiles::Close(_hfile);
        _fOpen = fFalse;
        _hfile = nullptr;
    }

    if ((_grffil & ffilTemp) && fFinal && _fEverOpen)
    {
        if (!KauaiFiles::Delete(_fni._stnFile.Psz()))
            Warn("Deleting temp file failed");
    }

    _mutx.Leave();
}

/***************************************************************************
    Flush the file (and its volume?).
***************************************************************************/
void FileObject::Flush(void)
{
    AssertThis(0);

    _mutx.Enter();

    if (_fOpen)
        KauaiFiles::Flush(_hfile);

    _mutx.Leave();
}

/***************************************************************************
    Seek to the given fp - assumes the mutx is already entered.
***************************************************************************/
void FileObject::_SetFpPos(FilePosition fp)
{
    AssertThis(0);

    if (!_fOpen)
        _FOpen(fFalse, _grffil);

    if (_el < kelSeek && !KauaiFiles::Seek(_hfile, static_cast<uint32_t>(fp)))
    {
        PushErc(ercFileGeneral);
        _el = kelSeek;
    }
}

/***************************************************************************
    Set the length of the file.  This doesn't zero the appended portion.
***************************************************************************/
bool FileObject::FSetFpMac(FilePosition fp)
{
    AssertThis(0);
    AssertIn(fp, 0, kcbMax);
    bool fRet;

    _mutx.Enter();

    Assert(_grffil & ffilWriteEnable, "can't write to read only file");
    _SetFpPos(fp);

    if (_el < kelWrite)
    {
        _fWrote = fTrue;
        if (!KauaiFiles::Resize(_hfile, static_cast<uint32_t>(fp)))
        {
            PushErc(ercFileGeneral);
            _el = kelWrite;
        }
    }
    fRet = _el < kelWrite;

    _mutx.Leave();

    return fRet;
}

/***************************************************************************
    Return the length of the file.
***************************************************************************/
FilePosition FileObject::FpMac(void)
{
    AssertThis(0);
    uint32_t length = 0;

    _mutx.Enter();

    if (!_fOpen)
        _FOpen(fFalse, _grffil);

    if (_el < kelSeek && !KauaiFiles::Size(_hfile, &length))
    {
        PushErc(ercFileGeneral);
        _el = kelSeek;
    }

    if (_el >= kelSeek)
        length = 0;

    _mutx.Leave();

    return static_cast<FilePosition>(length);
}

/***************************************************************************
    Read a block from the file.
***************************************************************************/
bool FileObject::FReadRgb(void *pv, LegacyLong cb, FilePosition fp)
{
    AssertThis(0);
    AssertIn(cb, 0, kcbMax);
    AssertIn(fp, 0, klwMax);
    AssertPvCb(pv, cb);

    bool fRet = fFalse;

    if (cb <= 0)
        return fTrue;

    _mutx.Enter();

    if (!_fOpen)
        _FOpen(fFalse, _grffil);

    Debug(FilePosition dfp = FpMac() - fp;)

        _SetFpPos(fp);
    if (_el >= kelRead)
        goto LRet;

    Assert(dfp >= cb, "read past EOF");
    if (!KauaiFiles::Read(_hfile, pv, static_cast<uint32_t>(cb)))
    {
        PushErc(ercFileGeneral);
        _el = kelRead;
        goto LRet;
    }

    fRet = fTrue;

LRet:
    _mutx.Leave();
    return fRet;
}

/***************************************************************************
    Write a block to the file.
***************************************************************************/
bool FileObject::FWriteRgb(void *pv, LegacyLong cb, FilePosition fp)
{
    AssertThis(0);
    AssertIn(cb, 0, kcbMax);
    AssertIn(fp, 0, klwMax);
    AssertPvCb(pv, cb);

    bool fRet = fFalse;

    if (cb <= 0)
        return fTrue;

    _mutx.Enter();

    Assert(_grffil & ffilWriteEnable, "can't write to read only file");

    if (!_fOpen)
        _FOpen(fFalse, _grffil);

    _SetFpPos(fp);
    if (_el >= kelWrite)
        goto LRet;

    _fWrote = fTrue;
    if (!KauaiFiles::Write(_hfile, pv, static_cast<uint32_t>(cb)))
    {
        PushErc(ercFileGeneral);
        _el = kelWrite;
        goto LRet;
    }

    fRet = fTrue;

LRet:
    _mutx.Leave();
    return fRet;
}

/***************************************************************************
    Swap the names of the two files.  They should be in the same directory.
***************************************************************************/
bool FileObject::FSwapNames(PFileObject pfil)
{
    AssertThis(0);
    AssertPo(pfil, 0);
    Filename fni;
    Filename fniT;
    bool fRet = fFalse;

    if (this == pfil)
    {
        Bug("Why are you calling FSwapNames on the same file?");
        return fTrue;
    }

    _mutx.Enter();
    pfil->_mutx.Enter();

    Assert(_fni.FSameDir(&pfil->_fni), "trying to change directories with FSwapNames");

    fni = pfil->_fni;
    if (!fni.FGetUnique(fni.Ftg()))
        goto LRet;

    _Close();
    pfil->_Close();

    if (!_fni.FRename(&fni))
        goto LFail;
    if (!pfil->_fni.FRename(&_fni))
        goto LRenameFail;
    if (!fni.FRename(&pfil->_fni))
    {
        if (!_fni.FRename(&pfil->_fni))
        {
            Bug("rename failure");
            pfil->_fni = _fni;
            _fni = fni;
            goto LFail;
        }

    LRenameFail:
        if (!fni.FRename(&_fni))
        {
            Bug("rename failure");
            _fni = fni;
        }
        goto LFail;
    }

    fni = _fni;
    _fni = pfil->_fni;
    pfil->_fni = fni;
    fRet = fTrue;

LFail:
    // reopen the files
    _FOpen(fFalse, _grffil);
    pfil->_FOpen(fFalse, pfil->_grffil);

LRet:
    _mutx.Leave();
    pfil->_mutx.Leave();

    if (!fRet)
        PushErc(ercFileSwapNames);

    AssertThis(0);
    AssertPo(pfil, 0);

    return fRet;
}

/***************************************************************************
    Rename a file.  The new fni should be on the same volume.
    This may fail without an error code being set.
***************************************************************************/
bool FileObject::FRename(Filename *pfni)
{
    AssertThis(0);
    AssertPo(pfni, ffniFile);
    Filename fni;
    bool fRet = fFalse;

    _mutx.Enter();

    Assert(_fni.FSameDir(pfni), "trying to change directories with FRename");

    _Close();
    if (fRet = _fni.FRename(pfni))
        _fni = *pfni;

    // reopen the file
    if (!_FOpen(fFalse, _grffil))
        fRet = fFalse;

    _mutx.Leave();

    if (!fRet)
        PushErc(ercFileRename);

    AssertThis(0);
    return fRet;
}
