# 3DMMForever modernization plan

## Hard constraints

- **`.3MM` files written by 3DMMForever must remain loadable by original 1995 3DMM.** Reduced fidelity OK; load failure or playback breakage not OK.
  - Additive chunks at unused chids/ctgs only. No in-place struct growth (the loader rejects on size mismatch).
  - No new `SceneEventType` values. The 1995 reader doesn't understand them; using them in a movie breaks it.
  - **Adding new *optional* chunks is fine; making any new chunk required is forbidden** — required chunks force a `kcvnBack` bump beyond 2, and the 1995 reader's `DataVersion::FReadable` check rejects.
  - Compat-breaking work belongs in the future `3DMMPlus` fork.

## Non-goals

This plan does NOT cover:

- Modernizing the kidspace scripting language or `chomp` chunked-source pipeline (the bytecode VM ships in 1995 3DMM and matches its behavior; replacing it adds regression risk on the user-facing kidspace flow without helping the `.3MM` compat mission).
- Replacing or removing BRender (kept as-is; only ported, not rewritten).
- Replacing the 3D pipeline with Vulkan / OpenGL / Metal — see *Future / 3DMMPlus* note below.
- v3dmm content support, the SoftImage-DKIT-dependent SITOBREN tool, or new file format design (those go to 3DMMPlus).
- macOS/Linux Mac toolbox compatibility — kauai's `*mac.cpp` files are not the SDL backend.

## Future / 3DMMPlus

Items deferred to the future 3DMMPlus fork rather than 3DMMForever, in rough order of attractiveness:

- **Lua-based kidspace runtime.** Replace the in-house stack VM (`scrcom.cpp` / `screxe.cpp` and their graphical variants) with Lua 5.3+. Match: stack-based, integer-friendly, coroutines map to kidspace's "fire-and-wait" patterns, ~150 KB interpreter vs. ~3-4 kLOC of in-house VM, real debugger and profiler available, MIT-licensed. Rough scope: write a `.cht`-source-to-Lua transpiler, port the ~30-50 kidspace intrinsics (`ChangeStateThis`, `FRunScript`, etc.) as Lua C functions, build a GOK ↔ Lua table binding for cross-GOK variable access, validate against the existing 500+ scripts. Not Python — Python's footprint (5-15 MB embedded), startup cost, and GC pauses make it the wrong fit for embedded UI scripting, even though it's the right choice for engine bindings (Project 2). Both languages can coexist in the same binary because they target different layers. Worth doing in 3DMMPlus because it enables modding and gives content authors real tooling; not worth doing in 3DMMForever because compat with 1995 behavior is the priority and the existing VM works.
- **Modern 3D renderer** (Vulkan / OpenGL / Metal, full color, lighting, moveable camera) — the headline 3DMMPlus charter item. Requires a new file format that breaks 1995 compat and a renderer rewrite, not a port.
- **New file format** with extension capacity beyond 1995's `MovieFilePrefix` size budget. Imports `.3MM` files for backwards play.
- **Kidspace UI redesign** for modern resolutions (1920×1080+) and full color, beyond the 640×480 8-bit-palette constraint of 1995 3DMM.

These items are forward-pointers, not commitments. Listing them here makes it easier to reject scope creep into 3DMMForever — when someone proposes "wouldn't it be cool to add X," the answer is either "yes, in 3DMMForever" or "yes, in 3DMMPlus, see Future section."

## Scope decisions

### Rejected: rewriting C++ to C

Not worth doing.

- The C++ surface is shallow (no STL, no exceptions, no RTTI of consequence) but **deep where it matters**: Kauai's `BaseClass`/`RTCLASS` object model and the inheritance trees in `engine` (`Movie : DocumentBase`, `MovieView : DocumentDisplayGraphicsObject`, every `*Undo*`, every `gob`, every easel).
- Mechanical hand-rolled-vtables-in-C conversion across ~190k LOC. High risk of silent regressions in the chunky-file format and undo system, both of which threaten the `.3MM` compat constraint.
- Does not unlock either of the other goals (Python bindings can be C-shim around C++; SDL port doesn't care about C vs C++).

If specific files annoy you, do targeted modernization. The rename pass (Project 0) is the right level of investment.

---

## Project 0: rename modernization

**Status:** in flight on branch `c`. ~24 commits already; queue tracked in `plan-renames.md`.

**Why:** Improves readability and grep-ability without changing behavior. Cheap to do incrementally; warms muscle memory for the codebase.

**Done when:** No bare 4-or-5-letter Hungarian-style type names remain in `inc/` and `kauai/src/` *except* the ones explicitly deferred (PT/RC/RAT — defer until Project 3 surfaces platform-header collisions; SITOBREN classes — disabled build).

**Abandonment criteria:** None — this can be paused and resumed indefinitely. Every commit is independently revertible.

### §0a — Test infrastructure (now-doable)

Three test buckets that need no Project 1/2/3 dependency. Set these up early; they pay off immediately by giving every subsequent rename and refactor a regression net.

1. **Chomp output determinism.** Hash every `.chk` produced by `build/3dmovie/`, store the hashes in-tree (e.g. `tests/fixtures/chunk-hashes.txt`), fail CI if they change without an explicit baseline update. Turns the "byte-identical chomped chunks" criterion in `plan-renames.md` into automation. Catches accidental regressions in `chomp.exe`, `kpack`, `mkmbmp`, `chcm`, plus any rename that touches `.cht`/`.chh` files.
2. **Geometry math tests** for `PT`, `RC`, `Region` (`kauai/src/utilint.h` and `kauai/src/region.cpp`). Pure unit tests, no I/O. Tests: `RC::Union`/`Intersect`/`OffsetToOrigin`, `PT::Map`/`Transform`, `Region::Union`/`Intersect`/`Diff` against fixed input rects. **Becomes load-bearing for Project 1's sized-types audit** — these are the types most likely to be silently miscompiled under LP64.
3. **Codec round-trip** (`kauai/src/codec.cpp`, `codkauai.cpp`, `kcdc_386.c`, `kcd2_386.c`). For each codec, compress a known buffer, decompress, byte-diff. Catches silent codec regressions that are otherwise undetectable until a chomped chunk happens to round-trip wrong.

**Test framework:** small home-rolled C++ harness (or pull in Catch2 / doctest header-only) plus a thin Python pytest wrapper that invokes the test binaries. Don't over-engineer — these are pure-function tests; they don't need fixtures or mocking infrastructure.

---

## Project 1: 64-bit build

**Why first:** Today `CMakeLists.txt` `FATAL_ERROR`s on 64-bit pointer width, and `kcdc-386`/`kcd2-386` host codegen tools cannot cross-compile. No Mac/Linux work is real until this is fixed (Apple Silicon doesn't run x86 binaries usefully).

**Tasks:**

1. **Audit `IN_80386` define usage.** Find every conditional and decide: portable C replacement, or `<cstdint>`-typed equivalent.
2. **Replace `kcdc_386.c`/`kcd2_386.c`** with portable codegen, or pre-generate the headers and check them in.
3. **Audit byte-order plumbing** (`kbom*` constants, `SwapBytesBom`) — confirm 32-vs-64 is irrelevant. These are byte-stream FOURCCs; should be word-size-independent but verify.
4. **Replace bare `long` in on-disk structs with sized types.** This is the single most likely place to silently break `.3MM` compat. MSVC x64 keeps `long` at 32 bits (Windows LLP64), so a Windows-only 64-bit build hides the bug — but on Linux/macOS `long` is 64 bits (LP64) and the layout silently changes. Files to audit: `MovieFilePrefix`, `SceneOnFile`, `ActorChunkOnFile`, `MACTR`, `SceneEvent`, `SceneSoundEvent`, `TagChildPair`, `MaterialOnFile`, `BackgroundOnFile`, etc. Replace bare `long` with `int32_t`, `short` with `int16_t`. The `blck.Cb() != size(...)` checks will catch any miss at runtime.
5. **Strip `set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded...")` static-CRT lock-in** if it's only there for x86 reasons.
6. **Remove the `FATAL_ERROR` and fix the resulting cascade.**

**Risk:** medium. Likely to surface places where `long` was used as a pointer-width int. Not architecturally hard; lots of mechanical fixes.

**Abandonment criteria:** If task 4 surfaces a struct that genuinely cannot be made layout-compatible without breaking compat (e.g., uses pointers stored on disk), document it and defer to 3DMMPlus. Don't try to invent compat shims — those are bug farms.

**Done when:** `cmake --preset x64:msvc:debug` builds, `3dmovie.exe` opens a sample `.3MM` from `cd3/SAMPLES/` cleanly, and `git diff` of chomped chunks against the x86 build is byte-identical.

---

## Project 2: library extraction + Python bindings

**Why:** Enables `.3MM` inspection, batch conversion, and headless tooling without spinning up the studio app. Validates that engine is separable from UI — prerequisite for the SDL port.

**The friction is not C++.** It's that `engine` depends on Kauai for non-data reasons: error stack (`PushErc`), memory primitives (`HQ`, `PvAddBv`), `String`/`StringTable_GST`, `DataVersion`, the chunky-file API, and `TagManager` (which scans drives for content). You don't get a clean "engine without UI" by shaving off `studio` — you get "engine + a chunk of Kauai's runtime."

**Tasks:**

1. **Pre-flight survey.** Before designing the shim, grep `src/engine/` for includes of `appb.h`, `gob.h`, `dlg.h`, `cmd.h`, `cursor.h`, `menu.h`. Catalogue every UI-leaning header that engine pulls. The result calibrates the headless-Kauai effort and may surface engine code that needs lifting into `studio` first.
2. **Headless Kauai.** Add CMake option `KAUAI_HEADLESS`. When set, swap `appbwin.cpp`, `gobwin.cpp`, `gfxwin.cpp`, `dlgwin.cpp`, `menuwin.cpp`, `picwin.cpp` for stubs returning `fFalse`/no-op. Keep file/fni/mem/region as-is (they're mostly std-C already).
3. **Lazy/skip TagManager.** `vptagm`'s drive-scan triggers UI prompts when content sources are missing. Add a "no-prompt, fail-fast" mode used by headless callers. Tags that fail to resolve return `pvNil` instead of triggering a CD-search dialog.
4. **Define `engine_capi.h`** — narrow C-callable shim. Realistic surface: **100-150 functions** (open/close, scene enumeration, actor enumeration, roll-call read, tag enumeration, chunk-level metadata, save). Do not try to mirror every method of `Movie`. Implementation stays C++. Each shim function wraps Kauai's `String`/`HQ`/`DataBlock` types and copies into POD output buffers — so Python sees `bytes`/`str`/`int`/list-of-dict, never opaque C++ objects.
5. **`libsoc` shared lib target.** Links `engine + brender + kauai (headless) + audioman (stub)`, exports only the shim. `__declspec(dllexport)` on Windows, visibility attrs elsewhere.
6. **Python binding via `cffi`.** `cffi` against a flat C ABI sidesteps the static-CRT/MSVC-ABI mess. `pybind11` would force Python and engine to share the C++ runtime — annoying with `MultiThreaded` static CRT.
7. **Reuse `movie-chomp`.** `src/tools/movie_chomp.cpp` already opens `.3MM`, walks chunks, dumps the prefix and per-scene structures. Treat it as the prototype for the read side of the shim — extract its calls.

### §2a — Test-driver methods and round-trip test suite

Extend the C ABI (task 4) with a small test-driver surface that the existing engine-without-UI code path supports for free:

- `soc_movie_save(handle, path)` — pairs with `soc_movie_open` to enable round-trip tests
- `soc_movie_get_scene_count(handle)` / `soc_scene_get_actor_count(handle, iscen)` / `soc_actor_get_tag(handle, iscen, iactr, *tag_out)` — scene/actor introspection beyond just enumeration (returns full state for assertion)
- `soc_movie_advance_frame(handle)` — drive scene playback in headless mode without a render target
- `soc_set_tag_resolver(callback)` — let test code stub tag resolution rather than hitting the real `TagManager`

**Round-trip test suite as a Project 2 done-criterion:** for every sample `.3MM` in `cd3/SAMPLES/`, open → save to a temp file → byte-diff against the source. This is the most direct check of the `.3MM` compat constraint and runs entirely from Python via the C ABI — no UI driving needed.

Two further test categories that fall out for free once the C ABI exists:

- **DataVersion handshake tests.** Build synthetic `MovieFilePrefix` blobs with `(swCur, swBack)` combinations spanning `<kcvnMin`, `=kcvnMin`, `=kcvnCur`, `>kcvnCur`, and verify the loader accepts/rejects correctly. Directly exercises the `.3MM` compat enforcement code (`Movie::FVerifyVersion` in `src/engine/movie.cpp:1783`, `DataVersion::FReadable`).
- **Tag manager with mocked content sources.** Use the `soc_set_tag_resolver` callback (task 4 above) to stub out `vptagm`. Tests: missing-CD path, multiple sources with shadow names, `ksidUseCrf` tag resolution, version-skewed source GST. The headless tag-manager mode is one of the riskiest pieces of Project 2; tests are the only way to keep it honest.

**Why this matters:** the high-value tests for this codebase aren't pixel-perfect rendering tests (too flaky for hobbyist pace). They're tests that catch silent breakage of the on-disk format invariants — exactly the regressions Project 1's sized-types audit and any future Kauai changes are most likely to introduce.

### §2b — MCP wrapper (optional add-on)

Once §2a lands, a thin MCP server exposing the same C ABI lets Claude drive 3DMMForever for exploratory regression hunting and bug-repro from issue reports ("open BONGO.3MM, jump to scene 3, list actors"). **Effort: 1-2 days.** This is a quality-of-life add-on, not load-bearing — pytest against the C ABI directly is the actual test infrastructure; MCP is just a different frontend on top.

UI-level driving (clicks, drags, easel interactions) is **explicitly out of scope** for §2a/§2b. That's a separate test-harness effort that depends on Project 3 being on Windows-x64 first, and is only worth doing if you commit to running it in CI.

**Phasing inside Project 2:** ship **read-only** first (open + enumerate + dump). Write/save (which §2a depends on) is the second milestone — write paths in `engine` are the most globally-stateful and most likely to need still more Kauai surface. MCP wrapper is the third, optional milestone.

**Risk:** medium. Header reorganisation may surface latent UI-layer leakage from engine. Tag manager headless mode is a subtle bug source. Most insidious: `engine` and Kauai both rely on global init order (`vpappb`, `vptagm`, `vpsndm` etc.) — calling shim functions before/around those globals will crash in non-obvious ways. Plan for an explicit `soc_init()`/`soc_shutdown()` pair in the shim.

**Abandonment criteria:** If task 2 (headless Kauai) reveals that >30% of `kauai/src/*.cpp` files transitively depend on `appbwin.cpp` globals, the shim is more invasive than the SDL backend itself — pivot directly to Project 3 and skip Python bindings until SDL is in.

**Done when (read-only milestone):** from Python, `Movie.open("cd3/SAMPLES/BONGO.3MM")` returns scenes/actors/sounds/tags as native Python objects, no UI launched, no CD-search dialog.

---

## Project 3: SDL3 backend (cross-platform UI)

**Why:** Mac/Linux port. The porting unit is **Kauai**, not engine. Engine is mostly platform-clean already; Kauai is what owns Win32.

**SDL3, not SDL2 or raylib.** Decision drivers:

- **8-bit indexed-palette blitting with palette animation** (kidspace fade transitions, Mczee anims). Both SDL versions have first-class `SDL_Surface` palette support; raylib's GL/texture pipeline makes palette work awkward.
- **Region-based dirty-rect drawing.** SDL has texture-update with dirty rects; raylib assumes per-frame redraw.
- **Win32 `MM_TIME`/MIDI/AVI** is the largest port surface. SDL lives closer to "Win32 minus Microsoft" — natural fit. raylib pulls in opinions about how draw should work that fight Kauai.
- **SDL3 over SDL2:** built-in `SDL_ShowOpenFileDialog`/`SDL_ShowSaveFileDialog` removes the `nativefiledialog-extended` dependency entirely. Redesigned audio subsystem shrinks the AudioMan port. Better high-DPI on Retina (matters for 640×480 kidspace scaling). SDL2 is now in maintenance; picking it today bakes in a future migration. The tradeoff is that SDL3 (stable since Jan 2025) has less battle-tested ecosystem than SDL2, but by the time Project 3 actually ships, that gap will be much smaller.

**Depends on:** Project 1 (64-bit). Strongly benefits from Project 2 (the headless Kauai work directly bridges into a clean platform-backend abstraction).

**Tasks:**

1. **Kauai SDL backend files** mirroring the existing Win32 implementations: `appbsdl.cpp`, `gfxsdl.cpp`, `gobsdl.cpp`, `filesdl.cpp`, `fnisdl.cpp`, `dlgsdl.cpp`, `menusdl.cpp`, `picsdl.cpp`, `memsdl.cpp`. CMake picks backend by `CMAKE_SYSTEM_NAME` / explicit option. File dialogs (`dlgsdl.cpp`) wrap SDL3's built-in `SDL_ShowOpenFileDialog`/`SDL_ShowSaveFileDialog` — no extra dependency needed.
2. **Replace `Vfw32`** (AVI) — *highest-risk component, see §3a*.
3. **Replace `Winmm`** (MIDI) with fluidsynth + a free GM-compatible SoundFont (or libADLMIDI for an OPL3 vibe closer to original era). Splash/ambient music.
4. **Replace `Msacm32`/AudioMan** using SDL3's audio subsystem (`SDL_AudioStream` + `SDL_OpenAudioDevice`). `audioman/audioman.cpp` is already a thin wrapper — easy swap, and SDL3's audio API is closer to AudioMan's queue/mix model than SDL2's was.
5. **Replace `MM_TIME`** with `SDL_GetTicks` / `SDL_AddTimer`. **All clocks must agree** — palette animation, MIDI scheduling, AVI playback, and game tick all need to share one timeline. Pick one early; thread it through everything.
6. **BRender source build** — see §3b. This is its own sub-project with its own risk, not a single bullet here.

**Audio/MIDI codec tests** to add alongside tasks 3-4 above: pure parsers (`MIDI` stream events, WAV via `RiffHeader` in `inc/srec.h`), no platform dependency. Tests: parse fixed `.mid`/`.wav` blobs, verify decoded byte streams match a stored reference. Critical when Project 3 swaps `Winmm`/`Msacm32`/AudioMan — these tests verify the new audio backends produce the same decoded output as the old ones, otherwise audio drift / pitch bugs go undetected until a user hears them.

### §3a — AVI replacement is the highest-risk piece

Kidspace transitions sync to AVI playback. VfW delivers frames synchronously when the next frame is ready; ffmpeg/libav's frame delivery is asynchronous and threaded. Naively swapping in `libavformat`+`libswscale` will cause:

- Palette flashes during transitions (palette swap timing now off by frame queue depth)
- Audio drift on long Mczee animations
- Frame-rate mismatches if the source AVI is variable-rate

Plan: prototype this in isolation (single-AVI test harness with audio + palette) **before** wiring it into kidspace. Worth 2-3 weeks alone.

### §3b — BRender source build is its own sub-project

> "MIT-relicensed via foone's repos" is a realistic *direction*, not a realistic *path*.

The BRender 1.3.2 / 1997 source predates `<stdint.h>`, contains inline x86 assembly fast paths, and was tested against late-90s C compilers. Building for ARM macOS is unbounded. Realistic estimate: **2-6 weeks dedicated work to get a software-rendering-only build on macOS x86_64 + ARM and Linux x86_64**. Do not block Project 3's Windows 64-bit milestone on this.

**Sub-project sequencing:** ship Project 3 first as Windows-x64-only (using existing `bren.lib` after Project 1 sized-types pass); then take on BRender source build as a separate effort; then integrate into the SDL backend for non-Windows targets.

**Regression baseline before touching BRender:** before §3b's source-build effort, build a render-to-buffer hash test suite — load a fixed `TMPL` chunk, set known camera/lighting, render one frame to a software buffer, hash. **Without this, you have no way to know if the BRender source build is correct** — the binary you replace it with will produce different pixels and you won't be able to tell "different but right" from "different and wrong." Build this suite while still running on the original `bren.lib`, store the hashes, then verify against them after every BRender source-build change.

**Risk:** high. Multi-month even excluding BRender sub-project. Many subsystems, lots of platform-specific edge cases, palette-animation and AVI-blit timing especially fiddly.

**Abandonment criteria:** if the BRender source-build sub-project fails to produce a clean macOS/Linux build after **6 weeks**, drop back to a software-3D replacement (e.g., a tiny custom SW rasterizer or porting to a modern free renderer). Document the time spent and the specific blockers.

**Done when:** `3dmovie` opens on macOS / Linux, plays through the kidspace intro, opens a sample movie, plays it back at the right framerate with sound. Vertical-slice milestone first: splash + main menu rendering on macOS, no kidspace, no movie playback.

---

## Sequencing

1. **Project 0 (renames)** — already in flight. Background task; can interleave with anything else.
2. **Project 1 (64-bit build)** — first real project. **Hard prerequisite for 3.** Don't start Project 2 until at least task 4 (sized-types audit) is done; otherwise the shim ABI bakes in `long`-width assumptions you'll have to redo.
3. **Project 2 (libsoc + Python)** — second. Validates engine/UI separation. Read-only milestone unlocks tooling immediately. Write milestone is a follow-up, not part of the same shippable.
4. **Project 3 (SDL2)** — third. Windows-x64 milestone first, then BRender source sub-project, then macOS/Linux.

Project 3 is **not** independently shippable — it depends on Project 1 hard and benefits enormously from Project 2. Projects 1 and 2 ARE independently shippable.
