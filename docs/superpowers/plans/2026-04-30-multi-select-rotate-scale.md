# Multi-Select Rotate / Resize / Squash-Stretch — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: superpowers:subagent-driven-development or superpowers:executing-plans.

**Goal:** With N≥2 actors selected, the rotate (`toolRotateX/Y/Z`), uniform-scale (`toolResize`), and per-axis-scale (`toolSquashStretch`) tools transform the group around the **selection centroid** rather than touching only the primary actor.

**Spec:** `docs/superpowers/specs/2026-04-30-multi-select-rotate-scale-design.md`

**Architecture:** Add `Scene::FXyzSelectionCentroid` (averages positions of currently-alive selected actors). At mousedown, when `cactrSel ≥ 2` and the tool is one of the rotate/resize/squash tools, capture the centroid into new `MovieView::_xrPivot/_yrPivot/_zrPivot` fields and build the same `ActorMoveGroupUndo` that Phase 1 builds for `toolCompose`. At each drag tick, branch on `cactrSel ≥ 2`: iterate `PactrSelectedAt(i)`, compute per-actor `delta_i` from the pivot, call `FMoveRoute(delta_i)` then `FRotate/FScale/FPull` on the actor. `cactrSel == 1` falls through to the existing `Movie::FRotateActr/FScaleActr/FSquashStretchActr` path unchanged.

**Tech stack:** C++ (MSVC, x86), Kauai (`PDynamicArray`, `MovieUndo`), BRender scalars (`BRS`).

**Compat:** Pure runtime UI state. Nothing in this plan reaches `.3MM`/`.chk`. Original 1995 3DMM playback preserved.

**Testing approach:** Same as Phase 1 — no automated unit tests for engine code. Each task ends with build verification and a manual smoke checklist. Single-actor regression checks after each task.

---

## File map

| File | Role | Action |
|------|------|--------|
| `inc/scene.h` | Declare `FXyzSelectionCentroid` | Modify |
| `src/engine/scene.cpp` | Implement centroid averaging | Modify |
| `inc/movie.h` | Declare `_xrPivot/_yrPivot/_zrPivot` | Modify |
| `src/engine/movie.cpp` | Mousedown pivot capture + group-undo extension; multi-actor rotate/resize/squash drag | Modify |
| `plan.md` | Update UI-5 Phase 2+ entry to mark rotate/scale shipped | Modify |

No new files. No `.cht`/`.chh`. No `chomp` runs.

## Pre-flight

- [ ] Clean working tree on branch `c`. `git status` should be clean.
- [ ] Clean baseline build: `pushvc (invoke-vcvars -TargetArch x86 -HostArch AMD64); cmake --build build --target studio` succeeds.

---

## Task 1: `Scene::FXyzSelectionCentroid`

- [ ] **1a.** In `inc/scene.h`, add a public method declaration near `CactrSelected` / `PactrSelectedAt`:

```cpp
bool FXyzSelectionCentroid(BRS *pxr, BRS *pyr, BRS *pzr);
// Mean of selected actors' current world positions.
// fFalse if no selected actor is alive at the current frame.
```

- [ ] **1b.** In `src/engine/scene.cpp`, implement: iterate `i = 0 .. CactrSelected()-1`, fetch `PactrSelectedAt(i)`, query position via `pactr->Pbody()->GetPosition(&xr, &yr, &zr)`. Sum and divide by the live count. Return `fFalse` if zero live. Skip actors that aren't currently alive (use whatever predicate `FMoveRoute` uses to no-op — likely `pactr->Pbody() == pvNil` or an "in scene at this frame" check; verify in actor.cpp).

- [ ] **1c.** Build engine target:

```
pushvc (invoke-vcvars -TargetArch x86 -HostArch AMD64)
cmake --build build --target engine
```

Expected: clean compile, no warnings on the new symbol.

## Task 2: `MovieView` pivot fields

- [ ] **2a.** In `inc/movie.h`, near `_paundGroup` (line ~161), add:

```cpp
BRS _xrPivot, _yrPivot, _zrPivot; // Group-rotate/scale pivot, frozen at mousedown when cactrSel >= 2.
```

- [ ] **2b.** In `src/engine/movie.cpp`, in `MovieView::PmvuNew` / wherever `_paundGroup`-style fields are zeroed, init `_xrPivot/_yrPivot/_zrPivot` to `rZero`. (Grep for `_pactrSelToggle = pvNil` to find the init site.)

- [ ] **2c.** Build engine target. Expected: clean.

## Task 3: Mousedown — capture pivot and arm group-undo for rotate/resize/squash

- [ ] **3a.** In `src/engine/movie.cpp` `_MouseDown`, find the `case toolCompose: case toolRotateX: ... case toolSquashStretch:` block (around line 7094). Today it builds `ActorMoveGroupUndo` only when `Tool() == toolCompose`. Extend the gate to include rotate/resize/squash tools:

```cpp
bool fGroupTool = (Tool() == toolCompose) || (Tool() == toolRotateX) || (Tool() == toolRotateY) ||
                  (Tool() == toolRotateZ) || (Tool() == toolResize) || (Tool() == toolSquashStretch);
long cactrSel = (fGroupTool && Pmvie()->Pscen() != pvNil)
                    ? Pmvie()->Pscen()->CactrSelected()
                    : 1;
```

- [ ] **3b.** When `cactrSel >= 2`, also capture the pivot:

```cpp
if (!Pmvie()->Pscen()->FXyzSelectionCentroid(&_xrPivot, &_yrPivot, &_zrPivot))
{
    // No live actors -- fall back to single-actor flow.
    cactrSel = 1;
}
```

(Place this just before the existing `if (cactrSel >= 2)` block that allocates `pamgu`.)

- [ ] **3c.** Build studio target. Expected: clean.

- [ ] **3d.** Smoke: select one actor + the move tool, drag → still works (single-actor toolCompose). Select two actors + move tool, drag → still works (Phase 1 group-translate, no behavioral change from Task 3 alone).

## Task 4: Multi-actor rotate drag (`toolRotateX/Y/Z`)

- [ ] **4a.** In `src/engine/movie.cpp` `_MouseDrag`, find `case toolRotateX: case toolRotateY: case toolRotateZ:` (around line 7649). Inside, after computing `xa, ya, za`, branch on `cactrSel`:

```cpp
long cactrSel = (Pmvie()->Pscen() != pvNil) ? Pmvie()->Pscen()->CactrSelected() : 0;

if (cactrSel >= 2)
{
    // Build per-tick rotation matrix
    BMAT34 bmat;
    BrMatrix34Identity(&bmat);
    if (xa != aZero) BrMatrix34PostRotateX(&bmat, xa);
    if (ya != aZero) BrMatrix34PostRotateY(&bmat, ya);
    if (za != aZero) BrMatrix34PostRotateZ(&bmat, za);

    bool fAnyMoved = fFalse;
    bool fFwd = FPure(_grfcust & fcustCmd);
    for (long iactr = 0; iactr < cactrSel; iactr++)
    {
        PActor pa = Pmvie()->Pscen()->PactrSelectedAt(iactr);
        if (pa == pvNil || pa->Pbody() == pvNil) continue;

        BRS xr, yr, zr;
        pa->Pbody()->GetPosition(&xr, &yr, &zr);
        BRS dx = BrsSub(xr, _xrPivot), dy = BrsSub(yr, _yrPivot), dz = BrsSub(zr, _zrPivot);
        // delta_i = (R - I) * (pos_i - pivot)
        BRS rx = BrsAdd(BrsAdd(BrsMul(bmat.m[0][0], dx), BrsMul(bmat.m[1][0], dy)), BrsMul(bmat.m[2][0], dz));
        BRS ry = BrsAdd(BrsAdd(BrsMul(bmat.m[0][1], dx), BrsMul(bmat.m[1][1], dy)), BrsMul(bmat.m[2][1], dz));
        BRS rz = BrsAdd(BrsAdd(BrsMul(bmat.m[0][2], dx), BrsMul(bmat.m[1][2], dy)), BrsMul(bmat.m[2][2], dz));
        BRS dxr = BrsSub(rx, dx), dyr = BrsSub(ry, dy), dzr = BrsSub(rz, dz);

        bool fMovedThis = fFalse;
        pa->FMoveRoute(dxr, dyr, dzr, &fMovedThis, fmafNil);
        if (pa->FRotate(xa, ya, za, fFwd))
        {
            fAnyMoved = fTrue;
        }
    }

    if (fAnyMoved)
    {
        _CommitMoveUndo();
    }
}
else
{
    // existing single-actor path
    if (pmvie->FRotateActr(xa, ya, za, FPure(_grfcust & fcustCmd)))
    {
        if ((_paund != pvNil) && !Pmvie()->FAddUndo(_paund))
        {
            PushErc(ercSocNotUndoable);
            Pmvie()->ClearUndo();
        }
        ReleasePpo(&_paund);
    }
}
```

(Note: BRender uses row-major 3x4 matrices — `bmat.m[i][j]` is row `i`, col `j`; vector is row-vector × matrix. Verify the multiplication sense by writing a tiny smoke test before relying on the formulas above. If the rotation visibly goes the wrong way, transpose the indexing.)

- [ ] **4b.** `Pmvie()->Pbwld()->MarkDirty(); Pmvie()->MarkViews(); AdjustCursor(...);` after the if/else (existing code already does this — keep it once after the branch).

- [ ] **4c.** Build studio target. Expected: clean.

- [ ] **4d.** Smoke:
  - Select one actor, rotate-X drag → unchanged.
  - Select two actors widely separated, rotate-Z drag → both swing around the midpoint, both individually rotate.
  - Cmd-rotate (`fFromHereFwd`) → only future frames affected.
  - Single undo step reverts both actors.

## Task 5: Multi-actor uniform scale (`toolResize`)

- [ ] **5a.** Same shape as Task 4 in the `case toolResize:` block (around line 7691). Replace the per-actor `FRotate` block with:

```cpp
BRS s = brs2; // uniform scale factor
BRS sm1 = BrsSub(s, rOne);
for each selected actor pa:
    pa->Pbody()->GetPosition(&xr, &yr, &zr);
    BRS dxr = BrsMul(sm1, BrsSub(xr, _xrPivot));
    BRS dyr = BrsMul(sm1, BrsSub(yr, _yrPivot));
    BRS dzr = BrsMul(sm1, BrsSub(zr, _zrPivot));
    pa->FMoveRoute(dxr, dyr, dzr, &fMovedThis, fmafNil);
    pa->FScale(s);
```

- [ ] **5b.** Build studio target. Expected: clean.

- [ ] **5c.** Smoke:
  - Single-actor resize → unchanged.
  - Two-actor scale-up → actors move apart and individually grow.
  - Two-actor scale-down → actors move together and shrink.
  - Single undo reverts both.

## Task 6: Multi-actor squash/stretch (`toolSquashStretch`)

- [ ] **6a.** In `case toolSquashStretch:` (around line 7725). Today it calls `pmvie->FSquashStretchActr(brs2)` which calls `pactr->FPull(brs, BrsDiv(rOne, brs), brs)` (movie.cpp:3527). For multi-actor:

```cpp
BRS sx = brs2;                  // x scale
BRS sy = BrsDiv(rOne, brs2);    // y scale (inverse — stretch in y as squash in x/z)
BRS sz = brs2;                  // z scale
// (mirror what FSquashStretchActr does internally)

for each selected actor pa:
    pa->Pbody()->GetPosition(&xr, &yr, &zr);
    BRS dxr = BrsMul(BrsSub(sx, rOne), BrsSub(xr, _xrPivot));
    BRS dyr = BrsMul(BrsSub(sy, rOne), BrsSub(yr, _yrPivot));
    BRS dzr = BrsMul(BrsSub(sz, rOne), BrsSub(zr, _zrPivot));
    pa->FMoveRoute(dxr, dyr, dzr, &fMovedThis, fmafNil);
    pa->FPull(sx, sy, sz);
```

- [ ] **6b.** Build studio target. Expected: clean.

- [ ] **6c.** Smoke:
  - Single-actor squash → unchanged.
  - Two-actor squash → group flexes along axis around centroid.
  - Single undo reverts both.

## Task 7: Update `plan.md`

- [ ] **7a.** In `plan.md`'s UI-5 Phase 2+ section, move "Multi-target rotate / squash/stretch / scale" out of the remaining-work bullet list and into a new "Phase 2a shipped" subsection with commit references and a "deferred" subsection for the bullets still pending (marquee, costume, sooner/later, action menu intersect, etc.).

## Final verification

- [ ] **Full build:** `pushvc (invoke-vcvars -TargetArch x86 -HostArch AMD64); cmake --build build --target studio` — clean.
- [ ] **clang-format:** confirm `.clang-format` is satisfied on touched files.
- [ ] **Smoke matrix:** for each of {1 actor, 2 actors, 3 actors}, exercise {move, rotate-X, rotate-Y, rotate-Z, resize, squash} with and without Cmd. Confirm undo collapses to one step per drag.

## Risks / opens

- **Event bloat** (2x events per drag tick per actor) — accept for phase 2a. Future composite-event work tracked separately.
- **BRender matrix-index sense** — verify by smoke test before relying on the per-actor delta formulas. If rotation goes the wrong way, transpose `bmat.m[i][j]` indexing in Task 4a.
- **Squash on non-rigid groups** — geometrically correct, may visually surprise. Acceptable.
- **Cursor warp / "AdjustCursor"** — single-actor path warps cursor toward the rotated actor. For multi-actor, leaving the cursor at the user's mouse position is more natural. Match Phase 1's group-translate behavior.
