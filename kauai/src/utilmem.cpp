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
    diagnostic graph/header/mark policy uses mm/memory_debug.h; native alignment,
    stack capture, locking and diagnostic services remain here.
***************************************************************************/
#include "win_headers.h"
#include "util.h"
#include "mm/memory_debug.h"
ASSERTNAME

#ifdef DEBUG
const LegacyLong kclwStackMbh = 5;

using MBH = MMMemoryDebugRecord;
using MBF = MMMemoryDebugFooter;
struct alignas(std::max_align_t) NativeMemoryHeader { MMMemoryDebugRecord record; };
static MMMemoryDebugGraph memoryGraph = {nullptr};
#ifdef KAUAI_EXPENSIVE_CHECKS
static const int memoryExpensive = 1;
#else
static const int memoryExpensive = 0;
#endif
static const MMMemoryDebugLayout debugMemoryLayout = {
    sizeof(NativeMemoryHeader), sizeof(MBF), kcbMax, kswMagicMem, memoryExpensive, &vcactSuspendCheckPointers};
#endif // DEBUG

PFNLIB vpfnlib = pvNil;
namespace {
MMFixedMemory fixedMemory = {0};
struct NativeMemoryRequest {
    PSZS file; LegacyLong line;
#ifdef DEBUG
    MMMemoryDebugMove move;
#endif
};
#ifdef DEBUG
const MMMemoryLayout memoryLayout = {sizeof(NativeMemoryHeader), sizeof(MBF), kcbMax, 1, kbGarbage};
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
void DebugAddress(void *, const void *p, size_t bytes) { AssertPvCb(const_cast<void *>(p), static_cast<LegacyLong>(bytes)); }
void DebugAssertion(void *, int code, const void *value, size_t bytes)
{
    static const char *messages[] = {"", nullptr, "_pmbhFirst's prev is not nil", nullptr,
        "prev is wrongly nil", "prev should be nil", "prev's next wrong", "next's prev wrong", "bad magic number",
        "long too small", "long too big", "bigger than allocated block", "wrong next in prev", "first has prev!",
        "wrong prev in next", "bad tail magic number", "nil pv", "wrong cb", nullptr, "bad cbOld value passed to _FResizePpv"};
    AssertCore(false, messages[code], const_cast<void *>(value), static_cast<LegacyLong>(bytes));
}
size_t DebugAllocationSize(void *, const void *p) { return KauaiPlatform::AllocationSize(p); }
MMMemoryCounter DebugThread(void *) { return LwThreadCur(); }
void DebugLost(void *, MMMemoryDebugRecord *r)
{
    String stn; SZS szs;
    stn.FFormatSz(PszLit("\nLost block: size=%d, StackTrace=(use map file)"), r->total);
    stn.GetSzs(szs);
    if (FAssertProc(r->source, r->line, szs, r->stack, kclwStackMbh * size(LegacyLong))) Debugger();
}
void DebugAfterReport(void *) { Mac(_AssertUnmarkedHqs();) }
void DebugAfterUnmark(void *) { Mac(_UnmarkAllHqs();) }
MMMemoryDebugHost DebugHost()
{
    MMMemoryDebugHost h{};
    h.enter = MemoryEnter; h.leave = MemoryLeave; h.address = DebugAddress; h.assertion = DebugAssertion;
    h.allocation_size = DebugAllocationSize; h.current_thread = DebugThread; h.lost = DebugLost;
    h.after_report = DebugAfterReport; h.after_unmark = DebugAfterUnmark; return h;
}
void BeforeAllocate(void *, MMU32 payload)
{
    const auto host = DebugHost();
    mm_memory_debug_before_allocate(&memoryGraph, &debugMemoryLayout, &host, payload);
}
void Allocated(void *context, void *raw, MMU32 total)
{
    auto *request = static_cast<NativeMemoryRequest *>(context);
    auto *pmbh = static_cast<MBH *>(raw);
    const auto host = DebugHost();
    mm_memory_debug_created(&debugMemoryLayout, &host, pmbh, total, request->file, request->line);
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
            pmbh->stack[ilw] = 0;
            plw = pvNil;
        }
        else
        {
            pmbh->stack[ilw] = plw[1];
            plw = (LegacyLong *)*plw;
        }
    }
#else
    // x64: inline asm not allowed. Optional debug instrumentation; leave empty.
    for (LegacyLong ilw = 0; ilw < kclwStackMbh; ilw++)
        pmbh->stack[ilw] = 0;
#endif // IN_80386
#endif // WIN
    mm_memory_debug_publish(&memoryGraph, &debugMemoryLayout, &host, pmbh);
}
void InspectResize(void *, void *raw)
{
    const auto host = DebugHost();
    mm_memory_debug_validate_record(&memoryGraph, &debugMemoryLayout, &host, static_cast<MBH *>(raw));
}
void BeforeResize(void *context, void *raw, MMU32 oldTotal, MMU32 newTotal)
{
    const auto host = DebugHost();
    mm_memory_debug_prepare_resize(&debugMemoryLayout, &host, static_cast<MBH *>(raw), oldTotal, newTotal,
                                   &static_cast<NativeMemoryRequest *>(context)->move);
}
void Relocated(void *context, void *raw, const unsigned char *oldAddress)
{
    const auto host = DebugHost();
    mm_memory_debug_relocated(&memoryGraph, &debugMemoryLayout, &host, raw,
                              &static_cast<NativeMemoryRequest *>(context)->move, oldAddress);
}
void Resized(void *, void *raw, MMU32 total)
{ mm_memory_debug_resized(&debugMemoryLayout, static_cast<MBH *>(raw), total); }
void BeforeFree(void *, void *raw)
{
    const auto host = DebugHost();
    mm_memory_debug_before_free(&memoryGraph, &debugMemoryLayout, &host, static_cast<MBH *>(raw));
}
MMU32 BlockSize(void *, void *raw) { return static_cast<MBH *>(raw)->total; }
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
void AssertPvAlloced(void *p, LegacyLong bytes)
{
    const auto host = DebugHost();
    mm_memory_debug_validate_payload(&memoryGraph, &debugMemoryLayout, &host, p, bytes);
}
void AssertUnmarkedMem(void)
{
    const auto host = DebugHost();
    mm_memory_debug_report(&memoryGraph, &debugMemoryLayout, &host);
}
void UnmarkAllMem(void)
{
    const auto host = DebugHost();
    mm_memory_debug_unmark(&memoryGraph, &debugMemoryLayout, &host);
}
void MarkPv(void *p)
{
    const auto host = DebugHost();
    mm_memory_debug_mark(&memoryGraph, &debugMemoryLayout, &host, p);
}
#endif // DEBUG
