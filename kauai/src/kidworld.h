/* Copyright (c) Microsoft Corporation.
   Licensed under the MIT License. */

/* Copyright (c) Microsoft Corporation.
   Licensed under the MIT License. */

/***************************************************************************
    Author: ShonK
    Project: Kauai
    Copyright (c) Microsoft Corporation

    This is a class that knows how to create GOKs, Help Balloons and
    Kidspace script interpreters. It exists so an app can customize
    default behavior.

***************************************************************************/
#ifndef KIDWORLD_H
#define KIDWORLD_H

using namespace Help;
using namespace ScriptCompiler;
using GraphicalObjectRepresentation::PKidspaceGraphicObject;

/***************************************************************************
    Base KidspaceGraphicObject descriptor.
***************************************************************************/
#include "kidspace_descriptor.h"

/***************************************************************************
    World of Kidspace class.
***************************************************************************/
typedef class WorldOfKidspace *PWorldOfKidspace;
#define WorldOfKidspace_PAR GraphicsObject
#define kclsWorldOfKidspace 'WOKS'
class WorldOfKidspace : public WorldOfKidspace_PAR
{
    RTCLASS_DEC
    ASSERT
    MARKMEM

  protected:
    PStringRegistry _pstrg;
    StringRegistry _strg;
    ulong _grfcust;

    Clock _clokAnim;
    Clock _clokNoSlip;
    Clock _clokGen;
    Clock _clokReset;

  public:
    WorldOfKidspace(GraphicsObjectBlock *pgcb, PStringRegistry pstrg = pvNil);
    ~WorldOfKidspace(void);

    PStringRegistry Pstrg(void)
    {
        return _pstrg;
    }

    virtual bool FGobIn(PGraphicsObject pgob);
    virtual PKidspaceGraphicObjectDescriptor PgokdFetch(ChunkTagOrType ctg, ChunkNumber cno, PResourceCache prca);
    virtual PKidspaceGraphicObject PgokNew(PGraphicsObject pgobPar, long hid, ChunkNumber cno, PResourceCache prca);
    virtual PGraphicsObjectInterpreter PscegNew(PResourceCache prca, PGraphicsObject pgob);
    virtual PBalloon PhbalNew(PGraphicsObject pgobPar, PResourceCache prca, ChunkNumber cnoTopic, Help::PTopic phtop = pvNil);
    virtual PCommandHandler PcmhFromHid(long hid);
    virtual PGraphicsObject PgobParGob(PGraphicsObject pgob);
    virtual bool FFindFile(PString pstnSrc, PFilename pfni);
    virtual tribool TGiveAlert(PString pstn, long bk, long cok);
    virtual void Print(PString pstn);

    virtual ulong GrfcustCur(bool fAsynch = fFalse);
    virtual void ModifyGrfcust(ulong grfcustOr, ulong grfcustXor);
    virtual ulong GrfcustAdjust(ulong grfcust);

    virtual bool FModalTopic(PResourceCache prca, ChunkNumber cnoTopic, long *plwRet);
    virtual PClock PclokAnim(void)
    {
        return &_clokAnim;
    }
    virtual PClock PclokNoSlip(void)
    {
        return &_clokNoSlip;
    }
    virtual PClock PclokGen(void)
    {
        return &_clokGen;
    }
    virtual PClock PclokReset(void)
    {
        return &_clokReset;
    }
    virtual PClock PclokFromHid(long hid);
};

#endif //! KIDWORLD_H
