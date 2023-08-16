/* Copyright (c) Microsoft Corporation.
   Licensed under the MIT License. */

/* Copyright (c) Microsoft Corporation.
   Licensed under the MIT License. */

/***************************************************************************
    Author: ShonK
    Project: Kauai
    Reviewed:
    Copyright (c) Microsoft Corporation

    Chunky file classes. See comments in chunk.cpp.

***************************************************************************/
#ifndef CHUNK_H
#define CHUNK_H

namespace Chunky {

using namespace Group;

/***************************************************************************
    These must be unsigned longs!  We sort on them and assume in the code
    that they are unsinged.
***************************************************************************/
typedef ulong ChunkTag;  // chunk tag/type
typedef ulong ChunkNumber;  // chunk number
typedef ulong ChildChunkID; // child chunk id

enum
{
    fcflNil = 0x0000,
    fcflWriteEnable = 0x0001,
    fcflTemp = 0x0002,
    fcflMark = 0x0004,
    fcflAddToExtra = 0x0008,

    // This flag indicates that when data is read, it should first be
    // copied to the extra file (if it's not already there). This is
    // for chunky files that are on a CD for which we want to cache data
    // to the hard drive.
    fcflReadFromExtra = 0x0010,

#ifdef DEBUG
    // for AssertValid
    fcflGraph = 0x4000, // check the graph structure for cycles
    fcflFull = fobjAssertFull,
#endif // DEBUG
};

// chunk identification
struct ChunkIdentification
{
    ChunkTag ctg;
    ChunkNumber cno;
};
const ByteOrderMask kbomCki = 0xF0000000;

// child chunk identification
struct ChildChunkIdentification
{
    ChunkIdentification cki;
    ChildChunkID chid;
};
const ByteOrderMask kbomKid = 0xFC000000;

/***************************************************************************
    Chunky file class.
***************************************************************************/
typedef class ChunkyFile *PChunkyFile;
#define ChunkyFile_PAR BLL
#define kclsChunkyFile 'CFL'
class ChunkyFile : public ChunkyFile_PAR
{
    RTCLASS_DEC
    BLL_DEC(ChunkyFile, PcflNext)
    ASSERT
    MARKMEM

  private:
    // chunk storage
    struct CSTO
    {
        PFIL pfil;  // the file
        FP fpMac;   // logical end of file (for writing new chunks)
        PDynamicArray pglfsm; // free space map
    };

    PGeneralGroup _pggcrp;     // the index
    CSTO _csto;      // the main file
    CSTO _cstoExtra; // the scratch file

    bool _fAddToExtra : 1;
    bool _fMark : 1;
    bool _fFreeMapNotRead : 1;
    bool _fReadFromExtra : 1;
    bool _fInvalidMainFile : 1;

    // for deferred reading of the free map
    FP _fpFreeMap;
    long _cbFreeMap;

#ifndef CHUNK_BIG_INDEX
    struct RTIE
    {
        ChunkTag ctg;
        ChunkNumber cno;
        long rti;
    };

    PDynamicArray _pglrtie;

    bool _FFindRtie(ChunkTag ctg, ChunkNumber cno, RTIE *prtie = pvNil, long *pirtie = pvNil);
#endif //! CHUNK_BIG_INDEX

    // static member variables
    static long _rtiLast;
    static PChunkyFile _pcflFirst;

  private:
    // private methods
    ChunkyFile(void);
    ~ChunkyFile(void);

    static ulong _GrffilFromGrfcfl(ulong grfcfl);

    bool _FReadIndex(void);
    tribool _TValidIndex(void);
    bool _FWriteIndex(ChunkTag ctgCreator);
    bool _FCreateExtra(void);
    bool _FAllocFlo(long cb, PFLO pflo, bool fForceOnExtra = fFalse);
    bool _FFindCtgCno(ChunkTag ctg, ChunkNumber cno, long *picrp);
    void _GetUniqueCno(ChunkTag ctg, long *picrp, ChunkNumber *pcno);
    void _FreeFpCb(bool fOnExtra, FP fp, long cb);
    bool _FAdd(long cb, ChunkTag ctg, ChunkNumber cno, long icrp, PDataBlock pblck);
    bool _FPut(long cb, ChunkTag ctg, ChunkNumber cno, PDataBlock pblck, PDataBlock pblckSrc, void *pv);
    bool _FCopy(ChunkTag ctgSrc, ChunkNumber cnoSrc, PChunkyFile pcflDst, ChunkNumber *pcnoDst, bool fClone);
    bool _FFindMatch(ChunkTag ctgSrc, ChunkNumber cnoSrc, PChunkyFile pcflDst, ChunkNumber *pcnoDst);
    bool _FFindCtgRti(ChunkTag ctg, long rti, ChunkNumber cnoMin, ChunkNumber *pcnoDst);
    bool _FDecRefCount(long icrp);
    void _DeleteCore(long icrp);
    bool _FFindChild(long icrpPar, ChunkTag ctgChild, ChunkNumber cnoChild, ChildChunkID chid, long *pikid);
    bool _FAdoptChild(long icrpPar, long ikid, ChunkTag ctgChild, ChunkNumber cnoChild, ChildChunkID chid, bool fClearLoner);
    void _ReadFreeMap(void);
    bool _FFindChidCtg(ChunkTag ctgPar, ChunkNumber cnoPar, ChildChunkID chid, ChunkTag ctg, ChildChunkIdentification *pkid);
    bool _FSetName(long icrp, PSTN pstn);
    bool _FGetName(long icrp, PSTN pstn);
    void _GetFlo(long icrp, PFLO pflo);
    void _GetBlck(long icrp, PDataBlock pblck);
    bool _FEnsureOnExtra(long icrp, FLO *pflo = pvNil);

    long _Rti(ChunkTag ctg, ChunkNumber cno);
    bool _FSetRti(ChunkTag ctg, ChunkNumber cno, long rti);

  public:
    // static methods
    static PChunkyFile PcflFirst(void)
    {
        return _pcflFirst;
    }
    static PChunkyFile PcflOpen(Filename *pfni, ulong grfcfl);
    static PChunkyFile PcflCreate(Filename *pfni, ulong grfcfl);
    static PChunkyFile PcflCreateTemp(Filename *pfni = pvNil);
    static PChunkyFile PcflFromFni(Filename *pfni);

    static void ClearMarks(void);
    static void CloseUnmarked(void);
#ifdef CHUNK_STATS
    static void DumpStn(PSTN pstn, PFIL pfil = pvNil);
#endif // CHUNK_STATS

    virtual void Release(void);
    bool FSetGrfcfl(ulong grfcfl, ulong grfcflMask = (ulong)~0);
    void Mark(void)
    {
        _fMark = fTrue;
    }
    void SetTemp(bool f)
    {
        _csto.pfil->SetTemp(f);
    }
    bool FTemp(void)
    {
        return _csto.pfil->FTemp();
    }
    void GetFni(Filename *pfni)
    {
        _csto.pfil->GetFni(pfni);
    }
    bool FSetFni(Filename *pfni)
    {
        return _csto.pfil->FSetFni(pfni);
    }
    long ElError(void);
    void ResetEl(long el = elNil);
    bool FReopen(void);

    // finding and reading chunks
    bool FOnExtra(ChunkTag ctg, ChunkNumber cno);
    bool FEnsureOnExtra(ChunkTag ctg, ChunkNumber cno);
    bool FFind(ChunkTag ctg, ChunkNumber cno, DataBlock *pblck = pvNil);
    bool FFindFlo(ChunkTag ctg, ChunkNumber cno, PFLO pflo);
    bool FReadHq(ChunkTag ctg, ChunkNumber cno, HQ *phq);
    void SetPacked(ChunkTag ctg, ChunkNumber cno, bool fPacked);
    bool FPacked(ChunkTag ctg, ChunkNumber cno);
    bool FUnpackData(ChunkTag ctg, ChunkNumber cno);
    bool FPackData(ChunkTag ctg, ChunkNumber cno);

    // creating and replacing chunks
    bool FAdd(long cb, ChunkTag ctg, ChunkNumber *pcno, PDataBlock pblck = pvNil);
    bool FAddPv(void *pv, long cb, ChunkTag ctg, ChunkNumber *pcno);
    bool FAddHq(HQ hq, ChunkTag ctg, ChunkNumber *pcno);
    bool FAddBlck(PDataBlock pblckSrc, ChunkTag ctg, ChunkNumber *pcno);
    bool FPut(long cb, ChunkTag ctg, ChunkNumber cno, PDataBlock pblck = pvNil);
    bool FPutPv(void *pv, long cb, ChunkTag ctg, ChunkNumber cno);
    bool FPutHq(HQ hq, ChunkTag ctg, ChunkNumber cno);
    bool FPutBlck(PDataBlock pblck, ChunkTag ctg, ChunkNumber cno);
    bool FCopy(ChunkTag ctgSrc, ChunkNumber cnoSrc, PChunkyFile pcflDst, ChunkNumber *pcnoDst);
    bool FClone(ChunkTag ctgSrc, ChunkNumber cnoSrc, PChunkyFile pcflDst, ChunkNumber *pcnoDst);
    void SwapData(ChunkTag ctg1, ChunkNumber cno1, ChunkTag ctg2, ChunkNumber cno2);
    void SwapChildren(ChunkTag ctg1, ChunkNumber cno1, ChunkTag ctg2, ChunkNumber cno2);
    void Move(ChunkTag ctg, ChunkNumber cno, ChunkTag ctgNew, ChunkNumber cnoNew);

    // creating child chunks
    bool FAddChild(ChunkTag ctgPar, ChunkNumber cnoPar, ChildChunkID chid, long cb, ChunkTag ctg, ChunkNumber *pcno, PDataBlock pblck = pvNil);
    bool FAddChildPv(ChunkTag ctgPar, ChunkNumber cnoPar, ChildChunkID chid, void *pv, long cb, ChunkTag ctg, ChunkNumber *pcno);
    bool FAddChildHq(ChunkTag ctgPar, ChunkNumber cnoPar, ChildChunkID chid, HQ hq, ChunkTag ctg, ChunkNumber *pcno);

    // deleting chunks
    void Delete(ChunkTag ctg, ChunkNumber cno);
    void SetLoner(ChunkTag ctg, ChunkNumber cno, bool fLoner);
    bool FLoner(ChunkTag ctg, ChunkNumber cno);

    // chunk naming
    bool FSetName(ChunkTag ctg, ChunkNumber cno, PSTN pstn);
    bool FGetName(ChunkTag ctg, ChunkNumber cno, PSTN pstn);

    // graph structure
    bool FAdoptChild(ChunkTag ctgPar, ChunkNumber cnoPar, ChunkTag ctgChild, ChunkNumber cnoChild, ChildChunkID chid = 0, bool fClearLoner = fTrue);
    void DeleteChild(ChunkTag ctgPar, ChunkNumber cnoPar, ChunkTag ctgChild, ChunkNumber cnoChild, ChildChunkID chid = 0);
    long CckiRef(ChunkTag ctg, ChunkNumber cno);
    tribool TIsDescendent(ChunkTag ctg, ChunkNumber cno, ChunkTag ctgSub, ChunkNumber cnoSub);
    void ChangeChid(ChunkTag ctgPar, ChunkNumber cnoPar, ChunkTag ctgChild, ChunkNumber cnoChild, ChildChunkID chidOld, ChildChunkID chidNew);

    // enumerating chunks
    long Ccki(void);
    bool FGetCki(long icki, ChunkIdentification *pcki, long *pckid = pvNil, PDataBlock pblck = pvNil);
    bool FGetIcki(ChunkTag ctg, ChunkNumber cno, long *picki);
    long CckiCtg(ChunkTag ctg);
    bool FGetCkiCtg(ChunkTag ctg, long icki, ChunkIdentification *pcki, long *pckid = pvNil, PDataBlock pblck = pvNil);

    // enumerating child chunks
    long Ckid(ChunkTag ctgPar, ChunkNumber cnoPar);
    bool FGetKid(ChunkTag ctgPar, ChunkNumber cnoPar, long ikid, ChildChunkIdentification *pkid);
    bool FGetKidChid(ChunkTag ctgPar, ChunkNumber cnoPar, ChildChunkID chid, ChildChunkIdentification *pkid);
    bool FGetKidChidCtg(ChunkTag ctgPar, ChunkNumber cnoPar, ChildChunkID chid, ChunkTag ctg, ChildChunkIdentification *pkid);
    bool FGetIkid(ChunkTag ctgPar, ChunkNumber cnoPar, ChunkTag ctg, ChunkNumber cno, ChildChunkID chid, long *pikid);

    // Serialized chunk forests
    bool FWriteChunkTree(ChunkTag ctg, ChunkNumber cno, PFIL pfilDst, FP fpDst, long *pcb);
    static PChunkyFile PcflReadForestFromFlo(PFLO pflo, bool fCopyData);
    bool FForest(ChunkTag ctg, ChunkNumber cno);
    void SetForest(ChunkTag ctg, ChunkNumber cno, bool fForest = fTrue);
    PChunkyFile PcflReadForest(ChunkTag ctg, ChunkNumber cno, bool fCopyData);

    // writing
    bool FSave(ChunkTag ctgCreator, Filename *pfni = pvNil);
    bool FSaveACopy(ChunkTag ctgCreator, Filename *pfni);
};

/***************************************************************************
    Chunk graph enumerator
***************************************************************************/
enum
{
    // inputs
    fcgeNil = 0x0000,
    fcgeSkipToSib = 0x0001,

    // outputs
    fcgePre = 0x0010,
    fcgePost = 0x0020,
    fcgeRoot = 0x0040,
    fcgeError = 0x0080
};

#define CGE_PAR BASE
#define kclsCGE 'CGE'
class CGE : public CGE_PAR
{
    RTCLASS_DEC
    ASSERT
    MARKMEM
    NOCOPY(CGE)

  private:
    // data enumeration push state
    struct DPS
    {
        ChildChunkIdentification kid;
        long ikid;
    };

    // enumeration states
    enum
    {
        esStart,    // waiting to start the enumeration
        esGo,       // go to the next node
        esGoNoSkip, // there are no children to skip, so ignore fcgeSkipToSib
        esDone      // we're done with the enumeration
    };

    long _es;    // current state
    PChunkyFile _pcfl;  // the chunky file
    PDynamicArray _pgldps; // our stack of DPSs
    DPS _dps;    // the current DPS

  public:
    CGE(void);
    ~CGE(void);

    void Init(PChunkyFile pcfl, ChunkTag ctg, ChunkNumber cno);
    bool FNextKid(ChildChunkIdentification *pkid, ChunkIdentification *pckiPar, ulong *pgrfcgeOut, ulong grfcgeIn);
};

#ifdef CHUNK_STATS
extern bool vfDumpChunkRequests;
#endif // CHUNK_STATS

} // end of namespace Chunky

#endif //! CHUNK_H
