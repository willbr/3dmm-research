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

`kauai/src/chunk.h:27-29`:

```cpp
typedef ulong ChunkTagOrType;  // chunk tag/type
typedef ulong ChunkNumber;     // chunk number
typedef ulong ChildChunkID;    // child chunk id
```

These are the *identity* types for every chunk graph relationship — they appear in every chunk header, every parent-child link, every `kctg*` 4-byte tag literal. On disk they're 4 bytes today; under LP64 they'd silently become 8 bytes. **Every** on-disk struct that uses any of them changes shape, including most of the structs catalogued below.

**Fix:** redefine the typedefs as `uint32_t` (in `chunk.h`). Single-line change with massive blast radius — but *correct* blast radius. The kauai chunk file format spec (`kauai/doc/chunk.txt`) explicitly says these are 32-bit values; the `ulong` typedef was just the 1990s way of spelling that.

This change must land before the per-struct fixes — every struct edit below assumes `ChunkTagOrType` etc. are 4 bytes.

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

**Two options:**

1. **Split TAG into runtime-TAG and on-disk-TAG** (`TAG` and `TAGOnFile`?). Conversion at read/write boundaries. Cleaner long-term but high blast radius — touches every site that reads/writes a TAG.
2. **Replace `pcrf` field with a 4-byte filler under LP64/x64.** E.g., `union { PChunkyResourceFile pcrf; uint32_t _pcrfPad; };` plus a `static_assert(sizeof(TAG) == 16)` to pin it. Keeps source-call sites identical. Less clean but minimal-diff.

Option 1 is the right answer for 3DMMPlus. **Option 2 is the right answer for 3DMMForever** — preserves the wire format and 1995 compat without rewriting every TAG-handling site. Implementation: move `pcrf` out of `TAG` entirely and into a parallel `TagRuntimeData` map keyed by (sid, ctg, cno), or use the union-with-pad trick. The latter is simpler.

This is the single biggest decision in the audit. Defer to the user before implementing.

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

1. **Add layout assertions to current x86 build** as a "freeze" pass. One `static_assert` per on-disk struct, asserting the current x86 size. No semantic change. This catches any future accidental layout drift even before LP64 is on the table. Land first, alone.

2. **Redefine `ChunkTagOrType`/`ChunkNumber`/`ChildChunkID` as `uint32_t`** in `kauai/src/chunk.h`. Mass-rebuild check; no source-call changes needed because the underlying width is identical on x86. Verify with the `static_assert`s from commit 1.

3. **Replace bare `long`/`short`/`ulong` with `int32_t`/`int16_t`/`uint32_t`** in engine on-disk structs. One commit per file (`movie.cpp`, `scene.cpp`, `actrsave.cpp`, `mtrl.h`, `bkgd.h`, `tmpl.h`, `modl.h`, `msnd.h`, `actor.h`, plus the three TBOX/TDF/TDT files once inspected). Asserts from commit 1 verify each.

4. **Decide TAG strategy** with user (option 1 vs. option 2 above) and implement. This is the only commit that needs design alignment before coding.

5. **Inspect and fix `TBOXH`, `ThreeDFontF`, `ThreeDTextF`, `TAGF`, `CC`** — placeholders in the table above.

6. **Out of scope but follow-on:** kauai chunk-format internals (`ChunkyFilePrefix` et al.) and group on-file formats. Same techniques, separate audit because they're orthogonal to the engine layer.

## Done when

- Every on-disk struct in the engine has a `static_assert(sizeof(...) == N)` with N matching the current x86 layout.
- A LP64 build (later: simulated via `-Dlong=int32_t`-equivalent or actually built on Linux/macOS) produces byte-identical chunk output to the x86 build for `cd3/SAMPLES/BONGO.3MM` round-trip.
- The TAG decision is documented in this file and implemented.

## Out of scope

- Kauai chunk-format internals (`ChunkyFilePrefix`, `ChunkRepresentationBig/Small`, `EmbeddedChunkDescriptorOnFile`, `ChunkNumberMapEntry`).
- Kauai group on-file formats (`DynamicArrayOnFile`, `AllocatedArrayOnFile`, `GeneralGroupOnFile`, `StringTableOnFile`).
- `RegionOnFile` (no such type yet — Region is runtime-only).
- In-memory class member `long`s that aren't part of any on-disk struct.
- `kbom*` byte-order plumbing audit — that's Project 1 task 3, separate.
