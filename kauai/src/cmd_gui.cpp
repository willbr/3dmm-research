/* Copyright (c) Microsoft Corporation.
   Licensed under the MIT License. */

/***************************************************************************
    cmd_gui.cpp -- gui-side overrides for the seam virtuals on
    CommandExecutionManager. These bodies pull in vpappb / GraphicsObject
    refs that cmd.cpp itself can't have once it lives in kauai-core.

    Lives in kauai (gui) target. Studio's appb._FInit constructs a
    GuiCommandExecutionManager via PgcexNew so the dispatcher's
    polymorphic dispatch reaches these overrides.

    Part of the cmd_core split planned in
    docs/superpowers/plans/2026-05-02-cmd-core-split.md (Task 3).
***************************************************************************/
#include "frame.h"
ASSERTNAME

RTCLASS(GuiCommandExecutionManager)

/***************************************************************************
    Construct a gui-side CEM. Same init as the base class -- the subclass
    only overrides behavior, not data layout.
***************************************************************************/
PGuiCommandExecutionManager GuiCommandExecutionManager::PgcexNew(long ccmdInit, long ccmhInit)
{
    PGuiCommandExecutionManager pgcex;

    pgcex = NewObj GuiCommandExecutionManager;
    if (pvNil == pgcex)
        return pvNil;

    if (!pgcex->_FInit(ccmdInit, ccmhInit))
    {
        ReleasePpo(&pgcex);
        return pvNil;
    }

    AssertPo(pgcex, 0);
    return pgcex;
}

/***************************************************************************
    OS key-queue pump: delegate to the platform layer.
***************************************************************************/
bool GuiCommandExecutionManager::_FGetKeyFromOs(PCommand pcmd)
{
    AssertVarMem(pcmd);
    return vpappb->FGetNextKeyFromOsQueue((PCMD_KEY)pcmd);
}

/***************************************************************************
    Synthesize a tracking-mouse command when the queue is empty and a
    gob is tracking. This is the body that used to live inline in
    CommandExecutionManager::_TGetNextCmd.
***************************************************************************/
tribool GuiCommandExecutionManager::_TGetTrackingMouseCmd(void)
{
    AssertThis(0);
    AssertPo(_pgobTrack, 0);

    PT pt;
    PCMD_MOUSE pcmd = (PCMD_MOUSE)&_cmdCur;

    _cmdCur.pcmh = _pgobTrack;
    _cmdCur.cid = cidTrackMouse;
    vpappb->TrackMouse(_pgobTrack, &pt);
    pcmd->xp = pt.xp;
    pcmd->yp = pt.yp;
    pcmd->grfcust = vpappb->GrfcustCur();

    if (!vpappb->FForeground())
    {
        // if we're not in the foreground, toggle the state of fcustMouse
        // repeatedly so most clients stop tracking. (Same comment as the
        // original _TGetNextCmd body.)
        static bool _fDown;

        _fDown = !_fDown;
        if (!_fDown)
            pcmd->grfcust ^= fcustMouse;
        else
            pcmd->grfcust &= ~fcustMouse;
    }
    return tYes;
}

/***************************************************************************
    Notify the user that a command was rejected because a modal dialog is
    up. (System "no" beep / shake.)
***************************************************************************/
void GuiCommandExecutionManager::_BadModalCmd(PCommand pcmd)
{
    AssertVarMem(pcmd);
    vpappb->BadModalCmd(pcmd);
}

/***************************************************************************
    Modal-gob walk: only allow commands targeting the modal gob's
    subtree. Pulled out of CommandExecutionManager::_FCmhOk by the
    cmd_core split because it dereferences PGraphicsObject (FIs +
    PgobPar) which the base TU doesn't have visibility for.
***************************************************************************/
bool GuiCommandExecutionManager::_FCmhModalOk(PCommandHandler pcmh)
{
    AssertNilOrPo(pcmh, 0);
    PGraphicsObject pgob;

    if (pvNil == _pgobModal || pvNil == pcmh || !pcmh->FIs(kclsGraphicsObject))
        return fTrue;

    for (pgob = (PGraphicsObject)pcmh; pgob != _pgobModal; pgob = pgob->PgobPar())
    {
        if (pvNil == pgob)
            return fFalse;
    }

    return fTrue;
}

/***************************************************************************
    Mouse tracking + modal-gob API: gui implementations. The base CEM
    has no-op defaults; these overrides do the real work via Win32
    capture APIs and PGraphicsObject dereferencing.
***************************************************************************/
void GuiCommandExecutionManager::TrackMouse(PGraphicsObject pgob)
{
    AssertThis(0);
    AssertPo(pgob, 0);
    Assert(_pgobTrack == pvNil, "some other gob is already tracking the mouse");

    _pgobTrack = pgob;
#ifdef WIN
    _hwndCapture = pgob->HwndContainer();
    SetCapture(_hwndCapture);
#endif // WIN
}

void GuiCommandExecutionManager::EndMouseTracking(void)
{
    AssertThis(0);

#ifdef WIN
    if (pvNil != _pgobTrack)
    {
        if (hNil != _hwndCapture && GetCapture() == _hwndCapture)
            ReleaseCapture();
        _hwndCapture = hNil;
    }
#endif // WIN
    _pgobTrack = pvNil;
}

PGraphicsObject GuiCommandExecutionManager::PgobTracking(void)
{
    AssertThis(0);
    return _pgobTrack;
}

void GuiCommandExecutionManager::Suspend(bool fSuspend)
{
    AssertThis(0);

#ifdef WIN
    if (pvNil == _pgobTrack || hNil == _hwndCapture)
        return;

    if (fSuspend && GetCapture() == _hwndCapture)
        ReleaseCapture();
    else if (!fSuspend && GetCapture() != _hwndCapture)
        SetCapture(_hwndCapture);
#endif // WIN
}

void GuiCommandExecutionManager::SetModalGob(PGraphicsObject pgob)
{
    AssertThis(0);
    AssertNilOrPo(pgob, 0);

    _pgobModal = pgob;
}
