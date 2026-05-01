# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project

3DMMForever is an open-source restoration of Microsoft's 3D Movie Maker (1995, MIT-released in 2022). The intent is to keep behavior and feature parity with the original (640x480 UI, kidspace, 3DMM-compatible movie files) while making the code build with modern tooling and eventually port to Mac/Linux. Heavier modernizations are reserved for the future `3DMMPlus` fork.

## Build

The project only compiles as **x86 (32-bit)**. CMake `FATAL_ERROR`s on 64-bit pointer width, and the `kcdc-386`/`kcd2-386` bootstrap codegen tools cannot currently cross-compile, so they are built host-as-target. Building from a 64-bit Windows host requires an x86 MSVC environment (e.g., via [VCVars](https://github.com/bruxisma/VCVars):
`pushvc (invoke-vcvars -TargetArch x86 -HostArch AMD64)`).

Configure + build (Ninja generator, MSVC):

```
cmake --preset x86:msvc:debug    # or :release / :relwithdebinfo / :minsizerel
cmake --build build
```

Output: `build/3dmovie.exe` plus chomped `.chk` data files under `build/3dmovie/`.

`CMAKE_MSVC_RUNTIME_LIBRARY` is `MultiThreaded` (static CRT). Compile flags include `/permissive-` and `/Zc:strictStrings-` (the latter is needed because `/permissive-` would otherwise force `const char*` for literals and break legacy code). `WIN` is defined on Windows builds; `IN_80386` is defined only on x86 (32-bit) builds — it gates inline `__asm` fast paths in kauai that don't compile under MSVC x64; the portable C `#else` branches handle non-x86. `DEBUG` is defined in Debug.

Most auxiliary targets are `EXCLUDE_FROM_ALL` — built only when explicitly requested, e.g. `cmake --build build --target chomp`. These include the Kauai authoring tools (`chomp`, `ched`, `chelp`, `chelpdmp`, `chmerge`, `kpack`, `mkmbmp`), the test apps (`ft`, `ut`), and `movie-chomp`.

### Things that are intentionally broken / missing

- `SITOBREN.EXE` is disabled — needs the SoftImage SDK "DKIT", not redistributable.
- Comic Sans TTFs (`COMIC.TTF`, `COMICBD.TTF`) are not in the tree for licensing reasons. See `FONTS.md` — hydrate from a Windows install if you need them.
- `install` target's chunk install includes a HACK that renames the `UTEST.CHK` artifact to `3dmovie.chk`; do not "fix" by changing `utest.cht` without also fixing the install filter expressions in `CMakeLists.txt`.

### Lint

CI enforces clang-format v14 against `.clang-format` (BasedOnStyle: Microsoft, `SortIncludes: Never` — include order is load-bearing, do not reorder).

## Architecture

Three static libraries feed the `studio` executable (renamed at link to `3dmovie.exe`). From bottom to top:

- **`audioman`** (`audioman/`) — thin wrapper over the legacy Windows AudioMan API; provides `3DMMForever::AudioMan`.
- **`kauai`** (`kauai/src/`) — Microsoft's in-house application framework, originally cross-platform between Windows and Mac. Provides graphical objects (`gob`), document model (`docb`), graphics (`gfx`), files (`file`/`fni`), text rendering (`rtxt`), MIDI/sound (`midi`/`sndm`/`sndam`), the **chunky file format** (`chunk`/`chcm`/`chse`/`crf`), an embedded scripting/compilation layer (`scrcom`/`screxe`/`lex`), and the **kidspace** UI runtime (`kidspace`/`kidworld`/`kidhelp`). Cross-platform sources are bare `.cpp`; platform implementations live in `*win.cpp`/`*mac.cpp` (the Mac variants are present but not built today).
- **`brender`** (`bren/`) — wrapper over the BRender 3D engine. The BRender source itself lives vendored under `bren/lib/` (Argonaut BRender 1.1.2, MIT-licensed via the foone/Hanselman 2022 release). It builds as the `brender_fw` and `brender_zb` static libraries, exposed as `BRender::Libraries` for backward compatibility with the original `elib/`-based build.
- **`engine`** (`src/engine/`, headers in `inc/`) — the movie engine proper: actors, scenes, movies, bodies/models/materials, templates, text boxes, 3D text, sound records, tag manager (content references across CDs).
- **`studio`** (`src/studio/`) — the application: tool palettes, browsers, easels, kidspace screens, portfolio, splot machine, scene sorter, etc. Output: `3dmovie.exe`.

Public engine/studio headers all live in the top-level `inc/` directory rather than per-component include folders.

### Chunky files and the `chomp` build step

3DMM data is shipped as **chunky files** (`.chk`) — directed acyclic graphs of typed, optionally-compressed binary chunks identified by a 4-byte type tag (`ChunkTagOrType`) and a `ChunkNumber`. See `kauai/doc/chunk.txt` and `kauai/doc/glossary.txt` for the format.

`.cht` files are **chunky source**, `.chh` are chunky headers. The build runs each `.cht` through the C++ preprocessor (using the consuming target's `INCLUDE_DIRECTORIES` and `COMPILE_DEFINITIONS`), then through `chomp.exe` to produce a `.chk`. This pipeline is wired up by `target_chomp_sources()` in `cmake/TargetChompSources.cmake` and is invoked from `CMakeLists.txt` for `studio` over `src/studio/`, `src/building/`, `src/help/`, `src/helpaud/`, `src/shared/` sources. Resulting chunks are stored on the target as the `CHOMPED_CHUNKS` property and installed under `Microsoft Kids/3D Movie Maker/`.

Therefore: `chomp` must be built before any chunked source can be processed, and editing `.cht`/`.chh` files triggers re-chomping. `kcdc_386.h` and `kcd2_386.h` are codegen artifacts produced by the `kcdc-386`/`kcd2-386` executables at configure-time-ish into `${PROJECT_BINARY_DIR}/generated/kauai/src/`; they are gitignored.

### Naming-modernization in flight

The git log shows an ongoing, mechanical rename pass replacing Hungarian-notation-style 3-5 letter type/struct names with descriptive English ones (e.g. `TGOB → TextGraphicsObject`, `FDSC → FrameDescriptor`, `APP → Application`, `DMGLOB → DebugMemoryGlobals`, `ChunkTag → ChunkTagOrType`). Each rename is its own commit. When working in this repo, follow the same pattern: one type per commit, commit message `rename OLD -> NEW`. Field/local-variable Hungarian prefixes (`p`, `pv`, `cb`, `sz`, `cno`, `ctg`, …) are typically *not* renamed — only type names.

### Bug-gate macros

`inc/socdef.h` defines `BUG####` macros (e.g. `BUG1866`, `BUG1973`). These gate fixes/changes for specific original-Microsoft bug-tracker IDs. Do not remove them blindly — code paths sometimes still reference both branches.
