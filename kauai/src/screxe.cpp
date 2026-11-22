/* Copyright (c) Microsoft Corporation.
   Licensed under the MIT License. */

/***************************************************************************
    Author: ShonK
    Project: Kauai
    Reviewed:
    Copyright (c) Microsoft Corporation

    Script interpreter.  See scrcom.cpp for an explanation of the format
    of compiled scripts.

***************************************************************************/
#include "util.h"
ASSERTNAME
#include "script_resource_host.h"
#include "handle_services.h"

namespace ScriptInterpreter {

RTCLASS(Interpreter)
RTCLASS(StringRegistry)

#ifdef DEBUG
// these strings are for debug only error messages
static String _stn;
#endif // DEBUG

/***************************************************************************
    Constructor for the script interpreter.
***************************************************************************/
Interpreter::Interpreter(PResourceCache prca, PStringRegistry pstrg)
{
    AssertNilOrPo(prca, 0);
    AssertNilOrPo(pstrg, 0);

    AssertDo(mm_script_stack_init(&_stack, KauaiCopyAllocator(), Debug(1 +) 0) == MM_STORAGE_OK, 0);
    AssertDo(mm_script_variables_init(&_locals, KauaiCopyAllocator()) == MM_STORAGE_OK, 0);
    _pscpt = pvNil;
    _fPaused = fFalse;

    _prca = prca;
    if (pvNil != _prca)
        _prca->AddRef();
    _pstrg = pstrg;
    if (pvNil != _pstrg)
        _pstrg->AddRef();

    AssertThis(0);
}

/***************************************************************************
    Destructor for the script interpreter.
***************************************************************************/
Interpreter::~Interpreter(void)
{
    Free();
    ReleasePpo(&_prca);
    ReleasePpo(&_pstrg);
}

/***************************************************************************
    Free our claim to all this stuff.
***************************************************************************/
void Interpreter::Free(void)
{
    AssertThis(0);

    // nuke literal strings left in the global string table
    if (pvNil != _pscpt && pvNil != _pstrg && mm_script_has_literals(_pscpt) && _locals.count)
    {
        RuntimeVariableName rtvn;
        MMI32 stid;

        rtvn.lu1 = 0;
        for (rtvn.lu2 = mm_script_literal_count(_pscpt); rtvn.lu2-- > 0;)
        {
            if (mm_script_variables_get(&_locals, rtvn.lu1, rtvn.lu2, &stid))
                _pstrg->Delete(stid);
        }
    }

    mm_script_stack_dispose(&_stack);
    mm_script_variables_dispose(&_locals);
    ReleaseScript(&_pscpt);
    _fPaused = fFalse;
}

#ifdef DEBUG
/***************************************************************************
    Assert the validity of a Interpreter.
***************************************************************************/
void Interpreter::AssertValid(LegacyUlong grfsceb)
{
    Interpreter_PAR::AssertValid(0);
    if (grfsceb & fscebRunnable)
    {
        Assert(pvNil != _stack.storage.buffers[0].data, "nil stack");
        Assert(pvNil != _pscpt, "nil script");
        Assert(_ilwMac == static_cast<LegacyLong>(mm_script_word_count(_pscpt)), 0);
        AssertIn(_ilwCur, 1, _ilwMac + 1);
        Assert(!_fError, 0);
    }
    AssertNilOrPo(_pstrg, 0);
    AssertNilOrPo(_prca, 0);
}

/***************************************************************************
    Mark memory for the Interpreter.
***************************************************************************/
void Interpreter::MarkMem(void)
{
    AssertValid(0);
    Interpreter_PAR::MarkMem();
    MarkPv(_stack.storage.buffers[0].data);
    MarkScript(_pscpt);
    MarkPv(_locals.storage.buffers[0].data);
    MarkMemObj(_prca);
}
#endif // DEBUG

/***************************************************************************
    Run the given script.  (prglw, clw) is the list of parameters for the
    script.
***************************************************************************/
bool Interpreter::FRunScript(PScript pscpt, LegacyLong *prglw, LegacyLong clw, LegacyLong *plwReturn, bool *pfPaused)
{
    AssertThis(0);
    return FAttachScript(pscpt, prglw, clw) && FResume(plwReturn, pfPaused);
}

/***************************************************************************
    Attach a script to this Interpreter and pause the script.
***************************************************************************/
bool Interpreter::FAttachScript(PScript pscpt, LegacyLong *prglw, LegacyLong clw)
{
    AssertThis(0);
    Assert(pscpt, "nil script");
    AssertIn(clw, 0, kcbMax);
    AssertPvCb(prglw, LwMul(clw, size(LegacyLong)));
    LegacyLong lw;
    DataVersion dver;

    // Keep the incoming script alive if this interpreter already owns it.
    mm_script_retain(pscpt);
    Free();
    _pscpt = pscpt;
    _lwReturn = 0;
    _fError = fFalse;

    if (mm_script_stack_reserve(&_stack, 10) != MM_STORAGE_OK)
        goto LFail;

    // check the version
    // get and check the version number
    if ((_ilwMac = mm_script_word_count(_pscpt)) < 1)
    {
        Bug("No version info on script");
        goto LFail;
    }
    lw = mm_script_word(_pscpt, 0);
    dver.Set(SwHigh(lw), SwLow(lw));
    if (!dver.FReadable(_SwCur(), _SwMin()))
    {
        Bug("Script version doesn't match script interpreter version");
        goto LFail;
    }

    // add the parameters and literal strings
    if (clw > 0)
        _AddParameters(prglw, clw);
    if (mm_script_has_literals(_pscpt) && !_fError)
        _AddStrings();

    if (_fError)
    {
    LFail:
        Free();
        return fFalse;
    }

    // set the pc and claim we're paused
    _ilwCur = 1;
    _fPaused = fTrue;
    AssertThis(fscebRunnable);
    return fTrue;
}

/***************************************************************************
    Resume a paused script.
***************************************************************************/
bool Interpreter::FResume(LegacyLong *plwReturn, bool *pfPaused)
{
    AssertThis(fscebRunnable);
    AssertNilOrVarMem(plwReturn);
    AssertNilOrVarMem(pfPaused);
    RuntimeVariableName rtvn;
    LegacyLong ilw, clwPush;
    LegacyLong lw;
    LegacyLong op;

    TrashVar(plwReturn);
    TrashVar(pfPaused);
    if (!_fPaused || _fError)
    {
        Bug("script not paused");
        goto LFail;
    }

    AssertIn(_ilwCur, 1, _ilwMac + 1);
    for (_fPaused = fFalse; _ilwCur < _ilwMac && !_fError && !_fPaused;)
    {
        lw = mm_script_word(_pscpt, _ilwCur++);
        clwPush = B2Lw(lw);
        if (!FIn(clwPush, 0, _ilwMac - _ilwCur + 1))
        {
            Bug("bad instruction");
            goto LFail;
        }

        ilw = _ilwCur;
        if (opNil != (op = B3Lw(lw)))
        {
            // this instruction acts on a variable
            if (clwPush == 0)
            {
                Bug("bad var instruction");
                goto LFail;
            }
            clwPush--;
            rtvn.lu1 = (LegacyUlong)lw & 0x0000FFFF;
            rtvn.lu2 = static_cast<LegacyUlong>(mm_script_word(_pscpt, _ilwCur++));
            ilw = _ilwCur;
            if (!_FExecVarOp(op, &rtvn))
                goto LFail;
        }
        else if (opNil != (op = SuLow(lw)))
        {
            // normal opcode
            if (!_FExecOp(op))
                goto LFail;
        }

        // push the stack stuff (if we didn't do a jump)
        if (clwPush > 0 && ilw == _ilwCur)
        {
            if (mm_script_stack_push(&_stack, mm_script_word_span(_pscpt, _ilwCur, clwPush), clwPush) !=
                MM_STORAGE_OK)
            {
            LFail:
                _Error(fFalse);
                break;
            }
            _ilwCur += clwPush;
        }
    }

    if (_ilwCur >= _ilwMac || _fError)
        _fPaused = fFalse;
    if (!_fPaused)
        Free();
    if (!_fError && pvNil != plwReturn)
        *plwReturn = _lwReturn;
    if (pvNil != pfPaused)
        *pfPaused = _fPaused;

    AssertThis(0);
    return !_fError;
}

/***************************************************************************
    Put the parameters in the local variable list.
***************************************************************************/
void Interpreter::_AddParameters(LegacyLong *prglw, LegacyLong clw)
{
    AssertThis(0);
    AssertIn(clw, 1, kcbMax);
    AssertPvCb(prglw, LwMul(clw, size(LegacyLong)));
    String stn;
    LegacyLong ilw;
    RuntimeVariableName rtvn;

    // put the parameters in the local variable gl
    stn = PszLit("_cparm");
    rtvn.SetFromStn(&stn);
    _AssignVar(&_locals, &rtvn, clw);
    stn = PszLit("_parm");
    rtvn.SetFromStn(&stn);
    for (ilw = 0; ilw < clw; ilw++)
    {
        rtvn.lu1 = LwHighLow(SwLow(ilw), SwLow(rtvn.lu1));
        _AssignVar(&_locals, &rtvn, prglw[ilw]);
    }
}

/***************************************************************************
    Put the literal strings into the registry.  And assign the string id's
    to the internal string variables.
***************************************************************************/
void Interpreter::_AddStrings()
{
    AssertThis(0);
    RuntimeVariableName rtvn;
    LegacyLong stid;
    String stn;

    if (pvNil == _pstrg)
    {
        _Error(fFalse);
        return;
    }

    rtvn.lu1 = 0;
    for (rtvn.lu2 = 0; rtvn.lu2 < mm_script_literal_count(_pscpt); rtvn.lu2++)
    {
        if (!ReadScriptLiteral(_pscpt, rtvn.lu2, &stn) || !_pstrg->FAdd(&stid, &stn))
        {
            _Error(fFalse);
            break;
        }
        _AssignVar(&_locals, &rtvn, stid);
        if (_fError)
        {
            _pstrg->Delete(stid);
            break;
        }
    }
}

/***************************************************************************
    Return the current version number of the script compiler.
***************************************************************************/
short Interpreter::_SwCur(void)
{
    AssertBaseThis(0);
    return kswCurSccb;
}

/***************************************************************************
    Return the min version number of the script compiler.  Read can read
    scripts back to this version.
***************************************************************************/
short Interpreter::_SwMin(void)
{
    AssertBaseThis(0);
    return kswMinSccb;
}

/***************************************************************************
    Execute an instruction that has a variable as an argument.
***************************************************************************/
bool Interpreter::_FExecVarOp(LegacyLong op, RuntimeVariableName *prtvn)
{
    AssertThis(0);
    AssertVarMem(prtvn);
    LegacyLong lw;

    if (FIn(op, kopMinArray, kopLimArray))
    {
        // an array access, munge the rtvn
        lw = _LwPop();
        if (_fError)
            return fFalse;
        prtvn->lu1 = LwHighLow(SwLow(lw), SwLow(prtvn->lu1));
        op += kopPushLocVar - kopPushLocArray;
    }

    switch (op)
    {
    case kopPushLocVar:
        _PushVar(&_locals, prtvn);
        break;
    case kopPopLocVar:
        _AssignVar(&_locals, prtvn, _LwPop());
        break;
    case kopPushThisVar:
        _PushVar(_ThisVariables(), prtvn);
        break;
    case kopPopThisVar:
        _AssignVar(_ThisVariables(), prtvn, _LwPop());
        break;
    case kopPushGlobalVar:
        _PushVar(_GlobalVariables(), prtvn);
        break;
    case kopPopGlobalVar:
        _AssignVar(_GlobalVariables(), prtvn, _LwPop());
        break;
    case kopPushRemoteVar:
        lw = _LwPop();
        if (!_fError)
            _PushVar(_RemoteVariables(lw), prtvn);
        break;
    case kopPopRemoteVar:
        lw = _LwPop();
        if (!_fError)
            _AssignVar(_RemoteVariables(lw), prtvn, _LwPop());
        break;
    default:
        _Error(fTrue);
        break;
    }
    return !_fError;
}

/***************************************************************************
    Execute an instruction.
***************************************************************************/
bool Interpreter::_FExecOp(LegacyLong op)
{
    AssertThis(0);
    double dou;
    LegacyLong lw1, lw2, lw3;

    // OP's that don't have any arguments
    switch (op)
    {
    case kopExit:
        // jump to the end
        _ilwCur = _ilwMac;
        return fTrue;
    case kopNextCard:
        _Push(vsflUtil.LwNext(0));
        return fTrue;
    case kopPause:
        _fPaused = fTrue;
        return fTrue;
    }

    // OP's that have at least one argument
    lw1 = _LwPop();
    switch (op)
    {
    case kopAdd:
        _Push(_LwPop() + lw1);
        break;
    case kopSub:
        _Push(_LwPop() - lw1);
        break;
    case kopMul:
        _Push(_LwPop() * lw1);
        break;
    case kopDiv:
        if (lw1 == 0)
            _Error(fTrue);
        else
            _Push(_LwPop() / lw1);
        break;
    case kopMod:
        if (lw1 == 0)
            _Error(fTrue);
        else
            _Push(_LwPop() % lw1);
        break;
    case kopNeg:
        _Push(-lw1);
        break;
    case kopInc:
        _Push(lw1 + 1);
        break;
    case kopDec:
        _Push(lw1 - 1);
        break;
    case kopShr:
        _Push((LegacyUlong)_LwPop() >> lw1);
        break;
    case kopShl:
        _Push((LegacyUlong)_LwPop() << lw1);
        break;
    case kopBOr:
        _Push(_LwPop() | lw1);
        break;
    case kopBAnd:
        _Push(_LwPop() & lw1);
        break;
    case kopBXor:
        _Push(_LwPop() ^ lw1);
        break;
    case kopBNot:
        _Push(~lw1);
        break;
    case kopLXor:
        _Push(FPure(_LwPop()) != FPure(lw1));
        break;
    case kopLNot:
        _Push(!lw1);
        break;
    case kopEq:
        _Push(_LwPop() == lw1);
        break;
    case kopNe:
        _Push(_LwPop() != lw1);
        break;
    case kopGt:
        _Push(_LwPop() > lw1);
        break;
    case kopLt:
        _Push(_LwPop() < lw1);
        break;
    case kopGe:
        _Push(_LwPop() >= lw1);
        break;
    case kopLe:
        _Push(_LwPop() <= lw1);
        break;
    case kopAbs:
        _Push(LwAbs(lw1));
        break;
    case kopRnd:
        if (lw1 <= 0)
            _Error(fTrue);
        else
            _Push(vrndUtil.LwNext(lw1));
        break;
    case kopMulDiv:
        lw2 = _LwPop();
        lw3 = _LwPop();
        if (lw3 == 0)
            _Error(fTrue);
        else
        {
            dou = (double)lw1 * lw2 / lw3;
            if (dou < (double)klwMin || dou > (double)klwMax)
                _Error(fTrue);
            else
                _Push((LegacyLong)dou);
        }
        break;
    case kopDup:
        _Push(lw1);
        _Push(lw1);
        break;
    case kopPop:
        break;
    case kopSwap:
        lw2 = _LwPop();
        _Push(lw1);
        _Push(lw2);
        break;
    case kopRot:
        lw2 = _LwPop();
        _Rotate(lw2, lw1);
        break;
    case kopRev:
        _Reverse(lw1);
        break;
    case kopDupList:
        _DupList(lw1);
        break;
    case kopPopList:
        _PopList(lw1);
        break;
    case kopRndList:
        _RndList(lw1);
        break;
    case kopSelect:
        _Select(lw1, _LwPop());
        break;
    case kopGoEq:
        lw1 = (_LwPop() == lw1);
        goto LGoNz;
    case kopGoNe:
        lw1 = (_LwPop() != lw1);
        goto LGoNz;
    case kopGoGt:
        lw1 = (_LwPop() > lw1);
        goto LGoNz;
    case kopGoLt:
        lw1 = (_LwPop() < lw1);
        goto LGoNz;
    case kopGoGe:
        lw1 = (_LwPop() >= lw1);
        goto LGoNz;
    case kopGoLe:
        lw1 = (_LwPop() <= lw1);
        goto LGoNz;
    case kopGoZ:
        lw1 = !lw1;
        // fall through
    case kopGoNz:
    LGoNz:
        lw2 = _LwPop();
        // labels should have their high byte equal to kbLabel
        if (B3Lw(lw2) != kbLabel || (lw2 &= 0x00FFFFFF) > _ilwMac)
            _Error(fTrue);
        else if (lw1 != 0)
        {
            // perform the goto
            _ilwCur = lw2;
        }
        break;
    case kopGo:
        // labels should have their high byte equal to kbLabel
        if (B3Lw(lw1) != kbLabel || (lw1 &= 0x00FFFFFF) > _ilwMac)
            _Error(fTrue);
        else
        {
            // perform the goto
            _ilwCur = lw1;
        }
        break;
    case kopReturn:
        // jump to the end
        _ilwCur = _ilwMac;
        // fall through
    case kopSetReturn:
        _lwReturn = lw1;
        break;
    case kopShuffle:
        if (lw1 <= 0)
            _Error(fTrue);
        else
            vsflUtil.Shuffle(lw1);
        break;
    case kopShuffleList:
        if (lw1 <= 0)
            _Error(fTrue);
        else
        {
            const void *words = _StackWords(lw1);
            if (words)
            {
                AssertDo(mm_storage_lock(&_stack.storage, 0) == MM_STORAGE_OK, 0);
                vsflUtil.ShuffleRglw(lw1, words);
                AssertDo(mm_storage_unlock(&_stack.storage, 0) == MM_STORAGE_OK, 0);
            }
        }
        break;
    case kopMatch:
        _Match(lw1);
        break;
    case kopCopyStr:
        _CopySubStr(lw1, 0, kcchMaxStn, _LwPop());
        break;
    case kopMoveStr:
        if (pvNil == _pstrg)
            _Error(fTrue);
        else if (_pstrg->FMove(lw1, lw2 = _LwPop()))
            _Push(lw2);
        else
            _Push(stidNil);
        break;
    case kopNukeStr:
        if (pvNil == _pstrg)
            _Error(fTrue);
        else
            _pstrg->Delete(lw1);
        break;
    case kopMergeStrs:
        _MergeStrings(lw1, _LwPop());
        break;
    case kopScaleTime:
        vpusac->Scale(lw1);
        break;
    case kopNumToStr:
        _NumToStr(lw1, _LwPop());
        break;
    case kopStrToNum:
        lw2 = _LwPop();
        _StrToNum(lw1, lw2, _LwPop());
        break;
    case kopConcatStrs:
        lw2 = _LwPop();
        _ConcatStrs(lw1, lw2, _LwPop());
        break;
    case kopLenStr:
        _LenStr(lw1);
        break;
    case kopCopySubStr:
        lw2 = _LwPop();
        lw3 = _LwPop();
        _CopySubStr(lw1, lw2, lw3, _LwPop());
        break;

    default:
        _Error(fTrue);
        break;
    }

    return !_fError;
}

void Interpreter::_Push(LegacyLong lw)
{
    const MMI32 word = lw;
    if (!_fError && mm_script_stack_push(&_stack, &word, 1) != MM_STORAGE_OK)
        _Error(fFalse);
}

LegacyLong Interpreter::_LwPop(void)
{
    MMI32 word;
    if (_fError)
        return 0;
    if (mm_script_stack_pop(&_stack, &word) == MM_STORAGE_OK)
        return word;
    _Error(fTrue);
    return 0;
}

const void *Interpreter::_StackWords(LegacyLong clw)
{
    if (_fError)
        return pvNil;
    const void *words = mm_script_stack_top(&_stack, clw);
    if (!words)
        _Error(fTrue);
    return words;
}

/***************************************************************************
    Register an error.
***************************************************************************/
void Interpreter::_Error(bool fAssert)
{
    AssertThis(0);
    if (!_fError)
    {
        Assert(!fAssert, "Runtime error in script");
        Debug(_WarnSz(PszLit("Runtime error")));
        _fError = fTrue;
    }
}

#ifdef DEBUG
/***************************************************************************
    Emits a warning with the given format string and optional parameters.
***************************************************************************/
void Interpreter::_WarnSz(PZString psz, ...)
{
    AssertThis(0);
    AssertSz(psz);
    String stn1, stn2;
    SZS szs;

    {
        // Build a transient va_list from the trailing varargs of this fn.
        // The old `(ulong *)(&psz + 1)` trick assumes contiguous 4-byte
        // stack args -- broken on Win64 where the first 4 args are in
        // registers and stack slots are 8 bytes.
        va_list va;
        va_start(va, psz);
        stn1.FFormatRgch(psz, CchSz(psz), va);
        va_end(va);
    }
    const auto binding = mm_script_binding(_pscpt);
    stn2.FFormatSz(PszLit("Script ('%f', 0x%x, %d): %s"), binding->type, binding->number, _ilwCur, &stn1);
    stn2.GetSzs(szs);
    Warn(szs);
}
#endif // DEBUG

void Interpreter::_Rotate(LegacyLong clwTot, LegacyLong clwShift)
{
    if (!_fError && mm_script_stack_rotate(&_stack, clwTot, clwShift) != MM_STORAGE_OK)
        _Error(fTrue);
}

void Interpreter::_Reverse(LegacyLong clw)
{
    if (!_fError && mm_script_stack_reverse(&_stack, clw) != MM_STORAGE_OK)
        _Error(fTrue);
}

void Interpreter::_DupList(LegacyLong clw)
{
    if (_fError)
        return;
    const int result = mm_script_stack_duplicate(&_stack, clw);
    if (result != MM_STORAGE_OK)
        _Error(result == MM_STORAGE_ARGUMENT);
}

void Interpreter::_PopList(LegacyLong clw)
{
    if (!_fError && mm_script_stack_drop(&_stack, clw) != MM_STORAGE_OK)
        _Error(fTrue);
}

void Interpreter::_Select(LegacyLong clw, LegacyLong ilw)
{
    if (!_fError && mm_script_stack_select(&_stack, clw, ilw) != MM_STORAGE_OK)
        _Error(fTrue);
}

void Interpreter::_Match(LegacyLong clw)
{
    const LegacyLong key = _LwPop();
    const LegacyLong fallback = _LwPop();
    if (!_fError && mm_script_stack_match(&_stack, clw, key, fallback) != MM_STORAGE_OK)
        _Error(fTrue);
}

/***************************************************************************
    Generates a random entry from a list of numbers on the stack.
***************************************************************************/
void Interpreter::_RndList(LegacyLong clw)
{
    AssertThis(0);

    if (clw <= 0)
        _Error(fTrue);
    else
        _Select(clw, vrndUtil.LwNext(clw));
}

/***************************************************************************
    Copy the string from stidSrc to stidDst.
***************************************************************************/
void Interpreter::_CopySubStr(LegacyLong stidSrc, LegacyLong ichMin, LegacyLong cch, LegacyLong stidDst)
{
    AssertThis(0);
    String stn;

    if (pvNil == _pstrg)
        _Error(fTrue);

    if (_fError)
        return;

    if (!_pstrg->FGet(stidSrc, &stn))
        Debug(_WarnSz(PszLit("Source string doesn't exist (stid = %d)"), stidSrc));

    if (ichMin > 0)
        stn.Delete(0, ichMin);
    if (cch < stn.Cch())
        stn.Delete(cch);
    if (!_pstrg->FPut(stidDst, &stn))
    {
        Debug(_WarnSz(PszLit("Setting dst string failed")));
        _Push(stidNil);
    }
    else
        _Push(stidDst);
}

/***************************************************************************
    Concatenate two strings and put the result in a third. Push the id
    of the destination.
***************************************************************************/
void Interpreter::_ConcatStrs(LegacyLong stidSrc1, LegacyLong stidSrc2, LegacyLong stidDst)
{
    AssertThis(0);
    String stn1, stn2;

    if (pvNil == _pstrg)
        _Error(fTrue);

    if (_fError)
        return;

    if (!_pstrg->FGet(stidSrc1, &stn1))
    {
        Debug(_WarnSz(PszLit("Source string 1 doesn't exist (stid = %d)"), stidSrc1));
    }

    if (!_pstrg->FGet(stidSrc2, &stn2))
    {
        Debug(_WarnSz(PszLit("Source string 2 doesn't exist (stid = %d)"), stidSrc2));
    }

    stn1.FAppendStn(&stn2);
    if (!_pstrg->FPut(stidDst, &stn1))
    {
        Debug(_WarnSz(PszLit("Setting dst string failed")));
        _Push(stidNil);
    }
    else
        _Push(stidDst);
}

/***************************************************************************
    Push the length of the given string.
***************************************************************************/
void Interpreter::_LenStr(LegacyLong stid)
{
    AssertThis(0);
    String stn;

    if (pvNil == _pstrg)
        _Error(fTrue);

    if (!_pstrg->FGet(stid, &stn))
    {
        Debug(_WarnSz(PszLit("Source string doesn't exist (stid = %d)"), stid));
    }

    _Push(stn.Cch());
}

/***************************************************************************
    ChunkyResourceFile reader function to read a string registry string table.
***************************************************************************/
static int ReadStringRegistry(MMResourceCache *, MMU32, MMU32, MMOwnedBlock *block, void **out, MMI32 *bytes)
{
    return mm_script_registry_fetch(block, out, bytes, &ScriptHost());
}

/***************************************************************************
    Merge a string table into the string registry.
***************************************************************************/
void Interpreter::_MergeStrings(ChunkNumber cno, RSC rsc)
{
    AssertThis(0);

    if (_fError)
        return;

    if (pvNil == _pstrg)
    {
        Bug("no string registry to put the string in");
        _Error(fFalse);
        return;
    }

    if (pvNil == _prca)
    {
        Bug("no rca to read string table from");
        _Error(fFalse);
        return;
    }

    const MMResourceRequest request = {kctgStringReg, cno, rsc, ReadStringRegistry, MM_RESOURCE_NORMAL_PRIORITY};
    auto table = static_cast<MMScriptCollection *>(
        mm_resource_access_fetch(&_prca->Resources(), &request, mm_script_collection_services(), nullptr));
    if (!table)
    {
        Debug(_WarnSz(PszLit("Reading string table failed (cno = 0x%x)"), cno));
        return;
    }

    const int ok = mm_script_registry_merge(table, [](void *registry, MMI32 id, const void *text, MMU32 count) -> int {
        String stn;
        stn.SetRgch(const_cast<achar *>(static_cast<const achar *>(text)), count);
        return static_cast<PStringRegistry>(registry)->FPut(id, &stn);
    }, _pstrg);
    if (!ok)
        Debug(_WarnSz(PszLit("Merging string table failed")));
    mm_script_collection_priority(table, crepTossFirst);
    mm_script_collection_release(table);
}

/***************************************************************************
    Convert a number to a string and add the string to the registry.
***************************************************************************/
void Interpreter::_NumToStr(LegacyLong lw, LegacyLong stid)
{
    AssertThis(0);
    String stn;

    if (pvNil == _pstrg)
        _Error(fTrue);

    if (_fError)
        return;

    stn.FFormatSz(PszLit("%d"), lw);
    if (!_pstrg->FPut(stid, &stn))
    {
        Debug(_WarnSz(PszLit("Putting string in registry failed")));
        stid = stidNil;
    }
    _Push(stid);
}

/***************************************************************************
    Convert a string to a number and push the result. If the string is
    empty, push lwEmpty; if there is an error, push lwError.
***************************************************************************/
void Interpreter::_StrToNum(LegacyLong stid, LegacyLong lwEmpty, LegacyLong lwError)
{
    AssertThis(0);
    String stn;
    LegacyLong lw;

    if (pvNil == _pstrg)
        _Error(fTrue);

    if (_fError)
        return;

    _pstrg->FGet(stid, &stn);
    if (0 == stn.Cch())
        lw = lwEmpty;
    else if (!stn.FGetLw(&lw, 0))
        lw = lwError;
    _Push(lw);
}

/***************************************************************************
    Push the value of a variable onto the runtime stack.
***************************************************************************/
void Interpreter::_PushVar(const MMScriptVariables *variables, RuntimeVariableName *prtvn)
{
    AssertThis(0);
    AssertVarMem(prtvn);
    MMI32 lw;

    if (_fError)
        return;

    if (!mm_script_variables_get(variables, prtvn->lu1, prtvn->lu2, &lw))
    {
#ifdef DEBUG
        prtvn->GetStn(&_stn);
        _WarnSz(PszLit("Pushing uninitialized script variable: %s"), &_stn);
#endif // DEBUG
        _Push(0);
    }
    else
        _Push(lw);
}

void Interpreter::_AssignVar(MMScriptVariables *variables, RuntimeVariableName *prtvn, LegacyLong lw)
{
    AssertThis(0);
    AssertVarMem(prtvn);

    if (_fError)
        return;
    if (!variables)
        _Error(fTrue);
    else if (!mm_script_variables_set(variables, prtvn->lu1, prtvn->lu2, lw))
        _Error(fFalse);
}

MMScriptVariables *Interpreter::_ThisVariables(void)
{
    return pvNil;
}

MMScriptVariables *Interpreter::_GlobalVariables(void)
{
    return pvNil;
}

MMScriptVariables *Interpreter::_RemoteVariables(LegacyLong)
{
    return pvNil;
}

PScript CreateScript()
{
    return mm_script_create(&ScriptHost());
}

static int ReadCachedScript(MMResourceCache *cache, MMU32 type, MMU32 number, MMOwnedBlock *block,
                            void **out, MMI32 *bytes)
{
    return mm_script_resource_fetch(cache, type, number, block, out, bytes, &ScriptHost());
}

PScript FetchScript(const MMResourceAccess &source, ChunkTagOrType type, ChunkNumber number, int *error)
{
    const MMResourceRequest request = {type, number, rscNil, ReadCachedScript, MM_RESOURCE_NORMAL_PRIORITY};
    return static_cast<PScript>(mm_resource_access_fetch(&source, &request, mm_script_object_services(), error));
}

PScript ReadScript(PChunkyFile file, ChunkTagOrType type, ChunkNumber number)
{
    AssertPo(file, 0);
    const auto chunks = file->ReadChunkAccess();
    return mm_script_resource_archive(&chunks, KauaiChunkPayloadServices(), file, type, number, &ScriptHost());
}

bool ReadScriptLiteral(PScript script, MMU32 index, PString out)
{
    const void *text;
    MMU32 count;
    if (!mm_script_literal(script, index, &text, &count))
        return false;
    out->SetRgch(const_cast<achar *>(static_cast<const achar *>(text)), count);
    return true;
}

static bool WriteScriptSection(PScript script, bool literals, DataBlock &block)
{
    MMBlockIO io;
    MMStorageWriter writer;
    return block.FSetTemp(mm_script_section_size(script, literals)) &&
           mm_owned_block_io(block.CoreBlock(), &io) == MM_DATA_OK &&
           mm_block_io_writer(&io, &writer) == MM_DATA_OK &&
           mm_script_write_section(script, literals, &writer);
}

/***************************************************************************
    Save the script to the given chunky file.
***************************************************************************/
bool SaveScript(PScript script, PChunkyFile pcfl, ChunkTagOrType ctg, ChunkNumber cno, bool fPack)
{
    Assert(script, "nil script");
    AssertPo(pcfl, 0);
    DataBlock blck;
    ChunkNumber cnoT, cnoStrs;

    // write the script chunk
    if (!WriteScriptSection(script, false, blck))
        return fFalse;
    if (fPack)
        blck.FPackData();

    if (!pcfl->FFind(ctg, cno))
    {
        // chunk doesn't exist, just write it
        if (!pcfl->FPutBlck(&blck, ctg, cnoT = cno))
            return fFalse;
    }
    else
    {
        // chunk already exists - add a new temporary one
        if (!pcfl->FAddBlck(&blck, ctg, &cnoT))
            return fFalse;
    }

    // write the string table if there is one.  The cno for this is allocated
    // via FAdd.
    if (mm_script_has_literals(script))
    {
        if (!WriteScriptSection(script, true, blck))
            goto LFail;
        if (fPack)
            blck.FPackData();

        if (!pcfl->FAddBlck(&blck, kctgScriptStrs, &cnoStrs))
            goto LFail;
        if (!pcfl->FAdoptChild(ctg, cnoT, kctgScriptStrs, cnoStrs))
        {
            pcfl->Delete(kctgScriptStrs, cnoStrs);
        LFail:
            pcfl->Delete(ctg, cnoT);
            return fFalse;
        }
    }

    // swap the data and children of the temporary chunk and the destination
    if (cno != cnoT)
    {
        pcfl->SwapData(ctg, cno, ctg, cnoT);
        pcfl->SwapChildren(ctg, cno, ctg, cnoT);
        pcfl->Delete(ctg, cnoT);
    }

    return fTrue;
}

/***************************************************************************
    Constructor for the runtime string registry.
***************************************************************************/
StringRegistry::StringRegistry(void)
{
    _pgst = pvNil;
    AssertThis(0);
}

/***************************************************************************
    Constructor for the runtime string registry.
***************************************************************************/
StringRegistry::~StringRegistry(void)
{
    AssertThis(0);
    ReleasePpo(&_pgst);
}

#ifdef DEBUG
/***************************************************************************
    Assert the validity of a StringRegistry.
***************************************************************************/
void StringRegistry::AssertValid(LegacyUlong grf)
{
    StringRegistry_PAR::AssertValid(0);
    AssertNilOrPo(_pgst, 0);
}

/***************************************************************************
    Mark memory for the StringRegistry.
***************************************************************************/
void StringRegistry::MarkMem(void)
{
    AssertValid(0);
    StringRegistry_PAR::MarkMem();
    MarkMemObj(_pgst);
}
#endif // DEBUG

/***************************************************************************
    Put the string in the registry with the given string id.
***************************************************************************/
bool StringRegistry::FPut(LegacyLong stid, PString pstn)
{
    AssertThis(0);
    AssertPo(pstn, 0);
    LegacyLong istn;

    if (!_FEnsureGst())
        return fFalse;

    if (_FFind(stid, &istn))
        return _pgst->FPutStn(istn, pstn);

    return _pgst->FInsertStn(istn, pstn, &stid);
}

/***************************************************************************
    Get the string with the given string id.  If the string isn't in the
    registry, sets pstn to an empty string and returns false.
***************************************************************************/
bool StringRegistry::FGet(LegacyLong stid, PString pstn)
{
    AssertThis(0);
    AssertPo(pstn, 0);
    LegacyLong istn;

    if (!_FFind(stid, &istn))
    {
        pstn->SetNil();
        return fFalse;
    }

    _pgst->GetStn(istn, pstn);
    return fTrue;
}

/***************************************************************************
    Add a string to the registry, assigning it an unused id.  The assigned
    id's are not repeated in the near future.  All assigned id's have their
    high bit set.
***************************************************************************/
bool StringRegistry::FAdd(LegacyLong *pstid, PString pstn)
{
    AssertThis(0);
    AssertVarMem(pstid);
    AssertPo(pstn, 0);
    LegacyLong istn;

    for (;;)
    {
        _stidLast = (_stidLast + 1) | 0x80000000L;
        if (!_FFind(_stidLast, &istn))
            break;
    }

    *pstid = _stidLast;
    return FPut(_stidLast, pstn);
}

/***************************************************************************
    Delete a string from the registry.
***************************************************************************/
void StringRegistry::Delete(LegacyLong stid)
{
    AssertThis(0);
    LegacyLong istn;

    if (_FFind(stid, &istn))
        _pgst->Delete(istn);
}

/***************************************************************************
    Change the id of a string from stidSrc to stidDst.  If stidDst already
    exists, it is replaced.  Returns false if the source string doesn't
    exist.  Can't fail if the source does exist.
***************************************************************************/
bool StringRegistry::FMove(LegacyLong stidSrc, LegacyLong stidDst)
{
    AssertThis(0);
    LegacyLong istnSrc, istnDst;

    if (stidSrc == stidDst)
        return _FFind(stidSrc, &istnSrc);

    if (!_FFind(stidDst, &istnSrc))
        return fFalse;

    if (_FFind(stidDst, &istnDst))
    {
        _pgst->Delete(istnDst);
        if (istnSrc > istnDst)
            istnSrc--;
    }

    _pgst->PutExtra(istnSrc, &stidDst);
    _pgst->Move(istnSrc, istnDst);
    return fTrue;
}

/***************************************************************************
    Do a binary search for the string id.  Returns true iff the string is
    in the registry.  In either case, sets *pistn with where the string
    should go.
***************************************************************************/
bool StringRegistry::_FFind(LegacyLong stid, LegacyLong *pistn)
{
    AssertThis(0);
    AssertVarMem(pistn);
    LegacyLong ivMin, ivLim, iv;
    LegacyLong stidT;

    if (pvNil == _pgst)
    {
        *pistn = 0;
        return fFalse;
    }

    for (ivMin = 0, ivLim = _pgst->IvMac(); ivMin < ivLim;)
    {
        iv = (ivMin + ivLim) / 2;
        _pgst->GetExtra(iv, &stidT);
        if (stidT < stid)
            ivMin = iv + 1;
        else if (stidT > stid)
            ivLim = iv;
        else
        {
            *pistn = iv;
            return fTrue;
        }
    }

    *pistn = ivMin;
    return fFalse;
}

/***************************************************************************
    Make sure the StringTable_GST exists.
***************************************************************************/
bool StringRegistry::_FEnsureGst(void)
{
    AssertThis(0);

    if (pvNil != _pgst)
        return fTrue;
    _pgst = StringTable_GST::PgstNew(size(LegacyLong));
    AssertThis(0);

    return pvNil != _pgst;
}

} // end of namespace ScriptInterpreter
