/* Copyright (c) Microsoft Corporation.
   Licensed under the MIT License. */

/* Copyright (c) Microsoft Corporation.
   Licensed under the MIT License. */

/***************************************************************************
    Author: ShonK
    Project: Kauai
    Reviewed:
    Copyright (c) Microsoft Corporation

    File management.

***************************************************************************/
#ifndef FILE_H
#define FILE_H

/****************************************
    Byte order issues
****************************************/

const short kboCur = 0x0001;
const short kboOther = 0x0100;

/****************************************
    Basic types
****************************************/

typedef LegacyLong FilePosition;

enum
{
    ffilNil = 0x00,
    ffilWriteEnable = 0x01, // we can write to the file
    ffilDenyWrite = 0x02,   // others can't write to the file
    ffilDenyRead = 0x04,    // others can't read the file

    ffilTemp = 0x10,
    ffilMark = 0x20
};
const LegacyUlong kgrffilPerm = ffilWriteEnable | ffilDenyWrite | ffilDenyRead;

// file error levels - in order of severity
enum
{
    elNil,
    kelWrite,
    kelRead,
    kelSeek,
    kelCritical
};

/****************************************
    FileObject class
****************************************/
typedef class FileObject *PFileObject;
#define FileObject_PAR BaseLinkedList
#define kclsFileObject 'FIL'
class FileObject : public FileObject_PAR
{
    RTCLASS_DEC
    BLL_DEC(FileObject, PfilNext)
    ASSERT

  protected:
    // static member variables
    static Mutex _mutxList;
    static PFileObject _pfilFirst;

    Mutex _mutx;

    Filename _fni;
    bool _fOpen : 1;
    bool _fEverOpen : 1;
    bool _fWrote : 1;
    LegacyUlong _grffil; // permissions, mark and temp flags
    LegacyLong _el;

    KauaiFiles::FileHandle _hfile;

    // private methods
    FileObject(Filename *pfni, LegacyUlong grffil);
    ~FileObject(void);

    bool _FOpen(bool fCreate, LegacyUlong grffil);
    void _Close(bool fFinal = fFalse);
    void _SetFpPos(FilePosition fp);

  public:
    // public static members
    static FileType vftgCreator;

  public:
    // static methods
    static void ClearMarks(void);
    static void CloseUnmarked(void);
    static void ShutDown(void);

    // static methods returning a PFileObject
    static PFileObject PfilFirst(void)
    {
        return _pfilFirst;
    }
    static PFileObject PfilOpen(Filename *pfni, LegacyUlong grffil = ffilDenyWrite);
    static PFileObject PfilCreate(Filename *pfni, LegacyUlong grffil = ffilWriteEnable | ffilDenyWrite);
    static PFileObject PfilCreateTemp(Filename *pfni = pvNil);
    static PFileObject PfilFromFni(Filename *pfni);

    virtual void Release(void);
    void Mark(void)
    {
        _grffil |= ffilMark;
    }

    LegacyLong ElError(void)
    {
        return _el;
    }
    void SetEl(LegacyLong el = elNil)
    {
        _el = el;
    }
    bool FSetGrffil(LegacyUlong grffil, LegacyUlong grffilMask = ~0);
    LegacyUlong GrffilCur(void)
    {
        return _grffil;
    }
    void SetTemp(bool fTemp = fTrue);
    bool FTemp(void)
    {
        return FPure(_grffil & ffilTemp);
    }
    void GetFni(Filename *pfni)
    {
        *pfni = _fni;
    }
    void GetStnPath(PString pstn);

    bool FSetFpMac(FilePosition fp);
    FilePosition FpMac(void);
    bool FReadRgb(void *pv, LegacyLong cb, FilePosition fp);
    bool FReadRgbSeq(void *pv, LegacyLong cb, FilePosition *pfp)
    {
        AssertVarMem(pfp);
        if (!FReadRgb(pv, cb, *pfp))
            return fFalse;
        *pfp += cb;
        return fTrue;
    }
    bool FWriteRgb(void *pv, LegacyLong cb, FilePosition fp);
    bool FWriteRgbSeq(void *pv, LegacyLong cb, FilePosition *pfp)
    {
        AssertVarMem(pfp);
        if (!FWriteRgb(pv, cb, *pfp))
            return fFalse;
        *pfp += cb;
        return fTrue;
    }
    bool FSwapNames(PFileObject pfil);
    bool FRename(Filename *pfni);
    bool FSetFni(Filename *pfni);
    void Flush(void);
};

/****************************************
    File Location Class
****************************************/
// for AssertValid
enum
{
    ffloNil,
    ffloReadable,
};

typedef struct FileLocation *PFileLocation;
struct FileLocation
{
    PFileObject pfil;
    FilePosition fp;
    LegacyLong cb;

    bool FRead(void *pv)
    {
        return FReadRgb(pv, cb, 0);
    }
    bool FWrite(void *pv)
    {
        return FWriteRgb(pv, cb, 0);
    }
    bool FReadRgb(void *pv, LegacyLong cbRead, FilePosition dfp);
    bool FWriteRgb(void *pv, LegacyLong cbWrite, FilePosition dfp);
    bool FCopy(PFileLocation pfloDst);
    bool FReadHq(HQ *phq, LegacyLong cbRead, FilePosition dfp = 0);
    bool FWriteHq(HQ hq, FilePosition dfp = 0);
    bool FReadHq(HQ *phq)
    {
        return FReadHq(phq, cb, 0);
    }
    bool FTranslate(short osk);

    ASSERT
};

/***************************************************************************
    Data block class - wrapper around either a flo or an hq.
***************************************************************************/
enum
{
    fblckNil = 0,
    fblckPacked = 1,
    fblckUnpacked = 2,
    fblckFile = 4,
    fblckMemory = 8,
    fblckReadable = 16,
};

typedef class DataBlock *PDataBlock;
#define DataBlock_PAR BASE
#define kclsDataBlock 'BLCK'
class DataBlock : public DataBlock_PAR
{
    RTCLASS_DEC
    NOCOPY(DataBlock)
    ASSERT
    MARKMEM

  protected:
    bool _fPacked;

    // for file based blocks
    FileLocation _flo;

    // for memory based blocks
    HQ _hq;
    LegacyLong _ibMin;
    LegacyLong _ibLim;

  public:
    DataBlock(PFileLocation pflo, bool fPacked = fFalse);
    DataBlock(PFileObject pfil, FilePosition fp, LegacyLong cb, bool fPacked = fFalse);
    DataBlock(HQ *phq, bool fPacked = fFalse);
    DataBlock(void);
    ~DataBlock(void);

    void Set(PFileLocation pflo, bool fPacked = fFalse);
    void Set(PFileObject pfil, FilePosition fp, LegacyLong cb, bool fPacked = fFalse);
    void SetHq(HQ *phq, bool fPacked = fFalse);
    void Free(void);
    HQ HqFree(bool fPackedOk = fFalse);
    LegacyLong Cb(bool fPackedOk = fFalse);

    // changing the range of the block
    bool FMoveMin(LegacyLong dib);
    bool FMoveLim(LegacyLong dib);

    // create a temp block
    bool FSetTemp(LegacyLong cb, bool fForceFile = fFalse);

    // reading from and writing to
    bool FRead(void *pv, bool fPackedOk = fFalse)
    {
        return FReadRgb(pv, Cb(fPackedOk), 0, fPackedOk);
    }
    bool FWrite(void *pv, bool fPackedOk = fFalse)
    {
        return FWriteRgb(pv, Cb(fPackedOk), 0, fPackedOk);
    }
    bool FReadRgb(void *pv, LegacyLong cb, LegacyLong ib, bool fPackedOk = fFalse);
    bool FWriteRgb(void *pv, LegacyLong cb, LegacyLong ib, bool fPackedOk = fFalse);
    bool FReadHq(HQ *phq, LegacyLong cb, LegacyLong ib, bool fPackedOk = fFalse);
    bool FWriteHq(HQ hq, LegacyLong ib, bool fPackedOk = fFalse);
    bool FReadHq(HQ *phq, bool fPackedOk = fFalse)
    {
        return FReadHq(phq, Cb(fPackedOk), 0, fPackedOk);
    }

    // writing a block to a flo or another blck.
    bool FWriteToFlo(PFileLocation pfloDst, bool fPackedOk = fFalse);
    bool FWriteToBlck(PDataBlock pblckDst, bool fPackedOk = fFalse);
    bool FGetFlo(PFileLocation pflo, bool fPackedOk = fFalse);

    // packing and unpacking
    bool FPacked(LegacyLong *pcfmt = pvNil);
    bool FPackData(LegacyLong cfmt = cfmtNil);
    bool FUnpackData(void);

    // Amount of memory being used
    LegacyLong CbMem(void);
};

/***************************************************************************
    Message sink class. Basic interface for output streaming.
***************************************************************************/
typedef class MessageSink *PMSNK;
#define MessageSink_PAR BASE
#define kclsMessageSink 'MSNK'
class MessageSink : public MessageSink_PAR
{
    RTCLASS_INLINE(MessageSink)

  public:
    virtual void ReportLine(PZString psz) = 0;
    virtual void Report(PZString psz) = 0;
    virtual bool FError(void) = 0;
};

/***************************************************************************
    File based message sink.
***************************************************************************/
typedef class MessageSinkFile *PMessageSinkFile;
#define MessageSinkFile_PAR MessageSink
#define kclsMessageSinkFile 'msfl'
class MessageSinkFile : public MessageSinkFile_PAR
{
    ASSERT
    RTCLASS_DEC

  protected:
    bool _fError;
    PFileObject _pfil;
    FilePosition _fpCur;
    void _EnsureFile(void);

  public:
    MessageSinkFile(PFileObject pfil = pvNil);
    ~MessageSinkFile(void);

    virtual void ReportLine(PZString psz);
    virtual void Report(PZString psz);
    virtual bool FError(void);

    void SetFile(PFileObject pfil);
    PFileObject PfilRelease(void);
};

#endif //! FILE_H
