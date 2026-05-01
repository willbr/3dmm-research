/* Copyright (c) Microsoft Corporation.
   Licensed under the MIT License. */

/***************************************************************************

    popup.h: Popup menu classes

    Primary Author: ******
             MenuPopupFont: ******
    Review Status: REVIEWED - any changes to this file must be reviewed!

    BASE ---> CommandHandler ---> GraphicsObject ---> KidspaceGraphicObject ---> BrowserDisplay ---> BrowserList ---> MenuPopup
                                          |
                                          +------> BrowserText ---> MenuPopupFont

***************************************************************************/
#ifndef POPUP_H
#define POPUP_H

/************************************
    MenuPopup - Generic popup menu class
*************************************/
#define MenuPopup_PAR BrowserList
#define kclsMenuPopup 'MP'
typedef class MenuPopup *PMenuPopup;
class MenuPopup : public MenuPopup_PAR
{
    ASSERT
    MARKMEM
    RTCLASS_DEC
    CMD_MAP_DEC(MenuPopup)

  protected:
    long _cid;  // cid to enqueue to apply selection
    PCommandHandler _pcmh; // command handler to enqueue command to

  protected:
    virtual void _ApplySelection(long ithumSelect, long sid);
    virtual long _IthumFromThum(long thumSelect, long sidSelect);
    MenuPopup(PGraphicsObjectBlock pgcb) : MenuPopup_PAR(pgcb)
    {
    }
    bool _FInit(PResourceCache prca);

  public:
    static PMenuPopup PmpNew(long kidParent, long kidMenu, PResourceCache prca, PCommand pcmd, BrowserSelectionFlags bws, long ithumSelect, long sidSelect,
                      ChunkIdentification ckiRoot, ChunkTagOrType ctg, PCommandHandler pcmh, long cid, bool fMoveTop);

    virtual bool FCmdSelIdle(PCommand pcmd);
};

/************************************
    MenuPopupFont - Font popup menu class
*************************************/
#define MenuPopupFont_PAR BrowserText
#define kclsMenuPopupFont 'mpft'
typedef class MenuPopupFont *PMenuPopupFont;
class MenuPopupFont : public MenuPopupFont_PAR
{
    ASSERT
    MARKMEM
    RTCLASS_DEC
    CMD_MAP_DEC(MenuPopupFont)

  protected:
    void _AdjustRc(long cthum, long cfrm);

    virtual void _ApplySelection(long ithumSelect, long sid);
    virtual bool _FSetThumFrame(long istn, PGraphicsObject pgobPar);
    MenuPopupFont(PGraphicsObjectBlock pgcb) : MenuPopupFont_PAR(pgcb)
    {
    }

  public:
    static PMenuPopupFont PmpfntNew(PResourceCache prca, long kidParent, long kidMenu, PCommand pcmd, long ithumSelect, PStringTable_GST pgst);

    virtual bool FCmdSelIdle(PCommand pcmd);
};

#endif // POPUP_H
