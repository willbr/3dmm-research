/* Copyright (c) Microsoft Corporation.
   Licensed under the MIT License. */

/* Copyright (c) Microsoft Corporation.
   Licensed under the MIT License. */

/***************************************************************************
    Author: ShonK
    Project: Kauai
    Reviewed:
    Copyright (c) Microsoft Corporation

    Code for implementing help balloons in kidspace.

***************************************************************************/
#ifndef KIDHELP_H
#define KIDHELP_H

namespace Help {

using ScriptCompiler::PStringRegistry;

/***************************************************************************
    Help topic construction information.
***************************************************************************/
struct Topic
{
    ChunkNumber cnoBalloon;
    long hidThis;
    long hidTarget;
    ChunkNumber cnoScript;
    long dxp;
    long dyp;
    ChunkIdentification ckiSnd;
};
typedef Topic *PTopic;
const ByteOrderMask kbomHtop = 0xFFF00000;

// help topic on file
struct TopicFile
{
    short bo;
    short osk;
    Topic htop;
};

// edit control object
struct EditControl
{
    ChunkTag ctg;  // kctgEditControl
    long dxp; // width
};

/***************************************************************************
    Help text document
***************************************************************************/
enum
{
    ftxhdNil = 0,
    ftxhdCopyText = 1,
    ftxhdExpandStrings = 2,
};

typedef class TextDocument *PTextDocument;
#define TextDocument_PAR RichTextDocument
#define kclsTextDocument 'TXHD'
class TextDocument : public TextDocument_PAR
{
    RTCLASS_DEC
    ASSERT
    MARKMEM

  protected:
    enum
    {
        sprmGroup = 64, // grouped (hot) text - uses the AllocatedGroup
    };

    PRCA _prca;         // source of pictures and buttons
    Topic _htop;         // our gob creation information
    bool _fHideButtons; // whether to draw buttons

    TextDocument(PRCA prca, PDocumentBase pdocb = pvNil, ulong grfdoc = fdocNil);
    ~TextDocument(void);

    virtual bool _FReadChunk(PChunkyFile pcfl, ChunkTag ctg, ChunkNumber cno, PStringRegistry pstrg = pvNil, ulong grftxhd = ftxhdNil);
    virtual bool _FOpenArg(long icact, byte sprm, short bo, short osk);
    virtual bool _FGetObjectRc(long icact, byte sprm, PGNV pgnv, PCHP pchp, RC *prc);
    virtual bool _FDrawObject(long icact, byte sprm, PGNV pgnv, long *pxp, long yp, PCHP pchp, RC *prcClip);

  public:
    static PTextDocument PtxhdReadChunk(PRCA prca, PChunkyFile pcfl, ChunkTag ctg, ChunkNumber cno, PStringRegistry pstrg = pvNil,
                                ulong grftxhd = ftxhdExpandStrings);

    virtual bool FSaveToChunk(PChunkyFile pcfl, ChunkIdentification *pcki, bool fRedirectText = fFalse);

    bool FInsertPicture(ChunkNumber cno, void *pvExtra, long cbExtra, long cp, long ccpDel, PCHP pchp = pvNil,
                        ulong grfdoc = fdocUpdate);
    bool FInsertButton(ChunkNumber cno, ChunkNumber cnoTopic, void *pvExtra, long cbExtra, long cp, long ccpDel, PCHP pchp = pvNil,
                       ulong grfdoc = fdocUpdate);
    PRCA Prca(void)
    {
        return _prca;
    }
    bool FGroupText(long cp1, long cp2, byte bGroup, ChunkNumber cnoTopic = cnoNil, PSTN pstnTopic = pvNil);
    bool FGrouped(long cp, long *pcpMin = pvNil, long *pcpLim = pvNil, byte *pbGroup = pvNil, ChunkNumber *pcnoTopic = pvNil,
                  PSTN pstnTopic = pvNil);

    void GetHtop(PTopic phtop);
    void SetHtop(PTopic phtop);
    void HideButtons(bool fHide = fTrue)
    {
        _fHideButtons = FPure(fHide);
    }
};

/***************************************************************************
    A runtime DocumentDisplayGraphicsObject for a help topic.
***************************************************************************/
typedef class TopicGraphicsObject *PTopicGraphicsObject;
#define TopicGraphicsObject_PAR RichTextDocumentGraphicsObject
#define kclsTopicGraphicsObject 'TXHG'
class TopicGraphicsObject : public TopicGraphicsObject_PAR
{
    RTCLASS_DEC
    CMD_MAP_DEC(TopicGraphicsObject)

  protected:
    byte _bTrack;
    ChunkNumber _cnoTrack;
    long _hidBase;
    ulong _grfcust;
    PWorldOfKidspace _pwoks;

    TopicGraphicsObject(PWorldOfKidspace pwoks, PTextDocument ptxhd, PGCB pgcb);
    virtual bool _FInit(void);
    virtual bool _FRunScript(byte bGroup, ulong grfcust, long hidHit, achar ch, ChunkNumber cnoTopic = cnoNil,
                             long *plwRet = pvNil);

  public:
    static PTopicGraphicsObject PtxhgNew(PWorldOfKidspace pwoks, PTextDocument ptxhd, PGCB pgcb);

    PTextDocument Ptxhd(void)
    {
        return (PTextDocument)_ptxtb;
    }
    virtual bool FPtIn(long xp, long yp);
    virtual bool FCmdTrackMouse(PCMD_MOUSE pcmd);
    virtual bool FCmdMouseMove(PCMD_MOUSE pcmd);
    virtual bool FCmdBadKey(PCMD_BADKEY pcmd);
    virtual bool FGroupFromPt(long xp, long yp, byte *pbGroup = pvNil, ChunkNumber *pcnoTopic = pvNil);
    virtual void DoHit(byte bGroup, ChunkNumber cnoTopic, ulong grfcust, long hidHit);
    virtual void SetCursor(ulong grfcust);
};

/***************************************************************************
    Help balloon.
***************************************************************************/
typedef class Balloon *PBalloon;
#define Balloon_PAR KidspaceGraphicObject
#define kclsBalloon 'HBAL'
class Balloon : public Balloon_PAR
{
    RTCLASS_DEC

  protected:
    PTopicGraphicsObject _ptxhg;

    Balloon(GraphicsObjectBlock *pgcb);
    virtual void _SetGorp(PGORP pgorp, long dxp, long dyp);
    virtual bool _FInit(PWorldOfKidspace pwoks, PTextDocument ptxhd, Topic *phtop, PRCA prca);
    virtual bool _FSetTopic(PTextDocument ptxhd, PTopic phtop, PRCA prca);

  public:
    static PBalloon PhbalCreate(PWorldOfKidspace pwoks, PGraphicsObject pgobPar, PRCA prca, ChunkNumber cnoTopic, PTopic phtop = pvNil);
    static PBalloon PhbalNew(PWorldOfKidspace pwoks, PGraphicsObject pgobPar, PRCA prca, PTextDocument ptxhd, PTopic phtop = pvNil);

    virtual bool FSetTopic(PTextDocument ptxhd, PTopic phtop, PRCA prca);
};

/***************************************************************************
    Help balloon button.
***************************************************************************/
typedef class BalloonButton *PBalloonButton;
#define BalloonButton_PAR KidspaceGraphicObject
#define kclsBalloonButton 'HBTN'
class BalloonButton : public BalloonButton_PAR
{
    RTCLASS_DEC

  protected:
    BalloonButton(GraphicsObjectBlock *pgcb);

    byte _bGroup;
    ChunkNumber _cnoTopic;

  public:
    static PBalloonButton PhbtnNew(PWorldOfKidspace pwoks, PGraphicsObject pgobPar, long hid, ChunkNumber cno, PRCA prca, byte bGroup, ChunkNumber cnoTopic,
                          long xpLeft, long ypBottom);

    virtual bool FPtIn(long xp, long yp);
    virtual bool FCmdClicked(PCMD_MOUSE pcmd);
};

} // end of namespace Help

#endif //! KIDHELP_H
