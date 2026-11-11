# Playback Review

Native C89 + cimgui browser for playback regression results, initially targeting
Windows 11. Application and SQLite access code is C89; Dear ImGui, its generated
cimgui implementation and the small platform/backend adapter compile as C++.
The build is independent of Marcus and the original engine.

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
  through candidate images with mismatched hashes/geometry or missing references.
- Reference and candidate PNGs appear beside a magenta mask of changed RGB
  pixels. Zoom uses nearest-neighbour sampling; horizontal scrolling supports
  large images. Pairing requires matching sequence, scene and authored frame.
  Missing captures or unequal dimensions have an explicit unavailable message.
- Expand recorded comparisons for verdicts, metrics and policies. These are the
  latest stored comparisons per reference/candidate/component; the browser does
  not run new comparisons. Historical comparisons may name a different reference
  run from the image pair; the reference ID is shown for each.
- Expand timing/state, provenance or audio details to inspect the stored evidence.
  Timing is shown in recorded nanoseconds, including the interval since the prior
  observation. Audio paths, metadata and frame sound events are inspectable;
  listening and waveform playback are not implemented in this first version.

Pixel equality of seek images does not establish continuous playback quality.
Missing images, missing comparisons and `inconclusive` verdicts are not passes.
The mask is a visual aid computed from files currently on disk, not a new stored
verdict or artifact-integrity check. Run the regression CLI comparisons to verify
artifact hashes. The viewer snapshots the run list at startup; reopen after new
captures are added.

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
