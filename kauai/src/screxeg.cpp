/* Copyright (c) Microsoft Corporation.
   Licensed under the MIT License. */

/***************************************************************************
    Author: ShonK
    Project: Kauai
    Reviewed:
    Copyright (c) Microsoft Corporation

    Script interpreter for GraphicsObject based scripts.

***************************************************************************/
#include <cstring>
#include "kidframe.h"
#include "mm/script_kidspace.h"
ASSERTNAME
#include "script_resource_host.h"

namespace ScriptInterpreter {

using GraphicalObjectRepresentation::fgokNil;
using GraphicalObjectRepresentation::fgokKillAnim;
using GraphicalObjectRepresentation::fgokNoAnim;

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
        [](void *context, void *object, MMU32 child, MMI32 dx, MMI32 dy, MMI32 delay) {
            static_cast<PGraphicsObjectInterpreter>(static_cast<PInterpreter>(context))->GobMayDie();
            static_cast<PKidspaceGraphicObject>(object)->FSetRep(child, fgokNoAnim, ctgNil, dx, dy, delay);
        },
#ifdef DEBUG
        [](void *context, int reason, unsigned opcode, MMI32 id, MMU32 detail) {
            auto vm = static_cast<PGraphicsObjectInterpreter>(static_cast<PInterpreter>(context));
            if (reason == MM_KIDSPACE_ARGUMENT_ALLOCATION)
                vm->_WarnSz(PszLit("OOM attempting to run script"));
            else if (reason == MM_KIDSPACE_NESTED_FAILURE)
                vm->_WarnSz(PszLit("Running script failed (chid = 0x%x, gid = %d)"), static_cast<LegacyLong>(detail),
                            static_cast<LegacyLong>(id));
            else
                vm->_WarnSz(PszLit("Missing or incompatible GraphicsObject (opcode = 0x%x, gid = %d)"),
                            static_cast<LegacyLong>(opcode), static_cast<LegacyLong>(id));
        }
#else
        nullptr
#endif
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
    PGraphicsObject pgob;
    PClock pclok;
    LegacyLong hid;
    LegacyLong dtim;
    LegacyLong lw1, lw2, lw3, lw4;
    const void *words;

    hid = _hid; // for "this" operations
    switch (op)
    {
    case kopDestroyGob:
        hid = _LwPop();
        // fall through
    case kopDestroyThis:
        if (!_vm.error && pvNil != (pgob = _PgobFromHid(hid)))
        {
            GobMayDie();
            if (pgob == _pwoks)
            {
                Debug(_WarnSz(PszLit("Can't Destroy WorldOfKidspace - destroying all its children")));
                PGraphicsObject pgobT;

                while (pvNil != (pgobT = pgob->PgobFirstChild()))
                    ReleasePpo(&pgobT);
            }
            else
                ReleasePpo(&pgob);
        }
        break;

    case kopCreateChildGob:
        hid = _LwPop();
        // fall through
    case kopCreateChildThis:
        lw1 = _LwPop();
        lw2 = _LwPop();
        pgob = pvNil;
        if (!_vm.error && pvNil != (pgob = _PgobFromHid(hid)))
        {
            GobMayDie();
            pgob = _pwoks->PgokNew(pgob, lw1, lw2, _prca);
        }
        else
        {
            Debug(_WarnSz(PszLit("Missing parent GraphicsObject for CreateChild(Gob|This) (gid = %d)"), hid));
        }
        _Push(pvNil == pgob ? hidNil : pgob->Hid());
        break;

    case kopCreateHelpGob:
        hid = _LwPop();
        // fall through
    case kopCreateHelpThis:
        lw1 = _LwPop();
        pgob = pvNil;
        if (!_vm.error && pvNil != (pgob = _PgobFromHid(hid)))
        {
            GobMayDie();
            pgob = _pwoks->PhbalNew(pgob, _prca, lw1);
        }
        else
        {
            Debug(_WarnSz(PszLit("Missing parent GraphicsObject for CreateHelp(Gob|This) (gid = %d)"), hid));
        }
        _Push(pvNil == pgob ? hidNil : pgob->Hid());
        break;

    case kopChangeStateGob:
        hid = _LwPop();
        // fall through
    case kopChangeStateThis:
        lw1 = _LwPop();
        if (_vm.error || pvNil == (pgob = _PgobFromHid(hid)) || !pgob->FIs(kclsKidspaceGraphicObject) || !FIn(lw1, 0, kswMax))
        {
            Debug(_WarnSz(PszLit("Missing GraphicsObject or state out of range for ")
                              PszLit("ChangeState(Gob|This) (gid = %d, sno = %d)"),
                          hid, lw1));
        }
        else
        {
            GobMayDie();
            ((PKidspaceGraphicObject)pgob)->FChangeState(lw1);
        }
        break;

    case kopAnimateGob:
        hid = _LwPop();
        // fall through
    case kopAnimateThis:
        lw1 = _LwPop();
        if (_vm.error || pvNil == (pgob = _PgobFromHid(hid)) || !pgob->FIs(kclsKidspaceGraphicObject))
        {
            Debug(_WarnSz(PszLit("Missing GraphicsObject for Animate(Gob|This) (gid = %d)"), hid));
        }
        else
        {
            GobMayDie();
            ((PKidspaceGraphicObject)pgob)->FSetRep(lw1, fgokNil, kctgAnimation);
        }
        break;

    case kopSetPictureGob:
        hid = _LwPop();
        // fall through
    case kopSetPictureThis:
        lw1 = _LwPop();
        if (_vm.error || pvNil == (pgob = _PgobFromHid(hid)) || !pgob->FIs(kclsKidspaceGraphicObject))
        {
            Debug(_WarnSz(PszLit("Missing GraphicsObject for SetPicture(Gob|This) (gid = %d)"), hid));
        }
        else
        {
            GobMayDie();
            ((PKidspaceGraphicObject)pgob)->FSetRep(lw1, fgokKillAnim, kctgMbmp);
        }
        break;

    case kopSetRepGob:
        hid = _LwPop();
        // fall through
    case kopSetRepThis:
        lw1 = _LwPop();
        if (_vm.error || pvNil == (pgob = _PgobFromHid(hid)) || !pgob->FIs(kclsKidspaceGraphicObject))
        {
            Debug(_WarnSz(PszLit("Missing GraphicsObject for SetRep(Gob|This) (gid = %d)"), hid));
        }
        else
        {
            GobMayDie();
            ((PKidspaceGraphicObject)pgob)->FSetRep(lw1);
        }
        break;

    case kopStateGob:
        hid = _LwPop();
        // fall through
    case kopStateThis:
        if (_vm.error || pvNil == (pgob = _PgobFromHid(hid)) || !pgob->FIs(kclsKidspaceGraphicObject))
        {
            lw1 = 0;
            Debug(_WarnSz(PszLit("Missing GraphicsObject for State(Gob|This) (gid = %d)"), hid));
        }
        else
            lw1 = ((PKidspaceGraphicObject)pgob)->Sno();
        _Push(lw1);
        break;

    case kopAlert:
    case kopPrint:
    case kopPrintStat:
    case kopAlertStr:
    case kopPrintStr:
    case kopPrintStrStat:
        GobMayDie();
        _DoAlert(op);
        break;

    case kopCreateClock:
        hid = _LwPop();
        if (hidNil != hid && pvNil != (pclok = _pwoks->PclokFromHid(hid)))
        {
            Debug(_WarnSz(PszLit("Clock already exists - incrementing ref count (hid = %d)"), hid));
            pclok->AddRef();
        }
        else
        {
            if (hidNil == hid)
                hid = CommandHandler::HidUnique();
            if (pvNil == NewObj Clock(hid))
                hid = hidNil;
        }
        _Push(hid);
        break;

    case kopDestroyClock:
        if (pvNil != (pclok = _pwoks->PclokFromHid(_LwPop())))
            ReleasePpo(&pclok);
        break;

    case kopStartClock:
        hid = _LwPop();
        lw1 = _LwPop();
        if (!_vm.error && pvNil != (pclok = _pwoks->PclokFromHid(hid)))
            pclok->Start(lw1);
        else
        {
            Debug(_WarnSz(PszLit("Missing clock for StartClock (hid = %d)"), hid));
        }
        break;

    case kopStopClock:
        if (pvNil != (pclok = _pwoks->PclokFromHid(_LwPop())))
            pclok->Stop();
        else
        {
            Debug(_WarnSz(PszLit("Missing clock for StopClock (hid = %d)"), hid));
        }
        break;

    case kopTimeCur:
        if (pvNil != (pclok = _pwoks->PclokFromHid(_LwPop())))
            _Push(pclok->TimCur());
        else
        {
            Debug(_WarnSz(PszLit("Missing clock for TimeCur (hid = %d)"), hid));
            _Push(0);
        }
        break;

    case kopSetAlarmGob:
        hid = _LwPop();
    case kopSetAlarmThis:
    case kopSetAlarm:
        lw1 = _LwPop();
        dtim = _LwPop();
        if (kopSetAlarm == op)
            lw2 = chidNil;
        else
            lw2 = _LwPop();
        if (_vm.error || pvNil == (pgob = _PgobFromHid(hid)) || pvNil == (pclok = _pwoks->PclokFromHid(lw1)) ||
            !pclok->FSetAlarm(dtim, pgob, lw2))
        {
            Debug(_WarnSz(PszLit("Setting Alarm failed (hid = %d)"), lw1));
        }
        break;

    case kopZGob:
        hid = _LwPop();
        // fall through
    case kopZThis:
        if (_vm.error || pvNil == (pgob = _PgobFromHid(hid)) || !pgob->FIs(kclsKidspaceGraphicObject))
        {
            Debug(_WarnSz(PszLit("Missing GraphicsObject for Z(Gob|This) (gid = %d)"), hid));
            _Push(0);
        }
        else
            _Push(((PKidspaceGraphicObject)pgob)->ZPlane());
        break;

    case kopSetZGob:
        hid = _LwPop();
        // fall through
    case kopSetZThis:
        lw1 = _LwPop();
        if (_vm.error || pvNil == (pgob = _PgobFromHid(hid)) || !pgob->FIs(kclsKidspaceGraphicObject))
        {
            Debug(_WarnSz(PszLit("Missing GraphicsObject for SetZ(Gob|This) (gid = %d)"), hid));
        }
        else
            ((PKidspaceGraphicObject)pgob)->SetZPlane(lw1);
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

    case kopPlayGob:
        hid = _LwPop();
        // fall through
    case kopPlayThis:
        if (_vm.error || pvNil == (pgob = _PgobFromHid(hid)) || !pgob->FIs(kclsKidspaceGraphicObject))
        {
            Debug(_WarnSz(PszLit("Missing GraphicsObject for Play(Gob|This) (gid = %d)"), hid));
        }
        else
            ((PKidspaceGraphicObject)pgob)->FPlay();
        break;

    case kopPlayingGob:
        hid = _LwPop();
        // fall through
    case kopPlayingThis:
        if (_vm.error || pvNil == (pgob = _PgobFromHid(hid)) || !pgob->FIs(kclsKidspaceGraphicObject))
        {
            Debug(_WarnSz(PszLit("Missing GraphicsObject for Playing(Gob|This) (gid = %d)"), hid));
            _Push(fFalse);
        }
        else
            _Push(((PKidspaceGraphicObject)pgob)->FPlaying());
        break;

    case kopStopGob:
        hid = _LwPop();
        // fall through
    case kopStopThis:
        if (_vm.error || pvNil == (pgob = _PgobFromHid(hid)) || !pgob->FIs(kclsKidspaceGraphicObject))
        {
            Debug(_WarnSz(PszLit("Missing GraphicsObject for Stop(Gob|This) (gid = %d)"), hid));
        }
        else
            ((PKidspaceGraphicObject)pgob)->Stop();
        break;

    case kopCurFrameGob:
        hid = _LwPop();
        // fall through
    case kopCurFrameThis:
        if (_vm.error || pvNil == (pgob = _PgobFromHid(hid)) || !pgob->FIs(kclsKidspaceGraphicObject))
        {
            Debug(_WarnSz(PszLit("Missing GraphicsObject for (CurFrame(Gob|This) (gid = %d)"), hid));
            _Push(0);
        }
        else
            _Push(((PKidspaceGraphicObject)pgob)->NfrCur());
        break;

    case kopCountFramesGob:
        hid = _LwPop();
        // fall through
    case kopCountFramesThis:
        if (_vm.error || pvNil == (pgob = _PgobFromHid(hid)) || !pgob->FIs(kclsKidspaceGraphicObject))
        {
            Debug(_WarnSz(PszLit("Missing GraphicsObject for CountFrames(Gob|This) (gid = %d)"), hid));
            _Push(0);
        }
        else
            _Push(((PKidspaceGraphicObject)pgob)->NfrMac());
        break;

    case kopGotoFrameGob:
        hid = _LwPop();
        // fall through
    case kopGotoFrameThis:
        lw1 = _LwPop();
        if (_vm.error || pvNil == (pgob = _PgobFromHid(hid)) || !pgob->FIs(kclsKidspaceGraphicObject))
        {
            Debug(_WarnSz(PszLit("Missing GraphicsObject for GotoFrame(Gob|This) (gid = %d)"), hid));
        }
        else
            ((PKidspaceGraphicObject)pgob)->GotoNfr(lw1);
        break;

    case kopFilterCmdsGob:
        hid = _LwPop();
        // fall through
    case kopFilterCmdsThis:
        lw1 = _LwPop();
        lw2 = _LwPop();
        lw3 = _LwPop();
        if (_vm.error || pvNil == (pgob = _PgobFromHid(hid)) || !pgob->FIs(kclsKidspaceGraphicObject))
        {
            Debug(_WarnSz(PszLit("Missing GraphicsObject for FilterCmds(Gob|This) (gid = %d)"), hid));
        }
        else if (!((PKidspaceGraphicObject)pgob)->FFilterCidHid(lw1, lw2, lw3))
        {
            Debug(_WarnSz(PszLit("Filtering failed (gid = %d)"), hid));
        }
        break;

    case kopDestroyChildrenGob:
        hid = _LwPop();
        // fall through
    case kopDestroyChildrenThis:
        if (!_vm.error && pvNil != (pgob = _PgobFromHid(hid)))
        {
            PGraphicsObject pgobT;

            GobMayDie();
            while (pvNil != (pgobT = pgob->PgobFirstChild()))
                ReleasePpo(&pgobT);
        }
        break;

    case kopPlaySoundGob:
        hid = _LwPop();
        // fall through
    case kopPlaySoundThis:
        lw1 = siiNil;
        if (!_vm.error)
        {
            pgob = (hidNil == hid) ? pvNil : _PgobFromHid(hid);

            if (pvNil != pgob && pgob->FIs(kclsKidspaceGraphicObject))
            {
                if (pvNil != (words = _StackWords(7)))
                {
                    MMI32 args[7];
                    std::memcpy(args, words, sizeof(args));
                    lw1 = ((PKidspaceGraphicObject)pgob)
                              ->SiiPlaySound(args[6], args[5], args[4], args[3], args[2], 0, args[1], args[0]);
                }
            }
            else
            {
#ifdef DEBUG
                if (hidNil != hid)
                {
                    _WarnSz(PszLit("No KidspaceGraphicObject for PlaySound(Gob|This) (gid = %d)"), hid);
                }
#endif // DEBUG

                if (pvNil != vpsndm && pvNil != (words = _StackWords(7)))
                {
                    MMI32 args[7];
                    std::memcpy(args, words, sizeof(args));
                    lw1 = vpsndm->SiiPlay(_prca->Resources(), args[6], args[5], args[4], args[3], args[2], 0,
                                          args[1], args[0]);
                }
            }
            _PopList(7);
        }
        _Push(lw1);
        break;

    case kopStopSound:
        lw1 = _LwPop();
        if (!_vm.error && pvNil != vpsndm)
            vpsndm->Stop(lw1);
        break;

    case kopStopSoundClass:
        lw1 = _LwPop();
        lw2 = _LwPop();
        if (!_vm.error && pvNil != vpsndm)
            vpsndm->StopAll(lw1, lw2);
        break;

    case kopPlayingSound:
        lw1 = _LwPop();
        if (!_vm.error && pvNil != vpsndm)
            _Push(vpsndm->FPlaying(lw1));
        break;

    case kopPlayingSoundClass:
        lw1 = _LwPop();
        lw2 = _LwPop();
        if (!_vm.error && pvNil != vpsndm)
            _Push(vpsndm->FPlayingAll(lw1, lw2));
        break;

    case kopPauseSound:
        lw1 = _LwPop();
        if (!_vm.error && pvNil != vpsndm)
            vpsndm->Pause(lw1);
        break;

    case kopPauseSoundClass:
        lw1 = _LwPop();
        lw2 = _LwPop();
        if (!_vm.error && pvNil != vpsndm)
            vpsndm->PauseAll(lw1, lw2);
        break;

    case kopResumeSound:
        lw1 = _LwPop();
        if (!_vm.error && pvNil != vpsndm)
            vpsndm->Resume(lw1);
        break;

    case kopResumeSoundClass:
        lw1 = _LwPop();
        lw2 = _LwPop();
        if (!_vm.error && pvNil != vpsndm)
            vpsndm->ResumeAll(lw1, lw2);
        break;

    case kopPlayMouseSoundGob:
        hid = _LwPop();
        // fall through
    case kopPlayMouseSoundThis:
        lw1 = _LwPop();
        lw2 = _LwPop();
        if (!_vm.error && pvNil != (pgob = _PgobFromHid(hid)) && pgob->FIs(kclsKidspaceGraphicObject))
        {
            _Push(((PKidspaceGraphicObject)pgob)->SiiPlayMouseSound(lw1, lw2));
        }
        else
            _Push(siiNil);
        break;

    case kopSetNoSlipGob:
        hid = _LwPop();
        // fall through
    case kopSetNoSlipThis:
        lw1 = _LwPop();
        if (!_vm.error && pvNil != (pgob = _PgobFromHid(hid)) && pgob->FIs(kclsKidspaceGraphicObject))
        {
            ((PKidspaceGraphicObject)pgob)->SetNoSlip(lw1);
        }
        else
        {
            Debug(_WarnSz(PszLit("Missing GraphicsObject for SetNoSlip(Gob|This) (gid = %d)"), hid));
        }
        break;

    case kopSetMasterVolume:
        lw1 = _LwPop();
        if (!_vm.error && pvNil != vpsndm)
            vpsndm->SetVlm(lw1);
        break;

    case kopGetMasterVolume:
        if (pvNil != vpsndm)
            _Push(vpsndm->VlmCur());
        else
            _Push(0);
        break;

    case kopStartLongOp:
        vpappb->BeginLongOp();
        break;

    case kopEndLongOp:
        vpappb->EndLongOp(_LwPop());
        break;

    case kopSetToolTipSourceGob:
        hid = _LwPop();
        // fall through
    case kopSetToolTipSourceThis:
        lw1 = _LwPop();
        if (!_vm.error && pvNil != (pgob = _PgobFromHid(hid)) && pgob->FIs(kclsKidspaceGraphicObject))
        {
            ((PKidspaceGraphicObject)pgob)->SetHidToolTip(lw1);
        }
        else
        {
            Debug(_WarnSz(PszLit("Missing GraphicsObject for SetToolTipSource(Gob|This) (gid = %d)"), hid));
        }
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

    case kopStreamGob:
        hid = _LwPop();
        // fall through
    case kopStreamThis:
        lw1 = _LwPop();
        if (_vm.error || pvNil == (pgob = _PgobFromHid(hid)) || !pgob->FIs(kclsKidspaceGraphicObject))
        {
            Debug(_WarnSz(PszLit("Missing GraphicsObject for Stream(Gob|This) (gid = %d)"), hid));
        }
        else
            ((PKidspaceGraphicObject)pgob)->Stream(FPure(lw1));
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
