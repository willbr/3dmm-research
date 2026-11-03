/* Copyright (c) Microsoft Corporation.
   Licensed under the MIT License. */

/* Copyright (c) Microsoft Corporation.
   Licensed under the MIT License. */

/***************************************************************************
    Author: ShonK
    Project: Kauai
    Reviewed:
    Copyright (c) Microsoft Corporation

    Script interpreter.  See scrcom.h and scrcom.cpp for the script
    compiler and an explanation of what a compiled script consists of.

***************************************************************************/
#ifndef SCREXE_H
#define SCREXE_H

namespace ScriptInterpreter {

using namespace Chunky;
using namespace ScriptCompiler;
using namespace Group;

/****************************************
    Run-Time Variable Map structure
****************************************/
struct RunTimeVariableMap
{
    RuntimeVariableName rtvn;
    LegacyLong lwValue;
};

bool FFindRtvm(PDynamicArray pglrtvm, RuntimeVariableName *prtvn, LegacyLong *plwValue, LegacyLong *pirtvm);
bool FAssignRtvm(PDynamicArray *ppglrtvm, RuntimeVariableName *prtvn, LegacyLong lw);

/***************************************************************************
    A script.  This is here rather than in scrcom.* because scrcom is
    rarely included in shipping products, but screxe.* is.
***************************************************************************/
typedef class Script *PScript;
#define Script_PAR BaseCacheableObject
#define kclsScript 'SCPT'
class Script : public Script_PAR
{
    RTCLASS_DEC
    MARKMEM
    ASSERT

  protected:
    PDynamicArray _pgllw;
    PStringTable_GST _pgstLiterals;

    Script(void)
    {
    }

    friend class Interpreter;
    friend class ScriptCompiler::CompilerBase;

  public:
    static bool FReadScript(PChunkyResourceFile pcrf, ChunkTagOrType ctg, ChunkNumber cno, PDataBlock pblck, PBaseCacheableObject *ppbaco, LegacyLong *pcb);
    static PScript PscptRead(PChunkyFile pcfl, ChunkTagOrType ctg, ChunkNumber cno);
    ~Script(void);

    bool FSaveToChunk(PChunkyFile pcfl, ChunkTagOrType ctg, ChunkNumber cno, bool fPack = fFalse);
};

/***************************************************************************
    Runtime string registry.
***************************************************************************/
typedef class StringRegistry *PStringRegistry;
#define StringRegistry_PAR BASE
#define kclsStringRegistry 'STRG'
class StringRegistry : public StringRegistry_PAR
{
    RTCLASS_DEC
    ASSERT
    MARKMEM
    NOCOPY(StringRegistry)

  protected:
    LegacyLong _stidLast;
    PStringTable_GST _pgst;

    bool _FFind(LegacyLong stid, LegacyLong *pistn);
    bool _FEnsureGst(void);

  public:
    StringRegistry(void);
    ~StringRegistry(void);

    bool FPut(LegacyLong stid, PString pstn);
    bool FGet(LegacyLong stid, PString pstn);
    bool FAdd(LegacyLong *pstid, PString pstn);
    bool FMove(LegacyLong stidSrc, LegacyLong stidDst);
    void Delete(LegacyLong stid);
};

/***************************************************************************
    The script interpreter.
***************************************************************************/
enum
{
    fscebNil = 0,
    fscebRunnable = 1,
};

typedef class Interpreter *PInterpreter;
#define Interpreter_PAR BASE
#define kclsInterpreter 'SCEB'
class Interpreter : public Interpreter_PAR
{
    RTCLASS_DEC
    ASSERT
    MARKMEM

  protected:
    PResourceCache _prca; // the chunky resource file list (may be nil)
    PStringRegistry _pstrg;
    PDynamicArray _pgllwStack;   // the execution stack
    PDynamicArray _pglrtvm;      // the local variables
    PScript _pscpt;      // the script
    LegacyLong _ilwMac;      // the length of the script
    LegacyLong _ilwCur;      // the current location in the script
    bool _fError : 1;  // an error has occured
    bool _fPaused : 1; // if we're paused
    LegacyLong _lwReturn;    // the return value from the script

    void _Push(LegacyLong lw)
    {
        if (!_fError && !_pgllwStack->FPush(&lw))
            _Error(fFalse);
    }
    LegacyLong _LwPop(void);
    LegacyLong *_QlwGet(LegacyLong clw);
    void _Error(bool fAssert);

    void _Rotate(LegacyLong clwTot, LegacyLong clwShift);
    void _Reverse(LegacyLong clw);
    void _DupList(LegacyLong clw);
    void _PopList(LegacyLong clw);
    void _Select(LegacyLong clw, LegacyLong ilw);
    void _RndList(LegacyLong clw);
    void _Match(LegacyLong clw);
    void _CopySubStr(LegacyLong stidSrc, LegacyLong ichMin, LegacyLong cch, LegacyLong stidDst);
    void _MergeStrings(ChunkNumber cno, RSC rsc);
    void _NumToStr(LegacyLong lw, LegacyLong stid);
    void _StrToNum(LegacyLong stid, LegacyLong lwEmpty, LegacyLong lwError);
    void _ConcatStrs(LegacyLong stidSrc1, LegacyLong stidSrc2, LegacyLong stidDst);
    void _LenStr(LegacyLong stid);

    virtual void _AddParameters(LegacyLong *prglw, LegacyLong clw);
    virtual void _AddStrings(PStringTable_GST pgst);
    virtual bool _FExecVarOp(LegacyLong op, RuntimeVariableName *prtvn);
    virtual bool _FExecOp(LegacyLong op);
    virtual void _PushVar(PDynamicArray pglrtvm, RuntimeVariableName *prtvn);
    virtual void _AssignVar(PDynamicArray *ppglrtvm, RuntimeVariableName *prtvn, LegacyLong lw);
    virtual PDynamicArray _PglrtvmThis(void);
    virtual PDynamicArray *_PpglrtvmThis(void);
    virtual PDynamicArray _PglrtvmGlobal(void);
    virtual PDynamicArray *_PpglrtvmGlobal(void);
    virtual PDynamicArray _PglrtvmRemote(LegacyLong lw);
    virtual PDynamicArray *_PpglrtvmRemote(LegacyLong lw);

    virtual short _SwCur(void);
    virtual short _SwMin(void);

#ifdef DEBUG
    void _WarnSz(PZString psz, ...);
#endif // DEBUG

  public:
    Interpreter(PResourceCache prca = pvNil, PStringRegistry pstrg = pvNil);
    ~Interpreter(void);

    virtual bool FRunScript(PScript pscpt, LegacyLong *prglw = pvNil, LegacyLong clw = 0, LegacyLong *plwReturn = pvNil,
                            bool *pfPaused = pvNil);
    virtual bool FResume(LegacyLong *plwReturn = pvNil, bool *pfPaused = pvNil);
    virtual bool FAttachScript(PScript pscpt, LegacyLong *prglw = pvNil, LegacyLong clw = 0);
    virtual void Free(void);
};

} // end of namespace ScriptInterpreter

#endif //! SCREXE_H
