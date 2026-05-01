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
- `Actor::PgltagFetch` had a stray `PggRead` + `_FIsIaevTag` loop that cast
  variable-part bytes to runtime `Costume*`/`Sound*` directly off the
  on-file GG. Refactored: extracted `_PggaevMarshalFromOnFile` from
  `_FReadEvents` and wired it through `PgltagFetch` so both call sites see
  runtime layout — commit `6cbf6a1`.

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

6. **Follow-on, surveyed in pass 2 below:** kauai chunk-format internals (`ChunkyFilePrefix` et al.) and group on-file formats. Same techniques, separate implementation project because they sit at the bottom of the dependency stack and a break here breaks the build itself.

## Done when

- ✅ Every on-disk struct in the engine has a `static_assert(sizeof(...) == N)` with N matching the current x86 layout.
- A LP64 build (later: simulated via `-Dlong=int32_t`-equivalent or actually built on Linux/macOS) produces byte-identical chunk output to the x86 build for `cd3/SAMPLES/BONGO.3MM` round-trip. **Not yet attempted.**
- ✅ The TAG decision is documented in this file and implemented.

## Remaining LP64 risk after this audit

These didn't change shape *inside* the audited structs, but would still bite an LP64 build:

- ✅ `aetFreeze` event variable-part — `kcbVarFreeze` widened to `sizeof(int32_t)` and 7 read/write locals converted from `long` to `int32_t` — commit `bde00d7`.
- ✅ `ByteOrderMask` typedef widened from `ulong` to `uint32_t` in `kauai/src/utilint.h` — commit `eda46fe`. Runtime-only typedef, no on-disk impact.
- ✅ Format-string sites in `movie_chomp.cpp` for fields that became int32/uint32 in step 3 — `%ld`/`%lu` → `%d`/`%u` for `RollCallActorEntryOnFile.{arid,cactRef,grfbrws}`, `ActorEvent::Base.nfrm`, `RouteLocation.dnfrm`, `SceneEvent.nfrm` — commit `a4c478c`. BRS sites (still `long`-typedef'd via BRender) and the generic GST-extra dumper deferred (BRender modernization is a separate story; the generic dumper is structurally broken on LP64 in ways format strings can't fix).
- ✅ `sevtChngCamera` / `sevtPause` GG variable-part sizing — pinned 4 sites to `size(int32_t)` / `size(SceneEventPause)` instead of `size(long)` / `size(long)*2`; routed `picamOld`/`icam`/`icamNext` GG accesses through int32_t scratch where the public Scene::FChangeCamCore signature still takes `long` — commit `e5c7c15`.
- **Out of scope (Project 2 — kauai modernization):** pervasive `size(long)` use inside kauai itself (`groups.cpp` LogicalOffsetAndCount swap unit, `chcm.cpp` codec word size, `screxe.cpp` script word size, `rtxt.cpp` MPE swap, etc.) — these encode a "long is the natural word size" assumption that runs through the whole library; `utilint.cpp:407` literally `Assert(size(long) == 4)`. Modernizing kauai to use explicit int32_t for its serialized word abstraction is a separate effort the size of Project 1 itself.

## Pass 2: Kauai chunk-format internals (surveyed 2026-05-01)

Pass 1 ended at the engine boundary because the kauai container format is
orthogonal — it sits one layer below the engine's chunk *payloads*. But every
.3MM file starts with a `ChunkyFilePrefix`, holds an index of
`ChunkRepresentation*`, and the GG/GST containers wrap their entries in
`*OnFile` headers. All of these embed bare `long` and the `FilePosition` typedef
(which is `typedef long FilePosition;` in `kauai/src/file.h:30`). Under LP64
every one of them widens, breaking compatibility with the 1995 .3MM wire format
(which is what foone's MIT release of the original codebase reads).

`ChunkTagOrType` / `ChunkNumber` / `ChildChunkID` were already redefined as
`uint32_t` in pass 1, so anything composed purely of those is already stable.
The remaining trouble is `long`-typed fields and `FilePosition`.

### Kauai per-struct findings

| Struct | File:line | x86 size | LP64 size | Layout drift cause | Resolution sketch |
|---|---|---|---|---|---|
| `ChunkyFilePrefix` | `chunk.cpp:116` | 128 | 240 | 5×long, 3×FilePosition, `long rglwReserved[23]` | Pin all 28 longs to int32_t. The `lwMagic` field is also `long` — pin to int32_t. The reserved array becomes `int32_t rglwReserved[23]`. |
| `FreeSpaceMap` | `chunk.cpp:135` | 8 | 16 | FilePosition + long | Pin both to int32_t (matches what kauai/doc/chunk.txt says is on disk). |
| `ChunkRepresentationBig` | `chunk.cpp:155` | 32 | 56 | FilePosition + 5×long | Pin to int32_t. The runtime `_pggcrp` GG stores these directly as `_cbFixed` bytes per entry, and `_cbFixed` is written into `GeneralGroupOnFile.cbFixed` as the on-disk per-entry stride — so this struct's wire layout *is* the index format. Critical. |
| `ChunkRepresentationSmall` | `chunk.cpp:219` | 20 | 24 | FilePosition only (others are ushort/ulong = 16/32) | Pin FilePosition to int32_t. Note the `luGrfcrpCb` field packs grfcrp + cb (24-bit) into one ulong — already pinned via uint32_t typedef change would be needed (currently still `ulong`). |
| `EmbeddedChunkDescriptorOnFile` | `chunk.cpp:574` | 24 | 32 | 2×long (cb, ckid) | Pin to int32_t. Used by `FWriteChunkTree` / `PcflReadForestFromFlo` for embedded chunk graphs. |
| `ChunkNumberMapEntry` | `chunk.cpp:3733` | 12 | 12 | none | ✅ Stable — pure ChunkTagOrType + 2×ChunkNumber. |
| `DynamicArrayOnFile` | `groups.cpp:465` | 12 | 20 | 2×long (cbEntry, ivMac) | Pin to int32_t. This is the header for *every* DynamicArray on disk. |
| `AllocatedArrayOnFile` | `groups.cpp:815` | 16 | 28 | 3×long | Pin to int32_t. Header for every AllocatedArray on disk. |
| `GeneralGroupOnFile` | `groups.cpp:1114` | 20 | 36 | 4×long (ilocMac, bvMac, clocFree, cbFixed) | Pin to int32_t. Header for every GG on disk — including the chunky-file index itself (`_pggcrp` is a GG). |
| `StringTableOnFile` | `groups2.cpp:73` | 20 | 36 | 4×long | Pin to int32_t. Header for every GST on disk. |
| `LogicalOffsetAndCount` | `groups.h:253` | 8 | 16 | 2×long (bv, cb) | Pin to int32_t. This is the *per-entry* descriptor inside GG variable storage — one per GG entry, written sequentially after the `GeneralGroupOnFile` header. **Doubly critical**: this is not just a one-time header, it scales with entry count. The GG byteswap path swaps "ivMac longs starting at bv" — that swap unit must change too. |

### `FilePosition`: runtime vs on-disk

`typedef long FilePosition;` at `file.h:30`. Used by `FileObject` for seeking
and at-position reads/writes. Two competing forces:

1. **On-disk widths**: `ChunkyFilePrefix.fpMac/fpIndex/fpMap`, `FreeSpaceMap.fp`,
   and `ChunkRepresentation*.fp` are 4 bytes on disk in 1995 3DMM files. They
   must remain 4 bytes for compat. → On-disk fields must use a fixed-width
   alias (`int32_t`).
2. **Runtime ergonomics**: a 32-bit FilePosition limits chunky files to 2 GB.
   3DMM files are well under 100 MB, but generalising kauai for non-3DMM use
   would want 64-bit. → Runtime API can stay `long` (8 bytes on LP64) without
   breaking compat, as long as the I/O layer converts to int32_t at the
   boundary.

Recommended approach: keep the `FilePosition` runtime typedef as is (so file
APIs continue to take `long`), but replace each on-disk occurrence with a
literal `int32_t`. The per-struct findings above all follow this rule —
the OnFile structs use int32_t directly, never `FilePosition`.

### Estimated work

The 11 affected structs are all in three files (`chunk.cpp`, `groups.cpp`,
`groups2.cpp`). Each fix is a localised typedef swap plus a `static_assert`,
following the same pattern pass 1 used for the engine. The byteswap masks
(`kbomCfp`, `kbomCrpbg*`, `kbomEcdf`, `kbomGlf`, `kbomAlf`, `kbomGgf`,
`kbomGstf`) are already correct for the 1995 layout; they don't change.

Two structural items need extra care:
- `LogicalOffsetAndCount` is consumed inline as part of GG variable storage,
  not via a marshal step. The serialization paths in `VirtualGroup::FWrite`
  / `_FRead` (`groups.cpp:1135-1230` ish) byteswap N×size(long) at the
  storage block — that swap unit must become explicit int32_t too.
- The chunky-file index `_pggcrp` is itself a GG of `ChunkRepresentation*`.
  Both layers (GG header + per-entry CRP) need their fixes simultaneously
  for the index to stay readable.

### Why this is its own project

The engine pass touched ~30 structs across 8 files; this kauai pass touches
11 structs but they sit at the bottom of the dependency stack, used by every
.3MM read/write, including the bootstrap `kpack`/`chomp` toolchain that
produces the data files in the build. A regression here breaks the build
itself, not just runtime.

### Pass 2 implementation status (commits on 2026-05-01)

All 11 surveyed structs have now been pinned to explicit-width types and
gained `static_assert(sizeof(...) == N)` matching their x86 layouts:

- `DynamicArrayOnFile`, `AllocatedArrayOnFile`, `GeneralGroupOnFile`,
  `StringTableOnFile` — `d7acdf8`.
- `LogicalOffsetAndCount` + GG byteswap unit (4 sites in
  `VirtualGroup::FWrite`/`_FRead` decoupled from `size(long)` and
  expressed as the literal 2 four-byte words) — `f5160f1`.
- `ChunkyFilePrefix` (incl. `rglwReserved[23]`), `FreeSpaceMap`,
  `EmbeddedChunkDescriptorOnFile` — `60ed011`.
- `ChunkRepresentationBig`/`Small` — `6c5d2fe`.

Build stays clean on x86; 33/33 geometry tests pass after each commit.

### Still out of scope (pass 3 territory)

These didn't change shape *inside* the audited OnFile structs but would
still bite a real LP64 attempt:

- `CbRoundToLong` (`utilint.h:274`) and the pervasive `size(long) - 1`
  alignment use inside GG variable storage growth/packing logic
  (`groups.cpp:1278-1310` etc.). These encode "GG variable storage entries
  are aligned to size(long) bytes". On x86 = 4 = the disk format. On LP64
  this becomes 8, breaking the wire format. Fix is structural: introduce
  a `kcbAlignVarStorage = 4` constant and route everything through that.
- `SwapBytesRglw` (`utilint.cpp:464`) literally `Assert(size(long) == 4)`.
  Its name and signature (`long clw`) are misleading — the implementation
  is hard-coded 4-byte word arithmetic. A pass-3 cleanup should rename to
  `SwapBytesRgInt32` and change the count parameter type to make the
  4-byte-word semantics explicit at every call site.
- `FilePosition` runtime typedef stays `long` (8 bytes on LP64) — fine
  for the file API surface, but means every `FilePosition` field in
  in-memory-only structs and locals will widen. Audit those uses if
  attempting LP64 configure.

## Out of scope

- `RegionOnFile` (no such type yet — Region is runtime-only).
- In-memory class member `long`s that aren't part of any on-disk struct.
- `kbom*` byte-order plumbing audit — that's Project 1 task 3, separate.
- The pervasive `size(long)` use *inside kauai algorithms* (chcm codec word
  size, screxe script word size, rtxt MPE swap, etc.) — these are runtime
  invariants, not on-disk format, but they encode a "long is 4 bytes"
  assumption (`utilint.cpp:407` literally `Assert(size(long) == 4)`).
  Modernizing those is project-sized on its own, separate from the on-disk
  format fix above.
