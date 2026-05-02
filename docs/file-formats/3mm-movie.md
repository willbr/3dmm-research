# `.3MM` movie file

A `.3MM` is the user document — a chunky file containing one movie. It's
opened by the studio's portfolio and by the kauai docb close-with-save
machinery; programmatically the entry points are
[`Movie::FLoadAutoSave`](../../src/engine/movie.cpp) and
[`Movie::FAutoSave`](../../src/engine/movie.cpp). The on-disk format is
the same one Microsoft 3DMM 1.0 wrote in 1995.

> **Compatibility constraint.** `.3MM` files saved by 3DMMForever must
> still load and play in original Microsoft 3DMM. This is the single
> hardest constraint in the project — see the project memory note
> `feedback_3mm_compat.md`. Don't change a `*OnFile` struct without
> auditing every consumer and round-tripping the result through a real
> 1995 build.

## Top-level shape

The chunky file contains one root `kctgMvie` chunk (its `cno` is
captured by `Movie::_cno`). The first `MVIE` chunk in the file is
treated as the movie root by `Movie::FLoadAutoSave`.

```
('SOC ', creator)               <- ChunkyFile::FSave(kctgSoc, ...)
   |
   v  (top-level chunks; not a parent/child link, just file membership)
('MVIE', _cno)                  <- MovieFilePrefix payload (8 B)
   |--chid 0--> ('SCEN', ...)   <- per-scene chunks, ordered by chid
   |--chid 1--> ('SCEN', ...)
   |--chid N--> ('SCEN', ...)
   |
   |--chidGstSource--> ('GST ', ...)  <- source-name string table
   |--chidGstRollCall->('GST ', ...)  <- roll-call (RollCallActorEntryOnFile array)
   |
   |--chid--> ('MSND', ...)     <- movie-level sounds (zero or more)
```

The exact `chid` constants live in `src/engine/movie.cpp` (`chidGstSource`
etc.). Scenes are addressed by their `chid` (0..N-1); reordering scenes
re-shuffles `chid`s with `ChunkyFile::ChangeChid`.

## `MVIE` chunk payload

The chunk body is a single `MovieFilePrefix` ([movie.cpp:163](../../src/engine/movie.cpp)):

```cpp
struct MovieFilePrefix {
    int16_t bo;       // byte order (kboCur on write)
    int16_t osk;      // OS kind that wrote this (koskCur)
    DataVersion dver; // chunky file version (kcvnCur, kcvnBack)
};
static_assert(sizeof(MovieFilePrefix) == 8, "MovieFilePrefix on-disk layout drift");
const ByteOrderMask kbomMfp = 0x55000000;
```

The reader checks `bo` against the host's native order and byte-swaps if
needed using `kbomMfp`. `dver` carries forward / backward version
numbers; see `kcvnCur` / `kcvnMin` / `kcvnBack` at the top of
`movie.cpp` for the current values.

## `SCEN` (scene) children

Each scene chunk is loaded by `Scene::FRead`. The payload starts with a
`SceneOnFile` (16 B fixed: byte order, OSK, first/last frame numbers,
transition kind). Children of a `SCEN` chunk include:

- `BKGD` — the scene's background.
- `ACTR` — actors placed in this scene (one chunk per actor).
- Sound events — variable-length `SceneSoundEvent` records, each holding
  a `(vlm, sty, fLoop, ctagc)` header followed by `ctagc` instances of
  `TagChildPairOnFile` (20 B each — `chid` + 16-byte `TAGOnFile`).

`Scene::SwapBytes` walks the SSE header (`kbomSse`) and each
`TagChildPair` array element (`kbomTagc`) when the file's byte order
differs from the host.

## `ACTR` (actor) children

Each `ACTR` chunk holds:

- An `ActorOnFile` header (template tag, position, …).
- A `GGAE` general-group chunk: the **actor event stream** (`_pggaev`).
  Each entry is a fixed `ActorEvent::Base` followed by an event-type-
  dependent variable part. The event types that embed `TAG` use
  `*OnFile` wire forms:

  | Event       | Wire struct       | Wire size | kbom mask        |
  |-------------|-------------------|-----------|------------------|
  | `aetCost`   | `CostumeOnFile`   | 28 B      | `kbomAevcost`    |
  | `aetSnd`    | `SoundOnFile`     | 44 B      | `kbomAevsnd`     |
  | `aetActn`   | `Action`          | 8 B       | `kbomAevactn`    |
  | `aetAdd`    | `Add`             | 20 B      | `kbomAevadd`     |
  | `aetSize`   | `Size`            | 12 B      | `kbomAevsize`    |
  | `aetPull`   | `Stretch`         | 12 B      | `kbomAevpull`    |

  Other event types (move, rotate…) have no `TAG` embed and pass through
  opaquely. See [`inc/actor.h`](../../inc/actor.h) for the full union.

## The roll call GST

The movie's "roll call" — the named, browser-visible list of actors
across all scenes — is a `GST ` chunk owned by `MVIE` at
`chidGstRollCall`. Each entry is a `RollCallActorEntryOnFile` (28 B
fixed: `arid`, `cactRef`, `grfbrws`, `TAGOnFile tagTmpl`). The runtime
form `RollCallActorEntry` embeds the full `TAG` (with runtime `pcrf`)
and is built by `Movie::_FReadRollCallExtra` reading the on-file array.

A roll-call entry can hold a `ksidUseCrf` template tag — meaning the
template chunk is embedded in this movie's autosave document rather
than referenced from a CD. Custom 3D-text templates work this way.

## The source GST

`MVIE`'s other GST child (`chidGstSource`) is a string table of source
names — used by the tag manager to display "please insert the *Socrates*
CD" prompts when a `(sid, ctg, cno)` reference can't be resolved. See
[`inc/tagman.h`](../../inc/tagman.h).

## Save path

`Movie::FAutoSave` (`movie.cpp:2480`):

1. Ensures the `MVIE` chunk exists (`pcfl->FAdd(sizeof(MovieFilePrefix), kctgMvie, ...)`).
2. Writes the current scene with `Scene::FWrite` and adopts the new
   `SCEN` chunk under `MVIE` at the correct `chid`.
3. Writes `_pgstSource` and `_pgstRollCall`, marshalling each entry
   through its `*OnFile` form.
4. `pcfl->FSave(kctgSoc)` — flushes the chunky index and chunk data to
   disk.

## See also

- [`chunky-files.md`](chunky-files.md) — the underlying chunk model.
- [`bmdl-models.md`](bmdl-models.md) — model chunks referenced from `ACTR`.
- [`audio-and-midi.md`](audio-and-midi.md) — `MSND` / `SND ` chunks.
- [`chunk-type-reference.md`](chunk-type-reference.md) — every tag in one place.
- [`../code-map/engine.md`](../code-map/engine.md) — where the
  `Movie` / `Scene` / `Actor` code lives.
