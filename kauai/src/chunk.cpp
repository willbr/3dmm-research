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
#include "mm/chunk_save.h"
#include "mm/chunk_heap.h"
#include "mm/chunk_edit.h"
#include "mm/chunk_ops.h"
#include "mm/chunk_copy.h"
#include "mm/chunk_forest.h"
#include "mm/movie_title.h"
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
using ChunkRepresentation = MMChunkRecordBig;
constexpr int expandedRecords = 1;
constexpr LegacyLong kcbMaxCrp = klwMax, kckidMax = kcbMax;
#else
using ChunkRepresentation = MMChunkRecordSmall;
constexpr int expandedRecords = 0;
constexpr LegacyLong kcbMaxCrp = 0x00ffffff, kckidMax = ksuMax;
#endif
using PChunkTable = MMChunkTable *;
namespace {
PChunkTable NewChunkTable(LegacyLong width, LegacyLong reserve = 0)
{
    PChunkTable table = nullptr; int trash = 0; Debug(trash = 1;)
    const auto allocator = CollectionAllocator();
    mm_chunk_table_create(&allocator, width, reserve, trash, &table); return table;
}
void CloseChunkTable(PChunkTable table)
{ AssertDo(mm_chunk_table_close(table) == MM_STORAGE_OK, "invalid chunk table disposal"); }
void *ChunkFixed(PChunkTable table, LegacyLong i, LegacyLong *bytes = nullptr)
{
    MMU32 count = 0; auto p = mm_chunk_table_fixed(table, i, &count);
    Assert(p != nullptr, "invalid chunk table position"); if (bytes) *bytes = count; return p;
}
void ChunkReadBytes(PChunkTable table, LegacyLong i, LegacyLong offset, LegacyLong bytes, void *out)
{ AssertDo(mm_chunk_table_read(table, i, offset, bytes, out) == MM_STORAGE_OK, "invalid chunk table read"); }
void ChunkReadFixed(PChunkTable table, LegacyLong i, void *out)
{ std::memcpy(out, ChunkFixed(table, i), table->fixed_bytes); }
}

namespace
{
int ReadRuntimeChunk(void *context, MMU32 position, MMChunkEntry *entry)
{
    return mm_chunk_table_entry(static_cast<PChunkTable>(context), position, entry);
}
int CopyFileExtent(void *, void *source, MMU32 sourceOffset, void *destination, MMU32 offset, MMU32 bytes)
{
    FileLocation from = {static_cast<PFileObject>(source), static_cast<FilePosition>(sourceOffset), static_cast<LegacyLong>(bytes)};
    FileLocation to = {static_cast<PFileObject>(destination), static_cast<FilePosition>(offset), static_cast<LegacyLong>(bytes)};
    return from.FCopy(&to);
}
}
MMChunkAccess ChunkyFile::_ChunkAccess()
{
    return mm_chunk_table_access(_Index());
}

#define _BvKid(ikid) LwMul(ikid, size(ChildChunkIdentification))

MMChunkContext ChunkyFile::_context;

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
    AssertDo(mm_chunk_owner_init(&_owner, KauaiCopyAllocator(), expanded) == MM_CHUNK_OK, "invalid container owner");
    MMChunkOwnerFileIO io = {
        this,
        [](void *) -> void * { return FileObject::PfilCreateTemp(); },
        [](void *file) { return static_cast<PFileObject>(file)->ReadStream(); },
        [](void *file) -> MMU32 { return static_cast<PFileObject>(file)->FpMac(); },
        [](void *file, MMU32 bytes) -> int { return static_cast<PFileObject>(file)->FSetFpMac(bytes); },
        CopyFileExtent,
        [](void *file, MMU32 offset, const void *bytes, size_t count) -> int {
            return static_cast<PFileObject>(file)->FWriteRgb(const_cast<void *>(bytes), count, offset);
        },
        [](void *file) { static_cast<PFileObject>(file)->Release(); },
        [](void *file) -> int { return static_cast<PFileObject>(file)->FTemp(); },
        [](void *file, int temporary) { static_cast<PFileObject>(file)->SetTemp(temporary != 0); },
        nullptr,
        [](void *, int kind) {
            if (kind == MM_EDIT_LOW_MEMORY)
                Warn("memory failure in ChunkyFile::Delete - adjusting ref counts");
            else if (kind == MM_EDIT_CYCLE_RISK)
                Warn("Performing this adoption may cause a cycle");
            else if (kind == MM_EDIT_INDEX_OPERATION)
                Bug("invalid chunk table mutation");
            else if (kind == MM_CHUNK_OWNER_FREE_RANGE)
                Bug("bad free range");
            else if (kind == MM_CHUNK_OWNER_MAP_PRESENT)
                Bug("free map already exists");
            else if (kind == MM_CHUNK_OWNER_EXTENT_LIMIT)
                Bug("Requested FileLocation too big");
            else if (kind == MM_CHUNK_OWNER_MAP_UNREAD)
                Assert(false, "free map not read yet");
            else
                Bug("invalid chunk deletion reference");
        }};
#ifdef CHUNK_STATS
    io.trace = [](void *context, int kind, const MMChunkEntry *entry) {
        if (!vfDumpChunkRequests)
            return;
        auto self = static_cast<PChunkyFile>(context);
        String text;
        if (kind == MM_STORAGE_CACHE)
            text.FFormatSz(PszLit("Cache: '%f', 0x%08x, fp = 0x%08x, cb = 0x%08x"), entry->type, entry->number, entry->offset, entry->bytes);
        else if (kind == MM_STORAGE_FETCH_EXTRA)
            text.FFormatSz(PszLit("Fetch from extra: '%f', 0x%08x, fp = 0x%08x, cb = 0x%08x"), entry->type, entry->number, entry->offset, entry->bytes);
        else
            text.FFormatSz(PszLit("Fetch from %z: '%f', 0x%08x, fp = 0x%08x, cb = 0x%08x"),
                entry->flags & fcrpOnExtra ? PszLit("extra") : PszLit("main"), entry->type, entry->number, entry->offset, entry->bytes);
        DumpStn(&text, self->_MainFile());
    };
#endif
    mm_chunk_owner_bind_storage(&_storage, &_owner, KauaiCopyAllocator(), kcbMaxCrp, &io);
    // add it to the linked list
    mm_chunk_link_init(&_link, this);
    AssertDo(mm_chunk_context_attach(&_context, &_link) == MM_CHUNK_OK, "invalid container registry attachment");
    AssertBaseThis(0);
}

/***************************************************************************
    Close the associated files.  Private.
***************************************************************************/
ChunkyFile::~ChunkyFile(void)
{
    AssertBaseThis(0);
    const MMChunkDisposal services = {this,
        [](void *, void *file) { static_cast<PFileObject>(file)->Release(); },
        [](void *, void *index) { CloseChunkTable(static_cast<PChunkTable>(index)); },
        [](void *c, const MMChunkReopenState *state) { static_cast<PChunkyFile>(c)->_owner.files = *state; }};
    AssertDo(mm_chunk_owner_dispose(&_owner, &services) == MM_CHUNK_OK, "invalid container disposal");
    AssertDo(mm_chunk_context_detach(&_link) == MM_CHUNK_OK, "invalid container registry detachment");
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
    auto owner = static_cast<PChunkyFile>(mm_chunk_construct(&services, grfcfl, 0));
    return owner;
}

/***************************************************************************
    If the file is read-only, toss the index and extra file and re-read
    the index.
***************************************************************************/
bool ChunkyFile::FReopen(void)
{
    AssertThis(0);
    const MMChunkReopenServices services = {this,
        [](void *, void *file) -> int { return (static_cast<PFileObject>(file)->GrffilCur() & ffilWriteEnable) != 0; },
        [](void *, void *file) { static_cast<PFileObject>(file)->AddRef(); },
        [](void *, void *file) { static_cast<PFileObject>(file)->Release(); },
        [](void *, void *index) { CloseChunkTable(static_cast<PChunkTable>(index)); },
        [](void *c, MMChunkReopenState *state) -> int {
            auto self = static_cast<PChunkyFile>(c); const bool ok = self->_FReadIndex(); *state = self->_owner.files; return ok;
        },
        [](void *c) -> int { return static_cast<PChunkyFile>(c)->_TValidIndex() == tYes; },
        [](void *c, const MMChunkReopenState *state) { static_cast<PChunkyFile>(c)->_owner.files = *state; }};
    const bool ok = mm_chunk_owner_reopen(&_owner, &services) == MM_CHUNK_OK;
    AssertThis(0); return ok;
}
/***************************************************************************
    Native object, file and collection primitives for the shared C
    construction policy.
***************************************************************************/
MMChunkConstruction ChunkyFile::_ConstructionServices(Filename *name)
{
    static_assert(ffilWriteEnable == 1 && ffilDenyWrite == 2 && ffilDenyRead == 4 && ffilTemp == 16, "file flag encoding");
    return {name,
        [](void *c) -> void * { return PcflFromFni(static_cast<Filename *>(c)); },
        [](void *) -> void * { return NewObj ChunkyFile(); },
        [](void *, void *owner) { static_cast<PChunkyFile>(owner)->AddRef(); },
        [](void *, void *owner) { static_cast<PChunkyFile>(owner)->Release(); },
        [](void *c, int create, MMU32 flags) -> void * {
            return create ? FileObject::PfilCreate(static_cast<Filename *>(c), flags) : FileObject::PfilOpen(static_cast<Filename *>(c), flags);
        },
        [](void *) -> void * { return NewChunkTable(sizeof(ChunkRepresentation)); },
        [](void *, void *owner, void *file) { static_cast<PChunkyFile>(owner)->_owner.files.main_file = static_cast<PFileObject>(file); },
        [](void *, void *owner, void *index) { static_cast<PChunkyFile>(owner)->_owner.files.index = static_cast<PChunkTable>(index); },
        [](void *, void *owner, MMU32 end) { static_cast<PChunkyFile>(owner)->_owner.files.main_end = end; },
        [](void *, void *file, MMU32 end) -> int { return static_cast<PFileObject>(file)->FSetFpMac(end); },
        [](void *, void *owner) -> int { return static_cast<PChunkyFile>(owner)->_FReadIndex(); },
        [](void *, void *owner) -> int { return static_cast<PChunkyFile>(owner)->_TValidIndex() == tYes; },
        [](void *, void *owner, MMU32 flags) -> int { return static_cast<PChunkyFile>(owner)->FSetGrfcfl(flags); },
        [](void *, int issue) {
            if (issue == MM_CONTAINER_ALREADY_OPEN) Bug("trying to create an open file");
            else if (issue == MM_CONTAINER_INITIAL_OPTIONS) Bug("initial container options failed");
            else PushErc(issue == MM_CONTAINER_OPEN_FAILED ? ercCflOpen : ercCflCreate);
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
    auto owner = static_cast<PChunkyFile>(mm_chunk_construct(&services, grfcfl, 1));
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
    return _PcflFromFile(FileObject::PfilFromFni(pfni));
}

PChunkyFile ChunkyFile::_PcflFromFile(PFileObject file)
{
    const MMChunkLookup services = {
        [](void *c) -> void * { return static_cast<PChunkyFile>(c)->PcflNext(); },
        [](void *c) -> void * { return static_cast<PChunkyFile>(c)->_MainFile(); },
        [](void *c) -> void * { return static_cast<PChunkyFile>(c)->_ExtraFile(); },
        [](void *c) -> int { return static_cast<PChunkyFile>(c)->_owner.files.invalid_main; },
        [](void *) { Bug("who is using this Filename?"); }};
    return static_cast<PChunkyFile>(mm_chunk_find_container(PcflFirst(), file, &services));
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
MMChunkGraph ChunkGraphServices(PChunkyFile);
struct ForestWriteHost { PChunkyFile source; PFileObject destination; };
}
bool ChunkyFile::FWriteChunkTree(ChunkTagOrType ctg, ChunkNumber cno, PFileObject pfilDst, FilePosition fpDst, LegacyLong *pcb)
{
    AssertThis(0); AssertNilOrPo(pfilDst, 0); AssertNilOrVarMem(pcb);
    ForestWriteHost host = {this, pfilDst};
    const short one = 1;
    MMForestExport output = {};
    output.context = &host; output.graph = ChunkGraphServices(this);
    output.big_endian = reinterpret_cast<const byte *>(&one)[0] == 0; output.system = koskCur;
    output.entry = [](void *c, MMU32 type, MMU32 number, MMChunkEntry *entry) -> int {
        auto source = static_cast<ForestWriteHost *>(c)->source; LegacyLong position;
        return source->_FFindCtgCno(type, number, &position) && ReadRuntimeChunk(source->_Index(), position, entry);
    };
    if (pfilDst)
    {
        output.write = [](void *c, MMU32 offset, const void *bytes, size_t count) -> int {
            return static_cast<ForestWriteHost *>(c)->destination->FWriteRgb(const_cast<void *>(bytes), count, offset);
        };
        output.copy = [](void *c, MMU32 type, MMU32 number, MMU32 offset, MMU32 bytes) -> int {
            auto h = static_cast<ForestWriteHost *>(c); FileLocation source;
            if (!h->source->FFindFlo(type, number, &source)) return 0;
            FileLocation destination = {h->destination, static_cast<FilePosition>(offset), static_cast<LegacyLong>(bytes)};
            return source.FCopy(&destination);
        };
    }
    MMU32 bytes;
    if (mm_forest_write(&output, KauaiCopyAllocator(), ctg, cno, fpDst, &bytes) != MM_CHUNK_OK)
    { TrashVar(pcb); return false; }
    if (pcb) *pcb = bytes;
    return true;
}

PChunkyFile ChunkyFile::PcflReadForestFromFlo(PFileLocation pflo, bool fCopyData)
{
    AssertVarMem(pflo);
    AssertPo(pflo->pfil, 0);
    const MMForestConstruction construction = {
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
        [](void *, void *file, MMU32 end) -> int { return static_cast<PFileObject>(file)->FSetFpMac(end); },
        [](void *, void *owner, MMU32 end, int borrowed) {
            auto file = static_cast<PChunkyFile>(owner);
            file->_owner.files.main_end = end;
            file->_owner.files.invalid_main = borrowed != 0;
            file->_owner.add_to_extra = borrowed != 0;
        },
        [](void *, void *owner, void *source, MMU32 offset, MMU32 bytes, int copy) -> int {
            struct ImportHost
            {
                PChunkyFile destination;
                PFileObject source;
            } host = {static_cast<PChunkyFile>(owner), static_cast<PFileObject>(source)};
            const MMForestImport services = {
                &host,
                [](void *c, MMU32 type, MMU32 offset, MMU32 bytes, MMU32 flags, int copy,
                   MMU32 *number) -> int {
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
                        mm_chunk_record_set_bytes(row, expandedRecords, bytes);
                        mm_chunk_record_set_flags(row, expandedRecords, flags, fcrpPacked | fcrpForest);
                    }
                    *number = created;
                    return 1;
                },
                [](void *c, MMU32 parentType, MMU32 parentNumber, MMU32 type, MMU32 number,
                   MMU32 slot) -> int {
                    return static_cast<ImportHost *>(c)->destination->FAdoptChild(parentType, parentNumber, type,
                                                                                  number, slot);
                }};
            const auto stream = static_cast<PFileObject>(source)->ReadStream();
            return mm_forest_read(&stream, offset, bytes, &services, KauaiCopyAllocator(), copy) ==
                   MM_CHUNK_OK;
        },
        [](void *, void *owner) { AssertPo(static_cast<PChunkyFile>(owner), fcflFull | fcflGraph); }};
    return static_cast<PChunkyFile>(
        mm_chunk_construct_forest(&construction, pflo->pfil, pflo->fp, pflo->cb, fCopyData));
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
    return FPure(mm_chunk_record_flags(qcrp, expandedRecords, fcrpForest));
}

/***************************************************************************
    Set or clear the "forest" flag.
***************************************************************************/
void ChunkyFile::SetForest(ChunkTagOrType ctg, ChunkNumber cno, bool fForest)
{
    AssertThis(0);
    _SetChunkFlag(ctg, cno, MM_CHUNK_FOREST, fForest);
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
MMChunkRegistry ChunkyFile::_RegistryServices()
{
    return {
        [](void *c) -> void * { auto self = static_cast<PChunkyFile>(c); AssertPo(self, 0); return self->PcflNext(); },
        [](void *c) -> int { return static_cast<PChunkyFile>(c)->_owner.marked; },
        [](void *c) -> MMI32 { return static_cast<PChunkyFile>(c)->_references.count; },
        [](void *c) { auto self = static_cast<PChunkyFile>(c); AssertPo(self, 0); self->_owner.marked = false; },
        [](void *c) { delete static_cast<PChunkyFile>(c); }};
}
void ChunkyFile::ClearMarks(void)
{
    const auto registry = _RegistryServices();
    AssertDo(mm_chunk_clear_marks(PcflFirst(), &registry) == MM_CHUNK_OK, "invalid container registry");
}
/***************************************************************************
    Static method: close any chunky files that are unmarked and have 0
    open count.
***************************************************************************/
void ChunkyFile::CloseUnmarked(void)
{
    const auto registry = _RegistryServices();
    AssertDo(mm_chunk_close_unmarked(PcflFirst(), &registry) == MM_CHUNK_OK, "invalid container registry" );
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
    static_assert(fcflWriteEnable == MM_CHUNK_WRITE && fcflTemp == MM_CHUNK_TEMP &&
        fcflMark == MM_CHUNK_MARK && fcflAddToExtra == MM_CHUNK_ADD_EXTRA &&
        fcflReadFromExtra == MM_CHUNK_READ_EXTRA, "container flag encoding");
    const MMChunkOptionServices services = {this,
        [](void *c) -> MMU32 { return static_cast<PChunkyFile>(c)->_MainFile()->GrffilCur(); },
        [](void *c, MMU32 flags, MMU32 mask) -> int { return static_cast<PChunkyFile>(c)->_MainFile()->FSetGrffil(flags, mask); },
        [](void *c) { static_cast<PChunkyFile>(c)->_ReadFreeMap(); }};
    return mm_chunk_owner_options(&_owner, &services, grfcfl, grfcflMask) == MM_CHUNK_OK;
}
/***************************************************************************
    Return the level of error that we've encountered for this chunky file.
***************************************************************************/
namespace {
const MMChunkErrorServices ChunkErrors = {
    [](void *file) -> MMI32 { return static_cast<PFileObject>(file)->ElError(); },
    [](void *file, MMI32 level) { static_cast<PFileObject>(file)->SetEl(level); }};
}
LegacyLong ChunkyFile::ElError(void)
{
    AssertThis(0);
    return mm_chunk_owner_error(&_owner, &ChunkErrors);
}

/***************************************************************************
    Make sure the el level on both files is at or below el.
***************************************************************************/
void ChunkyFile::ResetEl(LegacyLong el)
{
    AssertThis(0);
    AssertIn(el, elNil, kcbMax);

    mm_chunk_owner_reset_error(&_owner, &ChunkErrors, el);
}

/***************************************************************************
    Decrement the open count.  If it is zero and the chunky file isn't
    marked, the file is closed.
***************************************************************************/
void ChunkyFile::Release(void)
{
    AssertBaseThis(0);
    MMI32 remaining;
    const int decision = mm_chunk_release(_references.count, _owner.marked, &remaining);
    if (decision < 0) { Bug("calling Release without an AddRef"); return; }
    _references.count = remaining;
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
    Assert(mm_chunk_context_valid(&_link), "invalid container registry links");
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
    Assert(_owner.runtime.count <= _owner.runtime.storage.buffers[0].capacity / sizeof(MMChunkRuntimeEntry), "bad runtime cache count");
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
        cbVar = mm_chunk_table_variable_bytes(_Index(), icrp);
        cbRgch = mm_chunk_record_name_bytes(&crp, expandedRecords, cbVar);

        AssertIn(crp.ckid, 0, kckidMax + 1);
        AssertIn(cbRgch, 0, kcbMaxDataStn + 1);

        // we use this in checking the graph structure
        Assert(!mm_chunk_record_flags(&crp, expandedRecords, fcrpMarkT), "fcrpMarkT set");

        ckiNew = {crp.cki.ctg, crp.cki.cno};
        ccrpRefTot += crp.ccrpRef;
        ckidTot += crp.ckid;

#ifdef CHUNK_BIG_INDEX
        Assert(crp.rti >= 0 && crp.rti <= mm_chunk_runtime_highest(), "bad runtime ID");
#endif // CHUNK_BIG_INDEX

        // assert that the file storage (fp,cb) is OK
        if (mm_chunk_record_flags(&crp, expandedRecords, fcrpOnExtra))
        {
            Assert(_ExtraFile() != pvNil, "fcrpOnExtra wrong");
            AssertIn(crp.fp, 0, _owner.files.extra_end);
            AssertIn(mm_chunk_record_bytes(&crp, expandedRecords), 1, _owner.files.extra_end - crp.fp + 1);
            cbTotExtra += mm_chunk_record_bytes(&crp, expandedRecords);
        }
        else
        {
            AssertVar(mm_chunk_record_bytes(&crp, expandedRecords) > 0 && (crp.fp >= fpBase) || mm_chunk_record_bytes(&crp, expandedRecords) == 0 && crp.fp == 0, "bad fp", &crp.fp);
            AssertVar(crp.fp <= _owner.files.main_end, "bad fp", &crp.fp);
            AssertIn(mm_chunk_record_bytes(&crp, expandedRecords), 0, _owner.files.main_end - crp.fp + 1);
            cbTot += mm_chunk_record_bytes(&crp, expandedRecords);
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
        MMChunkFreeRange fsm;
        FilePosition fpOld;

        fpOld = _owner.files.main_end;
        for (ifsm = _owner.files.main_map->count; ifsm-- != 0;)
        {
            AssertDo(mm_free_map_get(_owner.files.main_map, ifsm, &fsm) == MM_CHUNK_OK, "bad free range");
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
        MMChunkFreeRange fsm;
        FilePosition fpOld;

        fpOld = _owner.files.extra_end;
        for (ifsm = _owner.files.extra_map->count; ifsm-- != 0;)
        {
            AssertDo(mm_free_map_get(_owner.files.extra_map, ifsm, &fsm) == MM_CHUNK_OK, "bad extra free range");
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
    MMChunkRuntimeEntry cached, previous;
    for (MMU32 i = 0; i < _owner.runtime.count; ++i)
    {
        AssertDo(mm_chunk_runtime_at(&_owner.runtime, i, &cached) == MM_CHUNK_OK, "bad runtime cache entry");
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
    AssertDo(mm_chunk_table_clear_flags(_Index(), fcrpMarkT) == MM_STORAGE_OK, "invalid chunk flag clear");
    auto access = _ChunkAccess();
    SuspendAssertValid();
    SuspendCheckPointers();
    const int result = mm_chunk_validate_graph(&access, kckidMax);
    ResumeAssertValid();
    ResumeCheckPointers();
    return result == MM_CHUNK_OK ? tYes : result == MM_CHUNK_MEMORY ? tMaybe : tNo;
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
    static_assert(kcvnCur == MM_CHUNK_CURRENT_VERSION && kcvnBack == MM_CHUNK_BACKWARD_VERSION &&
                  kcvnMin == 1 && sizeof(ChunkyFilePrefix) == MM_CHUNK_PREFIX_BYTES &&
                  sizeof(ChildChunkIdentification) == sizeof(MMChunkChild), "Chunk C89 wire contract");
    const auto allocator = CollectionAllocator();
    int trash = 0;
    Debug(trash = 1;)
    return mm_chunk_owner_read_index(&_storage, &allocator, trash, kcbMaxDataStn,
                                     KauaiEncodeLegacyChunkName, nullptr) == MM_CHUNK_OK;
}

int KauaiEncodeLegacyChunkName(void *, const char *text, void *wire, MMU32 capacity, MMU32 *bytes)
{
    String converted;
    converted.SetSzs(const_cast<char *>(text));
    const auto size = static_cast<MMU32>(converted.CbData());
    if (size > capacity)
        return 0;
    converted.GetData(wire);
    *bytes = size;
    return 1;
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

    mm_chunk_owner_read_free_map(&_storage);
}

/***************************************************************************
    If we have don't have write permission or there's an extra file,
    write out a new file and do the rename stuff.  If not, just write
    the index and free map.
***************************************************************************/
namespace {
struct ChunkSavePath
{
    Filename *directory, *target;
};
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
    ChunkSavePath path = {&fni, pfni};
    const MMChunkOwnerSaveTarget target = {&path,
        [](void *context) -> void * { return FileObject::PfilCreateTemp(static_cast<ChunkSavePath *>(context)->directory); },
        [](void *context, void *file) -> int {
            auto name = static_cast<ChunkSavePath *>(context)->target;
            if (name->TExists() != tNo) name->FDelete();
            return static_cast<PFileObject>(file)->FRename(name);
        },
        [](void *, void *file, void *old) -> int {
            return static_cast<PFileObject>(file)->FSwapNames(static_cast<PFileObject>(old));
        }};
    const bool ok = mm_chunk_owner_save(&_storage, &target, pfni != nullptr, ctgCreator, koskCur) == MM_CHUNK_OK;
    if (!ok) PushErc(ercCflSave);
    AssertThis(fcflFull | fcflGraph);
    return ok;
}

/***************************************************************************
    Save a copy of the chunky file out to *pfni.  The ChunkyFile and its FileObject
    are untouched.
***************************************************************************/
bool ChunkyFile::FSaveACopy(ChunkTagOrType ctgCreator, Filename *pfni)
{
    AssertThis(fcflFull | fcflGraph);
    AssertPo(pfni, ffniFile);
    PChunkyFile destination = ChunkyFile::PcflCreate(pfni, fcflWriteEnable);
    if (!destination) { PushErc(ercCflSaveCopy); return fFalse; }
    int directoryFailed;
    const bool ok = mm_chunk_owner_save_copy(&_storage, &destination->_storage, ctgCreator, koskCur,
                                              &directoryFailed) == MM_CHUNK_OK;
    if (ok || directoryFailed) { AssertPo(destination, fcflFull | fcflGraph); }
    if (directoryFailed) PushErc(ercCflSave);
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
    return FPure(mm_chunk_record_flags(qcrp, expandedRecords, fcrpOnExtra));
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

    return mm_chunk_owner_cache(&_storage, icrp, nullptr) == MM_CHUNK_OK;
}
/***************************************************************************
    Get the FileLocation from the chunk.
***************************************************************************/
void ChunkyFile::_GetFlo(LegacyLong icrp, PFileLocation pflo)
{
    AssertThis(0);
    AssertVarMem(pflo);

    MMChunkLocation location;
    AssertDo(mm_chunk_owner_fetch(&_storage, icrp, &location) == MM_CHUNK_OK, "invalid chunk fetch");
    *pflo = {static_cast<PFileObject>(location.file), static_cast<FilePosition>(location.offset), static_cast<LegacyLong>(location.bytes)};
    AssertPo(pflo->pfil, 0);
}
/***************************************************************************
    Get the DataBlock from the chunk.
***************************************************************************/
bool ChunkyFile::_GetBlockLocation(LegacyLong position, PFileLocation location)
{
    _GetFlo(position, location);
    return mm_chunk_record_flags(ChunkFixed(_Index(), position), expandedRecords, fcrpPacked) != 0;
}

void ChunkyFile::_GetBlck(LegacyLong icrp, PDataBlock pblck)
{
    AssertThis(0);
    AssertPo(pblck, 0);

    FileLocation flo;
    const bool packed = _GetBlockLocation(icrp, &flo);
    pblck->Set(&flo, packed);
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

bool ChunkyFile::FFindBlockFile(ChunkTagOrType type, ChunkNumber number, MMBlockFile *file, int *packed)
{
    AssertThis(0);
    AssertVarMem(file);
    AssertVarMem(packed);
    LegacyLong position;
    if (!_FFindCtgCno(type, number, &position))
        return false;

    FileLocation location;
    *packed = _GetBlockLocation(position, &location);
    *file = {location.pfil, static_cast<MMU32>(location.fp), static_cast<MMU32>(location.cb)};
    return true;
}

const MMChunkPayloadServices *KauaiChunkPayloadServices()
{
    static const MMChunkPayloadServices services = {
        KauaiHandleServices(),
        KauaiBlockFileServices(),
        [](void *archive, MMU32 type, MMU32 number, MMBlockFile *file, int *packed) -> int {
            return static_cast<PChunkyFile>(archive)->FFindBlockFile(type, number, file, packed);
        },
        nullptr,
        KauaiDecodeBlockHandle,
    };
    return &services;
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
    _SetChunkFlag(ctg, cno, MM_CHUNK_PACKED, fPacked);
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
    return FPure(mm_chunk_record_flags(qcrp, expandedRecords, fcrpPacked));
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
    MMU32 position = 0;
    const int result = mm_chunk_find(&access, ctg, cno, &position);
    *picrp = static_cast<LegacyLong>(position);
    return result == MM_CHUNK_OK;
}

/***************************************************************************
    Find an unused cno for the given ctg.  Fill in *picrp and *pcno.
***************************************************************************/
namespace {
struct ChunkPayloadHost
{
    PDataBlock output, source, temporary;
};
}
MMChunkEdit ChunkyFile::EditIndex()
{
    int scan = 0;
    Debug(scan = 1;)
    return mm_chunk_owner_editor(&_storage, kckidMax, scan);
}
MMChunkEdit ChunkyFile::_PayloadEdit(void *context)
{
    auto edit = EditIndex();
    edit.payload_context = context;
    edit.output = [](void *c, const MMChunkLocation *location, int added) {
        auto host = static_cast<ChunkPayloadHost *>(c);
        auto block = host->output ? host->output : added ? host->temporary : nullptr;
        if (!block) return;
        if (!location) { block->Free(); return; }
        FileLocation native = {static_cast<PFileObject>(location->file), static_cast<FilePosition>(location->offset),
            static_cast<LegacyLong>(location->bytes)};
        block->Set(&native);
    };
    edit.write = [](void *c, const MMChunkLocation *location, int added) -> int {
        auto host = static_cast<ChunkPayloadHost *>(c);
        auto block = host->output ? host->output : host->temporary;
        FileLocation native = {static_cast<PFileObject>(location->file), static_cast<FilePosition>(location->offset),
            static_cast<LegacyLong>(location->bytes)};
        if (host->source) return added ? host->source->FWriteToBlck(block, fTrue) : host->source->FWriteToFlo(&native, fTrue);
        return 1;
    };
    return edit;
}
void ChunkyFile::_SetChunkFlag(ChunkTagOrType type, ChunkNumber number, MMU32 flag, bool enabled)
{
    const auto edit = EditIndex();
    if (mm_chunk_set_flag(&edit, type, number, flag, enabled) != MM_CHUNK_OK) Bug("chunk not there");
}
void ChunkyFile::_GetUniqueCno(ChunkTagOrType ctg, LegacyLong *picrp, ChunkNumber *pcno)
{
    AssertBaseThis(0);
    AssertVarMem(picrp); AssertVarMem(pcno);
    const auto edit = EditIndex();
    MMU32 position, number;
    AssertDo(mm_chunk_unique(&edit, ctg, &position, &number) == MM_CHUNK_OK, "invalid chunk index");
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
    const auto edit = EditIndex();
    const bool ok = mm_chunk_add_bytes(&edit, KauaiBlockFileServices(), ctg, pv, cb, pcno) == MM_CHUNK_OK;
    if (!ok)
        TrashVar(pcno);
    AssertThis(0);
    return ok;
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

    const auto edit = EditIndex();
    const bool ok = mm_chunk_add_block_copy(&edit, KauaiBlockFileServices(), KauaiCopyAllocator(), ctg,
                                            pblckSrc->CoreBlock(), pcno) == MM_CHUNK_OK;
    if (!ok)
        TrashVar(pcno);
    AssertThis(0);
    return ok;
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
    ChunkPayloadHost host = {pblck, nullptr, nullptr};
    const auto edit = _PayloadEdit(&host);
    const bool ok = mm_chunk_add(&edit, ctg, cno, icrp, cb, nullptr) == MM_CHUNK_OK;
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

    return _FPut(cb, ctg, cno, pblck, pvNil);
}

/***************************************************************************
    Replace or create a chunk with the given cno and put the data in it.
***************************************************************************/
bool ChunkyFile::FPutPv(void *pv, LegacyLong cb, ChunkTagOrType ctg, ChunkNumber cno)
{
    AssertThis(0);
    AssertIn(cb, 0, kcbMax);
    AssertPvCb(pv, cb);

    const auto edit = EditIndex();
    const bool ok = mm_chunk_put_bytes(&edit, KauaiBlockFileServices(), ctg, cno, pv, cb) == MM_CHUNK_OK;
    AssertThis(0);
    return ok;
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

    fRet = _FPut(blckSrc.Cb(), ctg, cno, pvNil, &blckSrc);
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

    const auto edit = EditIndex();
    const bool ok = mm_chunk_put_block(&edit, KauaiBlockFileServices(), KauaiCopyAllocator(), ctg, cno,
                                       pblckSrc->CoreBlock()) == MM_CHUNK_OK;
    AssertThis(0);
    return ok;
}

/***************************************************************************
    Low level put.  Writes data from an optional source block.  If the
    chunk doesn't already exist, this has the same affect as doing an add.
    If it does exist, this doesn't change the fcrpLoner flag or anything
    else except the data.  If pblck isn't nil, this sets it to the location
    of the new data.  Doesn't change the packed flag.
***************************************************************************/
bool ChunkyFile::_FPut(LegacyLong cb, ChunkTagOrType ctg, ChunkNumber cno, PDataBlock pblck, PDataBlock pblckSrc)
{
    AssertBaseThis(0); AssertIn(cb, 0, kcbMax); AssertNilOrPo(pblck, 0); AssertNilOrPo(pblckSrc, 0);
    DataBlock temporary;
    ChunkPayloadHost host = {pblck, pblckSrc, &temporary};
    const auto edit = _PayloadEdit(&host);
    const bool ok = mm_chunk_put(&edit, ctg, cno, cb) == MM_CHUNK_OK;
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
    const auto edit = EditIndex();
    if (mm_chunk_swap_data(&edit, ctg1, cno1, ctg2, cno2) != MM_CHUNK_OK) { Bug("can't find the chunks"); return; }
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
    const auto edit = EditIndex();
    if (mm_chunk_swap_children(&edit, ctg1, cno1, ctg2, cno2) != MM_CHUNK_OK) { Bug("invalid child swap"); return; }
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
    const auto edit = EditIndex();
    if (mm_chunk_move(&edit, ctg, cno, ctgNew, cnoNew) != MM_CHUNK_OK) { Bug("bad move request or references"); return; }
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
    const auto edit = EditIndex();
    const int result = mm_chunk_delete(&edit, ctg, cno);
    if (result != MM_CHUNK_OK) { Bug("chunk not deletable or missing"); return; }
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
    const auto edit = EditIndex();
    if (mm_chunk_set_flag(&edit, ctg, cno, MM_CHUNK_LONER, fLoner) != MM_CHUNK_OK) Bug("chunk not there");
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
    return FPure(mm_chunk_record_flags(qcrp, expandedRecords, fcrpLoner));
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
MMChunkGraph ChunkGraphServices(PChunkyFile file)
{
    return {file,
        [](void *context, MMU32 type, MMU32 number) -> MMU32 {
            return static_cast<PChunkyFile>(context)->Ckid(type, number);
        },
        [](void *context, MMU32 type, MMU32 number, MMU32 position, MMChunkChild *child) -> int {
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
    const int result = mm_chunk_reaches(&graph, KauaiCopyAllocator(), ctg, cno, ctgSub, cnoSub, CckiRef(ctgSub, cnoSub));
    return result == 0 ? tNo : result == 1 ? tYes : tMaybe;
}

/***************************************************************************
    Delete the given child chunk.  Handles deleting child chunks that are
    no longer referenced and don't have the fcrpLoner flag set.
***************************************************************************/
void ChunkyFile::DeleteChild(ChunkTagOrType ctgPar, ChunkNumber cnoPar, ChunkTagOrType ctgChild, ChunkNumber cnoChild, ChildChunkID chid)
{
    AssertThis(0);
    const auto edit = EditIndex();
    const MMChunkChild child = {ctgChild, cnoChild, chid};
    if (mm_chunk_unlink(&edit, ctgPar, cnoPar, &child) != MM_CHUNK_OK) { Bug("chunk or child link missing"); return; }
    AssertThis(fcflFull);
}

/***************************************************************************
    Decrements the reference count on the chunk.  Return true if the ref
    count becomes zero (after decrementing) and fcrpLoner is not set.
***************************************************************************/
bool ChunkyFile::_FDecRefCount(LegacyLong icrp)
{
    AssertBaseThis(0);
    const auto edit = EditIndex();
    int remove = 0;
    if (mm_chunk_decrement(&edit, icrp, &remove) != MM_CHUNK_OK) Bug("ref count wrong");
    return remove != 0;
}

/***************************************************************************
    Remove entry icrp from _Index() and add the file space to the free map.
***************************************************************************/
void ChunkyFile::_DeleteCore(LegacyLong icrp)
{
    AssertBaseThis(0);
    const auto edit = EditIndex();
    AssertDo(mm_chunk_remove(&edit, icrp) == MM_CHUNK_OK, "invalid chunk removal");
}

/***************************************************************************
    Set the name of the chunk.
***************************************************************************/
MMChunkNameValue ChunkyFile::EncodeName(PString name, void *wire)
{
    const auto count = name ? name->Cch() : 0;
    if (count)
        name->GetData(wire);
    return {wire, count ? static_cast<MMU32>(name->CbData()) : 0U, static_cast<MMU32>(count)};
}
bool ChunkyFile::FSetName(ChunkTagOrType ctg, ChunkNumber cno, PString pstn)
{
    AssertNilOrPo(pstn, 0);
    byte wire[kcbMaxDataStn];
    const auto value = EncodeName(pstn, wire);
    return FWriteNameBytes(ctg, cno, wire, value.bytes);
}

bool ChunkyFile::FWriteNameBytes(ChunkTagOrType ctg, ChunkNumber cno, const void *wire, MMU32 bytes)
{
    AssertThis(0);
    AssertNilOrPvCb(const_cast<void *>(wire), bytes);
    const auto edit = EditIndex();
    const auto names = NameIndex();
    const MMChunkNameValue value = {wire, bytes, bytes != 0};
    const int status = mm_chunk_name_set_key(&edit, &names, ctg, cno, &value);
    if (status == MM_CHUNK_NOT_FOUND)
        Bug("chunk not there");
    if (status != MM_CHUNK_OK)
        return fFalse;

    AssertThis(0);
    return fTrue;
}

/***************************************************************************
    Set the name of the chunk at the given index.
***************************************************************************/
MMChunkNames ChunkyFile::NameIndex()
{
    return mm_chunk_table_names(_Index(), kcbMaxDataStn);
}
MMArchiveWriter ChunkyFile::WriteIndex()
{
    const unsigned short order = 1;
    return {EditIndex(), NameIndex(), KauaiChunkPayloadServices(),
        *reinterpret_cast<const unsigned char *>(&order) == 0, koskCur,
        [](MMU32 *number) { (void)number; TrashVar(number); }, KauaiCopyAllocator()};
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
    const auto names = NameIndex();
    const MMChunkNameDecoder decoder = {pstn,
        [](void *c, const void *wire, MMU32 bytes) -> int { return static_cast<PString>(c)->FSetData(const_cast<void *>(wire), bytes); },
        [](void *c) { static_cast<PString>(c)->SetNil(); },
        [](void *c) -> MMU32 { return static_cast<PString>(c)->Cch(); }};
    return mm_chunk_name_get(&names, icrp, &decoder) != 0;
}

bool ChunkyFile::FReadNameBytes(ChunkTagOrType type, ChunkNumber number, void *wire, MMU32 capacity, MMU32 *written)
{
    LegacyLong position;
    if (!wire || !written || !_FFindCtgCno(type, number, &position))
        return false;
    struct Output { void *wire; MMU32 capacity, written; } output = {wire, capacity, 0};
    const MMChunkNameDecoder decoder = {&output,
        [](void *p, const void *data, MMU32 bytes) -> int {
            auto &o = *static_cast<Output *>(p);
            if (bytes > o.capacity)
                return 0;
            std::memcpy(o.wire, data, bytes);
            o.written = bytes;
            return 1;
        },
        [](void *p) { static_cast<Output *>(p)->written = 0; },
        [](void *p) -> MMU32 { return static_cast<Output *>(p)->written; }};
    const auto names = NameIndex();
    if (!mm_chunk_name_get(&names, position, &decoder))
        return false;
    *written = output.written;
    return true;
}

MMChunkTitleIO KauaiChunkTitleIO()
{
    return {nullptr, koskCur, MacWin(koskUniWin, koskUniMac),
        [](void *, const void *input, MMU32 bytes, int encoding, void *output, MMU32 capacity) -> MMI32 {
            return CchTranslateRgb(const_cast<void *>(input), bytes, static_cast<short>(encoding),
                                   static_cast<achar *>(output), capacity);
        },
        [](void *file, MMU32 type, MMU32 number, void *wire, MMU32 capacity, MMU32 *written) -> int {
            return static_cast<PChunkyFile>(file)->FReadNameBytes(type, number, wire, capacity, written);
        },
        [](void *file, MMU32 type, MMU32 number, const void *wire, MMU32 bytes) -> int {
            return static_cast<PChunkyFile>(file)->FWriteNameBytes(type, number, wire, bytes);
        },
        [](void *) { PushErc(ercStnRead); Warn("bad String data"); }};
}

/***************************************************************************
    Make a node a child of another node.  If fClearLoner is set, the loner
    flag of the child is cleared.
***************************************************************************/
bool ChunkyFile::FAdoptChild(ChunkTagOrType ctgPar, ChunkNumber cnoPar, ChunkTagOrType ctgChild, ChunkNumber cnoChild, ChildChunkID chid, bool fClearLoner)
{
    AssertThis(0);
    const auto edit = EditIndex();
    const MMChunkChild child = {ctgChild, cnoChild, chid};
    const int result = mm_chunk_adopt(&edit, ctgPar, cnoPar, &child, fClearLoner);
    if (result == MM_CHUNK_NOT_FOUND) Bug("parent or child not there");
    AssertThis(0);
    return result == MM_CHUNK_OK;
}

/***************************************************************************
    Changes the chid value of the given child link.  Assert that
    (ctgChild, cnoChild, chidOld) is a child of (ctgPar, cnoPar) and that
    (ctgChild, cnoChild, chidNew) is not currently a child.
***************************************************************************/
void ChunkyFile::ChangeChid(ChunkTagOrType ctgPar, ChunkNumber cnoPar, ChunkTagOrType ctgChild, ChunkNumber cnoChild, ChildChunkID chidOld, ChildChunkID chidNew)
{
    AssertThis(0);
    const auto edit = EditIndex();
    const MMChunkChild child = {ctgChild, cnoChild, chidOld};
    if (mm_chunk_change_slot(&edit, ctgPar, cnoPar, &child, chidNew) != MM_CHUNK_OK)
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
    MMChunkEntry entry;
    if (mm_chunk_at(&access, static_cast<MMU32>(icki), &entry) != MM_CHUNK_OK)
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
    MMChunkEntry entry;
    MMU32 position;
    if (mm_chunk_type_at(&access, ctg, static_cast<MMU32>(icki), &entry, &position) != MM_CHUNK_OK)
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
    MMChunkChild child;
    if (mm_chunk_child_at(&access, parent, static_cast<MMU32>(ikid), &child) != MM_CHUNK_OK)
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
    MMChunkChild child;
    if (mm_chunk_child_type(&access, parent, chid, ctg, &child) != MM_CHUNK_OK)
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
    MMU32 position = 0;
    const int result = mm_chunk_find_child(&access, icrpPar, chid, ctgChild, cnoChild, &position);
    *pikid = static_cast<LegacyLong>(position);
    return result == MM_CHUNK_OK;
}

MMChunkEndpoint ChunkyFile::CopyEndpoint(MMChunkFileCopy &binding)
{
    AssertThis(fcflFull);
    const auto archive = WriteIndex();
    const auto titles = KauaiChunkTitleIO();
    return mm_chunk_file_copy_endpoint(&binding, this, &archive, &titles, sizeof(achar), false);
}

bool ChunkyFile::_FCopy(ChunkTagOrType type, ChunkNumber number, PChunkyFile destination, ChunkNumber *out,
                       bool clone, int *created)
{
    AssertThis(fcflFull);
    AssertPo(destination, fcflFull);
    AssertVarMem(out);
    MMChunkFileCopy sourceBinding, destinationBinding;
    const auto source = CopyEndpoint(sourceBinding);
    const auto dst = destination->CopyEndpoint(destinationBinding);
    MMU32 copied;
    const int result = created ? mm_chunk_copy_reference(&source, &dst, type, number, &copied, created)
                               : mm_chunk_copy(&source, &dst, type, number, clone, &copied);
    if (result == MM_CHUNK_OK) *out = copied;
    else { TrashVar(out); if (result == MM_CHUNK_NOT_FOUND) Bug("chunk not found"); }
    AssertPo(destination, fcflFull);
    AssertThis(fcflFull);
    return result == MM_CHUNK_OK;
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

bool ChunkyFile::FCopyReference(ChunkTagOrType type, ChunkNumber number, PChunkyFile destination,
                               ChunkNumber *copied, int *created)
{
    AssertVarMem(created);
    return _FCopy(type, number, destination, copied, fFalse, created);
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
    Constructor for chunk graph enumerator.
***************************************************************************/
ChunkGraphEnumerator::ChunkGraphEnumerator(void)
{
    AssertThisMem();
    _pcfl = nullptr;
    AssertDo(mm_chunk_walk_init(&_walk, KauaiCopyAllocator()) == MM_CHUNK_OK, "invalid graph allocator");
    AssertThis(0);
}
ChunkGraphEnumerator::~ChunkGraphEnumerator(void)
{
    AssertThis(0);
    AssertDo(mm_chunk_walk_dispose(&_walk) == MM_CHUNK_OK, "invalid graph disposal");
}
#ifdef DEBUG
void ChunkGraphEnumerator::AssertValid(LegacyUlong grf)
{
    ChunkGraphEnumerator_PAR::AssertValid(0);
    AssertIn(_walk.phase, MM_WALK_START, MM_WALK_DONE + 1);
    if (_walk.phase != MM_WALK_DONE) AssertPo(_pcfl, 0);
    else Assert(!_walk.depth && !_walk.created && !_walk.stack.buffers[0].data, "graph stack retained after completion");
    Assert(_walk.depth <= _walk.stack.buffers[0].capacity / sizeof(MMChunkWalkFrame), "bad graph depth");
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
    AssertDo(mm_chunk_walk_reset(&_walk, &graph, ctg, cno) == MM_CHUNK_OK, "invalid graph reset");
    _pcfl = pcfl;
    AssertThis(0);
}
bool ChunkGraphEnumerator::FNextKid(ChildChunkIdentification *pkid, ChunkIdentification *pckiPar, LegacyUlong *pgrfcgeOut, LegacyUlong grfcgeIn)
{
    AssertThis(0); AssertVarMem(pkid); AssertNilOrVarMem(pckiPar); AssertVarMem(pgrfcgeOut);
    static_assert(fcgePre == MM_WALK_PRE && fcgePost == MM_WALK_POST &&
        fcgeRoot == MM_WALK_ROOT && fcgeError == MM_WALK_ERROR, "graph event flags drift");
    MMChunkChild node, parent; MMU32 flags;
    if (!mm_chunk_walk_next(&_walk, (grfcgeIn & fcgeSkipToSib) != 0, &node, &parent, &flags))
    {
        TrashVar(pkid); TrashVar(pckiPar); TrashVar(pgrfcgeOut); return fFalse;
    }
    *pkid = {{node.type, node.number}, node.slot}; *pgrfcgeOut = flags;
    if (pckiPar && !(flags & MM_WALK_ROOT)) *pckiPar = {parent.type, parent.number};
    TrashVarIf(flags & MM_WALK_ROOT, pckiPar);
    TrashVarIf(flags & MM_WALK_ROOT, &pkid->chid);
    return fTrue;
}

} // end of namespace Chunky
