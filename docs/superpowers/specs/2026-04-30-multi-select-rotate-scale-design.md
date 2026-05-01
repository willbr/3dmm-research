# Multi-Select Rotate / Resize / Squash-Stretch — Design

**Goal:** Extend Phase 1's multi-selection so that with N≥2 actors selected, the rotate (`toolRotateX/Y/Z`), uniform-scale (`toolResize`), and per-axis-scale (`toolSquashStretch`) tools transform the whole group around the **selection centroid** instead of operating on only the primary actor.

**Phase:** UI-5 Phase 2a (rotate/scale subset of the broader Phase 2 backlog: marquee select, costume change, sooner/later, action change, etc.).

**Compat:** Selection state and pivot are runtime-only. No on-disk format change. Original 1995 3DMM playback unaffected.

---

## Behavior

For each selected actor `i` per drag tick, given pivot `P` (frozen at mousedown) and per-tick rotation `R` or scale factors:

```
rotate:        new_pos_i = P + R · (pos_i - P)
uniform scale: new_pos_i = P + s · (pos_i - P)
squash/stretch:new_pos_i = P + (sx,sy,sz) ⊙ (pos_i - P)
delta_i        = new_pos_i - pos_i
```

Per actor per tick: `FMoveRoute(delta_i)` followed by `FRotate(R)` / `FScale(s)` / `FPull(sx,sy,sz)`. The local-orientation/scale change matches the existing single-actor behavior; the position delta is what the group-pivot adds.

**Pivot:** centroid (arithmetic mean) of selected actors' world positions, sampled once at mousedown via `Body::GetPosition`. Pivot is a fixed point of the per-tick transform, so freezing at mousedown stays self-consistent across the drag (no centroid drift).

**Cmd modifier (`fcustCmd → fFromHereFwd`)** carries through unchanged per actor.

**Single-actor (`cactrSel < 2`) path is untouched.** Falls through to the existing `Movie::FRotateActr` / `FScaleActr` / `FSquashStretchActr`.

**Dead/unborn actors** at the current frame are skipped both by the centroid average and by the per-tick loop (`FMoveRoute`/`FRotate` are no-ops on stale routes).

## Alternatives rejected

- **Per-actor pivot** (each actor pivots in place around its own origin). Trivial but visually weird for "group" semantics.
- **Bounding-box center** (vs. arithmetic mean). Negligibly different for typical scenes; centroid is cheaper.
- **Recompute centroid each tick.** Mathematically identical to frozen pivot for symmetric ops on a self-consistent set. Just churn.
- **New actor-side composite event (translate+rotate atomic).** Cleaner timeline output but requires new event type and event-replay support. Defer; phase-2a accepts the 2x event count.

## File map

| File | Action |
|------|--------|
| `inc/scene.h` | Declare `Scene::FXyzSelectionCentroid(BRS *pxr, BRS *pyr, BRS *pzr)` |
| `src/engine/scene.cpp` | Implement centroid (averages `Pbody()->GetPosition()` of selected actors that are alive at current frame; `fFalse` if zero alive) |
| `inc/movie.h` | Add `_xrPivot/_yrPivot/_zrPivot` BRS fields on `MovieView` |
| `src/engine/movie.cpp` | (a) `_MouseDown` rotate/resize/squash arms group-undo + captures pivot when `cactrSel ≥ 2`; (b) `_MouseDrag` rotate/resize/squash branches on `cactrSel ≥ 2` and runs per-actor `FMoveRoute + FRotate/FScale/FPull` loop |

No new files. No new `.cht`/`.chh`. No new undo class — reuse `ActorMoveGroupUndo` from Phase 1 (its child `ActorUndo` snapshots capture the full transform via `FDup(fTrue)`, so rotation/scale roll back cleanly).

## Acceptance criteria

1. **Single-actor rotate/resize/squash unchanged.** `cactrSel == 1` runs the existing `pmvie->FRotateActr/...` path.
2. **N≥2 rotate orbits the centroid.** Rotating a 3-actor selection visibly swings them around their common center; per-actor orientation also rotates.
3. **N≥2 resize scales positions away from / toward centroid.** Uniform scale up moves actors apart; scale down moves them together. Each actor's local size scales too.
4. **N≥2 squash/stretch on a group preserves centroid.** Per-axis scaling around the centroid; visually similar to flexing the group along an axis.
5. **Cmd-tweak (fFromHereFwd) honored per-actor.**
6. **One-undo-per-drag.** `ActorMoveGroupUndo` captures all selected actors at mousedown; the whole transform reverts as a unit.
7. **Dead actors skipped.** Selected actors that aren't alive at the current frame are excluded from centroid and per-tick loop; no crash.
8. **`.3MM` compat.** Original 1995 3DMM still loads/plays output movies (no format change).

## Risks / opens

- **Action-event bloat.** Each drag tick now writes 2 action events per actor (`FMoveRoute` + `FRotate/FScale/FPull`) where Phase 1 wrote 1 (`FMoveRoute` only). A 50-tick rotate of 4 actors writes 400 events. May want a future composite-event optimization, but probably not blocking.
- **Per-axis squash around centroid produces non-rigid distortion of the group.** Geometrically correct (per-axis scale around a non-actor point shears the spatial layout). Acceptable; user can fall back to single-select for pure in-place squash.
- **`fFromHereFwd` semantics across actors with different frame ranges.** "From here forward" is per-actor; actors not alive at current frame are skipped entirely. Match Phase 1.

## Scope estimate

~3-5 days. Same shape as Phase 1 (centroid math + per-tick loop) with no new selection plumbing or undo class. Risk: low-medium — touches three drag cases that share a structure with the already group-aware `toolCompose`.
