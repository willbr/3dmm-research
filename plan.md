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
- **Modern 3D renderer** (Vulkan / OpenGL / Metal, full color, lighting, full match-move camera animation) — the headline 3DMMPlus charter item. Requires a new file format that breaks 1995 compat and a renderer rewrite, not a port. Custom camera *poses* are achievable in 3DMMForever via UI-9's BKGD-embedding trick; what 3DMMPlus adds is *animated cameras within a shot* (continuous match-move, key-framed pans/zooms) — that requires new `SceneEventType` values which 3DMMForever's compat constraint forbids.
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

## Project 4: WebAssembly / browser deployment

**Why:** Once SDL3 is in (Project 3) and BRender source compiles cleanly (§3b or its software-3D fallback), the same codebase compiles to WASM with Emscripten and runs in any modern browser. SDL3 has Emscripten as a first-class target — `appbsdl.cpp` / `gfxsdl.cpp` / etc. cross-compile without source changes. This unlocks a "click-to-watch" movie sharing path that no other approach delivers.

**Compat:** unchanged. Same `.3MM` format, same playback semantics, just different runtime. The compat constraint applies to the file format, not to where the binary executes.

**Depends on:**
- Project 1 (64-bit). The sized-types audit must be done first; without it the engine and Kauai are full of `long`-width assumptions that emcc compiles incorrectly.
- Project 3 SDL3 backend, including §3b (BRender source build) **or** its 6-week-abandonment fallback (software-3D replacement). `bren.lib` is x86 Windows only — there is no shortcut.

### Milestone 4a: Read-only web player

**Why:** A simple "click here to watch this movie" sharing path. Massively smaller scope than porting the full studio. The most useful and most realistic first milestone.

**Implementation:**

- Drop `studio` entirely. Ship a build that links `engine + brender + kauai (SDL3) + audioman` plus a minimal "movie viewer" GOK tree — viewport, play/pause, scene-forward/back, scene name, volume slider. No toolbox, no easels, no portfolio, no help machine, no kidspace navigation flow.
- **Asset bundling.** Pre-transcode the AVIs (kidspace intros / Mczee animations) to WebM and use HTML5 `<video>` instead of bringing libav into WASM — saves 5-10 MB of binary size. Stock content `.3cn` files (`bkgds.3cn`, `tmpls.3cn`, `mtrls.3cn`, `snds.3cn`, `tdfs.3cn`) are required runtime assets but lazy-loaded on first reference. Tag manager already does drive-scan; adapt it to async fetch from a CDN.
- **`.3MM` input.** User picks via `<input type="file">` or File System Access API. Movie loads, autoplays. URL-based loading (e.g., `?movie=https://...`) for embeddable links.
- **No save support.** Read-only: opens, plays, stops. Eliminates the IDBFS / download-blob complexity for this milestone.
- **Audio.** SDL3's audio backend over Web Audio API. AudioMan replacement (Project 3 task 4) compiles to WASM the same way it does on macOS/Linux.
- **MIDI.** Either ship fluidsynth-WASM (~3 MB) plus a SoundFont, or pre-render the MIDI cues to PCM and ship those. PCM is smaller per-asset and avoids the runtime synth dependency; pick this for 4a.

**Scope: 6-8 weeks after Project 3 + §3b land** (or after the software-3D fallback is in). Most of the work is the simplified viewer GOK and the asset bundle / lazy-load pipeline. The build configuration (CMake + emscripten toolchain file) is bounded — a few days.

**Risk:** medium. Biggest unknowns are BRender-WASM (already covered by §3b's risk profile) and Web Audio latency vs. palette-animation timing (kidspace transitions are tight; verify on real browsers).

**Done when:** a static-hosted page loads `BONGO.3MM` (or another sample) and plays it through to the end with sound, in current Chrome/Firefox/Safari, with the kidspace intro skipped.

### Milestone 4b: Full web editor

**Why:** All of the studio in the browser — author and save movies entirely in browser. Useful but niche; editing 1995-format movies in a 2026+ browser is a narrow use case.

**Implementation:**

- Same WASM build as 4a but linking `studio` instead of the simplified viewer GOK. Includes the full toolbox, easels, browsers, portfolio.
- **Save/load.** File System Access API where supported (Chrome family); fall back to IDBFS for in-browser persistence and download-blob for export. Engine code calling `FileObject::PfilFromFni` synchronously needs adapting — Emscripten's IDBFS makes it look synchronous after a preload, but new file picks have to round-trip through async UI.
- **Threading.** Optional; single-threaded build first. Adding pthreads requires SharedArrayBuffer which requires COOP/COEP headers, which restricts embedding contexts. Skip unless profiling shows a real bottleneck.

**Scope: another 4-6 weeks** on top of 4a. Mostly UX adaptation (async file dialogs, save UX) and audio polishing.

**Risk:** medium-low given 4a is in. Most of the architectural risk (BRender, asset pipeline, audio) was paid down in 4a.

**Done when:** in a browser, the user can open a sample movie, edit it (add an actor, record a path, change a camera, save a scene), and export the result back to a downloadable `.3MM` file that round-trips correctly through desktop 3DMMForever.

**Honest framing:** 4b is more 3DMMPlus material than 3DMMForever. The compat constraint says "the file format works," but a full editor in the browser is enough scope to justify a fresh evaluation of whether to keep targeting the 1995 format. Defer the 4b decision until 4a has shipped and use is observed.

---

## Studio UI features (incremental backlog)

Discrete UX additions that don't fit Projects 1-3 but are individually shippable. UI-1 through UI-6 are pure UI changes that don't touch the `.3MM` file format. UI-7 produces video output (not `.3MM`), so it's also compat-safe. UI-8 produces standard one-scene `.3MM` files using the existing format. UI-9 *does* persist new content into `.3MM` files (custom CAM chunks embedded under user-customized BKGD copies) — but does so additively, using the same `ksidUseCrf` embedding pattern the studio already uses for custom MTRLs and TMPLs, so 1995 3DMM continues to load and play these movies correctly.

Listed roughly in increasing scope.

### UI-1: Exact-input dialogs for rotate / move / scale (low effort, low risk)

**Why:** Today every transform tool is mouse-drag-driven only. Authors who want "rotate exactly 90°" or "move actor to (X, Y, Z) = (50, 0, 100)" have no way to do it precisely.

**Implementation:** new `cidRotateActorTo` / `cidMoveActorTo` / `cidScaleActorTo` cids. Each has a small dialog with X/Y/Z text fields and an absolute/relative radio. Studio handler reads the dialog and calls `Movie::FRotateActr` / a new `Movie::FMoveActrTo` wrapper / `Movie::FScaleActr`. Add toolbox glyphs in `sectools.cht` or as menu items in `popups.cht`. Engine API is already there (`FRotateActr` accepts BRA radians, body has `SetPosition`); only the studio UI needs work.

**Scope: 2-3 days per tool.** Touches: 1 dialog, 1 cid handler, 1 wrapper method, 1 glyph each.

### UI-2: Numeric pose readout (low effort, low risk)

**Why:** Authors editing a path can't see exact actor coordinates. A status-line readout showing "X=12.3 Y=0.0 Z=-45.7 RotY=90°" updated on every tick eliminates a class of "did I move it where I meant to?" bugs.

**Implementation:** small `gob` rendered in a corner of the viewport, polls `Pscen()->PactrSelected()->Pbody()->GetPosition()` and orientation each frame. No engine changes required.

**Scope: half a day.**

### UI-3: Constrain-to-axis modifier (low effort, low risk)

**Why:** When you want to drag an actor along just one axis, holding Shift should lock the others. Standard 3D-tool behavior; absent today.

**Implementation:** in `MovieView::_FActorMouseDrag`, when `_grfcust & fcustShift`, snap the dominant axis component and zero the others before passing to `FMoveActr`. Same applies to rotation tools.

**Scope: 1-2 days.**

### UI-4: Free-fly preview camera (medium effort, medium risk)

**Why:** Today cameras are pre-authored presets per background; authors pick from N pre-baked angles via the camera browser, recorded as `sevtChngCamera`. There is no way to look at a shot from any other angle while editing. A free-fly preview camera lets authors orbit/pan/dolly during editing to inspect the scene; recorded playback still uses the preset.

**Importantly:** this is editor-only. The recorded `.3MM` still references preset cameras via `sevtChngCamera` — no on-disk format change. **Persisting custom poses is a separate feature (UI-9)** that uses BKGD embedding to stay 1995-compat-safe at a file-size cost; see UI-9 for details.

**Implementation:**

- New `_fFreeCam` flag on `MovieView` (or `Scene`).
- New tools `toolCamOrbit`, `toolCamPan`, `toolCamDolly`, `toolCamZoom`. Mouse-drag deltas modify a held `BMAT34` + FOV in place. Each tick calls `World::SetCamera(pbmat34, ...)` directly (`bren/bwld.cpp:429`), **bypassing `Background::FSetCamera`** so the preset state isn't disturbed.
- "Reset to preset" button — `_pbkgd->FSetCamera(pbwld, _pbkgd->Icam())`.
- Force-reset to preset when entering playback (`Movie::FPlaying()` true) or when starting to record actor motion.
- Actor placement (click-to-place ground-zero projection, `Background::_bmat34Mouse`) **always uses the preset matrix**, never the preview camera. Otherwise users would place actors based on a temporary view and be confused when the recorded shot has them in the "wrong" place.
- Toolbox UI: new "Camera" cover under the Settings primary tools — glyphs in `popups.cht`, scripts firing the new cids, command-map handlers in `studio.cpp`.

**Scope: 2-3 weeks.** Risk: medium. No file format changes, no compat exposure, no LP64 concerns — can ship without waiting on Project 1.

### UI-5: Multi-actor selection — Phase 1 (move-tool MVP) DONE

**Why:** Today `Scene::_pactrSelected` is exactly one pointer (`inc/scene.h:147`). Operating on N actors requires N independent edits. Standard editor multi-select would let users rotate, move, copy, or delete a group at once.

**Phase 1 (move-tool MVP), shipped on branch `c`:**

- Spec: `docs/superpowers/specs/2026-04-29-multi-select-move-mvp-design.md`
- Plan: `docs/superpowers/plans/2026-04-29-multi-select-move-mvp.md`
- Commits: `b27ac34..bf74f13` (Tasks 1-5 with their fix-ups).

What landed:

- `Scene::_pglpactrSelExtra` (lazy `DynamicArray` of additional selected actors, `pvNil` when no extras) plus `FIsActrSelected` / `CactrSelected` / `PactrSelectedAt` / `FToggleActrSelected` / `ClearSelection` accessors. `SelectActr` clears extras as well as primary; `RemActrCore` scrubs extras when an actor is removed (no dangling pointers).
- `ActorMoveGroupUndo` composite undo: owns N child `ActorUndo` snapshots and replays them as a unit. The undo loop stops on the first child failure to avoid post-`ClearUndo` perturbation.
- Shift-click an actor with the move/default/select tool to toggle its membership in the selection set (3 px drag tolerance disarms the toggle and falls through to the existing single-actor drag).
- With N≥2 selected, a `toolCompose` drag iterates the selection and applies the same world-space delta to each via `FMoveRoute` / `FTweakRoute`. Cmd-tweak and Shift-entire-subroute modifiers carry through per-actor. One drag = one undo step (incidentally fixed a latent Tweak-undo leak; see commit `c1022fd`).
- Esc (`kvkEscape`, fixed Mac value at the same time) clears the entire selection.
- Ctrl+A (Cmd+A on Mac) selects every actor in the current scene via new `Scene::FSelectAllActrs`; first actor becomes primary, rest are added as extras. Falls through to the base in text mode so TBOX edit can still interpret it as select-all-text.
- Non-move tools collapse to single-select on mousedown via the existing `SelectActr` path — no extra code needed, since Task 1's `SelectActr` clears extras.
- Compat: pure runtime UI state, never persisted. Original 1995 3DMM playback unaffected.

**Acceptance status (9 criteria from the spec):**

Automated-verified by build + spec-compliance + code-quality reviews:

- (1) Single-actor behavior bit-for-bit identical: build passes; reviewers confirmed single-select code paths unchanged.
- (5) One-undo-per-drag: code review traced per-actor `ActorUndo` refcount/ownership through `ActorMoveGroupUndo`.
- (8) Deleting a selected actor leaves no dangling pointer: `RemActrCore` scrubs extras (Task 1 fix-up `3775fcf`).
- (9) Original 1995 3DMM still loads / plays movies produced: theoretical — nothing was added to disk format.

Deferred to user manual smoke (interactive click sequences, cannot run GUI from a subagent):

- (2) Shift-click on unselected actor adds it; hilite turns on; no drag.
- (3) Shift-click on selected actor removes it; hilite turns off.
- (4) N≥2 + `toolCompose` drag translates all selected by the same delta. Cmd / Shift modifiers apply per-actor.
- (6) Esc clears all selection.
- (7) Non-move-tool click on actor collapses selection.

**Phase 2a (rotate / resize / squash-stretch around group centroid) shipped on branch `c`:**

- Spec: `docs/superpowers/specs/2026-04-30-multi-select-rotate-scale-design.md`
- Plan: `docs/superpowers/plans/2026-04-30-multi-select-rotate-scale.md`

What landed:

- `Scene::FXyzSelectionCentroid` averages currently-visible (`FIsInView`) selected actors' world positions.
- `MovieView::_xrPivot/_yrPivot/_zrPivot` capture the centroid at mousedown when the tool is rotate / resize / squash and N≥2 are selected; same `ActorMoveGroupUndo` infrastructure as Phase 1 captures per-actor snapshots.
- `_MouseDrag` rotate / resize / squash branches on `_paundGroup != pvNil`: per actor per tick, compute world-space `delta_i` to orbit/scale the actor around the frozen pivot, then `FMoveRoute(delta_i)` and `FRotate / FScale / FPull` for the local body change. Cmd modifier (`fFromHereFwd`) carries through. Single-select path unchanged.
- One drag = one undo step (reuses `ActorMoveGroupUndo`).

**Phase 3 (named selection groups via hashtag tags) shipped on branch `c`:**

- Spec: `docs/superpowers/specs/2026-04-30-actor-tag-groups-design.md`
- Plan: `docs/superpowers/plans/2026-04-30-actor-tag-groups.md`

What landed:

- `Scene::FSelectActrsByTag` parses ASCII `#tag` substrings out of actor names (case-insensitive) and replaces the current selection with every matching actor in the scene.
- `ActorRenameGroupUndo` composite undo for batched actor-rename ops (mirrors `ActorMoveGroupUndo`'s shape but uses a `StringTable_GST` to store the swap state).
- `Ctrl+G` appends the next free `#N` (auto-numbered, scene-local) to every selected actor's name. `Ctrl+Shift+G` strips the rightmost tag from each selected actor. `Ctrl+1`..`Ctrl+9` re-selects the matching numeric group.
- Tags ride in the existing `_pgstmactr` string table inside the movie chunk — no on-disk format change. Original 1995 3DMM displays the tag suffix verbatim; round-trip is preserved.

**Phase 2+ remaining work (still in scope for "UI-5"):**

- Marquee / box-select / lasso selection.
- Multi-target costume change, sooner/later, action change.
- `BuildActionMenu` becomes the intersection of available actions across the selection.
- Distinct hilite color for primary vs. secondary selected actors.
- Multi-select for text boxes, and mixed actor + tbox selection.
- Phase B of named groups: alphanumeric tag picker (`Ctrl+/`), dimmed `#tag` rendering in roll-call, movie-wide `#tag` operations.
- Optional `fMultiSelect` feature flag if a regression risk emerges.

**Scope: phase 1 shipped; phase 2+ is 2-3 weeks of additional work, medium risk** — touches every tool path. Compat-safe (selection state isn't persisted to `.3MM`).

### UI-6: Integer UI scaling for modern displays (medium effort, medium risk)

**Why:** Studio runs at fixed 640×480, illegible on modern Retina/4K displays. Authors want 2x/3x/4x scale.

**Implementation: defer to Project 3.** SDL3 provides this almost for free via `SDL_SetRenderLogicalPresentation` with `SDL_LOGICAL_PRESENTATION_INTEGER_SCALE`. Doing it on Win32 first would require introducing a logical-canvas concept into Kauai's `gob.cpp` + `gfxwin.cpp` plus mouse-coord unscaling on the input path — 2-3 weeks of work that becomes nearly free under SDL3.

**Scope: 1-2 days as part of Project 3 SDL3 backend; 2-3 weeks if attempted on Win32 first.** Don't do the Win32 version.

### UI-7: Render-to-video with free-fly camera (medium effort, medium risk)

**Why:** Authors who want to share a movie as video (rather than as a `.3MM` requiring 3DMM to play) have no path today. Combined with UI-4, this also enables machinima — render an existing movie's timeline through a user-chosen camera path, write to MP4/WebM/AVI.

**Depends on:** UI-4 (free-fly camera) and Project 3 §3a (which already pulls libavformat/libavcodec into the build for AVI playback replacement).

**Implementation:**

- Capture each rendered frame from BRender's RGB output buffer (via the existing palette-to-RGB conversion path) and feed to `libavcodec`'s encoder.
- New `cidRenderToFile` command. Dialog asks for output path, codec choice, resolution (1×/2×/4× the logical 640×480), frame range.
- During render, **suppress the scene's `sevtChngCamera` events** when a free-fly path is active — otherwise BRender camera would jump back to the preset every time the recorded event fires.
- Audio: muxed from the existing scene playback (sound events) into the output file. Same audio in render mode as in playback.
- Optional: a small camera-path **sidecar file** (e.g., `.3SH` "shot list") storing `(scene_index, frame, camera_matrix, fov, hither, yon)` keyframes. Render interpolates between keyframes. Sidecar is a separate file from the `.3MM` — original movie unchanged, multiple sidecars give multiple takes.

**Scope: 4-6 weeks** including audio mux, palette-state-per-frame handling, codec parameter UX. The encoder integration is the bulk; the camera-suppression flag and sidecar format are small additions on top.

**Compat:** doesn't touch `.3MM`. Output is video, not movie. Sidecar is a new file type the new reader knows about and the old reader ignores (sidecars never get loaded by 1995 3DMM).

### UI-8: Reusable scene library (medium effort, low risk)

**Why:** Today scenes only exist inside `.3MM` movies. A scene authored once with a particular set of actors, paths, sounds, and camera cuts can't be reused in another movie without manual recreation. A scene library lets authors save individual scenes and import them into other movies.

**Key insight: no new file format needed.** `SCEN` chunks already write to any `PChunkyResourceFile` via `Scene::FWrite` (`src/engine/scene.cpp:4187`) and read from any via `Scene::PscenRead` (line 3764). A scene library is just a directory of one-scene `.3MM` files — each is a tiny standard movie with a single SCEN under MVIE. Original 1995 3DMM can open these as single-scene movies; the new reader can extract the scene and inject it into another movie.

**Implementation:**

- **"Save scene as..."**: pick scene N from the open movie. Create a new chunky file with file-type `'MVIE'`, write a fresh MVIE root chunk (`MovieFilePrefix` with `bo`, `osk`, `dver` set to current values), call `Scene::FWrite(pcrfDest, &cnoScen)`, adopt the SCEN under MVIE at chid 0, write the standard companion children (roll-call GST, source GST). Save with `.3MM` extension.
- **"Insert scene from..."**: file picker for `.3MM` files. Open as `PChunkyResourceFile`, find the first SCEN child, call `Scene::PscenRead`, then use the existing scene-paste path to insert into the current movie at the chosen position. Tag references resolve through the destination's tag manager — the destination must have access to the same content sources (templates, materials, sounds, **backgrounds**) the source referenced.
- **Optional library browser**: kidspace browser (modeled after the camera/actor browsers) listing `.3MM` files in a known directory, showing each scene's thumbnail (the SCEN already has a `kctgThumbMbmp` child), drag-drop into the current movie.

**Camera handling:**
- Preset cameras (`sevtChngCamera icam=K` referencing a built-in BKGD camera) ride along automatically — they're just integers in the SCEN's frame-event GST.
- If the source scene used a custom camera (UI-9), the saved scene must also embed the BKGD copy with the custom CAMs (or the import has nothing to resolve the high icam against). UI-9 import logic handles this.

**Scope: 2-3 weeks.** The hard part isn't the file format — it's tag resolution across movies (importing requires the destination's content stack to provide the same template/material/sound chunks the source referenced). For backgrounds with custom cameras, see UI-9.

### UI-9: Custom camera authoring with BKGD embedding (high effort, medium risk)

**Why:** UI-4 lets authors free-fly to a custom angle but only for preview. UI-9 makes that pose **persist** in the movie file — the recorded `.3MM` plays back from the custom angle in both 3DMMForever and original 1995 3DMM.

**Compat strategy:** the chunk-graph design already supports this without new chunk types or new event types:

- Cameras are `CAM` chunks owned as children of the BKGD chunk (`src/engine/bkgd.cpp:258`).
- `sevtChngCamera icam=N` looks up child #N of the scene's BKGD via generic chid enumeration (no fixed range check).
- The studio already embeds custom MTRL/TMPL chunks in movies via `ksidUseCrf` tags — the same mechanism applies to BKGDs. When the tag resolves, the engine reads the BKGD from the movie file instead of `bkgds.3cn`.
- 1995 3DMM uses the same tag-resolution code path. It will load the embedded BKGD, count the extra CAM children correctly, and resolve `sevtChngCamera icam=N+K` to the new camera's matrix without any code changes. **Compat-safe.**

**Implementation:**

- When the user records a free-fly pose (UI-4 active + "save this angle" command):
  1. Check if the scene's BKGD is already embedded (`tagBkgd.sid == ksidUseCrf`). If not:
     - Copy the standard BKGD chunk from `bkgds.3cn` into the movie file.
     - Adopt all of the standard BKGD's children (existing CAMs, BDS, GLLT, palette, etc.) as children of the embedded copy. Use the chunky-file's multi-parent child support if possible to share data; otherwise deep-copy.
     - Update `tagBkgd` to reference the embedded BKGD (`sid = ksidUseCrf`, new cno).
  2. Write a new `CAM` chunk into the movie with the custom pose's data (`CameraPosition` struct: matrix, FOV, hither/yon, APOS).
  3. Adopt the new CAM as a child of the embedded BKGD at chid `_ccam` (next index past the existing presets).
  4. Write a `sevtChngCamera icam=_ccam` event into the SCEN's frame events GST at the current frame.
  5. Increment `_ccam` for future custom cameras in this movie.

- **Garbage collection at save:** add BKGD GC to `_FDoMtrlTmplGC` (or sibling routine) — if no scene's `sevtChngCamera` references an embedded BKGD's custom CAMs anymore, drop the embedded BKGD copy and revert tags to the standard `bkgds.3cn` reference.

- **UI:** "Save current preview camera as new angle" button on the camera toolbar (next to UI-4's free-fly tools). Once saved, the new angle appears in the camera browser alongside built-in presets.

**File-size caveat:** an embedded BKGD is 200 KB-2 MB depending on the source. Each distinct background that gets a custom camera adds that overhead to the movie file. **Multiple scenes using the same custom-camera background share the embedded BKGD** (multi-parent child support means the embedded BKGD is referenced by all scenes that use it; only embedded once per movie).

**Scope: 4-6 weeks.** The BKGD-embedding logic is the load-bearing piece; needs careful testing to confirm 1995 3DMM correctly resolves the embedded BKGD, plays the custom angles, and doesn't lose data on re-save. Add a regression test (per §0a / §2a) that round-trips a custom-camera movie through 1995 3DMM (or, since we don't have a 1995 binary, through a strict-mode 3DMMForever reader that simulates the 1995 behavior).

**Risk:** medium. Edge case to watch: BKGDs reference content (palette chunks, lights, default sound) that may itself be tagged. Embedding a BKGD requires resolving and either copying or sharing those references. The garbage-collection logic at save time has to be careful not to drop chunks that the embedded BKGD's children still reference.

### UI-10: Flash-style timeline widget (high effort, low risk)

**Why:** Today the studio's only timeline UI is a single frame slider plus a scene scrollbar. Authors can't see at a glance when an actor enters or leaves, when a sound starts, when a camera cut happens. Multi-actor scenes are hard to visualize. A Flash-style timeline — horizontal time axis, vertical layer stack, bars showing object lifetimes, keyframe icons for scene events, scrubbable playhead — is a substantial UX upgrade.

**Why this codebase is well-suited:**

- The data is already there in the right shape. `Scene::_pggsevFrm` is literally a list of `(nfrm, sevt, payload)` tuples — exactly what a timeline visualizes. Actor paths are per-frame continuous keyframed data (`PATH` chunks). The playhead is `Scene::_nfrmCur`.
- No file format changes required — purely a new view onto existing data.
- No engine API changes for read-only visualization. For drag-to-retime editing, the engine already has scene-chop / scene-paste primitives that shift events; the timeline just exposes them via a new UI path.
- Compat-safe by construction (UI-only).

**Phased implementation:**

- **Phase 1 — Read-only visualization (~3-4 weeks).** New `gob` rendered below the 3D viewport (or pop-out). Horizontal axis: frames. Vertical layers: one per actor (lifetime bar from sevtAddActr to scene end), one per text box, one per sound channel, one per camera (cuts shown as keyframe markers), one per scene-level event. Playhead drawn at `_nfrmCur`. Click-to-scrub: clicking on the timeline jumps `_nfrmCur` to that frame. No editing yet, just a viewer that always stays in sync with the live scene state.
- **Phase 2 — Drag-to-scrub + multi-select (~2-3 weeks, depends on UI-5).** Drag the playhead. Multi-select keyframes. The undo-grouping work in UI-5 unblocks bulk timeline operations.
- **Phase 3 — Drag-to-retime events (~3-4 weeks).** Drag a keyframe to a different frame, scene event's `nfrm` updates. Drag an actor's lifetime bar to shift their entire path data — uses existing scene-chop / scene-paste primitives under the hood. Bulk drag of N keyframes coalesces to one undo group (UI-5 prerequisite).
- **Phase 4 — Bonus features (variable scope).** Onion skinning (overlay neighboring frames), tweening visualization, keyframe-type icons (different glyph per `sevt`), zoom in/out on the timeline, horizontal scroll for long scenes. These are individually scoped sub-features that can be added or skipped per priority.

**Layout challenge:**

640×480 is small. The timeline needs vertical space the studio doesn't easily give up. Options:

- Pop-out window (Kauai supports MDI gob trees). Simple but requires multi-window coordination.
- Replace one of the secondary tool panels when timeline mode is active (toggle on/off via toolbar button).
- Wait for UI-6 (integer UI scaling) — at 2x scale the studio has 1280×960 logical pixels and the timeline can sit below the viewport without crowding.

The "wait for UI-6" path is probably the cleanest, which also implies waiting for Project 3 SDL3 backend to land integer scaling cheaply. Or just ship Phase 1 in pop-out form on the existing 640×480 layout and let UI-6 unblock the integrated layout later.

**Scope: 8-12 weeks** for Phases 1-3. Risk: low — no engine changes, no file format, no compat exposure. The risk is purely scope creep; Flash-style timelines are notoriously fractal (per-property keyframes, custom interpolation curves, motion paths overlaid on viewport, layer locking/visibility, frame labels, looping markers). Stay disciplined with the phased rollout.

**Dependencies:** UI-5 (multi-actor selection) is a hard prerequisite for Phase 2-3 because the undo-grouping infrastructure for bulk operations is the same problem solved twice if done independently. UI-6 (integer scaling) is a soft prerequisite for the integrated layout (Phase 1 can ship as pop-out without it).

**Compat:** zero. This is purely a new view + new UI input paths over existing data. The on-disk `.3MM` format doesn't even know the timeline exists.

### UI-11: Custom prop composer (medium effort, low risk)

**Why:** 3DMM ships with a fixed set of props (chairs, vehicles, signs, etc.) authored at Microsoft. Users can't author new ones. A primitive-composer easel (similar in shape to the existing 3D Words easel) lets authors build static props from cubes, cylinders, spheres, etc., resize/position them, and save the result as a custom prop usable like any built-in.

**Why this codebase fits well:**

A "prop" in 3DMM terminology is just a `TMPL` chunk with one flag bit set: `ftmplProp` (`inc/tmpl.h:134`). Props use the same data structures as character actors — same body hierarchy, same action system, same path system. The bit only controls which browser the prop appears in (props vs. actors). A static prop is a *very* simple TMPL: one body part, one ACTN ("rest"), one CEL pointing at a single MODL chunk. Nothing exotic.

`Model::PmodlNew(cbrv, prgbrv, cbrf, prgbrf)` (`inc/modl.h:57`) is public API — takes raw vertex/face arrays and builds a Model. The 3D Words easel already uses this pattern to generate 3D text geometry on-the-fly. Same pattern applies for prop geometry.

The studio already embeds custom TMPL chunks in movies via `ksidUseCrf` tags (the mechanism behind custom-character costumes). `_FDoMtrlTmplGC` (`src/engine/movie.cpp:2774`) garbage-collects unreferenced embedded TMPLs at save time — works for custom props with no changes since the GC doesn't distinguish prop from character TMPLs.

**Compat:** original 1995 3DMM resolves embedded TMPLs via the same tag-resolution code path used for custom characters today. It reads the embedded MODL, hands the geometry to BRender, renders. **Custom user-built props work in 1995 3DMM unchanged**, no new chunk types or event types required.

**Phased implementation:**

- **Phase 1 — Primitive composer (5-7 weeks).**
  - Pure-C++ primitive geometry generator: cube, cylinder, sphere, cone, plane. Returns `(BRV*, count, BRF*, count)` arrays. ~1 week.
  - New easel — kidspace `.cht` + new gob. Shape similar to the 3D Words easel: small preview viewport, primitive picker, per-primitive transform widgets (position X/Y/Z, rotation X/Y/Z, scale), part list, name field, "save as prop" button. ~3 weeks.
  - MODL generator: takes the user's primitive composition, applies transforms, optionally welds duplicate vertices, emits a single MODL chunk via `Model::PmodlNew` then writes via `Model::FWrite`. ~1 week.
  - TMPL writer: wraps the MODL in a TMPL chunk with `ftmplProp` set, single body part, single "rest" ACTN with one CEL referencing the MODL by chid. ~1 week.
  - Embed + roll-call wiring: add the new TMPL to the movie via `ksidUseCrf`, register in the props browser (the browser already filters roll-call by `ftmplProp`, no browser changes needed). ~1 week.
- **Phase 2 — Materials + color (2-3 weeks).** Assign existing MTRLs (or solid colors via a CMTL) to faces. Per-primitive material picker. UV generation: simple planar/box projection per primitive type.
- **Phase 3 — Multi-part / articulation (3-4 weeks).** Build a TMPL with N body parts (one per primitive), so user can later author a prop with movable parts. Reuses the body hierarchy infrastructure that character actors already use.
- **Phase 4 — Mesh import (open-ended).** Import `.obj` / `.gltf` / similar, convert to BRender vertices/faces, emit MODL. Substantial scope and intersects with the v3dmm content pipeline. Defer.

**Caveats:**

- **BRender geometry validation.** Stock MODLs were authored via SoftImage and tend to be clean closed manifolds. User-built geometry might have non-manifold edges, flipped face winding, near-coincident vertices. BRender's renderer can glitch on these (z-fighting, missing faces). Add a validator pass that runs on Save with warnings/auto-fix for common cases.
- **Pivot point.** Each MODL has a `bvec3Pivot` controlling rotation behavior. Default to bounding-box center; expose an override in the easel.
- **No texture coords in Phase 1.** Untextured geometry only. Phase 2 adds materials and the UV-generation problem.
- **File size.** Embedded MODLs are small (KB range); embedded TMPLs are slightly bigger but still small. Multiple custom props per movie are cheap. Garbage-collected at save when unused.

**Scope: 5-7 weeks for Phase 1; 10-14 weeks for Phases 1-3.** Phase 4 (mesh import) is open-ended.

**Risk:** low — engine API is fully there, embedding pattern is existing, browser integration is automatic. Main risk is BRender quirks with user-built geometry; mitigated by the validator pass.

**Dependencies:** none. Could ship today on Win32 without Projects 1-4. UI-1 (exact-input dialogs) is a nice-to-have for the per-primitive transform widgets but not required.

### Recommended order

Bundles into roughly five phases:

1. **Quick wins (1-2 weeks total):** UI-1, UI-2, UI-3. Each ships visible value, each proves the "add a tool" pattern works without disturbing anything load-bearing. Any order, parallelizable.
2. **Free-fly camera (2-3 weeks):** UI-4. Higher value, more design surface; requires the camera-cover toolbar and careful interaction with actor-placement projection. Editor-only — doesn't touch the file format.
3. **Custom-content composers (10-16 weeks total):** UI-11 prop composer (5-7 weeks Phase 1; +5-7 weeks Phases 2-3) and UI-9 custom camera authoring (4-6 weeks, depends on UI-4). Both add user-authored content via `ksidUseCrf` embedding — natural to design and ship together since they share the embedded-content patterns.
4. **Scene library (~2-3 weeks):** UI-8. Independent of UI-9/UI-11 but synergizes — once they land, library scenes can carry custom cameras and custom props via their embedded TMPLs/BKGDs.
5. **Project 3-era (UI-5, UI-6, UI-7, UI-10):** UI-5 multi-select needs careful undo/menu design (3-5 weeks); UI-6 integer UI scaling falls out of SDL3's renderer almost for free; UI-7 render-to-video sits naturally on top of Project 3 §3a's libav integration; UI-10 timeline is the most ambitious (8-12 weeks, hard prereq on UI-5). All gated on Project 3 reaching at least Windows-x64.

Each item is independently shippable and independently revertible.

## Sequencing

1. **Project 0 (renames)** — already in flight. Background task; can interleave with anything else.
2. **Project 1 (64-bit build)** — first real project. **Hard prerequisite for 3 and 4.** Don't start Project 2 until at least task 4 (sized-types audit) is done; otherwise the shim ABI bakes in `long`-width assumptions you'll have to redo.
3. **Project 2 (libsoc + Python)** — second. Validates engine/UI separation. Read-only milestone unlocks tooling immediately. Write milestone is a follow-up, not part of the same shippable.
4. **Project 3 (SDL3)** — third. Windows-x64 milestone first, then BRender source sub-project, then macOS/Linux.
5. **Project 4 (WASM / browser)** — fourth, gated on Project 3 reaching macOS/Linux + §3b's BRender-source-build (or its software-3D fallback). Ship 4a (read-only player) before 4b (full editor); defer the 4b decision until 4a has shipped.

Projects 0, 1, and 2 are independently shippable. Projects 3 and 4 have hard dependencies on Project 1, and Project 4 has a hard dependency on Project 3.
