/* Copyright (c) Microsoft Corporation.
   Licensed under the MIT License. */

/***************************************************************************
    Author: ShonK
    Project: Kauai
    Reviewed:
    Copyright (c) Microsoft Corporation

    Basic command classes: CommandExecutionManager (command dispatcher), CommandHandler (command handler).

    The command dispatcher (CommandExecutionManager) has a command (Command) queue and a list of
    command handlers (CommandHandler). During normal operation (CommandExecutionManager::FDispatchNextCmd),
    the CommandExecutionManager takes the next Command from the queue, passes it to each CommandHandler in its
    list (by calling CommandHandler::FDoCmd) until one of the handlers returns true.
    If none of the handlers in the list returns true, the command is passed
    to the handler specified in the Command itself (cmd.pcmh, if not nil).

    A CommandHandler is placed in the handler list by a call to CommandExecutionManager::FAddCmh. The
    cmhl parameter determines the order of CommandHandler's in the list. The grfcmm
    parameter indicates which targets the CommandHandler wants to see commands for.
    The options are fcmmThis, fcmmNobody, fcmmOthers.

    The CommandExecutionManager class supports command stream recording and playback.

***************************************************************************/
#include <exception>
#include <new>
#include "kauai_core.h"
#include "cmd.h"
#include "clok.h"
#include "cmd_hooks.h"
#include "document_storage_host.h"
ASSERTNAME

// command map shared by every command handler
BEGIN_CMD_MAP_BASE(CommandHandler)
END_CMD_MAP_NIL()

RTCLASS(CommandHandler)
RTCLASS(CommandExecutionManager)

PCommandExecutionManager vpcex;

static MMQueuedCommand QueueRecord(const Command &cmd)
{
    return {cmd.pcmh, cmd.cid, cmd.pgg, {cmd.rglw[0], cmd.rglw[1], cmd.rglw[2], cmd.rglw[3]}};
}

static Command NativeCommand(const MMQueuedCommand &cmd)
{
    return {static_cast<PCommandHandler>(cmd.target), cmd.id, static_cast<PGeneralGroup>(cmd.group),
            {cmd.arguments[0], cmd.arguments[1], cmd.arguments[2], cmd.arguments[3]}};
}

struct CommandExecutionManager::DispatchHost : DocumentStorageHost
{
    PCommandExecutionManager manager;
    PCommand command;

    DispatchHost(PCommandExecutionManager owner, PCommand current = nullptr)
        : DocumentStorageHost(nullptr), manager(owner), command(current) {}

    static int ReleaseGroup(void *context, void *group)
    {
        auto &host = *static_cast<DispatchHost *>(context);
        return host.Guard([&] {
            static_cast<PGeneralGroup>(group)->Release();
            return 0;
        });
    }

    static int Prepare(void *context)
    {
        auto &host = *static_cast<DispatchHost *>(context);
        // Native dispatch historically normalized the tri-state result to bool.
        return host.Guard([&] { return host.manager->_TGetNextCmd() != tNo; });
    }

    static void *Target(void *context)
    {
        return static_cast<DispatchHost *>(context)->command->pcmh;
    }

    static int Send(void *context, void *handler)
    {
        auto &host = *static_cast<DispatchHost *>(context);
        return host.Guard([&] { return host.manager->_FSendCmd(static_cast<PCommandHandler>(handler)); });
    }

    static int Finish(void *context)
    {
        auto &host = *static_cast<DispatchHost *>(context);
        return host.Guard([&] {
            host.manager->_CleanUpCmd();
            return 0;
        });
    }

    static int Enable(void *context, void *handler, MMU32 *flags)
    {
        auto &host = *static_cast<DispatchHost *>(context);
        return host.Guard([&] {
            LegacyUlong nativeFlags = fedsNil;
            const bool handled = host.manager->_FEnableCmd(static_cast<PCommandHandler>(handler),
                                                           host.command, &nativeFlags);
            *flags = nativeFlags;
            return handled;
        });
    }
};

LegacyLong CommandHandler::_hidLast;

/***************************************************************************
    cmd_hooks.h seam: function pointers installed by the gui-side
    appb._FInit so cmd.cpp can be moved to kauai-core without dragging
    in vpappb. Without hooks, lookup returns nil and burial still cleans up
    core dispatcher/clock references. Targeted recorded-command replay needs
    a host lookup callback; see cmd_hooks.h for lifetime/global-scope rules.

    Part 1 of the cmd_core split (see plan
    docs/superpowers/plans/2026-05-02-cmd-core-split.md).
***************************************************************************/
static PFN_PcmhFromHid s_pfnPcmhFromHid = pvNil;
static PFN_BuryCmh s_pfnBuryCmh = pvNil;

void SetCmdHooks(PFN_PcmhFromHid pfn1, PFN_BuryCmh pfn2)
{
    s_pfnPcmhFromHid = pfn1;
    s_pfnBuryCmh = pfn2;
}

PCommandHandler PcmhFromHid_Hook(LegacyLong hid)
{
    return (s_pfnPcmhFromHid != pvNil) ? s_pfnPcmhFromHid(hid) : pvNil;
}

void BuryCmh_Hook(PCommandHandler pcmh)
{
    if (s_pfnBuryCmh != pvNil)
        s_pfnBuryCmh(pcmh);
    else
    {
        // A standalone dispatcher must not retain handlers or alarms after
        // destruction. The GUI hook additionally covers suspended contexts.
        if (vpcex != pvNil)
            vpcex->BuryCmh(pcmh);
        Clock::BuryCmh(pcmh);
    }
}

#ifdef DEBUG
/***************************************************************************
    Assert the validity of a Command.
***************************************************************************/
void Command::AssertValid(LegacyUlong grf)
{
    AssertThisMem();
    AssertNilOrPo(pgg, 0);
    AssertNilOrPo(pcmh, 0);
}
#endif // DEBUG

/***************************************************************************
    Static method to return a hid (command handler ID) such that the numbers
    { hid, hid + 1, ... , hid + ccmh - 1 } are not currently in use and all
    have their high bit set. To avoid possible conflicts, hard-wired
    handler ID's should have their high bit clear. This guarantees that
    the values will not be returned again in the near future (whether or not
    they are in use).

    This calls vpappb->PcmhFromHid to determine if a hid is in use. This
    means that the returned hid is only unique over handlers that the
    application class knows about.
***************************************************************************/
LegacyLong CommandHandler::HidUnique(LegacyLong ccmh)
{
    AssertIn(ccmh, 1, 1000);
    LegacyLong ccmhT;

    _hidLast |= 0x80000000L;
    for (ccmhT = ccmh; ccmhT-- > 0;)
    {
        _hidLast++;
        if (!(_hidLast & 0x80000000L))
        {
            _hidLast = 0x80000000L;
            ccmhT = ccmh;
            continue;
        }
        if (pvNil != PcmhFromHid_Hook(_hidLast))
            ccmhT = ccmh;
    }

    return _hidLast - (ccmh - 1);
}

/***************************************************************************
    Constructor for a command handler - set the handler id.
***************************************************************************/
CommandHandler::CommandHandler(LegacyLong hid)
{
    AssertBaseThis(0);
    Assert(hid != hidNil, "bad hid");
    _hid = hid;
}

/***************************************************************************
    Destructor for a command handler - purge any global references to it
    from the app. The app purges it from the command dispatcher.
***************************************************************************/
CommandHandler::~CommandHandler(void)
{
    AssertThis(0);
    BuryCmh_Hook(this);
}

#ifdef DEBUG
/***************************************************************************
    Assert the validity of a CommandHandler.
***************************************************************************/
void CommandHandler::AssertValid(LegacyUlong grf)
{
    CommandHandler_PAR::AssertValid(0);
    Assert(_hid != hidNil, 0);
}
#endif // DEBUG

/***************************************************************************
    Protected virtual function to find a CommandMapEntry (command map entry) for the
    given command id.
***************************************************************************/
bool CommandHandler::_FGetCmme(LegacyLong cid, LegacyUlong grfcmmWanted, CommandMapEntry *pcmme)
{
    AssertThis(0);
    AssertVarMem(pcmme);
    Assert(cid != cidNil, "why is the cid nil?");
    CommandMap *pcmm;
    CommandMapEntry *pcmmeT;
    CommandMapEntry *pcmmeDef = pvNil;

    for (pcmm = Pcmm(); pcmm != pvNil; pcmm = pcmm->pcmmBase)
    {
        for (pcmmeT = pcmm->prgcmme; pcmmeT->cid != cidNil; pcmmeT++)
        {
            if (pcmmeT->cid == cid && (pcmmeT->grfcmm & grfcmmWanted))
            {
                *pcmme = *pcmmeT;
                return fTrue;
            }
        }

        // check for a default function
        if (pcmmeT->pfncmd != pvNil && (pcmmeT->grfcmm & grfcmmWanted) && pcmmeDef == pvNil)
        {
            pcmmeDef = pcmmeT;
        }
    }

    // no specific one found, return the default one
    if (pcmmeDef != pvNil)
    {
        *pcmme = *pcmmeDef;
        return fTrue;
    }
    return fFalse;
}

/***************************************************************************
    Determines whether this command handler can handle the given command. If
    not, returns false (and does nothing else). If so, executes the command
    and returns true.

    NOTE: a true return indicates that the command was handled and should
    not be passed on down the command handler list - regardless of the
    success/failure of the execution of the command. Do not return false
    to indicate that command execution failed.
***************************************************************************/
bool CommandHandler::FDoCmd(PCommand pcmd)
{
    AssertThis(0);
    AssertPo(pcmd, 0);
    CommandMapEntry cmme;
    const auto grfcmm = mm_command_target_mask(this, pcmd->pcmh);

    if (!_FGetCmme(pcmd->cid, grfcmm, &cmme) || pvNil == cmme.pfncmd)
        return fFalse;

    return (this->*cmme.pfncmd)(pcmd);
}

/***************************************************************************
    Determines whether the command is enabled. If this command handler
    doesn't normally handle the command, this returns false (and does
    nothing else). Otherwise sets the grfeds and returns true.
***************************************************************************/
bool CommandHandler::FEnableCmd(PCommand pcmd, LegacyUlong *pgrfeds)
{
    AssertThis(0);
    AssertPo(pcmd, 0);
    AssertVarMem(pgrfeds);
    CommandMapEntry cmme;
    const auto grfcmm = mm_command_target_mask(this, pcmd->pcmh);

    if (!_FGetCmme(pcmd->cid, grfcmm, &cmme) || pvNil == cmme.pfncmd)
        return fFalse;

    if (cmme.pfneds == pvNil)
    {
        if (cidNil == cmme.cid)
            return fFalse;
        *pgrfeds = fedsEnable;
    }
    else if (!(this->*cmme.pfneds)(pcmd, pgrfeds))
        return fFalse;

    return fTrue;
}

/***************************************************************************
    Command dispatcher constructor.
***************************************************************************/
CommandExecutionManager::CommandExecutionManager(void)
{
    AssertBaseThis(0);
#ifdef DEBUG
    const int trash = 1;
#else
    const int trash = 0;
#endif
    const auto allocator = Group::CollectionAllocator();
    AssertDo(mm_commands_init(&_dispatch, &allocator, trash), "invalid command allocator");
}

/***************************************************************************
    Destructor for a CommandExecutionManager.
***************************************************************************/
CommandExecutionManager::~CommandExecutionManager(void)
{
    AssertBaseThis(0);
    {
        DocumentStorageHost queue(&_dispatch.queue_storage), filters(&_dispatch.filter_storage);
        DispatchHost host(this);
        mm_commands_dispose(&_dispatch, &host, DispatchHost::ReleaseGroup);
        host.Rethrow();
        queue.Rethrow();
        filters.Rethrow();
    }
    ReleasePpo(&_pcfl);
    ReleasePpo(&_pglcmdf);
    ReleasePpo(&_cmdCur.pgg);
}

/***************************************************************************
    Static method to create a new CommandExecutionManager object.
***************************************************************************/
PCommandExecutionManager CommandExecutionManager::PcexNew(LegacyLong ccmdInit, LegacyLong ccmhInit)
{
    AssertIn(ccmdInit, 0, kcbMax);
    AssertIn(ccmhInit, 0, kcbMax);
    PCommandExecutionManager pcex;

    if (pvNil == (pcex = NewObj CommandExecutionManager))
        return pvNil;

    if (!pcex->_FInit(ccmdInit, ccmhInit))
        ReleasePpo(&pcex);

    AssertNilOrPo(pcex, 0);
    return pcex;
}

/***************************************************************************
    Initialization of the command dispatcher.
***************************************************************************/
bool CommandExecutionManager::_FInit(LegacyLong ccmdInit, LegacyLong ccmhInit)
{
    AssertBaseThis(0);
    AssertIn(ccmdInit, 0, kcbMax);
    AssertIn(ccmhInit, 0, kcbMax);

    DocumentStorageHost queue(&_dispatch.queue_storage), filters(&_dispatch.filter_storage);
    const bool ok = mm_commands_reserve(&_dispatch, ccmdInit, ccmhInit) != 0;
    queue.Rethrow();
    filters.Rethrow();
    return ok;
}

/***************************************************************************
    Start recording a macro to the given chunky file.
***************************************************************************/
void CommandExecutionManager::Record(PChunkyFile pcfl)
{
    AssertThis(0);
    AssertPo(pcfl, 0);

    if (_rs != rsNormal)
    {
        Bug("already recording or playing");
        return;
    }

    _rs = rsRecording;
    _rec = recNil;
    _pcfl = pcfl;
    _pcfl->AddRef();
    _cno = cnoNil;
    _icmdf = 0;
    _chidLast = 0;
    _cact = 0;
    Assert(_pglcmdf == pvNil, "why isn't _pglcmdf nil?");

    if ((_pglcmdf = DynamicArray::PglNew(size(CommandFile), 100)) == pvNil)
        _rec = recMemError;
    else if (!_pcfl->FAdd(0, kctgMacro, &_cno))
    {
        _rec = recFileError;
        _cno = cnoNil;
    }
}

/***************************************************************************
    Stop recording a command stream, and write the command stream to
    file. If there were any errors, delete the command stream from the
    chunky file. Pushes a command notifying the world that recording
    has stopped. The command (cidCexRecordDone) contains the error code
    (rec), and cno in the first two lw's of the command. If the
    rec is not recNil, the cno is cnoNil and wasn't actually created.
***************************************************************************/
void CommandExecutionManager::StopRecording(void)
{
    AssertThis(0);
    DataBlock blck;

    if (_rs != rsRecording)
        return;

    if (_rec == recNil)
    {
        LegacyLong cb;

        if (_cact > 1)
        {
            // rewrite the last one's _cact
            CommandFile cmdf;

            _pglcmdf->Get(_icmdf, &cmdf);
            cmdf.cact = _cact;
            _pglcmdf->Put(_icmdf, &cmdf);
        }
        cb = _pglcmdf->CbOnFile();
        if (!_pcfl->FPut(cb, kctgMacro, _cno, &blck) || !_pglcmdf->FWrite(&blck))
        {
            _rec = recFileError;
        }
    }

    if (_rec != recNil && _cno != cnoNil)
    {
        _pcfl->Delete(kctgMacro, _cno);
        _cno = cnoNil;
    }

    if (_cno != cnoNil)
    {
        if (_pcfl->FSave(kctgFramework))
            _pcfl->SetTemp(fFalse);
        else
        {
            _rec = recFileError;
            _cno = cnoNil;
        }
    }
    _rs = rsNormal;
    ReleasePpo(&_pglcmdf);
    ReleasePpo(&_pcfl);

    PushCid(cidCexRecordDone, pvNil, pvNil, _rec, _cno);
}

/***************************************************************************
    Record a command.
***************************************************************************/
void CommandExecutionManager::RecordCmd(PCommand pcmd)
{
    AssertThis(0);
    AssertPo(pcmd, 0);
    Assert(_rs == rsRecording, "not recording");
    CommandFile cmdf;

    if (_rec != recNil)
        return;

    if (_cact > 0)
    {
        if (_cmd.pgg == pvNil && FEqualRgb(pcmd, &_cmd, size(_cmd)))
        {
            // commands are the same, just increment the _cact
            if (pcmd->cid < cidMinNoRepeat || pcmd->cid >= cidLimNoRepeat)
                _cact++;
            return;
        }

        // new command is not the same as the previous one
        if (_cact > 1)
        {
            // rewrite the previous one's _cact
            _pglcmdf->Get(_icmdf, &cmdf);
            cmdf.cact = _cact;
            _pglcmdf->Put(_icmdf, &cmdf);
        }

        // increment _icmdf
        _icmdf++;
        _cact = 0;
    }

    // fill in the cmdf and save it in the list
    cmdf.cid = pcmd->cid;
    cmdf.hid = pcmd->pcmh == pvNil ? hidNil : pcmd->pcmh->Hid();
    cmdf.cact = 1;
    cmdf.chidGg = pcmd->pgg != pvNil ? ++_chidLast : 0;
    CopyPb(pcmd->rglw, cmdf.rglw, kclwCmd * size(LegacyLong));

    if (!_pglcmdf->FInsert(_icmdf, &cmdf))
    {
        // out of memory
        _rec = recMemError;
        return;
    }

    // write the group and make it a child of the macro
    if (pvNil != pcmd->pgg)
    {
        DataBlock blck;
        LegacyLong cb;
        ChunkNumber cno;

        cb = pcmd->pgg->CbOnFile();
        if (!_pcfl->FAddChild(kctgMacro, _cno, cmdf.chidGg, cb, kctgGg, &cno, &blck))
        {
            goto LFileError;
        }
        if (!pcmd->pgg->FWrite(&blck))
        {
            _pcfl->DeleteChild(kctgMacro, _cno, kctgGg, cno, cmdf.chidGg);
        LFileError:
            _pglcmdf->Delete(_icmdf);
            _rec = recFileError;
            return;
        }
    }
    _cact = 1;
    _cmd = *pcmd;
}

/***************************************************************************
    Play back the command stream starting in the given pcfl with the given
    cno.
***************************************************************************/
void CommandExecutionManager::Play(PChunkyFile pcfl, ChunkNumber cno)
{
    AssertThis(0);
    AssertPo(pcfl, 0);
    DataBlock blck;
    short bo, osk;

    if (_rs != rsNormal)
    {
        Bug("already recording or playing");
        return;
    }

    _rs = rsPlaying;
    _rec = recNil;
    _pcfl = pcfl;
    _pcfl->AddRef();
    _cno = cno;
    _icmdf = 0;
    _cact = 0;
    Assert(_pglcmdf == pvNil, "why isn't _pglcmdf nil?");

    if (!_pcfl->FFind(kctgMacro, _cno, &blck) || (_pglcmdf = DynamicArray::PglRead(&blck, &bo, &osk)) == pvNil)
    {
        _rec = recFileError;
        StopPlaying();
    }
    else if (bo != kboCur || osk != koskCur)
    {
        _rec = recWrongPlatform;
        StopPlaying();
    }
}

/***************************************************************************
    Stop play back of a command stream. Pushes a command notifying the
    world that play back has stopped. The command (cidCexPlayDone) contains
    the error code (rec), and cno in the first two lw's of the command.
***************************************************************************/
void CommandExecutionManager::StopPlaying(void)
{
    AssertThis(0);

    if (_rs != rsPlaying)
        return;

    if (_rec == recNil && (_cact > 0 || _icmdf < _pglcmdf->IvMac()))
        _rec = recAbort;

    PushCid(cidCexPlayDone, pvNil, pvNil, _rec, _cno);

    _rs = rsNormal;
    ReleasePpo(&_pcfl);
    ReleasePpo(&_pglcmdf);
}

/***************************************************************************
    Read the next command.
***************************************************************************/
bool CommandExecutionManager::_FReadCmd(PCommand pcmd)
{
    AssertThis(0);
    AssertVarMem(pcmd);
    Assert(_rs == rsPlaying, "not playing a command stream");
    AssertPo(_pglcmdf, 0);
    CommandFile cmdf;

    if (_cact > 0)
    {
        // this command is being repeated
        *pcmd = _cmd;
        _cact--;
        return fTrue;
    }
    if (_icmdf >= _pglcmdf->IvMac())
        goto LStop;

    _pglcmdf->Get(_icmdf, &cmdf);

    ClearPb(pcmd, size(*pcmd));
    pcmd->cid = cmdf.cid;
    pcmd->pcmh = PcmhFromHid_Hook(cmdf.hid);
    pcmd->pgg = pvNil;
    CopyPb(cmdf.rglw, pcmd->rglw, kclwCmd * size(LegacyLong));

    if (cmdf.chidGg != 0)
    {
        DataBlock blck;
        ChildChunkIdentification kid;
        short bo, osk;

        Assert(cmdf.cact <= 1, 0);

        // read the gg
        if (!_pcfl->FGetKidChidCtg(kctgMacro, _cno, cmdf.chidGg, kctgGg, &kid) ||
            !_pcfl->FFind(kid.cki.ctg, kid.cki.cno, &blck) || pvNil == (pcmd->pgg = GeneralGroup::PggRead(&blck, &bo, &osk)))
        {
            _rec = recFileError;
            goto LStop;
        }
        if (bo != kboCur || osk != koskCur)
        {
            // don't know how to change byte order or translate strings
            ReleasePpo(&pcmd->pgg);
            _rec = recWrongPlatform;
            goto LStop;
        }
    }
    AssertPo(pcmd, 0);

    _icmdf++;
    if ((_cact = cmdf.cact - 1) > 0)
        _cmd = *pcmd;
    return fTrue;

    // error handling
LStop:
    StopPlaying();
    return fFalse;
}

/***************************************************************************
    Determine whether it's OK to communicate with the CommandHandler. Default is to
    return true iff there is no current modal gob or the cmh is not a gob
    or it is a gob in the tree of the modal gob.
***************************************************************************/
bool CommandExecutionManager::_FCmhOk(PCommandHandler pcmh)
{
    AssertNilOrPo(pcmh, 0);
    return _FCmhModalOk(pcmh);
}

/***************************************************************************
    Default modal check: headless / no modal context, so any cmh is
    reachable. Gui CEM subclass overrides with the _pgobModal ->
    kclsGraphicsObject ancestor walk (in cmd_gui.cpp).
***************************************************************************/
bool CommandExecutionManager::_FCmhModalOk(PCommandHandler pcmh)
{
    AssertNilOrPo(pcmh, 0);
    return fTrue;
}

/***************************************************************************
    Default OS key-queue pump: there is no OS event source, so no key.
    The gui CEM subclass overrides to call vpappb->FGetNextKeyFromOsQueue.
***************************************************************************/
bool CommandExecutionManager::_FGetKeyFromOs(PCommand pcmd)
{
    AssertVarMem(pcmd);
    TrashVar(pcmd);
    return fFalse;
}

/***************************************************************************
    Default tracking-mouse synthesis: when the cmd queue is empty and a
    gob is tracking the mouse, headless has nowhere to read the cursor
    from. Return tNo so the dispatcher behaves as if the queue is just
    plain empty. Gui override fills _cmdCur with a cidTrackMouse via
    vpappb->TrackMouse + GrfcustCur + FForeground.
***************************************************************************/
tribool CommandExecutionManager::_TGetTrackingMouseCmd(void)
{
    return tNo;
}

/***************************************************************************
    Default modal-rejection notification: no-op. Gui override calls
    vpappb->BadModalCmd to play the system "no" sound.
***************************************************************************/
void CommandExecutionManager::_BadModalCmd(PCommand pcmd)
{
    AssertVarMem(pcmd);
}

/***************************************************************************
    Add a command handler to the filter list. These command handlers get
    a crack at every command whether or not it is for them. grfcmm
    determines which targets the handler will see commands for (as in
    command map entries). The cmhl is a command handler level - indicating
    the priority of the command handler. Handlers with lower cmhl values
    get first crack at commands. It is legal for a handler to be in the
    list more than once (even with the same cmhl value).
***************************************************************************/
bool CommandExecutionManager::FAddCmh(PCommandHandler pcmh, LegacyLong cmhl, LegacyUlong grfcmm)
{
    AssertThis(0);
    AssertPo(pcmh, 0);

    if (!(grfcmm & kgrfcmmAll))
    {
        Bug("why is grfcmm nil?");
        return fFalse;
    }
    if (!_FCmhOk(pcmh))
        return fFalse;
    DocumentStorageHost host(&_dispatch.filter_storage);
    const bool added = mm_commands_add_filter(&_dispatch, pcmh, cmhl, grfcmm) != 0;
    host.Rethrow();
    return added;
}

/***************************************************************************
    Removes the the handler (at the given cmhl level) from the handler list.
***************************************************************************/
void CommandExecutionManager::RemoveCmh(PCommandHandler pcmh, LegacyLong cmhl)
{
    AssertThis(0);
    AssertPo(pcmh, 0);
    mm_commands_remove_filter(&_dispatch, pcmh, cmhl);
}

/***************************************************************************
    Remove all references to the handler from the command dispatcher,
    including from the handler list and the command queue.
***************************************************************************/
void CommandExecutionManager::BuryCmh(PCommandHandler pcmh)
{
    AssertThis(0);
    Assert(pcmh != pvNil, 0);
    if (_cmdCur.pcmh == pcmh)
        _cmdCur.pcmh = pvNil;
    if (_cmd.pcmh == pcmh)
    {
        _cmd.pcmh = pvNil;
        _cact = 0;
    }
    DispatchHost host(this);
    mm_commands_bury(&_dispatch, pcmh, &host, DispatchHost::ReleaseGroup);
    host.Rethrow();
}

/***************************************************************************
    Adds a command to the tail of the queue.
***************************************************************************/
void CommandExecutionManager::EnqueueCid(LegacyLong cid, PCommandHandler pcmh, PGeneralGroup pgg, LegacyLong lw0, LegacyLong lw1, LegacyLong lw2, LegacyLong lw3)
{
    Assert(cid != cidNil, 0);
    AssertNilOrPo(pcmh, 0);
    AssertNilOrPo(pgg, 0);
    Command cmd;

    cmd.cid = cid;
    cmd.pcmh = pcmh;
    cmd.pgg = pgg;
    cmd.rglw[0] = lw0;
    cmd.rglw[1] = lw1;
    cmd.rglw[2] = lw2;
    cmd.rglw[3] = lw3;
    EnqueueCmd(&cmd);
}

/***************************************************************************
    Pushes a command onto the head of the queue.
***************************************************************************/
void CommandExecutionManager::PushCid(LegacyLong cid, PCommandHandler pcmh, PGeneralGroup pgg, LegacyLong lw0, LegacyLong lw1, LegacyLong lw2, LegacyLong lw3)
{
    Assert(cid != cidNil, 0);
    AssertNilOrPo(pcmh, 0);
    AssertNilOrPo(pgg, 0);
    Command cmd;

    cmd.cid = cid;
    cmd.pcmh = pcmh;
    cmd.pgg = pgg;
    cmd.rglw[0] = lw0;
    cmd.rglw[1] = lw1;
    cmd.rglw[2] = lw2;
    cmd.rglw[3] = lw3;
    PushCmd(&cmd);
}

/***************************************************************************
    Adds a command to the tail of the queue. This asserts if it can't add
    it to the queue. Clients should make sure that the value of ccmdInit
    passed to PcexNew is large enough to handle the busiest session.
***************************************************************************/
void CommandExecutionManager::EnqueueCmd(PCommand pcmd)
{
    _QueueCommand(pcmd, false);
}

/***************************************************************************
    Pushes a command onto the head of the queue. This asserts if it can't
    add it to the queue. Clients should make sure that the value of ccmdInit
    passed to PcexNew is large enough to handle the busiest session.
***************************************************************************/
void CommandExecutionManager::PushCmd(PCommand pcmd)
{
    _QueueCommand(pcmd, true);
}

void CommandExecutionManager::_QueueCommand(PCommand pcmd, bool atHead)
{
    AssertThis(0);
    AssertPo(pcmd, 0);
    Assert(pcmd->cid != cidNil, "why enqueue a nil command?");

    const auto record = QueueRecord(*pcmd);
    {
        DocumentStorageHost host(&_dispatch.queue_storage);
        const bool added = mm_commands_insert(&_dispatch, &record, atHead) != 0;
        host.Rethrow();
        if (added)
            return;
    }
    Bug("event queue not big enough");
    ReleasePpo(&pcmd->pgg);
}

/***************************************************************************
    Checks if a cid is in the queue.
***************************************************************************/
bool CommandExecutionManager::FCidIn(LegacyLong cid)
{
    AssertThis(0);
    Assert(cid != cidNil, "why check for a nil command?");
    return mm_commands_contains(&_dispatch, cid) != 0;
}

/***************************************************************************
    Flushes all instances of a cid in the queue.
***************************************************************************/
void CommandExecutionManager::FlushCid(LegacyLong cid)
{
    AssertThis(0);
    Assert(cid != cidNil, "why flush a nil command?");
    DispatchHost host(this);
    mm_commands_flush(&_dispatch, cid, &host, DispatchHost::ReleaseGroup);
    host.Rethrow();
}

/***************************************************************************
    Get the next command to be dispatched (put it in _cmdCur). Return tYes
    if there was a command and it should be dispatched. Return tNo if there
    wasn't a command (if the system queue should be checked). Return tMaybe
    if the command shouldn't be dispatched, but we shouldn't check the
    system queue.
***************************************************************************/
tribool CommandExecutionManager::_TGetNextCmd(void)
{
    AssertThis(0);

    // get the next command from the command stream
    MMQueuedCommand next;
    if (mm_commands_take(&_dispatch, &next))
        _cmdCur = NativeCommand(next);
    else
    {
        ClearPb(&_cmdCur, size(_cmdCur));
        if (pvNil == _pgobTrack)
            return tNo;

        /* Synthesize a tracking-mouse command. Headless default returns
           tNo; gui override fills _cmdCur via vpappb. */
        tribool tRet = _TGetTrackingMouseCmd();
        if (tYes != tRet)
            return tRet;
    }
    AssertPo(&_cmdCur, 0);

    // handle playing and recording
    if (rsPlaying == _rs)
    {
        // We're playing back. Throw away the incoming command and play
        // one from the stream.
        ReleasePpo(&_cmdCur.pgg);

        // let a cidCexStopPlay go through and handle it at the end.
        // this is so a cmh can intercept it.
        if (_cmdCur.cid != cidCexStopPlay && !_FReadCmd(&_cmdCur))
            return tMaybe;
    }

    if (!_FCmhOk(_cmdCur.pcmh))
    {
        _BadModalCmd(&_cmdCur);
        ReleasePpo(&_cmdCur.pgg);
        return tMaybe;
    }

    return tYes;
}

/***************************************************************************
    Send the command (_cmdCur) to the given command handler.
***************************************************************************/
bool CommandExecutionManager::_FSendCmd(PCommandHandler pcmh)
{
    AssertPo(pcmh, 0);

    if (!_FCmhOk(pcmh))
        return fFalse;

    return pcmh->FDoCmd(&_cmdCur);
}

/***************************************************************************
    Handle post processing on the command - record it if we're recording,
    free the pgg, etc.
***************************************************************************/
void CommandExecutionManager::_CleanUpCmd(void)
{
    // If the handler went away during command dispatching, we should
    // have heard about it (via BuryCmh) and should have set _cmdCur.pcmh
    // to nil.
    AssertNilOrPo(_cmdCur.pcmh, 0);

    // record the command after dispatching, in case arguments got added
    // or the cid was set to nil.
    if (rsRecording == _rs && !FIn(_cmdCur.cid, cidMinNoRecord, cidLimNoRecord) && cidNil != _cmdCur.cid &&
        cidCexStopRec != _cmdCur.cid)
    {
        RecordCmd(&_cmdCur);
    }

    // check for a stop record or stop play command
    if (rsNormal != _rs)
    {
        if (cidCexStopPlay == _cmdCur.cid && rsPlaying == _rs)
            StopPlaying();
        else if (cidCexStopRec == _cmdCur.cid && rsRecording == _rs)
            StopRecording();
    }

    ReleasePpo(&_cmdCur.pgg);
}

/***************************************************************************
    If there is a command in the queue, this dispatches it and returns
    true. If there aren't any commands in the queue, it simply returns
    false. If a gob is tracking the mouse and the queue is empty, a
    cidTrackMouse command is generated and dispatched to the gob.

    NOTE: care has to be taken here because a CommandHandler may go away while
    dispatching the command. BuryCmh can clear the current target,
    and the C dispatcher adjusts its filter cursor.
***************************************************************************/
bool CommandExecutionManager::FDispatchNextCmd(void)
{
    AssertThis(0);
    if (_dispatch.dispatching)
    {
        Bug("recursing into FDispatchNextCmd!");
        return fFalse;
    }
    DispatchHost host(this, &_cmdCur);
    const MMCommandRoute route = {DispatchHost::Prepare, DispatchHost::Target, DispatchHost::Send,
                                  DispatchHost::Finish};
    const int result = mm_commands_route(&_dispatch, &host, &route);
    if (result < 0)
        ReleasePpo(&_cmdCur.pgg);
    host.Rethrow();
    return result > 0;
}

/***************************************************************************
    Give the handler a crack at enabling/disabling the command.
***************************************************************************/
bool CommandExecutionManager::_FEnableCmd(PCommandHandler pcmh, PCommand pcmd, LegacyUlong *pgrfeds)
{
    AssertPo(pcmh, 0);
    AssertPo(pcmd, 0);
    AssertVarMem(pgrfeds);

    if (!_FCmhOk(pcmh))
        return fFalse;

    return pcmh->FEnableCmd(pcmd, pgrfeds);
}

/***************************************************************************
    Determines whether the given command is currently enabled. This is
    normally used for menu graying/checking etc and toolbar enabling/status.
***************************************************************************/
LegacyUlong CommandExecutionManager::GrfedsForCmd(PCommand pcmd)
{
    AssertThis(0);
    AssertPo(pcmd, 0);
    DispatchHost host(this, pcmd);
    const MMCommandEnable query = {DispatchHost::Target, DispatchHost::Enable};
    MMU32 flags;
    const int handled = mm_commands_enable(&_dispatch, &host, &query, &flags);
    host.Rethrow();
    if (handled > 0)
        return flags;
    if ((pcmd->cid == cidCexStopRec && _rs == rsRecording) ||
        (pcmd->cid == cidCexStopPlay && _rs == rsPlaying))
        return fedsEnable;
    return fedsDisable;
}

/***************************************************************************
    Determines whether the given command is currently enabled. This is
    normally used for menu graying/checking etc and toolbar enabling/status.
***************************************************************************/
LegacyUlong CommandExecutionManager::GrfedsForCid(LegacyLong cid, PCommandHandler pcmh, PGeneralGroup pgg, LegacyLong lw0, LegacyLong lw1, LegacyLong lw2, LegacyLong lw3)
{
    AssertThis(0);
    Assert(cid != cidNil, 0);
    AssertNilOrPo(pcmh, 0);
    AssertNilOrPo(pgg, 0);
    Command cmd;

    cmd.cid = cid;
    cmd.pcmh = pcmh;
    cmd.pgg = pgg;
    cmd.rglw[0] = lw0;
    cmd.rglw[1] = lw1;
    cmd.rglw[2] = lw2;
    cmd.rglw[3] = lw3;
    return GrfedsForCmd(&cmd);
}

/***************************************************************************
    See if the next command is a key command and if so, put it in *pcmd
    (and remove it from the queue).
***************************************************************************/
bool CommandExecutionManager::FGetNextKey(PCommand pcmd)
{
    AssertThis(0);
    AssertVarMem(pcmd);
    if (_rs == rsNormal)
    {
        MMQueuedCommand key;
        const int status = mm_commands_take_key(&_dispatch, cidKey, &key);
        if (status > 0)
        {
            *pcmd = NativeCommand(key);
            return fTrue;
        }
        if (status == 0)
            return _FGetKeyFromOs(pcmd);
    }
    TrashVar(pcmd);
    return fFalse;
}

/***************************************************************************
    Mouse tracking + modal-gob API: no-op defaults on the base CEM.
    The gui CEM subclass overrides these in cmd_gui.cpp to manipulate
    Win32 capture state and the gob hierarchy. Headless tests / future
    kauai-core consumers see no-ops, which is the right semantics for
    them since they have no mouse and no GraphicsObject hierarchy.
***************************************************************************/
void CommandExecutionManager::TrackMouse(PGraphicsObject pgob)
{
    AssertThis(0);
}
void CommandExecutionManager::EndMouseTracking(void)
{
    AssertThis(0);
}
PGraphicsObject CommandExecutionManager::PgobTracking(void)
{
    AssertThis(0);
    return pvNil;
}
void CommandExecutionManager::Suspend(bool fSuspend)
{
    AssertThis(0);
}
void CommandExecutionManager::SetModalGob(PGraphicsObject pgob)
{
    AssertThis(0);
}

#ifdef DEBUG
/***************************************************************************
    Assert the validity of the command dispatcher
***************************************************************************/
void CommandExecutionManager::AssertValid(LegacyUlong grf)
{
    CommandExecutionManager_PAR::AssertValid(fobjAllocated);
    Assert(_dispatch.queue.storage == &_dispatch.queue_storage, "invalid command queue");
    Assert(_dispatch.filters.storage == &_dispatch.filter_storage, "invalid command filters");
    AssertNilOrPo(_pglcmdf, 0);
    AssertNilOrPo(_pcfl, 0);
    AssertNilOrPo(_cmdCur.pgg, 0);
}

/***************************************************************************
    Mark the memory associated with the command dispatcher.
***************************************************************************/
void CommandExecutionManager::MarkMem(void)
{
    AssertThis(0);
    CommandExecutionManager_PAR::MarkMem();
    MarkPv(_dispatch.queue_storage.buffers[0].data);
    MarkPv(_dispatch.filter_storage.buffers[0].data);
    MarkMemObj(_pglcmdf);
    MarkMemObj(_cmdCur.pgg);

    MMQueuedCommand command;
    for (MMU32 i = _dispatch.queue.count; i-- != 0;)
    {
        AssertDo(mm_commands_at(&_dispatch, i, &command), "missing queued command");
        MarkMemObj(static_cast<PGeneralGroup>(command.group));
    }
}
#endif // DEBUG
