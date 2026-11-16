/* Copyright (c) Microsoft Corporation.
   Licensed under the MIT License. */

/***************************************************************************
    Author: ShonK
    Project: Kauai
    Reviewed:
    Copyright (c) Microsoft Corporation

    Shared (between Mac and Win) memory allocation routines.

    The APIs in this module implement fixed block (non-moveable,
    non-resizeable) memory management. Raw storage uses the portable
    core_platform allocator. Fixed allocation/resize/free transactions,
    failure injection, counters and liberation control use mm/memory.h;
    native debug headers, lists and physical heap services remain here.
***************************************************************************/
#include "win_headers.h"
#include "util.h"
#include <cstring>
ASSERTNAME

#ifdef DEBUG
const LegacyLong kclwStackMbh = 5;

// memory block header
struct alignas(std::max_align_t) MBH
{
    LegacyLong cb;                      // size of block, including header and footer
    PSZS pszsFile;                // source file that allocation request is coming from
    LegacyLong lwLine;                  // line in file that allocation request is coming from
    LegacyLong lwThread;                // thread id
    MBH *pmbhPrev;                // previous allocated block (in doubly linked list)
    MBH *pmbhNext;                // next allocated block
    LegacyLong rglwStack[kclwStackMbh]; // the EBP/A6 chain
    short cactRef;                // for marking memory
    short swMagic;                // magic number, to detect memory trashing
};

// memory block footer
struct MBF
{
    short swMagic; // magic number, to detect memory trashing
};

MBH *_pmbhFirst; // head of the doubly linked list

priv void _LinkMbh(MBH *pmbh);
priv void _UnlinkMbh(MBH *pmbh, MBH *pmbhOld);
priv void _AssertMbh(MBH *pmbh);
#endif // DEBUG

PFNLIB vpfnlib = pvNil;
namespace {
MMFixedMemory fixedMemory = {0};
struct NativeMemoryRequest { PSZS file; LegacyLong line; };
#ifdef DEBUG
const MMMemoryLayout memoryLayout = {sizeof(MBH), sizeof(MBF), kcbMax, 1, kbGarbage};
#else
const MMMemoryLayout memoryLayout = {0, 0, kcbMax, 0, kbGarbage};
#endif
void *RawAllocate(void *, size_t bytes) { return KauaiPlatform::Allocate(bytes); }
void *RawResize(void *, void *old, size_t bytes) { return KauaiPlatform::Reallocate(old, bytes); }
void RawFree(void *, void *raw) { KauaiPlatform::Free(raw); }
void MemoryEnter(void *) { vmutxMem.Enter(); }
void MemoryLeave(void *) { vmutxMem.Leave(); }
int HasLiberator(void *) { return vpfnlib != pvNil; }
MMI32 Liberate(void *, MMI32 bytes, MMI32 priority) { return (*vpfnlib)(bytes, priority); }
void TooLarge(void *, MMU32 requested, int resizing)
{
    LegacyLong bytes = static_cast<LegacyLong>(requested);
    if (resizing) { BugVar("who's resizing a humongous block?", &bytes); }
    else { BugVar("who's allocating a humongous block?", &bytes); }
}
void MemoryFailed(void *, int) { PushErc(ercOomPv); }
#ifdef DEBUG
void BeforeAllocate(void *, MMU32 payload)
{
    if (_pmbhFirst != pvNil) _AssertMbh(_pmbhFirst);
    Assert(payload + size(MBH) + size(MBF) > payload, 0);
}
void WriteFooter(MBH *pmbh, MMU32 total)
{
    MBF footer; footer.swMagic = kswMagicMem;
    CopyPb(&footer, PvAddBv(pmbh, total - size(MBF)), size(MBF));
}
void Allocated(void *context, void *raw, MMU32 total)
{
    auto *request = static_cast<NativeMemoryRequest *>(context);
    auto *pmbh = static_cast<MBH *>(raw);
    pmbh->cb = total; pmbh->swMagic = kswMagicMem;
    pmbh->pszsFile = request->file; pmbh->lwLine = request->line;
    pmbh->lwThread = LwThreadCur();
#ifdef WIN
#ifdef IN_80386
    // follow the EBP chain to capture a debug-mode allocation stack trace.
    LegacyLong *plw;
    LegacyLong ilw;

    __asm { mov plw,ebp }
    for (ilw = 0; ilw < kclwStackMbh; ilw++)
    {
        if (pvNil == plw || IsBadReadPtr(plw, 2 * size(LegacyLong)) || *plw <= (LegacyLong)plw)
        {
            pmbh->rglwStack[ilw] = 0;
            plw = pvNil;
        }
        else
        {
            pmbh->rglwStack[ilw] = plw[1];
            plw = (LegacyLong *)*plw;
        }
    }
#else
    // x64: inline asm not allowed. Optional debug instrumentation; leave empty.
    for (LegacyLong ilw = 0; ilw < kclwStackMbh; ilw++)
        pmbh->rglwStack[ilw] = 0;
#endif // IN_80386
#endif // WIN
    WriteFooter(pmbh, total);
    _LinkMbh(pmbh);
}
void InspectResize(void *, void *raw) { _AssertMbh(static_cast<MBH *>(raw)); }
void BeforeResize(void *, void *raw, MMU32 oldTotal, MMU32 newTotal)
{
    Assert(oldTotal >= size(MBH) + size(MBF) && newTotal >= size(MBH) + size(MBF), 0);
    LegacyLong expected = oldTotal;
    AssertVar(static_cast<MBH *>(raw)->cb == expected, "bad cbOld value passed to _FResizePpv", &expected);
}
void Relocated(void *, void *raw, const unsigned char *oldAddress)
{
    void *oldRaw;
    std::memcpy(&oldRaw, oldAddress, sizeof(oldRaw));
    if (raw == oldRaw) return;
    auto *pmbh = static_cast<MBH *>(raw);
    _UnlinkMbh(pmbh, static_cast<MBH *>(oldRaw));
    _LinkMbh(pmbh);
}
void Resized(void *, void *raw, MMU32 total)
{
    auto *pmbh = static_cast<MBH *>(raw);
    pmbh->cb = total; WriteFooter(pmbh, total);
}
void BeforeFree(void *, void *raw)
{
    auto *pmbh = static_cast<MBH *>(raw);
    _AssertMbh(pmbh); _UnlinkMbh(pmbh, pmbh);
}
MMU32 BlockSize(void *, void *raw) { return static_cast<MBH *>(raw)->cb; }
void Validate(void *, void *payload, MMU32 bytes) { AssertPvAlloced(payload, bytes); }
#endif
MMMemoryHost MemoryHost(NativeMemoryRequest *request)
{
    MMMemoryHost host = {};
    host.context = request; host.allocate = RawAllocate; host.reallocate = RawResize; host.release = RawFree;
    host.enter = MemoryEnter; host.leave = MemoryLeave; host.has_liberator = HasLiberator; host.liberate = Liberate;
    host.too_large = TooLarge; host.failed = MemoryFailed;
#ifdef DEBUG
    host.before_allocate = BeforeAllocate; host.allocated = Allocated; host.inspect_resize = InspectResize;
    host.before_resize = BeforeResize; host.relocated = Relocated; host.resized = Resized;
    host.before_free = BeforeFree; host.block_size = BlockSize; host.validate = Validate;
#endif
    return host;
}
}
#ifdef DEBUG
bool DebugMemoryAllocatorGlobals::FFail(void)
{
    vmutxMem.Enter(); const bool failed = mm_memory_stats_fail(this) != 0; vmutxMem.Leave();
    return failed;
}
void DebugMemoryAllocatorGlobals::Allocate(LegacyLong bytes)
{
    static_assert(sizeof(DebugMemoryAllocatorGlobals) == 7 * sizeof(LegacyLong), "native allocator statistics size");
    vmutxMem.Enter(); mm_memory_stats_allocate(this, bytes); vmutxMem.Leave();
}
void DebugMemoryAllocatorGlobals::Resize(LegacyLong delta)
{ vmutxMem.Enter(); mm_memory_stats_resize(this, delta); vmutxMem.Leave(); }
void DebugMemoryAllocatorGlobals::Free(LegacyLong bytes) { mm_memory_stats_free(this, bytes); }
#endif
#ifdef DEBUG
bool FAllocPvDebug(void **ppv, LegacyLong cb, LegacyUlong grfmem, LegacyLong mpr, PSZS pszsFile, LegacyLong lwLine, DebugMemoryAllocatorGlobals *pdmagl)
#else
bool FAllocPv(void **ppv, LegacyLong cb, LegacyUlong grfmem, LegacyLong mpr)
#endif
{
    AssertVarMem(ppv); AssertIn(cb, 0, kcbMax);
    static_assert(fmemClear == 1, "memory clear flag");
#ifdef DEBUG
    NativeMemoryRequest request = {pszsFile, lwLine};
#else
    NativeMemoryRequest request = {nullptr, 0};
    MMMemoryStats *pdmagl = nullptr;
#endif
    const auto host = MemoryHost(&request);
    return mm_fixed_allocate(&fixedMemory, &memoryLayout, &host, pdmagl, ppv, cb, grfmem, mpr) != 0;
}
#ifdef DEBUG
bool _FResizePpvDebug(void **ppv, LegacyLong cbNew, LegacyLong cbOld, LegacyUlong grfmem, LegacyLong mpr, DebugMemoryAllocatorGlobals *pdmagl)
#else
bool _FResizePpv(void **ppv, LegacyLong cbNew, LegacyLong cbOld, LegacyUlong grfmem, LegacyLong mpr)
#endif
{
    AssertVarMem(ppv); AssertIn(cbNew, 0, kcbMax); AssertIn(cbOld, 0, kcbMax); AssertPvAlloced(*ppv, cbOld);
#ifndef DEBUG
    MMMemoryStats *pdmagl = nullptr;
#endif
    NativeMemoryRequest request = {nullptr, 0}; const auto host = MemoryHost(&request);
    return mm_fixed_resize(&fixedMemory, &memoryLayout, &host, pdmagl, ppv, cbNew, cbOld, grfmem, mpr) != 0;
}
#ifdef DEBUG
void FreePpvDebug(void **ppv, DebugMemoryAllocatorGlobals *pdmagl)
#else
void FreePpv(void **ppv)
#endif
{
    AssertVarMem(ppv);
#ifndef DEBUG
    MMMemoryStats *pdmagl = nullptr;
#endif
    NativeMemoryRequest request = {nullptr, 0}; const auto host = MemoryHost(&request);
    mm_fixed_free(&memoryLayout, &host, pdmagl, ppv);
}

#ifdef DEBUG
/***************************************************************************
    Link the Mbh into the debug-only doubly linked list.
***************************************************************************/
priv void _LinkMbh(MBH *pmbh)
{
    AssertVarMem(pmbh);

    vmutxMem.Enter();
    pmbh->pmbhPrev = pvNil;
    pmbh->pmbhNext = _pmbhFirst;
    if (_pmbhFirst != pvNil)
    {
        Assert(_pmbhFirst->pmbhPrev == pvNil, "_pmbhFirst's prev is not nil");
        _pmbhFirst->pmbhPrev = pmbh;
    }
    _pmbhFirst = pmbh;
    vmutxMem.Leave();
}

/***************************************************************************
    Unlink the MBH from the debug-only doubly linked list.  pmbhOld is the
    previous value of the linked block.  pmbhOld may not be a valid pointer
    now (when mem is resized).
***************************************************************************/
priv void _UnlinkMbh(MBH *pmbh, MBH *pmbhOld)
{
    AssertVarMem(pmbh);
    Assert(pmbhOld != pvNil, 0);

    vmutxMem.Enter();
    // update prev's next pointer
    if (pvNil == pmbh->pmbhPrev)
    {
        Assert(_pmbhFirst == pmbhOld, "prev is wrongly nil");
        _pmbhFirst = pmbh->pmbhNext;
    }
    else
    {
        Assert(_pmbhFirst != pmbhOld, "prev should be nil");
        Assert(pmbh->pmbhPrev->pmbhNext == pmbhOld, "prev's next wrong");
        pmbh->pmbhPrev->pmbhNext = pmbh->pmbhNext;
    }

    // update next's prev pointer
    if (pvNil != pmbh->pmbhNext)
    {
        Assert(pmbh->pmbhNext->pmbhPrev == pmbhOld, "next's prev wrong");
        pmbh->pmbhNext->pmbhPrev = pmbh->pmbhPrev;
    }
    vmutxMem.Leave();
}

/***************************************************************************
    Validate the MBH and the block that it is the head of.
***************************************************************************/
void _AssertMbh(MBH *pmbh)
{
#ifdef KAUAI_EXPENSIVE_CHECKS
    short sw;

    if (vcactSuspendCheckPointers != 0)
        return;

    vmutxMem.Enter();
    AssertVarMem(pmbh);
    Assert(pmbh->swMagic == kswMagicMem, "bad magic number");
    AssertIn(pmbh->cb - size(MBH) - size(MBF), 0, kcbMax);
    Assert(pmbh->cb <= (LegacyLong)KauaiPlatform::AllocationSize(pmbh), "bigger than allocated block");
    AssertPvCb(pmbh, pmbh->cb);
    if (pmbh->pmbhPrev != pvNil)
    {
        AssertVarMem(pmbh->pmbhPrev);
        Assert(pmbh->pmbhPrev->pmbhNext == pmbh, "wrong next in prev");
        Assert(pmbh != _pmbhFirst, "first has prev!");
    }
    if (pmbh->pmbhNext != pvNil)
    {
        AssertVarMem(pmbh->pmbhNext);
        Assert(pmbh->pmbhNext->pmbhPrev == pmbh, "wrong prev in next");
    }

    ((byte *)&sw)[0] = *(byte *)PvAddBv(pmbh, pmbh->cb - 2);
    ((byte *)&sw)[1] = *(byte *)PvAddBv(pmbh, pmbh->cb - 1);
    Assert(sw == kswMagicMem, "bad tail magic number");
    vmutxMem.Leave();
#endif
}

/***************************************************************************
    Assert the validity of an allocated fixed block.  Cb is the size.
    If cb is unknown, pass cvNil.
***************************************************************************/
void AssertPvAlloced(void *pv, LegacyLong cb)
{
#ifdef KAUAI_EXPENSIVE_CHECKS
    if (vcactSuspendCheckPointers != 0)
        return;

    Assert(pv != pvNil, "nil pv");
    MBH *pmbh = (MBH *)PvSubBv(pv, size(MBH));
    _AssertMbh(pmbh);
    if (cb != cvNil)
        Assert(pmbh->cb == cb + size(MBH) + size(MBF), "wrong cb");
#else
    Assert(pv != pvNil, "nil pv");
#endif
}

/***************************************************************************
    Asserts on unmarked blocks.
***************************************************************************/
void AssertUnmarkedMem(void)
{
    MBH *pmbh;
    LegacyLong lwThread = LwThreadCur();

    // enter the critical section
    vmutxMem.Enter();

    for (pmbh = _pmbhFirst; pmbh != pvNil; pmbh = pmbh->pmbhNext)
    {
        _AssertMbh(pmbh);
        if (pmbh->cactRef == 0 && pmbh->lwThread == lwThread)
        {
            String stn;
            SZS szs;

            stn.FFormatSz(PszLit("\nLost block: size=%d, StackTrace=(use map file)"), pmbh->cb);
            stn.GetSzs(szs);

            if (FAssertProc(pmbh->pszsFile, pmbh->lwLine, szs, pmbh->rglwStack, kclwStackMbh * size(LegacyLong)))
            {
                Debugger();
            }
        }
    }
    Mac(_AssertUnmarkedHqs();)

        // leave the critical section
        vmutxMem.Leave();
}

/***************************************************************************
    Clears all marks on memory blocks.
***************************************************************************/
void UnmarkAllMem(void)
{
    MBH *pmbh;
    LegacyLong lwThread = LwThreadCur();

    // enter the critical section
    vmutxMem.Enter();

    for (pmbh = _pmbhFirst; pmbh != pvNil; pmbh = pmbh->pmbhNext)
    {
        _AssertMbh(pmbh);
        if (pmbh->lwThread == lwThread)
            pmbh->cactRef = 0;
    }
    Mac(_UnmarkAllHqs();)

        // leave the critical section
        vmutxMem.Leave();
}

/***************************************************************************
    Increment the ref count on an allocated pv.
***************************************************************************/
void MarkPv(void *pv)
{
    if (pvNil != pv)
    {
        AssertPvAlloced(pv, cvNil);
        MBH *pmbh = (MBH *)PvSubBv(pv, size(MBH));
        pmbh->cactRef++;
    }
}
#endif // DEBUG
