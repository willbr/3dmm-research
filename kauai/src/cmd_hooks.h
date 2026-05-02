/* Copyright (c) Microsoft Corporation.
   Licensed under the MIT License. */

/***************************************************************************
    cmd_hooks.h -- function-pointer seam between the bare CEM/CommandHandler
    machinery (destined for kauai-core) and the gui-side ApplicationBase
    that owns hid registration and modal-gob tracking.

    Background: kauai/src/cmd.cpp historically calls into vpappb directly
    for hid -> CommandHandler resolution and for cleanup-on-destruct
    (BuryCmh). That coupling forces cmd.cpp to live in kauai (gui side)
    and forces every CommandHandler subclass (Clock, MovieSoundQueue,
    Movie, Scene, ...) into the gui-side libs. The seam declared here
    breaks that coupling: cmd code calls PcmhFromHid_Hook() / BuryCmh_Hook(),
    which forward to function pointers installed by appb._FInit. Headless
    tests / kauai-core consumers that never bring up ApplicationBase get
    nil/no-op defaults, which is the correct behavior for them.

    This is the first step of the cmd_core split planned in
    docs/superpowers/plans/2026-05-02-cmd-core-split.md.
***************************************************************************/
#ifndef CMD_HOOKS_H
#define CMD_HOOKS_H

class CommandHandler;
typedef CommandHandler *PCommandHandler;

/* Resolve a registered handler-id to its CommandHandler. Default (no hook
   installed) returns nil -- caller treats nil as "hid not in use". */
typedef PCommandHandler (*PFN_PcmhFromHid)(long hid);

/* Remove all references the gui side holds to a CommandHandler that's
   about to be destroyed (modal stack, mouse-tracking gob, etc).
   Default (no hook installed) is a no-op -- correct for headless. */
typedef void (*PFN_BuryCmh)(PCommandHandler pcmh);

/* Installer used by gui-side appb._FInit. */
void SetCmdHooks(PFN_PcmhFromHid pfnPcmhFromHid, PFN_BuryCmh pfnBuryCmh);

/* Internal accessors used by cmd.cpp. Forward-declared here so cmd.cpp
   doesn't have to see appb.h once it moves to kauai-core. */
PCommandHandler PcmhFromHid_Hook(long hid);
void BuryCmh_Hook(PCommandHandler pcmh);

#endif // !CMD_HOOKS_H
