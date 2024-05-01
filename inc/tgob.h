/* Copyright (c) Microsoft Corporation.
   Licensed under the MIT License. */

/***************************************************************************

    Lite, low-cholestoral, politically correct, ethinically and genderally
    mixed text gobs.

        TextGraphicsObject 	--->   	GraphicsObject

***************************************************************************/

#ifndef TGOB_H
#define TGOB_H

#include "frame.h"

//
// Tgob class
//
#define TextGraphicsObject_PAR GraphicsObject
#define kclsTextGraphicsObject 'tgob'
typedef class TextGraphicsObject *PTextGraphicsObject;
class TextGraphicsObject : public TextGraphicsObject_PAR
{
    RTCLASS_DEC
    ASSERT
    MARKMEM

  protected:
    long _onn;
    long _dypFont;
    String _stn;
    long _tah;
    long _tav;
    AbstractColor _acrFore;
    AbstractColor _acrBack;
    ~TextGraphicsObject(void)
    {
    }

  public:
    //
    // Create and destroy functions
    //
    TextGraphicsObject(PGraphicsObjectBlock pgcb);
    TextGraphicsObject(long hid);

    void SetFont(long onn)
    {
        AssertThis(0);
        _onn = onn;
    }
    void SetFontSize(long dypFont)
    {
        AssertThis(0);
        _dypFont = dypFont;
    }
    void SetText(PString pstn)
    {
        AssertThis(0);
        _stn = *pstn;
        InvalRc(pvNil, kginMark);
    }
    void SetAcrFore(AbstractColor acrFore)
    {
        AssertThis(0);
        _acrFore = acrFore;
    }
    void SetAcrBack(AbstractColor acrBack)
    {
        AssertThis(0);
        _acrBack = acrBack;
    }
    void SetAlign(long tah = tahLim, long tav = tavLim);
    long GetFont(void)
    {
        AssertThis(0);
        return (_onn);
    }
    long GetFontSize(void)
    {
        AssertThis(0);
        return _dypFont;
    }
    AbstractColor GetAcrFore(void)
    {
        AssertThis(0);
        return (_acrFore);
    }
    AbstractColor GetAcrBack(void)
    {
        AssertThis(0);
        return (_acrBack);
    }
    void GetAlign(long *ptah = pvNil, long *ptav = pvNil);
    static PTextGraphicsObject PtgobCreate(long kidFrm, long idsFont, long tav = tavTop, long hid = hidNil);

    virtual void Draw(PGraphicsEnvironment pgnv, RC *prcClip);
};

#endif
