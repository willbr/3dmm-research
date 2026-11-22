/* Copyright (c) Microsoft Corporation.
   Licensed under the MIT License. */

/***************************************************************************
    Author: ShonK
    Project: Kauai
    Reviewed:
    Copyright (c) Microsoft Corporation

    Script compiler.  This supports a post-fix (RPN-style) stack base
    "assembly" language and an in-fix (standard) higher level language similar
    to "C" in its operator set and syntax.  The only data type supported
    is 32 bit signed integers.  The CompilerBase class can also "disassemble"
    a script.  Compilation is case sensitive.  The in-fix compiler supports
    all valid "C" expressions not involving arrays, pointers or structures,
    including the ternary operator (?:).  Supported control structures
    include While and If blocks, with the following syntax:

        While (exp);
            // statements
        End;

        If (exp);
            // statements
        Elif (exp);
            // statements
        Else;
            // statements
        End;

    The semicolons after the keywords are required (they make compilation
    easier); the Elif and Else clauses are (of course) optional.  A "Break;"
    primitive is also supported to exit a While loop early.  The in-fix
    compiler also supports "IfGoto(exp, label$);" and "Goto(label$);".  These
    are compiled directly to single op-codes (plus operands) so are treated
    differently than the control structures.  See the opcode signatures in script_dialect.c
    for other function-like primitives supported by the in-fix compiler.

    Label references appear in the in-fix source as a name followed by a $ sign,
    eg, "Goto(label$);".  Label definitions are a name followed by an @ sign.

    In the post-fix source, label references are $label and label definitions
    are @label.

    Local variables as well as sub-class defined "this" variables and remote
    variables are supported.  Variables are not declared, are case sensitive
    and have 8 significant characters.  During execution, if a variable is used
    before being assigned to, its value is 0 and a warning is generated.

    For in-fix compilation, "this" variables are preceeded by a period and
    remote variables are preceeded by "exp->" where exp is a valid numeric
    expression.  The -> operator has very high precedence, so it's common
    to have () around the expression.

    For post-fix compilation, this variables are again prefixed by a period
    and remote variables are preceeded by a ~.  Remote variable accesses use
    the top stack value for their "address".  Use the < operator to push a
    variable and > to pop the top stack value into a variable.  Constants
    in the source are automatically pushed.  Here's a sample post-fix script
    that computes the 10th Fibonacci number and stores it in a remote variable.
    The in-fix equivalent is given in the comments.  Of course, an in-fix
    version of this could use a While loop.

        1 Dup >a >b 10 >n			// a = b = 1; n = 10;
        $LTest Go					// Goto (LTest$);
    @LLoop							// LLoop@
        <a Dup <b Add >a >b			// temp = a; a += b; b = temp;
    @LTest							// LTest@
        $LLoop <n Dec Dup >n GoNz	// IfGoto (--n, LLoop$);
        <a 100 >~answer				// 100->answer = a;


    A compiled script consists of a DynamicArray of longs.  Each long is one of 3 types:

        1) an opcode that acts on a variable:
            byte : opcode
            byte : number of immediate longs that follow (at least 1)
            short: 2 bytes of rtvn data, the other 4 bytes are in the next long
        2) an opcode that doesn't involve a variable:
            byte : 0
            byte : number of immediate longs that follow
            short: opcode
        3) immediate data

    After executing an opcode that doesn't jump, the immediate data is placed on
    the execution stack en-masse.  In the case of a variable opcode, the first
    long is used as part of the rtvn, so isn't placed on the stack.

    Label references are considered immediate data and consist of
    MM_SCRIPT_LABEL_BYTE in the high byte and the destination index (into the gl) in
    the low 3 bytes.

    The first long in the DynamicArray is version number information (a dver).

***************************************************************************/
#include "util.h"
#include "script_resource_host.h"
ASSERTNAME

namespace ScriptCompiler {

RTCLASS(CompilerBase)

// common error messages
PZString _pszOom = PszLit("Out of memory");
PZString _pszSyntax = PszLit("Syntax error");

/***************************************************************************
    Constructor for a script compiler.
***************************************************************************/
CompilerBase::CompilerBase(void)
{
    AssertBaseThis(0);
    _plexb = pvNil;
    _emitter = nullptr;
    _pmsnk = pvNil;
    AssertThis(0);
}

/***************************************************************************
    Destructor for a script compiler.
***************************************************************************/
CompilerBase::~CompilerBase(void)
{
    AssertThis(0);
    _Free();
}

#ifdef DEBUG
/***************************************************************************
    Assert the validity of a CompilerBase.
***************************************************************************/
void CompilerBase::AssertValid(LegacyUlong grf)
{
    CompilerBase_PAR::AssertValid(0);
    AssertNilOrPo(_plexb, 0);
    AssertNilOrPo(_pmsnk, 0);
}

/***************************************************************************
    Mark memory for the CompilerBase.
***************************************************************************/
void CompilerBase::MarkMem(void)
{
    AssertValid(0);

    CompilerBase_PAR::MarkMem();
    MarkMemObj(_plexb);
    mm_script_emitter_mark_memory(_emitter);
    MarkMemObj(_pmsnk);
}
#endif // DEBUG

/***************************************************************************
    Initializes the script compiler to compile the stream from the given
    lexer object.  pmsnk is a message sink for error reporting.
***************************************************************************/
bool CompilerBase::_FInit(PLexerBase plexb, PMSNK pmsnk)
{
    AssertThis(0);
    AssertPo(plexb, 0);
    AssertPo(pmsnk, 0);

    _Free();
    _fHitEnd = fFalse;
    _plexb = plexb;
    _plexb->AddRef();
    _pmsnk = pmsnk;
    _pmsnk->AddRef();

    _emitter = mm_script_emitter_create(&ScriptHost(), _SwCur(), _SwBack(),
        [](void *context, int reason) {
            static const PZString messages[] = {
                _pszOom, _pszSyntax, PszLit("Unknown name"), PszLit("Variable expected"),
                PszLit("duplicate label"), PszLit("Undefined label"), PszLit("Script label out of range"),
                PszLit("divide by zero"), PszLit("division overflow"), PszLit("unknown function"),
                PszLit("Wrong number of parameters"), PszLit("Using void return value"),
                PszLit("Using keyword as variable"), PszLit("unexpected Elif"), PszLit("unexpected Else"),
                PszLit("unexpected End"), PszLit("unexpected Continue"), PszLit("unexpected Break"),
                PszLit("unexpected end of source")
            };
            static_assert(sizeof(messages) / sizeof(*messages) == MM_SCRIPT_COMPILE_UNEXPECTED_EOF + 1,
                          "script compiler diagnostics");
            static_cast<PCompilerBase>(context)->_ReportError(messages[reason]);
        }, this);
    if (!_emitter)
        _ReportError(_pszOom);

    AssertThis(0);
    return fTrue;
}

/***************************************************************************
    Free all memory hanging off this CompilerBase.
***************************************************************************/
void CompilerBase::_Free(void)
{
    AssertThis(0);
    mm_script_emitter_destroy(_emitter);
    _emitter = nullptr;
    ReleasePpo(&_plexb);
    ReleasePpo(&_pmsnk);
}

/***************************************************************************
    Compile a script given a lexer object.  Compilation is terminated when
    the lexer's stream is exhausted or token ttEnd is encountered.  ttEnd
    allows scripts to be embedded in source for other tools (such as
    chomp.exe).
***************************************************************************/
PScript CompilerBase::PscptCompileLex(PLexerBase plexb, bool fInFix, PMSNK pmsnk, LegacyLong ttEnd)
{
    AssertThis(0);
    AssertPo(plexb, 0);
    AssertPo(pmsnk, 0);
    PScript pscpt;

    if (!_FInit(plexb, pmsnk))
        return pvNil;
    _ttEnd = ttEnd;

    _Compile(fInFix);

    pscpt = mm_script_emitter_finish(_emitter);
    _Free();
    return pscpt;
}

/***************************************************************************
    Compile the given text file and return the executable script.
    Uses the in-fix or post-fix compiler according to fInFix.
***************************************************************************/
PScript CompilerBase::PscptCompileFil(PFileObject pfil, bool fInFix, PMSNK pmsnk)
{
    AssertThis(0);
    AssertPo(pfil, 0);
    AssertPo(pmsnk, 0);
    PScript pscpt;
    PLexerBase plexb;

    if (pvNil == (plexb = NewObj LexerBase(pfil)))
        return pvNil;
    pscpt = PscptCompileLex(plexb, fInFix, pmsnk);
    ReleasePpo(&plexb);
    return pscpt;
}

/***************************************************************************
    Compile a script from the given text file name.
***************************************************************************/
PScript CompilerBase::PscptCompileFni(Filename *pfni, bool fInFix, PMSNK pmsnk)
{
    AssertPo(pfni, ffniFile);
    AssertPo(pmsnk, 0);
    PFileObject pfil;
    PScript pscpt;

    if (pvNil == (pfil = FileObject::PfilOpen(pfni)))
        return pvNil;
    pscpt = PscptCompileFil(pfil, fInFix, pmsnk);
    ReleasePpo(&pfil);
    return pscpt;
}

/***************************************************************************
    Get the next token.  Returns false if the token is a _ttEnd.
***************************************************************************/
bool CompilerBase::_FGetTok(PToken ptok)
{
    AssertBaseThis(0);
    AssertPo(_plexb, 0);
    if (_fHitEnd || !_plexb->FGetTok(ptok) || ptok->tt == _ttEnd)
    {
        _fHitEnd = fTrue;
        return fFalse;
    }
    return fTrue;
}

/***************************************************************************
    Return the current version number of the script compiler.  This is
    a virtual method so subclasses of CompilerBase can provide their own
    version numbers.
***************************************************************************/
short CompilerBase::_SwCur(void)
{
    AssertBaseThis(0);
    return kswCurSccb;
}

/***************************************************************************
    Return the back version number of the script compiler.  Versions
    back to here can read this script.
***************************************************************************/
short CompilerBase::_SwBack(void)
{
    AssertBaseThis(0);
    return kswBackSccb;
}

/***************************************************************************
    Return the min version number of the script compiler.  We can read
    scripts back to this version.
***************************************************************************/
short CompilerBase::_SwMin(void)
{
    AssertBaseThis(0);
    return kswMinSccb;
}

/***************************************************************************
    An error occured.  Report it to the message sink.
***************************************************************************/
void CompilerBase::_ReportError(PZString psz)
{
    AssertThis(0);
    AssertPo(_plexb, 0);
    AssertPo(_pmsnk, 0);
    String stn;
    String stnFile;

    mm_script_emitter_abort(_emitter);
    _plexb->GetStnFile(&stnFile);
    stn.FFormatSz(PszLit("%s(%d:%d) : error : %z"), &stnFile, _plexb->LwLine(), _plexb->IchLine() + 1, psz);
    _pmsnk->ReportLine(stn.Psz());
}

int CompilerBase::_Dialect(void)
{
    return MM_SCRIPT_BASE;
}

void CompilerBase::_Compile(bool infix)
{
    struct Source
    {
        PCompilerBase compiler;
        Token token;
    } source{this, {}};
    const MMScriptInput input = {
        &source,
        [](void *context, MMScriptToken *out) -> int {
            auto source = static_cast<Source *>(context);
            auto &token = source->token;
            if (!source->compiler->_FGetTok(&token))
                return 0;
            out->kind = token.tt;
            out->value = token.tt == ttLong ? token.lw : 0;
            out->text = token.stn.Prgch();
            out->length = token.stn.Cch();
            return 1;
        },
        _Dialect()
    };
    if (infix)
        mm_script_compile_infix(_emitter, &input);
    else
        mm_script_compile_postfix(_emitter, &input);
}

/***************************************************************************
    Write a source listing into the message sink.
***************************************************************************/
bool CompilerBase::FDisassemble(PScript pscpt, PMSNK pmsnk, PMSNK pmsnkError)
{
    AssertThis(0);
    AssertPo(pmsnk, 0);
    AssertNilOrPo(pmsnkError, 0);
    const int result = mm_script_disassemble(pscpt, _Dialect(), _SwCur(), _SwMin(),
        [](void *context, const void *text, MMU32 length, int endLine) {
            auto sink = static_cast<PMSNK>(context);
            String fragment;
            if (length)
                fragment.SetRgch(const_cast<achar *>(static_cast<const achar *>(text)), length);
            if (endLine)
                sink->ReportLine(fragment.Psz());
            else
                sink->Report(fragment.Psz());
        }, pmsnk);
    if (result == MM_SCRIPT_DISASSEMBLED)
        return true;
    if (pmsnkError)
    {
        static const PZString messages[] = {
            PszLit("No version numbers on script"),
            PszLit("Script version doesn't match script compiler version"),
            PszLit("bad instruction"), PszLit("bad var instruction"), PszLit("bad variable"),
            PszLit("bad internal variable"), PszLit("bad var op"), PszLit("bad op in script")
        };
        static_assert(sizeof(messages) / sizeof(*messages) == MM_SCRIPT_DISASSEMBLE_OPCODE,
                      "script listing diagnostics");
        pmsnkError->ReportLine(messages[result - 1]);
    }
    return false;
}

/***************************************************************************
    Set the values of the RuntimeVariableName from the given stn.  Only the first 8
    characters of the stn are significant.  The high word of lu1 is
    guaranteed to be zero.
***************************************************************************/
void RuntimeVariableName::SetFromStn(PString pstn)
{
    AssertThisMem();
    AssertPo(pstn, 0);
    MMU32 high, low;
    AssertDo(mm_script_variable_name(pstn->Prgch(), pstn->Cch(), sizeof(achar), &high, &low), "bad identifier");
    lu1 = high;
    lu2 = low;
}

/***************************************************************************
    Get the variable name that an rtvn stores.
***************************************************************************/
void RuntimeVariableName::GetStn(PString pstn)
{
    AssertThisMem();
    AssertPo(pstn, 0);
    char ascii[MM_SCRIPT_VARIABLE_TEXT_CAPACITY];
    achar text[MM_SCRIPT_VARIABLE_TEXT_CAPACITY];
    const auto length = mm_script_variable_text(lu1, lu2, ascii);
    for (MMU32 i = 0; i < length; ++i)
        text[i] = ascii[i];
    pstn->SetRgch(text, length);
}

} // end of namespace ScriptCompiler
