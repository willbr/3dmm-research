# Multi-Select Move-Tool MVP — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Let the user shift-click multiple actors and translate them as a rigid group with the move tool (`toolCompose`); Esc clears the selection. One drag = one undo step.

**Architecture:** Extend `Scene` with an "extras" `DynamicArray<PActor>` next to the existing `_pactrSelected` primary pointer. Add toggle / iteration accessors. Wire shift-click in `MovieView::_MouseDown`/`_MouseUp`. In `_MouseDrag toolCompose`, iterate the full set and apply the same world-space delta to each actor via the existing `FMoveRoute` / `FTweakRoute` API. Add an `ActorMoveGroupUndo` composite that owns N child `ActorUndo` snapshots. Add a `MovieView::FCmdKey` override for Esc. All other tools collapse to single-select on mousedown — no behavior change for them.

**Tech Stack:** C++ (MSVC, x86), Kauai framework (`PDynamicArray`, `MovieUndo`, `cidKey`), BRender scalars (`BRS`), `target_chomp_sources`-based build (untouched here).

**Spec:** `docs/superpowers/specs/2026-04-29-multi-select-move-mvp-design.md`

**Compat:** Selection is purely runtime UI state. Nothing this plan changes is persisted to `.3MM` / `.chk`. Original 1995 3DMM playback compatibility is preserved.

**Testing approach:** This codebase has no automated unit-test culture for engine code (CI runs clang-format only; `ft.cpp`/`ut.cpp` are GUI test apps, not unit tests). Each task ends with a **build verification** plus a **manual smoke checklist** with specific click sequences and expected outcomes. Single-select regression checks run after every task.

---

## File map

| File                      | Role                                                  | Action  |
|---------------------------|-------------------------------------------------------|---------|
| `inc/scene.h`             | declare extras list + accessors                       | Modify  |
| `src/engine/scene.cpp`    | implement extras storage, toggle, RemActrCore scrub   | Modify  |
| `inc/socutil.h`           | declare `ActorMoveGroupUndo` composite undo           | Modify  |
| `src/engine/actredit.cpp` | implement `ActorMoveGroupUndo` FDo/FUndo              | Modify  |
| `inc/movie.h`             | declare `_fSelToggleArmed`, `_pactrSelToggle`, `FCmdKey` override | Modify |
| `src/engine/movie.cpp`    | shift-click toggle, group-move drag, Esc, tool-collapse rule | Modify |
| `plan.md`                 | add UI-N "multi-select move-tool MVP" entry           | Modify  |

No new files. No new `.cht`/`.chh`. No `chomp` runs needed for this feature.

---

## Pre-flight

- [ ] **Step 0a: Confirm a clean working tree on branch `c`**

```bash
git -C C:/Users/wjbr/src/3DMMForever status
```

Expected: `On branch c`, `nothing to commit, working tree clean`. If anything is dirty, stash or commit first.

- [ ] **Step 0b: Confirm a clean baseline build**

```bash
cd C:/Users/wjbr/src/3DMMForever
cmake --preset x86:msvc:debug
cmake --build build
```

Expected: build succeeds, `build/3dmovie.exe` exists. (Requires the x86 MSVC environment per `CLAUDE.md`.) If this fails, **stop** — the feature work cannot proceed until the baseline builds cleanly.

---

## Task 1: Add multi-selection state to `Scene`

**Goal:** `Scene` can hold an ordered set of selected actors. Single-select callers see no behavior change.

**Files:**
- Modify: `inc/scene.h` (around line 147 for the field; around line 293 for the methods)
- Modify: `src/engine/scene.cpp` (around line 2423 for `SelectActr`; around the `RemActrCore` body)

- [ ] **Step 1.1: Add the extras field to `Scene`'s private section**

In `inc/scene.h`, find the line:

```cpp
    PActor _pactrSelected; // Currently selected actor, if any
```

Add the new field directly under it:

```cpp
    PActor _pactrSelected; // Primary selected actor (still authoritative single-select pointer)
    PDynamicArray _pglpactrSelExtra; // Additional selected actors (excluding primary). pvNil when no extras.
```

- [ ] **Step 1.2: Add new public methods to `Scene`**

In `inc/scene.h`, find:

```cpp
    void SelectActr(Actor *pactr);                      // Sets the selected actor
    PActor PactrFromPt(long xp, long yp, long *pibset); // Gets actor pointed at by the mouse.
```

Insert immediately above `PactrFromPt`:

```cpp
    bool FIsActrSelected(PActor pactr); // primary OR in extras
    long CactrSelected(void);           // total count: 0 if primary is pvNil, else 1 + extras count
    PActor PactrSelectedAt(long iactr); // 0 = primary, 1..N = extras in insertion order
    bool FToggleActrSelected(PActor pactr); // shift-click entry: add if absent, remove if present. fFalse on alloc failure.
    void ClearSelection(void);          // empty primary AND extras; drop all hilites
```

- [ ] **Step 1.3: Initialize `_pglpactrSelExtra` to `pvNil` in the constructor**

In `src/engine/scene.cpp`, find `Scene::Scene(PMovie pmvie)`. Add `_pglpactrSelExtra = pvNil;` next to where `_pactrSelected` is implicitly nil (or explicitly set near the other init lines).

- [ ] **Step 1.4: Release the extras list in the destructor**

In `src/engine/scene.cpp`, find `Scene::~Scene(void)`. Add:

```cpp
    ReleasePpo(&_pglpactrSelExtra);
```

next to the other `ReleasePpo` calls.

- [ ] **Step 1.5: Update `Scene::SelectActr` to clear extras**

In `src/engine/scene.cpp`, find `void Scene::SelectActr(Actor *pactr)` (around line 2423). The current body unhilites `_pactrSelected`, hilites `pactr`, deselects the tbox, and assigns `_pactrSelected = pactr`. Modify it to **also unhilite every actor in `_pglpactrSelExtra` and empty the list** before assigning the new primary:

```cpp
void Scene::SelectActr(Actor *pactr)
{
    AssertThis(0);
    AssertNilOrPo(pactr, 0);

    PMovieView pmvu;

    pmvu = (PMovieView)Pmvie()->PddgGet(0);
    AssertNilOrPo(pmvu, 0);

    if ((pmvu != pvNil) && !pmvu->FTextMode())
    {
        if (pvNil != _pactrSelected)
        {
            _pactrSelected->Unhilite();
        }

        // Clear any extras and unhilite each.
        if (pvNil != _pglpactrSelExtra)
        {
            for (long iactr = 0; iactr < _pglpactrSelExtra->IvMac(); iactr++)
            {
                PActor pactrExtra;
                _pglpactrSelExtra->Get(iactr, &pactrExtra);
                if (pvNil != pactrExtra)
                {
                    pactrExtra->Unhilite();
                }
            }
            _pglpactrSelExtra->FSetIvMac(0); // Empty the list but keep the storage.
        }

        if (pvNil != pactr)
        {
            pactr->Hilite();
        }

        if (_ptboxSelected != pvNil)
        {
            _ptboxSelected->Select(fFalse);
        }
    }

    _pmvie->InvalViews();
    _pactrSelected = pactr;

    _pmvie->BuildActionMenu();
}
```

If `FSetIvMac` is not the right kauai API for "drop all elements," substitute the equivalent: in `kauai/src/groups.cpp`, the convention is typically `while (pgl->IvMac() > 0) pgl->Delete(pgl->IvMac() - 1);`. Inspect `kauai/src/groups.h` to pick the cheapest available, then use that.

- [ ] **Step 1.6: Implement `Scene::ClearSelection`**

In `src/engine/scene.cpp`, append (near the other selection methods):

```cpp
void Scene::ClearSelection(void)
{
    AssertThis(0);
    SelectActr(pvNil); // SelectActr already drops primary + extras and unhilites everything.
}
```

- [ ] **Step 1.7: Implement `Scene::CactrSelected` and `Scene::PactrSelectedAt`**

```cpp
long Scene::CactrSelected(void)
{
    AssertThis(0);
    if (_pactrSelected == pvNil)
    {
        return 0;
    }
    return 1 + (_pglpactrSelExtra == pvNil ? 0 : _pglpactrSelExtra->IvMac());
}

PActor Scene::PactrSelectedAt(long iactr)
{
    AssertThis(0);
    AssertIn(iactr, 0, CactrSelected());
    if (iactr == 0)
    {
        return _pactrSelected;
    }
    PActor pactr;
    _pglpactrSelExtra->Get(iactr - 1, &pactr);
    return pactr;
}
```

- [ ] **Step 1.8: Implement `Scene::FIsActrSelected`**

```cpp
bool Scene::FIsActrSelected(PActor pactr)
{
    AssertThis(0);
    AssertNilOrPo(pactr, 0);
    if (pactr == pvNil)
    {
        return fFalse;
    }
    if (pactr == _pactrSelected)
    {
        return fTrue;
    }
    if (_pglpactrSelExtra == pvNil)
    {
        return fFalse;
    }
    for (long iactr = 0; iactr < _pglpactrSelExtra->IvMac(); iactr++)
    {
        PActor pactrEntry;
        _pglpactrSelExtra->Get(iactr, &pactrEntry);
        if (pactrEntry == pactr)
        {
            return fTrue;
        }
    }
    return fFalse;
}
```

- [ ] **Step 1.9: Implement `Scene::FToggleActrSelected`**

```cpp
bool Scene::FToggleActrSelected(PActor pactr)
{
    AssertThis(0);
    AssertPo(pactr, 0);

    // If toggling the primary: promote first extra to primary, or clear if no extras.
    if (pactr == _pactrSelected)
    {
        pactr->Unhilite();
        if (_pglpactrSelExtra != pvNil && _pglpactrSelExtra->IvMac() > 0)
        {
            PActor pactrPromote;
            _pglpactrSelExtra->Get(0, &pactrPromote);
            _pglpactrSelExtra->Delete(0);
            _pactrSelected = pactrPromote;
        }
        else
        {
            _pactrSelected = pvNil;
        }
        _pmvie->InvalViews();
        _pmvie->BuildActionMenu();
        return fTrue;
    }

    // If toggling an extra: remove it, unhilite.
    if (_pglpactrSelExtra != pvNil)
    {
        for (long iactr = 0; iactr < _pglpactrSelExtra->IvMac(); iactr++)
        {
            PActor pactrEntry;
            _pglpactrSelExtra->Get(iactr, &pactrEntry);
            if (pactrEntry == pactr)
            {
                pactrEntry->Unhilite();
                _pglpactrSelExtra->Delete(iactr);
                _pmvie->InvalViews();
                _pmvie->BuildActionMenu();
                return fTrue;
            }
        }
    }

    // Adding a new actor.
    // If no primary yet, make it primary (matches plain-click semantics).
    if (_pactrSelected == pvNil)
    {
        SelectActr(pactr);
        return fTrue;
    }

    // Add as extra. Lazily allocate the list.
    if (_pglpactrSelExtra == pvNil)
    {
        _pglpactrSelExtra = DynamicArray::PglNew(size(PActor));
        if (_pglpactrSelExtra == pvNil)
        {
            return fFalse;
        }
    }
    if (!_pglpactrSelExtra->FAdd(&pactr))
    {
        return fFalse;
    }
    pactr->Hilite();
    _pmvie->InvalViews();
    _pmvie->BuildActionMenu();
    return fTrue;
}
```

If `DynamicArray::PglNew` has a different signature in this codebase (e.g., `PglNew(cb, cvInit)`), inspect `kauai/src/groups.h` and adjust. The signature commonly used in this codebase is `DynamicArray::PglNew(long cb, long cvInit = 0)`.

- [ ] **Step 1.10: Scrub `_pglpactrSelExtra` when an actor is removed**

In `src/engine/scene.cpp`, find `void Scene::RemActrCore(long arid)`. After the existing logic that may set `_pactrSelected = pvNil`, add a scrub of the extras list:

```cpp
    // Remove the actor from the multi-selection extras list as well.
    if (_pglpactrSelExtra != pvNil)
    {
        for (long iactr = _pglpactrSelExtra->IvMac() - 1; iactr >= 0; iactr--)
        {
            PActor pactrEntry;
            _pglpactrSelExtra->Get(iactr, &pactrEntry);
            if (pactrEntry != pvNil && pactrEntry->Arid() == arid)
            {
                _pglpactrSelExtra->Delete(iactr);
            }
        }
    }
```

(Iterate backward so deletions don't shift indices we haven't visited.)

- [ ] **Step 1.11: Build and verify**

```bash
cmake --build build
```

Expected: clean build, no new warnings related to our edits.

- [ ] **Step 1.12: Manual smoke — single-select regression**

Launch `build/3dmovie.exe`. Open the default starter movie or any sample.
1. Pick an actor from the props/people browser, place it. Click it. Move-tool drag. Expected: actor moves, hilite stays on actor — bit-for-bit identical to before.
2. Click empty stage. Expected: actor deselects, hilite clears.
3. Click the actor again, then click another newly placed actor. Expected: first deselects, second selects.

If any of these regress, **stop and fix before proceeding**. Single-select regression is the bar.

- [ ] **Step 1.13: Commit**

```bash
git -C C:/Users/wjbr/src/3DMMForever add inc/scene.h src/engine/scene.cpp
git -C C:/Users/wjbr/src/3DMMForever commit -m "$(cat <<'EOF'
scene: add multi-actor selection state and toggle accessors

Adds Scene::_pglpactrSelExtra plus FIsActrSelected, CactrSelected,
PactrSelectedAt, FToggleActrSelected, and ClearSelection. SelectActr now
clears extras as well as the primary. RemActrCore scrubs the extras list
when an actor is removed.

No UI is wired up to these methods yet; single-select behavior is
unchanged.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 2: Add `ActorMoveGroupUndo` composite undo class

**Goal:** A single undo entry that owns N child `ActorUndo` snapshots and undoes/redoes them as a unit.

**Files:**
- Modify: `inc/socutil.h` (declare class after `ActorUndo`)
- Modify: `src/engine/actredit.cpp` (implement methods near the other `ActorUndo` impls around line 1412–1530)

- [ ] **Step 2.1: Declare `ActorMoveGroupUndo` in `inc/socutil.h`**

After the closing `};` of `class ActorUndo` (around line 144), add:

```cpp
//
// Undo object for a single drag of the move tool that affects N selected actors.
// Owns N child ActorUndo snapshots and undoes/redoes them as a unit.
//
typedef class ActorMoveGroupUndo *PActorMoveGroupUndo;

#define ActorMoveGroupUndo_PAR MovieUndo
#define kclsActorMoveGroupUndo 'AMGU'
class ActorMoveGroupUndo : public ActorMoveGroupUndo_PAR
{
    RTCLASS_DEC
    ASSERT
    MARKMEM

  protected:
    PDynamicArray _pglpaund; // List of PActorUndo. Owned (each child is AddRef'd).
    ActorMoveGroupUndo(void)
    {
    }

  public:
    static PActorMoveGroupUndo PamguNew(void);
    ~ActorMoveGroupUndo(void);

    bool FAddChild(PActorUndo paund); // Adds and AddRefs the child undo.
    long Cchild(void)
    {
        return _pglpaund == pvNil ? 0 : _pglpaund->IvMac();
    }

    virtual bool FDo(PDocumentBase pdocb);
    virtual bool FUndo(PDocumentBase pdocb);
};
```

- [ ] **Step 2.2: Implement `ActorMoveGroupUndo` in `src/engine/actredit.cpp`**

After the closing `}` of `ActorUndo::FUndo` (around line 1530), append:

```cpp
RTCLASS(ActorMoveGroupUndo)

PActorMoveGroupUndo ActorMoveGroupUndo::PamguNew(void)
{
    PActorMoveGroupUndo pamgu;
    pamgu = NewObj ActorMoveGroupUndo();
    if (pamgu == pvNil)
    {
        return pvNil;
    }
    pamgu->_pglpaund = DynamicArray::PglNew(size(PActorUndo));
    if (pamgu->_pglpaund == pvNil)
    {
        ReleasePpo(&pamgu);
        return pvNil;
    }
    return pamgu;
}

ActorMoveGroupUndo::~ActorMoveGroupUndo(void)
{
    if (_pglpaund != pvNil)
    {
        for (long iaund = 0; iaund < _pglpaund->IvMac(); iaund++)
        {
            PActorUndo paund;
            _pglpaund->Get(iaund, &paund);
            ReleasePpo(&paund);
        }
        ReleasePpo(&_pglpaund);
    }
}

bool ActorMoveGroupUndo::FAddChild(PActorUndo paund)
{
    AssertThis(0);
    AssertPo(paund, 0);
    paund->AddRef();
    if (!_pglpaund->FAdd(&paund))
    {
        ReleasePpo(&paund);
        return fFalse;
    }
    return fTrue;
}

bool ActorMoveGroupUndo::FUndo(PDocumentBase pdocb)
{
    AssertThis(0);
    AssertPo(pdocb, 0);

    bool fAllOk = fTrue;
    for (long iaund = 0; iaund < _pglpaund->IvMac(); iaund++)
    {
        PActorUndo paund;
        _pglpaund->Get(iaund, &paund);
        if (paund != pvNil)
        {
            // Mirror MovieUndo's iscen/nfrm onto each child so its FUndo
            // navigates to the correct frame.
            paund->SetPmvie(_pmvie);
            paund->SetIscen(_iscen);
            paund->SetNfrm(_nfrm);
            if (!paund->FUndo(pdocb))
            {
                fAllOk = fFalse;
            }
        }
    }
    return fAllOk;
}

bool ActorMoveGroupUndo::FDo(PDocumentBase pdocb)
{
    // ActorUndo::FDo delegates to FUndo (it's a swap-based undo), so the
    // composite redo is the same loop.
    return FUndo(pdocb);
}

#ifdef DEBUG
void ActorMoveGroupUndo::AssertValid(ulong grf)
{
    ActorMoveGroupUndo_PAR::AssertValid(0);
    AssertPo(_pglpaund, 0);
}

void ActorMoveGroupUndo::MarkMem(void)
{
    AssertValid(0);
    ActorMoveGroupUndo_PAR::MarkMem();
    MarkMemObj(_pglpaund);
    if (_pglpaund != pvNil)
    {
        for (long iaund = 0; iaund < _pglpaund->IvMac(); iaund++)
        {
            PActorUndo paund;
            _pglpaund->Get(iaund, &paund);
            MarkMemObj(paund);
        }
    }
}
#endif // DEBUG
```

- [ ] **Step 2.3: Build and verify**

```bash
cmake --build build
```

Expected: clean build, no warnings. If linker complains about unresolved `ActorMoveGroupUndo::AssertValid` or `MarkMem`, check that `ASSERT` and `MARKMEM` macro pattern matches the surrounding `ActorUndo` definition exactly — the macro generators expect the pattern.

- [ ] **Step 2.4: Commit**

```bash
git -C C:/Users/wjbr/src/3DMMForever add inc/socutil.h src/engine/actredit.cpp
git -C C:/Users/wjbr/src/3DMMForever commit -m "$(cat <<'EOF'
engine: add ActorMoveGroupUndo composite undo for group drags

A single drag of the move tool that affects N actors needs to undo as one
step. ActorMoveGroupUndo owns N child ActorUndo snapshots and forwards
FUndo / FDo to each. ActorUndo's existing swap-based FDo means the same
loop serves as redo.

Not yet wired up to the drag pipeline.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 3: Wire shift-click selection toggle in `MovieView`

**Goal:** Shift-clicking an actor toggles its membership in the selection set. Drag past tolerance cancels the toggle and falls through to existing single-actor behavior on the primary.

**Files:**
- Modify: `inc/movie.h` (around line 175 in the `MovieView` private section)
- Modify: `src/engine/movie.cpp` — `_MouseDown` (around line 6840), `_MouseUp` (find via grep), and the bool-flag init in `PmvuNew`

- [ ] **Step 3.1: Add the pending-toggle state to `MovieView`**

In `inc/movie.h`, find `bool _fMouseDownSeen;` (around line 175) and add:

```cpp
    bool _fMouseDownSeen; // Was the mouse depressed during a place.
    bool _fSelToggleArmed : 1; // Shift-clicked an actor at mousedown; apply toggle on mouseup if no drag.
    PActor _pactrSelToggle;    // Actor under cursor at the armed shift-click.
    long _xpSelToggleDown;     // Mousedown x for drag-tolerance check.
    long _ypSelToggleDown;     // Mousedown y for drag-tolerance check.
```

- [ ] **Step 3.2: Initialize the flag in `MovieView::PmvuNew`**

In `src/engine/movie.cpp`, find `MovieView::PmvuNew` (around line 5835). After the existing assignments to `_dxp`, `_dyp`, `_tool`, etc., add:

```cpp
    pmvu->_fSelToggleArmed = fFalse;
    pmvu->_pactrSelToggle = pvNil;
```

- [ ] **Step 3.3: Detect shift-on-actor at mousedown**

In `src/engine/movie.cpp`, find `void MovieView::_MouseDown(CMD_MOUSE *pcmd)` (around line 6840). The existing flow (around line 6874–6907) calls `Pmvie()->Pscen()->SelectActr(pactr)` on a non-place, non-tbox click. **Wrap that call** in a shift-detection branch.

Replace this block:

```cpp
    else if ((Tool() != toolPlace) && (Tool() != toolSceneChop) && (Tool() != toolSceneChopBack))
    {

        //
        // Select the actor under the cursor
        //
        pactr = Pmvie()->Pscen()->PactrSelected();
        AssertNilOrPo(pactr, 0);

        if ((pactr != pvNil) && pactr->FTimeFrozen())
        {
            pactr->SetTimeFreeze(fFalse);
        }

        pactrDup = Pmvie()->Pscen()->PactrFromPt(pcmd->xp, pcmd->yp, &ibset);

        //
        // Use previously selected actor if mouse in the actor.
        // Don't change the selected actor if we're using the default tool
        //
        if (((pactr == pvNil) || !pactr->FIsInView() || !pactr->FPtIn(pcmd->xp, pcmd->yp, &ibset)) &&
            Tool() != toolDefault)
        {
            pactr = pactrDup;
            AssertNilOrPo(pactr, 0);
        }

        if (pvNil != pactr)
        {
            _ActorClicked(pactr, fTrue);
        }
        Pmvie()->Pscen()->SelectActr(pactr); // okay even if pactr is pvNil
        Pmvie()->Pbwld()->MarkDirty();
    }
```

with:

```cpp
    else if ((Tool() != toolPlace) && (Tool() != toolSceneChop) && (Tool() != toolSceneChopBack))
    {
        //
        // Select the actor under the cursor.
        //
        pactr = Pmvie()->Pscen()->PactrSelected();
        AssertNilOrPo(pactr, 0);

        if ((pactr != pvNil) && pactr->FTimeFrozen())
        {
            pactr->SetTimeFreeze(fFalse);
        }

        pactrDup = Pmvie()->Pscen()->PactrFromPt(pcmd->xp, pcmd->yp, &ibset);

        // Shift-click on an actor with a selection-friendly tool: arm a toggle for mouseup.
        // Move tool, default tool, and explicit selection tool participate; other tools
        // collapse to single-select via the existing path below.
        bool fShiftSelectTool = (Tool() == toolCompose) || (Tool() == toolDefault) || (Tool() == toolActorSelect);
        if (fShiftSelectTool && (pcmd->grfcust & fcustShift) && pactrDup != pvNil)
        {
            _fSelToggleArmed = fTrue;
            _pactrSelToggle = pactrDup;
            _xpSelToggleDown = pcmd->xp;
            _ypSelToggleDown = pcmd->yp;
            Pmvie()->Pbwld()->MarkDirty();
            // Do NOT call SelectActr here. Toggle fires at mouseup if we didn't drag.
            // Skip the rest of the mousedown selection flow; existing behavior resumes
            // at the switch on Tool() below if the user goes on to drag.
        }
        else
        {
            //
            // Use previously selected actor if mouse in the actor.
            // Don't change the selected actor if we're using the default tool.
            //
            if (((pactr == pvNil) || !pactr->FIsInView() || !pactr->FPtIn(pcmd->xp, pcmd->yp, &ibset)) &&
                Tool() != toolDefault)
            {
                pactr = pactrDup;
                AssertNilOrPo(pactr, 0);
            }

            if (pvNil != pactr)
            {
                _ActorClicked(pactr, fTrue);
            }
            Pmvie()->Pscen()->SelectActr(pactr); // okay even if pactr is pvNil
            Pmvie()->Pbwld()->MarkDirty();
        }
    }
```

- [ ] **Step 3.4: Apply or cancel the toggle at mouseup**

In `src/engine/movie.cpp`, find `void MovieView::_MouseUp` (search `git grep -n "void MovieView::_MouseUp"`). At the very top of the function body, before any other logic, insert:

```cpp
    // Shift-click selection toggle: if the user shift-clicked at mousedown
    // and didn't drag past tolerance, fire the toggle now.
    if (_fSelToggleArmed)
    {
        const long kdxpSelToggleTol = 3;
        const long kdypSelToggleTol = 3;
        long dxp = LwAbs(pcmd->xp - _xpSelToggleDown);
        long dyp = LwAbs(pcmd->yp - _ypSelToggleDown);
        if (dxp <= kdxpSelToggleTol && dyp <= kdypSelToggleTol)
        {
            if (_pactrSelToggle != pvNil && Pmvie()->Pscen() != pvNil)
            {
                Pmvie()->Pscen()->FToggleActrSelected(_pactrSelToggle);
                Pmvie()->Pbwld()->MarkDirty();
                Pmvie()->MarkViews();
            }
        }
        _fSelToggleArmed = fFalse;
        _pactrSelToggle = pvNil;
    }
```

If `LwAbs` is not the abs primitive in this codebase, search `kauai/src/util*.h` for `LwAbs` or substitute the equivalent (`BrsAbs` is for BRS, not long; the kauai integer abs is typically `LwAbs`).

- [ ] **Step 3.5: Build and verify**

```bash
cmake --build build
```

Expected: clean build.

- [ ] **Step 3.6: Manual smoke — shift-click toggle**

1. Place two actors A and B in the scene.
2. Click A. Expected: A hilites, B does not.
3. Shift-click B. Expected: **both A and B hilited**, A still primary.
4. Shift-click B again. Expected: A hilited, B unhilited.
5. Shift-click A (the primary). Expected: B promoted to primary (hilited alone), A unhilited.
6. Click empty space. Expected: both actors unhilited, no selection.
7. Place a third actor C, select A, shift-click B, shift-click C. Expected: all three hilited.
8. Drag with the move tool on the primary. Expected: only the primary moves (we have not wired group-move yet — that's Task 4). This confirms we did not break single-actor drag.

- [ ] **Step 3.7: Commit**

```bash
git -C C:/Users/wjbr/src/3DMMForever add inc/movie.h src/engine/movie.cpp
git -C C:/Users/wjbr/src/3DMMForever commit -m "$(cat <<'EOF'
movie: shift-click toggles actor membership in the selection set

Shift-clicking an actor with the move, default, or explicit-select tool
arms a pending toggle. If the mouse comes back up within a 3px tolerance
of the down point, the toggle fires via Scene::FToggleActrSelected.
Dragging past tolerance cancels the toggle.

Group movement is not yet wired up; the existing single-actor drag still
runs on the primary only.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 4: Group move in `_MouseDrag toolCompose`

**Goal:** When N≥2 actors are selected, a `toolCompose` drag translates each by the same world-space delta. One drag = one undo step.

**Files:**
- Modify: `src/engine/movie.cpp` — the `case toolCompose:` block in `_MouseDown` (~line 7072) for per-actor undo capture, and in `_MouseDrag` (~line 7401) for the iteration

- [ ] **Step 4.1: Capture one `ActorUndo` snapshot per selected actor at mousedown**

In `src/engine/movie.cpp`, find the `case toolCompose:` block in `_MouseDown` (around line 7072). The current code allocates one `ActorUndo` for the primary `pactr` and stores it in `_paund`. Modify it to allocate one snapshot per selected actor and aggregate them into an `ActorMoveGroupUndo`.

Replace:

```cpp
    case toolCompose:
    case toolRotateX:
    case toolRotateY:
    case toolRotateZ:
    case toolResize:
    case toolSquashStretch:
        if (pactr != pvNil)
        {

            vpappb->HideCurs();

            //
            // Create an actor undo object
            //
            paund = ActorUndo::PaundNew();
            if ((paund == pvNil) || !pactr->FDup(&pactrDup, fTrue))
            {
                Pmvie()->ClearUndo();
                PushErc(ercSocNotUndoable);
            }
            else
            {
                paund->SetPactr(pactrDup);
                ReleasePpo(&pactrDup);
                paund->SetArid(pactr->Arid());

                //
                // Store it.  We will only add it if there is a change done
                // to the actor.
                //
                _paund = paund;
            }

            if ((Tool() != toolResize) && (Tool() != toolSquashStretch))
            {
                Pmvie()->Pmcc()->PlayUISound(Tool(), _grfcust);
            }
            else
            {
                _lwLastTime = 0;
            }
        }
        break;
```

with:

```cpp
    case toolCompose:
    case toolRotateX:
    case toolRotateY:
    case toolRotateZ:
    case toolResize:
    case toolSquashStretch:
        if (pactr != pvNil)
        {
            vpappb->HideCurs();

            // For toolCompose with multi-selection: snapshot every selected actor
            // and bundle into an ActorMoveGroupUndo. Other tools and single-select
            // compose retain the original single-actor _paund flow.
            long cactrSel = (Tool() == toolCompose && Pmvie()->Pscen() != pvNil)
                                ? Pmvie()->Pscen()->CactrSelected()
                                : 1;

            if (cactrSel >= 2)
            {
                PActorMoveGroupUndo pamgu = ActorMoveGroupUndo::PamguNew();
                bool fOk = (pamgu != pvNil);
                for (long iactr = 0; fOk && iactr < cactrSel; iactr++)
                {
                    PActor pactrSel = Pmvie()->Pscen()->PactrSelectedAt(iactr);
                    PActorUndo paundChild = ActorUndo::PaundNew();
                    PActor pactrSnap = pvNil;
                    if (paundChild == pvNil || !pactrSel->FDup(&pactrSnap, fTrue))
                    {
                        ReleasePpo(&paundChild);
                        ReleasePpo(&pactrSnap);
                        fOk = fFalse;
                        break;
                    }
                    paundChild->SetPactr(pactrSnap);
                    ReleasePpo(&pactrSnap);
                    paundChild->SetArid(pactrSel->Arid());
                    if (!pamgu->FAddChild(paundChild))
                    {
                        ReleasePpo(&paundChild);
                        fOk = fFalse;
                        break;
                    }
                    ReleasePpo(&paundChild); // pamgu holds its own ref via FAddChild
                }

                if (!fOk)
                {
                    ReleasePpo(&pamgu);
                    Pmvie()->ClearUndo();
                    PushErc(ercSocNotUndoable);
                }
                else
                {
                    // Treat the composite as our pending undo. Reuse _paund storage
                    // — _paund is typed PActorUndo, so cast through a sibling slot.
                    Assert(_paundGroup == pvNil, "leaking previous group undo");
                    _paundGroup = pamgu;
                }
            }
            else
            {
                //
                // Create an actor undo object (single-actor flow, unchanged).
                //
                paund = ActorUndo::PaundNew();
                if ((paund == pvNil) || !pactr->FDup(&pactrDup, fTrue))
                {
                    Pmvie()->ClearUndo();
                    PushErc(ercSocNotUndoable);
                }
                else
                {
                    paund->SetPactr(pactrDup);
                    ReleasePpo(&pactrDup);
                    paund->SetArid(pactr->Arid());

                    //
                    // Store it.  We will only add it if there is a change done
                    // to the actor.
                    //
                    _paund = paund;
                }
            }

            if ((Tool() != toolResize) && (Tool() != toolSquashStretch))
            {
                Pmvie()->Pmcc()->PlayUISound(Tool(), _grfcust);
            }
            else
            {
                _lwLastTime = 0;
            }
        }
        break;
```

- [ ] **Step 4.2: Add the `_paundGroup` field**

In `inc/movie.h`, find `PActorUndo _paund;` (around line 160) and add directly under it:

```cpp
    PActorUndo _paund;        // Actor undo object to save from mouse down to drag.
    PActorMoveGroupUndo _paundGroup; // Composite undo for multi-select group drag (toolCompose only). pvNil unless N>=2.
```

In `MovieView::PmvuNew` (`src/engine/movie.cpp:5835`), add `pmvu->_paundGroup = pvNil;` near the other init lines.

In `MovieView::~MovieView` (`src/engine/movie.cpp:5815`), add `ReleasePpo(&_paundGroup);` so a dropped drag is cleaned up.

You will need to forward-declare `PActorMoveGroupUndo` near the top of `inc/movie.h` (or include `socutil.h` if not already pulled in transitively). Check existing includes first.

- [ ] **Step 4.3: Iterate the selection set in the `toolCompose` drag branch**

In `src/engine/movie.cpp`, find the `case toolCompose: {` block in `_MouseDrag` (around line 7401). The current body operates on the single `pactr = pscen->PactrSelected()`. Restructure so it iterates `[primary, ...extras]` when multi-selected:

Replace:

```cpp
    case toolCompose: {
        ulong grfmaf = fmafNil;
        bool fMoved{};

        if (_fRespectGround)
        {
            grfmaf |= fmafGround;
        }

        if (_grfcust & fcustCmd)
        {
            AdjustCursor(pcmd->xp, pcmd->yp);

            if (pactr->FTweakRoute(dxrWld, dyrWld, dzrWld, grfmaf))
            {
                if (fMoved)
                {
                    if ((_paund != pvNil) && !Pmvie()->FAddUndo(_paund))
                    {
                        PushErc(ercSocNotUndoable);
                        Pmvie()->ClearUndo();
                    }

                    ReleasePpo(&_paund);
                }
            }

            Pmvie()->MarkViews();
        }
        else
        {

            if (_grfcust & fcustShift)
            {
                grfmaf |= fmafEntireSubrte;
            }

            // FMoveRoute returns fTrue if the distance moved was non-zero
            if (pactr->FMoveRoute(dxrWld, dyrWld, dzrWld, &fMoved, grfmaf))
            {
                if (fMoved)
                {
                    if ((_paund != pvNil) && !Pmvie()->FAddUndo(_paund))
                    {
                        PushErc(ercSocNotUndoable);
                        Pmvie()->ClearUndo();
                    }

                    ReleasePpo(&_paund);

                    AdjustCursor(pcmd->xp, pcmd->yp);
                    Pmvie()->Pbwld()->MarkDirty();
                    Pmvie()->MarkViews();
                }
            }
        }
    }
    break;
```

with:

```cpp
    case toolCompose: {
        ulong grfmaf = fmafNil;
        bool fAnyMoved = fFalse;
        long cactrSel = (Pmvie()->Pscen() != pvNil) ? Pmvie()->Pscen()->CactrSelected() : 0;

        if (_fRespectGround)
        {
            grfmaf |= fmafGround;
        }

        if (_grfcust & fcustCmd)
        {
            AdjustCursor(pcmd->xp, pcmd->yp);

            for (long iactr = 0; iactr < cactrSel; iactr++)
            {
                PActor pa = Pmvie()->Pscen()->PactrSelectedAt(iactr);
                if (pa == pvNil)
                {
                    continue;
                }
                if (pa->FTweakRoute(dxrWld, dyrWld, dzrWld, grfmaf))
                {
                    fAnyMoved = fTrue;
                }
            }

            if (fAnyMoved)
            {
                _CommitMoveUndo();
            }

            Pmvie()->MarkViews();
        }
        else
        {
            if (_grfcust & fcustShift)
            {
                grfmaf |= fmafEntireSubrte;
            }

            for (long iactr = 0; iactr < cactrSel; iactr++)
            {
                PActor pa = Pmvie()->Pscen()->PactrSelectedAt(iactr);
                if (pa == pvNil)
                {
                    continue;
                }
                bool fMovedThis = fFalse;
                if (pa->FMoveRoute(dxrWld, dyrWld, dzrWld, &fMovedThis, grfmaf) && fMovedThis)
                {
                    fAnyMoved = fTrue;
                }
            }

            if (fAnyMoved)
            {
                _CommitMoveUndo();

                AdjustCursor(pcmd->xp, pcmd->yp);
                Pmvie()->Pbwld()->MarkDirty();
                Pmvie()->MarkViews();
            }
        }
    }
    break;
```

- [ ] **Step 4.4: Add the `_CommitMoveUndo` private helper**

In `inc/movie.h`, in the protected section of `MovieView`, add:

```cpp
    void _CommitMoveUndo(void); // commits whichever of _paundGroup / _paund is pending; nil-safe.
```

In `src/engine/movie.cpp`, near `_MouseDrag`, add:

```cpp
void MovieView::_CommitMoveUndo(void)
{
    AssertThis(0);

    if (_paundGroup != pvNil)
    {
        if (!Pmvie()->FAddUndo(_paundGroup))
        {
            PushErc(ercSocNotUndoable);
            Pmvie()->ClearUndo();
        }
        ReleasePpo(&_paundGroup);
    }
    else if (_paund != pvNil)
    {
        if (!Pmvie()->FAddUndo(_paund))
        {
            PushErc(ercSocNotUndoable);
            Pmvie()->ClearUndo();
        }
        ReleasePpo(&_paund);
    }
}
```

- [ ] **Step 4.5: Drop any uncommitted group undo at mouseup**

In `_MouseUp`, locate where the existing code releases `_paund` if it was never used. Mirror that for `_paundGroup`:

```cpp
    ReleasePpo(&_paundGroup);
```

(Search for existing `ReleasePpo(&_paund);` in `_MouseUp` and add the line next to it.)

- [ ] **Step 4.6: Build and verify**

```bash
cmake --build build
```

Expected: clean build. Watch for any "use of undeclared identifier `PActorMoveGroupUndo`" — if that appears, ensure `inc/movie.h` includes `socutil.h` or has the appropriate forward declaration.

- [ ] **Step 4.7: Manual smoke — group move**

1. Place actors A and B; click A; shift-click B (both hilited).
2. With the move tool, drag on stage. Expected: **both A and B translate by the same delta**. Hilites stay on both.
3. Hit Undo (Ctrl-Z). Expected: **both A and B return to their pre-drag positions in one undo step**.
4. Hit Redo. Expected: both return to the dragged positions in one redo.
5. Repeat with three actors.
6. Repeat the drag with Cmd held (tweak route) and with Shift held (entire subroute). Expected: corresponding semantics applied to each actor.
7. Single-select drag (only one actor selected). Expected: identical to old single-actor behavior — this is the regression check.

- [ ] **Step 4.8: Commit**

```bash
git -C C:/Users/wjbr/src/3DMMForever add inc/movie.h src/engine/movie.cpp
git -C C:/Users/wjbr/src/3DMMForever commit -m "$(cat <<'EOF'
movie: group-translate selected actors with the move tool

When two or more actors are selected, a toolCompose drag iterates the
selection set and applies the same world-space delta to each via
FMoveRoute / FTweakRoute. The drag produces a single ActorMoveGroupUndo
entry that undoes/redoes all moved actors atomically.

Single-select drags retain the original single-actor undo path
unchanged.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 5: Esc clears selection + non-move-tool collapse rule

**Goal:** Esc clears the entire selection. Mousedown over an actor with any non-move/non-default/non-select tool collapses the selection to that one actor before the existing single-actor logic runs.

**Files:**
- Modify: `inc/movie.h` — declare `FCmdKey` override
- Modify: `src/engine/movie.cpp` — implement `FCmdKey`, register `cidKey` in the command map, and add the collapse rule in `_MouseDown`

- [ ] **Step 5.1: Declare the `FCmdKey` override**

In `inc/movie.h`, in the `MovieView` public section, add:

```cpp
    virtual bool FCmdKey(PCMD_KEY pcmd) override;
```

- [ ] **Step 5.2: Register `cidKey` in the command map**

In `src/engine/movie.cpp`, find the `BEGIN_CMD_MAP` ... `END_CMD_MAP_NIL` block for `MovieView` (around line 5790–5806). Add:

```cpp
ON_CID_GEN(cidKey, &MovieView::FCmdKeyCore, pvNil)
```

near the other `ON_CID_GEN` rows. (`FCmdKeyCore` is the trampoline declared in `kauai/src/gob.h:295` that downcasts the `PCommand` to `PCMD_KEY` and calls the virtual `FCmdKey`.)

- [ ] **Step 5.3: Implement `FCmdKey`**

In `src/engine/movie.cpp`, add a new method (place it near other command handlers, e.g., after `FCmdRollOff`):

```cpp
bool MovieView::FCmdKey(PCMD_KEY pcmd)
{
    AssertThis(0);
    AssertVarMem(pcmd);

#ifdef WIN
    const long kvkEsc = VK_ESCAPE;
#endif
#ifdef MAC
    const long kvkEsc = 0x35; // Mac escape key code
#endif

    if (pcmd->vk == kvkEsc)
    {
        if (Pmvie() != pvNil && Pmvie()->Pscen() != pvNil)
        {
            Pmvie()->Pscen()->ClearSelection();
            Pmvie()->InvalViews();
            Pmvie()->Pbwld()->MarkDirty();
        }
        return fTrue; // command consumed
    }

    return MovieView_PAR::FCmdKey(pcmd);
}
```

If the codebase already defines a portable `kvkEscape` or similar constant elsewhere (search `kauai/src/cmd.h` and `appbwin.cpp`), use that and drop the platform `#ifdef`.

- [ ] **Step 5.4: Add tool-collapse rule in `_MouseDown`**

In `src/engine/movie.cpp`, in `_MouseDown`, locate the **non-shift** branch we restructured in Step 3.3 (the `else` after `fShiftSelectTool && (pcmd->grfcust & fcustShift)`). Inside that else, the existing `Pmvie()->Pscen()->SelectActr(pactr)` already collapses to single-select for any tool. So the rule is already enforced for non-shift mousedowns.

To be explicit and defensive, **add an early-return shortcut**: when the tool is one we explicitly want to collapse (any tool other than `toolCompose`, `toolDefault`, `toolActorSelect`, and the tools that already had special handling), call `Pmvie()->Pscen()->SelectActr(pactr)` even when shift is held. Replace:

```cpp
        bool fShiftSelectTool = (Tool() == toolCompose) || (Tool() == toolDefault) || (Tool() == toolActorSelect);
        if (fShiftSelectTool && (pcmd->grfcust & fcustShift) && pactrDup != pvNil)
```

with:

```cpp
        // Only the move tool, default, and explicit selection tool participate in
        // multi-selection. Any other tool collapses to single-select on mousedown
        // even if shift is held.
        bool fShiftSelectTool = (Tool() == toolCompose) || (Tool() == toolDefault) || (Tool() == toolActorSelect);
        if (fShiftSelectTool && (pcmd->grfcust & fcustShift) && pactrDup != pvNil)
```

(no change here — the gate already does the right thing because non-collaborating tools fall through to the existing `else` which calls `SelectActr(pactr)`.)

This step is now a **no-code-change verification step**: confirm by reading the surrounding code that any non-collaborating tool indeed falls through to `SelectActr(pactr)` (which clears extras per Task 1).

- [ ] **Step 5.5: Build and verify**

```bash
cmake --build build
```

Expected: clean build.

- [ ] **Step 5.6: Manual smoke — Esc and tool collapse**

1. Place A, B, C. Select all three (click A, shift-click B, shift-click C). Expected: all three hilited.
2. Press **Esc**. Expected: all three unhilited; nothing selected.
3. Re-select all three. Switch to a rotate tool (e.g., RotateY). Click on B. Expected: only B is hilited; A and C are not.
4. Re-select all three. Switch to costume tool. Click on A. Expected: only A is hilited; B and C are not.
5. Re-select all three. Switch back to the move tool **without clicking anything**. Expected: all three still hilited (selection preserved across tool change).

- [ ] **Step 5.7: Commit**

```bash
git -C C:/Users/wjbr/src/3DMMForever add inc/movie.h src/engine/movie.cpp
git -C C:/Users/wjbr/src/3DMMForever commit -m "$(cat <<'EOF'
movie: Esc clears selection; non-move tools collapse to single-select

Adds MovieView::FCmdKey override bound to cidKey so VK_ESCAPE clears the
entire selection set via Scene::ClearSelection. Non-move-tool mousedown
already calls SelectActr (which clears extras), so the collapse rule
holds without further code change.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 6: Acceptance pass + plan.md UI-N entry

**Goal:** Verify every acceptance criterion from the spec, then record the feature in `plan.md` so it shows up alongside UI-1..UI-11.

- [ ] **Step 6.1: Run the acceptance checklist from the spec**

For each of the 9 acceptance criteria in `docs/superpowers/specs/2026-04-29-multi-select-move-mvp-design.md`, perform the check and tick:

- [ ] (1) Single-actor behavior bit-for-bit identical to today.
- [ ] (2) Shift-click on unselected actor adds it; hilite turns on; no drag.
- [ ] (3) Shift-click on selected actor removes it; hilite turns off.
- [ ] (4) N≥2 + `toolCompose` drag translates all selected by the same delta. Cmd-drag tweaks each. Shift-drag affects entire subroute on each.
- [ ] (5) Drag in (4) → exactly one undo step that restores all.
- [ ] (6) Esc clears all selection.
- [ ] (7) Non-move-tool click on actor collapses selection.
- [ ] (8) Deleting a selected actor (try `toolActorNuke` on each of: primary, an extra) leaves no dangling pointer (no crashes; subsequent operations work).
- [ ] (9) Save the movie, close, reopen in this binary and in original 1995 3DMM if available — movie loads and plays the same. (Compat is theoretical here since we changed nothing on disk; this check is just paranoia.)

If any check fails, fix the regression in a follow-up commit before proceeding.

- [ ] **Step 6.2: Add a `plan.md` entry**

The repo's `plan.md` is the running backlog. Read the current "Studio UI features" section and append a new entry numbered after the last UI-N. As of writing, the last is UI-11; the new entry should be UI-12 unless other PRs have landed in the interim — re-check.

```bash
grep -nE '^### UI-[0-9]+' C:/Users/wjbr/src/3DMMForever/plan.md | tail -5
```

Add an entry roughly like:

```markdown
### UI-12 — Multi-object selection (move tool MVP) — DONE

Status: shipped on branch `c`, merged via [PR/commit list].

Users can shift-click multiple actors and drag them as a rigid group with
the move tool. Esc clears the selection. One drag = one undo step. Pure
runtime UI state — original 1995 3DMM playback compatibility unaffected.

Spec: `docs/superpowers/specs/2026-04-29-multi-select-move-mvp-design.md`
Plan: `docs/superpowers/plans/2026-04-29-multi-select-move-mvp.md`

Out of scope (future entries): box-select, multi-target rotate / costume /
sooner-later, distinct primary highlight, "select all in scene", multi-
select of text boxes.
```

- [ ] **Step 6.3: Commit**

```bash
git -C C:/Users/wjbr/src/3DMMForever add plan.md
git -C C:/Users/wjbr/src/3DMMForever commit -m "$(cat <<'EOF'
plan.md: record UI-12 (multi-select move-tool MVP) as shipped

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

- [ ] **Step 6.4: Final lint pass**

```bash
git -C C:/Users/wjbr/src/3DMMForever diff --name-only HEAD~6 HEAD
```

For each `.cpp`/`.h` in the diff, run clang-format-14 in check mode (CI enforces it):

```bash
clang-format-14 --dry-run --Werror inc/scene.h inc/movie.h inc/socutil.h src/engine/scene.cpp src/engine/actredit.cpp src/engine/movie.cpp
```

Expected: no diagnostics. If any line is reformatted, run `clang-format-14 -i <file>` and amend the relevant commit (or — preferred per `CLAUDE.md` — make a new "format" commit).

- [ ] **Step 6.5: Push the branch**

```bash
git -C C:/Users/wjbr/src/3DMMForever push origin c
```

---

## Self-review notes

**Coverage check:** Each spec section maps to a task:
- Selection model (Scene state, accessors): Task 1
- Composite undo: Task 2
- Shift-click toggle: Task 3
- Group move: Task 4
- Esc + tool collapse: Task 5
- Acceptance + backlog: Task 6

**Type consistency check:** All method names referenced match: `FIsActrSelected`, `CactrSelected`, `PactrSelectedAt`, `FToggleActrSelected`, `ClearSelection` (defined in Task 1, used in Tasks 3–5). `ActorMoveGroupUndo`, `PamguNew`, `FAddChild`, `Cchild`, `FUndo`, `FDo` (defined Task 2, used Task 4). `_paundGroup`, `_fSelToggleArmed`, `_pactrSelToggle`, `_xpSelToggleDown`, `_ypSelToggleDown`, `_CommitMoveUndo` (defined Task 3 / 4, used in Tasks 3–5).

**No placeholders:** No "TBD", "TODO", or "fill in details." Every code change is shown in full.

**Risks called out in the plan:**
- Step 1.5: kauai API uncertainty about `FSetIvMac` vs. iterative `Delete`. Plan tells the implementer to inspect `kauai/src/groups.h` and substitute.
- Step 2.3: macro pattern for `RTCLASS`/`ASSERT`/`MARKMEM` must mirror surrounding code.
- Step 3.4: `LwAbs` confirmation.
- Step 4.6: `socutil.h` include / forward declaration in `inc/movie.h`.
- Step 5.3: portable Esc constant — uses `#ifdef WIN/MAC` with a hint to substitute if a portable constant exists.

These are all small, locally-resolvable uncertainties — they do not invalidate the plan's structure.
