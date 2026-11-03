/* Copyright (c) Microsoft Corporation.
   Licensed under the MIT License. */

/* Copyright (c) Microsoft Corporation.
   Licensed under the MIT License. */

/***************************************************************************
    Author: ShonK
    Project: Kauai
    Reviewed:
    Copyright (c) Microsoft Corporation

    Memory handling

***************************************************************************/
#ifndef UTILMEM_H
#define UTILMEM_H

// used for asserts and limiting memory
const byte kbGarbage = 0xA3;    // new blocks are filled with this
const LegacyLong kcbMax = 0x08000000; // 128 Megabytes
const short kswMagicMem = (short)0xA253;
const LegacyLong klwMagicMem = (LegacyLong)0xA253A253;

/***************************************************************************
    When an allocation fails, vpfnlib is called to free some memory (if it's
    not nil).
***************************************************************************/
typedef LegacyLong (*PFNLIB)(LegacyLong cb, LegacyLong mpr);
extern PFNLIB vpfnlib;
extern bool _fInAlloc;

/****************************************
    OS memory handles and management
****************************************/
#ifdef MAC
typedef Handle HN;
// version of SetHandleSize that returns an error code
inline short ErrSetHandleSize(HN hn, Size cb)
{
    SetHandleSize(hn, cb);
    return MemError();
}

// address stipper
class ADST
{
  private:
    LegacyLong _lwMaskAddress;

  public:
    ADST(void);

    void *PvStrip(void *pv)
    {
        return (void *)((LegacyLong)pv & _lwMaskAddress);
    }
};
extern ADST vadst;

#else
typedef CoreNative::MemoryHandle HN;
#endif

/****************************************
    Moveable/resizeable memory management
****************************************/
typedef void *HQ;
#define hNil 0
#define hqNil ((HQ)0)

// memory request priority
enum
{
    // lower priority
    mprDebug,
    mprForSpeed,
    mprNormal,
    mprCritical,
    // higher priority
};

// memory allocation options
enum
{
    fmemNil = 0,
    fmemClear = 1,
};

void FreePhq(HQ *phq);
LegacyLong CbOfHq(HQ hq);
bool FCopyHq(HQ hqSrc, HQ *phqDst, LegacyLong mpr);
bool FResizePhq(HQ *phq, LegacyLong cb, LegacyUlong grfmem, LegacyLong mpr);
void *PvLockHq(HQ hq);
void UnlockHq(HQ hq);

#ifdef DEBUG

// debug memory allocator globals
// enter vmutxMem before modifying these...
struct DebugMemoryAllocatorGlobals
{
    LegacyLong cv;    // number of allocations
    LegacyLong cvTot; // total number of allocations over all time
    LegacyLong cvRun; // running max of cv
    LegacyLong cb;    // total size of allocations
    LegacyLong cbRun; // running max of cb

    LegacyLong cactDo;   // number of times to succeed before failing
    LegacyLong cactFail; // number of times to fail

    bool FFail(void);
    void Allocate(LegacyLong cbT);
    void Resize(LegacyLong dcb);
    void Free(LegacyLong cbT);
};

// debug memory globals
struct DebugMemoryGlobals
{
    DebugMemoryAllocatorGlobals dmaglBase; // for NewObj
    DebugMemoryAllocatorGlobals dmaglHq;   // for HQs
    DebugMemoryAllocatorGlobals dmaglPv;   // for FAllocPv, etc
};
extern DebugMemoryGlobals vdmglob;

extern LegacyLong vcactSuspendCheckPointers;
#define SuspendCheckPointers() vcactSuspendCheckPointers++;
#define ResumeCheckPointers() vcactSuspendCheckPointers--;

bool FAllocHqDebug(HQ *phq, LegacyLong cb, LegacyUlong grfmem, LegacyLong mpr, schar *pszsFile, LegacyLong lwLine);
#define FAllocHq(phq, cb, grfmem, mpr) FAllocHqDebug(phq, cb, grfmem, mpr, __szsFile, __LINE__)
void *QvFromHq(HQ hq);

void AssertHq(HQ hq);
void MarkHq(HQ hq);
#ifdef MAC
void _AssertUnmarkedHqs(void);
void _UnmarkAllHqs(void);
#endif // MAC

#else //! DEBUG

#define FAllocHqDebug(phq, cb, grfmem, mpr, pszsFile, luLine) FAllocHq(phq, cb, grfmem, mpr)
bool FAllocHq(HQ *phq, LegacyLong cb, LegacyUlong grfmem, LegacyLong mpr);
#ifdef MAC
inline void *QvFromHq(HQ hq)
{
    return vadst.PvStrip(*(void **)hq);
}
#else
inline void *QvFromHq(HQ hq)
{
    return (void *)hq;
}
#endif // WIN

#define AssertHq(hq)
#define MarkHq(hq)

#endif //! DEBUG

/****************************************
    Fixed (non-moveable) memory.
****************************************/
#ifdef DEBUG

// allocation routine
bool FAllocPvDebug(void **ppv, LegacyLong cb, LegacyUlong grfmem, LegacyLong mpr, schar *pszsFile, LegacyLong lwLine, DebugMemoryAllocatorGlobals *pdmagl);
#define FAllocPv(ppv, cb, grfmem, mpr) FAllocPvDebug(ppv, cb, grfmem, mpr, __szsFile, __LINE__, &vdmglob.dmaglPv)

// Resizing preserves the original block on failure.
bool _FResizePpvDebug(void **ppv, LegacyLong cbNew, LegacyLong cbOld, LegacyUlong grfmem, LegacyLong mpr, DebugMemoryAllocatorGlobals *pdmagl);

// freeing routine
void FreePpvDebug(void **ppv, DebugMemoryAllocatorGlobals *pdmagl);
#define FreePpv(ppv) FreePpvDebug(ppv, &vdmglob.dmaglPv)

void AssertPvAlloced(void *pv, LegacyLong cb);
void AssertUnmarkedMem(void);
void UnmarkAllMem(void);
void MarkPv(void *pv);

#else //! DEBUG

#define SuspendCheckPointers()
#define ResumeCheckPointers()

// allocation routine
#define FAllocPvDebug(ppv, cb, grfmem, mpr, pszsFile, luLine, pdmagl) FAllocPv(ppv, cb, grfmem, mpr)
bool FAllocPv(void **ppv, LegacyLong cb, LegacyUlong grfmem, LegacyLong mpr);

// Resizing preserves the original block on failure.
#define _FResizePpvDebug(ppv, cbNew, cbOld, grfmem, mpr, pdmagl) _FResizePpv(ppv, cbNew, cbOld, grfmem, mpr)
bool _FResizePpv(void **ppv, LegacyLong cbNew, LegacyLong cbOld, LegacyUlong grfmem, LegacyLong mpr);

// freeing routine
#define FreePpvDebug(ppv, pdmagl) FreePpv(ppv)
void FreePpv(void **ppv);

#define AssertPvAlloced(pv, cb)
#define AssertUnmarkedMem()
#define UnmarkAllMem()
#define MarkPv(pv)
#endif //! DEBUG

/****************************************
    Memory trashing
****************************************/
#ifdef DEBUG

#define TrashVar(pfoo)                                                                                                 \
    if (pvNil != (pfoo))                                                                                               \
        FillPb(pfoo, size(*(pfoo)), kbGarbage);                                                                        \
    else                                                                                                               \
        (void)0
#define TrashVarIf(f, pfoo)                                                                                            \
    if ((f) && pvNil != (pfoo))                                                                                        \
        FillPb(pfoo, size(*(pfoo)), kbGarbage);                                                                        \
    else                                                                                                               \
        (void)0
#define TrashPvCb(pv, cb)                                                                                              \
    if (pvNil != (pv))                                                                                                 \
        FillPb(pv, cb, kbGarbage);                                                                                     \
    else                                                                                                               \
        (void)0
#define TrashPvCbIf(f, pv, cb)                                                                                         \
    if ((f) && pvNil != (pv))                                                                                          \
        FillPb(pv, cb, kbGarbage);                                                                                     \
    else                                                                                                               \
        (void)0

#else //! DEBUG

#define TrashVar(pfoo)
#define TrashVarIf(f, pfoo)
#define TrashPvCb(pv, cb)
#define TrashPvCbIf(f, pv, cb)

#endif //! DEBUG

/****************************************
    Pointer arithmetic
****************************************/
inline void *PvAddBv(void *pv, LegacyLong bv)
{
    return (byte *)pv + bv;
}
inline void *PvSubBv(void *pv, LegacyLong bv)
{
    return (byte *)pv - bv;
}
inline LegacyLong BvSubPvs(void *pv1, void *pv2)
{
    return (byte *)pv1 - (byte *)pv2;
}

/****************************************
    Mutex (critical section) object
****************************************/
// Mutex is supplied by core_platform.h, without OS handle types.

extern Mutex vmutxMem;

/****************************************
    Current thread id
****************************************/
inline LegacyLong LwThreadCur(void)
{
    return KauaiPlatform::ThreadToken();
}

#endif //! UTILMEM_H
