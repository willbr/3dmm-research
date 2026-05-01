# Actor Tag Groups via Hashtags — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: superpowers:subagent-driven-development or superpowers:executing-plans.

**Goal:** Add `Ctrl+G` to mark a multi-selection as a group (auto-numbered `#N` tag appended to each actor's name), `Ctrl+Shift+G` to strip the rightmost tag, and `Ctrl+1`..`Ctrl+9` to re-select group `#N`. Persistence rides on the existing actor-name string table — zero on-disk format change.

**Spec:** `docs/superpowers/specs/2026-04-30-actor-tag-groups-design.md`

**Architecture:** A tag is a `#` followed by `[A-Za-z0-9_]+`, parsed at access time from the actor's display name. `Scene::FSelectActrsByTag(PCSZ)` walks `_pglpactr`, parses each name, replaces the current selection with matching actors. `Ctrl+G` finds the next free `#N` in the scene's existing numeric tags and appends ` #N` to each selected actor's name via `Movie::FNameActr`. `Ctrl+Shift+G` removes the rightmost tag (and any preceding whitespace) from each selected actor's name. All batch rename ops bundle into a new `ActorRenameGroupUndo`. Hotkeys land in `MovieView::FCmdKey` next to the existing Esc and Ctrl+A handlers.

**Tech stack:** C++ (MSVC, x86), Kauai (`String`, `PDynamicArray`, `MovieUndo`).

**Compat:** Names are stored in `_pgstmactr` (movie-level string table) per `Movie::FNameActr` (movie.cpp:954). Original 1995 3DMM displays the full name including `#tags` — no format change, no on-disk field added.

**Testing approach:** Same as Phase 1 / 2a — no automated unit-test culture for engine code. Each task ends with build verification + a manual smoke checklist with specific click sequences.

---

## File map

| File | Role | Action |
|------|------|--------|
| `inc/scene.h` | Declare `FSelectActrsByTag` | Modify |
| `src/engine/scene.cpp` | Implement tag parser + tag-based selection | Modify |
| `inc/socutil.h` | Declare `ActorRenameGroupUndo` | Modify |
| `src/engine/actredit.cpp` | Implement composite rename undo (mirror `ActorMoveGroupUndo`) | Modify |
| `inc/movie.h` | (Optional) declare a small helper on `MovieView` if Ctrl+G logic gets long | Modify |
| `src/engine/movie.cpp` | Hotkey handlers in `FCmdKey`; tag-assign / tag-strip helpers | Modify |
| `plan.md` | Update UI-5 phase 2+ section with phase 3 status | Modify |

No new files. No `.cht`/`.chh`. No chunk-format change.

## Pre-flight

- [ ] **0a.** Clean working tree on branch `c`. `git status` clean.
- [ ] **0b.** Clean baseline build:
```
pushvc (invoke-vcvars -TargetArch x86 -HostArch AMD64)
cmake --build build --target studio
```

- [ ] **0c.** **Hotkey-collision audit.** Grep `FCmdKey` overrides in the codebase (`grep -rn "FCmdKey" inc src kauai/src`) and confirm `Ctrl+1`..`Ctrl+9` and `Ctrl+G` aren't already bound. If they are, decide whether to override (probably yes — the existing bindings are likely text-mode shortcuts that already defer to the base via `FTextMode()` like Ctrl+A does).

---

## Task 1: Tag parser + `Scene::FSelectActrsByTag`

- [ ] **1a.** In a new helper inside `src/engine/scene.cpp` (file-local), implement `_FNameContainsTag(PCSZ pszName, PCSZ pszTag)`:
  - Iterate `pszName`, find each `#`.
  - For each `#`, scan trailing `[A-Za-z0-9_]+`, lowercase compare against `pszTag` (which is also lowercased on entry).
  - Return `fTrue` on first match, `fFalse` if none.
- [ ] **1b.** In `inc/scene.h`, add public method:
```cpp
// Replace the current selection with every actor in the current scene
// whose display name contains the tag `#pszTag` (case-insensitive).
// fFalse on alloc failure mid-build, in which case selection is empty.
bool FSelectActrsByTag(PCSZ pszTag);
```
- [ ] **1c.** Implement in `src/engine/scene.cpp` modelled on `FSelectAllActrs`:
  - `ClearSelection()` first.
  - For each actor in `_pglpactr`, fetch its name via `Movie::FGetName(arid, &stn)` (or `Actor::GetName`), call `_FNameContainsTag`.
  - First match: `SelectActr(pactr)`. Subsequent matches: `FToggleActrSelected(pactr)` to add as extras (it's an add since the actor isn't already selected).
- [ ] **1d.** Build engine target. Expected: clean.
- [ ] **1e.** Smoke (manual): no UI yet, so call from a debugger watch or temporarily wire a test hotkey. Defer until task 4 wires `Ctrl+1`.

## Task 2: `ActorRenameGroupUndo`

- [ ] **2a.** In `inc/socutil.h`, declare a new composite-undo class. Mirror `ActorMoveGroupUndo`'s shape: holds an array of `(arid, oldName)` pairs.
- [ ] **2b.** In `src/engine/actredit.cpp`, implement:
  - `PamruNew()` factory.
  - `FAddChild(long arid, PString pstnOld)` records one rename's pre-state.
  - `FDo` is a no-op (the rename was already done inline; we just need the undo).
  - `FUndo` walks the children, calls `Movie::FNameActr(arid, pstn)` to restore each old name. Stops on first failure (Phase 1 pattern).
- [ ] **2c.** Build engine target. Expected: clean.

## Task 3: `Ctrl+G` — assign next free numeric tag

- [ ] **3a.** In `src/engine/movie.cpp`, add a helper (file-local or `MovieView`-private):
```cpp
long _LwNextFreeNumericTagInScene(PScene pscen);
// Walks pscen->Cactr() actors. For each, parse all #N tags, track max
// integer value seen. Returns max+1 (or 1 if none).
```
- [ ] **3b.** Add `MovieView::_FApplyGroupTagToSelection(long lwTagNum)`:
  - Open an `ActorRenameGroupUndo`.
  - For each selected actor (`Pmvie()->Pscen()->PactrSelectedAt(i)`):
    - Get current name into local `String` via `Pmvie()->FGetName(arid, &stn)`.
    - Record old name in the undo.
    - Append ` #N` to a new `String stnNew`.
    - Call `Pmvie()->FNameActr(arid, &stnNew)`.
  - Commit the undo via `Pmvie()->FAddUndo(pamru)`.
  - On any allocation failure mid-loop, release the undo and `PushErc(ercSocNotUndoable)`.
- [ ] **3c.** In `MovieView::FCmdKey`, add:
```cpp
if (pcmd->vk == 'G' && (pcmd->grfcust & fcustCmd) && !FTextMode()
    && !(pcmd->grfcust & fcustShift))
{
    if (Pmvie()->Pscen() != pvNil && Pmvie()->Pscen()->CactrSelected() >= 1)
    {
        long lwTag = _LwNextFreeNumericTagInScene(Pmvie()->Pscen());
        _FApplyGroupTagToSelection(lwTag);
    }
    return fTrue;
}
```
- [ ] **3d.** Build studio target.
- [ ] **3e.** Smoke:
  - Select two actors, Ctrl+G. Look at roll-call: each actor's name has ` #1` appended.
  - Select two more actors, Ctrl+G. They get ` #2`.
  - Undo (Ctrl+Z): the second Ctrl+G reverts; both actors lose their ` #2` tag in one step.

## Task 4: `Ctrl+1`..`Ctrl+9` — re-select numeric group

- [ ] **4a.** In `MovieView::FCmdKey`, add:
```cpp
if (pcmd->vk >= '1' && pcmd->vk <= '9' && (pcmd->grfcust & fcustCmd) && !FTextMode())
{
    if (Pmvie()->Pscen() != pvNil)
    {
        char szTag[8];
        snprintf(szTag, sizeof(szTag), "%c", (char)pcmd->vk);
        if (Pmvie()->Pscen()->FSelectActrsByTag(szTag))
        {
            Pmvie()->Pbwld()->MarkDirty();
        }
    }
    return fTrue;
}
```
(Adapt to the project's string-handling conventions — Kauai uses `String` / `Char` rather than raw `char[]`. Verify by reading nearby code.)
- [ ] **4b.** Build studio target.
- [ ] **4c.** Smoke:
  - Tag a group (Ctrl+G → `#1`).
  - Click empty space to clear selection.
  - Ctrl+1: the group is reselected.
  - Apply rotate: rigid group rotates as expected.

## Task 5: `Ctrl+Shift+G` — strip rightmost tag

- [ ] **5a.** Add a file-local helper `_FStripRightmostTag(PString pstn, bool *pfStripped)` that walks `*pstn` from right to left, finds the last `#`, and removes that `#tag` and any leading whitespace. Returns the modified string.
- [ ] **5b.** In `MovieView::FCmdKey`, add the Shift-Ctrl-G handler. Open an `ActorRenameGroupUndo`. For each selected actor: get name, strip rightmost tag, save undo + apply. Commit at end.
- [ ] **5c.** Build studio target.
- [ ] **5d.** Smoke:
  - Tag actors with `#1` (Ctrl+G), then `#2` (Ctrl+G).
  - Ctrl+Shift+G: `#2` is stripped.
  - Ctrl+Shift+G again: `#1` is stripped.
  - One Ctrl+Z reverts each strip as a single group undo.

## Task 6: `plan.md` update

- [ ] **6a.** In the UI-5 section, add a "Phase 3 (named selection groups via hashtags) shipped" subsection with commit references and remove the matching bullet from "Phase 2+ remaining work".

## Final verification

- [ ] **Full build:** `cmake --build build --target studio` clean.
- [ ] **Compat smoke:** save a movie that uses `#tag` names; reload in the modern build to confirm round-trip; if a 1995 3DMM install is available, attempt to load there and confirm playback works (the spec is the authoritative claim).
- [ ] **Smoke matrix:** for each of {1, 2, 3+ actors}, exercise {Ctrl+G, Ctrl+Shift+G, Ctrl+1..9}. Confirm undo collapses to one step per operation. Confirm renaming an actor manually (existing UI) also tags/untags correctly.
- [ ] **Roll-call regression:** open the roll-call UI; tagged actor names display correctly with the `#tag` suffix.

## Risks / opens

- **Hotkey collisions** — pre-flight 0c addresses this. If `Ctrl+G` or `Ctrl+1..9` are taken, defer to base when in text mode (mirror Ctrl+A pattern).
- **`Char` vs. `char`** — Kauai's `String` class likely wraps a fixed-size buffer with its own char type. Use `String::FAppendStn` and `Achs/Achw` accessors when implementing the parser; don't reach for `<cstring>`.
- **Multi-byte / DBCS names** — the original 3DMM might support multi-byte characters in actor names. The `[A-Za-z0-9_]` parser is ASCII-only; non-ASCII characters terminate the tag (so `#日本` would only tag as `#`). Acceptable since `#` itself is ASCII; document in release notes.
- **Existing actors with `#` in name** — see spec; expected behavior, not a bug.
- **Performance** — `FSelectActrsByTag` is `O(actors * name_length)`. For typical scenes (≤30 actors) this is trivial. No optimization needed.
