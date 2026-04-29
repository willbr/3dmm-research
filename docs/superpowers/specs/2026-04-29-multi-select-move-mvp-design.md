# Multi-Object Selection MVP — Move Tool Only

**Status:** Design accepted, awaiting implementation plan.
**Date:** 2026-04-29
**Scope:** Studio in-scene editing. Single feature, single implementation plan.

## Problem

`Scene` currently holds at most one selected actor (`Scene::_pactrSelected`,
`inc/scene.h:147`). When the user picks the move tool (`toolCompose`) and drags,
`MovieView::_MouseDrag` (`src/engine/movie.cpp:7401`) calls
`pactr->FMoveRoute(dxrWld, dyrWld, dzrWld, &fMoved, grfmaf)` on that single actor.
There is no way to translate two or more actors as a rigid group.

We want: a user can select multiple actors and, with the move tool, drag them
together so each receives the same world-space delta.

## Compatibility constraint

Selection is **runtime UI state only.** Nothing in this design writes to disk,
modifies any chunk type, or changes any scene event. Therefore movies created
after this lands continue to load and play in original 1995 Microsoft 3DMM
unchanged. This satisfies the
`feedback_3mm_compat.md` constraint recorded in MEMORY.md.

## Non-goals (explicit out-of-scope for MVP)

- Box-select / lasso / marquee. (Phase 2 candidate.)
- Multi-select for any tool other than `toolCompose`. Rotate, costume,
  sooner/later, action, copy/cut/paste, nuke, sound tools, and listener all
  collapse to single-select on mousedown — see "Tool interaction rules" below.
- Multi-select of text boxes. Mixed actor + tbox selection.
- Saved or named selection groups.
- Cross-scene selection. Selection clears on scene change.
- "Select all actors in scene" keyboard shortcut. (Easy follow-up.)
- Distinct visual styling for primary vs. secondary selected actors. All
  selected actors get the existing `Hilite()` highlight equally.

## User-visible behavior

### Selection model

| Action                                | Result                                                                  |
|---------------------------------------|-------------------------------------------------------------------------|
| Plain click on actor                  | Selection becomes exactly that actor (today's behavior).                |
| Plain click on empty space            | Selection cleared (today's behavior).                                   |
| **Shift-click on unselected actor**   | Actor added to selection. No drag starts even if mouse moves slightly.  |
| **Shift-click on selected actor**     | Actor removed from selection. No drag starts.                           |
| **Esc**                               | Selection fully cleared.                                                |
| Scene change                          | Selection cleared (the new scene starts with its own empty selection).  |
| Frame change, play start, play stop   | Selection persistence matches today's single-select behavior. Whatever the engine does to `_pactrSelected` today, do the same to the extras list. |
| Switching tools                       | Selection preserved. Next non-move-tool mousedown collapses it.         |

The shift-click toggle is detected at `_MouseUp` time by remembering at
`_MouseDown` that the click was a shift-on-actor. If the user dragged past
`kdxpDragTol` / `kdypDragTol` between down and up, the toggle is canceled and
the existing shift-drag semantics apply on the **primary** actor only (i.e., no
behavioral surprise vs. today). If the user did not drag, the toggle fires on
mouseup and the selection set is updated. Drag tolerances reuse whatever the
codebase already exposes; if none is exposed at studio level, the spec's
implementer adds a small constant `kdxpSelToggleTol = kdypSelToggleTol = 3` (px)
local to `movie.cpp`.

### Move tool with N≥2 selected

On `toolCompose` drag, every selected actor receives the same world-space delta
`(dxrWld, dyrWld, dzrWld)` via the existing API:

- Default: `pactr->FMoveRoute(dx, dy, dz, &fMoved, grfmaf)`
- Cmd held (`fcustCmd`): `pactr->FTweakRoute(dx, dy, dz, grfmaf)`
- Shift held (`fcustShift`): `grfmaf |= fmafEntireSubrte`
- `_fRespectGround`: `grfmaf |= fmafGround`

No new movement math. The translation is rigid — every actor moves by the same
delta — which is the only sensible default for "group move."

If a particular actor's `FMoveRoute` returns `fFalse` (off-stage, time-frozen,
or other reason), the drag continues for the remaining actors. The undo entry
covers only the actors whose move succeeded. This matches today's
"continue but log" behavior of the engine.

### Undo

A single user drag — from `cidMouseDown` to `cidMouseUp` for `toolCompose` —
produces one undo step regardless of how many actors moved. Internally this is
implemented as a small composite undo object (working name `ActorMoveGroupUndo`)
that owns N child `ActorUndo` objects and calls `FUndo` on each when invoked.
On redo it calls each child's `FRedo`.

The composite is built up across the drag: `_paund` (one slot today) is
generalized into a list of pending undos, one per moved actor. On the first
mousemove that produces motion in a given actor, that actor's `ActorUndo`
snapshot is captured and added. On `_MouseUp`, the composite is pushed to
`Pmvie()->FAddUndo()` as a single entry.

### Visual feedback

`Hilite()` is called on every actor in the selection set on selection change.
`Unhilite()` is called on every actor that is leaving the set. There is no
distinct "primary" highlight in MVP; primary is purely an internal concept used
to keep the existing `_pactrSelected` API valid for the 22 callers that read it.

## Architecture

### Data model — `Scene`

`inc/scene.h`:

```cpp
PActor _pactrSelected;        // primary (existing field, unchanged)
PDynamicArray _pglpactrSelExtra; // additional selected actors. pvNil when empty.
```

`_pactrSelected` keeps its meaning as "the primary selected actor." The 22
existing callers (`movie.cpp`, `actredit.cpp`, `splot.cpp`, `esl.cpp`,
`scene.cpp`) continue to read it without change. They are tools that act on a
single actor; per the tool-interaction rules they receive collapsed-to-primary
selection on mousedown anyway.

New public methods on `Scene`:

```cpp
bool FIsActrSelected(PActor pactr); // primary OR in extras
long CactrSelected(void);           // total count, 0/1/N
PActor PactrSelectedAt(long iactr); // 0 = primary, then extras in order
bool FToggleActrSelected(PActor pactr); // shift-click entry; returns fTrue on success
void ClearSelection(void);          // synonym for SelectActr(pvNil) for clarity at call sites
```

`SelectActr(pactr)` semantics are preserved: it clears `_pglpactrSelExtra` and
sets primary. All existing call sites stay correct.

When `_pactrSelected` is set to a new value or to `pvNil`, the extras list is
emptied and each leaving actor gets `Unhilite()`. When an actor is added or
removed via `FToggleActrSelected`, only that actor's hilite state changes.

### Selection invalidation

`Scene::RemActrCore(arid)` and any path that destroys an actor must scrub
`_pglpactrSelExtra` of any reference to the removed actor. This is in addition
to the existing `if (_pactrSelected == pactr) _pactrSelected = pvNil` logic.
Scene chop and full scene close already call into the same removal path, so
fixing it once covers all routes.

### MovieView state

`inc/movie.h` — `MovieView` gains:

```cpp
bool _fSelToggleArmed : 1;   // shift-clicked an actor at mousedown, awaiting mouseup
PActor _pactrSelToggle;      // actor under the cursor when the shift-click began
```

These are reset at every `_MouseDown` and consumed at every `_MouseUp`.

### Movement loop

In `src/engine/movie.cpp`, the `case toolCompose:` block in `_MouseDrag`
(currently `:7401`) is restructured:

```cpp
case toolCompose: {
    long cactr = pscen->CactrSelected();
    for (long iactr = 0; iactr < cactr; iactr++) {
        PActor pa = pscen->PactrSelectedAt(iactr);
        // existing single-actor logic, applied to pa
    }
}
```

The undo capture (`_paund`) becomes a list owned by the composite; the existing
`if ((_paund != pvNil) && !Pmvie()->FAddUndo(_paund))` pattern moves to mouseup
so the composite is built up across the drag and committed once.

### Esc handler

`MovieView` overrides `FCmdKey`:

```cpp
bool MovieView::FCmdKey(PCMD_KEY pcmd) {
    if (pcmd->vk == VK_ESCAPE) {
        if (Pmvie() && Pmvie()->Pscen()) {
            Pmvie()->Pscen()->ClearSelection();
            Pmvie()->InvalViews();
        }
        return fTrue;
    }
    return MovieView_PAR::FCmdKey(pcmd);
}
```

`VK_ESCAPE` is the Windows virtual-key constant (`0x1B`). The `MovieView`
command map (`src/engine/movie.cpp:5800`) does not currently bind `cidKey`, so
the override on `FCmdKey` is reached via the default Kauai dispatch
(`gob.cpp:17`). The Mac equivalent is already defined elsewhere in the codebase
and uses the same `pcmd->vk` field; spec implementer should confirm at coding
time.

## Tool interaction rules

| Tool                                                        | Behavior with multi-selection            |
|-------------------------------------------------------------|------------------------------------------|
| `toolCompose` (move)                                        | Honors full set. Group rigid translate.  |
| `toolDefault`, `toolActorSelect`                            | Selection toggles apply.                 |
| `toolPlace`                                                 | Unaffected. Place is a single-actor tool by construction (newly added actor). |
| All other tools (rotate, costume, sooner/later, action, copy, cut, paste, nuke, sound, listener, scene chop, etc.) | On mousedown over an actor, **collapse to single-select** (call `SelectActr(pactrUnderCursor)`). The existing single-actor code path runs unchanged. |

This rule is the safety valve for MVP risk. We do not introduce hidden
multi-target behavior in 20+ tools whose interactions we have not designed.

## Files touched (estimate)

| File                     | Changes                                                    | LOC |
|--------------------------|------------------------------------------------------------|-----|
| `inc/scene.h`            | new fields, new methods                                    | ~15 |
| `src/engine/scene.cpp`   | implement helpers, update `SelectActr`, scrub on actor removal, hilite-on-toggle | ~80 |
| `inc/movie.h`            | `_fSelToggleArmed`, `_pactrSelToggle`                      | ~3  |
| `src/engine/movie.cpp`   | `_MouseDown` (shift detect), `_MouseUp` (toggle apply, undo commit), `_MouseDrag` `toolCompose` (group loop), `FCmdKey` (Esc), tool collapse rule | ~80 |
| `src/engine/actor.cpp`   | `ActorMoveGroupUndo` composite (or factor existing undo to support batching) | ~50 |

Total: ~230 lines, three .cpp files, two .h files. No new chunk types. No
`.cht` / `.chh` changes. No `chomp` re-runs. No Kauai changes.

## Risks and mitigations

- **Per-actor `FMoveRoute` failure mid-batch.** Continue with the rest, only
  commit undo entries for actors whose move succeeded. Visual desync recovers
  on the next render.
- **An actor in the selection becomes invalid mid-drag** (e.g., another thread
  or callback removes it). `Scene::RemActrCore` scrubs `_pglpactrSelExtra`, so
  the next iteration of the drag loop reads the updated count and skips it.
- **Shift modifier collision** between "shift-click toggles selection" and
  "shift-drag means entire subroute." Resolved at mouseup via the
  `_fSelToggleArmed` flag and a small drag tolerance, with the design choice
  that **a movement past the tolerance suppresses the toggle and lets the
  existing shift-drag run on the primary actor only.** Behavior with no extras
  in the selection is bit-for-bit identical to today.
- **Stale `_pactrSelected` references.** Audited: 22 read sites today, all
  compatible with "primary stays primary."

## Acceptance criteria

1. With one actor selected, all behavior is bit-for-bit identical to today
   (regression bar).
2. Shift-clicking an unselected actor adds it to the selection; the actor's
   hilite turns on. No drag starts.
3. Shift-clicking a selected actor removes it; hilite turns off.
4. With N≥2 actors selected and `toolCompose` active, dragging on stage
   translates all selected actors by the same world-space delta. Cmd-drag
   tweaks the route on each. Shift-drag affects entire subroute on each.
5. The drag in (4) produces exactly one undo step that, when undone, restores
   all moved actors to their pre-drag positions.
6. Pressing Esc clears the entire selection (primary + extras).
7. Switching to any non-move tool and clicking an actor reduces selection to
   exactly that actor.
8. Deleting a selected actor (any path) does not leave a dangling pointer in
   the selection.
9. Original 1995 3DMM still loads and plays any movie produced with this
   feature, since nothing is persisted.

## Open follow-ups (deliberately deferred)

- Box-select.
- Multi-target rotate / costume / sooner-later.
- Distinct primary highlight vs. secondary highlight.
- `Ctrl+A` / `Cmd+A` "select all in scene."
- Multi-select of text boxes.

These belong in a future plan.md UI-N entry once the MVP has shipped and we
know what selection ergonomics actually want.
