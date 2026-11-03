/* Copyright (c) Microsoft Corporation.
   Licensed under the MIT License. */

/* Copyright (c) Microsoft Corporation.
   Licensed under the MIT License. */

/***************************************************************************
    Author: ShonK
    Project: Kauai
    Reviewed:
    Copyright (c) Microsoft Corporation

    Scalar, rectangle and point declarations

***************************************************************************/
#ifndef UTILINT_H
#define UTILINT_H

#include <cstdint>

/****************************************
    Scalar constants
****************************************/
const bool fTrue = 1;
const bool fFalse = 0;
enum tribool
{
    tNo,
    tYes,
    tMaybe,
    tLim
};
#define AssertT(t) AssertIn(t, 0, tLim)

// standard comparison flags
enum
{
    fcmpEq = 0x0001,
    fcmpGt = 0x0002,
    fcmpLt = 0x0004,
};
const LegacyUlong kgrfcmpGe = (fcmpEq | fcmpGt);
const LegacyUlong kgrfcmpLe = (fcmpEq | fcmpLt);
const LegacyUlong kgrfcmpNe = (fcmpGt | fcmpLt);

#define FPure(f) ((f) != fFalse)
#define ivNil (-1L)
#define bvNil (-1L)
#define cvNil (-1L)
#define pvNil 0

/****************************************
    Memory access asserts
****************************************/
#ifdef DEBUG
void AssertPvCb(void *pv, LegacyLong cb);
inline void AssertNilOrPvCb(void *pv, LegacyLong cb)
{
    if (pv != pvNil)
        AssertPvCb(pv, cb);
}
#else //! DEBUG
#define AssertPvCb(pv, cb)
#define AssertNilOrPvCb(pv, cb)
#endif //! DEBUG

#define AssertThisMem() AssertPvCb(this, size(*this))
#define AssertVarMem(pvar) AssertPvCb(pvar, size(*(pvar)))
#define AssertNilOrVarMem(pvar) AssertNilOrPvCb(pvar, size(*(pvar)))

/****************************************
    Scalar APIs
****************************************/
inline bool FIn(LegacyLong lw, LegacyLong lwMin, LegacyLong lwLim)
{
    return lw >= lwMin && lw < lwLim;
}
inline LegacyLong LwBound(LegacyLong lw, LegacyLong lwMin, LegacyLong lwMax)
{
    return lw < lwMin ? lwMin : lw >= lwMax ? lwMax - 1 : lw;
}
void SortLw(LegacyLong *plw1, LegacyLong *plw2);
// Ordering in-memory indices/coordinates does not depend on stored word width.
// Retain the fixed-width overload above for existing core callers and ABI.
template <class T> inline void SortLw(T *first, T *second)
{
    if (*first > *second)
    {
        const T value = *first;
        *first = *second;
        *second = value;
    }
}

inline short SwHigh(LegacyLong lw)
{
    return (short)(lw >> 16);
}
inline short SwLow(LegacyLong lw)
{
    return (short)lw;
}
inline LegacyLong LwHighLow(short swHigh, short swLow)
{
    return static_cast<LegacyLong>((static_cast<LegacyUlong>(static_cast<ushort>(swHigh)) << 16) | static_cast<ushort>(swLow));
}
inline LegacyUlong LuHighLow(ushort suHigh, ushort suLow)
{
    return ((LegacyUlong)suHigh << 16) | (LegacyUlong)suLow;
}
inline byte B0Lw(LegacyLong lw)
{
    return (byte)lw;
}
inline byte B1Lw(LegacyLong lw)
{
    return (byte)(lw >> 8);
}
inline byte B2Lw(LegacyLong lw)
{
    return (byte)(lw >> 16);
}
inline byte B3Lw(LegacyLong lw)
{
    return (byte)(lw >> 24);
}
inline LegacyLong LwFromBytes(byte b3, byte b2, byte b1, byte b0)
{
    return static_cast<LegacyLong>((static_cast<LegacyUlong>(b3) << 24) | (static_cast<LegacyUlong>(b2) << 16) | (static_cast<LegacyUlong>(b1) << 8) | b0);
}

inline ushort SuHigh(LegacyLong lw)
{
    return (ushort)((LegacyUlong)lw >> 16);
}
inline ushort SuLow(LegacyLong lw)
{
    return (ushort)lw;
}

inline byte BHigh(short sw)
{
    return (byte)((ushort)sw >> 8);
}
inline byte BLow(short sw)
{
    return (byte)sw;
}
inline short SwHighLow(byte bHigh, byte bLow)
{
    return ((short)bHigh << 8) | (short)bLow;
}
inline ushort SuHighLow(byte bHigh, byte bLow)
{
    return ((ushort)bHigh << 8) | (ushort)bLow;
}

inline short SwTruncLw(LegacyLong lw)
{
    return lw <= kswMax ? (lw >= kswMin ? (short)lw : kswMin) : kswMax;
}
inline short SwMin(short sw1, short sw2)
{
    return sw1 < sw2 ? sw1 : sw2;
}
inline short SwMax(short sw1, short sw2)
{
    return sw1 >= sw2 ? sw1 : sw2;
}
inline ushort SuMin(ushort su1, ushort su2)
{
    return su1 < su2 ? su1 : su2;
}
inline ushort SuMax(ushort su1, ushort su2)
{
    return su1 >= su2 ? su1 : su2;
}
inline LegacyLong LwMin(LegacyLong lw1, LegacyLong lw2)
{
    return lw1 < lw2 ? lw1 : lw2;
}
inline LegacyLong LwMax(LegacyLong lw1, LegacyLong lw2)
{
    return lw1 >= lw2 ? lw1 : lw2;
}
inline LegacyUlong LuMin(LegacyUlong lu1, LegacyUlong lu2)
{
    return lu1 < lu2 ? lu1 : lu2;
}
inline LegacyUlong LuMax(LegacyUlong lu1, LegacyUlong lu2)
{
    return lu1 >= lu2 ? lu1 : lu2;
}

inline short SwAbs(short sw)
{
    return sw < 0 ? -sw : sw;
}
inline LegacyLong LwAbs(LegacyLong lw)
{
    return lw < 0 ? -lw : lw;
}

inline LegacyLong LwMulSw(short sw1, short sw2)
{
    return (LegacyLong)sw1 * sw2;
}

#ifdef MC_68020

/***************************************************************************
    Motorola 68020 routines.
***************************************************************************/
extern "C"
{
    LegacyLong __cdecl LwMulDiv(LegacyLong lw, LegacyLong lwMul, LegacyLong lwDiv);
    void __cdecl MulLw(LegacyLong lw1, LegacyLong lw2, LegacyLong *plwHigh, LegacyUlong *pluLow);
    LegacyUlong __cdecl LuMulDiv(LegacyUlong lu, LegacyUlong luMul, LegacyUlong luDiv);
    void __cdecl MulLu(LegacyUlong lu1, LegacyUlong lu2, LegacyUlong *pluHigh, LegacyUlong *pluLow);
}
LegacyLong LwMulDivMod(LegacyLong lw, LegacyLong lwMul, LegacyLong lwDiv, LegacyLong *plwRem);

#elif defined(IN_80386)

/***************************************************************************
    Intel 80386 routines.
***************************************************************************/
inline LegacyLong LwMulDiv(LegacyLong lw, LegacyLong lwMul, LegacyLong lwDiv)
{
    AssertH(lwDiv != 0);
    __asm
    {
		mov		eax,lw
		imul	lwMul
		idiv	lwDiv
		mov		lw,eax
    }
    return lw;
}

inline LegacyLong LwMulDivMod(LegacyLong lw, LegacyLong lwMul, LegacyLong lwDiv, LegacyLong *plwRem)
{
    AssertH(lwDiv != 0);
    AssertVarMem(plwRem);
    __asm
    {
		mov		eax,lw
		imul	lwMul
		idiv	lwDiv
		mov		ecx,plwRem
		mov		DWORD PTR[ecx],edx
		mov		lw,eax
    }
    return lw;
}

void MulLw(LegacyLong lw1, LegacyLong lw2, LegacyLong *plwHigh, LegacyUlong *pluLow);
LegacyUlong LuMulDiv(LegacyUlong lu, LegacyUlong luMul, LegacyUlong luDiv);
void MulLu(LegacyUlong lu1, LegacyUlong lu2, LegacyUlong *pluHigh, LegacyUlong *pluLow);

#else //! MC_68020 && !IN_80386

/***************************************************************************
    Other processors.  These generally use floating point.
***************************************************************************/
LegacyLong LwMulDiv(LegacyLong lw, LegacyLong lwMul, LegacyLong lwDiv);
LegacyLong LwMulDivMod(LegacyLong lw, LegacyLong lwMul, LegacyLong lwDiv, LegacyLong *plwRem);
void MulLw(LegacyLong lw1, LegacyLong lw2, LegacyLong *plwHigh, LegacyUlong *pluLow);
LegacyUlong LuMulDiv(LegacyUlong lu, LegacyUlong luMul, LegacyUlong luDiv);
void MulLu(LegacyUlong lu1, LegacyUlong lu2, LegacyUlong *pluHigh, LegacyUlong *pluLow);

#endif //! MC_68020 && !IN_80386

LegacyLong LwMulDivAway(LegacyLong lw, LegacyLong lwMul, LegacyLong lwDiv);
LegacyUlong LuMulDivAway(LegacyUlong lu, LegacyUlong luMul, LegacyUlong luDiv);

LegacyUlong FcmpCompareFracs(LegacyLong lwNum1, LegacyLong lwDen1, LegacyLong lwNum2, LegacyLong lwDen2);

LegacyLong LwDivAway(LegacyLong lwNum, LegacyLong lwDen);
LegacyLong LwDivClosest(LegacyLong lwNum, LegacyLong lwDen);
LegacyLong LwRoundAway(LegacyLong lwSrc, LegacyLong lwBase);
LegacyLong LwRoundToward(LegacyLong lwSrc, LegacyLong lwBase);
LegacyLong LwRoundClosest(LegacyLong lwSrc, LegacyLong lwBase);

// Round to a multiple of the kauai 4-byte alignment quantum. The historical
// name says "Long" because in 1995 `long` was 4 bytes everywhere this code
// targeted; on LP64 `sizeof(long) == 8` and using it here would silently
// double the wire-format alignment, breaking every chunky-file payload that
// relies on this padding. The contract is 4 — see kauai/src/test.cpp's
// AssertDo(CbRoundToLong(1) == 4, ...) regression test.
inline LegacyLong CbRoundToLong(LegacyLong cb)
{
    constexpr LegacyLong kcbAlign = 4;
    return (cb + kcbAlign - 1) & ~(LegacyLong)(kcbAlign - 1);
}
inline LegacyLong CbRoundToShort(LegacyLong cb)
{
    return (cb + size(short) - 1) & ~(LegacyLong)(size(short) - 1);
}
inline LegacyLong CbFromCbit(LegacyLong cbit)
{
    return (cbit + 7) / 8;
}
inline byte Fbit(LegacyLong ibit)
{
    return 1 << (ibit & 0x0007);
}
inline LegacyLong IbFromIbit(LegacyLong ibit)
{
    return ibit >> 3;
}

LegacyLong LwGcd(LegacyLong lw1, LegacyLong lw2);
LegacyUlong LuGcd(LegacyUlong lu1, LegacyUlong lu2);

bool FAdjustIv(LegacyLong *piv, LegacyLong iv, LegacyLong cvIns, LegacyLong cvDel);

#ifdef DEBUG
void AssertIn(LegacyLong lw, LegacyLong lwMin, LegacyLong lwLim);
LegacyLong LwMul(LegacyLong lw1, LegacyLong lw2);
#else //! DEBUG
#define AssertIn(lw, lwMin, lwLim)
inline LegacyLong LwMul(LegacyLong lw1, LegacyLong lw2)
{
    return lw1 * lw2;
}
#endif //! DEBUG

/****************************************
    Byte Swapping
****************************************/

// byte order mask: a packed array of 16 2-bit lane descriptors used by
// SwapBytesBom to byteswap a struct field-by-field. Pinned to 32 bits via
// uint32_t so the kbom* literals (e.g. 0xC0000000) keep the same bit layout
// on every architecture; bare `unsigned long` would silently widen to 64 bits
// on LP64 (Linux/Mac x64) and reinterpret the high half as extra fields.
typedef uint32_t ByteOrderMask;

void SwapBytesBom(void *pv, ByteOrderMask bom);
void SwapBytesRgsw(void *psw, LegacyLong csw);
void SwapBytesRglw(void *plw, LegacyLong clw);

const ByteOrderMask bomNil = 0;
const ByteOrderMask kbomSwapShort = 0x40000000;
const ByteOrderMask kbomSwapLong = 0xC0000000;
const ByteOrderMask kbomLeaveShort = 0x00000000;
const ByteOrderMask kbomLeaveLong = 0x80000000;

/* You can chain up to 16 of these (2 bits each) */
#define BomField(bomNew, bomLast) ((bomNew) | ((bomLast) >> 2))

#ifdef DEBUG
void AssertBomRglw(ByteOrderMask bom, LegacyLong cb);
void AssertBomRgsw(ByteOrderMask bom, LegacyLong cb);
#else //! DEBUG
#define AssertBomRglw(bom, cb)
#define AssertBomRgsw(bom, cb)
#endif //! DEBUG

/****************************************
    OS level rectangle and point
****************************************/

#ifdef MAC
typedef Rect SystemRectangle;
typedef Point SystemPoint;
#else
typedef CoreNative::Rectangle SystemRectangle;
typedef CoreNative::Point SystemPoint;
#endif // WIN

/****************************************
    Rectangle and point stuff
****************************************/
// options for PT::Transform and RC::Transform
enum
{
    fptNil,
    fptNegateXp = 1,  // negate xp values (and swap them in an RC)
    fptNegateYp = 2,  // negate yp values (and swap them in an RC)
    fptTranspose = 4, // swap xp and yp values (done after negating)
};

class RC;
typedef class RC *PRC;

class PT
{
  public:
    LegacyLong xp;
    LegacyLong yp;

  public:
    // constructors
    PT(void)
    {
    }
    PT(LegacyLong xpT, LegacyLong ypT)
    {
        xp = xpT, yp = ypT;
    }

    // for assigning to/from a SystemPoint
    operator SystemPoint(void);
    PT &operator=(SystemPoint &pts);
    PT(SystemPoint &pts)
    {
        *this = pts;
    }

    // interaction with other points
    bool operator==(PT &pt)
    {
        return xp == pt.xp && yp == pt.yp;
    }
    bool operator!=(PT &pt)
    {
        return xp != pt.xp || yp != pt.yp;
    }
    PT operator+(PT &pt)
    {
        return PT(xp + pt.xp, yp + pt.yp);
    }
    PT operator-(PT &pt)
    {
        return PT(xp - pt.xp, yp - pt.yp);
    }
    PT &operator+=(PT &pt)
    {
        xp += pt.xp;
        yp += pt.yp;
        return *this;
    }
    PT &operator-=(PT &pt)
    {
        xp -= pt.xp;
        yp -= pt.yp;
        return *this;
    }
    void Offset(LegacyLong dxp, LegacyLong dyp)
    {
        xp += dxp;
        yp += dyp;
    }

    // map the point from prcSrc to prcDst coordinates
    void Map(RC *prcSrc, RC *prcDst);
    PT PtMap(RC *prcSrc, RC *prcDst);

    void Transform(LegacyUlong grfpt);
};

class RC
{
  public:
    LegacyLong xpLeft;
    LegacyLong ypTop;
    LegacyLong xpRight;
    LegacyLong ypBottom;

  public:
    // constructors
    RC(void)
    {
    }
    RC(LegacyLong xpLeftT, LegacyLong ypTopT, LegacyLong xpRightT, LegacyLong ypBottomT)
    {
        AssertThisMem();
        xpLeft = xpLeftT;
        ypTop = ypTopT;
        xpRight = xpRightT;
        ypBottom = ypBottomT;
    }

    // for assigning to/from an SystemRectangle
    operator SystemRectangle(void);
    RC &operator=(SystemRectangle &rcs);
    RC(SystemRectangle &rcs)
    {
        *this = rcs;
    }

    void Zero(void)
    {
        AssertThisMem();
        xpLeft = ypTop = xpRight = ypBottom = 0;
    }
    void Set(LegacyLong xp1, LegacyLong yp1, LegacyLong xp2, LegacyLong yp2)
    {
        AssertThisMem();
        xpLeft = xp1;
        ypTop = yp1;
        xpRight = xp2;
        ypBottom = yp2;
    }
    // use klwMin / 2 and klwMax / 2 so Dxp and Dyp are correct
    void Max(void)
    {
        AssertThisMem();
        xpLeft = ypTop = klwMin / 2;
        xpRight = ypBottom = klwMax / 2;
    }
    bool FMax(void)
    {
        AssertThisMem();
        return xpLeft == klwMin / 2 && ypTop == klwMin / 2 && xpRight == klwMax / 2 && ypBottom == klwMax / 2;
    }

    // interaction with other rc's and pt's
    bool operator==(RC &rc);
    bool operator!=(RC &rc);
    RC &operator+=(PT &pt)
    {
        xpLeft += pt.xp;
        ypTop += pt.yp;
        xpRight += pt.xp;
        ypBottom += pt.yp;
        return *this;
    }
    RC &operator-=(PT &pt)
    {
        xpLeft -= pt.xp;
        ypTop -= pt.yp;
        xpRight -= pt.xp;
        ypBottom -= pt.yp;
        return *this;
    }
    RC operator+(PT &pt)
    {
        return RC(xpLeft + pt.xp, ypTop + pt.yp, xpRight + pt.xp, ypBottom + pt.yp);
    }
    RC operator-(PT &pt)
    {
        return RC(xpLeft - pt.xp, ypTop - pt.yp, xpRight - pt.xp, ypBottom - pt.yp);
    }
    PT PtTopLeft(void)
    {
        return PT(xpLeft, ypTop);
    }
    PT PtBottomRight(void)
    {
        return PT(xpRight, ypBottom);
    }
    PT PtTopRight(void)
    {
        return PT(xpRight, ypTop);
    }
    PT PtBottomLeft(void)
    {
        return PT(xpLeft, ypBottom);
    }

    // map the rectangle from prcSrc to prcDst coordinates
    void Map(RC *prcSrc, RC *prcDst);

    void Transform(LegacyUlong grfpt);

    LegacyLong Dxp(void)
    {
        AssertThisMem();
        return xpRight - xpLeft;
    }
    LegacyLong Dyp(void)
    {
        AssertThisMem();
        return ypBottom - ypTop;
    }
    LegacyLong XpCenter(void)
    {
        AssertThisMem();
        return (xpLeft + xpRight) / 2;
    }
    LegacyLong YpCenter(void)
    {
        AssertThisMem();
        return (ypTop + ypBottom) / 2;
    }
    bool FEmpty(void)
    {
        AssertThisMem();
        return ypBottom <= ypTop || xpRight <= xpLeft;
    }

    void CenterOnRc(RC *prcBase);
    void CenterOnPt(LegacyLong xp, LegacyLong yp);
    bool FIntersect(RC *prc1, RC *prc2);
    bool FIntersect(RC *prc);
    bool FPtIn(LegacyLong xp, LegacyLong yp);
    void InsetCopy(RC *prc, LegacyLong dxp, LegacyLong dyp);
    void Inset(LegacyLong dxp, LegacyLong dyp);
    void OffsetCopy(RC *prc, LegacyLong dxp, LegacyLong dyp);
    void Offset(LegacyLong dxp, LegacyLong dyp);
    void OffsetToOrigin(void);
    void PinPt(PT *ppt);
    void PinToRc(RC *prc);
    void SqueezeIntoRc(RC *prcBase);
    void StretchToRc(RC *prcBase);
    void Union(RC *prc1, RC *prc2);
    void Union(RC *prc);
    LegacyLong LwArea(void);
    bool FContains(RC *prc);
    void SetToCell(RC *prcSrc, LegacyLong crcWidth, LegacyLong crcHeight, LegacyLong ircWidth, LegacyLong ircHeight);
    bool FMapToCell(LegacyLong xp, LegacyLong yp, LegacyLong crcWidth, LegacyLong crcHeight, LegacyLong *pircWidth, LegacyLong *pircHeight);
};

/****************************************
    fractions (ratio/rational)
****************************************/
class RAT
{
    ASSERT

  private:
    LegacyLong _lwNum;
    LegacyLong _lwDen;

    // the third argument of this constructor is bogus.  This constructor is
    // provided so the GCD calculation can be skipped when we already know
    // the numerator and denominator are relatively prime.
    RAT(LegacyLong lwNum, LegacyLong lwDen, LegacyLong lwJunk)
    {
        // lwNum and lwDen are already relatively prime
        if (lwDen > 0)
        {
            _lwNum = lwNum;
            _lwDen = lwDen;
        }
        else
        {
            _lwNum = -lwNum;
            _lwDen = -lwDen;
        }
        AssertThis(0);
    }

  public:
    // constructors
    RAT(void)
    {
        _lwDen = 0;
    }
    RAT(LegacyLong lw)
    {
        _lwNum = lw;
        _lwDen = 1;
    }
    RAT(LegacyLong lwNum, LegacyLong lwDen)
    {
        Set(lwNum, lwDen);
    }
    void Set(LegacyLong lwNum, LegacyLong lwDen)
    {
        LegacyLong lwGcd = LwGcd(lwNum, lwDen);
        if (lwDen < 0)
            lwGcd = -lwGcd;
        _lwNum = lwNum / lwGcd;
        _lwDen = lwDen / lwGcd;
        AssertThis(0);
    }

    // unary minus
    RAT operator-(void) const
    {
        return RAT(-_lwNum, _lwDen, 0);
    }

    // access functions
    LegacyLong LwNumerator(void)
    {
        return _lwNum;
    }
    LegacyLong LwDenominator(void)
    {
        return _lwDen;
    }
    LegacyLong LwAway(void)
    {
        return LwDivAway(_lwNum, _lwDen);
    }
    LegacyLong LwToward(void)
    {
        return _lwNum / _lwDen;
    }
    LegacyLong LwClosest(void)
    {
        return LwDivClosest(_lwNum, _lwDen);
    }

    operator LegacyLong(void)
    {
        return _lwNum / _lwDen;
    }

    // applying to a long (as a multiplicative operator)
    LegacyLong LwScale(LegacyLong lw)
    {
        return (_lwNum != _lwDen) ? LwMulDiv(lw, _lwNum, _lwDen) : lw;
    }
    LegacyLong LwUnscale(LegacyLong lw)
    {
        return (_lwNum != _lwDen) ? LwMulDiv(lw, _lwDen, _lwNum) : lw;
    }

    // operator functions
    friend RAT operator+(const RAT &rat1, const RAT &rat2);
    friend RAT operator+(const RAT &rat, LegacyLong lw)
    {
        return RAT(rat._lwNum + LwMul(rat._lwDen, lw), rat._lwDen);
    }
    friend RAT operator+(LegacyLong lw, const RAT &rat)
    {
        return RAT(rat._lwNum + LwMul(rat._lwDen, lw), rat._lwDen);
    }

    friend RAT operator-(const RAT &rat1, const RAT &rat2)
    {
        return rat1 + (-rat2);
    }
    friend RAT operator-(const RAT &rat, LegacyLong lw)
    {
        return RAT(rat._lwNum + LwMul(rat._lwDen, -lw), rat._lwDen);
    }
    friend RAT operator-(LegacyLong lw, const RAT &rat)
    {
        return RAT(rat._lwNum + LwMul(rat._lwDen, -lw), rat._lwDen);
    }

    friend RAT operator*(const RAT &rat1, const RAT &rat2)
    {
        LegacyLong lwGcd1 = LwGcd(rat1._lwNum, rat2._lwDen);
        LegacyLong lwGcd2 = LwGcd(rat1._lwDen, rat2._lwNum);
        return RAT(LwMul(rat1._lwNum / lwGcd1, rat2._lwNum / lwGcd2), LwMul(rat1._lwDen / lwGcd2, rat2._lwDen / lwGcd1),
                   0);
    }
    friend RAT operator*(const RAT &rat, LegacyLong lw)
    {
        LegacyLong lwGcd = LwGcd(rat._lwDen, lw);
        return RAT(LwMul(lw / lwGcd, rat._lwNum), rat._lwDen / lwGcd, 0);
    }
    friend RAT operator*(LegacyLong lw, const RAT &rat)
    {
        LegacyLong lwGcd = LwGcd(rat._lwDen, lw);
        return RAT(LwMul(lw / lwGcd, rat._lwNum), rat._lwDen / lwGcd, 0);
    }

    friend RAT operator/(const RAT &rat1, const RAT &rat2)
    {
        LegacyLong lwGcd1 = LwGcd(rat1._lwNum, rat2._lwNum);
        LegacyLong lwGcd2 = LwGcd(rat1._lwDen, rat2._lwDen);
        return RAT(LwMul(rat1._lwNum / lwGcd1, rat2._lwDen / lwGcd2), LwMul(rat1._lwDen / lwGcd2, rat2._lwNum / lwGcd1),
                   0);
    }
    friend RAT operator/(const RAT &rat, LegacyLong lw)
    {
        LegacyLong lwGcd = LwGcd(rat._lwNum, lw);
        return RAT(rat._lwNum / lwGcd, LwMul(lw / lwGcd, rat._lwDen), 0);
    }
    friend RAT operator/(LegacyLong lw, const RAT &rat)
    {
        LegacyLong lwGcd = LwGcd(rat._lwNum, lw);
        return RAT(LwMul(lw / lwGcd, rat._lwDen), rat._lwNum / lwGcd, 0);
    }

    friend int operator==(const RAT &rat1, const RAT &rat2)
    {
        return rat1._lwNum == rat2._lwNum && rat1._lwDen == rat2._lwDen;
    }
    friend int operator==(const RAT &rat, LegacyLong lw)
    {
        return rat._lwDen == 1 && rat._lwNum == lw;
    }
    friend int operator==(LegacyLong lw, const RAT &rat)
    {
        return rat._lwDen == 1 && rat._lwNum == lw;
    }

    friend int operator!=(const RAT &rat1, const RAT &rat2)
    {
        return rat1._lwNum != rat2._lwNum || rat1._lwDen != rat2._lwDen;
    }
    friend int operator!=(const RAT &rat, LegacyLong lw)
    {
        return rat._lwDen != 1 || rat._lwNum != lw;
    }
    friend int operator!=(LegacyLong lw, const RAT &rat)
    {
        return rat._lwDen != 1 || rat._lwNum != lw;
    }

    // operator methods
    RAT &operator=(LegacyLong lw)
    {
        _lwNum = lw;
        _lwDen = 1;
        return *this;
    }

    RAT &operator+=(const RAT &rat)
    {
        *this = *this + rat;
        return *this;
    }
    RAT &operator+=(LegacyLong lw)
    {
        *this = *this + lw;
        return *this;
    }

    RAT &operator-=(const RAT &rat)
    {
        *this = *this - rat;
        return *this;
    }
    RAT &operator-=(LegacyLong lw)
    {
        *this = *this + (-lw);
        return *this;
    }

    RAT &operator*=(const RAT &rat)
    {
        *this = *this * rat;
        return *this;
    }
    RAT &operator*=(LegacyLong lw)
    {
        *this = *this * lw;
        return *this;
    }

    RAT &operator/=(const RAT &rat)
    {
        *this = *this / rat;
        return *this;
    }
    RAT &operator/=(LegacyLong lw)
    {
        *this = *this / lw;
        return *this;
    }
};

/***************************************************************************
    Data versioning utility
***************************************************************************/
struct DataVersion
{
    short _swCur;
    short _swBack;

    void Set(short swCur, short swBack);
    bool FReadable(short swCur, short swMin);
};
static_assert(sizeof(DataVersion) == 4, "DataVersion on-disk layout drift");

#endif // UTILINT_H
