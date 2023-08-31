/* Copyright (c) Microsoft Corporation.
   Licensed under the MIT License. */

/***************************************************************************

    esl.h: Easel classes

    Primary Author: ******
    Review Status: REVIEWED - any changes to this file must be reviewed!

    BASE ---> CommandHandler ---> GraphicsObject ---> KidspaceGraphicObject ---> ESL (generic easel)
                                          |
                                          +---> ESLT (text easel)
                                          |
                                          +---> ESLC (costume easel)
                                          |
                                          +---> ESLL (listener easel)
                                          |
                                          +---> ESLR (sound recording easel)

***************************************************************************/
#ifndef ESL_H
#define ESL_H

// Function to build a GraphicsObjectBlock to construct a child under a parent
bool FBuildGcb(PGCB pgcb, long kidParent, long kidChild);

// Function to set a KidspaceGraphicObject to a different state
void SetGokState(long kid, long st);

/*****************************
    The generic easel class
*****************************/
typedef class ESL *PESL;
#define ESL_PAR KidspaceGraphicObject
#define kclsESL 'ESL'
class ESL : public ESL_PAR
{
    RTCLASS_DEC
    ASSERT
    MARKMEM
    CMD_MAP_DEC(ESL)

  protected:
    ESL(PGCB pgcb) : KidspaceGraphicObject(pgcb)
    {
    }
    bool _FInit(PRCA prca, long kidEasel);
    virtual bool _FAcceptChanges(bool *pfDismissEasel)
    {
        return fTrue;
    }

  public:
    static PESL PeslNew(PRCA prca, long kidParent, long hidEasel);
    ~ESL(void);

    bool FCmdDismiss(PCommand pcmd); // Handles both OK and Cancel
};

typedef class ESLT *PESLT; // SNE needs this
/****************************************
    Spletter Name Editor class.  It's
    derived from EDSL, which is a Kauai
    single-line edit control
****************************************/
typedef class SNE *PSNE;
#define SNE_PAR EDSL
#define kclsSNE 'SNE'
class SNE : public SNE_PAR
{
    RTCLASS_DEC
    ASSERT
    MARKMEM

  protected:
    PESLT _peslt; // easel to notify when text changes

  protected:
    SNE(PEDPAR pedpar) : EDSL(pedpar)
    {
    }

  public:
    static PSNE PsneNew(PEDPAR pedpar, PESLT peslt, PSTN pstnInit);
    virtual bool FReplace(achar *prgch, long cchIns, long ich1, long ich2, long gin);
};

/****************************************
    The text easel class
****************************************/
typedef class ESLT *PESLT;
#define ESLT_PAR ESL
#define kclsESLT 'ESLT'
class ESLT : public ESLT_PAR
{
    RTCLASS_DEC
    ASSERT
    MARKMEM
    CMD_MAP_DEC(ESLT)

  protected:
    PMovie _pmvie; // Movie that this TDT is in
    PActor _pactr; // Actor of this TDT, or pvNil for new TDT
    PAPE _pape;   // Actor Preview Entity
    PSNE _psne;   // Spletter Name Editor
    PRCA _prca;   // Resource source for cursors
    PSFL _psflMtrl;
    PBCL _pbclMtrl;
    PSFL _psflTdf;
    PBCL _pbclTdf;
    PSFL _psflTdts;

  protected:
    ESLT(PGCB pgcb) : ESL(pgcb)
    {
    }
    bool _FInit(PRCA prca, long kidEasel, PMovie pmvie, PActor pactr, PSTN pstnNew, long tdtsNew, PTAG ptagTdfNew);
    virtual bool _FAcceptChanges(bool *pfDismissEasel);

  public:
    static PESLT PesltNew(PRCA prca, PMovie pmvie, PActor pactr, PSTN pstnNew = pvNil, long tdtsNew = tdtsNil,
                          PTAG ptagTdfNew = pvNil);
    ~ESLT(void);

    bool FCmdRotate(PCommand pcmd);
    bool FCmdTransmogrify(PCommand pcmd);
    bool FCmdStartPopup(PCommand pcmd);
    bool FCmdSetFont(PCommand pcmd);
    bool FCmdSetShape(PCommand pcmd);
    bool FCmdSetColor(PCommand pcmd);

    bool FTextChanged(PSTN pstn);
};

/********************************************
    The actor easel (costume changer) class
********************************************/
typedef class ESLA *PESLA;
#define ESLA_PAR ESL
#define kclsESLA 'ESLA'
class ESLA : public ESLA_PAR
{
    RTCLASS_DEC
    ASSERT
    MARKMEM
    CMD_MAP_DEC(ESLA)

  protected:
    PMovie _pmvie; // Movie that this actor is in
    PActor _pactr; // The actor that is being edited
    PAPE _pape;   // Actor Preview Entity
    PEDSL _pedsl; // Single-line edit control (for actor's name)

  protected:
    ESLA(PGCB pgcb) : ESL(pgcb)
    {
    }
    bool _FInit(PRCA prca, long kidEasel, PMovie pmvie, PActor pactr);
    virtual bool _FAcceptChanges(bool *pfDismissEasel);

  public:
    static PESLA PeslaNew(PRCA prca, PMovie pmvie, PActor pactr);
    ~ESLA(void);

    bool FCmdRotate(PCommand pcmd);
    bool FCmdTool(PCommand pcmd);
};

/****************************************
    Listener sound class
****************************************/
typedef class LSND *PLSND;
#define LSND_PAR BASE
#define kclsLSND 'LSND'
class LSND : public LSND_PAR
{
    RTCLASS_DEC
    ASSERT
    MARKMEM

  protected:
    PDynamicArray _pgltag;      // PDynamicArray in case of chained sounds
    long _vlm;        // Initial volume
    long _vlmNew;     // User can redefine with slider
    bool _fLoop;      // Looping sound
    long _objID;      // Owner's object ID
    long _sty;        // Sound type
    long _kidVol;     // Kid of volume slider
    long _kidIcon;    // Kid of sound-type icon
    long _kidEditBox; // Kid of sound-name box
    bool _fMatcher;   // Whether this is a motion-matched sound

  public:
    LSND(void)
    {
        _pgltag = pvNil;
    }
    ~LSND(void);

    bool FInit(long sty, long kidVol, long kidIcon, long kidEditBox, PDynamicArray *ppgltag, long vlm, bool fLoop, long objID,
               bool fMatcher);
    bool FValidSnd(void);
    void SetVlmNew(long vlmNew)
    {
        _vlmNew = vlmNew;
    }
    void Play(void);
    bool FChanged(long *pvlmNew, bool *pfNuked);
};

/****************************************
    The listener easel class
****************************************/
typedef class ESLL *PESLL;
#define ESLL_PAR ESL
#define kclsESLL 'ESLL'
class ESLL : public ESLL_PAR
{
    RTCLASS_DEC
    ASSERT
    MARKMEM
    CMD_MAP_DEC(ESLL)

  protected:
    PMovie _pmvie; // Movie that these sounds are in
    PScene _pscen; // Scene that these sounds are in
    PActor _pactr; // Actor that sounds are attached to (or pvNil)
    LSND _lsndSpeech;
    LSND _lsndSfx;
    LSND _lsndMidi;
    LSND _lsndSpeechMM;
    LSND _lsndSfxMM;

  protected:
    ESLL(PGCB pgcb) : ESL(pgcb)
    {
    }

    bool _FInit(PRCA prca, long kidEasel, PMovie pmvie, PActor pactr);
    virtual bool _FAcceptChanges(bool *pfDismissEasel);

  public:
    static PESLL PesllNew(PRCA prca, PMovie pmvie, PActor pactr);
    ~ESLL(void);

    bool FCmdVlm(PCommand pcmd);
    bool FCmdPlay(PCommand pcmd);
};

/****************************************
    The sound recording easel class
****************************************/
typedef class ESLR *PESLR;
#define ESLR_PAR ESL
#define kclsESLR 'ESLR'
class ESLR : public ESLR_PAR
{
    RTCLASS_DEC
    ASSERT
    MARKMEM
    CMD_MAP_DEC(ESLR)

  protected:
    PMovie _pmvie;      // The movie to insert sound into
    bool _fSpeech;     // Recording Speech or SFX?
    PEDSL _pedsl;      // Single-line edit control for sound name
    PSREC _psrec;      // Sound recording object
    Clock _clok;        // Clock to limit sound length
    bool _fRecording;  // Are we recording right now?
    bool _fPlaying;    // Are we playing back the recording?
    ulong _tsStartRec; // Time at which we started recording

  protected:
    ESLR(PGCB pgcb) : ESL(pgcb), _clok(HidUnique())
    {
    }
    bool _FInit(PRCA prca, long kidEasel, PMovie pmvie, bool fSpeech, PSTN pstnNew);
    virtual bool _FAcceptChanges(bool *pfDismissEasel);
    void _UpdateMeter(void);

  public:
    static PESLR PeslrNew(PRCA prca, PMovie pmvie, bool fSpeech, PSTN pstnNew);
    ~ESLR(void);

    bool FCmdRecord(PCommand pcmd);
    bool FCmdPlay(PCommand pcmd);
    bool FCmdUpdateMeter(PCommand pcmd);
};

#endif ESL_H
