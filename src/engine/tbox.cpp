/* Copyright (c) Microsoft Corporation.
   Licensed under the MIT License. */

//
//  tbox.cpp
//
//  Author: Sean Selitrennikoff
//
//  Status: All changes must be code reviewed.
//
//  Date: November, 1994
//
//  This file contains all functionality for text box manipulation.
//
#include <exception>
#include <new>
#include "soc.h"
ASSERTNAME
#include "text_box_store_host.h"
#include "text_box_runtime_host.h"
#include "mm/text_box_undo.h"

RTCLASS(TextBox)
RTCLASS(TextBoxGobject)
RTCLASS(TextBoxBase)
RTCLASS(TextBoxClipboard)

//
// How many pixels a scrolling text box scrolls by
//
#define kdypScroll 1

//
//
// UNDO objects for text boxes
//
//

//
// Undoes changing textbox type operations
//
typedef class TextBoxUndoType *PTextBoxUndoType;

#define TextBoxUndoType_PAR MovieUndo
#define kclsTextBoxUndoType 'TUNT'
class TextBoxUndoType : public TextBoxUndoType_PAR
{
    RTCLASS_DEC
    MARKMEM
    ASSERT

  protected:
    MMTextBoxUndo _undo = {MM_BOX_UNDO_TYPE};
    TextBoxUndoType(void)
    {
    }

  public:
    friend struct TextBoxEditHost;
    MMTextBoxUndo *Backend() { return &_undo; }
    static PTextBoxUndoType PtuntNew(void);
    ~TextBoxUndoType(void);

    void SetType(bool fStory)
    {
        _undo.story = fStory;
    }
    void SetItbox(long itbox)
    {
        _undo.index = itbox;
    }

    virtual bool FDo(PDocumentBase pdocb);
    virtual bool FUndo(PDocumentBase pdocb);
};

RTCLASS(TextBoxUndoType)

//
// Undoes sizing operations
//
typedef class TextBoxUndoSize *PTextBoxUndoSize;

#define TextBoxUndoSize_PAR MovieUndo
#define kclsTextBoxUndoSize 'TUNS'
class TextBoxUndoSize : public TextBoxUndoSize_PAR
{
    RTCLASS_DEC
    MARKMEM
    ASSERT

  protected:
    MMTextBoxUndo _undo = {MM_BOX_UNDO_SIZE};
    TextBoxUndoSize(void)
    {
    }

  public:
    friend struct TextBoxEditHost;
    MMTextBoxUndo *Backend() { return &_undo; }
    static PTextBoxUndoSize PtunsNew(void);
    ~TextBoxUndoSize(void);

    void SetRc(RC *prc)
    {
        _undo.bounds = {prc->xpLeft, prc->ypTop, prc->xpRight, prc->ypBottom};
    }
    void SetItbox(long itbox)
    {
        _undo.index = itbox;
    }

    virtual bool FDo(PDocumentBase pdocb);
    virtual bool FUndo(PDocumentBase pdocb);
};

RTCLASS(TextBoxUndoSize)

//
// Undoes hiding/showing operations
//
typedef class TextBoxUndoHide *PTextBoxUndoHide;

#define TextBoxUndoHide_PAR MovieUndo
#define kclsTextBoxUndoHide 'TUNH'
class TextBoxUndoHide : public TextBoxUndoHide_PAR
{
    RTCLASS_DEC
    MARKMEM
    ASSERT

  protected:
    MMTextBoxUndo _undo = {MM_BOX_UNDO_HIDE};
    TextBoxUndoHide(void)
    {
    }

  public:
    friend struct TextBoxEditHost;
    MMTextBoxUndo *Backend() { return &_undo; }
    static PTextBoxUndoHide PtunhNew(void);
    ~TextBoxUndoHide(void);

    void SetFrmFirst(long nfrmFirst)
    {
        _undo.first = nfrmFirst;
    }
    void SetFrmLast(long nfrmMax)
    {
        _undo.end = nfrmMax;
    }
    void SetItbox(long itbox)
    {
        _undo.index = itbox;
    }

    virtual bool FDo(PDocumentBase pdocb);
    virtual bool FUndo(PDocumentBase pdocb);
};

RTCLASS(TextBoxUndoHide)

//
// Undoes all editing operations
//
typedef class TextBoxUndoEdit *PTextBoxUndoEdit;

#define TextBoxUndoEdit_PAR MovieUndo
#define kclsTextBoxUndoEdit 'TUND'
class TextBoxUndoEdit : public TextBoxUndoEdit_PAR
{
    RTCLASS_DEC
    MARKMEM
    ASSERT

  protected:
    MMTextBoxUndo _undo = {MM_BOX_UNDO_EDIT};
    TextBoxUndoEdit(void)
    {
    }

  public:
    friend struct TextBoxEditHost;
    MMTextBoxUndo *Backend() { return &_undo; }
    static PTextBoxUndoEdit PtundNew(PUndoBase pundb);
    ~TextBoxUndoEdit(void);

    void SetItbox(long itbox)
    {
        _undo.index = itbox;
    }

    virtual bool FDo(PDocumentBase pdocb);
    virtual bool FUndo(PDocumentBase pdocb);
};

RTCLASS(TextBoxUndoEdit)

//
// Undoes coloring background operations
//
typedef class TextBoxUndoColor *PTextBoxUndoColor;

#define TextBoxUndoColor_PAR MovieUndo
#define kclsTextBoxUndoColor 'TUNC'
class TextBoxUndoColor : public TextBoxUndoColor_PAR
{
    RTCLASS_DEC
    MARKMEM
    ASSERT

  protected:
    MMTextBoxUndo _undo = {MM_BOX_UNDO_COLOR};
    TextBoxUndoColor(void)
    {
    }

  public:
    friend struct TextBoxEditHost;
    MMTextBoxUndo *Backend() { return &_undo; }
    static PTextBoxUndoColor PtuncNew(void);
    ~TextBoxUndoColor(void);

    void SetItbox(long itbox)
    {
        _undo.index = itbox;
    }
    void SetAcrBack(AbstractColor acr)
    {
        _undo.color = static_cast<MMU32>(acr.LwGet());
    }

    virtual bool FDo(PDocumentBase pdocb);
    virtual bool FUndo(PDocumentBase pdocb);
};

RTCLASS(TextBoxUndoColor)

#include "text_box_edit_host.h"

//
//
// BEGIN TextBoxBase and TextBoxGobject
//
//
/****************************************************
 *
 * Creates a textbox display area.
 *
 * Parameters:
 *	ptbox - The owning text box for this view.
 *	pgcb - Creation block.
 *
 * Returns:
 *  A pointer to the view, else pvNil.
 *
 ****************************************************/
PTextBoxBase TextBoxBase::PtbxbNew(PTBOX ptbox, PGraphicsObjectBlock pgcb)
{
    AssertPo(ptbox, 0);
    AssertPvCb(pgcb, size(GraphicsObjectBlock));

    PTextBoxBase ptbxb;
    PTextBoxGobject ptbxg;
    RC rcRel, rcAbs;
    AbstractColor acr;

    //
    // Create the border
    //
    ptbxb = NewObj TextBoxBase(ptbox, pgcb);
    if (ptbxb == pvNil)
    {
        return (pvNil);
    }

    //
    // Now create the DocumentDisplayGraphicsObject area for the text
    //
    rcAbs.Set(kdzpBorderTbox, kdzpBorderTbox, -kdzpBorderTbox, -kdzpBorderTbox);
    rcRel.Set(krelZero, krelZero, krelOne, krelOne);
    pgcb->Set(pgcb->_hid, ptbxb, pgcb->_grfgob, pgcb->_gin, &rcAbs, &rcRel);
    if (pvNil == (ptbxg = (PTextBoxGobject)ptbox->PddgNew(pgcb)))
    {
        ReleasePpo(&ptbxb);
        return (pvNil);
    }

    ptbxg->SetTbxb(ptbxb);

    return ptbxb;
}

/****************************************************
 *
 * Draws a textbox border
 *
 * Parameters:
 *	pgnv - The graphic environment describing the draw.
 *	prcClip - the clipping rectangle to draw into.
 *
 * Returns:
 *  None.
 *
 ****************************************************/
void TextBoxBase::Draw(PGraphicsEnvironment pgnv, RC *prcClip)
{
    AssertThis(0);
    AssertPo(pgnv, 0);
    AssertPvCb(prcClip, size(RC));

    RC rc;
    RC rcClip;
    long lwSave;

    Assert(_ptbox->FIsVisible(), "DocumentDisplayGraphicsObject existing for invisible tbox");

    if (!_ptbox->FSelected())
    {
        return;
    }

    //
    // Draw border
    //
    pgnv->GetRcSrc(&rc);
    rc.Inset(kdzpBorderTbox / 2, kdzpBorderTbox / 2);

    if (_ptbox->FStory())
    {
        pgnv->FrameRcApt(&rc, &vaptLtGray, kacrBlack, kacrClear);
    }
    else
    {
        pgnv->FrameRcApt(&rc, &vaptLtGray, kacrYellow, kacrClear);
    }

    //
    // Upper left anchor
    //
    pgnv->GetRcSrc(&rc);
    rc.ypBottom = rc.ypTop + kdzpBorderTbox;
    lwSave = rc.xpRight;
    rc.xpRight = rc.xpLeft + kdzpBorderTbox;
    if (rcClip.FIntersect(&rc, prcClip))
    {
        pgnv->FillRc(&rcClip, kacrWhite);
        pgnv->FrameRc(&rcClip, kacrBlack);
    }

    //
    // Upper Middle anchor
    //
    rc.xpLeft = (lwSave - rc.xpLeft - kdzpBorderTbox) / 2;
    rc.xpRight = rc.xpLeft + kdzpBorderTbox;
    if (rcClip.FIntersect(&rc, prcClip))
    {
        pgnv->FillRc(&rcClip, kacrWhite);
        pgnv->FrameRc(&rcClip, kacrBlack);
    }

    //
    // Upper right anchor
    //
    rc.xpLeft = (lwSave - kdzpBorderTbox);
    rc.xpRight = lwSave;
    if (rcClip.FIntersect(&rc, prcClip))
    {
        pgnv->FillRc(&rcClip, kacrWhite);
        pgnv->FrameRc(&rcClip, kacrBlack);
    }

    //
    // Middle left anchor
    //
    pgnv->GetRcSrc(&rc);
    rc.ypTop = (rc.ypBottom - rc.ypTop - kdzpBorderTbox) / 2;
    rc.ypBottom = rc.ypTop + kdzpBorderTbox;
    lwSave = rc.xpRight;
    rc.xpRight = rc.xpLeft + kdzpBorderTbox;
    if (rcClip.FIntersect(&rc, prcClip))
    {
        pgnv->FillRc(&rcClip, kacrWhite);
        pgnv->FrameRc(&rcClip, kacrBlack);
    }

    //
    // Middle right anchor
    //
    rc.xpLeft = (lwSave - kdzpBorderTbox);
    rc.xpRight = lwSave;
    if (rcClip.FIntersect(&rc, prcClip))
    {
        pgnv->FillRc(&rcClip, kacrWhite);
        pgnv->FrameRc(&rcClip, kacrBlack);
    }

    //
    // Lower left anchor
    //
    pgnv->GetRcSrc(&rc);
    rc.ypTop = rc.ypBottom - kdzpBorderTbox;
    lwSave = rc.xpRight;
    rc.xpRight = rc.xpLeft + kdzpBorderTbox;
    if (rcClip.FIntersect(&rc, prcClip))
    {
        pgnv->FillRc(&rcClip, kacrWhite);
        pgnv->FrameRc(&rcClip, kacrBlack);
    }

    //
    // Lower middle anchor
    //
    rc.xpLeft = (lwSave - rc.xpLeft - kdzpBorderTbox) / 2;
    rc.xpRight = rc.xpLeft + kdzpBorderTbox;
    if (rcClip.FIntersect(&rc, prcClip))
    {
        pgnv->FillRc(&rcClip, kacrWhite);
        pgnv->FrameRc(&rcClip, kacrBlack);
    }

    //
    // Lower right anchor
    //
    rc.xpLeft = (lwSave - kdzpBorderTbox);
    rc.xpRight = lwSave;
    if (rcClip.FIntersect(&rc, prcClip))
    {
        pgnv->FillRc(&rcClip, kacrWhite);
        pgnv->FrameRc(&rcClip, kacrBlack);
    }
}

/***************************************************************************
 *
 * Handles mouse commands for clicking and dragging.
 *
 * Parameters:
 *  pcmd - Pointer to the mouse command.
 *
 * Returns:
 *  fTrue if it handles the command, else fFalse.
 *
 **************************************************************************/
bool TextBoxBase::FCmdTrackMouse(PCMD_MOUSE pcmd)
{
    AssertThis(0);
    AssertVarMem(pcmd);

    PMovieView pmvu;
    PT pt;
    RC rc, rcOld;
    RC rcBound(0, 0, _ptbox->Pscen()->Pmvie()->Pmcc()->Dxp(), _ptbox->Pscen()->Pmvie()->Pmcc()->Dyp());
    static long itbox = ivNil;

    pmvu = (PMovieView)_ptbox->Pscen()->Pmvie()->PddgGet(0);
    AssertPo(pmvu, 0);

    pt.xp = pcmd->xp;
    pt.yp = pcmd->yp;
    MapPt(&pt, cooLocal, cooParent);

    if (_fTrackingMouse)
    {

        if (pcmd->grfcust & fcustMouse)
        {
            _fTrackingMouse = fFalse;
            vpcex->EndMouseTracking();
            vpappb->ShowCurs();
            vpcex->EnqueueCid(cidTboxClicked, pvNil, pvNil, _ptbox->Itbox(), fTrue);
            return (fTrue);
        }
    }

    if (pcmd->cid == cidMouseDown)
    {

        Assert(vpcex->PgobTracking() == pvNil, "mouse already being tracked!");
        //
        // Select this text box if not already
        //
        if (_ptbox->Pscen()->PtboxSelected() != _ptbox)
        {
            _ptbox->Pscen()->SelectTbox(_ptbox);
        }

        itbox = _ptbox->Itbox();

        vpcex->EnqueueCid(cidTboxClicked, pvNil, pvNil, itbox, fTrue);

        //
        // Check for nuker
        //
        if (pmvu->Tool() == toolActorNuke)
        {
            _ptbox->Pscen()->Pmvie()->Pmcc()->PlayUISound(toolActorNuke, fcustShift);
            _ptbox->Pscen()->FRemTbox(_ptbox);
            return (fTrue);
        }

        if ((pmvu->Tool() != toolCutObject) && (pmvu->Tool() != toolCopyObject) && (pmvu->Tool() != toolPasteObject))
        {
            _tbxt = _TbxtAnchor(pcmd->xp, pcmd->yp);
            GetRc(&_rcOrig, cooParent);
            _xpPrev = pt.xp;
            _ypPrev = pt.yp;
            vpappb->HideCurs();
        }

        //
        // Store all initial information
        //
        vpcex->TrackMouse(this);
    }
    else
    {

        if (pmvu->Tool() == toolActorNuke)
        {
            return (fTrue);
        }

        //
        // mouse drag/up
        //
        Assert(vpcex->PgobTracking() == this, "not tracking mouse!");
        Assert(pcmd->cid == cidTrackMouse, 0);

        if (pmvu->Tool() == toolPasteObject)
        {

            if (!(pcmd->grfcust & fcustMouse))
            {
                vpcex->EnqueueCid(cidTboxClicked, pvNil, pvNil, itbox, fFalse);
                vpcex->EndMouseTracking();
            }

            return (fTrue);
        }

        if ((pmvu->Tool() == toolCutObject) || (pmvu->Tool() == toolCopyObject))
        {

            if (!(pcmd->grfcust & fcustMouse))
            {
                PTextBoxGobject ptbxg = (PTextBoxGobject)_ptbox->PddgGet(0);
                AssertPo(ptbxg, 0);
                ptbxg->_FDoClip(pmvu->Tool());
                vpcex->EndMouseTracking();
                vpcex->EnqueueCid(cidTboxClicked, pvNil, pvNil, itbox, fFalse);
            }

            return (fTrue);
        }

        //
        // Now do work based on what we are doing -- dragging, stretching, shrinking.
        //
        GetRc(&rc, cooParent);
        rcOld = rc;

        //
        // Do vertical adjustment
        //
        switch (_tbxt)
        {
        case tbxtDown:
        case tbxtDownRight:
        case tbxtDownLeft:
            rc.ypBottom = LwBound(rcOld.ypBottom + pt.yp - _ypPrev, rc.ypTop + kdypMinTbox + LwMul(2, kdzpBorderTbox),
                                  rcBound.ypBottom + 1);
            _ypPrev += rc.ypBottom - rcOld.ypBottom;
            break;

        case tbxtUp:
        case tbxtUpRight:
        case tbxtUpLeft:
            rc.ypTop = LwBound(rcOld.ypTop + pt.yp - _ypPrev, rcBound.ypTop,
                               rc.ypBottom - kdypMinTbox - LwMul(2, kdzpBorderTbox) + 1);
            _ypPrev += rc.ypTop - rcOld.ypTop;
            break;

        case tbxtMove:
            break;
        }

        //
        // Do horizontal adjustment
        //
        switch (_tbxt)
        {
        case tbxtRight:
        case tbxtDownRight:
        case tbxtUpRight:
            rc.xpRight = LwBound(rcOld.xpRight + pt.xp - _xpPrev, rc.xpLeft + kdxpMinTbox + LwMul(2, kdzpBorderTbox),
                                 rcBound.xpRight + 1);
            _xpPrev += rc.xpRight - rcOld.xpRight;
            break;

        case tbxtLeft:
        case tbxtDownLeft:
        case tbxtUpLeft:
            rc.xpLeft = LwBound(rcOld.xpLeft + pt.xp - _xpPrev, rcBound.xpLeft,
                                (rc.xpRight - (kdxpMinTbox + LwMul(2, kdzpBorderTbox))) + 1);
            _xpPrev += rc.xpLeft - rcOld.xpLeft;
            break;

        case tbxtMove:
            rc.Offset(pt.xp - _xpPrev, pt.yp - _ypPrev);
            rc.PinToRc(&rcBound);
            _xpPrev += rc.xpLeft - rcOld.xpLeft;
            _ypPrev += rc.ypTop - rcOld.ypTop;
            break;
        }

        vpappb->PositionCurs(PgobPar(), _xpPrev, _ypPrev);

        if ((rc != rcOld) && !_ptbox->Pscen()->Pmvie()->Pmcc()->FMinimized())
        {
            //
            // Reposition the textbox.
            //

            SetPos(&rc);
            _ptbox->SetRc(&rc);
        }

        //
        // If mouse up
        //
        if (!(pcmd->grfcust & fcustMouse) && !_fTrackingMouse)
        {

            //
            // Add an undo object for the resize/move
            //
            GetRc(&rc, cooLocal);
            if (_rcOrig != rc)
            {
                PTextBoxUndoSize ptuns;

                ptuns = TextBoxUndoSize::PtunsNew();
                if (ptuns == pvNil)
                {
                    PushErc(ercSocNotUndoable);
                    _ptbox->Pscen()->Pmvie()->ClearUndo();
                }
                else
                {
                    ptuns->SetItbox(_ptbox->Itbox());
                    ptuns->SetRc(&_rcOrig);

                    if (!_ptbox->Pscen()->Pmvie()->FAddUndo(ptuns))
                    {
                        PushErc(ercSocNotUndoable);
                        _ptbox->Pscen()->Pmvie()->ClearUndo();
                    }
                    ReleasePpo(&ptuns);
                }
            }

            vpcex->EnqueueCid(cidTboxClicked, pvNil, pvNil, _ptbox->Itbox(), fFalse);

            vpcex->EndMouseTracking();
            vpappb->ShowCurs();
        }
    }

    return (fTrue);
}

/***************************************************************************
 *
 * Handles mouse move commands
 *
 * Parameters:
 *  pcmd - Pointer to the mouse move command.
 *
 * Returns:
 *  fTrue if it handles the command, else fFalse.
 *
 **************************************************************************/
bool TextBoxBase::FCmdMouseMove(PCMD_MOUSE pcmd)
{
    AssertThis(0);
    AssertVarMem(pcmd);

    PMovieView pmvu;

    pmvu = (PMovieView)_ptbox->Pscen()->Pmvie()->PddgGet(0);
    AssertPo(pmvu, 0);

    //
    // Check for cut, copy, paste, nuke tools
    //
    if ((pmvu->Tool() == toolCutObject) || (pmvu->Tool() == toolCopyObject) || (pmvu->Tool() == toolPasteObject) ||
        (pmvu->Tool() == toolActorNuke))
    {
        _ptbox->Pscen()->Pmvie()->Pmcc()->SetCurs(pmvu->Tool());
        return (fTrue);
    }

    switch (_TbxtAnchor(pcmd->xp, pcmd->yp))
    {
    case tbxtUp:
    case tbxtDown:
        _ptbox->Pscen()->Pmvie()->Pmcc()->SetCurs(toolTboxUpDown);
        break;

    case tbxtUpRight:
    case tbxtDownLeft:
        _ptbox->Pscen()->Pmvie()->Pmcc()->SetCurs(toolTboxRising);
        break;

    case tbxtLeft:
    case tbxtRight:
        _ptbox->Pscen()->Pmvie()->Pmcc()->SetCurs(toolTboxLeftRight);
        break;

    case tbxtUpLeft:
    case tbxtDownRight:
        _ptbox->Pscen()->Pmvie()->Pmcc()->SetCurs(toolTboxFalling);
        break;

    default:
        _ptbox->Pscen()->Pmvie()->Pmcc()->SetCurs(toolTboxMove);
        break;
    }

    return (fTrue);
}

/***************************************************************************
 *
 * Finds which anchor point the given point is in.
 *
 * Parameters:
 *  xp - X position in local coordinates
 *	yp - Y position in local coordinates
 *
 * Returns:
 *  Anchor point number.
 *
 **************************************************************************/
TBXT TextBoxBase::_TbxtAnchor(long xp, long yp)
{
    AssertThis(0);

    RC rc;

    GetRc(&rc, cooLocal);

    //
    // Is the cursor in upper-left anchor?
    //
    if ((xp < kdzpBorderTbox) && (yp < kdzpBorderTbox))
    {
        return (tbxtUpLeft);
    }

    //
    // Is the cursor in lower-right anchor?
    //
    if ((xp > rc.xpRight - kdzpBorderTbox) && (yp > rc.ypBottom - kdzpBorderTbox))
    {
        return (tbxtDownRight);
    }

    //
    // Is the cursor in upper-right anchor?
    //
    if ((xp > rc.xpRight - kdzpBorderTbox) && (yp < kdzpBorderTbox))
    {
        return (tbxtUpRight);
    }

    //
    // Is the cursor in lower-left anchor?
    //
    if ((xp < kdzpBorderTbox) && (yp > rc.ypBottom - kdzpBorderTbox))
    {
        return (tbxtDownLeft);
    }

    //
    // Is the cursor in middle top?
    //
    if ((xp <= (rc.xpRight - rc.xpLeft + kdzpBorderTbox) / 2) &&
        (xp >= (rc.xpRight - rc.xpLeft - kdzpBorderTbox) / 2) && (yp < kdzpBorderTbox))
    {
        return (tbxtUp);
    }

    //
    // Is the cursor in middle bottom?
    //
    if ((xp <= (rc.xpRight - rc.xpLeft + kdzpBorderTbox) / 2) &&
        (xp >= (rc.xpRight - rc.xpLeft - kdzpBorderTbox) / 2) && (yp > rc.ypBottom - kdzpBorderTbox))
    {
        return (tbxtDown);
    }

    //
    // Is the cursor in middle left?
    //
    if ((yp <= (rc.ypBottom - rc.ypTop + kdzpBorderTbox) / 2) &&
        (yp >= (rc.ypBottom - rc.ypTop - kdzpBorderTbox) / 2) && (xp < kdzpBorderTbox))
    {
        return (tbxtLeft);
    }

    //
    // Is the cursor in middle right?
    //
    if ((yp <= (rc.ypBottom - rc.ypTop + kdzpBorderTbox) / 2) &&
        (yp >= (rc.ypBottom - rc.ypTop - kdzpBorderTbox) / 2) && (xp > rc.xpRight - kdzpBorderTbox))
    {
        return (tbxtRight);
    }

    //
    // Not in an anchor.
    //
    return (tbxtMove);
}

/***************************************************************************
 *
 * Callback for when this text box window gets activated
 *
 * Parameters:
 *  fActive - Is this the active window?
 *
 * Returns:
 *  None.
 *
 **************************************************************************/
void TextBoxBase::Activate(bool fActive)
{
    PDocumentDisplayGraphicsObject pddg;

    pddg = _ptbox->PddgGet(0);
    AssertPo(pddg, 0);
    pddg->Activate(fActive);

    if (fActive)
    {
        BringToFront();
        _ptbox->Pscen()->SelectTbox(_ptbox);
    }
}

/***************************************************************************
 *
 * Will return fFalse if tbox is to be ignored.
 *
 * Parameters:
 *  xp, yp - The current mouse point.
 *
 * Returns:
 *  fTrue if it handles the command, else fFalse.
 *
 **************************************************************************/
bool TextBoxBase::FPtIn(long xp, long yp)
{
    AssertThis(0);

    PMovieView pmvu;

    pmvu = (PMovieView)_ptbox->Pscen()->Pmvie()->PddgGet(0);
    AssertPo(pmvu, 0);

    //
    // Pass through if not in text mode, or in a tool that
    // does not select this text box.
    //
    if (!pmvu->FTextMode() ||
        ((pmvu->Tool() == toolSceneNuke) || (pmvu->Tool() == toolSceneChop) || (pmvu->Tool() == toolSceneChopBack)) ||
        ((_ptbox->Pscen()->PtboxSelected() != _ptbox) &&
         ((pmvu->Tool() == toolTboxStory) || (pmvu->Tool() == toolTboxCredit))))
    {
        return (fFalse);
    }

    return (TextBoxBase_PAR::FPtIn(xp, yp));
}

/****************************************************
 * Attach the mouse to this border.
 *
 * Parameters:
 * 	None.
 *
 * Returns:
 *  None.
 *
 ****************************************************/
void TextBoxBase::AttachToMouse(void)
{
    AssertThis(0);

    PMovieView pmvu;
    PT pt;

    pmvu = (PMovieView)_ptbox->Pscen()->Pmvie()->PddgGet(0);
    AssertPo(pmvu, 0);

    _fTrackingMouse = fTrue;
    _tbxt = tbxtMove;
    GetRc(&_rcOrig, cooParent);
    pt.xp = _rcOrig.xpLeft;
    pt.yp = _rcOrig.ypTop;
    _xpPrev = pt.xp;
    _ypPrev = pt.yp;
    pmvu->SetTool(toolTboxMove);
    _ptbox->Pscen()->Pmvie()->Pmcc()->ChangeTool(toolTboxMove);
    vpappb->PositionCurs(PgobPar(), _xpPrev, _ypPrev);
    vpappb->HideCurs();
    vpcex->TrackMouse(this);
}

#ifdef DEBUG

/****************************************************
 * Mark memory used by the TextBoxBase
 *
 * Parameters:
 * 	None.
 *
 * Returns:
 *  None.
 *
 ****************************************************/
void TextBoxBase::MarkMem(void)
{
    AssertThis(0);
    TextBoxBase_PAR::MarkMem();
}

/***************************************************************************
 *
 * Assert the validity of the TextBoxBase.
 *
 * Parameters:
 *	grf - Bit field of options
 *
 * Returns:
 *  None.
 *
 **************************************************************************/
void TextBoxBase::AssertValid(LegacyUlong grf)
{
    TextBoxBase_PAR::AssertValid(fobjAllocated);
}

#endif

//
// Disable the some default rich text functionality,
// and then intercept other commands.
//
BEGIN_CMD_MAP(TextBoxGobject, DocumentDisplayGraphicsObject)
ON_CID_GEN(cidSave, pvNil, pvNil)
ON_CID_GEN(cidClose, pvNil, pvNil)
ON_CID_GEN(cidSaveAndClose, pvNil, pvNil)
ON_CID_GEN(cidSaveAs, pvNil, pvNil)
ON_CID_GEN(cidSaveCopy, pvNil, pvNil)
ON_CID_GEN(cidCutTool, &TextBoxGobject::FCmdClip, &TextBoxGobject::FEnableDdgCmd)
ON_CID_GEN(cidCopyTool, &TextBoxGobject::FCmdClip, &TextBoxGobject::FEnableDdgCmd)
ON_CID_GEN(cidPasteTool, &TextBoxGobject::FCmdClip, &TextBoxGobject::FEnableDdgCmd)
ON_CID_GEN(cidPaste, &TextBoxGobject::FCmdClip, &TextBoxGobject::FEnableDdgCmd)
ON_CID_GEN(cidCut, &TextBoxGobject::FCmdClip, pvNil)
ON_CID_GEN(cidCopy, &TextBoxGobject::FCmdClip, pvNil)
ON_CID_GEN(cidUndo, pvNil, pvNil)
ON_CID_GEN(cidRedo, pvNil, pvNil)
END_CMD_MAP_NIL()

/****************************************************
 *
 * Destructor for text box DDGs.
 *
 ****************************************************/
TextBoxGobject::~TextBoxGobject()
{
}

/****************************************************
 *
 * Creates a textbox display area.
 *
 * Parameters:
 *	ptbox - The owning text box for this view.
 *	pgcb - Creation block.
 *
 * Returns:
 *  A pointer to the view, else pvNil.
 *
 ****************************************************/
PTextBoxGobject TextBoxGobject::PtbxgNew(PTBOX ptbox, PGraphicsObjectBlock pgcb)
{
    AssertPo(ptbox, 0);
    AssertPvCb(pgcb, size(GraphicsObjectBlock));

    PTextBoxGobject ptbxg;

    if (pvNil == (ptbxg = NewObj TextBoxGobject(ptbox, pgcb)))
    {
        return (pvNil);
    }

    if (!ptbxg->_FInit())
    {
        ReleasePpo(&ptbxg);
        return pvNil;
    }

    ptbxg->GetRc(&ptbxg->_rcOld, cooLocal);
    return ptbxg;
}

/****************************************************
 *
 * Draws a textbox innards
 *
 * Parameters:
 *	pgnv - The graphic environment describing the draw.
 *	prcClip - the clipping rectangle to draw into.
 *
 * Returns:
 *  None.
 *
 ****************************************************/
void TextBoxGobject::Draw(PGraphicsEnvironment pgnv, RC *prcClip)
{
    AssertThis(0);
    AssertPo(pgnv, 0);
    AssertPvCb(prcClip, size(RC));

    RC rc;

    //
    // In order to do scrolling text boxex, the easiest
    // way to get the text to scroll is to grow the DocumentDisplayGraphicsObject
    // upward (to the top of the screen), but then clip
    // the drawing to within the border.
    //
    // The DocumentDisplayGraphicsObject will automatically be clipped to within
    // the border GraphicsObject, but the drawn border (dashes and
    // anchors) must then be subtracted.
    //
    GetRc(&rc, cooParent);
    rc.ypTop = kdzpBorderTbox;
    MapRc(&rc, cooParent, cooLocal);
    rc.FIntersect(prcClip);
    pgnv->ClipRc(&rc);

    TextBoxGobject_PAR::Draw(pgnv, &rc);
}

/***************************************************************************
 *
 * Callback for when this text box window gets activated
 *
 * Parameters:
 *  fActive - Is this the active window?
 *
 * Returns:
 *  None.
 *
 **************************************************************************/
void TextBoxGobject::Activate(bool fActive)
{
    AssertThis(0);

    PTBOX ptbox = (PTBOX)_pdocb;
    AssertPo(ptbox, 0);

    TextBoxGobject_PAR::Activate(fActive);
    ptbox->Select(fActive);
}

/***************************************************************************
 *
 * Handles when the user starts typing
 *
 * Parameters:
 *  cp - Character position.
 *	ccpIns - Number of characters to insert.
 *	ccpDel - Number of characters to delete.
 *
 * Returns:
 *  None.
 *
 **************************************************************************/
void TextBoxGobject::InvalCp(long cp, long ccpIns, long ccpDel)
{
    PMovieView pmvu;
    PTBOX ptbox = (PTBOX)_pdocb;
    AssertPo(ptbox, 0);

    pmvu = (PMovieView)ptbox->Pscen()->Pmvie()->PddgGet(0);
    AssertPo(pmvu, 0);

    if (((pmvu->Tool() == toolTboxPaintText) || (pmvu->Tool() == toolTboxFont) || (pmvu->Tool() == toolTboxSize) ||
         (pmvu->Tool() == toolTboxStyle)) &&
        (ccpIns != ccpDel))
    {
        pmvu->SetTool(toolTboxMove);
        ptbox->Pscen()->Pmvie()->Pmcc()->ChangeTool(toolTboxMove);
    }

    TextBoxGobject_PAR::InvalCp(cp, ccpIns, ccpDel);
}

/***************************************************************************
 *
 * Handles mouse move commands
 *
 * Parameters:
 *  pcmd - Pointer to the mouse move command.
 *
 * Returns:
 *  fTrue if it handles the command, else fFalse.
 *
 **************************************************************************/
bool TextBoxGobject::FCmdMouseMove(PCMD_MOUSE pcmd)
{
    AssertThis(0);
    AssertVarMem(pcmd);

    PTBOX ptbox = (PTBOX)_pdocb;
    PMovieView pmvu;

    pmvu = (PMovieView)ptbox->Pscen()->Pmvie()->PddgGet(0);
    AssertPo(pmvu, 0);

    //
    // Check for cut, copy, paste tools
    //
    switch (pmvu->Tool())
    {
    case toolCutObject:
        ptbox->Pscen()->Pmvie()->Pmcc()->SetCurs(toolCutText);
        return (fTrue);

    case toolCopyObject:
        ptbox->Pscen()->Pmvie()->Pmcc()->SetCurs(toolCopyText);
        return (fTrue);

    case toolPasteObject:
        ptbox->Pscen()->Pmvie()->Pmcc()->SetCurs(toolPasteText);
        return (fTrue);

    case toolTboxFillBkgd:
    case toolTboxPaintText:
    case toolActorNuke:
    case toolTboxStory:
    case toolTboxCredit:
    case toolTboxFont:
    case toolTboxSize:
    case toolTboxStyle:
        ptbox->Pscen()->Pmvie()->Pmcc()->SetCurs(pmvu->Tool());
        return (fTrue);
    }

    ptbox->Pscen()->Pmvie()->Pmcc()->SetCurs(toolIBeam);

    return (fTrue);
}

/***************************************************************************
 *
 * Handles other mouse commands
 *
 * Parameters:
 *  pcmd - Pointer to the mouse command.
 *
 * Returns:
 *  fTrue if it handles the command, else fFalse.
 *
 **************************************************************************/
bool TextBoxGobject::FCmdTrackMouse(PCMD_MOUSE pcmd)
{
    AssertThis(0);
    AssertVarMem(pcmd);

    PTBOX ptbox = (PTBOX)_pdocb;
    PMovieView pmvu;
    CHP chpNew, chpDiff;

    chpNew.Clear();
    chpDiff.Clear();

    pmvu = (PMovieView)ptbox->Pscen()->Pmvie()->PddgGet(0);
    AssertPo(pmvu, 0);

    //
    // Check for the nuker
    //
    if (pmvu->Tool() == toolActorNuke)
    {
        if (pcmd->cid == cidMouseDown)
        {
            vpcex->EnqueueCid(cidTboxClicked, pvNil, pvNil, ptbox->Itbox(), fTrue);

            ptbox->Pscen()->Pmvie()->Pmcc()->PlayUISound(toolActorNuke, fcustShift);
            ptbox->Pscen()->FRemTbox(ptbox);
        }
        return (fTrue);
    }

    //
    // No selecting text with the fill bucket or type changers
    //
    if ((pmvu->Tool() != toolTboxFillBkgd) && (pmvu->Tool() != toolTboxStory) && (pmvu->Tool() != toolTboxCredit))
    {
        TextBoxGobject_PAR::FCmdTrackMouse(pcmd);
    }

    if ((pcmd->cid == cidMouseDown) && (pcmd->grfcust & fcustMouse))
    {

        vpcex->EnqueueCid(cidTboxClicked, pvNil, pvNil, ptbox->Itbox(), fTrue);

        switch (pmvu->Tool())
        {
        case toolTboxFillBkgd:
            ptbox->FSetAcrBack(pmvu->AcrPaint());
            ptbox->Pscen()->Pmvie()->Pmcc()->PlayUISound(pmvu->Tool());
            break;

        case toolTboxStory:
            ptbox->FSetType(fTrue);
            ptbox->Pscen()->Pmvie()->Pmcc()->PlayUISound(pmvu->Tool());
            break;

        case toolTboxCredit:
            ptbox->FSetType(fFalse);
            ptbox->Pscen()->Pmvie()->Pmcc()->PlayUISound(pmvu->Tool());
            break;
        }

        return (fTrue);
    }

    if ((pcmd->cid != cidMouseDown) && !(pcmd->grfcust & fcustMouse))
    {

        vpcex->EnqueueCid(cidTboxClicked, pvNil, pvNil, ptbox->Itbox(), fFalse);

        switch (pmvu->Tool())
        {
        case toolCutObject:
        case toolCopyObject:
        case toolPasteObject:
            _FDoClip(pmvu->Tool(), true);
            break;

        case toolTboxPaintText:
            if (!FTextSelected())
                break;
            chpNew.acrFore = pmvu->AcrPaint();
            chpDiff.acrFore.SetFromLw(~chpNew.acrFore.LwGet());
            goto LApplyFormat;
        case toolTboxFont:
            if (!FTextSelected())
                break;
            chpNew.onn = pmvu->OnnTextCur();
            chpDiff.onn = ~chpNew.onn;
            goto LApplyFormat;
        case toolTboxStyle:
            if (!FTextSelected())
                break;
            chpNew.grfont = pmvu->GrfontStyleTextCur();
            chpDiff.grfont = ~chpNew.grfont;
            goto LApplyFormat;
        case toolTboxSize:
            if (!FTextSelected())
                break;
            chpNew.dypFont = pmvu->DypFontTextCur();
            chpDiff.dypFont = ~chpNew.dypFont;
        LApplyFormat:
            ptbox->Pscen()->Pmvie()->Pmcc()->PlayUISound(pmvu->Tool());
            FApplyChp(&chpNew, &chpDiff);
            break;
        }
    }

    return (fTrue);
}

/***************************************************************************
 *
 * Will return fFalse if tbox is to be ignored.
 *
 * Parameters:
 *  xp, yp - The current mouse point.
 *
 * Returns:
 *  fTrue if it handles the command, else fFalse.
 *
 **************************************************************************/
bool TextBoxGobject::FPtIn(long xp, long yp)
{
    AssertThis(0);

    PTBOX ptbox = (PTBOX)_pdocb;
    PMovieView pmvu;

    AssertPo(ptbox, 0);

    pmvu = (PMovieView)ptbox->Pscen()->Pmvie()->PddgGet(0);
    AssertPo(pmvu, 0);

    //
    // Pass through if not in text mode, or in a tool that
    // does not select this text box.
    //
    if (!pmvu->FTextMode() ||
        ((pmvu->Tool() == toolSceneNuke) || (pmvu->Tool() == toolSceneChop) || (pmvu->Tool() == toolSceneChopBack)))
    {
        return (fFalse);
    }

    return (TextBoxGobject_PAR::FPtIn(xp, yp));
}

/***************************************************************************
 *
 * Used for resizing.  We don't actually change the width of the
 * document, only the size of the view.
 *
 * NOTE: Do not AssertThis(0) in this routine, as this routine
 * is called during a time the doc is invalid.
 *
 * Parameters:
 *  None.
 *
 * Returns:
 *	Width in pixels.
 *
 **************************************************************************/
long TextBoxGobject::_DxpDoc()
{
    AssertBaseThis(0);

    RC rc;

    GetRc(&rc, cooLocal);
    return (rc.Dxp() - 2 * kdxpIndentTxtg);
}

/***************************************************************************
 *
 * Notification that there is a new rectangle.  Here we reformat the text
 * to fit into the new rectangle.
 *
 * Parameters:
 *	None.
 *
 * Returns:
 * 	None.
 *
 **************************************************************************/
void TextBoxGobject::_NewRc(void)
{
    AssertBaseThis(0);

    RC rc;

    GetRc(&rc, cooLocal);

    //
    // Only reformat if the width changes
    //
    if (_rcOld.Dxp() == rc.Dxp())
    {
        return;
    }

    _rcOld = rc;

    TextBoxGobject_PAR::_NewRc();

    long cpLim = _ptxtb->CpMac() - 1;

    _Reformat(0, cpLim, cpLim);
}

static int TextBoxClipboardCommand(long command)
{
    switch (command)
    {
    case cidCut: return MM_BOX_CLIP_CUT;
    case cidCopy: return MM_BOX_CLIP_COPY;
    case cidPaste: return MM_BOX_CLIP_PASTE;
    case cidCutTool: return MM_BOX_CLIP_CUT_TOOL;
    case cidCopyTool: return MM_BOX_CLIP_COPY_TOOL;
    case cidPasteTool: return MM_BOX_CLIP_PASTE_TOOL;
    default: return MM_BOX_CLIP_UNKNOWN;
    }
}

struct TextBoxGobject::ClipboardHost
{
    PTextBoxGobject view;
    PTBOX box;
    PMovie movie;
    PCommand command;
    std::exception_ptr exception;

    ClipboardHost(PTextBoxGobject view, PCommand command = nullptr)
        : view(view), box(static_cast<PTBOX>(view->_pdocb)),
          movie(box->Pscen() ? box->Pscen()->Pmvie() : nullptr), command(command)
    {
    }

    MMTextBoxClipboardServices Services() { return {this, Call}; }

    int Check(int result)
    {
        if (exception)
            std::rethrow_exception(exception);
        return result;
    }

    static long CommandId(int action)
    {
        switch (action)
        {
        case MM_BOX_CLIP_CUT: return cidCut;
        case MM_BOX_CLIP_COPY: return cidCopy;
        default: return cidPaste;
        }
    }

    static long SoundTool(int action, bool inlineText)
    {
        switch (action)
        {
        case MM_BOX_CLIP_CUT: return inlineText ? toolCutText : toolCutObject;
        case MM_BOX_CLIP_COPY: return inlineText ? toolCopyText : toolCopyObject;
        default: return inlineText ? toolPasteText : toolPasteObject;
        }
    }

    static int Call(void *context, MMTextBoxClipboardOperation operation, MMTextBoxClipboardRequest *request)
    {
        auto &host = *static_cast<ClipboardHost *>(context);
        auto &q = *request;
        try
        {
            switch (operation)
            {
            case MM_BOX_CLIP_BOX_SELECTED: return host.box->FSelected();
            case MM_BOX_CLIP_TEXT_SELECTED: return host.view->FTextSelected();
            case MM_BOX_CLIP_EMPTY: return vpclip->FDocIsClip(pvNil);
            case MM_BOX_CLIP_CAN_PASTE_TEXT: return host.view->_FPaste(vpclip, fFalse, cidPaste);
            case MM_BOX_CLIP_HAS_ACTOR: return vpclip->FGetFormat(kclsActorClipboard);
            case MM_BOX_CLIP_HAS_BOX: return vpclip->FGetFormat(kclsTextBoxClipboard);
            case MM_BOX_CLIP_GET_ACTOR:
            case MM_BOX_CLIP_GET_BOX:
            {
                PDocumentBase document;
                long kind = operation == MM_BOX_CLIP_GET_ACTOR ? kclsActorClipboard : kclsTextBoxClipboard;
                if (!vpclip->FGetFormat(kind, &document))
                    return 0;
                q.object = document;
                break;
            }
            case MM_BOX_CLIP_ROUTE_ONLY:
                return static_cast<PActorClipboard>(static_cast<PDocumentBase>(q.object))->FRouteOnly();
            case MM_BOX_CLIP_RELEASE: static_cast<PDocumentBase>(q.object)->Release(); break;
            case MM_BOX_CLIP_DUPLICATE:
            {
                PTBOX duplicate;
                if (!host.box->FDup(&duplicate))
                    return 0;
                q.object = static_cast<PDocumentBase>(duplicate);
                break;
            }
            case MM_BOX_CLIP_CREATE:
                q.object = static_cast<PDocumentBase>(TextBoxClipboard::PtclpNew(
                    static_cast<PTBOX>(static_cast<PDocumentBase>(q.object))));
                return q.object != nullptr;
            case MM_BOX_CLIP_HIDE: return host.movie->FHideTbox();
            case MM_BOX_CLIP_PUBLISH: vpclip->Set(static_cast<PDocumentBase>(q.object)); break;
            case MM_BOX_CLIP_PASTE_BOX:
            {
                auto scene = host.box->Pscen();
                auto document = static_cast<PTextBoxClipboard>(static_cast<PDocumentBase>(q.object));
                return scene && document->FPaste(scene);
            }
            case MM_BOX_CLIP_EDIT_TEXT:
            {
                Command cmd = {};
                cmd.cid = CommandId(q.action);
                return host.view->TextBoxGobject_PAR::FCmdClip(&cmd);
            }
            case MM_BOX_CLIP_COLLAPSE:
                host.view->SetSel(host.view->_cpOther, host.view->_cpOther);
                break;
            case MM_BOX_CLIP_CUE:
                if (host.movie)
                    host.movie->Pmcc()->PlayUISound(SoundTool(q.action, q.inline_text != 0));
                break;
            case MM_BOX_CLIP_FORWARD:
            {
                Command cmd = *host.command;
                cmd.pcmh = host.movie->PddgGet(0);
                vpcex->EnqueueCmd(&cmd);
                break;
            }
            case MM_BOX_CLIP_DIAGNOSTIC:
                switch (q.action)
                {
                case MM_BOX_CLIP_UNSUPPORTED: PushErc(ercSocCannotPasteThatHere); break;
                case MM_BOX_CLIP_NOTHING: PushErc(ercSocNothingToPaste); break;
                default: Bug("Unknown tool type"); break;
                }
                break;
            }
            return 1;
        }
        catch (...)
        {
            host.exception = std::current_exception();
            return -1;
        }
    }
};

bool TextBoxGobject::FCmdClip(PCommand pcmd)
{
    AssertThis(0);
    AssertVarMem(pcmd);

    ClipboardHost host(this, pcmd);
#ifdef DEBUG
    auto view = static_cast<PMovieView>(host.movie->PddgGet(0));
    AssertPo(view, 0);
    Assert(view->FTextMode(), "Bad mode");
#endif
    const auto services = host.Services();
    return host.Check(mm_text_box_clipboard_command(TextBoxClipboardCommand(pcmd->cid), &services)) > 0;
}

bool TextBoxGobject::FEnableDdgCmd(PCommand pcmd, ulong *pgrfeds)
{
    AssertThis(0);
    AssertVarMem(pcmd);
    AssertVarMem(pgrfeds);

    ClipboardHost host(this);
    const auto services = host.Services();
    *pgrfeds = fedsEnable;
    const int result = host.Check(mm_text_box_clipboard_enable(TextBoxClipboardCommand(pcmd->cid), &services));
    if (result == MM_BOX_CLIP_USE_PARENT)
        return TextBoxGobject_PAR::FEnableDdgCmd(pcmd, pgrfeds);
    if (result == MM_BOX_CLIP_DISABLED)
        *pgrfeds = fedsDisable;
    return fTrue;
}

bool TextBoxGobject::_FDoClip(long tool, bool inlineText)
{
    int action = MM_BOX_CLIP_UNKNOWN;
    switch (tool)
    {
    case toolCutObject: action = MM_BOX_CLIP_CUT; break;
    case toolCopyObject: action = MM_BOX_CLIP_COPY; break;
    case toolPasteObject: action = MM_BOX_CLIP_PASTE; break;
    }

    AssertThis(0);
    ClipboardHost host(this);
    const auto services = host.Services();
    return host.Check(mm_text_box_clipboard_click(action, inlineText, &services)) > 0;
}

/****************************************************
 *
 * Decides if a text box needs to scroll right now.
 *
 * Parameters:
 *	None.
 *
 * Returns:
 *  fTrue if yes, else fFalse.
 *
 ****************************************************/
bool TextBoxGobject::FNeedToScroll()
{
    AssertThis(0);

    RC rc;
    long dyp;

    //
    // Get the height of the remaining text
    //
    GetNaturalSize(pvNil, &dyp);
    dyp -= _dypDisp;
    GetRc(&rc, cooLocal);

    //
    // Check vs the height of the box.
    //
    return dyp > rc.Dyp();
}

/****************************************************
 *
 * Scrolls up by one pixel, or to the beginning of the text.
 *
 * Parameters:
 *	scaVert - How to scroll vertically, scaNil == go to top,
 *		anything else scrolls down by 1 pixel.
 *
 * Returns:
 *  None.
 *
 ****************************************************/
void TextBoxGobject::Scroll(long scaVert)
{
    AssertThis(0);

    RC rcAbs, rcRel;

    if (scaVert == scaNil)
    {
        _Scroll(scaToVal, scaToVal, 0, 0);
    }
    else
    {
        GetPos(&rcAbs, &rcRel);
        rcAbs.ypTop -= kdypScroll;
        SetPos(&rcAbs, &rcRel);
    }
}

/****************************************************
 *
 * Tells if any text is selected or not.
 *
 * Parameters:
 *	None.
 *
 * Returns:
 *  None.
 *
 ****************************************************/
bool TextBoxGobject::FTextSelected(void)
{
    AssertThis(0);
    return (_cpAnchor != _cpOther);
}

/****************************************************
 *
 * Get the character properties for displaying the given cp.
 *
 * Parameters:
 *	cp - Character position.
 *	pchp - Destination chp.
 *	pcpMin - Starting character position.
 *	pcpLim - Ending character position.
 *
 * Returns:
 * 	None.
 *
 ****************************************************/
void TextBoxGobject::_FetchChp(long cp, PCHP pchp, long *pcpMin, long *pcpLim)
{
    TextBoxGobject_PAR::_FetchChp(cp, pchp, pcpMin, pcpLim);

    if (pchp->acrFore == kacrBlack)
    {
        pchp->acrFore = kacrYellow;
    }
}

#ifdef DEBUG

/****************************************************
 * Mark memory used by the TextBoxGobject
 *
 * Parameters:
 * 	None.
 *
 * Returns:
 *  None.
 *
 ****************************************************/
void TextBoxGobject::MarkMem(void)
{
    AssertThis(0);
    TextBoxGobject_PAR::MarkMem();
    MarkMemObj(_ptbxb);
}

/***************************************************************************
 *
 * Assert the validity of the TextBoxGobject.
 *
 * Parameters:
 *	grf - Bit field of options
 *
 * Returns:
 *  None.
 *
 **************************************************************************/
void TextBoxGobject::AssertValid(LegacyUlong grf)
{
    TextBoxGobject_PAR::AssertValid(fobjAllocated);
    AssertPo(_ptbxb, 0);
}

#endif // DEBUG

//
//
// BEGIN TextBox
//
//

/****************************************************
 *
 * Creates a textbox
 *
 * Parameters:
 *	pscen - Scene which owns this textbox.
 *	prcRel - The bounding rectangle of the DocumentDisplayGraphicsObject for the text box within the
 *		the owning MovieView.
 *	fStory - Is this a story text box?
 *
 * Returns:
 *  None.
 *
 ****************************************************/
PTBOX TextBox::PtboxNew(PScene pscen, RC *prcRel, bool fStory)
{
    AssertNilOrPo(pscen, 0); AssertPvCb(prcRel, size(RC));
    PTBOX result = nullptr;
    TextBoxEditHost host;
    host.bounds = prcRel; host.boxOutput = &result;
    auto services = host.Services();
    host.Check(mm_text_box_create(pscen, fStory, &services));
    return result;
}

/****************************************************
 *
 * Sets the dirty flag on the movie.
 *
 * Parameters:
 *	fDirty - To dirty, or not to dirty, that is the question.
 *
 * Returns:
 *  None.
 *
 ****************************************************/
void TextBox::SetDirty(bool fDirty)
{
    AssertThis(0);
    TextBoxRuntimeHost host = {this};
    auto services = host.Services();
    host.Check(mm_text_box_dirty(&_runtime, fDirty, &services));
}

/****************************************************
 *
 * Reads a textbox
 *
 * Parameters:
 *	pcfl - Chunky file to read from.
 *	cno - The chunk number to read.
 *	pscen - Scene which owns this textbox.
 *
 * Returns:
 *  Pointer to a new tbox, else pvNil.
 *
 ****************************************************/
PTBOX TextBox::PtboxRead(PChunkyResourceFile pcrf, ChunkNumber cno, PScene pscen)
{
    AssertPo(pcrf, 0); AssertNilOrPo(pscen, 0);
    PTBOX result = nullptr;
    TextBoxEditHost host;
    host.file = pcrf->Pcfl(); host.boxOutput = &result;
    auto services = host.Services();
    host.Check(mm_text_box_read_document(pscen, cno, &services));
    return result;
}

/****************************************************
 *
 * Writes the text box to a specifc chunk number
 *
 * Parameters:
 *	pcfl - The chunky file to write to.
 *	cno - The chunk number to write to.
 *
 * Returns:
 *  fTrue if successful, else fFalse.
 *
 ****************************************************/
bool TextBox::FWrite(PChunkyFile pcfl, ChunkNumber cno)
{
    AssertThis(0);
    AssertPo(pcfl, 0);
    TextBoxStoreHost host = {this, &_data, pcfl};
    auto services = host.Services();
    const int result = mm_text_box_store(cno, &services);
    if (host.exception)
        std::rethrow_exception(host.exception);
    return result > 0;
}

/****************************************************
 *
 * Sets the owning scene of the textbox.
 *
 * Parameters:
 *	pscen - The owning scene of the textbox.
 *
 * Returns:
 *  None.
 *
 ****************************************************/
void TextBox::SetScen(PScene pscen)
{
    AssertThis(0); AssertPo(pscen, 0);
    TextBoxRuntimeHost host = {this};
    auto services = host.Services();
    host.Check(mm_text_box_attach(&_data.state, &_runtime, pscen, &services));
}

/****************************************************
 *
 * Sets the type of the textbox.
 *
 * Parameters:
 *	fStory - fTrue if it is to become a story textbox,
 *		else fFalse.
 *
 * Returns:
 *  None.
 *
 ****************************************************/
void TextBox::SetTypeCore(bool fStory)
{
    AssertThis(0);
    TextBoxRuntimeHost host = {this};
    auto services = host.Services();
    host.Check(mm_text_box_set_type(&_data.state, fStory, &services));
}

/****************************************************
 *
 * Attaches the textbox to the mouse.
 *
 * Parameters:
 *	None.
 *
 * Returns:
 *  None.
 *
 ****************************************************/
void TextBox::AttachToMouse(void)
{
    AssertThis(0);
    TextBoxRuntimeHost host = {this};
    auto services = host.Services();
    host.Check(mm_text_box_attach_mouse(&services));
}

/****************************************************
 *
 * Sets the type of the textbox and creates an undo object
 *
 * Parameters:
 *	fStory - fTrue if it is to become a story textbox,
 *		else fFalse.
 *
 * Returns:
 *  fTrue if successful, else fFalse.
 *
 ****************************************************/
bool TextBox::FSetType(bool fStory)
{
    AssertThis(0);
    TextBoxEditHost host; host.box = this;
    auto services = host.Services();
    return host.Check(mm_text_box_change(this, MM_BOX_UNDO_TYPE, fStory, &services)) > 0;
}

/****************************************************
 *
 * Sets the bounding rectangle of the textbox.
 *
 * Parameters:
 *	prc - The new rectangle
 *
 * Returns:
 *  None.
 *
 ****************************************************/
void TextBox::SetRc(RC *prc)
{
    AssertThis(0); AssertPvCb(prc, size(RC));
    MMTextBoxBounds bounds = {prc->xpLeft, prc->ypTop, prc->xpRight, prc->ypBottom};
    TextBoxRuntimeHost host = {this};
    host.nativeBounds = prc;
    host.liveBounds = &bounds;
    auto services = host.Services();
    host.Check(mm_text_box_set_bounds(&_data.state, &_runtime, &bounds, &services));
}

/****************************************************
 *
 * Returns if the text box is currently visible or not.
 *
 * Parameters:
 *	None.
 *
 * Returns:
 *  fTrue if visible, else fFalse.
 *
 ****************************************************/
bool TextBox::FIsVisible(void)
{
    AssertThis(0);
    return mm_text_box_visible(&_data.state) != 0;
}

/***************************************************************************
 *
 * Returns the starting and ending frames the text box appears in.
 *
 * Parameters:
 *  pnfrmStart - Pointer to storage for first frame.
 *  pnfrmLast - Pointer to storage for final frame.
 *
 * Returns:
 *  fTrue if the lifetime is valid, else fFalse.
 *
 **************************************************************************/
bool TextBox::FGetLifetime(long *pnfrmStart, long *pnfrmLast)
{
    AssertThis(0); AssertVarMem(pnfrmStart); AssertVarMem(pnfrmLast);
    TextBoxRuntimeHost host = {this};
    auto services = host.Services();
    MMTextBoxLifetime value = {};
    int result = mm_text_box_lifetime(&_data.state, &value, &services);
    if (value.published & 1) *pnfrmStart = value.first;
    if (value.published & 2) *pnfrmLast = value.last;
    return host.Check(result) > 0;
}

/****************************************************
 *
 * Selects/Deselects a text box.
 *
 * Parameters:
 *	fSel - fTrue if the text box is to be selected, else
 *		fFalse.
 *
 * Returns:
 *  None.
 *
 ****************************************************/
void TextBox::Select(bool fSel)
{
    AssertThis(0);
    TextBoxRuntimeHost host = {this};
    auto services = host.Services();
    host.Check(mm_text_box_select(&_runtime, fSel, &services));
}

/****************************************************
 *
 * Makes a text box goto a certain frame.
 *
 * Parameters:
 *	nfrm - The destination frame number.
 *
 * Returns:
 *  fTrue if successful, else fFalse.
 *
 ****************************************************/
bool TextBox::FGotoFrame(long nfrm)
{
    AssertThis(0);
    TextBoxRuntimeHost host = {this};
    auto services = host.Services();
    return host.Check(mm_text_box_goto(&_data.state, nfrm, &services)) > 0;
}

/****************************************************
 *
 * Makes a text box visible at a certain frame.
 *
 * Parameters:
 *	None.
 *
 * Returns:
 *  fTrue if successful, else fFalse.
 *
 ****************************************************/
bool TextBox::FShowCore(void)
{
    AssertThis(0);
    TextBoxRuntimeHost host = {this};
    auto services = host.Services();
    return host.Check(mm_text_box_show(&_data.state, &services)) > 0;
}

/****************************************************
 *
 * Makes a text box visible at a certain frame and an undo.
 *
 * Parameters:
 *	None.
 *
 * Returns:
 *  fTrue if successful, else fFalse.
 *
 ****************************************************/
bool TextBox::FShow(void)
{
    AssertThis(0);
    TextBoxEditHost host; host.box = this;
    auto services = host.Services();
    return host.Check(mm_text_box_show_edit(this, &services)) > 0;
}

/****************************************************
 *
 * Removes a text box at a certain frame.
 *
 * Parameters:
 *	None.
 *
 * Returns:
 *  fTrue if successful, else fFalse.
 *
 ****************************************************/
void TextBox::HideCore(void)
{
    AssertThis(0);
    TextBoxRuntimeHost host = {this};
    auto services = host.Services();
    host.Check(mm_text_box_hide(&_data.state, &services));
}

/****************************************************
 *
 * Removes a text box at a certain frame and has an undo.
 *
 * Parameters:
 *	None.
 *
 * Returns:
 *  fTrue if successful, else fFalse.
 *
 ****************************************************/
bool TextBox::FHide(void)
{
    AssertThis(0);
    TextBoxEditHost host; host.box = this;
    auto services = host.Services();
    return host.Check(mm_text_box_hide_edit(this, &services)) > 0;
}

/****************************************************
 *
 * Copies an entire text box.
 *
 * Parameters:
 *	pptbox - Place to store the new text box.
 *
 * Returns:
 *  fTrue if successful, else fFalse.
 *
 ****************************************************/
bool TextBox::FDup(PTBOX *pptbox)
{
    AssertThis(0);
    TextBoxEditHost host; host.box = this; host.boxOutput = pptbox;
    auto services = host.Services();
    return host.Check(mm_text_box_duplicate(this, &services)) > 0;
}

/****************************************************
 *
 * Sets the background color of the text box.
 *
 * Parameters:
 *	acr - The color for the background.
 *
 * Returns:
 *  fTrue if successful, else fFalse.
 *
 ****************************************************/
bool TextBox::FSetAcrBack(AbstractColor acr)
{
    AssertThis(0);
    TextBoxEditHost host; host.box = this;
    auto services = host.Services();
    return host.Check(mm_text_box_change(this, MM_BOX_UNDO_COLOR, static_cast<MMU32>(acr.LwGet()), &services)) > 0;
}

/****************************************************
 *
 * Sets the color for text in the text box.
 *
 * Parameters:
 *	acr - The color for the text.
 *
 * Returns:
 *  fTrue if possible, else fFalse.
 *
 ****************************************************/
bool TextBox::FSetAcrText(AbstractColor acr)
{
    AssertThis(0);
    TextBoxEditHost host; host.box = this;
    auto services = host.Services();
    return host.Check(mm_text_box_apply_selection(this, MM_BOX_TEXT_COLOR, static_cast<MMU32>(acr.LwGet()), &services)) > 0;
}

/******************************************************************************
    FSetDypFontText
        Sets the font size for the current selection of the textbox

    Arguments:
        long dypFont  --  The new font size

    Returns:  fTrue if it succeeds

************************************************************ PETED ***********/
bool TextBox::FSetDypFontText(long dypFont)
{
    AssertThis(0);
    TextBoxEditHost host; host.box = this;
    auto services = host.Services();
    return host.Check(mm_text_box_apply_selection(this, MM_BOX_TEXT_HEIGHT, static_cast<MMU32>(dypFont), &services)) > 0;
}

/******************************************************************************
    FSetStyleText
        Sets the font style for the current selection of the textbox

    Arguments:
        long grfont  --  The new font style

    Returns:  fTrue if it succeeds

************************************************************ PETED ***********/
bool TextBox::FSetStyleText(ulong grfont)
{
    AssertThis(0);
    TextBoxEditHost host; host.box = this;
    auto services = host.Services();
    return host.Check(mm_text_box_apply_selection(this, MM_BOX_TEXT_STYLE, grfont, &services)) > 0;
}

/******************************************************************************
    FSetOnnText
        Sets the font face for the current selection of the textbox

    Arguments:
        long onn  --  the new font face

    Returns:  fTrue if it succeeds

************************************************************ PETED ***********/
bool TextBox::FSetOnnText(long onn)
{
    AssertThis(0);
    TextBoxEditHost host; host.box = this;
    auto services = host.Services();
    return host.Check(mm_text_box_apply_selection(this, MM_BOX_TEXT_FONT, static_cast<MMU32>(onn), &services)) > 0;
}

/******************************************************************************
    FetchChpSel
        Gets the character formatting for the current selection of the active
        DocumentDisplayGraphicsObject for this TextBox.  Returns the formatting of the first character of
        the selection in the CHP, and sets the corresponding bit in *pgrfchp
        if that particular formatting holds for the entire selection.

    Arguments:
        PCHP pchp       --  the CHP to take the formatting info
        ulong *pgrfchp  --  bitfield that indicates which formatting attributes
            hold for the entire selection.
************************************************************ PETED ***********/
void TextBox::FetchChpSel(PCHP pchp, ulong *pgrfchp)
{
    AssertVarMem(pchp); AssertVarMem(pgrfchp);
    TextBoxEditHost host; host.box = this; host.characterOutput = pchp; host.maskOutput = pgrfchp;
    auto services = host.Services();
    host.Check(mm_text_box_fetch_selection(this, &services));
}

/****************************************************
 *
 * Decides if a text box needs to scroll right now.
 *
 * Parameters:
 *	None.
 *
 * Returns:
 *  fTrue if yes, else fFalse.
 *
 ****************************************************/
bool TextBox::FNeedToScroll(void)
{
    AssertThis(0);
    TextBoxRuntimeHost host = {this};
    auto services = host.Services();
    return host.Check(mm_text_box_needs_scroll(&_data.state, &services)) > 0;
}

/****************************************************
 *
 * Scrolls up by one line.
 *
 * Parameters:
 *	None.
 *
 * Returns:
 *  None.
 *
 ****************************************************/
void TextBox::Scroll(void)
{
    AssertThis(0);
    TextBoxRuntimeHost host = {this};
    auto services = host.Services();
    host.Check(mm_text_box_scroll(&services));
}

/****************************************************
 *
 * Tells if any text is selected or not.
 *
 * Parameters:
 *	None.
 *
 * Returns:
 *  None.
 *
 ****************************************************/
bool TextBox::FTextSelected(void)
{
    AssertThis(0);
    TextBoxRuntimeHost host = {this};
    auto services = host.Services();
    return host.Check(mm_text_box_text_selected(&_data.state, &services)) > 0;
}

/****************************************************
 *
 * Sets the starting frame number for a text box.
 *
 * Parameters:
 *	nfrm - The new starting frame number.
 *
 * Returns:
 *  None.
 *
 ****************************************************/
void TextBox::SetStartFrame(long nfrm)
{
    AssertThis(0);
    _data.state.first = nfrm;
}

/****************************************************
 *
 * Adds an undo object to the movie.
 *
 * Parameters:
 *	pundb - The undo object to do.
 *
 * Returns:
 *  fTrue if successful, else fFalse.
 *
 ****************************************************/
bool TextBox::FAddUndo(PUndoBase pundb)
{
    AssertThis(0); AssertPo(Pscen(), 0);
    TextBoxEditHost host; host.box = this;
    auto services = host.Services();
    return host.Check(mm_text_box_add_edit_undo(this, pundb, &services)) > 0;
}

/****************************************************
 *
 * Clears the undo buffer.
 *
 * Parameters:
 *	None.
 *
 * Returns:
 *  None.
 *
 ****************************************************/
void TextBox::ClearUndo()
{
    AssertThis(0);
    TextBoxEditHost host; host.box = this;
    auto services = host.Services();
    host.Check(mm_text_box_clear_edit_undo(this, &services));
}

/****************************************************
 *
 * Ensure that the DocumentDisplayGraphicsObject for this tbox is the proper size,
 * used for cleaning after a playback.
 *
 * Parameters:
 * 	None.
 *
 * Returns:
 *  None.
 *
 ****************************************************/
void TextBox::CleanDdg(void)
{
    AssertThis(0);

    PDocumentDisplayGraphicsObject pddg;
    RC rcAbs, rcRel;

    pddg = PddgGet(0);
    if (pddg == pvNil)
    {
        return;
    }

    AssertPo(pddg, 0);

    pddg->GetPos(&rcAbs, &rcRel);
    rcAbs.Set(kdzpBorderTbox, kdzpBorderTbox, -kdzpBorderTbox, -kdzpBorderTbox);
    pddg->SetPos(&rcAbs, &rcRel);
}

/****************************************************
 *
 * Get the Itbox number for this tbox.
 *
 * Parameters:
 * 	None.
 *
 * Returns:
 *  Itbox.
 *
 ****************************************************/
long TextBox::Itbox(void)
{
    AssertThis(0);
    TextBoxEditHost host; host.box = this;
    auto services = host.Services();
    MMI32 index = 0;
    host.Check(mm_text_box_index(this, &index, &services));
    return index;
}

#ifdef DEBUG

/****************************************************
 * Mark memory used by the TextBox
 *
 * Parameters:
 * 	None.
 *
 * Returns:
 *  None.
 *
 ****************************************************/
void TextBox::MarkMem(void)
{
    AssertThis(0);
    TextBox_PAR::MarkMem();
}

/***************************************************************************
 *
 * Assert the validity of the TextBox.
 *
 * Parameters:
 *	grf - Bit field of options
 *
 * Returns:
 *  None.
 *
 **************************************************************************/
void TextBox::AssertValid(LegacyUlong grf)
{
    TextBox_PAR::AssertValid(fobjAllocated);
    if (PddgGet(0) != pvNil)
    {
        AssertPo(PddgGet(0), 0);
    }
}

#endif // DEBUG

//
//
//
// BEGIN UNDO STUFF
//
//
//

/****************************************************
 *
 * Public constructor for textbox undo objects.
 *
 * Parameters:
 *	None.
 *
 * Returns:
 *  pvNil if failure, else a pointer to the movie undo.
 *
 ****************************************************/
PTextBoxUndoType TextBoxUndoType::PtuntNew()
{
    PTextBoxUndoType ptunt;
    ptunt = NewObj TextBoxUndoType();
    return (ptunt);
}

/****************************************************
 *
 * Destructor for text box undo objects
 *
 ****************************************************/
TextBoxUndoType::~TextBoxUndoType(void)
{
}

/****************************************************
 *
 * Does a command stored in an undo object.
 *
 * Parameters:
 *	None.
 *
 * Returns:
 *  fTrue if successful, else fFalse.
 *
 ****************************************************/
bool TextBoxUndoType::FDo(PDocumentBase pdocb)
{
    AssertThis(0); AssertPo(pdocb, 0);
    TextBoxUndoHost host; host.owner = this;
    auto services = host.Services();
    return host.Check(mm_text_box_undo_replay(&_undo, 0, &services)) > 0;
}

/****************************************************
 *
 * Undoes a command stored in an undo object.
 *
 * Parameters:
 *	None.
 *
 * Returns:
 *  fTrue if successful, else fFalse.
 *
 ****************************************************/
bool TextBoxUndoType::FUndo(PDocumentBase pdocb)
{
    AssertThis(0); AssertPo(pdocb, 0);
    TextBoxUndoHost host; host.owner = this;
    auto services = host.Services();
    return host.Check(mm_text_box_undo_replay(&_undo, 1, &services)) > 0;
}

#ifdef DEBUG
/****************************************************
 * Mark memory used by the TextBoxUndoType
 *
 * Parameters:
 * 	None.
 *
 * Returns:
 *  None.
 *
 ****************************************************/
void TextBoxUndoType::MarkMem(void)
{
    AssertThis(0);
    TextBoxUndoType_PAR::MarkMem();
}

/***************************************************************************
    Assert the validity of the TextBoxUndoType.
***************************************************************************/
void TextBoxUndoType::AssertValid(LegacyUlong grf)
{
    TextBoxUndoType_PAR::AssertValid(fobjAllocated);
}
#endif

/****************************************************
 *
 * Public constructor for textbox undo objects.
 *
 * Parameters:
 *	None.
 *
 * Returns:
 *  pvNil if failure, else a pointer to the movie undo.
 *
 ****************************************************/
PTextBoxUndoSize TextBoxUndoSize::PtunsNew()
{
    PTextBoxUndoSize ptuns;
    ptuns = NewObj TextBoxUndoSize();
    return (ptuns);
}

/****************************************************
 *
 * Destructor for text box undo objects
 *
 ****************************************************/
TextBoxUndoSize::~TextBoxUndoSize(void)
{
}

/****************************************************
 *
 * Does a command stored in an undo object.
 *
 * Parameters:
 *	None.
 *
 * Returns:
 *  fTrue if successful, else fFalse.
 *
 ****************************************************/
bool TextBoxUndoSize::FDo(PDocumentBase pdocb)
{
    AssertThis(0); AssertPo(pdocb, 0);
    TextBoxUndoHost host; host.owner = this;
    auto services = host.Services();
    return host.Check(mm_text_box_undo_replay(&_undo, 0, &services)) > 0;
}

/****************************************************
 *
 * Undoes a command stored in an undo object.
 *
 * Parameters:
 *	None.
 *
 * Returns:
 *  fTrue if successful, else fFalse.
 *
 ****************************************************/
bool TextBoxUndoSize::FUndo(PDocumentBase pdocb)
{
    AssertThis(0); AssertPo(pdocb, 0);
    TextBoxUndoHost host; host.owner = this;
    auto services = host.Services();
    return host.Check(mm_text_box_undo_replay(&_undo, 1, &services)) > 0;
}

#ifdef DEBUG
/****************************************************
 * Mark memory used by the TextBoxUndoSize
 *
 * Parameters:
 * 	None.
 *
 * Returns:
 *  None.
 *
 ****************************************************/
void TextBoxUndoSize::MarkMem(void)
{
    AssertThis(0);
    TextBoxUndoSize_PAR::MarkMem();
}

/***************************************************************************
    Assert the validity of the TextBoxUndoSize.
***************************************************************************/
void TextBoxUndoSize::AssertValid(LegacyUlong grf)
{
    TextBoxUndoSize_PAR::AssertValid(fobjAllocated);
}
#endif

/****************************************************
 *
 * Public constructor for textbox undo objects.
 *
 * Parameters:
 *	None.
 *
 * Returns:
 *  pvNil if failure, else a pointer to the movie undo.
 *
 ****************************************************/
PTextBoxUndoHide TextBoxUndoHide::PtunhNew()
{
    PTextBoxUndoHide ptunh;
    ptunh = NewObj TextBoxUndoHide();
    return (ptunh);
}

/****************************************************
 *
 * Destructor for text box undo objects
 *
 ****************************************************/
TextBoxUndoHide::~TextBoxUndoHide(void)
{
    AssertBaseThis(0);
}

/****************************************************
 *
 * Does a command stored in an undo object.
 *
 * Parameters:
 *	None.
 *
 * Returns:
 *  fTrue if successful, else fFalse.
 *
 ****************************************************/
bool TextBoxUndoHide::FDo(PDocumentBase pdocb)
{
    AssertThis(0); AssertPo(pdocb, 0);
    TextBoxUndoHost host; host.owner = this;
    auto services = host.Services();
    return host.Check(mm_text_box_undo_replay(&_undo, 0, &services)) > 0;
}

/****************************************************
 *
 * Undoes a command stored in an undo object.
 *
 * Parameters:
 *	None.
 *
 * Returns:
 *  fTrue if successful, else fFalse.
 *
 ****************************************************/
bool TextBoxUndoHide::FUndo(PDocumentBase pdocb)
{
    AssertThis(0); AssertPo(pdocb, 0);
    TextBoxUndoHost host; host.owner = this;
    auto services = host.Services();
    return host.Check(mm_text_box_undo_replay(&_undo, 1, &services)) > 0;
}

#ifdef DEBUG
/****************************************************
 * Mark memory used by the TextBoxUndoHide
 *
 * Parameters:
 * 	None.
 *
 * Returns:
 *  None.
 *
 ****************************************************/
void TextBoxUndoHide::MarkMem(void)
{
    AssertThis(0);
    TextBoxUndoHide_PAR::MarkMem();
}

/***************************************************************************
    Assert the validity of the TextBoxUndoHide.
***************************************************************************/
void TextBoxUndoHide::AssertValid(LegacyUlong grf)
{
    TextBoxUndoHide_PAR::AssertValid(fobjAllocated);
}
#endif

/****************************************************
 *
 * Public constructor for textbox undo objects which
 * are the result of the document changing.
 *
 * Parameters:
 *	pundb - the undo object to encapsulate.
 *
 * Returns:
 *  pvNil if failure, else a pointer to the movie undo.
 *
 ****************************************************/
PTextBoxUndoEdit TextBoxUndoEdit::PtundNew(PUndoBase pundb)
{
    AssertPo(pundb, 0);
    PTextBoxUndoEdit result = nullptr;
    TextBoxEditHost host; host.undoOutput = &result;
    auto services = host.Services();
    host.Check(mm_text_box_wrap_undo(pundb, &services));
    return result;
}

/****************************************************
 *
 * Destructor for text box undo objects
 *
 ****************************************************/
TextBoxUndoEdit::~TextBoxUndoEdit(void)
{
    AssertBaseThis(0);
    TextBoxUndoHost host; host.owner = this;
    auto services = host.Services();
    host.Check(mm_text_box_undo_dispose(&_undo, &services));
}

/****************************************************
 *
 * Does a command stored in an undo object.
 *
 * Parameters:
 *	None.
 *
 * Returns:
 *  fTrue if successful, else fFalse.
 *
 ****************************************************/
bool TextBoxUndoEdit::FDo(PDocumentBase pdocb)
{
    AssertThis(0); AssertPo(pdocb, 0);
    TextBoxUndoHost host; host.owner = this;
    auto services = host.Services();
    return host.Check(mm_text_box_undo_replay(&_undo, 0, &services)) > 0;
}

/****************************************************
 *
 * Undoes a command stored in an undo object.
 *
 * Parameters:
 *	None.
 *
 * Returns:
 *  fTrue if successful, else fFalse.
 *
 ****************************************************/
bool TextBoxUndoEdit::FUndo(PDocumentBase pdocb)
{
    AssertThis(0); AssertPo(pdocb, 0);
    TextBoxUndoHost host; host.owner = this;
    auto services = host.Services();
    return host.Check(mm_text_box_undo_replay(&_undo, 1, &services)) > 0;
}

#ifdef DEBUG
/****************************************************
 * Mark memory used by the TextBoxUndoEdit
 *
 * Parameters:
 * 	None.
 *
 * Returns:
 *  None.
 *
 ****************************************************/
void TextBoxUndoEdit::MarkMem(void)
{
    AssertThis(0);
    TextBoxUndoEdit_PAR::MarkMem();
    MarkMemObj(static_cast<PUndoBase>(_undo.edit));
}

/***************************************************************************
    Assert the validity of the TextBoxUndoEdit.
***************************************************************************/
void TextBoxUndoEdit::AssertValid(LegacyUlong grf)
{
    AssertBaseThis(0);
}
#endif

/****************************************************
 *
 * Public constructor for textbox undo objects which
 * are the result of background color changing.
 *
 * Parameters:
 *	None.
 *
 * Returns:
 *  pvNil if failure, else a pointer to the movie undo.
 *
 ****************************************************/
PTextBoxUndoColor TextBoxUndoColor::PtuncNew(void)
{
    PTextBoxUndoColor ptunc;

    ptunc = NewObj TextBoxUndoColor();
    return (ptunc);
}

/****************************************************
 *
 * Destructor for text box undo objects
 *
 ****************************************************/
TextBoxUndoColor::~TextBoxUndoColor(void)
{
    AssertBaseThis(0);
}

/****************************************************
 *
 * Does a command stored in an undo object.
 *
 * Parameters:
 *	pdocb - The owning docb.
 *
 * Returns:
 *  fTrue if successful, else fFalse.
 *
 ****************************************************/
bool TextBoxUndoColor::FDo(PDocumentBase pdocb)
{
    AssertThis(0); AssertPo(pdocb, 0);
    TextBoxUndoHost host; host.owner = this;
    auto services = host.Services();
    return host.Check(mm_text_box_undo_replay(&_undo, 0, &services)) > 0;
}

/****************************************************
 *
 * Undoes a command stored in an undo object.
 *
 * Parameters:
 *	None.
 *
 * Returns:
 *  fTrue if successful, else fFalse.
 *
 ****************************************************/
bool TextBoxUndoColor::FUndo(PDocumentBase pdocb)
{
    AssertThis(0); AssertPo(pdocb, 0);
    TextBoxUndoHost host; host.owner = this;
    auto services = host.Services();
    return host.Check(mm_text_box_undo_replay(&_undo, 1, &services)) > 0;
}

#ifdef DEBUG
/****************************************************
 * Mark memory used by the TextBoxUndoColor
 *
 * Parameters:
 * 	None.
 *
 * Returns:
 *  None.
 *
 ****************************************************/
void TextBoxUndoColor::MarkMem(void)
{
    AssertThis(0);
    TextBoxUndoColor_PAR::MarkMem();
}

/***************************************************************************
    Assert the validity of the TextBoxUndoColor.
***************************************************************************/
void TextBoxUndoColor::AssertValid(LegacyUlong grf)
{
    AssertBaseThis(0);
}
#endif

//
//
//
//
// BEGIN TextBoxClipboard
//
//
//
//

/***************************************************************************
 *
 * Destructor for text box clipboard documents
 *
 **************************************************************************/
TextBoxClipboard::~TextBoxClipboard(void)
{
    TextBoxEditHost host;
    auto services = host.Services();
    host.Check(mm_text_box_clipboard_dispose(&_clipboard, &services));
}

/***************************************************************************
 *
 * Destructor for text box clipboard documents
 *
 * Parameters:
 *	ptbox - The tbox to associate with the clipboard.
 *
 * Returns:
 *  Pointer to a clipboard document, or pvNil if failure.
 *
 **************************************************************************/
PTextBoxClipboard TextBoxClipboard::PtclpNew(PTBOX ptbox)
{
    AssertPo(ptbox, 0);
    PTextBoxClipboard result = nullptr;
    TextBoxEditHost host; host.clipboardOutput = &result;
    auto services = host.Services();
    host.Check(mm_text_box_clipboard_create(ptbox, &services));
    return result;
}

/***************************************************************************
 *
 * Pastes the text box associated with this clipboard object to the
 * current frame.
 *
 * Parameters:
 *	pscen - The scene to paste into.
 *
 * Returns:
 *  fTrue if successful, else fFalse.
 *
 **************************************************************************/
bool TextBoxClipboard::FPaste(PScene pscen)
{
    AssertThis(0); AssertPo(pscen, 0);
    TextBoxEditHost host; host.scene = pscen;
    auto services = host.Services();
    return host.Check(mm_text_box_clipboard_paste(&_clipboard, &services)) > 0;
}

#ifdef DEBUG

/****************************************************
 * Mark memory used by the TextBoxClipboard
 *
 * Parameters:
 * 	None.
 *
 * Returns:
 *  None.
 *
 ****************************************************/
void TextBoxClipboard::MarkMem(void)
{
    AssertThis(0);
    TextBoxClipboard_PAR::MarkMem();
    MarkMemObj(static_cast<PTBOX>(_clipboard.text));
}

/***************************************************************************
 *
 * Assert the validity of the TextBoxClipboard.
 *
 * Parameters:
 *	grf - Bit field of options
 *
 * Returns:
 *  None.
 *
 **************************************************************************/
void TextBoxClipboard::AssertValid(LegacyUlong grf)
{
    TextBoxClipboard_PAR::AssertValid(fobjAllocated);
    AssertPo(static_cast<PTBOX>(_clipboard.text), 0);
}

#endif // DEBUG
