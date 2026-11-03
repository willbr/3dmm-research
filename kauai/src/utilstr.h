/* Copyright (c) Microsoft Corporation.
   Licensed under the MIT License. */

/* Copyright (c) Microsoft Corporation.
   Licensed under the MIT License. */

#include <cstdarg>

/***************************************************************************
    Author: ShonK
    Project: Kauai
    Reviewed:
    Copyright (c) Microsoft Corporation

    String manipulation declarations.

    We use four types of strings: stn, stz, st, sz.  Unless there's good reason
    not to, all code should use stn's.

    sz - zero terminated (standard C) string.
    st - length byte prefixed (standard Pascal) string.
    stz - zero terminated and length prefixed string.
    stn - string class.

***************************************************************************/
#ifndef UTILSTR_H
#define UTILSTR_H

/***************************************************************************
    OS kind for string translation - these should always have their high
    and low bytes equal.
***************************************************************************/
const short oskNil = 0; // signifies unknown
const short koskSbMac = 0x0202;
const short koskSbWin = 0x0303;
const short koskUniMac = 0x0404; // big endian unicode
const short koskUniWin = 0x0505; // little endian unicode

#ifdef UNICODE
const short koskMac = koskUniMac;
const short koskWin = koskUniWin;
#else  //! UNICODE
const short koskMac = koskSbMac;
const short koskWin = koskSbWin;
#endif //! UNICODE
const short koskCur = MacWin(koskMac, koskWin);
const short koskSb = MacWin(koskSbMac, koskSbWin);
const short koskUni = MacWin(koskUniMac, koskUniWin);

#ifdef DEBUG
void AssertOsk(short osk);
#else //! DEBUG
#define AssertOsk(osk)
#endif //! DEBUG

/***************************************************************************
    Return the number of bytes a character occupies in the given osk.
***************************************************************************/
inline LegacyLong CbCharOsk(short osk)
{
    switch (osk)
    {
    case koskSbMac:
    case koskSbWin:
        return size(schar);

    case koskUniMac:
    case koskUniWin:
        return size(wchar);

    default:
        return 0;
    }
}

/***************************************************************************
    Constants
***************************************************************************/
const achar chNil = ChLit('\x0');
const achar kchReturn = ChLit('\xD');
const achar kchLineFeed = ChLit('\xA');
const achar kchTab = ChLit('\x9');
const achar kchSpace = ChLit(' ');
const wchar kchwUnicode = 0xFEFF;
const wchar kchwUnicodeSwap = 0xFFFE;

const LegacyLong kcchMaxSz = 255;
const LegacyLong kcchMaxSt = 255;
const LegacyLong kcchMaxStz = 255;
const LegacyLong kcchExtraSz = 1;
const LegacyLong kcchExtraSt = 1;
const LegacyLong kcchExtraStz = 2;
const LegacyLong kcchTotSz = kcchMaxSz + kcchExtraSz;
const LegacyLong kcchTotSt = kcchMaxSt + kcchExtraSt;
const LegacyLong kcchTotStz = kcchMaxStz + kcchExtraStz;

const LegacyLong kcchMaxStn = 255;
const LegacyLong kcbMaxDataStn = kcchTotStz * size(wchar) + size(short);

enum
{
    fstnNil = 0,
    fstnIgnoreCase,
};

/***************************************************************************
    String types
***************************************************************************/
typedef achar *PZString;
typedef achar *PPascalString;
typedef achar *PSTZ;
typedef achar ZString[kcchTotSz];
typedef achar PascalString[kcchTotSt];
typedef achar STZ[kcchTotStz];
typedef schar *PSZS;
typedef schar SZS[kcchTotSz];

/***************************************************************************
    String related asserts
***************************************************************************/
#ifdef DEBUG
void AssertRgch(achar *prgch, LegacyLong cch);
void AssertStz(PSTZ pstz);
void AssertSt(PPascalString pst);
void AssertSz(PZString psz);
void AssertNilOrSz(PZString psz);
#else
#define AssertRgch(prgch, cch)
#define AssertStz(pstz)
#define AssertSt(pst)
#define AssertSz(psz)
#define AssertNilOrSz(psz)
#endif

/***************************************************************************
    Testing validity of an stz or st
***************************************************************************/
bool FValidStz(PSTZ pstz);
bool FValidSt(PPascalString pst);

/***************************************************************************
    Cch means the number of characters (not including prefix and termination
    bytes) and CchTot means the total number of characters including
    overhead.
***************************************************************************/
LegacyLong CchSz(PZString psz);
inline LegacyLong CchTotSz(PZString psz)
{
    return CchSz(psz) + kcchExtraSz;
}
inline LegacyLong CchSt(PPascalString pst)
{
    AssertSt(pst);
    return (LegacyLong)(byte)pst[0];
}
inline LegacyLong CchTotSt(PPascalString pst)
{
    AssertSt(pst);
    return (LegacyLong)(byte)pst[0] + kcchExtraSt;
}
inline LegacyLong CchStz(PSTZ pstz)
{
    AssertStz(pstz);
    return (LegacyLong)(byte)pstz[0];
}
inline LegacyLong CchTotStz(PSTZ pstz)
{
    AssertStz(pstz);
    return (LegacyLong)(byte)pstz[0] + kcchExtraStz;
}

inline achar *PrgchSt(PPascalString pst)
{
    return pst + 1;
}
inline PZString PszStz(PSTZ pstz)
{
    return pstz + 1;
}

/***************************************************************************
    Byte-wise comparison and sorting.
    WARNING: these don't do normal UI level compares they do byte-wise
    comparison, including any length and/or terminating bytes and should
    be used only for internal sorting (for binary search etc).
***************************************************************************/
bool FEqualRgch(achar *prgch1, LegacyLong cch1, achar *prgch2, LegacyLong cch2);
LegacyUlong FcmpCompareRgch(achar *prgch1, LegacyLong cch1, achar *prgch2, LegacyLong cch2);

/***************************************************************************
    User level (case insensitive, locale aware) comparison and sorting.
***************************************************************************/
bool FEqualUserRgch(achar *prgch1, LegacyLong cch1, achar *prgch2, LegacyLong cch2, LegacyUlong grfstn = fstnIgnoreCase);
LegacyUlong FcmpCompareUserRgch(achar *prgch1, LegacyLong cch1, achar *prgch2, LegacyLong cch2, LegacyUlong grfstn = fstnIgnoreCase);

/***************************************************************************
    Upper and lower case utilies
***************************************************************************/
void UpperRgchs(schar *prgchs, LegacyLong cchs);
void LowerRgchs(schar *prgchs, LegacyLong cchs);
inline schar ChsUpper(schar chs)
{
    UpperRgchs(&chs, 1);
    return chs;
}
inline schar ChsLower(schar chs)
{
    LowerRgchs(&chs, 1);
    return chs;
}
void UpperRgchw(wchar *prgchw, LegacyLong cchw);
void LowerRgchw(wchar *prgchw, LegacyLong cchw);
inline wchar ChwUpper(wchar chw)
{
    UpperRgchw(&chw, 1);
    return chw;
}
inline wchar ChwLower(wchar chw)
{
    LowerRgchw(&chw, 1);
    return chw;
}

#ifdef UNICODE
inline void UpperRgch(achar *prgch, LegacyLong cch)
{
    UpperRgchw(prgch, cch);
}
inline void LowerRgch(achar *prgch, LegacyLong cch)
{
    LowerRgchw(prgch, cch);
}
inline achar ChUpper(achar ch)
{
    UpperRgchw(&ch, 1);
    return ch;
}
inline achar ChLower(achar ch)
{
    LowerRgchw(&ch, 1);
    return ch;
}
#else  //! UNICODE
inline void UpperRgch(achar *prgch, LegacyLong cch)
{
    UpperRgchs(prgch, cch);
}
inline void LowerRgch(achar *prgch, LegacyLong cch)
{
    LowerRgchs(prgch, cch);
}
inline achar ChUpper(achar ch)
{
    UpperRgchs(&ch, 1);
    return ch;
}
inline achar ChLower(achar ch)
{
    LowerRgchs(&ch, 1);
    return ch;
}
#endif //! UNICODE

/***************************************************************************
    Translation from one OS to another (eg, Win to Mac, single byte to
    unicode, etc).
***************************************************************************/
LegacyLong CchTranslateRgb(void *pvSrc, LegacyLong cbSrc, short oskSrc, achar *prgchDst, LegacyLong cchMaxDst);

/***************************************************************************
    These APIs assert if osk specifies a different sized character than
    koskCur uses.
***************************************************************************/
void TranslateRgch(achar *prgch, LegacyLong cch, short osk, bool fToCur = fTrue);
inline void TranslateSt(PPascalString pst, short osk, bool fToCur = fTrue)
{
    TranslateRgch(pst + 1, CchSt(pst), osk, fToCur);
}
inline void TranslateStz(PSTZ pstz, short osk, bool fToCur = fTrue)
{
    TranslateRgch(pstz + 1, CchStz(pstz), osk, fToCur);
}
inline void TranslateSz(PZString psz, short osk, bool fToCur = fTrue)
{
    TranslateRgch(psz, CchSz(psz), osk, fToCur);
}

/***************************************************************************
    Testing for type of character.
***************************************************************************/
enum
{
    fchNil = 0x00,

    // can overhang the end of a line and doesn't need to be draw
    fchWhiteOverhang = 0x01,

    // should break a line
    fchBreak = 0x02,

    // may break a line
    fchMayBreak = 0x04,

    // should be totally ignored (and not draw)
    fchIgnore = 0x08,

    // some sort of control character
    fchControl = 0x10,

    // a tab character
    fchTab = 0x20,
};

LegacyUlong GrfchFromCh(achar ch);

/***************************************************************************
    The hexadecimal digits 0 - 9, A - F.
***************************************************************************/
extern const achar vrgchHex[];

/***************************************************************************
    General string class.
***************************************************************************/
typedef class String *PString;
class String
{
    ASSERT

  private:
    achar _rgch[kcchMaxStn + 2];

  public:
    String(void)
    {
        _rgch[0] = _rgch[1] = 0;
        AssertThis(0);
    }
    String(String &stnSrc);
    String(PZString pszSrc);

    // pointers to the data - these should be considered readonly!
    achar *Prgch(void)
    {
        AssertThis(0);
        return _rgch + 1;
    }
    PZString Psz(void)
    {
        AssertThis(0);
        return _rgch + 1;
    }
    PPascalString Pst(void)
    {
        AssertThis(0);
        return _rgch;
    }
    PSTZ Pstz(void)
    {
        AssertThis(0);
        return _rgch;
    }
    LegacyLong Cch(void)
    {
        AssertThis(0);
        return (uchar)_rgch[0];
    }

    // setting the string
    void SetNil(void)
    {
        AssertThis(0);
        _rgch[0] = _rgch[1] = 0;
    }
    void SetRgch(achar *prgchSrc, LegacyLong cch);
    void SetSz(PZString pszSrc)
    {
        SetRgch(pszSrc, CchSz(pszSrc));
    }
    void SetSt(PPascalString pstSrc)
    {
        SetRgch(PrgchSt(pstSrc), CchSt(pstSrc));
    }
    void SetStz(PSTZ pstzSrc)
    {
        SetRgch(PszStz(pstzSrc), CchStz(pstzSrc));
    }
    void SetSzs(PSZS pszsSrc);

    // assignment operators
    String &operator=(String &stnSrc);
    String &operator=(PZString pszSrc)
    {
        SetSz(pszSrc);
        return *this;
    }

    // getting the string into a buffer
    void GetRgch(achar *prgchDst)
    {
        AssertThis(0);
        CopyPb(Prgch(), prgchDst, Cch() * size(achar));
    }
    void GetSz(PZString pszDst)
    {
        AssertThis(0);
        CopyPb(Psz(), pszDst, (Cch() + kcchExtraSz) * size(achar));
    }
    void GetSt(PPascalString pstDst)
    {
        AssertThis(0);
        CopyPb(Pst(), pstDst, (Cch() + kcchExtraSt) * size(achar));
    }
    void GetStz(PSTZ pstzDst)
    {
        AssertThis(0);
        CopyPb(Pstz(), pstzDst, (Cch() + kcchExtraStz) * size(achar));
    }
    void GetSzs(PSZS pszs);

    // modifying the string
    void Delete(LegacyLong ich, LegacyLong cch = kcchMaxStn);
    bool FAppendRgch(achar *prgchSrc, LegacyLong cch);
    bool FAppendCh(achar chSrc)
    {
        return FAppendRgch(&chSrc, 1);
    }
    bool FAppendSz(achar *pszSrc)
    {
        return FAppendRgch(pszSrc, CchSz(pszSrc));
    }
    bool FAppendStn(PString pstnSrc)
    {
        return FAppendRgch(pstnSrc->Prgch(), pstnSrc->Cch());
    }
    bool FInsertRgch(LegacyLong ich, achar *prgchSrc, LegacyLong cch);
    bool FInsertCh(LegacyLong ich, achar chSrc)
    {
        return FInsertRgch(ich, &chSrc, 1);
    }
    bool FInsertStn(LegacyLong ich, PString pstnSrc)
    {
        return FInsertRgch(ich, pstnSrc->Prgch(), pstnSrc->Cch());
    }

    // for testing equality
    bool FEqualRgch(achar *prgch, LegacyLong cch);
    bool FEqualSz(PZString psz)
    {
        return FEqualRgch(psz, CchSz(psz));
    }
    bool FEqual(PString pstn)
    {
        return FEqualRgch(pstn->Prgch(), pstn->Cch());
    }
    bool FEqualUserRgch(achar *prgch, LegacyLong cch, LegacyUlong grfstn = fstnIgnoreCase);
    bool FEqualUserSz(PZString psz, LegacyUlong grfstn = fstnIgnoreCase)
    {
        return FEqualUserRgch(psz, CchSz(psz), grfstn);
    }
    bool FEqualUser(PString pstn, LegacyUlong grfstn = fstnIgnoreCase)
    {
        return FEqualUserRgch(pstn->Prgch(), pstn->Cch(), grfstn);
    }

    // for sorting
    LegacyUlong FcmpCompare(PString pstn)
    {
        return ::FcmpCompareRgch(Prgch(), Cch(), pstn->Prgch(), pstn->Cch());
    }
    LegacyUlong FcmpCompareUser(PString pstn, LegacyUlong grfstn = fstnIgnoreCase)
    {
        return ::FcmpCompareUserRgch(Prgch(), Cch(), pstn->Prgch(), pstn->Cch(), grfstn);
    }

    // storing and retrieving strings to other buffers (or file).
    LegacyLong CbData(void);
    void GetData(void *pv);
    bool FSetData(void *pv, LegacyLong cbMax, LegacyLong *pcbRead = pvNil);
    bool FWrite(PDataBlock pblck, LegacyLong ib);
    bool FRead(PDataBlock pblck, LegacyLong ib, LegacyLong *pcbRead = pvNil);

    bool FFormat(PString pstnFormat, ...);
    bool FFormatSz(PZString pszFormat, ...);
    bool FFormatSz(const achar *pszFormat, ...);
    bool FFormatRgch(achar *prgchFormat, LegacyLong cchFormat, va_list va);
    bool FGetLw(LegacyLong *plw, LegacyLong lwBase = 0);
    bool FExpandControls(void);
};

#endif // UTILSTR_H
