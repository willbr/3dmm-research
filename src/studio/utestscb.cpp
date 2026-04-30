/* Copyright (c) Microsoft Corporation.
   Licensed under the MIT License. */

//
//  movie.cpp
//
//  Author: Sean Selitrennikoff
//
//  Date: August, 1994
//
//  This file contains all functionality for movie manipulation.
//
#include "soc.h"
ASSERTNAME

//
//
//
// BEGIN MScrollBar STUFF
//
//
//

RTCLASS(MScrollBar)

BEGIN_CMD_MAP(MScrollBar, GraphicsObject)
ON_CID_ME(cidDoScroll, MScrollBar::FCmdScroll, pvNil)
ON_CID_ME(cidEndScroll, MScrollBar::FCmdScroll, pvNil)
END_CMD_MAP_NIL()

/****************************************************
 *
 * Constructor for movie scroll bars.  This function
 * is private, use PmscbNew() for public construction.
 *
 ****************************************************/
MScrollBar::MScrollBar(PMovie pmvie, PGraphicsObjectBlock pgcb) : GraphicsObject(pgcb)
{
    _pmvie = pmvie;
}

/****************************************************
 *
 * Public constructor for movie scroll bars.
 *
 * Parameters:
 *	pmvie - The owning movie
 *	pgcb - The creation block for location and size
 *
 * Returns:
 *	A pointer to the scroll bar objects, else pvNil.
 *
 ****************************************************/
PMScrollBar MScrollBar::PmscbNew(PMovie pmvie, PGraphicsObjectBlock pgcb)
{
    PMScrollBar pmscb;
    RC rc;
    GraphicsObjectBlock gcb;

    AssertNilOrPo(pmvie, 0);
    AssertVarMem(pgcb);

    if (pvNil == (pmscb = NewObj MScrollBar(pmvie, pgcb)))
        return pvNil;

    //
    // Frame slider
    //
    rc.Set(0, 32, 200, 48);
    gcb.Set(mscbhidFrame, pmscb, fgobNil, kginDefault, &rc);
    ScrollBar::PscbNew(&gcb, fscbHorz, 0, 0, 0);

    //
    // Scene slider
    //
    rc.Set(250, 32, 450, 48);
    gcb.Set(mscbhidScene, pmscb, fgobNil, kginDefault, &rc);
    ScrollBar::PscbNew(&gcb, fscbHorz, 0, 0, 0);

    AssertPo(pmscb, 0);
    return (pmscb);
}

/****************************************************
 *
 * Processes scroll commands from the framework
 *
 * Parameters:
 *	pcmd - The command to process
 *
 * Returns:
 *	fTrue if is processed the command, else fFalse.
 *
 ****************************************************/
bool MScrollBar::FCmdScroll(PCommand pcmd)
{
    long hid, val, dval;
    bool fUp, fPage;
    PScrollBar pscb;

    //
    // Verify that the command is for this object
    //
    hid = pcmd->rglw[0];

    if ((hid != mscbhidFrame) && (hid != mscbhidScene))
    {
        return (fFalse);
    }

    //
    // Get the scroll bar.
    //
    pscb = (PScrollBar)PgobFromHidScr(hid);

    if ((pscb == pvNil) || (_pmvie == pvNil))
        return fTrue;

    //
    // Process the command
    //
    switch (pcmd->cid)
    {
    case cidDoScroll:
        if (pcmd->rglw[1] == scaToVal)
        {
            return (fTrue);
        }
        else
        {
            fUp = (pcmd->rglw[1] == scaLineUp) || (pcmd->rglw[1] == scaPageUp);
            fPage = (pcmd->rglw[1] == scaPageUp) || (pcmd->rglw[1] == scaPageDown);
            dval = fPage ? 10 : 1;
            if (fUp)
                dval = -dval;
            val = pscb->Val() + dval;
        }
        break;

    case cidEndScroll:
        val = pcmd->rglw[1];
        break;
    }

    //
    // pin the new value to the max and min
    //
    val = LwMin(pscb->ValMax(), LwMax(pscb->ValMin(), val));

    //
    // Any changes?
    //
    if (val != pscb->Val() && !_pmvie->FPlaying())
    {

        //
        // Process new value
        //
        pscb->SetVal(val);

        if (hid == mscbhidFrame)
        {
            if (!_pmvie->Pscen()->FGotoFrm(val))
            {
                // REVIEW seanse: what to do?
            }
        }
        else // scene change
        {
            if (!_pmvie->FSwitchScen(val))
            {
                // REVIEW seanse: what to do?
            }
        }

        _pmvie->MarkViews();
        InvalRc(pvNil);
    }

    return fTrue;
}

/***************************************************************************
 *
 * Draw the scroll bars.
 *
 * Parameters:
 *	pgnv - The environment to write to.
 *  prcClip - The clipping rectangle.
 *
 * Returns:
 *  None.
 *
 ***************************************************************************/
void MScrollBar::Draw(PGraphicsEnvironment pgnv, RC *prcClip)
{
    AssertThis(0);
    AssertPo(pgnv, 0);
    AssertVarMem(prcClip);
    String stn;
    RC rc;
    PScrollBar pscb;

    pscb = (PScrollBar)PgobFromHidScr(mscbhidScene);

    if (pscb != pvNil)
    {
        if ((_pmvie != pvNil) && (_pmvie->Pscen() != pvNil))
        {
            pscb->SetValMinMax(_pmvie->Iscen(), 0, _pmvie->Cscen() - 1);
        }
        else
        {
            pscb->SetValMinMax(0, 0, 0);
        }

        pscb->InvalRc(pvNil);
    }

    pscb = (PScrollBar)PgobFromHidScr(mscbhidFrame);

    if (pscb != pvNil)
    {
        if ((_pmvie != pvNil) && (_pmvie->Pscen() != pvNil))
        {
            pscb->SetValMinMax(_pmvie->Pscen()->Nfrm(), _pmvie->Pscen()->NfrmFirst(), _pmvie->Pscen()->NfrmLast());
        }
        else
        {
            pscb->SetValMinMax(0, 0, 0);
        }

        pscb->InvalRc(pvNil);
    }

    if (_pmvie != pvNil)
    {
        float fps;

        fps = ((float)_pmvie->Cnfrm() * kdtsSecond) / ((float)(TsCurrent() - _pmvie->TsStart()));

        if (_pmvie->Pscen() != pvNil)
        {
            stn.FFormatSz(PszLit("Frame #%d    "), _pmvie->Pscen()->Nfrm());
        }
        else
        {
            stn.FFormatSz(PszLit("Frame #%d    "), 0);
        }
        pgnv->DrawStn(&stn, 20, 16, kacrBlack, kacrWhite);

        stn.FFormatSz(PszLit("Scene #%d    "), _pmvie->Iscen() + 1);
        pgnv->DrawStn(&stn, 270, 16, kacrBlack, kacrWhite);

        stn.FFormatSz(PszLit("Fps: %2d.%02d"), (int)fps, (int)((fps - (int)fps) * 100));
        pgnv->DrawStn(&stn, 450, 16, kacrRed, kacrWhite);

        if (_pmvie->Pscen() != pvNil)
        {
            _pmvie->Pscen()->GetName(&stn);
            pgnv->DrawStn(&stn, 160, 0, kacrBlack, kacrWhite);
        }
    }
}

/****************************************************
 *
 * Update the scroll bars
 *
 * Parameters:
 * 	None.
 *
 * Returns:
 *  None.
 *
 ****************************************************/
void MScrollBar::Update()
{
    InvalRc(pvNil, kginMark);
}

/****************************************************
 *
 * Change to movie for these scroll bars
 *
 * Parameters:
 * 	pmvie - New movie
 *
 * Returns:
 *  None.
 *
 ****************************************************/
void MScrollBar::SetMvie(PMovie pmvie)
{
    _pmvie = pmvie;
}

#ifdef DEBUG

/****************************************************
 * Mark memory used by the MScrollBar
 *
 * Parameters:
 * 	None.
 *
 * Returns:
 *  None.
 *
 ****************************************************/
void MScrollBar::MarkMem(void)
{
    PScrollBar pscb;

    AssertThis(0);
    MScrollBar_PAR::MarkMem();

    pscb = (PScrollBar)PgobFromHidScr(mscbhidFrame);

    if (pscb != pvNil)
    {
        MarkMemObj(pscb);
    }

    pscb = (PScrollBar)PgobFromHidScr(mscbhidScene);
    if (pscb != pvNil)
    {
        MarkMemObj(pscb);
    }
}

/***************************************************************************
 * Assert the validity of the MScrollBar
 *
 * Parameters:
 * 	grf - bit array of options.
 *
 * Returns:
 *  None.
 *
 **************************************************************************/
void MScrollBar::AssertValid(ulong grf)
{
    MScrollBar_PAR::AssertValid(fobjAllocated);
}

#endif
