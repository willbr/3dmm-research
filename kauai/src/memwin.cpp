/* Copyright (c) Microsoft Corporation.
   Licensed under the MIT License. */

/***************************************************************************
    Author: ShonK
    Project: Kauai
    Reviewed:
    Copyright (c) Microsoft Corporation

    Windows memory management.

***************************************************************************/
#include "util.h"
#include "handle_services.h"
#include "mm/handle_memory.h"
ASSERTNAME
namespace {
int AllocateHandle(void *, MMU32 bytes, void **handle) { return FAllocHq(handle, bytes, fmemNil, mprNormal); }
void ReleaseHandle(void *, void *handle) { FreePhq(&handle); }
MMU32 HandleSize(void *, void *handle) { return CbOfHq(handle); }
void *LockHandle(void *, void *handle) { return PvLockHq(handle); }
void UnlockHandle(void *, void *handle) { UnlockHq(handle); }
int ResizeHandle(void *, void **handle, MMU32 bytes) { return FResizePhq(handle, bytes, fmemNil, mprNormal); }
}
const MMHandleServices *KauaiHandleServices()
{
    static const MMHandleServices services = {nullptr, AllocateHandle, ReleaseHandle, HandleSize, LockHandle, UnlockHandle, ResizeHandle};
    return &services;
}
namespace {
#ifdef DEBUG
using HandleRecord = MMHandleDebugRecord;
#else
using HandleRecord = MMHandleState;
#endif
struct alignas(std::max_align_t) NativeHandleHeader { HandleRecord record; };
#ifdef KAUAI_EXPENSIVE_CHECKS
const int handleExpensive = 1;
#else
const int handleExpensive = 0;
#endif
#ifdef DEBUG
const MMHandleLayout handleLayout{sizeof(NativeHandleHeader), kcbMax, klwMagicMem, 1, handleExpensive};
#else
const MMHandleLayout handleLayout{sizeof(NativeHandleHeader), kcbMax, 0, 0, 0};
#endif
struct HandleRequest { PSZS file; LegacyLong line; };
int FixedAllocate(void *context, void **raw, MMU32 bytes, MMU32 flags, MMI32 priority)
{
    auto *request = static_cast<HandleRequest *>(context);
    return FAllocPvDebug(raw, static_cast<LegacyLong>(bytes), flags, priority, request->file, request->line, &vdmglob.dmaglHq);
}
int FixedResize(void *, void **raw, MMU32 bytes, MMU32 oldBytes, MMU32 flags, MMI32 priority)
{ return _FResizePpvDebug(raw, static_cast<LegacyLong>(bytes), static_cast<LegacyLong>(oldBytes), flags, priority, &vdmglob.dmaglHq); }
void FixedRelease(void *, void **raw) { FreePpvDebug(raw, &vdmglob.dmaglHq); }
void HandleAddress(void *, const void *p, size_t bytes) { AssertPvCb(const_cast<void *>(p), static_cast<LegacyLong>(bytes)); }
void HandleAssertion(void *, int code, const void *value, size_t bytes)
{
    static const char *messages[] = {"", "long too small", "long too big", "Resizing locked HQ", "Freeing locked HQ",
        "overflow in cactLock", "hq not locked", "hq is nil", "beginning of hq block is trashed"};
    AssertCore(false, messages[code], const_cast<void *>(value), static_cast<LegacyLong>(bytes));
}
void FixedValidate(void *, void *raw, MMI32 bytes) { AssertPvAlloced(raw, bytes); }
void FixedMark(void *, void *raw) { MarkPv(raw); }
void HandleFailed(void *) { PushErc(ercOomHq); }
MMHandleHost HandleHost(HandleRequest *request = nullptr)
{
    MMHandleHost host{};
    host.context = request; host.allocate = FixedAllocate; host.resize = FixedResize; host.release = FixedRelease;
    host.address = HandleAddress; host.assertion = HandleAssertion; host.validate_fixed = FixedValidate;
    host.mark_fixed = FixedMark; host.failed = HandleFailed; return host;
}
} // namespace
#ifdef DEBUG
LegacyLong vcactSuspendCheckPointers = 0;
bool FAllocHqDebug(HQ *owner, LegacyLong bytes, LegacyUlong flags, LegacyLong priority, PSZS file, LegacyLong line)
#else
bool FAllocHq(HQ *owner, LegacyLong bytes, LegacyUlong flags, LegacyLong priority)
#endif
{
#ifdef DEBUG
    HandleRequest request{file, line};
#else
    HandleRequest request{nullptr, 0};
#endif
    const auto host = HandleHost(&request);
    return mm_handle_allocate(&handleLayout, &host, owner, bytes, flags, priority) != 0;
}
bool FResizePhq(HQ *owner, LegacyLong bytes, LegacyUlong flags, LegacyLong priority)
{
    const auto host = HandleHost();
    return mm_handle_resize(&handleLayout, &host, owner, bytes, flags, priority) != 0;
}
void FreePhq(HQ *owner)
{
    const auto host = HandleHost(); mm_handle_free(&handleLayout, &host, owner);
}
bool FCopyHq(HQ source, HQ *destination, LegacyLong priority)
{
    HandleRequest request{DebugShip(__szsFile, nullptr), __LINE__};
    const auto host = HandleHost(&request);
    return mm_handle_copy(&handleLayout, &host, source, destination, priority) != 0;
}
LegacyLong CbOfHq(HQ handle)
{
    const auto host = HandleHost(); return mm_handle_size(&handleLayout, &host, handle);
}
void *QvFromHq(HQ handle)
{
    const auto host = HandleHost(); return mm_handle_pointer(&handleLayout, &host, handle);
}
void *PvLockHq(HQ handle)
{
    const auto host = HandleHost(); return mm_handle_lock(&handleLayout, &host, handle);
}
void UnlockHq(HQ handle)
{
    const auto host = HandleHost(); mm_handle_unlock(&handleLayout, &host, handle);
}
#ifdef DEBUG
void AssertHq(HQ handle)
{
    const auto host = HandleHost(); mm_handle_validate(&handleLayout, &host, handle);
}
void MarkHq(HQ handle)
{
    const auto host = HandleHost(); mm_handle_mark(&handleLayout, &host, handle);
}

/***************************************************************************
    Make sure we can access a pointer's memory.  If cb is 0, pv can be
    anything (including nil).
***************************************************************************/
void AssertPvCb(void *pv, LegacyLong cb)
{
    if (vcactSuspendCheckPointers == 0 && cb != 0)
    {
        // This assert has been disabled because AssertPvCb is called on pointers to
        // globals which were previously read/write but are now read-only.

        // AssertVar(!IsBadWritePtr(pv, cb), "no write access to ptr", &pv);
        //  I (ShonK) am assuming that write access implies read access for
        //  memory, so it would just be a waste of time to call this.
        //  AssertVar(!IsBadReadPtr(pv, cb), "no read access to ptr", &pv);
    }
}
#endif // DEBUG
