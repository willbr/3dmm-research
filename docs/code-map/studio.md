# Studio (`src/studio/`)

The studio is the application — the UI on top of the engine, plus
bootstrap, dialogs, and tooling. Linked as `studio` (output renamed to
`3dmovie.exe`) and `PRIVATE`-depends on `engine` (which transitively
pulls everything).

| File             | Purpose                                                                                        |
|------------------|------------------------------------------------------------------------------------------------|
| `studio.cpp`     | `Studio` main window — menu, command dispatch, top-level kidspace world. The "main" of 3DMM.   |
| `utest.cpp`      | `Application` subclass + WinMain entry point. (`utest` is the historical target name; the executable is renamed `3dmovie.exe` at link.) |
| `mminstal.cpp`   | First-run installer / file-association setup.                                                   |

## Browsers

| File             | Purpose                                                                                        |
|------------------|------------------------------------------------------------------------------------------------|
| `browser.cpp`    | Generic asset browser (templates, props, sounds, …) — base classes shared by per-asset browsers. |
| `stdiobrw.cpp`   | Studio-specific browser hooks (folder-aware browsing, custom-content paths).                    |
| `scnsort.cpp`    | Scene sorter UI — drag-reorder scenes within a movie.                                           |

## Editors / palettes / easels

| File             | Purpose                                                                                        |
|------------------|------------------------------------------------------------------------------------------------|
| `portf.cpp`      | Portfolio (file open/save dialog wrapper). Notable: `OPENFILENAME.lCustData` must be `LPARAM`, not `DWORD`, on x64. |
| `splot.cpp`      | "Splot machine" — random-prop generator UI.                                                    |
| `tatr.cpp`       | Actor (T)ext attribute editor (font, colour, size of a 3D text actor).                         |
| `tgob.cpp`       | 3D-test gob (debug viewport for templates).                                                    |
| `esl.cpp`        | Event-script list — the easel that lets you scrub through and edit a scene's event list.       |
| `ape.cpp`        | Action-pose editor (per-actor pose timeline within an action).                                  |

## Dialogs / pop-ups / file dialogs

| File             | Purpose                                                                                        |
|------------------|------------------------------------------------------------------------------------------------|
| `popup.cpp`      | Generic in-kidspace pop-up dialogs (modal panels rendered inside the kidspace world).          |
| `stdioscb.cpp`   | Studio scrollbar customisations / scrollbar pump.                                              |

## Headless / debug

| File             | Purpose                                                                                        |
|------------------|------------------------------------------------------------------------------------------------|
| `mcpserv.cpp`    | Embedded MCP server — DEBUG-only. Exposes `screenshot` / `send_command` / `quit` / etc. tools over JSON-RPC stdin-stdout. See [`mcp-server.md`](mcp-server.md). |

## Resources

| File             | Purpose                                                                                        |
|------------------|------------------------------------------------------------------------------------------------|
| `utest.rc` / `utest.rc2` | Win32 resource files (icon, version block, manifest).                                  |
| `studio.cht`, `utest.cht` | Studio-side chunky source (chomped at build).                                         |

The chomped chunk targets are listed in
[`CMakeLists.txt`](../../CMakeLists.txt) under `target_chomp_sources(studio …)`
and span `src/studio/`, `src/building/`, `src/help/`, `src/helpaud/`,
`src/shared/`. See
[`../file-formats/chunky-source.md`](../file-formats/chunky-source.md).

## Build-time stamp

`build_stamp.cpp` is generated at build time into
`${PROJECT_BINARY_DIR}/generated/studio/` and embeds a build timestamp
that the about-box (and the MCP `get_state` tool) can report.

## See also

- [`engine.md`](engine.md) — the runtime studio drives.
- [`kauai-framework.md`](kauai-framework.md) — kauai application + kidspace.
- [`mcp-server.md`](mcp-server.md) — drive 3dmovie.exe headlessly.
