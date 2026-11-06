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
    core_platform allocator; the legacy debug headers, failure injection
    and memory-liberation callback remain at this layer.
***************************************************************************/
#include "win_headers.h"
#include "util.h"
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
bool _fInLiberator = fFalse;

#ifdef DEBUG
/***************************************************************************
    Do simulated failure testing.
***************************************************************************/
bool DebugMemoryAllocatorGlobals::FFail(void)
{
    bool fRet = fFalse;

    vmutxMem.Enter();
    if (cactFail > 0)
    {
        if (cactDo <= 0)
        {
            cactFail--;
            fRet = fTrue;
        }
        else
            cactDo--;
    }
    vmutxMem.Leave();

    return fRet;
}

/***************************************************************************
    Update values after an allocation
***************************************************************************/
void DebugMemoryAllocatorGlobals::Allocate(LegacyLong cbT)
{
    vmutxMem.Enter();
    if (cvRun < ++cv)
        cvRun = cv;
    cvTot++;
    if (cbRun < (cb += cbT))
        cbRun = cb;
    vmutxMem.Leave();
}

/***************************************************************************
    Update values after a resize
***************************************************************************/
void DebugMemoryAllocatorGlobals::Resize(LegacyLong dcb)
{
    vmutxMem.Enter();
    if (cbRun < (cb += dcb))
        cbRun = cb;
    vmutxMem.Leave();
}

/***************************************************************************
    Update values after a block is freed
***************************************************************************/
void DebugMemoryAllocatorGlobals::Free(LegacyLong cbT)
{
    --cv;
    cb -= cbT;
}
#endif // DEBUG

/***************************************************************************
    Allocates a fixed block.
***************************************************************************/
#ifdef DEBUG
bool FAllocPvDebug(void **ppv, LegacyLong cb, LegacyUlong grfmem, LegacyLong mpr, PSZS pszsFile, LegacyLong lwLine, DebugMemoryAllocatorGlobals *pdmagl)
#else  //! DEBUG
bool FAllocPv(void **ppv, LegacyLong cb, LegacyUlong grfmem, LegacyLong mpr)
#endif //! DEBUG
{
    AssertVarMem(ppv);
    AssertIn(cb, 0, kcbMax);
    LegacyLong cbFree;

    if (cb > kcbMax)
    {
        BugVar("who's allocating a humongous block?", &cb);
        goto LFail;
    }

#ifdef DEBUG
    // do simulated failure
    if (pdmagl->FFail())
        goto LFail;

    vmutxMem.Enter();
    if (pvNil != _pmbhFirst)
        _AssertMbh(_pmbhFirst);
    vmutxMem.Leave();

    Assert(cb + size(MBH) + size(MBF) > cb, 0);
    cb += size(MBH) + size(MBF);
#endif // DEBUG

    for (;;)
    {
        *ppv = KauaiPlatform::Allocate(cb);
        if (pvNil != *ppv || pvNil == vpfnlib)
            break;

        vmutxMem.Enter();
        if (_fInLiberator)
            cbFree = 0;
        else
        {
            _fInLiberator = fTrue;
            vmutxMem.Leave();
            cbFree = (*vpfnlib)(cb, mpr);
            vmutxMem.Enter();
            _fInLiberator = fFalse;
        }
        vmutxMem.Leave();

        if (cbFree <= 0)
            break;
    }

    if (pvNil == *ppv)
    {
    LFail:
        *ppv = pvNil;
        PushErc(ercOomPv);
        return fFalse;
    }

    if (grfmem & fmemClear)
        ClearPb(*ppv, cb);
#ifdef DEBUG
    else
        FillPb(*ppv, cb, kbGarbage);

    // fill in the header
    MBH *pmbh = (MBH *)*ppv;
    *ppv = pmbh + 1;
    pmbh->cb = cb;
    pmbh->swMagic = kswMagicMem;
    pmbh->pszsFile = pszsFile;
    pmbh->lwLine = lwLine;
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

    // write the footer
    MBF mbf;
    mbf.swMagic = kswMagicMem;
    CopyPb(&mbf, PvAddBv(pmbh, cb - size(MBF)), size(MBF));

    // link the block
    _LinkMbh(pmbh);

    // update statistics
    pdmagl->Allocate(cb - size(MBF) - size(MBH));

    AssertPvAlloced(*ppv, cb - size(MBF) - size(MBH));
#endif // DEBUG

    return fTrue;
}

/***************************************************************************
    Resizes the given block.  *ppv may change.  If fmemClear, clears any
    newly added space.
***************************************************************************/
#ifdef DEBUG
bool _FResizePpvDebug(void **ppv, LegacyLong cbNew, LegacyLong cbOld, LegacyUlong grfmem, LegacyLong mpr, DebugMemoryAllocatorGlobals *pdmagl)
#else  //! DEBUG
bool _FResizePpv(void **ppv, LegacyLong cbNew, LegacyLong cbOld, LegacyUlong grfmem, LegacyLong mpr)
#endif //! DEBUG
{
    AssertVarMem(ppv);
    AssertIn(cbNew, 0, kcbMax);
    AssertIn(cbOld, 0, kcbMax);
    AssertPvAlloced(*ppv, cbOld);
    const LegacyLong cbOldPayload = cbOld; // Preserve caller units on every failure path.
    LegacyLong cbFree;
    void *pvNew, *pvOld;

#ifdef DEBUG
    MBH *pmbh = (MBH *)PvSubBv(*ppv, size(MBH));
    _AssertMbh(pmbh);
#endif // DEBUG

    if (cbNew > kcbMax)
    {
        BugVar("who's resizing a humongous block?", &cbNew);
        goto LFail;
    }

    pvOld = *ppv;
#ifdef DEBUG
    // do simulated failure - we can only fail if the block is growing
    if (cbNew > cbOld && pdmagl->FFail())
        goto LFail;

    // assert we don't overflow (the limit of kcbMax should ensure this)
    Assert(cbOld + size(MBH) + size(MBF) > cbOld, 0);
    Assert(cbNew + size(MBH) + size(MBF) > cbNew, 0);
    cbOld += size(MBH) + size(MBF);
    cbNew += size(MBH) + size(MBF);
    AssertVar(pmbh->cb == cbOld, "bad cbOld value passed to _FResizePpv", &cbOld);

    // Do not trash truncated bytes before realloc: a failed shrink must leave
    // the original payload and footer intact, just like a failed growth.
    pvOld = pmbh;
#endif // DEBUG

    for (;;)
    {
        pvNew = KauaiPlatform::Reallocate(pvOld, cbNew);

        if (pvNil != pvNew || pvNil == vpfnlib)
            break;

        vmutxMem.Enter();
        if (_fInLiberator)
            cbFree = 0;
        else
        {
            _fInLiberator = fTrue;
            vmutxMem.Leave();
            cbFree = (*vpfnlib)(cbNew - cbOld, mpr);
            vmutxMem.Enter();
            _fInLiberator = fFalse;
        }
        vmutxMem.Leave();

        if (cbFree <= 0)
            break;
    }

    if (pvNil == pvNew)
    {
    LFail:
        AssertPvAlloced(*ppv, cbOldPayload);
        PushErc(ercOomPv);

        return fFalse;
    }
    *ppv = pvNew;

    if ((grfmem & fmemClear) && cbOld < cbNew)
    {
        // Clear the new stuff
        ClearPb(PvAddBv(pvNew, cbOld), cbNew - cbOld);
    }

#ifdef DEBUG
    if ((grfmem & fmemClear) && cbOld < cbNew)
        ClearPb(PvAddBv(pvNew, cbOld - size(MBF)), size(MBF));
    else if (cbOld < cbNew)
    {
        // fill the new stuff with garbage
        FillPb(PvAddBv(pvNew, cbOld - size(MBF)), cbNew - cbOld + size(MBF), kbGarbage);
    }

    // update the header
    if (pvNew != pmbh)
    {
        _UnlinkMbh((MBH *)pvNew, pmbh);
        pmbh = (MBH *)pvNew;
        _LinkMbh(pmbh);
    }
    *ppv = pmbh + 1;
    pmbh->cb = cbNew;

    // write the footer
    MBF mbf;
    mbf.swMagic = kswMagicMem;
    CopyPb(&mbf, PvAddBv(pmbh, cbNew - size(MBF)), size(MBF));
    AssertPvAlloced(*ppv, cbNew - size(MBF) - size(MBH));

    // update statistics
    pdmagl->Resize(cbNew - cbOld);
#endif // DEBUG

    return fTrue;
}

/***************************************************************************
    If *ppv is not nil, frees it and sets *ppv to nil.
***************************************************************************/
#ifdef DEBUG
void FreePpvDebug(void **ppv, DebugMemoryAllocatorGlobals *pdmagl)
#else  //! DEBUG
void FreePpv(void **ppv)
#endif //! DEBUG
{
    AssertVarMem(ppv);
    if (*ppv == pvNil)
        return;

#ifdef DEBUG
    MBH *pmbh = (MBH *)PvSubBv(*ppv, size(MBH));
    _AssertMbh(pmbh);
    _UnlinkMbh(pmbh, pmbh);

    // update statistics
    pdmagl->Free(pmbh->cb - size(MBF) - size(MBH));

    // fill the block with garbage before freeing it
    FillPb(pmbh, pmbh->cb, kbGarbage);
    *ppv = pmbh;
#endif // DEBUG

    KauaiPlatform::Free(*ppv);
    *ppv = pvNil;
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
