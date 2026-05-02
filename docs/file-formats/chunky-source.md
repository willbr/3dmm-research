# Chunky source (`.CHT` / `.CHH`) and the chomp pipeline

`.CHT` files are **chunky source** — human-readable definitions of the
chunks that ship in a `.CHK` data file. `.CHH` files are **chunky
headers**, included from `.cht` files the same way C headers are
included. Both pass through the C++ preprocessor before being read by
`chomp.exe`, which emits the binary `.chk`.

## The pipeline

```
+----------+    cl /E       +-----------+   chomp /c    +---------+
|  *.cht   | -------------> |  *.cht.i  | ------------> |  *.chk  |
+----------+                +-----------+               +---------+
   |  uses target's
   |  INCLUDE + DEFINES
   v
+----------+
|  *.chh   |
+----------+
```

Wired up by
[`cmake/TargetChompSources.cmake`](../../cmake/TargetChompSources.cmake)
via `target_chomp_sources(<target> <sources…>)`. The function:

1. Pulls the consuming target's `INCLUDE_DIRECTORIES` and
   `COMPILE_DEFINITIONS` and turns them into `/I` and `/D` flags.
2. Adds a custom command that runs the C++ compiler with `/E` (preprocess
   only) over the `.cht`, writing to
   `${CMAKE_CURRENT_BINARY_DIR}/chomp/<target>/<file>.cht.i`.
3. Adds a second custom command that runs `chomp /c <preprocessed> <out>`
   to produce `${CMAKE_CURRENT_BINARY_DIR}/3dmovie/<file>.chk`.
4. Records each output on the target's `CHOMPED_CHUNKS` property; an
   aggregate `<target>-chomp-chunks` custom target depends on all of
   them, and the main `<target>` depends on that.

Therefore: `chomp` must be built before any chunky source can be
processed (it is, automatically — `chomp` is in
[`kauai/tools/chomp.cpp`](../../kauai/tools/chomp.cpp)), and editing a
`.cht` or any `.chh` it includes triggers a rebuild of just the affected
chunks.

## A small example

[`src/building/palette.cht`](../../src/building/palette.cht) is one of
the smallest `.cht` files in the tree:

```cpp
PALETTECHUNK( "Soc Base 140 Palette",   kpalSocBase,        "building\\bitmaps\\palette\\socbase.bmp" )
PALETTECHUNK( "Soc Imaginoplis Palette", kpalImaginopolis,  "building\\bitmaps\\palette\\imaginpl.bmp" )
PALETTECHUNK( "Soc Ticket booth palette", kpalSocTicket,    "building\\bitmaps\\palette\\ticketpl.bmp" )
... (one row per palette)
```

`PALETTECHUNK` is a macro expanded by the preprocessor (defined in
`.chh` headers higher up). After expansion, each line becomes a chomp
directive that:

- Allocates a chunk with type tag `kctgGlpi` (palette) and a chunk number
  derived from the `kpal*` constant.
- Reads the bitmap file at the given path and stores its palette as the
  chunk payload.
- Sets the chunk's name to the human-readable string for browsing in
  `ched`.

Other `PALETTECHUNK`-style macros wrap bitmap chunks (`BITMAP_CHUNK`),
sound chunks (`WAVE_CHUNK`), help topics, and so on — see the `.chh`
headers under `src/building/` and `src/shared/`.

## Where the chomped output ends up

Each chunked target in [`CMakeLists.txt`](../../CMakeLists.txt) wires
`target_chomp_sources()` against its source files. Outputs land in
`${CMAKE_CURRENT_BINARY_DIR}/3dmovie/`:

- `studio/` chunks → `3dmovie.chk` (via the install hack that renames
  `UTEST.CHK` → `3dmovie.chk`).
- `building/` chunks → `building.chk`.
- `shared/` chunks → `shared.chk`.
- `help/` and `helpaud/` chunks → `help.chk` / `helpaud.chk`.

The runtime opens each `.chk` via `ChunkyFile::PcflOpen` (or the
`ChunkyResourceManager` for cached/refcounted access).

## Authoring tools

Two related kauai tools live next to `chomp`:

- [`ched`](../../kauai/tools/ched.cpp) — chunky editor; opens a `.chk`
  and lets you browse, rename, delete, or extract chunks.
- [`chmerge`](../../kauai/tools/chmerge.cpp) — merge multiple `.chk`s.

Both are `EXCLUDE_FROM_ALL`; build with `cmake --build build --target
ched` etc.

## See also

- [`chunky-files.md`](chunky-files.md) — the on-disk chunk model.
- [`chunk-type-reference.md`](chunk-type-reference.md) — what tags
  `chomp` emits.
- [`../code-map/library-split.md`](../code-map/library-split.md) — where
  `chomp` lives in the library split.
