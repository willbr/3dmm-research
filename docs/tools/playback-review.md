# Playback Review

Native C89 + cimgui browser for playback regression results, initially targeting
Windows 11. Application and SQLite access code is C89; Dear ImGui, its generated
cimgui implementation and the small platform/backend adapter compile as C++.
The build is independent of Marcus and the original engine.

## Movie comparison

`image-compare` opens a **movie**, captures every authored frame in the
background, and displays synchronized **Retail**, **Ours**, and **Diff** contact
sheets. Choose a `.3mm` file or a ZIP containing one `.3mm` movie. PNG paths are
not part of the user workflow.

```powershell
cmake --build build/playback-review --target image-compare
build/playback-review/image-compare.exe movies/stranger1.zip
```

Launch without arguments and use **Open movie...**, or enter a movie path and
press **Render movie**. Progress shows completed frames for retail and all four
Marcus renderers. Captures arrive together in scene/frame order: the worker
retains each scene, evaluates one frame, and captures retail plus BRender CPU,
BRender compute, SDL GPU and CPU ASM x86 before proceeding to the next frame. This avoids
reopening scenes and rebuilding caption/asset caches before every frame.

- **Tab** cycles the Marcus renderer in Ours and Diff while Retail remains fixed.
  Radio buttons select a renderer directly. Switching preserves zoom and pan.
  The four choices are CPU C, compute, SDL GPU and CPU ASM x86. Existing sessions
  with only the original three renders still open; the ASM pane is unavailable
  until the movie is rendered again.
- Wheel over a pane to zoom all three around the pixel under the pointer.
- Drag with the left or middle button to pan all three together.
- Hold **D** to highlight changed RGB pixels in magenta on both original images;
  release it to restore them. This shortcut is inactive in text inputs.
- **Fit width**, **Fit all**, **1:1**, the zoom slider and **Columns** control the
  contact-sheet view. Double-click a pane to return to fit width.

Diff is the absolute native RGB difference, with black meaning equal RGB.
Headers and scene/frame labels are UI elements outside the captured pixels, so
neither appears in Diff or the D overlay. Scene/frame labels are one-based.
Sources are captured at 544x306, including retail's exact canvas at client
rectangle `48 100 592 406`. Nearest-neighbour sampling is explicitly selected
through the cimgui renderer. At magnification the viewer uses original frame
pixels; small overviews use reduced cached textures.

Movie rendering runs in owned child processes. A bounded decoder thread reads
frame files, computes differences and overlays, and prepares reduced surfaces.
The UI uploads at most one decoded frame set per iteration. The overview uses
persistent 64-frame texture atlases that stay valid across
zoom changes and are prefetched while idle. Wheel motion eases around its image
anchor, and native-resolution detail upgrades after the gesture settles. Cached
textures are trimmed toward a 256 MiB budget, retaining those used by the current
view. Retail launches with no activation and capture-only window hooks keep it
behind other windows and prevent its own show/focus calls from taking foreground
focus. It still uses its original
renderer and an interactive desktop; no minimized-window rendering is assumed.

**Cancel capture** requests cooperative cancellation. Closing the viewer also
requests cancellation and waits for the worker to close its owned processes.
Source movies and installed retail preferences remain unchanged. Captures and
provenance are saved under `tmp/movie-compare/run-*`. For diagnostic reopening:

```powershell
build/playback-review/image-compare.exe --session tmp/movie-compare/run-EXAMPLE
```

The worker is `tools/movie_compare_capture.py` and requires Python with Pillow,
`build/marcus-x64/src/marcus/marcus-capture.exe`, the x86 bridge in
`build/movie-compare-retail`, original content in `content-files`, and
`build/studio-portable-x64/3dmovie/utest.chk`. It reuses the prepared ISO package
or prepares it from `dist/3DMOVIE100.iso`. Its CLI accepts explicit overrides for
those dependency paths. The old `marcus-regression.exe` and
`tmp/retail-oracle/build` paths remain fallbacks. Each job snapshots its helpers
so a running capture does not lock workspace build outputs and its recorded
hashes describe the binaries actually used. Missing dependencies and failed renderers have explicit
error/partial statuses; unavailable captures never imply a match.

Build `marcus-asm-worker` in `build/marcus-x86` as described in the Marcus guide
to enable CPU ASM captures. Each job also snapshots and hashes this helper. If
it is missing, the other renderers continue and the capture explicitly reports
the ASM failure. Native geometry is rendered by that x86 process while caption
and transition composition remains shared with the other Marcus renders.

Retail eagerly caches expansion references from the movie's roll call, including
assets not fetched by an authored-frame seek. The worker snapshots installed
`EXPANSIONS`, `DORAEMON`, `N3DMM EXPANSION` and `FRANKIE` data beside a private copy
of the ISO executable and assets. It discovers these under
`ProgramFiles(x86)/Microsoft Kids`, or the CLI's `--expansions` root. The cache is
keyed by base and expansion file hashes; each capture records the original paths
and expansion hashes in `retail-expansions.json` and `capture.json`. It does not
launch an installed executable. Explicit `--product` disables automatic expansion
discovery unless `--expansions` is also supplied. This resolves the native
insert-media startup stall seen in Ringworld Act One Teaser Trailer (153 frames)
and Liberation Day Teaser Trailer (391 frames), both captured fully in all four
backends after supplying their expansion data.

These are ordered still-frame seeks, retaining each scene, rather than
normal-speed playback or independent scene resets for every frame. They do not
cover intermediate scrolling/transition presentations. The ordinary regression
seek commands retain their independent scene-selection behavior.

`review-image` checks navigation and RGB/overlay calculations;
`review-image-decode` checks background decoding, queue bounds, native diff/mask
pixels, reduced surfaces and missing-file failures. `image-compare-c89` checks
the new application and helpers with strict C89 diagnostics. A hidden render
check can save a viewer screenshot from existing captures:

```powershell
build/playback-review/image-compare.exe --session tmp/movie-compare/run-EXAMPLE --smoke viewer.png
```

## Build and open

From an x64 Visual Studio developer shell at the repository root:

```powershell
cmake -S tools/playback_review -B build/playback-review -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build/playback-review
ctest --test-dir build/playback-review --output-on-failure
build/playback-review/playback-review.exe tmp/textbox-acid-fonts-results/playback.sqlite
```

Launching without arguments opens a Windows file picker. The initial build
downloads pinned SDL3, cimgui (including its matching Dear ImGui submodule), and
SQLite sources. Dependency licenses are copied to the build's `licenses` folder;
SQLite is public domain. No Marcus rebuild is required.

Use **Open Database...** to switch results files without restarting, or
**Refresh** to reload the current database after new captures/comparisons arrive.
Refresh preserves the selected run and observation when they still exist, along
with filtering, sorting and note scope, and reloads saved notes and images.
Opening a different database resets the filter and selects a default run. A
failed open leaves the current database visible. Both controls are disabled
while a note has unsaved edits; save or discard those edits first.

## Reviewing evidence

- Filter runs by movie, player, suite, mode or status. The newest SDL seek run
  is selected initially when present. Select v3dmm to check it against retail.
- The run table has sortable, resizable columns for player, mode, status,
  repeat, case, run ID and preparation. Click a header to change sort order;
  run ID and repeat sort numerically. Headers remain visible while scrolling.
  Blue identifies the selected run; hovering another row uses grey. The suite
  ID remains searchable and appears in the run details instead of every row.
- Reference selection follows the case manifest, matching suite, case, mode,
  preparation and repetition. If several reference runs match, the newest is
  used. The displayed run IDs identify the exact pair.
- Step observations or use the slider. Scene numbers are zero-based, authored
  frame numbers retain their original values. **Next image difference** wraps
  through seek images with mismatched hashes/geometry or missing references.
  Playback navigation follows presentation images, including multiple redraws
  while one authored frame is held. The seek-only difference shortcut is disabled.
- Reference and candidate PNGs appear beside a magenta mask of changed RGB
  pixels. Zoom uses nearest-neighbour sampling; horizontal scrolling supports
  large images. Seek pairing requires matching sequence, scene and authored frame.
  Playback pairs by scene/authored frame without a timing requirement when all
  captured pixel hashes within that frame are unchanged in each run. The two runs
  may still have different pixels. This is observed stability, not proof that
  sampling caught every intermediate change. Otherwise it pairs the nearest
  reference using elapsed time relative to the frame marker. The displayed
  capture-phase offset is approximate alignment, not playback lateness.
  If timestamps are unavailable, the first reference image at that scene/frame
  is shown with a timing-unavailable label. This is a review aid, not a parity verdict: native blits,
  SDL presents and Marcus updates have different boundaries and rates.
  Missing captures or unequal dimensions have an explicit unavailable message.
  A playback image with no captured reference at that scene/frame remains viewable
  on its own. Older databases retain their original missing-image limitations.
- Expand recorded comparisons for verdicts, metrics and policies. These are the
  latest stored comparisons per reference/candidate/component; the browser does
  not run new comparisons. Historical comparisons may name a different reference
  run from the image pair; the reference ID is shown for each.
- Expand timing/state, provenance or audio details to inspect the stored evidence.
  Render and capture durations are displayed separately where recorded. Timing
  verdicts require equivalent authored-frame marker semantics.
  Timing is shown in recorded nanoseconds, including the interval since the prior
  observation. Audio paths, metadata and frame sound events are inspectable;
  listening and waveform playback are not implemented in this first version.

Pixel equality of seek images does not establish continuous playback quality.
Missing images, missing comparisons and `inconclusive` verdicts are not passes.
The mask is a visual aid computed from files currently on disk, not a new stored
verdict or artifact-integrity check. Run the regression CLI comparisons to verify
artifact hashes. The run list updates on startup, opening a database, or Refresh;
it does not automatically poll while captures are being added.

## Notes

Write a run note or enable **Note on this frame**, then press **Save note**.
Notes have an open/resolved flag, and **Saved notes for this run** lists existing
entries. Frame notes use capture sequence, so repeated presentations of the same
authored frame remain distinguishable. Save or discard pending edits before
switching runs/frames/scopes or closing the window. An oversized existing note is
refused rather than silently truncated (editor limit: 32,767 UTF-8 bytes).

The capture database is opened with `SQLITE_OPEN_READONLY`. Notes are stored in
`<database-path>.notes.sqlite`, keyed by suite ID, run ID and sequence (`-1` for a
run note), with UTC modification times. Saving replaces the note for that scope;
there is no revision history. Keep the sidecar with its results database when
moving or sharing evidence, and retain the relative artifact tree. The containing
folder must be writable. Concurrent editors use SQLite transactions, but the last
save of the same note wins.

## Verification

`review-store` checks persistence across reopen, frame/run isolation, Unicode and
quoted text, updates, missing runs, schema rejection, suite identity and read-only
capture access. `review-c89` runs strict C89 syntax checks when clang/gcc is found;
external dependency declarations are system headers.

For a short hidden render check against a real database:

```powershell
build/playback-review/playback-review.exe tmp/textbox-acid-fonts-results/playback.sqlite --smoke tmp/review.png
```

This renders three UI frames and optionally saves a screenshot. It creates the
notes sidecar if absent but writes no notes. This is not an automated authoring
UI test or a playback parity test. XP/Windows 98 support is not claimed; reviewing
their future capture databases on Windows 11 is separate from running this UI on
those operating systems.
