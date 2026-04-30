/* Copyright (c) Microsoft Corporation.
   Licensed under the MIT License. */

/***************************************************************************

    esl.h: Easel classes

    Primary Author: ******
    Review Status: REVIEWED - any changes to this file must be reviewed!

    BASE ---> CommandHandler ---> GraphicsObject ---> KidspaceGraphicObject ---> Easel (generic easel)
                                          |
                                          +---> EaselText (text easel)
                                          |
                                          +---> EaselC (costume easel)
                                          |
                                          +---> EaselListen (listener easel)
                                          |
                                          +---> EaselRecord (sound recording easel)

***************************************************************************/
#ifndef Easel_H
#define Easel_H

// Function to build a GraphicsObjectBlock to construct a child under a parent
bool FBuildGcb(PGraphicsObjectBlock pgcb, long kidParent, long kidChild);

// Function to set a KidspaceGraphicObject to a different state
void SetGokState(long kid, long st);

/*****************************
    The generic easel class
*****************************/
typedef class Easel *PEasel;
#define Easel_PAR KidspaceGraphicObject
#define kclsEasel 'ESL'
class Easel : public Easel_PAR
{
    RTCLASS_DEC
    ASSERT
    MARKMEM
    CMD_MAP_DEC(Easel)

  protected:
    Easel(PGraphicsObjectBlock pgcb) : KidspaceGraphicObject(pgcb)
    {
    }
    bool _FInit(PResourceCache prca, long kidEasel);
    virtual bool _FAcceptChanges(bool *pfDismissEasel)
    {
        return fTrue;
    }

  public:
    static PEasel PeslNew(PResourceCache prca, long kidParent, long hidEasel);
    ~Easel(void);

    bool FCmdDismiss(PCommand pcmd); // Handles both OK and Cancel
};

typedef class EaselText *PEaselText; // SpletterNameEditor needs this
/****************************************
    Spletter Name Editor class.  It's
    derived from EDSL, which is a Kauai
    single-line edit control
****************************************/
typedef class SpletterNameEditor *PSpletterNameEditor;
#define SpletterNameEditor_PAR EDSL
#define kclsSpletterNameEditor 'SNE'
class SpletterNameEditor : public SpletterNameEditor_PAR
{
    RTCLASS_DEC
    ASSERT
    MARKMEM

  protected:
    PEaselText _peslt; // easel to notify when text changes

  protected:
    SpletterNameEditor(PEDPAR pedpar) : EDSL(pedpar)
    {
    }

  public:
    static PSpletterNameEditor PsneNew(PEDPAR pedpar, PEaselText peslt, PString pstnInit);
    virtual bool FReplace(achar *prgch, long cchIns, long ich1, long ich2, long gin);
};

/****************************************
    The text easel class
****************************************/
typedef class EaselText *PEaselText;
#define EaselText_PAR Easel
#define kclsEaselText 'ESLT'
class EaselText : public EaselText_PAR
{
    RTCLASS_DEC
    ASSERT
    MARKMEM
    CMD_MAP_DEC(EaselText)

  protected:
    PMovie _pmvie; // Movie that this TDT is in
    PActor _pactr; // Actor of this TDT, or pvNil for new TDT
    PActorPreviewEntity _pape;   // Actor Preview Entity
    PSpletterNameEditor _psne;   // Spletter Name Editor
    PResourceCache _prca;   // Resource source for cursors
    PShuffler _psflMtrl;
    PBrowserContentList _pbclMtrl;
    PShuffler _psflTdf;
    PBrowserContentList _pbclTdf;
    PShuffler _psflTdts;

  protected:
    EaselText(PGraphicsObjectBlock pgcb) : Easel(pgcb)
    {
    }
    bool _FInit(PResourceCache prca, long kidEasel, PMovie pmvie, PActor pactr, PString pstnNew, long tdtsNew, PTAG ptagTdfNew);
    virtual bool _FAcceptChanges(bool *pfDismissEasel);

  public:
    static PEaselText PesltNew(PResourceCache prca, PMovie pmvie, PActor pactr, PString pstnNew = pvNil, long tdtsNew = tdtsNil,
                          PTAG ptagTdfNew = pvNil);
    ~EaselText(void);

    bool FCmdRotate(PCommand pcmd);
    bool FCmdTransmogrify(PCommand pcmd);
    bool FCmdStartPopup(PCommand pcmd);
    bool FCmdSetFont(PCommand pcmd);
    bool FCmdSetShape(PCommand pcmd);
    bool FCmdSetColor(PCommand pcmd);

    bool FTextChanged(PString pstn);
};

/********************************************
    The actor easel (costume changer) class
********************************************/
typedef class EaselActor *PEaselActor;
#define EaselActor_PAR Easel
#define kclsEaselActor 'ESLA'
class EaselActor : public EaselActor_PAR
{
    RTCLASS_DEC
    ASSERT
    MARKMEM
    CMD_MAP_DEC(EaselActor)

  protected:
    PMovie _pmvie; // Movie that this actor is in
    PActor _pactr; // The actor that is being edited
    PActorPreviewEntity _pape;   // Actor Preview Entity
    PEDSL _pedsl; // Single-line edit control (for actor's name)

  protected:
    EaselActor(PGraphicsObjectBlock pgcb) : Easel(pgcb)
    {
    }
    bool _FInit(PResourceCache prca, long kidEasel, PMovie pmvie, PActor pactr);
    virtual bool _FAcceptChanges(bool *pfDismissEasel);

  public:
    static PEaselActor PeslaNew(PResourceCache prca, PMovie pmvie, PActor pactr);
    ~EaselActor(void);

    bool FCmdRotate(PCommand pcmd);
    bool FCmdTool(PCommand pcmd);
};

/****************************************
    Listener sound class
****************************************/
typedef class ListenerSound *PListenerSound;
#define ListenerSound_PAR BASE
#define kclsListenerSound 'LSND'
class ListenerSound : public ListenerSound_PAR
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
    ListenerSound(void)
    {
        _pgltag = pvNil;
    }
    ~ListenerSound(void);

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
typedef class EaselListen *PEaselListen;
#define EaselListen_PAR Easel
#define kclsEaselListen 'ESLL'
class EaselListen : public EaselListen_PAR
{
    RTCLASS_DEC
    ASSERT
    MARKMEM
    CMD_MAP_DEC(EaselListen)

  protected:
    PMovie _pmvie; // Movie that these sounds are in
    PScene _pscen; // Scene that these sounds are in
    PActor _pactr; // Actor that sounds are attached to (or pvNil)
    ListenerSound _lsndSpeech;
    ListenerSound _lsndSfx;
    ListenerSound _lsndMidi;
    ListenerSound _lsndSpeechMM;
    ListenerSound _lsndSfxMM;

  protected:
    EaselListen(PGraphicsObjectBlock pgcb) : Easel(pgcb)
    {
    }

    bool _FInit(PResourceCache prca, long kidEasel, PMovie pmvie, PActor pactr);
    virtual bool _FAcceptChanges(bool *pfDismissEasel);

  public:
    static PEaselListen PesllNew(PResourceCache prca, PMovie pmvie, PActor pactr);
    ~EaselListen(void);

    bool FCmdVlm(PCommand pcmd);
    bool FCmdPlay(PCommand pcmd);
};

/****************************************
    The sound recording easel class
****************************************/
typedef class EaselRecord *PEaselRecord;
#define EaselRecord_PAR Easel
#define kclsEaselRecord 'ESLR'
class EaselRecord : public EaselRecord_PAR
{
    RTCLASS_DEC
    ASSERT
    MARKMEM
    CMD_MAP_DEC(EaselRecord)

  protected:
    PMovie _pmvie;      // The movie to insert sound into
    bool _fSpeech;     // Recording Speech or SFX?
    PEDSL _pedsl;      // Single-line edit control for sound name
    PSoundRecorder _psrec;      // Sound recording object
    Clock _clok;        // Clock to limit sound length
    bool _fRecording;  // Are we recording right now?
    bool _fPlaying;    // Are we playing back the recording?
    ulong _tsStartRec; // Time at which we started recording

  protected:
    EaselRecord(PGraphicsObjectBlock pgcb) : Easel(pgcb), _clok(HidUnique())
    {
    }
    bool _FInit(PResourceCache prca, long kidEasel, PMovie pmvie, bool fSpeech, PString pstnNew);
    virtual bool _FAcceptChanges(bool *pfDismissEasel);
    void _UpdateMeter(void);

  public:
    static PEaselRecord PeslrNew(PResourceCache prca, PMovie pmvie, bool fSpeech, PString pstnNew);
    ~EaselRecord(void);

    bool FCmdRecord(PCommand pcmd);
    bool FCmdPlay(PCommand pcmd);
    bool FCmdUpdateMeter(PCommand pcmd);
};

#endif Easel_H
