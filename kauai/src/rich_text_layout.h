/* Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#ifndef KAUAI_RICH_TEXT_LAYOUT_H
#define KAUAI_RICH_TEXT_LAYOUT_H
/* The original SDL/retail rich-text formatter, independent of UI and graphics.
 * Hosts supply document properties, character classes and font measurements. */
#include <cassert>
#include <cstdint>
#include <cstring>
namespace KauaiTextLayout
{
inline long Min(long a, long b)
{
    return a < b ? a : b;
}
inline long Max(long a, long b)
{
    return a > b ? a : b;
}
inline long RoundAway(long value, long step)
{
    return ((value + step - 1) / step) * step;
}
inline long MulDiv(long value, long mul, long div)
{
    return static_cast<long>(std::int64_t(value) * mul / div);
}
enum
{
    fchWhiteOverhang = 1,
    fchBreak = 2,
    fchMayBreak = 4,
    fchIgnore = 8,
    fchTab = 32,
    kchObject = 1
};
enum
{
    ndFirst = 1,
    ndRest = 2,
    ndAll = 3
};
inline unsigned long Flags(unsigned long ch)
{
    switch (ch)
    {
    case '\r':
        return fchBreak | fchMayBreak | 16;
    case '\n':
        return fchIgnore | 16;
    case '\t':
        return fchTab | fchMayBreak | 16;
    case ' ':
        return fchWhiteOverhang | fchMayBreak;
    case 127:
        return 16;
    default:
        return ch < 32 ? 16 : 0;
    }
}
struct Line
{
    long cpMin, dypTot;
    short ccp, xpLeft, dyp, dypAscent;
};
struct RunData
{
    long cpLim;
    long cpLimDraw;
    long xpLim;
    long xpLimDraw;
};

/***************************************************************************
    Character run class. This is used to format a line, draw a line,
    map between cp and xp on a line, etc.
***************************************************************************/
const long kcchMaxChr = 128;
template <class Host> class CharacterRun
{

  public:
    using Character = typename Host::Character;
    using CHP = typename Host::CharacterProperties;
    using PAP = typename Host::ParagraphProperties;
    using RC = typename Host::Rectangle;

  private:
    CHP _chp;
    PAP _pap;
    Host *_host;
    bool _fMustAdvance : 1;
    bool _fBreak : 1;
    bool _fObject : 1;

    long _cpMin;
    long _cpLim;
    long _cpLimFetch;
    long _xpMin;
    long _xpBreak;

    RunData _chrd;
    RunData _chrdBop;

    long _dypAscent;
    long _dypDescent;

    Character _rgch[kcchMaxChr];

    bool _FFit(void);
    void _SetToBop(void);
    void _SkipIgnores(void);
    void _DoTab(void);

  public:
    void Init(CHP *pchp, PAP *ppap, Host *host, long cpMin, long cpLim, long xpBase, long xpLimLine, long xpBreak);

    void GetNextRun(bool fMustAdvance = false);
    bool FBreak(void)
    {
        return _fBreak;
    }
    void GetChrd(RunData *pchrd, RunData *pchrdBop = nullptr)
    {

        if (nullptr != pchrd)
            *pchrd = _chrd;
        if (nullptr != pchrdBop)
            *pchrdBop = _chrdBop;
    }
    long DypAscent(void)
    {
        return _dypAscent;
    }
    long DypDescent(void)
    {
        return _dypDescent;
    }
    long XpMin(void)
    {
        return _xpMin;
    }
    long XpBreak(void)
    {
        return _xpBreak;
    }
    Character *Prgch(void)
    {
        return _rgch;
    }
    long CpMin(void)
    {
        return _cpMin;
    }
    bool FObject(void)
    {
        return _fObject;
    }
};

/***************************************************************************
    Initialize the CHR.
***************************************************************************/
template <class Host>
void CharacterRun<Host>::Init(CHP *pchp, PAP *ppap, Host *host, long cpMin, long cpLim, long xpBase, long xpLimLine,
                              long xpBreak)
{

    RC rc;

    _chp = *pchp;
    _pap = *ppap;
    _host = host;
    _fBreak = false;

    _cpMin = cpMin;
    _cpLim = cpLim;
    _cpLimFetch = cpMin;
    _xpMin = xpBase;
    _xpBreak = Min(xpBreak, xpLimLine);

    // apply any indenting
    if (0 == xpBase)
    {
        switch (_pap.nd)
        {
        case ndFirst:
            if (_host->FMinPara(_cpMin))
                _xpMin += _pap.dxpTab;
            break;
        case ndRest:
            if (!_host->FMinPara(_cpMin))
                _xpMin += _pap.dxpTab;
            break;
        case ndAll:
            _xpMin += _pap.dxpTab;
            break;
        }
    }
    if (ndAll == _pap.nd)
        _xpBreak = Min(_xpBreak, xpLimLine - _pap.dxpTab);

    _chrd.cpLim = _chrd.cpLimDraw = _cpMin;
    _chrd.xpLim = _chrd.xpLimDraw = _xpMin;
    _chrdBop = _chrd;

    // get the vertical dimensions
    _host->SetFont(_chp);

    _host->WarmFont();

    _host->GetRcFromRgch(&rc, nullptr, 0);
    _dypAscent = Max(0, -rc.ypTop - _host->Offset(_chp));
    _dypDescent = Max(0, rc.ypBottom + _host->Offset(_chp));
}

/***************************************************************************
    Get the next run (within the bounds we were inited with).
***************************************************************************/
template <class Host> void CharacterRun<Host>::GetNextRun(bool fMustAdvance)
{

    unsigned long grfch;
    Character ch;
    RC rc;

    // Start the next run
    if ((_chrd.cpLim >= _cpMin + 1 && _chrd.cpLim < _cpLimFetch))
        std::memmove(_rgch, _rgch + _chrd.cpLim - _cpMin, (_cpLimFetch - _chrd.cpLim) * sizeof(Character));
    _cpMin = _chrd.cpLimDraw = _chrd.cpLim;
    _xpMin = _chrd.xpLimDraw = _chrd.xpLim;
    _xpBreak = Max(_xpBreak, _xpMin);
    _chrdBop = _chrd;

    if (_cpMin >= _cpLim)
        return;

    // Refill the buffer
    if (_cpLimFetch < _cpLim && _cpLimFetch < _cpMin + kcchMaxChr)
    {
        long cpFetch = Max(_cpMin, _cpLimFetch);

        _cpLimFetch = Min(kcchMaxChr, _cpLim - _cpMin) + _cpMin;
        _host->FetchRgch(cpFetch, _cpLimFetch - cpFetch, _rgch + cpFetch - _cpMin);
    }

    _fMustAdvance = fMustAdvance;
    _fBreak = false;
    _fObject = false;

    _host->SetFont(_chp);
    for (;;)
    {
        if (_chrd.cpLim >= _cpLimFetch || (fchIgnore & (grfch = _host->Flags(ch = _rgch[_chrd.cpLim - _cpMin]))))
        {
            // we're out of characters or this is an ignoreable character -
            // return the run
            if (!_FFit())
                _SetToBop();
            else
                _SkipIgnores();
            break;
        }

        if (grfch & fchTab)
        {
            // handle the string of tabs, then return the run
            _DoTab();
            break;
        }

        if (grfch & fchBreak)
        {
            // This line must break after this character - return the run.
            if (!_FFit())
            {
                _SetToBop();
                break;
            }

            _chrd.cpLim++;
            _SkipIgnores();

            // this is a BOP
            _chrdBop = _chrd;
            _fBreak = true;
            break;
        }

        if (grfch & fchMayBreak)
        {
            _chrd.cpLimDraw = ++_chrd.cpLim;
            if (_FFit())
            {
                // this is a BOP
                _chrdBop = _chrd;
                continue;
            }

            _fBreak = true;
            if (grfch & fchWhiteOverhang)
            {
                // see if everything but this character fits
                long xp = _chrd.xpLim;

                _chrd.cpLimDraw--;
                if (_FFit())
                {
                    // fits with the overhang
                    _chrd.xpLim = xp;
                    _chrdBop = _chrd;
                    _SkipIgnores();
                    break;
                }
            }

            _SetToBop();
            break;
        }

        if (kchObject == ch)
        {
            // this is an object character
            if (_chrd.cpLim > _cpMin)
            {
                // return the run before processing the object - objects
                // go in their own run.
                if (!_FFit())
                    _SetToBop();
                else
                    _chrdBop = _chrd;
                break;
            }

            // return just the object (if it really is an object)
            if (!_host->FGetObjectRc(_chrd.cpLim, &_chp, &rc))
            {
                // treat as a normal character
                _chrd.cpLimDraw = ++_chrd.cpLim;
                continue;
            }

            rc.Offset(0, _host->Offset(_chp));

            if (fMustAdvance || _xpMin + rc.xpRight <= _xpBreak)
            {
                _fObject = true;
                _chrd.cpLimDraw = ++_chrd.cpLim;
                _chrd.xpLim = _chrd.xpLimDraw = _xpMin + rc.xpRight;
                _chrdBop = _chrd;

                _dypAscent = Max(_dypAscent, -rc.ypTop);
                _dypDescent = Max(_dypDescent, rc.ypBottom);

                if (_xpMin + rc.xpRight >= _xpBreak)
                    _fBreak = true;
                _SkipIgnores();
            }
            break;
        }

        // normal character
        _chrd.cpLimDraw = ++_chrd.cpLim;
    }
}

/***************************************************************************
    Test whether everything from _cpMin to _chrd.cpLimDraw fits. Assumes the
    font is set in the _pgnv.
***************************************************************************/
template <class Host> bool CharacterRun<Host>::_FFit(void)
{

    RC rc;

    if (_chrd.cpLimDraw == _cpMin)
        _chrd.xpLim = _chrd.xpLimDraw = _xpMin;
    else
    {
        _host->GetRcFromRgch(&rc, _rgch, _chrd.cpLimDraw - _cpMin);
        _chrd.xpLim = _chrd.xpLimDraw = _xpMin + rc.Dxp();
    }

    return _chrd.xpLimDraw <= _xpBreak;
}

/***************************************************************************
    Set the CHR to the last break opportunity. If there wasn't one and
    _fMustAdvance is true, gobble as many characters as we can, but at least
    one.
***************************************************************************/
template <class Host> void CharacterRun<Host>::_SetToBop(void)
{

    _fBreak = true;
    if (_chrdBop.cpLim <= _cpMin && _fMustAdvance)
    {
        // no break opportunity seen - gobble as many characters as we can,
        // but at least one.
        // do a binary search for the character to break at

        RC rc;
        long ivMin, ivLim, iv;
        long dxp = _xpBreak - _xpMin;

        for (ivMin = 0, ivLim = _chrd.cpLimDraw - _cpMin; ivMin < ivLim;)
        {
            iv = (ivMin + ivLim) / 2 + 1;

            _host->GetRcFromRgch(&rc, _rgch, iv);
            if (rc.Dxp() <= dxp)
                ivMin = iv;
            else
                ivLim = iv - 1;
        }

        if (ivMin == 0)
        {
            // nothing fits - use one character
            ivMin = 1;
        }

        // set _chrd.cpLim and _chrd.cpLimDraw, then set the _xp values
        _chrd.cpLim = _chrd.cpLimDraw = _cpMin + ivMin;
        _FFit();
    }
    else
        _chrd = _chrdBop;

    _SkipIgnores();
}

/***************************************************************************
    Skip any trailing ignore characters. Just changes _chrd.cpLim.
***************************************************************************/
template <class Host> void CharacterRun<Host>::_SkipIgnores(void)
{

    while (_chrd.cpLim < _cpLimFetch && (fchIgnore & _host->Flags(_rgch[_chrd.cpLim - _cpMin])))
    {
        _chrd.cpLim++;
    }

    if (_chrd.cpLim == _cpLimFetch)
    {
        Character ch;
        long cpMac = _host->CpMac();

        while (_chrd.cpLim < cpMac)
        {
            _host->FetchRgch(_chrd.cpLim, 1, &ch);
            if (!(fchIgnore & _host->Flags(ch)))
                return;
            _chrd.cpLim++;
        }
    }
}

/***************************************************************************
    Swallow as many tabs as possible.
***************************************************************************/
template <class Host> void CharacterRun<Host>::_DoTab(void)
{

    if (!_FFit())
    {
        _SetToBop();
        return;
    }

    while (_chrd.cpLim < _cpLimFetch && (fchTab & _host->Flags(_rgch[_chrd.cpLim - _cpMin])))
    {
        _chrd.cpLim++;
        _chrd.xpLim = RoundAway(_chrd.xpLim + 1, _pap.dxpTab);
        if (_chrd.xpLim > _xpBreak)
        {
            // this tab would carry us over the edge.
            if (_chrd.cpLim == _cpMin + 1 && _fMustAdvance)
            {
                // the line is empty, so we have to force the tab onto the line
                _SkipIgnores();
                _fBreak = true;
            }
            else
                _SetToBop();
            return;
        }

        // this is a BOP
        _chrd.xpLimDraw = _chrd.xpLim;
        _chrdBop = _chrd;
    }

    _SkipIgnores();
}

template <class Host> void CalculateLine(Host &host, long width, long cpMin, long dypBase, Line *plin)
{

    struct RUN
    {
        long cpLim;
        long xpLim;
        long dypAscent;
        long dypDescent;
    };

    long dxpDoc;
    typename Host::ParagraphProperties pap;
    typename Host::CharacterProperties chp;
    long cpLimPap, cpLimChp;
    CharacterRun<Host> chr;
    RunData chrd, chrdBop;
    RUN run, runSure, runT;

    dxpDoc = width;
    host.FetchPap(cpMin, &pap, &cpLimPap);

    cpLimChp = cpMin;
    run.cpLim = cpMin;
    run.xpLim = 0;
    run.dypAscent = run.dypDescent = 0;
    runSure = run;

    for (;;)
    {
        // make sure the chp is valid
        if (run.cpLim >= cpLimChp)
        {
            host.FetchChp(run.cpLim, &chp, &cpLimChp);
            cpLimChp = Min(cpLimChp, cpLimPap);
            if (cpLimChp <= run.cpLim)
            {
                assert(false);
                break;
            }

            chr.Init(&chp, &pap, &host, run.cpLim, cpLimChp, run.xpLim, dxpDoc, dxpDoc);
        }

        chr.GetNextRun(runSure.cpLim == cpMin);
        chr.GetChrd(&chrd, &chrdBop);

        if (chrd.cpLim == run.cpLim)
        {
            // didn't move forward - use runSure

            run = runSure;
            break;
        }

        runT = run;
        run.cpLim = chrd.cpLim;
        run.xpLim = chrd.xpLimDraw;
        run.dypAscent = Max(run.dypAscent, chr.DypAscent());
        run.dypDescent = Max(run.dypDescent, chr.DypDescent());

        if (chr.FBreak())
        {
            // we know that this is the end of the line - use run or runT
            if (run.xpLim > chr.XpBreak() && runT.cpLim > cpMin)
                run = runT;
            break;
        }

        if (chrdBop.cpLim > runT.cpLim)
        {
            // put chrdBop info into runSure
            runSure.cpLim = chrdBop.cpLim;
            runSure.xpLim = chrdBop.xpLimDraw;
            runSure.dypAscent = Max(runSure.dypAscent, chr.DypAscent());
            runSure.dypDescent = Max(runSure.dypDescent, chr.DypDescent());
        }
    }

    switch (pap.jc)
    {
    default:
        plin->xpLeft = 0;
        break;
    case 1:
        plin->xpLeft = (short)(dxpDoc - run.xpLim);
        break;
    case 2:
        plin->xpLeft = (dxpDoc - run.xpLim) / 2;
        break;
    }

    plin->ccp = (short)(run.cpLim - cpMin);
    plin->dyp = (short)(run.dypAscent + run.dypDescent);
    if (pap.numLine != 256)
        plin->dyp = (short)MulDiv(plin->dyp, pap.numLine, 256);
    plin->dyp += pap.dypExtraLine;
    if (host.FMinPara(run.cpLim) || run.cpLim == host.CpMac())
    {
        if (pap.numAfter != 256)
            plin->dyp = (short)MulDiv(plin->dyp, pap.numAfter, 256);
        plin->dyp += pap.dypExtraAfter;
    }
    if (plin->dyp <= 0)
        plin->dyp = 1;
    plin->dypAscent = (short)run.dypAscent;
    if (plin->dypAscent <= 0)
        plin->dypAscent = 1;
    plin->cpMin = cpMin;
    plin->dypTot = dypBase;
}

} // namespace KauaiTextLayout
#endif
