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

/***************************************************************************
    Constructor for the script interpreter.
***************************************************************************/
Interpreter::Interpreter(PResourceCache prca, PStringRegistry pstrg)
{
    AssertNilOrPo(prca, 0);
    AssertNilOrPo(pstrg, 0);

    static const MMScriptVMServices services = {
        [](void *context, int scope, MMI32 object) -> MMScriptVariables * {
            auto vm = static_cast<PInterpreter>(context);
            switch (scope)
            {
            case MM_SCRIPT_SCOPE_THIS:
                return vm->_ThisVariables();
            case MM_SCRIPT_SCOPE_GLOBAL:
                return vm->_GlobalVariables();
            case MM_SCRIPT_SCOPE_REMOTE:
                return vm->_RemoteVariables(object);
            default:
                return nullptr;
            }
        },
#ifdef DEBUG
        [](void *context, MMU32 high, MMU32 low) {
            RuntimeVariableName key = {high, low};
            String name;
            key.GetStn(&name);
            static_cast<PInterpreter>(context)->_WarnSz(PszLit("Pushing uninitialized script variable: %s"), &name);
        },
#else
        nullptr,
#endif
        [](void *context, int reason) { static_cast<PInterpreter>(context)->_ReportScriptError(reason); },
        [](void *, MMI32 count) -> MMI32 { return vrndUtil.LwNext(count); },
        [](void *, MMI32 count, const void *words) {
            if (words)
                vsflUtil.ShuffleRglw(count, words);
            else
                vsflUtil.Shuffle(count);
        },
        [](void *) -> MMI32 { return vsflUtil.LwNext(0); },
        [](void *context, unsigned opcode) -> int { return static_cast<PInterpreter>(context)->_FExecHostOp(opcode); }
    };
    AssertDo(mm_script_vm_init(&_vm, KauaiCopyAllocator(), Debug(1 +) 0, &services, this) == MM_STORAGE_OK, 0);

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
    if (pvNil != _vm.script && pvNil != _pstrg && mm_script_has_literals(_vm.script) && _vm.locals.count)
    {
        RuntimeVariableName rtvn;
        MMI32 stid;

        rtvn.lu1 = 0;
        for (rtvn.lu2 = mm_script_literal_count(_vm.script); rtvn.lu2-- > 0;)
        {
            if (mm_script_variables_get(&_vm.locals, rtvn.lu1, rtvn.lu2, &stid))
                _pstrg->Delete(stid);
        }
    }

    mm_script_vm_dispose(&_vm);
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
        Assert(pvNil != _vm.stack.storage.buffers[0].data, "nil stack");
        Assert(pvNil != _vm.script, "nil script");
        Assert(_vm.word_count == mm_script_word_count(_vm.script), 0);
        AssertIn(_vm.pc, 1, _vm.word_count + 1);
        Assert(!_vm.error, 0);
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
    MarkPv(_vm.stack.storage.buffers[0].data);
    MarkScript(_vm.script);
    MarkPv(_vm.locals.storage.buffers[0].data);
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

    // Retain the incoming script across cleanup of the interpreter's old literals.
    if (pscpt)
        mm_script_retain(pscpt);
    Free();
    const bool attached = mm_script_vm_attach(&_vm, pscpt, _SwCur(), _SwMin());
    mm_script_release(pscpt);
    if (attached)
    {
        if (clw > 0)
            _AddParameters(prglw, clw);
        if (mm_script_has_literals(_vm.script) && !_vm.error)
            _AddStrings();
    }
    if (!attached || _vm.error)
    {
        Free();
        return fFalse;
    }

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

    TrashVar(plwReturn);
    TrashVar(pfPaused);
    const bool ok = mm_script_vm_resume(&_vm);
    if (!_vm.paused)
        Free();
    if (ok && pvNil != plwReturn)
        *plwReturn = _vm.result;
    if (pvNil != pfPaused)
        *pfPaused = _vm.paused != 0;

    AssertThis(0);
    return ok;
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
    _SetLocal(rtvn, clw);
    stn = PszLit("_parm");
    rtvn.SetFromStn(&stn);
    for (ilw = 0; ilw < clw; ilw++)
    {
        rtvn.lu1 = LwHighLow(SwLow(ilw), SwLow(rtvn.lu1));
        _SetLocal(rtvn, prglw[ilw]);
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
    for (rtvn.lu2 = 0; rtvn.lu2 < mm_script_literal_count(_vm.script); rtvn.lu2++)
    {
        if (!ReadScriptLiteral(_vm.script, rtvn.lu2, &stn) || !_pstrg->FAdd(&stid, &stn))
        {
            _Error(fFalse);
            break;
        }
        _SetLocal(rtvn, stid);
        if (_vm.error)
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
    Dispatch opcodes that use the native registry or application clock.
***************************************************************************/
bool Interpreter::_FExecHostOp(LegacyLong op)
{
    AssertThis(0);
    LegacyLong lw2, lw3;
    if (!FIn(op, kopCopyStr, kopLimSccb))
        return fFalse;

    const LegacyLong lw1 = _LwPop();
    switch (op)
    {
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

    return !_vm.error;
}

void Interpreter::_Push(LegacyLong lw)
{
    mm_script_vm_push(&_vm, lw);
}

LegacyLong Interpreter::_LwPop(void)
{
    return mm_script_vm_pop(&_vm);
}

const void *Interpreter::_StackWords(LegacyLong clw)
{
    return mm_script_vm_words(&_vm, clw);
}

void Interpreter::_Error(bool fAssert)
{
    mm_script_vm_fail(&_vm, fAssert ? MM_SCRIPT_ERROR_OPERAND : MM_SCRIPT_ERROR_RESOURCE);
}

void Interpreter::_ReportScriptError(int reason)
{
    AssertThis(0);
    switch (reason)
    {
    case MM_SCRIPT_ERROR_STATE:
        Bug("script not paused");
        break;
    case MM_SCRIPT_ERROR_VERSION:
        if (_vm.word_count == 0)
        {
            Bug("No version info on script");
        }
        else
        {
            Bug("Script version doesn't match script interpreter version");
        }
        break;
    case MM_SCRIPT_ERROR_INSTRUCTION:
        Bug("bad instruction");
        break;
    case MM_SCRIPT_ERROR_VARIABLE:
        Bug("bad var instruction");
        break;
    case MM_SCRIPT_ERROR_OPERAND:
    case MM_SCRIPT_ERROR_OPCODE:
        Bug("Runtime error in script");
        break;
    default:
        break;
    }
    Debug(_WarnSz(PszLit("Runtime error")));
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
    if (_vm.script)
    {
        const auto binding = mm_script_binding(_vm.script);
        stn2.FFormatSz(PszLit("Script ('%f', 0x%x, %d): %s"), binding->type, binding->number,
                       static_cast<LegacyLong>(_vm.pc), &stn1);
        stn2.GetSzs(szs);
    }
    else
        stn1.GetSzs(szs);
    Warn(szs);
}
#endif // DEBUG

void Interpreter::_PopList(LegacyLong clw)
{
    mm_script_vm_drop(&_vm, clw);
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

    if (_vm.error)
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

    if (_vm.error)
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

    if (_vm.error)
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

    if (_vm.error)
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

    if (_vm.error)
        return;

    _pstrg->FGet(stid, &stn);
    if (0 == stn.Cch())
        lw = lwEmpty;
    else if (!stn.FGetLw(&lw, 0))
        lw = lwError;
    _Push(lw);
}

void Interpreter::_SetLocal(const RuntimeVariableName &name, LegacyLong lw)
{
    if (!_vm.error && !mm_script_variables_set(&_vm.locals, name.lu1, name.lu2, lw))
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
