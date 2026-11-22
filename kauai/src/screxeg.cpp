/* Copyright (c) Microsoft Corporation.
   Licensed under the MIT License. */

/***************************************************************************
    Author: ShonK
    Project: Kauai
    Reviewed:
    Copyright (c) Microsoft Corporation

    Script interpreter for GraphicsObject based scripts.

***************************************************************************/
#include "kidframe.h"
#include "mm/script_kidspace.h"
#include "mm/wide.h"
ASSERTNAME
#include "script_resource_host.h"

namespace ScriptInterpreter {

RTCLASS(GraphicsObjectInterpreter)

#ifdef DEBUG
// these strings are for debug only error messages
static String _stn;
#endif // DEBUG

/***************************************************************************
    Constructor for a GraphicsObject based script interpreter. We don't just keep
    the pgob in case the GraphicsObject goes away while the script is running.
***************************************************************************/
GraphicsObjectInterpreter::GraphicsObjectInterpreter(PWorldOfKidspace pwoks, PResourceCache prca, PGraphicsObject pgob) : GraphicsObjectInterpreter_PAR(prca, pwoks->Pstrg())
{
    AssertPo(pwoks, 0);
    AssertPo(prca, 0);
    AssertPo(pgob, 0);

    _pwoks = pwoks;
    _hid = pgob->Hid();
    _grid = pgob->Grid();
    _pgob = pvNil;

    static_assert(hidNil == 0 && siiNil == 0 && static_cast<MMU32>(chidNil) == 0xffffffffUL,
                  "Kidspace sentinel values");
    static_assert(MM_KIDSPACE_STATE_LIMIT == kswMax && ctgNil == 0 &&
                  MM_KIDSPACE_TAG_ANIMATION == kctgAnimation && MM_KIDSPACE_TAG_BITMAP == kctgMbmp &&
                  MM_KIDSPACE_REP_KILL_ANIMATION == unsigned(GraphicalObjectRepresentation::fgokKillAnim) &&
                  MM_KIDSPACE_REP_SKIP_ANIMATION == unsigned(GraphicalObjectRepresentation::fgokNoAnim),
                  "Kidspace representation values");
    static const MMKidspaceObjectServices objects = {
        [](void *context) -> void * {
            auto interpreter = static_cast<PGraphicsObjectInterpreter>(static_cast<PInterpreter>(context));
            return static_cast<PGraphicsObject>(interpreter->_pwoks);
        },
        [](void *context) {
            static_cast<PGraphicsObjectInterpreter>(static_cast<PInterpreter>(context))->GobMayDie();
        },
        [](void *context, void *parent, MMI32 id, MMU32 descriptor) -> void * {
            auto interpreter = static_cast<PGraphicsObjectInterpreter>(static_cast<PInterpreter>(context));
            return static_cast<PGraphicsObject>(interpreter->_pwoks->PgokNew(
                static_cast<PGraphicsObject>(parent), id, descriptor, interpreter->_prca));
        },
        [](void *context, void *parent, MMU32 topic) -> void * {
            auto interpreter = static_cast<PGraphicsObjectInterpreter>(static_cast<PInterpreter>(context));
            return static_cast<PGraphicsObject>(interpreter->_pwoks->PhbalNew(
                static_cast<PGraphicsObject>(parent), interpreter->_prca, topic));
        },
        [](void *, void *gob) { static_cast<PGraphicsObject>(gob)->Release(); },
        [](void *, void *gob, MMI32 state) { static_cast<PKidspaceGraphicObject>(gob)->FChangeState(state); },
        [](void *, void *gob) -> MMI32 { return static_cast<PKidspaceGraphicObject>(gob)->Sno(); },
        [](void *, void *gob) -> MMI32 { return static_cast<PKidspaceGraphicObject>(gob)->ZPlane(); },
        [](void *, void *gob, MMI32 plane) { static_cast<PKidspaceGraphicObject>(gob)->SetZPlane(plane); },
        [](void *, void *gob) { static_cast<PKidspaceGraphicObject>(gob)->FPlay(); },
        [](void *, void *gob) -> int { return static_cast<PKidspaceGraphicObject>(gob)->FPlaying(); },
        [](void *, void *gob) { static_cast<PKidspaceGraphicObject>(gob)->Stop(); },
        [](void *, void *gob) -> MMI32 { return static_cast<PKidspaceGraphicObject>(gob)->NfrCur(); },
        [](void *, void *gob) -> MMI32 { return static_cast<PKidspaceGraphicObject>(gob)->NfrMac(); },
        [](void *, void *gob, MMI32 frame) { static_cast<PKidspaceGraphicObject>(gob)->GotoNfr(frame); },
        [](void *, void *gob, MMI32 command, MMI32 id, MMU32 script) -> int {
            return static_cast<PKidspaceGraphicObject>(gob)->FFilterCidHid(command, id, script);
        },
        [](void *, void *gob, int enabled) { static_cast<PKidspaceGraphicObject>(gob)->SetNoSlip(enabled != 0); },
        [](void *, void *gob, MMI32 id) { static_cast<PKidspaceGraphicObject>(gob)->SetHidToolTip(id); },
        [](void *, void *gob, int enabled) { static_cast<PKidspaceGraphicObject>(gob)->Stream(enabled != 0); }
    };
    static const MMKidspaceClockServices clocks = {
        [](void *context, MMI32 id) -> MMAlarmClock * {
            auto interpreter = static_cast<PGraphicsObjectInterpreter>(static_cast<PInterpreter>(context));
            auto clock = interpreter->_pwoks->PclokFromHid(id);
            return clock ? clock->CoreClock() : nullptr;
        },
        [](void *, MMI32 id) -> int { return (NewObj Clock(id)) != nullptr; },
        [](void *, MMAlarmClock *clock) { static_cast<PClock>(clock->owner)->AddRef(); },
        [](void *, MMAlarmClock *clock) { static_cast<PClock>(clock->owner)->Release(); },
        [](void *, MMAlarmClock *clock, MMU32 time) { static_cast<PClock>(clock->owner)->Start(time); },
        [](void *, MMAlarmClock *clock) { static_cast<PClock>(clock->owner)->Stop(); },
        [](void *, MMAlarmClock *clock, void *gob, MMI32 delay, MMI32 script) -> int {
            return static_cast<PClock>(clock->owner)->FSetAlarm(delay, static_cast<PGraphicsObject>(gob), script);
        }
    };
    static const MMKidspaceSoundServices sound = {
        [](void *context, void *gob, const MMSoundPlay *play) -> MMI32 {
            if (gob)
                return static_cast<PKidspaceGraphicObject>(gob)->SiiPlaySound(
                    play->type, play->number, play->queue, play->volume, play->plays, play->offset,
                    play->priority, play->sound_class);
            if (!vpsndm)
                return siiNil;
            auto interpreter = static_cast<PGraphicsObjectInterpreter>(static_cast<PInterpreter>(context));
            return vpsndm->SiiPlay(interpreter->_prca->Resources(), play->type, play->number, play->queue,
                                   play->volume, play->plays, play->offset, play->priority, play->sound_class);
        },
        [](void *, void *gob, MMU32 type, MMU32 number) -> MMI32 {
            return static_cast<PKidspaceGraphicObject>(gob)->SiiPlayMouseSound(type, number);
        },
        [](void *, int operation, MMI32 first, MMI32 second, MMI32 *out) -> int {
            if (!vpsndm)
                return 0;
            *out = CallSoundDevice(vpsndm, operation, first, second);
            return 1;
        }
    };
    static const MMKidspaceScriptServices kidspace = {
        [](void *context, MMI32 id) -> void * {
            return static_cast<PGraphicsObjectInterpreter>(static_cast<PInterpreter>(context))->_PgobFromHid(id);
        },
        [](void *, void *object) -> MMI32 { return static_cast<PGraphicsObject>(object)->Hid(); },
        [](void *context, void *object, int relation) -> void * {
            auto gob = static_cast<PGraphicsObject>(object);
            switch (relation)
            {
            case MM_KIDSPACE_PARENT:
                return static_cast<PGraphicsObjectInterpreter>(static_cast<PInterpreter>(context))->_pwoks->PgobParGob(gob);
            case MM_KIDSPACE_NEXT_SIBLING:
                return gob->PgobNextSib();
            case MM_KIDSPACE_PREVIOUS_SIBLING:
                return gob->PgobPrevSib();
            case MM_KIDSPACE_FIRST_CHILD:
                return gob->PgobFirstChild();
            }
            return nullptr;
        },
        [](void *) -> MMI32 { return CommandHandler::HidUnique(); },
        [](void *, void *object, int mode, MMPixelRect *out) -> int {
            auto gob = static_cast<PGraphicsObject>(object);
            if (mode == MM_KIDSPACE_MOVABLE_RECT && gob->Hwnd() != hNil)
                return 0;
            RC rect;
            gob->GetRc(&rect, mode == MM_KIDSPACE_LOCAL_RECT ? cooLocal : cooParent);
            *out = {rect.xpLeft, rect.ypTop, rect.xpRight, rect.ypBottom};
            return 1;
        },
        [](void *, void *object, const MMPixelRect *in) {
            RC rect(in->left, in->top, in->right, in->bottom);
            static_cast<PGraphicsObject>(object)->SetPos(&rect, pvNil);
        },
        [](void *, void *object, int mode, MMPixelPoint *out) -> int {
            auto gob = static_cast<PGraphicsObject>(object);
            PT point;
            if (mode == MM_KIDSPACE_MOUSE)
            {
                bool down;
                gob->GetPtMouse(&point, &down);
            }
            else
            {
                if (!gob->FIs(kclsKidspaceGraphicObject))
                    return 0;
                auto kid = static_cast<PKidspaceGraphicObject>(gob);
                if (mode == MM_KIDSPACE_LOCAL_REGISTRATION)
                    kid->GetPtReg(&point, cooLocal);
                else
                    kid->GetPtReg(&point);
            }
            *out = {point.xp, point.yp};
            return 1;
        },
        [](void *context, MMI32 id) -> void * {
            return static_cast<PGraphicsObjectInterpreter>(static_cast<PInterpreter>(context))->_pwoks->PcmhFromHid(id);
        },
        [](void *, const MMQueuedCommand *in) {
            Command command;
            command.cid = in->id;
            command.pcmh = static_cast<PCommandHandler>(in->target);
            command.pgg = pvNil;
            for (unsigned i = 0; i < MM_COMMAND_ARGUMENTS; ++i)
                command.rglw[i] = in->arguments[i];
            vpcex->EnqueueCmd(&command);
        },
        [](void *context, void *object, MMU32 number, int byNumber, void *parameters, MMI32 count, MMI32 *out) -> int {
            auto gob = static_cast<PGraphicsObject>(object);
            if (!gob->FIs(kclsKidspaceGraphicObject))
                return -1;
            auto kid = static_cast<PKidspaceGraphicObject>(gob);
            static_cast<PGraphicsObjectInterpreter>(static_cast<PInterpreter>(context))->GobMayDie();
            auto words = static_cast<LegacyLong *>(parameters);
            LegacyLong result;
            tribool status;
            if (byNumber)
                kid->FRunScriptCno(number, words, count, &result, &status);
            else
                kid->FRunScript(number, words, count, &result, &status);
            if (status != tYes)
                return 0;
            *out = result;
            return 1;
        },
        [](void *context, void *object, MMU32 child, unsigned flags, MMU32 tag, MMI32 dx, MMI32 dy, MMU32 delay) {
            static_cast<PGraphicsObjectInterpreter>(static_cast<PInterpreter>(context))->GobMayDie();
            static_cast<PKidspaceGraphicObject>(object)->FSetRep(child, flags, tag, dx, dy, delay);
        },
#ifdef DEBUG
        [](void *context, int reason, unsigned opcode, MMI32 id, MMU32 detail) {
            auto vm = static_cast<PGraphicsObjectInterpreter>(static_cast<PInterpreter>(context));
            if (reason == MM_KIDSPACE_ARGUMENT_ALLOCATION)
                vm->_WarnSz(PszLit("OOM attempting to run script"));
            else if (reason == MM_KIDSPACE_NESTED_FAILURE)
                vm->_WarnSz(PszLit("Running script failed (chid = 0x%x, gid = %d)"), static_cast<LegacyLong>(detail),
                            static_cast<LegacyLong>(id));
            else if (reason == MM_KIDSPACE_CLOCK_RETAINED)
                vm->_WarnSz(PszLit("Clock already exists - incrementing ref count (hid = %d)"), static_cast<LegacyLong>(id));
            else if (reason == MM_KIDSPACE_CLOCK_MISSING)
                vm->_WarnSz(PszLit("Missing clock (opcode = 0x%x, hid = %d)"),
                            static_cast<LegacyLong>(opcode), static_cast<LegacyLong>(id));
            else if (reason == MM_KIDSPACE_ALARM_FAILED)
                vm->_WarnSz(PszLit("Setting Alarm failed (hid = %d)"), static_cast<LegacyLong>(id));
            else if (reason == MM_KIDSPACE_SOUND_FALLBACK)
                vm->_WarnSz(PszLit("No KidspaceGraphicObject for PlaySound(Gob|This) (gid = %d)"),
                            static_cast<LegacyLong>(id));
            else if (reason == MM_KIDSPACE_DESTROY_WORLD)
                vm->_WarnSz(PszLit("Can't Destroy WorldOfKidspace - destroying all its children"));
            else if (reason == MM_KIDSPACE_INVALID_STATE)
                vm->_WarnSz(PszLit("State out of range for ChangeState(Gob|This) (gid = %d, sno = %d)"),
                            static_cast<LegacyLong>(id), static_cast<LegacyLong>(mm_signed_word(detail)));
            else if (reason == MM_KIDSPACE_FILTER_FAILED)
                vm->_WarnSz(PszLit("Filtering failed (gid = %d)"), static_cast<LegacyLong>(id));
            else
                vm->_WarnSz(PszLit("Missing or incompatible GraphicsObject (opcode = 0x%x, gid = %d)"),
                            static_cast<LegacyLong>(opcode), static_cast<LegacyLong>(id));
        },
#else
        nullptr,
#endif
        &clocks,
        &sound,
        [](void *, void *gob) -> int { return static_cast<PGraphicsObject>(gob)->FIs(kclsKidspaceGraphicObject); },
        &objects
    };
    _vm.kidspace = &kidspace;
    _vm.this_object = _hid;

    AssertThis(0);
}

#ifdef DEBUG
/***************************************************************************
    Assert the validity of a GraphicsObjectInterpreter.
***************************************************************************/
void GraphicsObjectInterpreter::AssertValid(LegacyUlong grf)
{
    GraphicsObjectInterpreter_PAR::AssertValid(0);
    Assert(hidNil != _hid, 0);
    AssertPo(_pwoks, 0);
    Assert(_prca != pvNil, 0);
}
#endif // DEBUG

/***************************************************************************
    The script is being resumed, so set _pgob to nil (don't know whether
    it exists).
***************************************************************************/
bool GraphicsObjectInterpreter::FResume(LegacyLong *plwReturn, bool *pfPaused)
{
    GobMayDie();
    return GraphicsObjectInterpreter_PAR::FResume(plwReturn, pfPaused);
}

/***************************************************************************
    Return the gob that corresponds to this script interpreter.
***************************************************************************/
PGraphicsObject GraphicsObjectInterpreter::_PgobThis(void)
{
    AssertThis(0);

    if (pvNil != _pgob)
    {
        AssertPo(_pgob, 0);
        Assert(_pgob->Grid() == _grid, "bad value of _pgob");
        return _pgob;
    }

    _pgob = _pwoks->PgobFromGrid(_grid);
    return _pgob;
}

/***************************************************************************
    Return the gob that is accessible to this script interpreter and has
    the given hid.
***************************************************************************/
PGraphicsObject GraphicsObjectInterpreter::_PgobFromHid(LegacyLong hid)
{
    AssertThis(0);

    if (hid == _hid)
        return _PgobThis();

    return _pwoks->PgobFromHid(hid);
}

/***************************************************************************
    Return the address of the variable table for the GraphicsObject associated with
    this script interpreter.
***************************************************************************/
MMScriptVariables *GraphicsObjectInterpreter::_ThisVariables(void)
{
    PGraphicsObject pgob = _PgobThis();

    if (pvNil == pgob)
        return pvNil;
    return pgob->ScriptVariables();
}

/***************************************************************************
    Return the address of the variable table for the WorldOfKidspace associated with
    this script interpreter.
***************************************************************************/
MMScriptVariables *GraphicsObjectInterpreter::_GlobalVariables(void)
{
    AssertThis(0);

    return _pwoks->ScriptVariables();
}

/***************************************************************************
    Return the address of the variable table for the GraphicsObject with given hid.
***************************************************************************/
MMScriptVariables *GraphicsObjectInterpreter::_RemoteVariables(LegacyLong lw)
{
    PGraphicsObject pgob = _PgobFromHid(lw);

    if (pvNil == pgob)
        return pvNil;
    return pgob->ScriptVariables();
}

/***************************************************************************
    Return the current version number of the script compiler.
***************************************************************************/
short GraphicsObjectInterpreter::_SwCur(void)
{
    return kswCurSccg;
}

/***************************************************************************
    Return the min version number of the script compiler. Read can read
    scripts back to this version.
***************************************************************************/
short GraphicsObjectInterpreter::_SwMin(void)
{
    return kswMinSccg;
}

/***************************************************************************
    Execute a script command.
***************************************************************************/
bool GraphicsObjectInterpreter::_FExecHostOp(LegacyLong op)
{
    LegacyLong dtim;
    LegacyLong lw1, lw2, lw3, lw4;

    switch (op)
    {
    case kopAlert:
    case kopPrint:
    case kopPrintStat:
    case kopAlertStr:
    case kopPrintStr:
    case kopPrintStrStat:
        GobMayDie();
        _DoAlert(op);
        break;

    case kopSetColorTable:
        _SetColorTable(_LwPop());
        break;

    case kopGetModifierState:
        _Push(_pwoks->GrfcustCur());
        break;

    case kopChangeModifierState:
        lw1 = _LwPop();
        lw2 = _LwPop();
        if (!_vm.error)
            _pwoks->ModifyGrfcust(lw1, lw2);
        break;

    case kopTransition:
        lw1 = _LwPop();
        lw2 = _LwPop();
        dtim = _LwPop();
        lw3 = _LwPop();
        lw4 = _LwPop();
        if (!_vm.error)
        {
            AbstractColor acr;
            PDynamicArray pglclr = _PglclrGet((ChunkNumber)lw4);
            acr.SetFromLw(lw3);
            vpappb->SetGft(lw1, lw2, LuMulDiv(dtim, kdtsSecond, kdtimSecond), pglclr, acr);
            ReleasePpo(&pglclr);
        }
        break;

    case kopGetEdit:
        lw1 = _LwPop();
        _DoEditControl(lw1, _LwPop(), fTrue);
        break;

    case kopSetEdit:
        lw1 = _LwPop();
        _DoEditControl(lw1, _LwPop(), fFalse);
        break;

    case kopGetProp:
        lw1 = _LwPop();
        if (!_vm.error)
        {
            GobMayDie();
            long property;
            if (!vpappb->FGetProp(lw1, &property))
            {
                Debug(_WarnSz(PszLit("GetProp failed (prid = %d)"), lw1));
                lw2 = 0;
            }
            else
                lw2 = static_cast<LegacyLong>(property);
            _Push(lw2);
        }
        break;

    case kopSetProp:
        lw1 = _LwPop();
        lw2 = _LwPop();
        GobMayDie();
        if (!_vm.error && !vpappb->FSetProp(lw1, lw2))
        {
            Debug(_WarnSz(PszLit("SetProp failed (prid = %d, val = %d)"), lw1, lw2));
        }
        break;

    case kopLaunch:
        lw1 = _LwPop();
        GobMayDie();
        if (!_vm.error)
            _Push(_FLaunch(lw1));
        break;

    case kopStartLongOp:
        vpappb->BeginLongOp();
        break;

    case kopEndLongOp:
        vpappb->EndLongOp(_LwPop());
        break;

    case kopModalHelp:
        lw1 = _LwPop();
        lw2 = _LwPop();
        if (!_vm.error)
        {
            GobMayDie();
            long result;
            if (_pwoks->FModalTopic(_prca, lw1, &result))
                lw2 = static_cast<LegacyLong>(result);
            _Push(lw2);
        }
        break;

    case kopFlushUserEvents:
        vpappb->FlushUserEvents(_LwPop());
        break;

    default:
        return GraphicsObjectInterpreter_PAR::_FExecHostOp(op);
    }

    return !_vm.error;
}

/***************************************************************************
    Put up an alert containing a list of numbers.
***************************************************************************/
void GraphicsObjectInterpreter::_DoAlert(LegacyLong op)
{
    String stn1;
    String stn2;
    LegacyLong lw;
    bool fStrings;
    LegacyLong clw = _LwPop();

    switch (op)
    {
    default:
        return;

#ifdef CHUNK_STATS
    case kopPrintStat:
#endif // CHUNK_STATS
    case kopAlert:
    case kopPrint:
        fStrings = fFalse;
        break;

#ifdef CHUNK_STATS
    case kopPrintStrStat:
#endif // CHUNK_STATS
    case kopAlertStr:
    case kopPrintStr:
        fStrings = fTrue;
        break;
    }

    if (fStrings && pvNil == _pstrg)
    {
        _Error(fTrue);
        return;
    }

    const auto binding = mm_script_binding(_vm.script);
    stn1.FFormatSz(PszLit("Script Message ('%f', 0x%x, %d): "), binding->type, binding->number, static_cast<LegacyLong>(_vm.pc));

    while (clw-- > 0)
    {
        lw = _LwPop();
        if (fStrings)
            _pstrg->FGet(lw, &stn2);
        else
            stn2.FFormatSz(PszLit("%d (0x%x) "), lw, lw);
        stn1.FAppendStn(&stn2);
    }

    switch (op)
    {
#ifdef CHUNK_STATS
    case kopPrintStat:
    case kopPrintStrStat:
        ChunkyFile::DumpStn(&stn1);
        break;
#endif // CHUNK_STATS

    case kopAlert:
    case kopAlertStr:
        _pwoks->TGiveAlert(&stn1, bkOk, cokInformation);
        break;

    case kopPrint:
    case kopPrintStr:
        _pwoks->Print(&stn1);
        break;
    }
}

/***************************************************************************
    Get or set the string in an edit control.
***************************************************************************/
void GraphicsObjectInterpreter::_DoEditControl(LegacyLong hid, LegacyLong stid, bool fGet)
{
    PEditControlBase pedcb;
    LegacyLong cch;
    achar rgch[kcchMaxStn];
    String stn;

    if (_vm.error || pvNil == (pedcb = (PEditControlBase)_PgobFromHid(hid)) || !pedcb->FIs(kclsEditControlBase))
    {
        Debug(_WarnSz(PszLit("Missing edit control for (Get|Set)Edit (gid = %d)"), hid));
        return;
    }

    if (pvNil == _pstrg)
    {
        _Error(fTrue);
        return;
    }

    cch = pedcb->IchMac();
    if (fGet)
    {
        cch = pedcb->CchFetch(rgch, 0, LwMin(kcchMaxStn, cch));
        stn.SetRgch(rgch, cch);
        _pstrg->FPut(stid, &stn);
    }
    else
    {
        _pstrg->FGet(stid, &stn);
        pedcb->FReplace(stn.Psz(), stn.Cch(), 0, cch);
    }
}

/***************************************************************************
    Set the current color table.
***************************************************************************/
void GraphicsObjectInterpreter::_SetColorTable(ChunkNumber cno)
{
    PDynamicArray pglclr;

    if (pvNil == (pglclr = _PglclrGet(cno)))
        return;

    GraphicsPort::SetActiveColors(pglclr, fpalIdentity);
    ReleasePpo(&pglclr);
}

/***************************************************************************
    Read the indicated color table and return a reference to it.
***************************************************************************/
PDynamicArray GraphicsObjectInterpreter::_PglclrGet(ChunkNumber cno)
{
    if (cnoNil == cno)
        return pvNil;

    const MMResourceRequest request = {kctgColorTable, cno, rscNil, FReadColorTable, MM_RESOURCE_NORMAL_PRIORITY};
    auto table = static_cast<MMScriptCollection *>(
        mm_resource_access_fetch(&_prca->Resources(), &request, mm_script_collection_services(), nullptr));
    if (!table)
        return pvNil;

    MMU32 count;
    const auto rows = mm_script_color_rows(table, &count);
    auto palette = DynamicArray::PglNew(sizeof(Color), count);
    if (palette && count && !palette->FInsert(0, const_cast<void *>(rows), count))
        ReleasePpo(&palette);
    mm_script_collection_priority(table, crepTossFirst);
    mm_script_collection_release(table);
    return palette;
}

/***************************************************************************
    A chunky resource reader to read a color table into a C cache owner.
***************************************************************************/
int FReadColorTable(MMResourceCache *, MMU32, MMU32, MMOwnedBlock *block, void **out, MMI32 *bytes)
{
    static_assert(sizeof(Color) == 4, "script color row size");
    return mm_script_colors_fetch(block, out, bytes, &ScriptHost());
}

/***************************************************************************
    Launch an app with the given command line. Return true iff the launch
    was successful.
***************************************************************************/
bool GraphicsObjectInterpreter::_FLaunch(LegacyLong stid)
{
    AssertThis(0);
    String stn;

    if (pvNil == _pstrg)
    {
        _Error(fTrue);
        return fFalse;
    }

    if (!_pstrg->FGet(stid, &stn))
    {
        Debug(_WarnSz(PszLit("String missing for Launch (stid = %d)"), stid));
        Warn("string missing");
        return fFalse;
    }

#ifdef WIN
    STARTUPINFO sui;
    PROCESS_INFORMATION pi;

    ClearPb(&sui, size(sui));
    sui.cb = size(sui);

    return CreateProcess(pvNil, stn.Psz(), pvNil, pvNil, fFalse, DETACHED_PROCESS, pvNil, pvNil, &sui, &pi);
#else  //! WIN
    RawRtn(); // REVIEW shonk: Mac: implement GraphicsObjectInterpreter::_FLaunch
    return fFalse;
#endif //! WIN
}

} // end of namespace ScriptInterpreter
