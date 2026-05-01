# Actor Tag Groups via Hashtags — Design

**Goal:** Let the user mark a multi-selection as a named group via Ctrl+G, then re-select that group later with one keystroke. Group membership is stored as `#tag` substrings inside the actor's existing display name, so it persists in the .3MM file with zero format change.

**Phase:** UI-5 Phase 3 (named/saved selection groups, called out as "Phase 2+ remaining work" in plan.md after the rotate/scale work).

**Compat:** Actor names are persisted via the existing `_pgstmactr` string table inside the movie chunk (movie.cpp:954, `Movie::FNameActr`). Original 1995 3DMM treats names as opaque strings — `#1` displayed alongside the actor name in roll-call is acceptable, the file still loads and plays. No new chunks, no new fields.

---

## Behavior

A **tag** is a `#` followed by one or more characters drawn from `[A-Za-z0-9_]`, terminated by whitespace or end-of-string. Examples: `#1`, `#boss`, `#room_a`. An actor's tags are derived by parsing its name string at access time — no separate storage.

An actor can have multiple tags. The display name `Wizard #boss #room1` puts the actor in groups `boss` and `room1`. Tags are case-insensitive (`#Boss` and `#boss` match). `#` not followed by an identifier character (e.g. `#`, `# `, `#!`) is not a tag.

### `Ctrl+G` — assign group to current selection

With N≥2 actors selected:

1. Walk the scene's roll-call, collect every existing `#N` numeric tag, find `Nfree = max+1` (or `1` if none).
2. For each selected actor, if it doesn't already have `#Nfree` (it shouldn't), append ` #Nfree` to its name via `Movie::FNameActr`.
3. One undoable step covering all renames (composite undo, mirroring `ActorMoveGroupUndo`).

Auto-numbering is the no-UI MVP. Phase B adds a small inline rename dialog so the user can name the group (`#boss` rather than `#3`); Phase A defaults to numeric.

With N=1 selected: same flow — single-actor tagging is fine, lets the user pre-build groups one at a time.

With N=0: no-op, beep.

### `Ctrl+Shift+G` — strip the most recent tag

For each selected actor, remove the **rightmost** `#tag` from its name (and any leading whitespace before it). Single undo step. Idempotent: actors without tags are skipped.

This is "ungroup the most recent tag" — simpler than picking a tag to remove. To strip a specific tag, the user can edit the name directly via the existing rename UI.

### `Ctrl+1` … `Ctrl+9` — select group by number

Replaces the current selection with all actors in the current scene whose name contains the corresponding `#N` tag. If no actors match, beep and leave selection unchanged. Numeric shortcuts are deliberately limited to `1`–`9` so the typical "few groups per scene" workflow is two keystrokes; named tags need a different path (Phase B: `Ctrl+/` opens a tag picker).

Match is **scene-local**. Actors in other scenes with the same `#1` tag are not affected — selection state is per-scene anyway.

### Roll-call display (Phase A: deferred)

Phase A keeps tags visible inline in actor names — no separate UI. Phase B will dim the `#tag` portion or move it to a sidebar column.

## Alternatives rejected

- **Separate group field on `Actor`/`MACTR`.** Cleaner data model but requires a chunk-format change. Per the user's compat requirement (memory: must round-trip with 1995 3DMM), this is off the table for this work.
- **Auxiliary "groups" chunk inside the movie.** Same problem — original 3DMM wouldn't preserve it on save.
- **Selection state persisted to disk.** Phase 1 explicitly skipped this; tags-in-names give the persistence-of-membership benefit without persisting the *current selection*.
- **Tags as a prefix-only convention (e.g. names must start with `#1: Wizard`).** More fragile against user editing; harder to support multi-tag.

## File map

| File | Role | Action |
|------|------|--------|
| `inc/scene.h` | Declare `FSelectActrsByTag(PCSZ pszTag)` and a tag-parse helper | Modify |
| `src/engine/scene.cpp` | Implement tag parser + tag-based selection | Modify |
| `inc/socutil.h` | Declare `ActorRenameGroupUndo` (composite undo for batch rename) | Modify |
| `src/engine/actredit.cpp` | Implement composite rename undo | Modify |
| `src/engine/movie.cpp` | `MovieView::FCmdKey` handles Ctrl+G, Ctrl+Shift+G, Ctrl+1..9 | Modify |
| `plan.md` | Update UI-5 section to mark phase 3 (groups via tags) as design+plan landed | Modify |

No new files. No `.cht`/`.chh`. No chunk-format change. No new on-disk field.

## Acceptance criteria

1. **Ctrl+G on N≥2 selected appends `#Nfree`** to each actor's name. `Nfree` is `max(existing-numeric-tags-in-scene) + 1`, or 1 if none.
2. **Ctrl+1 selects all actors with `#1`** in the current scene; replaces existing selection.
3. **Ctrl+Shift+G strips the rightmost tag** from each selected actor. Idempotent.
4. **One undo step** reverts a Ctrl+G or Ctrl+Shift+G group operation across all affected actors.
5. **Single-actor regressions:** all existing single-actor flows (rename via easel, roll-call, etc.) work unchanged. Editing a name through the existing rename UI to add/remove `#tags` manually works.
6. **Compat:** original 1995 3DMM still loads and plays movies that have hashtag-laden actor names. (Verified by inspection of the actor-name save path; no automated test.)
7. **Tags survive round-trip save/load** in the modern build.

## Risks / opens

- **`#` in user-typed names today.** Some movies might already have `#` in actor names (e.g. "Actor #2" as a manual numbering convention). After this lands, those become real tags and Ctrl+2 might select them. Acceptable — that's actually the desired behavior. Document in release notes.
- **Numeric-tag collisions across scenes.** A scene-local `Nfree` choice means scene A's `#1` and scene B's `#1` are independent groups. Probably fine; if confusing, Phase B can add a movie-wide "next free" mode.
- **Display ergonomics.** `Wizard #boss #room1 #important` is long. Phase B (separate display) addresses this — Phase A accepts the verbosity.
- **Renaming via roll-call.** The existing rename path stores the full name verbatim, so `#tags` round-trip without special handling. Verify by smoke test.
- **Ctrl+1..9 vs. existing shortcuts.** 3DMM's keyboard map is small but non-trivial; need to scan `FCmdKey` overrides across the codebase to confirm 1..9 are unbound. (Plan task 0.)

## Scope estimate

**Phase A (this spec):** ~3-5 days. Tag parser + selection accessor + 3 hotkey handlers + 1 composite undo class. No new UI, no chunk changes.

**Phase B (deferred):** named-tag picker, dimmed display, movie-wide tag operations. ~1 week.
