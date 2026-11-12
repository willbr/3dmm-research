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
ASSERTNAME
namespace {
int AllocateHandle(void *, ThreeMMU32 bytes, void **handle) { return FAllocHq(handle, bytes, fmemNil, mprNormal); }
void ReleaseHandle(void *, void *handle) { FreePhq(&handle); }
ThreeMMU32 HandleSize(void *, void *handle) { return CbOfHq(handle); }
void *LockHandle(void *, void *handle) { return PvLockHq(handle); }
void UnlockHandle(void *, void *handle) { UnlockHq(handle); }
int ResizeHandle(void *, void **handle, ThreeMMU32 bytes) { return FResizePhq(handle, bytes, fmemNil, mprNormal); }
}
const ThreeMMHandleServices *KauaiHandleServices()
{
    static const ThreeMMHandleServices services = {nullptr, AllocateHandle, ReleaseHandle, HandleSize, LockHandle, UnlockHandle, ResizeHandle};
    return &services;
}
struct alignas(std::max_align_t) HQH
{
    LegacyLong cb;       // size of client area
    LegacyLong cactLock; // lock count
#ifdef DEBUG
    LegacyLong lwMagic; // for detecting memory trashing
#endif            // DEBUG
};

#ifdef DEBUG
LegacyLong vcactSuspendCheckPointers = 0;
#endif // DEBUG

/***************************************************************************
    Allocates a new moveable block.
***************************************************************************/
#ifdef DEBUG
bool FAllocHqDebug(HQ *phq, LegacyLong cb, LegacyUlong grfmem, LegacyLong mpr, PSZS pszsFile, LegacyLong lwLine)
#else  //! DEBUG
bool FAllocHq(HQ *phq, LegacyLong cb, LegacyUlong grfmem, LegacyLong mpr)
#endif //! DEBUG
{
    AssertVarMem(phq);
    AssertIn(cb, 0, kcbMax);
    HQH *phqh;

    if (!FAllocPvDebug((void **)&phqh, cb + size(HQH), grfmem, mpr, pszsFile, lwLine, &vdmglob.dmaglHq))
    {
        PushErc(ercOomHq);
        *phq = hqNil;
    }
    else
    {
        phqh->cb = cb;
        phqh->cactLock = 0;
        Debug(phqh->lwMagic = klwMagicMem;) *phq = (HQ)(phqh + 1);
        AssertHq(*phq);
    }

    return pvNil != *phq;
}

/***************************************************************************
    Resizes the given hq.  *phq may change.  If fmemClear, clears any
    newly added space.
***************************************************************************/
bool FResizePhq(HQ *phq, LegacyLong cb, LegacyUlong grfmem, LegacyLong mpr)
{
    AssertVarMem(phq);
    AssertHq(*phq);
    AssertIn(cb, 0, kcbMax);
    bool fRet = fFalse;
    HQH *phqh = (HQH *)PvSubBv(*phq, size(HQH));

    if (phqh->cactLock > 0)
    {
        Bug("Resizing locked HQ");
        PushErc(ercOomHq);
    }
    else if (!_FResizePpvDebug((void **)&phqh, cb + size(HQH), phqh->cb + size(HQH), grfmem, mpr, &vdmglob.dmaglHq))
    {
        PushErc(ercOomHq);
        AssertHq(*phq);
    }
    else
    {
        phqh->cb = cb;
        *phq = (HQ)(phqh + 1);
        AssertHq(*phq);
        fRet = fTrue;
    }

    return fRet;
}

/***************************************************************************
    If hq is not nil, frees it.
***************************************************************************/
void FreePhq(HQ *phq)
{
    AssertVarMem(phq);

    if (*phq == hqNil)
        return;

    AssertHq(*phq);
    HQH *phqh = (HQH *)PvSubBv(*phq, size(HQH));
    *phq = hqNil;
    Assert(phqh->cactLock == 0, "Freeing locked HQ");

    FreePpvDebug((void **)&phqh, &vdmglob.dmaglHq);
}

/***************************************************************************
    Create a new HQ the same size as hqSrc and copy hqSrc into it.
***************************************************************************/
bool FCopyHq(HQ hqSrc, HQ *phqDst, LegacyLong mpr)
{
    AssertHq(hqSrc);
    AssertVarMem(phqDst);
    LegacyLong cb;

    if (!FAllocHq(phqDst, cb = CbOfHq(hqSrc), fmemNil, mpr))
        return fFalse;
    CopyPb(QvFromHq(hqSrc), QvFromHq(*phqDst), cb);
    return fTrue;
}

/***************************************************************************
    Return the size of the hq (the client area of the block).
***************************************************************************/
LegacyLong CbOfHq(HQ hq)
{
    AssertHq(hq);
    HQH *phqh = (HQH *)PvSubBv(hq, size(HQH));
    return phqh->cb;
}

#ifdef DEBUG
/***************************************************************************
    Returns a volatile pointer from an hq.
***************************************************************************/
void *QvFromHq(HQ hq)
{
    AssertHq(hq);
    return (void *)hq;
}
#endif // DEBUG

/***************************************************************************
    Lock the hq and return a pointer to the data.
***************************************************************************/
void *PvLockHq(HQ hq)
{
    AssertHq(hq);
    HQH *phqh = (HQH *)PvSubBv(hq, size(HQH));
    phqh->cactLock++;
    Assert(phqh->cactLock > 0, "overflow in cactLock");
    return (void *)hq;
}

/***************************************************************************
    Unlock the hq.  Asserts and does nothing if the lock count is zero.
***************************************************************************/
void UnlockHq(HQ hq)
{
    AssertHq(hq);
    HQH *phqh = (HQH *)PvSubBv(hq, size(HQH));
    Assert(phqh->cactLock > 0, "hq not locked");
    if (phqh->cactLock > 0)
        --phqh->cactLock;
}

#ifdef DEBUG
/***************************************************************************
    Assert that a given hq is valid.
***************************************************************************/
void AssertHq(HQ hq)
{
    // make sure hq isn't nil
    if (hq == hqNil)
    {
        Bug("hq is nil");
        return;
    }

    // verify the HQH
#ifdef KAUAI_EXPENSIVE_CHECKS
    HQH *phqh = (HQH *)PvSubBv(hq, size(HQH));
    if (phqh->lwMagic != klwMagicMem)
    {
        BugVar("beginning of hq block is trashed", phqh);
        return;
    }
    AssertIn(phqh->cactLock, 0, kcbMax);
    AssertPvAlloced(phqh, phqh->cb + size(HQH));
#endif
}

/***************************************************************************
    Increment the ref count on an hq.
***************************************************************************/
void MarkHq(HQ hq)
{
    if (hqNil != hq)
    {
        AssertHq(hq);
        MarkPv(PvSubBv(hq, size(HQH)));
    }
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
