# Code map

This is the orientation map for the source tree. Read this first; jump
to the per-area pages once you know which subsystem your change belongs
in.

## Library dependency graph

```
                    +---------+
                    | studio  |   3dmovie.exe (renamed at link)
                    +---------+
                         |
                         v
                    +---------+
                    | engine  |   movie / scene / tbox playback runtime
                    +---------+
                    /         \
                   v           v
          +-------------+   +---------+
          | engine-core |   | brender |
          +-------------+   +---------+
            /         \    /          \
           v           v  v            v
   +-------------+   +---------+   +-----------+
   |  kauai      |   |brender- |   | BRender:: |
   +-------------+   |  core   |   | Libraries |
           |         +---------+   | (vendored)|
           v             |         +-----------+
   +-------------+       |
   | kauai-core  |       |
   +-------------+       |
        |                |
        +-------+--------+
                v
        +---------------+
        | Win32 + STL   |
        +---------------+
```

`audioman` is a separate static lib pulled in by `kauai` for the
sampled-audio device. It's a thin wrapper over the legacy AudioMan API.

## Why two halves of each library

Each library is split into a `*-core` (CLI/headless, x64-clean) and a
gui-side `*` (full Win32 + UI runtime):

- `kauai-core` / `kauai`
- `engine-core` / `engine`
- `brender-core` / `brender`

The split lets headless tests (`geometry-test`, `codec-test`,
`actor-render-test`, `extract-bmdl`, `inspect-chunks`) link only the
core libs and avoid the whole UI stack — useful for CI and for porting
to non-Windows hosts. See [`library-split.md`](library-split.md) for
the full source-by-source breakdown and the rationale per file.

## Where things live

| Want to change…                       | Read this                                       |
|---------------------------------------|-------------------------------------------------|
| Movie / scene playback, actors, 3D    | [`engine.md`](engine.md)                        |
| Studio UI, browsers, easels, dialogs  | [`studio.md`](studio.md)                        |
| Kauai framework (gob/gfx/text/sound)  | [`kauai-framework.md`](kauai-framework.md)      |
| BRender wrapper / 3D plumbing         | [`brender-wrapper.md`](brender-wrapper.md)      |
| Library boundaries / what goes where  | [`library-split.md`](library-split.md)          |
| Drive 3dmovie.exe headlessly          | [`mcp-server.md`](mcp-server.md)                |
| The on-disk file formats              | [`../file-formats/`](../file-formats/)          |
| The kauai API (chunky / file / mem)   | [`../kauai-reference/`](../kauai-reference/)    |

## Public headers

All engine and studio public headers live in the top-level
[`inc/`](../../inc/) directory rather than in per-component include
folders. Kauai headers are in [`kauai/src/`](../../kauai/src/) (no
separate `inc/`); BRender headers are in [`bren/inc/`](../../bren/inc/)
(wrapper) and [`bren/lib/inc/`](../../bren/lib/inc/) (vendored library).

## Build entry points

- [`CMakeLists.txt`](../../CMakeLists.txt) — defines all targets and
  the library split. Library source lists for `kauai-core`, `kauai`,
  `engine-core`, `engine`, `brender-core`, `brender`, and `studio`
  start around line 310.
- [`cmake/TargetChompSources.cmake`](../../cmake/TargetChompSources.cmake)
  — the chomp build pipeline for `.cht` chunky source.
- [`CMakePresets.json`](../../CMakePresets.json) — `x86:msvc:debug` and
  friends. See the project [README](../../README.md#build) for how to
  use them from a 64-bit host.

## Aux targets

Most non-studio targets are `EXCLUDE_FROM_ALL`; build with
`cmake --build build --target <name>`:

| Target                   | Purpose                                              |
|--------------------------|------------------------------------------------------|
| `chomp`                  | The chunky-source compiler (used internally by the build). |
| `ched`, `chmerge`        | Chunky editor / merger.                              |
| `chelp`, `chelpdmp`      | Help-file compiler / dumper.                         |
| `mkmbmp`                 | MaskedBitmap (MBMP) builder.                         |
| `kpack`                  | Kauai pack utility.                                  |
| `ft`, `ut`               | Kauai framework / utility test apps.                 |
| `extract-bmdl`           | Dumps `BMDL` chunks out of a `.CHK`.                 |
| `inspect-chunks`         | Walks a chunky file and prints its index.            |
| `geometry-test`          | Pure unit test (PT/RC/Region arithmetic). x64-clean. |
| `codec-test`             | KauaiCodec round-trip (asm vs C). x64-clean.         |
| `actor-render-test`      | Headless actor render via `engine-core`. x64.        |
| `bren-rasterizer-test`   | BRender ZB scan-converter harness; asm vs C diff.    |
| `movie-save-load-test`   | Round-trips a `.3MM` for byte-identity verification. |
| `movie-chomp`            | Chomps a movie sample into a chunky file.            |
