/* Copyright (c) Microsoft Corporation.
   Licensed under the MIT License. */

/***************************************************************************

    splot.h: Splot machine class

    Primary Author: ******
    Review Status: Reviewed

***************************************************************************/

#define SplotMachine_PAR KidspaceGraphicObject
typedef class SplotMachine *PSplotMachine;
#define kclsSplotMachine 'splt'
class SplotMachine : public SplotMachine_PAR
{
    RTCLASS_DEC
    ASSERT
    MARKMEM
    CMD_MAP_DEC(SplotMachine)

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

    /* State of the SplotMachine */
    bool _fDirty;

    /* Miscellaneous stuff */
    PDynamicArray _pglclrSav;

    SplotMachine(PGraphicsObjectBlock pgcb) : SplotMachine_PAR(pgcb)
    {
        _fDirty = fFalse;
        _pbclBkgd = _pbclCam = _pbclActr = _pbclProp = _pbclSound = pvNil;
    }

  public:
    ~SplotMachine(void);
    static PSplotMachine PsplotNew(long hidPar, long hid, PResourceCache prca);

    bool FCmdInit(PCommand pcmd);
    bool FCmdSplot(PCommand pcmd);
    bool FCmdUpdate(PCommand pcmd);
    bool FCmdDismiss(PCommand pcmd);

    PMovie Pmvie(void)
    {
        return _pmvie;
    }
};
