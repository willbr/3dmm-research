/* Copyright (c) Microsoft Corporation.
   Licensed under the MIT License. */

/***************************************************************************
    Author: ShonK
    Project: Kauai
    Reviewed:
    Copyright (c) Microsoft Corporation

    Cursor class.

***************************************************************************/
#ifdef KAUAI_SDL3
#include "sdl_host.h"
#include "cursor_data.h"
#endif
#include "frame.h"
ASSERTNAME

RTCLASS(Cursor)

/***************************************************************************
    Destructor for the cursor class.
***************************************************************************/
Cursor::~Cursor(void)
{
#ifdef KAUAI_SDL3
    KauaiSDL::Main().DestroyCursor(_sdlCursor);
#elif defined(WIN)
    if (hNil != _hcrs)
        DestroyCursor(_hcrs);
#endif // WIN
}

/***************************************************************************
    Read a cursor out of a ChunkyResourceFile.
***************************************************************************/
bool Cursor::FReadCurs(PChunkyResourceFile pcrf, ChunkTagOrType ctg, ChunkNumber cno, PDataBlock pblck, PBaseCacheableObject *ppbaco, LegacyLong *pcb)
{
#ifdef KAUAI_SDL3
    *pcb = size(Cursor);
    if (!ppbaco)
        return fTrue;
    *ppbaco = pvNil;
    KauaiCursors::Monochrome image;
    if (!KauaiCursors::Read(*pblck, 32, 32, image))
        return fFalse;
    // Match the original 32-pixel cursor canvas even for a 16-pixel image.
    byte andBits[128], xorBits[128];
    FillPb(andBits, size(andBits), 255);
    ClearPb(xorBits, size(xorBits));
    for (int y = 0; y < image.height; ++y)
    {
        CopyPb(image.andBits.data() + y * image.pitch, andBits + y * 4, image.pitch);
        CopyPb(image.xorBits.data() + y * image.pitch, xorBits + y * 4, image.pitch);
    }
    const auto id = KauaiSDL::Main().CreateCursor(andBits, xorBits, 32, 32, 4, image.hotX, image.hotY);
    if (!id)
        return fFalse;
    auto cursor = NewObj Cursor;
    if (!cursor)
    {
        KauaiSDL::Main().DestroyCursor(id);
        return fFalse;
    }
    cursor->_sdlCursor = id;
    *ppbaco = cursor;
    return fTrue;
#else
    PGeneralGroup pggcurf;
    LegacyLong icurf, icurfBest;
    CursorOnFile curf;
    short bo;
    LegacyLong dxp, dyp, dzpT;
    LegacyLong dzpBest;
    LegacyLong cbRowDst, cbRowSrc, cbT;
    byte *prgb, *qrgb;
    PCursor pcurs = pvNil;

    *pcb = size(Cursor);
    if (pvNil == ppbaco)
        return fTrue;

    if (pvNil == (pggcurf = GeneralGroup::PggRead(pblck, &bo)) || pggcurf->IvMac() == 0)
    {
        ReleasePpo(&pggcurf);
        return fFalse;
    }

#if defined(MAC) && !defined(KAUAI_SDL3)
    dxp = dyp = 16;
#endif // MAC
#ifdef WIN
    dxp = GetSystemMetrics(SM_CXCURSOR);
    dyp = GetSystemMetrics(SM_CYCURSOR);
#endif // WIN

    icurfBest = 0;
    dzpBest = klwMax;
    for (icurf = 0; icurf < pggcurf->IvMac(); icurf++)
    {
        pggcurf->GetFixed(icurf, &curf);
        if (kboOther == bo)
            SwapBytesBom(&curf, kbomCurf);
        if (curf.dxp > dxp || curf.dyp > dyp || curf.curt != curtMonochrome ||
            pggcurf->Cb(icurf) != (LegacyLong)curf.dxp * curf.dyp / 4)
        {
            continue;
        }

        dzpT = (dxp - curf.dxp) + (dyp - curf.dyp);
        if (dzpBest > dzpT)
        {
            icurfBest = icurf;
            if (dzpT == 0)
                break;
            dzpBest = dzpT;
        }
    }
    AssertIn(icurfBest, 0, pggcurf->IvMac());
    pggcurf->GetFixed(icurfBest, &curf);
    if (kboOther == bo)
        SwapBytesBom(&curf, kbomCurf);
    cbRowSrc = LwRoundAway(LwDivAway(curf.dxp, 8), 2);
    cbRowDst = LwRoundAway(LwDivAway(dxp, 8), 2);

    if (!FAllocPv((void **)&prgb, LwMul(cbRowDst, 2 * dyp), fmemClear, mprNormal))
        goto LFail;

    if (pvNil == (pcurs = NewObj Cursor))
        goto LFail;

    FillPb(prgb, LwMul(cbRowDst, dyp), 0xFF);
    qrgb = (byte *)pggcurf->QvGet(icurfBest);
    cbT = LwMin(cbRowSrc, cbRowDst);
    for (dzpT = LwMin(dyp, curf.dyp); dzpT-- > 0;)
    {
        CopyPb(qrgb + LwMul(dzpT, cbRowSrc), prgb + LwMul(dzpT, cbRowDst), cbT);
        CopyPb(qrgb + LwMul(curf.dyp + dzpT, cbRowSrc), prgb + LwMul(dyp + dzpT, cbRowDst), cbT);
    }

#ifdef WIN
    pcurs->_hcrs = CreateCursor(vwig.hinst, curf.xp, curf.yp, dxp, dyp, prgb, prgb + LwMul(dxp, cbRowDst));
    if (hNil == pcurs->_hcrs)
        ReleasePpo(&pcurs);
#endif // WIN
#if defined(MAC) && !defined(KAUAI_SDL3)
    Assert(dxp == 16, 0);
    LegacyLong *plwAnd, *plwXor;
    LegacyLong ilw;

    plwAnd = (LegacyLong *)prgb;
    plwXor = plwAnd + 8;
    pcurs->_crs.hotSpot.h = curf.xp;
    pcurs->_crs.hotSpot.v = curf.yp;
    for (ilw = 0; ilw < 8; ilw++)
    {
        ((LegacyLong *)pcurs->_crs.mask)[ilw] = ~*plwAnd;
        ((LegacyLong *)pcurs->_crs.data)[ilw] = ~*plwAnd++ ^ *plwXor++;
    }
#endif // MAC

LFail:
    FreePpv((void **)&prgb);
    ReleasePpo(&pggcurf);

    *ppbaco = pcurs;
    return pvNil != pcurs;
#endif
}

/***************************************************************************
    Set the cursor.
***************************************************************************/
void Cursor::Set(void)
{
#ifdef KAUAI_SDL3
    KauaiSDL::Main().SetCursor(_sdlCursor);
#elif defined(WIN)
    SetCursor(_hcrs);
#endif // WIN
#if defined(MAC) && !defined(KAUAI_SDL3)
    SetCursor(&_crs);
#endif // MAC
}
