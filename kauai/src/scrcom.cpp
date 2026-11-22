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
    differently than the control structures.  See the _rgarop array below
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
    kbLabel in the high byte and the destination index (into the gl) in
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

// name to op lookup table for post-fix compilation
StringOpcodeMap _rgszop[] = {
    {kopAdd, PszLit("Add")},
    {kopSub, PszLit("Sub")},
    {kopMul, PszLit("Mul")},
    {kopDiv, PszLit("Div")},
    {kopMod, PszLit("Mod")},
    {kopAbs, PszLit("Abs")},
    {kopNeg, PszLit("Neg")},
    {kopInc, PszLit("Inc")},
    {kopDec, PszLit("Dec")},
    {kopRnd, PszLit("Rnd")},
    {kopMulDiv, PszLit("MulDiv")},
    {kopBAnd, PszLit("BAnd")},
    {kopBOr, PszLit("BOr")},
    {kopBXor, PszLit("BXor")},
    {kopBNot, PszLit("BNot")},
    {kopLXor, PszLit("LXor")},
    {kopLNot, PszLit("LNot")},
    {kopEq, PszLit("Eq")},
    {kopNe, PszLit("Ne")},
    {kopGt, PszLit("Gt")},
    {kopLt, PszLit("Lt")},
    {kopGe, PszLit("Ge")},
    {kopLe, PszLit("Le")},
    {kopDup, PszLit("Dup")},
    {kopPop, PszLit("Pop")},
    {kopSwap, PszLit("Swap")},
    {kopRot, PszLit("Rot")},
    {kopRev, PszLit("Rev")},
    {kopDupList, PszLit("DupList")},
    {kopPopList, PszLit("PopList")},
    {kopRndList, PszLit("RndList")},
    {kopSelect, PszLit("Select")},
    {kopGoEq, PszLit("GoEq")},
    {kopGoNe, PszLit("GoNe")},
    {kopGoGt, PszLit("GoGt")},
    {kopGoLt, PszLit("GoLt")},
    {kopGoGe, PszLit("GoGe")},
    {kopGoLe, PszLit("GoLe")},
    {kopGoZ, PszLit("GoZ")},
    {kopGoNz, PszLit("GoNz")},
    {kopGo, PszLit("Go")},
    {kopExit, PszLit("Exit")},
    {kopReturn, PszLit("Return")},
    {kopSetReturn, PszLit("SetReturn")},
    {kopShuffle, PszLit("Shuffle")},
    {kopShuffleList, PszLit("ShuffleList")},
    {kopNextCard, PszLit("NextCard")},
    {kopMatch, PszLit("Match")},
    {kopPause, PszLit("Pause")},
    {kopCopyStr, PszLit("CopyStr")},
    {kopMoveStr, PszLit("MoveStr")},
    {kopNukeStr, PszLit("NukeStr")},
    {kopMergeStrs, PszLit("MergeStrs")},
    {kopScaleTime, PszLit("ScaleTime")},
    {kopNumToStr, PszLit("NumToStr")},
    {kopStrToNum, PszLit("StrToNum")},
    {kopConcatStrs, PszLit("ConcatStrs")},
    {kopLenStr, PszLit("LenStr")},
    {kopCopySubStr, PszLit("CopySubStr")},
    {opNil, pvNil},
};

// name to op look up table for in-fix compilation
StringOpcodeArgumentMap _rgarop[] = {
    {kopAbs, PszLit("Abs"), 1, 0, 0, fFalse},
    {kopRnd, PszLit("Rnd"), 1, 0, 0, fFalse},
    {kopMulDiv, PszLit("MulDiv"), 3, 0, 0, fFalse},
    {kopRndList, PszLit("RndList"), 0, 1, 1, fFalse},
    {kopSelect, PszLit("Select"), 1, 1, 1, fFalse},
    {kopGoNz, PszLit("IfGoto"), 2, 0, 0, fTrue},
    {kopGo, PszLit("Goto"), 1, 0, 0, fTrue},
    {kopExit, PszLit("Exit"), 0, 0, 0, fTrue},
    {kopReturn, PszLit("Return"), 1, 0, 0, fTrue},
    {kopSetReturn, PszLit("SetReturn"), 1, 0, 0, fTrue},
    {kopShuffle, PszLit("Shuffle"), 1, 0, 0, fTrue},
    {kopShuffleList, PszLit("ShuffleList"), 0, 1, 1, fTrue},
    {kopNextCard, PszLit("NextCard"), 0, 0, 0, fFalse},
    {kopMatch, PszLit("Match"), 2, 2, 1, fFalse},
    {kopPause, PszLit("Pause"), 0, 0, 0, fTrue},
    {kopCopyStr, PszLit("CopyStr"), 2, 0, 0, fFalse},
    {kopMoveStr, PszLit("MoveStr"), 2, 0, 0, fFalse},
    {kopNukeStr, PszLit("NukeStr"), 1, 0, 0, fTrue},
    {kopMergeStrs, PszLit("MergeStrs"), 2, 0, 0, fTrue},
    {kopScaleTime, PszLit("ScaleTime"), 1, 0, 0, fTrue},
    {kopNumToStr, PszLit("NumToStr"), 2, 0, 0, fFalse},
    {kopStrToNum, PszLit("StrToNum"), 3, 0, 0, fFalse},
    {kopConcatStrs, PszLit("ConcatStrs"), 3, 0, 0, fFalse},
    {kopLenStr, PszLit("LenStr"), 1, 0, 0, fFalse},
    {kopCopySubStr, PszLit("CopySubStr"), 4, 0, 0, fFalse},
    {opNil, pvNil, 0, 0, 0, fTrue},
};

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

/***************************************************************************
    Find the opcode that corresponds to the given stn.
***************************************************************************/
LegacyLong CompilerBase::_OpFromStn(PString pstn)
{
    AssertThis(0);
    AssertPo(pstn, 0);

    return _OpFromStnRgszop(pstn, _rgszop);
}

/***************************************************************************
    Check the pstn against the strings in the prgszop and return the
    corresponding op code.
***************************************************************************/
LegacyLong CompilerBase::_OpFromStnRgszop(PString pstn, StringOpcodeMap *prgszop)
{
    AssertThis(0);
    AssertPo(pstn, 0);
    AssertVarMem(prgszop);
    StringOpcodeMap *pszop;

    for (pszop = prgszop; pszop->psz != pvNil; pszop++)
    {
        if (pstn->FEqualSz(pszop->psz))
            return pszop->op;
    }
    return opNil;
}

/***************************************************************************
    Find the string corresponding to the given opcode.  This is used during
    disassembly.
***************************************************************************/
bool CompilerBase::_FGetStnFromOp(LegacyLong op, PString pstn)
{
    AssertThis(0);
    AssertPo(pstn, 0);

    return _FGetStnFromOpRgszop(op, pstn, _rgszop);
}

/***************************************************************************
    Check the op against the ops in the prgszop and return the corresponding
    string.
***************************************************************************/
bool CompilerBase::_FGetStnFromOpRgszop(LegacyLong op, PString pstn, StringOpcodeMap *prgszop)
{
    AssertThis(0);
    AssertPo(pstn, 0);
    AssertVarMem(prgszop);
    StringOpcodeMap *pszop;

    for (pszop = prgszop; pszop->psz != pvNil; pszop++)
    {
        if (op == pszop->op)
        {
            *pstn = pszop->psz;
            return fTrue;
        }
    }
    pstn->SetNil();
    return fFalse;
}

/***************************************************************************
    Compile source using the native lexer and opcode tables.
***************************************************************************/
void CompilerBase::_Compile(bool infix)
{
    struct Source
    {
        PCompilerBase compiler;
        Token token;
    } source{this, {}};
    auto next = [](void *context, MMScriptToken *out) -> int {
        auto source = static_cast<Source *>(context);
        auto &token = source->token;
        if (!source->compiler->_FGetTok(&token))
            return 0;
        out->kind = token.tt;
        out->value = token.tt == ttLong ? token.lw : 0;
        out->text = token.stn.Prgch();
        out->length = token.stn.Cch();
        return 1;
    };
    if (infix)
    {
        const MMScriptInfixInput input = {
            &source, next,
            [](void *context, const void *name, MMU32 length, MMScriptFunction *function) -> int {
                auto source = static_cast<Source *>(context);
                String text;
                if (length)
                    text.SetRgch(const_cast<achar *>(static_cast<const achar *>(name)), length);
                LegacyLong opcode, fixed, group, minimum;
                bool returnsVoid;
                if (!source->compiler->_FGetOpFromName(&text, &opcode, &fixed, &group, &minimum, &returnsVoid))
                    return 0;
                *function = {static_cast<MMU32>(opcode), fixed, group, minimum, returnsVoid};
                return 1;
            }
        };
        mm_script_compile_infix(_emitter, &input);
    }
    else
    {
        const MMScriptPostfixInput input = {
            &source, next,
            [](void *context, const void *name, MMU32 length) -> MMU32 {
                auto source = static_cast<Source *>(context);
                String text;
                if (length)
                    text.SetRgch(const_cast<achar *>(static_cast<const achar *>(name)), length);
                return source->compiler->_OpFromStn(&text);
            }
        };
        mm_script_compile_postfix(_emitter, &input);
    }
}

/***************************************************************************
    Find the string in the given rgarop and get the associated parameter
    and return type information.
***************************************************************************/
bool CompilerBase::_FGetArop(PString pstn, StringOpcodeArgumentMap *prgarop, LegacyLong *pop, LegacyLong *pclwFixed, LegacyLong *pclwVar, LegacyLong *pcactMinVar,
                     bool *pfVoid)
{
    AssertThis(0);
    AssertPo(pstn, 0);
    AssertVarMem(prgarop);
    AssertVarMem(pop);
    AssertVarMem(pclwFixed);
    AssertVarMem(pclwVar);
    AssertVarMem(pcactMinVar);
    AssertVarMem(pfVoid);
    StringOpcodeArgumentMap *parop;

    for (parop = prgarop; parop->psz != pvNil; parop++)
    {
        if (pstn->FEqualSz(parop->psz))
        {
            *pop = parop->op;
            *pclwFixed = parop->clwFixed;
            *pclwVar = parop->clwVar;
            *pcactMinVar = parop->cactMinVar;
            *pfVoid = parop->fVoid;
            return fTrue;
        }
    }
    return fFalse;
}

/***************************************************************************
    See if the given name is a function and give argument and return type
    information.
***************************************************************************/
bool CompilerBase::_FGetOpFromName(PString pstn, LegacyLong *pop, LegacyLong *pclwFixed, LegacyLong *pclwVar, LegacyLong *pcactMinVar, bool *pfVoid)
{
    AssertThis(0);
    return _FGetArop(pstn, _rgarop, pop, pclwFixed, pclwVar, pcactMinVar, pfVoid);
}

/***************************************************************************
    Disassemble the script into a message sink (MessageSink) and return whether
    there was an error.
***************************************************************************/
bool CompilerBase::FDisassemble(PScript pscpt, PMSNK pmsnk, PMSNK pmsnkError)
{
    AssertThis(0);
    Assert(pscpt, "nil script");
    AssertPo(pmsnk, 0);
    AssertPo(pmsnkError, 0);
    RuntimeVariableName rtvn;
    LegacyLong ilwMac, ilw, clwPush;
    LegacyLong lw;
    LegacyLong op;
    String stn;
    DataVersion dver;
    PZString pszError = pvNil;

    ilwMac = mm_script_word_count(pscpt);
    if (ilwMac < 1)
    {
        pszError = PszLit("No version numbers on script");
        goto LFail;
    }

    // check the version
    lw = mm_script_word(pscpt, 0);
    dver.Set(SwHigh(lw), SwLow(lw));
    if (!dver.FReadable(_SwCur(), _SwMin()))
    {
        pszError = PszLit("Script version doesn't match script compiler version");
        goto LFail;
    }

    for (ilw = 1;;)
    {
        // write the label
        stn.FFormatSz(PszLit("@L_%04x "), ilw);
        pmsnk->Report(stn.Psz());
        if (ilw >= ilwMac)
            break;

        lw = mm_script_word(pscpt, ilw++);
        clwPush = B2Lw(lw);
        if (!FIn(clwPush, 0, ilwMac - ilw + 1))
        {
            pszError = PszLit("bad instruction");
            goto LFail;
        }

        // write the op string
        if (opNil != (op = B3Lw(lw)))
        {
            // this instruction acts on a variable
            if (clwPush == 0)
            {
                pszError = PszLit("bad var instruction");
                goto LFail;
            }
            clwPush--;
            rtvn.lu1 = (LegacyUlong)SuLow(lw);
            rtvn.lu2 = static_cast<LegacyUlong>(mm_script_word(pscpt, ilw++));
            if (rtvn.lu1 == 0)
            {
                if (op != kopPushLocVar)
                {
                    pszError = PszLit("bad variable");
                    goto LFail;
                }

                // string literal
                if (!ReadScriptLiteral(pscpt, rtvn.lu2, &stn))
                {
                    pszError = PszLit("bad internal variable");
                    goto LFail;
                }
                stn.FExpandControls();
                pmsnk->Report(PszLit("\""));
                pmsnk->Report(stn.Psz());
                pmsnk->Report(PszLit("\" "));
            }
            else
            {
                bool fArray = fFalse;

                if (FIn(op, kopMinArray, kopLimArray))
                {
                    op += kopPushLocVar - kopPushLocArray;
                    fArray = fTrue;
                }

                stn.SetNil();
                switch (op)
                {
                case kopPushLocVar:
                    stn.FAppendCh(ChLit('<'));
                    break;
                case kopPopLocVar:
                    stn.FAppendCh(ChLit('>'));
                    break;
                case kopPushThisVar:
                    stn.FAppendSz(PszLit("<."));
                    break;
                case kopPopThisVar:
                    stn.FAppendSz(PszLit(">."));
                    break;
                case kopPushGlobalVar:
                    stn.FAppendSz(PszLit("<::"));
                    break;
                case kopPopGlobalVar:
                    stn.FAppendSz(PszLit(">::"));
                    break;
                case kopPushRemoteVar:
                    stn.FAppendSz(PszLit("<~"));
                    break;
                case kopPopRemoteVar:
                    stn.FAppendSz(PszLit(">~"));
                    break;
                default:
                    pszError = PszLit("bad var op");
                    goto LFail;
                }
                if (fArray)
                    stn.FAppendCh(ChLit('&'));
                pmsnk->Report(stn.Psz());

                rtvn.GetStn(&stn);
                stn.FAppendCh(kchSpace);
                pmsnk->Report(stn.Psz());
            }
        }
        else if (opNil != (op = SuLow(lw)))
        {
            // normal opcode
            if (!_FGetStnFromOp(op, &stn))
            {
                pszError = PszLit("bad op in script");
            LFail:
                if (pmsnkError != pvNil && pszError != pvNil)
                    pmsnkError->ReportLine(pszError);
                return fFalse;
            }
            stn.FAppendCh(kchSpace);
            pmsnk->Report(stn.Psz());
        }

        // dump the stack stuff
        while (clwPush-- > 0)
        {
            lw = mm_script_word(pscpt, ilw++);
            if (B3Lw(lw) == kbLabel && FIn(lw &= 0x00FFFFFF, 1, ilwMac + 1))
            {
                // REVIEW shonk: label identification: this isn't foolproof
                stn.FFormatSz(PszLit("$L_%04x "), lw);
                pmsnk->Report(stn.Psz());
            }
            else
            {
                stn.FFormatSz(PszLit("%d /*0x%x*/ "), lw, lw);
                pmsnk->Report(stn.Psz());
            }
        }
        pmsnk->ReportLine(PszLit(""));
    }
    pmsnk->ReportLine(PszLit(""));

    return fTrue;
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
    byte rgb[8];
    byte bT;
    LegacyLong ib;

    // unpack the individual bytes
    rgb[0] = (byte)((lu1 & 0x0000FC00) >> 10);
    rgb[1] = (byte)((lu1 & 0x000003F0) >> 4);
    rgb[2] = (byte)(((lu1 & 0x0000000F) << 2) | ((lu2 & 0xC0000000) >> 30));
    rgb[3] = (byte)((lu2 & 0x3F000000) >> 24);
    rgb[4] = (byte)((lu2 & 0x00FC0000) >> 18);
    rgb[5] = (byte)((lu2 & 0x0003F000) >> 12);
    rgb[6] = (byte)((lu2 & 0x00000FC0) >> 6);
    rgb[7] = (byte)(lu2 & 0x0000003F);

    // convert the bytes to characters and append them to the stn
    pstn->SetNil();
    for (ib = 0; ib < 8; ib++)
    {
        bT = rgb[ib];
        if (bT == 0)
            break;
        if (FIn(bT, 1, 11))
            bT += '0' - 1;
        else if (FIn(bT, 11, 37))
            bT += 'A' - 11;
        else if (FIn(bT, 37, 63))
            bT += 'a' - 37;
        else
        {
            Assert(bT == 63, 0);
            bT = '_';
        }
        pstn->FAppendCh((achar)bT);
    }

    if (lu1 & 0xFFFF0000)
    {
        String stn;

        stn.FFormatSz(PszLit("[%d]"), SuHigh(lu1));
        pstn->FAppendStn(&stn);
    }
}

} // end of namespace ScriptCompiler
