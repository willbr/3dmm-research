/* Copyright (c) Microsoft Corporation.
   Licensed under the MIT License. */

/* Copyright (c) Microsoft Corporation.
   Licensed under the MIT License. */

/***************************************************************************
    Author: ShonK
    Project: Kauai
    Reviewed:
    Copyright (c) Microsoft Corporation

    Script interpreter for the gob based scripts.

***************************************************************************/
#ifndef SCREXEG_H
#define SCREXEG_H

namespace ScriptInterpreter {

/****************************************
    Gob based script interpreter
****************************************/
typedef class GraphicsObjectInterpreter *PGraphicsObjectInterpreter;
#define GraphicsObjectInterpreter_PAR Interpreter
#define kclsGraphicsObjectInterpreter 'SCEG'
class GraphicsObjectInterpreter : public GraphicsObjectInterpreter_PAR
{
    RTCLASS_DEC
    ASSERT

  protected:
    // CAUTION: _pgob may be nil (even if the gob still exists)! Always access
    // thru _PgobThis.  When something is done that may cause the gob to be
    // freed (such as calling another script), set this to nil.
    PGraphicsObject _pgob;
    LegacyLong _hid;    // the handler id of the initialization gob
    LegacyLong _grid;   // the unique gob run-time id of the initialization gob
    PWorldOfKidspace _pwoks; // the kidspace world this script belongs to

    virtual PGraphicsObject _PgobThis(void);
    virtual PGraphicsObject _PgobFromHid(LegacyLong hid);

    bool _FExecOp(LegacyLong op) override;
    PDynamicArray *_PpglrtvmThis(void) override;
    PDynamicArray *_PpglrtvmGlobal(void) override;
    PDynamicArray *_PpglrtvmRemote(LegacyLong lw) override;

    short _SwCur(void) override;
    short _SwMin(void) override;

    void _DoAlert(LegacyLong op);
    void _SetColorTable(ChildChunkID chid);
    void _DoEditControl(LegacyLong hid, LegacyLong stid, bool fGet);
    PDynamicArray _PglclrGet(ChunkNumber cno);
    bool _FLaunch(LegacyLong stid);

  public:
    GraphicsObjectInterpreter(PWorldOfKidspace pwoks, PResourceCache prca, PGraphicsObject pgob);

    void GobMayDie(void)
    {
        _pgob = pvNil;
    }
    bool FResume(LegacyLong *plwReturn = pvNil, bool *pfPaused = pvNil) override;
};

// a Chunky resource reader for a color table
bool FReadColorTable(PChunkyResourceFile pcrf, ChunkTagOrType ctg, ChunkNumber cno, PDataBlock pblck, PBaseCacheableObject *ppbaco, LegacyLong *pcb);

} // end of namespace ScriptInterpreter

#endif //! SCREXEG_H
