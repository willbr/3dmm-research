/* Copyright (c) Microsoft Corporation.
   Licensed under the MIT License. */

/***************************************************************************

    popup.h: Popup menu classes

    Primary Author: ******
             MenuPopupFont: ******
    Review Status: REVIEWED - any changes to this file must be reviewed!

    BASE ---> CommandHandler ---> GraphicsObject ---> KidspaceGraphicObject ---> BrowserDisplay ---> BrowserList ---> MP
                                          |
                                          +------> BRWT ---> MenuPopupFont

***************************************************************************/
#ifndef POPUP_H
#define POPUP_H

/************************************
    MP - Generic popup menu class
*************************************/
#define MP_PAR BrowserList
#define kclsMP 'MP'
typedef class MP *PMP;
class MP : public MP_PAR
{
    ASSERT
    MARKMEM
    RTCLASS_DEC
    CMD_MAP_DEC(MP)

  protected:
    long _cid;  // cid to enqueue to apply selection
    PCommandHandler _pcmh; // command handler to enqueue command to

  protected:
    virtual void _ApplySelection(long ithumSelect, long sid);
    virtual long _IthumFromThum(long thumSelect, long sidSelect);
    MP(PGraphicsObjectBlock pgcb) : MP_PAR(pgcb)
    {
    }
    bool _FInit(PResourceCache prca);

  public:
    static PMP PmpNew(long kidParent, long kidMenu, PResourceCache prca, PCommand pcmd, BrowserSelectionFlags bws, long ithumSelect, long sidSelect,
                      ChunkIdentification ckiRoot, ChunkTagOrType ctg, PCommandHandler pcmh, long cid, bool fMoveTop);

    virtual bool FCmdSelIdle(PCommand pcmd);
};

/************************************
    MenuPopupFont - Font popup menu class
*************************************/
#define MenuPopupFont_PAR BRWT
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
