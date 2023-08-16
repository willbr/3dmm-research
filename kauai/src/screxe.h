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
    long lwValue;
};

bool FFindRtvm(PDynamicArray pglrtvm, RuntimeVariableName *prtvn, long *plwValue, long *pirtvm);
bool FAssignRtvm(PDynamicArray *ppglrtvm, RuntimeVariableName *prtvn, long lw);

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
    PStringTable _pgstLiterals;

    Script(void)
    {
    }

    friend class Interpreter;
    friend class CompilerBase;

  public:
    static bool FReadScript(PChunkyResourceFile pcrf, ChunkTag ctg, ChunkNumber cno, PDataBlock pblck, PBaseCacheableObject *ppbaco, long *pcb);
    static PScript PscptRead(PChunkyFile pcfl, ChunkTag ctg, ChunkNumber cno);
    ~Script(void);

    bool FSaveToChunk(PChunkyFile pcfl, ChunkTag ctg, ChunkNumber cno, bool fPack = fFalse);
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
    long _stidLast;
    PStringTable _pgst;

    bool _FFind(long stid, long *pistn);
    bool _FEnsureGst(void);

  public:
    StringRegistry(void);
    ~StringRegistry(void);

    bool FPut(long stid, PSTN pstn);
    bool FGet(long stid, PSTN pstn);
    bool FAdd(long *pstid, PSTN pstn);
    bool FMove(long stidSrc, long stidDst);
    void Delete(long stid);
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
    PRCA _prca; // the chunky resource file list (may be nil)
    PStringRegistry _pstrg;
    PDynamicArray _pgllwStack;   // the execution stack
    PDynamicArray _pglrtvm;      // the local variables
    PScript _pscpt;      // the script
    long _ilwMac;      // the length of the script
    long _ilwCur;      // the current location in the script
    bool _fError : 1;  // an error has occured
    bool _fPaused : 1; // if we're paused
    long _lwReturn;    // the return value from the script

    void _Push(long lw)
    {
        if (!_fError && !_pgllwStack->FPush(&lw))
            _Error(fFalse);
    }
    long _LwPop(void);
    long *_QlwGet(long clw);
    void _Error(bool fAssert);

    void _Rotate(long clwTot, long clwShift);
    void _Reverse(long clw);
    void _DupList(long clw);
    void _PopList(long clw);
    void _Select(long clw, long ilw);
    void _RndList(long clw);
    void _Match(long clw);
    void _CopySubStr(long stidSrc, long ichMin, long cch, long stidDst);
    void _MergeStrings(ChunkNumber cno, RSC rsc);
    void _NumToStr(long lw, long stid);
    void _StrToNum(long stid, long lwEmpty, long lwError);
    void _ConcatStrs(long stidSrc1, long stidSrc2, long stidDst);
    void _LenStr(long stid);

    virtual void _AddParameters(long *prglw, long clw);
    virtual void _AddStrings(PStringTable pgst);
    virtual bool _FExecVarOp(long op, RuntimeVariableName *prtvn);
    virtual bool _FExecOp(long op);
    virtual void _PushVar(PDynamicArray pglrtvm, RuntimeVariableName *prtvn);
    virtual void _AssignVar(PDynamicArray *ppglrtvm, RuntimeVariableName *prtvn, long lw);
    virtual PDynamicArray _PglrtvmThis(void);
    virtual PDynamicArray *_PpglrtvmThis(void);
    virtual PDynamicArray _PglrtvmGlobal(void);
    virtual PDynamicArray *_PpglrtvmGlobal(void);
    virtual PDynamicArray _PglrtvmRemote(long lw);
    virtual PDynamicArray *_PpglrtvmRemote(long lw);

    virtual short _SwCur(void);
    virtual short _SwMin(void);

#ifdef DEBUG
    void _WarnSz(PSZ psz, ...);
#endif // DEBUG

  public:
    Interpreter(PRCA prca = pvNil, PStringRegistry pstrg = pvNil);
    ~Interpreter(void);

    virtual bool FRunScript(PScript pscpt, long *prglw = pvNil, long clw = 0, long *plwReturn = pvNil,
                            bool *pfPaused = pvNil);
    virtual bool FResume(long *plwReturn = pvNil, bool *pfPaused = pvNil);
    virtual bool FAttachScript(PScript pscpt, long *prglw = pvNil, long clw = 0);
    virtual void Free(void);
};

} // end of namespace ScriptInterpreter

#endif //! SCREXE_H
