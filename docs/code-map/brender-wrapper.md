# BRender wrapper (`bren/`)

BRender is the 1995 Argonaut software 3D renderer that powers 3DMM's
in-window playback and authoring view. It's vendored under `bren/lib/`
(Argonaut BRender 1.1.2, MIT-licensed via the foone/Hanselman 2022
release). 3DMMForever wraps the C BRender API in a small C++ layer
under `bren/`.

## Layout

```
bren/
├── inc/         BRender wrapper headers (Br* aliases, BMAT34, etc.)
├── lib/         Vendored Argonaut BRender 1.1.2 source
│   ├── inc/     BRender public headers (br_pixelmap, br_face, …)
│   ├── fmt/     Format loaders (br_actor, .dat files)
│   ├── fw/      Framework
│   ├── std/     Standard library shims
│   └── …        zb (Z-buffer rasterizer), v1db (v1 database), …
├── *.cpp        Wrapper sources (this directory)
├── *.c          Adapter sources (file/error/memory glue)
├── makefile     Original 1995 Argonaut makefile (not used by our build)
└── CMakeLists.txt (under bren/lib/)
```

## Wrapper sources

| File             | Side          | Purpose                                                       |
|------------------|---------------|---------------------------------------------------------------|
| `bwld.cpp`       | gui (`brender`) | `World` — wraps a BRender world together with kauai gui bits (`PGraphicsPort`). The actual rendering surface. |
| `tmap.cpp`       | core (`brender-core`) | `TextureMap` — wrapper for `br_pixelmap` textures.       |
| `zbmp.cpp`       | core          | Z-buffer pixmap helpers.                                      |
| `material.cpp`   | gui           | `Material` glue helpers.                                      |
| `brenfun.cpp`    | gui           | Misc utility functions BRender callers want (transforms, scalars). |
| `stdfile.c`      | core          | BRender filesystem adapter — routes `br_filesystem` calls through kauai's `FileObject`. |
| `stderr.c`       | core          | BRender diag adapter — routes `BR_DIAG_*` to kauai's error stack. |
| `stdmem.c`       | core          | BRender allocator adapter.                                    |

## Library split

`brender-core`:

- `bren/stderr.c`, `bren/stdfile.c`, `bren/stdmem.c`, `bren/tmap.cpp`, `bren/zbmp.cpp`
- Pulls only `kauai-core` + `BRender::Libraries` (the vendored static libs).
- Used by headless tests like `actor-render-test`, `extract-bmdl`.

`brender`:

- `bren/bwld.cpp`
- Adds `kauai` (full gui) on top so `World` can bind to a `PGraphicsPort`.

`material.cpp` and `brenfun.cpp` are linked into `kauai`/`brender`
through the broader `engine` build (see CMake — they're consumed via
include rather than per-target source listing).

## Vendored library

`bren/lib/` is built as `brender_fw` and `brender_zb` (the framework
and the Z-buffer rasterizer), exported as `BRender::Libraries`. Build
flags include `/Zp4` (4-byte struct packing) to preserve the 1995
in-memory layouts.

> **The `/Zp4` band-aid.** The lib is built with `/Zp4` but the C++
> wrapper is not. To stop those two from disagreeing about struct
> offsets (e.g. `br_pixelmap.type` lands at offset 30 inside the lib
> but offset 34 inside the wrapper if both don't pack the same), the
> wrapper header `bren/inc/brender.h` bakes a `#pragma pack(push, 4) /
> pop` around all BRender struct definitions. This works but is
> load-bearing — the long-term plan (in
> [`docs/superpowers/specs/2026-05-01-sized-types-audit.md`](../superpowers/specs/2026-05-01-sized-types-audit.md))
> is to drop `/Zp4` entirely and add `Br*OnFile` mirror structs at every
> serialisation boundary.

## Provenance

BRender 1.1.2 release: 2022, foone/Hanselman, MIT-licensed with email
permission from Jez San (Argonaut CEO). See
[`bren/lib/`](../../bren/lib/) and [`vendor/BRender-v1.3.2-main/README.md`](../../vendor/BRender-v1.3.2-main/README.md)
for the historical contributors list. Newer BRender versions exist at
<https://github.com/foone/BRender-v1.3.2> and
<https://github.com/foone/BRender-1997>; we use 1.1.2 because that's
what 1995 3DMM was built against.

## Useful targets

- `bren-rasterizer-test` — direct-drive harness for the BRender ZB
  scan-converters (asm vs C). Used by the x64 enablement work.
- `extract-bmdl` — dump a `BMDL` chunk's vertex / face data out of a
  `.CHK` file. CLI-only, links `brender-core`.

## See also

- [`../file-formats/bmdl-models.md`](../file-formats/bmdl-models.md) — BMDL chunk layout.
- [`engine.md`](engine.md#3d-rendering-data-engine-core) — `Model`, `Material`, `Body`.
- [`library-split.md`](library-split.md) — exact source lists.
