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
        vrndUtil.RandomCore(),
        vsflUtil.ShuffleCore(),
        [](void *, MMU32 scale) { vpusac->Scale(scale); },
        [](void *context, unsigned opcode) -> int { return static_cast<PInterpreter>(context)->_FExecHostOp(opcode); },
        [](void *context, MMU32 number, MMU32 scope) {
            return static_cast<PInterpreter>(context)->_FetchStringResource(number, scope);
        },
#ifdef DEBUG
        [](void *context, int reason, MMI32 value) {
            static_cast<PInterpreter>(context)->_ReportScriptString(reason, value);
        }
#else
        nullptr
#endif
    };
    AssertDo(mm_script_vm_init(&_vm, KauaiCopyAllocator(), Debug(1 +) 0, pstrg ? pstrg->Storage() : nullptr,
                               &services, this) == MM_STORAGE_OK,
             0);

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
    const bool attached = mm_script_vm_attach(&_vm, pscpt, _SwCur(), _SwMin(), prglw, clw);
    mm_script_release(pscpt);
    if (!attached)
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
    Host-specific opcodes are provided by derived interpreters.
***************************************************************************/
bool Interpreter::_FExecHostOp(LegacyLong)
{
    AssertThis(0);
    return fFalse;
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

static int ReadStringRegistry(MMResourceCache *, MMU32, MMU32, MMOwnedBlock *block, void **out, MMI32 *bytes)
{
    return mm_script_registry_fetch(block, out, bytes, &ScriptHost());
}

MMScriptCollection *Interpreter::_FetchStringResource(MMU32 number, MMU32 scope)
{
    if (!_prca)
    {
        Bug("no rca to read string table from");
        _Error(fFalse);
        return nullptr;
    }
    const MMResourceRequest request = {kctgStringReg, number, scope, ReadStringRegistry, MM_RESOURCE_NORMAL_PRIORITY};
    return static_cast<MMScriptCollection *>(
        mm_resource_access_fetch(&_prca->Resources(), &request, mm_script_collection_services(), nullptr));
}

#ifdef DEBUG
void Interpreter::_ReportScriptString(int reason, LegacyLong value)
{
    switch (reason)
    {
    case MM_SCRIPT_STRING_MISSING:
        _WarnSz(PszLit("Source string doesn't exist (stid = %d)"), value);
        break;
    case MM_SCRIPT_STRING_STORE_FAILED:
        _WarnSz(PszLit("Setting dst string failed (stid = %d)"), value);
        break;
    case MM_SCRIPT_STRING_RESOURCE_MISSING:
        _WarnSz(PszLit("Reading string table failed (cno = 0x%x)"), value);
        break;
    case MM_SCRIPT_STRING_MERGE_FAILED:
        _WarnSz(PszLit("Merging string table failed (cno = 0x%x)"), value);
        break;
    case MM_SCRIPT_STRING_BAD_SLICE:
        Bug("Bad character position to delete from");
        break;
    case MM_SCRIPT_STRING_NO_REGISTRY:
        Bug("no string registry to put the string in");
        break;
    }
}
#endif // DEBUG

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

StringRegistry::StringRegistry(void)
{
    AssertDo(mm_script_strings_init(&_strings, sizeof(achar), Debug(1 +) 0, KauaiCopyAllocator()) == MM_STORAGE_OK, 0);
    AssertThis(0);
}

StringRegistry::~StringRegistry(void)
{
    AssertThis(0);
    mm_script_strings_dispose(&_strings);
}

#ifdef DEBUG
void StringRegistry::AssertValid(LegacyUlong)
{
    StringRegistry_PAR::AssertValid(0);
    Assert(_strings.unit_bytes == sizeof(achar), 0);
}

void StringRegistry::MarkMem(void)
{
    AssertValid(0);
    StringRegistry_PAR::MarkMem();
    MarkPv(_strings.storage.buffers[0].data);
    MarkPv(_strings.storage.buffers[1].data);
}
#endif // DEBUG

bool StringRegistry::FPut(LegacyLong stid, PString pstn)
{
    AssertThis(0);
    AssertPo(pstn, 0);
    return mm_script_strings_put(&_strings, stid, pstn->Prgch(), pstn->Cch());
}

bool StringRegistry::FGet(LegacyLong stid, PString pstn)
{
    AssertThis(0);
    AssertPo(pstn, 0);
    const void *text;
    MMU32 count;
    if (!mm_script_strings_get(&_strings, stid, &text, &count))
    {
        pstn->SetNil();
        return fFalse;
    }
    pstn->SetRgch(const_cast<achar *>(static_cast<const achar *>(text)), count);
    return fTrue;
}

bool StringRegistry::FAdd(LegacyLong *pstid, PString pstn)
{
    AssertThis(0);
    AssertVarMem(pstid);
    AssertPo(pstn, 0);
    MMI32 id;
    const bool ok = mm_script_strings_add(&_strings, &id, pstn->Prgch(), pstn->Cch());
    *pstid = id;
    return ok;
}

void StringRegistry::Delete(LegacyLong stid)
{
    AssertThis(0);
    mm_script_strings_delete(&_strings, stid);
}

bool StringRegistry::FMove(LegacyLong stidSrc, LegacyLong stidDst)
{
    AssertThis(0);
    return mm_script_strings_move(&_strings, stidSrc, stidDst);
}

} // end of namespace ScriptInterpreter
