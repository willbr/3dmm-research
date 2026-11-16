/* Copyright (c) Microsoft Corporation.
   Licensed under the MIT License. */

/***************************************************************************
    Author: ShonK
    Project: Kauai
    Reviewed:
    Copyright (c) Microsoft Corporation

    Base classes.  Base class that all other classes are derived from and
    a base linked list class for implementing singly linked lists.

***************************************************************************/
#include "win_headers.h"
#include "util.h"
#include "mm/object_memory.h"
ASSERTNAME

#ifdef DEBUG
LegacyLong vcactSuspendAssertValid = 0;
LegacyLong vcactAVSave = 0;
LegacyLong vcactAV = kswMax;

/*****************************************************************************
    Debugging stuff to track leaked allocated objects.
******************************************************************************/
// Debugging object info.
const LegacyLong kclwStackDoi = 10;
struct alignas(std::max_align_t) DebugObjectInfo
{
    short swMagic;      // magic number == kswMagicMem
    short cactRef;      // for marking memory and asserting on unused objects
    PSZS pszsFile;      // file NewObj appears in
    LegacyLong lwLine;        // line NewObj appears on
    LegacyLong cbTot;         // total size of the block, including the DebugObjectInfo
    LegacyLong lwThread;      // thread that allocated this
    LegacyLong rglwStack[10]; // what we get from following the EBP/A6 chain
    DebugObjectInfo *pdoiNext;      // singly linked list
    DebugObjectInfo **ppdoiPrev;
};

#define kcbBaseDebug size(DebugObjectInfo)

priv void _AssertDoi(DebugObjectInfo *pdoi, bool tLinked);
inline DebugObjectInfo *_PdoiFromBase(void *pv)
{
    return (DebugObjectInfo *)PvSubBv(pv, kcbBaseDebug);
}
inline BASE *_PbaseFromDoi(DebugObjectInfo *pdoi)
{
    return (BASE *)PvAddBv(pdoi, kcbBaseDebug);
}
priv void _LinkDoi(DebugObjectInfo *pdoi, DebugObjectInfo **ppdoiFirst);
priv void _UnlinkDoi(DebugObjectInfo *pdoi);

DebugObjectInfo *_pdoiFirst;    // Head of linked list of all allocated objects.
DebugObjectInfo *_pdoiFirstRaw; // Head of linked list of raw newly allocated objects.

inline void _Enter(void)
{
    vmutxBase.Enter();
}
inline void _Leave(void)
{
    vmutxBase.Leave();
}

#define klwMagicAllocatedBase 'ALOC'
#define klwMagicNonAllocBase 'NOAL'

#else //! DEBUG
#define kcbBaseDebug 0
#endif //! DEBUG

RTCLASS(BaseLinkedList)

/***************************************************************************
    Returns the run-time class id (cls) of the class.
***************************************************************************/
LegacyLong BASE::Cls(void)
{
    return kclsBASE;
}

/***************************************************************************
    Returns true iff cls is kclsBASE.
***************************************************************************/
bool BASE::FIs(LegacyLong cls)
{
    return kclsBASE == cls;
}

/***************************************************************************
    Static method. Returns true iff cls is kclsBASE.
***************************************************************************/
bool BASE::FWouldBe(LegacyLong cls)
{
    return kclsBASE == cls;
}

/***************************************************************************
    Constructor for a BASE object.  Sets _references.count to 1 and in debug sets
    the magic number to indicate whether the thing was allocated.
***************************************************************************/
BASE::BASE(void)
{
    static_assert(sizeof(MMReference) == sizeof(LegacyLong), "native reference storage size");
    mm_reference_init(&_references);
#ifdef DEBUG
    DebugObjectInfo *pdoi = _pdoiFirstRaw;

    if (pvNil != pdoi)
    {
        // Note that we have to check _pdoiFirstRaw before entering the
        // mutx so we don't try to enter the mutx before it's been
        // initialized (during global object construction).
        _Enter();

        // see if this is in the raw list - note that we have to refresh
        // _pdoiFirstRaw in case another thread grabbed it before we got
        // the critical section
        for (pdoi = _pdoiFirstRaw; pdoi != pvNil && this != _PbaseFromDoi(pdoi); pdoi = pdoi->pdoiNext)
        {
            _AssertDoi(pdoi, tYes);
        }

        if (pvNil != pdoi)
        {
            // this is us!
            _UnlinkDoi(pdoi);
            _LinkDoi(pdoi, &_pdoiFirst);

            _lwMagic = klwMagicAllocatedBase;
            AssertValid(0);
        }

        _Leave();
    }

    if (pvNil == pdoi)
    {
        _lwMagic = klwMagicNonAllocBase;
        // don't call AssertValid here, since this may be during
        // global initialization (FAssertProc and/or AssertPvCb may not be
        // callable).
    }

#endif // DEBUG
}

/***************************************************************************
    Increments the reference count.
***************************************************************************/
void BASE::AddRef(void)
{
    AssertThis(0);
    mm_reference_add(&_references);

    // NOTE: some classes allow _references.count == 0, so we can't assert _references.count > 1
    Assert(_references.count > 0, "reference count not positive");
}

/***************************************************************************
    Decrement the reference count and delete it if the reference count goes
    to zero.
***************************************************************************/
void BASE::Release(void)
{
    AssertThis(0);
    mm_reference_release(&_references, this, [](void *p) {
        auto object = static_cast<BASE *>(p);
        AssertPo(object, fobjAllocated);
        delete object;
    });
}

namespace
{
void *ObjectAllocate(void *, size_t bytes, int clear) { return KauaiPlatform::Allocate(bytes, clear != 0); }
void ObjectRelease(void *, void *raw) { KauaiPlatform::Free(raw); }
void ObjectFailed(void *) { PushErc(ercOomNew); }
#ifdef DEBUG
struct ObjectSource { PSZS file; LegacyLong line; };
void ObjectEnter(void *) { _Enter(); }
void ObjectLeave(void *) { _Leave(); }
void ObjectStatsEnter(void *) { vmutxMem.Enter(); }
void ObjectStatsLeave(void *) { vmutxMem.Leave(); }
void ObjectAllocated(void *context, void *raw, size_t total)
{
    auto *source = static_cast<ObjectSource *>(context);
    auto *pdoi = static_cast<DebugObjectInfo *>(raw);
    pdoi->swMagic = kswMagicMem;
    pdoi->cactRef = 0;
    pdoi->pszsFile = source->file;
    pdoi->lwLine = source->line;
    pdoi->cbTot = static_cast<LegacyLong>(total);
    pdoi->lwThread = LwThreadCur();
    pdoi->ppdoiPrev = pvNil;
    _LinkDoi(pdoi, &_pdoiFirstRaw);
#ifdef WIN
#ifdef IN_80386
    // follow the EBP chain to capture a debug-mode allocation stack trace.
    LegacyLong *plw;
    LegacyLong ilw;

    __asm { mov plw,ebp }
    for (ilw = 0; ilw < kclwStackDoi; ilw++)
    {
        if (pvNil == plw || IsBadReadPtr(plw, 2 * size(LegacyLong)) || *plw <= (LegacyLong)plw)
        {
            pdoi->rglwStack[ilw] = 0;
            plw = pvNil;
        }
        else
        {
            pdoi->rglwStack[ilw] = plw[1];
            plw = (LegacyLong *)*plw;
        }
    }
#else
    // x64 doesn't allow inline asm. The diagnostic stack trace is
    // optional debug instrumentation; leave it empty on x64 for now.
    // CaptureStackBackTrace would be the proper Win32 replacement.
    for (LegacyLong ilw = 0; ilw < kclwStackDoi; ilw++)
        pdoi->rglwStack[ilw] = 0;
#endif // IN_80386
#endif // WIN
}
void ObjectBeforeFree(void *, void *raw) { _UnlinkDoi(static_cast<DebugObjectInfo *>(raw)); }
size_t ObjectSize(void *, void *raw) { return static_cast<DebugObjectInfo *>(raw)->cbTot; }
const MMObjectLayout objectLayout{ kcbBaseDebug, 1, kbGarbage };
#else
const MMObjectLayout objectLayout{ 0, 0, 0 };
#endif
MMObjectHost ObjectHost(void *context)
{
    MMObjectHost host{};
    host.context = context;
    host.allocate = ObjectAllocate;
    host.release = ObjectRelease;
    host.failed = ObjectFailed;
#ifdef DEBUG
    host.enter_objects = ObjectEnter;
    host.leave_objects = ObjectLeave;
    host.enter_stats = ObjectStatsEnter;
    host.leave_stats = ObjectStatsLeave;
    host.allocated = ObjectAllocated;
    host.before_free = ObjectBeforeFree;
    host.block_size = ObjectSize;
#endif
    return host;
}
} // namespace

/***************************************************************************
    Allocate cleared object storage through the shared C89 transaction policy.
***************************************************************************/
#ifdef DEBUG
void *BASE::operator new(size_t cb, PSZS pszsFile, LegacyLong lwLine) noexcept
#else
void *BASE::operator new(size_t cb) noexcept
#endif
{
    AssertVarMem(pszsFile);
#ifdef DEBUG
    ObjectSource source{pszsFile, lwLine};
    auto host = ObjectHost(&source);
    return mm_object_allocate(&objectLayout, &host, &vdmglob.dmaglBase, cb);
#else
    auto host = ObjectHost(nullptr);
    return mm_object_allocate(&objectLayout, &host, nullptr, cb);
#endif
}

void BASE::operator delete(void *pv)
{
    auto host = ObjectHost(nullptr);
#ifdef DEBUG
    mm_object_free(&objectLayout, &host, &vdmglob.dmaglBase, pv);
#else
    mm_object_free(&objectLayout, &host, nullptr, pv);
#endif
}

#ifdef DEBUG
void BASE::operator delete(void *pv, schar *pszsFile, LegacyLong lwLine)
{
    BASE::operator delete(pv);
}
#endif // DEBUG


#ifdef DEBUG
/***************************************************************************
    Assert the this pointer is valid.
***************************************************************************/
void BASE::AssertValid(LegacyUlong grfobj)
{
    AssertVarMem(this);
    AssertIn(_references.count, 0, kcbMax);
    if (_lwMagic != klwMagicAllocatedBase)
    {
        Assert(!(grfobj & fobjAllocated), "should be allocated");
        AssertVar(_lwMagic == klwMagicNonAllocBase, "_lwMagic wrong", &_lwMagic);
        AssertPvCb(this, size(BASE));
        return;
    }
    Assert(!(grfobj & fobjNotAllocated), "should not be allocated");

    _Enter();

    DebugObjectInfo *pdoi = _PdoiFromBase(this);
    _AssertDoi(pdoi, tYes);

    _Leave();
}

/***************************************************************************
    If this objects has already been marked, just return. If not, call
    its MarkMem method.
***************************************************************************/
void BASE::MarkMemStub(void)
{
    AssertThis(0);
    if (_lwMagic != klwMagicAllocatedBase)
    {
        AssertVar(_lwMagic == klwMagicNonAllocBase, "_lwMagic wrong", &_lwMagic);
        MarkMem();
        return;
    }

    DebugObjectInfo *pdoi = _PdoiFromBase(this);
    if (pdoi->cactRef == 0 && pdoi->lwThread == LwThreadCur())
        MarkMem();
}

/***************************************************************************
    Mark object.
***************************************************************************/
void BASE::MarkMem(void)
{
    AssertValid(0);
    if (_lwMagic == klwMagicAllocatedBase)
        _PdoiFromBase(this)->cactRef++;
}

/***************************************************************************
    Assert that a doi is valid and optionally linked or unlinked.
***************************************************************************/
void _AssertDoi(DebugObjectInfo *pdoi, bool tLinked)
{
    _Enter();

    AssertPvCb(pdoi, size(BASE) + kcbBaseDebug);
    AssertIn(pdoi->cbTot, size(BASE) + kcbBaseDebug, kcbMax);
    AssertPvCb(pdoi, pdoi->cbTot);

    AssertVar(pdoi->swMagic == kswMagicMem, "magic number has been hammered", &pdoi->swMagic);
    AssertVar(pdoi->cactRef >= 0, "negative reference count", &pdoi->cactRef);
    if (pvNil == pdoi->ppdoiPrev)
        Assert(tLinked != tYes, "should be linked");
    else
    {
        Assert(tLinked != tNo, "should NOT be linked");

        AssertVarMem(pdoi->ppdoiPrev);
        AssertVar(*pdoi->ppdoiPrev == pdoi, "*ppdoiPrev is wrong", pdoi->ppdoiPrev);
        if (pdoi->pdoiNext != pvNil)
        {
            AssertVarMem(pdoi->pdoiNext);
            AssertVar(pdoi->pdoiNext->ppdoiPrev == &pdoi->pdoiNext, "ppdoiPrev in next is wrong",
                      &pdoi->pdoiNext->ppdoiPrev);
        }
    }

    _Leave();
}

/***************************************************************************
    Link object into list.
***************************************************************************/
priv void _LinkDoi(DebugObjectInfo *pdoi, DebugObjectInfo **ppdoiFirst)
{
    _Enter();

    _AssertDoi(pdoi, tNo);
    AssertVarMem(ppdoiFirst);

    if (*ppdoiFirst != pvNil)
    {
        _AssertDoi(*ppdoiFirst, tYes);
        (*ppdoiFirst)->ppdoiPrev = &pdoi->pdoiNext;
    }
    pdoi->pdoiNext = *ppdoiFirst;
    pdoi->ppdoiPrev = ppdoiFirst;
    *ppdoiFirst = pdoi;
    _AssertDoi(pdoi, tYes);

    _Leave();
}

/***************************************************************************
    Unlink object from list.
***************************************************************************/
priv void _UnlinkDoi(DebugObjectInfo *pdoi)
{
    _Enter();

    _AssertDoi(pdoi, tYes);
    *pdoi->ppdoiPrev = pdoi->pdoiNext;
    if (pvNil != pdoi->pdoiNext)
    {
        pdoi->pdoiNext->ppdoiPrev = pdoi->ppdoiPrev;
        _AssertDoi(pdoi->pdoiNext, tYes);
    }
    pdoi->ppdoiPrev = pvNil;
    pdoi->pdoiNext = pvNil;
    _AssertDoi(pdoi, tNo);

    _Leave();
}

/***************************************************************************
    Called if anyone tries to copy a class with NOCOPY(cls) in its
    declaration.
***************************************************************************/
void __AssertOnCopy(void)
{
    Bug("Copying a non-copyable object");
}

/***************************************************************************
    Asserts on unmarked allocated (BASE) objects.
***************************************************************************/
void AssertUnmarkedObjs(void)
{
    _Enter();

    String stn;
    SZS szs;
    BASE *pbase;
    DebugObjectInfo *pdoi;
    DebugObjectInfo *pdoiLast;
    bool fAssert;
    LegacyLong cdoiLost = 0;
    LegacyLong lwThread = LwThreadCur();

    Assert(_pdoiFirstRaw == pvNil, "Raw list is not empty!");

    // we want to traverse the list in the reverse order to report problems
    // find the end of the list and see if there are any lost blocks
    pdoiLast = pvNil;
    for (pdoi = _pdoiFirst; pvNil != pdoi; pdoi = pdoi->pdoiNext)
    {
        pdoiLast = pdoi;
        if (pdoi->cactRef == 0 && pdoi->lwThread == lwThread)
            cdoiLost++;
    }

    if (cdoiLost == 0)
    {
        // no lost blocks
        goto LDone;
    }

    stn.FFormatSz(PszLit("Total lost objects: %d. Press 'Debugger' for detail"), cdoiLost);
    stn.GetSzs(szs);
    fAssert = FAssertProc(__szsFile, __LINE__, szs, pvNil, 0);

    for (pdoi = pdoiLast;;)
    {
        pbase = _PbaseFromDoi(pdoi);
        AssertPo(pbase, fobjAllocated);

        if (pdoi->cactRef == 0 && pdoi->lwThread == lwThread)
        {
            if (fAssert)
            {
                stn.FFormatSz(PszLit("\nLost object: cls='%f', size=%d, ") PszLit("StackTrace=(use map file)"),
                              pbase->Cls(), pdoi->cbTot - size(DebugObjectInfo));
                stn.GetSzs(szs);

                if (FAssertProc(pdoi->pszsFile, pdoi->lwLine, szs, pdoi->rglwStack, kclwStackDoi * size(LegacyLong)))
                {
                    Debugger();
                }
            }

            MarkMemObj(pbase);
        }

        if (pdoi->ppdoiPrev == &_pdoiFirst)
            break;

        // UUUUGGGGH! We don't have a pointer to the previous DebugObjectInfo, we
        // have a pointer to the previous DebugObjectInfo's pdoiNext!
        pdoi = (DebugObjectInfo *)PvSubBv(pdoi->ppdoiPrev, offset(DebugObjectInfo, pdoiNext));
    }

LDone:
    _Leave();
}

/***************************************************************************
    Clears all marks on allocated (BASE) objects.
***************************************************************************/
void UnmarkAllObjs(void)
{
    _Enter();

    BASE *pbase;
    DebugObjectInfo *pdoi;
    LegacyLong lwThread = LwThreadCur();

    Assert(_pdoiFirstRaw == pvNil, "Raw list is not empty!");
    for (pdoi = _pdoiFirst; pvNil != pdoi; pdoi = pdoi->pdoiNext)
    {
        pbase = _PbaseFromDoi(pdoi);
        AssertPo(pbase, fobjAllocated);

        if (pdoi->lwThread == lwThread)
            pdoi->cactRef = 0;
    }

    _Leave();
}
#endif // DEBUG

/***************************************************************************
    Linked list element constructor
***************************************************************************/
BaseLinkedList::BaseLinkedList(void)
{
    _ppbllPrev = pvNil;
    _pbllNext = pvNil;
}

/***************************************************************************
    Remove the element from the linked list
***************************************************************************/
BaseLinkedList::~BaseLinkedList(void)
{
    // unlink the thing
    if (_ppbllPrev != pvNil)
        _Attach(pvNil);
}

/***************************************************************************
    Remove the element from the linked list (if it's in one) and reattach
    it at ppbllPrev (if not pvNil).
***************************************************************************/
void BaseLinkedList::_Attach(void *ppbllPrev)
{
    AssertThis(0);
    PBaseLinkedList *ppbll = (PBaseLinkedList *)ppbllPrev;
    AssertNilOrVarMem(ppbll);

    // unlink the thing
    if (_ppbllPrev != pvNil)
    {
        Assert(*_ppbllPrev == this, "links corrupt");
        if ((*_ppbllPrev = _pbllNext) != pvNil)
        {
            Assert(_pbllNext->_ppbllPrev == &_pbllNext, "links corrupt 2");
            _pbllNext->_ppbllPrev = _ppbllPrev;
        }
    }

    // link the thing
    if ((_ppbllPrev = ppbll) == pvNil)
    {
        // not in a linked list
        _pbllNext = pvNil;
        return;
    }
    if ((_pbllNext = *ppbll) != pvNil)
    {
        Assert(_pbllNext->_ppbllPrev == ppbll, "links corrupt 3");
        _pbllNext->_ppbllPrev = &_pbllNext;
    }
    *ppbll = this;
}

#ifdef DEBUG
/***************************************************************************
    Check the links.
***************************************************************************/
void BaseLinkedList::AssertValid(LegacyUlong grf)
{
    BaseLinkedList_PAR::AssertValid(grf);

    if (_pbllNext != pvNil)
    {
        AssertVarMem(_pbllNext);
        Assert(_pbllNext->_ppbllPrev == &_pbllNext, "links corrupt");
    }
    if (_ppbllPrev != pvNil)
    {
        AssertVarMem(_ppbllPrev);
        Assert(*_ppbllPrev == this, "links corrupt 2");
    }
}
#endif // DEBUG
