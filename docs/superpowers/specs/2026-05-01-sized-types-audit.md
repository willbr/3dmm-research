# Sized-types audit (Project 1 task 4)

**Goal:** find every on-disk struct in `engine/` and `inc/`, identify fields that change shape under LP64 (`long` becomes 8 bytes on Linux/macOS x86_64), and decide a portable replacement per field.

**Why this is dangerous:** Windows x64 keeps `long` at 32 bits (LLP64), so a Windows-only x64 build hides the bug. The first `.3MM` open on a Linux/macOS x64 build silently produces garbage — and the existing `blck.Cb() != size(struct)` runtime check will *catch the mismatch* but only because the struct grew, not because the data is wrong. Tests in `tests/geometry_test.cpp` (§0a item 2) cover the arithmetic side; this audit covers the layout side.

## Type-stability cheat sheet

| Type | LLP64 (Win x64) | LP64 (Linux/Mac x64) | On-disk safe? |
|---|---|---|---|
| `bool` (Kauai) | 1 byte | 1 byte | ✅ stable |
| `byte` = `unsigned char` | 1 | 1 | ✅ |
| `short` / `ushort` | 2 | 2 | ✅ |
| `int` | 4 | 4 | ✅ |
| **`long` / `ulong`** | **4** | **8** | ❌ **widens** |
| `float` / `double` | 4 / 8 | 4 / 8 | ✅ |
| pointer | 8 | 8 | ❌ widens vs. x86 (4) |
| `BRS` (`br_scalar` = `br_fixed_ls` or `float`) | 4 | 4 | ✅ stable |
| `BRA` (`br_angle` = `br_fixed_luf`, 16-bit fixed) | 2 | 2 | ✅ |
| `br_ufraction` (16-bit fixed) | 2 | 2 | ✅ |
| `ChunkTagOrType` = `ulong` | **4** | **8** | ❌ **see below** |
| `ChunkNumber` = `ulong` | **4** | **8** | ❌ **see below** |
| `ChildChunkID` = `ulong` | **4** | **8** | ❌ **see below** |
| `tribool` (enum, MSVC = 4 bytes) | 4 | 4 | ✅ |

Action: replace bare `long` → `int32_t` and bare `short` → `int16_t` in every on-disk struct. Replace `ulong` → `uint32_t`. Don't touch types that already have explicit-width semantics (`BRS`, `BRA`, etc.).

## ⚠ Critical finding: `ChunkTagOrType` / `ChunkNumber` / `ChildChunkID` widen under LP64

`kauai/src/chunk.h:27-29` (was):

```cpp
typedef ulong ChunkTagOrType;  // chunk tag/type
typedef ulong ChunkNumber;     // chunk number
typedef ulong ChildChunkID;    // child chunk id
```

These are the *identity* types for every chunk graph relationship — they appear in every chunk header, every parent-child link, every `kctg*` 4-byte tag literal. On disk they're 4 bytes today; under LP64 they'd silently become 8 bytes. **Every** on-disk struct that uses any of them changes shape, including most of the structs catalogued below.

**Resolved (2026-05-01):** redefined as `uint32_t` in `kauai/src/chunk.h` — commit `a5dc721`. The kauai chunk file format spec (`kauai/doc/chunk.txt`) explicitly says these are 32-bit values; the `ulong` typedef was just the 1990s way of spelling that. The 32 `static_assert(sizeof(*OnFile) == N)` sites added during the freeze pass verify no on-disk struct shifted on x86 (uint32_t and ulong are interchangeable on LLP64). On LP64 these typedefs now stay 32 bits where ulong would have widened.

## ⚠ Critical finding: `TAG` embeds a runtime pointer that's already being serialized

`inc/tagman.h:48-60`:

```cpp
struct TAG
{
    long sid;
    PChunkyResourceFile pcrf;  // <-- runtime pointer mixed into an on-disk struct
    ChunkTagOrType ctg;
    ChunkNumber cno;
};
const ByteOrderMask kbomTag = 0xFF000000;
```

Today on x86: 4 + 4 + 4 + 4 = 16 bytes. The BOM mask `0xFF000000` byteswaps 4 longs of 4 bytes = 16 bytes — confirming the on-disk size of TAG is 16 bytes including a 4-byte slot for `pcrf`. That slot holds garbage on disk (the pointer value at the moment the struct was written — meaningless to the reader, which always sets `pcrf` to its own value).

`TAG` is embedded directly in many on-disk structs (`MACTR.tagTmpl`, `ActorChunkOnFile.tagTmpl`, `BackgroundDefaultSound.tagSnd`, `ActorEvent::Costume.tag`, `ActorEvent::Sound.tag`, `TagChildPair.tag`, etc.). Under x64 LLP64 `pcrf` becomes 8 bytes → `sizeof(TAG)` becomes 20 → every embedding struct's layout shifts by 4 bytes per TAG → first read fails the `blck.Cb()` check.

**Resolved (2026-05-01):** Option 1 (split TAG / TAGOnFile, marshal at I/O).

Implementation:
- `inc/tagman.h` adds `struct TAGOnFile` (16 bytes always: int32 sid + uint32
  pcrf-pad + uint32 ctg + uint32 cno), plus `TAGOnFile::From(const TAG&)` and
  free function `TagFromOnFile(PTAG, const TAGOnFile&)` for conversion.
- For structs read/written via `blck.FReadRgb` / `pcfl->FPutPv` (one-shot I/O
  with explicit size), the embedded `TAG` is replaced with `TAGOnFile`
  directly: `ActorChunkOnFile.tagTmpl`, `ThreeDTextF.tagTdf`,
  `BackgroundDefaultSound`. No marshal layer needed — the OnFile struct *is*
  the runtime form for these.
- For structs stored in kauai containers (GG / GST / DynamicArray) where the
  container does verbatim memcpy I/O (no per-entry hook), the runtime struct
  keeps a full `TAG` with pcrf, and a parallel wire-format `XxxOnFile` struct
  is defined. The I/O sites marshal entry-by-entry: read into transient
  on-file container, walk into runtime container, drop on-file copy. Save is
  the mirror. Affected sites:
  - `RollCallActorEntry` (movie.cpp, GST) — commit `fe09d0d`
  - `Costume` / `Sound` (actor.h + actrsave.cpp, GG `_pggaev`) — `938e9b9`
  - `TagChildPair` in `SceneSoundEvent` and sevtSetBkgd's bare TAG (scene.cpp,
    GG `_pggsevFrm` / `_pggsevStart`) — `46d4756`
  - `_pgltagSnd` array of TAGs (tmpl.cpp, DA) — `445d38e`
- Runtime `CachedTag` (tagl.cpp) is allowed to grow on x64 — TagList is built
  fresh by enumerating scenes, never serialised — commit `cf73d87`.
- Aspirational `static_assert(sizeof(TAG) == 16)` in tagman.h was relaxed to
  x86-only — commit `bdc6715`.

Net result: every TAG embed in the codebase is either at the I/O boundary as
TAGOnFile, or wrapped in a runtime-only container that marshals at I/O. The
.3MM wire format is byte-for-byte unchanged on every architecture. Runtime
TAG is free to grow.

## Per-struct catalogue

Surveyed: `inc/`, `src/engine/`. Excluded: kauai chunk-format internals (`ChunkyFilePrefix`, `ChunkRepresentationBig`, `ChunkRepresentationSmall`, etc. in `kauai/src/chunk.cpp`) and groups (`DynamicArrayOnFile`, `AllocatedArrayOnFile`, `GeneralGroupOnFile`, `StringTableOnFile` in `kauai/src/groups*.cpp`) — those are a separate, larger audit because they govern the chunky-file container itself, not the engine's per-chunk payloads.

### Engine chunk-payload structs

| Struct | Location | Bare `long` | Bare `short` | `ulong` | Embeds | Notes |
|---|---|---|---|---|---|---|
| `MovieFilePrefix` | `movie.cpp:163` | — | bo, osk | — | `DataVersion` | clean, just bo+osk+dver |
| `MACTR` | `movie.cpp:174` | arid, cactRef | — | grfbrws | `TAG` | TAG is the layout risk |
| `SceneEvent` | `scene.cpp:83` | nfrm | — | — | `SceneEventType` (enum, 4) | trivial |
| `SceneEventPause` | `scene.cpp:67` | dts | — | — | `WaitReason` (enum) | |
| `SceneOnFile` | `scene.cpp:95` | nfrmLast, nfrmFirst | bo, osk | — | `TRANS` (enum) | |
| `TagChildPair` | `scene.cpp:111` | — | — | — | `ChildChunkID`, `TAG` | both embeds widen — see critical findings above |
| `SceneSoundEvent` | `scene.cpp:122` | vlm, sty, ctagc | — | — | `bool` | trailing variable-length `TagChildPair[]` — also affected |
| `ActorChunkOnFile` | `actrsave.cpp:29` | arid, nfrmFirst, nfrmLast | bo, osk | — | `RoutePoint` (3×BRS), `TAG` | TAG layout risk |
| `MaterialOnFile` | `mtrl.h:30` | — | bo, osk | — | `br_colour`, `br_ufraction`x3, `BRS` | `byte`s for palette indices — fine |
| `CustomMaterialOnFile` | `mtrl.h:21` | ibset | bo, osk | — | — | trivial |
| `BackgroundFile` | `bkgd.h:20` | — | bo, osk, swPad | — | byte×2 | trivial |
| `BackgroundDefaultSound` | `bkgd.h:84` | vlm | bo, osk | — | bool, `TAG` | TAG risk |
| `LightPosition` | `bkgd.h:34` | lt | — | — | `BMAT34`, `BRS` | check `BMAT34` shape |
| `CameraPosition` | `bkgd.h:55` | — | bo, osk, swPad | — | `BRS`x2, `BRA`, `APOS` (3×BRS or BVEC3), `BMAT34` | clean once BMAT34 is verified |
| `ModelOnFile` | `modl.h:20` | — | bo, osk, cver, cfac | — | `BRS`, `BRB`, `BVEC3` | clean, all 4-byte |
| `TemplateOnFile` | `tmpl.h:54` | — | bo, osk, swPad | grftmpl | `BRA`x3 | grftmpl widens |
| `ActionChunkOnFile` | `tmpl.h:67` | — | bo, osk | grfactn | — | grfactn widens |
| `MovieSoundFile` | `msnd.h:76` | sty, vlmDefault | bo, osk | — | `bool` | clean |
| `AnimationCel` (was `CEL`) | `tmpl.h:46` | — | — | — | `ChildChunkID` (widens), `BRS` | trailing `CPS[]` variable part |
| `CPS` | `tmpl.h:31` | — | chidModl, imat34 | — | — | trivial — both already 16-bit |
| `RoutePoint` | `actor.h:28` | — | — | — | `BRS`x3 | clean |
| `RouteDistancePoint` | `actor.h:52` | — | — | — | `RoutePoint`, `BRS` | clean |
| `RouteLocation` | `actor.h:110` | dnfrm | — | — | `int irpt`, `BRS dwrOffset` | uses `int` (good) and bare `long` (bad) — see also embedded inside `ActorEvent::Base` |
| `ActorEvent::Base` | `actor.h:155` | aet, nfrm | — | — | `RouteLocation` | |
| `ActorEvent::Stretch` / `Add` | `actor.h:192-211` | — | — | — | `BRS`, `BRA` | clean |
| `ActorEvent::Action` | `actor.h:214` | anid, celn | — | — | — | trivial |
| `ActorEvent::Costume` | `actor.h:221` | ibset, cmid | — | — | `tribool` (4 bytes), `TAG` | TAG risk |
| `ActorEvent::Sound` | `actor.h:230` | vlm, celn, sty | — | — | `tribool`x3, `ChildChunkID`, `TAG` | both ChildChunkID and TAG risk |
| `TBOXH` | `tbox.cpp:1785` | (read needed) | | | | TODO inspect |
| `ThreeDFontF` | `tdf.cpp:42` | (read needed) | | | | TODO inspect |
| `ThreeDTextF` | `tdt.cpp:70` | (read needed) | | | | TODO inspect |
| `TAGF` / `CC` | `tagl.cpp:26,38` | (read needed) | | | | TODO inspect (TagList on-file format) |

### Embedded BRender types (verified stable)

Empirical sizes from the freeze-pass probe:

- `BRS` = 4 bytes (`br_scalar` = `br_fixed_ls` or `float`)
- `BRA` = **2 bytes** (`br_angle` = `br_fixed_luf` = 16-bit fixed) — *not 4 as I'd initially assumed*
- `br_colour` = 4 bytes
- `br_ufraction` = **2 bytes** (16-bit fixed)
- `BVEC3` = 12 bytes (3 × BRS)
- `BMAT34` = 48 bytes (12 × BRS)
- `BRB` (br_bounds) = 24 bytes (2 × BVEC3)

All scale with BRS only, which is 4 bytes either way. Stable under LLP64 and LP64.

### Bug-gate / non-on-disk fields to leave alone

Many class member variables use bare `long` for runtime counters / IDs / etc. Those are not on-disk and don't need touching for compat — but they may show up as size mismatches when an LP64 build links against the engine. Out of scope for this audit (separate task: in-memory `long` vs. `int32_t` cleanup).

## Recommended commit sequence

Each commit independently revertible; each pinned by `static_assert(sizeof(...) == N)` so the LP64 build will refuse to link if a future edit drifts the layout.

1. **✅ Add layout assertions to current x86 build** as a "freeze" pass. Done in-tree across the rename/marshal commits — every entry in the per-struct catalogue now has a corresponding `static_assert(sizeof(...) == N)` on the wire-format type (33 sites; see `grep "static_assert.*sizeof" {src/engine,inc}`).

2. **✅ Redefine `ChunkTagOrType`/`ChunkNumber`/`ChildChunkID` as `uint32_t`** in `kauai/src/chunk.h` — commit `a5dc721`. x86 build byte-identical; LP64 build now safe from chunk-header widening.

3. **✅ Replace bare `long`/`short`/`ulong` with `int32_t`/`int16_t`/`uint32_t`** in engine on-disk structs. Done as one commit per file: `msnd.h` (cc30238), `mtrl.h` (db5f613), `bkgd.h` (ee206f6), `tmpl.h` (5b11722), `actor.h` (9c6442c), `movie.cpp` (eb1dfbf), `scene.cpp` (322d719), `actrsave.cpp` (3b91bfb), `tbox.cpp/tdf.cpp/tdt.cpp` (6f995d7). `modl.h` already used `short` only and needed no change.

4. **✅ Decide TAG strategy** with user (option 1) and implement — done; see the "marshal at the I/O boundary" resolution above.

5. **✅ Inspect and fix `TBOXH`, `ThreeDFontF`, `ThreeDTextF`, `TAGF`, `CC`** — done: `TextBoxOnFile` (was `TBOXH`), `ThreeDTextF`, `ThreeDFontF` all have static_asserts AND explicit-width fields. `ChidCtgPair` (was `CC`) uses ChildChunkID + ChunkTagOrType, both already widened to uint32_t in step 2. `CachedTag` (was `TAGF`) is documented runtime-only, never serialized.

6. **Out of scope but follow-on:** kauai chunk-format internals (`ChunkyFilePrefix` et al.) and group on-file formats. Same techniques, separate audit because they're orthogonal to the engine layer.

## Done when

- ✅ Every on-disk struct in the engine has a `static_assert(sizeof(...) == N)` with N matching the current x86 layout.
- A LP64 build (later: simulated via `-Dlong=int32_t`-equivalent or actually built on Linux/macOS) produces byte-identical chunk output to the x86 build for `cd3/SAMPLES/BONGO.3MM` round-trip. **Not yet attempted.**
- ✅ The TAG decision is documented in this file and implemented.

## Remaining LP64 risk after this audit

These didn't change shape *inside* the audited structs, but would still bite an LP64 build:

- ✅ `aetFreeze` event variable-part — `kcbVarFreeze` widened to `sizeof(int32_t)` and 7 read/write locals converted from `long` to `int32_t` — commit `bde00d7`.
- ✅ `ByteOrderMask` typedef widened from `ulong` to `uint32_t` in `kauai/src/utilint.h` — commit `eda46fe`. Runtime-only typedef, no on-disk impact.
- Format-string sites (`printf("%lu", cno)` etc.) across `src/tools/movie_chomp.cpp` etc. Cosmetic on x86 (warns clean), miscompile on LP64. Mass `%u`/`%d` sweep when LP64 is on the table.
- Win64 LLP64 pointer-in-CHID-slot bugs in kauai container code. Fixed for `sevtAddActr`/`sevtAddTbox` as a side effect of the TagChildPair work; other instances likely exist and need a separate sweep.

## Out of scope

- Kauai chunk-format internals (`ChunkyFilePrefix`, `ChunkRepresentationBig/Small`, `EmbeddedChunkDescriptorOnFile`, `ChunkNumberMapEntry`).
- Kauai group on-file formats (`DynamicArrayOnFile`, `AllocatedArrayOnFile`, `GeneralGroupOnFile`, `StringTableOnFile`).
- `RegionOnFile` (no such type yet — Region is runtime-only).
- In-memory class member `long`s that aren't part of any on-disk struct.
- `kbom*` byte-order plumbing audit — that's Project 1 task 3, separate.
