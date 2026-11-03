/* Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "frame.h"
ASSERTNAME

AbstractPattern vaptGray = {0xAA, 0x55, 0xAA, 0x55, 0xAA, 0x55, 0xAA, 0x55};
AbstractPattern vaptLtGray = {0x22, 0x88, 0x44, 0x11, 0x22, 0x88, 0x44, 0x11};
AbstractPattern vaptDkGray = {0xDD, 0x77, 0xBB, 0xEE, 0xDD, 0x77, 0xBB, 0xEE};

/***************************************************************************
    Set the AbstractColor from the lw.  The lw should have been returned by a call
    to AbstractColor::LwGet().
***************************************************************************/
void AbstractColor::SetFromLw(long lw)
{
    _lu = (ulong)lw;
    AssertThis(0);
}

/***************************************************************************
    Get an lw from the AbstractColor.  The lw can then be stored on file, reread
    and passed to AbstractColor::SetFromLw.  Valid non-nil colors always return
    non-zero, so zero can be used as a nil value.
***************************************************************************/
long AbstractColor::LwGet(void) const
{
    return (long)_lu;
}

/***************************************************************************
    Get a clr from the AbstractColor.  Asserts that the acr is an rgb color.
***************************************************************************/
void AbstractColor::GetClr(Color *pclr)
{
    AssertThis(facrRgb);
    AssertVarMem(pclr);

    pclr->bRed = B2Lw(_lu);
    pclr->bGreen = B1Lw(_lu);
    pclr->bBlue = B0Lw(_lu);
    pclr->bZero = 0;
}

#ifdef DEBUG
/***************************************************************************
    Assert that the acr is a valid color.
***************************************************************************/
void AbstractColor::AssertValid(LegacyUlong grfacr)
{
    switch (B3Lw(_lu))
    {
    case kbNilAcr:
        Assert(grfacr == facrNil, "unexpected nil AbstractColor");
        break;
    case kbRgbAcr:
        Assert(grfacr == facrNil || (grfacr & facrRgb), "unexpected RGB AbstractColor");
        break;
    case kbIndexAcr:
        Assert(B2Lw(_lu) == 0 && B1Lw(_lu) == 0, "bad Index AbstractColor");
        Assert(grfacr == facrNil || (grfacr & facrIndex), "unexpected Index AbstractColor");
        break;
    case kbSpecialAcr:
        Assert(_lu == kluAcrClear || _lu == kluAcrInvert, "unknown acr");
        Assert(grfacr == facrNil, "unexpected Special AbstractColor");
        break;
    default:
        BugVar("invalid AbstractColor", &_lu);
        break;
    }
}
#endif // DEBUG

/***************************************************************************
    Change the origin on the pattern.
***************************************************************************/
void AbstractPattern::MoveOrigin(long dxp, long dyp)
{
    // this cast to ulong works because 2^32 is a multiple of 8.
    dxp = (ulong)dxp % 8;
    dyp = (ulong)dyp % 8;
    if (dxp != 0)
    {
        byte *pb;

        for (pb = rgb + 8; pb-- != rgb;)
            *pb = (*pb >> dxp) | (*pb << (8 - dxp));
    }
    if (dyp != 0)
        SwapBlocks(rgb, 8 - dyp, dyp);
}
