# cmd.cpp split: extract CommandHandler/CEM core to kauai-core

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Move the bare `CommandHandler` + `CommandExecutionManager` (CEM) machinery from `kauai/src/cmd.cpp` (currently in kauai-gui) into a new `kauai/src/cmd_core.cpp` in kauai-core, keeping the gui-coupled parts (mouse synth, modal-gob, key-event translation, record/play) on the gui side. This unblocks promoting `clok.cpp`, `msnd.cpp`, and eventually `scene.cpp` / `tbox.cpp` / `movie.cpp` to `*-core` libraries.

**Architecture:** Function-pointer hooks (`Pcmh*Hook`) declared in `kauai-core` and installed by `appb._FInit` on the gui side. The 3-4 places where the core CEM/CommandHandler call into `vpappb` get replaced by hook-call-with-default-no-op. The gui-only methods (`TrackMouse`, `SetModalGob`, recording playback, mouse synthesis in `_TGetNextCmd`) move to a new `kauai/src/cmd_gui.cpp` that stays in kauai-gui.

**Tech Stack:** C++ (kauai's pre-C++11 dialect). MSVC x86 + x64. Static lib targets defined in `CMakeLists.txt`.

---

## Why this isn't just file-promotion

Every other file we've moved to `*-core` was a pure cut — no symbol changes. `cmd.cpp` is different: the boundary doesn't fall along file lines. The base classes (`CommandHandler`, `CommandExecutionManager`) reach into gui globals (`vpappb->PcmhFromHid`, `vpappb->BuryCmh`, modal-gob walk, mouse tracking, key event source). To split on lib boundary we need to *introduce a seam* — function-pointer hooks the gui side fills in.

Without the seam: `clok.cpp`/`msnd.cpp` (and any future engine-core promotion of `movie/scene/tbox`) cannot live in `*-core` because they inherit from `CommandHandler`, which forces the entire `cmd.cpp` TU into the link, which drags `vpappb` references in.

## Audit: gui-coupling sites in cmd.cpp

(Line numbers as of `bf95ade`.)

| # | Function | Lines | Coupling | Resolution |
|---|---|---|---|---|
| 1 | `CommandHandler::HidUnique` | 63-78 | `vpappb->PcmhFromHid` for uniqueness | hook `pfnPcmhFromHid` (default returns nil → unique) |
| 2 | `CommandHandler::~CommandHandler` | 102-103 | `vpappb->BuryCmh(this)` | hook `pfnBuryCmh` (default no-op) |
| 3 | `_FReadCmd` (record/play) | 545 | `vpappb->PcmhFromHid` to resolve hid | same hook (1) |
| 4 | `_FCmhOk` (modal-gob walk) | 593-598 | `_pgobModal` + `kclsGraphicsObject` | virtual `_FCmhModalOk(pcmh)` → default returns true (no modal context) |
| 5 | `_TGetNextCmd` (mouse synth, foreground) | 902-941 | `vpappb->TrackMouse`, `GrfcustCur`, `FForeground`, `BadModalCmd` | move whole method body into `cmd_gui.cpp`; stub in core just returns next-from-queue |
| 6 | `FGetNextKey` | 1146-1168 | `vpappb->FGetNextKeyFromOsQueue` | virtual `_FGetKeyFromOs(pcmd)` → default returns false |
| 7 | `TrackMouse / EndMouseTracking / PgobTracking / SetModalGob` | 1174-1248 | `PGraphicsObject _pgobModal`, `_pgobTrack` | move to `cmd_gui.cpp` (kauai-gui). Core declares `_pgobModal/_pgobTrack` slots so the linker can resolve, but core never reads/writes them. |

The record/play paths (`Record`, `StopRecording`, `RecordCmd`, `Play`, `StopPlaying`) only touch chunky-file I/O — no `vpappb`, no GraphicsObject. They can stay in core.

---

## Files

- **Create:** `kauai/src/cmd_core.cpp` — Command, CommandHandler, CEM minus the gui-only methods (5, 7) and with hook calls for (1)(2)(3)(4)(6).
- **Create:** `kauai/src/cmd_gui.cpp` — gui-side mouse/modal/key code (5)(7).
- **Create:** `kauai/src/cmd_hooks.h` — hook function-pointer typedefs + setter declarations.
- **Modify:** `kauai/src/cmd.h` — add `_FCmhModalOk`/`_FGetKeyFromOs` virtuals (default impls in core); declare hook setters.
- **Modify:** `kauai/src/cmd.cpp` — **delete** (replaced by cmd_core + cmd_gui).
- **Modify:** `kauai/src/appb.cpp` — in `_FInit`, install the four hooks (PcmhFromHid, BuryCmh, derive a CEM subclass that overrides the modal/key virtuals, instantiate it as `vpcex`).
- **Modify:** `CMakeLists.txt` — add cmd_core.cpp to kauai-core sources; add cmd_gui.cpp to kauai sources; remove cmd.cpp.

---

## Tasks

### Task 1: Hook header + setter scaffolding (no behavior change)

**Files:**
- Create: `kauai/src/cmd_hooks.h`
- Modify: `kauai/src/cmd.cpp:63-103` (HidUnique, ~CommandHandler)
- Modify: `kauai/src/appb.cpp:_FInit`

- [ ] **Step 1: Add cmd_hooks.h with typedefs**

```cpp
// kauai/src/cmd_hooks.h
#ifndef CMD_HOOKS_H
#define CMD_HOOKS_H

class CommandHandler;
typedef CommandHandler *PCommandHandler;

// PcmhFromHid: resolve a registered handler id to its CommandHandler.
// Default (nil) returns nil — caller treats nil as "hid not in use".
typedef PCommandHandler (*PFN_PcmhFromHid)(long hid);

// BuryCmh: remove all references to a CommandHandler (e.g. modal stack,
// tracking gob, etc). Default (nil) is a no-op — fine for headless tests.
typedef void (*PFN_BuryCmh)(PCommandHandler pcmh);

// Setters used by the gui-side appb._FInit. core consumers don't need them.
void SetCmdHooks(PFN_PcmhFromHid pfnPcmhFromHid, PFN_BuryCmh pfnBuryCmh);

// Internal accessors used by cmd_core (forward-declared so cmd_core
// doesn't need to see appb.h).
PCommandHandler PcmhFromHid_Hook(long hid);
void BuryCmh_Hook(PCommandHandler pcmh);

#endif
```

- [ ] **Step 2: Implement the hook plumbing in cmd.cpp (still in gui side, just the seam)**

In `kauai/src/cmd.cpp`, near the top after `#include "frame.h"`:

```cpp
#include "cmd_hooks.h"

static PFN_PcmhFromHid s_pfnPcmhFromHid = pvNil;
static PFN_BuryCmh s_pfnBuryCmh = pvNil;

void SetCmdHooks(PFN_PcmhFromHid pfn1, PFN_BuryCmh pfn2)
{
    s_pfnPcmhFromHid = pfn1;
    s_pfnBuryCmh = pfn2;
}
PCommandHandler PcmhFromHid_Hook(long hid)
{
    return s_pfnPcmhFromHid ? s_pfnPcmhFromHid(hid) : pvNil;
}
void BuryCmh_Hook(PCommandHandler pcmh)
{
    if (s_pfnBuryCmh) s_pfnBuryCmh(pcmh);
}
```

- [ ] **Step 3: Replace direct `vpappb->PcmhFromHid` and `vpappb->BuryCmh` calls in cmd.cpp**

Three sites:
- `cmd.cpp:78` `if (pvNil != vpappb->PcmhFromHid(_hidLast))` → `if (pvNil != PcmhFromHid_Hook(_hidLast))`
- `cmd.cpp:102-103` `if (pvNil != vpappb) vpappb->BuryCmh(this);` → `BuryCmh_Hook(this);`
- `cmd.cpp:545` `pcmd->pcmh = vpappb->PcmhFromHid(cmdf.hid);` → `pcmd->pcmh = PcmhFromHid_Hook(cmdf.hid);`

- [ ] **Step 4: Install hooks in appb._FInit**

In `kauai/src/appb.cpp`, near where `vpcex = CommandExecutionManager::PcexNew(20, 20)` is called:

```cpp
SetCmdHooks(
    [](long hid) -> PCommandHandler { return vpappb->PcmhFromHid(hid); },
    [](PCommandHandler pcmh) { if (vpappb) vpappb->BuryCmh(pcmh); }
);
```

(C++03 compat: if lambdas can't compile here, define two free functions and pass their addresses.)

- [ ] **Step 5: Build studio + run regression**

```
pushvc (Invoke-VCVars -TargetArch x86 -HostArch AMD64); cmake --build build
cmake --build build --target movie-save-load-test && build\movie-save-load-test.exe
```

Expected: clean build, save-load test passes (it already does — this commit is no-behavior-change scaffolding).

- [ ] **Step 6: Commit**

```
git add kauai/src/cmd_hooks.h kauai/src/cmd.cpp kauai/src/appb.cpp
git commit -m "kauai: introduce cmd hooks seam (no behavior change)

Replace direct vpappb->PcmhFromHid/BuryCmh calls in cmd.cpp with
function-pointer hooks installed by appb._FInit. Same behavior, but
the seam means cmd.cpp can later move to kauai-core without dragging
in vpappb. Part 1/3 of the cmd-core split (see plan doc)."
```

### Task 2: Make `_FCmhOk` and `FGetNextKey` virtual; default impls in core-friendly form

**Files:**
- Modify: `kauai/src/cmd.h` (add two new protected virtuals on CEM)
- Modify: `kauai/src/cmd.cpp` (split _FCmhOk + FGetNextKey)

- [ ] **Step 1: Add virtuals in cmd.h on CommandExecutionManager**

```cpp
protected:
    // Default returns fTrue (no modal context). Gui override walks
    // _pgobModal -> kclsGraphicsObject hierarchy.
    virtual bool _FCmhModalOk(PCommandHandler pcmh);
    // Default returns fFalse (no OS key queue). Gui override delegates
    // to vpappb->FGetNextKeyFromOsQueue.
    virtual bool _FGetKeyFromOs(PCommand pcmd);
```

- [ ] **Step 2: Implement defaults in cmd.cpp (above the call sites that currently inline this logic)**

```cpp
bool CommandExecutionManager::_FCmhModalOk(PCommandHandler pcmh)
{
    return fTrue;   // headless / no-modal default
}
bool CommandExecutionManager::_FGetKeyFromOs(PCommand pcmd)
{
    return fFalse;  // no OS event queue
}
```

- [ ] **Step 3: Replace inline gui logic at the call sites with virtual calls**

`_FCmhOk` (line 593-598): replace the `_pgobModal`/`kclsGraphicsObject`/`PgobPar` walk with `return _FCmhModalOk(pcmh);`. Move the existing implementation into a future `_FCmhModalOk` override (Task 4).

`FGetNextKey` (line 1168): replace `return vpappb->FGetNextKeyFromOsQueue((PCMD_KEY)pcmd);` with `return _FGetKeyFromOs(pcmd);`. Move the existing call into a future `_FGetKeyFromOs` override (Task 4).

- [ ] **Step 4: Build + smoke-test studio still launches and dispatches commands**

Manual smoke: build, run `dist-x64\3dmovie.exe`, click around. Modal dialogs should still gate non-modal commands.

- [ ] **Step 5: Commit**

```
git commit -m "kauai: virtualize _FCmhOk modal check + FGetNextKey OS pump

Default impls return permissive (true / false) so a kauai-core CEM
with no UI works headless. Gui CEM will override these in Task 4
to restore full behavior. No user-visible change yet."
```

### Task 3: Extract gui-only methods into cmd_gui.cpp

**Files:**
- Create: `kauai/src/cmd_gui.cpp`
- Modify: `kauai/src/cmd.cpp` (delete the migrated methods)
- Modify: `kauai/src/cmd.h` (declare a `GuiCommandExecutionManager` subclass)
- Modify: `kauai/src/appb.cpp` (instantiate `GuiCommandExecutionManager` instead of base)
- Modify: `CMakeLists.txt` (add cmd_gui.cpp to kauai target)

- [ ] **Step 1: Add subclass declaration in cmd.h**

```cpp
class GuiCommandExecutionManager : public CommandExecutionManager
{
  protected:
    PGraphicsObject _pgobModal;   // moved from base
    PGraphicsObject _pgobTrack;   // moved from base
    virtual bool _FCmhModalOk(PCommandHandler pcmh) override;
    virtual bool _FGetKeyFromOs(PCommand pcmd) override;
  public:
    static PGuiCommandExecutionManager PgcexNew(long ccmdInit, long ccmhInit);
    void TrackMouse(PGraphicsObject pgob);
    void EndMouseTracking(void);
    PGraphicsObject PgobTracking(void);
    void SetModalGob(PGraphicsObject pgob);
    // mouse-synth path that used to live in _TGetNextCmd
    virtual tribool _TGetNextCmd(void) override;
};
```

- [ ] **Step 2: Move bodies into cmd_gui.cpp**

Files to migrate (from cmd.cpp):
- `TrackMouse` (line 1174)
- `EndMouseTracking`
- `PgobTracking` (line 1208)
- `SetModalGob` (line 1237)
- The mouse-synth and `BadModalCmd` branches inside `_TGetNextCmd` (line 902-941). Override `_TGetNextCmd` in subclass: do mouse stuff, then `return CommandExecutionManager::_TGetNextCmd();`. (Or the inverse — base does the queue read, subclass injects synthetic mouse cmds beforehand.)
- The override of `_FCmhModalOk` that does the modal-gob walk (uses `_pgobModal` member moved to subclass).
- The override of `_FGetKeyFromOs` that calls `vpappb->FGetNextKeyFromOsQueue`.

- [ ] **Step 3: Wire CMake**

In `CMakeLists.txt`, on the `kauai` target add:

```cmake
"${PROJECT_SOURCE_DIR}/kauai/src/cmd_gui.cpp"
```

- [ ] **Step 4: Replace `vpcex = CommandExecutionManager::PcexNew(...)` with `GuiCommandExecutionManager::PgcexNew(...)` in appb.cpp**

- [ ] **Step 5: Full build (x86) + studio smoke test**

```
cmake --build build
dist\3dmovie.exe
```

Verify: app starts, menus open, modal File-Open dialog still blocks main-window commands, mouse-track still drives drag operations.

- [ ] **Step 6: Commit**

```
git commit -m "kauai: split mouse/modal/key code into cmd_gui.cpp

cmd.cpp now contains only the headless-safe CEM core. The gui
GuiCommandExecutionManager subclass owns _pgobModal/_pgobTrack,
mouse-synth, OS key pumping, and the public TrackMouse / SetModalGob
API. Behavior is unchanged for the studio. Sets up cmd.cpp -> cmd_core.cpp
rename in Task 4."
```

### Task 4: Move cmd.cpp to kauai-core (the actual promotion)

**Files:**
- Rename: `kauai/src/cmd.cpp` -> `kauai/src/cmd_core.cpp`
- Modify: `CMakeLists.txt` (move from kauai sources to kauai-core sources)

- [ ] **Step 1: git mv**

```
git mv kauai/src/cmd.cpp kauai/src/cmd_core.cpp
```

- [ ] **Step 2: Update CMakeLists.txt**

Move the `${PROJECT_SOURCE_DIR}/kauai/src/cmd.cpp` line out of the `kauai` target's `target_sources` into the `kauai-core` target's `target_sources`, with the new filename.

Verify cmd_hooks.h and the GuiCommandExecutionManager subclass compile cleanly with the new arrangement (cmd_hooks.h is included from cmd_core.cpp; cmd_gui.cpp #includes "cmd.h" which already declares the subclass).

- [ ] **Step 3: Audit kauai-core for any lingering vpappb/PGraphicsObject ref**

```
grep -nE "vpappb|PGraphicsObject\b|GraphicsObject\b|kclsGraphicsObject" kauai/src/cmd_core.cpp
```

Expected: zero. If any remain, they're sites missed in Tasks 1-3 and must be fixed before this step lands.

- [ ] **Step 4: Build x86 + x64**

```
pushvc (Invoke-VCVars -TargetArch x86 -HostArch AMD64); cmake --build build
pushvc (Invoke-VCVars -TargetArch x64 -HostArch AMD64); cmake --build build-x64
```

Both must succeed. movie-save-load-test still passes on both.

- [ ] **Step 5: Commit**

```
git commit -m "build: promote cmd_core.cpp to kauai-core

The CommandHandler base + bare CEM machinery now lives in kauai-core.
clok.cpp + msnd.cpp + future engine-core consumers can now link them
without dragging in vpappb / GraphicsObject. Completes the cmd.cpp
split planned in 2026-05-02-cmd-core-split.md."
```

### Task 5: Promote clok.cpp to kauai-core (verifies the seam works)

**Files:**
- Modify: `CMakeLists.txt`

- [ ] **Step 1: Audit clok.cpp gui deps after the cmd split**

```
grep -nE "vpappb|GraphicsObject" kauai/src/clok.cpp
```

Expected: zero. clok.cpp only uses CommandHandler / CEM / Command — all in core after Task 4.

- [ ] **Step 2: Move from kauai sources to kauai-core sources in CMakeLists.txt**

- [ ] **Step 3: Build + run**

- [ ] **Step 4: Commit**

```
git commit -m "build: promote clok.cpp to kauai-core

Clock now uses the CEM via the cmd-core seam (no vpappb refs).
First consumer of the cmd_core split."
```

### Task 6: Promote msnd.cpp to engine-core (the practical payoff)

**Files:**
- Modify: `CMakeLists.txt`

- [ ] **Step 1: Audit msnd.cpp gui deps**

```
grep -nE "vpappb|GraphicsObject|MovieClient" src/engine/msnd.cpp
```

Expected: only the existing CommandHandler-via-cmd-core bits remain (now harmless).

- [ ] **Step 2: Move from engine sources to engine-core sources**

- [ ] **Step 3: Build + actor-render-test still works**

- [ ] **Step 4: Commit**

```
git commit -m "build: promote msnd.cpp to engine-core

MovieSoundQueue (CommandHandler subclass) now compiles in engine-core
thanks to the cmd_core split."
```

---

## Verification

1. **Studio runs unchanged.** No user-visible regressions: launch dist\3dmovie.exe, navigate the kidspace gallery, open the studio, add a scene, add an actor, save, exit. (movie-save-load-test passes on both x86 and x64 from the earlier work.)
2. **Existing CLI tests still pass.** geometry-test, codec-test, actor-render-test, movie-save-load-test all green on both arches at every commit.
3. **kauai-core has zero gui symbol refs after Task 4:**
   ```
   grep -rE "vpappb|PGraphicsObject\b|GraphicsObject\b|kclsGraphicsObject" \
     kauai/src/cmd_core.cpp kauai/src/clok.cpp
   ```
   should return nothing.
4. **Headless test linking just kauai-core (no kauai gui) compiles a CommandHandler.** Add a one-off CLI test that constructs a `Clock` and a custom `CommandHandler` subclass, asserts the dispatcher calls FDoCmd. Drop after the chain is verified.

## Risks

- **The hooks aren't installed when ApplicationBase is never constructed.** That's fine — defaults are no-ops. Headless tests work without an `ApplicationBase`. But studio paths that previously assumed `vpappb != pvNil` and now hit the hook seam need careful audit. Specifically: command recording's `_FReadCmd` is only used during play-back of recorded test scripts; if appb hasn't run yet, hook returns nil and play-back finds no handler, which matches the pre-split semantics for that case.
- **Modal-gob breaks.** The hardest semantic risk: if `_FCmhModalOk` virtual dispatch fires before the gui subclass is installed as `vpcex`, the headless `return fTrue` lets a non-modal cmd run that the gui code would have blocked. Mitigation: appb._FInit sets up the gui subclass BEFORE any cmd dispatch happens (it already does today, just with the base class).
- **Mouse-tracking gob lifetime.** `_pgobTrack` is currently freed in `~CommandExecutionManager`. After moving it to the subclass, ensure the subclass dtor handles cleanup (it will, by virtue of being on the subclass).

## Followups out of scope

- `scene.cpp`, `tbox.cpp`, `movie.cpp` promotions — they have additional gui couplings beyond CommandHandler (vpappb->BeginLongOp, GraphicsObject hierarchy walk, MovieClientCallbacks). Those need their own cuts, planned separately.
- `ched`/`chelp` link errors — pre-existing, unrelated.
