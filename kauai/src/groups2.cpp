/* Copyright (c) Microsoft Corporation.
   Licensed under the MIT License. */

/***************************************************************************
    Author: ******
    Project: Kauai
    Reviewed:
    Copyright (c) Microsoft Corporation

    Basic collection classes (continued from groups.cpp):
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

RTCLASS(VirtualStringTable)
RTCLASS(StringTable_GST)
RTCLASS(AllocatedStringTable)

ThreeMMStringTable VirtualStringTable::_StringStorage()
{
    ThreeMMStringTable storage = {};
    storage.strings = _Cb1() ? _Qb1(0) : nullptr;
    storage.entries = _Cb2() ? _Qb2(0) : nullptr;
    storage.bytes = _bstMac;
    storage.count = _ivMac;
    storage.stride = _cbEntry;
    storage.free_count = _cbstFree;
    storage.unit_bytes = sizeof(achar);
    storage.string_capacity = _Cb1();
    storage.entry_capacity = _Cb2();
#ifdef DEBUG
    storage.trash_unused = 1;
#endif
    storage.context = this;
    storage.reserve = _ReserveStringStorage;
    return storage;
}
void VirtualStringTable::_ApplyStringStorage(const ThreeMMStringTable &storage)
{
    _bstMac = storage.bytes;
    _ivMac = storage.count;
    _cbstFree = storage.free_count;
}
int VirtualStringTable::_ReserveStringStorage(void *context, ThreeMMStringTable *storage,
                                             ThreeMMU32 strings, ThreeMMU32 entries)
{
    auto table = static_cast<VirtualStringTable *>(context);
    table->_ApplyStringStorage(*storage);
    const bool ok = table->_FEnsureSizes(strings, entries, fgrpNil);
    // The first allocation may have grown even when the second one failed.
    storage->strings = table->_Cb1() ? table->_Qb1(0) : nullptr;
    storage->entries = table->_Cb2() ? table->_Qb2(0) : nullptr;
    storage->string_capacity = table->_Cb1();
    storage->entry_capacity = table->_Cb2();
    return ok;
}
namespace {
int CompareStringStorage(void *context, const void *left, ThreeMMU32 leftCount,
                         const void *right, ThreeMMU32 rightCount)
{
    auto a = const_cast<achar *>(static_cast<const achar *>(left));
    auto b = const_cast<achar *>(static_cast<const achar *>(right));
    const auto flags = *static_cast<LegacyUlong *>(context);
    const auto result = flags & fgstUserSorted ? FcmpCompareUserRgch(a, leftCount, b, rightCount) :
                                               FcmpCompareRgch(a, leftCount, b, rightCount);
    return result == fcmpLt ? -1 : result == fcmpGt ? 1 : 0;
}
size_t ConvertStringStorage(void *, const void *input, size_t bytes, int source, void *output, size_t capacity)
{
    return CchTranslateRgb(const_cast<void *>(input), bytes, source, static_cast<achar *>(output), capacity);
}
} // namespace


/***************************************************************************
    Constructor for a base string table.
***************************************************************************/
VirtualStringTable::VirtualStringTable(LegacyLong cbExtra, LegacyUlong grfgst)
{
    AssertIn(cbExtra, 0, kcbMax);
    Assert(cbExtra % size(LegacyLong) == 0, "cbExtra not multiple of size(long)");

    _cbEntry = cbExtra + size(LegacyLong);
    _cbstFree = (grfgst & fgstAllowFree) ? 0 : cvNil;

    // use some reasonable values for _cbMinGrow* - code can always set
    // set these to something else
    _cbMinGrow1 = 120;
    _cbMinGrow2 = 12 * _cbEntry;
    AssertThis(fobjAssertFull);
}

/***************************************************************************
    Duplicate the string table.
***************************************************************************/
bool VirtualStringTable::_FDup(PVirtualStringTable pgstbDst)
{
    AssertThis(fobjAssertFull);
    AssertPo(pgstbDst, fobjAssertFull);
    Assert(_cbEntry == pgstbDst->_cbEntry, "why do these have different sized entries?");

    if (!VirtualStringTable_PAR::_FDup(pgstbDst, _bstMac, LwMul(_cbEntry, _ivMac)))
        return fFalse;

    pgstbDst->_cbEntry = _cbEntry;
    pgstbDst->_bstMac = _bstMac;
    pgstbDst->_cbstFree = _cbstFree;
    AssertPo(pgstbDst, fobjAssertFull);

    return fTrue;
}

// string table on file. Fixed 20-byte wire format on every architecture.
struct StringTableOnFile
{
    int16_t bo;
    int16_t osk;
    int32_t cbEntry;
    int32_t ibstMac;
    int32_t bstMac;
    int32_t cbstFree;
};
static_assert(sizeof(StringTableOnFile) == 20, "StringTableOnFile wire format drift");
const ByteOrderMask kbomGstf = 0x5FF00000L;

/***************************************************************************
    Return the amount of space on file needed for the string table.
***************************************************************************/
LegacyLong VirtualStringTable::CbOnFile(void)
{
    AssertThis(0);
    return size(StringTableOnFile) + LwMul(_ivMac, _cbEntry) + _bstMac;
}

/***************************************************************************
    Write the string table to disk.  It is the client's responsibility to
    ensure that the extra data has the given byte order (bo) and osk.
    If the osk has a different character size than the current one, the
    strings are actually saved in the corresponding osk with the same sized
    characters.
***************************************************************************/
bool VirtualStringTable::FWrite(PDataBlock pblck, short bo, short osk)
{
    AssertThis(fobjAssertFull);
    AssertPo(pblck, 0);
    Assert(kboCur == bo || kboOther == bo, "bad bo");
    AssertOsk(osk);

    StringTableOnFile gstf;
    bool fRet;

    if (osk != koskCur)
    {
        switch (osk)
        {
        case koskSbMac:
        case koskUniMac:
            osk = koskMac;
            break;

        case koskSbWin:
        case koskUniWin:
            osk = koskWin;
            break;

        default:
            Bug("unknown osk");
            return fFalse;
        }
    }

    gstf.bo = kboCur;
    gstf.osk = osk;
    gstf.cbEntry = _cbEntry;
    gstf.ibstMac = _ivMac;
    gstf.bstMac = _bstMac;
    gstf.cbstFree = _cbstFree;
    if (kboOther == bo)
    {
        SwapBytesBom(&gstf, kbomGstf);
        Assert(gstf.osk == osk, "osk not invariant under byte swapping");
        _SwapBytesRgbst();
    }
    if (koskCur != osk)
        _TranslateGrst(osk, fFalse);
    fRet = _FWrite(pblck, &gstf, size(gstf), _bstMac, LwMul(_cbEntry, _ivMac));
    if (koskCur != osk)
        _TranslateGrst(osk, fTrue);
    if (kboOther == bo)
        _SwapBytesRgbst();
    return fRet;
}

/***************************************************************************
    Read string table data from a block.
***************************************************************************/
bool VirtualStringTable::_FRead(PDataBlock pblck, short *pbo, short *posk)
{
    AssertThis(0);
    AssertPo(pblck, 0);
    AssertNilOrVarMem(pbo);
    AssertNilOrVarMem(posk);

    StringTableOnFile gstf;
    LegacyLong cbT;
    short bo;
    LegacyLong cb;
    bool fRet = fFalse;

    if (!pblck->FUnpackData())
        goto LFail;

    cb = pblck->Cb();
    if (cb < size(gstf))
        goto LBug;

    if (!pblck->FReadRgb(&gstf, size(gstf), 0))
        goto LFail;

    if (pbo != pvNil)
        *pbo = gstf.bo;
    if (posk != pvNil)
        *posk = gstf.osk;

    if ((bo = gstf.bo) == kboOther)
        SwapBytesBom(&gstf, kbomGstf);

    cb -= size(gstf);
    // don't use LwMul, in case the file is corrupted, we don't want to assert,
    // we should detect it below.
    cbT = gstf.cbEntry * gstf.ibstMac;
    if (gstf.bo != kboCur || !FIn(gstf.cbEntry, size(LegacyLong), kcbMax) || (gstf.cbEntry % size(LegacyLong)) != 0 ||
        !FIn(gstf.ibstMac, 0, kcbMax) || !FIn(gstf.bstMac, gstf.ibstMac - LwMax(0, gstf.cbstFree), kcbMax) ||
        cb != cbT + gstf.bstMac || (gstf.cbstFree == cvNil) != (_cbstFree == cvNil) ||
        gstf.cbstFree != cvNil && !FIn(gstf.cbstFree, 0, LwMax(1, gstf.ibstMac)))
    {
    LBug:
        Warn("file corrupt or not a VirtualStringTable");
        goto LFail;
    }

    _cbEntry = gstf.cbEntry;
    _ivMac = gstf.ibstMac;
    _bstMac = gstf.bstMac;
    _cbstFree = gstf.cbstFree;
    fRet = _FReadData(pblck, cb - cbT, cbT, size(gstf));
    if (fRet)
    {
        if (bo == kboOther)
            _SwapBytesRgbst();
        if (koskCur != gstf.osk)
            fRet = _FTranslateGrst(gstf.osk);
    }

LFail:
    TrashVarIf(!fRet, pbo);
    TrashVarIf(!fRet, posk);
    return fRet;
}

/***************************************************************************
    Ensures that there is room to add at least cstnAdd new strings with
    a total of cchAdd characters.  If there is more than enough room and
    fgrpShrink is passed, the VirtualStringTable may shrink.
***************************************************************************/
bool VirtualStringTable::FEnsureSpace(LegacyLong cstnAdd, LegacyLong cchAdd, LegacyUlong grfgrp)
{
    AssertThis(0);
    AssertIn(cstnAdd, 0, kcbMax);
    AssertIn(cchAdd, 0, kcbMax);
    LegacyLong cbstAdd;
    LegacyLong cbAdd = cchAdd * size(achar);

    if (cvNil == _cbstFree)
        cbstAdd = cstnAdd;
    else
        cbstAdd = LwMax(0, cstnAdd - _cbstFree);
    if (cbstAdd > kcbMax / _cbEntry - _ivMac || cstnAdd > kcbMax - _bstMac || cbAdd > kcbMax - _bstMac - cstnAdd)
    {
        Bug("why is this string table growing so large?");
        return fFalse;
    }

    return _FEnsureSizes(_bstMac + cbAdd + cstnAdd, LwMul(_ivMac + cbstAdd, _cbEntry), grfgrp);
}

/***************************************************************************
    Set the minimum that a VirtualStringTable should grow by.
***************************************************************************/
void VirtualStringTable::SetMinGrow(LegacyLong cstnAdd, LegacyLong cchAdd)
{
    AssertThis(0);
    AssertIn(cstnAdd, 0, kcbMax);
    AssertIn(cchAdd, 0, kcbMax);

    _cbMinGrow1 = CbRoundToLong(cchAdd * size(achar) + cstnAdd);
    _cbMinGrow2 = LwMul(cstnAdd, _cbEntry);
}

/***************************************************************************
    Append an stn to string table.
***************************************************************************/
bool VirtualStringTable::FAddStn(PString pstn, void *pvExtra, LegacyLong *pistn)
{
    AssertThis(0);
    AssertPo(pstn, 0);

    return FAddRgch(pstn->Prgch(), pstn->Cch(), pvExtra, pistn);
}

/***************************************************************************
    Replace the ith string.
***************************************************************************/
bool VirtualStringTable::FPutRgch(LegacyLong istn, achar *prgch, LegacyLong cch)
{
    AssertThis(fobjAssertFull);
    auto storage = _StringStorage();
    const bool ok = three_mm_strings_put(&storage, istn, prgch, cch) != 0;
    _ApplyStringStorage(storage);
    AssertThis(fobjAssertFull);
    return ok;
}

/***************************************************************************
    Replace the ith string with stn.
***************************************************************************/
bool VirtualStringTable::FPutStn(LegacyLong istn, PString pstn)
{
    AssertThis(0);
    AssertPo(pstn, 0);

    return FPutRgch(istn, pstn->Prgch(), pstn->Cch());
}

/***************************************************************************
    Get up to cchMax characters for the istn'th string.
***************************************************************************/
void VirtualStringTable::GetRgch(LegacyLong istn, achar *prgch, LegacyLong cchMax, LegacyLong *pcch)
{
    AssertThis(0);
    AssertIn(istn, 0, _ivMac);
    Assert(!FFree(istn), "string entry is free!");
    AssertIn(cchMax, 0, kcbMax);
    AssertPvCb(prgch, cchMax * size(achar));
    AssertVarMem(pcch);
    PPascalString qst = _Qst(istn);

    *pcch = LwMin(cchMax, CchSt(qst));
    CopyPb(PrgchSt(qst), prgch, *pcch * size(achar));
}

/***************************************************************************
    Get the ith string.
***************************************************************************/
void VirtualStringTable::GetStn(LegacyLong istn, PString pstn)
{
    AssertThis(0);
    AssertIn(istn, 0, _ivMac);
    Assert(!FFree(istn), "string entry is free!");
    AssertPo(pstn, 0);

    pstn->SetSt(_Qst(istn));
}

/***************************************************************************
    Find the given stn in the string table.
***************************************************************************/
bool VirtualStringTable::FFindStn(PString pstn, LegacyLong *pistn, LegacyUlong grfgst)
{
    AssertThis(0);
    AssertPo(pstn, 0);

    return FFindRgch(pstn->Prgch(), pstn->Cch(), pistn, grfgst);
}

/***************************************************************************
    Search for the string in the string table.  This version does a linear
    search.  StringTable_GST overrides this to do a binary search if fgstSorted is
    passed in grfgst.
***************************************************************************/
bool VirtualStringTable::FFindRgch(achar *prgch, LegacyLong cch, LegacyLong *pistn, LegacyUlong grfgst)
{
    AssertThis(0);
    auto storage = _StringStorage();
    ThreeMMU32 position = storage.count;
    const bool found = three_mm_strings_find(&storage, prgch, cch, nullptr, nullptr, &position) != 0;
    *pistn = position;
    return found;
}

/***************************************************************************
    Find the string with the given extra data in the string table.
***************************************************************************/
bool VirtualStringTable::FFindExtra(void *prgbFind, PString pstn, LegacyLong *pistn)
{
    AssertThis(0);
    AssertPvCb(prgbFind, CbExtra());
    AssertNilOrPo(pstn, 0);
    AssertNilOrVarMem(pistn);

    LegacyLong istn;
    byte *qbExtra;
    LegacyLong cbExtra = CbExtra();
    LegacyLong ivMac = IvMac();

    if (cbExtra == 0)
    {
        Bug("no extra data");
        TrashVar(pistn);
        return fFalse;
    }

    for (istn = 0; istn < ivMac; istn++)
    {
        qbExtra = (byte *)_Qbst(istn) + size(LegacyLong);
        if (FEqualRgb(prgbFind, qbExtra, cbExtra))
        {
            if (pstn != pvNil)
                GetStn(istn, pstn);
            if (pistn != pvNil)
                *pistn = istn;
            return fTrue;
        }
    }

    if (pvNil != pstn)
        pstn->SetNil();
    TrashVar(pistn);
    return fFalse;
}

/***************************************************************************
    Fetch the extra data for element istn.
***************************************************************************/
void VirtualStringTable::GetExtra(LegacyLong istn, void *pv)
{
    AssertThis(0);
    AssertIn(istn, 0, _ivMac);
    Assert(!FFree(istn), "string entry is free!");
    Assert(_cbEntry > size(LegacyLong), "no extra data");
    AssertPvCb(pv, _cbEntry - size(LegacyLong));

    byte *qb;

    qb = (byte *)_Qbst(istn) + size(LegacyLong);
    CopyPb(qb, pv, _cbEntry - size(LegacyLong));
}

/***************************************************************************
    Put the extra data for element istn.
***************************************************************************/
void VirtualStringTable::PutExtra(LegacyLong istn, void *pv)
{
    AssertThis(0);
    AssertIn(istn, 0, _ivMac);
    Assert(!FFree(istn), "string entry is free!");
    Assert(_cbEntry > size(LegacyLong), "no extra data");
    AssertPvCb(pv, _cbEntry - size(LegacyLong));

    byte *qb;

    qb = (byte *)_Qbst(istn) + size(LegacyLong);
    CopyPb(pv, qb, _cbEntry - size(LegacyLong));
    AssertThis(0);
}

/***************************************************************************
    Return a volatile pointer to the string given the ibst (not the bst).
***************************************************************************/
achar *VirtualStringTable::_Qst(LegacyLong ibst)
{
    AssertIn(ibst, 0, _ivMac);
    LegacyLong bst = _Bst(ibst);
    AssertIn(bst, 0, _bstMac);
    return (achar *)_Qb1(bst);
}





/***************************************************************************
    Swap the bytes in the BST values.  Note that each bst is followed
    by the extra data, so we can't just use SwapBytesRglw.
***************************************************************************/
void VirtualStringTable::_SwapBytesRgbst(void)
{
    auto storage = _StringStorage();
    three_mm_strings_swap_offsets(&storage);
}

/***************************************************************************
    Translate the strings to/from the platform osk.  This only works if
    CbCharOsk(osk) == CbCharOsk(koskCur) (it asserts otherwise).
***************************************************************************/
void VirtualStringTable::_TranslateGrst(short osk, bool fToCur)
{
    auto storage = _StringStorage();
    AssertDo(three_mm_strings_translate_same(&storage, osk, koskCur, fToCur), "invalid string translation");
}

/***************************************************************************
    Translate the strings to the current osk.
***************************************************************************/
bool VirtualStringTable::_FTranslateGrst(short osk)
{
    auto storage = _StringStorage();
    const bool ok = three_mm_strings_translate(&storage, osk, koskCur, ConvertStringStorage, nullptr) != 0;
    _ApplyStringStorage(storage);
    if (ok && CbCharOsk(osk) != size(achar))
        AssertDo(_FEnsureSizes(_bstMac, LwMul(_ivMac, _cbEntry), fgrpShrink), 0);
    return ok;
}

/***************************************************************************
    Returns true iff ibst is out of range or the corresponding bst is
    bvNil.
***************************************************************************/
bool VirtualStringTable::FFree(LegacyLong istn)
{
    AssertIn(istn, 0, kcbMax);
    LegacyLong bst;

    if (!FIn(istn, 0, _ivMac))
        return fTrue;
    bst = _Bst(istn);
    Assert(bvNil == bst || FIn(bst, 0, _bstMac), "bad bst");
    return bvNil == bst;
}

#ifdef DEBUG
/***************************************************************************
    Validate a string table.
***************************************************************************/
void VirtualStringTable::AssertValid(LegacyUlong grfobj)
{
    LegacyLong ibst;
    LegacyLong cchTot, cbstFree;
    LegacyLong bst;

    VirtualStringTable_PAR::AssertValid(grfobj);
    AssertIn(_cbEntry, size(LegacyLong), kcbMax);
    Assert(_cbEntry % size(LegacyLong) == 0, "_cbEntry bad");
    AssertIn(_ivMac, 0, kcbMax);
    Assert(_Cb1() >= _bstMac, "grst area too small");
    Assert(_Cb2() >= LwMul(_ivMac, _cbEntry), "rgbst area too small");
    Assert(_cbstFree == cvNil || FIn(_cbstFree, 0, LwMax(1, _ivMac)), "_cbstFree is wrong");

    if (grfobj & fobjAssertFull)
    {
        // it would be nice to assert that no strings overlap each other,
        // but that's hard to do in linear time, so just assert that the
        // grst is the correct size.
        for (cbstFree = cchTot = ibst = 0; ibst < _ivMac; ibst++)
        {
            bst = _Bst(ibst);
            if (bvNil == bst)
            {
                Assert(ibst < _ivMac - 1, "last element free");
                cbstFree++;
                continue;
            }
            AssertIn(bst, 0, _bstMac);
            AssertSt((achar *)_Qb1(bst));
            cchTot += CchTotSt((PPascalString)_Qb1(bst));
        }
        Assert(cchTot * size(achar) == _bstMac, "grst wrong size");
        Assert(cbstFree == _cbstFree || _cbstFree == cvNil && cbstFree == 0, "bad _cbstFree");
    }
}
#endif // DEBUG

/***************************************************************************
    Allocate a new string table and ensure that it has space for cstnInit
    strings, totalling cchInit characters.
***************************************************************************/
PStringTable_GST StringTable_GST::PgstNew(LegacyLong cbExtra, LegacyLong cstnInit, LegacyLong cchInit)
{
    AssertIn(cbExtra, 0, kcbMax);
    Assert(cbExtra % size(LegacyLong) == 0, "cbExtra not multiple of size(long)");
    AssertIn(cstnInit, 0, kcbMax);
    AssertIn(cchInit, 0, kcbMax);
    PStringTable_GST pgst;

    if ((pgst = NewObj StringTable_GST(cbExtra)) == pvNil)
        return pvNil;
    if ((cstnInit > 0 || cchInit > 0) && !pgst->FEnsureSpace(cstnInit, cchInit, fgrpNil))
    {
        ReleasePpo(&pgst);
        return pvNil;
    }
    AssertPo(pgst, 0);
    return pgst;
}

/***************************************************************************
    Read a string table from a block and return it.
***************************************************************************/
PStringTable_GST StringTable_GST::PgstRead(PDataBlock pblck, short *pbo, short *posk)
{
    AssertPo(pblck, 0);
    AssertNilOrVarMem(pbo);
    AssertNilOrVarMem(posk);

    PStringTable_GST pgst;

    if ((pgst = NewObj StringTable_GST(0)) == pvNil)
        goto LFail;
    if (!pgst->_FRead(pblck, pbo, posk))
    {
        ReleasePpo(&pgst);
    LFail:
        TrashVar(pbo);
        TrashVar(posk);
        return pvNil;
    }
    AssertPo(pgst, 0);
    return pgst;
}

/***************************************************************************
    Read a string table from file and return it.
***************************************************************************/
PStringTable_GST StringTable_GST::PgstRead(PFileObject pfil, FilePosition fp, LegacyLong cb, short *pbo, short *posk)
{
    DataBlock blck(pfil, fp, cb);
    return PgstRead(&blck, pbo, posk);
}

/***************************************************************************
    Duplicate this StringTable_GST.
***************************************************************************/
PStringTable_GST StringTable_GST::PgstDup(void)
{
    AssertThis(0);
    PStringTable_GST pgst;

    if (pvNil == (pgst = PgstNew(_cbEntry - size(LegacyLong))))
        return pvNil;

    if (!_FDup(pgst))
        ReleasePpo(&pgst);

    AssertNilOrPo(pgst, 0);
    return pgst;
}

/***************************************************************************
    Append a string to the string table.
***************************************************************************/
bool StringTable_GST::FAddRgch(achar *prgch, LegacyLong cch, void *pvExtra, LegacyLong *pistn)
{
    AssertThis(0);
    AssertIn(cch, 0, kcchMaxGst + 1);
    AssertPvCb(prgch, cch * size(achar));
    AssertNilOrPvCb(pvExtra, _cbEntry - size(LegacyLong));
    AssertNilOrVarMem(pistn);

    if (pistn != pvNil)
        *pistn = _ivMac;
    if (!FInsertRgch(_ivMac, prgch, cch, pvExtra))
    {
        TrashVar(pistn);
        return fFalse;
    }
    return fTrue;
}

/***************************************************************************
    Find the given string and put its location in *pistn.  If it's not
    there, fill *pistn with where it would be.  If fgstSorted or fgstUserSorted
    is passed in, this does a binary search for the string; otherwise it
    does a linear search.
***************************************************************************/
bool StringTable_GST::FFindRgch(achar *prgch, LegacyLong cch, LegacyLong *pistn, LegacyUlong grfgst)
{
    AssertThis(0);
    auto storage = _StringStorage();
    ThreeMMU32 position = storage.count;
    const bool sorted = (grfgst & (fgstSorted | fgstUserSorted)) != 0;
    const bool found = three_mm_strings_find(&storage, prgch, cch, sorted ? CompareStringStorage : nullptr,
                                             &grfgst, &position) != 0;
    *pistn = position;
    return found;
}

/***************************************************************************
    Insert a new entry into the string text.
***************************************************************************/
bool StringTable_GST::FInsertRgch(LegacyLong istn, achar *prgch, LegacyLong cch, void *pvExtra)
{
    AssertThis(fobjAssertFull);
    auto storage = _StringStorage();
    const bool ok = three_mm_strings_insert(&storage, istn, prgch, cch, pvExtra) != 0;
    _ApplyStringStorage(storage);
    AssertThis(fobjAssertFull);
    return ok;
}

/***************************************************************************
    Insert an stn into the string table
***************************************************************************/
bool StringTable_GST::FInsertStn(LegacyLong istn, PString pstn, void *pvExtra)
{
    AssertThis(0);
    AssertIn(istn, 0, _ivMac + 1);
    AssertPo(pstn, 0);
    AssertNilOrPvCb(pvExtra, _cbEntry - size(LegacyLong));

    return FInsertRgch(istn, pstn->Prgch(), pstn->Cch(), pvExtra);
}

/***************************************************************************
    Delete the string at location istn.
***************************************************************************/
void StringTable_GST::Delete(LegacyLong istn)
{
    AssertThis(fobjAssertFull);
    auto storage = _StringStorage();
    AssertDo(three_mm_strings_delete(&storage, istn), "invalid string deletion");
    _ApplyStringStorage(storage);
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
void StringTable_GST::Move(LegacyLong ivSrc, LegacyLong ivTarget)
{
    AssertThis(0);
    auto storage = _StringStorage();
    AssertDo(three_mm_strings_move(&storage, ivSrc, ivTarget), "invalid string move");
    AssertThis(0);
}

#ifdef DEBUG
/***************************************************************************
    Validate a string table.
***************************************************************************/
void StringTable_GST::AssertValid(LegacyUlong grfobj)
{
    StringTable_GST_PAR::AssertValid(grfobj);
    AssertVar(_cbstFree == cvNil, "bad _cbstFree in StringTable_GST", &_cbstFree);
}
#endif // DEBUG

/***************************************************************************
    Allocate a new allocated string table and ensure that it has space for
    cstnInit strings, totalling cchInit characters.
***************************************************************************/
PAllocatedStringTable AllocatedStringTable::PastNew(LegacyLong cbExtra, LegacyLong cstnInit, LegacyLong cchInit)
{
    AssertIn(cbExtra, 0, kcbMax);
    Assert(cbExtra % size(LegacyLong) == 0, "cbExtra not multiple of size(long)");
    AssertIn(cstnInit, 0, kcbMax);
    AssertIn(cchInit, 0, kcbMax);
    PAllocatedStringTable past;

    if ((past = NewObj AllocatedStringTable(cbExtra)) == pvNil)
        return pvNil;
    if ((cstnInit > 0 || cchInit > 0) && !past->FEnsureSpace(cstnInit, cchInit, fgrpNil))
    {
        ReleasePpo(&past);
        return pvNil;
    }
    AssertPo(past, 0);
    return past;
}

/***************************************************************************
    Read an allocated string table from a block and return it.
***************************************************************************/
PAllocatedStringTable AllocatedStringTable::PastRead(PDataBlock pblck, short *pbo, short *posk)
{
    AssertPo(pblck, 0);
    AssertNilOrVarMem(pbo);
    AssertNilOrVarMem(posk);

    PAllocatedStringTable past;

    if ((past = NewObj AllocatedStringTable(0)) == pvNil)
        goto LFail;
    if (!past->_FRead(pblck, pbo, posk))
    {
        ReleasePpo(&past);
    LFail:
        TrashVar(pbo);
        TrashVar(posk);
        return pvNil;
    }
    AssertPo(past, 0);
    return past;
}

/***************************************************************************
    Read an allocated string table from file and return it.
***************************************************************************/
PAllocatedStringTable AllocatedStringTable::PastRead(PFileObject pfil, FilePosition fp, LegacyLong cb, short *pbo, short *posk)
{
    DataBlock blck;
    return PastRead(&blck, pbo, posk);
}

/***************************************************************************
    Duplicate this AllocatedStringTable.
***************************************************************************/
PAllocatedStringTable AllocatedStringTable::PastDup(void)
{
    AssertThis(0);
    PAllocatedStringTable past;

    if (pvNil == (past = PastNew(_cbEntry - size(LegacyLong))))
        return pvNil;

    if (!_FDup(past))
        ReleasePpo(&past);

    AssertNilOrPo(past, 0);
    return past;
}

/***************************************************************************
    Append a string to the allocated string table.
***************************************************************************/
bool AllocatedStringTable::FAddRgch(achar *prgch, LegacyLong cch, void *pvExtra, LegacyLong *pistn)
{
    AssertThis(fobjAssertFull);
    auto storage = _StringStorage();
    ThreeMMU32 position;
    const bool ok = three_mm_strings_add(&storage, prgch, cch, pvExtra, &position) != 0;
    _ApplyStringStorage(storage);
    if (ok && pistn) *pistn = position;
    TrashVarIf(!ok, pistn);
    AssertThis(fobjAssertFull);
    return ok;
}

/***************************************************************************
    Delete the string at location istn.
***************************************************************************/
void AllocatedStringTable::Delete(LegacyLong istn)
{
    AssertThis(fobjAssertFull);
    auto storage = _StringStorage();
    AssertDo(three_mm_strings_delete(&storage, istn), "invalid string deletion");
    _ApplyStringStorage(storage);
    AssertThis(fobjAssertFull);
}

#ifdef DEBUG
/***************************************************************************
    Validate a string table.
***************************************************************************/
void AllocatedStringTable::AssertValid(LegacyUlong grfobj)
{
    AllocatedStringTable_PAR::AssertValid(grfobj);
    AssertIn(_cbstFree, 0, LwMax(1, _ivMac));
}
#endif // DEBUG

} // end of namespace Group
