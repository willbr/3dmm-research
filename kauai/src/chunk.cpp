/* Copyright (c) Microsoft Corporation.
   Licensed under the MIT License. */

/***************************************************************************
    Author: ShonK
    Project: Kauai
    Reviewed:
    Copyright (c) Microsoft Corporation

    Chunky file classes.

    Chunky files store distinct "chunks" of data. Each chunk is identified
    by a chunk tag (ChunkTagOrType) and chunk number (ChunkNumber) pair. By convention, ChunkTags
    consist of 4 ASCII characters. ChunkTags that are shorter than 4 characters
    should be padded with spaces (eg, 'GST '). The ChunkyFile class does not
    enforce these conventions, but many Kauai tools assume that ChunkTags
    consist of 4 printable characters.

    Chunks store arbitrary data. Chunks can have names. Most importantly,
    chunks can have child chunks. A chunk can also be a child of more
    than one chunk and can be a child of a chunk mutliple times. The main
    restriction on parent-child relationships is that there can be no loops.
    Mathematically speaking, the chunks form an acyclic directed graph.

    A parent-child relationship has a client specified number associated
    with it. This is called the child id (ChildChunkID). Child id's don't have to
    be unique. ChunkyFile places no significance on child id's. The only
    restriction concerning CHIDs is that if chunk B is to be a child of
    chunk A twice, then those two parent-child relationships must have
    different child id's. In a nutshell, a parent-child relationship
    (an arrow in the acyclic directed graph) is uniquely identified by
    the 5-tuple (ctgPar, cnoPar, ctgChild, cnoChild, chid).

    On disk, chunky files are organized as follows:

        * A header (ChunkyFilePrefix struct defined below).
        * The raw chunk data arranged in arbitrary order (a heap).
        * The index for the chunky file.
        * An optional free map indicating which portions of the heap
          are currently not being used to store chunk data.

    The header stores signatures, version numbers, and pointers to the
    index and free map. The heap contains only the raw chunk data.
    The index is not updated on disk until FSave is called.

    The index is implemented as a general group (GeneralGroup). The fixed portion
    of each entry is a ChunkRepresentation (defined below). The variable portion contains
    the list of children of the chunk (including ChildChunkID values) and the
    chunk name (if it has one).

    The ChunkRepresentation indicates where in the heap the chunk data is, whether
    the chunk data is compressed, whether the chunk is a "loner" and how
    many parents the chunk has. Loner chunks are ones that can exist without
    a parent. When pcfl->Delete(ctg, cno) is called, the chunk's loner
    flag is cleared. If the chunk has no parents, the chunk is deleted
    (by removing its ChunkRepresentation from the index and adding the space in the
    heap that it occupied to the free map). We also decrement the
    parent counts of the children of the chunk. Child chunks whose
    parent count becomes zero and are not loner chunks are then deleted,
    and the process continues down through the chunk graph.

    The ChunkyFile class implements intelligent chunk copying within a run-time
    session. Suppose a source ChunkyFile contains two chunks A and B, with B a
    child of A. If chunk A is copied to another chunky file, its entire
    subgraph is copied, so B is copied with it. Suppose however that chunk
    B was previously copied to the destination, then A is copied. We see
    that chunk B is already in the destination ChunkyFile, so don't actually copy
    the chunk - we just copy A and make the previously coped B a child of
    the newly copied A. If this is not what you want, use FClone instead
    of FCopy. FClone copies the entire chunk subgraph, but does not use
    any previously copied chunks. The intelligent chunk copying only
    happens within a single run-time session (from the files point of view).
    If the chunky files are closed and re-opened, no sharing will occur.

***************************************************************************/
#include <vector>
#include <new>
#include "util.h"
#include "three_mm/chunk_save.h"
#include "three_mm/chunk_heap.h"
ASSERTNAME

namespace Chunky {

/* HISTORY of ChunkyFile version numbers

    1	ShonK: instantiated, 10/28/93
    2	ShonK: added compression support, 2/14/95
    3	ShonK: changed format of chunk names to be a saved String, 3/30/95
    4	ShonK: implemented support for compact index (ChunkRepresentation is smaller). 8/21/95
    5	ShonK: added the fcrpForest flag. 9/4/95

*/

// A file written by this version of chunk.cpp receives this cvn.  Any
// file with this cvn value has exactly the same file format
const short kcvnCur = 5;

// A file written by this version of chunk.cpp can be read by any version
// of chunk.cpp whose kcvnCur is >= to this (this should be <= kcvnCur)
const short kcvnBack = 4;

// A file whose cvn is less than kcvnMin cannot be directly read by
// this version of chunk.cpp (maybe a converter will read it).
// (this should be <= kcvnCur)
// NOTE: (ShonK, 3/30/95): if this is >= 3, the fOldNames handling in
// _FReadIndex can be removed!
// NOTE: (ShonK, 8/21/95): if this is >= 4, the fOldIndex handling in
// _FReadIndex can be removed!
const short kcvnMin = 1;

const auto kcvnMinStnNames = 3;
const auto kcvnMinGrfcrp = 4;
const auto kcvnMinSmallIndex = 4;
const auto kcvnMinForest = 5;

// chunky file prefix. Fixed 128-byte wire format on every architecture.
// FilePosition fields and trailing reserved array are pinned to int32_t so
// LP64 doesn't silently widen the file header.
struct ChunkyFilePrefix
{
    int32_t lwMagic;           // identifies this as a chunky file
    ChunkTagOrType ctgCreator; // program that created this file
    DataVersion dver;          // chunky file version
    int16_t bo;                // byte order
    int16_t osk;               // which system wrote this

    int32_t fpMac;             // logical end of file
    int32_t fpIndex;           // location of chunky index
    int32_t cbIndex;           // size of chunky index
    int32_t fpMap;             // location of free space map
    int32_t cbMap;             // size of free space map (may be 0)

    int32_t rglwReserved[23];  // reserved for future use - should be zero
};
static_assert(sizeof(ChunkyFilePrefix) == 128, "ChunkyFilePrefix wire format drift");
const ByteOrderMask kbomCfp = 0xB55FFC00L;

// free space map entry. Fixed 8-byte wire format on every architecture.
struct FreeSpaceMap
{
    int32_t fp;
    int32_t cb;
};
static_assert(sizeof(FreeSpaceMap) == 8, "FreeSpaceMap wire format drift");
const ByteOrderMask kbomFsm = 0xF0000000L;

enum
{
    fcrpNil = 0,
    fcrpOnExtra = 0x01, // data is on the extra file
    fcrpLoner = 0x02,   // the chunk can stand (may also be a child)
    fcrpPacked = 0x04,  // the data is compressed
    fcrpMarkT = 0x08,   // used for consistency checks (see _TValidIndex)
    fcrpForest = 0x10,  // the chunk contains a forest of chunks
};

// Chunk Representation (big version) - fixed element in pggcrp.
// Variable part of group element is an rgkid and stn data (the name).
//
// Wire layout: this is the per-entry stride of the chunky-file index GG
// (`_pggcrp`), serialized via GeneralGroupOnFile.cbFixed. Pinned to 32 bytes
// on every architecture so the index format matches 1995 .3MM files on LP64.
const int32_t kcbMaxCrpbg = klwMax;
struct ChunkRepresentationBig
{
    ChunkIdentification cki; // chunk id
    int32_t fp;              // location on file
    int32_t cb;              // size of data on file
    int32_t ckid;            // number of owned chunks
    int32_t ccrpRef;         // number of owners of this chunk
    int32_t rti;             // run-time id
    union {
        struct
        {
            // for cvn <= kcvnMinGrfcrp
            byte fOnExtra; // fcrpOnExtra
            byte fLoner;   // fcrpLoner
            byte fPacked;  // fcrpPacked
            byte bT;       // fcrpMarkT
        };

        // for cvn >= kcvnMinGrfcrp
        uint32_t grfcrp;
    };

    LegacyLong BvRgch(void)
    {
        return LwMul(ckid, size(ChildChunkIdentification));
    }
    LegacyLong CbRgch(LegacyLong cbVar)
    {
        return cbVar - BvRgch();
    }

    uint32_t Grfcrp(uint32_t grfcrpMask = (uint32_t)(-1))
    {
        return grfcrp & grfcrpMask;
    }
    void ClearGrfcrp(uint32_t grfcrpT)
    {
        grfcrp &= ~grfcrpT;
    }
    void SetGrfcrp(uint32_t grfcrpT)
    {
        grfcrp |= grfcrpT;
    }
    void AssignGrfcrp(uint32_t grfcrpT, uint32_t grfcrpMask = (uint32_t)(-1))
    {
        grfcrp = grfcrp & ~grfcrpMask | grfcrpT & grfcrpMask;
    }
    LegacyLong Cb(void)
    {
        return cb;
    }
    void SetCb(LegacyLong cbT)
    {
        cb = cbT;
    }
};
static_assert(sizeof(ChunkRepresentationBig) == 32, "ChunkRepresentationBig wire format drift");
const ByteOrderMask kbomCrpbgGrfcrp = 0xFFFF0000L;
const ByteOrderMask kbomCrpbgBytes = 0xFFFE0000L;

// Chunk Representation (small version) - fixed element in pggcrp.
// Variable part of group element is an rgkid and stn data (the name).
// Fixed 20 bytes on every architecture (the small-index variant).
const int32_t kcbMaxCrpsm = 0x00FFFFFF;
const int32_t kcbitGrfcrp = 8;
const uint32_t kgrfcrpAll = (1 << kcbitGrfcrp) - 1;
struct ChunkRepresentationSmall
{
    ChunkIdentification cki; // chunk id
    int32_t fp;              // location on file
    uint32_t luGrfcrpCb;     // low byte is the grfcrp, high 3 bytes is cb
    uint16_t ckid;           // number of owned chunks
    uint16_t ccrpRef;        // number of owners of this chunk

    LegacyLong BvRgch(void)
    {
        return LwMul(ckid, size(ChildChunkIdentification));
    }
    LegacyLong CbRgch(LegacyLong cbVar)
    {
        return cbVar - BvRgch();
    }

    uint32_t Grfcrp(uint32_t grfcrpMask = (uint32_t)(-1))
    {
        return luGrfcrpCb & grfcrpMask & kgrfcrpAll;
    }
    void ClearGrfcrp(uint32_t grfcrpT)
    {
        luGrfcrpCb &= ~(grfcrpT & kgrfcrpAll);
    }
    void SetGrfcrp(uint32_t grfcrpT)
    {
        luGrfcrpCb |= grfcrpT & kgrfcrpAll;
    }
    void AssignGrfcrp(uint32_t grfcrpT, uint32_t grfcrpMask = (uint32_t)(-1))
    {
        luGrfcrpCb = luGrfcrpCb & ~(grfcrpMask & kgrfcrpAll) | grfcrpT & grfcrpMask & kgrfcrpAll;
    }
    LegacyLong Cb(void)
    {
        return luGrfcrpCb >> kcbitGrfcrp;
    }
    void SetCb(LegacyLong cbT)
    {
        luGrfcrpCb = (cbT << kcbitGrfcrp) | luGrfcrpCb & kgrfcrpAll;
    }
};
static_assert(sizeof(ChunkRepresentationSmall) == 20, "ChunkRepresentationSmall wire format drift");
const ByteOrderMask kbomCrpsm = 0xFF500000L;

#ifdef CHUNK_BIG_INDEX

typedef ChunkRepresentationBig ChunkRepresentation;
typedef ChunkRepresentationSmall ChunkRepresentationOther;
const LegacyLong kcbMaxCrp = kcbMaxCrpbg;
typedef LegacyLong CKID;
const LegacyLong kckidMax = kcbMax;
const ByteOrderMask kbomCrpsm = kbomCrpbgGrfcrp;

#else //! CHUNK_BIG_INDEX

typedef ChunkRepresentationSmall ChunkRepresentation;
typedef ChunkRepresentationBig ChunkRepresentationOther;
const LegacyLong kcbMaxCrp = kcbMaxCrpsm;
typedef ushort CKID;
const LegacyLong kckidMax = ksuMax;
const ByteOrderMask kbomCrp = kbomCrpsm;

#endif //! CHUNK_BIG_INDEX


namespace
{
int ReadChunkFile(void *context, ThreeMMU32 offset, void *bytes, size_t count)
{
    if (offset > 0x7fffffffUL || count > 0x7fffffffUL)
        return 0;
    return static_cast<PFileObject>(context)->FReadRgb(bytes, static_cast<LegacyLong>(count), static_cast<LegacyLong>(offset));
}
int ReadRuntimeChunk(void *context, ThreeMMU32 position, ThreeMMChunkEntry *entry)
{
    auto group = static_cast<PGeneralGroup>(context);
    LegacyLong variable;
    if (position >= static_cast<ThreeMMU32>(group->IvMac()))
        return 0;
    auto crp = static_cast<ChunkRepresentation *>(group->QvFixedGet(position, &variable));
    *entry = {crp->cki.ctg, crp->cki.cno, static_cast<ThreeMMU32>(crp->fp),
              static_cast<ThreeMMU32>(crp->Cb()), crp->Grfcrp(), static_cast<ThreeMMU32>(crp->ckid),
              static_cast<ThreeMMU32>(crp->ccrpRef), static_cast<ThreeMMU32>(variable)};
    return 1;
}
int ReadRuntimeChild(void *context, ThreeMMU32 parent, ThreeMMU32 position, ThreeMMChunkChild *child)
{
    auto group = static_cast<PGeneralGroup>(context);
    LegacyLong variable;
    if (parent >= static_cast<ThreeMMU32>(group->IvMac()))
        return 0;
    auto crp = static_cast<ChunkRepresentation *>(group->QvFixedGet(parent, &variable));
    if (variable < 0 || position >= static_cast<ThreeMMU32>(crp->ckid) || position >= static_cast<ThreeMMU32>(variable) / 12)
        return 0;
    ChildChunkIdentification native;
    group->GetRgb(parent, position * sizeof(native), sizeof(native), &native);
    *child = {native.cki.ctg, native.cki.cno, native.chid};
    return 1;
}
}
ThreeMMChunkAccess ChunkyFile::_ChunkAccess()
{
    return {_pggcrp, static_cast<ThreeMMU32>(_pggcrp->IvMac()), ReadRuntimeChunk, ReadRuntimeChild};
}

#define _BvKid(ikid) LwMul(ikid, size(ChildChunkIdentification))

const LegacyLong rtiNil = 0; // no rti assigned
LegacyLong ChunkyFile::_rtiLast = rtiNil;
PChunkyFile ChunkyFile::_pcflFirst;

#ifdef CHUNK_STATS
bool vfDumpChunkRequests = fTrue;
PFileObject _pfilStats;
FilePosition _fpStats;

/***************************************************************************
    Static method to dump a string to the chunk stats file
***************************************************************************/
void ChunkyFile::DumpStn(PString pstn, PFileObject pfil)
{
    Filename fni;
    String stn;

    if (pvNil == _pfilStats)
    {
        stn = PszLit("ChnkStat.txt");
        if (!fni.FGetTemp() || !fni.FSetLeaf(&stn) || pvNil == (_pfilStats = FileObject::PfilCreate(&fni)))
        {
            return;
        }
    }

    if (pvNil != pfil)
    {
        pfil->GetFni(&fni);
        fni.GetStnPath(&stn);
        stn.FAppendSz(PszLit(": "));
        _pfilStats->FWriteRgbSeq(stn.Prgch(), LwMul(stn.Cch(), size(achar)), &_fpStats);
    }

    _pfilStats->FWriteRgbSeq(pstn->Prgch(), LwMul(pstn->Cch(), size(achar)), &_fpStats);
    _pfilStats->FWriteRgbSeq(PszLit("\xD\xA"), MacWin(size(achar), 2 * size(achar)), &_fpStats);
}
#endif // CHUNK_STATS

RTCLASS(ChunkyFile)
RTCLASS(ChunkGraphEnumerator)

/***************************************************************************
    Constructor for ChunkyFile - private.
***************************************************************************/
ChunkyFile::ChunkyFile(void)
{
    // add it to the linked list
    _Attach(&_pcflFirst);
    AssertBaseThis(0);
}

/***************************************************************************
    Close the associated files.  Private.
***************************************************************************/
ChunkyFile::~ChunkyFile(void)
{
    AssertBaseThis(0);
    ReleasePpo(&_csto.pfil);
    ReleasePpo(&_csto.pglfsm);
    ReleasePpo(&_cstoExtra.pfil);
    ReleasePpo(&_cstoExtra.pglfsm);
    ReleasePpo(&_pggcrp);
#ifndef CHUNK_BIG_INDEX
    ReleasePpo(&_pglrtie);
#endif //! CHUNK_BIG_INDEX
}

/***************************************************************************
    Static method: open an existing file as a chunky file.  Increments the
    open count.
***************************************************************************/
PChunkyFile ChunkyFile::PcflOpen(Filename *pfni, LegacyUlong grfcfl)
{
    AssertPo(pfni, ffniFile);
    PChunkyFile pcfl;
    LegacyUlong grffil;

    Assert(!(grfcfl & fcflTemp), "can't open a file as temp");
    if (pvNil != (pcfl = PcflFromFni(pfni)))
    {
        if (!pcfl->FSetGrfcfl(grfcfl))
            return pvNil;
        pcfl->AddRef();
        return pcfl;
    }

    grffil = _GrffilFromGrfcfl(grfcfl);
    if ((pcfl = NewObj ChunkyFile()) == pvNil)
        goto LFail;

    if ((pcfl->_csto.pfil = FileObject::PfilOpen(pfni, grffil)) == pvNil || !pcfl->_FReadIndex())
    {
        goto LFail;
    }
    if (tYes != pcfl->_TValidIndex())
    {
    LFail:
        ReleasePpo(&pcfl);
        PushErc(ercCflOpen);
        return pvNil;
    }
    AssertDo(pcfl->FSetGrfcfl(grfcfl), 0);

    // We don't assert with fcflGraph, because we've already
    // called _TValidIndex.
    AssertPo(pcfl, fcflFull);
    return pcfl;
}

/***************************************************************************
    If the file is read-only, toss the index and extra file and re-read
    the index.
***************************************************************************/
bool ChunkyFile::FReopen(void)
{
    AssertThis(0);
    ChunkStorage csto, cstoExtra;
    PGeneralGroup pggcrp;
#ifndef CHUNK_BIG_INDEX
    PDynamicArray pglrtie;
#endif // CHUNK_BIG_INDEX
    bool fFreeMapNotRead;
    bool fRet;

    if (_csto.pfil->GrffilCur() & ffilWriteEnable || _fInvalidMainFile)
        return fFalse;

    pggcrp = _pggcrp;
    _pggcrp = pvNil;

#ifndef CHUNK_BIG_INDEX
    pglrtie = _pglrtie;
    _pglrtie = pvNil;
#endif // CHUNK_BIG_INDEX

    fFreeMapNotRead = _fFreeMapNotRead;
    _fFreeMapNotRead = fFalse;

    csto = _csto;
    ClearPb(&_csto, size(csto));
    _csto.pfil = csto.pfil;
    _csto.pfil->AddRef();

    cstoExtra = _cstoExtra;
    ClearPb(&_cstoExtra, size(cstoExtra));

    fRet = _FReadIndex() && tYes == _TValidIndex();
    if (!fRet)
    {
        SwapVars(&_pggcrp, &pggcrp);
#ifndef CHUNK_BIG_INDEX
        SwapVars(&_pglrtie, &pglrtie);
#endif // CHUNK_BIG_INDEX
        SwapVars(&_csto, &csto);
        SwapVars(&_cstoExtra, &cstoExtra);
        _fFreeMapNotRead = FPure(fFreeMapNotRead);
    }
    ReleasePpo(&pggcrp);
#ifndef CHUNK_BIG_INDEX
    ReleasePpo(&pglrtie);
#endif // CHUNK_BIG_INDEX
    ReleasePpo(&csto.pfil);
    ReleasePpo(&csto.pglfsm);
    ReleasePpo(&cstoExtra.pfil);
    ReleasePpo(&cstoExtra.pglfsm);

    AssertThis(0);
    return fRet;
}

/***************************************************************************
    Static method to get file options corresponding to the given chunky
    file options.
***************************************************************************/
LegacyUlong ChunkyFile::_GrffilFromGrfcfl(LegacyUlong grfcfl)
{
    LegacyUlong grffil = ffilDenyWrite;

    if (grfcfl & fcflWriteEnable)
        grffil |= ffilWriteEnable | ffilDenyRead;

    if (grfcfl & fcflTemp)
        grffil |= ffilTemp;

    return grffil;
}

/***************************************************************************
    Static method: create a new file.  Increments the open count.
***************************************************************************/
PChunkyFile ChunkyFile::PcflCreate(Filename *pfni, LegacyUlong grfcfl)
{
    AssertPo(pfni, ffniFile);
    PChunkyFile pcfl;
    LegacyUlong grffil;

    grfcfl |= fcflWriteEnable;
    grffil = _GrffilFromGrfcfl(grfcfl);

    if (pvNil != (pcfl = ChunkyFile::PcflFromFni(pfni)))
    {
        Bug("trying to create an open file");
        goto LFail;
    }

    if ((pcfl = NewObj ChunkyFile()) == pvNil)
        goto LFail;

    if ((pcfl->_pggcrp = GeneralGroup::PggNew(size(ChunkRepresentation))) == pvNil ||
        (pcfl->_csto.pfil = FileObject::PfilCreate(pfni, grffil)) == pvNil || !pcfl->_csto.pfil->FSetFpMac(size(ChunkyFilePrefix)))
    {
        ReleasePpo(&pcfl);
    LFail:
        PushErc(ercCflCreate);
        return pvNil;
    }
    pcfl->_csto.fpMac = size(ChunkyFilePrefix);
    AssertDo(pcfl->FSetGrfcfl(grfcfl), 0);

    AssertPo(pcfl, fcflFull | fcflGraph);
    return pcfl;
}

/***************************************************************************
    Static method to create a temporary chunky file in the same directory as
    *pfni and with the same ftg.  If pfni is nil, the file is created in
    the standard place with a temp ftg.
***************************************************************************/
PChunkyFile ChunkyFile::PcflCreateTemp(Filename *pfni)
{
    AssertNilOrPo(pfni, ffniFile);
    Filename fni;

    if (pvNil != pfni)
    {
        fni = *pfni;
        if (!fni.FGetUnique(pfni->Ftg()))
            goto LFail;
    }
    else if (!fni.FGetTemp())
    {
    LFail:
        PushErc(ercCflCreate);
        return pvNil;
    }

    return PcflCreate(&fni, fcflTemp);
}

/***************************************************************************
    Static method: if we have the chunky file indicated by fni open, returns
    the pcfl, otherwise returns pvNil.  Doesn't affect the open count.
***************************************************************************/
PChunkyFile ChunkyFile::PcflFromFni(Filename *pfni)
{
    AssertPo(pfni, ffniFile);
    PFileObject pfil;
    PChunkyFile pcfl;

    if ((pfil = FileObject::PfilFromFni(pfni)) == pvNil)
        return pvNil;
    for (pcfl = _pcflFirst; pcfl != pvNil; pcfl = pcfl->PcflNext())
    {
        Assert(pfil != pcfl->_cstoExtra.pfil, "who is using this Filename?");
        if (pcfl->_csto.pfil == pfil && !pcfl->_fInvalidMainFile)
            break;
    }
    return pcfl;
}

/***************************************************************************
    Embedded chunk descriptor on file. Chunks and their subtrees can
    be combined into a single stream by the following rules:

    CF stands for a chunk forest. CT stands for a chunk tree.
    EmbeddedChunkDescriptorOnFile(n, m) stands for an EmbeddedChunkDescriptorOnFile structure with cb == n and ckid == m.
    data(n) stands for n bytes of data.

        CF -> (empty)
        CF -> CT CF
        CT -> EmbeddedChunkDescriptorOnFile(n, 0) data(n)
        CT -> EDCF(n, m) data(n) CT CT ... CT (m times)

    ChunkyFile::FWriteChunkTree writes a CT from a chunk and its children.

    ChunkyFile::PcflReadForestFromFlo reads a CF from a flo and creates a ChunkyFile
    around the data.
***************************************************************************/
// Embedded chunk descriptor. Fixed 24-byte wire format on every architecture.
struct EmbeddedChunkDescriptorOnFile
{
    int16_t bo;
    int16_t osk;
    ChunkTagOrType ctg;
    ChildChunkID chid;
    int32_t cb;
    int32_t ckid;
    uint32_t grfcrp;
};
static_assert(sizeof(EmbeddedChunkDescriptorOnFile) == 24, "EmbeddedChunkDescriptorOnFile wire format drift");
const ByteOrderMask kbomEcdf = 0x5FFC0000L;

/***************************************************************************
    Combine the indicated chunk and its children into an embedded chunk.
    If pfil is pvNil, just computes the size.
***************************************************************************/
bool ChunkyFile::FWriteChunkTree(ChunkTagOrType ctg, ChunkNumber cno, PFileObject pfilDst, FilePosition fpDst, LegacyLong *pcb)
{
    AssertThis(0);
    AssertNilOrPo(pfilDst, 0);
    AssertNilOrVarMem(pcb);
    ChunkGraphEnumerator cge;
    EmbeddedChunkDescriptorOnFile ecdf;
    FileLocation floSrc, floDst;
    ChildChunkIdentification kid;
    ChunkIdentification ckiPar;
    LegacyUlong grfcge;

    if (pvNil != pcb)
        *pcb = 0;

    cge.Init(this, ctg, cno);
    while (cge.FNextKid(&kid, &ckiPar, &grfcge, fcgeNil))
    {
        if (grfcge & fcgeError)
            goto LFail;

        if (!(grfcge & fcgePre))
            continue;

        AssertDo(FFindFlo(kid.cki.ctg, kid.cki.cno, &floSrc), 0);
        if (pvNil != pcb)
            *pcb += size(EmbeddedChunkDescriptorOnFile) + floSrc.cb;

        if (pvNil != pfilDst)
        {
            // write the data
            ecdf.bo = kboCur;
            ecdf.osk = koskCur;
            ecdf.ctg = kid.cki.ctg;
            ecdf.chid = (grfcge & fcgeRoot) ? chidNil : kid.chid;
            ecdf.cb = floSrc.cb;
            ecdf.ckid = Ckid(kid.cki.ctg, kid.cki.cno);
            ecdf.grfcrp = fcrpNil;
            if (FPacked(kid.cki.ctg, kid.cki.cno))
                ecdf.grfcrp |= fcrpPacked;
            if (FForest(kid.cki.ctg, kid.cki.cno))
                ecdf.grfcrp |= fcrpForest;

            if (!pfilDst->FWriteRgbSeq(&ecdf, size(ecdf), &fpDst))
                goto LFail;

            floDst.pfil = pfilDst;
            floDst.fp = fpDst;
            floDst.cb = floSrc.cb;
            if (!floSrc.FCopy(&floDst))
            {
            LFail:
                TrashVar(pcb);
                return fFalse;
            }
            fpDst += floDst.cb;
        }
    }

    return fTrue;
}

/***************************************************************************
    Static method to read a serialized chunk stream (as written by a series
    of calls to FWriteChunkTree) and create a ChunkyFile around it. If fCopyData
    is false, we construct the ChunkyFile to use pointers to the data in the FileLocation.
    Otherwise, we copy the data to a new file.
***************************************************************************/
PChunkyFile ChunkyFile::PcflReadForestFromFlo(PFileLocation pflo, bool fCopyData)
{
    AssertVarMem(pflo);
    AssertPo(pflo->pfil, 0);

    // embedded chunk stack descriptor
    struct EmbeddedChunkStackDescriptor
    {
        ChunkTagOrType ctg;
        ChunkNumber cno;
        LegacyLong ckid;
    };

    PChunkyFile pcfl;
    EmbeddedChunkDescriptorOnFile ecdf;
    EmbeddedChunkStackDescriptor ecsdT, ecsdCur;
    FilePosition fpSrc, fpLimSrc;
    PDynamicArray pglecsd = pvNil;

    if (pvNil == (pglecsd = DynamicArray::PglNew(size(EmbeddedChunkStackDescriptor))))
        goto LFail;

    if ((pcfl = NewObj ChunkyFile()) == pvNil)
        goto LFail;

    if (pvNil == (pcfl->_pggcrp = GeneralGroup::PggNew(size(ChunkRepresentation))))
        goto LFail;

    if (fCopyData)
    {
        if (pvNil == (pcfl->_csto.pfil = FileObject::PfilCreateTemp()) || !pcfl->_csto.pfil->FSetFpMac(size(ChunkyFilePrefix)))
        {
            goto LFail;
        }
        pcfl->_csto.fpMac = size(ChunkyFilePrefix);
    }
    else
    {
        pcfl->_csto.pfil = pflo->pfil;
        pcfl->_csto.pfil->AddRef();
        pcfl->_csto.fpMac = pflo->fp + pflo->cb;
        pcfl->_fInvalidMainFile = fTrue;
        pcfl->_fAddToExtra = fTrue;
    }

    AssertPo(pcfl, fcflFull | fcflGraph);

    ClearPb(&ecsdCur, size(ecsdCur));

    fpSrc = pflo->fp;
    fpLimSrc = pflo->fp + pflo->cb;
    for (;;)
    {
        if (fpSrc >= fpLimSrc)
            break;

        if (fpSrc > fpLimSrc + size(ecdf) || !pflo->pfil->FReadRgbSeq(&ecdf, size(ecdf), &fpSrc))
        {
            goto LFail;
        }

        if (kboOther == ecdf.bo)
            SwapBytesBom(&ecdf, kbomEcdf);
        else if (kboCur != ecdf.bo)
            goto LFail;

        if (!FIn(ecdf.cb, 0, fpLimSrc - fpSrc + 1))
            goto LFail;

        if (fCopyData)
        {
            DataBlock blck(pflo->pfil, fpSrc, ecdf.cb, ecdf.grfcrp & fcrpPacked);

            if (!pcfl->FAddBlck(&blck, ecdf.ctg, &ecsdT.cno))
                goto LFail;
        }
        else
        {
            ChunkRepresentation *qcrp;
            LegacyLong icrp;

            pcfl->_GetUniqueCno(ecdf.ctg, &icrp, &ecsdT.cno);
            if (!pcfl->_FAdd(0, ecdf.ctg, ecsdT.cno, icrp, pvNil))
                goto LFail;

            qcrp = (ChunkRepresentation *)pcfl->_pggcrp->QvFixedGet(icrp);
            qcrp->fp = ecdf.cb > 0 ? fpSrc : 0;
            qcrp->SetCb(ecdf.cb);
            qcrp->AssignGrfcrp(ecdf.grfcrp, fcrpPacked | fcrpForest);
        }
        fpSrc += ecdf.cb;

        if (pglecsd->IvMac() > 0)
        {
            Assert(ecsdCur.ckid > 0, 0);

            // Make this a child
            if (!pcfl->FAdoptChild(ecsdCur.ctg, ecsdCur.cno, ecdf.ctg, ecsdT.cno, ecdf.chid))
            {
                goto LFail;
            }
            ecsdCur.ckid--;
        }

        if (ecdf.ckid > 0)
        {
            // This one has children, so we need to push the current ecsd and
            // make this the current one
            ecsdT.ctg = ecdf.ctg;
            ecsdT.ckid = ecdf.ckid;

            if (!pglecsd->FPush(&ecsdCur))
                goto LFail;

            ecsdCur = ecsdT;
        }
        else
        {
            // pop up while there are no more children and there's something
            // to pop.
            while (ecsdCur.ckid <= 0 && pglecsd->IvMac() > 0)
                pglecsd->FPop(&ecsdCur);
        }
    }

    if (pglecsd->IvMac() > 0 || ecsdCur.ckid != 0)
    {
    LFail:
        // something failed or the data was bad
        ReleasePpo(&pcfl);
    }

    ReleasePpo(&pglecsd);
    AssertNilOrPo(pcfl, fcflFull | fcflGraph);

    return pcfl;
}

/***************************************************************************
    Return whether the chunk contains a forest of subchunks.
***************************************************************************/
bool ChunkyFile::FForest(ChunkTagOrType ctg, ChunkNumber cno)
{
    AssertThis(0);
    LegacyLong icrp;
    ChunkRepresentation *qcrp;

    if (!_FFindCtgCno(ctg, cno, &icrp))
    {
        Bug("chunk not there");
        return fFalse;
    }
    qcrp = (ChunkRepresentation *)_pggcrp->QvFixedGet(icrp);
    Assert(qcrp->cki.ctg == ctg && qcrp->cki.cno == cno, 0);
    return FPure(qcrp->Grfcrp(fcrpForest));
}

/***************************************************************************
    Set or clear the "forest" flag.
***************************************************************************/
void ChunkyFile::SetForest(ChunkTagOrType ctg, ChunkNumber cno, bool fForest)
{
    AssertThis(0);
    LegacyLong icrp;
    ChunkRepresentation *qcrp;

    if (!_FFindCtgCno(ctg, cno, &icrp))
    {
        Bug("chunk not there");
        return;
    }
    qcrp = (ChunkRepresentation *)_pggcrp->QvFixedGet(icrp);
    Assert(qcrp->cki.ctg == ctg && qcrp->cki.cno == cno, 0);
    if (FPure(qcrp->Grfcrp(fcrpForest)) != FPure(fForest))
    {
        if (fForest)
            qcrp->SetGrfcrp(fcrpForest);
        else
            qcrp->ClearGrfcrp(fcrpForest);
        _FSetRti(ctg, cno, rtiNil);
    }
}

/***************************************************************************
    Read the chunk's data as an embedded forest and construct a ChunkyFile around
    the data.
***************************************************************************/
PChunkyFile ChunkyFile::PcflReadForest(ChunkTagOrType ctg, ChunkNumber cno, bool fCopyData)
{
    AssertThis(0);
    FileLocation flo;

    if (!FFindFlo(ctg, cno, &flo))
        return pvNil;

    if (FPacked(ctg, cno))
    {
        DataBlock blck(&flo, fTrue);

        if (!blck.FUnpackData() || !blck.FGetFlo(&flo))
            return pvNil;
        fCopyData = fFalse;
    }

    return PcflReadForestFromFlo(&flo, fCopyData);
}

/***************************************************************************
    Static method: clear the marks for chunky files.
***************************************************************************/
void ChunkyFile::ClearMarks(void)
{
    PChunkyFile pcfl;

    for (pcfl = _pcflFirst; pcfl != pvNil; pcfl = pcfl->PcflNext())
    {
        AssertPo(pcfl, 0);
        pcfl->_fMark = fFalse;
    }
}

/***************************************************************************
    Static method: close any chunky files that are unmarked and have 0
    open count.
***************************************************************************/
void ChunkyFile::CloseUnmarked(void)
{
    PChunkyFile pcfl, pcflNext;

    for (pcfl = _pcflFirst; pcfl != pvNil; pcfl = pcflNext)
    {
        AssertPo(pcfl, 0);
        pcflNext = pcfl->PcflNext();
        if (!pcfl->_fMark && pcfl->_cactRef == 0)
            delete pcfl;
    }
}

/***************************************************************************
    Set the grfcfl options.  This sets or clears fcflTemp and only sets
    (never clears) fcflMark, fcflWriteEnable and fcflAddToExtra.  This
    can only fail if fcflWriteEnable is specified and we can't make
    the base file write enabled.
***************************************************************************/
bool ChunkyFile::FSetGrfcfl(LegacyUlong grfcfl, LegacyUlong grfcflMask)
{
    AssertThis(0);
    LegacyUlong grffil, grffilMask;

    grfcfl &= grfcflMask;
    grffil = _GrffilFromGrfcfl(grfcfl);
    grffilMask = _GrffilFromGrfcfl(grfcflMask);

    if ((grffil ^ _csto.pfil->GrffilCur()) & grffilMask)
    {
        // need to change some file properties
        if (_fInvalidMainFile || !_csto.pfil->FSetGrffil(grffil, grffilMask))
            return fFalse;
    }

    // If we're becoming write enabled and we haven't read the free map yet,
    // do so now.  Reading the free map is non-critical.
    if ((grfcfl & fcflWriteEnable) && _fFreeMapNotRead)
        _ReadFreeMap();

    // don't clear the mark field if it's already set
    if (grfcfl & fcflMark)
        _fMark = fTrue;

    // don't clear the _fAddToExtra field if it's already set
    if ((grfcfl & fcflAddToExtra) || ((grfcfl ^ grfcflMask) & fcflWriteEnable))
        _fAddToExtra = fTrue;

    if (grfcfl & fcflReadFromExtra)
        _fReadFromExtra = fTrue;

    return fTrue;
}

/***************************************************************************
    Return the level of error that we've encountered for this chunky file.
***************************************************************************/
LegacyLong ChunkyFile::ElError(void)
{
    AssertThis(0);
    LegacyLong elT;

    elT = _csto.pfil->ElError();
    if (pvNil != _cstoExtra.pfil)
        elT = LwMax(elT, _cstoExtra.pfil->ElError());
    return elT;
}

/***************************************************************************
    Make sure the el level on both files is at or below el.
***************************************************************************/
void ChunkyFile::ResetEl(LegacyLong el)
{
    AssertThis(0);
    AssertIn(el, elNil, kcbMax);

    if (_csto.pfil->ElError() > el)
        _csto.pfil->SetEl(el);

    if (pvNil != _cstoExtra.pfil && _cstoExtra.pfil->ElError() > el)
        _cstoExtra.pfil->SetEl(el);
}

/***************************************************************************
    Decrement the open count.  If it is zero and the chunky file isn't
    marked, the file is closed.
***************************************************************************/
void ChunkyFile::Release(void)
{
    AssertBaseThis(0);
    if (_cactRef <= 0)
    {
        Bug("calling Release without an AddRef");
        return;
    }
    if (--_cactRef == 0 && !_fMark)
        delete this;
}

#ifdef DEBUG
/***************************************************************************
    Assert the validity of the chunky file.  If fcflFull, does some checking
    of the index.  If fcflGraph, does fcflFull and checks the graph structure
    for cycles.
***************************************************************************/
void ChunkyFile::AssertValid(LegacyUlong grfcfl)
{
    ChunkyFile_PAR::AssertValid(fobjAllocated);
    bool fFirstCrp;
    LegacyLong cbTot = 0;
    LegacyLong cbTotExtra = 0;
    ChunkIdentification ckiOld;
    ChunkIdentification ckiNew;
    ChildChunkIdentification kid, kidOld;
    bool fFirstKid;
    LegacyLong icrp, icrpT;
    LegacyLong ccrpRefTot = 0;
    LegacyLong ckidTot = 0;
    LegacyLong cbVar, cbRgch;
    ChunkRepresentation crp;
    LegacyLong ikid;
    FilePosition fpBase = _fInvalidMainFile ? 0 : size(ChunkyFilePrefix);

    Assert(!_fInvalidMainFile || _fAddToExtra, 0);

    AssertPo(_pggcrp, 0);
    AssertPo(_csto.pfil, 0);
    if (_csto.pglfsm != pvNil)
    {
        AssertPo(_csto.pglfsm, 0);
        Assert(!_fFreeMapNotRead, "free map never read, but exists!");
    }
    Assert(_csto.fpMac >= fpBase, "fpMac wrong");

    if (_cstoExtra.pfil != pvNil)
    {
        AssertPo(_cstoExtra.pfil, 0);
        if (_cstoExtra.pglfsm != pvNil)
            AssertPo(_cstoExtra.pglfsm, 0);
    }
    else
        Assert(_cstoExtra.pglfsm == pvNil, 0);

#ifndef CHUNK_BIG_INDEX
    AssertNilOrPo(_pglrtie, 0);
#endif //! CHUNK_BIG_INDEX

    if (!(grfcfl & (fcflFull | fcflGraph)))
        return;

    SuspendAssertValid();

    // Verify that the index and free map(s) are in sorted order.
    // Would be nice to verify ccrpRef and that crp's don't overlap,
    // but this is hard, so instead we verify that the sum of ccrpRef
    // values is correct and that the total length of all blocks is not
    // too big.
    fFirstCrp = fTrue;
    for (icrp = _pggcrp->IvMac(); icrp-- != 0;)
    {
        _pggcrp->GetFixed(icrp, &crp);
        cbVar = _pggcrp->Cb(icrp);
        cbRgch = crp.CbRgch(cbVar);

        AssertIn(crp.ckid, 0, kckidMax + 1);
        AssertIn(cbRgch, 0, kcbMaxDataStn + 1);

        // we use this in checking the graph structure
        Assert(!crp.Grfcrp(fcrpMarkT), "fcrpMarkT set");

        ckiNew = crp.cki;
        ccrpRefTot += crp.ccrpRef;
        ckidTot += crp.ckid;

#ifdef CHUNK_BIG_INDEX
        AssertIn(crp.rti, 0, _rtiLast + 1);
#endif // CHUNK_BIG_INDEX

        // assert that the file storage (fp,cb) is OK
        if (crp.Grfcrp(fcrpOnExtra))
        {
            Assert(_cstoExtra.pfil != pvNil, "fcrpOnExtra wrong");
            AssertIn(crp.fp, 0, _cstoExtra.fpMac);
            AssertIn(crp.Cb(), 1, _cstoExtra.fpMac - crp.fp + 1);
            cbTotExtra += crp.Cb();
        }
        else
        {
            AssertVar(crp.Cb() > 0 && (crp.fp >= fpBase) || crp.Cb() == 0 && crp.fp == 0, "bad fp", &crp.fp);
            AssertVar(crp.fp <= _csto.fpMac, "bad fp", &crp.fp);
            AssertIn(crp.Cb(), 0, _csto.fpMac - crp.fp + 1);
            cbTot += crp.Cb();
        }

        // assert that this crp is in order
        AssertVar(fFirstCrp || ckiNew.ctg < ckiOld.ctg || ckiNew.ctg == ckiOld.ctg && ckiNew.cno < ckiOld.cno,
                  "crp not in order", &ckiNew);
        ckiOld = ckiNew;
        fFirstCrp = fFalse;

        fFirstKid = fTrue;
        for (ikid = 0; ikid < crp.ckid; ikid++)
        {
            _pggcrp->GetRgb(icrp, _BvKid(ikid), size(ChildChunkIdentification), &kid);
            AssertVar(_FFindCtgCno(kid.cki.ctg, kid.cki.cno, &icrpT), "child doesn't exist", &kid.cki);
            Assert(kid.cki.ctg != ckiOld.ctg || kid.cki.cno != ckiOld.cno, "chunk is child of itself!");

            Assert(fFirstKid || kidOld.chid < kid.chid || kidOld.chid == kid.chid && kidOld.cki.ctg < kid.cki.ctg ||
                       kidOld.chid == kid.chid && kidOld.cki.ctg == kid.cki.ctg && kidOld.cki.cno < kid.cki.cno,
                   "kid's not sorted or duplicate kid");
            kidOld = kid;
            fFirstKid = fFalse;
        }
    }
    Assert(ccrpRefTot == ckidTot, "ref counts messed up");
    Assert(cbTotExtra <= _cstoExtra.fpMac, "overlapping chunks on extra");
    Assert(cbTot <= _csto.fpMac - fpBase, "overlapping chunks on file");

    if (_csto.pglfsm != pvNil)
    {
        LegacyLong ifsm;
        FreeSpaceMap fsm;
        FilePosition fpOld;

        fpOld = _csto.fpMac;
        for (ifsm = _csto.pglfsm->IvMac(); ifsm-- != 0;)
        {
            _csto.pglfsm->Get(ifsm, &fsm);
            Assert(fsm.fp >= fpBase && fsm.fp < fpOld, "bad fp in fsm");
            Assert(fsm.cb + fsm.fp > fsm.fp && fsm.cb + fsm.fp < fpOld, "bad cb in fsm");
            fpOld = fsm.fp;
            cbTot += fsm.cb;
        }
        Assert(cbTot <= _csto.fpMac - fpBase, "too much free space");
    }
    if (_cstoExtra.pglfsm != pvNil)
    {
        LegacyLong ifsm;
        FreeSpaceMap fsm;
        FilePosition fpOld;

        fpOld = _cstoExtra.fpMac;
        for (ifsm = _cstoExtra.pglfsm->IvMac(); ifsm-- != 0;)
        {
            _cstoExtra.pglfsm->Get(ifsm, &fsm);
            Assert(fsm.fp >= 0 && fsm.fp < fpOld, "bad fp in fsm");
            Assert(fsm.cb + fsm.fp > fsm.fp && fsm.cb + fsm.fp < fpOld, "bad cb in fsm");
            fpOld = fsm.fp;
            cbTotExtra += fsm.cb;
        }
        Assert(cbTotExtra <= _cstoExtra.fpMac, "too much free space on extra");
    }

    if (grfcfl & fcflGraph)
        Assert(_TValidIndex() != tNo, "bad index");

#ifndef CHUNK_BIG_INDEX
    // verify that the _pglrtie is in sorted order and all the chunks exist
    if (pvNil != _pglrtie && _pglrtie->IvMac() > 0)
    {
        LegacyLong irtie;
        RuntimeIDEntry rtie, rtiePrev;
        LegacyLong icrp;

        irtie = _pglrtie->IvMac() - 1;
        _pglrtie->Get(irtie, &rtie);
        Assert(rtiNil != rtie.rti && _FFindCtgCno(rtie.ctg, rtie.cno, &icrp), "Bad RuntimeIDEntry");

        while (irtie-- > 0)
        {
            rtiePrev = rtie;
            _pglrtie->Get(irtie, &rtie);
            Assert(rtiNil != rtie.rti && _FFindCtgCno(rtie.ctg, rtie.cno, &icrp), "Bad RuntimeIDEntry");
            Assert(rtie.ctg < rtiePrev.ctg || rtie.ctg == rtiePrev.ctg && rtie.cno < rtiePrev.cno, "RuntimeIDEntry out of order");
        }
    }
#endif //! CHUNK_BIG_INDEX

    ResumeAssertValid();
}

/***************************************************************************
    Mark memory used by the ChunkyFile
***************************************************************************/
void ChunkyFile::MarkMem(void)
{
    AssertThis(0);
    ChunkyFile_PAR::MarkMem();
    MarkMemObj(_pggcrp);
    MarkMemObj(_csto.pglfsm);
    MarkMemObj(_cstoExtra.pglfsm);
#ifndef CHUNK_BIG_INDEX
    MarkMemObj(_pglrtie);
#endif //! CHUNK_BIG_INDEX
}
#endif // DEBUG

/***************************************************************************
    Checks for cycles and other consistency in the chunky index.  If
    we find something wrong, return tNo.  If a memory error occurs,
    return tMaybe.  If it's AOK, return tYes.
***************************************************************************/
tribool ChunkyFile::_TValidIndex(void)
{
    // The C validator owns traversal state; only legacy scratch marks need clearing.
    for (LegacyLong i = 0; i < _pggcrp->IvMac(); ++i)
        static_cast<ChunkRepresentation *>(_pggcrp->QvFixedGet(i))->ClearGrfcrp(fcrpMarkT);
    auto access = _ChunkAccess();
    SuspendAssertValid();
    SuspendCheckPointers();
    const int result = three_mm_chunk_validate_graph(&access, kckidMax);
    ResumeAssertValid();
    ResumeCheckPointers();
    return result == THREE_MM_CHUNK_OK ? tYes : result == THREE_MM_CHUNK_MEMORY ? tMaybe : tNo;
}

/***************************************************************************
    Verifies that this is a chunky file and reads the index into memory.
    Sets the _fpFreeMap and _cbFreeMap fields and sets the _fFreeMapNotRead
    flag. The free map is read separately by _ReadFreeMap. This is to avoid
    hogging memory for the free map when we may never need it.
***************************************************************************/
bool ChunkyFile::_FReadIndex(void)
{
    AssertBaseThis(0);
    AssertPo(_csto.pfil, 0);
    Assert(!_fInvalidMainFile, 0);
    Assert(_pggcrp == pvNil && _csto.pglfsm == pvNil && _cstoExtra.pfil == pvNil && _cstoExtra.pglfsm == pvNil,
           "cfl has wrong non-nil entries");
    static_assert(kcvnCur == THREE_MM_CHUNK_CURRENT_VERSION && kcvnBack == THREE_MM_CHUNK_BACKWARD_VERSION &&
                  kcvnMin == 1 && sizeof(ChunkyFilePrefix) == THREE_MM_CHUNK_PREFIX_BYTES &&
                  sizeof(ChildChunkIdentification) == sizeof(ThreeMMChunkChild), "Chunk C89 wire contract");
    const auto end = _csto.pfil->FpMac();
    if (end < size(ChunkyFilePrefix))
        return fFalse;
    const short nativeOrder = 1;
    ThreeMMChunkPolicy policy = {};
#ifdef CHUNK_BIG_INDEX
    policy.expanded_runtime = 1;
#endif
    policy.native_big_endian = reinterpret_cast<const byte *>(&nativeOrder)[0] == 0;
    policy.max_name_bytes = kcbMaxDataStn;
    ThreeMMReadStream stream = {_csto.pfil, static_cast<ThreeMMU32>(end), ReadChunkFile};
    ThreeMMChunkIndex *index = nullptr;
    if (three_mm_chunk_index_open(&stream, &policy, &index) != THREE_MM_CHUNK_OK)
        return fFalse;
    struct IndexOwner
    {
        ThreeMMChunkIndex *value;
        ~IndexOwner() { three_mm_chunk_index_close(value); }
    } owner = {index};
    const auto header = *three_mm_chunk_index_header(index);
    auto access = three_mm_chunk_index_access(index);
    PGeneralGroup group = GeneralGroup::PggNew(size(ChunkRepresentation), access.count);
    if (!group)
        return fFalse;
    try
    {
        for (ThreeMMU32 i = 0; i < access.count; ++i)
        {
            ThreeMMChunkEntry entry;
            const unsigned char *name;
            ThreeMMU32 nameBytes;
            if (three_mm_chunk_at(&access, i, &entry) != THREE_MM_CHUNK_OK ||
                three_mm_chunk_index_name(index, i, &name, &nameBytes) != THREE_MM_CHUNK_OK)
                goto LFail;
            String converted;
            std::vector<byte> oldName;
            if (header.version < kcvnMinStnNames && nameBytes)
            {
                // Keep retail's pre-v3 SZS conversion, including discarding oversized names.
                SZS buffer;
                if (nameBytes < sizeof(buffer))
                {
                    CopyPb(const_cast<unsigned char *>(name), buffer, nameBytes);
                    buffer[nameBytes] = 0;
                    converted.SetSzs(buffer);
                    oldName.resize(converted.CbData());
                    converted.GetData(oldName.data());
                    name = oldName.data();
                    nameBytes = converted.CbData();
                }
                else
                    nameBytes = 0;
            }
            std::vector<byte> variable(entry.child_count * sizeof(ChildChunkIdentification) + nameBytes);
            for (ThreeMMU32 childIndex = 0; childIndex < entry.child_count; ++childIndex)
            {
                ThreeMMChunkChild child;
                if (three_mm_chunk_child_at(&access, i, childIndex, &child) != THREE_MM_CHUNK_OK)
                    goto LFail;
                ChildChunkIdentification native = {{child.type, child.number}, child.slot};
                CopyPb(&native, variable.data() + childIndex * sizeof(native), sizeof(native));
            }
            if (nameBytes)
                CopyPb(const_cast<unsigned char *>(name), variable.data() + entry.child_count * sizeof(ChildChunkIdentification), nameBytes);
            ChunkRepresentation crp;
            ClearPb(&crp, size(crp));
            crp.cki = {entry.type, entry.number};
            crp.fp = static_cast<LegacyLong>(entry.offset);
            crp.SetCb(static_cast<LegacyLong>(entry.bytes));
            crp.AssignGrfcrp(entry.flags);
            crp.ckid = static_cast<CKID>(entry.child_count);
            crp.ccrpRef = static_cast<CKID>(entry.parent_count);
            if (!group->FInsert(i, entry.child_count * sizeof(ChildChunkIdentification) + nameBytes, variable.data(), &crp))
                goto LFail;
        }
    }
    catch (const std::bad_alloc &)
    {
        ReleasePpo(&group);
        return fFalse;
    }
    _pggcrp = group;
    _cbFreeMap = header.map_bytes;
    _fpFreeMap = header.map_offset;
    _fFreeMapNotRead = header.map_bytes > 0;
    _csto.fpMac = header.index_offset;
    return fTrue;
LFail:
    ReleasePpo(&group);
    return fFalse;
}

/***************************************************************************
    Assert that the free map hasn't been read (and doesn't exist).  Read
    the free map from the file.  This _cannot_ be called after chunk
    data has been added to the main file (as opposed to the extra file).
    This _cannot_ be called before _FReadIndex has been called.
***************************************************************************/
void ChunkyFile::_ReadFreeMap(void)
{
    AssertBaseThis(0);
    Assert(_fFreeMapNotRead, "free map already read");
    Assert(!_fInvalidMainFile, 0);

    short bo;
    short osk;
    LegacyLong cfsm;

    // clear this even if reading the free map fails - so we don't try to
    // read again
    _fFreeMapNotRead = fFalse;

    if (_csto.pglfsm != pvNil)
    {
        Bug("free map already exists");
        return;
    }

    if (_cbFreeMap > 0)
    {
        if ((_csto.pglfsm = DynamicArray::PglRead(_csto.pfil, _fpFreeMap, _cbFreeMap, &bo, &osk)) == pvNil ||
            _csto.pglfsm->CbEntry() != size(FreeSpaceMap))
        {
            // it failed, but so what
            ReleasePpo(&_csto.pglfsm);
            return;
        }
        // swap bytes
        AssertBomRglw(kbomFsm, size(FreeSpaceMap));
        if (bo != kboCur && (cfsm = _csto.pglfsm->IvMac()) > 0)
        {
            SwapBytesRglw(_csto.pglfsm->QvGet(0), cfsm * (size(FreeSpaceMap) / size(int32_t)));
        }
    }
}

/***************************************************************************
    If we have don't have write permission or there's an extra file,
    write out a new file and do the rename stuff.  If not, just write
    the index and free map.
***************************************************************************/
namespace {
bool WriteChunkDirectory(PFileObject, PGeneralGroup, PDynamicArray, FilePosition, ChunkTagOrType);
struct ChunkSaveHost
{
    ChunkyFile *owner;
    Filename *directory, *target;
    PGeneralGroup index;
};
void SaveEntry(void *context, ThreeMMU32 position, ThreeMMChunkEntry *entry)
{
    AssertDo(ReadRuntimeChunk(context, position, entry), "invalid save index position");
}
void SavePlace(void *context, ThreeMMU32 position, ThreeMMU32 offset, ThreeMMU32 flags)
{
    auto row = static_cast<ChunkRepresentation *>(static_cast<PGeneralGroup>(context)->QvFixedGet(position));
    row->fp = offset;
    row->AssignGrfcrp(flags);
}
void *SaveCreateTemp(void *context) { return FileObject::PfilCreateTemp(static_cast<ChunkSaveHost *>(context)->directory); }
int SaveResize(void *, void *file, ThreeMMU32 bytes) { return static_cast<PFileObject>(file)->FSetFpMac(bytes); }
int SaveCopy(void *, void *source, ThreeMMU32 sourceOffset, void *destination, ThreeMMU32 offset, ThreeMMU32 bytes)
{
    FileLocation from = {static_cast<PFileObject>(source), static_cast<FilePosition>(sourceOffset), static_cast<LegacyLong>(bytes)};
    FileLocation to = {static_cast<PFileObject>(destination), static_cast<FilePosition>(offset), static_cast<LegacyLong>(bytes)};
    return from.FCopy(&to);
}
void SaveReleaseFile(void *, void *file) { auto native = static_cast<PFileObject>(file); ReleasePpo(&native); }
void SaveReleaseMap(void *, void *map) { auto native = static_cast<PDynamicArray>(map); ReleasePpo(&native); }
int SaveIsTemp(void *, void *file) { return static_cast<PFileObject>(file)->FTemp(); }
void SaveSetTemp(void *, void *file, int temporary) { static_cast<PFileObject>(file)->SetTemp(temporary != 0); }
int SaveIndex(void *context, const ThreeMMChunkSaveState *state, ThreeMMU32 creator)
{
    return WriteChunkDirectory(static_cast<PFileObject>(state->main_file), static_cast<ChunkSaveHost *>(context)->index,
        static_cast<PDynamicArray>(state->main_map), state->main_end, creator);
}
int SaveRename(void *context, void *file)
{
    auto target = static_cast<ChunkSaveHost *>(context)->target;
    if (target->TExists() != tNo) target->FDelete();
    return static_cast<PFileObject>(file)->FRename(target);
}
int SaveSwapNames(void *, void *file, void *old) { return static_cast<PFileObject>(file)->FSwapNames(static_cast<PFileObject>(old)); }
}
bool ChunkyFile::FSave(ChunkTagOrType ctgCreator, Filename *pfni)
{
    AssertThis(fcflFull | fcflGraph);
    AssertNilOrPo(pfni, ffniFile);
    Filename fni;
    if (_fInvalidMainFile)
    {
        if (!pfni) { Bug("can't save a ChunkyFile that has no file attached!"); return fFalse; }
        fni = *pfni;
    }
    else
    {
        GetFni(&fni);
        if (pfni)
        {
            if (pfni->FEqual(&fni)) pfni = nullptr;
            else fni = *pfni;
        }
    }
    ThreeMMChunkSaveState state = {_csto.pfil, _cstoExtra.pfil, _csto.pglfsm, _cstoExtra.pglfsm,
        static_cast<ThreeMMU32>(_csto.fpMac), static_cast<ThreeMMU32>(_cstoExtra.fpMac),
        _fInvalidMainFile, _fAddToExtra, _fFreeMapNotRead};
    ChunkSaveHost host = {this, &fni, pfni, _pggcrp};
    const auto publish = [](void *context, const ThreeMMChunkSaveState *saved) {
        auto self = static_cast<ChunkSaveHost *>(context)->owner;
        self->_csto.pfil = static_cast<PFileObject>(saved->main_file);
        self->_cstoExtra.pfil = static_cast<PFileObject>(saved->extra_file);
        self->_csto.pglfsm = static_cast<PDynamicArray>(saved->main_map);
        self->_cstoExtra.pglfsm = static_cast<PDynamicArray>(saved->extra_map);
        self->_csto.fpMac = saved->main_end; self->_cstoExtra.fpMac = saved->extra_end;
        self->_fInvalidMainFile = saved->invalid_main != 0; self->_fAddToExtra = saved->add_to_extra != 0;
        self->_fFreeMapNotRead = saved->free_map_unread != 0;
    };
    const ThreeMMChunkSaveIndex index = {_pggcrp, static_cast<ThreeMMU32>(_pggcrp->IvMac()), SaveEntry, SavePlace};
    const ThreeMMChunkSaveServices services = {&host, SaveCreateTemp, SaveResize, SaveCopy, SaveReleaseFile,
        SaveReleaseMap, SaveIsTemp, SaveSetTemp, SaveIndex, SaveRename, SaveSwapNames, publish};
    const bool ok = three_mm_chunk_save(&state, &index, &services, ctgCreator, pfni != nullptr) == THREE_MM_CHUNK_OK;
    publish(&host, &state);
    if (!ok) PushErc(ercCflSave);
    AssertThis(fcflFull | fcflGraph);
    return ok;
}

/***************************************************************************
    Write the chunky index and free map to the end of the file.
***************************************************************************/
namespace {
struct ChunkIndexOutput
{
    PFileObject file;
    PGeneralGroup index;
    PDynamicArray map;
};
int WriteChunkIndex(void *context, ThreeMMU32 offset, ThreeMMU32 bytes)
{
    auto output = static_cast<ChunkIndexOutput *>(context);
    DataBlock block(output->file, offset, bytes);
    return output->index->FWrite(&block);
}
int WriteChunkMap(void *context, ThreeMMU32 offset, ThreeMMU32 bytes)
{
    auto output = static_cast<ChunkIndexOutput *>(context);
    DataBlock block(output->file, offset, bytes);
    return output->map->FWrite(&block);
}
int WriteChunkPrefix(void *context, const void *bytes, size_t count)
{
    return static_cast<ChunkIndexOutput *>(context)->file->FWriteRgb(const_cast<void *>(bytes), count, 0);
}
}
namespace {
bool WriteChunkDirectory(PFileObject file, PGeneralGroup index, PDynamicArray map, FilePosition end, ChunkTagOrType creator)
{
    const short one = 1;
    ChunkIndexOutput output = {file, index, map};
    const ThreeMMChunkSave save = {creator, static_cast<ThreeMMU32>(end),
        static_cast<ThreeMMU32>(index->CbOnFile()), map ? static_cast<ThreeMMU32>(map->CbOnFile()) : 0,
        reinterpret_cast<const byte *>(&one)[0] == 0, koskCur};
    const ThreeMMChunkWriter writer = {&output, WriteChunkIndex, WriteChunkMap, WriteChunkPrefix};
    return three_mm_chunk_write_index(&save, &writer, nullptr) == THREE_MM_CHUNK_OK;
}
}
bool ChunkyFile::_FWriteIndex(ChunkTagOrType ctgCreator)
{
    AssertBaseThis(0);
    AssertPo(_pggcrp, 0);
    AssertPo(_csto.pfil, 0);
    return WriteChunkDirectory(_csto.pfil, _pggcrp, _csto.pglfsm, _csto.fpMac, ctgCreator);
}

/***************************************************************************
    Save a copy of the chunky file out to *pfni.  The ChunkyFile and its FileObject
    are untouched.
***************************************************************************/
namespace {
struct ChunkCopyHost { PChunkyFile destination; PGeneralGroup source, target; };
void CopyPin(void *context) { static_cast<ChunkCopyHost *>(context)->source->Lock(); }
void CopyUnpin(void *context) { static_cast<ChunkCopyHost *>(context)->source->Unlock(); }
int CopyInsert(void *context, ThreeMMU32 position, const ThreeMMChunkEntry *entry)
{
    auto host = static_cast<ChunkCopyHost *>(context);
    auto original = static_cast<ChunkRepresentation *>(host->source->QvFixedGet(position));
    ChunkRepresentation row = *original;
    row.fp = entry->offset; row.SetCb(entry->bytes); row.AssignGrfcrp(entry->flags);
    return host->target->FInsert(position, host->source->Cb(position), host->source->QvGet(position), &row);
}
}
bool ChunkyFile::FSaveACopy(ChunkTagOrType ctgCreator, Filename *pfni)
{
    AssertThis(fcflFull | fcflGraph);
    AssertPo(pfni, ffniFile);
    PChunkyFile destination = ChunkyFile::PcflCreate(pfni, fcflWriteEnable);
    if (!destination) { PushErc(ercCflSaveCopy); return fFalse; }
    ChunkCopyHost host = {destination, _pggcrp, destination->_pggcrp};
    const auto finish = [](void *context, ThreeMMU32 end, ThreeMMU32 creator) -> int {
        auto file = static_cast<ChunkCopyHost *>(context)->destination;
        file->_csto.fpMac = end;
        return file->FSave(creator);
    };
    const ThreeMMChunkSaveIndex index = {_pggcrp, static_cast<ThreeMMU32>(_pggcrp->IvMac()), SaveEntry, nullptr};
    const ThreeMMChunkCopyServices services = {&host, _csto.pfil, _cstoExtra.pfil, destination->_csto.pfil,
        CopyPin, CopyUnpin, SaveCopy, CopyInsert, finish};
    const bool ok = three_mm_chunk_save_copy(&index, &services, ctgCreator) == THREE_MM_CHUNK_OK;
    if (!ok) destination->SetTemp(fTrue);
    ReleasePpo(&destination);
    if (!ok) PushErc(ercCflSaveCopy);
    return ok;
}

/***************************************************************************
    Return whether the chunk's data is on the extra file.
***************************************************************************/
bool ChunkyFile::FOnExtra(ChunkTagOrType ctg, ChunkNumber cno)
{
    AssertThis(0);
    LegacyLong icrp;
    ChunkRepresentation *qcrp;

    if (!_FFindCtgCno(ctg, cno, &icrp))
        return fFalse;

    qcrp = (ChunkRepresentation *)_pggcrp->QvFixedGet(icrp);
    return FPure(qcrp->Grfcrp(fcrpOnExtra));
}

/***************************************************************************
    Make sure the given chunk's data is on the extra file. Fails iff the
    chunk doesn't exist or copying the data failed.
***************************************************************************/
bool ChunkyFile::FEnsureOnExtra(ChunkTagOrType ctg, ChunkNumber cno)
{
    AssertThis(0);
    LegacyLong icrp;

    if (!_FFindCtgCno(ctg, cno, &icrp))
        return fFalse;

    return _FEnsureOnExtra(icrp);
}

/***************************************************************************
    Make sure the given chunk's data is on the extra file. Optionally get
    the flo for the data.
***************************************************************************/
bool ChunkyFile::_FEnsureOnExtra(LegacyLong icrp, FileLocation *pflo)
{
    AssertThis(0);
    AssertNilOrVarMem(pflo);

    ChunkRepresentation *qcrp;

    qcrp = (ChunkRepresentation *)_pggcrp->QvFixedGet(icrp);
    if (qcrp->Grfcrp(fcrpOnExtra) || qcrp->Cb() == 0)
    {
        if (pvNil != pflo)
        {
            pflo->pfil = _cstoExtra.pfil;
            pflo->fp = qcrp->fp;
            pflo->cb = qcrp->Cb();
        }
    }
    else
    {
        FileLocation floSrc, floDst;

#ifdef CHUNK_STATS
        if (vfDumpChunkRequests)
        {
            String stn;

            qcrp = (ChunkRepresentation *)_pggcrp->QvFixedGet(icrp);
            stn.FFormatSz(PszLit("Cache: '%f', 0x%08x, fp = 0x%08x, cb = 0x%08x"), qcrp->cki.ctg, qcrp->cki.cno,
                          qcrp->fp, qcrp->Cb());
            DumpStn(&stn, _csto.pfil);
        }
#endif // CHUNK_STATS

        floSrc.pfil = _csto.pfil;
        floSrc.fp = qcrp->fp;
        floSrc.cb = qcrp->Cb();

        if (!_FAllocFlo(floSrc.cb, &floDst, fTrue))
            return fFalse;
        if (!floSrc.FCopy(&floDst))
        {
            _FreeFpCb(fTrue, floDst.fp, floDst.cb);
            return fFalse;
        }

        qcrp = (ChunkRepresentation *)_pggcrp->QvFixedGet(icrp);
        qcrp->SetGrfcrp(fcrpOnExtra);
        qcrp->fp = floDst.fp;
        if (pvNil != pflo)
            *pflo = floDst;
    }

#ifdef CHUNK_STATS
    if (pvNil != pflo && vfDumpChunkRequests)
    {
        String stn;

        qcrp = (ChunkRepresentation *)_pggcrp->QvFixedGet(icrp);
        stn.FFormatSz(PszLit("Fetch from extra: '%f', 0x%08x, fp = 0x%08x, cb = 0x%08x"), qcrp->cki.ctg, qcrp->cki.cno,
                      qcrp->fp, qcrp->Cb());
        DumpStn(&stn, _csto.pfil);
    }
#endif // CHUNK_STATS

    return fTrue;
}

/***************************************************************************
    Get the FileLocation from the chunk.
***************************************************************************/
void ChunkyFile::_GetFlo(LegacyLong icrp, PFileLocation pflo)
{
    AssertThis(0);
    AssertVarMem(pflo);

    if (!_fReadFromExtra || !_FEnsureOnExtra(icrp, pflo))
    {
        ChunkRepresentation *qcrp;

        qcrp = (ChunkRepresentation *)_pggcrp->QvFixedGet(icrp);
        pflo->pfil = qcrp->Grfcrp(fcrpOnExtra) ? _cstoExtra.pfil : _csto.pfil;
        pflo->fp = qcrp->fp;
        pflo->cb = qcrp->Cb();

#ifdef CHUNK_STATS
        if (vfDumpChunkRequests)
        {
            String stn;

            qcrp = (ChunkRepresentation *)_pggcrp->QvFixedGet(icrp);
            stn.FFormatSz(PszLit("Fetch from %z: '%f', 0x%08x, fp = 0x%08x, cb = 0x%08x"),
                          qcrp->Grfcrp(fcrpOnExtra) ? PszLit("extra") : PszLit("main"), qcrp->cki.ctg, qcrp->cki.cno,
                          qcrp->fp, qcrp->Cb());
            DumpStn(&stn, _csto.pfil);
        }
#endif // CHUNK_STATS
    }
    AssertPo(pflo->pfil, 0);
}

/***************************************************************************
    Get the DataBlock from the chunk.
***************************************************************************/
void ChunkyFile::_GetBlck(LegacyLong icrp, PDataBlock pblck)
{
    AssertThis(0);
    AssertPo(pblck, 0);

    FileLocation flo;
    ChunkRepresentation *qcrp;

    _GetFlo(icrp, &flo);
    qcrp = (ChunkRepresentation *)_pggcrp->QvFixedGet(icrp);
    pblck->Set(&flo, qcrp->Grfcrp(fcrpPacked));
}

/***************************************************************************
    Map the (ctg, cno) to a DataBlock.
***************************************************************************/
bool ChunkyFile::FFind(ChunkTagOrType ctg, ChunkNumber cno, DataBlock *pblck)
{
    AssertThis(0);
    AssertNilOrPo(pblck, 0);

    LegacyLong icrp;

    if (!_FFindCtgCno(ctg, cno, &icrp))
    {
        if (pvNil != pblck)
            pblck->Free();
        return fFalse;
    }

    if (pvNil != pblck)
        _GetBlck(icrp, pblck);

    return fTrue;
}

/***************************************************************************
    Map the (ctg, cno) to a pflo.
***************************************************************************/
bool ChunkyFile::FFindFlo(ChunkTagOrType ctg, ChunkNumber cno, PFileLocation pflo)
{
    AssertThis(0);
    AssertVarMem(pflo);

    LegacyLong icrp;

    if (!_FFindCtgCno(ctg, cno, &icrp))
    {
        TrashVar(pflo);
        return fFalse;
    }

    _GetFlo(icrp, pflo);
    return fTrue;
}

/***************************************************************************
    Reads the chunk (if it exists) into an hq.  Returns false if the
    chunk doesn't exist or something failed, in which case *phq is set
    to hqNil.  Doesn't unpack the data if it's packed.
***************************************************************************/
bool ChunkyFile::FReadHq(ChunkTagOrType ctg, ChunkNumber cno, HQ *phq)
{
    AssertThis(0);
    AssertVarMem(phq);
    DataBlock blck;

    *phq = hqNil; // in case FFind fails
    return FFind(ctg, cno, &blck) && blck.FReadHq(phq, fTrue);
}

/***************************************************************************
    Make sure the packed flag is set or clear according to fPacked.
    This doesn't affect the data at all.
***************************************************************************/
void ChunkyFile::SetPacked(ChunkTagOrType ctg, ChunkNumber cno, bool fPacked)
{
    AssertThis(0);
    LegacyLong icrp;
    ChunkRepresentation *qcrp;

    if (!_FFindCtgCno(ctg, cno, &icrp))
    {
        Bug("chunk not there");
        return;
    }
    qcrp = (ChunkRepresentation *)_pggcrp->QvFixedGet(icrp);
    Assert(qcrp->cki.ctg == ctg && qcrp->cki.cno == cno, 0);
    if (FPure(qcrp->Grfcrp(fcrpPacked)) != FPure(fPacked))
    {
        if (fPacked)
            qcrp->SetGrfcrp(fcrpPacked);
        else
            qcrp->ClearGrfcrp(fcrpPacked);
        _FSetRti(ctg, cno, rtiNil);
    }
}

/***************************************************************************
    Return the value of the packed flag.
***************************************************************************/
bool ChunkyFile::FPacked(ChunkTagOrType ctg, ChunkNumber cno)
{
    AssertThis(0);
    LegacyLong icrp;
    ChunkRepresentation *qcrp;

    if (!_FFindCtgCno(ctg, cno, &icrp))
    {
        Bug("chunk not there");
        return fFalse;
    }
    qcrp = (ChunkRepresentation *)_pggcrp->QvFixedGet(icrp);
    Assert(qcrp->cki.ctg == ctg && qcrp->cki.cno == cno, 0);
    return FPure(qcrp->Grfcrp(fcrpPacked));
}

/***************************************************************************
    If the data for the chunk is packed, unpack it.
***************************************************************************/
bool ChunkyFile::FUnpackData(ChunkTagOrType ctg, ChunkNumber cno)
{
    AssertThis(0);
    DataBlock blck;

    if (!FPacked(ctg, cno))
        return fTrue;

    AssertDo(FFind(ctg, cno, &blck), 0);
    if (!blck.FUnpackData())
        return fFalse;
    return FPutBlck(&blck, ctg, cno);
}

/***************************************************************************
    If the data isn't already packed, pack it.
***************************************************************************/
bool ChunkyFile::FPackData(ChunkTagOrType ctg, ChunkNumber cno)
{
    AssertThis(0);
    DataBlock blck;

    if (FPacked(ctg, cno))
        return fTrue;

    AssertDo(FFind(ctg, cno, &blck), 0);
    if (!blck.FPackData())
        return fFalse;
    return FPutBlck(&blck, ctg, cno);
}

/***************************************************************************
    Create the extra file.  Note: the extra file doesn't have a ChunkyFilePrefix -
    just raw data.
***************************************************************************/
bool ChunkyFile::_FCreateExtra(void)
{
    AssertBaseThis(0);
    Assert(_cstoExtra.pfil == pvNil, 0);
    Assert(_cstoExtra.pglfsm == pvNil, 0);

    if (pvNil == (_cstoExtra.pfil = FileObject::PfilCreateTemp()))
        return fFalse;

    _cstoExtra.fpMac = 0;
    return fTrue;
}

/***************************************************************************
    Find a place to put a block the given size.
***************************************************************************/
namespace {
struct ChunkHeapHost { PFileObject file; PDynamicArray *map; };
ThreeMMU32 HeapCount(void *context)
{
    auto map = *static_cast<ChunkHeapHost *>(context)->map;
    return map ? map->IvMac() : 0;
}
void HeapGet(void *context, ThreeMMU32 position, ThreeMMChunkFreeRange *range)
{
    FreeSpaceMap native; (*static_cast<ChunkHeapHost *>(context)->map)->Get(position, &native);
    *range = {static_cast<ThreeMMU32>(native.fp), static_cast<ThreeMMU32>(native.cb)};
}
void HeapPut(void *context, ThreeMMU32 position, const ThreeMMChunkFreeRange *range)
{
    FreeSpaceMap native = {static_cast<int32_t>(range->offset), static_cast<int32_t>(range->bytes)};
    (*static_cast<ChunkHeapHost *>(context)->map)->Put(position, &native);
}
void HeapErase(void *context, ThreeMMU32 position) { (*static_cast<ChunkHeapHost *>(context)->map)->Delete(position); }
int HeapInsert(void *context, ThreeMMU32 position, const ThreeMMChunkFreeRange *range)
{
    FreeSpaceMap native = {static_cast<int32_t>(range->offset), static_cast<int32_t>(range->bytes)};
    return (*static_cast<ChunkHeapHost *>(context)->map)->FInsert(position, &native);
}
int HeapEnsure(void *context)
{
    auto map = static_cast<ChunkHeapHost *>(context)->map;
    if (!*map) *map = DynamicArray::PglNew(sizeof(FreeSpaceMap), 1);
    return *map != nullptr;
}
ThreeMMU32 HeapPhysicalEnd(void *context) { return static_cast<ChunkHeapHost *>(context)->file->FpMac(); }
int HeapResize(void *context, ThreeMMU32 bytes) { return static_cast<ChunkHeapHost *>(context)->file->FSetFpMac(bytes); }
ThreeMMChunkHeap ChunkHeapServices(ChunkHeapHost *host)
{
    return {host, HeapCount, HeapGet, HeapPut, HeapErase, HeapInsert, HeapEnsure, HeapPhysicalEnd, HeapResize};
}
}
bool ChunkyFile::_FAllocFlo(LegacyLong cb, PFileLocation pflo, bool fForceOnExtra)
{
    AssertBaseThis(0);
    AssertIn(cb, 0, kcbMax);
    AssertVarMem(pflo);

    ChunkStorage *pcsto;

    if (cb > kcbMaxCrp)
    {
        Bug("Requested FileLocation too big");
        return fFalse;
    }

    if ((fForceOnExtra || _fAddToExtra) && cb > 0)
    {
        pcsto = &_cstoExtra;
        if (pcsto->pfil == pvNil && !_FCreateExtra())
        {
            TrashVar(pflo);
            return fFalse;
        }
    }
    else
    {
        // Empty chunks reserve no storage, including on read-only files whose
        // free-space map is loaded only when primary storage becomes writable.
        Assert(cb == 0 || !_fFreeMapNotRead, "free map not read yet");
        pcsto = &_csto;
    }

    // set the file and cb - just need to find an fp
    AssertPo(pcsto->pfil, 0);
    pflo->pfil = pcsto->pfil;
    pflo->cb = cb;

    if (cb <= 0)
    {
        pflo->fp = 0;
        pflo->cb = 0; // for safety
        return fTrue;
    }

    ChunkHeapHost host = {pcsto->pfil, &pcsto->pglfsm};
    const auto heap = ChunkHeapServices(&host);
    ThreeMMU32 end = pcsto->fpMac, offset = 0;
    if (three_mm_chunk_heap_allocate(&heap, &end, cb, &offset) != THREE_MM_CHUNK_OK)
    {
        TrashVar(pflo); return fFalse;
    }
    pcsto->fpMac = end; pflo->fp = offset;
    return fTrue;
}

/***************************************************************************
    Look for the (ctg, cno) pair.  Fills *picrp with where it should be.
    Returns whether or not it was found.  Assumes the _pggcrp is sorted
    by (ctg, cno).  Does a binary search.
***************************************************************************/
bool ChunkyFile::_FFindCtgCno(ChunkTagOrType ctg, ChunkNumber cno, LegacyLong *picrp)
{
    // Called during AssertValid; callbacks access GG directly without re-entering ChunkyFile.
    AssertBaseThis(0);
    AssertVarMem(picrp);
    auto access = _ChunkAccess();
    ThreeMMU32 position = 0;
    const int result = three_mm_chunk_find(&access, ctg, cno, &position);
    *picrp = static_cast<LegacyLong>(position);
    return result == THREE_MM_CHUNK_OK;
}

/***************************************************************************
    Find an unused cno for the given ctg.  Fill in *picrp and *pcno.
***************************************************************************/
void ChunkyFile::_GetUniqueCno(ChunkTagOrType ctg, LegacyLong *picrp, ChunkNumber *pcno)
{
    AssertBaseThis(0);
    AssertVarMem(picrp);
    AssertVarMem(pcno);

    LegacyLong icrp, ccrp;
    ChunkNumber cno;
    ChunkRepresentation *qcrp;

    if (!_FFindCtgCno(ctg, 0, picrp))
    {
        *pcno = 0;
        return;
    }

    // 0 already exists so do a linear search for the first useable slot
    ccrp = _pggcrp->IvMac();
    for (icrp = *picrp + 1, cno = 1; icrp < ccrp; icrp++, cno++)
    {
        qcrp = (ChunkRepresentation *)_pggcrp->QvFixedGet(icrp);
        if (qcrp->cki.ctg != ctg || qcrp->cki.cno != cno)
            break;
    }
    *picrp = icrp;
    *pcno = cno;
}

/***************************************************************************
    Add a new chunk.
***************************************************************************/
bool ChunkyFile::FAdd(LegacyLong cb, ChunkTagOrType ctg, ChunkNumber *pcno, PDataBlock pblck)
{
    AssertThis(0);
    AssertIn(cb, 0, kcbMax);
    AssertVarMem(pcno);
    AssertNilOrPo(pblck, 0);
    LegacyLong icrp;

    _GetUniqueCno(ctg, &icrp, pcno);
    if (!_FAdd(cb, ctg, *pcno, icrp, pblck))
    {
        TrashVar(pcno);
        AssertThis(0);
        return fFalse;
    }
    AssertThis(0);
    return fTrue;
}

/***************************************************************************
    Add a new chunk and write the pv to it.
***************************************************************************/
bool ChunkyFile::FAddPv(void *pv, LegacyLong cb, ChunkTagOrType ctg, ChunkNumber *pcno)
{
    AssertThis(0);
    AssertVarMem(pcno);
    AssertIn(cb, 0, kcbMax);
    AssertPvCb(pv, cb);
    DataBlock blck;

    if (!FAdd(cb, ctg, pcno, &blck))
        return fFalse;
    if (!blck.FWrite(pv))
    {
        Delete(ctg, *pcno);
        TrashVar(pcno);
        return fFalse;
    }
    return fTrue;
}

/***************************************************************************
    Add a new chunk and write the hq to it.
***************************************************************************/
bool ChunkyFile::FAddHq(HQ hq, ChunkTagOrType ctg, ChunkNumber *pcno)
{
    AssertThis(0);
    AssertVarMem(pcno);
    AssertHq(hq);

    DataBlock blck;
    LegacyLong cb = CbOfHq(hq);

    if (!FAdd(cb, ctg, pcno, &blck))
        return fFalse;
    if (!blck.FWriteHq(hq, 0))
    {
        Delete(ctg, *pcno);
        TrashVar(pcno);
        return fFalse;
    }
    return fTrue;
}

/***************************************************************************
    Add a new chunk and write the block to it.
***************************************************************************/
bool ChunkyFile::FAddBlck(PDataBlock pblckSrc, ChunkTagOrType ctg, ChunkNumber *pcno)
{
    AssertThis(0);
    AssertPo(pblckSrc, 0);
    AssertVarMem(pcno);

    DataBlock blck;
    LegacyLong cb = pblckSrc->Cb(fTrue);

    if (!FAdd(cb, ctg, pcno, &blck))
        return fFalse;
    if (!pblckSrc->FWriteToBlck(&blck, fTrue))
    {
        Delete(ctg, *pcno);
        TrashVar(pcno);
        return fFalse;
    }
    if (pblckSrc->FPacked())
        SetPacked(ctg, *pcno, fTrue);

    return fTrue;
}

/***************************************************************************
    Adds the chunk and makes it a child of (ctgPar, cnoPar). The loner flag
    of the new chunk will be clear.
***************************************************************************/
bool ChunkyFile::FAddChild(ChunkTagOrType ctgPar, ChunkNumber cnoPar, ChildChunkID chid, LegacyLong cb, ChunkTagOrType ctg, ChunkNumber *pcno, PDataBlock pblck)
{
    AssertThis(0);
    AssertVarMem(pcno);
    AssertNilOrPo(pblck, 0);

    if (!FAdd(cb, ctg, pcno, pblck))
        return fFalse;
    if (!FAdoptChild(ctgPar, cnoPar, ctg, *pcno, chid, fTrue))
    {
        Delete(ctg, *pcno);
        TrashVar(pcno);
        if (pvNil != pblck)
            pblck->Free();
        return fFalse;
    }
    return fTrue;
}

/***************************************************************************
    Adds the chunk and makes it a child of (ctgPar, cnoPar).  The child's
    loner flag will be clear.
***************************************************************************/
bool ChunkyFile::FAddChildPv(ChunkTagOrType ctgPar, ChunkNumber cnoPar, ChildChunkID chid, void *pv, LegacyLong cb, ChunkTagOrType ctg, ChunkNumber *pcno)
{
    if (!FAddPv(pv, cb, ctg, pcno))
        return fFalse;
    if (!FAdoptChild(ctgPar, cnoPar, ctg, *pcno, chid, fTrue))
    {
        Delete(ctg, *pcno);
        TrashVar(pcno);
        return fFalse;
    }
    return fTrue;
}

/***************************************************************************
    Adds the chunk and makes it a child of (ctgPar, cnoPar).  The child's
    loner flag will be clear.
***************************************************************************/
bool ChunkyFile::FAddChildHq(ChunkTagOrType ctgPar, ChunkNumber cnoPar, ChildChunkID chid, HQ hq, ChunkTagOrType ctg, ChunkNumber *pcno)
{
    if (!FAddHq(hq, ctg, pcno))
        return fFalse;
    if (!FAdoptChild(ctgPar, cnoPar, ctg, *pcno, chid, fTrue))
    {
        Delete(ctg, *pcno);
        TrashVar(pcno);
        return fFalse;
    }
    return fTrue;
}

/***************************************************************************
    Low level add.  Sets the loner flag.
***************************************************************************/
bool ChunkyFile::_FAdd(LegacyLong cb, ChunkTagOrType ctg, ChunkNumber cno, LegacyLong icrp, PDataBlock pblck)
{
    AssertBaseThis(0);
    AssertIn(cb, 0, kcbMax);
    AssertNilOrPo(pblck, 0);

    ChunkRepresentation *qcrp;
    FileLocation flo;

    if (!_pggcrp->FInsert(icrp, 0))
    {
        if (pvNil != pblck)
            pblck->Free();
        return fFalse;
    }

    if (!_FAllocFlo(cb, &flo))
    {
        _pggcrp->Delete(icrp);
        AssertThis(0);
        if (pvNil != pblck)
            pblck->Free();
        return fFalse;
    }
    Assert(flo.pfil == _csto.pfil || flo.pfil == _cstoExtra.pfil, 0);

    qcrp = (ChunkRepresentation *)_pggcrp->QvFixedGet(icrp);
    ClearPb(qcrp, size(ChunkRepresentation));
    qcrp->cki.ctg = ctg;
    qcrp->cki.cno = cno;
    qcrp->fp = flo.fp;
    qcrp->SetCb(flo.cb);
    if (flo.pfil == _cstoExtra.pfil)
        qcrp->SetGrfcrp(fcrpOnExtra);
    qcrp->SetGrfcrp(fcrpLoner);

    if (pvNil != pblck)
        pblck->Set(&flo);
    AssertThis(0);
    return fTrue;
}

/***************************************************************************
    Replace or create a chunk of a particular cno.
***************************************************************************/
bool ChunkyFile::FPut(LegacyLong cb, ChunkTagOrType ctg, ChunkNumber cno, PDataBlock pblck)
{
    AssertThis(0);
    AssertNilOrPo(pblck, 0);

    return _FPut(cb, ctg, cno, pblck, pvNil, pvNil);
}

/***************************************************************************
    Replace or create a chunk with the given cno and put the data in it.
***************************************************************************/
bool ChunkyFile::FPutPv(void *pv, LegacyLong cb, ChunkTagOrType ctg, ChunkNumber cno)
{
    AssertThis(0);
    AssertIn(cb, 0, kcbMax);
    AssertPvCb(pv, cb);

    return _FPut(cb, ctg, cno, pvNil, pvNil, pv);
}

/***************************************************************************
    Replace or create a chunk with the given cno and put the hq's
    data in it.
***************************************************************************/
bool ChunkyFile::FPutHq(HQ hq, ChunkTagOrType ctg, ChunkNumber cno)
{
    AssertThis(0);
    AssertHq(hq);
    bool fRet;
    HQ hqT = hq;
    DataBlock blckSrc(&hq);

    fRet = _FPut(blckSrc.Cb(), ctg, cno, pvNil, &blckSrc, pvNil);
    AssertDo(hqT == blckSrc.HqFree(), "blckSrc.HqFree() returned a differnt hq!");

    return fRet;
}

/***************************************************************************
    Replace or create a chunk with the given cno and put the block's
    data in it.  Set the packed flag as in the block.
***************************************************************************/
bool ChunkyFile::FPutBlck(PDataBlock pblckSrc, ChunkTagOrType ctg, ChunkNumber cno)
{
    AssertThis(0);
    AssertPo(pblckSrc, 0);

    if (!_FPut(pblckSrc->Cb(fTrue), ctg, cno, pvNil, pblckSrc, pvNil))
        return pvNil;
    SetPacked(ctg, cno, pblckSrc->FPacked());
    return fTrue;
}

/***************************************************************************
    Low level put.  Writes data from pblckSrc or pv (or neither).  If the
    chunk doesn't already exist, this has the same affect as doing an add.
    If it does exist, this doesn't change the fcrpLoner flag or anything
    else except the data.  If pblck isn't nil, this sets it to the location
    of the new data.  Doesn't change the packed flag.
***************************************************************************/
bool ChunkyFile::_FPut(LegacyLong cb, ChunkTagOrType ctg, ChunkNumber cno, PDataBlock pblck, PDataBlock pblckSrc, void *pv)
{
    AssertBaseThis(0);
    AssertIn(cb, 0, kcbMax);
    AssertNilOrPo(pblck, 0);
    AssertNilOrPo(pblckSrc, 0);

    LegacyLong icrp;
    ChunkRepresentation *qcrp;
    FileLocation flo;

    if (!_FFindCtgCno(ctg, cno, &icrp))
    {
        DataBlock blck;

        if (pvNil == pblck)
            pblck = &blck;

        if (!_FAdd(cb, ctg, cno, icrp, pblck))
            return fFalse;

        if (pv != pvNil)
        {
            Assert(pvNil == pblckSrc, 0);
            if (!pblck->FWrite(pv))
                goto LFailNew;
        }
        else if (pvNil != pblckSrc && !pblckSrc->FWriteToBlck(pblck, fTrue))
        {
        LFailNew:
            Delete(ctg, cno);
            pblck->Free();
            AssertThis(0);
            return fFalse;
        }

        AssertThis(0);
        return fTrue;
    }

    if (!_FAllocFlo(cb, &flo))
    {
        AssertThis(0);
        return fFalse;
    }
    Assert(flo.pfil == _csto.pfil || flo.pfil == _cstoExtra.pfil, 0);
    if (pvNil != pblck)
        pblck->Set(&flo);

    if (pv != pvNil)
    {
        Assert(pvNil == pblckSrc, 0);
        if (!flo.FWrite(pv))
            goto LFailOld;
    }
    else if (pvNil != pblckSrc && !pblckSrc->FWriteToFlo(&flo, fTrue))
    {
    LFailOld:
        _FreeFpCb(_fAddToExtra, flo.fp, flo.cb);
        if (pvNil != pblck)
            pblck->Free();
        AssertThis(0);
        return fFalse;
    }

    qcrp = (ChunkRepresentation *)_pggcrp->QvFixedGet(icrp);
    Assert(qcrp->cki.ctg == ctg, 0);
    Assert(qcrp->cki.cno == cno, 0);
    _FreeFpCb(qcrp->Grfcrp(fcrpOnExtra), qcrp->fp, qcrp->Cb());

    qcrp = (ChunkRepresentation *)_pggcrp->QvFixedGet(icrp);
    qcrp->fp = flo.fp;
    qcrp->SetCb(flo.cb);
    if (flo.pfil == _cstoExtra.pfil)
        qcrp->SetGrfcrp(fcrpOnExtra);
    else
        qcrp->ClearGrfcrp(fcrpOnExtra);
    _FSetRti(ctg, cno, rtiNil);

    AssertThis(0);
    return fTrue;
}

/***************************************************************************
    Swaps the data for the two chunks.  This allows a "safe save" of
    individual chunks (create a temp chunk, write the data, swap the data,
    delete the temp chunk).  Doesn't affect child/parent relationships
    or the loner flag.
***************************************************************************/
void ChunkyFile::SwapData(ChunkTagOrType ctg1, ChunkNumber cno1, ChunkTagOrType ctg2, ChunkNumber cno2)
{
    AssertThis(0);
    LegacyLong icrp1, icrp2;
    ChunkRepresentation *qcrp1, *qcrp2;
    FilePosition fp;
    LegacyLong cb;
    LegacyUlong grfcrpT;
    const LegacyUlong kgrfcrpMask = fcrpOnExtra | fcrpPacked | fcrpForest;

    if (!_FFindCtgCno(ctg1, cno1, &icrp1) || !_FFindCtgCno(ctg2, cno2, &icrp2))
    {
        Bug("can't find the chunks");
        return;
    }
    qcrp1 = (ChunkRepresentation *)_pggcrp->QvFixedGet(icrp1);
    qcrp2 = (ChunkRepresentation *)_pggcrp->QvFixedGet(icrp2);

    fp = qcrp1->fp;
    qcrp1->fp = qcrp2->fp;
    qcrp2->fp = fp;
    cb = qcrp1->Cb();
    qcrp1->SetCb(qcrp2->Cb());
    qcrp2->SetCb(cb);

    // swap the bits of grfcrp in grfcrpMask
    grfcrpT = qcrp1->Grfcrp(kgrfcrpMask);
    qcrp1->AssignGrfcrp(qcrp2->Grfcrp(kgrfcrpMask), kgrfcrpMask);
    qcrp2->AssignGrfcrp(grfcrpT, kgrfcrpMask);

    _FSetRti(ctg1, cno1, rtiNil);
    _FSetRti(ctg2, cno2, rtiNil);

    AssertThis(fcflFull);
}

/***************************************************************************
    Swaps the children for the two chunks.  This allows a "safe save" of
    chunk graphs (create a temp chunk, write the data, swap the data,
    swap the children, delete the temp chunk).  Doesn't affect the data
    or the loner flag.
***************************************************************************/
void ChunkyFile::SwapChildren(ChunkTagOrType ctg1, ChunkNumber cno1, ChunkTagOrType ctg2, ChunkNumber cno2)
{
    AssertThis(0);
    LegacyLong icrp1, icrp2;
    ChunkRepresentation *qcrp1, *qcrp2;
    LegacyLong cb1, cb2;

    if (!_FFindCtgCno(ctg1, cno1, &icrp1) || !_FFindCtgCno(ctg2, cno2, &icrp2))
    {
        Bug("can't find the chunks");
        return;
    }

    // Swap the child lists.
    qcrp1 = (ChunkRepresentation *)_pggcrp->QvFixedGet(icrp1);
    qcrp2 = (ChunkRepresentation *)_pggcrp->QvFixedGet(icrp2);
    cb1 = LwMul(qcrp1->ckid, size(ChildChunkIdentification));
    cb2 = LwMul(qcrp2->ckid, size(ChildChunkIdentification));
    SwapVars(&qcrp1->ckid, &qcrp2->ckid);

    // These FMoveRgb calls won't fail, because no padding is necessary for
    // child entries. (FMoveRgb can fail only if the number of bytes being
    // moved is not a multiple of size(long)).
    if (0 < cb1)
        AssertDo(_pggcrp->FMoveRgb(icrp1, 0, icrp2, cb2, cb1), 0);
    if (0 < cb2)
        AssertDo(_pggcrp->FMoveRgb(icrp2, 0, icrp1, 0, cb2), 0);

    AssertThis(fcflFull);
}

/***************************************************************************
    Move the chunk from (ctg, cno) to (ctgNew, cnoNew).  Asserts that there
    is not already a chunk labelled (ctgNew, cnoNew).  If the chunk has
    parents, updates the parent links to point to (ctgNew, cnoNew).
    Adjusting the links is slow.
***************************************************************************/
void ChunkyFile::Move(ChunkTagOrType ctg, ChunkNumber cno, ChunkTagOrType ctgNew, ChunkNumber cnoNew)
{
    AssertThis(0);
    LegacyLong icrpCur, icrpTarget;
    ChunkRepresentation *qcrp;
    LegacyLong ccrpRef;
    LegacyLong rti;

    if (ctg == ctgNew && cno == cnoNew)
        return;

    if (!_FFindCtgCno(ctg, cno, &icrpCur) || _FFindCtgCno(ctgNew, cnoNew, &icrpTarget))
    {
        Bug("bad move request");
        return;
    }

    rti = _Rti(ctg, cno);
    if (rtiNil != rti)
        _FSetRti(ctg, cno, rtiNil);

    qcrp = (ChunkRepresentation *)_pggcrp->QvFixedGet(icrpCur);
    ccrpRef = qcrp->ccrpRef;
    qcrp->cki.ctg = ctgNew;
    qcrp->cki.cno = cnoNew;
    _pggcrp->Move(icrpCur, icrpTarget);

    if (ccrpRef > 0)
    {
        // chunk has some parents
        ChunkRepresentation crp;
        LegacyLong icrp, ikid, ikidNew;
        ChildChunkIdentification *qkid, *qrgkid;

        // In debug, increment ccrpRef so we'll traverse the entire
        // index.  In ship, we'll stop once we changed ccrpRef KIDs.
        Debug(ccrpRef++;) for (icrp = _pggcrp->IvMac(); icrp-- > 0 && ccrpRef > 0;)
        {
            _pggcrp->GetFixed(icrp, &crp);
            qkid = (ChildChunkIdentification *)_pggcrp->QvGet(icrp);
            for (ikid = 0; ikid < crp.ckid;)
            {
                if (qkid->cki.ctg != ctg || qkid->cki.cno != cno)
                {
                    ikid++;
                    qkid++;
                    continue;
                }

                // replace this kid
                AssertDo(!_FFindChild(icrp, ctgNew, cnoNew, qkid->chid, &ikidNew), "already a child");

                // refresh the qkid and qrgkid pointers
                qkid = (qrgkid = (ChildChunkIdentification *)_pggcrp->QvGet(icrp)) + ikid;
                qkid->cki.ctg = ctgNew;
                qkid->cki.cno = cnoNew;

                MoveElement(qrgkid, size(ChildChunkIdentification), ikid, ikidNew);
                ccrpRef--;
            }
        }
        Assert(ccrpRef == 1, "corrupt chunky index");
        // in ship, ccrpRef should be 0 here
    }

    if (rtiNil != rti && ctgNew == ctg)
        _FSetRti(ctgNew, cnoNew, rti);

    AssertThis(fcflFull);
}

/***************************************************************************
    Delete the given chunk.  Handles deleting child chunks that are
    no longer referenced.  If the chunk has the loner flag set, this clears
    it.  If the chunk has no parents, the chunk is also physically deleted
    from the ChunkyFile.
***************************************************************************/
void ChunkyFile::Delete(ChunkTagOrType ctg, ChunkNumber cno)
{
    AssertThis(0);
    LegacyLong icrp;
    ChunkGraphEnumerator cge;
    LegacyUlong grfcgeIn, grfcge;
    ChildChunkIdentification kid;
    ChunkRepresentation *qcrp;

    if (!_FFindCtgCno(ctg, cno, &icrp))
    {
        Bug("chunk not there");
        return;
    }
    qcrp = (ChunkRepresentation *)_pggcrp->QvFixedGet(icrp);
    Assert(qcrp->Grfcrp(fcrpLoner) || qcrp->ccrpRef == 0, "can't directly delete a child chunk");
    qcrp->ClearGrfcrp(fcrpLoner);
    if (qcrp->ccrpRef > 0)
        return;

    cge.Init(this, ctg, cno);
    for (grfcgeIn = fcgeNil; cge.FNextKid(&kid, pvNil, &grfcge, grfcgeIn);)
    {
        grfcgeIn = fcgeSkipToSib;
        if (!_FFindCtgCno(kid.cki.ctg, kid.cki.cno, &icrp))
        {
            Bug("MIA");
            continue;
        }
        if (grfcge & fcgePre)
        {
            if (!(grfcge & fcgeRoot) && !_FDecRefCount(icrp))
                continue;
            grfcgeIn = fcgeNil;
        }
        if (grfcge & fcgePost)
        {
            // actually delete the node
            if (grfcge & fcgeError)
            {
                Warn("memory failure in ChunkyFile::Delete - adjusting ref counts");
                // memory failure - adjust the ref counts of this chunk's
                // children, but don't try to delete them
                LegacyLong ikid, icrpChild;
                ChildChunkIdentification kid;

                qcrp = (ChunkRepresentation *)_pggcrp->QvFixedGet(icrp);
                for (ikid = qcrp->ckid; ikid-- != 0;)
                {
                    _pggcrp->GetRgb(icrp, _BvKid(ikid), size(kid), &kid);
                    if (!_FFindCtgCno(kid.cki.ctg, kid.cki.cno, &icrpChild))
                    {
                        Bug("child not there");
                        continue;
                    }
                    _FDecRefCount(icrpChild);
                }
            }
            _DeleteCore(icrp);
        }
    }
    AssertThis(fcflFull);
}

/***************************************************************************
    Make sure the loner flag is set or clear according to fLoner.  If
    fLoner is false and (ctg, cno) is not currently the child of anything,
    it will be deleted.
***************************************************************************/
void ChunkyFile::SetLoner(ChunkTagOrType ctg, ChunkNumber cno, bool fLoner)
{
    AssertThis(0);
    LegacyLong icrp;
    ChunkRepresentation *qcrp;

    if (!_FFindCtgCno(ctg, cno, &icrp))
    {
        Bug("chunk not there");
        return;
    }
    qcrp = (ChunkRepresentation *)_pggcrp->QvFixedGet(icrp);

    if (fLoner)
        qcrp->SetGrfcrp(fcrpLoner);
    else if (qcrp->Grfcrp(fcrpLoner))
        Delete(ctg, cno);
}

/***************************************************************************
    Return the value of the loner flag.
***************************************************************************/
bool ChunkyFile::FLoner(ChunkTagOrType ctg, ChunkNumber cno)
{
    AssertThis(0);
    LegacyLong icrp;
    ChunkRepresentation *qcrp;

    if (!_FFindCtgCno(ctg, cno, &icrp))
    {
        Bug("chunk not there");
        return fFalse;
    }
    qcrp = (ChunkRepresentation *)_pggcrp->QvFixedGet(icrp);
    return FPure(qcrp->Grfcrp(fcrpLoner));
}

/***************************************************************************
    Returns the number of parents of this chunk
***************************************************************************/
LegacyLong ChunkyFile::CckiRef(ChunkTagOrType ctg, ChunkNumber cno)
{
    AssertThis(0);
    LegacyLong icrp;
    ChunkRepresentation *qcrp;

    if (!_FFindCtgCno(ctg, cno, &icrp))
    {
        Bug("chunk not there");
        return 0;
    }
    qcrp = (ChunkRepresentation *)_pggcrp->QvFixedGet(icrp);
    return qcrp->ccrpRef;
}

/***************************************************************************
    Determines if (ctgSub, cnoSub) is in the chunk subgraph of (ctg, cno).
    Returns tMaybe on error.
***************************************************************************/
tribool ChunkyFile::TIsDescendent(ChunkTagOrType ctg, ChunkNumber cno, ChunkTagOrType ctgSub, ChunkNumber cnoSub)
{
    AssertThis(0);
    ChunkGraphEnumerator cge;
    ChildChunkIdentification kid;
    LegacyUlong grfcge;

    if (CckiRef(ctgSub, cnoSub) == 0)
        return tNo;

    cge.Init(this, ctg, cno);
    while (cge.FNextKid(&kid, pvNil, &grfcge, fcgeNil))
    {
        if (kid.cki.ctg == ctgSub && kid.cki.cno == cnoSub)
            return tYes;
        if (grfcge & fcgeError)
            return tMaybe;
    }
    return tNo;
}

/***************************************************************************
    Delete the given child chunk.  Handles deleting child chunks that are
    no longer referenced and don't have the fcrpLoner flag set.
***************************************************************************/
void ChunkyFile::DeleteChild(ChunkTagOrType ctgPar, ChunkNumber cnoPar, ChunkTagOrType ctgChild, ChunkNumber cnoChild, ChildChunkID chid)
{
    AssertThis(0);
    LegacyLong icrpPar, icrpChild, ikid;
    ChunkRepresentation *qcrp;

    if (!_FFindCtgCno(ctgPar, cnoPar, &icrpPar) || !_FFindCtgCno(ctgChild, cnoChild, &icrpChild))
    {
        Bug("chunk not there");
        return;
    }
    if (!_FFindChild(icrpPar, ctgChild, cnoChild, chid, &ikid))
    {
        Bug("not a child");
        return;
    }

    // remove the reference
    qcrp = (ChunkRepresentation *)_pggcrp->QvFixedGet(icrpPar);
    qcrp->ckid--;
    _pggcrp->DeleteRgb(icrpPar, _BvKid(ikid), size(ChildChunkIdentification));

    // now decrement the ref count and nuke it if the ref count is zero
    if (_FDecRefCount(icrpChild))
    {
        // delete the chunk
        Delete(ctgChild, cnoChild);
    }
    AssertThis(fcflFull);
}

/***************************************************************************
    Decrements the reference count on the chunk.  Return true if the ref
    count becomes zero (after decrementing) and fcrpLoner is not set.
***************************************************************************/
bool ChunkyFile::_FDecRefCount(LegacyLong icrp)
{
    AssertBaseThis(0);
    ChunkRepresentation *qcrp;

    qcrp = (ChunkRepresentation *)_pggcrp->QvFixedGet(icrp);
    if (qcrp->ccrpRef <= 0)
    {
        Bug("ref count wrong");
        return fFalse;
    }
    return --(qcrp->ccrpRef) == 0 && !qcrp->Grfcrp(fcrpLoner);
}

/***************************************************************************
    Remove entry icrp from _pggcrp and add the file space to the free map.
***************************************************************************/
void ChunkyFile::_DeleteCore(LegacyLong icrp)
{
    AssertBaseThis(0);
    ChunkRepresentation *qcrp;
    ChunkIdentification cki;

    qcrp = (ChunkRepresentation *)_pggcrp->QvFixedGet(icrp);
    cki = qcrp->cki;
    _FreeFpCb(qcrp->Grfcrp(fcrpOnExtra), qcrp->fp, qcrp->Cb());
    _FSetRti(cki.ctg, cki.cno, rtiNil);
    _pggcrp->Delete(icrp);
}

/***************************************************************************
    Add the (fp, cb) to the free map.
***************************************************************************/
void ChunkyFile::_FreeFpCb(bool fOnExtra, FilePosition fp, LegacyLong cb)
{
    AssertBaseThis(0);
    Assert(cb > 0 || cb == 0 && fp == 0, "bad cb");
    Assert(fp >= 0 && (fOnExtra || fp >= size(ChunkyFilePrefix) || cb == 0 || _fInvalidMainFile), "bad fp");

    if (!cb) return;
    if (!fOnExtra && _fFreeMapNotRead) _ReadFreeMap();
    auto storage = fOnExtra ? &_cstoExtra : &_csto;
    AssertPo(storage->pfil, 0);
    ChunkHeapHost host = {storage->pfil, &storage->pglfsm};
    const auto heap = ChunkHeapServices(&host);
    ThreeMMU32 end = storage->fpMac;
    const int result = three_mm_chunk_heap_free(&heap, &end, fp, cb);
    storage->fpMac = end;
    if (result != THREE_MM_CHUNK_OK && result != THREE_MM_CHUNK_MEMORY) Bug("bad free range");
}

/***************************************************************************
    Set the name of the chunk.
***************************************************************************/
bool ChunkyFile::FSetName(ChunkTagOrType ctg, ChunkNumber cno, PString pstn)
{
    AssertThis(0);
    AssertNilOrPo(pstn, 0);
    LegacyLong icrp;

    if (!_FFindCtgCno(ctg, cno, &icrp))
    {
        Bug("chunk not there");
        return fFalse;
    }

    if (!_FSetName(icrp, pstn))
        return fFalse;

    _FSetRti(ctg, cno, rtiNil);

    AssertThis(0);
    return fTrue;
}

/***************************************************************************
    Set the name of the chunk at the given index.
***************************************************************************/
bool ChunkyFile::_FSetName(LegacyLong icrp, PString pstn)
{
    AssertBaseThis(0);
    AssertIn(icrp, 0, _pggcrp->IvMac());
    AssertNilOrPo(pstn, 0);
    LegacyLong cbVar, cbOld, cbNew;
    ChunkRepresentation *qcrp;
    LegacyLong bvRgch;

    if (pstn != pvNil && pstn->Cch() > 0)
        cbNew = pstn->CbData();
    else
        cbNew = 0;

    qcrp = (ChunkRepresentation *)_pggcrp->QvFixedGet(icrp, &cbVar);
    bvRgch = qcrp->BvRgch();
    cbOld = cbVar - bvRgch;
    AssertIn(cbOld, 0, kcbMaxDataStn + 1);

    if (cbOld > cbNew)
        _pggcrp->DeleteRgb(icrp, bvRgch, cbOld - cbNew);
    else if (cbOld < cbNew && !_pggcrp->FInsertRgb(icrp, bvRgch, cbNew - cbOld, pvNil))
    {
        return fFalse;
    }

    if (cbNew > 0)
    {
        pstn->GetData(PvAddBv(_pggcrp->PvLock(icrp), bvRgch));
        _pggcrp->Unlock();
    }

    return fTrue;
}

/***************************************************************************
    Retrieve the name of the chunk.  Returns whether the string is
    non-empty.
***************************************************************************/
bool ChunkyFile::FGetName(ChunkTagOrType ctg, ChunkNumber cno, PString pstn)
{
    AssertThis(0);
    AssertPo(pstn, 0);

    LegacyLong icrp;

    if (!_FFindCtgCno(ctg, cno, &icrp))
    {
        Bug("chunk not there");
        pstn->SetNil();
        return fFalse;
    }

    return _FGetName(icrp, pstn);
}

/***************************************************************************
    Retrieve the name of the chunk at the given index.  Returns whether
    the string is non-empty.
***************************************************************************/
bool ChunkyFile::_FGetName(LegacyLong icrp, PString pstn)
{
    AssertBaseThis(0);
    AssertIn(icrp, 0, _pggcrp->IvMac());
    AssertPo(pstn, 0);

    LegacyLong cbRgch, bvRgch;
    LegacyLong cbVar;
    ChunkRepresentation *qcrp;

    qcrp = (ChunkRepresentation *)_pggcrp->QvFixedGet(icrp, &cbVar);
    cbRgch = qcrp->CbRgch(cbVar);
    AssertIn(cbRgch, 0, kcbMaxDataStn + 1);
    if (cbRgch <= 0)
    {
        pstn->SetNil();
        return fFalse;
    }
    bvRgch = qcrp->BvRgch();

    if (!pstn->FSetData(PvAddBv(_pggcrp->PvLock(icrp), bvRgch), cbRgch))
        pstn->SetNil();
    _pggcrp->Unlock();

    return pstn->Cch() > 0;
}

/***************************************************************************
    Make a node a child of another node.  If fClearLoner is set, the loner
    flag of the child is cleared.
***************************************************************************/
bool ChunkyFile::FAdoptChild(ChunkTagOrType ctgPar, ChunkNumber cnoPar, ChunkTagOrType ctgChild, ChunkNumber cnoChild, ChildChunkID chid, bool fClearLoner)
{
    AssertThis(0);
    LegacyLong icrpPar;
    LegacyLong ikid;

    if (!_FFindCtgCno(ctgPar, cnoPar, &icrpPar))
    {
        Bug("parent not there");
        return fFalse;
    }
    if (_FFindChild(icrpPar, ctgChild, cnoChild, chid, &ikid))
        return fTrue; // already a child

    if (!_FAdoptChild(icrpPar, ikid, ctgChild, cnoChild, chid, fClearLoner))
    {
        AssertThis(0);
        return fFalse;
    }

    AssertThis(0);
    return fTrue;
}

/***************************************************************************
    Make (ctgChild, cnoChild) a child of icrpPar.
***************************************************************************/
bool ChunkyFile::_FAdoptChild(LegacyLong icrpPar, LegacyLong ikid, ChunkTagOrType ctgChild, ChunkNumber cnoChild, ChildChunkID chid, bool fClearLoner)
{
    AssertBaseThis(0);
    AssertIn(icrpPar, 0, _pggcrp->IvMac());
    LegacyLong icrp;
    ChunkRepresentation *qcrp;
    ChildChunkIdentification kid;

    if (!_FFindCtgCno(ctgChild, cnoChild, &icrp))
    {
        Bug("child not there");
        return fFalse;
    }
    qcrp = (ChunkRepresentation *)_pggcrp->QvFixedGet(icrpPar);
    AssertIn(ikid, 0, (LegacyLong)qcrp->ckid + 1);
    if (tNo != TIsDescendent(ctgChild, cnoChild, qcrp->cki.ctg, qcrp->cki.cno))
    {
        Warn("Performing this adoption may cause a cycle");
        return fFalse;
    }

    kid.cki.ctg = ctgChild;
    kid.cki.cno = cnoChild;
    kid.chid = chid;
    if (!_pggcrp->FInsertRgb(icrpPar, _BvKid(ikid), size(ChildChunkIdentification), &kid))
    {
        AssertThis(0);
        return fFalse;
    }
    qcrp = (ChunkRepresentation *)_pggcrp->QvFixedGet(icrpPar);
    if (qcrp->ckid >= kckidMax)
        goto LOverFlow;
    qcrp->ckid++;

    qcrp = (ChunkRepresentation *)_pggcrp->QvFixedGet(icrp);
    if (qcrp->ccrpRef >= kckidMax)
    {
        qcrp = (ChunkRepresentation *)_pggcrp->QvFixedGet(icrpPar);
        qcrp->ckid--;
    LOverFlow:
        _pggcrp->DeleteRgb(icrpPar, _BvKid(ikid), size(ChildChunkIdentification));
        AssertThis(0);
        return fFalse;
    }
    qcrp->ccrpRef++;

    if (fClearLoner)
        qcrp->ClearGrfcrp(fcrpLoner);
    AssertThis(0);
    return fTrue;
}

/***************************************************************************
    Changes the chid value of the given child link.  Assert that
    (ctgChild, cnoChild, chidOld) is a child of (ctgPar, cnoPar) and that
    (ctgChild, cnoChild, chidNew) is not currently a child.
***************************************************************************/
void ChunkyFile::ChangeChid(ChunkTagOrType ctgPar, ChunkNumber cnoPar, ChunkTagOrType ctgChild, ChunkNumber cnoChild, ChildChunkID chidOld, ChildChunkID chidNew)
{
    AssertThis(0);
    LegacyLong icrp, ikidOld, ikidNew;
    ChildChunkIdentification *qkid, *qrgkid;

    if (chidOld == chidNew)
        return;

    if (!_FFindCtgCno(ctgPar, cnoPar, &icrp))
    {
        Bug("parent not there");
        return;
    }
    if (!_FFindChild(icrp, ctgChild, cnoChild, chidOld, &ikidOld) ||
        _FFindChild(icrp, ctgChild, cnoChild, chidNew, &ikidNew))
    {
        Bug("src not a child or dest already a child");
        return;
    }

    qkid = (qrgkid = (ChildChunkIdentification *)_pggcrp->QvGet(icrp)) + ikidOld;
    qkid->chid = chidNew;

    MoveElement(qrgkid, size(ChildChunkIdentification), ikidOld, ikidNew);

    AssertThis(0);
}

/***************************************************************************
    Return the total number of chunks.
***************************************************************************/
LegacyLong ChunkyFile::Ccki(void)
{
    AssertThis(0);
    return _pggcrp->IvMac();
}

/***************************************************************************
    Finds the icki'th chunk.  If there is such a chunk (icki isn't too big),
    fills in *pcki and *pckid and returns true.  Otherwise, returns fFalse.
***************************************************************************/
bool ChunkyFile::FGetCki(LegacyLong icki, ChunkIdentification *pcki, LegacyLong *pckid, PDataBlock pblck)
{
    AssertThis(0);
    AssertNilOrVarMem(pcki);
    AssertNilOrVarMem(pckid);
    AssertNilOrPo(pblck, 0);
    AssertIn(icki, 0, kcbMax);
    auto access = _ChunkAccess();
    ThreeMMChunkEntry entry;
    if (three_mm_chunk_at(&access, static_cast<ThreeMMU32>(icki), &entry) != THREE_MM_CHUNK_OK)
    {
        TrashVar(pcki);
        TrashVar(pckid);
        if (pblck)
            pblck->Free();
        return fFalse;
    }
    if (pcki)
        *pcki = {entry.type, entry.number};
    if (pckid)
        *pckid = entry.child_count;
    if (pblck)
        _GetBlck(icki, pblck);
    return fTrue;
}

/***************************************************************************
    Finds the icki corresponding to the given (ctg, cno).  If the (ctg, cno)
    is not in the ChunkyFile, fills *picki with where it would be.
***************************************************************************/
bool ChunkyFile::FGetIcki(ChunkTagOrType ctg, ChunkNumber cno, LegacyLong *picki)
{
    AssertThis(0);
    AssertVarMem(picki);
    return _FFindCtgCno(ctg, cno, picki);
}

/***************************************************************************
    Return the total number of chunks of the given type in the file.
***************************************************************************/
LegacyLong ChunkyFile::CckiCtg(ChunkTagOrType ctg)
{
    AssertThis(0);
    LegacyLong icrpMac;
    LegacyLong icrpMin;
    LegacyLong icrpLim;

    if ((icrpMac = _pggcrp->IvMac()) == 0)
        return 0;

    _FFindCtgCno(ctg, 0, &icrpMin);
    if (ctg + 1 < ctg)
    {
        // ctg is the largest possible ctg!
        return icrpMac - icrpMin;
    }

    _FFindCtgCno(ctg + 1, 0, &icrpLim);
    return icrpLim - icrpMin;
}

/***************************************************************************
    Finds the icki'th chunk of type ctg.  If there is such a chunk,
    fills in *pcki and returns true.  Otherwise, returns fFalse.
***************************************************************************/
bool ChunkyFile::FGetCkiCtg(ChunkTagOrType ctg, LegacyLong icki, ChunkIdentification *pcki, LegacyLong *pckid, PDataBlock pblck)
{
    AssertThis(0);
    AssertIn(icki, 0, kcbMax);
    AssertNilOrVarMem(pcki);
    AssertNilOrVarMem(pckid);
    AssertNilOrPo(pblck, 0);
    auto access = _ChunkAccess();
    ThreeMMChunkEntry entry;
    ThreeMMU32 position;
    if (three_mm_chunk_type_at(&access, ctg, static_cast<ThreeMMU32>(icki), &entry, &position) != THREE_MM_CHUNK_OK)
    {
        TrashVar(pcki);
        TrashVar(pckid);
        if (pblck)
            pblck->Free();
        return fFalse;
    }
    if (pcki)
        *pcki = {entry.type, entry.number};
    if (pckid)
        *pckid = entry.child_count;
    if (pblck)
        _GetBlck(icki, pblck); // Preserve the original type-relative payload argument pending a retail check.
    return fTrue;
}

/***************************************************************************
    Return the number of children of the given chunk.
***************************************************************************/
LegacyLong ChunkyFile::Ckid(ChunkTagOrType ctg, ChunkNumber cno)
{
    AssertThis(0);
    ChunkRepresentation *qcrp;
    LegacyLong icrp;

    if (!_FFindCtgCno(ctg, cno, &icrp))
    {
        Bug("chunk not found");
        return 0;
    }

    qcrp = (ChunkRepresentation *)_pggcrp->QvFixedGet(icrp);
    return qcrp->ckid;
}

/***************************************************************************
    If ikid is less than number of children of the given chunk,
    fill *pkid and return true.  Otherwise, return false.
***************************************************************************/
bool ChunkyFile::FGetKid(ChunkTagOrType ctg, ChunkNumber cno, LegacyLong ikid, ChildChunkIdentification *pkid)
{
    AssertThis(0);
    AssertVarMem(pkid);
    AssertIn(ikid, 0, kcbMax);
    LegacyLong parent;
    if (!_FFindCtgCno(ctg, cno, &parent))
    {
        Bug("chunk not found");
        TrashVar(pkid);
        return fFalse;
    }
    auto access = _ChunkAccess();
    ThreeMMChunkChild child;
    if (three_mm_chunk_child_at(&access, parent, static_cast<ThreeMMU32>(ikid), &child) != THREE_MM_CHUNK_OK)
    {
        TrashVar(pkid);
        return fFalse;
    }
    *pkid = {{child.type, child.number}, child.slot};
    Assert(_FFindCtgCno(pkid->cki.ctg, pkid->cki.cno, &parent), "child not there");
    Assert(pkid->cki.ctg != ctg || pkid->cki.cno != cno, "chunk is child of itself!");
    return fTrue;
}

/***************************************************************************
    Look for a child of (ctgPar, cnoPar) with the given chid value.  If one
    is found, fill in *pkid and return true; else return false.  Kid's are
    sorted by (chid, ctg, cno).
***************************************************************************/
bool ChunkyFile::FGetKidChid(ChunkTagOrType ctgPar, ChunkNumber cnoPar, ChildChunkID chid, ChildChunkIdentification *pkid)
{
    AssertThis(0);
    AssertVarMem(pkid);
    return _FFindChidCtg(ctgPar, cnoPar, chid, (ChunkTagOrType)0, pkid);
}

/***************************************************************************
    Look for a child of (ctgPar, cnoPar) with the given chid and ctg value.
    If one is found, fill in *pkid and return true; else return false.
    Kid's are sorted by (chid, ctg, cno).
***************************************************************************/
bool ChunkyFile::FGetKidChidCtg(ChunkTagOrType ctgPar, ChunkNumber cnoPar, ChildChunkID chid, ChunkTagOrType ctg, ChildChunkIdentification *pkid)
{
    AssertThis(0);
    AssertVarMem(pkid);

    // the kid returned from _FFindChidCtg should have ctg >= the given ctg,
    // but not necessarily equal
    if (!_FFindChidCtg(ctgPar, cnoPar, chid, ctg, pkid) || pkid->cki.ctg != ctg)
    {
        TrashVar(pkid);
        return fFalse;
    }
    return fTrue;
}

/***************************************************************************
    Find the first child with the given chid and with ctg >= the given ctg.
    Returns true iff there is such a child and fills in the *pkid.
***************************************************************************/
bool ChunkyFile::_FFindChidCtg(ChunkTagOrType ctgPar, ChunkNumber cnoPar, ChildChunkID chid, ChunkTagOrType ctg, ChildChunkIdentification *pkid)
{
    AssertThis(0);
    AssertVarMem(pkid);
    LegacyLong parent;
    if (!_FFindCtgCno(ctgPar, cnoPar, &parent))
    {
        Bug("chunk not found");
        TrashVar(pkid);
        return fFalse;
    }
    auto access = _ChunkAccess();
    ThreeMMChunkChild child;
    if (three_mm_chunk_child_type(&access, parent, chid, ctg, &child) != THREE_MM_CHUNK_OK)
    {
        TrashVar(pkid);
        return fFalse;
    }
    *pkid = {{child.type, child.number}, child.slot};
    return fTrue;
}

/***************************************************************************
    Finds the ikid value associated with the given child (ctg, cno, chid) of
    the given chunk.  If the (ctg, cno, chid) is not a child of
    (ctgPar, cnoPar), fills *pikid with where it would be if it were.
***************************************************************************/
bool ChunkyFile::FGetIkid(ChunkTagOrType ctgPar, ChunkNumber cnoPar, ChunkTagOrType ctg, ChunkNumber cno, ChildChunkID chid, LegacyLong *pikid)
{
    AssertThis(0);
    AssertVarMem(pikid);
    LegacyLong icrp;

    if (!_FFindCtgCno(ctgPar, cnoPar, &icrp))
    {
        Bug("chunk not found");
        *pikid = 0;
        return fFalse;
    }

    return _FFindChild(icrp, ctg, cno, chid, pikid);
}

/***************************************************************************
    If (ctgChild, cnoChild, chid) is a child of icrpPar, return true and put
    the index in *pikid.  If not set *pikid to where to insert it.  Kids are
    sorted by (chid, ctg, cno).
***************************************************************************/
bool ChunkyFile::_FFindChild(LegacyLong icrpPar, ChunkTagOrType ctgChild, ChunkNumber cnoChild, ChildChunkID chid, LegacyLong *pikid)
{
    AssertBaseThis(0);
    AssertVarMem(pikid);
    auto access = _ChunkAccess();
    ThreeMMU32 position = 0;
    const int result = three_mm_chunk_find_child(&access, icrpPar, chid, ctgChild, cnoChild, &position);
    *pikid = static_cast<LegacyLong>(position);
    return result == THREE_MM_CHUNK_OK;
}

// cno map entry
struct ChunkNumberMapEntry
{
    ChunkTagOrType ctg;
    ChunkNumber cnoSrc;
    ChunkNumber cnoDst;
};

bool _FFindCnom(PDynamicArray pglcnom, ChunkTagOrType ctg, ChunkNumber cno, ChunkNumberMapEntry *pcnom = pvNil, LegacyLong *picnom = pvNil);
bool _FAddCnom(PDynamicArray *ppglcnom, ChunkNumberMapEntry *pcnom);

/***************************************************************************
    Look for a cnom for the given (ctg, cno). Whether or not it exists,
    fill *picnom with where it would go in the pglcnom.
***************************************************************************/
bool _FFindCnom(PDynamicArray pglcnom, ChunkTagOrType ctg, ChunkNumber cno, ChunkNumberMapEntry *pcnom, LegacyLong *picnom)
{
    AssertNilOrPo(pglcnom, 0);
    AssertNilOrVarMem(pcnom);
    AssertNilOrVarMem(picnom);
    LegacyLong ivMin, ivLim, iv;
    ChunkNumberMapEntry cnom;

    if (pvNil == pglcnom)
    {
        TrashVar(pcnom);
        if (pvNil != picnom)
            *picnom = 0;
        return fFalse;
    }

    for (ivMin = 0, ivLim = pglcnom->IvMac(); ivMin < ivLim;)
    {
        iv = (ivMin + ivLim) / 2;
        pglcnom->Get(iv, &cnom);
        if (cnom.ctg < ctg)
            ivMin = iv + 1;
        else if (cnom.ctg > ctg)
            ivLim = iv;
        else if (cnom.cnoSrc < cno)
            ivMin = iv + 1;
        else if (cnom.cnoSrc > cno)
            ivLim = iv;
        else
        {
            if (pvNil != pcnom)
                *pcnom = cnom;
            if (pvNil != picnom)
                *picnom = iv;
            return fTrue;
        }
    }

    TrashVar(pcnom);
    if (pvNil != picnom)
        *picnom = ivMin;
    return fFalse;
}

/***************************************************************************
    Add a cnom to the *ppglcnom. Allocated *ppglcnom if it is nil.
***************************************************************************/
bool _FAddCnom(PDynamicArray *ppglcnom, ChunkNumberMapEntry *pcnom)
{
    AssertVarMem(ppglcnom);
    AssertNilOrPo(*ppglcnom, 0);
    AssertVarMem(pcnom);
    LegacyLong icnom;

    if (pvNil == *ppglcnom && pvNil == (*ppglcnom = DynamicArray::PglNew(size(ChunkNumberMapEntry))))
        return fFalse;

    AssertDo(!_FFindCnom(*ppglcnom, pcnom->ctg, pcnom->cnoSrc, pvNil, &icnom), "why is this cnom already in the gl?");
    return (*ppglcnom)->FInsert(icnom, pcnom);
}

/***************************************************************************
    Copy a chunk (ctgSrc, cnoSrc) from this chunky file to pcflDst.
    The new cno is put in *pcno.  The destination chunk is marked as a
    loner. If possible, the cno in the destination file will be the same
    as the cno in the source file. If fClone is set, no chunk sharing will
    occur. Otherwise, this does intelligent chunk sharing.
***************************************************************************/
bool ChunkyFile::_FCopy(ChunkTagOrType ctgSrc, ChunkNumber cnoSrc, PChunkyFile pcflDst, ChunkNumber *pcnoDst, bool fClone)
{
    AssertThis(fcflFull);
    AssertPo(pcflDst, fcflFull);
    AssertVarMem(pcnoDst);

    LegacyLong icrpSrc, icrpDst;
    LegacyLong rtiSrc;
    ChunkGraphEnumerator cge;
    ChildChunkIdentification kid;
    ChunkIdentification ckiPar;
    DataBlock blckSrc;
    LegacyUlong grfcge, grfcgeIn;
    ChunkNumberMapEntry cnom, cnomPar;
    String stn;
    ChunkRepresentation *qcrp;

    bool fFreeDstOnFailure = fFalse;
    PDynamicArray pglcnom = pvNil;
    bool fRet = fFalse;

    if (!_FFindCtgCno(ctgSrc, cnoSrc, &icrpSrc))
    {
        Bug("chunk not found");
        TrashVar(pcnoDst);
        return fFalse;
    }

    if (!fClone && this == pcflDst)
    {
        SetLoner(ctgSrc, cnoSrc, fTrue);
        *pcnoDst = cnoSrc;
        return fTrue;
    }

    // copy chunks to the destination ChunkyFile
    cge.Init(this, ctgSrc, cnoSrc);
    grfcgeIn = fcgeNil;
    while (cge.FNextKid(&kid, &ckiPar, &grfcge, grfcgeIn))
    {
        grfcgeIn = fcgeNil;
        if (grfcge & fcgeError)
            goto LFail;

        // do pre-order handling only
        if (!(grfcge & fcgePre))
            continue;

        if (_FFindCnom(pglcnom, kid.cki.ctg, kid.cki.cno, &cnom))
        {
            // chunk has already been copied - just link it to the parent
            // and skip to its sibling
            Assert(!(grfcge & fcgeRoot), "how can the root already have been copied?");
            grfcgeIn = fcgeSkipToSib;
        }
        else if (rtiNil != (rtiSrc = _Rti(kid.cki.ctg, kid.cki.cno)) && !fClone &&
                 _FFindMatch(kid.cki.ctg, kid.cki.cno, pcflDst, &cnom.cnoDst))
        {
            // chunk and its subgraph exists in the destination, just link it
            // to the parent and skip to its sibling
            grfcgeIn = fcgeSkipToSib;
        }
        else
        {
            // must copy the chunk

            // assign the source chunk an rti (if it doesn't have one)
            if (rtiNil == rtiSrc && _FSetRti(kid.cki.ctg, kid.cki.cno, _rtiLast + 1))
            {
                rtiSrc = ++_rtiLast;
            }

            cnom.ctg = kid.cki.ctg;
            cnom.cnoSrc = kid.cki.cno;

            // find the source icrp
            AssertDo(_FFindCtgCno(kid.cki.ctg, kid.cki.cno, &icrpSrc), 0);

            // get the source blck
            _GetBlck(icrpSrc, &blckSrc);

            // allocate the dst chunk and copy the data - use the source cno
            // if possible
            if (this != pcflDst && !pcflDst->FFind(kid.cki.ctg, kid.cki.cno))
            {
                // can preserve cno
                cnom.cnoDst = kid.cki.cno;
                if (!pcflDst->FPutBlck(&blckSrc, kid.cki.ctg, cnom.cnoDst))
                    goto LFail;
            }
            else
            {
                if (!pcflDst->FAddBlck(&blckSrc, kid.cki.ctg, &cnom.cnoDst))
                    goto LFail;
                if (this == pcflDst)
                {
                    AssertDo(_FFindCtgCno(kid.cki.ctg, kid.cki.cno, &icrpSrc), 0);
                }
            }

            AssertDo(pcflDst->_FFindCtgCno(kid.cki.ctg, cnom.cnoDst, &icrpDst), "_FFindCtgCno doesn't work");

            // make sure the forest flags match
            qcrp = (ChunkRepresentation *)_pggcrp->QvFixedGet(icrpSrc);
            if (qcrp->Grfcrp(fcrpForest))
            {
                qcrp = (ChunkRepresentation *)pcflDst->_pggcrp->QvFixedGet(icrpDst);
                qcrp->SetGrfcrp(fcrpForest);
            }

            // set the dst name and rti to the src name and rti
            if (_FGetName(icrpSrc, &stn) && !pcflDst->_FSetName(icrpDst, &stn))
            {
                pcflDst->Delete(kid.cki.ctg, cnom.cnoDst);
                goto LFail;
            }
            if (rtiNil != rtiSrc)
                pcflDst->_FSetRti(kid.cki.ctg, cnom.cnoDst, rtiSrc);

            if (!_FAddCnom(&pglcnom, &cnom))
                goto LFail;
            fFreeDstOnFailure = fTrue;
        }

        // if it's the root, it has no parent and we need to set *pcnoDst
        if (grfcge & fcgeRoot)
        {
            *pcnoDst = cnom.cnoDst;
            continue;
        }

        // get the source parent's cnom
        AssertDo(_FFindCnom(pglcnom, ckiPar.ctg, ckiPar.cno, &cnomPar), 0);

        // make sure the dst child is a child of the dst parent
        if (!pcflDst->FAdoptChild(ckiPar.ctg, cnomPar.cnoDst, kid.cki.ctg, cnom.cnoDst, kid.chid, grfcgeIn == fcgeNil))
        {
            if (grfcgeIn == fcgeNil)
                pcflDst->Delete(kid.cki.ctg, cnom.cnoDst);
            goto LFail;
        }
    }

    fRet = fTrue;
    pcflDst->SetLoner(ctgSrc, *pcnoDst, fTrue);

LFail:
    AssertThis(fcflFull);
    AssertPo(pcflDst, fcflFull);
    ReleasePpo(&pglcnom);
    if (!fRet && fFreeDstOnFailure)
        pcflDst->Delete(ctgSrc, *pcnoDst);
    TrashVarIf(!fRet, pcnoDst);

    return fRet;
}

/***************************************************************************
    See if there is a subgraph of pcflDst matching the subgraph at
    (ctgSrc, cnoSrc). Subgraphs match if there is one-to-one correspondence
    of nodes and arcs of the two subgraphs and the rti's of corresponding
    nodes are equal.
***************************************************************************/
bool ChunkyFile::_FFindMatch(ChunkTagOrType ctgSrc, ChunkNumber cnoSrc, PChunkyFile pcflDst, ChunkNumber *pcnoDst)
{
    AssertBaseThis(0);
    AssertPo(pcflDst, 0);
    AssertVarMem(pcnoDst);

    LegacyLong rtiSrc, rtiKid;
    LegacyLong ckidSrc;
    ChunkNumber cnoMin, cnoDst;
    ChunkGraphEnumerator cgeSrc, cgeDst;
    ChildChunkIdentification kidSrc, kidDst;
    ChunkIdentification ckiParSrc, ckiParDst;
    LegacyUlong grfcgeSrc, grfcgeDst;
    bool fKidSrc, fKidDst;

    if (this == pcflDst || rtiNil == (rtiSrc = _Rti(ctgSrc, cnoSrc)))
        return fFalse;

    ckidSrc = Ckid(ctgSrc, cnoSrc);
    for (cnoDst = cnoMin = 0;; cnoMin = cnoDst + 1)
    {
        // get the next chunk with the same rti
        if (cnoDst == (ChunkNumber)(-1) || !pcflDst->_FFindCtgRti(ctgSrc, rtiSrc, cnoMin, &cnoDst))
        {
            return fFalse;
        }

        if (pcflDst->Ckid(ctgSrc, cnoDst) != ckidSrc)
            continue;

        cgeSrc.Init(this, ctgSrc, cnoSrc);
        cgeDst.Init(pcflDst, ctgSrc, cnoDst);
        for (;;)
        {
            // get the next element of the the source graph
            fKidSrc = cgeSrc.FNextKid(&kidSrc, &ckiParSrc, &grfcgeSrc, fcgeNil);

            // if the source chunk doesn't have an rti, there's no hope
            if (fKidSrc && rtiNil == (rtiKid = _Rti(kidSrc.cki.ctg, kidSrc.cki.cno)))
            {
                return fFalse;
            }

            // get the next element of the destination graph
            fKidDst = cgeDst.FNextKid(&kidDst, &ckiParDst, &grfcgeDst, fcgeNil);

            if (FPure(fKidSrc) != FPure(fKidDst))
            {
                // the two graphs have different numbers of nodes, so they
                // don't match
                break;
            }

            if (!fKidSrc)
            {
                // we're finished with the enumeration and everything matched
                *pcnoDst = cnoDst;
                return fTrue;
            }
            if ((grfcgeSrc | grfcgeDst) & fcgeError)
                return fFalse;

            if ((grfcgeSrc ^ grfcgeDst) & (fcgePre | fcgePost | fcgeRoot) || kidSrc.cki.ctg != kidDst.cki.ctg ||
                !(grfcgeSrc & fcgeRoot) && kidSrc.chid != kidDst.chid ||
                rtiKid != pcflDst->_Rti(kidDst.cki.ctg, kidDst.cki.cno) ||
                Ckid(kidSrc.cki.ctg, kidSrc.cki.cno) != pcflDst->Ckid(kidDst.cki.ctg, kidDst.cki.cno))
            {
                // children don't match
                break;
            }
        }
    }
}

/***************************************************************************
    Looks for a chunk of the given type with assigned rti and cno at least
    cnoMin.
***************************************************************************/
bool ChunkyFile::_FFindCtgRti(ChunkTagOrType ctg, LegacyLong rti, ChunkNumber cnoMin, ChunkNumber *pcno)
{
    AssertBaseThis(0);
    AssertNilOrVarMem(pcno);

#ifdef CHUNK_BIG_INDEX

    LegacyLong icrp, ccrp;
    ChunkRepresentation *qcrp;

    _FFindCtgCno(ctg, cnoMin, &icrp);
    ccrp = Ccki();
    for (; icrp < ccrp; icrp++)
    {
        qcrp = (ChunkRepresentation *)_pggcrp->QvFixedGet(icrp);
        if (qcrp->cki.ctg != ctg)
            break; // done
        if (qcrp->rti == rti)
        {
            // found one
            if (pcno != pvNil)
                *pcno = qcrp->cki.cno;
            return fTrue;
        }
    }

#else //! CHUNK_BIG_INDEX

    LegacyLong irtie, crtie;
    RuntimeIDEntry rtie;

    if (pvNil != _pglrtie && 0 < (crtie = _pglrtie->IvMac()))
    {
        _FFindRtie(ctg, cnoMin, pvNil, &irtie);
        for (; irtie < crtie; irtie++)
        {
            _pglrtie->Get(irtie, &rtie);
            if (rtie.ctg != ctg)
                break; // done
            if (rtie.rti == rti)
            {
                // found one
                if (pcno != pvNil)
                    *pcno = rtie.cno;
                return fTrue;
            }
        }
    }

#endif //! CHUNK_BIG_INDEX

    TrashVar(pcno);
    return fFalse;
}

/***************************************************************************
    Copy a chunk (ctgSrc, cnoSrc) from this chunky file to pcflDst.
    The new cno is put in *pcno.  The destination chunk is marked as a
    loner. If possible, the cno in the destination file will be the same
    as the cno in the source file. This does intelligent chunk sharing.
***************************************************************************/
bool ChunkyFile::FCopy(ChunkTagOrType ctgSrc, ChunkNumber cnoSrc, PChunkyFile pcflDst, ChunkNumber *pcnoDst)
{
    return _FCopy(ctgSrc, cnoSrc, pcflDst, pcnoDst, fFalse);
}

/***************************************************************************
    Clone a chunk subgraph from this chunky file to pcflDst. This will
    make a copy of the the chunk and its descendents without using any
    previously existing chunks in the destination. The new cno is put in
    *pcno.  The destination chunk is marked as a loner. If possible, the
    cno in the destination file will be the same as the cno in the
    source file.
***************************************************************************/
bool ChunkyFile::FClone(ChunkTagOrType ctgSrc, ChunkNumber cnoSrc, PChunkyFile pcflDst, ChunkNumber *pcnoDst)
{
    return _FCopy(ctgSrc, cnoSrc, pcflDst, pcnoDst, fTrue);
}

/***************************************************************************
    Return the run time id of the given chunk.
***************************************************************************/
LegacyLong ChunkyFile::_Rti(ChunkTagOrType ctg, ChunkNumber cno)
{
    AssertBaseThis(0);

#ifdef CHUNK_BIG_INDEX

    LegacyLong icrp;
    ChunkRepresentation *qcrp;

    if (!_FFindCtgCno(ctg, cno, &icrp))
        return rtiNil;

    qcrp = (ChunkRepresentation *)_pggcrp->QvFixedGet(icrp, &cbVar);
    return qcrp->rti;

#else //! CHUNK_BIG_INDEX

    RuntimeIDEntry rtie;

    if (!_FFindRtie(ctg, cno, &rtie))
        return rtiNil;

    return rtie.rti;

#endif //! CHUNK_BIG_INDEX
}

/***************************************************************************
    Set the run time id of the given chunk.
***************************************************************************/
bool ChunkyFile::_FSetRti(ChunkTagOrType ctg, ChunkNumber cno, LegacyLong rti)
{
    AssertBaseThis(0);

#ifdef CHUNK_BIG_INDEX

    LegacyLong icrp;
    ChunkRepresentation *qcrp;

    if (!_FFindCtgCno(ctg, cno, &icrp))
        return fFalse;

    qcrp = (ChunkRepresentation *)_pggcrp->QvFixedGet(icrp, &cbVar);
    qcrp->rti = rti;
    return fTrue;

#else //! CHUNK_BIG_INDEX

    RuntimeIDEntry rtie;
    LegacyLong irtie;

    if (_FFindRtie(ctg, cno, &rtie, &irtie))
    {
        if (rtiNil == rti)
            _pglrtie->Delete(irtie);
        else
        {
            rtie.rti = rti;
            _pglrtie->Put(irtie, &rtie);
        }
        return fTrue;
    }

    if (rtiNil == rti)
        return fTrue;

    rtie.ctg = ctg;
    rtie.cno = cno;
    rtie.rti = rti;
    if (pvNil == _pglrtie && pvNil == (_pglrtie = DynamicArray::PglNew(size(RuntimeIDEntry), 1)))
        return fFalse;

    return _pglrtie->FInsert(irtie, &rtie);

#endif //! CHUNK_BIG_INDEX
}

#ifndef CHUNK_BIG_INDEX
/***************************************************************************
    Look for an RuntimeIDEntry entry for the given (ctg, cno).
***************************************************************************/
bool ChunkyFile::_FFindRtie(ChunkTagOrType ctg, ChunkNumber cno, RuntimeIDEntry *prtie, LegacyLong *pirtie)
{
    AssertBaseThis(0);
    AssertNilOrVarMem(prtie);
    AssertNilOrVarMem(pirtie);

    LegacyLong ivMin, ivLim, iv;
    RuntimeIDEntry rtie;

    if (pvNil == _pglrtie)
    {
        if (pvNil != pirtie)
            *pirtie = 0;
        TrashVar(prtie);
        return fFalse;
    }

    for (ivMin = 0, ivLim = _pglrtie->IvMac(); ivMin < ivLim;)
    {
        iv = (ivMin + ivLim) / 2;
        _pglrtie->Get(iv, &rtie);
        if (rtie.ctg < ctg)
            ivMin = iv + 1;
        else if (rtie.ctg > ctg)
            ivLim = iv;
        else if (rtie.cno < cno)
            ivMin = iv + 1;
        else if (rtie.cno > cno)
            ivLim = iv;
        else
        {
            if (pvNil != prtie)
                *prtie = rtie;
            if (pvNil != pirtie)
                *pirtie = iv;
            return fTrue;
            ;
        }
    }

    if (pvNil != pirtie)
        *pirtie = ivMin;
    TrashVar(prtie);
    return fFalse;
}
#endif //! CHUNK_BIG_INDEX

/***************************************************************************
    Constructor for chunk graph enumerator.
***************************************************************************/
ChunkGraphEnumerator::ChunkGraphEnumerator(void)
{
    AssertThisMem();

    _es = esDone;
    _pgldps = pvNil;
    AssertThis(0);
}

/***************************************************************************
    Destructor for chunk graph enumerator.
***************************************************************************/
ChunkGraphEnumerator::~ChunkGraphEnumerator(void)
{
    AssertThis(0);
    ReleasePpo(&_pgldps);
}

#ifdef DEBUG
/***************************************************************************
    Assert the validity of the cge
***************************************************************************/
void ChunkGraphEnumerator::AssertValid(LegacyUlong grf)
{
    ChunkGraphEnumerator_PAR::AssertValid(0);
    AssertIn(_es, esStart, esDone + 1);
    if (FIn(_es, esStart, esDone))
    {
        AssertPo(_pcfl, 0);
        AssertNilOrPo(_pgldps, 0);
    }
    else
        Assert(_pgldps == pvNil, "_pgldps not nil");
}

/***************************************************************************
    Mark memory used by the cge.
***************************************************************************/
void ChunkGraphEnumerator::MarkMem(void)
{
    AssertThis(0);
    ChunkGraphEnumerator_PAR::MarkMem();
    MarkMemObj(_pgldps);
}
#endif // DEBUG

/***************************************************************************
    Start a new enumeration.
***************************************************************************/
void ChunkGraphEnumerator::Init(PChunkyFile pcfl, ChunkTagOrType ctg, ChunkNumber cno)
{
    AssertThis(0);
    AssertPo(pcfl, 0);

    ReleasePpo(&_pgldps);
    _es = esStart;
    _pcfl = pcfl;
    _dps.kid.cki.ctg = ctg;
    _dps.kid.cki.cno = cno;
    TrashVar(&_dps.kid.chid);
    AssertThis(0);
}

/***************************************************************************
    Fetch the next node in the graph enumeration.  Generally, parent nodes
    are returned twice (once with fcgePre and again with fcgePost).  Nodes
    without children are returned only once (with both fcgePre and fcgePost
    set).  The new node is put in *pkid, and the node's parent (if the node
    is not the root of the enumeration) is put in *pckiPar.  pckiPar may
    be nil.

    If (grfcgeIn & fcgeSkipToSib), skips all children and the upward touch
    of the last node returned.

    The *pgrfcgeOut value can contain any combination of: fcgePre, fcgePost,
    fcgeError, fcgeRoot.  These have the following meanings:

        fcgePre:  *pkid is valid; haven't traversed the node's children yet
        fcgePost:  *pkid is valid; have already traversed the children (or
            there aren't any children)
        fcgeError:  a memory error occurred; may be set in conjunction with
            other flags
        fcgeRoot:  *pkid is valid (except the chid value); *pckiPar is
            invalid; the node is the root of the enumeration
***************************************************************************/
bool ChunkGraphEnumerator::FNextKid(ChildChunkIdentification *pkid, ChunkIdentification *pckiPar, LegacyUlong *pgrfcgeOut, LegacyUlong grfcgeIn)
{
    AssertThis(0);
    AssertVarMem(pkid);
    AssertNilOrVarMem(pckiPar);
    AssertVarMem(pgrfcgeOut);

    *pgrfcgeOut = fcgeNil;
    switch (_es)
    {
    case esStart:
        // starting the enumeration
        // hit the node on the way down
        *pgrfcgeOut |= fcgePre | fcgeRoot;
        if (_pcfl->Ckid(_dps.kid.cki.ctg, _dps.kid.cki.cno) == 0)
            goto LPost;
        *pkid = _dps.kid;
        _dps.ikid = 0;
        _es = esGo;
        break;

    case esGo:
        if ((grfcgeIn & fcgeSkipToSib) && (_pgldps == pvNil || !_pgldps->FPop(&_dps)))
        {
            goto LDone;
        }
        // fall through
    case esGoNoSkip:
        if (!_pcfl->FGetKid(_dps.kid.cki.ctg, _dps.kid.cki.cno, _dps.ikid++, pkid))
        {
        LPost:
            // no more children, hit the node on the way up
            *pgrfcgeOut |= fcgePost;
            *pkid = _dps.kid;
            if (_pgldps == pvNil || !_pgldps->FPop(&_dps))
            {
                // this is the root
                *pgrfcgeOut |= fcgeRoot;
                _es = esDone;
                ReleasePpo(&_pgldps);
            }
            else
            {
                _es = esGoNoSkip;
                if (pckiPar != pvNil)
                    *pckiPar = _dps.kid.cki;
            }
            break;
        }

        // hit the child
        if (pckiPar != pvNil)
            *pckiPar = _dps.kid.cki;
        if (_pcfl->Ckid(pkid->cki.ctg, pkid->cki.cno) > 0)
        {
            // child has children, need to push the dps
            if (_pgldps == pvNil && (_pgldps = DynamicArray::PglNew(size(DataEnumerationPushState), 10)) == pvNil || !_pgldps->FPush(&_dps))
            {
                // mem failure, pretend it has no children
                *pgrfcgeOut |= fcgeError;
                goto LNoChildren;
            }
            _dps.kid = *pkid;
            _dps.ikid = 0;
            *pgrfcgeOut |= fcgePre;
            _es = esGo;
        }
        else
        {
        LNoChildren:
            // child doesn't have children, just handle it
            *pgrfcgeOut |= fcgePost | fcgePre;
            _es = esGoNoSkip;
        }
        break;

    default:
    LDone:
        _es = esDone;
        ReleasePpo(&_pgldps);
        TrashVar(pkid);
        TrashVar(pckiPar);
        TrashVar(pgrfcgeOut);
        return fFalse;
    }

    TrashVarIf((*pgrfcgeOut & fcgeRoot), pckiPar);
    TrashVarIf((*pgrfcgeOut & fcgeRoot), &pkid->chid);
    return fTrue;
}

} // end of namespace Chunky
