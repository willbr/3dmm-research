/* Copyright (c) Microsoft Corporation.
   Licensed under the MIT License. */

/* Copyright (c) Microsoft Corporation.
   Licensed under the MIT License. */

/***************************************************************************
    Author: ShonK
    Project: Kauai
    Reviewed:
    Copyright (c) Microsoft Corporation

    Script compiler declarations.

***************************************************************************/
#ifndef SCRCOM_H
#define SCRCOM_H
#include "mm/script_opcodes.h"

namespace ScriptCompiler {

using namespace ScriptInterpreter;
using namespace Group;

// structure to map a string to an opcode (post-fix)
struct StringOpcodeMap
{
    LegacyLong op;
    PZString psz;
};

// structure to map a string to an opcode and argument information (in-fix)
struct StringOpcodeArgumentMap
{
    LegacyLong op;
    PZString psz;
    LegacyLong clwFixed;   // number of fixed arguments
    LegacyLong clwVar;     // number of arguments per variable group
    LegacyLong cactMinVar; // minimum number of variable groups
    bool fVoid;      // return a value?
};

// script version numbers
// if you bump these, also bump the numbers in scrcomg.h
const short kswCurSccb = 0xA;  // this version
const short kswBackSccb = 0xA; // we can be read back to this version
const short kswMinSccb = 0xA;  // we can read back to this version

// high byte of a label value
const byte kbLabel = MM_SCRIPT_LABEL_BYTE;

/***************************************************************************
    Run-time variable name.  The first 8 characters of the name are
    significant.  These 8 characters are packed into lu2 and the low
    2 bytes of lu1, so clients can store the info in 6 bytes. The high
    2 bytes of lu1 are used for array subscripts.
***************************************************************************/
struct RuntimeVariableName
{
    LegacyUlong lu1;
    LegacyUlong lu2;

    void SetFromStn(PString pstn);
    void GetStn(PString pstn);
};

/***************************************************************************
    The script compiler base class.
***************************************************************************/
typedef class CompilerBase *PCompilerBase;
#define CompilerBase_PAR BASE
#define kclsCompilerBase 'SCCB'
class CompilerBase : public CompilerBase_PAR
{
    RTCLASS_DEC
    ASSERT
    MARKMEM

  protected:
    enum
    {
        fsccNil = 0,
        fsccWantVoid = 1,
        fsccTop = 2
    };

    PLexerBase _plexb;       // the lexer
    PScript _pscpt;       // the script we're building
    PDynamicArray _pgletnTree;    // expression tree (in-fix only)
    PDynamicArray _pgletnStack;   // token stack for building expression tree (in-fix only)
    PDynamicArray _pglcstd;       // control structure stack (in-fix only)
    PStringTable_GST _pgstNames;    // encountered names (in-fix only)
    PStringTable_GST _pgstLabel;    // encountered labels, sorted, extra long is label value
    PStringTable_GST _pgstReq;      // label references, extra long is address of reference
    LegacyLong _ilwOpLast;    // address of the last opcode
    LegacyLong _lwLastLabel;  // for internal temporary labels
    bool _fError : 1;   // whether an error has occured during compiling
    bool _fForceOp : 1; // when pushing a constant, make sure the last long
                        // is an opcode (because a label references this loc)
    bool _fHitEnd : 1;  // we've exhausted our input stream
    LegacyLong _ttEnd;        // stop compiling when we see this
    PMSNK _pmsnk;       // the message sink - for error reporting when compiling

    bool _FInit(PLexerBase plexb, bool fInFix, PMSNK pmsnk);
    void _Free(void);

    // general compilation methods
    void _PushLw(LegacyLong lw);
    void _PushString(PString pstn);
    void _PushOp(LegacyLong op);
    void _EndOp(void);
    void _PushVarOp(LegacyLong op, RuntimeVariableName *prtvn);
    bool _FFindLabel(PString pstn, LegacyLong *plwLoc);
    void _AddLabel(PString pstn);
    void _PushLabelRequest(PString pstn);
    void _AddLabelLw(LegacyLong lw);
    void _PushLabelRequestLw(LegacyLong lw);

    virtual void _ReportError(PZString psz);
    virtual short _SwCur(void);
    virtual short _SwBack(void);
    virtual short _SwMin(void);

    virtual bool _FGetTok(PToken ptok);

    // post-fix compiler routines
    virtual void _CompilePost(void);
    LegacyLong _OpFromStnRgszop(PString pstn, StringOpcodeMap *prgszop);
    virtual LegacyLong _OpFromStn(PString pstn);
    bool _FGetStnFromOpRgszop(LegacyLong op, PString pstn, StringOpcodeMap *prgszop);
    virtual bool _FGetStnFromOp(LegacyLong op, PString pstn);

    // in-fix compiler routines
    virtual void _CompileIn(void);
    bool _FResolveToOpl(LegacyLong opl, LegacyLong oplMin, LegacyLong *pietn);
    void _EmitCode(LegacyLong ietnTop, LegacyUlong grfscc, LegacyLong *pclwArg);
    void _EmitVarAccess(LegacyLong ietn, RuntimeVariableName *prtvn, LegacyLong *popPush, LegacyLong *popPop, LegacyLong *pclwStack);
    virtual bool _FGetOpFromName(PString pstn, LegacyLong *pop, LegacyLong *pclwFixed, LegacyLong *pclwVar, LegacyLong *pcactMinVar, bool *pfVoid);
    bool _FGetArop(PString pstn, StringOpcodeArgumentMap *prgarop, LegacyLong *pop, LegacyLong *pclwFixed, LegacyLong *pclwVar, LegacyLong *pcactMinVar,
                   bool *pfVoid);
    void _PushLabelRequestIetn(LegacyLong ietn);
    void _AddLabelIetn(LegacyLong ietn);
    void _PushOpFromName(LegacyLong ietn, LegacyUlong grfscc, LegacyLong clwArg);
    void _GetIstnNameFromIetn(LegacyLong ietn, LegacyLong *pistn);
    void _GetRtvnFromName(LegacyLong istn, RuntimeVariableName *prtvn);
    bool _FKeyWord(PString pstn);
    void _GetStnFromIstn(LegacyLong istn, PString pstn);
    void _AddNameRef(PString pstn, LegacyLong *pistn);
    LegacyLong _CstFromName(LegacyLong ietn);
    void _BeginCst(LegacyLong cst, LegacyLong ietn);
    bool _FHandleCst(LegacyLong ietn);
    bool _FAddToTree(struct ETN *petn, LegacyLong *pietn);
    bool _FConstEtn(LegacyLong ietn, LegacyLong *plw);
    bool _FCombineConstValues(LegacyLong op, LegacyLong lw1, LegacyLong lw2, LegacyLong *plw);
    void _SetDepth(struct ETN *petn, bool fCommute = fFalse);
    void _PushStringIstn(LegacyLong istn);

  public:
    CompilerBase(void);
    ~CompilerBase(void);

    virtual PScript PscptCompileLex(PLexerBase plexb, bool fInFix, PMSNK pmsnk, LegacyLong ttEnd = ttNil);
    virtual PScript PscptCompileFil(PFileObject pfil, bool fInFix, PMSNK pmsnk);
    virtual PScript PscptCompileFni(Filename *pfni, bool fInFix, PMSNK pmsnk);
    virtual bool FDisassemble(PScript pscpt, PMSNK pmsnk, PMSNK pmsnkError = pvNil);
};

} // end of namespace ScriptCompiler

#endif //! SCRCOM_H
