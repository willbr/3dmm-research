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
const LegacyLong kclwStackDoi = 10;
using DebugObjectInfo = MMObjectDebugRecord;
struct alignas(std::max_align_t) NativeObjectHeader { MMObjectDebugRecord record; };
#define kcbBaseDebug size(NativeObjectHeader)
static MMObjectDebugGraph objectGraph = {nullptr, nullptr};
static const MMObjectDebugLayout debugLayout{sizeof(NativeObjectHeader), sizeof(BASE), kcbMax, kswMagicMem};
namespace { MMObjectDebugHost DebugHost(); }

inline void _Enter(void)
{
    vmutxBase.Enter();
}
inline void _Leave(void)
{
    vmutxBase.Leave();
}

static_assert(MM_OBJECT_DEBUG_ALLOCATED == 'ALOC' && MM_OBJECT_DEBUG_LOCAL == 'NOAL', "native object magic values");
static_assert(MM_OBJECT_REQUIRE_ALLOCATED == fobjAllocated && MM_OBJECT_REQUIRE_LOCAL == fobjNotAllocated, "native diagnostic flags");
static_assert(sizeof(MMObjectDebugState) == sizeof(LegacyLong), "native object diagnostic state width");

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
    const auto host = DebugHost();
    mm_object_debug_attach(&objectGraph, &debugLayout, &host, this, &_debugState);
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
    const auto host = DebugHost();
    mm_object_debug_created(&objectGraph, &debugLayout, &host, pdoi, source->file, source->line,
                            static_cast<LegacyLong>(total), LwThreadCur());
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
            pdoi->stack[ilw] = 0;
            plw = pvNil;
        }
        else
        {
            pdoi->stack[ilw] = plw[1];
            plw = (LegacyLong *)*plw;
        }
    }
#else
    // x64 doesn't allow inline asm. The diagnostic stack trace is
    // optional debug instrumentation; leave it empty on x64 for now.
    // CaptureStackBackTrace would be the proper Win32 replacement.
    for (LegacyLong ilw = 0; ilw < kclwStackDoi; ilw++)
        pdoi->stack[ilw] = 0;
#endif // IN_80386
#endif // WIN
}
void ObjectBeforeFree(void *, void *raw)
{
    const auto host = DebugHost();
    mm_object_debug_unlink(&debugLayout, &host, static_cast<DebugObjectInfo *>(raw));
}
size_t ObjectSize(void *, void *raw) { return static_cast<DebugObjectInfo *>(raw)->total; }
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
namespace
{
void DebugEnter(void *) { _Enter(); }
void DebugLeave(void *) { _Leave(); }
void DebugAddress(void *, const void *p, size_t bytes) { AssertPvCb(const_cast<void *>(p), static_cast<LegacyLong>(bytes)); }
void DebugAssertion(void *, int code, const void *value, size_t bytes)
{
    static const char *messages[] = {"", "long too small", "magic number has been hammered", "negative reference count",
        "should be linked", "should NOT be linked", "*ppdoiPrev is wrong", "ppdoiPrev in next is wrong",
        "long too small", "should be allocated", "should not be allocated", "_lwMagic wrong", "Raw list is not empty!",
        "long too big", "long too big"};
    AssertCore(false, messages[code], const_cast<void *>(value), static_cast<LegacyLong>(bytes));
}
MMMemoryCounter DebugThread(void *) { return LwThreadCur(); }
void DebugAttached(void *, void *p) { static_cast<BASE *>(p)->BASE::AssertValid(0); }
void DebugValidate(void *, void *p, MMU32 flags) { AssertPo(static_cast<BASE *>(p), flags); }
void DebugMark(void *, void *p) { static_cast<BASE *>(p)->MarkMem(); }
void DebugMarkRoot(void *, void *p) { MarkMemObj(static_cast<BASE *>(p)); }
int DebugLostSummary(void *, MMI32 count)
{
    String stn; SZS szs;
    stn.FFormatSz(PszLit("Total lost objects: %d. Press 'Debugger' for detail"), static_cast<LegacyLong>(count));
    stn.GetSzs(szs);
    return FAssertProc(__szsFile, __LINE__, szs, pvNil, 0);
}
void DebugLostObject(void *, void *p, MMObjectDebugRecord *r, size_t payload)
{
    String stn; SZS szs;
    stn.FFormatSz(PszLit("\nLost object: cls='%f', size=%d, ") PszLit("StackTrace=(use map file)"),
                  static_cast<BASE *>(p)->Cls(), static_cast<LegacyLong>(payload));
    stn.GetSzs(szs);
    if (FAssertProc(r->source, r->line, szs, r->stack, kclwStackDoi * size(LegacyLong))) Debugger();
}
MMObjectDebugHost DebugHost()
{
    MMObjectDebugHost host{};
    host.enter = DebugEnter; host.leave = DebugLeave; host.address = DebugAddress; host.assertion = DebugAssertion;
    host.current_thread = DebugThread; host.attached = DebugAttached; host.validate = DebugValidate;
    host.mark = DebugMark; host.mark_root = DebugMarkRoot; host.lost_summary = DebugLostSummary; host.lost_object = DebugLostObject;
    return host;
}
} // namespace

void BASE::AssertValid(LegacyUlong flags)
{
    const auto host = DebugHost();
    mm_object_debug_validate(&debugLayout, &host, this, &_debugState, _references.count, flags);
}
void BASE::MarkMemStub(void)
{
    const auto host = DebugHost();
    mm_object_debug_mark_stub(&debugLayout, &host, this, &_debugState);
}
void BASE::MarkMem(void)
{
    const auto host = DebugHost();
    mm_object_debug_mark(&debugLayout, &host, this, &_debugState, _references.count);
}
void __AssertOnCopy(void) { Bug("Copying a non-copyable object"); }
void AssertUnmarkedObjs(void)
{
    const auto host = DebugHost();
    mm_object_debug_report(&objectGraph, &debugLayout, &host);
}
void UnmarkAllObjs(void)
{
    const auto host = DebugHost();
    mm_object_debug_unmark(&objectGraph, &debugLayout, &host);
}

#endif // DEBUG

/***************************************************************************
    Linked list element constructor
***************************************************************************/
BaseLinkedList::BaseLinkedList(void)
{
    mm_linked_list_init(&_list, this);
}

/***************************************************************************
    Remove the element from the linked list
***************************************************************************/
BaseLinkedList::~BaseLinkedList(void)
{
    // unlink the thing
    if (_list.previous != pvNil)
        _Attach(pvNil);
}

namespace {
#ifdef DEBUG
void ListAddress(void *, const void *address, int owner)
{
    if (owner) { AssertVarMem(static_cast<BaseLinkedList *>(const_cast<void *>(address))); }
    else { AssertVarMem(static_cast<MMLinkedListNode **>(const_cast<void *>(address))); }
}
void ListAssertion(void *, int code)
{
    switch (code) {
    case MM_LIST_UNLINK_NEXT: Assert(fFalse, "links corrupt 2"); break;
    case MM_LIST_ATTACH_NEXT: Assert(fFalse, "links corrupt 3"); break;
    case MM_LIST_VALIDATE_PREVIOUS: Assert(fFalse, "links corrupt 2"); break;
    default: Assert(fFalse, "links corrupt"); break;
    }
}
const MMLinkedListHost listHost = {nullptr, ListAddress, ListAssertion};
#else
const MMLinkedListHost listHost = {};
#endif
}

/***************************************************************************
    Remove the element from the linked list (if it's in one) and reattach
    it at the previous C node slot (if not pvNil).
***************************************************************************/
void BaseLinkedList::_Attach(MMLinkedListNode **previous)
{
    AssertThis(0);
    AssertNilOrVarMem(previous);
    mm_linked_list_attach(&_list, previous, &listHost);
}

#ifdef DEBUG
/***************************************************************************
    Check the links.
***************************************************************************/
void BaseLinkedList::AssertValid(LegacyUlong grf)
{
    BaseLinkedList_PAR::AssertValid(grf);

    mm_linked_list_validate(&_list, &listHost);
}
#endif // DEBUG
