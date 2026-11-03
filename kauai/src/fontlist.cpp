/* Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "frame.h"
ASSERTNAME
FontList vntl;
RTCLASS(FontList)

#ifdef DEBUG
/******************************************************************************
    Assert the validity of the font description.
******************************************************************************/
void FontDescription::AssertValid(LegacyUlong grf)
{
    AssertThisMem();
    AssertIn(dyp, 1, kswMax);
    AssertVar(vntl.FValidOnn(onn), "Invalid onn", &onn);
    AssertIn(tah, 0, tahLim);
    AssertIn(tav, 0, tavLim);
}
#endif // DEBUG

/******************************************************************************
    Initialize the graphics module.
******************************************************************************/
bool FInitGfx(void)
{
    return vntl.FInit();
}

/***************************************************************************
    Construct a new font list.
***************************************************************************/
FontList::FontList(void)
{
    _pgst = pvNil;
#ifdef KAUAI_SDL3
    _fPortableFonts = false;
#endif
}

/***************************************************************************
    Destroy a font list.
***************************************************************************/
FontList::~FontList(void)
{
    ReleasePpo(&_pgst);
}

#ifdef DEBUG
/***************************************************************************
    Assert the validity of the font list.
***************************************************************************/
void FontList::AssertValid(LegacyUlong grf)
{
    FontList_PAR::AssertValid(0);
    AssertPo(_pgst, 0);
}

/***************************************************************************
    Mark memory for the font table.
***************************************************************************/
void FontList::MarkMem(void)
{
    AssertValid(0);
    FontList_PAR::MarkMem();
    MarkMemObj(_pgst);
}

/***************************************************************************
    Return whether the font number is valid.
***************************************************************************/
bool FontList::FValidOnn(long onn)
{
    return pvNil != _pgst && onn >= 0 && onn < _pgst->IstnMac();
}
#endif // DEBUG

/***************************************************************************
    Find the name of the given font.
***************************************************************************/
void FontList::GetStn(long onn, PString pstn)
{
    AssertThis(0);
    AssertPo(pstn, 0);

    _pgst->GetStn(onn, pstn);
}

/***************************************************************************
    Get the font number for the given font name.
***************************************************************************/
bool FontList::FGetOnn(PString pstn, long *ponn)
{
    AssertThis(0);
    AssertPo(pstn, 0);
    AssertVarMem(ponn);

    LegacyLong index = 0;
    const bool found = _pgst->FFindStn(pstn, &index, fgstUserSorted);
    *ponn = index;
    return found;
}

/***************************************************************************
    Map a font name to an existing font.  Use platform information if
    possible.
    REVIEW shonk: implement font mapping for real.
***************************************************************************/
long FontList::OnnMapStn(PString pstn, short osk)
{
    AssertThis(0);
    AssertPo(pstn, 0);
    long onn;

    if (!FGetOnn(pstn, &onn))
        onn = _onnSystem;
    return onn;
}

/***************************************************************************
    Return the font number mac.
***************************************************************************/
long FontList::OnnMac(void)
{
    AssertThis(0);
    return _pgst->IstnMac();
}
