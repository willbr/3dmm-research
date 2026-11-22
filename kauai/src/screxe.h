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

#include "mm/script_resource.h"
#include "mm/script_vm.h"

namespace ScriptInterpreter {

using namespace Chunky;
using namespace ScriptCompiler;
using namespace Group;

PScript CreateScript();
PScript FetchScript(const MMResourceAccess &, ChunkTagOrType, ChunkNumber, int *error = nullptr);
PScript ReadScript(PChunkyFile, ChunkTagOrType, ChunkNumber);
bool ReadScriptLiteral(PScript, MMU32 index, PString);
bool SaveScript(PScript, PChunkyFile, ChunkTagOrType, ChunkNumber, bool pack = false);
inline void ReleaseScript(PScript *script)
{
    mm_script_release(*script);
    *script = nullptr;
}
#ifdef DEBUG
inline void MarkScript(PScript script)
{
    if (script)
        mm_script_object_services()->mark_memory(script);
}
#endif

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
    MMScriptVM _vm;

    void _Push(LegacyLong lw);
    LegacyLong _LwPop(void);
    const void *_StackWords(LegacyLong clw);
    void _Error(bool fAssert);
    void _ReportScriptError(int reason);

    void _PopList(LegacyLong clw);
    void _CopySubStr(LegacyLong stidSrc, LegacyLong ichMin, LegacyLong cch, LegacyLong stidDst);
    void _MergeStrings(ChunkNumber cno, RSC rsc);
    void _NumToStr(LegacyLong lw, LegacyLong stid);
    void _StrToNum(LegacyLong stid, LegacyLong lwEmpty, LegacyLong lwError);
    void _ConcatStrs(LegacyLong stidSrc1, LegacyLong stidSrc2, LegacyLong stidDst);
    void _LenStr(LegacyLong stid);

    virtual void _AddParameters(LegacyLong *prglw, LegacyLong clw);
    void _AddStrings();
    virtual bool _FExecHostOp(LegacyLong op);
    void _SetLocal(const RuntimeVariableName &name, LegacyLong lw);
    virtual MMScriptVariables *_ThisVariables(void);
    virtual MMScriptVariables *_GlobalVariables(void);
    virtual MMScriptVariables *_RemoteVariables(LegacyLong lw);

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
