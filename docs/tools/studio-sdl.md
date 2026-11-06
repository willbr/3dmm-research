# Existing Studio through SDL3

The port keeps Studio's original Kauai UI, resource scripts and editing tools.
The archive inspector is a separate diagnostic application.

## Debug validation

Ordinary Debug builds retain assertions, range and null checks, debug symbols,
allocation tracking and memory failure injection. Repeated recursive object
validation and allocation header/footer checks are disabled by default: these
checks can dominate frame stepping in movies with dense animation tracks.

For a diagnostic Debug build with the original validation enabled, configure
with `-DKAUAI_EXPENSIVE_CHECKS=ON`, then rebuild. For example:

```sh
cmake --preset x64:msvc:portable-sdl-debug -DKAUAI_EXPENSIVE_CHECKS=ON
cmake --build build/studio-portable-x64 --target studio
```

Set the option back to `OFF` and rebuild to restore normal Debug validation.
The option propagates through `kauai-core` to its consumers and only takes
effect in Debug configurations. Release and RelWithDebInfo are unchanged.
The existing `cactAV` control adjusts recursive object-validation depth only
when expensive checks are compiled in. `debug-checks` verifies both policies
using the selected build configuration.

## Run with the retail ISO

The SDL executable can run with just the executable and an original ISO 9660
3D Movie Maker disc image. It uses the original disc's UI archives; rebuilding
or bundling replacement `.chk` files is unnecessary for this mode. A single
`.iso` beside the executable (or in the working directory) is discovered when
no extracted content tree is selected. To select a particular image explicitly:

```sh
./3dmovie-sdl --iso "/path/to/3DMOVIE100.iso"
```

Windows uses the same option on `3dmovie-sdl.exe`. `--iso=PATH` and the UTF-8
`THREEDMM_ISO` environment variable are also supported. Explicit ISO selection
takes precedence over `THREEDMM_CONTENT_ROOT`. Without explicit ISO selection,
the existing extracted-tree lookup is preferred; ISO discovery happens before
falling back to unrelated system installations. Multiple adjacent ISOs require
an explicit selection.

The Windows portable runtime builds as a GUI executable, so normal launches do
not open a console window. MSVC uses `mainCRTStartup` with the existing portable
`main(argc, argv)` adapter; redirected stdin/stdout remain available for
`--mcp-server` automation. Mac/Linux startup is unchanged.

On first use, the built-in reader extracts the retail product files, sample
movies, help files and Comic Sans fonts into an application-private cache.
It does not mount the image, run its installer or require 7-Zip. The software
MIDI instrument bank and its licenses are embedded in the executable and written
to the same cache, so no adjacent `assets/` folder is needed. Explicit font and
SoundFont overrides remain supported. The default cache is under SDL's per-user
preference directory for `3DMMForever/Studio`; `THREEDMM_CACHE_DIR` selects an
absolute alternative.

Cache identity includes the canonical ISO path, size, modification time and
supplemental MIDI contents. A completed manifest and file sizes are checked on
reuse. Extraction publishes atomically, handles concurrent launches and rejects
unsafe filenames, out-of-range extents, truncated images and unsupported layouts.
An incomplete or damaged cache is preserved and a new generation is created;
cached samples or other existing files are never deleted during repair. The
original ISO and existing `Microsoft Kids` trees remain untouched. Expansion
packs and user-authored content are separate from the base-game ISO.

`studio-iso-cache` tests a synthetic disc, cache reuse/repair, fonts and samples,
malformed input, supplemental files and concurrent extraction. With
`TEST_STUDIO_ISO` set to an available retail image, `studio-iso-workflow` runs a
copied executable in an isolated directory containing only those two files. It
checks cold/warm caching, room colors, audible software MIDI, scene creation,
save/reopen and explicit ISO precedence. Reports and test packages are under
`tmp/studio-iso-workflow/`.

## Mac/Linux Studio build, 4 November 2026

The full SDL Studio target is enabled on Mac/Linux. Resource preprocessing now
supports Clang/GCC as well as MSVC, preserves the original lexer's line format,
and tracks nested header dependencies with Ninja/Make. Native builds build and
run `chomp` automatically (`build/studio-sdl/tools/chomp` on POSIX). Cross builds
must supply a host tool with `-DCHOMP_EXECUTABLE=/path/to/host/chomp` or an emulator.
The embedded dialog templates require no Windows resource compiler.

With a native C/C++ toolchain, CMake and Ninja installed, configure an original
Microsoft Kids data tree as the content overlay (the directory containing
`3D Movie Maker` and `Users`):

```sh
cmake --preset sdl-debug -DMSKIDS_OVERLAY_DIR="/path/to/Microsoft Kids"
cmake --build build/studio-sdl
cmake --install build/studio-sdl
KAUAI_FONT_PATH="/path/to/fonts" ./dist-sdl/3dmovie-sdl
```

Dependencies are fetched by the build. macOS needs Apple's native SDK/toolchain
for SDL's Cocoa backend. Native Apple Silicon build/run checks now pass, as
recorded below. Original data and fonts are separate inputs; see
[`FONTS.md`](../../FONTS.md). The font fixture used for Linux verification includes
the original Windows families, not an invented replacement font. No fonts or
proprietary SDK files are added to the repository.

Installation renames the generated main archive to `3dmovie.chk`, preserves newly
built resource archives when overlaying original data with different filename
casing, and creates the default profile directory even without the sample CDs.
The executable recognizes its adjacent `Microsoft Kids` tree. A separately
located tree can be selected with `THREEDMM_CONTENT_ROOT`.

For the local ISO in `dist`, prepare an original content overlay and the bundled
Comic Sans fonts without mounting the image:

```sh
7zz e dist/3DMOVIE100.iso '-odist/original/Microsoft Kids/3D Movie Maker' '3DMOVIE/*' -aos
7zz e dist/3DMOVIE100.iso '-odist/original/Microsoft Kids/Users/Melanie' 'SAMPLES/*' -aos
7zz e dist/3DMOVIE100.iso -odist/original/fonts SETUP/COMIC.TTF SETUP/COMICBD.TTF -aos
cmake --preset sdl-debug -DMSKIDS_OVERLAY_DIR="$PWD/dist/original/Microsoft Kids" -DCMAKE_INSTALL_PREFIX="$PWD/dist/sdl"
cmake --build build/studio-sdl --parallel 8
cmake --install build/studio-sdl
KAUAI_FONT_PATH="$PWD/dist/original/fonts" ./dist/sdl/3dmovie-sdl
```

This uses the ISO's complete `BLDGDATA.CHK`; the separate `Microsoft Kids.7z`
installation has a much smaller replacement. Generated archives still take
precedence over the original overlay. Build artifacts and extracted content
stay under the ignored `build` and `dist` directories.

The canvas fits the available area while preserving its 4:3 aspect ratio.
Open **Product Info** with **Control+Shift+I** and enable **Integer scaling** to
restrict scaling to whole-number pixel multiples in windowed, maximized and
fullscreen modes. **Make these the default settings** saves the scaling choice
for future launches.

**Alt+Enter** toggles desktop fullscreen and restores the previous window size
when returning to windowed mode. Holding the keys toggles only once per press.
The shortcut preserves the scaling choice, updates Product Info's window-mode
selection, and remains available while placing an actor or prop. The Windows
`--fullscreen --actor` smoke workflow checks these paths with desktop key input.

**Shift+click Play** starts movie-only playback in the current window. The existing
544x306 movie viewport fits the window at its original 16:9 aspect ratio; Studio's
controls are omitted from presentation. A windowed app stays windowed, and an
already-fullscreen app stays fullscreen. **Alt+Enter** controls fullscreen independently.
**Space** pauses/resumes this mode, with
a large **Paused / press space to resume** banner. The movie clock and queued
audio pause in place, retaining frame, scrolling-caption progress and remaining
authored waits. Clicks while Space-paused do not resume or dismiss an authored
pause. **Escape** stops playback and restores Studio in the current window;
natural completion also restores the editor. Ordinary Play
and the original movie data remain unchanged.

Product Info's **Render size** selects **Original (544x306)**, **2x super size
(1088x612)** or **4x super size (2176x1224)**. Original is the default. Super sizes
rerasterize actors, props and 3D text with the same camera framing; they do not
add detail to the original background artwork. Background colors and depths are
replicated to preserve the authored occlusion. Both Studio and movie-only playback
render the complete movie view at the selected size, including 2D text boxes and
scrolling credits. Studio composes a **1280x960** image at 2x or **2560x1920** at
4x, placing the full-resolution movie directly inside the enlarged interface.
It avoids reducing the movie to 544x306 before enlarging Studio. Bitmap controls
use nearest-neighbor scaling; the layout, hit testing and mouse coordinates
remain 640x480. Normal draw order keeps panels, text editing and dialogs above
the movie, and custom cursor masks scale with the presented image. Movie-only
presentation fits the movie itself to the window and adds the pause banner.
The shared renderer retains the movie palette so both views preserve the original
colors and transitions. The legacy palette-based scene transitions retain their
native canvas resolution. **Better Speed** applies to Original size; super sizes use
full-quality buffers and require more CPU and memory. **Make these the default
settings** persists the rendering size without changing movie files.

Equal-size indexed and true-color transfers use SDL surface blits. Unsupported
legacy formats, palette remapping, overlapping buffers and legacy scaling keep
the existing pixel path. The high-resolution movie remains indexed until a
true-color consumer needs it, and its RGB conversion is cached per frame.
Movie-only playback supplies an indexed SDL texture and palette; SDL handles
renderer support and conversion. Studio caches its composed image and redraws
changed regions through the normal object tree, retaining foreground panels
and editing controls. Unchanged frames reuse the uploaded texture. Software
AND/XOR cursors retain true-color compositing, including inverted colors.

In the Windows x64 Debug desktop benchmark (Rebirth scene 19, frames 15–34,
three runs), median 4x Studio playback improved from 2.18 to 6.60 fps and 4x
movie-only playback from 3.61 to 6.61 fps. Original stayed at 6.66 fps. These
reach the existing playback clock's pace; no movie timing was changed. Five
captured Rebirth frames matched the prior native and 4x composed images exactly.
Evidence: `tmp/sdl-presentation/performance.json`. The 4x fixture workflow also
passed transitions, scrolling captions and pause/resume. Desktop editing checked
text entry, the font popup, save and caret blinking with rapid screen captures
of the caret itself (`tmp/shared-render/edit-3_8hzbqo/report.json`), avoiding
aliasing from repeated large PNG transfers.

`studio-movie-only-workflow` checks generated animated and scrolling-caption
movies against the ISO, including a scene dissolve, frozen images/state, resume,
Escape while paused, natural completion and unchanged movie hashes. It runs headlessly with
SDL's dummy driver. Run `tests/studio_movie_only_workflow.py EXE FIXTURE ISO
ARTIFACTS --desktop` for real display checks and pause-banner desktop captures.
`--render-scale 2` or `--render-scale 4` also checks actual frame dimensions,
foreground detail finer than repeated native pixels, frozen high-resolution
captions, resumed scrolling, live size changes and saved settings across restart.
It also checks that idle Studio text changes with render size, that Studio controls
remain unchanged, and that its fitted text pixels match the movie-only image.
MCP `screenshot` with `presentation: true` captures the composed Studio image or
movie-only frame at its internal size; ordinary screenshots retain the 640x480
Studio canvas contract. Enlarged Studio checks require fine 3D and text detail
inside the viewport and verify that bitmap buttons remain nearest-neighbor
copies of their native artwork. Super-size checks also complete ordinary Studio
playback through both fixture scenes and their dissolve. Cursor tests cover scaled AND/XOR masks,
hotspots and clipping; a resized desktop editing run also verified text input,
the font menu, caret blinking and save.
The enlarged Studio checks passed at 2x and 4x; evidence is in
`tmp/shared-render/large-final-4x/run-wxfbtbsi/report.json` and
`tmp/shared-render/edit-ji9hi3yn/report.json`. Original-size playback also passed.

For complete real-movie coverage, run `tests/studio_complete_movie_workflow.py
EXE ISO MOVIE ARTIFACTS`. This uses ordinary paced Shift+Play with transitions,
audio processing and scrolling captions enabled; it does not use turbo playback,
which changes transitions to cuts. Authored click pauses are acknowledged only
when the view reports waiting for a click. The workflow requires every scene,
the final frame and restoration of the editor, and verifies that playback leaves
the movie copy unchanged. It uses a private movie copy and settings; `--desktop`
enables the real video driver. The default uses SDL dummy video/audio drivers.
The report records playback completion separately from application shutdown;
an assertion in the movie's close/save handling still fails the overall check.
The identified unused-sound prompt is answered with **Keep** by default;
`--unused-sounds remove` tests the other choice. Closing may rewrite the private
copy, matching retail; the original source movie must remain unchanged.
`--render-scale 4` runs the complete movie at the largest rendering size.

The super-size checks passed at 2x and 4x, including a real desktop 4x run.
A complete 4x `Litterbug 2.3mm` run rendered at 2176x1224 through all 134 scenes
(3,179 reported frames), processed audio and scrolling credits, restored Studio
and exited normally with an empty error stack. Playback preserved the test copy;
the original movie remained unchanged through shutdown. Headless modal dialogs
restore logical focus so SDL's dummy video driver resumes audio after Product
Info closes. Evidence: `tmp/supersize-full-movie/run-f5fc9fa6/report.json`.

SDL transition snapshots recover the displayed movie's palette colors exactly
from its RGB canvas. This avoids the unsupported general RGB-to-indexed copy
that previously raised `ercGfxCantDraw` during a dissolve. Colors absent from the
palette still produce an error; the conversion does not approximate colors or
skip transitions.

Resource-cache totals and purge scores use 64-bit accounting. Decoded SDL sound
samples can individually or collectively exceed the legacy 128 MiB allocation
limit while still in use. File/chunk limits and eviction priorities remain intact;
`resource-cache` checks retention and release beyond 2 GiB of reported resident
sizes without allocating that memory.

A Windows SDL Debug run of `Litterbug 2.3mm` completed ordinary paced movie-only
playback through all 134 scenes (3,179 reported frames), including dissolves,
audio processing and closing captions, then restored Studio with an empty error
stack. SDL dummy video/audio drivers were used. The original movie and test copy
were unchanged. The subsequent Quit failed the existing `FUnusedSndsUser`
assertion, `Not a child of Movie chunk`, recorded as a shutdown failure in that
run's report. The retail comparison below led to removing this Debug-only check.

A subsequent close test with the hash-pinned ISO retail EXE confirmed its normal
quit confirmation is followed by the unused-sound keep/remove prompt for this
movie. Both choices exited normally with code 0 and rewrote the disposable movie
copy. Both retained 220 MSND chunks, 210 WAVE chunks and 219 movie-root sound links;
the source movie and installed preferences remained unchanged. The Debug
root-link assertion and the user-facing unused-sound prompt check different
conditions; the assertion is absent from retail. Evidence is under
`tmp/retail-close/verified-close-summary.json` and its referenced run reports.
The SDL Debug close check now also reaches the prompt and exits with code 0 for
both choices, with assertions otherwise enabled. The reference-count scan,
prompt and sound cleanup remain unchanged; no missing links are added or repaired.

Native macOS checks in `build/mac-fullscreen-smoke/` verify a 1200x900 fitted
canvas on a 1440x900 display, a 640x480 canvas with integer scaling enabled,
and restoration of the 640x480 window. The SDL host test also checks rendered
pixels, pointer coordinates, both scaling modes and desktop-driven fullscreen
transitions with the dummy display backend.

The shared editing smoke script supports Mac/Linux, including `--fullscreen`
checks for both scaling modes and returning to a window. Its desktop
focus/minimize/resize checks remain Windows-specific:

```sh
python3 tests/studio_mcp_stdio_test.py build/studio-sdl/3dmovie-sdl "/path/to/Microsoft Kids/3D Movie Maker"
python3 tests/studio_sdl_smoke.py build/studio-sdl/3dmovie-sdl "/path/to/Microsoft Kids/3D Movie Maker" build/mac-smoke --portable-runtime --actor --portable-fonts --sdl-cursors --portfolio --portfolio-files --portable-pixels --text --playback
```

Integration fixes include explicit case-insensitive legacy content reads on
case-sensitive filesystems (exact matches win; ambiguous matches fail), preserving
live Filename objects on failed content probes, 32-bit FILL resource fields,
defined unsigned script-name/numeric packing, and safe packed BRender list unlinking.
Ordinary filesystem operations keep their native case behavior. Headless SDL
modal dialogs use the existing session loop without native dummy-driver modality.

Evidence is recorded in `build/studio-linux-{configure,build,default-build,install}.log`,
`build/studio-linux-app-final2-runtime.log`, `build/studio-linux-native-resources.json`,
`build/studio-final-windows-{build,tests,smoke}.log`, and
`build/windows-portable-integration/`. Linux application/runtime checks and native
resource comparisons are separate from macOS verification: the Windows host has
no complete Apple toolchain. The native Cocoa results below were collected on
the user's Mac. Earlier guarded-build checkpoints below are historical.

Native macOS arm64 verification on 4 November 2026 used AppleClang 17, the
`sdl-debug` preset and the ISO overlay recipe above. Build and install passed.
The Cocoa Studio workflow passed scene selection, actor placement, textbox
input/font selection, timeline play/restart/stop, edited movie save/reopen,
portfolio previews/overwrite decisions, F1 help and save/discard exit. PNGs,
saved fixture movies and an empty retained error stack are recorded in
`build/mac-smoke/`; the workflow log is `build/mac-smoke.log`.
MCP fragmented input, notifications, screenshot capture, quit and EOF shutdown
passed (`build/mac-mcp.log`). Eight focused CTest checks passed: global cleanup,
BRender lists, the resource compiler pipeline, string compatibility, platform
services, SDL host, dialogs and file dialogs. The real BRender actor render
check also passed. These runs do not establish microphone, MIDI or AVI playback
behavior on macOS.

Native fixes use forward slashes in resource includes, classic Mach-O
relocations for BRender's four-byte-packed pointer layouts, process-lifetime
allocator locks for global destructor cleanup, ImGui's configured shortcut
modifier for dialog text replacement, and four-byte font IDs in the font menu.
`global-cleanup-test` covers tracked allocation release after `main`; the editing
workflow covers the font picker and Command+A dialog regression. Startup may
include scanning macOS's full system font collection; the smoke test allows a
separate, configurable startup timeout.

Final Linux runtime checks passed original Studio initialization, PNG capture,
scene selection, movie save, quit with stdin still open, adjacent installed-data
discovery from an unrelated cwd, and EOF shutdown with the last request answered.
The retained error stack is empty. The packed-list, script packing/numeric and
case-sensitive filename regressions run in the same VM. All eight archives built
by native Linux `chomp` match the Windows tool's output exactly; the JSON report
records that resource phase separately from an earlier application cleanup test
that exposed the now-fixed BRender alignment bug. The cross test build uses
optimization plus undefined-behavior checks to keep emulated compression tractable.
The full Windows portable regression suite and original actor/text/timeline/
edited-save/reopen desktop workflow passed; focused checks also cover the later
packed-list fix. Native Windows GUI support is retained.

## Portable Windows runtime, 4 November 2026

`STUDIO_PORTABLE_RUNTIME=ON` selects the CPU/SDL GUI backend on Windows. It
excludes native window, GDI graphics/font, picture, dialog, menu and region
adapters, and compiled Windows UI resources from Studio. The native backend
remains available separately. Windows OS adapters still supply files, settings,
identity and font locations; these services have separate POSIX implementations.
The Debug MCP server now uses portable stdio and SDL tools, with optional native
Windows diagnostics. This verifies the shared GUI runtime intended for Mac/Linux,
not a complete Mac application. The full Mac/Linux build path is now enabled;
see the current checkpoint above for verification limits.

### Portable MCP checkpoint, 4 November 2026

The actual Debug server is included in `studio-runtime-compile-check` and
cross-compiles for Linux x64 and macOS arm64. `studio-mcp-io` supplies inherited
stdio with bounded reader waits and a lossless RGB PNG encoder. The SDL server
uses no GDI+ screenshot encoder. Input, window state and modal actions use the
existing SDL host, including opaque window tokens on platforms without HWNDs.
Timing uses the C++ steady clock; quit and EOF enqueue Studio's normal quit
command. Native Windows dialog inspection/dismissal and MSVC diagnostics stay
conditional. No network dependency was added.

`mcp-io-test` covers idle pipes, fragmented UTF-8/CRLF input, EOF, writes larger
than a pipe buffer and disconnected stdout (including POSIX SIGPIPE handling).
`mcp-png` independently decodes CRCs, zlib data, channel order and row order.
The real Studio protocol regression opens no movie and uses fresh processes:

```console
python tests/studio_mcp_stdio_test.py build/studio-portable-x64/3dmovie-sdl.exe "dist/Microsoft Kids/3D Movie Maker"
```

Windows passed fragmented JSON-RPC, large IDs, notifications, protocol errors,
screenshots, quit with stdin still open, and EOF with a final queued request.
The original editing workflow passed resize/fullscreen, actors, caption fonts,
timeline playback, dialogs and edited movie save/reopen. Linux ran the pipe
regression in the headless VM. Evidence is in
`build/mcp-{portable,native,linux,mac}-build.log`,
`build/mcp-{protocol,linux}-runtime.log`, `build/mcp-editing-final.log` and
`build/windows-mcp-portable-final/`. macOS execution and full non-Windows Studio
integration remain unverified. PNG output uses stored DEFLATE blocks and is
larger than the previous GDI+ encoding. See [the MCP code map](../code-map/mcp-server.md).

From an initialized x64 MSVC environment:

```console
cmake --preset x64:msvc:portable-sdl-debug
cmake --build build/studio-portable-x64 --target studio library-checks
ctest --test-dir build/studio-portable-x64 --output-on-failure
python tests/studio_sdl_smoke.py build/studio-portable-x64/3dmovie-sdl.exe "dist/Microsoft Kids/3D Movie Maker" build/windows-portable-smoke --portable-runtime --resize --fullscreen --actor --portable-fonts --sdl-cursors --portfolio --portfolio-files --portable-pixels --sdl-audio --sdl-midi --sdl-video --text --playback
```

The full desktop workflow passed actor placement, caption font changes,
timeline advancement/completion/restart/stop, WAVE/MIDI/AVI playback, dialogs,
overwrite decisions and edited-movie save/reopen. Its state reports zero retained
errors, native font operations, screen contexts, indexed drawing contexts/mirrors
and framebuffer contexts/mirrors. Portable runtime regressions also check picture
data preservation/unsupported operations and UTF-8 filename dialogs with native
path conversion, cancellation and overwrite decisions.
A separate desktop run passed recording, preview and WAVE import through the
original recording easel with synthetic SDL capture input; no room audio was
recorded. Its artifacts are in `build/windows-portable-recording-verified/`.

This exposed and fixed initial application focus/activation, the browser memory
query, movie read-only detection and uninitialized SDL repaint-damage bounds.
SDL reports installed RAM; unknown available RAM makes the old cache heuristic
conservatively release unused resources. `KAUAI_TEST_FAIL_FAST=1` makes automated
portable assertions exit with console diagnostics; interactive assertions retain
their dialog.

### Temporary picture stubs

Portable EMF/WMF/PICT import, recording and drawing are deferred. The portable
adapter returns null or reports `ercGfxCantDraw`, without assertions or changes
to destination pixels. Recording never returns a pretend successful picture.
Existing `META` envelopes, payload bytes and preferred display bounds still
round-trip unchanged through `PictureData` and the `Picture` wrapper. Native
Windows picture implementations remain available in the native backend.

All 15 installed `.chk`/`.3cn` archives were inspected and contain no `META`
chunks, and no direct picture import/record/draw calls were found in Studio or
the movie engine. The callers are the Kauai `ft` test app and `chomp`/`ched`
authoring tools. The stubs satisfy framework linkage while those optional
picture features remain unsupported. The `picture-portable` regression checks
byte preservation, explicit unsupported returns, and unchanged destination
pixels. The earlier experimental EMF interpreter, recorder and GDI fixtures
have been removed.

After this change, all 80 portable Windows CTests passed. The desktop regression
again passed actor/text editing, font changes, timeline play/restart/stop,
portfolios and edited-movie save/reopen, with an empty error stack. Evidence:
`build/picture-stubs-ctest.log`, `build/picture-stubs-smoke.log`, and
`build/windows-picture-stubs-smoke/`. The native Windows Studio build and
`pixel-port` regression also passed.

### Portable dialog resources

The original 15 Studio templates are embedded in `src/studio/dialog_resources.cpp`
and supplied through `KauaiDialogs::ResourceProvider`, with the original parser,
controls and callbacks. The portable executable loads neither Windows resources
nor `.rc` data. The native Windows regression compares the bundle against freshly
compiled resource output. After changing dialog sources, regenerate it with:

```console
python tools/embed-dialog-resources.py build/studio-sdl-x64/CMakeFiles/studio.dir/src/studio/utest.rc.res src/studio/dialog_resources.cpp
```

The two controller-test templates are bundled in
`tests/dialog_resources_portable.cpp` by the same tool. Headless Linux now links
and runs the actual picture, filename-dialog and font-list checks. Logs are
`build/picture-stubs-linux-build.log` and `build/picture-stubs-linux-runtime.log`; Windows
logs use `build/portable-*.log`, with desktop artifacts in
`build/windows-portable-final-verified/` for the earlier full media workflow.
The retained Windows SDL backend previously passed 94 CTests. The edited movie
has identical x86/x64 archive dumps and neither reader changes its input hash.
GitHub Actions remain disabled.

## Current priority and testing authorization, 4 November 2026

The user resumed work and explicitly directed the port to focus on Windows
with SDL. Linux/macOS integration is deferred; it is not the current completion
gate. Preserve the original Studio editor and verify real Windows SDL editing,
playback, dialogs and movie save/reopen workflows.

The user is away today and explicitly authorized use of their desktop for
testing. Desktop Studio smoke tests, window focus, resizing and fullscreen
checks are authorized for this testing session. Use fresh fixture movies and
test-owned processes. This authorization supersedes the earlier desktop-testing
restriction below for today; the older checkpoints remain historical evidence.

## Windows SDL verification checkpoint, 4 November 2026

The original Studio works through SDL3 on Windows in the tested workflows.
The current Debug x64 Studio and `library-checks` builds passed, followed by
all 93 Windows CTests (107 seconds). The full desktop workflow passed with
the normal Windows audio device: scene selection, actor placement, text entry
and font changes, WAVE/MIDI preview, original project AVI playback, help,
portfolio selection, overwrite decisions, movie save/reopen and normal exit.
A separate full run exercised the original recording easel with synthetic SDL
capture input; no room audio was recorded.

The desktop regression now also extends a scene to 31 frames through Studio's
original scrollbar commands, observes actual timeline advancement and natural
completion, and checks restart and early stop through the original Play button.
Stop completion must be observed after the normal event loop runs: nested MCP
`wait_ms` pumping omits the idle event required by the render alarm. The test
now polls normal-loop progress; no playback implementation change was needed.

The edited movie is saved and reopened after actor/text editing. Scene and actor
counts, frame bounds, caption text, and first/last character fonts match before
and after. The fixture retains Comic Sans MS text with a Consolas final character.
Both x86 and x64 archive readers produce identical dumps of that file without
changing its hash. This checks the actual edited fixture and stored data; it
does not claim an exhaustive comparison against every historical editor build.

Fullscreen used the existing 2560x1440 desktop, with integer presentation;
windowed resizing to a 984x661 client retained a centered 640x480 canvas.
A normal launch without `--mcp-server` also displayed the editor, resized with
black borders, and closed with exit code zero. The desktop capture was inspected.
Final Studio error-stack depth was zero, the test logs contained no diagnostics,
and all test-owned Studio processes exited.

Evidence under `build/inspector-deps/`:

- `windows-sdl-resume-build.log`, `windows-sdl-library-build.log` and
  `windows-sdl-regressions-20261104.log`.
- `windows-sdl-smoke-20261104.log`: full workflow including synthetic recording.
- `windows-sdl-editing-focused-20261104.log`: extended timeline and edited save/reopen.
- `windows-sdl-final-20261104.log`: combined workflow with normal audio output.
- `windows-edited-compat.json`: unchanged fixture hash and matching x86/x64 dumps.

Screenshots and JSON observations are in `build/windows-sdl-final-20261104/`;
normal-launch results are in `build/windows-sdl-normal-20261104/`.
The expanded diagnostic state reports timeline and caption values without
changing the movie. Re-run the combined desktop test with:

```console
python tests/studio_sdl_smoke.py build/studio-sdl-x64/3dmovie-sdl.exe "dist/Microsoft Kids/3D Movie Maker" build/windows-sdl-smoke --resize --fullscreen --actor --portable-fonts --sdl-cursors --portfolio --portfolio-files --portable-pixels --sdl-audio --sdl-midi --sdl-video --text --playback
```

Linux/macOS work remains deferred. Before the priority change, the unfinished
POSIX filename-dialog fixture was changed to use the project's file services
and compiled successfully; its runtime checks have not been run. The native
picture link gap remains specific to the deferred non-Windows integration.
GitHub Actions remain disabled.

## Desktop testing constraint and current handoff

### Paused commit checkpoint, 3 November 2026

Work is paused at the user's request. The commit preserves ongoing work;
it does not establish a complete non-Windows Studio port. Do not resume goal
work or desktop testing merely because this checkpoint has been committed.

Since the shared-UI checkpoint below, `STUDIO_SDL3=ON` can build runtime
libraries on POSIX with `BUILD_STUDIO=OFF`. The actual Linux `kauai` static
library compiles, including the resource compiler, fixed-width diagnostic
callbacks, POSIX SDL application initialization and the new document file
dialog adapter. Native MIDI source selection is restricted to Windows.
The document adapter uses the existing SDL file browser and includes an
overwrite confirmation; its runtime validation is still outstanding.

The `font-list-test` executable link still fails on native-picture methods
(`Picture` lifecycle/read/write and `GraphicsPort::DrawPic`). The full
non-Windows Studio build remains guarded. The new optional
`filename-dialog-sdl-test` also fails to compile in the current Linux cross
toolchain because the required `std::filesystem` operations are unavailable.
Fix that fixture before running its headless open/cancel/save/overwrite cases.
The most recent changes have not been revalidated on Windows or macOS;
successful results below describe their earlier checkpoints.

Evidence: `build/inspector-deps/gui-library-linux-build.log`,
`gui-link-linux-build.log`, and `filename-dialog-linux-build.log`.
All of these checks were console-only; no desktop windows were opened.

The user paused work because automated Studio tests were repeatedly taking
focus. Do not run desktop smoke tests, GUI CTests, window activation, resizing
or fullscreen checks unless the user explicitly requests a desktop test.
Use builds and headless tests for routine verification.

The four-bit text implementation and targeted SDL/native checks below passed.
The new packed coverage fixtures also executed successfully on Linux:
`build/port-cross/linux-packed-text-runtime.log` contains pixel-canvas status 0
and `PIXELS_LINUX_OK`. macOS verification remains cross-compilation only.
The full application smoke attempt failed at music preview while Studio lacked
focus (`build/inspector-deps/packed-text-smoke.log`). Its retry was stopped at
the user's request; neither attempt establishes an application pass for this
change. Do not retry it automatically. The earlier monochrome-solid full-suite
results remain valid for that earlier checkpoint. The user reactivated the goal;
continue with builds and headless checks under the desktop testing constraint.

## Shared UI runtime compilation update for 3 November 2026

The SDL runtime compilation gate now covers clipboard/command handling,
documents and controls, Kidspace objects/world/help, rich text and text editing,
and graphics script execution. Together with the editor and playback gates,
these real sources compile for Linux and macOS. This remains compilation
coverage, not a complete non-Windows application link or startup pass.

Graphics script overrides now match the core's 32-bit virtual interfaces;
script argument buffers and output values use that same contract. Rich-text
property entries, font argument headers/reference counts, help-topic records,
object locations and cursor maps retain their original fixed-width layouts.
These changes preserve the existing resource/movie formats rather than
introducing a new format.

The actual Kidspace descriptor reader now belongs to `kauai-core` through
`kidspace_descriptor.h/.cpp`, without GUI or SDK imports. Its regression builds
the original little- and big-endian bytes independently of the C++ structures,
then checks parent/default locations, signed coordinates, cursor IDs and the
high-bit state mask through the real resource cache reader. It passes on native
x86 Windows, SDL x64 Windows and headless Linux. macOS is cross-compiled only.

Both Windows Studio builds passed, along with `studio-layout`,
`kidspace-descriptor` and `movie-save-load` in each configuration. No desktop
windows were opened or manipulated. Evidence:
`build/inspector-deps/gui-runtime-{linux,mac,sdl,native}-build.log`,
`gui-runtime-{sdl,native}-tests.log`, and
`build/port-cross/linux-kidspace-descriptor-runtime.log` (status zero and
`PIXELS_LINUX_OK`). Caption save/reopen coverage currently checks plain text;
explicit styled-caption/font-argument round trips remain a useful follow-up.

Next milestone: link the complete shared GUI runtime and then start the
original Studio headlessly with its real resources. Remaining integration
includes document file-dialog implementations, native picture fallback,
platform-specific MIDI/diagnostic source selection and resource generation.
Auxiliary native document controls currently report unsupported on POSIX;
Studio's own scripted controls remain in the shared UI. Keep the non-Windows
Studio guard until the complete application is verified. Desktop testing
remains opt-in.

## Playback runtime compilation update for 3 November 2026

`engine-runtime-compile-check` compiles the same playback source list used by
`engine`, plus the SDL recorder and BRender `World` adapter. Actor editing,
save/load, sounds, backgrounds, movies, scenes and text boxes now compile for
Linux and macOS with `KAUAI_SDL3`. This target is included in `core-checks` and
does not substitute stubs for runtime implementations.

Output arguments and callbacks now match the fixed-width core interfaces:
actor action flags/cel counts, step resolution, lifetime calculation, background
cache reads, roll-call loading and sound timeouts. Framework navigation key
constants preserve their existing values without requiring Windows `VK_*`
macros. Numeric formatting uses the formatter's 32-bit argument contract.

The audit also fixed scene-event reads that compiled but were unsafe on LP64.
Pause playback now reads `SceneEventPause`'s existing two 32-bit fields; camera
playback reads the existing 32-bit camera word. Calls that supplied temporary
native `long` camera values now pass 32-bit values consistently. The stored
scene/movie format is unchanged.

Both Windows Studio builds passed. The console `movie-save-load` regression
passed in native x86 and SDL x64, including real pause-event replay with durations
1 and `0x11223344`, actor track/seek behavior, sound queue dispatch, playback
startup/alarm handling, caption persistence and movie save/close/reopen. These
tests create no desktop window; caption checks borrow only a GDI context.
Linux/macOS verification of the full playback implementation is compilation
only until the remaining GUI runtime can link there.

Evidence: `build/inspector-deps/engine-runtime-{linux,mac,sdl,native}-build.log`,
`engine-runtime-linux-final.log`, and `engine-runtime-{sdl,native}-tests.log`.

Next: finish compilation/linking of the shared Kauai GUI runtime, including
script execution, rich text and document/control services. Then connect native
picture fallbacks, resource generation and platform-specific diagnostics to the
complete application. The non-Windows Studio guard remains; no full POSIX Studio
startup has been demonstrated. Keep desktop testing opt-in.

## Studio editor compilation update for 3 November 2026

`studio-runtime-compile-check` now covers the original actor preview, browsers,
easels, popups, portfolio/SDL portfolio adapter, scene sorter, Splot machine,
scrollbars, Studio controller, theater and text graphics objects, in addition
to startup and the graphics script compiler. These sources compile for Linux
and macOS. Windows codec installation and the native debug MCP server are
explicitly outside this target; this is not a complete application link.

Editor calls now match the core's fixed-width output arguments and sound-reader
callback. Browser selection output is typed as a 32-bit value, avoiding partial
writes into LP64 `long` variables. Thumbnail sorting/lookup reads one signed
32-bit ID, preserving the native comparison behavior. Font-menu extras, action
table checks, cache-index list storage and numeric formatting use the same
32-bit contracts. Debug high-resolution export retains the Windows Desktop
path; its POSIX path uses the current directory and reports the resolved name.

Studio's resource string-table reader is shared through `studio_resources.h`
and `resource_tables.cpp`, with no GUI/SDK dependency. It accepts strings alone
or one 32-bit extra value, converts extras from the stored byte order, and
rejects other widths. The general-purpose GST reader/writer and movie format
are unchanged. `studio-resources-test` exercises the actual Studio reader with
native and swapped tables, strings, positive/negative/high-bit values, adjacent
memory preservation, and rejection of eight-byte extras.

Native x86 and SDL x64 Windows Studio builds passed. Both Windows configurations
passed `studio-layout` and `studio-resources`; headless Linux ran both with
status zero and `PIXELS_LINUX_OK`. macOS compiled the expanded editor target and
resource test. No desktop windows were opened or manipulated.

Evidence: `build/inspector-deps/studio-full-{linux,mac,sdl,native}-build.log`,
`studio-resources-{linux,mac,sdl,native}-build.log`,
`studio-full-{sdl,native}-tests.log`,
`studio-editor-{linux,mac,sdl,native}-final.log`, and
`build/port-cross/linux-studio-resources-runtime.log`.

Next: compile and integrate the remaining playback/GUI runtime, resolve native
picture fallbacks, resource generation and Windows-only MCP diagnostics, then
link the complete application. The non-Windows Studio guard remains in place.
Headless startup must load the real Studio UI/resources before a runtime pass
can be claimed; desktop testing still requires an explicit request.

## Studio startup compilation update for 3 November 2026

`studio-runtime-compile-check` now compiles Studio's actual `utest.cpp` startup
and the graphics script compiler on Linux and macOS. SDL window creation is
shared across platforms; Windows keeps native icons, structured exception
handling and native display/device prerequisites. SDL uses its own audio/video
backends and fixed logical canvas. Callback arguments and script compiler
overrides now use the existing portable or fixed-width types where required.
This is compilation evidence, not a complete Studio link or startup pass.

The portability check exposed a thumbnail compatibility hazard: `ulong` is
64-bit on LP64 platforms. `BrowserThumbEntry` now explicitly retains its original
12-byte layout, and the in-memory `ThumbnailDescriptor` aliases derive their
offsets from `TAG` rather than Windows-specific padding. `studio-layout-test`
checks original record bytes and verifies that font/child aliases preserve the
resource pointer and source ID. No new serialized format was introduced.

Both Windows Studio builds and their layout tests passed. Linux executed the
layout test with status zero and `PIXELS_LINUX_OK`; macOS compiled both new
targets. No desktop windows were opened or manipulated.

Evidence: `build/inspector-deps/studio-runtime-sdl-build.log`,
`studio-runtime-native-build.log`, `studio-runtime-sdl-tests.log`,
`studio-runtime-native-tests.log`, `studio-runtime-linux-build.log`,
`studio-runtime-mac-build.log`, and
`build/port-cross/linux-studio-layout-runtime.log`.

Next milestone: link and start the complete original Studio on a non-Windows
SDL host. Expand compilation to the remaining Studio and engine sources, finish
application callbacks and native-picture/graphics fallbacks, and connect the
portable resource/content build pipeline. Keep the non-Windows `STUDIO_SDL3`
guard until those requirements are satisfied. Then use dummy SDL video/audio to
verify startup, original UI/resource loading and clean shutdown. Movie loading,
editing and save/reopen compatibility follow as integration checks. Desktop
testing remains opt-in under the constraint above.

## SDL root-window attachment update for 3 November 2026

`GraphicsPort::PgptNewSDLWindow` is now the shared root-surface factory. It accepts
only the active SDL window token, allocates a logical-size 32-bit CPU port and
binds its pixels to the host. Both the Windows SDL adapter and POSIX
`PgptNewHwnd` use it. The public runtime window argument uses the existing opaque
`CoreNative::WindowHandle`; a POSIX SDL window needs no Windows device context.

`gob_window.cpp` shares screen-object creation, attachment and window lookup.
The root-window provider is platform-specific, with SDL tokens preferred when
the host is active. Screen initialization now respects `fgobEnsureHwnd` and
releases a newly created root if attachment fails. `gob_sdl.cpp` supplies POSIX
invalidation/validation, mouse mapping, damage cleanup, title and activation
operations. The single-window Studio host has no MDI client; native MDI creation
explicitly fails rather than pretending to create a window.

SDL title conversion, window creation and closure moved from `appbwin.cpp` to
`appbsdl.cpp`. Small Windows hooks retain the system double-click interval and
optional GDI font context lease. The POSIX hooks use SDL ownership directly.
These actual shared application/graphics-object sources compile on Linux and
macOS; this still does not prove the full application links or starts there.

The opt-in `pixel-port-test --sdl-window` links the real POSIX graphics port and
refuses to create a window unless `SDL_VIDEODRIVER=dummy`. On headless Linux it
verified null/foreign/closed-token rejection, exact visible framebuffer bytes,
presentation, integer scaling after a virtual resize, fixed 640x480 logical
dimensions, replacement-port ownership and both host-first and port-first
shutdown. Closing/reopening the host and releasing an old port leaves the new
surface bound. Pixel-buffer counts and bytes return to baseline. The POSIX CTest
entry `pixel-port-window` sets dummy video/audio and software rendering explicitly;
Windows routine checks continue to use the window-free default test mode.

Both Windows Studio builds passed. The default Windows port regression suite
passed, including all text/transfer checks. Linux executed the SDL attachment
test with status zero and `PIXELS_LINUX_OK`; macOS passed the expanded runtime
compilation gate. No desktop windows were opened or manipulated.

Evidence: `build/inspector-deps/window-port-sdl-build.log`,
`window-port-native-build.log`, `window-port-sdl-tests.log`,
`window-port-linux-build.log`, `window-port-mac-build.log`, and
`build/port-cross/linux-window-port-runtime.log`.

Next: finish Studio-specific OS startup, remaining application callbacks and
native-picture/fallback requirements, then link and exercise the complete
application using a headless SDL driver. The POSIX Studio build guard remains;
this checkpoint verifies the root graphics-port/host connection, not a full
graphics-object/application runtime launch.

## Portable text runtime update for 3 November 2026

The POSIX `GraphicsPort` now measures and draws counted Studio text through the
existing FreeType font host. `gfx_text.cpp` contains the shared font cache,
measurement/layout, rasterization, packed coverage, background and boxed-text
paths previously embedded in `gfxwin.cpp`. Windows retains its native font
selection adapter and existing fallback. Native color references and monochrome
background updates use the same adapter hooks as the shared shape/bitmap paths.
POSIX destroys its cached font face with the port and reports failed measurement
or drawing through the existing graphics errors.

Shared font-list lifetime, name lookup and font-description validation moved to
`fontlist.cpp`. POSIX supplies portable initialization and fixed-pitch queries.
Color/pattern value operations and the original gray patterns moved to
`gfx_values.cpp`, so boxed text does not pull the entire graphics environment
and application into the port test. The same files remain linked by native
Windows Studio. Stored strings and movie serialization have not changed.

With FreeType enabled, `pixel-port-test` now initializes the real font list and
runs 1,296 exact-pixel text cases: six formats, two heights, three styles, every
horizontal/vertical alignment, and transparent/opaque/inverted backgrounds.
Independent placement arithmetic distinguishes the legacy logical bounds from
the one-pixel-left drawing origin of odd-width centered runs. Expected packed
bytes use font masks or provider color output, with clipping, palette selection,
packed conversion and row padding checked separately from port dispatch.
Additional cases verify boxed text, empty strings, failure reporting and unchanged
pixels after rejected input. Font statistics prove cache reuse and no native
font creation, bootstrap, drawing, measurement or enumeration in this test.

Both Windows Studio builds and the three selected offscreen tests (`pixel-port`,
`font-list`, `font-gdi`) passed. The macOS runtime compilation gate passed.
Linux linked the same backend with FreeType and executed the complete port test
under QEMU: all 1,296 text cases passed, the executable exited with status zero,
and the harness reported `PIXELS_LINUX_OK`. Its isolated font directory contains
the local Arial test fixture; the test exercises actual file-backed font discovery
and rasterization. macOS remains compilation-only. No desktop window was opened.

Evidence: `build/inspector-deps/pixel-text-sdl-build.log`,
`pixel-text-native-build.log`, `pixel-text-sdl-tests.log`,
`pixel-text-linux-build.log`, `pixel-text-linux-configure.log`,
`pixel-text-mac-build.log`, and `build/port-cross/linux-pixel-text-runtime.log`.

Remaining drawing limitations include mixed monochrome patterned frames (including
boxed text), the existing near-size true-color-to-8-bit conversion policy and
native-picture rendering. Next: connect root SDL window attachment and application
initialization to this backend, resolving required fallbacks as the complete
Studio links. The full POSIX Studio build guard remains; no complete POSIX Studio
launch is claimed.

## Portable bitmap transfers update for 3 November 2026

The actual POSIX `GraphicsPort` now implements `CopyPixels`, `ScrollRcs` and
`DrawMbmp`. Their CPU dispatch paths moved from `gfxwin.cpp` into the shared
`gfx_pixels.cpp`; Windows SDL uses those same paths before its native fallback.
Adapter hooks supply private-palette ownership and the legacy monochrome
background color. The indexed double-stretch fast paths, same-size raw-index
artwork behavior, palette translation and masked shrink policy are retained.
This changes graphics implementation only, not `.3mm` storage or serialization.

The original `DoubleStretch`/`DoubleVertStretch` routines moved from the GUI
graphics source into `kauai-core` as `pixel_stretch.cpp`, with an SDK-free header.
The new integration test exposed a bug in the old vertical-stretch C fallback:
its second-row copy offset the source and overwrote the first destination row.
It now copies the same source into the second destination row, matching the
existing x86 assembly behavior. The test checks both vertical-only and full 2x
stretching, including clipped regions with holes.

`pixel-port-test` exercises the real portable entry points in all six pixel
formats. Independent packed-byte expectations cover scaled and overlapping
copies, clipped vertical scrolling, masked sprites and solid masks at original
and doubled sizes, nonzero port/sprite origins, transparent background
preservation and row padding. Additional fixtures cover private-palette color
conversion and the legacy distinction between raw-index same-size artwork and
palette-translated scaled artwork. Existing offscreen GDI comparisons continue
to cover broader copy, scroll and masked-transfer cases.

Both Windows Studio builds passed. All five selected headless/offscreen tests
passed: `pixel-port`, `pixel-canvas-gdi`, `masked-transfer-gdi`,
`indexed-copy-gdi` and `packed-copy-gdi`. Linux linked and executed the actual
portable backend successfully under QEMU. macOS compiled the core and portable
runtime gate; no macOS runtime result is claimed. No desktop windows were opened.

Evidence: `build/inspector-deps/pixel-transfer-sdl-build.log`,
`pixel-transfer-native-build.log`, `pixel-transfer-sdl-tests.log`,
`pixel-transfer-port-tests.log`, `pixel-transfer-linux-build.log`,
`pixel-transfer-linux-test-build.log`, `pixel-transfer-mac-build.log`, and
`build/port-cross/linux-pixel-transfer-runtime.log`.

Known gap: near-size true-color-to-8-bit copies still require the native
quantization policy. The portable entry point explicitly reports
`ercGfxCantDraw` without modifying the destination; the test verifies this.
Next: port text drawing/measurement through the existing font services, resolve
remaining required drawing fallbacks, then attach the root SDL window and
application startup. Native-picture drawing is still missing. Full POSIX Studio
is not yet linked or launched; its CMake guard remains in place.

## POSIX pixel-port runtime update for 3 November 2026

`gfx_posix.cpp` now implements the real CPU-owned `GraphicsPort` lifecycle,
palette state, pixel access/locking, shape dispatch and native-color-reference
hooks. Its rectangle, highlight, oval, polygon, line and video paths use the
same `gfx_pixels.cpp`/pixel-canvas code as Windows SDL. Pixel buffers use the
existing checked allocator for 1/4/8/16/24/32-bit layouts and four-byte row
alignment. Port destruction unbinds any host presentation buffer before freeing
storage and releases its clip region. CPU writes are synchronous, so flushing
requires no OS drawing operation.

Active colors use the same `PaletteState` policy as Windows SDL, including
reserved identity colors and animation behavior. Existing ports lazily update
their application palette; a private palette is retained until explicitly reset.
Four/eight-bit creation currently requires an installed application palette: the
POSIX backend cannot borrow a host device palette as the Windows adapter does.
Failed creation returns null. Unsupported CPU drawing reports `ercGfxCantDraw`
instead of claiming a successful native fallback. This restriction and remaining
unsupported drawing cases must be resolved where required by full Studio startup.

Port RTTI, region ownership/base mapping, memory marking and the palette
realization counter moved to `gfx_port.cpp`, allowing the actual port to link
without the application/window loop. The original six-point line construction
also lives there and is shared with the native GDI path. The high-level
`GraphicsEnvironment::DrawVideoFrame` wrapper moved into `gfx.cpp`, keeping the
low-level port independent of graphics-object lookup.

`pixel-port-test` links the actual POSIX backend, including on Windows for a
headless native-host check. It verifies exact packed pixel bytes and row padding
for all six depths, palette ownership/reset, palette animation no-op behavior,
clip-region ownership and nonzero port origins, line pixels, reversible
highlighting, oval/polygon dispatch, video writes and buffer counts after release.
Diagnostic failures print and exit directly, without crash-reporting UI. It is
included in `core-checks` when the SDL host target is available.

Both Windows Studio builds passed. The CPU-port and native packed-shape GDI
checks passed (2/2); the expanded CPU-port test also passed. The complete new
backend compiled for Linux and macOS. Headless Linux QEMU executed the expanded
CPU-port test successfully: status zero and `PIXELS_LINUX_OK`. This is actual
port runtime evidence, not just an object-file check. It remains narrower than
a complete Studio launch. No desktop windows were created; macOS runtime has not
been tested.

Evidence under `build/inspector-deps`: `pixel-port-sdl-build.log`,
`pixel-port-native-build.log`, `pixel-port-sdl-tests.log`,
`pixel-port-expanded-tests.log`, `pixel-port-sdl-tests-build.log`,
`pixel-port-linux-build.log`, `pixel-port-linux-tests-build.log`,
`pixel-port-linux-compile.log`, `pixel-port-mac-build.log`. Headless Linux logs:
`build/port-cross/linux-pixel-port-runtime.log` and
`build/port-cross/linux-pixel-port-expanded-runtime.log` (the latter includes the
expanded shape-dispatch checks).

Next: implement the remaining port entry points using the existing shared
algorithms, especially bitmap copies, masked artwork, scrolling and text. The
POSIX backend does not yet define these operations or native-picture support;
do not substitute silent success stubs. Then connect root-window attachment and
application initialization to this backend. Full POSIX Studio still cannot be
linked/launched, and its CMake guard remains in place.

## Shared graphics runtime update for 3 November 2026

The shared graphics-object and graphics-environment sources now compile on
Linux and macOS. `gob.cpp` uses opaque window handles in common code, and measures
SDL windows in their logical canvas dimensions. The native `GetClientRect`
fallback is Windows-only; a failed or unknown window measurement reports a bug
and retains the prior layout instead of consuming an uninitialized rectangle.
The graphics-port clip-dirty flag now belongs to the common port state because
`ClipToRegn` changes it on every backend.

Graphics assertion signatures now match the core's fixed-width ABI. Font lookup
uses a `LegacyLong` temporary at the string-table boundary and copies the found
or insertion index into the public runtime `long` output. Portable font-list
construction also uses fixed-width string-table indices. Windows retains its
existing type identities and lookup behavior.

The existing CPU shape/video implementation has moved from `gfxwin.cpp` into
`gfx_pixels.cpp`, selected for SDL builds. This shared source contains indexed
and true-color brush selection, rectangles, ovals, polygons, monochrome brush
reuse state and DIB video-frame drawing. It uses the existing pixel canvas
algorithms. Native synchronization, palette activation and color-reference
queries remain adapter operations. Windows implements the new color hooks with
the original GDI operations; packed background color conversion retains the
same BGR/reference encoding. Shared palette/pixel storage fields now live outside
the Windows-only class section, with 32-bit pitch/depth values.

`sdl-runtime-compile-check` now additionally compiles `gob.cpp`, `gfx.cpp`,
`gfx_pixels.cpp` and `fontlist_portable.cpp`. All nine real application/runtime
translation units and the header/ABI check compile on Linux and macOS. This
still does not link or start a complete POSIX Studio: the POSIX graphics-port
constructor, palette/synchronization/color hooks and remaining backend entry
points must be implemented and connected. Text, bitmap transfers and several
native fallbacks still live in `gfxwin.cpp`; extract/reuse those when building
the portable backend rather than creating a divergent renderer.

Both Windows Studio builds passed. The SDL offscreen `packed-shape-gdi`,
`pixel-canvas-gdi` and `pixel-canvas-indexed-gdi` checks passed (3/3), exercising
the moved code against native drawing. `font-list` also passed after the
string-table boundary changes. These tests use offscreen buffers or font files;
no desktop window was created, activated or resized. Linux/macOS results here
are compilation evidence; no new complete application runtime result is claimed.

Evidence under `build/inspector-deps`: `graphics-runtime-sdl-build.log`,
`graphics-runtime-native-build.log`, `graphics-runtime-sdl-tests.log`,
`graphics-runtime-font-sdl-build.log`, `graphics-runtime-font-native-build.log`,
`graphics-runtime-font-tests.log`, `graphics-runtime-linux-build.log` and
`graphics-runtime-mac-build.log`. The final font follow-up builds include the
latest shared lookup changes. The full POSIX Studio CMake guard remains in place.

## Shared SDL application runtime update for 3 November 2026

The new `sdl-runtime-compile-check` target compiles actual application sources
against the portable core on Linux and macOS: `appb.cpp`, `appbsdl.cpp`,
`sdl_studio.cpp`, `pic.cpp` and `cursor.cpp`, plus a shared-GUI header/ABI check.
It is part of `core-checks`. This is an object compilation gate, not a linked
Studio executable or proof that the complete application starts.

The first direct Linux probe could not get past `frame.h`: it exposed native
region/control handles, Windows clipboard macros and the optional legacy CSAPI
spell-checker header. Native region helpers are now Windows-only, controls use
an opaque `CoreNative::WindowHandle`, and clipboard format IDs preserve their
original numeric values with a Windows SDK static assertion. Optional Windows
spell-checker/AudioMan/legacy MIDI declarations no longer enter the POSIX GUI
umbrella. The Windows-only spell-checker and legacy MIDI implementation sources
are selected only on Windows; the portable MIDI stream backend remains shared.
Startup wave-format IDs moved unchanged into `legacy_wave_formats.h` so shared
application startup does not need the AudioMan class declaration.

Picture/cursor cache callbacks and picture byte counts now use `LegacyLong`,
matching the core's 32-bit contract on LP64. The application command-hook and
assertion signatures now match the core, and shared application code uses the
opaque window-handle type. Windows types retain their previous ABI; native
picture payload bytes and cursor formats are unchanged.

SDL paint dispatch moved from `sdl_studio_win.*` to `sdl_studio.*` and no longer
reads the Windows `vwig` global. `Host::WindowToken()` supplies the window identity:
it is the existing HWND when available, or an opaque SDL window token on POSIX
and dummy drivers. `NativeWindow()` retains its native-interoperability meaning.
`IsWindow()` validates the portable identity while the host is active. Painting
uses that identity to locate the original Kauai graphics object and dispatch
its existing update path; it does not replace Studio's UI or renderer.

Both Windows Studio executables and the compile gate built successfully. The
existing picture-data/cursor-data tests passed (2/2 in each Windows build).
Linux and macOS compiled the actual shared application sources listed above.
Headless QEMU Linux ran picture-data, cursor-data and the real SDL window-services
test with the dummy driver: all three returned zero and `PIXELS_LINUX_OK`.
The latter now checks null/foreign/active window identities in addition to the
existing title, integer-scaling/letterboxing, coordinate mapping and restart checks.
No desktop test or Studio launch ran, and macOS runtime remains unverified.

Evidence under `build/inspector-deps`: `runtime-sdl-build.log`,
`runtime-native-build.log`, `runtime-sdl-tests.log`, `runtime-native-tests.log`,
`runtime-linux-build.log`, `runtime-linux-host-build.log`,
`runtime-mac-build.log`. Runtime evidence:
`build/port-cross/linux-runtime-interface.log`.

The next concrete compile blockers were probed separately and remain unfixed:

- `gob.cpp` still uses native `HWND` spelling in shared implementations. Its
  core callback/word sizes also need auditing as compilation progresses.
- `gfx.cpp` has LP64 assertion/font-output signature mismatches, and accesses
  `_fNewClip` even though its declaration belongs to the native port. The
  graphics-port backing state and platform adapter still need a complete POSIX
  implementation using the existing CPU canvas services.
- Window/root attachment, application native initialization and resource build
  tooling must then be connected for a full POSIX Studio build/startup attempt.

The direct failure evidence is `studio-linux-gob-probe.log` and
`studio-linux-graphics-probe.log` under `build/inspector-deps`. The CMake guard
rejecting a full non-Windows STUDIO_SDL3 build remains intentional; do not treat
this successful compilation subset as completion of the port.

## Installed-product discovery update for 3 November 2026

`studio-content` is an independent application library with an SDK-free
`inc/studio_content.h` interface. Studio now reads installed titles through it,
then retains its existing source-ID parsing, TagManager merge and product-directory
selection. Its in-memory source table uses explicit `LegacyLong` entries, keeping
IDs four bytes on POSIX as well as Windows. Movie/chunk serialization is unchanged.

Windows still reads the original HKLM
`Software\Microsoft\Microsoft Kids\3D Movie Maker\Products` key using
`KEY_READ | KEY_WOW64_32KEY`. A missing key still fails startup; an existing empty
catalog still uses the original `1 -> 3D Movie Maker/3DMovie` fallback. The adapter
closes the key, resets buffer sizes for every entry, skips oversized/non-string
values, and returns other enumeration errors without exposing partial results.
Unterminated REG_SZ data is bounded before conversion. These checks follow the
[RegEnumValue contract](https://learn.microsoft.com/en-us/windows/win32/api/winreg/nf-winreg-regenumvaluea);
valid installed title bytes and enumeration order are retained.

SDL startup now accepts `THREEDMM_CONTENT_ROOT` as an absolute native path of at
most 255 code units. It names the parent of product directories, for example
`/opt/3dmm/Microsoft Kids`, not the `3D Movie Maker` directory itself. An explicit
root must contain the selected product's `BLDGDATA.CHK`; an invalid override
fails instead of silently finding another install. With no override, existing
cwd/executable searches remain. The Program Files probes now compile only on
Windows. Native non-SDL Studio does not use this override.

On POSIX, product registration comes from `<content-root>/products.tsv`:

```text
2	3D Movie Maker/3DMovie
3	N3DMM EXPANSION/N3DMMExp
4	Doraemon Character Kit/DORAEMON
5	Expansions/EXPANSIONS
```

The separator is one literal tab. This example was read from this machine's
32-bit Windows Products key, not inferred from directory order. **When migrating
an installation, copy its exact source IDs and titles. Do not renumber them.**
A different installation can have different registrations. The reader preserves
the supplied IDs; it does not assign IDs by scanning folders. An absent or empty
manifest retains the legacy base-only SID 1 fallback, which is not a replacement
for the registration list when copying an existing installation like this one.

The manifest accepts LF/CRLF, blank lines and lines beginning with `#`. Fields
retain their native bytes and are limited to 255 bytes each; no text transcoding,
quoting or escaping occurs. Malformed rows, embedded NULs, nonregular files and
files larger than 1 MiB fail without returning a partial catalog. The root must
exist; only an absent optional manifest triggers the base fallback. Read errors
are reported. A FIFO is opened nonblocking and rejected rather than hanging.
The source-ID conversion and treatment of syntactically invalid IDs still happen
in Studio's existing `String::FGetLw` path.

Validation: both Windows Studio builds and their `studio-content` tests passed,
including isolated disposable HKCU fixtures (no writes to installed products).
A separate UTF-16 build/test passed. The same component and tests cross-compiled
for Linux and macOS; headless Linux QEMU ran the actual manifest/environment
checks successfully (`status 0`, `PIXELS_LINUX_OK`). macOS runtime and complete
Studio startup are still unverified. No Studio window or desktop test ran.
Evidence under `build/inspector-deps`: `content-sdl-build.log`,
`content-native-build.log`, `content-sdl-tests.log`, `content-native-tests.log`,
`content-unicode-build.log`, `content-unicode-tests.log`,
`content-linux-build.log`, `content-mac-build.log` and the corresponding
`content-*-build-verify.log` files. PowerShell reported the initial cross-builds'
CMake developer warnings as errors despite successful linking; the verification
commands exited zero. Linux runtime evidence is
`build/port-cross/linux-content-runtime.log`.

Next: establish the remaining blockers to compiling and starting the complete
SDL Studio on POSIX. CMake still deliberately rejects non-Windows STUDIO_SDL3;
the portable service tests do not prove the application is ready. Inspect its
application/graphics dependencies before removing that guard. The remaining
`LoadGenResource` call in `utest.cpp` supplies the About dialog's product ID,
not the installed product catalog; prioritize actual startup/build blockers.

## User identity and profile-directory update for 3 November 2026

Studio now gets its username and writable profile fallback through the SDK-free
`platform_users.h` interface. Windows retains `WNetGetUser` and the extended
provider-error information used by the existing localized alert path. Other
lookup failures still select Studio's localized default username. POSIX uses
`getpwuid_r` for the effective uid, retries account records larger than the
initial buffer, and ignores USER/LOGNAME overrides. Names beyond Studio's
255-code-unit limit are unavailable rather than silently truncated.

Profile lookup order is unchanged: a valid saved HomeDirectory wins; otherwise
Studio tries its installed Users/profile tree before the writable fallback:

- Windows: `%LOCALAPPDATA%\Microsoft Kids\Users\<username>`, exactly as before.
- Linux: `$XDG_DATA_HOME/3dmmforever/Users/<username>`, or
  `$HOME/.local/share/3dmmforever/Users/<username>` when XDG_DATA_HOME is empty,
  unset or relative. These follow the [XDG data-directory rules](https://specifications.freedesktop.org/basedir/0.8/).
- macOS: `$HOME/Library/Application Support/3DMMForever/Users/<username>`.

`KauaiFiles::MakeDirectories` and `ChangeDirectory` now own parent creation and
process working-directory changes. Existing directories are accepted, files in
the path fail, and failed directory changes retain the previous cwd. The
Windows adapter normalizes forward slashes before calling the shell helper;
a direct native probe confirmed that the helper otherwise fails on mixed
separators. Its null owner prevents any UI, as documented for
[SHCreateDirectoryEx](https://learn.microsoft.com/en-us/windows/win32/api/shlobj_core/nf-shlobj_core-shcreatedirectoryexa).
The POSIX preference backend now shares this directory-creation service.

The application no longer calls WNet, shell-folder or SetCurrentDirectory APIs
directly for these operations. It retains the username error/default policy
and HomeDirectory preference. HomeDirectory writes now count UTF-16 bytes
correctly; ANSI bytes are unchanged. Failure-path messages use bounded buffers,
including enough space for a full legacy username.

Both Studio builds succeeded and four targeted tests passed in each:
file-services, user-services, settings-store and settings-store-win. Independent
UTF-16 builds of the user/file services also passed. The standalone
`tests/platform` project built and passed its file/user tests without the
Studio UI. Linux ran all four corresponding file/user/preference tests under
headless QEMU. Its passwd fixture has a record larger than 2 KiB and explicitly
requires the expected account name, exercising the account-buffer retry path.
macOS cross-compilation passed; no macOS runtime result is claimed. The rare
Windows provider-error branches were preserved by inspection, not fault-injected.
No desktop application was launched. Evidence:
`build/inspector-deps/users-sdl-build.log`, `users-native-build.log`,
`users-sdl-tests.log`, `users-native-tests.log`, `users-linux-build.log`,
`users-mac-build.log`, `users-unicode-tests.log`,
`users-unicode-file-build.log`, `users-standalone-build.log`,
`users-standalone-tests.log`, `users-path-oracle.json` in the same directory,
and `build/port-cross/linux-users-runtime.log`. The initial standalone UTF-16
file link lacked user32; the recorded follow-up link and runtime passed.

Full POSIX Studio startup remains unverified and incomplete. Installed-product
discovery, native resources, diagnostics and remaining application/drawing code
still need platform boundaries; the installed sample/content directories are
still required by the original Studio startup policy.

## POSIX preference persistence update for 3 November 2026

`settings_store_posix.cpp` now supplies the real non-Windows preference backend,
and Studio selects it through the same `KauaiSettings::Access` policy used by
the Windows adapter. The old non-Windows preference failure stub is removed.
Windows continues using its original registry keys and 32-bit view.

Configuration roots are selected by the SDK-free `ConfigurationRoot` helper:

- Linux user settings: `$XDG_CONFIG_HOME/3dmmforever`, falling back to
  `$HOME/.config/3dmmforever`. Relative XDG paths are ignored, following the
  [XDG base-directory rules](https://specifications.freedesktop.org/basedir/0.8/).
- macOS user settings: `$HOME/Library/Application Support/3DMMForever`, using
  Apple's [application-support location](https://developer.apple.com/library/archive/documentation/FileManagement/Conceptual/FileSystemProgrammingGuide/MacOSXDirectories/MacOSXDirectories.html).
- Machine scope uses `/etc/3dmmforever` on Linux and
  `/Library/Application Support/3DMMForever` on macOS. It retains filesystem
  permissions and does not silently redirect machine writes to user storage.
- User storage fails if neither an applicable absolute XDG path nor an absolute
  HOME is available. This helper chooses preference paths, not movie-save or
  installed-content directories.

Each namespace has an exclusive `flock` held throughout read/default/write.
Missing values are checked under that lock, preventing concurrent default
creation from replacing another process's value. Writes use a private temporary
file, flush it and atomically rename it over the old record. Directory flush is
best effort where the filesystem supports it. Failed writes remove their
temporary file; an interrupted process can leave an ignored `.tmp-*` file.
New directories/files use modes 0700/0600. Record and namespace symlinks are
not followed; nonregular or malformed records report errors, not missing values.

The private preference format is separate from `.3mm`:

- Namespace directories are `k-` plus hex-encoded name bytes; records are `v-`
  plus hex-encoded value-name bytes. ASCII letters are folded to lowercase;
  other bytes are preserved. Each original name is limited to 126 bytes.
- A 12-byte header contains `KPS1`, one type byte (1 number, 2 string, 3 binary),
  three reserved zero bytes, and a little-endian 32-bit payload length.
- Payload bytes are retained exactly, with a 16 MiB limit. Numbers require four
  bytes; strings require a terminator within their payload but may retain bytes
  after it; binary payloads may be empty. Header/version/type/length errors,
  truncation and trailing bytes are rejected before changing the caller buffer.
- Default creation never replaces a corrupt record or hides an I/O error.

Studio's stored debug/user-data words and numeric startup flags now explicitly
use 32-bit types. Native warning flags previously passed `sizeof(bool)` to a
REG_DWORD write; they now use four-byte values. A registry regression test proves
that existing one-byte flags still read correctly without eager rewriting, and
that a subsequent explicit write uses a complete DWORD. Existing Windows
debug/user-data binary layouts remain the same; `.3mm` serialization is untouched.

Both Studio builds and Windows policy/backend tests passed (2/2 each). Linux
executed both the policy and actual filesystem-backend tests under headless QEMU.
Those tests cover persistent reopen, exact header/payload bytes, path selection,
permissions, ten corrupt-record cases, short buffers, symlink/FIFO rejection,
partial-write failure via a file-size limit, temporary-file cleanup and four
processes racing to create one default. All filesystem writes occur beneath a
unique temporary test directory; machine-scope paths are only checked as strings.
The backend and both tests also cross-compiled for macOS, with no macOS runtime
claim. No desktop application was launched. Evidence:
`build/inspector-deps/posix-settings-sdl-build.log`,
`posix-settings-native-build.log`, `posix-settings-sdl-compat-build.log`,
`posix-settings-native-compat-build.log`, `posix-settings-sdl-tests.log`,
`posix-settings-native-tests.log`, `posix-settings-linux-build.log`,
`posix-settings-mac-build.log` in the same directory, and
`build/port-cross/linux-posix-settings-runtime.log`.

This removes the preference-persistence blocker, not every startup dependency.
User-directory/name discovery, installed-product enumeration, native resources
and remaining drawing/runtime code still prevent a complete POSIX Studio launch.

## Preference policy boundary update for 3 November 2026

`Application::FGetSetRegKey` now maps legacy flags to the SDK-free
`KauaiSettings::Access` policy in `settings_store.h/.cpp`. The policy owns
read/write/default ordering, missing-value versus error reporting, stored-type
diagnostics and handle cleanup. It accepts a backend table, so persistence can
be supplied without registry types in the application or policy interface.

The Windows implementation is isolated in `settings_store_win.cpp`. It keeps
the original HKCU/HKLM key, `KEY_ALL_ACCESS | KEY_WOW64_32KEY`, REG_DWORD/REG_SZ/
REG_BINARY values and payload bytes. Existing settings are not migrated.
Reads with the wrong stored type still report that condition to Studio's
existing assertion policy. Defaults never overwrite a value after a read
error. Numeric writes require four bytes; string defaults are bounded by the
supplied buffer and use the correct byte count for UTF-16 code units. Explicit
string writes retain their supplied byte count, including any trailing data.

Both Studio builds succeeded. The settings policy and Windows backend tests
passed in each build (2/2), and both passed again in a separate UTF-16 build.
The backend test creates and deletes a unique key under
`HKCU\\Software\\3DMMForever\\Tests`, verifies raw registry bytes through the
native API, and never opens the real Studio preference key. Policy tests cover
default insertion/preservation, missing versus error results, failed opens and
writes, short buffers, unterminated defaults, type mismatches and handle cleanup.
The policy test also passed under headless Linux QEMU and cross-compiled for
macOS. No desktop application was launched. Evidence:
`build/inspector-deps/settings-sdl-build.log`, `settings-sdl-tests.log`,
`settings-native-build.log`, `settings-native-tests.log`,
`settings-unicode-build.log`, `settings-unicode-tests.log`,
`settings-linux-build.log`, `settings-mac-build.log` in the same directory, and
`build/port-cross/linux-settings-runtime.log`.

POSIX preference persistence is still missing: the Linux/macOS evidence covers
the policy with a test backend, not an on-disk preference store or full Studio
startup. `FGetSetRegKey` still selects the Windows backend under `WIN` and keeps
its old non-Windows failure path. Next is a POSIX persistence backend and its
user-data/configuration path selection. Installed-product enumeration remains
a separate Windows registry dependency; this change does not remove it.

## POSIX argument entry update for 3 November 2026

`app_entry_posix.cpp` now supplies the framework's non-Windows `main` adapter.
It copies `argc`/`argv` into the SDK-free launch context before calling
`FrameMain`; it does not create a window or rebuild a Windows-style command
line. The Windows adapter still captures the original raw text and uses the
existing parser unchanged.

Studio selects the parser according to the launch context. Argument-list input
uses `-` options and `--` to end options, accepts one document argument, treats
absolute `/` paths as filenames, and preserves embedded spaces and literal
quotes. Product overrides remain inline (`-pName`, `-tName`); empty overrides
are retained. Extra document arguments, null entries and empty document names
are rejected without partially replacing parsed options. Literal document paths
reach `Filename` without the Windows quote stripping/short-name lookup path.
MCP activation also recognizes an exact argument-list switch before `--` or
the document, while retaining its existing raw Windows behavior.

The launch context owns both strings and argument pointers. Failed updates
preserve existing borrowed document pointers; successful updates switch modes
and invalidate old readers. Startup initialization remains single-threaded.
The Studio parser block is now enabled for SDL as well as Windows.

Both Studio builds succeeded, and startup plus entry tests passed in each
(2/2). The new `studio-entry-test` uses the actual POSIX main adapter with a
stub `FrameMain`, proving delivery of a spaced product name and a quoted path
without starting Studio. Both tests passed under headless Linux QEMU and
cross-compiled for macOS; the startup tests also passed in a separate UTF-16
build. Evidence: `build/inspector-deps/argv-sdl-build.log`,
`argv-sdl-tests.log`, `argv-native-build.log`, `argv-native-tests.log`,
`argv-linux-build.log`, `argv-mac-build.log`, `argv-unicode-build.log`,
`argv-unicode-tests.log` in the same directory, and
`build/port-cross/linux-argv-runtime.log`. No desktop test ran.

This verifies the entry/argument components, not a complete Linux/macOS Studio
launch. Other Studio initialization, resource and drawing dependencies remain
Windows-specific. The legacy string/path size limits and product-display text
encoding also still apply; no movie-format or global text-encoding migration
is part of this change.

## Launch context and Windows entry update for 3 November 2026

Raw command-line text is no longer part of `WindowsAppGlobals`. The SDK-free
`launch_context.h/.cpp` owns an exact copy, including executable name, quotes
and native character code units. Studio option parsing, MCP enablement and the
legacy `ched`/`chelp` tools read it through `KauaiLaunch::CommandLine`.
The context is populated before application readers or worker threads start;
null input fails without replacing the previous value.

`WinMain` now lives in `app_entry_win.cpp`, separate from `appbwin.cpp`.
It captures the native command line, sets the remaining Windows instance/show/
thread fields, preserves inherited output pipes and calls `FrameMain` in the
same order. A command-line allocation failure exits before application startup.
The entry adapter still uses Windows and the Studio still needs a POSIX argv
path; this is isolation of bootstrap dependencies, not a complete portable
entry point. Existing option grammar, product overrides and movie data formats
are unchanged.

SDL and native Studio builds passed, as did native `ched` and `chelp` builds.
Startup tests passed in both builds and in a separate UTF-16 configuration.
They cover owned input lifetime, retained borrowed document text after rejected
updates, self-assignment, empty/long input and the existing parser cases.
The updated test also ran successfully under headless Linux QEMU and
cross-compiled for macOS. No desktop applications were launched. Evidence:
`build/inspector-deps/launch-sdl-build.log`, `launch-sdl-tests.log`,
`launch-native-build.log`, `launch-native-tests.log`, `launch-unicode-build.log`,
`launch-unicode-tests.log`, `launch-linux-build.log`, `launch-mac-build.log` in
the same directory, and `build/port-cross/linux-launch-runtime.log`.

## Startup filename lookup update for 3 November 2026

Studio's command-line document lookup now calls the SDK-free
`KauaiFiles::LookupLeafName` service instead of owning `WIN32_FIND_DATA` and
`FindFirstFile`/`FindClose` calls. Windows retains the original lookup, including
8.3 short-name expansion, case matching, wildcard matching and its acceptance
of the trailing quote left by legacy parsing. The POSIX backend checks the
literal path with `lstat` and returns the supplied leaf spelling. Neither
implementation rewrites parent directories or movie data. Empty document text
also no longer indexes before the start of the string when removing a quote.

Both Studio builds succeeded; file-services and studio-startup passed in each
(2/2 per build). The Windows tests exercised a real 8.3 alias successfully,
plus quoted/case/wildcard lookup, directories, missing paths and exact buffer
limits. The file-services test passed on Linux under headless QEMU; macOS
verification remains cross-compilation only. Evidence:
`build/inspector-deps/leaf-sdl-build.log`, `leaf-sdl-tests.log`,
`leaf-native-build.log`, `leaf-native-tests.log`, `leaf-linux-build.log`,
`leaf-mac-build.log`, `leaf-native-oracle.json` in the same directory, and
`build/port-cross/linux-leaf-runtime.log`. No Studio window was launched.

The startup adapter still obtains its raw command line from Windows globals
and remains guarded by `WIN`; POSIX argv integration and the Windows entry
point remain unfinished. This change isolates the filename service, not the
entire startup path. Remaining graphics/resource dependencies below also still
prevent declaring the full SDL port complete.

## Startup product names update for 3 November 2026

SDL startup no longer calls `LoadString` to obtain the application name or the
long/short product names. Those three defaults now live in the SDK-free
`inc/studio_product_names.h`. The native `.rc` string table consumes the same
constants, while the SDL adapter constructs its legacy strings directly.
Command-line name overrides and the existing long-to-short fallback retain
their ordering and behavior. Native builds continue loading their string
resources as before.

Both Studio builds succeeded. An offscreen resource check loaded each compiled
executable as data (without running it) and verified all three original values:
Microsoft 3D Movie Maker, 3D Movie Maker and 3DMovie. Evidence:
`build/inspector-deps/product-names-build.log`,
`build/inspector-deps/product-names-native-build.log` and
`build/inspector-deps/product-names-resources.json`. No desktop test ran.

This removes the startup-name resource dependency, not every resource use.
Raw command-line acquisition, long-filename lookup, other legacy resources and
the Windows entry point still need platform boundaries. The graphics work
remaining below also prevents declaring the complete SDL port finished.

## Startup path services update for 3 November 2026

Studio now obtains the current directory and executable path through the
SDK-free `KauaiFiles` interface. Windows native queries are confined to
`platform_files_win.cpp`; POSIX current-directory discovery uses `getcwd`,
Linux executable discovery uses `/proc/self/exe`, and macOS uses dyld's
`_NSGetExecutablePath` followed by path normalization. The Windows path
representation remains unchanged (ANSI or UTF-16 according to the build).

Both operations return lengths excluding the terminator, reject insufficient
capacity and clear a nonempty output buffer on failure. Startup now stops path
parsing with a warning when either query fails, instead of constructing a
Filename from truncated or invalid output. The executable and current-directory
Filename objects continue feeding the same content-directory discovery logic.

Native and SDL Studio builds pass. Startup/file-service tests pass in both
Windows configurations (2/2 each), including path existence/type, null/empty
buffers, exact capacities and truncation rejection. The file-service tests
also executed on Linux (`build/port-cross/linux-startup-paths-runtime.log`: status
0 and `PIXELS_LINUX_OK`), and the macOS cross-build passed. Evidence: `build/inspector-deps/startup-paths-build.log`,
`build/inspector-deps/startup-paths-tests.log`,
`build/inspector-deps/startup-paths-native-build.log`,
`build/inspector-deps/startup-paths-native-tests.log`,
`build/inspector-deps/startup-paths-linux-build.log` and
`build/inspector-deps/startup-paths-mac-build.log`.

Raw Windows command-line acquisition, long-filename lookup and resource-backed
product names remain startup dependencies to address. macOS verification is
cross-compilation only. No desktop test was run.

## Startup parser boundary update for 3 November 2026

Studio now delegates its legacy command-line grammar to the SDK-free
`StudioStartup::Parse` component in `inc/studio_startup.h`. The parser returns
speed/minimization options, product names, unknown-switch counts and a borrowed
raw document remainder. It preserves option ordering, quoted names, empty
product overrides and the original rule that the first non-option consumes
all remaining text as the document path. Malformed unterminated quoted product
names are rejected without modifying the output; the adapter reports a warning.
The previous private pointer-scanning helpers were removed.

Both native and SDL Studio builds pass, as does the new `studio-startup` test
in both configurations. The same parser test executed on Linux (status 0 and
`PIXELS_LINUX_OK`) and cross-compiled for macOS. Tests cover both byte strings
and UTF-16, independently of Windows argument acquisition or filesystem APIs.
Evidence: `build/inspector-deps/startup-parser-build.log`,
`build/inspector-deps/startup-parser-tests.log`,
`build/inspector-deps/startup-parser-native-build.log`,
`build/inspector-deps/startup-parser-native-tests.log`,
`build/inspector-deps/startup-parser-linux-build.log`,
`build/inspector-deps/startup-parser-mac-build.log` and
`build/port-cross/linux-startup-parser-runtime.log`.

The adapter still obtains the Windows raw command line, current/executable
paths and long filenames through native APIs. Product-name resource loading
also remains native. Those are the next startup boundaries; this parser does
not yet provide a POSIX argv entry point or make full Studio build on Linux.

The picture call-site review found direct capture/playback use in Kauai's file
tools and offscreen tests, with no direct Studio editor call found. Indirect
resource usage has not been exhaustively audited. Preserve the legacy path and
stored bytes while prioritizing startup dependencies that Studio directly uses;
do not infer that all metafile support is unnecessary. No desktop tests ran.

## Monochrome masked artwork update for 3 November 2026

Monochrome masked artwork now uses the portable packed transfer. Mask bits
remain raw; indexed source colors translate through the monochrome palette.
The host retains the black background/reference state established by the
original native blit, so subsequent solid drawing sees the same state even
when no native context has been allocated. Existing brush realization remains
selected until the next brush change.

The oracle passes 2,232 monochrome comparisons, now including solid draws
before and after the bitmap to verify the background transition. All cases
require the SDL destination to remain without a native context or bitmap
mirror. The 4/8/16/24-bit comparisons still pass. Fixed portable fixtures cover
partial bytes, transparent cells, shrink reduction, padding and malformed-input
rejection before writing.

Studio and library-check targets rebuilt. Affected SDL checks passed (5/5),
and native x86 checks passed (2/2). Core tests executed on Linux and
cross-compiled for macOS. The runtime log
`build/port-cross/linux-masked1-runtime.log` records test status 0 and
`PIXELS_LINUX_OK`.
Evidence: `build/inspector-deps/masked1-final-build.log`,
`build/inspector-deps/masked1-final-tests.log`,
`build/inspector-deps/masked1-native-tests.log`,
`build/inspector-deps/masked1-linux-build.log` and
`build/inspector-deps/masked1-mac-build.log`.

Masked artwork now has portable paths for every supported offscreen depth.
This does not finish the graphics port: mixed monochrome patterns and legacy
picture playback still use native drawing, and approximate near-size indexed
quantization retains its native fallback. Windows startup/resource assumptions
and a full non-Windows Studio application remain unfinished. No desktop test
was run.

## Four-bit masked artwork update for 3 November 2026

Four-bit masked artwork now uses the core RLE transfer. Source colors and both
mask colors are translated into the destination palette before applying the
legacy Boolean operation. Missing source entries translate as black. Packed
writes preserve adjacent nibbles and padding; transparent pixels retain the
original palette-dependent two-pass behavior rather than assuming a no-op.

The native oracle passes 11,160 four-bit comparisons across five destination
palette arrangements, three source palettes, ordinary/solid masks, both stretch
modes, clipping and varied destination sizes. Each SDL call requires no native
context or framebuffer mirror. The 8/16/24-bit comparisons remain passing.
Fixed-byte fixtures check nibble boundaries, partial rows, shrinking, palette
validation and unchanged output on rejection.

Studio and library-check targets rebuilt. Affected SDL checks passed (4/4),
and native x86 checks passed (2/2). Core pixel tests executed on Linux and
cross-compiled for macOS. Linux evidence is
`build/port-cross/linux-masked4-runtime.log` (test status 0 and `PIXELS_LINUX_OK`).
Evidence: `build/inspector-deps/masked4-final-build.log`,
`build/inspector-deps/masked4-final-tests.log`,
`build/inspector-deps/masked4-native-tests.log`,
`build/inspector-deps/masked4-linux-build.log` and
`build/inspector-deps/masked4-mac-build.log`.

Monochrome masked destinations still use native drawing. Mixed monochrome
patterns, legacy pictures and Windows startup/resource assumptions remain
unfinished. Desktop testing remains restricted.

## Packed masked artwork update for 3 November 2026

RGB555 and BGR24 masked artwork now draws through the portable RLE transfer
beneath `GraphicsPort::DrawMbmp`. The core retains the original two-pass
mask-OR/color-AND semantics, including near-size sampling and BLACKONWHITE
shrink reduction of source indices before palette translation. Copying pixels
also records COLORONCOLOR mode for packed destinations, so subsequent masked
artwork uses the same persistent stretch policy as native drawing.

The native oracle passes 2,232 comparisons for each new destination depth,
covering ordinary/solid masks, clipping holes, padding, enlargement, shrinking,
short palettes and both stretch modes. These calls require the SDL destination
to remain without a native context or framebuffer mirror. The existing 11,160
indexed/private-palette comparisons still pass. Fixed portable byte fixtures
cover transparency, RGB555 unused-bit clearing, shrink reduction and malformed
input rejection before writing; those fixtures also executed on Linux and
cross-compiled for macOS.

Studio and library-check targets rebuilt. Affected SDL checks passed (5/5),
and native x86 pixel/masked-transfer checks passed (2/2). Evidence:
`build/inspector-deps/packed-masked-final-build.log`,
`build/inspector-deps/packed-masked-final-tests.log`,
`build/inspector-deps/packed-masked-native-tests.log`,
`build/inspector-deps/packed-masked-linux-build.log`,
`build/inspector-deps/packed-masked-mac-build.log` and
`build/port-cross/linux-packed-masked-runtime.log`.

Monochrome and four-bit masked destinations still use native drawing. Mixed
monochrome patterns, legacy pictures and Windows startup/resource assumptions
remain unfinished. No desktop test was run; the full macOS Studio has not been
built or executed.

## Portable font canvas update for 3 November 2026

Moved the complete RGB555/BGR24/BGRX32 text paint-and-copy sequence from
`gfxwin.cpp` into the SDK-free `Face::DrawPacked` implementation in
`font_canvas.cpp`. The shared library owns destination expansion, font color
rendering, RGB555 unused-bit preservation, clipping and final packed output.
Studio's adapter supplies the selected font, placement, colors and storage.
The native RGB555 smoothing fallback remains an explicit provider choice.

The portable tests use fixed coverage/color expectations to verify exact
RGB555 and BGR24 output, untouched stored bits, clipping holes, padding and
unchanged output after provider failure or an unsupported destination format.
These fixtures executed successfully on Linux (`font-services-test: status 0`
and `PIXELS_LINUX_OK`) and cross-compiled for macOS. They validate the shared
rendering path, not a full non-Windows Studio application.

Studio and library-check targets rebuilt. Affected SDL checks passed (3/3:
font services, native font oracle and mixed pixel storage); native x86 font
checks passed (2/2). Evidence:
`build/inspector-deps/font-packed-final-build.log`,
`build/inspector-deps/font-packed-final-tests.log`,
`build/inspector-deps/font-packed-native-tests.log`,
`build/inspector-deps/font-packed-linux-confirm.log`,
`build/inspector-deps/font-packed-mac-confirm.log` and
`build/port-cross/linux-font-packed-runtime.log`.

The temporary RGB snapshot still covers the full destination surface; limiting
it to glyph bounds remains a memory/performance improvement. Mixed monochrome
patterns, legacy pictures, remaining masked bitmap paths and Windows startup/
resource assumptions remain porting work. No desktop tests were run.

## Monochrome brush reuse update for 3 November 2026

Fixed the repeated-solid-brush compatibility gap recorded below. The host now
retains the background/reference state used to realize its selected solid
monochrome brush. A background change caused by text does not re-realize that
brush. Selecting a different solid color, stock inversion brush or mixed
pattern replaces/invalidates the retained state; clear draws leave it intact.
Rectangle, oval and polygon paths share this explicit selection state.

`PackedText` now keeps the swatch color constant across all 1,080 monochrome
cases, retaining the previously failing case as a regression. A separate
FreeType sequence changes the text background, reuses the solid brush, selects
the inversion brush and recreates the solid color. It verifies expected packed
bytes while requiring both native context and framebuffer mirror to stay absent.

Studio rebuilt successfully. All three affected offscreen SDL suites pass:
font, packed shapes (87,480 comparisons) and pixel storage (111,780 scrolling
comparisons plus mixed drawing/export checks). The expanded font oracle also
passes in native x86. Logs:
`build/inspector-deps/mono-cache-final-build.log`,
`build/inspector-deps/mono-cache-final-tests.log` and
`build/inspector-deps/mono-cache-native-tests.log`.

The native mixed-pattern realization, including monochrome boxed-text borders,
remains unfinished. Legacy pictures, remaining masked bitmap paths and Windows
startup/resource dependencies remain broader port work. All verification was
offscreen; the desktop-testing constraint remains in force.

## Monochrome text update for 3 November 2026

Monochrome glyphs and text backgrounds now use portable packed coverage. Text
and solid shapes share `ResolveMonochromeColor`, whose inputs include the
background color and source-reference identity. Opaque text retains its new
background state even before a native context exists; later native allocation
receives that state. FreeType checks require ordinary monochrome text and a
subsequent solid draw to remain free of native contexts and bitmap mirrors.
The patterned border of boxed monochrome text still uses native drawing.

The expanded oracle passed 1,080 monochrome text comparisons, including
transparent/opaque/inverted backgrounds, alignment, clipping and subsequent
solid draws with newly selected brushes. The SDL font, canvas, shape and
storage checks passed (4/4); the font oracle also passed in native x86. Studio
rebuilt successfully. The shared pixel canvas built for Linux and macOS; no
new cross-platform runtime execution or desktop test was performed this turn.
Logs: `build/inspector-deps/text1-final-build.log`,
`build/inspector-deps/text1-final-tests.log`,
`build/inspector-deps/text1-native-tests.log`,
`build/inspector-deps/text1-linux-build.log` and
`build/inspector-deps/text1-mac-build.log`.

**Historical finding, fixed by the brush reuse update above:** a repeated solid monochrome brush can retain its
native realization across a background-color change. The portable solid path
currently resolves using current background state instead. Reproduce by
changing `PackedText`'s alternating swatch colors to the constant
`AbstractColor(13, 98, 201)`: monochrome case 362 (style 0, opaque background,
ink 0, after the earlier boxed-text cases) differs at byte 140, native 0x4f
versus portable 0x4e. The text-only and alternating-new-brush comparisons pass;
they do not prove this cached-brush sequence compatible. Model native brush
reuse/invalidation explicitly and retain the constant-color case as a regression.

Mixed monochrome patterns, legacy pictures, remaining masked bitmap paths and
Windows startup/resource dependencies remain unfinished. Desktop testing stays
restricted as documented above; the overall SDL goal remains active.

## RGB555 text update for 3 November 2026

The FreeType provider now draws into 16-bit RGB555 surfaces through portable
storage. The temporary RGB image carries the original unused RGB555 bit in
its high byte. The portable font renderer preserves that byte for untouched
pixels (including background inversion) and clears it when coverage writes a
pixel; conversion back to RGB555 therefore preserves the original storage
semantics. Fresh-port checks verify untouched high bits, visible written ink
and absence of a native context or framebuffer mirror.

The optional GDI provider retains native RGB555 drawing. Offscreen probes show
that its native 16-bit smoothing differs from both binary glyph coverage and
32-bit rendering followed by truncation. The native comparison suite now
covers 1,080 RGB555 cases in addition to 4/8/24/32-bit text. This exposed a
mixed-backend bug: an inverted CPU background could be overwritten when native
text used the earlier bitmap snapshot. The fallback now uploads the inverted
pixels before native text drawing.

Studio rebuilt successfully. The SDL font/storage checks passed (2/2), and
the expanded font oracle passed in native x86. Evidence:
`build/inspector-deps/text16-final-build.log`,
`build/inspector-deps/text16-final-tests.log` and
`build/inspector-deps/text16-native-tests.log`. All checks were offscreen;
no desktop test was run. This update does not establish a full macOS build.

Remaining work includes monochrome text, mixed monochrome patterns, legacy
pictures, remaining masked bitmap paths and Windows startup/resource code.
The GDI provider's RGB555 path remains a compatibility fallback; the FreeType
path advances the SDL runtime independently of it.

## 24-bit text update for 3 November 2026

BGR24 text now uses the font-service color renderer and portable packed copies.
A lossless RGB snapshot supplies the existing destination colors for smoothing;
the completed image is copied back through the original clip rectangle/region.
This preserves transparent, opaque and inverted backgrounds, boxed text and
palette-derived source colors. Failure before the copy leaves the destination
untouched. The snapshot currently covers the entire surface, so this path has
an extra temporary 32-bit buffer; reducing that to the text bounds is a future
memory/performance improvement.

The offscreen font oracle passed 1,080 BGR24 comparisons against native GDI,
in addition to 3,240 four-bit and the existing 2,160 indexed/2,160 RGB cases.
The FreeType checks also draw boxed text into fresh 4/24-bit ports and require
both native context and framebuffer mirror to remain absent. The same oracle
passes in the native x86 build. Studio rebuilt successfully, and the SDL font
and storage tests passed (2/2). All verification for this update was offscreen;
no desktop smoke test or window activation was performed.

Evidence: `build/inspector-deps/text24-final-build.log`,
`build/inspector-deps/text24-final-tests.log` and
`build/inspector-deps/text24-native-tests.log`.

Monochrome and RGB555 text still have native drawing paths. The RGB555 port
must preserve the unused stored bit and native smoothing/quantization behavior;
blindly round-tripping an entire 16-bit surface through RGB would change that
bit in untouched pixels. Mixed monochrome patterns, legacy pictures, remaining
masked bitmap paths and startup/resource assumptions are also unfinished.
The full SDL goal remains active, with desktop testing restricted as above.

## Four-bit text update for 3 November 2026

Resumed the SDL goal and completed the previous checkpoint's verification:
83/83 SDL tests, 58/58 native x86 tests and Linux execution of the monochrome
solid-brush fixtures passed. Logs are `build/inspector-deps/mono-solid-final-tests.log`,
`build/inspector-deps/mono-solid-native-final-tests.log` and
`build/port-cross/linux-mono-solid-runtime.log`.

Four-bit text now uses the portable canvas beneath Studio's existing font API.
The font host supplies glyph/background coverage; the adapter maps colors into
the destination palette. Transparent, opaque and inverted backgrounds, boxed
frames, alignment and clipping retain the native behavior. FreeType drawing
also leaves the port without a native device context or framebuffer mirror.
The GDI font provider remains available for compatibility comparisons.

The core now provides binary coverage into all six packed formats. It takes
raw stored samples, thresholds coverage at 128 and preserves neighboring bits,
unused nibbles and row padding. This is a raster primitive; it does not itself
implement font layout or color conversion for the remaining text formats.
Independent bit-level tests exercise packing, clipping holes, offscreen masks,
guard rows and rejected inputs.

Verified 3,240 four-bit drawing comparisons against native GDI across fonts,
styles, alignment, backgrounds and three destination palettes. The existing
2,160 indexed and 2,160 RGB comparisons also pass. The affected pixel canvas,
storage and font tests pass in both SDL and native x86 builds (3/3 each).
Linux execution and macOS cross-compilation of pixel-canvas pass. See the
current handoff above for the incomplete desktop smoke verification.

Evidence: `build/inspector-deps/packed-text-targeted-tests.log`,
`build/inspector-deps/packed-text-native-tests.log`,
`build/inspector-deps/packed-text-linux-build.log` and
`build/inspector-deps/packed-text-mac-build.log`.

Next drawing work includes monochrome and 16/24-bit text, mixed monochrome
patterns, remaining masked bitmap paths, legacy pictures and indexed
quantization. Windows startup/resource assumptions and a complete macOS Studio
build/run remain unfinished. See the current handoff above for current status.

## Paused checkpoint for 3 November 2026

Paused at the user's request after the monochrome solid-brush change. The
objective remains making the original Studio work through SDL, preserving its
UI, resource scripts, editing behavior and movie compatibility.

Solid monochrome rectangles, ovals, polygons and thick lines now use the
portable canvas. Explicit `MonochromeBrushState` carries background color and
opaque color-reference identity from the host, preserving the measured native
conversion rules. Constant patterns and equal-source-color patterns also use
this path. Mixed two-color patterns still fall back to native drawing; their
observed brush realization is not yet reproduced by the portable renderer.

Verified the three affected rendering tests in both SDL and native x86 builds
(3/3 each), and the full original Studio smoke workflow. The portable pixel
canvas also built for Linux and cross-compiled for macOS. This change has not
yet received a fresh run of the full 83-test SDL and 58-test native suites, or
Linux runtime execution; the older full-suite results below apply to earlier
milestones. A complete macOS Studio has not been built or run.

Evidence: `build/inspector-deps/mono-solid-targeted-tests.log`,
`build/inspector-deps/mono-solid-native-targeted-tests.log`,
`build/inspector-deps/mono-solid-smoke.log`,
`build/inspector-deps/mono-solid-linux-build.log` and
`build/inspector-deps/mono-solid-mac-build.log`. Workflow artifacts are in
`build/studio-sdl-mono-solid`.

Resume by running the full SDL/native suites and the rebuilt Linux pixel-canvas
fixture image (`SHAPE_ONLY=1`). Then continue removing native drawing behind
the existing API: reproduce mixed monochrome pattern behavior, packed text,
remaining masked bitmap paths, legacy pictures and indexed quantization.
Preserve the native comparison tests and transactional unsupported-input
fallbacks. Windows startup/resource assumptions remain broader porting work.

## Four-bit drawing update for 3 November 2026

Four-bit indexed rectangles, framed/filled ovals, polygons and thick lines now
use the portable canvas beneath the existing Studio API. The palette-aware
brush resolver preserves normal, reversed and nonstandard private palettes,
including palette-dependent Boolean operations. Monochrome rectangle highlights
also operate directly on packed pixels, without palette metadata or a native
context. Partial final bytes, clip holes and row padding are preserved.

Verified **83/83 SDL tests**, **58/58 native x86 tests** and the full original
Studio workflow. The expanded shape oracle checks **52,920 cases**, including
4-bit/RGB555/BGR24 drawing and unchanged monochrome highlight/fill fallback
behavior. Fresh-port highlight checks require the new paths to remain free of
native contexts and bitmap mirrors. Portable fixed-byte tests cover nibble
boundaries, palette reversal, raw highlighting and unsupported-input rejection.
These tests also executed on Linux and cross-compiled for macOS; a complete
macOS Studio still has not been built or run.

Logs: `build/inspector-deps/subbyte-shape-final-tests.log`,
`build/inspector-deps/subbyte-shape-native-final-tests.log`,
`build/inspector-deps/subbyte-shape-smoke.log` and
`build/port-cross/linux-subbyte-shape-runtime.log`. Studio workflow artifacts
are in `build/studio-sdl-subbyte-shape`.

Monochrome fills remain native. Native probes and the expanded oracle show
that independently mapping foreground/background RGB colors to black/white
fails to preserve patterned brush output. Solid brush conversion also depends
on DC background state. Continue by modeling that state and native brush
realization before switching these fills to the portable renderer. The native
comparison cases remain in `packed_shape_gdi_test.cpp`. Microsoft's
[pattern-brush reference](https://learn.microsoft.com/windows/win32/api/wingdi/nf-wingdi-createpatternbrush)
describes the DC text/background color dependency; measured output remains the
compatibility oracle.

Other remaining drawing work includes packed text, masked-bitmap paths,
legacy pictures and near-size indexed quantization. Windows startup/resource
assumptions and the complete macOS Studio build/run remain in the broader plan.
The goal remains active.

## Packed scrolling update for 3 November 2026

Scrolling now uses portable storage for all six packed formats beneath the
original Studio API, without a native bitmap or device context for valid
rectangles. The shared code clips source and destination, handles overlapping
moves and preserves row padding, partial-byte bits and raw RGB555 samples.
Raw scrolling does not require palette metadata.

The comparison work exposed an original monochrome ordering rule: leftward
moves copy full bytes before masked edges within each clip band. Those edge
reads can observe the moved body. Studio explicitly selects that behavior with
`PackedScrollOptions::legacyMonochromeEdges`; the general API defaults to
snapshot semantics. Eight captured native fixtures run without Windows, while
an independent bit-level oracle checks ordinary scrolling across all formats.
Reversed rectangles remain outside the portable API contract and preserve the
existing native fallback rather than changing their legacy behavior.

Verified **83/83 SDL tests**, **58/58 native x86 tests** and the full Studio
workflow. The expanded storage test compares **111,780 scroll combinations**
against native GDI, including clipping regions, crops, both move directions,
byte alignments, empty/reversed rectangles and native-free valid SDL calls.
The updated portable pixel tests also executed on Linux and cross-compiled
for macOS. This still does not establish a working macOS Studio application.

Logs: `build/inspector-deps/packed-scroll-final-tests.log`,
`build/inspector-deps/packed-scroll-native-final-tests.log`,
`build/inspector-deps/packed-scroll-smoke.log` and
`build/port-cross/linux-packed-scroll-runtime.log`. Studio workflow artifacts
are in `build/studio-sdl-packed-scroll`.

Next drawing work remains monochrome/4-bit shapes, packed text, remaining
masked-bitmap paths, legacy pictures and near-size indexed quantization.
Windows startup/resource assumptions and the full macOS Studio build/run
remain in the broader plan. The goal remains active.

## Progress for 3 November 2026

Resumed the SDL goal and removed native shape drawing from RGB555/BGR24
surfaces. Rectangles, framed/filled ovals, polygons, thick lines and highlights
now use the portable canvas beneath the existing Studio API. The shared brush
resolver retains indexed behavior and the distinct raw RGB555 rules for brush
inversion versus highlighting. Native metafile fallback and mixed drawing
synchronization remain available.

Verified **83/83 SDL tests** and **58/58 native x86 tests**, including **5,760
packed-shape comparisons** against GDI in each build. The new comparisons check
raw bytes, unused bits, padding, solid/patterned brushes, clipping, and that the
SDL adapter never allocates native drawing resources for these shapes. Full
Studio workflow artifacts are in `build/studio-sdl-packed-shape`; the smoke log
is `build/inspector-deps/packed-shape-smoke.log`. Suite logs are
`build/inspector-deps/packed-shape-final-tests.log` and
`build/inspector-deps/packed-shape-native-final-tests.log`.

The updated portable pixel tests executed successfully on Linux
(`build/port-cross/linux-packed-shape-runtime.log`) and cross-compiled for
macOS. The complete Studio still has not been built or run on macOS.

Continue with monochrome/4-bit shape drawing, packed scrolling and text,
remaining masked-bitmap paths, legacy pictures and near-size indexed
quantization; then remove the remaining Windows startup/resource assumptions.
Keep native rendering as an oracle and preserve the original Studio and movie
formats. The goal remains active; this is a completed graphics step within it.

## Handoff for 2 November 2026

Paused at the user's request after completing the packed-surface allocation
change and its regression checks. The objective remains making the original
Studio work through SDL, preserving its editor, resources, commands and movie
formats. The full Studio is still a Windows bridge; a macOS Studio application
has not been built or run.

Latest completed work:

- The application header compiles without Windows SDK headers; native bootstrap
  globals are private, with x86/x64 callback ABI checks.
- Main-thread assertions use SDL/ImGui with the original answers and diagnostics.
- Studio restores minimized windows through SDL before its open/save portfolios;
  maximize/fullscreen properties use actual SDL window state.
- All six packed pixel formats can start without native bitmaps or device
  contexts. Indexed 4/8-bit creation still needs an application palette for this
  path. Native drawing creates a synchronized compatibility mirror on demand.
  Copies from every source format into 32-bit destinations remain native-free.

Verification at this stopping point:

- SDL build: **82/82 tests passed** (`build/inspector-deps/packed-lazy-final-tests.log`).
- Native x86 build: **57/57 tests passed**
  (`build/inspector-deps/packed-lazy-native-final-tests.log`). Both Studio binaries built.
- Full original Studio smoke workflow passed, including integer scaling,
  fullscreen, minimized-window portfolio restoration, save/reopen/overwrite,
  video, MIDI/audio/recording, actor placement, text/font picker and exit.
  Artifacts: `build/studio-sdl-packed-lazy`; log:
  `build/inspector-deps/packed-lazy-smoke.log`.
- The preceding window-service work also passed Linux host execution and macOS
  host cross-compilation. This does not establish a working macOS Studio.

Resume with the remaining native drawing boundary beneath `GraphicsPort`:
trace the native fallbacks reachable from original Studio, then remove them
while retaining the native implementation as a comparison oracle. Remaining
areas include legacy metafile pictures, drawing in packed formats other than
8/32-bit, near-size truecolor-to-indexed palette conversion and device-palette
bootstrap. Continue with Windows startup/resource assumptions and the remaining
window/control adapters, then build and run the same Studio on macOS. The
[remaining-work list](#remaining-work) records the broader scope, including
real device and platform integration checks.

Resume from this worktree without resetting it: changes remain uncommitted and
include earlier user/porting work. No commit or push was made for this pause.
GitHub workflows remain intentionally disabled. Keep original `.3mm` data and
compatibility fixtures throughout; do not replace Studio with the inspector.

## Current Windows bridge

Configure a separate build from a Visual Studio developer shell:

```console
cmake -S . -B build/studio-sdl-x64 -G Ninja -DCMAKE_BUILD_TYPE=Debug -DSTUDIO_SDL3=ON -DBUILD_PACKAGES=OFF
cmake --build build/studio-sdl-x64 --target studio sdl-host-test --parallel 8
ctest --test-dir build/studio-sdl-x64 -R "^sdl-host$" --output-on-failure
```

Run `build/studio-sdl-x64/3dmovie-sdl.exe` with the original installed content
directory as its working directory, for example
`dist/Microsoft Kids/3D Movie Maker`. The experimental build has its own
single-instance name and skips the native build's post-build installation copy.

SDL3 presents a 640 by 480 framebuffer and delivers pointer and keyboard events
to the existing Kauai command handlers. Offscreen pixel ownership, including
the presented framebuffer, now lives in SDK-free `KauaiPixels::PixelBuffer`
storage. Windows DIBs remain as compatibility mirrors for native drawing.
Rectangle fill/frame/highlight operations, filled polygons, thick
lines and 32-bit, indexed-to-RGB or indexed-to-indexed bitmap transfers now use the portable
`kauai-core` pixel canvas. Rectangle/polygon fills, frames, thick lines and
rectangle highlights also draw directly into indexed 8-bit surfaces. Indexed
brush preparation preserves the original palette-dependent Boolean operations,
including palettes with black/white in reversed slots. Masked bitmaps
draw directly from validated compressed rows into 32-bit targets; scaled 8-bit
targets using the active palette use the same portable sampler. Private-palette
8-bit stretches also draw through the portable canvas, preserving the original
mask-OR/color-AND operations and palette translation. This includes both
nearest-sample and default AND reduction modes. The SDL port records that
persistent sampling state directly; masked draws no longer query the native DC
to choose the portable sampler. Unscaled 8-bit drawing retains its raw-index behavior.
General indexed copies translate palettes, keep duplicate-color indices when
the palettes match, and snapshot overlapping source storage. The existing
active-palette 2x memory-copy optimizations remain in use. The Windows adapter
still maintains native clipping and stretch state for mixed drawing paths;
the core copy operations use no native objects.
RGB555 and BGR24 surfaces now also draw rectangles, framed/filled ovals,
polygons, thick lines and rectangle highlights through the same portable span
geometry. A shared brush resolver preserves raw Boolean operations across
indexed and packed formats. RGB555 brush inversion XORs packed white (0x7FFF),
while highlight inversion toggles all stored bits (0xFFFF); both retain the
original behavior. Padding and complex clip regions remain untouched outside
the shape. `packed-shape-gdi-test` compares 5,760 combinations against native
GDI, checks raw bytes including RGB555's unused bit, and requires the SDL
adapter to create neither a native context nor a bitmap mirror. The core pixel
canvas test covers fixed RGB555/BGR24 bytes, clipped holes, highlight inversion,
padding and transactional rejection of unsupported/oversized inputs.
Monochrome solid brushes now use explicit `MonochromeBrushState` data in the
portable canvas. Conversion checks exact palette colors first, then the DC
background color and the origin of its color reference. Equal RGB values with
different origin tokens remain distinct. The rasterizer compares these tokens
without interpreting native flags or importing a window SDK. The Windows
adapter supplies state from an existing DC when one is present; fresh pixel
ports use the original white background without allocating a native context.
Solid/constant patterns and equal-source-color brushes draw rectangles,
ovals, polygons and thick lines through this path. Mixed two-color patterned
brushes still fail before writing and retain the original native fallback.

Four-bit indexed surfaces now use the same portable rectangle, oval, polygon
and thick-line geometry, including reversed/private palette Boolean operations.
Raw highlighting also supports monochrome without palette metadata or a native
context. `packed-shape-gdi-test` now checks seven ink choices against three
4-bit palettes, retaining monochrome fill fallback comparisons. Fresh-port
highlight checks require all packed formats to leave native resources absent.
The portable core tests also check odd nibble boundaries, clip holes, row
padding and the distinction between a white-index brush XOR and raw highlight
inversion. Mixed monochrome patterns, 1/16-bit text and native picture playback remain
separate work.

Packed bitmap copies also use the portable canvas for monochrome, 4-bit and
8-bit indexed pixels, RGB555, BGR24 and BGRX32. Color conversion, packed writes,
source crops, clipping and overlap snapshots operate on borrowed pixel storage.
The adapter caches 1/4-bit color tables as well as 8-bit palettes. One conversion
still uses GDI: true-color to 8-bit when both dimensions differ by at most one
pixel. Native comparisons exposed an approximate 5-bit palette lookup whose
results varied between independently created native palettes. A captured lookup
was therefore not reliable enough to replace that path. `CopyPacked` rejects
this case without writing; Studio retains its existing native conversion.
Bitmap scrolling now uses the portable canvas for all six packed formats.
Raw scrolling requires no palette, preserves unused bits in partial bytes and
RGB555 samples, and retains guard rows and padding. The shared algorithm clips
both source and destination and uses a snapshot for overlapping moves.
The Studio adapter explicitly selects the original leftward monochrome policy:
GDI processes clip bands in move order, copies complete bytes first, then lets
masked edge reads observe that moved body. Four-bit and truecolor scrolling
retain ordinary snapshot semantics. Eight captured native fixtures preserve
these rules in portable tests, while an independent bit-level test verifies the
default snapshot API across all formats. The expanded native test covers
111,780 scroll combinations and requires valid SDL calls to leave native
contexts and mirrors absent. Reversed rectangles remain outside the core API
contract and retain the existing native fallback in Studio.

SDL-window controls route scrolling to
the actual framebuffer instead of `ScrollWindowEx`, retaining Kauai's exposed
rectangles and immediate/marked/deferred repaint modes. The SDL host now tracks
exact damage in logical pixels, including validation and scrolling of pending
damage. Window resizing and letterboxing do not change these coordinates.
Paint passes consume their damage before drawing, retaining any repaint requests
raised during drawing for the next pass. Windows paint messages acknowledge
native exposure and request a logical repaint; the SDL canvas no longer uses
Windows update regions for invalidation queries or validation.
Legacy inverted rectangles fall
back to the native adapter; the portable API rejects them without writing.
Text measurement and painting now
use the SDK-free `font_services.h` face/layout interface. Indexed glyph masks
are painted by the portable canvas; RGB runs preserve smoothing through color
patches supplied by the provider. SDL builds default to the portable FreeType
2.14.3 provider (`KAUAI_FREETYPE=ON`). Its SDK-free `FontCatalog` reads font files,
enumerates collection/bitmap faces and selects a family and style. Text runs no
longer create an HFONT or borrow GDI metrics. Cell geometry comes from OpenType
OS/2, hhea and hinted VDMX tables, or the WinFNT header. The logical 96-DPI System
font uses the VGA bitmap with integer enlargement. The original Studio font
menu/list now uses catalog families, with portable fixed-pitch metadata and the
existing name-based lookup and user-locale sorting. This path does not enumerate
GDI fonts or need a device context. `kauai-font-host` owns discovery, selection,
System policy and statistics behind `font_host.h`. `font_gdi.cpp` now contains
only the Windows compatibility face/bootstrap. Saved text records retain their original
encoding and font-name mapping; no record versions or writer changes were added.
Set `KAUAI_FONT_PROVIDER=gdi` before launching to use native enumeration and the
compatibility provider. Complex-script shaping and broader localized-name
matching remain future work.

Font search roots are private platform backends: Windows uses the per-user and
Windows font folders; POSIX uses Linux XDG font folders or macOS user, shared and
system font folders. `KAUAI_FONT_PATH` prepends additional directories (semicolon
separated on Windows, colon separated on POSIX). These additional directories
and POSIX font trees are scanned up to eight levels below each root without
following links. Windows retains its VGA System bitmap; the macOS policy tries
Helvetica, Helvetica Neue and other available outline fallbacks, while Linux
tries DejaVu Sans, Liberation Sans and other fallbacks. This changes the runtime
System alias only, not stored font names. No fonts are bundled. Filesystem
discovery does not reproduce CoreText/fontconfig activation or downloadable-font
management; macOS execution remains unverified.
The macOS roots follow Apple's [Font Book install locations](https://support.apple.com/en-ie/guide/font-book/fntbk1004/mac)
and [system font directory](https://support.apple.com/en-gb/103203).

Legacy pictures and remaining packed bitmap formats still use GDI.
The high-level drawing API in `gfx.h` can now be included without Windows SDK
headers. `GraphicsPort` stays opaque there; its native fields, polygon layout
and region helpers live in `gfx_port.h`, included by runtime/native adapters.
Font enumeration callback signatures stay in the native source. Legacy native
font and DIB interoperability uses forward-declared tokens whose Windows types
are checked by the core/native include-order gate.
Clipping in drawing data now uses core `RC` geometry. Font-description words,
abstract colors, drawing flags and pen sizes retain their Windows 32-bit widths
on LP64 hosts. Eight-byte pattern operations no longer cast their storage to
host `long` arrays, which read/write beyond the pattern on 64-bit Unix systems.
`graphics-api-test` checks the SDK-free include, fixed-width value layouts and
all 64 pattern bits; it runs on Linux and cross-compiles for macOS. Native pixel
comparisons retain separate SDK rectangles as their oracle and pass core
rectangles into the drawing API. This separates the API from the remaining GDI
implementation; it does not yet supply a native-free `GraphicsPort` backend.

Decoded video frames have a portable true-color drawing path. `dib_frame.h/.cpp`
reads bounded standard 40-byte `BI_RGB` DIBs with 24-bit BGR or 32-bit BGRX pixels,
normalizes row order into owned top-down BGRX32 storage, and preserves the unused
byte for 32-bit input. Invalid headers, compression, palettes, overflow and
truncation are rejected transactionally. `DrawDibFrame` draws those pixels with
the original format-specific sampling and existing clip/region rules. The SDL
`GraphicsPort::DrawDib` adapter uses it for bottom-up frames on 32-bit ports.
Top-down input retains the native adapter because `DrawDibDraw`'s default source
height does not behave like a logical top-down DIB read. Indexed targets and
unsupported formats also retain the existing path in this legacy decoder
adapter. SDL's own video playback uses the portable decoder described below.
`dib-frame-test` checks orientation, BGR/BGRX bytes, drawing, alias rejection and
680 truncated inputs without SDK headers. It runs on Linux and cross-compiles
for macOS. `dib-frame-gdi-test` checks 3,024 native drawing comparisons and, in
SDL builds, 3,024 retained top-down fallback cases. It covers arbitrary and
integral scaling, mixed shrink/grow axes, negative origins, clipping and the
actual SDL adapter. Supported-case adapter checks pass a null native drawing
handle, so accidentally falling back cannot satisfy the pixel comparison.

The video API also has a host boundary. `video.h` no longer includes native AVI,
decoder, drawing or window handles. `video_stream_host.h` defines explicit
32-bit metadata, relative frame timing and a UI-thread presentation service.
`video.cpp` keeps Studio's command scheduling, playback position and invalidation;
`video_stream_win.cpp` privately owns VfW file/stream/frame/drawing handles and
their lifetime. Native window-based playback, including MCI audio suspension and
window positioning, lives in `video_window.cpp` behind an opaque state pointer.
This preserves both existing video modes rather than substituting silent stream
playback for the window-based mode.
`video-api-check` includes the public header without Windows SDK headers on
Windows, Linux and macOS. `video-stream-host-test` writes a real three-frame AVI
with a nonzero starting sample, compares timing against direct native calls,
checks decoded pixels through the actual graphics adapter, then exercises failed
open/reopen and file-handle release. Those native hosts remain active in the
native build; SDL Studio selects the portable host described below.

SDL playback uses the decoder in `avi_movie.h/.cpp`, in `kauai-core`.
It reads AVI 1.0 with indexed Microsoft Video 1 and optional 8/16-bit PCM audio,
covering all 24 shipped clips without an installed codec. Palette updates and
delta frames are decoded into top-down BGRX32; backward seeks replay the indexed
reference frame. Timing preserves native integer truncation, negative-time
errors and end-of-stream clamping. PCM bytes and their format are retained for
the SDL audio adapter. Input is owned and bounded to 256 MiB, 256K frames and
16M pixels; compressed packets are validated when decoded. Unsupported codecs
are rejected. `avi-movie-test` checks malformed inputs and synthetic format
cases; `avi-corpus-win-test`, when original `content-files/*.avi` are available,
compares all 4,458 original frames pixel for pixel with VfW, including seeks,
timing and exact audio bytes. The core test runs on Linux and cross-compiles for
macOS.

`video_sdl.h/.cpp`, in `kauai-sdl-video`, owns playback and SDL audio streams
without a window or graphics SDK. `video_stream_sdl.cpp` connects that player to
the existing video representations. The original window-mode flag enables audio;
silent stream representations remain silent. Videos now draw into Studio's
canvas through `DrawVideoFrame`, inheriting integer presentation and ordinary
clipping/occlusion. A paired true-colour redraw request preserves their palette
colours through the application's temporary drawing buffer; releasing the final
video restores indexed redraws. Playback no longer creates a native child window.
The original scripts still query frame position, stop, seek and resume playback.
Audio consumption provides the shared clock, with one device buffer allowed for
output latency and tail draining. Silent playback uses SDL's monotonic clock.
The final frame retains its complete duration. The existing master-volume
control supplies per-stream SDL gain; it does not change system mixer settings.

`video-sdl-test` verifies real U8/S16 SDL dummy-device playback, audible postmix,
audio-clock progression, pause/restart, seeking, mute, failed reopen and final
frame duration. `video-sdl-host-test` checks cached frame pixels through the
actual graphics environment without native mirrors/contexts, paired redraw-depth
changes and file-handle release. The player can be built independently of Studio
with `BUILD_SDL_VIDEO=ON`, including on Linux; the complete Studio GUI still has
other Windows dependencies. Mac compilation of the playback source is not a Mac
runtime test.

Add `--sdl-video` to the Studio smoke check to run the original Movie Techniques
entry script, play the first project AVI to completion, require visible SDL
draws and audible output, and exit through the original project script. The
test saves playing/advanced/completed screenshots and `video-state.json`.
Original clips must be present in the product content directory. If available,
`STUDIO_AVI_SOURCE_DIR` (default `content-files`) supplies `*.avi` to the local
install step; an empty value skips them. SDL startup recognizes a complete
content tree supplied as the working directory before searching system installs,
so available local clips do not incorrectly trigger a request for another CD.

The bridge flushes queued GDI drawing before CPU pixel access. SDL creates and owns the resizable Studio
window and fits the canvas to the available area without changing the display's
resolution or the canvas's 4:3 aspect ratio. The Product Info dialog's **Integer scaling**
option enables whole-number scaling in windowed, maximized and fullscreen modes. Nearest-neighbor
sampling is used in both modes; a 640x480 minimum client area prevents windowed
downscaling.
The host and smoke checks verify the actual presentation rectangle after
resizing and switching fullscreen modes. SDL's own window procedure now stays
installed: Kauai no longer subclasses the SDL main window or forwards its native
messages into the legacy application window procedure. Exposure and close
requests reach Kauai through SDL events, with logical damage driving the existing
renderer. Default FreeType startup no longer borrows a GDI screen DC. The
explicit GDI font option retains a borrowed context and releases it at shutdown.
`window-lifecycle-sdl-test` verifies both provider paths, the installed procedure,
OS paint/close event translation, repeated creation and idempotent cleanup using
the actual Windows SDL driver. Legacy Windows-message
automation remains a native-build interface; the SDL smoke test reads actor
counts through MCP's `get_state.scene_actor_count` instead.

Window titles, activation of the SDL root, logical client bounds and desktop
coordinate conversion now use the SDL host. Canvas/desktop mapping includes
the actual presentation viewport, integer scale and display density. The
integer gob API chooses a containing pixel consistently when an odd letterbox
places an edge at half a window pixel, preserving nested point/rectangle round
trips. Failed or overflowing rectangle conversions preserve the entire input.
The retained Windows global-hit helper still asks Windows which native window
is under a desktop point, then uses SDL to convert points inside Studio; its
letterbox area does not hit Studio controls.

Window maximize/fullscreen properties and Studio's pre-dialog restoration now
use SDL window services. The host reports actual window-manager state rather
than assuming a request has already completed. Bringing a minimized Studio
forward restores it; bringing a maximized window forward preserves its size.
`window-lifecycle-sdl` checks the framework properties against the Windows
window-manager state, including host-driven fullscreen changes. The standalone
window-services test also covers hidden/closed windows and state-query failure.
The portfolio smoke checks minimize their own Studio process before opening
both original open/save requests and require Studio to be restored.

`appb.h` now compiles without Windows headers too. Native runtime globals live
in private `appb_native_win.h`; callback aliases retain the original native ABI.
Both SDL and native header checks compile on Windows, Linux and macOS, with
Windows interop checks for callback signatures.

`gob.h` is now an SDK-free public header alongside `gfx.h`. Window identities
and native invalidation rectangle pointers use opaque `CoreNative` interop
types. Native invalidation implementations live in `gobwin.cpp`; SDL bridge
headers are included by implementations rather than the public object header.
`gob-api-check` compiles the header first on Windows, Linux and macOS without
importing a window SDK. This isolates declarations; the GUI implementation still
contains native window/drawing services.

`window-services-sdl-test` checks UTF-8 titles, independent viewport/desktop
geometry, inverse mappings, invalid input and restart through SDL's dummy
backend. `window-geometry-sdl-test` uses the real Windows driver to check nested
gobs, local/parent/port/window/global conversions, overflow, actual global hits,
letterbox rejection and legacy title conversion. The host can now be built and
tested independently with `BUILD_SDL_HOST=ON`, including Linux. The Linux test
runs the same window-service implementation without the Windows bridge; Mac
host compilation is not a complete Mac application runtime check.

SDL now owns external text
clipboard access and clipboard-change notifications; the SDL window does not
join the Windows clipboard-viewer chain. Kauai retains its original rich
internal clipboard document when SDL reports a change owned by this process.
External UTF-8 text is converted to the existing text encoding, with LF/CRLF
paragraph endings normalized to CR. The ANSI build retains its existing code
page conversion behavior for characters it cannot represent. Empty external
text clears the internal clipboard. No new movie encoding is introduced.

Cursor resources now pass through SDK-free `cursor_data.h` decoding and SDL
host cursor IDs. The eight-byte GGCR fixed record and original MSB-first AND/XOR
planes are unchanged. The decoder selects valid 16/32-pixel entries, checks
hotspots and handles byte order. Studio keeps its original 32-pixel cursor
canvas, padding a 16-pixel resource with transparent pixels. Custom cursors are
composited into a temporary presentation buffer using the original Boolean
pixel operations; the borrowed Studio framebuffer remains untouched. This
preserves inversion that the pinned SDL hardware cursor path lost in a native
Windows comparison. Pointer coordinates and hotspots follow the logical Studio
canvas as its window scales. Letterbox areas retain an arrow, while arrow/wait
selection, visibility and global pointer positioning use SDL host services.
Host-owned cursor IDs cannot be reused by stale resources after a host restart.
This is a Windows bridge, not a portable Studio application yet.

`pixel_buffer.h` owns zero-initialized top-down packed storage for 1/4/8/16/24/32
bits with the original four-byte row alignment. Allocation and layout validation
use checked sizes before the Windows mirror is created; failed initialization
leaves previous pixels and metadata unchanged. `GraphicsPort` presents the
portable pointer to Studio, BRender and SDL. Native fallback draws explicitly
upload the current CPU bytes and copy completed drawing back; native copies also
upload an owned source before GDI reads it. SDL ports in all six packed formats
now start without a Windows bitmap mirror or device context, provided indexed
4/8-bit ports have an application palette.
Monochrome uses its original black/white table; truecolor keeps RGB555, BGR24
and BGRX32 layouts. Portable drawing, supported packed copies and pixel access
leave native resources unallocated.
Native font measurement requests a context alone.
The first native draw or native source read creates and synchronizes the mirror,
then restores clipping before drawing. Private palette changes, partial updates,
global palette changes and returning to the global palette update the CPU table
without creating native resources. Deferred contexts preserve the logical
palette selection from port creation; their identity palette is also created
on demand. Indexed 4/8-bit ports created before an application palette still use
Windows to obtain the device palette and retain eager mirrors and memory DCs.
This removes unnecessary bitmap/DC allocations
while retaining the remaining native rasterizer fallbacks.

`pixel-buffer-test` checks packed pitches, row padding, zero initialization,
alignment, rejected/oversized layouts, independent storage and lifetime counts
without any UI or SDK headers. `pixel-storage-gdi-test` runs 60 comparisons
across all six packed formats, mixing edits through a locked CPU pointer with
rectangles, patterned/framed ovals, metafile pictures, scrolling, self-copies,
native exports and copies back into CPU ownership. It checks that portable
8/32-bit drawing leaves the mirror and context absent, metafile playback creates
them, and subsequent portable edits survive another metafile draw. Creation,
zeroed pixels, palette bookkeeping and destruction remain native-free across
all six formats. Packed-copy comparisons also require every source format
to reach a 32-bit destination without creating native resources. `font-gdi-test` checks that native font measurement creates a context
without creating a framebuffer mirror and retains native measurement parity.
The storage test compares raw bytes and padding; the unused X byte in 32-bit
XRGB is excluded. Existing pixel/font/bitmap comparisons also run with portable
ownership enabled.

Eight palette-transition fixtures cover 4/8-bit ports and compare both SDL and
native offscreen ports against a native DIB with the same identity palette. They cover full and partial
private tables, global palette changes and releasing a private palette, before
and after creating a native mirror. The Debug MCP state also reports lifetime
8-bit port, native-context and native-mirror creation counts, so transient native
allocations remain visible after their surfaces have been released.

`palette_state.h/.cpp` implements the original truecolor-display palette policy
in `kauai-core`, without SDK headers, a window or a device context. It preserves
the first/last ten reserved colors, the legacy red-component collision adjustment,
retained entries and animation count/empty-input rules. SDL's `SetActiveColors`
uses this state; the native build retains the original Windows implementation.
Palette updates do not change movie data or serialized palettes.
`palette-state-test` checks known colors, collisions, animation, aliases and
invalid inputs. `palette-gdi-test` compares 2,408 sequential updates across all
flag combinations and lengths 0..300 against the original native implementation
on a truecolor Windows display. In the SDL build the same adapter test starts
with no screen DC. The pixel-storage comparison also performs its native drawing
fallbacks with no borrowed screen DC in the SDL build. The portable state test
runs on Linux and cross-compiles for macOS.

The storage comparison also checks 8,820 indexed/RGB scroll combinations:
7,350 canonical/empty rectangle cases through portable scrolling and 1,470
legacy inverted-rectangle fallback cases. These cover source/destination clips,
holes, disjoint and empty regions, directions, large shifts, offscreen bounds,
padding and unchanged exposed pixels. The headless canvas test checks known
indexed/RGB results, source clipping, holes, overlap, guards, invalid input and
extreme signed deltas independently. `scroll-sdl-test` exercises the actual
Kauai control/window route, verifying framebuffer pixels, exposed rectangles,
repaint modes, exact pending-damage translation and invalidation raised inside
a draw callback. Its SDL Windows-driver window stays hidden; it does not move
the pointer or open user files. `damage-map-test` makes 3,000 comparisons against
an independent byte-grid oracle, covering word boundaries, clipping, snapshots,
scrolling with extreme deltas and repaint retention. The SDK-free damage library
also builds independently of the Studio host.

Verified interactions include the original scene browser, loading a background,
F1 help, closing help, selecting and placing an actor, creating a story textbox,
entering text, changing its insertion font through the original picker, and the
normal save/discard prompt. The standalone host
test covers framebuffer presentation, capture, queued pointer/key/text state, text
buffer ownership, input
flushing and restarting with SDL's dummy/software drivers. These checks do not
establish complete editing, playback or audio compatibility.

`clipboard-sdl-test` runs the actual Kauai clipboard object and rich-text
documents against SDL's isolated dummy-driver clipboard. It verifies text
export, retention of the original rich document, external replacement
notifications, accented text, LF/CRLF paragraphs, complete imports longer than
1 KB and empty replacements. It does not overwrite the desktop clipboard or
claim verification of cross-application exchange on macOS. The standalone host
test also covers UTF-8 clipboard text, update ownership, empty text and rejection
of embedded NULs. `text_utf8.h` supplies strict counted UTF-8/UTF-16 conversion
for clipboard and font-name callers without SDK, SDL or font-provider headers.

`cursor-data-test` checks the original cursor planes, entry selection, byte
order, hotspots, malformed candidates and fixed-record widths without SDL or
native cursor APIs. `cursor-sdl-test` compares SDL's actual captured output
against GDI `CreateCursor`/`DrawIconEx` for 16/32-pixel images containing black,
white, transparent and inverted pixels, including hotspot placement, visibility,
resource destruction and host restart. It runs with SDL's dummy/software drivers
without moving the desktop pointer. Host tests also check clipped software
cursor presentation, unchanged framebuffer pixels, cursor IDs and restart;
the clipboard integration test covers nested Studio hide/show calls.
Normal captures exclude the cursor, matching the earlier hardware-cursor
screenshots; `Host::SaveBMP(path, true)` includes it for renderer comparisons.

Repeat the interactive check with the original English content (it creates and
discards a fresh movie and writes screenshots for review):

```console
python tests/studio_sdl_smoke.py build/studio-sdl-x64/3dmovie-sdl.exe "dist/Microsoft Kids/3D Movie Maker" build/studio-sdl-smoke
```

This check uses the Debug MCP interface and SDL's queued input. It requires a
desktop session, so it is kept separate from the headless host test.
Add `--resize` to repeat the workflow in a wider, scaled window and verify that
the same logical clicks still reach the original controls.
Add `--fullscreen` to switch into and out of fullscreen using the original
Product Info dialog, checking that the desktop resolution stays unchanged.
The test leaves the dialog's default-settings checkbox unchecked.
Add `--actor` to open the original actor browser, select its first thumbnail and
place an actor on the stage through SDL input. The test uses Studio's existing
read-only actor query to check the movie changed, then discards the test movie.
Add `--text` to create a story textbox through the original Words controls,
enter text through SDL, choose another family in the original font picker, and
verify the next inserted character's font in the movie document. This also uses
only the test's new unsaved movie.
Add `--portable-fonts` to require FreeType faces and zero recorded native font
faces, fallback creations, drawing calls, measurement calls, native font
enumerations or bootstraps through the save query.
Add `--sdl-cursors` to require the workflow to create and select Studio's original
cursor resources through the SDL host, as reported by Debug MCP state counters.
Add `--portable-pixels` to require live portable pixel buffers holding at least
the original 640x480 XRGB framebuffer through the save/discard query, with no
native bitmap mirror or device context allocated for that framebuffer during
the tested workflow. It also requires indexed surfaces to be exercised without
creating any 8-bit native contexts or mirrors, including transient ones, and
checks that Studio has no borrowed native screen context.
Use `--baseline PATH` to require identical PNG captures from an earlier run with
the same encoder. This is a strict diagnostic, not yet a deterministic image
regression gate: repeated runs of an unchanged GDI reference path produced two
loaded-scene captures differing in 12 highlight pixels. Other captured screens
matched. The individual portable drawing operations are checked separately
against GDI below. Stabilizing scene palette/timing state remains work to do;
do not normalize away those differences in a compatibility check.

`pixel-canvas-test` tests the drawing module without a window, SDL or GDI.
Its packed-copy fixtures check encoded bytes, RGB555 masks, monochrome color
tables, adjacent bits/nibbles, row padding, clipping, region holes, overlap and
transactional rejection. `packed-copy-gdi-test` adds 55,902 exact native comparisons
across all six formats, two palettes per source/destination, source crops,
negative destination offsets, clipped regions and overlapping self-copies. It
checks the core and, in SDL builds, Studio's offscreen-port adapter. Another
576 cases check the retained native palette fallback's clipping/coverage and
the core's rejection without writes. Those cases use an exact indexed coverage
oracle; color equality between independently created native inverse palettes
is not asserted because the reference was unstable. Portable replacement of
that color policy remains unfinished.
`pixel-canvas-gdi-test` compares its output against the original GDI path for
48 rectangle pattern/color/frame/clip combinations, 414 polygon cases,
88 thick-line cases through Studio's actual offscreen-port adapter, eight RGB/indexed
stretches and 24 masked bitmap stretches into 8-bit/32-bit targets. The masked
comparisons explicitly select GDI's `COLORONCOLOR` nearest-neighbor mode;
its default `BLACKONWHITE` mode combines mask pixels when shrinking. The
portable tests also run under Linux in QEMU and cross-build
for ARM64 macOS; macOS execution remains unverified.

`pixel-canvas-indexed-gdi-test` compares 2,048 indexed drawing cases through the
actual Studio offscreen-port adapter against a DC-only GDI reference: four
active/private palettes (including duplicate colors), eight foreground/background ink choices, solid/patterned
rectangles, frames, polygons, thick lines and highlights. It checks raw palette
indices, complex clipping and unchanged row padding. The SDK-free canvas test
also covers indexed shapes and palette-dependent brush operations directly.

`masked-transfer-gdi-test` compares 11,160 private-palette masked bitmap draws
against the original DC-only implementation. It covers full and short source
palettes, duplicate colors, five destination palettes, literal and solid masks,
near-size copies, enlargement and reduction, both `BLACKONWHITE` and
`COLORONCOLOR` state, complex clipping and untouched row padding. It checks raw
indices, including legacy cases where transparent source cells alter the
destination through the palette-dependent Boolean operations. AND reduction
consumes source pixels through each nearest sample, leaving the final tail
unused; when both dimensions differ by at most one pixel, GDI copies samples
directly and repeats the last on enlargement. The SDK-free bitmap tests cover
these policies, transparent rows and malformed input without GDI. No movie
format or writer changes are involved.

`indexed-copy-gdi-test` compares 9,990 indexed bitmap copies through Studio's
offscreen-port adapter against the DC-only reference. It covers three source
and five destination palettes, duplicate colors, near-size copies, enlargement,
reduction, source crops and samples outside source bounds, offscreen targets,
complex clipping, row padding, and overlapping self-copies. The headless canvas
test also checks short palettes, reserved color bytes, partially overlapping
storage with different starting addresses, and rejected invalid input.

Filled and framed ovals on indexed and XRGB surfaces now use the portable
canvas. Their coverage preserves the original distinction: fills correspond
to `Ellipse` with right/bottom expanded by one, while frames XOR two unexpanded
elliptic regions. Small degenerate shapes retain native behavior: a 2 by 2
filled oval is empty, and a 3 by 3 fill has spans [1,2), [0,3), [1,2).

The SDK-free `ellipse_geometry.cpp` constructs fixed-point cubic outlines,
uses the native small/large-bound subdivision policies and scan-converts only
the requested rows. Invalid/oversized geometry is rejected before drawing.
The fixed-point subdivision is adapted from Apache-2.0 licensed
[emf-converter](https://github.com/christophervr/emf-converter); its pinned
source and modifications are recorded in
[`ellipse-NOTICE.txt`](../licenses/ellipse-NOTICE.txt). The license and notice
are installed with the libraries.

`ellipse-geometry-gdi-test` makes 16,808 native comparisons: every width/height
from 1 through 128, narrow and large bounds through 8192, and the precision
transition. `pixel-canvas-gdi-test` checks RGB oval brushes, clipped regions,
frames and the actual offscreen adapter. `pixel-canvas-indexed-gdi-test` checks
3,840 rectangle/oval/polygon/line/highlight combinations over four palettes,
including transparent and invert patterns, zero-width and oversized pens,
complex clipping and row padding. Headless checks include known small shapes,
clipped row ranges, holes, guard rows and rejected extents. The pure geometry
and canvas checks also build for Linux/macOS; macOS execution remains unverified.
Other pixel formats and DC-only native ports retain their original drawing path.

`font-services-test` exercises layout and provider-independent glyph blending
without SDK headers, plus strict UTF-8/UTF-16 font-name conversion and rejection
of lossy legacy conversion. `font-list-test` initializes Studio's actual font
list without a device context, checking sorting, uniqueness, name/index mapping,
fixed-pitch metadata and the existing missing-font fallback to System.
`font-host-test` links the font service without Studio or the GDI adapter. It
checks recursive discovery and depth limits, bitmap and outline System policies,
ANSI/UTF-16 requests, rendering, failed reconfiguration and the platform's default
discovery entry point. It runs under Linux in QEMU and cross-builds for macOS.
`font-gdi-test` compares 2,160 indexed and 2,160 RGB text
draws against the original GDI path in the SDL build: three font families,
styles including synthetic italics and boxes, all alignments, two ink colors,
three background modes, clipping and both active/private palettes. It also
checks ANSI/UTF-16 measurement and independent font-handle lifetimes. The GDI
comparison targets retain a DC-only reference port so the reference does not
accidentally select the new font adapter.
With FreeType enabled it additionally compares 168 file-selected cell metrics
against GDI: seven families, six pixel sizes and four styles. These checks
require identical cell height, ascent and external leading without native font
bootstrap calls; glyph rasterization is not claimed to be universally identical.

`font-freetype-test` checks outline and Windows bitmap fonts, legacy-byte and
UTF-16 text, styles, coverage, painting, metadata discovery, family/style
selection, bitmap enlargement, fonts without Windows OS/2 tables and invalid input without Windows SDK
headers. These font tests also pass under Linux in QEMU and cross-build for
ARM64 macOS; macOS execution remains unverified. Tests use local font fixtures;
fonts are not redistributed. `KAUAI_FONT_TEST_COLLECTION` optionally adds a local
multi-face collection fixture (Cambria on Windows when installed). Catalog
matching folds ASCII case in UTF-8 family names; localized aliases and full
Unicode case folding remain future work. FreeType
is pinned in `cmake/FreeTypeDependency.cmake` and used under the
[FreeType License](../licenses/freetype-FTL.txt).
Portions of this software are copyright © 2026 The FreeType Project
(https://freetype.org). All rights reserved.

## Remaining work

Dialog resource decoding and logical control grouping now live in the SDK-free
`dialog_resource.h/.cpp` module. It reads the original standard and version-1
extended templates, retaining UTF-16 captions, class/resource ordinals, dialog
units, styles, help IDs, font metadata and creation payloads. It rejects truncated
input transactionally. The parser follows verified native behavior: creation-data
counts describe the following payload in both formats. Microsoft's
[standard item description](https://learn.microsoft.com/en-us/windows/win32/api/winuser/ns-winuser-dlgitemtemplate)
says to include the count word, while its
[extended item description](https://learn.microsoft.com/en-us/windows/win32/dlgbox/dlgitemtemplateex)
excludes it. A two-control fixture verifies actual User32 acceptance and the next
control's alignment, independently of our parser. This matches the old loader's
stepping rule.
The original radio-group/item-index rules remain shared with the native loader.

`dlg.h` and the common value-handling code no longer include native SDK headers
or expose a Windows callback signature. Dialog IDs, values and callback item
indices use explicit 32-bit legacy types, preserving the Windows ABI. The native
adapter still loads executable resources. In SDL Studio, `dlgsdl.cpp` runs the
original templates and callbacks through `kauai-sdl-dialog`, an SDL3/ImGui modal
host. The session model separates active control values from stored `Dialog`
values, preserving explicit `SetValues`/`FGetValues`, callback vetoes and result
overrides. Text passes through the existing legacy encoding boundary; movie
writers and stored strings are unchanged. Supported controls include buttons,
checkboxes, radio groups, text edits and combo boxes. Unsupported templates use
the existing native backend. The compiled Studio inventory supports 15 of 16
templates. Custom portfolio template 223 is retained for native builds; SDL
Studio uses a dedicated portfolio view instead of that Windows template.

Standard `ApplicationBase::TGiveAlertSz` alerts also use this SDL/ImGui host.
Their SDK-free session builder preserves OK, OK/Cancel, Yes/No and Yes/No/Cancel
choices and the original `tYes`/`tNo`/`tMaybe` answers. Escape accepts an OK-only
alert, cancels choices with Cancel, and leaves a Yes/No alert open. Initialization
or rendering failure returns cancellation. Legacy message text is converted at
the host boundary, including literal ampersands; severity appears in the window
title. Native builds retain their existing Windows message boxes. Main-thread
assertions use an SDL/ImGui window with the original Ignore/Debugger/Quit choices,
diagnostic text, stack addresses and crash logging. Enter selects Ignore; Escape
and window close select Quit, matching the original dialog. Worker-thread,
startup and frontend-failure assertions retain native fallback UI.
`app-assert-sdl` exercises the real assertion dispatch and modal cleanup; portable
model and frontend tests cover all three answers. Old Windows installation
checks still use native UI.

Dialogs use separate modal windows, preserving the main Studio canvas's integer
scaling. Oversized resource layouts fit within the display's usable area and
scroll without shrinking their controls. Mnemonic markers are removed from
captions, but Alt-key mnemonic activation is not implemented yet.

`BUILD_SDL_DIALOGS=ON` builds the frontend independently of Windows Studio:

```console
cmake -S . -B build/dialogs -DBUILD_STUDIO=OFF -DBUILD_PACKAGES=OFF -DBUILD_SDL_DIALOGS=ON
cmake --build build/dialogs --target dialog-sdl-test
ctest --test-dir build/dialogs -R dialog-sdl --output-on-failure
```

Studio and the inspector share pinned Dear ImGui v1.92.5 through
`cmake/ImGuiDependency.cmake`; its MIT license is installed with the tools.
`dialog-sdl-test` exercises actual SDL pointer, text, keyboard and wheel events,
including off-screen controls, disabled/read-only controls and session cleanup.
`dialog-controller-sdl-test` runs the original `Dialog` class and callbacks through
that host and checks the original application alert API using actual queued SDL
input. `dialog-model-test` also checks alert result/cancellation policies and
transactional invalid text rejection without any UI. The standalone frontend
also runs under Linux with SDL's dummy video
driver. The macOS frontend object cross-compiles; a macOS application has not
been linked or run. The full Studio smoke test's `--fullscreen` path requires
the SDL Product Info dialog and saves screenshots of both window modes.

`dialog-api-check` compiles the header and common implementation without SDK
headers on Windows, Linux and ARM64 macOS. Shared logical-item initialization
also lives in `dlg.cpp`, so either host can load a template and initialize the
same value model without duplicating allocation or item-index rules.
`dialog-model-test` runs that production model with a resource-only test host;
native control access aborts. It checks four-byte checkbox/radio values, both
signed 32-bit numeric limits, empty/invalid edits and combo text/list preservation,
and runs without SDL, a window or native dialog services, including under Linux.
Those limit checks exposed signed overflow in the shared string formatter and
number reader. They now use defined unsigned arithmetic for magnitude/wrapping;
the existing 32-bit results, sign/base handling and permissive digit rule are
pinned by `string-compat-test`. Stored string encodings and movie writers are
unchanged.
`dialog-resource-test` checks standard
and extended layouts, Unicode code units, wide IDs, fonts, ordinal titles,
creation data, grouping and 3,288 truncation cases. `dialog-resource-win-test`
checks resource-compiler fixtures against hidden Windows dialogs and the actual
Kauai item/value adapter. An optional executable argument inventories Studio's
compiled dialogs without running that executable, pinning Product Info's callback
indices independently of the parser. The current Debug Studio inventory is 16
dialogs, 67 controls and 33 logical items.

```console
build/studio-sdl-x64/dialog-resource-win-test.exe build/studio-sdl-x64/3dmovie-sdl.exe
```

WAVE playback now uses an SDL device behind Kauai's existing `SoundManagerQueue`
and `SoundQueue` interfaces. The x64 Studio previously selected the nonworking
AudioMan stub. SDL reads the original WAVE resource bytes, including Microsoft
ADPCM, and converts them to 48 kHz stereo floats for playback. Stored sounds and
movie writers are unchanged. Existing queue IDs, classes, stop/pause/resume,
start offsets, repeat counts, activation and grouped starts remain on the
Kauai interface.

`kauai-audio-mixer` is an SDK-free library with deterministic sample rendering.
It plays each queue sequentially, mixes queues concurrently and clips the final
sum. Its audio-thread rendering allocates no memory and calls no Kauai code;
cache loading and queue updates happen on the main thread. Completed playback
references are released when the main-thread queue flushes. `audio-mixer-test` covers
exact samples, seamless transitions/loops, offsets, gain, pause/suspension,
grouped starts, invalid updates and cleanup. It also cross-builds for Linux and
ARM64 macOS. `audio-sdl-test` checks exact PCM/MS ADPCM decoding and the real SDL
callback through its dummy audio device; `sound-sdl-test` exercises a chunky
WAVE resource through the actual cache and device adapter, including nested
pauses and class filtering.
The queue adapter itself uses only `kauai-core` sound/cache interfaces;
`sdl-sound-boundary-check` compiles it without either SDK's headers on Windows,
Linux and macOS. Decoder checks also cover 22.05 kHz unsigned PCM conversion
to the mixer's sample rate and channel layout.

Add `--sdl-audio` to the Studio smoke check to require original-content WAVE
sounds decoded, started and completed through SDL. MCP state reports these
counters and whether the SDL audio device opened. Add `--sdl-midi` to preview
"funny moment 1" in the original music browser and require MIDI note events,
nonzero synthesized PCM and returned buffers when the browser closes.

`midi_stream_host.h` now defines the SDK-free stream-device boundary: fixed
12-byte event records, volume/activation, borrowed queued buffers, offsets,
repeats, stop and pointer-sized completion values. The existing priority mixer
and cache use that contract; `midi_stream_win.cpp/.h` owns the WinMM handles,
native callbacks, device volume handling and stream/scheduled output backends.
The native Studio retains its original scheduled WinMM output path. SDL Studio
uses `midi_sdl.cpp` by default; `KAUAI_MIDI_PROVIDER=winmm` selects the original
Windows device adapter for comparison. `mididev2.cpp` lives in `kauai-core`: its cache, queues,
priority selection, seeking and expiration handling import no GUI or SDK headers.
The player accepts a device factory; its existing default is supplied at the
Windows call site. Headless clients can inject a host without linking WinMM.
`midi_worker.cpp` uses a portable timed worker and coalescing wake event. Stop
waits for the actual buffer-return notification while releasing the mixer lock,
replacing the unsynchronized `Sleep(0)` polling loop.
Native buffers use the SDK's `MIDIHDR`, replacing a handwritten layout that
contained 32-bit pointer fields on Win64. This changes in-memory native adapter
state, not MIDI chunks or movie data.

`midi-stream-api-check` compiles the host contract without SDK headers on Windows,
Linux and macOS. The native interop check pins event offsets/kinds against the
SDK. `midi-stream-host-test` runs the actual MIDI mapper with tempo/no-op events:
it checks FIFO callbacks, repeats, initial offsets, stop/return, relative volume,
full-width callback values and native header preparation without sending notes.
It runs in both Win32 native and Win64 SDL builds, and reports a CTest skip when
the host has no MIDI output device. `midi-worker-test` covers event coalescing,
single-waiter wakes, timer expiration, signals during work and joined shutdown.
`midi-mixer-test` links only `kauai-core` and checks raw cached event layout,
factory injection, priority/tie-break, background expiry, preemption, seek/resume
and asynchronous stop with an observed test host. Both tests run on Linux and
cross-compile for macOS.

The portable `kauai-midi-synth` library renders the same fixed MIDI event records
through [TinySoundFont](https://github.com/schellingb/TinySoundFont), pinned to
commit `853a0a171759f1ddba0de1442133a75912bbeffa`. It supports GM programs,
channel-10 percussion, velocity/note-off, controllers, sustain and pitch wheel.
The frame-clock sequencer handles tempos, initial offsets, finite/infinite
repeats and fractional-frame carry. Its audio render path allocates no memory
and calls no framework code. SDL queues the PCM; a separate worker delivers
borrowed-buffer completions outside the audio/renderer locks. Completion of a
zero-time seek prefix preserves its held notes for the following timed buffer.
Reopening the device restores channel defaults before the mixer replays seek
state. MIDI volume affects this stream rather than the Windows system volume.

SDL builds download a SHA-256-pinned GeneralUser GS SoftSynth 1.44 bank
(31,279,314 bytes) into `assets/GeneralUserGS.sf2`. This is the legacy version
the [author recommends for synths without SoundFont modulators](https://www.schristiancollins.com/generaluser).
The build and install copy the bank and its license beside the executable,
including multi-configuration executable directories. `KAUAI_SOUNDFONT` can
select another complete GM SF2 file; `KAUAI_MIDI_BANK_FILE` selects a bank at
configure time. Loading requires every bank-zero GM program and the standard
percussion kit, so a partial bank cannot silently replace missing instruments
with piano. The original bank and movie/MIDI bytes are not rewritten.

This changes instrument timbres from the Windows synth. TinySoundFont does not
implement SoundFont modulators, chorus or reverb; the selected SoftSynth bank
avoids the modulator requirements of newer GeneralUser releases. The MIT synth
license, GeneralUser license and a local signed-offset integration correction
are recorded under `docs/licenses/`. `KAUAI_MIDI_SYNTH=ON` builds synthesis
independently of Studio; `BUILD_SDL_MIDI=ON` additionally builds the portable
SDL output host (both are enabled for SDL Studio).

`midi-synth-test` exercises all 128 GM programs and 47 standard percussion notes,
controllers, sample boundaries, repeats, offsets, stop, seek-prefix retention,
fractional timing and rejection of a structurally valid bank missing a GM
instrument. `midi-sdl-test` plays real synthesized PCM through SDL's dummy audio
driver and checks callbacks, volume, stop/reopen and controller reset. Both run
on Linux; synthesis also cross-compiles for macOS. The Studio smoke test adds
original-content preview coverage. These checks do not compare timbre with
WinMM or substitute for listening on physical audio hardware.
The current SDL WAVE cache decodes entire clips into memory; streaming long
sounds remains work. The native Studio continues using its original backend.

WAVE import in the SDL Studio now decodes/resamples through SDL and writes
legacy-compatible RIFF bytes through the SDK-free `kauai-wave-codec` library.
Compatible source bytes are retained when conversion would not save space.
Otherwise imports use the original 11.025 kHz mono unsigned PCM format and,
for longer clips, Microsoft ADPCM with the standard seven coefficient pairs.
The existing MSND envelope, sound name, type, default volume and WAVE child
writer are retained; no movie record version changed. New encoded WAVE bytes
are repeatable, but this does not make the entire existing movie writer byte
deterministic.

Clips of up to 1,500 normalized samples remain PCM. Testing against the actual
1995 x86 AudioMan library exposed its short-ADPCM length bug: below four full
256-byte blocks, it can report fewer samples than the sound contains.
`wave-audioman-compat-test` reads all intended samples from 39 normalized
imports, covering that boundary and partial final blocks. The underlying
ADPCM encoder also accepts shorter clips; `wave-import-sdl-test` verifies
that SDL and Windows ACM produce identical decoded PCM for 28 encoded streams.
The SDL decoder honors nonzero compressed WAVE `fact` lengths so block padding
does not extend playback. It ignores zero counts and PCM `fact` chunks.
`movie-sound-import-test` calls the original engine import method, checks the
MSND metadata and child relationship, saves/reopens the archive, verifies the
WAVE bytes and plays the reloaded sound through the actual SDL queue adapter.

The original recording easel now uses SDL capture and preview through the
existing `SoundRecorder` API. Capture stores at most the requested duration in
a preallocated buffer; its callback never allocates application buffers or
calls Kauai. Stop and automatic completion process the clip on the main thread.
The SDK-free `kauai-recording-data` library removes the final eight frames,
corrects DC bias using the first 250 ms, and trims quiet ends with the original
threshold and margin rules. Its mono PCM8/16 output matches the original 1995
AudioMan bias/trim filters byte for byte on 50 test captures, including silence
and partial clips. One-based trim positions and sample-width wrap are retained;
captures of eight frames or fewer safely produce no sound. Stereo PCM8/16 is
also supported and checked, without claiming parity with AudioMan's stereo
filter behavior.

Preview uses SDL playback with the manager's volume, focus state and nested
suspension. Save writes a standard PCM RIFF WAVE through the existing file
service; the original easel accepts it through `FCopyWave` and the unchanged
MSND writer. `recording-sdl-test` checks real SDL callbacks using the dummy
device, four PCM layouts, limits, early stop, repeat takes and preview.
`movie-recording-test` checks the engine API, nested suspension, WAVE save,
MSND import and destruction during recording. The processing library also
cross-builds for Linux/macOS and runs under Linux QEMU. Real microphone quality,
device disconnection and macOS capture execution remain unverified.

Add `--sdl-recording` to the Studio smoke check to record, preview, name and
accept a take using the original easel. This option explicitly selects SDL's
dummy capture/playback backend for that test process and discards its fresh
movie on exit. The normal Studio uses the system's default recording device.
The SDL runtime no longer links AudioMan or Windows ACM, and excludes their
WAVE device/codec probes. Native Windows Studio retains its original backend;
Video and other host services still require Windows APIs.

SDL input now reaches the application through the SDK-free `host_event.h`
value record and an application-owned queue. `appbsdl.cpp` dispatches focus,
clipboard, pointer, close and keyboard events directly to Kauai controls and
commands. The SDL `EVT` type is no longer a Windows `MSG`; fetching, key
translation, mouse tracking and input flushing do not call Windows message
translation or dispatch. UTF-8 text uses the existing legacy encoding service,
preserving movie text bytes. Lookahead retains intervening events and each
key's modifier snapshot. Pending characters are destroyed with their app.

Studio shortcuts use `inc/studio_shortcuts.h`, retaining both the full command
set and the restricted global set while accelerators are disabled. The
portable shortcut test cross-builds for Linux/macOS. On Windows, passing the
Studio executable to `studio-shortcuts-test` compares all 4,096 key/modifier
combinations against the original `acidMain` and `acidGlobal` resources.
`events-sdl-test` covers conversion, malformed input, queue ordering, modifier
snapshots, both shortcut scopes, flushing and queue lifetime through the real
application adapter. The control integration test checks local hit testing,
click multiplicity and tick wrap. Windows supplies the initial double-click
interval through its private window initializer; other hosts default to 500 ms.
SDL hover hit testing also uses the logical control tree instead of desktop
coordinate conversion and `WindowFromPoint`.

SDL focus events now drive Kauai application activation, including sound-manager
activation and foreground mouse-tracking behavior. Losing focus clears cached
mouse-button and modifier state; repeated focus events do not repeat activation.
The remaining native modal loops temporarily take over activation while their
owner is disabled. Their queued owner-focus transitions are discarded when the
modal loop returns. `focus-sdl-test` checks the queued dispatch, state reset,
duplicate suppression and native-modal handoff with a hidden Windows-driver
window.

Actor placement and textbox movement now position the cursor through the SDL
host in logical canvas coordinates. Warping updates the cached pointer position
immediately, before the generated motion event arrives, and converts through
SDL's scaling/letterboxing. Textbox drag deltas use parent coordinates instead
of desktop coordinates. The Studio smoke check explicitly places the floating
textbox before typing. The native Studio retains its desktop cursor backend.

SDL also owns explicit mouse capture for Kauai tracking and window confinement
for actor placement. The host disables SDL's automatic button capture so that
tracking suspension can release capture even while a button remains held.
Focus loss, command-manager suspension and native modal
loops release the effective capture/grab while preserving the tracking request.
Ending tracking or burying the tracked control clears capture, confinement and
the local slow-repeat policy. This replaces `SetCapture` and `ClipCursor` in
the SDL Studio paths. The existing native Studio retains its Windows backend.

Placement's slow keyboard repeat is local to SDL event dispatch: repeated
keydowns wait at least one second and then at least 400 ms between accepted
repeats for each key. Associated repeated text follows the same policy;
explicit queued text remains independent. Key release, focus loss and input
flushing reset the repeat history. SDL placement no longer changes Windows'
desktop keyboard delay or speed. `input-sdl-test` checks these boundaries using
timestamped SDL events and an isolated dummy backend, plus pointer policy and
host restart. The control integration test covers capture suspension and
tracked-control removal. The Studio smoke test requires actual SDL capture/grab
while placing actors and props, release afterward, and unchanged desktop keyboard
preferences. On Windows, `--actor` also moves the desktop pointer without clicking,
checks each object's live body transform and visible frame, and verifies that a
stationary pointer causes no drift. These checks map through the SDL viewport,
including fitted presentation in a resized window. `actor-motion.json` and
`prop-motion.json` record the movement states.

Placement uses the original 50-pixel edge margin for cursor recentering. A legacy
change to 200 pixels inverted the safe rectangle in the 306-pixel-high stage,
causing continuous warps to the center that swallowed mouse movement. Restoring
50 fixes actor and prop placement without changing the original movement math.
The expanded smoke check passes with the fix and rejects the previous build
because its actor does not follow the pointer.

Studio portfolio requests now use the SDK-free `inc/portfolio_host.h` boundary.
`portf.cpp` coordinates Studio strings, sounds and result commands.
SDL builds select `portfolio_sdl.cpp`; native builds select `portfolio_win.cpp`.
The SDL host displays the original portfolio backdrop and button bitmaps, with
nearest texture sampling, folder/name fields, a filtered file list, Up/Home
navigation and movie/bitmap previews. `file_browser.h/.cpp` owns SDK-free UTF-8
navigation, sorting and wildcard filters over `platform_files`; it never changes
the process directory or creates/writes the selected file. Unrepresentable
Windows ANSI paths are rejected rather than silently choosing another file.
The Studio controller converts legacy strings at the boundary, plays the original
invocation sounds, preserves open/save cancellation behavior, and leaves actual
movie loading/writing to Studio's existing commands. Overwrite confirmation still
uses the original help topic, with a declined overwrite returning to the chooser.
Preview loading and save-name helpers are shared in
`inc/portfolio_data.h` / `src/engine/portfolio_data.cpp` on `engine-core`; the
Windows and SDL portfolios use that code. It reads the original first-scene `THUM`
bitmap, falls back to an 8-bit BMP when requested, and returns owned indexed
pixels for the host to display with Studio's palette. Sound previews remain
unimplemented, matching the original portfolio; its invocation sound is separate.
Failed preview probes preserve the caller's queued errors. Save-name helpers
retain an entered suffix when appending a different default extension and strip
only the last suffix for the overwrite dialog. Overflow now rejects the whole
suffix operation instead of passing a partially modified name onward.
`portfolio-data-test` verifies exact fixture pixels, BMP fallback, missing-file
probes, error-stack preservation and naming boundaries without a window or SDK.
The same test runs on Linux and cross-compiles for macOS.
`portf.h` no longer exposes HWNDs, native callbacks, window
procedures or `OPENFILENAME` types. `portfolio-api-check` compiles the public
request interface without Windows SDK headers, including in Linux/macOS builds.
The ImGui frontend lives in `kauai-sdl-dialog`, alongside the standard dialog
host. Their private modal implementation shares SDL event handling, parent
modality, clipboard notifications, input automation and cleanup. No SDL/ImGui
types enter the public portfolio request or core browser APIs.
`file-browser-test` checks navigation, filters, suffixes and read-only selection.
`file-dialog-sdl-test` sends actual SDL text, Enter and pointer events, then checks
rendered artwork and preview pixels in its screenshot.

Add `--portfolio` to the Studio smoke check to open and cancel the original
open/save portfolios through Studio commands, requiring SDL controls and the
original artwork. It chooses no file and writes no movie. Add `--portfolio-files
--text` to save and reopen fresh fixture movies only in the artifact directory.
That check retains a non-default filename suffix, declines and accepts overwrite
through the original modal topic, verifies a declined overwrite leaves bytes
unchanged, displays the saved movie's thumbnail, and opens a copied fixture
without changing its bytes. The copied fixture lets Studio perform a fresh open
instead of activating an already-open document. Screenshots include
`portfolio-open.bmp`, `portfolio-save.bmp` and `portfolio-preview.bmp`.

Portable dialogs use the classic gray controls, beveled borders, dotted focus
rectangles and compact dialog-unit layout. Text uses Studio's font service,
including the original MS Sans Serif bitmap face when available, with a
monochrome outline fallback on other hosts. The portfolio retains its original
artwork, pressed buttons and document icon. Its location dropdown lists the
host's common user folders, drive roots and the current folder's ancestors;
Back, Up, sortable dates and file-type filtering support ordinary navigation.

The separate `studio-movie-workflow` CTest exercises a generated two-scene movie
in the actual portable Studio. It requires the original content fixtures and
runtime archives, and runs with SDL's dummy video/audio drivers. Build
`library-checks`, then run:

```sh
ctest --test-dir build/studio-portable-x64 -R '^studio-movie-workflow$' --output-on-failure
```

The fixture includes camera cuts, skeletal animation and movement, two props,
3D text, costume changes and two embedded checkerboard textures. The test checks
live actor transforms and material pixels, rendered workspace images, backward
and repeated seeking, and natural playback through both scenes. It saves through
the portfolio, reopens in a fresh process, and compares both scenes' state and
images while requiring the source fixture to remain unchanged. Artifacts under
`movie-workflow/` include screenshots, runtime logs, saved movies and a JSON
coverage report. This verifies loading, rendering, playback and persistence of
an authored movie; the editing smoke test above covers separate UI authoring
workflows. Neither check establishes compatibility with every community movie
or expansion pack, or substitutes for running Studio on macOS/Linux.

`studio-building-workflow` checks the original map navigation into the ticket
booth, lobby and studio rooms. The startup palette contains bright green
placeholders in unused slots; each room must install its own palette before
its artwork is drawn. Room scripts request this through `SetGft`, which applies
the palette even while animated transitions are disabled. The test rejects
screens filled with the startup green and checks the ticket booth before,
during and after its introductory video. Screenshots and the report go in
`tmp/studio-building-workflow/`. Both workflow tests suppress Python bytecode
files in the source tree.

For unattended movie compatibility runs, configure a **Release** build with
`-DSTUDIO_TEST_TOOLS=ON` and build `studio` and `preview-scene-fixture`.
This explicitly enables the MCP test interface without enabling debug assertions.
Ordinary Release builds leave the test interface disabled.

The portable audio adapter decodes Microsoft GSM 6.10 WAV49 and MP3-in-WAV
using statically linked libgsm and dr_mp3, alongside SDL's PCM and ADPCM
support. SDL still resamples and plays all decoded sounds. The same decoder
path supports importing these files, normalizing imports to the existing
legacy PCM/ADPCM formats. Neither FFmpeg nor installed native codecs are
required at runtime. Decoder licenses are embedded in Studio and included
in the ISO cache. Dependency versions and integration details are recorded in
`docs/licenses/audio-codecs.txt`.

`compressed-wave-test` compares generated GSM and stereo MP3 fixtures with
independent reference PCM, checks sample counts, malformed inputs and legacy
RIFF padding, and exercises both playback conversion and import. Its fixtures
are generated tones; no community movie audio is checked into the repository.

```sh
python tools/batch_movie_playback.py tmp/studio-batch-release/3dmovie-sdl.exe movies tmp/movie-batch --iso dist/3DMOVIE100.iso --jobs 2 --timeout 120
ctest --test-dir tmp/studio-batch-release -R '^(engine-core-link|studio-batch-workflow)$' --output-on-failure
```

The batch runner recursively finds `.3mm` files, copies each into its own test
directory, and opens it in a separate process using the original ISO resources.
It drives ordinary scene playback without frame-clock waits, verifies every
frame in every scene in sequence, and renders scrolling text on every scroll
update. Scroll updates have their own counter because a caption can keep
scrolling while the movie remains on the same timeline frame. Click, timed and
sound waits are skipped and counted; sounds are still loaded with playback
volume muted. Scene transitions become immediate cuts. This tests frame and
caption coverage, rather than audiovisual timing or transition animations.
Actual throughput depends on scene complexity; there is no fixed 1,000 fps rate.

Each process uses dummy SDL video/audio drivers and exits on ordinary runtime
errors or missing assets instead of waiting for a dialog. The runner imposes a
wall-clock timeout and frame limit, records the last scene/frame attempted, and
distinguishes completed movies, playback failures, missing assets, crashes and
timeouts. It never saves a source movie and checks its hash after testing.
`tmp/movie-batch/latest.json` points to the aggregate report. Each movie has a
progress journal, runtime log, source copy, result JSON and, when available, a
final screenshot. `.vmm` packages are excluded; expansion-dependent movies may
fail against the base retail ISO.
Failure checkpoints retain the engine's zero-based `scene` and stored `frame`
coordinates. Use `scene_number` and `frame_number` to locate the failure in
Studio's UI. Scene frame ranges can start above or below 1; the displayed frame
number is the stored frame minus the scene's first stored frame plus 1.

`studio-batch-workflow` generates a two-scene regression movie from the ISO,
including signed frame ranges, scrolling credits and all three pause types.
It requires all 66 timeline frames, both scenes, scroll progress and two skipped
pauses of each type to complete in Release with assertions disabled. It complements
the ordinary-speed movie workflow's camera, actor, texture and save/reopen checks.

### Original ISO retail regression oracle

`tools/retail_oracle` is a separate Windows x86 test bridge for the original
`3DMOVIE.EXE` extracted from the retail ISO. It rejects any EXE whose SHA256 is
not `cc7c05cd4b63c0d1f2a0cee6fed86d85bd9f54e35952818f412f128425b2bca5`.
The launcher creates its own process, injects the DLL before application startup,
restores its temporary entry-point breakpoint and detaches the debugger. Neither
the EXE on disk nor an existing user process is patched.

Build from an MSVC x86 developer shell, separately from the x64 SDL build:

```powershell
cmake -S tools/retail_oracle -B tmp/retail-oracle/build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build tmp/retail-oracle/build
```

Python 3 with Pillow and 7-Zip are developer dependencies. Retail requires an
interactive Windows desktop; its captures do not need foreground focus or mouse
input. SDL runs with dummy video/audio drivers. The original retail EXE remains
32-bit and uses its original renderer and timer. This is not a headless retail
backend or an accelerated replacement for its playback engine.

Compare every frame of a scene against an SDL executable built with
`STUDIO_TEST_TOOLS=ON`:

```powershell
python tools/retail_oracle/compare.py --sdl tmp/studio-batch-release/3dmovie-sdl.exe --iso dist/3DMOVIE100.iso --bridge tmp/retail-oracle/build --movie 'movies/Litterbug 2.3mm' --output tmp/retail-comparison --scenes 41 --frames all
```

`--scenes all` visits every scene. `--frames START:END` selects an inclusive
range. Both arguments use one-based UI numbers, including for scenes whose
stored frame range starts elsewhere. The SDL `seek_movie` MCP tool is restricted
to `THREEDMM_BATCH_TEST=1`. It executes the real engine methods and returns their
success result, including ordinary Release-build failures.

Each run freezes one movie copy for both processes, hashes the input, ISO,
retail resources, SDL EXE and bridge DLL, and records successful resource opens
from the retail process. Resource opens must stay inside the extracted ISO
package. The package includes the installer-created `Microsoft Kids/Users`
directories. Native paths use backslashes because the original command-line
parser splits document paths itself. Keep test paths within retail's original
255-character limit and representable in the Windows ANSI code page.

Retail's application registry writes go into a private per-process HKCU branch.
Windowed rendering, better quality and textures are explicitly selected there.
SDL test settings use a separate `KAUAI_TEST_SETTINGS_RUN` branch, available only
in Debug or `STUDIO_TEST_TOOLS` builds. Teardown removes those private preferences
and terminates only the identity-checked process created by that session. The
integration test checks that installed-game preferences and source movies remain
unchanged.

The native DLL runs scene/frame methods, captures and command dispatch on the
retail UI thread. Its layout profile is separate from the refactored classes:
original booleans are 32-bit, actor coordinates are signed BRender 16.16 scalars,
and addresses are RVAs for the pinned EXE. State includes scene/frame bounds,
camera, actor/textbox counts, error depth and each actor's identity, visibility,
template, action, cel, evaluated frame, next event and position. A native exception
or UI timeout poisons the bridge; subsequent operations fail rather than treating
partially modified state as valid.

`latest.json` points to a report with per-frame native results, state differences,
viewport pixel counts and channel errors. Both full client screenshots are saved.
Pixel comparison excludes the surrounding Studio UI; the default viewport is
`45 99 594 407`, adjustable with `--viewport`. Visible actor positions use an
explicit relative tolerance of `1e-6` and absolute tolerance of `1e-4` world units.
Pixels require an exact match by default. `--max-different-pixels` changes only
that explicit pixel threshold. Exit codes are 0 for matched, 1 for differences,
including shared playback failures, and 2 for an infrastructure error. If both
engines reject a seek with matching compared state, the frame is recorded as
`shared_failure` and still returns a nonzero exit code. Its screenshot metrics
remain diagnostic, with `comparable: false`: partial failed scenes can repaint
at different times and do not establish a rendered-frame regression. A renderer
difference, native false return,
exception and timeout must not be reported as successful compatibility.

The MCP stdio frontend launches and manages the same isolated process:

```powershell
python tools/retail_oracle/server.py --iso dist/3DMOVIE100.iso --bridge tmp/retail-oracle/build --movie 'movies/Litterbug 2.3mm' --output tmp/retail-mcp
```

Supported tools are `get_state`, `seek_movie`, `screenshot`, `wait_ms` and
`send_command`. The latter queues an untargeted original Kauai command with
`cid` and `lw0` through `lw3`; tests must observe the resulting state to prove it
executed. Targeted HID dispatch, direct textbox/material inspection, dialog
automation, save/reopen and audio sample comparison are not yet implemented in
the retail profile. Scrolling-caption timing, recording and authoring therefore
need additional scenarios; this bridge is the foundation for those comparisons,
not evidence that all original workflows have already passed.

Run the opt-in integration workflow with the existing generated movie fixture:

```powershell
python tests/retail_oracle_workflow.py --sdl tmp/studio-batch-release/3dmovie-sdl.exe --iso dist/3DMOVIE100.iso --bridge tmp/retail-oracle/build --fixture tmp/studio-batch-release/preview-scene-fixture.exe --output tmp/retail-oracle-workflow
```

It compares all 60 frames across two scenes containing camera cuts, animated
actors, two props, 3D text, costume changes and embedded textures. It also checks
the actual MCP frontend, invalid scene handling, backward/repeated seeking,
native command-driven playback, captures, source preservation and preferences
isolation. State mismatches fail the workflow. Pixel differences remain visible
as failures in the separate comparison report; passing the bridge workflow does
not waive those renderer differences. The standalone bridge CMake project can
register this test with `RETAIL_ORACLE_INTEGRATION_TESTS=ON` and paths supplied
through `RETAIL_ORACLE_ISO`, `RETAIL_ORACLE_SDL` and `RETAIL_ORACLE_FIXTURE`.

The Litterbug 2 scene 41 failure was traced with a data breakpoint to a textured
triangle in frame 6. A projected X coordinate of -2 was read as unsigned 65534
when constructing the rasterizer's buffer offset, and the resulting writes
corrupted the movie's in-memory chunk index. Resolving the next sound cue then
failed at UI frame 7. The renderer now retains signed coordinates and clips
colour/depth spans to each viewport row, advancing all interpolants over skipped
pixels. `brender-clipped-edges` replays that captured triangle and checks 72
edge/winding/transparency cases against a larger translated reference render,
including colour/depth guard regions and row padding. Litterbug's complete
134 scenes, 3,179 frames and 1,992 scrolling updates now finish in Release.
All 12 frames of scene 41 succeed and match retail's compared actor/camera state;
the smaller pre-existing pixel differences remain recorded. The separate
Directors War Part 1 failure at scene 106, frame 2 is also reproduced by the
original ISO retail EXE, with matching compared actor/camera state. The movie
contains the referenced MSND 0x1c and 0x1f chunks, but scene 0xc3 has no MSND
children for the actor's sound references. This failure does not require an
expansion diagnosis: it is an unresolved embedded sound link shared with retail.
Keep retail's rejection behavior; do not silently repair or bypass it. The full
38-movie rerun completed 28 movies (86,679 frames and 16,293 scroll updates),
set aside nine cases with unavailable assets, and retained this one shared
retail failure. All tested source copies remained unchanged.

1. Remove remaining native window/control helpers from the Kauai runtime while
   retaining the SDL-owned main window lifecycle and existing Studio commands.
2. Extend portable drawing beneath `GraphicsPort` to
   legacy pictures, remaining packed-format drawing and the near-size
   true-color-to-indexed palette policy, then remove the DIB/DC mirrors
   around portable pixel ownership.
   Validate the font discovery/System policy against real macOS font files,
   including localized names and font activation.
   Preserve Kauai layout and hit testing.
3. Remove remaining native file-dialog helpers and worker/startup diagnostic UI;
   add streaming playback for long
   WAVE clips and exercise real microphone/device-failure behavior. Validate SDL clipboard
   exchange with native applications on each target, and remove
   remaining Windows startup and device assumptions.
4. Exercise original Studio workflows on Windows, then build and run that same
   Studio on macOS. Keep movie-format and legacy-fixture checks throughout.

The host interface is `kauai/src/sdl_host.h`; application events are defined in
`host_event.h`. `sdl_studio_win.cpp` bridges remaining Windows drawing and
canvas-damage calls. Neither SDL nor Windows SDK types belong in the public
archive/preview APIs.
