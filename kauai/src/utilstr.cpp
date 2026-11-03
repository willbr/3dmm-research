/* Copyright (c) Microsoft Corporation.
   Licensed under the MIT License. */

/***************************************************************************
    Author: ShonK
    Project: Kauai
    Copyright (c) Microsoft Corporation

    String manipulation.
    WARNING: Must be in a fixed (pre-loaded) seg on Mac.

***************************************************************************/
#include <cstring>
#include "util.h"
#include "platform_text.h"
#include <cstdarg>
ASSERTNAME

#include "chtrans.h"

const achar vrgchHex[] = PszLit("0123456789ABCDEF");

/***************************************************************************
    Constructor for a string based on another string.
***************************************************************************/
String::String(String &stnSrc)
{
    AssertPo(&stnSrc, 0);

    CopyPb(stnSrc._rgch, _rgch, (stnSrc.Cch() + 2) * size(achar));
    AssertThis(0);
}

/***************************************************************************
    Constructor for a string based on an sz.
***************************************************************************/
String::String(PZString pszSrc)
{
    LegacyLong cch = LwBound(CchSz(pszSrc), 0, kcchMaxStn + 1);

    AssertIn(cch, 0, kcchMaxStn + 1);
    CopyPb(pszSrc, _rgch + 1, cch * size(achar));
    _rgch[0] = (achar)cch;
    _rgch[cch + 1] = 0;
    AssertThis(0);
}

/***************************************************************************
    Assignment of one string to another.
***************************************************************************/
String &String::operator=(String &stnSrc)
{
    AssertThis(0);
    AssertPo(&stnSrc, 0);

    CopyPb(stnSrc._rgch, _rgch, (stnSrc.Cch() + 2) * size(achar));
    AssertThis(0);
    return *this;
}

/***************************************************************************
    Set the string to the given array of characters.
***************************************************************************/
void String::SetRgch(achar *prgchSrc, LegacyLong cch)
{
    AssertThis(0);
    AssertIn(cch, 0, kcbMax);
    AssertPvCb(prgchSrc, cch * size(achar));

    if (cch > kcchMaxStn)
        cch = kcchMaxStn;

    if (cch > 0)
        CopyPb(prgchSrc, _rgch + 1, cch * size(achar));
    else
        cch = 0; // for safety

    _rgch[0] = (achar)cch;
    _rgch[cch + 1] = 0;
    AssertThis(0);
}

/***************************************************************************
    Put the zero terminated short character string into the String.
***************************************************************************/
void String::SetSzs(PSZS pszsSrc)
{
    AssertThis(0);
    AssertVarMem(pszsSrc);

#ifdef UNICODE
    LegacyLong count = static_cast<LegacyLong>(
        KauaiText::ToUtf16(pszsSrc, std::strlen(pszsSrc) + 1, KauaiText::Encoding::WindowsAnsi, _rgch + 1, kcchMaxStn));
    if (count == 0)
    {
        SetNil();
        return;
    }
    _rgch[0] = static_cast<achar>(count - 1);
#else
    SetSz(pszsSrc);
#endif
}

/***************************************************************************
    Delete (at most) cch characters starting at position ich.
***************************************************************************/
void String::Delete(LegacyLong ich, LegacyLong cch)
{
    AssertThis(0);
    AssertIn(cch, 0, kcbMax);
    LegacyLong cchCur = Cch();

    if (!FIn(ich, 0, cchCur))
    {
        Assert(ich == cchCur, "Bad character position to delete from");
        return;
    }

    if (ich + cch >= cchCur)
    {
        // delete to the end of the string
        _rgch[0] = (achar)ich;
        _rgch[ich + 1] = 0;
    }
    else
    {
        BltPb(_rgch + ich + cch + 1, _rgch + ich + 1, (cchCur - ich - cch) * size(achar));
        _rgch[0] = (achar)(cchCur - cch);
        _rgch[cchCur - cch + 1] = 0;
    }
    AssertThis(0);
}

/***************************************************************************
    Append some characters to the end of the string.
***************************************************************************/
bool String::FAppendRgch(achar *prgchSrc, LegacyLong cch)
{
    AssertThis(0);
    AssertIn(cch, 0, kcbMax);
    AssertPvCb(prgchSrc, cch * size(achar));
    bool fRet = fTrue;
    LegacyLong cchCur = Cch();

    if (cch > kcchMaxStn - cchCur)
    {
        cch = kcchMaxStn - cchCur;
        fRet = fFalse;
    }

    if (cch > 0)
    {
        CopyPb(prgchSrc, _rgch + cchCur + 1, cch * size(achar));
        _rgch[0] = (achar)(cchCur + cch);
        _rgch[cchCur + cch + 1] = 0;
    }

    AssertThis(0);
    return fRet;
}

/***************************************************************************
    Insert some characters into the middle of a string.
***************************************************************************/
bool String::FInsertRgch(LegacyLong ich, achar *prgchSrc, LegacyLong cch)
{
    AssertThis(0);
    AssertIn(cch, 0, kcbMax);
    AssertPvCb(prgchSrc, cch * size(achar));
    bool fRet = fTrue;
    LegacyLong cchCur = Cch();

    ich = LwBound(ich, 0, cchCur + 1);
    if (cch > kcchMaxStn - ich)
    {
        cchCur = ich;
        cch = kcchMaxStn - ich;
        fRet = fFalse;
    }
    else if (cch > kcchMaxStn - cchCur)
    {
        cchCur = kcchMaxStn - cch - ich;
        fRet = fFalse;
    }

    Assert(cchCur + cch <= kcchMaxStn, 0);
    if (cch > 0)
    {
        if (cchCur > ich)
        {
            BltPb(_rgch + ich + 1, _rgch + ich + cch + 1, (cchCur - ich) * size(achar));
        }
        CopyPb(prgchSrc, _rgch + ich + 1, cch * size(achar));
        _rgch[0] = (achar)(cchCur + cch);
        _rgch[cchCur + cch + 1] = 0;
    }

    AssertThis(0);
    return fRet;
}

/***************************************************************************
    Test whether the given rgch is equal to this string.  This does bytewise
    compare - not user level comparison.
***************************************************************************/
bool String::FEqualRgch(achar *prgch, LegacyLong cch)
{
    AssertThis(0);
    AssertIn(cch, 0, kcbMax);
    AssertPvCb(prgch, cch * size(achar));

    return cch == Cch() && FEqualRgb(_rgch + 1, prgch, cch * size(achar));
}

/***************************************************************************
    Do user level string equality testing with the options given in grfstn.
***************************************************************************/
bool String::FEqualUserRgch(achar *prgch, LegacyLong cch, LegacyUlong grfstn)
{
    AssertThis(0);
    AssertIn(cch, 0, kcbMax);
    AssertPvCb(prgch, cch * size(achar));

    return ::FEqualUserRgch(Prgch(), Cch(), prgch, cch, grfstn);
}

/***************************************************************************
    Return the buffer size needed by GetData, or the block size needed
    by String::FWrite.
***************************************************************************/
LegacyLong String::CbData(void)
{
    AssertThis(0);

    return size(short) + (Cch() + 2) * size(achar);
}

/***************************************************************************
    Get the streamed data for the stn. pv should point to a buffer
    CbData() bytes long.
***************************************************************************/
void String::GetData(void *pv)
{
    AssertThis(0);
    AssertPvCb(pv, CbData());
    short osk = koskCur;

    CopyPb(&osk, pv, size(short));
    CopyPb(_rgch, PvAddBv(pv, size(short)), (Cch() + 2) * size(achar));
}

/***************************************************************************
    Writes the string data to the given block starting at position ib.
***************************************************************************/
bool String::FWrite(PDataBlock pblck, LegacyLong ib)
{
    AssertThis(0);
    AssertPo(pblck, 0);
    LegacyLong cbWrite = CbData();
    LegacyLong cbTot = pblck->Cb();
    short osk = koskCur;

    if (!FIn(ib, 0, cbTot - cbWrite + 1))
    {
        Bug("DataBlock is not big enough");
        return fFalse;
    }

    if (!pblck->FWriteRgb(&osk, size(osk), ib))
        return fFalse;
    if (!pblck->FWriteRgb(_rgch, cbWrite - size(short), ib + size(short)))
        return fFalse;

    return fTrue;
}

/***************************************************************************
    Set the string from the given data.
***************************************************************************/
bool String::FSetData(void *pv, LegacyLong cbMax, LegacyLong *pcbRead)
{
    AssertThis(0);
    AssertIn(cbMax, 0, kcbMax);
    AssertPvCb(pv, cbMax);
    AssertNilOrVarMem(pcbRead);
    LegacyLong cch, ich, ibT, cbT;
    LegacyLong translated;
    wchar chw;
    short osk;

    ibT = 0;
    if (cbMax < size(short) + ibT)
        goto LFail;
    CopyPb(pv, &osk, size(short));
    ibT += size(short);

    if (osk == koskCur)
    {
        // no translation needed - read the length prefix
        if (cbMax < ibT + size(achar))
            goto LFail;
        CopyPb(PvAddBv(pv, ibT), &_rgch[0], size(achar));
        ibT += size(achar);
        cch = (LegacyLong)(uchar)_rgch[0];
        if (!FIn(cch, 0, kcchMaxStn + 1))
            goto LFail;

        // read the rest of the string
        cbT = size(achar) * (cch + 1);
        if (cbMax < ibT + cbT)
            goto LFail;
        CopyPb(PvAddBv(pv, ibT), _rgch + 1, cbT);
        ibT += cbT;

        // make sure the terminating zero is there
        if (_rgch[cch + 1] != 0)
            goto LFail;

        goto LCheck;
    }

    switch (CbCharOsk(osk))
    {
    case size(schar):
        if (ibT + 2 > cbMax)
            goto LFail;

        cch = (LegacyLong)(byte) * (schar *)PvAddBv(pv, ibT++);
        if (cch > kcchMaxStn || ibT + cch >= cbMax || *(schar *)PvAddBv(pv, ibT + cch) != 0)
        {
            goto LFail;
        }
        translated = CchTranslateRgb(PvAddBv(pv, ibT), cch, osk, _rgch + 1, kcchMaxStn);
        if (cch > 0 && translated <= 0)
            goto LFail;
        _rgch[0] = static_cast<achar>(translated);
        ibT += cch + 1;

        cch = (LegacyLong)(uchar)_rgch[0];
        _rgch[cch + 1] = 0;
        goto LCheck;

    case size(wchar):
        if (ibT + 2 * size(wchar) > cbMax)
            goto LFail;
        CopyPb(PvAddBv(pv, ibT), &chw, size(wchar));
        ibT += size(wchar);

        if (osk == MacWin(koskUniWin, koskUniMac))
            SwapBytesRgsw(&chw, 1);
        cch = (LegacyLong)(ushort)chw;

        if (cch > kcchMaxStn || ibT + (cch + 1) * size(wchar) > cbMax)
            goto LFail;
        CopyPb(PvAddBv(pv, ibT + cch * size(wchar)), &chw, size(wchar));
        if (chw != 0)
            goto LFail;
        translated = CchTranslateRgb(PvAddBv(pv, ibT), cch * size(wchar), osk, _rgch + 1, kcchMaxStn);
        if (cch > 0 && translated <= 0)
            goto LFail;
        _rgch[0] = static_cast<achar>(translated);
        ibT += (cch + 1) * size(wchar);

        cch = (LegacyLong)(uchar)_rgch[0];
        _rgch[cch + 1] = 0;

    LCheck:
        // make sure there aren't any other zeros.
        for (ich = 1; ich <= cch; ich++)
        {
            if (_rgch[ich] == 0)
                goto LFail;
        }
        if (pvNil != pcbRead)
            *pcbRead = ibT;
        else if (ibT != cbMax)
            goto LFail;
        AssertThis(0);
        return fTrue;

    default:
    LFail:
        PushErc(ercStnRead);
        Warn("bad String data");
        _rgch[0] = _rgch[1] = 0;
        TrashVar(pcbRead);
        return fFalse;
    }
}

/***************************************************************************
    Read a string from a block.
***************************************************************************/
bool String::FRead(PDataBlock pblck, LegacyLong ib, LegacyLong *pcbRead)
{
    AssertThis(0);
    AssertPo(pblck, 0);
    AssertNilOrVarMem(pcbRead);

    LegacyLong cch, ich, ibT, cbT, cbMax;
    LegacyLong translated;
    short osk;
    schar chs;
    wchar chw;
    byte rgb[kcbMaxDataStn];

    if (!pblck->FUnpackData())
        return fFalse;
    cbMax = pblck->Cb();

    ibT = ib;
    if (cbMax < size(short) + ibT || !pblck->FReadRgb(&osk, size(short), ibT))
        goto LFail;
    ibT += size(short);

    if (osk == koskCur)
    {
        // no translation needed - read the length prefix
        if (cbMax < ibT + size(achar) || !pblck->FReadRgb(&_rgch[0], size(achar), ibT))
        {
            goto LFail;
        }
        ibT += size(achar);
        cch = (LegacyLong)(uchar)_rgch[0];
        if (!FIn(cch, 0, kcchMaxStn + 1))
            goto LFail;

        // read the rest of the string
        cbT = size(achar) * (cch + 1);
        if (cbMax < ibT + cbT || !pblck->FReadRgb(_rgch + 1, cbT, ibT))
            goto LFail;
        ibT += cbT;

        // make sure the terminating zero is there
        if (_rgch[cch + 1] != 0)
            goto LFail;

        goto LCheck;
    }

    switch (CbCharOsk(osk))
    {
    case size(schar):
        if (ibT + 2 > cbMax || !pblck->FReadRgb(&chs, 1, ibT++))
            goto LFail;
        cch = (LegacyLong)(byte)chs;
        if (cch > kcchMaxStn || ibT + cch >= cbMax || !pblck->FReadRgb(rgb, cch + 1, ibT) || rgb[cch] != 0)
        {
            goto LFail;
        }
        ibT += cch + 1;
        translated = CchTranslateRgb(rgb, cch, osk, _rgch + 1, kcchMaxStn);
        if (cch > 0 && translated <= 0)
            goto LFail;
        _rgch[0] = static_cast<achar>(translated);

        cch = (LegacyLong)(uchar)_rgch[0];
        _rgch[cch + 1] = 0;
        goto LCheck;

    case size(wchar):
        if (ibT + 2 * size(wchar) > cbMax || !pblck->FReadRgb(&chw, size(wchar), ibT))
        {
            goto LFail;
        }
        ibT += size(wchar);

        if (osk == MacWin(koskUniWin, koskUniMac))
            SwapBytesRgsw(&chw, 1);
        cch = (LegacyLong)(ushort)chw;

        if (cch > kcchMaxStn || ibT + (cch + 1) * size(wchar) > cbMax ||
            !pblck->FReadRgb(rgb, (cch + 1) * size(wchar), ibT) || ((wchar *)rgb)[cch] != 0)
        {
            goto LFail;
        }
        ibT += (cch + 1) * size(wchar);
        translated = CchTranslateRgb(rgb, cch * size(wchar), osk, _rgch + 1, kcchMaxStn);
        if (cch > 0 && translated <= 0)
            goto LFail;
        _rgch[0] = static_cast<achar>(translated);

        cch = (LegacyLong)(uchar)_rgch[0];
        _rgch[cch + 1] = 0;

    LCheck:
        // make sure there aren't any other zeros.
        for (ich = 1; ich <= cch; ich++)
        {
            if (_rgch[ich] == 0)
                goto LFail;
        }
        if (pvNil != pcbRead)
            *pcbRead = ibT;
        else if (ibT != cbMax)
            goto LFail;
        AssertThis(0);
        return fTrue;

    default:
    LFail:
        PushErc(ercStnRead);
        Warn("bad String data or read failure");
        _rgch[0] = _rgch[1] = 0;
        TrashVar(pcbRead);
        return fFalse;
    }
}

/***************************************************************************
    Get a zero terminated short string from this string.
***************************************************************************/
void String::GetSzs(PSZS pszs)
{
    AssertThis(0);
    AssertPvCb(pszs, kcchTotSz);

#ifdef UNICODE
    if (!KauaiText::ToAnsi(_rgch + 1, (Cch() + 1) * sizeof(wchar), KauaiText::Encoding::Utf16LE, pszs, kcchMaxSz))
        pszs[0] = 0;
#else
    CopyPb(_rgch + 1, pszs, Cch() + 1);
#endif
}

/***************************************************************************
    Format this string using the given template string and any additional
    parameters (ala sprintf).  All parameters are assumed to be 4 bytes long.

    Returns false if the string ended up being too long to fit in an stn.
    The following controls are supported:

    %c (long)achar
    %s pstn
    %z psz
    %d signed decimal (long)
    %u unsigned decimal (long)
    %x hex
    %f long as a 4 character value: 'xxxx' (ala FileType and ChunkTagOrType values)
    %% a percent sign

    Supports the following options, in this order:

        Argument reordering ('<', 0 based decimal number, '>')
        Left justify ('-')
        Explicit plus sign ('+')
        Zero padding instead of space padding ('0')
        Minimum field width (decimal number)

    These all go between the '%' and the control letter.  Note that argument
    reordering affects everything after the reordered arg in the control
    string.  Eg, "%<1>d %<0>d %d %d"  will use arguments in the order
    { 1, 0, 1, 2 }.  If you just want to switch two arguments, the one
    following needs a number also.  So the above example would be
    "%<1>d %<0>d %<2>d %d", producing { 1, 0, 2, 3 }.

    WARNING: all arguments should be 4 bytes long.
***************************************************************************/
bool String::FFormat(PString pstnFormat, ...)
{
    AssertThis(0);
    AssertPo(pstnFormat, 0);

    va_list va;
    va_start(va, pstnFormat);
    bool fRet = FFormatRgch(pstnFormat->Prgch(), pstnFormat->Cch(), va);
    va_end(va);
    return fRet;
}

/***************************************************************************
    See comments for String::FFormat
***************************************************************************/
bool String::FFormatSz(const achar *pszFormat, ...)
{
    auto *format = const_cast<achar *>(pszFormat); // formatting never mutates it
    AssertThis(0);
    AssertSz(format);
    va_list va;
    va_start(va, pszFormat);
    bool result = FFormatRgch(format, CchSz(format), va);
    va_end(va);
    return result;
}

bool String::FFormatSz(PZString pszFormat, ...)
{
    AssertThis(0);
    AssertSz(pszFormat);

    va_list va;
    va_start(va, pszFormat);
    bool fRet = FFormatRgch(pszFormat, CchSz(pszFormat), va);
    va_end(va);
    return fRet;
}

/***************************************************************************
    Core routine for sprintf functionality.
***************************************************************************/
bool String::FFormatRgch(achar *prgchFormat, LegacyLong cchFormat, va_list va)
{
    AssertThis(0);
    AssertIn(cchFormat, 0, kcchMaxStn + 1);
    AssertPvCb(prgchFormat, cchFormat * size(achar));

    // Data Write Order - these dwo values are pcode for when to add what
    enum
    {
        dwoPadFirst = 0x0321,
        dwoPadSecond = 0x0312,
        dwoPadLast = 0x0132
    };
    achar *pchOut, *pchOutLim;
    achar *pchIn, *pchInLim;
    LegacyLong cch;
    LegacyLong cchMin;
    LegacyLong ivArg;
    LegacyUlong lu, luRad;
    void *pvArg; // pointer-sized arg for %s, %z (8 bytes on Win64)
    achar ch;
    achar rgchT[kcchMaxStn];
    achar *prgchTerm;
    achar chSign, chPad;
    LegacyUlong dwo;
    PString pstn;
    bool fRet = fFalse;

    pchInLim = (pchIn = prgchFormat) + cchFormat;
    pchOutLim = (pchOut = _rgch + 1) + kcchMaxStn;
    ivArg = 0;
    while (pchIn < pchInLim && pchOut < pchOutLim)
    {
        if ((ch = *pchIn++) != ChLit('%'))
        {
            *pchOut++ = ch;
            continue;
        }

        // pre-fetch the next character
        if (pchIn >= pchInLim)
            goto LBug;
        ch = *pchIn++;
        if (ch == ChLit('%'))
        {
            *pchOut++ = ChLit('%');
            continue;
        }

        dwo = dwoPadFirst;
        chSign = 0;
        chPad = kchSpace;

        if (ch == ChLit('<'))
        {
            ivArg = 0;
            for (;;)
            {
                if (ch < ChLit('0') || ch > ChLit('9'))
                    break;
                ivArg = ivArg * 10 + ch - ChLit('0');

                // pre-fetch the next character
                if (pchIn >= pchInLim)
                    goto LBug;
                ch = *pchIn++;
            }
            if (ch != ChLit('>'))
                goto LBug;
        }

        // get qualifiers (print sign, left justify, etc)
        for (;;)
        {
            switch (ch)
            {
            default:
                goto LGetCchMin;
            case ChLit('-'):
                dwo = dwoPadLast;
                break;
            case ChLit('+'):
                chSign = ChLit('+');
                break;
            case ChLit('0'):
                chPad = ChLit('0');
                dwo = dwoPadSecond;
                break;
            }

            // pre-fetch the next character
            if (pchIn >= pchInLim)
                goto LBug;
            ch = *pchIn++;
        }

    LGetCchMin:
        cchMin = 0;
        for (;;)
        {
            if (ch < ChLit('0') || ch > ChLit('9'))
                break;
            cchMin = cchMin * 10 + ch - ChLit('0');

            // pre-fetch the next character
            if (pchIn >= pchInLim)
                goto LBug;
            ch = *pchIn++;
        }

        // code after the switch assumes that prgchTerm points to the
        // characters to add to the stream and cch is the number of characters.
        // Pull each arg via va_arg with the correct type per format spec --
        // critical on Win64 where pointers are 8 bytes and homebrew
        // varargs unpacking by stack-pointer arithmetic is broken.
        ivArg++; // tracked only for diagnostics; positional %<N> not supported
        prgchTerm = rgchT;
        switch (ch)
        {
        case ChLit('c'):
            // char promotes to int through varargs.
            rgchT[0] = (achar)va_arg(va, int);
            cch = 1;
            break;

        case ChLit('s'):
            pvArg = va_arg(va, void *);
            pstn = (PString)pvArg;
            AssertPo(pstn, 0);
            prgchTerm = pstn->Prgch();
            cch = pstn->Cch();
            break;

        case ChLit('z'):
            pvArg = va_arg(va, void *);
            AssertSz((achar *)pvArg);
            prgchTerm = (achar *)pvArg;
            cch = CchSz(prgchTerm);
            break;

        case ChLit('f'):
            // 4-byte file/chunk tag; printed in big-endian byte order.
            lu = va_arg(va, LegacyUlong);
            for (cch = 4; cch-- > 0; lu >>= 8)
            {
                ch = (achar)(byte)lu;
                if (0 == ch)
                    ch = 1;
                rgchT[cch] = ch;
            }
            cch = 4;
            break;

        case ChLit('x'):
            lu = va_arg(va, LegacyUlong);
            // if cchMin is not 0, don't make it longer than cchMin
            if (cchMin > 0 && cchMin < 8)
                lu &= (1L << (cchMin * 4)) - 1;
            luRad = 16;
            goto LUnsigned;

        case ChLit('d'):
            {
                const LegacyLong value = va_arg(va, LegacyLong);
                lu = static_cast<LegacyUlong>(value);
                if (value < 0)
                {
                    chSign = ChLit('-');
                    lu = LegacyUlong(0) - lu;
                }
            }
            luRad = 10;
            goto LUnsigned;

        case ChLit('u'):
            lu = va_arg(va, LegacyUlong);
            luRad = 10;
        LUnsigned:
            prgchTerm = rgchT + CvFromRgv(rgchT);
            cch = 0;
            do
            {
                *--prgchTerm = vrgchHex[lu % luRad];
                cch++;
                lu /= luRad;
            } while (lu != 0);
            break;

        default:
        LBug:
            Bug("bad format string");
            goto LFail;
        }

        // set cchMin to the number of characters to pad
        cchMin = LwMax(0, cchMin - cch - (chSign != 0));
        if (pchOutLim - pchOut <= cch + cchMin + (chSign != 0))
        {
            // overflowed the output buffer
            goto LFail;
        }

        // arrange the sign, padding and rgch according to dwo
        while (dwo != 0)
        {
            switch (dwo & 0x0F)
            {
            case 1: // add padding
                for (; cchMin > 0; cchMin--)
                    *pchOut++ = chPad;
                break;

            case 2: // add the sign
                if (chSign != 0)
                    *pchOut++ = chSign;
                break;

            case 3: // add the text
                CopyPb(prgchTerm, pchOut, cch * size(achar));
                pchOut += cch;
                break;

            default:
                BugVar("bad dwo value", &dwo);
                break;
            }

            dwo >>= 4;
        }

        Assert(pchOut <= pchOutLim, "bad logic above - overflowed the rgch");
    }

    fRet = pchIn == pchInLim;
LFail:
    cch = pchOut - _rgch - 1;
    AssertIn(cch, 0, kcchMaxStn + 1);
    _rgch[0] = (achar)cch;
    _rgch[cch + 1] = 0;
    return fRet;
}

/***************************************************************************
    Parses the String as a number.  If lwBase is 0, automatically determines
    the base as one of 10, 8 or 16 (as in standard C) and allows leading
    spaces, '+' and '-' signs, and trailing spaces.  Doesn't deal with
    overflow.
***************************************************************************/
bool String::FGetLw(LegacyLong *plw, LegacyLong lwBase)
{
    AssertThis(0);
    AssertVarMem(plw);
    AssertIn(lwBase, 0, 36);
    Assert(lwBase != 1, "base can't be 1");
    LegacyLong lwDigit;
    LegacyUlong value = 0;
    achar ch;
    bool fNegative = fFalse;
    PZString psz = Psz();

    if (lwBase < 2)
    {
        for (;; psz++)
        {
            switch (*psz)
            {
            case ChLit('-'):
                fNegative = !fNegative;
                continue;

            case ChLit('+'):
            case kchSpace:
                continue;
            }
            break;
        }
    }

    *plw = 0;
    if (*psz == 0)
        goto LFail;

    // determine the base if lwBase is zero
    if (0 == lwBase)
    {
        // determine the base
        if (ChLit('0') == psz[0])
        {
            psz++;
            if (ChLit('x') == psz[0] || ChLit('X') == psz[0])
            {
                psz++;
                lwBase = 16;
            }
            else
                lwBase = 8;
        }
        else
            lwBase = 10;
    }

    while (0 != (ch = *psz++))
    {
        if (FIn(ch, ChLit('0'), ChLit('9') + 1))
            lwDigit = ch - ChLit('0');
        else
        {
            if (FIn(ch, ChLit('A'), ChLit('Z') + 1))
                ch += ChLit('a') - ChLit('A');
            else if (!FIn(ch, ChLit('a'), ChLit('z') + 1))
            {
                // not a letter, so only spaces should follow
                for (; ch != 0; ch = *psz++)
                {
                    if (ch != kchSpace)
                        goto LFail;
                }
                break;
            }
            lwDigit = ch - ChLit('a') + 10;
        }

        if (lwDigit > lwBase)
        {
        LFail:
            TrashVar(plw);
            return fFalse;
        }
        // Retain the original 32-bit wrap semantics without signed overflow.
        value = value * static_cast<LegacyUlong>(lwBase) + static_cast<LegacyUlong>(lwDigit);
    }

    if (fNegative)
        value = LegacyUlong(0) - value;
    *plw = value <= 0x7FFFFFFFu ? static_cast<LegacyLong>(value) : -static_cast<LegacyLong>(~value) - 1;

    return fTrue;
}

/***************************************************************************
    Doubles any backslash characters in the string and replaces " literals
    with \".
***************************************************************************/
bool String::FExpandControls(void)
{
    AssertThis(0);
    achar rgch[kcchMaxStn];
    achar *pchSrc, *pchDst, *pchLim, ch;
    LegacyLong cch;
    bool fAny;
    bool fRet = fFalse;

    fAny = fFalse;
    pchSrc = Psz();
    pchDst = rgch;
    pchLim = pchDst + kcchMaxStn;
    for (cch = Cch(); cch-- > 0;)
    {
        if (pchDst >= pchLim)
            goto LFail;
        ch = *pchSrc++;
        switch (ch)
        {
        case kchTab:
            ch = ChLit('t');
            goto LExpand;
        case kchReturn:
            ch = ChLit('n');
            goto LExpand;
        case ChLit('\\'):
        case ChLit('"'):
        LExpand:
            if (pchDst + 1 >= pchLim)
                goto LFail;
            *pchDst++ = ChLit('\\');
            fAny = fTrue;
            break;
        }
        *pchDst++ = ch;
    }
    fRet = fTrue;

LFail:
    if (fAny)
        SetRgch(rgch, pchDst - rgch);

    return fRet;
}

#ifdef DEBUG
/***************************************************************************
    Assert the validity of a String.
***************************************************************************/
void String::AssertValid(LegacyUlong grf)
{
    AssertThisMem();

    achar *pch;
    LegacyLong cch = (LegacyLong)(uchar)_rgch[0];

    AssertIn(cch, 0, kcchMaxStn + 1);
    Assert(_rgch[cch + 1] == 0, "missing termination byte");

    for (pch = _rgch + 1; *pch != 0; pch++)
        ;

    Assert(pch - _rgch == cch + 1, "internal null characters");
}
#endif // DEBUG

/***************************************************************************
    Check the validity of an st (make sure no zeros are in it).
***************************************************************************/
bool FValidSt(PPascalString pst)
{
    AssertVarMem(pst);
    achar *pch;
    LegacyLong cch = (LegacyLong)(uchar)pst[0];

    if (!FIn(cch, 0, kcchMaxSt + 1))
        return fFalse;

    AssertPvCb(pst, (cch + kcchExtraSt) * size(achar));
    for (pch = PrgchSt(pst); cch > 0 && *pch != 0; cch--, pch++)
        ;
    return (cch == 0);
}

/***************************************************************************
    Check the validity of an stz
***************************************************************************/
bool FValidStz(PSTZ pstz)
{
    AssertVarMem(pstz);
    return FValidSt(pstz) && 0 == PszStz(pstz)[CchSt(pstz)];
}

/***************************************************************************
    Find the length of a zero terminated string.
***************************************************************************/
LegacyLong CchSz(PZString psz)
{
    // WARNING: don't call AssertSz, since AssertSz calls CchSz!
    AssertVarMem(psz);
    achar *pch;

    for (pch = psz; *pch != 0; pch++)
        ;
    Assert(pch - psz <= kcchMaxSz, "sz too long");
    AssertPvCb(psz, (pch - psz + 1) * size(achar));
    return pch - psz;
}

/***************************************************************************
    Do string equality testing.  This does byte-wise comparison for
    internal (non-user) use only!
***************************************************************************/
bool FEqualRgch(achar *prgch1, LegacyLong cch1, achar *prgch2, LegacyLong cch2)
{
    AssertIn(cch1, 0, kcbMax);
    AssertPvCb(prgch1, cch1 * size(achar));
    AssertIn(cch2, 0, kcbMax);
    AssertPvCb(prgch2, cch2 * size(achar));

    return cch1 == cch2 && FEqualRgb(prgch1, prgch2, cch1 * size(achar));
}

/***************************************************************************
    Do string comparison for sorting.  This is byte-wise for internal
    (non-user) sorting only!  The sorting is byte-order independent.
    fcmpLt means that string 1 is less than string 2.
***************************************************************************/
LegacyUlong FcmpCompareRgch(achar *prgch1, LegacyLong cch1, achar *prgch2, LegacyLong cch2)
{
    AssertIn(cch1, 0, kcbMax);
    AssertPvCb(prgch1, cch1 * size(achar));
    AssertIn(cch2, 0, kcbMax);
    AssertPvCb(prgch2, cch2 * size(achar));
    LegacyLong ich, cbTot, cbMatch;

    if (cch1 < cch2)
        return fcmpLt;
    if (cch1 > cch2)
        return fcmpGt;

    cbTot = cch1 * size(achar);
    cbMatch = CbEqualRgb(prgch1, prgch2, cbTot);
    AssertIn(cbMatch, 0, cbTot + 1);
    if (cbTot == cbMatch)
        return fcmpEq;

    ich = cbMatch / size(achar);
    Assert(prgch1[ich] != prgch2[ich], 0);

    return prgch1[ich] < prgch2[ich] ? fcmpLt : fcmpGt;
}

/***************************************************************************
    User level equality testing of strings.
***************************************************************************/
bool FEqualUserRgch(achar *prgch1, LegacyLong cch1, achar *prgch2, LegacyLong cch2, LegacyUlong grfstn)
{
    AssertIn(cch1, 0, kcbMax);
    AssertPvCb(prgch1, cch1 * size(achar));
    AssertIn(cch2, 0, kcbMax);
    AssertPvCb(prgch2, cch2 * size(achar));
    LegacyLong cchBuf;
    achar rgch1[kcchMaxStn];
    achar rgch2[kcchMaxStn];

    // REVIEW shonk: implement for real
    if (!(grfstn & fstnIgnoreCase))
        return cch1 == cch2 && FEqualRgb(prgch1, prgch2, cch1 * size(achar));

    if (cch1 != cch2)
        return fFalse;

    while (cch1 > 0)
    {
        cchBuf = LwMin(kcchMaxStn, cch1);
        CopyPb(prgch1, rgch1, cchBuf * size(achar));
        CopyPb(prgch2, rgch2, cchBuf * size(achar));
#ifdef UNICODE
        KauaiText::UpperUtf16(rgch1, cchBuf);
        KauaiText::UpperUtf16(rgch2, cchBuf);
#else
        KauaiText::UpperBytes(rgch1, cchBuf);
        KauaiText::UpperBytes(rgch2, cchBuf);
#endif
        if (!FEqualRgb(rgch1, rgch2, cchBuf * size(achar)))
            return fFalse;
        prgch1 += cchBuf;
        prgch2 += cchBuf;
        cch1 -= cchBuf;
    }

    return fTrue;
}

/***************************************************************************
    Do user level string comparison with the given options.
***************************************************************************/
LegacyUlong FcmpCompareUserRgch(achar *prgch1, LegacyLong cch1, achar *prgch2, LegacyLong cch2, LegacyUlong grfstn)
{
    AssertIn(cch1, 0, kcbMax);
    AssertPvCb(prgch1, cch1 * size(achar));
    AssertIn(cch2, 0, kcbMax);
    AssertPvCb(prgch2, cch2 * size(achar));

#ifdef UNICODE
    int result = KauaiText::CompareUtf16(prgch1, cch1, prgch2, cch2, (grfstn & fstnIgnoreCase) != 0);
#else
    int result = KauaiText::CompareBytes(prgch1, cch1, prgch2, cch2, (grfstn & fstnIgnoreCase) != 0);
#endif
    if (result == -1)
        return fcmpLt;
    if (result == 0)
        return fcmpEq;
    if (result == 1)
        return fcmpGt;
    return FcmpCompareRgch(prgch1, cch1, prgch2, cch2);
}

/***************************************************************************
    Map an array of short characters to upper case equivalents.
***************************************************************************/
void UpperRgchs(schar *prgchs, LegacyLong cchs)
{
    AssertIn(cchs, 0, kcbMax);
    AssertPvCb(prgchs, cchs * size(schar));
    KauaiText::UpperMappedBytes(prgchs, cchs);
}

/***************************************************************************
    Map an array of characters to lower case equivalents.
***************************************************************************/
void LowerRgchs(schar *prgchs, LegacyLong cchs)
{
    AssertIn(cchs, 0, kcbMax);
    AssertPvCb(prgchs, cchs * size(schar));
    KauaiText::LowerMappedBytes(prgchs, cchs);
}

/***************************************************************************
    Map an array of unicode characters to upper case equivalents.
***************************************************************************/
void UpperRgchw(wchar *prgchw, LegacyLong cchw)
{
    AssertIn(cchw, 0, kcbMax);
    AssertPvCb(prgchw, cchw * size(wchar));
    KauaiText::UpperUtf16(prgchw, cchw);
}

/***************************************************************************
    Map an array of unicode characters to lower case equivalents.
***************************************************************************/
void LowerRgchw(wchar *prgchw, LegacyLong cchw)
{
    AssertIn(cchw, 0, kcbMax);
    AssertPvCb(prgchw, cchw * size(wchar));
    KauaiText::LowerUtf16(prgchw, cchw);
}

/***************************************************************************
    Translate text from the indicated source character set to koskCur.
***************************************************************************/
LegacyLong CchTranslateRgb(void *pvSrc, LegacyLong cbSrc, short oskSrc, achar *prgchDst, LegacyLong cchMaxDst)
{
    AssertPvCb(pvSrc, cbSrc);
    AssertOsk(oskSrc);
    AssertPvCb(prgchDst, cchMaxDst * size(achar));
    if (cbSrc < 0 || cchMaxDst < 0)
        return 0;
    auto encoding = static_cast<KauaiText::Encoding>(oskSrc);
#ifdef UNICODE
    return static_cast<LegacyLong>(KauaiText::ToUtf16(pvSrc, cbSrc, encoding, cchMaxDst ? prgchDst : pvNil, cchMaxDst));
#else
    return static_cast<LegacyLong>(KauaiText::ToAnsi(pvSrc, cbSrc, encoding, cchMaxDst ? prgchDst : pvNil, cchMaxDst));
#endif
}

/***************************************************************************
    Translates a string between the current platform and another.  If fToCur
    is true, the translation is from osk to koskCur, otherwise from koskCur
    to osk.
***************************************************************************/
void TranslateRgch(achar *prgch, LegacyLong cch, short osk, bool fToCur)
{
    AssertPvCb(prgch, cch);
    AssertOsk(osk);
    Assert(osk == koskMac || osk == koskWin, "TranslateRgch can't handle this osk");

    if (koskCur == osk)
        return;

#ifdef UNICODE
    // for unicode, we just have to change the byte ordering
    SwapBytesRgsw(prgch, cch);
#else  //! UNICODE
    auto pmpchschs = MacWin(!fToCur, fToCur) ? _mpchschsMacToWin : _mpchschsWinToMac;

    for (; cch > 0; cch--, prgch++)
    {
        if ((byte)*prgch >= (byte)0x80)
            *prgch = pmpchschs[(byte)*prgch - (byte)0x80];
    }
#endif //! UNICODE
}

/***************************************************************************
    Return the type of the character.
    This must follow these implications:
        fchBreak -> fchMayBreak
        fchBreak -> fchControl
        fchIgnore -> fchControl

    REVIEW shonk: make GrfchFromCh handle all unicode characters.
***************************************************************************/
LegacyUlong GrfchFromCh(achar ch)
{
    switch ((uchar)ch)
    {
    case kchReturn:
        return fchBreak | fchMayBreak | fchControl;

    case kchLineFeed:
        return fchIgnore | fchControl;

    case kchTab:
        return fchTab | fchMayBreak | fchControl;

    case kchSpace:
        return fchWhiteOverhang | fchMayBreak;

#ifdef REVIEW // shonk: implement correctly once we get the tables from MSKK.
#ifdef UNICODE
    // REVIEW shonk: others? 148
    case (uchar)',':
    case (uchar)'.':
    case (uchar)')':
    case (uchar)']':
    case (uchar)'}':
    case (uchar)':':
    case (uchar)';':
    case (uchar)'?':
    case (uchar)'!':
    case (uchar)'.':
    case (uchar)',':
        return fchTestBreak;
#endif // UNICODE
#endif // REVIEW

    case 0x7F:
        return fchControl;

    default:
        if ((uchar)ch < kchSpace)
            return fchControl;

        return fchNil;
    }
}

#ifdef DEBUG
/***************************************************************************
    Validate an osk.
***************************************************************************/
void AssertOsk(short osk)
{
    switch (osk)
    {
    case koskSbMac:
    case koskSbWin:
    case koskUniMac:
    case koskUniWin:
        break;
    default:
        BugVar("bad osk", &osk);
    }
}

/***************************************************************************
    Check the validity of an st (make sure no zeros are in it).
***************************************************************************/
void AssertSt(PPascalString pst)
{
    Assert(FValidSt(pst), "bad st");
}

/***************************************************************************
    Check the validity of an stz.
***************************************************************************/
void AssertStz(PSTZ pstz)
{
    Assert(FValidStz(pstz), "bad stz");
}

/***************************************************************************
    Make sure the sz isn't too long.
***************************************************************************/
void AssertSz(PZString psz)
{
    // CchSz does all the asserting we need
    LegacyLong cch = CchSz(psz);
}

/***************************************************************************
    Check the validity of an st, nil is allowed.
***************************************************************************/
void AssertNilOrSt(PPascalString pst)
{
    if (pst != pvNil)
        AssertSt(pst);
}

/***************************************************************************
    Check the validity of an stz, nil is allowed.
***************************************************************************/
void AssertNilOrStz(PSTZ pstz)
{
    if (pstz != pvNil)
        AssertStz(pstz);
}

/***************************************************************************
    Check the validity of an sz, nil is allowed.
***************************************************************************/
void AssertNilOrSz(PZString psz)
{
    if (psz != pvNil)
        AssertSz(psz);
}
#endif // DEBUG
