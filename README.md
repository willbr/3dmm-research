# 3DMMForever

_Making 3D Movie Maker accessible to all and preserving it for generations to come._

![3D Movie Maker](img/3dmovie.jpg?raw=true)

## Goals

A faithful, modernised build of Microsoft's 3D Movie Maker (1995, MIT-released
in 2022) with these aims:

- Keep the original feature set, 640x480 UI, kidspace, and graphics intact.
- Build with modern open-source tools (CMake + Ninja + MSVC today; clang/gcc
  later).
- Round-trip movie files with the original 1995 release: `.3MM` files saved
  by 3DMMForever must still load and play in original 3DMM.
- Eventually port to macOS and Linux.
- Heavier modernisations are reserved for the future
  [3DMMPlus](#3dmmplus) fork.

## About

Released by Microsoft in 1995, 3D Movie Maker (3DMM) is a creativity program
originally designed for kids that lets users build 3D animated movies from a
library of scenes, models, sounds and music. Users place, animate and pose 3D
actors with simple mouse drags, can record and import their own audio, and
share finished movies. The program also includes a "kidspace" — a movie
theatre you can navigate that holds tutorials and inspiration.

In May 2022, Microsoft released the original source code under the
[MIT license](LICENSE), which is what made 3DMMForever possible.

## Status

3DMMForever produces a working `3dmovie.exe` from a current MSVC toolchain.

- **x86 (32-bit) — primary target.** Stable; the original feature set runs.
- **x64 (64-bit) — in progress.** Links and reaches the main window. Several
  on-disk struct layouts that embed kauai's `TAG` have been split into
  fixed-width on-file forms with an explicit marshal step so the wire format
  stays byte-identical to 1995. The remaining x64 work is tracked in
  `docs/superpowers/specs/` and `docs/superpowers/plans/`.
- **macOS / Linux — not yet.** The kauai framework has Mac source files in
  the tree from the original Microsoft release but they are not built today.

A naming-modernisation pass is also in flight: the original Hungarian-style
3-5 letter type names (`TGOB`, `MACTR`, `APP`, …) are being renamed to
descriptive English (`TextGraphicsObject`, `RollCallActorEntry`,
`Application`, …), one type per commit. Field/local-variable Hungarian
prefixes (`pv`, `cb`, `cno`, …) are kept.

## Build

CMake 3.22+, Ninja, and Visual Studio 2022 are required.

The project currently only compiles cleanly as **x86 (32-bit)**. CMake will
warn loudly on x64 because some on-disk structs still need their LP64 marshal
work finished. Building from a 64-bit Windows host therefore requires an x86
MSVC environment — install [VCVars](https://github.com/bruxisma/VCVars) and:

```powershell
pushvc (invoke-vcvars -TargetArch x86 -HostArch AMD64)
```

(`popvc` removes the environment again.)

Then configure and build via the bundled CMake presets:

```console
$ cmake --preset x86:msvc:debug    # or :release / :relwithdebinfo / :minsizerel
$ cmake --build build
```

Output: `build/3dmovie.exe` plus chomped `.chk` data files under `build/3dmovie/`.

For experimental x64 builds (will produce a `3dmovie.exe` that launches but
has known asset/serialisation bugs), use the `x64:msvc:*` presets from an
AMD64 MSVC environment.

Most auxiliary targets (`chomp`, `ched`, `chelp`, `chmerge`, `kpack`,
`mkmbmp`, the `ft`/`ut` test apps, `movie-chomp`) are `EXCLUDE_FROM_ALL` —
build them explicitly via `cmake --build build --target <name>` when needed.

### Known issues / intentional gaps

- `SITOBREN.EXE` is disabled — it needs the SoftImage SDK "DKIT" which is not
  redistributable.
- Comic Sans TTFs (`COMIC.TTF`, `COMICBD.TTF`) are not in the tree for
  licensing reasons. See [`FONTS.md`](FONTS.md) for hydration instructions.
- The `install` target's chunk-install step renames `UTEST.CHK` to
  `3dmovie.chk` as part of the build hack inherited from the original —
  documented in `CMakeLists.txt` and `CLAUDE.md`.

### Lint

CI enforces `clang-format` v14 against `.clang-format` (Microsoft style,
`SortIncludes: Never` — include order is load-bearing in this codebase).

## Architecture

Three static libraries feed the `studio` executable (renamed at link to
`3dmovie.exe`):

- **`audioman`** (`audioman/`) — wrapper over the legacy Windows AudioMan API.
- **`kauai`** (`kauai/src/`) — Microsoft's in-house cross-platform application
  framework. Provides graphical objects, document model, graphics, files, text
  rendering, MIDI/sound, the chunky-file format, an embedded scripting layer,
  and the **kidspace** UI runtime. Cross-platform sources are bare `.cpp`;
  platform implementations live in `*win.cpp` / `*mac.cpp`.
- **`brender`** (`bren/`) — wrapper over the BRender 3D engine. The BRender
  source itself is now vendored under `bren/lib/` (Argonaut BRender 1.1.2,
  MIT-licensed via the foone/Hanselman 2022 release) and builds as
  `brender_fw` and `brender_zb`. The original repo's pre-compiled BRender
  `.lib` files are gone.
- **`engine`** (`src/engine/`, headers in `inc/`) — the movie engine: actors,
  scenes, movies, bodies/models/materials, templates, text boxes, 3D text,
  sound records, and the tag manager (cross-CD content references).
- **`studio`** (`src/studio/`) — the application: tool palettes, browsers,
  easels, kidspace screens, portfolio, splot machine, scene sorter, etc.
  Output: `3dmovie.exe`.

### Chunky files and the chomp build step

3DMM data ships as **chunky files** (`.chk`) — DAGs of typed binary chunks
identified by a 4-byte type tag and a chunk number. See
`kauai/doc/chunk.txt` for the format. `.cht` files are chunky source, `.chh`
are chunky headers; the build runs each `.cht` through the C preprocessor and
then `chomp.exe` to produce `.chk` artifacts. This is wired up by
`target_chomp_sources()` in `cmake/TargetChompSources.cmake`.

### MCP server (DEBUG builds)

DEBUG builds embed a small Model Context Protocol server in `3dmovie.exe`
that lets an AI agent (e.g. Claude Code) drive the application headlessly:
take screenshots, click, send commands, query window state, read crash logs,
and quit. Pass `--mcp-server` on the command line to enable it. A project-root
`.mcp.json` registers the server so a Claude Code session opened in the repo
auto-discovers it. A scriptable Python driver lives at
`scripts/mcp-driver.py` for shell-driven sessions.

## Contributing

3DMMForever (and eventually 3DMMPlus) depend on an enthusiastic open source
community. Have a look at [`CLAUDE.md`](CLAUDE.md) for the project conventions
followed by both human and AI contributors — especially the rename pattern
and the .3MM compatibility rule.

## 3DMMPlus

3DMMPlus is a future fork of 3DMMForever, planned once the macOS and Linux
ports are usable. The aim is a heavily-enhanced 3DMM that:

- Has a flexible full-colour UI that scales to modern resolutions.
- Has a modern full-colour 3D renderer with lighting, shading, a moveable
  camera, and more.
- Produces a new enhanced file type.
- Can import `.3MM` files and play them back as originally created.
- Maintains a strong consideration for and familiarity with the original
  3DMM's UI decisions and approach.

## Legal stuff

The following sections have been carried over from the original 3D Movie
Maker [GitHub repository](https://github.com/microsoft/Microsoft-3D-Movie-Maker)
released by Microsoft in May 2022.

### Code cleanup

This code was restored from the Microsoft corporate archives and cleared for release.

- Developer names and aliases were removed, with the exception of current
  employees who worked on the original release who consented to keeping their
  names in place.
- The archive consisted of several CDs, some of which were for alternate
  builds or products, and have been excluded.
- The code does not build with today's engineering tools, and is released as-is.

### Trademarks

This project may contain trademarks or logos for projects, products, or services. Authorized use of Microsoft 
trademarks or logos is subject to and must follow 
[Microsoft's Trademark & Brand Guidelines](https://www.microsoft.com/en-us/legal/intellectualproperty/trademarks/usage/general).
Use of Microsoft trademarks or logos in modified versions of this project must not cause confusion or imply Microsoft sponsorship.
Any use of third-party trademarks or logos are subject to those third-party's policies.

This repo includes a build from 1995 of BRender from Argonaut software. Approval to open source BRender as MIT was given in an email from Jez San, former CEO of Argonaut. Other versions of BRender exist at https://github.com/foone/BRender-v1.3.2 and https://github.com/foone/BRender-1997 Thanks to Jez and the whole BRender team for their hard work on this amazing engine. A full historical list of BRender contributors is available at https://github.com/foone/BRender-v1.3.2/blob/main/README.md 

This repo does NOT include the SoftImage SDK "./DKIT" from 1992.

Jez also offered this interesting BRender anecdote in an email:

```
When Sam Littlewood designed BRender, he didn't write the code. And then document it.  
The way most things were built at the time.
First, he wrote the manual.  The full documentation
That served as the spec.  Then the coding started.
```
