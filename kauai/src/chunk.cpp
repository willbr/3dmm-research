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
#include "util.h"
#include "three_mm/chunk_save.h"
#include "three_mm/chunk_heap.h"
#include "three_mm/chunk_edit.h"
#include "three_mm/chunk_ops.h"
#include "three_mm/chunk_forest.h"
#include "handle_services.h"
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

enum
{
    fcrpNil = 0,
    fcrpOnExtra = 0x01, // data is on the extra file
    fcrpLoner = 0x02,   // the chunk can stand (may also be a child)
    fcrpPacked = 0x04,  // the data is compressed
    fcrpMarkT = 0x08,   // used for consistency checks (see _TValidIndex)
    fcrpForest = 0x10,  // the chunk contains a forest of chunks
};

// Runtime records and packed size/flag access now belong to the C backend.
#ifdef CHUNK_BIG_INDEX
using ChunkRepresentation = ThreeMMChunkRecordBig;
constexpr int expandedRecords = 1;
constexpr LegacyLong kcbMaxCrp = klwMax, kckidMax = kcbMax;
#else
using ChunkRepresentation = ThreeMMChunkRecordSmall;
constexpr int expandedRecords = 0;
constexpr LegacyLong kcbMaxCrp = 0x00ffffff, kckidMax = ksuMax;
#endif
using PChunkTable = ThreeMMChunkTable *;
namespace {
PChunkTable NewChunkTable(LegacyLong width, LegacyLong reserve = 0)
{
    PChunkTable table = nullptr; int trash = 0; Debug(trash = 1;)
    const auto allocator = CollectionAllocator();
    three_mm_chunk_table_create(&allocator, width, reserve, trash, &table); return table;
}
void CloseChunkTable(PChunkTable table)
{ AssertDo(three_mm_chunk_table_close(table) == THREE_MM_STORAGE_OK, "invalid chunk table disposal"); }
void *ChunkFixed(PChunkTable table, LegacyLong i, LegacyLong *bytes = nullptr)
{
    ThreeMMU32 count = 0; auto p = three_mm_chunk_table_fixed(table, i, &count);
    Assert(p != nullptr, "invalid chunk table position"); if (bytes) *bytes = count; return p;
}
void *ChunkVariable(PChunkTable table, LegacyLong i)
{ return three_mm_chunk_table_variable(table, i, nullptr); }
bool ChunkInsert(PChunkTable table, LegacyLong i, LegacyLong bytes, const void *data = nullptr, const void *fixed = nullptr)
{ return three_mm_chunk_table_insert(table, i, bytes, data, fixed) == THREE_MM_STORAGE_OK; }
bool ChunkInsertBytes(PChunkTable table, LegacyLong i, LegacyLong offset, LegacyLong bytes, const void *data)
{ return three_mm_chunk_table_insert_bytes(table, i, offset, bytes, data) == THREE_MM_STORAGE_OK; }
void ChunkDelete(PChunkTable table, LegacyLong i)
{ AssertDo(three_mm_chunk_table_delete(table, i) == THREE_MM_STORAGE_OK, "invalid chunk table deletion"); }
void ChunkDeleteBytes(PChunkTable table, LegacyLong i, LegacyLong offset, LegacyLong bytes)
{ AssertDo(three_mm_chunk_table_delete_bytes(table, i, offset, bytes) == THREE_MM_STORAGE_OK, "invalid chunk table byte deletion"); }
void ChunkReadBytes(PChunkTable table, LegacyLong i, LegacyLong offset, LegacyLong bytes, void *out)
{ AssertDo(three_mm_chunk_table_read(table, i, offset, bytes, out) == THREE_MM_STORAGE_OK, "invalid chunk table read"); }
void ChunkPutBytes(PChunkTable table, LegacyLong i, LegacyLong offset, LegacyLong bytes, const void *data)
{ AssertDo(three_mm_chunk_table_put(table, i, offset, bytes, data) == THREE_MM_STORAGE_OK, "invalid chunk table write"); }
void ChunkReadFixed(PChunkTable table, LegacyLong i, void *out)
{ std::memcpy(out, ChunkFixed(table, i), table->fixed_bytes); }
void ChunkMove(PChunkTable table, LegacyLong from, LegacyLong to)
{ AssertDo(three_mm_chunk_table_move(table, from, to) == THREE_MM_STORAGE_OK, "invalid chunk table move"); }
bool ChunkMoveBytes(PChunkTable table, LegacyLong from, LegacyLong offset, LegacyLong to, LegacyLong targetOffset, LegacyLong bytes)
{ return three_mm_chunk_table_move_bytes(table, from, offset, to, targetOffset, bytes) == THREE_MM_STORAGE_OK; }
void ChunkLock(PChunkTable table)
{ AssertDo(three_mm_chunk_table_lock(table) == THREE_MM_STORAGE_OK, "invalid chunk table lock"); }
void ChunkUnlock(PChunkTable table)
{ AssertDo(three_mm_chunk_table_unlock(table) == THREE_MM_STORAGE_OK, "invalid chunk table unlock"); }
void *ChunkLockVariable(PChunkTable table, LegacyLong i)
{ ChunkLock(table); return ChunkVariable(table, i); }
}

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
    return three_mm_chunk_table_entry(static_cast<PChunkTable>(context), position, entry);
}
}
ThreeMMChunkAccess ChunkyFile::_ChunkAccess()
{
    return three_mm_chunk_table_access(_Index());
}

#define _BvKid(ikid) LwMul(ikid, size(ChildChunkIdentification))

const LegacyLong rtiNil = 0; // no rti assigned
ThreeMMChunkContext ChunkyFile::_context;

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
    int expanded = 0;
#ifdef CHUNK_BIG_INDEX
    expanded = 1;
#endif
    AssertDo(three_mm_chunk_owner_init(&_owner, KauaiCopyAllocator(), expanded) == THREE_MM_CHUNK_OK, "invalid container owner");
    // add it to the linked list
    three_mm_chunk_link_init(&_link, this);
    AssertDo(three_mm_chunk_context_attach(&_context, &_link) == THREE_MM_CHUNK_OK, "invalid container registry attachment");
    AssertBaseThis(0);
}

/***************************************************************************
    Close the associated files.  Private.
***************************************************************************/
ChunkyFile::~ChunkyFile(void)
{
    AssertBaseThis(0);
    const ThreeMMChunkDisposal services = {this,
        [](void *, void *file) { static_cast<PFileObject>(file)->Release(); },
        [](void *, void *index) { CloseChunkTable(static_cast<PChunkTable>(index)); },
        [](void *c, const ThreeMMChunkReopenState *state) { static_cast<PChunkyFile>(c)->_owner.files = *state; }};
    AssertDo(three_mm_chunk_owner_dispose(&_owner, &services) == THREE_MM_CHUNK_OK, "invalid container disposal");
    AssertDo(three_mm_chunk_context_detach(&_link) == THREE_MM_CHUNK_OK, "invalid container registry detachment");
}
/***************************************************************************
    Static method: open an existing file as a chunky file.  Increments the
    open count.
***************************************************************************/
PChunkyFile ChunkyFile::PcflOpen(Filename *pfni, LegacyUlong grfcfl)
{
    AssertPo(pfni, ffniFile);
    Assert(!(grfcfl & fcflTemp), "can't open a file as temp");
    const auto services = _ConstructionServices(pfni);
    auto owner = static_cast<PChunkyFile>(three_mm_chunk_construct(&services, grfcfl, 0));
    return owner;
}
/***************************************************************************
    If the file is read-only, toss the index and extra file and re-read
    the index.
***************************************************************************/
bool ChunkyFile::FReopen(void)
{
    AssertThis(0);
    const ThreeMMChunkReopenServices services = {this,
        [](void *, void *file) -> int { return (static_cast<PFileObject>(file)->GrffilCur() & ffilWriteEnable) != 0; },
        [](void *, void *file) { static_cast<PFileObject>(file)->AddRef(); },
        [](void *, void *file) { static_cast<PFileObject>(file)->Release(); },
        [](void *, void *index) { CloseChunkTable(static_cast<PChunkTable>(index)); },
        [](void *c, ThreeMMChunkReopenState *state) -> int {
            auto self = static_cast<PChunkyFile>(c); const bool ok = self->_FReadIndex(); *state = self->_owner.files; return ok;
        },
        [](void *c) -> int { return static_cast<PChunkyFile>(c)->_TValidIndex() == tYes; },
        [](void *c, const ThreeMMChunkReopenState *state) { static_cast<PChunkyFile>(c)->_owner.files = *state; }};
    const bool ok = three_mm_chunk_owner_reopen(&_owner, &services) == THREE_MM_CHUNK_OK;
    AssertThis(0); return ok;
}
/***************************************************************************
    Native object, file and collection primitives for the shared C
    construction policy.
***************************************************************************/
ThreeMMChunkConstruction ChunkyFile::_ConstructionServices(Filename *name)
{
    static_assert(ffilWriteEnable == 1 && ffilDenyWrite == 2 && ffilDenyRead == 4 && ffilTemp == 16, "file flag encoding");
    return {name,
        [](void *c) -> void * { return PcflFromFni(static_cast<Filename *>(c)); },
        [](void *) -> void * { return NewObj ChunkyFile(); },
        [](void *, void *owner) { static_cast<PChunkyFile>(owner)->AddRef(); },
        [](void *, void *owner) { static_cast<PChunkyFile>(owner)->Release(); },
        [](void *c, int create, ThreeMMU32 flags) -> void * {
            return create ? FileObject::PfilCreate(static_cast<Filename *>(c), flags) : FileObject::PfilOpen(static_cast<Filename *>(c), flags);
        },
        [](void *) -> void * { return NewChunkTable(sizeof(ChunkRepresentation)); },
        [](void *, void *owner, void *file) { static_cast<PChunkyFile>(owner)->_owner.files.main_file = static_cast<PFileObject>(file); },
        [](void *, void *owner, void *index) { static_cast<PChunkyFile>(owner)->_owner.files.index = static_cast<PChunkTable>(index); },
        [](void *, void *owner, ThreeMMU32 end) { static_cast<PChunkyFile>(owner)->_owner.files.main_end = end; },
        [](void *, void *file, ThreeMMU32 end) -> int { return static_cast<PFileObject>(file)->FSetFpMac(end); },
        [](void *, void *owner) -> int { return static_cast<PChunkyFile>(owner)->_FReadIndex(); },
        [](void *, void *owner) -> int { return static_cast<PChunkyFile>(owner)->_TValidIndex() == tYes; },
        [](void *, void *owner, ThreeMMU32 flags) -> int { return static_cast<PChunkyFile>(owner)->FSetGrfcfl(flags); },
        [](void *, int issue) {
            if (issue == THREE_MM_CONTAINER_ALREADY_OPEN) Bug("trying to create an open file");
            else if (issue == THREE_MM_CONTAINER_INITIAL_OPTIONS) Bug("initial container options failed");
            else PushErc(issue == THREE_MM_CONTAINER_OPEN_FAILED ? ercCflOpen : ercCflCreate);
        },
        [](void *, void *owner, int create) {
            AssertPo(static_cast<PChunkyFile>(owner), fcflFull | (create ? fcflGraph : 0));
        }};
}
/***************************************************************************
    Static method: create a new file.  Increments the open count.
***************************************************************************/
PChunkyFile ChunkyFile::PcflCreate(Filename *pfni, LegacyUlong grfcfl)
{
    AssertPo(pfni, ffniFile);
    const auto services = _ConstructionServices(pfni);
    auto owner = static_cast<PChunkyFile>(three_mm_chunk_construct(&services, grfcfl, 1));
    return owner;
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
    const ThreeMMChunkLookup services = {
        [](void *c) -> void * { return static_cast<PChunkyFile>(c)->PcflNext(); },
        [](void *c) -> void * { return static_cast<PChunkyFile>(c)->_MainFile(); },
        [](void *c) -> void * { return static_cast<PChunkyFile>(c)->_ExtraFile(); },
        [](void *c) -> int { return static_cast<PChunkyFile>(c)->_owner.files.invalid_main; },
        [](void *) { Bug("who is using this Filename?"); }};
    return static_cast<PChunkyFile>(three_mm_chunk_find_container(PcflFirst(), FileObject::PfilFromFni(pfni), &services));
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
namespace {
ThreeMMChunkGraph ChunkGraphServices(PChunkyFile);
struct ForestWriteHost { PChunkyFile source; PFileObject destination; };
}
bool ChunkyFile::FWriteChunkTree(ChunkTagOrType ctg, ChunkNumber cno, PFileObject pfilDst, FilePosition fpDst, LegacyLong *pcb)
{
    AssertThis(0); AssertNilOrPo(pfilDst, 0); AssertNilOrVarMem(pcb);
    ForestWriteHost host = {this, pfilDst};
    const short one = 1;
    ThreeMMForestExport output = {};
    output.context = &host; output.graph = ChunkGraphServices(this);
    output.big_endian = reinterpret_cast<const byte *>(&one)[0] == 0; output.system = koskCur;
    output.entry = [](void *c, ThreeMMU32 type, ThreeMMU32 number, ThreeMMChunkEntry *entry) -> int {
        auto source = static_cast<ForestWriteHost *>(c)->source; LegacyLong position;
        return source->_FFindCtgCno(type, number, &position) && ReadRuntimeChunk(source->_Index(), position, entry);
    };
    if (pfilDst)
    {
        output.write = [](void *c, ThreeMMU32 offset, const void *bytes, size_t count) -> int {
            return static_cast<ForestWriteHost *>(c)->destination->FWriteRgb(const_cast<void *>(bytes), count, offset);
        };
        output.copy = [](void *c, ThreeMMU32 type, ThreeMMU32 number, ThreeMMU32 offset, ThreeMMU32 bytes) -> int {
            auto h = static_cast<ForestWriteHost *>(c); FileLocation source;
            if (!h->source->FFindFlo(type, number, &source)) return 0;
            FileLocation destination = {h->destination, static_cast<FilePosition>(offset), static_cast<LegacyLong>(bytes)};
            return source.FCopy(&destination);
        };
    }
    ThreeMMU32 bytes;
    if (three_mm_forest_write(&output, KauaiCopyAllocator(), ctg, cno, fpDst, &bytes) != THREE_MM_CHUNK_OK)
    { TrashVar(pcb); return false; }
    if (pcb) *pcb = bytes;
    return true;
}

PChunkyFile ChunkyFile::PcflReadForestFromFlo(PFileLocation pflo, bool fCopyData)
{
    AssertVarMem(pflo);
    AssertPo(pflo->pfil, 0);
    const ThreeMMForestConstruction construction = {
        nullptr,
        [](void *) -> void * { return NewObj ChunkyFile(); },
        [](void *, void *owner) { static_cast<PChunkyFile>(owner)->Release(); },
        [](void *) -> void * { return NewChunkTable(sizeof(ChunkRepresentation)); },
        [](void *, void *owner, void *index) {
            static_cast<PChunkyFile>(owner)->_owner.files.index = static_cast<PChunkTable>(index);
        },
        [](void *) -> void * { return FileObject::PfilCreateTemp(); },
        [](void *, void *file) { static_cast<PFileObject>(file)->AddRef(); },
        [](void *, void *owner, void *file) {
            static_cast<PChunkyFile>(owner)->_owner.files.main_file = static_cast<PFileObject>(file);
        },
        [](void *, void *file, ThreeMMU32 end) -> int { return static_cast<PFileObject>(file)->FSetFpMac(end); },
        [](void *, void *owner, ThreeMMU32 end, int borrowed) {
            auto file = static_cast<PChunkyFile>(owner);
            file->_owner.files.main_end = end;
            file->_owner.files.invalid_main = borrowed != 0;
            file->_owner.add_to_extra = borrowed != 0;
        },
        [](void *, void *owner, void *source, ThreeMMU32 offset, ThreeMMU32 bytes, int copy) -> int {
            struct ImportHost
            {
                PChunkyFile destination;
                PFileObject source;
            } host = {static_cast<PChunkyFile>(owner), static_cast<PFileObject>(source)};
            const ThreeMMForestImport services = {
                &host,
                [](void *c, ThreeMMU32 type, ThreeMMU32 offset, ThreeMMU32 bytes, ThreeMMU32 flags, int copy,
                   ThreeMMU32 *number) -> int {
                    auto h = static_cast<ImportHost *>(c);
                    ChunkNumber created;
                    if (copy)
                    {
                        DataBlock block(h->source, offset, bytes, flags & fcrpPacked);
                        if (!h->destination->FAddBlck(&block, type, &created))
                            return 0;
                    }
                    else
                    {
                        LegacyLong position;
                        h->destination->_GetUniqueCno(type, &position, &created);
                        if (!h->destination->_FAdd(0, type, created, position, nullptr))
                            return 0;
                        auto row = static_cast<ChunkRepresentation *>(ChunkFixed(h->destination->_Index(), position));
                        row->fp = bytes ? offset : 0;
                        three_mm_chunk_record_set_bytes(row, expandedRecords, bytes);
                        three_mm_chunk_record_set_flags(row, expandedRecords, flags, fcrpPacked | fcrpForest);
                    }
                    *number = created;
                    return 1;
                },
                [](void *c, ThreeMMU32 parentType, ThreeMMU32 parentNumber, ThreeMMU32 type, ThreeMMU32 number,
                   ThreeMMU32 slot) -> int {
                    return static_cast<ImportHost *>(c)->destination->FAdoptChild(parentType, parentNumber, type,
                                                                                  number, slot);
                }};
            const ThreeMMReadStream stream = {
                source, static_cast<ThreeMMU32>(static_cast<PFileObject>(source)->FpMac()), ReadChunkFile};
            return three_mm_forest_read(&stream, offset, bytes, &services, KauaiCopyAllocator(), copy) ==
                   THREE_MM_CHUNK_OK;
        },
        [](void *, void *owner) { AssertPo(static_cast<PChunkyFile>(owner), fcflFull | fcflGraph); }};
    return static_cast<PChunkyFile>(
        three_mm_chunk_construct_forest(&construction, pflo->pfil, pflo->fp, pflo->cb, fCopyData));
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
    qcrp = (ChunkRepresentation *)ChunkFixed(_Index(), icrp);
    Assert(qcrp->cki.ctg == ctg && qcrp->cki.cno == cno, 0);
    return FPure(three_mm_chunk_record_flags(qcrp, expandedRecords, fcrpForest));
}

/***************************************************************************
    Set or clear the "forest" flag.
***************************************************************************/
void ChunkyFile::SetForest(ChunkTagOrType ctg, ChunkNumber cno, bool fForest)
{
    AssertThis(0);
    _SetChunkFlag(ctg, cno, THREE_MM_CHUNK_FOREST, fForest);
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
ThreeMMChunkRegistry ChunkyFile::_RegistryServices()
{
    return {
        [](void *c) -> void * { auto self = static_cast<PChunkyFile>(c); AssertPo(self, 0); return self->PcflNext(); },
        [](void *c) -> int { return static_cast<PChunkyFile>(c)->_owner.marked; },
        [](void *c) -> ThreeMMI32 { return static_cast<PChunkyFile>(c)->_cactRef; },
        [](void *c) { auto self = static_cast<PChunkyFile>(c); AssertPo(self, 0); self->_owner.marked = false; },
        [](void *c) { delete static_cast<PChunkyFile>(c); }};
}
void ChunkyFile::ClearMarks(void)
{
    const auto registry = _RegistryServices();
    AssertDo(three_mm_chunk_clear_marks(PcflFirst(), &registry) == THREE_MM_CHUNK_OK, "invalid container registry");
}
/***************************************************************************
    Static method: close any chunky files that are unmarked and have 0
    open count.
***************************************************************************/
void ChunkyFile::CloseUnmarked(void)
{
    const auto registry = _RegistryServices();
    AssertDo(three_mm_chunk_close_unmarked(PcflFirst(), &registry) == THREE_MM_CHUNK_OK, "invalid container registry" );
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
    static_assert(fcflWriteEnable == THREE_MM_CHUNK_WRITE && fcflTemp == THREE_MM_CHUNK_TEMP &&
        fcflMark == THREE_MM_CHUNK_MARK && fcflAddToExtra == THREE_MM_CHUNK_ADD_EXTRA &&
        fcflReadFromExtra == THREE_MM_CHUNK_READ_EXTRA, "container flag encoding");
    const ThreeMMChunkOptionServices services = {this,
        [](void *c) -> ThreeMMU32 { return static_cast<PChunkyFile>(c)->_MainFile()->GrffilCur(); },
        [](void *c, ThreeMMU32 flags, ThreeMMU32 mask) -> int { return static_cast<PChunkyFile>(c)->_MainFile()->FSetGrffil(flags, mask); },
        [](void *c) { static_cast<PChunkyFile>(c)->_ReadFreeMap(); }};
    return three_mm_chunk_owner_options(&_owner, &services, grfcfl, grfcflMask) == THREE_MM_CHUNK_OK;
}
/***************************************************************************
    Return the level of error that we've encountered for this chunky file.
***************************************************************************/
namespace {
const ThreeMMChunkErrorServices ChunkErrors = {
    [](void *file) -> ThreeMMI32 { return static_cast<PFileObject>(file)->ElError(); },
    [](void *file, ThreeMMI32 level) { static_cast<PFileObject>(file)->SetEl(level); }};
}
LegacyLong ChunkyFile::ElError(void)
{
    AssertThis(0);
    return three_mm_chunk_owner_error(&_owner, &ChunkErrors);
}

/***************************************************************************
    Make sure the el level on both files is at or below el.
***************************************************************************/
void ChunkyFile::ResetEl(LegacyLong el)
{
    AssertThis(0);
    AssertIn(el, elNil, kcbMax);

    three_mm_chunk_owner_reset_error(&_owner, &ChunkErrors, el);
}

/***************************************************************************
    Decrement the open count.  If it is zero and the chunky file isn't
    marked, the file is closed.
***************************************************************************/
void ChunkyFile::Release(void)
{
    AssertBaseThis(0);
    ThreeMMI32 remaining;
    const int decision = three_mm_chunk_release(_cactRef, _owner.marked, &remaining);
    if (decision < 0) { Bug("calling Release without an AddRef"); return; }
    _cactRef = remaining;
    if (decision) delete this;
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
    Assert(three_mm_chunk_context_valid(&_link), "invalid container registry links");
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
    FilePosition fpBase = _owner.files.invalid_main ? 0 : size(ChunkyFilePrefix);

    Assert(!_owner.files.invalid_main || _owner.add_to_extra, 0);

    AssertVarMem(_Index());
    AssertPo(_MainFile(), 0);
    if (_owner.files.main_map != pvNil)
    {
        AssertPvAlloced(_owner.files.main_map, sizeof(*_owner.files.main_map));
        Assert(!_owner.files.free_map_unread, "free map never read, but exists!");
    }
    Assert(_owner.files.main_end >= fpBase, "fpMac wrong");

    if (_ExtraFile() != pvNil)
    {
        AssertPo(_ExtraFile(), 0);
        if (_owner.files.extra_map != pvNil)
            AssertPvAlloced(_owner.files.extra_map, sizeof(*_owner.files.extra_map));
    }
    else
        Assert(_owner.files.extra_map == pvNil, 0);

#ifndef CHUNK_BIG_INDEX
    Assert(_owner.runtime.count <= _owner.runtime.storage.buffers[0].capacity / sizeof(ThreeMMChunkRuntimeEntry), "bad runtime cache count");
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
    for (icrp = _Index()->count; icrp-- != 0;)
    {
        ChunkReadFixed(_Index(), icrp, &crp);
        cbVar = three_mm_chunk_table_variable_bytes(_Index(), icrp);
        cbRgch = three_mm_chunk_record_name_bytes(&crp, expandedRecords, cbVar);

        AssertIn(crp.ckid, 0, kckidMax + 1);
        AssertIn(cbRgch, 0, kcbMaxDataStn + 1);

        // we use this in checking the graph structure
        Assert(!three_mm_chunk_record_flags(&crp, expandedRecords, fcrpMarkT), "fcrpMarkT set");

        ckiNew = {crp.cki.ctg, crp.cki.cno};
        ccrpRefTot += crp.ccrpRef;
        ckidTot += crp.ckid;

#ifdef CHUNK_BIG_INDEX
        Assert(crp.rti >= 0 && crp.rti <= three_mm_chunk_runtime_highest(), "bad runtime ID");
#endif // CHUNK_BIG_INDEX

        // assert that the file storage (fp,cb) is OK
        if (three_mm_chunk_record_flags(&crp, expandedRecords, fcrpOnExtra))
        {
            Assert(_ExtraFile() != pvNil, "fcrpOnExtra wrong");
            AssertIn(crp.fp, 0, _owner.files.extra_end);
            AssertIn(three_mm_chunk_record_bytes(&crp, expandedRecords), 1, _owner.files.extra_end - crp.fp + 1);
            cbTotExtra += three_mm_chunk_record_bytes(&crp, expandedRecords);
        }
        else
        {
            AssertVar(three_mm_chunk_record_bytes(&crp, expandedRecords) > 0 && (crp.fp >= fpBase) || three_mm_chunk_record_bytes(&crp, expandedRecords) == 0 && crp.fp == 0, "bad fp", &crp.fp);
            AssertVar(crp.fp <= _owner.files.main_end, "bad fp", &crp.fp);
            AssertIn(three_mm_chunk_record_bytes(&crp, expandedRecords), 0, _owner.files.main_end - crp.fp + 1);
            cbTot += three_mm_chunk_record_bytes(&crp, expandedRecords);
        }

        // assert that this crp is in order
        AssertVar(fFirstCrp || ckiNew.ctg < ckiOld.ctg || ckiNew.ctg == ckiOld.ctg && ckiNew.cno < ckiOld.cno,
                  "crp not in order", &ckiNew);
        ckiOld = ckiNew;
        fFirstCrp = fFalse;

        fFirstKid = fTrue;
        for (ikid = 0; ikid < crp.ckid; ikid++)
        {
            ChunkReadBytes(_Index(), icrp, _BvKid(ikid), size(ChildChunkIdentification), &kid);
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
    Assert(cbTotExtra <= _owner.files.extra_end, "overlapping chunks on extra");
    Assert(cbTot <= _owner.files.main_end - fpBase, "overlapping chunks on file");

    if (_owner.files.main_map != pvNil)
    {
        LegacyLong ifsm;
        ThreeMMChunkFreeRange fsm;
        FilePosition fpOld;

        fpOld = _owner.files.main_end;
        for (ifsm = _owner.files.main_map->count; ifsm-- != 0;)
        {
            AssertDo(three_mm_free_map_get(_owner.files.main_map, ifsm, &fsm) == THREE_MM_CHUNK_OK, "bad free range");
            Assert(fsm.offset >= fpBase && fsm.offset < fpOld, "bad fp in fsm");
            Assert(fsm.bytes + fsm.offset > fsm.offset && fsm.bytes + fsm.offset < fpOld, "bad cb in fsm");
            fpOld = fsm.offset;
            cbTot += fsm.bytes;
        }
        Assert(cbTot <= _owner.files.main_end - fpBase, "too much free space");
    }
    if (_owner.files.extra_map != pvNil)
    {
        LegacyLong ifsm;
        ThreeMMChunkFreeRange fsm;
        FilePosition fpOld;

        fpOld = _owner.files.extra_end;
        for (ifsm = _owner.files.extra_map->count; ifsm-- != 0;)
        {
            AssertDo(three_mm_free_map_get(_owner.files.extra_map, ifsm, &fsm) == THREE_MM_CHUNK_OK, "bad extra free range");
            Assert(fsm.offset >= 0 && fsm.offset < fpOld, "bad fp in fsm");
            Assert(fsm.bytes + fsm.offset > fsm.offset && fsm.bytes + fsm.offset < fpOld, "bad cb in fsm");
            fpOld = fsm.offset;
            cbTotExtra += fsm.bytes;
        }
        Assert(cbTotExtra <= _owner.files.extra_end, "too much free space on extra");
    }

    if (grfcfl & fcflGraph)
        Assert(_TValidIndex() != tNo, "bad index");

#ifndef CHUNK_BIG_INDEX
    ThreeMMChunkRuntimeEntry cached, previous;
    for (ThreeMMU32 i = 0; i < _owner.runtime.count; ++i)
    {
        AssertDo(three_mm_chunk_runtime_at(&_owner.runtime, i, &cached) == THREE_MM_CHUNK_OK, "bad runtime cache entry");
        Assert(cached.runtime != 0 && _FFindCtgCno(cached.type, cached.number, &icrpT), "runtime chunk missing");
        if (i) Assert(previous.type < cached.type || (previous.type == cached.type && previous.number < cached.number), "runtime order");
        previous = cached;
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
    MarkPv(_Index()); MarkPv(_Index()->storage.buffers[0].data); MarkPv(_Index()->storage.buffers[1].data);
    if (_owner.files.main_map) { MarkPv(_owner.files.main_map); MarkPv(_owner.files.main_map->storage.buffers[0].data); }
    if (_owner.files.extra_map) { MarkPv(_owner.files.extra_map); MarkPv(_owner.files.extra_map->storage.buffers[0].data); }
#ifndef CHUNK_BIG_INDEX
    MarkPv(_owner.runtime.storage.buffers[0].data);
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
    AssertDo(three_mm_chunk_table_clear_flags(_Index(), fcrpMarkT) == THREE_MM_STORAGE_OK, "invalid chunk flag clear");
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
    Sets the _owner.map_offset and _owner.map_bytes fields and sets the _owner.files.free_map_unread
    flag. The free map is read separately by _ReadFreeMap. This is to avoid
    hogging memory for the free map when we may never need it.
***************************************************************************/
bool ChunkyFile::_FReadIndex(void)
{
    AssertBaseThis(0);
    AssertPo(_MainFile(), 0);
    Assert(!_owner.files.invalid_main, 0);
    Assert(_Index() == pvNil && _owner.files.main_map == pvNil && _ExtraFile() == pvNil && _owner.files.extra_map == pvNil,
           "cfl has wrong non-nil entries");
    static_assert(kcvnCur == THREE_MM_CHUNK_CURRENT_VERSION && kcvnBack == THREE_MM_CHUNK_BACKWARD_VERSION &&
                  kcvnMin == 1 && sizeof(ChunkyFilePrefix) == THREE_MM_CHUNK_PREFIX_BYTES &&
                  sizeof(ChildChunkIdentification) == sizeof(ThreeMMChunkChild), "Chunk C89 wire contract");
    const auto end = _MainFile()->FpMac();
    if (end < size(ChunkyFilePrefix))
        return fFalse;
    const short nativeOrder = 1;
    ThreeMMChunkPolicy policy = {};
#ifdef CHUNK_BIG_INDEX
    policy.expanded_runtime = 1;
#endif
    policy.native_big_endian = reinterpret_cast<const byte *>(&nativeOrder)[0] == 0;
    policy.max_name_bytes = kcbMaxDataStn;
    ThreeMMReadStream stream = {_MainFile(), static_cast<ThreeMMU32>(end), ReadChunkFile};
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
    PChunkTable group = NewChunkTable(size(ChunkRepresentation), access.count);
    if (!group)
        return fFalse;
    const ThreeMMChunkMaterializer materializer = {group,
        [](void *, const char *text, void *wire, ThreeMMU32 capacity, ThreeMMU32 *bytes) -> int {
            String converted; converted.SetSzs(const_cast<char *>(text));
            if (converted.CbData() > capacity) return 0;
            converted.GetData(wire); *bytes = converted.CbData(); return 1;
        },
        [](void *context, ThreeMMU32 position, const ThreeMMChunkEntry *entry, const void *variable, ThreeMMU32 bytes) -> int {
            ChunkRepresentation row = {};
            three_mm_chunk_record_set(&row, expandedRecords, entry);
            return ChunkInsert(static_cast<PChunkTable>(context), position, bytes, const_cast<void *>(variable), &row);
        }};
    if (three_mm_chunk_materialize(index, &materializer, KauaiCopyAllocator()) != THREE_MM_CHUNK_OK)
    { CloseChunkTable(group); return false; }
    _owner.files.index = group;
    _owner.map_bytes = header.map_bytes;
    _owner.map_offset = header.map_offset;
    _owner.files.free_map_unread = header.map_bytes > 0;
    _owner.files.main_end = header.index_offset;
    return fTrue;
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
    Assert(_owner.files.free_map_unread, "free map already read");
    Assert(!_owner.files.invalid_main, 0);

    const bool existed = _owner.files.main_map != nullptr;
    const ThreeMMReadStream stream = {_MainFile(), static_cast<ThreeMMU32>(_MainFile()->FpMac()), ReadChunkFile};
    three_mm_free_map_load(&_owner.files.main_map, &_owner.files.free_map_unread, &stream, _owner.map_offset, _owner.map_bytes, KauaiCopyAllocator());
    if (existed) Bug("free map already exists");
}

/***************************************************************************
    If we have don't have write permission or there's an extra file,
    write out a new file and do the rename stuff.  If not, just write
    the index and free map.
***************************************************************************/
namespace {
bool WriteChunkDirectory(PFileObject, PChunkTable, ThreeMMChunkFreeMap *, FilePosition, ChunkTagOrType);
struct ChunkSaveHost
{
    ChunkyFile *owner;
    Filename *directory, *target;
    PChunkTable index;
};
void SaveEntry(void *context, ThreeMMU32 position, ThreeMMChunkEntry *entry)
{
    AssertDo(ReadRuntimeChunk(context, position, entry), "invalid save index position");
}
void SavePlace(void *context, ThreeMMU32 position, ThreeMMU32 offset, ThreeMMU32 flags)
{
    auto row = static_cast<ChunkRepresentation *>(ChunkFixed(static_cast<PChunkTable>(context), position));
    row->fp = offset;
    three_mm_chunk_record_set_flags(row, expandedRecords, flags, 0xffffffffUL);
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
void SaveReleaseMap(void *, void *map) { three_mm_free_map_close(static_cast<ThreeMMChunkFreeMap *>(map)); }
int SaveIsTemp(void *, void *file) { return static_cast<PFileObject>(file)->FTemp(); }
void SaveSetTemp(void *, void *file, int temporary) { static_cast<PFileObject>(file)->SetTemp(temporary != 0); }
int SaveIndex(void *context, const ThreeMMChunkSaveState *state, ThreeMMU32 creator)
{
    return WriteChunkDirectory(static_cast<PFileObject>(state->main_file), static_cast<ChunkSaveHost *>(context)->index,
        static_cast<ThreeMMChunkFreeMap *>(state->main_map), state->main_end, creator);
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
    if (_owner.files.invalid_main)
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
    auto state = three_mm_chunk_owner_save_state(&_owner);
    ChunkSaveHost host = {this, &fni, pfni, _Index()};
    const auto publish = [](void *context, const ThreeMMChunkSaveState *saved) {
        auto self = static_cast<ChunkSaveHost *>(context)->owner;
        three_mm_chunk_owner_publish_save(&self->_owner, saved);
    };
    const ThreeMMChunkSaveIndex index = {_Index(), static_cast<ThreeMMU32>(_Index()->count), SaveEntry, SavePlace};
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
    PChunkTable index;
    ThreeMMChunkFreeMap * map;
};
int WriteChunkIndex(void *context, ThreeMMU32 offset, ThreeMMU32 bytes)
{
    auto output = static_cast<ChunkIndexOutput *>(context);
    DataBlock block(output->file, offset, bytes);
    const short one = 1;
    const ThreeMMStorageWriter writer = {&block, bytes,
        [](void *c, ThreeMMU32 offset, const void *data, size_t count) -> int {
            return static_cast<PDataBlock>(c)->FWriteRgb(const_cast<void *>(data), count, offset);
        }};
    return three_mm_chunk_table_write(output->index, &writer, reinterpret_cast<const byte *>(&one)[0] == 0, koskCur) == THREE_MM_STORAGE_OK;
}
int WriteChunkMap(void *context, ThreeMMU32 offset, ThreeMMU32 bytes)
{
    auto output = static_cast<ChunkIndexOutput *>(context);
    DataBlock block(output->file, offset, bytes);
    const short one = 1;
    const ThreeMMStorageWriter writer = {&block, bytes,
        [](void *c, ThreeMMU32 offset, const void *data, size_t count) -> int {
            return static_cast<PDataBlock>(c)->FWriteRgb(const_cast<void *>(data), count, offset);
        }};
    return three_mm_free_map_write(output->map, &writer, reinterpret_cast<const byte *>(&one)[0] == 0, koskCur) == THREE_MM_STORAGE_OK;
}
int WriteChunkPrefix(void *context, const void *bytes, size_t count)
{
    return static_cast<ChunkIndexOutput *>(context)->file->FWriteRgb(const_cast<void *>(bytes), count, 0);
}
}
namespace {
bool WriteChunkDirectory(PFileObject file, PChunkTable index, ThreeMMChunkFreeMap * map, FilePosition end, ChunkTagOrType creator)
{
    const short one = 1;
    ChunkIndexOutput output = {file, index, map};
    const ThreeMMChunkSave save = {creator, static_cast<ThreeMMU32>(end),
        static_cast<ThreeMMU32>(three_mm_chunk_table_wire_size(index)), three_mm_free_map_wire_size(map),
        reinterpret_cast<const byte *>(&one)[0] == 0, koskCur};
    const ThreeMMChunkWriter writer = {&output, WriteChunkIndex, WriteChunkMap, WriteChunkPrefix};
    return three_mm_chunk_write_index(&save, &writer, nullptr) == THREE_MM_CHUNK_OK;
}
}
bool ChunkyFile::_FWriteIndex(ChunkTagOrType ctgCreator)
{
    AssertBaseThis(0);
    AssertVarMem(_Index());
    AssertPo(_MainFile(), 0);
    return WriteChunkDirectory(_MainFile(), _Index(), _owner.files.main_map, _owner.files.main_end, ctgCreator);
}

/***************************************************************************
    Save a copy of the chunky file out to *pfni.  The ChunkyFile and its FileObject
    are untouched.
***************************************************************************/
namespace {
struct ChunkCopyHost { PChunkyFile destination; PChunkTable source, target; };
void CopyPin(void *context) { ChunkLock(static_cast<ChunkCopyHost *>(context)->source); }
void CopyUnpin(void *context) { ChunkUnlock(static_cast<ChunkCopyHost *>(context)->source); }
int CopyInsert(void *context, ThreeMMU32 position, const ThreeMMChunkEntry *entry)
{
    auto host = static_cast<ChunkCopyHost *>(context);
    auto original = static_cast<ChunkRepresentation *>(ChunkFixed(host->source, position));
    ChunkRepresentation row = *original;
    row.fp = entry->offset; three_mm_chunk_record_set_bytes(&row, expandedRecords, entry->bytes); three_mm_chunk_record_set_flags(&row, expandedRecords, entry->flags, 0xffffffffUL);
    return ChunkInsert(host->target, position, three_mm_chunk_table_variable_bytes(host->source, position), ChunkVariable(host->source, position), &row);
}
}
bool ChunkyFile::FSaveACopy(ChunkTagOrType ctgCreator, Filename *pfni)
{
    AssertThis(fcflFull | fcflGraph);
    AssertPo(pfni, ffniFile);
    PChunkyFile destination = ChunkyFile::PcflCreate(pfni, fcflWriteEnable);
    if (!destination) { PushErc(ercCflSaveCopy); return fFalse; }
    ChunkCopyHost host = {destination, _Index(), destination->_Index()};
    const auto finish = [](void *context, ThreeMMU32 end, ThreeMMU32 creator) -> int {
        auto file = static_cast<ChunkCopyHost *>(context)->destination;
        file->_owner.files.main_end = end;
        return file->FSave(creator);
    };
    const ThreeMMChunkSaveIndex index = {_Index(), static_cast<ThreeMMU32>(_Index()->count), SaveEntry, nullptr};
    const ThreeMMChunkCopyServices services = {&host, _MainFile(), _ExtraFile(), destination->_MainFile(),
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

    qcrp = (ChunkRepresentation *)ChunkFixed(_Index(), icrp);
    return FPure(three_mm_chunk_record_flags(qcrp, expandedRecords, fcrpOnExtra));
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

    const auto policy = _StoragePolicy(); const auto services = _StorageServices();
    ThreeMMChunkLocation location;
    if (three_mm_chunk_storage_cache(&policy, &services, icrp, pflo ? &location : nullptr) != THREE_MM_CHUNK_OK) return false;
    if (pflo) *pflo = {static_cast<PFileObject>(location.file), static_cast<FilePosition>(location.offset), static_cast<LegacyLong>(location.bytes)};
    return true;
}
/***************************************************************************
    Get the FileLocation from the chunk.
***************************************************************************/
void ChunkyFile::_GetFlo(LegacyLong icrp, PFileLocation pflo)
{
    AssertThis(0);
    AssertVarMem(pflo);

    const auto policy = _StoragePolicy(); const auto services = _StorageServices();
    ThreeMMChunkLocation location;
    AssertDo(three_mm_chunk_storage_fetch(&policy, &services, icrp, &location) == THREE_MM_CHUNK_OK, "invalid chunk fetch");
    *pflo = {static_cast<PFileObject>(location.file), static_cast<FilePosition>(location.offset), static_cast<LegacyLong>(location.bytes)};
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
    qcrp = (ChunkRepresentation *)ChunkFixed(_Index(), icrp);
    pblck->Set(&flo, three_mm_chunk_record_flags(qcrp, expandedRecords, fcrpPacked));
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
    _SetChunkFlag(ctg, cno, THREE_MM_CHUNK_PACKED, fPacked);
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
    qcrp = (ChunkRepresentation *)ChunkFixed(_Index(), icrp);
    Assert(qcrp->cki.ctg == ctg && qcrp->cki.cno == cno, 0);
    return FPure(three_mm_chunk_record_flags(qcrp, expandedRecords, fcrpPacked));
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
    Assert(_ExtraFile() == pvNil, 0);
    Assert(_owner.files.extra_map == pvNil, 0);

    if (pvNil == (_owner.files.extra_file = FileObject::PfilCreateTemp()))
        return fFalse;

    _owner.files.extra_end = 0;
    return fTrue;
}

/***************************************************************************
    Find a place to put a block the given size.
***************************************************************************/
namespace {
struct ChunkHeapHost { PFileObject file; ThreeMMChunkFreeMap **map; };
ThreeMMU32 HeapCount(void *context)
{
    auto map = *static_cast<ChunkHeapHost *>(context)->map; return map ? map->count : 0;
}
void HeapGet(void *context, ThreeMMU32 position, ThreeMMChunkFreeRange *range)
{
    AssertDo(three_mm_free_map_get(*static_cast<ChunkHeapHost *>(context)->map, position, range) == THREE_MM_CHUNK_OK, "bad free range");
}
void HeapPut(void *context, ThreeMMU32 position, const ThreeMMChunkFreeRange *range)
{
    AssertDo(three_mm_free_map_put(*static_cast<ChunkHeapHost *>(context)->map, position, range) == THREE_MM_CHUNK_OK, "bad free range update");
}
void HeapErase(void *context, ThreeMMU32 position)
{
    AssertDo(three_mm_free_map_erase(*static_cast<ChunkHeapHost *>(context)->map, position) == THREE_MM_CHUNK_OK, "bad free range removal");
}
int HeapInsert(void *context, ThreeMMU32 position, const ThreeMMChunkFreeRange *range)
{
    return three_mm_free_map_insert(*static_cast<ChunkHeapHost *>(context)->map, position, range) == THREE_MM_CHUNK_OK;
}
int HeapEnsure(void *context)
{
    auto map = static_cast<ChunkHeapHost *>(context)->map;
    return *map || three_mm_free_map_create(KauaiCopyAllocator(), 1, map) == THREE_MM_CHUNK_OK;
}
ThreeMMU32 HeapPhysicalEnd(void *context) { return static_cast<ChunkHeapHost *>(context)->file->FpMac(); }
int HeapResize(void *context, ThreeMMU32 bytes) { return static_cast<ChunkHeapHost *>(context)->file->FSetFpMac(bytes); }
ThreeMMChunkHeap ChunkHeapServices(ChunkHeapHost *host)
{
    return {host, HeapCount, HeapGet, HeapPut, HeapErase, HeapInsert, HeapEnsure, HeapPhysicalEnd, HeapResize};
}
}
ThreeMMChunkStoragePolicy ChunkyFile::_StoragePolicy()
{
    return three_mm_chunk_owner_storage_policy(&_owner, kcbMaxCrp);
}
ThreeMMChunkStorageServices ChunkyFile::_StorageServices()
{
    ThreeMMChunkStorageServices services = {};
    services.context = this;
    services.file = [](void *c, int extra) -> void * {
        auto self = static_cast<PChunkyFile>(c); return extra ? self->_ExtraFile() : self->_MainFile();
    };
    services.create_extra = [](void *c) -> int { return static_cast<PChunkyFile>(c)->_FCreateExtra(); };
    services.reserve = [](void *c, int extra, ThreeMMU32 bytes, ThreeMMU32 *offset) -> int {
        auto self = static_cast<PChunkyFile>(c);
        auto endField = extra ? &self->_owner.files.extra_end : &self->_owner.files.main_end;
        auto map = extra ? &self->_owner.files.extra_map : &self->_owner.files.main_map;
        ChunkHeapHost host = {extra ? self->_ExtraFile() : self->_MainFile(), map};
        const auto heap = ChunkHeapServices(&host);
        ThreeMMU32 end = *endField;
        if (three_mm_chunk_heap_allocate(&heap, &end, bytes, offset) != THREE_MM_CHUNK_OK) return 0;
        *endField = end; return 1;
    };
    services.release_space = [](void *c, int extra, ThreeMMU32 offset, ThreeMMU32 bytes) {
        static_cast<PChunkyFile>(c)->_FreeFpCb(extra != 0, offset, bytes);
    };
    services.entry = [](void *c, ThreeMMU32 position, ThreeMMChunkEntry *entry) {
        AssertDo(ReadRuntimeChunk(static_cast<PChunkyFile>(c)->_Index(), position, entry), "invalid storage entry");
    };
    services.cache_place = [](void *c, ThreeMMU32 position, ThreeMMU32 offset) {
        auto row = static_cast<ChunkRepresentation *>(ChunkFixed(static_cast<PChunkyFile>(c)->_Index(), position));
        three_mm_chunk_record_set_flags(row, expandedRecords, fcrpOnExtra, fcrpOnExtra); row->fp = offset;
    };
    services.copy = [](void *, const ThreeMMChunkLocation *source, const ThreeMMChunkLocation *destination) -> int {
        FileLocation from = {static_cast<PFileObject>(source->file), static_cast<FilePosition>(source->offset), static_cast<LegacyLong>(source->bytes)};
        FileLocation to = {static_cast<PFileObject>(destination->file), static_cast<FilePosition>(destination->offset), static_cast<LegacyLong>(destination->bytes)};
        return from.FCopy(&to);
    };
#ifdef CHUNK_STATS
    if (vfDumpChunkRequests) services.trace = [](void *c, int kind, const ThreeMMChunkEntry *entry) {
        auto self = static_cast<PChunkyFile>(c); String text;
        if (kind == THREE_MM_STORAGE_CACHE)
            text.FFormatSz(PszLit("Cache: '%f', 0x%08x, fp = 0x%08x, cb = 0x%08x"), entry->type, entry->number, entry->offset, entry->bytes);
        else if (kind == THREE_MM_STORAGE_FETCH_EXTRA)
            text.FFormatSz(PszLit("Fetch from extra: '%f', 0x%08x, fp = 0x%08x, cb = 0x%08x"), entry->type, entry->number, entry->offset, entry->bytes);
        else
            text.FFormatSz(PszLit("Fetch from %z: '%f', 0x%08x, fp = 0x%08x, cb = 0x%08x"),
                entry->flags & fcrpOnExtra ? PszLit("extra") : PszLit("main"), entry->type, entry->number, entry->offset, entry->bytes);
        DumpStn(&text, self->_MainFile());
    };
#endif
    return services;
}
bool ChunkyFile::_FAllocFlo(LegacyLong cb, PFileLocation pflo, bool fForceOnExtra)
{
    AssertBaseThis(0); AssertIn(cb, 0, kcbMax); AssertVarMem(pflo);
    if (cb > kcbMaxCrp) { Bug("Requested FileLocation too big"); return false; }
    Assert(cb == 0 || fForceOnExtra || _owner.add_to_extra || !_owner.files.free_map_unread, "free map not read yet");
    const auto policy = _StoragePolicy(); const auto services = _StorageServices();
    ThreeMMChunkLocation location;
    if (three_mm_chunk_storage_allocate(&policy, &services, cb, fForceOnExtra, &location) != THREE_MM_CHUNK_OK)
    { TrashVar(pflo); return false; }
    *pflo = {static_cast<PFileObject>(location.file), static_cast<FilePosition>(location.offset), static_cast<LegacyLong>(location.bytes)};
    return true;
}
/***************************************************************************
    Look for the (ctg, cno) pair.  Fills *picrp with where it should be.
    Returns whether or not it was found.  Assumes the _Index() is sorted
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
namespace {
struct ChunkEditHost
{
    ChunkyFile *owner;
    PDataBlock output, source, temporary;
    void *bytes;
    ChunkGraphEnumerator *enumerator;
};
struct ChunkOpsHost
{
    ChunkEditHost edit;
    DataBlock payload;
    String name;
    ChunkGraphEnumerator deletion;
    explicit ChunkOpsHost(ChunkyFile *owner) : edit{owner, nullptr, nullptr, nullptr, nullptr, &deletion} {}
};
}
ThreeMMChunkEdit ChunkyFile::_EditServices(void *context)
{
    ThreeMMChunkEdit edit = {};
    edit.context = context;
    edit.count = [](void *c) -> ThreeMMU32 { return static_cast<ChunkEditHost *>(c)->owner->_Index()->count; };
    edit.entry = [](void *c, ThreeMMU32 i, ThreeMMChunkEntry *entry) {
        SaveEntry(static_cast<ChunkEditHost *>(c)->owner->_Index(), i, entry);
    };
    edit.child = [](void *c, ThreeMMU32 parent, ThreeMMU32 i, ThreeMMChunkChild *entry) {
        ChildChunkIdentification child;
        ChunkReadBytes(static_cast<ChunkEditHost *>(c)->owner->_Index(), parent, i * sizeof(child), sizeof(child), &child);
        *entry = {child.cki.ctg, child.cki.cno, child.chid};
    };
    edit.insert = [](void *c, ThreeMMU32 i) -> int { return ChunkInsert(static_cast<ChunkEditHost *>(c)->owner->_Index(), i, 0); };
    edit.store = [](void *c, ThreeMMU32 i, const ThreeMMChunkEntry *entry, int initialize) {
        auto row = static_cast<ChunkRepresentation *>(ChunkFixed(static_cast<ChunkEditHost *>(c)->owner->_Index(), i));
        if (initialize) ClearPb(row, sizeof(*row));
        three_mm_chunk_record_set(row, expandedRecords, entry);
    };
    edit.erase = [](void *c, ThreeMMU32 i) { ChunkDelete(static_cast<ChunkEditHost *>(c)->owner->_Index(), i); };
    edit.allocate = [](void *c, ThreeMMU32 bytes, ThreeMMChunkLocation *location) -> int {
        auto self = static_cast<ChunkEditHost *>(c)->owner;
        FileLocation native;
        if (!self->_FAllocFlo(bytes, &native)) return 0;
        *location = {native.pfil, static_cast<ThreeMMU32>(native.fp), static_cast<ThreeMMU32>(native.cb),
            native.pfil == self->_ExtraFile() ? 1U : 0U};
        return 1;
    };
    edit.free_space = [](void *c, int extra, ThreeMMU32 offset, ThreeMMU32 bytes) {
        static_cast<ChunkEditHost *>(c)->owner->_FreeFpCb(extra != 0, offset, bytes);
    };
    edit.output = [](void *c, const ThreeMMChunkLocation *location, int added) {
        auto host = static_cast<ChunkEditHost *>(c);
        auto block = host->output ? host->output : added ? host->temporary : nullptr;
        if (!block) return;
        if (!location) { block->Free(); return; }
        FileLocation native = {static_cast<PFileObject>(location->file), static_cast<FilePosition>(location->offset),
            static_cast<LegacyLong>(location->bytes)};
        block->Set(&native);
    };
    edit.write = [](void *c, const ThreeMMChunkLocation *location, int added) -> int {
        auto host = static_cast<ChunkEditHost *>(c);
        auto block = host->output ? host->output : host->temporary;
        FileLocation native = {static_cast<PFileObject>(location->file), static_cast<FilePosition>(location->offset),
            static_cast<LegacyLong>(location->bytes)};
        if (host->bytes) return added ? block->FWrite(host->bytes) : native.FWrite(host->bytes);
        if (host->source) return added ? host->source->FWriteToBlck(block, fTrue) : host->source->FWriteToFlo(&native, fTrue);
        return 1;
    };
    edit.reset_runtime = [](void *c, ThreeMMU32 type, ThreeMMU32 number) {
        static_cast<ChunkEditHost *>(c)->owner->_FSetRti(type, number, rtiNil);
    };
    edit.begin_delete = [](void *c, ThreeMMU32 type, ThreeMMU32 number) {
        auto host = static_cast<ChunkEditHost *>(c); host->enumerator->Init(host->owner, type, number);
    };
    edit.next_delete = [](void *c, int skip, ThreeMMChunkDeleteEvent *event) -> int {
        ChildChunkIdentification child; LegacyUlong flags;
        if (!static_cast<ChunkEditHost *>(c)->enumerator->FNextKid(&child, nullptr, &flags, skip ? fcgeSkipToSib : fcgeNil)) return 0;
        event->type = child.cki.ctg; event->number = child.cki.cno;
        event->flags = (flags & fcgePre ? THREE_MM_DELETE_PRE : 0) | (flags & fcgePost ? THREE_MM_DELETE_POST : 0) |
            (flags & fcgeRoot ? THREE_MM_DELETE_ROOT : 0) | (flags & fcgeError ? THREE_MM_DELETE_ERROR : 0);
        return 1;
    };
    edit.issue = [](void *, int kind) {
        if (kind == THREE_MM_EDIT_LOW_MEMORY) Warn("memory failure in ChunkyFile::Delete - adjusting ref counts");
        else if (kind == THREE_MM_EDIT_CYCLE_RISK) Warn("Performing this adoption may cause a cycle");
        else Bug("invalid chunk deletion reference");
    };
    edit.insert_child = [](void *c, ThreeMMU32 parent, ThreeMMU32 position, const ThreeMMChunkChild *child) -> int {
        ChildChunkIdentification native = {{child->type, child->number}, child->slot};
        return ChunkInsertBytes(static_cast<ChunkEditHost *>(c)->owner->_Index(), parent, position * sizeof(native), sizeof(native), &native);
    };
    edit.erase_child = [](void *c, ThreeMMU32 parent, ThreeMMU32 position) {
        ChunkDeleteBytes(static_cast<ChunkEditHost *>(c)->owner->_Index(), parent, position * sizeof(ChildChunkIdentification), sizeof(ChildChunkIdentification));
    };
    edit.put_child = [](void *c, ThreeMMU32 parent, ThreeMMU32 position, const ThreeMMChunkChild *child) {
        ChildChunkIdentification native = {{child->type, child->number}, child->slot};
        ChunkPutBytes(static_cast<ChunkEditHost *>(c)->owner->_Index(), parent, position * sizeof(native), sizeof(native), &native);
    };
    edit.descendant = [](void *c, ThreeMMU32 rootType, ThreeMMU32 rootNumber, ThreeMMU32 type, ThreeMMU32 number) -> int {
        const auto result = static_cast<ChunkEditHost *>(c)->owner->TIsDescendent(rootType, rootNumber, type, number);
        return result == tNo ? 0 : result == tYes ? 1 : 2;
    };
    edit.move_entry = [](void *c, ThreeMMU32 source, ThreeMMU32 destination) {
        ChunkMove(static_cast<ChunkEditHost *>(c)->owner->_Index(), source, destination);
    };
    edit.move_bytes = [](void *c, ThreeMMU32 source, ThreeMMU32 offset, ThreeMMU32 destination,
                         ThreeMMU32 destinationOffset, ThreeMMU32 bytes) -> int {
        return ChunkMoveBytes(static_cast<ChunkEditHost *>(c)->owner->_Index(), source, offset, destination, destinationOffset, bytes);
    };
    return edit;
}
void ChunkyFile::_SetChunkFlag(ChunkTagOrType type, ChunkNumber number, ThreeMMU32 flag, bool enabled)
{
    ChunkEditHost host = {this, nullptr, nullptr, nullptr, nullptr, nullptr}; const auto edit = _EditServices(&host);
    if (three_mm_chunk_set_flag(&edit, type, number, flag, enabled) != THREE_MM_CHUNK_OK) Bug("chunk not there");
}
void ChunkyFile::_GetUniqueCno(ChunkTagOrType ctg, LegacyLong *picrp, ChunkNumber *pcno)
{
    AssertBaseThis(0);
    AssertVarMem(picrp); AssertVarMem(pcno);
    ChunkEditHost host = {this, nullptr, nullptr, nullptr, nullptr, nullptr};
    const auto edit = _EditServices(&host);
    ThreeMMU32 position, number;
    AssertDo(three_mm_chunk_unique(&edit, ctg, &position, &number) == THREE_MM_CHUNK_OK, "invalid chunk index");
    *picrp = position; *pcno = number;
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
    AssertBaseThis(0); AssertIn(cb, 0, kcbMax); AssertNilOrPo(pblck, 0);
    ChunkEditHost host = {this, pblck, nullptr, nullptr, nullptr, nullptr};
    const auto edit = _EditServices(&host);
    const bool ok = three_mm_chunk_add(&edit, ctg, cno, icrp, cb, nullptr) == THREE_MM_CHUNK_OK;
    AssertThis(0);
    return ok;
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
    AssertBaseThis(0); AssertIn(cb, 0, kcbMax); AssertNilOrPo(pblck, 0); AssertNilOrPo(pblckSrc, 0);
    Assert(!pv || !pblckSrc, 0);
    DataBlock temporary;
    ChunkEditHost host = {this, pblck, pblckSrc, &temporary, pv, nullptr};
    const auto edit = _EditServices(&host);
    const bool ok = three_mm_chunk_put(&edit, ctg, cno, cb) == THREE_MM_CHUNK_OK;
    AssertThis(0);
    return ok;
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
    ChunkEditHost host = {this, nullptr, nullptr, nullptr, nullptr, nullptr};
    const auto edit = _EditServices(&host);
    if (three_mm_chunk_swap_data(&edit, ctg1, cno1, ctg2, cno2) != THREE_MM_CHUNK_OK) { Bug("can't find the chunks"); return; }
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
    ChunkEditHost host = {this, nullptr, nullptr, nullptr, nullptr, nullptr};
    const auto edit = _EditServices(&host);
    if (three_mm_chunk_swap_children(&edit, ctg1, cno1, ctg2, cno2) != THREE_MM_CHUNK_OK) { Bug("invalid child swap"); return; }
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
    ChunkOpsHost host(this);
    const auto endpoint = _OpsServices(&host);
    if (three_mm_chunk_move(&endpoint, ctg, cno, ctgNew, cnoNew) != THREE_MM_CHUNK_OK) { Bug("bad move request or references"); return; }
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
    ChunkGraphEnumerator enumerator;
    ChunkEditHost host = {this, nullptr, nullptr, nullptr, nullptr, &enumerator};
    const auto edit = _EditServices(&host);
    const int result = three_mm_chunk_delete(&edit, ctg, cno);
    if (result != THREE_MM_CHUNK_OK) { Bug("chunk not deletable or missing"); return; }
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
    ChunkGraphEnumerator enumerator;
    ChunkEditHost host = {this, nullptr, nullptr, nullptr, nullptr, &enumerator}; const auto edit = _EditServices(&host);
    if (three_mm_chunk_set_flag(&edit, ctg, cno, THREE_MM_CHUNK_LONER, fLoner) != THREE_MM_CHUNK_OK) Bug("chunk not there");
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
    qcrp = (ChunkRepresentation *)ChunkFixed(_Index(), icrp);
    return FPure(three_mm_chunk_record_flags(qcrp, expandedRecords, fcrpLoner));
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
    qcrp = (ChunkRepresentation *)ChunkFixed(_Index(), icrp);
    return qcrp->ccrpRef;
}

/***************************************************************************
    Determines if (ctgSub, cnoSub) is in the chunk subgraph of (ctg, cno).
    Returns tMaybe on error.
***************************************************************************/
namespace {
ThreeMMChunkGraph ChunkGraphServices(PChunkyFile file)
{
    return {file,
        [](void *context, ThreeMMU32 type, ThreeMMU32 number) -> ThreeMMU32 {
            return static_cast<PChunkyFile>(context)->Ckid(type, number);
        },
        [](void *context, ThreeMMU32 type, ThreeMMU32 number, ThreeMMU32 position, ThreeMMChunkChild *child) -> int {
            ChildChunkIdentification native;
            if (!static_cast<PChunkyFile>(context)->FGetKid(type, number, position, &native)) return 0;
            *child = {native.cki.ctg, native.cki.cno, native.chid}; return 1;
        }};
}
}
tribool ChunkyFile::TIsDescendent(ChunkTagOrType ctg, ChunkNumber cno, ChunkTagOrType ctgSub, ChunkNumber cnoSub)
{
    AssertThis(0);
    const auto graph = ChunkGraphServices(this);
    const int result = three_mm_chunk_reaches(&graph, KauaiCopyAllocator(), ctg, cno, ctgSub, cnoSub, CckiRef(ctgSub, cnoSub));
    return result == 0 ? tNo : result == 1 ? tYes : tMaybe;
}

/***************************************************************************
    Delete the given child chunk.  Handles deleting child chunks that are
    no longer referenced and don't have the fcrpLoner flag set.
***************************************************************************/
void ChunkyFile::DeleteChild(ChunkTagOrType ctgPar, ChunkNumber cnoPar, ChunkTagOrType ctgChild, ChunkNumber cnoChild, ChildChunkID chid)
{
    AssertThis(0);
    ChunkGraphEnumerator enumerator;
    ChunkEditHost host = {this, nullptr, nullptr, nullptr, nullptr, &enumerator};
    const auto edit = _EditServices(&host);
    const ThreeMMChunkChild child = {ctgChild, cnoChild, chid};
    if (three_mm_chunk_unlink(&edit, ctgPar, cnoPar, &child) != THREE_MM_CHUNK_OK) { Bug("chunk or child link missing"); return; }
    AssertThis(fcflFull);
}

/***************************************************************************
    Decrements the reference count on the chunk.  Return true if the ref
    count becomes zero (after decrementing) and fcrpLoner is not set.
***************************************************************************/
bool ChunkyFile::_FDecRefCount(LegacyLong icrp)
{
    AssertBaseThis(0);
    ChunkEditHost host = {this, nullptr, nullptr, nullptr, nullptr, nullptr};
    const auto edit = _EditServices(&host);
    int remove = 0;
    if (three_mm_chunk_decrement(&edit, icrp, &remove) != THREE_MM_CHUNK_OK) Bug("ref count wrong");
    return remove != 0;
}

/***************************************************************************
    Remove entry icrp from _Index() and add the file space to the free map.
***************************************************************************/
void ChunkyFile::_DeleteCore(LegacyLong icrp)
{
    AssertBaseThis(0);
    ChunkEditHost host = {this, nullptr, nullptr, nullptr, nullptr, nullptr};
    const auto edit = _EditServices(&host);
    AssertDo(three_mm_chunk_remove(&edit, icrp) == THREE_MM_CHUNK_OK, "invalid chunk removal");
}

/***************************************************************************
    Add the (fp, cb) to the free map.
***************************************************************************/
void ChunkyFile::_FreeFpCb(bool fOnExtra, FilePosition fp, LegacyLong cb)
{
    AssertBaseThis(0);
    Assert(cb > 0 || cb == 0 && fp == 0, "bad cb");
    Assert(fp >= 0 && (fOnExtra || fp >= size(ChunkyFilePrefix) || cb == 0 || _owner.files.invalid_main), "bad fp");

    if (!cb) return;
    if (!fOnExtra && _owner.files.free_map_unread) _ReadFreeMap();
    auto file = fOnExtra ? _ExtraFile() : _MainFile();
    auto map = fOnExtra ? &_owner.files.extra_map : &_owner.files.main_map;
    auto endField = fOnExtra ? &_owner.files.extra_end : &_owner.files.main_end;
    AssertPo(file, 0);
    ChunkHeapHost host = {file, map};
    const auto heap = ChunkHeapServices(&host);
    ThreeMMU32 end = *endField;
    const int result = three_mm_chunk_heap_free(&heap, &end, fp, cb);
    *endField = end;
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
namespace {
ThreeMMChunkNames ChunkNameServices(PChunkTable group)
{
    return {group,
        [](void *c) -> ThreeMMU32 { return static_cast<PChunkTable>(c)->count; }, ReadRuntimeChunk,
        [](void *c, ThreeMMU32 i, ThreeMMU32 offset, ThreeMMU32 bytes) -> int {
            return ChunkInsertBytes(static_cast<PChunkTable>(c), i, offset, bytes, nullptr);
        },
        [](void *c, ThreeMMU32 i, ThreeMMU32 offset, ThreeMMU32 bytes) { ChunkDeleteBytes(static_cast<PChunkTable>(c), i, offset, bytes); },
        [](void *c, ThreeMMU32 i) -> void * { return ChunkLockVariable(static_cast<PChunkTable>(c), i); },
        [](void *c) { ChunkUnlock(static_cast<PChunkTable>(c)); }, kcbMaxDataStn};
}
}
bool ChunkyFile::_FSetName(LegacyLong icrp, PString pstn)
{
    AssertBaseThis(0); AssertIn(icrp, 0, _Index()->count); AssertNilOrPo(pstn, 0);
    byte wire[kcbMaxDataStn]; ThreeMMChunkNameValue value = {};
    if (pstn) { value.bytes = pstn->CbData(); value.characters = pstn->Cch(); pstn->GetData(wire); value.wire = wire; }
    const auto names = ChunkNameServices(_Index());
    return three_mm_chunk_name_set(&names, icrp, pstn ? &value : nullptr) == THREE_MM_CHUNK_OK;
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
    AssertBaseThis(0); AssertIn(icrp, 0, _Index()->count); AssertPo(pstn, 0);
    const auto names = ChunkNameServices(_Index());
    const ThreeMMChunkNameDecoder decoder = {pstn,
        [](void *c, const void *wire, ThreeMMU32 bytes) -> int { return static_cast<PString>(c)->FSetData(const_cast<void *>(wire), bytes); },
        [](void *c) { static_cast<PString>(c)->SetNil(); },
        [](void *c) -> ThreeMMU32 { return static_cast<PString>(c)->Cch(); }};
    return three_mm_chunk_name_get(&names, icrp, &decoder) != 0;
}

/***************************************************************************
    Make a node a child of another node.  If fClearLoner is set, the loner
    flag of the child is cleared.
***************************************************************************/
bool ChunkyFile::FAdoptChild(ChunkTagOrType ctgPar, ChunkNumber cnoPar, ChunkTagOrType ctgChild, ChunkNumber cnoChild, ChildChunkID chid, bool fClearLoner)
{
    AssertThis(0);
    ChunkEditHost host = {this, nullptr, nullptr, nullptr, nullptr, nullptr};
    const auto edit = _EditServices(&host);
    const ThreeMMChunkChild child = {ctgChild, cnoChild, chid};
    const int result = three_mm_chunk_adopt(&edit, ctgPar, cnoPar, &child, fClearLoner, kckidMax);
    if (result == THREE_MM_CHUNK_NOT_FOUND) Bug("parent or child not there");
    AssertThis(0);
    return result == THREE_MM_CHUNK_OK;
}

/***************************************************************************
    Changes the chid value of the given child link.  Assert that
    (ctgChild, cnoChild, chidOld) is a child of (ctgPar, cnoPar) and that
    (ctgChild, cnoChild, chidNew) is not currently a child.
***************************************************************************/
void ChunkyFile::ChangeChid(ChunkTagOrType ctgPar, ChunkNumber cnoPar, ChunkTagOrType ctgChild, ChunkNumber cnoChild, ChildChunkID chidOld, ChildChunkID chidNew)
{
    AssertThis(0);
    ChunkEditHost host = {this, nullptr, nullptr, nullptr, nullptr, nullptr};
    const auto edit = _EditServices(&host);
    const ThreeMMChunkChild child = {ctgChild, cnoChild, chidOld};
    if (three_mm_chunk_change_slot(&edit, ctgPar, cnoPar, &child, chidNew) != THREE_MM_CHUNK_OK)
    { Bug("src not a child or dest already a child"); return; }
    AssertThis(0);
}

/***************************************************************************
    Return the total number of chunks.
***************************************************************************/
LegacyLong ChunkyFile::Ccki(void)
{
    AssertThis(0);
    return _Index()->count;
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

    if ((icrpMac = _Index()->count) == 0)
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

    qcrp = (ChunkRepresentation *)ChunkFixed(_Index(), icrp);
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

ThreeMMChunkEndpoint ChunkyFile::_OpsServices(void *context)
{
    auto host = static_cast<ChunkOpsHost *>(context);
    ThreeMMChunkEndpoint endpoint = {};
    endpoint.edit = _EditServices(&host->edit);
    endpoint.graph = ChunkGraphServices(this);
    endpoint.allocator = KauaiCopyAllocator(); endpoint.identity = this; endpoint.context = context;
    endpoint.max_references = kckidMax;
#ifdef DEBUG
    endpoint.scan_all_references = 1;
#endif
    endpoint.runtime = [](void *c, ThreeMMU32 type, ThreeMMU32 number) -> ThreeMMI32 {
        return static_cast<ChunkOpsHost *>(c)->edit.owner->_Rti(type, number);
    };
    endpoint.set_runtime = [](void *c, ThreeMMU32 type, ThreeMMU32 number, ThreeMMI32 runtime) -> int {
        return static_cast<ChunkOpsHost *>(c)->edit.owner->_FSetRti(type, number, runtime);
    };
    endpoint.read_payload = [](void *c, ThreeMMU32 position) {
        auto h = static_cast<ChunkOpsHost *>(c); h->edit.owner->_GetBlck(position, &h->payload);
    };
    endpoint.put_payload = [](void *c, ThreeMMU32 type, ThreeMMU32 number, void *source) -> int {
        return static_cast<ChunkOpsHost *>(c)->edit.owner->FPutBlck(&static_cast<ChunkOpsHost *>(source)->payload, type, number);
    };
    endpoint.add_payload = [](void *c, ThreeMMU32 type, void *source, ThreeMMU32 *number) -> int {
        ChunkNumber result;
        const bool ok = static_cast<ChunkOpsHost *>(c)->edit.owner->FAddBlck(&static_cast<ChunkOpsHost *>(source)->payload, type, &result);
        if (ok) *number = result;
        return ok;
    };
    endpoint.read_name = [](void *c, ThreeMMU32 position) -> int {
        auto h = static_cast<ChunkOpsHost *>(c); return h->edit.owner->_FGetName(position, &h->name);
    };
    endpoint.write_name = [](void *c, ThreeMMU32 position, void *source) -> int {
        return static_cast<ChunkOpsHost *>(c)->edit.owner->_FSetName(position, &static_cast<ChunkOpsHost *>(source)->name);
    };
    return endpoint;
}
bool ChunkyFile::_FCopy(ChunkTagOrType ctgSrc, ChunkNumber cnoSrc, PChunkyFile pcflDst, ChunkNumber *pcnoDst, bool fClone)
{
    AssertThis(fcflFull); AssertPo(pcflDst, fcflFull); AssertVarMem(pcnoDst);
    ChunkOpsHost source(this), destination(pcflDst);
    const auto src = _OpsServices(&source); const auto dst = pcflDst->_OpsServices(&destination);
    ThreeMMU32 copied;
    const int result = three_mm_chunk_copy(&src, &dst, ctgSrc, cnoSrc, fClone, &copied);
    if (result == THREE_MM_CHUNK_OK) *pcnoDst = copied;
    else { TrashVar(pcnoDst); if (result == THREE_MM_CHUNK_NOT_FOUND) Bug("chunk not found"); }
    AssertThis(fcflFull); AssertPo(pcflDst, fcflFull);
    return result == THREE_MM_CHUNK_OK;
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
    LegacyLong position;
    if (!_FFindCtgCno(ctg, cno, &position)) return rtiNil;
    return static_cast<ChunkRepresentation *>(ChunkFixed(_Index(), position))->rti;
#else
    return three_mm_chunk_runtime_get(&_owner.runtime, ctg, cno);
#endif
}
bool ChunkyFile::_FSetRti(ChunkTagOrType ctg, ChunkNumber cno, LegacyLong rti)
{
    AssertBaseThis(0);
#ifdef CHUNK_BIG_INDEX
    LegacyLong position;
    if (!_FFindCtgCno(ctg, cno, &position)) return fFalse;
    static_cast<ChunkRepresentation *>(ChunkFixed(_Index(), position))->rti = rti;
    return fTrue;
#else
    return three_mm_chunk_runtime_set(&_owner.runtime, ctg, cno, rti) == THREE_MM_CHUNK_OK;
#endif
}

/***************************************************************************
    Constructor for chunk graph enumerator.
***************************************************************************/
ChunkGraphEnumerator::ChunkGraphEnumerator(void)
{
    AssertThisMem();
    _pcfl = nullptr;
    AssertDo(three_mm_chunk_walk_init(&_walk, KauaiCopyAllocator()) == THREE_MM_CHUNK_OK, "invalid graph allocator");
    AssertThis(0);
}
ChunkGraphEnumerator::~ChunkGraphEnumerator(void)
{
    AssertThis(0);
    AssertDo(three_mm_chunk_walk_dispose(&_walk) == THREE_MM_CHUNK_OK, "invalid graph disposal");
}
#ifdef DEBUG
void ChunkGraphEnumerator::AssertValid(LegacyUlong grf)
{
    ChunkGraphEnumerator_PAR::AssertValid(0);
    AssertIn(_walk.phase, THREE_MM_WALK_START, THREE_MM_WALK_DONE + 1);
    if (_walk.phase != THREE_MM_WALK_DONE) AssertPo(_pcfl, 0);
    else Assert(!_walk.depth && !_walk.created && !_walk.stack.buffers[0].data, "graph stack retained after completion");
    Assert(_walk.depth <= _walk.stack.buffers[0].capacity / sizeof(ThreeMMChunkWalkFrame), "bad graph depth");
}
void ChunkGraphEnumerator::MarkMem(void)
{
    AssertThis(0);
    ChunkGraphEnumerator_PAR::MarkMem();
    MarkPv(_walk.stack.buffers[0].data);
}
#endif
void ChunkGraphEnumerator::Init(PChunkyFile pcfl, ChunkTagOrType ctg, ChunkNumber cno)
{
    AssertThis(0); AssertPo(pcfl, 0);
    const auto graph = ChunkGraphServices(pcfl);
    AssertDo(three_mm_chunk_walk_reset(&_walk, &graph, ctg, cno) == THREE_MM_CHUNK_OK, "invalid graph reset");
    _pcfl = pcfl;
    AssertThis(0);
}
bool ChunkGraphEnumerator::FNextKid(ChildChunkIdentification *pkid, ChunkIdentification *pckiPar, LegacyUlong *pgrfcgeOut, LegacyUlong grfcgeIn)
{
    AssertThis(0); AssertVarMem(pkid); AssertNilOrVarMem(pckiPar); AssertVarMem(pgrfcgeOut);
    static_assert(fcgePre == THREE_MM_WALK_PRE && fcgePost == THREE_MM_WALK_POST &&
        fcgeRoot == THREE_MM_WALK_ROOT && fcgeError == THREE_MM_WALK_ERROR, "graph event flags drift");
    ThreeMMChunkChild node, parent; ThreeMMU32 flags;
    if (!three_mm_chunk_walk_next(&_walk, (grfcgeIn & fcgeSkipToSib) != 0, &node, &parent, &flags))
    {
        TrashVar(pkid); TrashVar(pckiPar); TrashVar(pgrfcgeOut); return fFalse;
    }
    *pkid = {{node.type, node.number}, node.slot}; *pgrfcgeOut = flags;
    if (pckiPar && !(flags & THREE_MM_WALK_ROOT)) *pckiPar = {parent.type, parent.number};
    TrashVarIf(flags & THREE_MM_WALK_ROOT, pckiPar);
    TrashVarIf(flags & THREE_MM_WALK_ROOT, &pkid->chid);
    return fTrue;
}

} // end of namespace Chunky
