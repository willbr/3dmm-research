/* Copyright (c) Microsoft Corporation.
   Licensed under the MIT License. */

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
#ifndef GROUPS_H
#define GROUPS_H
#include "three_mm/string_table.h"
#include "three_mm/collections.h"

namespace Group {

ThreeMMStorageAllocator CollectionAllocator();

enum
{
    fgrpNil = 0,
    fgrpShrink = 1,
};

/****************************************
    GroupBase is a virtual class supporting
    all group classes
****************************************/
#define GroupBase_PAR BASE
#define kclsGroupBase 'GRPB'
class GroupBase : public GroupBase_PAR
{
    RTCLASS_DEC
    ASSERT
    MARKMEM

  private:
    ThreeMMCollectionStorage _storage;
    void _SetStorageGrowth();

  protected:
    LegacyLong _cbMinGrow1;
    LegacyLong _cbMinGrow2;
    LegacyLong _ivMac;

    byte *_Qb1(LegacyLong ib)
    {
        return _storage.buffers[0].data ? _storage.buffers[0].data + ib : nullptr;
    }
    byte *_Qb2(LegacyLong ib)
    {
        return _storage.buffers[1].data ? _storage.buffers[1].data + ib : nullptr;
    }
    LegacyLong _Cb1(void)
    {
        return _storage.buffers[0].capacity;
    }
    LegacyLong _Cb2(void)
    {
        return _storage.buffers[1].capacity;
    }
    ThreeMMCollectionStorage *_CollectionStorage()
    {
        _SetStorageGrowth();
        return &_storage;
    }
    bool _ReadCollection(PDataBlock, int kind, bool allocated, ThreeMMCollectionHeader *, short *, short *);
    bool _WriteCollection(PDataBlock, ThreeMMCollectionHeader, short bo, short osk);
    bool _FEnsureSizes(LegacyLong cbMin1, LegacyLong cbMin2, LegacyUlong grfgrp);
    bool _FWrite(PDataBlock pblck, void *pv, LegacyLong cb, LegacyLong cb1, LegacyLong cb2);
    bool _FReadData(PDataBlock pblck, LegacyLong cb1, LegacyLong cb2, LegacyLong ib);
    bool _FDup(PGroupBase pgrpbDst, LegacyLong cb1, LegacyLong cb2);

    GroupBase(void);

  public:
    ~GroupBase(void);

    void Lock(void);
    void Unlock(void);
    LegacyLong IvMac(void)
    {
        return _ivMac;
    }
    virtual bool FFree(LegacyLong iv) = 0;
    virtual void Delete(LegacyLong iv) = 0;

    // writing
    virtual bool FWriteFlo(PFileLocation pflo, short bo = kboCur, short osk = koskCur);
    virtual bool FWrite(PDataBlock pblck, short bo = kboCur, short osk = koskCur) = 0;
    virtual LegacyLong CbOnFile(void) = 0;
};

/****************************************
    VirtualArray is a virtual class supporting
    DynamicArray and AllocatedArray
****************************************/
#define VirtualArray_PAR GroupBase
#define kclsVirtualArray 'GLB'
class VirtualArray : public VirtualArray_PAR
{
    RTCLASS_DEC
    ASSERT

  protected:
    LegacyLong _cbEntry;

    ThreeMMArray _ArrayView(bool allocated = false, LegacyLong freeCount = 0);
    void _ApplyArray(const ThreeMMArray &);
    VirtualArray(LegacyLong cb);

  public:
    LegacyLong CbEntry(void)
    {
        return _cbEntry;
    }
    void *QvGet(LegacyLong iv);
    void Get(LegacyLong iv, void *pv);
    void Put(LegacyLong iv, void *pv);
    void *PvLock(LegacyLong iv);
    void SetMinGrow(LegacyLong cvAdd);

    virtual bool FAdd(void *pv, LegacyLong *piv = pvNil) = 0;
};

/****************************************
    DynamicArray is the basic dynamic array
****************************************/
#define DynamicArray_PAR VirtualArray
#define kclsDynamicArray 'GL'
class DynamicArray : public DynamicArray_PAR
{
    RTCLASS_DEC

  protected:
    DynamicArray(LegacyLong cb);
    bool _FRead(PDataBlock pblck, short *pbo, short *posk);

  public:
    // static methods
    static PDynamicArray PglNew(LegacyLong cb, LegacyLong cvInit = 0);
    static PDynamicArray PglRead(PDataBlock pblck, short *pbo = pvNil, short *posk = pvNil);
    static PDynamicArray PglRead(PFileObject pfil, FilePosition fp, LegacyLong cb, short *pbo = pvNil, short *posk = pvNil);

    // duplication
    PDynamicArray PglDup(void);

    // methods required by parent class
    virtual bool FAdd(void *pv, LegacyLong *piv = pvNil);
    virtual void Delete(LegacyLong iv);
    virtual bool FWrite(PDataBlock pblck, short bo = kboCur, short osk = koskCur);
    virtual LegacyLong CbOnFile(void);
    virtual bool FFree(LegacyLong iv);

    // new methods
    void Delete(LegacyLong iv, LegacyLong cv);
    bool FInsert(LegacyLong iv, void *pv = pvNil, LegacyLong cv = 1);
    bool FSetIvMac(LegacyLong ivMacNew);
    bool FEnsureSpace(LegacyLong cvAdd, LegacyUlong grfgrp = fgrpNil);
    void Move(LegacyLong ivSrc, LegacyLong ivTarget);
    bool FPush(void *pv)
    {
        return FInsert(_ivMac, pv);
    }
    bool FPop(void *pv = pvNil);
    bool FEnqueue(void *pv)
    {
        return FInsert(0, pv);
    }
    bool FDequeue(void *pv = pvNil)
    {
        return FPop(pv);
    }
};

/****************************************
    Allocated (fixed index) list class
****************************************/
#define AllocatedArray_PAR VirtualArray
#define kclsAllocatedArray 'AL'
class AllocatedArray : public AllocatedArray_PAR
{
    RTCLASS_DEC
    ASSERT

  private:
    LegacyLong _cvFree;

  private:
    // section 2 of the data contains a bit array
    byte *_Qgrfbit(LegacyLong iv)
    {
        return _Qb2(IbFromIbit(iv));
    }

  protected:
    AllocatedArray(LegacyLong cb);
    bool _FRead(PDataBlock pblck, short *pbo, short *posk);

  public:
    // static methods
    static PAllocatedArray PalNew(LegacyLong cb, LegacyLong cvInit = 0);
    static PAllocatedArray PalRead(PDataBlock pblck, short *pbo = pvNil, short *posk = pvNil);
    static PAllocatedArray PalRead(PFileObject pfil, FilePosition fp, LegacyLong cb, short *pbo = pvNil, short *posk = pvNil);

    // duplication
    PAllocatedArray PalDup(void);

    // methods required by parent class
    virtual bool FAdd(void *pv, LegacyLong *piv = pvNil);
    virtual void Delete(LegacyLong iv);
    virtual bool FWrite(PDataBlock pblck, short bo = kboCur, short osk = koskCur);
    virtual LegacyLong CbOnFile(void);
    virtual bool FFree(LegacyLong iv);

    // new methods
    bool FEnsureSpace(LegacyLong cvAdd, LegacyUlong grfgrp = fgrpNil);
    void DeleteAll(void);
};

/****************************************
    VirtualGroup is a virtual class supporting
    GeneralGroup and AllocatedGroup
****************************************/
const ByteOrderMask kbomLoc = 0xF0000000;
#define VirtualGroup_PAR GroupBase
#define kclsVirtualGroup 'GGB'
class VirtualGroup : public VirtualGroup_PAR
{
    RTCLASS_DEC
    ASSERT

  protected:
    // Per-entry descriptor inside GG variable storage. Fixed 8-byte wire format
    // on every architecture; sits between the GeneralGroupOnFile header and the
    // entries' variable bytes. Bare `long` would silently widen on LP64 and
    // shift every GG variable-storage layout.
    struct LogicalOffsetAndCount
    {
        int32_t bv;
        int32_t cb;
    };
    static_assert(sizeof(LogicalOffsetAndCount) == 8, "LogicalOffsetAndCount wire format drift");

    ThreeMMGroup _GroupView();
    void _ApplyGroup(const ThreeMMGroup &);
    LegacyLong _bvMac;
    LegacyLong _clocFree;
    LegacyLong _cbFixed;

  protected:
    VirtualGroup(LegacyLong cbFixed, bool fAllowFree);

    LogicalOffsetAndCount *_Qloc(LegacyLong iloc)
    {
        return (LogicalOffsetAndCount *)_Qb2(LwMul(iloc, size(LogicalOffsetAndCount)));
    }
    bool _FRead(PDataBlock pblck, short *pbo, short *posk);

    bool _FDup(PVirtualGroup pggbDst);

  public:
    // methods required by parent class
    virtual bool FWrite(PDataBlock pblck, short bo = kboCur, short osk = koskCur);
    virtual LegacyLong CbOnFile(void);
    virtual bool FFree(LegacyLong iv);

    bool FEnsureSpace(LegacyLong cvAdd, LegacyLong cbAdd, LegacyUlong grfgrp = fgrpNil);
    void SetMinGrow(LegacyLong cvAdd, LegacyLong cbAdd);

    virtual bool FAdd(LegacyLong cb, LegacyLong *piv = pvNil, void *pv = pvNil, void *pvFixed = pvNil) = 0;

    // access to the fixed portion
    LegacyLong CbFixed(void)
    {
        return _cbFixed;
    }
    void *QvFixedGet(LegacyLong iv, LegacyLong *pcbVar = pvNil);
    void *PvFixedLock(LegacyLong iv, LegacyLong *pcbVar = pvNil);
    void GetFixed(LegacyLong iv, void *pv);
    void PutFixed(LegacyLong iv, void *pv);

    // access to the variable portion
    LegacyLong Cb(LegacyLong iv);
    void *QvGet(LegacyLong iv, LegacyLong *pcb = pvNil);
    void *PvLock(LegacyLong iv, LegacyLong *pcb = pvNil);
    void Get(LegacyLong iv, void *pv);
    void Put(LegacyLong iv, void *pv);
    bool FPut(LegacyLong iv, LegacyLong cb, void *pv);
    void GetRgb(LegacyLong iv, LegacyLong bv, LegacyLong cb, void *pv);
    void PutRgb(LegacyLong iv, LegacyLong bv, LegacyLong cb, void *pv);
    void DeleteRgb(LegacyLong iv, LegacyLong bv, LegacyLong cb);
    bool FInsertRgb(LegacyLong iv, LegacyLong bv, LegacyLong cb, void *pv);
    bool FMoveRgb(LegacyLong ivSrc, LegacyLong bvSrc, LegacyLong ivDst, LegacyLong bvDst, LegacyLong cb);
    void Merge(LegacyLong ivSrc, LegacyLong ivDst);
};

/****************************************
    General Group - based on VirtualGroup
****************************************/
#define GeneralGroup_PAR VirtualGroup
#define kclsGeneralGroup 'GG'
class GeneralGroup : public GeneralGroup_PAR
{
    RTCLASS_DEC
    ASSERT

  protected:
    GeneralGroup(LegacyLong cbFixed) : VirtualGroup(cbFixed, fFalse)
    {
    }

  public:
    // static methods
    static PGeneralGroup PggNew(LegacyLong cbFixed = 0, LegacyLong cvInit = 0, LegacyLong cbInit = 0);
    static PGeneralGroup PggRead(PDataBlock pblck, short *pbo = pvNil, short *posk = pvNil);
    static PGeneralGroup PggRead(PFileObject pfil, FilePosition fp, LegacyLong cb, short *pbo = pvNil, short *posk = pvNil);

    // duplication
    PGeneralGroup PggDup(void);

    // methods required by parent class
    virtual bool FAdd(LegacyLong cb, LegacyLong *piv = pvNil, void *pv = pvNil, void *pvFixed = pvNil);
    virtual void Delete(LegacyLong iv);

    // new methods
    bool FInsert(LegacyLong iv, LegacyLong cb, void *pv = pvNil, void *pvFixed = pvNil);
    bool FCopyEntries(PGeneralGroup pggSrc, LegacyLong ivSrc, LegacyLong ivDst, LegacyLong cv);
    void Move(LegacyLong ivSrc, LegacyLong ivTarget);
    void Swap(LegacyLong iv1, LegacyLong iv2);
};

/****************************************
    Allocated Group - based on VirtualGroup
****************************************/
#define AllocatedGroup_PAR VirtualGroup
#define kclsAllocatedGroup 'AG'
class AllocatedGroup : public AllocatedGroup_PAR
{
    RTCLASS_DEC
    ASSERT

  protected:
    AllocatedGroup(LegacyLong cbFixed) : VirtualGroup(cbFixed, fTrue)
    {
    }

  public:
    // static methods
    static PAllocatedGroup PagNew(LegacyLong cbFixed = 0, LegacyLong cvInit = 0, LegacyLong cbInit = 0);
    static PAllocatedGroup PagRead(PDataBlock pblck, short *pbo = pvNil, short *posk = pvNil);
    static PAllocatedGroup PagRead(PFileObject pfil, FilePosition fp, LegacyLong cb, short *pbo = pvNil, short *posk = pvNil);

    // duplication
    PAllocatedGroup PagDup(void);

    // methods required by parent class
    virtual bool FAdd(LegacyLong cb, LegacyLong *piv = pvNil, void *pv = pvNil, void *pvFixed = pvNil);
    virtual void Delete(LegacyLong iv);
};

/****************************************
    String table classes
****************************************/
enum
{
    fgstNil = 0,
    fgstSorted = 1,
    fgstUserSorted = 2,
    fgstAllowFree = 4,
};

const LegacyLong kcchMaxGst = kcchMaxStn;

/****************************************
    VirtualStringTable is a virtual class supporting
    StringTable_GST and AllocatedStringTable.
****************************************/
#define VirtualStringTable_PAR GroupBase
#define kclsVirtualStringTable 'GSTB'
class VirtualStringTable : public VirtualStringTable_PAR
{
    RTCLASS_DEC
    ASSERT

  protected:
    LegacyLong _cbEntry;
    LegacyLong _bstMac;
    LegacyLong _cbstFree; // this is cvNil for non-allocated GSTBs

    ThreeMMStringTable _StringStorage();
    void _ApplyStringStorage(const ThreeMMStringTable &storage);
    static int _ReserveStringStorage(void *, ThreeMMStringTable *, ThreeMMU32, ThreeMMU32);

  protected:
    VirtualStringTable(LegacyLong cbExtra, LegacyUlong grfgst);

    LegacyLong _Bst(LegacyLong ibst)
    {
        return *(LegacyLong *)_Qb2(LwMul(ibst, _cbEntry));
    }
    LegacyLong *_Qbst(LegacyLong ibst)
    {
        return (LegacyLong *)_Qb2(LwMul(ibst, _cbEntry));
    }
    PPascalString _Qst(LegacyLong ibst);
    void _SwapBytesRgbst(void);
    void _TranslateGrst(short osk, bool fToCur);
    bool _FTranslateGrst(short osk);
    bool _FRead(PDataBlock pblck, short *pbo, short *posk);

    bool _FDup(PVirtualStringTable pgstbDst);

  public:
    // methods required by parent class
    virtual bool FWrite(PDataBlock pblck, short bo = kboCur, short osk = koskCur);
    virtual LegacyLong CbOnFile(void);
    virtual bool FFree(LegacyLong istn);

    bool FEnsureSpace(LegacyLong cstnAdd, LegacyLong cchAdd, LegacyUlong grfgrp = fgrpNil);
    void SetMinGrow(LegacyLong cstnAdd, LegacyLong cchAdd);

    virtual bool FAddRgch(achar *prgch, LegacyLong cch, void *pvExtra = pvNil, LegacyLong *pistn = pvNil) = 0;
    virtual bool FFindRgch(achar *prgch, LegacyLong cch, LegacyLong *pistn, LegacyUlong grfgst = fgstNil);

    LegacyLong IstnMac(void)
    {
        return _ivMac;
    }
    LegacyLong CbExtra(void)
    {
        return _cbEntry - size(LegacyLong);
    }

    bool FAddStn(PString pstn, void *pvExtra = pvNil, LegacyLong *pistn = pvNil);
    bool FPutRgch(LegacyLong istn, achar *prgch, LegacyLong cch);
    bool FPutStn(LegacyLong istn, PString pstn);
    void GetRgch(LegacyLong istn, achar *prgch, LegacyLong cchMax, LegacyLong *pcch);
    void GetStn(LegacyLong istn, PString pstn);
    bool FFindStn(PString pstn, LegacyLong *pistn, LegacyUlong grfgst = fgstNil);

    void GetExtra(LegacyLong istn, void *pv);
    void PutExtra(LegacyLong istn, void *pv);
    bool FFindExtra(void *prgbFind, PString pstn = pvNil, LegacyLong *pistn = pvNil);
};

/****************************************
    String table
****************************************/
#define StringTable_GST_PAR VirtualStringTable
#define kclsStringTable_GST 'GST'
class StringTable_GST : public StringTable_GST_PAR
{
    RTCLASS_DEC
    ASSERT

  protected:
    StringTable_GST(LegacyLong cbExtra) : VirtualStringTable(cbExtra, fgstNil)
    {
    }

  public:
    // static methods
    static PStringTable_GST PgstNew(LegacyLong cbExtra = 0, LegacyLong cstnInit = 0, LegacyLong cchInit = 0);
    static PStringTable_GST PgstRead(PDataBlock pblck, short *pbo = pvNil, short *posk = pvNil);
    static PStringTable_GST PgstRead(PFileObject pfil, FilePosition fp, LegacyLong cb, short *pbo = pvNil, short *posk = pvNil);

    // duplication
    PStringTable_GST PgstDup(void);

    // methods required by parent class
    virtual bool FAddRgch(achar *prgch, LegacyLong cch, void *pvExtra = pvNil, LegacyLong *pistn = pvNil);
    virtual bool FFindRgch(achar *prgch, LegacyLong cch, LegacyLong *pistn, LegacyUlong grfgst = fgstNil);
    virtual void Delete(LegacyLong istn);

    // new methods
    bool FInsertRgch(LegacyLong istn, achar *prgch, LegacyLong cch, void *pvExtra = pvNil);
    bool FInsertStn(LegacyLong istn, PString pstn, void *pvExtra = pvNil);
    void Move(LegacyLong istnSrc, LegacyLong istnDst);
};

/****************************************
    Allocated string table
****************************************/
#define AllocatedStringTable_PAR VirtualStringTable
#define kclsAllocatedStringTable 'AST'
class AllocatedStringTable : public AllocatedStringTable_PAR
{
    RTCLASS_DEC
    ASSERT

  protected:
    AllocatedStringTable(LegacyLong cbExtra) : VirtualStringTable(cbExtra, fgstAllowFree)
    {
    }

  public:
    // static methods
    static PAllocatedStringTable PastNew(LegacyLong cbExtra = 0, LegacyLong cstnInit = 0, LegacyLong cchInit = 0);
    static PAllocatedStringTable PastRead(PDataBlock pblck, short *pbo = pvNil, short *posk = pvNil);
    static PAllocatedStringTable PastRead(PFileObject pfil, FilePosition fp, LegacyLong cb, short *pbo = pvNil, short *posk = pvNil);

    // duplication
    PAllocatedStringTable PastDup(void);

    // methods required by parent class
    virtual bool FAddRgch(achar *prgch, LegacyLong cch, void *pvExtra = pvNil, LegacyLong *pistn = pvNil);
    virtual void Delete(LegacyLong istn);
};

} // end of namespace Group

#endif //! GROUPS_H
