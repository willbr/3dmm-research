/* Copyright (c) Microsoft Corporation.
   Licensed under the MIT License. */

/***************************************************************************
    Author: ShonK
    Project: Kauai
    Reviewed:
    Copyright (c) Microsoft Corporation

    Basic collection classes:
        General List (DynamicArray), Allocated List (AllocatedArray),
        General Group (GeneralGroup), Allocated Group (AllocatedGroup),
        General String Table (StringTable_GST), Allocated String Table (AllocatedStringTable).

        BASE ---> GroupBase -+-> VirtualArray -+-> DynamicArray
                        |        +-> AllocatedArray
                        |
                        +-> VirtualGroup -+-> GeneralGroup
                        |        +-> AllocatedGroup
                        |
                        +-> VirtualStringTable-+-> StringTable_GST
                                 +-> AllocatedStringTable

***************************************************************************/
#include "util.h"
ASSERTNAME

namespace Group {

RTCLASS(GroupBase)
RTCLASS(VirtualArray)
RTCLASS(DynamicArray)
RTCLASS(AllocatedArray)
RTCLASS(VirtualGroup)
RTCLASS(GeneralGroup)
RTCLASS(AllocatedGroup)

/***************************************************************************
    GroupBase:  Manages two sections of data.  Currently the two sections are
    in two C-owned buffers, but they could be in one without affecting the
    clients.  The actual data in the two sections is determined by the
    subclass (client).  This class just manages resizing the data sections.
***************************************************************************/

// 4-byte alignment quantum for GG/GGB variable storage. Historically spelled
// `size(long)`, which silently doubled to 8 on LP64 — but the .3MM wire format
// fixes this padding at 4 bytes (matching every existing on-disk GG entry).
// All uses inside this file that previously read `size(long)` for the
// GG-padding intent now use kcbAlignVar to keep the runtime layout coherent
// with the 32-bit on-disk alignment on every architecture.
static constexpr LegacyLong kcbAlignVar = 4;

namespace {
void *ResizeCollection(void *, void *data, ThreeMMU32 oldBytes, ThreeMMU32 newBytes, int priority)
{
    void *result = data;
    const LegacyLong mpr = priority == THREE_MM_STORAGE_SPEED ? mprForSpeed : mprNormal;
    const bool ok = data ? _FResizePpvDebug(&result, newBytes, oldBytes, fmemNil, mpr, &vdmglob.dmaglHq) :
        FAllocPvDebug(&result, newBytes, fmemNil, mpr, __szsFile, __LINE__, &vdmglob.dmaglHq);
    if (!ok) PushErc(ercOomHq);
    return ok ? result : nullptr;
}
void FreeCollection(void *, void *data, ThreeMMU32)
{
    FreePpvDebug(&data, &vdmglob.dmaglHq);
}
int ReadCollectionBlock(void *context, ThreeMMU32 offset, void *bytes, size_t count)
{
    if (offset > 0x7fffffffUL || count > 0x7fffffffUL) return 0;
    return static_cast<PDataBlock>(context)->FReadRgb(bytes, static_cast<LegacyLong>(count), offset);
}
int WriteCollectionBlock(void *context, ThreeMMU32 offset, const void *bytes, size_t count)
{
    if (offset > 0x7fffffffUL || count > 0x7fffffffUL) return 0;
    return static_cast<PDataBlock>(context)->FWriteRgb(const_cast<void *>(bytes), static_cast<LegacyLong>(count), offset);
}
} // namespace

namespace {
bool NativeBigEndian()
{
    const short value = 1;
    return reinterpret_cast<const byte *>(&value)[0] == 0;
}
ThreeMMCollectionHeader CollectionHeader(int kind, LegacyLong width, LegacyLong count, LegacyLong bytes, LegacyLong freeCount)
{
    ThreeMMCollectionHeader header = {};
    header.kind = kind; header.width = width; header.count = count; header.bytes = bytes; header.free_count = freeCount;
    AssertDo(three_mm_collection_layout(&header) == THREE_MM_STORAGE_OK, "invalid collection layout");
    return header;
}
LegacyLong CollectionSize(ThreeMMCollectionHeader header)
{
    return header.header_bytes + header.first_bytes + header.second_bytes;
}
} // namespace
bool GroupBase::_ReadCollection(PDataBlock block, int kind, bool allocated, ThreeMMCollectionHeader *header,
                                short *order, short *system)
{
    if (!block->FUnpackData())
    {
        TrashVar(order); TrashVar(system);
        return false;
    }
    ThreeMMReadStream reader = {block, static_cast<ThreeMMU32>(block->Cb()), ReadCollectionBlock};
    if (three_mm_collection_header_read(&reader, kind, allocated, header) != THREE_MM_STORAGE_OK)
    {
        Warn("file corrupt or not the requested collection");
        TrashVar(order); TrashVar(system);
        return false;
    }
    if (order) *order = (header->big_endian != 0) == NativeBigEndian() ? kboCur : kboOther;
    if (system)
    {
        auto value = static_cast<ushort>(header->system);
        if ((header->big_endian != 0) != NativeBigEndian()) value = static_cast<ushort>((value >> 8) | (value << 8));
        *system = static_cast<short>(value); // Preserve the original raw header output.
    }
    if (!_FReadData(block, header->first_bytes, header->second_bytes, header->header_bytes))
    {
        TrashVar(order); TrashVar(system);
        return false;
    }
    return true;
}
bool GroupBase::_WriteCollection(PDataBlock block, ThreeMMCollectionHeader header, short order, short system)
{
    Assert(order == kboCur || order == kboOther, "bad byte order");
    AssertOsk(system);
    byte wire[20];
    header.big_endian = order == kboCur ? NativeBigEndian() : !NativeBigEndian();
    header.system = static_cast<ushort>(system);
    if (three_mm_collection_header_write(&header, wire, sizeof(wire)) != THREE_MM_STORAGE_OK) return false;
    return _FWrite(block, wire, header.header_bytes, header.first_bytes, header.second_bytes);
}
ThreeMMArray VirtualArray::_ArrayView(bool allocated, LegacyLong freeCount)
{
    ThreeMMArray view = {};
    view.storage = _CollectionStorage(); view.width = _cbEntry; view.count = _ivMac;
    view.allocated = allocated; view.free_count = freeCount;
#ifdef DEBUG
    view.trash_unused = 1;
#endif
    return view;
}
void VirtualArray::_ApplyArray(const ThreeMMArray &view) { _ivMac = view.count; }
ThreeMMGroup VirtualGroup::_GroupView()
{
    ThreeMMGroup view = {};
    view.storage = _CollectionStorage(); view.fixed_bytes = _cbFixed; view.count = _ivMac;
    view.bytes = _bvMac; view.free_count = _clocFree;
#ifdef DEBUG
    view.trash_unused = 1;
#endif
    return view;
}
void VirtualGroup::_ApplyGroup(const ThreeMMGroup &view)
{
    _ivMac = view.count; _bvMac = view.bytes; _clocFree = view.free_count;
}

ThreeMMStorageAllocator CollectionAllocator()
{
    return {nullptr, ResizeCollection, FreeCollection};
}

GroupBase::GroupBase() : _cbMinGrow1(0), _cbMinGrow2(0), _ivMac(0)
{
    const auto allocator = CollectionAllocator();
    AssertDo(three_mm_storage_init(&_storage, &allocator) == THREE_MM_STORAGE_OK, "invalid collection allocator");
}
void GroupBase::_SetStorageGrowth()
{
    _storage.min_grow[0] = _cbMinGrow1;
    _storage.min_grow[1] = _cbMinGrow2;
}
void GroupBase::Lock()
{
    AssertDo(three_mm_storage_lock(&_storage, 0) == THREE_MM_STORAGE_OK, "collection lock overflow");
}
void GroupBase::Unlock()
{
    AssertDo(three_mm_storage_unlock(&_storage, 0) == THREE_MM_STORAGE_OK, "collection not locked");
}


/***************************************************************************
    Destructor for GroupBase. Releases the C-owned storage.
***************************************************************************/
GroupBase::~GroupBase(void)
{
    AssertThis(0);
    AssertDo(three_mm_storage_dispose(&_storage) == THREE_MM_STORAGE_OK, "freeing locked collection storage");
}

/***************************************************************************
    Ensure that the two sections are at least the given cb's large.
    if (grfgrp & fgrpShrink), makes them exact.
***************************************************************************/
bool GroupBase::_FEnsureSizes(LegacyLong cbMin1, LegacyLong cbMin2, LegacyUlong grfgrp)
{
    AssertThis(0);
    Assert(cbMin1 >= 0 && cbMin2 >= 0, "negative sizes");
    _SetStorageGrowth();
    const int result = three_mm_storage_ensure(&_storage, cbMin1, cbMin2, (grfgrp & fgrpShrink) != 0);
    if (result == THREE_MM_STORAGE_LOCKED)
    {
        Bug("resizing locked collection storage");
        PushErc(ercOomHq);
    }
    AssertThis(0);
    return result == THREE_MM_STORAGE_OK;
}



/***************************************************************************
    Make the given GroupBase a duplicate of this one.
***************************************************************************/
bool GroupBase::_FDup(PGroupBase pgrpbDst, LegacyLong cb1, LegacyLong cb2)
{
    AssertThis(0);
    AssertPo(pgrpbDst, 0);
    _SetStorageGrowth();
    pgrpbDst->_SetStorageGrowth();
    if (three_mm_storage_copy(&pgrpbDst->_storage, &_storage, cb1, cb2) != THREE_MM_STORAGE_OK)
        return fFalse;
    pgrpbDst->_cbMinGrow1 = _cbMinGrow1;
    pgrpbDst->_cbMinGrow2 = _cbMinGrow2;
    pgrpbDst->_ivMac = _ivMac;
    AssertPo(pgrpbDst, 0);
    return fTrue;
}

/***************************************************************************
    Write a group to a flo.
***************************************************************************/
bool GroupBase::FWriteFlo(PFileLocation pflo, short bo, short osk)
{
    DataBlock blck(pflo);
    return FWrite(&blck, bo, osk);
}

/***************************************************************************
    Write the GroupBase data to the block.  First write the (pv, cb), then
    cb1 bytes from the first section and cb2 bytes from the second.
***************************************************************************/
bool GroupBase::_FWrite(PDataBlock pblck, void *pv, LegacyLong cb, LegacyLong cb1, LegacyLong cb2)
{
    AssertPo(pblck, 0);
    const ThreeMMStorageWriter writer = {pblck, static_cast<ThreeMMU32>(pblck->Cb()), WriteCollectionBlock};
    return three_mm_storage_write(&_storage, &writer, pv, cb, cb1, cb2) == THREE_MM_STORAGE_OK;
}

/***************************************************************************
    Read the two sections of data from the given location in the given
    block.
***************************************************************************/
bool GroupBase::_FReadData(PDataBlock pblck, LegacyLong cb1, LegacyLong cb2, LegacyLong ib)
{
    AssertPo(pblck, fblckUnpacked);
    _SetStorageGrowth();
    const ThreeMMReadStream reader = {pblck, static_cast<ThreeMMU32>(pblck->Cb()), ReadCollectionBlock};
    return three_mm_storage_read(&_storage, &reader, ib, cb1, cb2) == THREE_MM_STORAGE_OK;
}

#ifdef DEBUG
/***************************************************************************
    Assert the validity of the grpb stuff.
***************************************************************************/
void GroupBase::AssertValid(LegacyUlong grfobj)
{
    GroupBase_PAR::AssertValid(grfobj | fobjAllocated);
    for (const auto &buffer : _storage.buffers)
    {
        Assert(buffer.capacity < THREE_MM_STORAGE_LIMIT, "collection storage too large");
        Assert((buffer.capacity == 0) == (buffer.data == nullptr), "collection storage size/pointer mismatch");
        if (buffer.data) AssertPvAlloced(buffer.data, buffer.capacity);
    }
}

/***************************************************************************
    Mark the host allocator blocks owned by C storage.
***************************************************************************/
void GroupBase::MarkMem(void)
{
    AssertThis(0);
    GroupBase_PAR::MarkMem();
    MarkPv(_storage.buffers[0].data);
    MarkPv(_storage.buffers[1].data);
}
#endif // DEBUG

/***************************************************************************
    VirtualArray:  Base class for DynamicArray (general list) and AllocatedArray (general allocated list).
    The list data goes in section 1.  The DynamicArray class doesn't use section 2.
    The AllocatedArray class uses section 2 for a bit array indicating whether an entry
    is free or in use.
***************************************************************************/

/***************************************************************************
    Constructor for the list base.
***************************************************************************/
VirtualArray::VirtualArray(LegacyLong cb)
{
    AssertIn(cb, 0, kcbMax);
    _cbEntry = cb;

    // use some reasonable values for _cbMinGrow* - code can always set
    // set these to something else
    _cbMinGrow1 = 128;
    _cbMinGrow2 = 16;
    AssertThis(0);
}

/***************************************************************************
    Return a volatile pointer to a list entry.
    NOTE: don't assert !FFree(iv) for allocated lists.
***************************************************************************/
void *VirtualArray::QvGet(LegacyLong iv)
{
    AssertThis(0);
    AssertIn(iv, 0, _ivMac + 1);
    return (0 == _ivMac) ? pvNil : _Qb1(LwMul(iv, _cbEntry));
}

/***************************************************************************
    Get the data for the iv'th element in the VirtualArray.
***************************************************************************/
void VirtualArray::Get(LegacyLong iv, void *pv)
{
    AssertThis(0);
    AssertIn(iv, 0, _ivMac);
    AssertPvCb(pv, _cbEntry);
    CopyPb(QvGet(iv), pv, _cbEntry);
}

/***************************************************************************
    Put data into the iv'th element in the VirtualArray.
***************************************************************************/
void VirtualArray::Put(LegacyLong iv, void *pv)
{
    AssertThis(0);
    AssertIn(iv, 0, _ivMac);
    AssertPvCb(pv, _cbEntry);
    CopyPb(pv, QvGet(iv), _cbEntry);
    AssertThis(0);
}

/***************************************************************************
    Lock the data and return a pointer to the ith item.
***************************************************************************/
void *VirtualArray::PvLock(LegacyLong iv)
{
    Lock();
    return QvGet(iv);
}

/***************************************************************************
    Set the minimum that a DynamicArray should grow by.
***************************************************************************/
void VirtualArray::SetMinGrow(LegacyLong cvAdd)
{
    AssertThis(0);
    AssertIn(cvAdd, 0, kcbMax);

    _cbMinGrow1 = CbRoundToLong(LwMul(cvAdd, _cbEntry));
    _cbMinGrow2 = CbRoundToLong(LwDivAway(cvAdd, 8));
}

#ifdef DEBUG
/***************************************************************************
    Assert the validity of a list (DynamicArray or AllocatedArray).
***************************************************************************/
void VirtualArray::AssertValid(LegacyUlong grfobj)
{
    VirtualArray_PAR::AssertValid(grfobj);
    AssertIn(_cbEntry, 1, kcbMax);
    AssertIn(_ivMac, 0, kcbMax);
    Assert(_Cb1() >= LwMul(_cbEntry, _ivMac), "array area too small");
}
#endif // DEBUG

/***************************************************************************
    Allocate a new list and ensure that it has space for cvInit elements.
***************************************************************************/
PDynamicArray DynamicArray::PglNew(LegacyLong cb, LegacyLong cvInit)
{
    AssertIn(cb, 1, kcbMax);
    AssertIn(cvInit, 0, kcbMax);
    PDynamicArray pgl;

    if ((pgl = NewObj DynamicArray(cb)) == pvNil)
        return pvNil;
    if (cvInit > 0 && !pgl->FEnsureSpace(cvInit, fgrpNil))
    {
        ReleasePpo(&pgl);
        return pvNil;
    }
    AssertPo(pgl, 0);
    return pgl;
}

/***************************************************************************
    Read a list from a block and return it.
***************************************************************************/
PDynamicArray DynamicArray::PglRead(PDataBlock pblck, short *pbo, short *posk)
{
    AssertPo(pblck, 0);
    AssertNilOrVarMem(pbo);
    AssertNilOrVarMem(posk);
    PDynamicArray pgl;

    /* the use of 4 for the cb is bogus, but _FRead overwrites the cb anyway */
    if ((pgl = NewObj DynamicArray(4)) == pvNil)
        goto LFail;
    if (!pgl->_FRead(pblck, pbo, posk))
    {
        ReleasePpo(&pgl);
    LFail:
        TrashVar(pbo);
        TrashVar(posk);
        return pvNil;
    }
    AssertPo(pgl, 0);
    return pgl;
}

/***************************************************************************
    Read a list from file and return it.
***************************************************************************/
PDynamicArray DynamicArray::PglRead(PFileObject pfil, FilePosition fp, LegacyLong cb, short *pbo, short *posk)
{
    DataBlock blck(pfil, fp, cb);
    return PglRead(&blck, pbo, posk);
}

/***************************************************************************
    Constructor for DynamicArray.
***************************************************************************/
DynamicArray::DynamicArray(LegacyLong cb) : VirtualArray(cb)
{
    AssertThis(0);
}

/***************************************************************************
    Provided for completeness (all GroupBase's have an FFree routine).
    Returns false iff iv is a valid index for the DynamicArray.
***************************************************************************/
bool DynamicArray::FFree(LegacyLong iv)
{
    AssertThis(0);
    return !FIn(iv, 0, _ivMac);
}

/***************************************************************************
    Duplicate this DynamicArray.
***************************************************************************/
PDynamicArray DynamicArray::PglDup(void)
{
    AssertThis(0);
    PDynamicArray pgl;

    if (pvNil == (pgl = PglNew(_cbEntry)))
        return pvNil;

    if (!_FDup(pgl, LwMul(_ivMac, _cbEntry), 0))
        ReleasePpo(&pgl);

    AssertNilOrPo(pgl, 0);
    return pgl;
}

// List on file. Fixed 12-byte wire format on every architecture; bare `long`
// would silently widen on LP64 (Linux/Mac x64) and break the 1995 .3MM format
// for every DynamicArray ever written by kauai.

/***************************************************************************
    Return the amount of space on file needed for the list.
***************************************************************************/
LegacyLong DynamicArray::CbOnFile(void)
{
    return CollectionSize(CollectionHeader(THREE_MM_COLLECTION_ARRAY, _cbEntry, _ivMac, 0, -1));
}

/***************************************************************************
    Write the list to disk.
***************************************************************************/
bool DynamicArray::FWrite(PDataBlock pblck, short bo, short osk)
{
    AssertThis(fobjAssertFull);
    return _WriteCollection(pblck, CollectionHeader(THREE_MM_COLLECTION_ARRAY, _cbEntry, _ivMac, 0, -1), bo, osk);
}

/***************************************************************************
    Read list data from disk.
***************************************************************************/
bool DynamicArray::_FRead(PDataBlock pblck, short *pbo, short *posk)
{
    ThreeMMCollectionHeader header;
    if (!_ReadCollection(pblck, THREE_MM_COLLECTION_ARRAY, false, &header, pbo, posk)) return false;
    _cbEntry = header.width; _ivMac = header.count;
    return true;
}

/***************************************************************************
    Insert some items into a list at position iv.  iv should be <= IvMac().
***************************************************************************/
bool DynamicArray::FInsert(LegacyLong iv, void *pv, LegacyLong cv)
{
    AssertThis(0);
    AssertIn(iv, 0, _ivMac + 1);
    AssertIn(cv, 1, kcbMax);
    auto view = _ArrayView();
    const bool ok = three_mm_array_insert(&view, iv, pv, cv) == THREE_MM_STORAGE_OK;
    _ApplyArray(view);
    AssertThis(0);
    return ok;
}

/***************************************************************************
    Delete an element from the list.  This changes the indices of all
    later elements.
***************************************************************************/
void DynamicArray::Delete(LegacyLong iv)
{
    AssertThis(0);
    Delete(iv, 1);
}

/***************************************************************************
    Delete a range of elements.  This changes the indices of all later
    elements.
***************************************************************************/
void DynamicArray::Delete(LegacyLong ivMin, LegacyLong cv)
{
    AssertIn(ivMin, 0, _ivMac);
    AssertIn(cv, 1, _ivMac - ivMin + 1);
    auto view = _ArrayView();
    AssertDo(three_mm_array_delete(&view, ivMin, cv) == THREE_MM_STORAGE_OK, "invalid array deletion");
    _ApplyArray(view);
    AssertThis(0);
}

/***************************************************************************
    Move the entry at ivSrc to be immediately before the element that is
    currently at ivTarget.  If ivTarget > ivSrc, the entry actually ends
    up at (ivTarget - 1) and the entry at ivTarget doesn't move.  If
    ivTarget < ivSrc, the entry ends up at ivTarget and the entry at
    ivTarget moves to (ivTarget + 1).  Everything in between is shifted
    appropriately.  ivTarget is allowed to be equal to IvMac().
***************************************************************************/
void DynamicArray::Move(LegacyLong ivSrc, LegacyLong ivTarget)
{
    AssertThis(0);
    auto view = _ArrayView();
    AssertDo(three_mm_array_move(&view, ivSrc, ivTarget) == THREE_MM_STORAGE_OK, "invalid array move");
    AssertThis(0);
}

/***************************************************************************
    Add an element to the end of the list.  Returns the location in *piv.
    On failure, returns false and *piv is undefined.
***************************************************************************/
bool DynamicArray::FAdd(void *pv, LegacyLong *piv)
{
    AssertThis(0);
    AssertNilOrVarMem(piv);

    if (piv != pvNil)
        *piv = _ivMac;
    if (!FInsert(_ivMac, pv))
    {
        TrashVar(piv);
        return fFalse;
    }
    return fTrue;
}

/***************************************************************************
    Stack operation.  Returns fFalse on stack underflow.
***************************************************************************/
bool DynamicArray::FPop(void *pv)
{
    AssertThis(0);
    auto view = _ArrayView();
    const bool ok = three_mm_array_pop(&view, pv) == THREE_MM_STORAGE_OK;
    _ApplyArray(view);
    AssertThis(0);
    return ok;
}

/***************************************************************************
    Set the number of elements.  Used rarely (to add a block of elements
    at a time or to "zero out" a list.
***************************************************************************/
bool DynamicArray::FSetIvMac(LegacyLong ivMacNew)
{
    AssertThis(0);
    auto view = _ArrayView();
    const bool ok = three_mm_array_set_count(&view, ivMacNew) == THREE_MM_STORAGE_OK;
    _ApplyArray(view);
    AssertThis(0);
    return ok;
}

/***************************************************************************
    Make sure there is room for at least cvAdd additional entries. If
    fgrpShrink is set, will shrink the list if it has more than cvAdd
    available entries.
***************************************************************************/
bool DynamicArray::FEnsureSpace(LegacyLong cvAdd, LegacyUlong grfgrp)
{
    AssertThis(0);
    auto view = _ArrayView();
    const bool ok = three_mm_array_reserve(&view, cvAdd, (grfgrp & fgrpShrink) != 0) == THREE_MM_STORAGE_OK;
    _ApplyArray(view);
    AssertThis(0);
    return ok;
}

/***************************************************************************
    Allocate a new allocated list and ensure that it has space for
    cvInit elements.
***************************************************************************/
PAllocatedArray AllocatedArray::PalNew(LegacyLong cb, LegacyLong cvInit)
{
    AssertIn(cb, 1, kcbMax);
    AssertIn(cvInit, 0, kcbMax);
    PAllocatedArray pal;

    if ((pal = NewObj AllocatedArray(cb)) == pvNil)
        return pvNil;
    if (cvInit > 0 && !pal->FEnsureSpace(cvInit, fgrpNil))
    {
        ReleasePpo(&pal);
        return pvNil;
    }
    AssertPo(pal, 0);
    return pal;
}

/***************************************************************************
    Read an allocated list from the block and return it.
***************************************************************************/
PAllocatedArray AllocatedArray::PalRead(PDataBlock pblck, short *pbo, short *posk)
{
    AssertPo(pblck, 0);
    AssertNilOrVarMem(pbo);
    AssertNilOrVarMem(posk);

    PAllocatedArray pal;

    /* the use of 4 for the cb is bogus, but _FRead overwrites the cb anyway */
    if ((pal = NewObj AllocatedArray(4)) == pvNil)
        goto LFail;
    if (!pal->_FRead(pblck, pbo, posk))
    {
        ReleasePpo(&pal);
    LFail:
        TrashVar(pbo);
        TrashVar(posk);
        return pvNil;
    }
    AssertPo(pal, 0);
    return pal;
}

/***************************************************************************
    Read an allocated list from file and return it.
***************************************************************************/
PAllocatedArray AllocatedArray::PalRead(PFileObject pfil, FilePosition fp, LegacyLong cb, short *pbo, short *posk)
{
    DataBlock blck(pfil, fp, cb);
    return PalRead(&blck, pbo, posk);
}

/***************************************************************************
    Constructor for AllocatedArray (allocated list) class.
***************************************************************************/
AllocatedArray::AllocatedArray(LegacyLong cb) : VirtualArray(cb)
{
    AssertThis(fobjAssertFull);
}

/***************************************************************************
    Duplicate this AllocatedArray.
***************************************************************************/
PAllocatedArray AllocatedArray::PalDup(void)
{
    AssertThis(fobjAssertFull);
    PAllocatedArray pal;

    if (pvNil == (pal = PalNew(_cbEntry)))
        return pvNil;

    if (!_FDup(pal, LwMul(_ivMac, _cbEntry), CbFromCbit(_ivMac)))
        ReleasePpo(&pal);
    else
        pal->_cvFree = _cvFree;

    AssertNilOrPo(pal, fobjAssertFull);
    return pal;
}

// Allocated list on file. Fixed 16-byte wire format on every architecture.

/***************************************************************************
    Return the amount of space on file needed for the list.
***************************************************************************/
LegacyLong AllocatedArray::CbOnFile(void)
{
    return CollectionSize(CollectionHeader(THREE_MM_COLLECTION_ALLOCATED_ARRAY, _cbEntry, _ivMac, 0, _cvFree));
}

/***************************************************************************
    Write the list to disk.
***************************************************************************/
bool AllocatedArray::FWrite(PDataBlock pblck, short bo, short osk)
{
    AssertThis(fobjAssertFull);
    return _WriteCollection(pblck, CollectionHeader(THREE_MM_COLLECTION_ALLOCATED_ARRAY, _cbEntry, _ivMac, 0, _cvFree), bo, osk);
}

/***************************************************************************
    Read allocated list data from the block.
***************************************************************************/
bool AllocatedArray::_FRead(PDataBlock pblck, short *pbo, short *posk)
{
    ThreeMMCollectionHeader header;
    if (!_ReadCollection(pblck, THREE_MM_COLLECTION_ALLOCATED_ARRAY, false, &header, pbo, posk)) return false;
    _cbEntry = header.width; _ivMac = header.count; _cvFree = header.free_count;
    return true;
}

/***************************************************************************
    Delete all entries in the AllocatedArray.
***************************************************************************/
void AllocatedArray::DeleteAll(void)
{
    _ivMac = 0;
    _cvFree = 0;
}

/***************************************************************************
    Returns whether the given element of the allocated list is free.
***************************************************************************/
bool AllocatedArray::FFree(LegacyLong iv)
{
    AssertThis(0);
    AssertIn(iv, 0, _ivMac);
    return (iv < _ivMac) && !(*_Qgrfbit(iv) & Fbit(iv));
}

/***************************************************************************
    Make sure there is room for at least cvAdd additional entries.  If
    fgrpShrink is set, will try to shrink the list if it has more than
    cvAdd available entries.
***************************************************************************/
bool AllocatedArray::FEnsureSpace(LegacyLong cvAdd, LegacyUlong grfgrp)
{
    auto view = _ArrayView(true, _cvFree);
    return three_mm_array_reserve(&view, cvAdd, (grfgrp & fgrpShrink) != 0) == THREE_MM_STORAGE_OK;
}

/***************************************************************************
    Add an element to the list.
***************************************************************************/
bool AllocatedArray::FAdd(void *pv, LegacyLong *piv)
{
    AssertThis(fobjAssertFull);
    auto view = _ArrayView(true, _cvFree);
    ThreeMMU32 position;
    const bool ok = three_mm_array_add(&view, pv, &position) == THREE_MM_STORAGE_OK;
    _ApplyArray(view); _cvFree = view.free_count;
    if (ok && piv) *piv = position;
    TrashVarIf(!ok, piv);
    AssertThis(fobjAssertFull);
    return ok;
}

/***************************************************************************
    Delete element iv from an allocated list.
***************************************************************************/
void AllocatedArray::Delete(LegacyLong iv)
{
    AssertThis(fobjAssertFull);
    auto view = _ArrayView(true, _cvFree);
    AssertDo(three_mm_array_delete(&view, iv, 1) == THREE_MM_STORAGE_OK, "invalid allocated-array deletion");
    _ApplyArray(view); _cvFree = view.free_count;
    AssertThis(fobjAssertFull);
}

#ifdef DEBUG
/***************************************************************************
    Check the validity of an allocated list.
***************************************************************************/
void AllocatedArray::AssertValid(LegacyUlong grfobj)
{
    LegacyLong cT, iv;

    AllocatedArray_PAR::AssertValid(0);
    Assert(_Cb2() >= CbFromCbit(_ivMac), "flag area too small");
    if (grfobj & fobjAssertFull)
    {
        for (cT = 0, iv = _ivMac; iv--;)
        {
            if ((*_Qgrfbit(iv) & Fbit(iv)) == 0)
                cT++;
        }
        Assert(cT == _cvFree, "_cvFree is wrong");
    }
}
#endif // DEBUG

/***************************************************************************
    Constructor for VirtualGroup class.
***************************************************************************/
VirtualGroup::VirtualGroup(LegacyLong cbFixed, bool fAllowFree)
{
    AssertIn(cbFixed, 0, kcbMax);
    _clocFree = fAllowFree ? 0 : cvNil;
    _cbFixed = cbFixed;

    // use some reasonable values for _cbMinGrow* - code can always set
    // set these to something else
    _cbMinGrow1 = LwMin(1024, 16 * cbFixed);
    _cbMinGrow2 = 16 * size(LogicalOffsetAndCount);
    AssertThis(fobjAssertFull);
}

/***************************************************************************
    Duplicate the group.
***************************************************************************/
bool VirtualGroup::_FDup(PVirtualGroup pggbDst)
{
    AssertThis(fobjAssertFull);
    AssertPo(pggbDst, fobjAssertFull);
    Assert(_cbFixed == pggbDst->_cbFixed, "why do these have different sized fixed portions?");

    if (!VirtualGroup_PAR::_FDup(pggbDst, _bvMac, LwMul(_ivMac, size(LogicalOffsetAndCount))))
        return fFalse;

    pggbDst->_bvMac = _bvMac;
    pggbDst->_clocFree = _clocFree;
    pggbDst->_cbFixed = _cbFixed;
    AssertPo(pggbDst, fobjAssertFull);

    return fTrue;
}

// group on file. Fixed 20-byte wire format on every architecture.

/***************************************************************************
    Return the amount of space on file needed for the group.
***************************************************************************/
LegacyLong VirtualGroup::CbOnFile(void)
{
    return CollectionSize(CollectionHeader(THREE_MM_COLLECTION_GROUP, _cbFixed, _ivMac, _bvMac, _clocFree));
}

/***************************************************************************
    Write the group to disk.  The client must ensure that the data in the
    VirtualGroup has the correct byte order (as specified by the bo).
***************************************************************************/
bool VirtualGroup::FWrite(PDataBlock pblck, short bo, short osk)
{
    AssertThis(fobjAssertFull);
    auto view = _GroupView();
    if (bo == kboOther) three_mm_group_swap_locations(&view);
    const bool ok = _WriteCollection(pblck, CollectionHeader(THREE_MM_COLLECTION_GROUP, _cbFixed, _ivMac, _bvMac, _clocFree), bo, osk);
    if (bo == kboOther) three_mm_group_swap_locations(&view);
    return ok;
}

/***************************************************************************
    Read group data from disk.
***************************************************************************/
bool VirtualGroup::_FRead(PDataBlock pblck, short *pbo, short *posk)
{
    ThreeMMCollectionHeader header;
    if (!_ReadCollection(pblck, THREE_MM_COLLECTION_GROUP, _clocFree != cvNil, &header, pbo, posk)) return false;
    _cbFixed = header.width; _ivMac = header.count; _bvMac = header.bytes; _clocFree = header.free_count;
    if ((header.big_endian != 0) != NativeBigEndian())
    {
        auto view = _GroupView();
        three_mm_group_swap_locations(&view);
    }
    return true;
}

/***************************************************************************
    Returns true iff the loc.bv is nil or iloc is out of range.
***************************************************************************/
bool VirtualGroup::FFree(LegacyLong iv)
{
    AssertBaseThis(0);
    AssertIn(iv, 0, kcbMax);
    LogicalOffsetAndCount *qloc;

    if (!FIn(iv, 0, _ivMac))
        return fTrue;
    qloc = _Qloc(iv);
    Assert(FIn(qloc->bv, 0, _bvMac) && FIn(qloc->cb, LwMax(_cbFixed, 1), _bvMac - qloc->bv + 1) ||
               0 == qloc->cb && (0 == qloc->bv || bvNil == qloc->bv),
           "bad loc");
    return bvNil == qloc->bv;
}

/***************************************************************************
    Ensures that there is room to add at least cvAdd new entries with
    a total of cbAdd bytes (among the variable parts of the elements).
    If there is more than enough room and fgrpShrink is passed, the StringTable_GST
    will shrink.
***************************************************************************/
bool VirtualGroup::FEnsureSpace(LegacyLong cvAdd, LegacyLong cbAdd, LegacyUlong grfgrp)
{
    AssertThis(0);
    auto view = _GroupView();
    const bool ok = three_mm_group_reserve(&view, cvAdd, cbAdd, (grfgrp & fgrpShrink) != 0) == THREE_MM_STORAGE_OK;
    _ApplyGroup(view);
    AssertThis(0);
    return ok;
}

/***************************************************************************
    Set the minimum that a VirtualGroup should grow by.
***************************************************************************/
void VirtualGroup::SetMinGrow(LegacyLong cvAdd, LegacyLong cbAdd)
{
    AssertThis(0);
    AssertIn(cvAdd, 0, kcbMax);
    AssertIn(cbAdd, 0, kcbMax);

    _cbMinGrow1 = CbRoundToLong(cbAdd + LwMul(cvAdd, _cbFixed + kcbAlignVar - 1));
    _cbMinGrow2 = LwMul(cvAdd, size(LogicalOffsetAndCount));
}





/***************************************************************************
    Returns a volative pointer the the fixed sized data in the element.
    If pcbVar is not nil, fills *pcbVar with the size of the variable part.
***************************************************************************/
void *VirtualGroup::QvFixedGet(LegacyLong iv, LegacyLong *pcbVar)
{
    AssertThis(0);
    AssertIn(_cbFixed, 1, kcbMax);
    AssertIn(iv, 0, _ivMac);
    Assert(!FFree(iv), "element free!");
    AssertNilOrVarMem(pcbVar);

    LogicalOffsetAndCount loc;

    loc = *_Qloc(iv);
    if (pcbVar != pvNil)
        *pcbVar = loc.cb - _cbFixed;
    AssertIn(loc.cb, _cbFixed, _bvMac - loc.bv + 1);
    return _Qb1(loc.bv);
}

/***************************************************************************
    Lock the data and return a pointer to the fixed sized data.
***************************************************************************/
void *VirtualGroup::PvFixedLock(LegacyLong iv, LegacyLong *pcbVar)
{
    AssertThis(0);
    Lock();
    return QvFixedGet(iv, pcbVar);
}

/***************************************************************************
    Get the fixed sized data for the element.
***************************************************************************/
void VirtualGroup::GetFixed(LegacyLong iv, void *pv)
{
    AssertThis(0);
    AssertIn(_cbFixed, 1, kcbMax);
    AssertIn(iv, 0, _ivMac);
    Assert(!FFree(iv), "element free!");
    AssertPvCb(pv, _cbFixed);

    LogicalOffsetAndCount loc;

    loc = *_Qloc(iv);
    AssertIn(loc.cb, _cbFixed, _bvMac - loc.bv + 1);
    CopyPb(_Qb1(loc.bv), pv, _cbFixed);
}

/***************************************************************************
    Put the fixed sized data for the element.
***************************************************************************/
void VirtualGroup::PutFixed(LegacyLong iv, void *pv)
{
    AssertThis(0);
    AssertIn(_cbFixed, 1, kcbMax);
    AssertIn(iv, 0, _ivMac);
    Assert(!FFree(iv), "element free!");
    AssertPvCb(pv, _cbFixed);

    LogicalOffsetAndCount loc;

    loc = *_Qloc(iv);
    AssertIn(loc.cb, _cbFixed, _bvMac - loc.bv + 1);
    CopyPb(pv, _Qb1(loc.bv), _cbFixed);
    AssertThis(0);
}

/***************************************************************************
    Return the length of the variable part of the iv'th element.
***************************************************************************/
LegacyLong VirtualGroup::Cb(LegacyLong iv)
{
    AssertThis(0);
    AssertIn(iv, 0, _ivMac);
    Assert(!FFree(iv), "element free!");

    return _Qloc(iv)->cb - _cbFixed;
}

/***************************************************************************
    Return a volatile pointer to the variable part of the iv'th element.
    If pcb is not nil, sets *pcb to the length of the (variable part of the)
    item.
***************************************************************************/
void *VirtualGroup::QvGet(LegacyLong iv, LegacyLong *pcb)
{
    AssertThis(0);
    AssertIn(iv, 0, _ivMac);
    Assert(!FFree(iv), "element free!");
    AssertNilOrVarMem(pcb);

    LogicalOffsetAndCount loc;

    loc = *_Qloc(iv);
    if (pcb != pvNil)
        *pcb = loc.cb - _cbFixed;
    AssertIn(loc.cb, _cbFixed, _bvMac - loc.bv + 1);
    return _Qb1(loc.bv + _cbFixed);
}

/***************************************************************************
    Lock the data and return a pointer to the (variable part of the) iv'th
    item.  If pcb is not nil, sets *pcb to the length of the (variable part
    of the) item.
***************************************************************************/
void *VirtualGroup::PvLock(LegacyLong iv, LegacyLong *pcb)
{
    AssertThis(0);
    Lock();
    return QvGet(iv, pcb);
}

/***************************************************************************
    Copy the (variable part of the) iv'th element to pv.
***************************************************************************/
void VirtualGroup::Get(LegacyLong iv, void *pv)
{
    AssertThis(0);
    AssertIn(iv, 0, _ivMac);
    Assert(!FFree(iv), "element free!");

    LogicalOffsetAndCount loc;

    loc = *_Qloc(iv);
    AssertPvCb(pv, loc.cb - _cbFixed);
    CopyPb(_Qb1(loc.bv + _cbFixed), pv, loc.cb - _cbFixed);
}

/***************************************************************************
    Copy *pv to the (variable part of the) iv'th element.
***************************************************************************/
void VirtualGroup::Put(LegacyLong iv, void *pv)
{
    AssertThis(0);
    AssertIn(iv, 0, _ivMac);
    Assert(!FFree(iv), "element free!");

    LogicalOffsetAndCount loc;

    loc = *_Qloc(iv);
    AssertPvCb(pv, loc.cb - _cbFixed);
    CopyPb(pv, _Qb1(loc.bv + _cbFixed), loc.cb - _cbFixed);
}

/***************************************************************************
    Replace the (variable part of the) iv'th element with the stuff in pv
    (cb bytes worth).  pv may be nil (effectively resizing the block).
***************************************************************************/
bool VirtualGroup::FPut(LegacyLong iv, LegacyLong cb, void *pv)
{
    AssertThis(0);
    auto view = _GroupView();
    const bool ok = three_mm_group_put(&view, iv, cb, pv) == THREE_MM_STORAGE_OK;
    _ApplyGroup(view);
    AssertThis(0);
    return ok;
}

/***************************************************************************
    Get a portion of the element.
***************************************************************************/
void VirtualGroup::GetRgb(LegacyLong iv, LegacyLong bv, LegacyLong cb, void *pv)
{
    AssertThis(0);
    AssertIn(iv, 0, _ivMac);
    Assert(!FFree(iv), "element free!");
    AssertPvCb(pv, cb);

    LogicalOffsetAndCount loc;

    bv += _cbFixed;
    loc = *_Qloc(iv);
    AssertIn(bv, _cbFixed, loc.cb);
    AssertIn(cb, 1, loc.cb - bv + 1);

    CopyPb(_Qb1(loc.bv + bv), pv, cb);
}

/***************************************************************************
    Put a portion of the element.
***************************************************************************/
void VirtualGroup::PutRgb(LegacyLong iv, LegacyLong bv, LegacyLong cb, void *pv)
{
    AssertThis(0);
    AssertIn(iv, 0, _ivMac);
    Assert(!FFree(iv), "element free!");
    AssertPvCb(pv, cb);

    LogicalOffsetAndCount loc;

    bv += _cbFixed;
    loc = *_Qloc(iv);
    AssertIn(bv, _cbFixed, loc.cb);
    AssertIn(cb, 1, loc.cb - bv + 1);

    CopyPb(pv, _Qb1(loc.bv + bv), cb);
    AssertThis(0);
}

/***************************************************************************
    Remove a portion of element iv (can't be all of it).
***************************************************************************/
void VirtualGroup::DeleteRgb(LegacyLong iv, LegacyLong bv, LegacyLong cb)
{
    AssertThis(0);
    auto view = _GroupView();
    AssertDo(three_mm_group_delete_bytes(&view, iv, bv, cb) == THREE_MM_STORAGE_OK, "invalid group mutation");
    _ApplyGroup(view);
    AssertThis(0);
}

/***************************************************************************
    Insert cb new bytes at location bv into the iv'th element.  pv may
    be nil.
***************************************************************************/
bool VirtualGroup::FInsertRgb(LegacyLong iv, LegacyLong bv, LegacyLong cb, void *pv)
{
    AssertThis(0);
    AssertIn(cb, 1, kcbMax);
    auto view = _GroupView();
    const bool ok = three_mm_group_insert_bytes(&view, iv, bv, cb, pv) == THREE_MM_STORAGE_OK;
    _ApplyGroup(view);
    AssertThis(0);
    return ok;
}

/***************************************************************************
    Move cb bytes from position bvSrc in ivSrc to position bvDst in ivDst.
    This can fail only because of the padding used for each entry (at most
    kcbAlignVar additional bytes will need to be allocated).
***************************************************************************/
bool VirtualGroup::FMoveRgb(LegacyLong ivSrc, LegacyLong bvSrc, LegacyLong ivDst, LegacyLong bvDst, LegacyLong cb)
{
    AssertThis(fobjAssertFull);
    auto view = _GroupView();
    const bool ok = three_mm_group_move_bytes(&view, ivSrc, bvSrc, ivDst, bvDst, cb) == THREE_MM_STORAGE_OK;
    _ApplyGroup(view);
    AssertThis(fobjAssertFull);
    return ok;
}

/***************************************************************************
    Append the variable data of ivSrc to ivDst, and delete ivSrc.

    NOTE: this is kind of goofy.  The only time FMoveRgb could possibly
    fail if we just do the naive thing (FMoveRgb the entire var data,
    then delete the source element) is if _cbFixed is not a multiple
    of kcbAlignVar.
***************************************************************************/
void VirtualGroup::Merge(LegacyLong ivSrc, LegacyLong ivDst)
{
    AssertThis(fobjAssertFull);
    auto view = _GroupView();
    AssertDo(three_mm_group_merge(&view, ivSrc, ivDst) == THREE_MM_STORAGE_OK, "invalid group mutation");
    _ApplyGroup(view);
    AssertThis(fobjAssertFull);
}

#ifdef DEBUG
/***************************************************************************
    Validate a group.
***************************************************************************/
void VirtualGroup::AssertValid(LegacyUlong grfobj)
{
    LogicalOffsetAndCount loc;
    LegacyLong iloc;
    LegacyLong cbTot, clocFree;

    VirtualGroup_PAR::AssertValid(grfobj);
    AssertIn(_ivMac, 0, kcbMax);
    AssertIn(_bvMac, 0, kcbMax);
    Assert(_Cb1() >= _bvMac, "group area too small");
    Assert(_Cb2() >= LwMul(_ivMac, size(LogicalOffsetAndCount)), "rgloc area too small");
    Assert(_clocFree == cvNil || _clocFree == 0 || _clocFree > 0 && _clocFree < _ivMac, "_clocFree is wrong");
    AssertIn(_cbFixed, 0, kcbMax);

    if (grfobj & fobjAssertFull)
    {
        for (clocFree = cbTot = iloc = 0; iloc < _ivMac; iloc++)
        {
            loc = *_Qloc(iloc);
            if (bvNil == loc.bv)
            {
                Assert(iloc < _ivMac - 1, "Last element free");
                Assert(loc.cb == 0, "bad cb in free loc");
                clocFree++;
                continue;
            }
            AssertIn(loc.cb, _cbFixed, _bvMac + 1);
            AssertIn(loc.bv, 0, _bvMac);
            Assert(loc.cb > 0 || loc.bv == 0, "zero sized item doesn't have zero bv");
            loc.cb = CbRoundToLong(loc.cb);
            Assert(loc.bv + loc.cb <= _bvMac, "loc extends past _bvMac");
            cbTot += loc.cb;
        }
        Assert(cbTot == _bvMac, "group wrong size");
        Assert(clocFree == _clocFree || _clocFree == cvNil && clocFree == 0, "bad _clocFree");
    }
}
#endif // DEBUG

/***************************************************************************
    Allocate a new group with room for at least cvInit elements containing
    at least cbInit bytes worth of (total) space.
***************************************************************************/
PGeneralGroup GeneralGroup::PggNew(LegacyLong cbFixed, LegacyLong cvInit, LegacyLong cbInit)
{
    AssertIn(cbFixed, 0, kcbMax);
    AssertIn(cvInit, 0, kcbMax);
    AssertIn(cbInit, 0, kcbMax);

    PGeneralGroup pgg;

    if ((pgg = NewObj GeneralGroup(cbFixed)) == pvNil)
        return pvNil;
    if ((cvInit > 0 || cbInit > 0) && !pgg->FEnsureSpace(cvInit, cbInit, fgrpNil))
    {
        ReleasePpo(&pgg);
        return pvNil;
    }
    AssertPo(pgg, fobjAssertFull);
    return pgg;
}

/***************************************************************************
    Read a group from a block and return it.
***************************************************************************/
PGeneralGroup GeneralGroup::PggRead(PDataBlock pblck, short *pbo, short *posk)
{
    AssertPo(pblck, 0);
    AssertNilOrVarMem(pbo);
    AssertNilOrVarMem(posk);

    PGeneralGroup pgg;

    if ((pgg = NewObj GeneralGroup(0)) == pvNil)
        goto LFail;
    if (!pgg->_FRead(pblck, pbo, posk))
    {
        ReleasePpo(&pgg);
    LFail:
        TrashVar(pbo);
        TrashVar(posk);
        return pvNil;
    }
    AssertPo(pgg, fobjAssertFull);
    return pgg;
}

/***************************************************************************
    Read a group from file and return it.
***************************************************************************/
PGeneralGroup GeneralGroup::PggRead(PFileObject pfil, FilePosition fp, LegacyLong cb, short *pbo, short *posk)
{
    DataBlock blck(pfil, fp, cb);
    return PggRead(&blck, pbo, posk);
}

/***************************************************************************
    Duplicate this GeneralGroup.
***************************************************************************/
PGeneralGroup GeneralGroup::PggDup(void)
{
    AssertThis(0);
    PGeneralGroup pgg;

    if (pvNil == (pgg = PggNew(_cbFixed)))
        return pvNil;

    if (!_FDup(pgg))
        ReleasePpo(&pgg);

    AssertNilOrPo(pgg, 0);
    return pgg;
}

/***************************************************************************
    Insert an element into the group.
***************************************************************************/
bool GeneralGroup::FInsert(LegacyLong iv, LegacyLong cb, void *pv, void *pvFixed)
{
    AssertThis(fobjAssertFull);
    auto view = _GroupView();
    const bool ok = three_mm_group_insert(&view, iv, cb, pv, pvFixed) == THREE_MM_STORAGE_OK;
    _ApplyGroup(view);
    AssertThis(fobjAssertFull);
    return ok;
}

/***************************************************************************
    Takes cv entries from pggSrc at ivSrc and inserts (a copy of) them into
    this GeneralGroup at ivDst.
***************************************************************************/
bool GeneralGroup::FCopyEntries(PGeneralGroup pggSrc, LegacyLong ivSrc, LegacyLong ivDst, LegacyLong cv)
{
    AssertThis(fobjAssertFull);
    auto destination = _GroupView();
    auto source = pggSrc->_GroupView();
    const bool ok = three_mm_group_copy_entries(&destination, &source, ivSrc, ivDst, cv) == THREE_MM_STORAGE_OK;
    _ApplyGroup(destination);
    AssertThis(fobjAssertFull);
    return ok;
}

/***************************************************************************
    Append an element to the group.
***************************************************************************/
bool GeneralGroup::FAdd(LegacyLong cb, LegacyLong *piv, void *pv, void *pvFixed)
{
    AssertThis(0);
    AssertNilOrVarMem(piv);

    if (piv != pvNil)
        *piv = _ivMac;
    if (!FInsert(_ivMac, cb, pv, pvFixed))
    {
        TrashVar(piv);
        return fFalse;
    }
    return fTrue;
}

/***************************************************************************
    Delete an element from the group.
***************************************************************************/
void GeneralGroup::Delete(LegacyLong iv)
{
    AssertThis(fobjAssertFull);
    auto view = _GroupView();
    AssertDo(three_mm_group_delete(&view, iv) == THREE_MM_STORAGE_OK, "invalid group mutation");
    _ApplyGroup(view);
    AssertThis(fobjAssertFull);
}

/***************************************************************************
    Move the entry at ivSrc to be immediately before the element that is
    currently at ivTarget.  If ivTarget > ivSrc, the entry actually ends
    up at (ivTarget - 1) and the entry at ivTarget doesn't move.  If
    ivTarget < ivSrc, the entry ends up at ivTarget and the entry at
    ivTarget moves to (ivTarget + 1).  Everything in between is shifted
    appropriately.  ivTarget is allowed to be equal to IvMac().
***************************************************************************/
void GeneralGroup::Move(LegacyLong ivSrc, LegacyLong ivTarget)
{
    AssertThis(0);
    auto view = _GroupView();
    AssertDo(three_mm_group_move(&view, ivSrc, ivTarget) == THREE_MM_STORAGE_OK, "invalid group mutation");
    _ApplyGroup(view);
    AssertThis(0);
}

/***************************************************************************
    Swap two elements in a GeneralGroup.
***************************************************************************/
void GeneralGroup::Swap(LegacyLong iv1, LegacyLong iv2)
{
    AssertThis(0);
    auto view = _GroupView();
    AssertDo(three_mm_group_swap(&view, iv1, iv2) == THREE_MM_STORAGE_OK, "invalid group mutation");
    _ApplyGroup(view);
    AssertThis(0);
}

#ifdef DEBUG
/***************************************************************************
    Validate a group.
***************************************************************************/
void GeneralGroup::AssertValid(LegacyUlong grfobj)
{
    GeneralGroup_PAR::AssertValid(grfobj);
    AssertVar(_clocFree == cvNil, "bad _clocFree in GeneralGroup", &_clocFree);
}
#endif // DEBUG

/***************************************************************************
    Allocate a new allocated group with romm for at least cvInit elements
    containing at least cbInit bytes worth of (total) space.
***************************************************************************/
PAllocatedGroup AllocatedGroup::PagNew(LegacyLong cbFixed, LegacyLong cvInit, LegacyLong cbInit)
{
    AssertIn(cbFixed, 0, kcbMax);
    AssertIn(cvInit, 0, kcbMax);
    AssertIn(cbInit, 0, kcbMax);

    PAllocatedGroup pag;

    if ((pag = NewObj AllocatedGroup(cbFixed)) == pvNil)
        return pvNil;
    if ((cvInit > 0 || cbInit > 0) && !pag->FEnsureSpace(cvInit, cbInit, fgrpNil))
    {
        ReleasePpo(&pag);
        return pvNil;
    }
    AssertPo(pag, fobjAssertFull);
    return pag;
}

/***************************************************************************
    Read an allocated group from a block and return it.
***************************************************************************/
PAllocatedGroup AllocatedGroup::PagRead(PDataBlock pblck, short *pbo, short *posk)
{
    AssertPo(pblck, 0);
    AssertNilOrVarMem(pbo);
    AssertNilOrVarMem(posk);

    PAllocatedGroup pag;

    if ((pag = NewObj AllocatedGroup(0)) == pvNil)
        goto LFail;
    if (!pag->_FRead(pblck, pbo, posk))
    {
        ReleasePpo(&pag);
    LFail:
        TrashVar(pbo);
        TrashVar(posk);
        return pvNil;
    }
    AssertPo(pag, fobjAssertFull);
    return pag;
}

/***************************************************************************
    Read an allocated group from file and return it.
***************************************************************************/
PAllocatedGroup AllocatedGroup::PagRead(PFileObject pfil, FilePosition fp, LegacyLong cb, short *pbo, short *posk)
{
    DataBlock blck(pfil, fp, cb);
    return PagRead(&blck, pbo, posk);
}

/***************************************************************************
    Duplicate this AllocatedGroup.
***************************************************************************/
PAllocatedGroup AllocatedGroup::PagDup(void)
{
    AssertThis(0);
    PAllocatedGroup pag;

    if (pvNil == (pag = PagNew(_cbFixed)))
        return pvNil;

    if (!_FDup(pag))
        ReleasePpo(&pag);

    AssertNilOrPo(pag, 0);
    return pag;
}

/***************************************************************************
    Add an element to the allocated group.
***************************************************************************/
bool AllocatedGroup::FAdd(LegacyLong cb, LegacyLong *piv, void *pv, void *pvFixed)
{
    AssertThis(fobjAssertFull);
    auto view = _GroupView();
    ThreeMMU32 position;
    const bool ok = three_mm_group_add(&view, cb, pv, pvFixed, &position) == THREE_MM_STORAGE_OK;
    _ApplyGroup(view);
    if (ok && piv) *piv = position;
    TrashVarIf(!ok, piv);
    AssertThis(fobjAssertFull);
    return ok;
}

/***************************************************************************
    Delete an element from the group.
***************************************************************************/
void AllocatedGroup::Delete(LegacyLong iv)
{
    AssertThis(fobjAssertFull);
    auto view = _GroupView();
    AssertDo(three_mm_group_delete(&view, iv) == THREE_MM_STORAGE_OK, "invalid group mutation");
    _ApplyGroup(view);
    AssertThis(fobjAssertFull);
}

#ifdef DEBUG
/***************************************************************************
    Validate a group.
***************************************************************************/
void AllocatedGroup::AssertValid(LegacyUlong grfobj)
{
    AllocatedGroup_PAR::AssertValid(grfobj);
    AssertIn(_clocFree, 0, LwMax(1, _ivMac));
}
#endif // DEBUG

} // end of namespace Group
