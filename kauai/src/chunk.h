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

#include <cstdint>
#include "mm/block_owner.h"
#include "mm/chunky_file.h"
#include "mm/chunk_graph.h"
#include "mm/chunk_metadata.h"
#include "mm/chunk_loading.h"
#include "mm/chunk_storage.h"
#include "mm/chunk_lifecycle.h"
#include "mm/chunk_container.h"
#include "mm/chunk_context.h"
#include "mm/chunk_owner.h"
#include "mm/chunk_table.h"

struct MMChunkEdit;
struct MMChunkEndpoint;
struct MMChunkFileCopy;
struct MMChunkTitleIO;
struct MMPrivateArchive;

namespace Chunky {

using namespace Group;

/***************************************************************************
    Chunk identity types. Must be 32-bit unsigned -- sortable as unsigned,
    and the kauai chunky-file format spec (kauai/doc/chunk.txt) pins them
    to 32 bits on disk. Were spelled `ulong` originally; uint32_t pins the
    width on LP64 systems (Linux/Mac x64), where bare `unsigned long` would
    silently widen to 8 bytes and shift every chunk header.
***************************************************************************/
typedef uint32_t ChunkTagOrType; // chunk tag/type
typedef uint32_t ChunkNumber;    // chunk number
typedef uint32_t ChildChunkID;   // child chunk id

const MMChunkPayloadServices *KauaiChunkPayloadServices();
int KauaiEncodeLegacyChunkName(void *, const char *, void *, MMU32 capacity, MMU32 *written);
MMChunkTitleIO KauaiChunkTitleIO();
MMChunkTitleIO KauaiChunkOwnerTitleIO();
MMPrivateArchive *KauaiTemporaryArchive();
MMPrivateArchive *KauaiWorkingArchive(Filename *, bool create);
bool KauaiSaveChunkOwner(MMChunkOwnerStorage *, ChunkTagOrType creator, Filename *);
bool KauaiSetChunkOwnerOptions(MMChunkOwnerStorage *, LegacyUlong flags, LegacyUlong mask);
const MMChunkOwnerFileFlags *KauaiChunkOwnerFileFlags();
const MMChunkErrorServices *KauaiChunkOwnerErrorServices();
LegacyLong KauaiChunkOwnerError(const MMChunkOwner *);
void KauaiResetChunkOwnerError(MMChunkOwner *, LegacyLong level);

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
    ChunkTagOrType ctg;
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
#define ChunkyFile_PAR BASE
#define kclsChunkyFile 'CFL'
class ChunkyFile : public ChunkyFile_PAR
{
    RTCLASS_DEC
    ASSERT
    MARKMEM

  private:
    MMChunkArchive *_archive;
    MMChunkOwnerRead &_reader;
    MMChunkOwnerStorage &_storage;
    MMChunkOwner &_owner;
    PFileObject _MainFile() { return static_cast<PFileObject>(_owner.files.main_file); }
    PFileObject _ExtraFile() { return static_cast<PFileObject>(_owner.files.extra_file); }
    MMChunkTable *_Index() { return static_cast<MMChunkTable *>(_owner.files.index); }

    // static member variables
    static MMChunkContext _context;
    MMChunkLink _link;

  private:
    // private methods
    explicit ChunkyFile(MMChunkArchive *);
    ~ChunkyFile(void);

    static MMChunkConstruction _ConstructionServices(Filename *);
    static PChunkyFile _PcflNew();
    static PChunkyFile _PcflFromFile(PFileObject);

    static MMChunkRegistry _RegistryServices();
    void _SetChunkFlag(ChunkTagOrType, ChunkNumber, MMU32, bool);
    bool _FReadIndex(void);
    MMChunkAccess _ChunkAccess();
    MMChunkEdit _PayloadEdit(void *context);
    tribool _TValidIndex(void);
    bool _FFindCtgCno(ChunkTagOrType ctg, ChunkNumber cno, LegacyLong *picrp);
    void _GetUniqueCno(ChunkTagOrType ctg, LegacyLong *picrp, ChunkNumber *pcno);
    bool _FAdd(LegacyLong cb, ChunkTagOrType ctg, ChunkNumber cno, LegacyLong icrp, PDataBlock pblck);
    bool _FPut(LegacyLong cb, ChunkTagOrType ctg, ChunkNumber cno, PDataBlock pblck, PDataBlock pblckSrc);
    bool _FCopy(ChunkTagOrType ctgSrc, ChunkNumber cnoSrc, PChunkyFile pcflDst, ChunkNumber *pcnoDst, bool fClone,
                int *created = nullptr);
    bool _FDecRefCount(LegacyLong icrp);
    void _DeleteCore(LegacyLong icrp);
    bool _FFindChild(LegacyLong icrpPar, ChunkTagOrType ctgChild, ChunkNumber cnoChild, ChildChunkID chid, LegacyLong *pikid);
    bool _FFindChidCtg(ChunkTagOrType ctgPar, ChunkNumber cnoPar, ChildChunkID chid, ChunkTagOrType ctg, ChildChunkIdentification *pkid);
    bool _FGetName(LegacyLong icrp, PString pstn);
    void _GetFlo(LegacyLong icrp, PFileLocation pflo);
    bool _GetBlockLocation(LegacyLong icrp, PFileLocation pflo);
    void _GetBlck(LegacyLong icrp, PDataBlock pblck);

  public:
    // static methods
    static PChunkyFile PcflFirst(void)
    {
        return static_cast<PChunkyFile>(mm_chunk_context_first(&_context));
    }
    PChunkyFile PcflNext(void)
    {
        return static_cast<PChunkyFile>(mm_chunk_context_next(&_link));
    }
    static PChunkyFile PcflOpen(Filename *pfni, LegacyUlong grfcfl);
    static PChunkyFile PcflCreate(Filename *pfni, LegacyUlong grfcfl);
    static PChunkyFile PcflCreateTemp(Filename *pfni = pvNil);
    static PChunkyFile PcflFromFni(Filename *pfni);

    static void ClearMarks(void);
    static void CloseUnmarked(void);
#ifdef CHUNK_STATS
    static void DumpStn(PString pstn, PFileObject pfil = pvNil);
#endif // CHUNK_STATS

    virtual void Release(void);
    bool FSetGrfcfl(LegacyUlong grfcfl, LegacyUlong grfcflMask = (LegacyUlong)~0);
    void Mark(void)
    {
        _owner.marked = fTrue;
    }
    void SetTemp(bool f)
    {
        _MainFile()->SetTemp(f);
    }
    bool FTemp(void)
    {
        return _MainFile()->FTemp();
    }
    void GetFni(Filename *pfni)
    {
        _MainFile()->GetFni(pfni);
    }
    bool FSetFni(Filename *pfni)
    {
        return _MainFile()->FSetFni(pfni);
    }
    LegacyLong ElError(void);
    void ResetEl(LegacyLong el = elNil);
    bool FReopen(void);

    // finding and reading chunks
    bool FOnExtra(ChunkTagOrType ctg, ChunkNumber cno);
    bool FEnsureOnExtra(ChunkTagOrType ctg, ChunkNumber cno);
    bool FFind(ChunkTagOrType ctg, ChunkNumber cno, DataBlock *pblck = pvNil);
    bool FFindBlockFile(ChunkTagOrType ctg, ChunkNumber cno, MMBlockFile *file, int *packed);
    bool FFindFlo(ChunkTagOrType ctg, ChunkNumber cno, PFileLocation pflo);
    bool FReadHq(ChunkTagOrType ctg, ChunkNumber cno, HQ *phq);
    void SetPacked(ChunkTagOrType ctg, ChunkNumber cno, bool fPacked);
    bool FPacked(ChunkTagOrType ctg, ChunkNumber cno);
    bool FUnpackData(ChunkTagOrType ctg, ChunkNumber cno);
    bool FPackData(ChunkTagOrType ctg, ChunkNumber cno);

    // creating and replacing chunks
    bool FAdd(LegacyLong cb, ChunkTagOrType ctg, ChunkNumber *pcno, PDataBlock pblck = pvNil);
    bool FAddPv(void *pv, LegacyLong cb, ChunkTagOrType ctg, ChunkNumber *pcno);
    bool FAddHq(HQ hq, ChunkTagOrType ctg, ChunkNumber *pcno);
    bool FAddBlck(PDataBlock pblckSrc, ChunkTagOrType ctg, ChunkNumber *pcno);
    bool FPut(LegacyLong cb, ChunkTagOrType ctg, ChunkNumber cno, PDataBlock pblck = pvNil);
    bool FPutPv(void *pv, LegacyLong cb, ChunkTagOrType ctg, ChunkNumber cno);
    bool FPutHq(HQ hq, ChunkTagOrType ctg, ChunkNumber cno);
    bool FPutBlck(PDataBlock pblck, ChunkTagOrType ctg, ChunkNumber cno);
    bool FCopy(ChunkTagOrType ctgSrc, ChunkNumber cnoSrc, PChunkyFile pcflDst, ChunkNumber *pcnoDst);
    bool FCopyReference(ChunkTagOrType type, ChunkNumber number, PChunkyFile destination, ChunkNumber *copied,
                        int *created);
    bool FClone(ChunkTagOrType ctgSrc, ChunkNumber cnoSrc, PChunkyFile pcflDst, ChunkNumber *pcnoDst);
    void SwapData(ChunkTagOrType ctg1, ChunkNumber cno1, ChunkTagOrType ctg2, ChunkNumber cno2);
    void SwapChildren(ChunkTagOrType ctg1, ChunkNumber cno1, ChunkTagOrType ctg2, ChunkNumber cno2);
    void Move(ChunkTagOrType ctg, ChunkNumber cno, ChunkTagOrType ctgNew, ChunkNumber cnoNew);

    // creating child chunks
    bool FAddChild(ChunkTagOrType ctgPar, ChunkNumber cnoPar, ChildChunkID chid, LegacyLong cb, ChunkTagOrType ctg, ChunkNumber *pcno, PDataBlock pblck = pvNil);
    bool FAddChildPv(ChunkTagOrType ctgPar, ChunkNumber cnoPar, ChildChunkID chid, void *pv, LegacyLong cb, ChunkTagOrType ctg, ChunkNumber *pcno);
    bool FAddChildHq(ChunkTagOrType ctgPar, ChunkNumber cnoPar, ChildChunkID chid, HQ hq, ChunkTagOrType ctg, ChunkNumber *pcno);

    // deleting chunks
    void Delete(ChunkTagOrType ctg, ChunkNumber cno);
    void SetLoner(ChunkTagOrType ctg, ChunkNumber cno, bool fLoner);
    bool FLoner(ChunkTagOrType ctg, ChunkNumber cno);

    // chunk naming
    bool FSetName(ChunkTagOrType ctg, ChunkNumber cno, PString pstn);
    bool FGetName(ChunkTagOrType ctg, ChunkNumber cno, PString pstn);
    bool FReadNameBytes(ChunkTagOrType, ChunkNumber, void *wire, MMU32 capacity, MMU32 *written);
    bool FWriteNameBytes(ChunkTagOrType, ChunkNumber, const void *wire, MMU32 bytes);

    // graph structure
    bool FAdoptChild(ChunkTagOrType ctgPar, ChunkNumber cnoPar, ChunkTagOrType ctgChild, ChunkNumber cnoChild, ChildChunkID chid = 0, bool fClearLoner = fTrue);
    void DeleteChild(ChunkTagOrType ctgPar, ChunkNumber cnoPar, ChunkTagOrType ctgChild, ChunkNumber cnoChild, ChildChunkID chid = 0);
    LegacyLong CckiRef(ChunkTagOrType ctg, ChunkNumber cno);
    tribool TIsDescendent(ChunkTagOrType ctg, ChunkNumber cno, ChunkTagOrType ctgSub, ChunkNumber cnoSub);
    void ChangeChid(ChunkTagOrType ctgPar, ChunkNumber cnoPar, ChunkTagOrType ctgChild, ChunkNumber cnoChild, ChildChunkID chidOld, ChildChunkID chidNew);

    // enumerating chunks
    LegacyLong Ccki(void);
    // Borrow the current index for synchronous reads. Reacquire after edits.
    MMChunkAccess ReadChunkAccess() { return _ChunkAccess(); }
    MMChunkOwnerRead *ReadOwner() { return &_reader; }
    // Borrow writable index primitives for a synchronous operation on this archive.
    MMChunkEdit EditIndex();
    MMChunkNames NameIndex();
    MMArchiveWriter WriteIndex();
    MMArchiveWriter WriteOwnerIndex();
    MMChunkEndpoint CopyEndpoint(MMChunkFileCopy &binding);
    // The caller supplies kcbMaxDataStn writable bytes for the encoded name.
    static MMChunkNameValue EncodeName(PString, void *wire);
    bool FGetCki(LegacyLong icki, ChunkIdentification *pcki, LegacyLong *pckid = pvNil, PDataBlock pblck = pvNil);
    bool FGetIcki(ChunkTagOrType ctg, ChunkNumber cno, LegacyLong *picki);
    LegacyLong CckiCtg(ChunkTagOrType ctg);
    bool FGetCkiCtg(ChunkTagOrType ctg, LegacyLong icki, ChunkIdentification *pcki, LegacyLong *pckid = pvNil, PDataBlock pblck = pvNil);

    // enumerating child chunks
    LegacyLong Ckid(ChunkTagOrType ctgPar, ChunkNumber cnoPar);
    bool FGetKid(ChunkTagOrType ctgPar, ChunkNumber cnoPar, LegacyLong ikid, ChildChunkIdentification *pkid);
    bool FGetKidChid(ChunkTagOrType ctgPar, ChunkNumber cnoPar, ChildChunkID chid, ChildChunkIdentification *pkid);
    bool FGetKidChidCtg(ChunkTagOrType ctgPar, ChunkNumber cnoPar, ChildChunkID chid, ChunkTagOrType ctg, ChildChunkIdentification *pkid);
    bool FGetIkid(ChunkTagOrType ctgPar, ChunkNumber cnoPar, ChunkTagOrType ctg, ChunkNumber cno, ChildChunkID chid, LegacyLong *pikid);

    // Serialized chunk forests
    bool FWriteChunkTree(ChunkTagOrType ctg, ChunkNumber cno, PFileObject pfilDst, FilePosition fpDst, LegacyLong *pcb);
    static PChunkyFile PcflReadForestFromFlo(PFileLocation pflo, bool fCopyData);
    bool FForest(ChunkTagOrType ctg, ChunkNumber cno);
    void SetForest(ChunkTagOrType ctg, ChunkNumber cno, bool fForest = fTrue);
    PChunkyFile PcflReadForest(ChunkTagOrType ctg, ChunkNumber cno, bool fCopyData);

    // writing
    bool FSave(ChunkTagOrType ctgCreator, Filename *pfni = pvNil);
    bool FSaveACopy(ChunkTagOrType ctgCreator, Filename *pfni);
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

#define ChunkGraphEnumerator_PAR BASE
#define kclsChunkGraphEnumerator 'CGE'
class ChunkGraphEnumerator : public ChunkGraphEnumerator_PAR
{
    RTCLASS_DEC
    ASSERT
    MARKMEM
    NOCOPY(ChunkGraphEnumerator)

  private:
    PChunkyFile _pcfl;
    MMChunkWalk _walk;

  public:
    ChunkGraphEnumerator(void);
    ~ChunkGraphEnumerator(void);

    void Init(PChunkyFile pcfl, ChunkTagOrType ctg, ChunkNumber cno);
    bool FNextKid(ChildChunkIdentification *pkid, ChunkIdentification *pckiPar, LegacyUlong *pgrfcgeOut, LegacyUlong grfcgeIn);
};

#ifdef CHUNK_STATS
extern bool vfDumpChunkRequests;
#endif // CHUNK_STATS

} // end of namespace Chunky

#endif //! CHUNK_H
