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
#include "mm/script_compile.h"
#include "mm/script_opcodes.h"

namespace ScriptCompiler {

using namespace ScriptInterpreter;
using namespace Group;

// script version numbers
// if you bump these, also bump the numbers in scrcomg.h
const short kswCurSccb = 0xA;  // this version
const short kswBackSccb = 0xA; // we can be read back to this version
const short kswMinSccb = 0xA;  // we can read back to this version

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
    PLexerBase _plexb;       // the lexer
    MMScriptEmitter *_emitter;
    bool _fHitEnd : 1;  // we've exhausted our input stream
    LegacyLong _ttEnd;        // stop compiling when we see this
    PMSNK _pmsnk;       // the message sink - for error reporting when compiling

    bool _FInit(PLexerBase plexb, PMSNK pmsnk);
    void _Free(void);
    void _Compile(bool infix);

    virtual void _ReportError(PZString psz);
    virtual short _SwCur(void);
    virtual short _SwBack(void);
    virtual short _SwMin(void);

    virtual bool _FGetTok(PToken ptok);

    virtual int _Dialect(void);

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
