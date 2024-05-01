/* Copyright (c) Microsoft Corporation.
   Licensed under the MIT License. */

/***************************************************************************

    splot.h: Splot machine class

    Primary Author: ******
    Review Status: Reviewed

***************************************************************************/

#define SPLOT_PAR KidspaceGraphicObject
typedef class SPLOT *PSPLOT;
#define kclsSPLOT 'splt'
class SPLOT : public SPLOT_PAR
{
    RTCLASS_DEC
    ASSERT
    MARKMEM
    CMD_MAP_DEC(SPLOT)

  private:
    /* The movie */
    PMovie _pmvie;

    /* The lists of content */
    PBrowserContentList _pbclBkgd;
    SFL _sflBkgd;
    PBrowserContentList _pbclCam;
    SFL _sflCam;
    PBrowserContentList _pbclActr;
    SFL _sflActr;
    PBrowserContentList _pbclProp;
    SFL _sflProp;
    PBrowserContentList _pbclSound;
    SFL _sflSound;

    /* Current selected content */
    long _ithdBkgd;
    long _ithdCam;
    long _ithdActr;
    long _ithdProp;
    long _ithdSound;

    /* State of the SPLOT */
    bool _fDirty;

    /* Miscellaneous stuff */
    PDynamicArray _pglclrSav;

    SPLOT(PGraphicsObjectBlock pgcb) : SPLOT_PAR(pgcb)
    {
        _fDirty = fFalse;
        _pbclBkgd = _pbclCam = _pbclActr = _pbclProp = _pbclSound = pvNil;
    }

  public:
    ~SPLOT(void);
    static PSPLOT PsplotNew(long hidPar, long hid, PResourceCache prca);

    bool FCmdInit(PCommand pcmd);
    bool FCmdSplot(PCommand pcmd);
    bool FCmdUpdate(PCommand pcmd);
    bool FCmdDismiss(PCommand pcmd);

    PMovie Pmvie(void)
    {
        return _pmvie;
    }
};
