# 3DMMForever modernization plan

## Hard constraints

- **`.3MM` files written by 3DMMForever must remain loadable by original 1995 3DMM.** Reduced fidelity OK; load failure or playback breakage not OK. Drives the format-extension rules (additive chunks only, no in-place struct growth, no new `SceneEventType` values, `kcvnBack` stays at 2). Compat-breaking work belongs in the future `3DMMPlus` fork.

## Scope decisions

### Rejected: rewriting C++ to C

Not worth doing.

- The C++ surface is shallow (no STL, no exceptions, no RTTI of consequence) but **deep where it matters**: Kauai's `BaseClass`/`RTCLASS` object model and the inheritance trees in `engine` (`Movie : DocumentBase`, `MovieView : DocumentDisplayGraphicsObject`, every `*Undo*`, every `gob`, every easel).
- Mechanical hand-rolled-vtables-in-C conversion across ~190k LOC. High risk of silent regressions in the chunky-file format and undo system, both of which threaten the `.3MM` compat constraint.
- Does not unlock either of the other goals (Python bindings can be C-shim around C++; SDL port doesn't care about C vs C++).

If specific files annoy you, do targeted modernization. The rename pass already in flight on branch `c` (`ChunkTag → ChunkTagOrType`, etc.) is the right level of investment.

---

## Project 1: 64-bit build

**Why first:** Today `CMakeLists.txt` `FATAL_ERROR`s on 64-bit pointer width, and `kcdc-386`/`kcd2-386` host codegen tools cannot cross-compile. No Mac/Linux work is real until this is fixed (Apple Silicon doesn't run x86 binaries usefully).

**Tasks:**

1. Audit `IN_80386` define usage. Find every conditional and decide: portable C replacement, or `<cstdint>`-typed equivalent.
2. Replace `kcdc_386.c`/`kcd2_386.c` with portable codegen, or pre-generate the headers and check them in.
3. Audit byte-order plumbing (`kbom*` constants, `SwapBytesBom`) — confirm 32-vs-64 is irrelevant (it should be; these are over-the-wire structs).
4. Audit struct layouts that round-trip to disk (`MovieFilePrefix`, `SceneOnFile`, `ActorChunkOnFile`, `MACTR`, etc.) — confirm none of them embed pointers or `long` width assumptions. The `.3MM` compat constraint forces 32-bit-equivalent layouts on disk regardless of host word size.
5. Strip `set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded...")` static-CRT lock-in if it's only there for x86 reasons.
6. Remove the `FATAL_ERROR` and fix the resulting cascade.

**Risk:** medium. Likely to surface places where `long` was used as a pointer-width int. Not architecturally hard; lots of mechanical fixes.

**Done when:** `cmake --preset x64:msvc:debug` builds and `3dmovie.exe` opens a sample `.3MM` from `cd3/SAMPLES/` cleanly.

---

## Project 2: library extraction + Python bindings

**Why:** Enables `.3MM` inspection, batch conversion, and headless tooling without spinning up the studio app. Validates that engine is separable from UI — prerequisite for the SDL port.

**The friction is not C++.** It's that `engine` depends on Kauai for non-data reasons: error stack (`PushErc`), memory primitives (`HQ`, `PvAddBv`), `StringTable_GST`, `DataVersion`, the chunky-file API, and `TagManager` (which scans drives for content). You don't get a clean "engine without UI" by shaving off `studio` — you get "engine + a chunk of Kauai's runtime."

**Tasks:**

1. **Headless Kauai**: add CMake option `KAUAI_HEADLESS`. When set, swap `appbwin.cpp`, `gobwin.cpp`, `gfxwin.cpp`, `dlgwin.cpp`, `menuwin.cpp`, `picwin.cpp` for stubs returning `fFalse`/no-op. Keep file/fni/mem/region as-is (they're mostly std-C already).
2. **Lazy/skip TagManager**: `vptagm`'s drive-scan triggers UI prompts when content sources are missing. Add a "no-prompt, fail-fast" mode used by headless callers. Tags that fail to resolve return `pvNil` instead of triggering a CD-search dialog.
3. **Define `engine_capi.h`** — narrow C-callable shim, ~50 functions for: open/save `.3MM`, enumerate scenes, enumerate actors, read/write the roll-call, dump tags, get/set chunk-level metadata. Don't try to mirror every method of `Movie`. Implementation stays C++.
4. **`libsoc` shared lib target** — links `engine + brender + kauai (headless) + audioman (stub)`, exports only the shim. `__declspec(dllexport)` on Windows, visibility attrs elsewhere.
5. **Python binding**: prefer `cffi` over `pybind11`. `cffi` against a flat C ABI sidesteps the static-CRT/MSVC-ABI mess. `pybind11` would force Python and engine to share the C++ runtime — annoying with `MultiThreaded` static CRT.
6. **Reuse `movie-chomp`**: `src/tools/movie_chomp.cpp` already opens `.3MM`, walks chunks, dumps the prefix and per-scene structures. Treat it as the prototype for the read side of the shim — extract its calls.

**Risk:** low-medium. The header reorganisation alone may surface latent UI-layer leakage from engine; budget time for that. Tag manager headless mode is the most likely subtle bug source.

**Done when:** from Python, `Movie.open("cd3/SAMPLES/BONGO.3MM")` returns scenes/actors/sounds/tags as native Python objects, no UI launched, no CD-search dialog.

---

## Project 3: SDL2 backend (cross-platform UI)

**Why:** Mac/Linux port. The porting unit is **Kauai**, not engine. Engine is mostly platform-clean already; Kauai is what owns Win32.

**SDL2, not raylib.** Decision drivers:

- **8-bit indexed-palette blitting with palette animation** (kidspace fade transitions, Mczee anims). SDL2 has first-class `SDL_Surface` palette support; raylib's GL/texture pipeline makes palette work awkward.
- **Region-based dirty-rect drawing.** SDL2 has texture-update with dirty rects; raylib assumes per-frame redraw.
- **Win32 `MM_TIME`/MIDI/AVI** is the largest port surface. SDL2 lives closer to "Win32 minus Microsoft" — natural fit. raylib pulls in opinions about how draw should work that fight Kauai.

**Depends on:** Project 1 (64-bit) and ideally Project 2 (the headless Kauai work directly bridges into a clean platform-backend abstraction).

**Tasks:**

1. **Kauai SDL backend files** mirroring the existing Win32 implementations: `appbsdl.cpp`, `gfxsdl.cpp`, `gobsdl.cpp`, `filesdl.cpp`, `fnisdl.cpp`, `dlgsdl.cpp`, `menusdl.cpp`, `picsdl.cpp`, `memsdl.cpp`. CMake picks backend by `CMAKE_SYSTEM_NAME` / explicit option.
2. **Native file dialogs** via `nativefiledialog-extended` — SDL itself doesn't ship file pickers.
3. **Replace `Vfw32`** (AVI) with ffmpeg/libav decoded to RGB frames blitted via the new SDL gfx layer. Used by kidspace intros, Mczee animations, theatre playback.
4. **Replace `Winmm`** (MIDI) with fluidsynth + a free GM-compatible SoundFont (or libADLMIDI for an OPL3 vibe closer to original era). Splash/ambient music.
5. **Replace `Msacm32`/AudioMan** with miniaudio or SDL_audio. `audioman/audioman.cpp` is already a thin wrapper — easy swap.
6. **Replace `MM_TIME`** with `SDL_GetTicks` / `SDL_AddTimer`.
7. **BRender source build** — the static libs in `elib/wind/bren.lib` are x86 Windows .libs only. For Mac/Linux, either:
   - **(a)** Build BRender from source. [foone/BRender-v1.3.2](https://github.com/foone/BRender-v1.3.2) and [foone/BRender-1997](https://github.com/foone/BRender-1997) are MIT-relicensed (per `README.md`). Realistic path.
   - **(b)** Drop in a software-3D replacement. Bigger scope; defer.

**Risk:** high. Multi-month. Many subsystems, lots of platform-specific edge cases, palette-animation and AVI-blit timing especially fiddly. Recommend a vertical slice first: get the splash screen + main menu rendering on macOS before tackling kidspace transitions.

**Done when:** `3dmovie` opens on macOS / Linux, plays through the kidspace intro, opens a sample movie, plays it back at the right framerate with sound.

---

## Sequencing

1. **Continue rename modernization** — already in flight on branch `c`. Background task; don't block other work on it.
2. **Project 1 (64-bit build)** — first real project. Gates everything cross-platform.
3. **Project 2 (libsoc + Python)** — second. Validates engine/UI separation. Unlocks tooling immediately.
4. **Project 3 (SDL2 + BRender source)** — third. The big one. Everything before makes it tractable.

Each project is independently shippable. Don't bundle.
