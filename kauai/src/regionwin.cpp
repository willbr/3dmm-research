/* Copyright (c) Microsoft Corporation.
   Licensed under the MIT License. */

/***************************************************************************
    regionwin.cpp -- Win32 GDI HRGN helpers.

    Thin wrappers over CreateRectRgn / DeleteObject / CombineRgn /
    SetRectRgn / GetRgnBox / EqualRgn that region.cpp's Region class
    needs to implement union/intersect/diff/etc. Originally lived in
    gfxwin.cpp, but region.cpp doesn't pull in any other gfxwin
    symbols, so splitting them out lets region.cpp be promoted to
    kauai-core and unblocks pure-data consumers (geometry-test and
    any future CLI tool that uses Region).

    No UI / GraphicsObject / GraphicsPort / Application dependencies --
    just HRGN handles plus the kauai Assert/RC types.
***************************************************************************/
#include "kauai_core.h"
#include "region.h"

ASSERTNAME

/***************************************************************************
    Create a new rectangular region.  If prc is nil, the region will be
    empty.
***************************************************************************/
bool FCreateRgn(HRGN *phrgn, RC *prc)
{
    AssertVarMem(phrgn);
    AssertNilOrVarMem(prc);
    SystemRectangle rcs;

    if (pvNil == prc)
        ClearPb(&rcs, size(rcs));
    else
        rcs = *prc;

    *phrgn = CreateRectRgnIndirect(&rcs);
    return *phrgn != hNil;
}

/***************************************************************************
    Free the region and set *phrgn to nil.
***************************************************************************/
void FreePhrgn(HRGN *phrgn)
{
    AssertVarMem(phrgn);

    if (*phrgn != hNil)
    {
        DeleteObject(*phrgn);
        *phrgn = hNil;
    }
}

/***************************************************************************
    Make the region rectangular.  If prc is nil, the region will be empty.
    If *phrgn is hNil, creates the region.  *phrgn may change even if
    *phrgn is not nil.
***************************************************************************/
bool FSetRectRgn(HRGN *phrgn, RC *prc)
{
    AssertVarMem(phrgn);
    AssertNilOrVarMem(prc);

    if (hNil == *phrgn)
        return FCreateRgn(phrgn, prc);
    if (pvNil == prc)
        return SetRectRgn(*phrgn, 0, 0, 0, 0);
    return SetRectRgn(*phrgn, prc->xpLeft, prc->ypTop, prc->xpRight, prc->ypBottom);
}

/***************************************************************************
    Put the union of hrgnSrc1 and hrgnSrc2 into hrgnDst.  The parameters
    need not be distinct.  Returns success/failure.
***************************************************************************/
bool FUnionRgn(HRGN hrgnDst, HRGN hrgnSrc1, HRGN hrgnSrc2)
{
    Assert(hNil != hrgnDst, "null dst");
    Assert(hNil != hrgnSrc1, "null src1");
    Assert(hNil != hrgnSrc2, "null src2");
    return ERROR != CombineRgn(hrgnDst, hrgnSrc1, hrgnSrc2, RGN_OR);
}

/***************************************************************************
    Put the intersection of hrgnSrc1 and hrgnSrc2 into hrgnDst.  The parameters
    need not be distinct.  Returns success/failure.
***************************************************************************/
bool FIntersectRgn(HRGN hrgnDst, HRGN hrgnSrc1, HRGN hrgnSrc2, bool *pfEmpty)
{
    Assert(hNil != hrgnDst, "null dst");
    Assert(hNil != hrgnSrc1, "null src1");
    Assert(hNil != hrgnSrc2, "null src2");
    long lw;

    lw = CombineRgn(hrgnDst, hrgnSrc1, hrgnSrc2, RGN_AND);
    if (ERROR == lw)
        return fFalse;
    if (pvNil != pfEmpty)
        *pfEmpty = (lw == NULLREGION);
    return fTrue;
}

/***************************************************************************
    Put hrgnSrc - hrgnSrcSub into hrgnDst.  The parameters need not be
    distinct.  Returns success/failure.
***************************************************************************/
bool FDiffRgn(HRGN hrgnDst, HRGN hrgnSrc, HRGN hrgnSrcSub, bool *pfEmpty)
{
    Assert(hNil != hrgnDst, "null dst");
    Assert(hNil != hrgnSrc, "null src");
    Assert(hNil != hrgnSrcSub, "null srcSub");
    long lw;

    lw = CombineRgn(hrgnDst, hrgnSrc, hrgnSrcSub, RGN_DIFF);
    if (ERROR == lw)
        return fFalse;
    if (pvNil != pfEmpty)
        *pfEmpty = (lw == NULLREGION);
    return fTrue;
}

/***************************************************************************
    Determine if the region is rectangular and put the bounding rectangle
    in *prc (if not nil).
***************************************************************************/
bool FRectRgn(HRGN hrgn, RC *prc)
{
    Assert(hNil != hrgn, "null rgn");
    SystemRectangle rcs;
    bool fRet;

    fRet = GetRgnBox(hrgn, &rcs) != COMPLEXREGION;
    if (pvNil != prc)
        *prc = rcs;
    return fRet;
}

/***************************************************************************
    Return true iff the region is empty.
***************************************************************************/
bool FEmptyRgn(HRGN hrgn, RC *prc)
{
    Assert(hNil != hrgn, "null rgn");
    SystemRectangle rcs;
    bool fRet;

    fRet = GetRgnBox(hrgn, &rcs) == NULLREGION;
    if (pvNil != prc)
        *prc = rcs;
    return fRet;
}

/***************************************************************************
    Return true iff the two regions are equal.
***************************************************************************/
bool FEqualRgn(HRGN hrgn1, HRGN hrgn2)
{
    Assert(hNil != hrgn1, "null rgn1");
    Assert(hNil != hrgn2, "null rgn2");
    return EqualRgn(hrgn1, hrgn2);
}
