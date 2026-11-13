# Playback regression corpus

Timing policy version 2 requires matching, known `frame_commit_semantics` before
issuing an authored-frame timing verdict. Different or missing marker semantics
produce an inconclusive result, retaining raw metrics and `observed_status`.
Recomparison appends results without changing the captured evidence.

Sharing the scroll evaluator does not make capture markers equivalent: Marcus
records its authored marker before composition, while retail observes an engine
callback before painting. In the shared-scroll capture, Marcus's first composition
took 304.97 ms, accounting for most of the reviewer's approximately 303 ms image
phase offset. That offset alone does not establish playback lateness. Static
captured frames are paired independently of timing; changing frames retain
approximate phase pairing. Equivalent presentation markers remain future work.

`tools/playback_regression.py` runs a hash-pinned movie corpus serially through
retail, original v3dmm, SDL Studio and Marcus on Windows. Observations and scoped
comparisons go into `playback.sqlite`; PNGs, WAVs, logs and original capture
journals stay alongside it. Imported movies are never committed or saved over;
the small, project-authored Text Acid fixture is checked in for direct use. This is playback-quality evidence, not a declaration of parity.

The default comparison targets are **retail, v3dmm and SDL**, with SDL parity
the current priority. Add `--players retail,v3dmm,sdl,marcus` to include Marcus.

The initial generated corpus contains six two-scene movies (60 authored frames
each): general rendering/animation, actor event changes, a dissolve transition,
sampled speech/effects with distinct quiet 600 Hz and 1300 Hz cues, static
text boxes and scrolling credits. A separate four-scene, 240-frame Text Acid
movie exercises text features together. The feature catalog explicitly reports features that still lack cases. A feature
tag says the fixture exercises it; it does not say that feature passes.

## Build and prepare

For the native results browser and review notes, see
[Playback Review](playback-review.md). It builds independently of Marcus and
opens these databases without modifying captured evidence.

Use the existing Marcus build configuration. Stop this workspace's running
Marcus before rebuilding, then relaunch it afterward, as required by AGENTS.md.

```powershell
cmake --build build/marcus-x64 --target marcus marcus-regression preview-scene-fixture
python -B tools/playback_regression.py prepare `
  --fixture build/marcus-x64/preview-scene-fixture.exe `
  --iso dist/3DMOVIE100.iso --output tmp/playback-corpus
```

Preparation refuses to overwrite an existing suite manifest. Use a new directory
when regenerating the fixtures. `suite.json` records every movie's SHA-256,
declared reference, intended players, features and expected silence/audibility.
Add real movies by adding entries with these same fields. Paths are relative to
the manifest. Base `.3mm` cases normally use retail as their reference; `.vmm`
cases use v3dmm and should omit retail from their `players` list.

The [original oracle](v3dmm-oracle.md) documents building the x86 bridge and audio
capture helper, obtaining local original resources, and preparing external
libraries. Live runs require an interactive Windows desktop, audio output and
Python with Pillow and NumPy. Use a Release SDL build with
`STUDIO_TEST_TOOLS=ON`. Marcus capture uses its real C player API without an
application window or simulated UI input. Its executable is distinct from
`marcus.exe`.

## Capture

```powershell
python -B tools/playback_regression.py run `
  --manifest tmp/playback-corpus/suite.json `
  --iso dist/3DMOVIE100.iso `
  --bridge tmp/v3dmm-oracle/build `
  --v3dmm tmp/v3dmm-research/unpacked/Virtual3DMM `
  --sdl tmp/studio-batch-release/3dmovie-sdl.exe `
  --output tmp/playback-regression `
  --environment-notes 'Record GPU/driver, audio endpoint, MIDI mapper/bank and fonts here'
```

By default every case has separate **absolute-seek** and **normal-speed playback**
passes, each repeated in two fresh processes. Seek inspection never precedes
Play in the playback process. `--case NAME` is repeatable; `--players`, `--modes`
and `--preparations` accept comma-separated selections. `--repeat 1` is useful
for a smoke run but does not establish original repeatability.
When including Marcus, supply
`--marcus build/marcus-x64/src/marcus/marcus-regression.exe` as well.

SDL package data is shared by default under `OUTPUT/sdl-cache`, avoiding a
fresh extracted copy for every player/mode/repetition. `--sdl-cache PATH` selects
another shared cache. Processes, settings, registry isolation and working movie
files remain private. The path is recorded as `sdl_package_cache`; this is warm
package-cache coverage, with OS file caching uncontrolled. The existing C89 warm
cache was moved into `tmp/c89-movie-data-regression/sdl-cache` without copying it.

Raw capture retention defaults to the latest two completed runs for each
case/player/mode/preparation/repetition key, plus the latest failure for each key,
active runs, the latest suite and explicitly pinned suites. A suite remains while
any of its runs is needed. Cleanup runs before and after a suite; it refuses to
delete while another capture is running. SQLite observations/hashes/timestamps
remain unchanged, and summaries/diagnostic JSON/logs are archived under
`retained-metadata`. `pruned-suites.json` records removed raw directories.
Historical sections below describe evidence available when recorded; superseded
raw images/audio have since been pruned, with observations and metadata retained.

`--keep-captures N` changes the completed-run limit (`0` disables automatic
pruning). `--pin-suite ID` protects additional suites for an invocation; a JSON
array in `OUTPUT/retention-pins.json` supplies persistent pins. Manual cleanup is
preview-only until `--apply` is supplied:

```powershell
python tools/playback_regression.py prune --database tmp/c89-movie-data-regression/playback.sqlite
python tools/playback_regression.py prune --database tmp/c89-movie-data-regression/playback.sqlite --apply
```

Capture also requires 4096 MiB free before input preparation and every player
run (`--min-free-mib` adjusts this). On 13 November, superseded captures had
grown `tmp` to roughly 90 GiB on disk; pruning them reduced it to roughly 28 GiB.
The current baselines/results and latest failures were retained. Thirty-six
harness tests cover retention boundaries, metadata preservation, shared-cache
selection, path/link safety and the free-space guard; no Marcus UI tests are used.

`--preparations fresh-process,replay` additionally plays the complete movie once
before the recorded pass in the **same process**. Its warm-up artifacts are kept
separately. This retains process caches, although normal rewind behavior still
applies. Fresh process does not imply a cold Windows file cache: OS caching is
explicitly recorded as uncontrolled. Players run sequentially; player order and
all preparation labels are stored. Organiser preparation is not implemented
yet and is never silently substituted with replay.

The original 640x480 Studio client contains the 544x306 movie canvas at
`48 100 592 406`. This was checked against SDL's actual movie-view rectangle.
`--viewport` changes the explicit crop; Marcus renders at that exact size.
There is no rescaling, border masking or image registration to hide differences.
Stored frames/scenes use zero-based scene indices and the movie's **raw authored
frame numbers**, which can be negative. UI seek coordinates are converted at
the adapter boundary.

A base movie is wrapped in a dependency-free v1 VMM envelope to run in original
v3dmm. Its inner `.3mm` bytes are unchanged. Existing VMMs are passed unchanged;
retail reports them as unsupported. Per-case `libraries` can map external
library names to local folders for the original v3dmm adapter. Corresponding
external-library preparation for SDL/Marcus is not wired into this runner yet;
missing dependencies remain errors rather than substituted content. Original runtime identity and private
resource paths retain the oracle's existing validation.

## Measurements and comparison

SQLite stores runs and their capabilities/provenance, scene ranges, every
observed authored-frame commit, seek images, all-player presentation images, native
sound requests, actual mixed output recordings, and comparison policies/results.
Available observations include:

- Exact RGB pixel hashes independent of PNG/PPM encoding; artifact hashes also
  detect missing or changed media. Seek differences include changed-pixel counts,
  peak channel error, the first differing frame, its bounding box and a diff PNG.
- Continuous ordered frame coverage, QPC timestamps, frame intervals, maximum
  and p95 timing difference and final drift relative to the first commit.
  Native bridge trace version 2 selects the actual `FCmdRender` presentation
  callback. Play refreshes and scene preloads are retained as raw callbacks in
  the capture journal, but do not count as presented frames. Older bridge
  versions are rejected rather than silently mixing different observation points.
- Native/SDL requested sound identities, queue, volume, repeats, offset, priority
  and class; unavailable timestamps remain null.
- Per-process 48 kHz stereo signed-16-bit PCM, packet validity and an anchor
  sample tied to the first frame commit. The original oracle rejects capture
  discontinuities, timestamp errors and incomplete playback.
  Capture requests float mixer output and converts deterministically to PCM16,
  avoiding Windows' random one-LSB conversion dither. Before Play, it requires
  250 ms of consecutive zero PCM, confirming quiet output before the movie. The full
  pre-roll is retained; a ten-second silence timeout fails capture.
- Marcus composed images during animated presentation, wait/scroll/transition
  state, render duration and image-copy/enqueue overhead. A bounded asynchronous
  writer avoids filesystem writes on the player thread; queue overflow or write
  failure invalidates capture. These are headless rendered images, not proof of
  physical display timing. The adapter services playback at up to 100 Hz.

Pixels and samples require exact equality by default. The explicit initial
frame-timing allowance is 25 ms, adjustable with `--frame-tolerance-ms`.
`--sample-tolerance` changes only the stated sample error bound. There is no
gain normalization, per-cue alignment or time warping. Declared silent fixtures
can establish expected silence, but cannot establish working audible output.
Repeated reference runs measure repeatability; a non-repeatable reference
component makes candidate attribution inconclusive while preserving raw metrics.

`complete` on a run means that capture finished and its authored-frame coverage
was checked. It does **not** mean that playback matched. `seek_fidelity` and
`playback_fidelity` are separate comparison scopes. Failed/incomplete captures
cannot match, including a shared failure in both players. Capture failures retain
their artifact journals and available trace observations. The command returns 0
for matched requested comparisons, 1 for differences/inconclusive comparisons,
and 2 for capture/infrastructure errors.

Continuous image capture now records native GDI canvas blits and SDL host
presents, alongside Marcus updates. The native observer mirrors blits to an
off-screen DIB rather than reading an occluded desktop or forcing movie redraws.
It seeds that surface once before playback. The SDL observer copies the canvas
after a host present. All images carry scene/frame labels and monotonic times;
multiple observations during an authored-frame hold are retained. Native and
SDL observation rates differ: native snapshots are limited to 60 Hz between
authored commits, while every blit still updates the mirror. This prevents tiny
dissolve blits from flooding the writer. Native trace metadata records the
sampling period and the number of blits not individually snapshotted. Native and
SDL writes use a bounded asynchronous queue (128 unique images), with a 20,000
observation limit. Consecutive identical pixels reuse one image file while
retaining every timestamp. Overflow/write failure invalidates capture.

Current gaps are explicit:

- Blits, host presents and Marcus updates have different presentation boundaries.
  `continuous_pixels` remains inconclusive until a validated temporal comparison
  policy exists. Playback Review offers nearest same-frame phase alignment
  for manual inspection; it does not compare raw image sequence numbers. Capture
  timestamps are not physical display timestamps, and capture adds overhead.
- Marcus sound-request and per-voice playback-position observers are missing.
  Marcus recordings observe actual output until player stop, but the C API does
  not establish whether a trailing voice completed; tail state is unavailable.
- Actual monitor presentation timestamps are unavailable. Marcus timestamps
  observe its host update after authored evaluation; original/SDL timestamps
  observe the `FCmdRender` presentation callback. These are not identical instrumentation
  points, and image-copy overhead can affect timing. Use optimized capture
  builds when assessing performance.
- Initial movies do not cover all text, waits, transitions, audio combinations,
  expansion assets or long-run cases. `features_without_cases` lists omissions.
- Organiser preparation, click-wait input scripts, XP and Win98 capture adapters
  remain future work. SQLite observations/review do not depend on Windows APIs.

These gaps deliberately prevent a full playback-parity pass.

## Windows 11 parity fixes, 10 November 2026

Studio's Windows host now installs `GetTickCount` through the core's existing
tick-source interface, preserving the master clock's epoch. Retail's coarse
tick boundaries affect the legacy frame-delay rounding: replacing them with a
high-resolution clock had made SDL run ahead. The portable core remains free
of Windows APIs; other hosts retain their existing clock source.

On the generated 60-frame `features` case, SDL's final drift against retail
changed from **206–353 ms early** to **3–19 ms late** across two runs. One new
run matched the existing 25 ms frame policy throughout; the other exceeded it
on four frames (34 ms maximum). Native v3dmm also varied against retail, so
this is evidence of reduced drift, not a claim of deterministic scheduling.
The local SQLite evidence is in `tmp/parity-fixes/windows-clock/playback.sqlite`.
The separate sampled-audio repeat check still ended 72–114 ms late in SDL;
retail's own repeated frame times differed by up to 76 ms, leaving timing
attribution inconclusive under the same policy. Its sound requests matched in
all three players. Those observations are in
`tmp/parity-fixes/windows-clock-audio/playback.sqlite`.

The portable untextured Gouraud scanner now follows the original x86 integer
edges, reciprocal gradients, half-pixel parameter bias and strict depth ties.
Independent original-assembly runs pin colour-plus-depth hashes for 1,536
triangle cases per scanner, including all vertex permutations. Both portable
untextured and existing textured scanners match those hashes. On the `features`
seek capture, SDL improved from 3,393 changed pixels over 30 differing frames
to **3,162 pixels over 29 frames**, with its first frame now exact. Remaining
geometry/pixel differences still fail exact comparison.

The final four-case seek sweep captured all **720 images** successfully.
Original v3dmm matched retail on all 240 frames. SDL matched exactly on 128 of
240; the other 112 still differed. SDL's changed-pixel totals were 3,162 for
`features`, 1,627 for `actor-events`, and 3,162 each for `dissolve` and
`sampled-audio`. These count absolute-seek images, including the dissolve case;
they do not establish continuously rendered transition parity. The database is
`tmp/parity-fixes/final-seek/playback.sqlite`.

Marcus preview now applies the same Windows display-palette policy as Studio.
On this fixture its changed-pixel count dropped from 221,896 over 60 differing
frames to 4,268 over 30 frames. These seek observations are in
`tmp/parity-fixes/visual/playback.sqlite`; Marcus remains an optional target.

The corrected frame observer also removed an apparent 161 ms Marcus scene-boundary
error: the old reference had counted scene preload as presentation. With the
actual presentation callback the observed boundary difference was about 15 ms.
Silent controls now record exact zero PCM in all four players. The independent
audio calibration still detects each process's own tone, excludes the other
process's tone, and captures MIDI. Audible original runs still vary in onset,
so exact mixed-waveform parity remains unestablished. Do not increase tolerances
or align individual cues to turn those recordings into passes.

## Review and recomparison

```powershell
python -B tools/playback_regression.py review --database tmp/playback-regression/playback.sqlite
python -B tools/playback_regression.py compare `
  --database tmp/playback-regression/playback.sqlite --reference 1 --candidate 5
python -B tools/playback_regression.py compare-suite `
  --database tmp/playback-regression/playback.sqlite
```

Comparisons append policy and result records without changing observations.
`latest_comparison_review` selects the newest policy for each run pair/component;
`comparison_review` retains the full history. Offline recomparison uses the
explicit policy supplied to that command; reference repeatability comparisons
remain separately reviewable. It must compare the same suite/case, mode and
preparation. `latest.json` identifies the latest suite and its local summary.
Use `compare-suite` to reapply a policy to all captured pairs with original
repeatability gating, without launching any players.

Useful SQLite queries:

```sql
SELECT * FROM run_review;
SELECT case_name, reference_player, candidate_player, mode, preparation,
       component, status, metrics_json
FROM latest_comparison_review
WHERE status <> 'matched';
SELECT * FROM frame_timing WHERE run_id = 2 ORDER BY sequence;
SELECT * FROM image_review WHERE equal_pixels = 0;
SELECT * FROM feature_coverage ORDER BY feature, player, mode;
```

Schema version 1 uses SQLite `user_version`. The database holds relative artifact
paths, so move it together with the run directories. Source revision, dirty-file
list, executable/resource hashes, OS details, adapter capabilities and user
environment notes are kept with the observations.

Harness verification uses independent tiny images and synthetic observations to
check missing/reordered frames, timing drift, changed artifacts, comparison
history and inconclusive reference behavior. It does not drive any Marcus UI:

```powershell
python -B tests/playback_regression_test.py
ctest --test-dir build/marcus-x64 -R '^playback-regression$' --output-on-failure
```

Pillow is required for the image checks. Without it, those individual checks
skip explicitly while standard-library timing/database checks still run.

## Actor-facing arithmetic, 11 November 2026

The portable `BrFixedATan2` now interpolates the original assembly lookup
table instead of using libm. A 513 by 513 direction sweep (all octants,
axes and zero) produces the same colour-independent angle hash as unchanged
x86 assembly: `a8b160126097ab95`. The registered `brender-atan-parity` test
pins this result. This corrects one-angle-unit discrepancies in actor facing.

The `features` seek rerun in `tmp/parity-atan/playback.sqlite` still has
29 SDL frames and 3,162 pixels different from retail; v3dmm remains exact.
Thus this arithmetic fix does not explain that fixture's remaining geometry
mismatch. Vertex/projection tracing, continuous playback images, audible
onset analysis and Organiser preparation remain outstanding.

## Transform diagnostics and clipping division, 11 November 2026

Native and SDL state journals now include `root_matrix_fixed` (12 raw signed
16.16 values) and `part_matrices_fixed` (12 values per body part, in body order).
Rows are stored consecutively; the fourth row is translation. Native reads
use the hash-pinned x86 actor/body layouts and reject unsupported root transform
kinds or invalid part counts. These observations are available in SQLite's
`frames.state_json`, without rounding to decimal positions.

In `tmp/parity-part-matrices/playback.sqlite`, all 240 actor roots and 2,100
body-part matrices across the 60-frame `features` case match retail exactly
in both original v3dmm and SDL. This rules out root placement and local pose
as the cause of this fixture's remaining pixels; projection, clipping, depth
and rasterization still need investigation.

The portable clipping divide `BrFixedDivR` incorrectly added half the divisor.
It now follows the original signed numerator construction and division. The
unchanged x86 assembly and portable implementation agree on 262,656 signed
input pairs, pinned by `brender-divr-parity` to hash `76d7761ee6511c0c`.
The four-case sweep in `tmp/parity-divr/playback.sqlite` completed all 720
images but retained the previous pixel mismatches. Neither this arithmetic
fix nor matching transforms establishes full rendered-image parity.

## Text-box regression cases

`prepare` now adds `static-text` and `scrolling-text`. Both contain a real 2D
text box in each of their two scenes, using Arial at size 16 with a white
background. `static-text` contains mixed-case words, digits and punctuation,
explicit paragraph breaks and lines long enough to wrap. Its box is visible
from frame 3 through 24 inclusive, providing before/after visibility checks.
`scrolling-text` contains eight numbered credit lines in a 180 by 48 box, visible
from frame 3 through the scene end. It runs through ordinary playback without
batch-mode wait skipping.

The manifest records a `text_contract`. Seek capture requires one loaded box
on every frame; SDL additionally checks the expected text prefix and exact
visibility boundaries. Missing content, early appearance or late disappearance
fails capture. Scrolling playback must hold the first visible frame for at
least one second in each scene, catching skipped credits. That minimum is a
fixture-exercise check, not a timing-parity tolerance or proof of smooth scrolling.
All players' pixels remain subject to exact comparison. Marcus
is optional and does not yet expose this state contract.

Run just these cases with `--case static-text --case scrolling-text`. Default
modes capture all 60 absolute seeks and normal-speed playback for each player.
Those original captures lacked intermediate images. New playback captures include
them, but scrolling speed, smoothness and exact moving glyph parity still require
temporal review; a successful seek pass cannot establish scrolling parity.
These are initial Arial cases, not exhaustive font/style/alignment coverage.

The final Windows 11 smoke sweep is in `tmp/playback-text-final/playback.sqlite`:
12 complete captures (two cases, three players, seek/playback), 360 seek images,
and no incomplete runs. All final text contracts passed when reapplied to the
stored observations (`text-contract-checks.json`). Original v3dmm matched retail
on all 120 seek frames. SDL differed on 51 static-text and 29 scrolling-text
seek frames; those full-canvas counts also include existing actor differences.
Within the static box's rectangle on scene 1/frame 3, SDL differed by eight
pixels and v3dmm by zero. The original scrolling runs held frame 3 for about
11.4 seconds per scene. One repetition is a smoke check, not reference
repeatability evidence. The earlier 24-line draft exceeded the 120-second
limit and is retained separately in `tmp/playback-text`; the final fixture
uses eight lines.

## Test-only startup and UI audio policy

The original-runtime bridge suppresses sound requests during startup/loading
until the first successful state observation. It then permits only movie
classes 0, 1 and 2 (non-looping sounds, looping WAVE, looping MIDI). The filter
is installed before the original application resumes, remains active outside
recording, and is shared by retail and v3dmm. Provenance records the policy and
suppressed-request counts. Old bridges without this policy are rejected.

SDL's private test settings explicitly enable `MuteStartupSound` and
`MuteStudioSounds`; the adapter checks both effective preferences. Movie audio
remains enabled. These changes affect oracle/regression processes only, and do
not change personal settings. Marcus's headless regression player has no Studio
startup/UI sounds. The pre-Play silence check remains a capture validity check.

## Text Acid movie

`prepare` also generates **`text-acid.3mm`** (`--text-acid` in the native
fixture author). It has four labelled 60-frame scenes, no actors, and 31 text
boxes in total. It references the standard retail background library; it does
not bundle fonts or copyrighted background assets. Use `--case text-acid` in
the existing run command to compare it in retail, v3dmm and SDL.

| Scene | What to inspect |
|---|---|
| 1: Typography | Arial, Times New Roman and Courier New; sizes 12/16/20/24; plain, bold, italic and combined style; font, size, style and colour changes within one box; red/green/blue text; white/black/yellow fills; left/centre/right paragraphs; transparent background. |
| 2: Geometry and lifetime | Narrow/wide wrapping examples, paragraphs, multiple spaces, a tab, long unbroken text, edge clipping, opaque overlapping boxes and a transparent overlay. The bottom-right box changes red ? green ? blue at frames 20 and 40, with exactly one label visible. |
| 3: Credits | Two simultaneous scrolling boxes with different fonts/styles, centre/right alignment, opaque/transparent backgrounds, and clipping. Both appear at frame 3 and disappear at frame 31. Replaying should restart the credits. |
| 4: Missing font | The deliberately absent `3DMM_Acid_Missing_7F5C2A` alongside an Arial control. Check legibility, metrics, fallback appearance and persistence. The test installs no substitute. |

The `text_acid_contract` checks page/box counts, SDL paragraph alignment,
mixed font runs, lifetime boundaries and preservation of the missing-font
sample. Playback must exercise the credit hold. Exact viewport comparisons
remain separate: passing the content contract is not a visual parity pass.
New continuous captures include moving-credit pixels for temporal review.

A movie file stores the **results** of authoring, not an editing history. To
exercise editor operations, use a copy: move and resize a box, select and replace
a text range, cut/copy/paste, duplicate a box, switch story ? credits, undo and
redo each operation, then save/reopen and compare content, formatting, bounds,
type and lifetime. The paired-width boxes demonstrate reflow but do not prove
interactive resize or undo correctness. These editor workflow checks are not
claimed as automated by the Acid movie. Underlining and arbitrary Unicode/glyph
coverage are also not claimed.

The reusable authored file is `tests/fixtures/textbox-acid.3mm`, with its
hash-pinned single-case manifest at `tests/fixtures/textbox-acid.json`. Use
`--manifest tests/fixtures/textbox-acid.json` with the existing run command.
Reference PNGs, recorded states, PCM and comparisons are retained together under
`tmp/textbox-acid-fonts-results/`. The exact nonexistent font resolves to
`System` in SDL on this Windows 11 host, while `font_fallbacks` stays zero;
inspect the effective font and image instead of trusting that counter alone.

The final four-scene run completed six captures and 720 seek images. V3dmm
matched all 240 retail frames; SDL differed on all 240, totalling 3,780 changed
pixels. The missing-font sample rectangle itself matched retail exactly on
scene 4/frame 1 (zero changed pixels), while that full page differed by 11
pixels elsewhere. All six recorded runs passed the final Acid contract checks
stored in `text-acid-contract-checks.json`. These are Windows 11 smoke results,
not proof of all-platform or continuously scrolling image parity.

To include Marcus in a fresh, repeated Text Acid capture alongside the native
references and SDL, run:

```powershell
python -B tools/playback_regression.py run `
  --manifest tests/fixtures/textbox-acid.json --iso dist/3DMOVIE100.iso `
  --bridge tmp/v3dmm-oracle/build --output tmp/textbox-acid-all-players-results `
  --sdl tmp/studio-batch-release/3dmovie-sdl.exe `
  --marcus build/marcus-x64/src/marcus/marcus-regression.exe `
  --v3dmm tmp/v3dmm-research/unpacked/Virtual3DMM `
  --resources build/studio-portable-x64/3dmovie/utest.chk --content-dir content-files `
  --players retail,v3dmm,sdl,marcus --repeat 2 --max-seconds 180
```

Open `tmp/textbox-acid-all-players-results/playback.sqlite` in Playback Review.
The older `textbox-acid-fonts-results` database contains no Marcus captures.
Marcus uses the headless player adapter; its native text-box state contract is
not exposed, so review its captured images and recorded comparisons rather than
interpreting completion as a text-feature pass.

Windows 11 suite `20261111T090157Z-d50b7175` completed all 16 captures in that
database. Marcus seek runs 13/14 each contain 240 images; playback runs 15/16
each contain 240 authored frame observations and 1,256 presentation images.
Retail's repeated seek images match exactly. V3dmm matches all 240 retail seek
images in both repeats; SDL differs on all 240 with 3,780 changed pixels per
repeat. Marcus differs on all 240 with 18,978,540 changed pixels per repeat.
The first Marcus typography image visibly differs in layout and clipping;
these observations do not identify the underlying implementation cause.

Marcus's authored frame sequence and expected silence match, but continuous
playback fidelity remains inconclusive. Retail playback timing did not repeat
within the 25 ms policy, so timing comparisons are gated as inconclusive, and
native continuous-image evidence was unavailable in that historical suite. This capture uses the
existing binaries, with their hashes recorded in provenance; no Marcus rebuild
or UI test was performed.

## Continuous image capture validation

`tmp/text-acid-continuous-results/playback.sqlite`, suite
`20261111T093115Z-0f7d26b9`, contains complete playback captures from all four
players: retail 610 images, v3dmm 610, SDL 2,856 and Marcus 1,229. Each native
capture contains 132 distinct moving-credit images at scene index 2/frame 3.
These are new captures; earlier databases are not backfilled. The native Text
Acid validation preceded the 60 Hz burst limit added for dissolves.

The new reviewer navigates presentation images rather than confusing them with
authored-frame observations. Same-scene/frame images are paired by phase since
the corresponding authored commit, with the offset visible. Offsets over 50 ms
are flagged but no longer hide the reference image; missing timestamps fall
back to the first same-scene/frame reference. In the first SDL typography
image, retail is 3.19 ms earlier in frame phase and the images differ by 10 RGB
pixels. This diagnostic pairing does not establish continuous playback parity.

`tmp/dissolve-continuous-final/playback.sqlite`, suite
`20261111T094121Z-9281954c`, validates the final native sampling limit: all three
captures complete, with 352 native images per player and 570 SDL images. Each
native capture has 234 distinct images at the incoming scene's frame 1 during
the dissolve. SDL has only one image at that scene/frame and 31 unique images
over the entire movie, versus 264 native unique images. The SDL observer is on
the host's present path; its transition loop currently flushes GDI operations
without calling that path, so intermediate transition states are not presented
there. This is evidence of an SDL playback gap, not a reason to force extra
renders in the capture adapter. The earlier unbounded-blit attempts in
`tmp/dissolve-continuous-results` failed capture and must not be treated as passes.

Validation also includes the writer's pixel conversion, duplicate reuse and
write-error handling; Python import retains repeated timestamps; C89 reviewer
tests check phase pairing across independent clocks, mismatched sequence
numbers, absent timestamps and large timing offsets without hiding references.

The coloured grid on the generated feature/dissolve actor is an intentional test
material, not a missing-texture fallback. The fixture writes texture pixels to
a `TMAP` chunk, a referencing `MTRL` chunk, and actor costume events into the
`.3mm` file. Retail loads those through its ordinary movie format; its installed
model/texture resources are not replaced.

## C89 playback-policy conversion (11 November 2026)

The first [C89 engine slice](../code-map/library-split.md#c89-engine-backend)
moves the playback/startup command evaluators, frame pacing and retail scrolling
into `libraries/engine/`. Studio and Marcus use that implementation through
temporary C++ facades. Rendering, devices, movie readers and actor evaluation
remain outside this converted slice.

Before/after captures use `tmp/playback-corpus-text-final/suite.json`, case
`scrolling-text`, players `retail,sdl,marcus`, modes `seek,playback`, preparation
`fresh-process`, and two repetitions. Evidence stays in:

- Before: `tmp/c89-policy-before/playback.sqlite`, suite
  `20261111T145154Z-dc569437`.
- After: `tmp/c89-policy-after/playback.sqlite`, suite
  `20261111T150201Z-c1d94263`.
- The local cross-database report is `tmp/c89-policy-comparison.json`.

Both suites have ten complete and two incomplete runs and remain
**inconclusive**, with ten different and nine inconclusive component comparisons
against their references. These totals are not a parity claim. Comparing each
player with its own pre-conversion capture gives:

| Observation | Before/after result |
| --- | --- |
| Retail, SDL Studio and Marcus absolute seeks | All 60 frame images match exactly in both repetitions for each player. |
| Retail and SDL Studio normal playback | All 60 authored frame positions retain their order in both repetitions. |
| Retail and SDL Studio continuous pixels | Each pass has the same ordered 328 image changes before and after, comparing exact pixel hashes and scene/frame coordinates while collapsing only adjacent identical images. Raw duplicate samples and timing evidence are retained. |
| Marcus normal playback | Incomplete on both sides. Before records an empty capture error and an image queue overflow; after records an image queue overflow and failure to settle audio to silence before Play. No continuous-playback verdict is issued. |

Retail timing already varies between repetitions; existing marker-semantics
restrictions still apply. Capture/environment failures are not changed to
passes. The scrolling fixture does not cover every startup failure, pause or
fade branch, nor editing/persistence.

Focused validation supplements that evidence: `engine-c89`,
`engine-c89-syntax`, `engine-core-link`, `movie-save-load`, `marcus-c89`,
`marcus-player` and `marcus-playback` pass. The library also builds and passes
its two standalone checks using a C-only Clang configuration. A one-off
differential check against the C++ policy at `6f070a33` matched 50,000 seeded
playback command/state traces, 50,000 startup command/state traces and 50,000
wrapping-clock calculations, including host state changes between replies.
That check exercises policy equivalence; it does not replace the missing
Marcus live-capture evidence or establish broader engine parity. No Marcus UI
tests were added.

## C89 movie-data reading and Marcus capture repair (11 November 2026)

The next backend slice converts bounded decoded-chunk reads, MVIE version
decoding, roll-call records and Archive's existing GST layout checks to C89.
Studio and Marcus use the same movie-version and actor-record decoders. See
the [remaining C++ dependencies](../code-map/library-split.md#movie-data-reading-slice).

Marcus's observer now uses the shared bounded `PlaybackCapture::Writer` for
live playback, reusing a file only when RGB bytes and image dimensions match.
Every presentation retains its own scene/frame and timestamp. Queue overflow,
event-limit exhaustion and write failures still fail capture. Same-process
warm-up preservation moves each unique file once and updates every referring
event; subsequent replay starts a fresh writer cache.

Silent movies did not necessarily open Marcus's lazy audio device before Play,
so process loopback could not establish measured pre-Play silence. The headless
capture tool now opens a separate hardware-clocked zero-valued SDL stream.
The movie player's audio and timing code are unchanged. Actual process PCM,
including pre-roll, is still recorded and checked; the silence threshold and
comparison policy are unchanged. Provenance explicitly records
`silence_carrier` and `duplicate_images_reused`, and hashes the new helper.

Evidence is in `tmp/c89-movie-data-regression/playback.sqlite`:

| Suite | Selection | Capture result / comparison verdict |
| --- | --- | --- |
| `20261111T154504Z-7563d577` | `scrolling-text`, retail/SDL/Marcus, seek and playback, two fresh-process repetitions | 12 complete, no capture failures; verdict `different`, with 10 different and 15 inconclusive component comparisons. |
| `20261111T155408Z-cbdea592` | `actor-events` and `sampled-audio`, retail/SDL/Marcus playback, one repetition | Six complete, no capture failures; verdict `different`. One pass does not establish repeatability. |
| `20261111T155940Z-9c908dcc` | Marcus `static-text`, same-process replay after a complete warm-up | One complete recorded run plus preserved warm-up; no reference comparison, verdict `inconclusive`. |

Compared with the pre-data-conversion suite `20261111T150201Z-c1d94263`, all
360 seek images match exactly across the three players and two repetitions.
Retail and SDL each retain the same 60 authored frame positions and ordered
328 exact image changes in each playback pass, collapsing only adjacent
identical images. The local report is `tmp/c89-movie-data-comparison.json`.
Marcus's earlier live passes were incomplete, so no before/after continuous
Marcus verdict can be issued; both new passes now complete all authored frames.

Sampled audio is present in all three players: observed PCM16 peak magnitudes
are 2380 in retail and 2176 in SDL and Marcus. This confirms audible capture;
it does not establish matching audio or attribute the differences. Existing
marker-semantics limits still prevent an equivalent cross-player authored-frame
timing verdict. Completed captures are not a declaration of playback parity.

All 19 runs retain unchanged movie, ISO-resource and Marcus-content hashes.
The three standalone C-only checks pass, as do the ten focused native checks
(`engine-c89`, `engine-movie-data`, `engine-c89-syntax`, `engine-core-link`,
`movie-save-load`, `archive-api`, `presentation-capture`, `marcus-c89`,
`marcus-player`, `marcus-playback`) and 25 Python harness checks. Headless
Marcus cases now explicitly reject invalid MVIE version/byte-order headers.
No Marcus UI tests were added.

## C89 chunky container reading (11 November 2026)

CHN2 header/GG index decoding, live chunk/child searches and graph validation
now use the shared C89 backend through Kauai in both hosts. Writable groups,
free maps, serialization, embedded-forest decoding and legacy name conversion
remain C++. This slice changes no capture thresholds or player audio policy.

Before/after CLI comparisons in `tmp/c89-chunky-baseline/comparison.json`
match all 51 inputs: 13 movie/archive dumps (including a corrupt-file rejection),
six asset indexes containing 20,568 entries, and 32 constructed legacy files.
The legacy cases preserve all 20 acceptances and 12 rejections across version,
layout, endian and old-name combinations. Successful outputs match exactly;
rejected inputs retain their failure result. Typed enumeration and child-list
checks also preserve the existing type-relative payload behavior.

The four standalone C-only checks and eleven focused native checks pass:
`engine-c89`, `engine-movie-data`, `engine-chunky`, `engine-c89-syntax`,
`archive-api`, `movie-save-load`, `kauai-core-link`, `engine-core-link`,
`marcus-c89`, `marcus-playback` and `marcus-player`. No Marcus UI tests were added.

Suite `20261111T170550Z-0ec5aad2`, in the existing
`tmp/c89-movie-data-regression/playback.sqlite`, runs `actor-events` in retail,
SDL Studio and Marcus, seek and playback, with one repetition. Five captures
complete. SDL playback fails before Play because audio does not settle to
silence; that failed attempt is retained. The suite verdict is `inconclusive`,
with four different and five inconclusive component comparisons.

SDL-only retry `20261111T172015Z-40d77fc0` completes all 60 authored frames
and 583 presentation samples. It has no reference run, so its comparison
verdict remains `inconclusive`; it does not erase the first failure.

Retail, SDL and Marcus playback retain the same 60 authored frame positions and
60 ordered exact image changes as pre-container suite
`20261111T155408Z-cbdea592`, collapsing only adjacent identical images.
The local comparison is `tmp/c89-chunky-playback-comparison.json`.
Raw timestamps and presentation samples remain preserved. Both suites' movie,
ISO-resource and Marcus-content integrity checks remain unchanged. These
checks establish neither full retail parity nor timing equivalence, and one
repetition does not establish repeatability. Existing cross-player timing
marker and presentation-alignment limits remain applicable.

## C89 string-table storage and conversion (11 November 2026)

Dense/allocated string-table mutation, packed-string reconstruction and searches
now use the C89 backend. Fixed legacy byte maps, portable conversions and UTF-16
byte-order handling share that backend. Kauai retains allocation/object and GST
I/O adapters; native Windows code-page and locale services remain callbacks.
Capture thresholds, silence checks and player timing/audio policies are unchanged.

Five standalone C-only checks and fifteen focused native checks pass, including
`engine-strings`, `string-compat`, `text-services` and `text-portable`. The native
string checks now exercise dense/allocated save/reopen, holes, opaque metadata,
both byte orders, Mac/Windows encoding, non-ASCII text, UTF-16BE input and
duplicate independence. Native and forced-portable text checks also pass in
both standalone ANSI and Unicode platform builds (four checks). No Marcus UI
tests were added. All 45 saved movie/archive and constructed legacy CLI
comparisons retain their exact output or rejection result; the local report
is `tmp/c89-strings-dump-comparison.json`.

Evidence remains in `tmp/c89-movie-data-regression/playback.sqlite`:

| Suite | Selection | Result |
| --- | --- | --- |
| `20261111T200557Z-e569664c` | `scrolling-text`, retail/SDL/Marcus, seek and playback, one repetition | Five complete; Marcus live capture fails while the drive fills. The initial comparison also encounters disk exhaustion. After space recovery, retained observations are recomputed: verdict `inconclusive`, six different and two inconclusive components. The failed run remains an error. |
| `20261111T201650Z-c357edf5` | Marcus `scrolling-text` playback retry | One complete, 60 authored frames and 2,181 presentation samples. No reference in this suite; verdict `inconclusive`. |

Compared with pre-string suite `20261111T154504Z-7563d577`, repetition zero,
all 180 seek images match exactly. Each player retains the same 60 authored
playback positions and ordered exact image changes: 328 for retail and SDL,
321 for Marcus, collapsing only adjacent identical images. Raw presentation
samples and timestamps remain available; no normalization of frame positions
or pixels is applied. The local report is
`tmp/c89-strings-playback-comparison.json`. These are before/after preservation
checks, not a declaration of cross-player parity or timing equivalence. A
single new repetition does not establish repeatability.

The original suite's recorded movie, extracted ISO and Marcus-content hashes
are rechecked after space recovery and remain unchanged; its summary records
that recovery explicitly. The retry's normal integrity checks also pass.
SQLite's `quick_check` returns `ok`. Old capture evidence remains on C:; no
files from this task remain on D:.

## C89 collection storage foundation (11 November 2026)

`GroupBase` now uses C89-owned buffers for arrays, groups, string tables and
chunk indexes. Allocation lifetime, growth hints and fallback priority, nested
pointer locks, independent copies and ordered two-section I/O share one C
implementation. Host callbacks retain fixed-memory diagnostics/failure hooks
and DataBlock access. Array/group mutations and their stored headers remain
C++; the whole backend conversion is still incomplete.

The six standalone C-only checks and seventeen focused native checks pass.
`engine-storage` adds a relocating/failing allocator and exercises speculative
fallback, partial two-section failures, failed shrink, size limits, locks,
independent copies, wire order and read/write callback failures. Core link
checks verify the real allocation-failure hooks and balanced allocation counts.
All 45 stored movie/archive and legacy-file CLI comparisons retain exact output
or the same rejection result (`tmp/c89-storage-dump-comparison.json`). Studio
and Marcus are rebuilt; no Marcus UI tests are added.

Evidence in `tmp/c89-movie-data-regression/playback.sqlite`:

| Suite | Selection | Capture result / comparison verdict |
| --- | --- | --- |
| `20261111T210623Z-1beefe45` | `scrolling-text`, retail/SDL/Marcus seek and playback, one repetition | Six complete, no capture failures; verdict `different`, six different and five inconclusive components. |
| `20261111T211008Z-843d3a11` | `sampled-audio`, retail/SDL/Marcus playback, one repetition | Three complete, no capture failures; verdict `different`, five different and four inconclusive components. |

Against the pre-storage string-conversion captures
(`20261111T200557Z-e569664c`, with Marcus live retry
`20261111T201650Z-c357edf5`), all 180 scrolling-text seek images match exactly.
Each player retains the same 60 authored playback positions and ordered exact
image changes: retail/SDL 328, Marcus 321. Only adjacent identical images are
collapsed; raw presentation samples and timestamps remain preserved. The local
report is `tmp/c89-storage-playback-comparison.json`.

Sampled-audio playback retains the same 60 authored positions and 60 ordered
exact image changes for each player as `20261111T155408Z-cbdea592`. Observed
PCM16 peak magnitudes remain 2380 for retail and 2176 for SDL and Marcus.
This establishes audible capture, not exact PCM parity. The local report is
`tmp/c89-storage-audio-comparison.json`.

All nine runs retain unchanged movie, ISO-resource and Marcus-content hashes.
There are no new timing/retail-parity claims: existing cross-player marker,
presentation-alignment and audio differences remain. One new repetition per
case does not establish repeatability. Regenerable resource caches were
recreated on C: after cleanup; no data is relocated to another drive.

## C89 array/group mutations and wire headers (11 November 2026)

Dense/allocated arrays and groups now use C89 mutation algorithms, including
free slots, bitmap state, four-byte padding, byte transfers, merges and entry
copying. GL12/AL16/GG20 headers use shared C decoders/encoders. Native adapters
retain object lifetime, pointer access and diagnostics, including the original
debug assertion levels. GST headers, DataBlock/stream services, codecs and
chunk-save policy remain further migration work.

All seven standalone C checks and eighteen focused native checks pass. The new
`engine-collections` check includes independent wire fixtures and 12,800 model
steps covering dense/allocated groups with fixed prefixes from zero through
seven bytes. It also passes under MSVC AddressSanitizer. Native core checks
exercise byte transfer/merge, sparse/dense array edits, both endian save/reopen,
opaque payloads and the raw OS-marker output. Header tests retain the negative
AL free-count and empty allocated GG acceptance/rejection rules. The documented
legacy edge-case boundary is in the [collection map](../code-map/library-split.md#arraygroup-mutation-and-wire-headers).
No Marcus UI tests were added.

All 51 CLI baselines match: 45 movie/archive/constructed-legacy comparisons
retain exact output or the same rejection, and six asset indexes retain all
20,568 entries. Reports are `tmp/c89-collections-dump-comparison.json` and
`tmp/c89-collections-asset-comparison.json`.

The final builds are captured in
`tmp/c89-movie-data-regression/playback.sqlite`:

| Suite | Selection | Capture result / comparison verdict |
| --- | --- | --- |
| `20261111T230920Z-3f3b70ad` | `scrolling-text`, retail/SDL/Marcus seek and playback, one repetition | Six complete, no capture failures; verdict `different`, six different and five inconclusive components. |
| `20261111T231330Z-2dc96e5f` | `sampled-audio`, retail/SDL/Marcus playback, one repetition | Three complete, no capture failures; verdict `different`, five different and four inconclusive components. |

Compared with storage-slice suites `20261111T210623Z-1beefe45` and
`20261111T211008Z-843d3a11`, all 180 seek images match exactly and each player
retains the same 60 authored playback positions. Scrolling-text ordered exact
image changes remain retail/SDL 328 and Marcus 321; sampled audio remains 60
for each player. Only adjacent identical image/position records are collapsed;
raw timestamps and presentation samples remain available. PCM16 peaks remain
2380 for retail and 2176 for SDL and Marcus. The local report is
`tmp/c89-collections-playback-comparison.json`.

All nine final captures retain unchanged movie, ISO-resource and Marcus-content
hashes. Existing cross-player pixel/audio differences and timing/alignment
limitations remain; these checks establish preservation against earlier builds,
not complete retail parity. One repetition does not establish repeatability.
Earlier captures made before the final native assertion-level review remain in
the database, separately from the final suites listed above.

## C89 GST headers, streams and decode policy (12 November 2026)

GST20 header encoding/decoding, memory-stream storage and edits, live file-piece
lookup/fetch, DataBlock bounds/unpack transactions and packed-envelope decode
dispatch now share C89 implementations. Native callbacks still own codec
algorithms, HQ services and files; file-piece editing and container persistence
remain further migration work. This is another slice of the shared backend,
not completion of the engine conversion.

Eight standalone C-only checks and twenty focused native checks pass.
`engine-data-stream` covers independent GST layouts, checked ranges, decode
failure ownership, packed subranges, codec query/error/size behavior, allocation
fallback, growth hints, 3,000 stream-model edits, write locks/failures and piece
read poisoning/continuation. It also passes under MSVC AddressSanitizer.
Native checks cover real full-memory pack/unpack, memory/file decode failure
retention, mixed file/memory reads, edits and compaction. String checks cover
both endian orders, width-normalized encoding requests and empty allocated GST.
No Marcus UI tests were added.

The adapter fixes the old assignment-in-assert typo that failed valid
full-memory unpack in debug builds. Packed memory subranges remain unsupported;
their rejection now retains the owning handle safely. Historical stream growth
hints and file-limit shift bounds remain preserved. See the
[dependency boundary](../code-map/library-split.md#gst-headers-byte-streams-and-block-decoding).

All 51 saved CLI baselines match: 45 movie/archive/legacy outputs or rejection
results and six asset indexes covering 20,568 entries. The report is
`tmp/c89-data-stream-output-comparison.json`.

Frozen Studio/Marcus builds were captured with no concurrent builds in
`tmp/c89-movie-data-regression/playback.sqlite`:

| Suite | Selection | Capture result / comparison verdict |
| --- | --- | --- |
| `20261112T052155Z-259b682f` | `scrolling-text`, retail/SDL/Marcus seek and playback, one repetition | Six complete, no capture failures; verdict `different`, six different and five inconclusive components. |
| `20261112T052622Z-74b6d2ac` | `sampled-audio`, retail/SDL/Marcus playback, one repetition | Three complete, no capture failures; verdict `different`, five different and four inconclusive components. |

Against the previous collection-slice suites `20261111T230920Z-3f3b70ad` and
`20261111T231330Z-2dc96e5f`, all 180 seek images match exactly. All nine runs
retain the same 60 authored frame positions and ordered exact image changes:
scrolling text retail/SDL 328 and Marcus 321, sampled audio 60 for each player.
Only adjacent identical image/position records are collapsed; raw presentation
samples and timestamps remain available. PCM16 peaks remain 2380 for retail
and 2176 for SDL and Marcus. The local preservation report is
`tmp/c89-data-stream-playback-comparison.json`.

Movie, ISO-resource and Marcus-content hashes remain unchanged in every run.
Existing cross-player image/audio differences and timing/alignment limitations
remain. These captures establish preservation against the previous build,
not full retail parity, exact PCM equality or repeatability from one repetition.

## C89 KCDC/KCD2 codecs and packed encoding (12 November 2026)

KCDC and KCD2 encode/decode algorithms now live in the shared C89 backend.
Studio and Marcus use the same match selection, bitstream rules and bounded
decoder. Packed encoding also uses C89 for the envelope, codec query/dispatch
and minimum-saving rules. Native callbacks retain allocation diagnostics;
codec registration and HQ transactions remain C++ migration work.

Nine standalone C-only checks and twenty-one focused native checks pass. The
pre-migration native codec snapshot covers 768 format/input/capacity cases,
including exact encoder bytes, failures and successful decoded results. Its
1,349,303 bytes match the migrated implementation exactly. Local snapshots are
`tmp/c89-codec-before.bin` and `tmp/c89-codec-after.bin`; the frozen hash is
also checked by `engine-codec` without a C++ toolchain.

The new check includes independent wire fixtures for all four offset tiers,
overlapping back references, KCD2 literal splits, both scratch-allocation failure
points, envelope queries/errors and 12,000 malformed decoder inputs. It passes
under MSVC AddressSanitizer. Native core checks exercise the real allocator
failure hooks and DataBlock pack/unpack through both formats. The existing
thirteen codec round trips pass. No Marcus UI tests were added.

All 51 saved CLI baselines match: 45 movie/archive/legacy outputs or rejection
results and six asset indexes covering 20,568 entries. The report is
`tmp/c89-codec-output-comparison.json`. See the
[codec dependency boundary](../code-map/library-split.md#kcdckcd2-algorithms-and-packed-encoding)
for remaining C++ services and the preserved format boundary.

Both hosts were rebuilt and held fixed during capture, with no concurrent
builds. Evidence is in `tmp/c89-movie-data-regression/playback.sqlite`:

| Suite | Selection | Capture result / comparison verdict |
| --- | --- | --- |
| `20261112T061451Z-2eb68cb9` | `scrolling-text`, retail/SDL/Marcus seek and playback, one repetition | Six complete, no capture failures; verdict `different`, six different and five inconclusive components. |
| `20261112T061954Z-da0299a5` | `sampled-audio`, retail/SDL/Marcus playback, one repetition | Three complete, no capture failures; verdict `different`, five different and four inconclusive components. |

Compared with the preceding stream-slice suites `20261112T052155Z-259b682f`
and `20261112T052622Z-74b6d2ac`, all 180 seek images match exactly. Each player
retains the same 60 authored frame positions and ordered exact image changes:
scrolling text retail/SDL 328 and Marcus 321, sampled audio 60 for each player.
Only adjacent identical image/position records are collapsed; raw presentation
samples and timestamps remain preserved. PCM16 peak magnitudes remain 2380 for
retail and 2176 for SDL and Marcus. The local report is
`tmp/c89-codec-playback-comparison.json`.

All nine runs retain unchanged movie, ISO-resource and Marcus-content hashes.
Existing cross-player pixel/audio differences and timing/alignment limitations
remain. These checks establish preservation against the preceding build;
they do not establish full retail parity, exact PCM equality or repeatability
from a single repetition.

## C89 owning file-piece tables (12 November 2026)

FileByteStream now uses C89-owned piece storage, split/merge, memory/file
replacement, cross-stream copying, insert-before-delete rollback and compaction.
The native GeneralGroup object is removed from this path. Host callbacks retain
file references and positional I/O; general DataBlock windowed copying remains
a native adapter. The backend conversion is still incomplete.

Ten standalone C-only checks and twenty-two focused native checks pass.
`engine-piece-table` includes a frozen native snapshot hash for 180 mixed edits
and file/cross-stream copies. Its 601,476 pre-conversion bytes match the migrated
native output exactly (`tmp/c89-pieces-before.bin`, `tmp/c89-pieces-after.bin`).
Independent byte models, whole-piece copies, twelve allocation-failure positions
for each of three operations, temporary/read/write/copy failures, rollback and
balanced reference cleanup also pass under MSVC AddressSanitizer. Both C and
native checks preserve same-stream memory append without relocation.

Repeated single-file compaction now checks piece zero rather than the old
out-of-range `Cb(1)` lookup. Empty and small-memory compaction are also checked.
The existing file threshold, copy/reference choice, growth hints and best-effort
shrink remain. Full-stream writes retain the DataBlock lock/window protocol;
no new general partial-write or overlapping self-copy guarantees are claimed.
See the [piece-table dependency boundary](../code-map/library-split.md#owning-file-piece-tables).
No Marcus UI tests were added.

All 51 saved CLI baselines match: 45 movie/archive/legacy outputs or rejection
results and six asset indexes covering 20,568 entries. The local report is
`tmp/c89-pieces-output-comparison.json`.

Frozen Studio/Marcus builds were captured without concurrent builds. Evidence
is in `tmp/c89-movie-data-regression/playback.sqlite`:

| Suite | Selection | Capture result / comparison verdict |
| --- | --- | --- |
| `20261112T074206Z-11e2b8a2` | `scrolling-text`, retail/SDL/Marcus seek and playback, one repetition | Six complete, no capture failures; verdict `different`, six different and five inconclusive components. |
| `20261112T074704Z-feeb5715` | `sampled-audio`, retail/SDL/Marcus playback, one repetition | Three complete, no capture failures; verdict `different`, five different and four inconclusive components. |

Against the preceding codec-slice suites `20261112T061451Z-2eb68cb9` and
`20261112T061954Z-da0299a5`, all 180 seek images match exactly. All nine runs
retain the same 60 authored frame positions and ordered exact image changes:
scrolling text retail/SDL 328 and Marcus 321, sampled audio 60 for each player.
Only adjacent identical image/position records are collapsed; raw presentation
samples and timestamps remain available. PCM16 peak magnitudes remain 2380
for retail and 2176 for SDL and Marcus. The local report is
`tmp/c89-pieces-playback-comparison.json`.

Movie, ISO-resource and Marcus-content hashes are unchanged in every run.
Existing cross-player pixel/audio differences and timing/alignment limitations
remain. The captures establish preservation against the preceding build,
not complete retail parity, exact PCM equality or repeatability from one run.

## C89 block copying and opaque handle transactions (12 November 2026)

DataBlock byte/HQ I/O routing, memory/file copy dispatch, FileLocation copies,
handle detach/normalization, codec HQ transactions and block packing now use
shared C89 policy. Primitive allocation/lock/file services remain host callbacks;
object/file lifetime setters and temporary-file materialization remain further
migration work. The whole backend conversion is still incomplete.

Eleven standalone C-only checks and twenty-three focused native checks pass.
The 80-case native matrix covers zero/one-byte and 1024/1025/4096-byte copies,
all memory/file combinations, packed flags, sliced ranges and handle transfers.
All 99,296 pre-conversion bytes match the migrated native output exactly
(`tmp/c89-block-before.bin`, `tmp/c89-block-after.bin`).

`engine-block-io` independently checks that matrix, balanced locks/references,
failed reads/writes, query/allocation/conversion failures, physical capacity on
shrink failure, slice normalization, packing transactions and partial forward
file copies with full-buffer allocation or 1024-byte fallback. It passes under
MSVC AddressSanitizer. Native checks exercise real HQ allocation failure,
copy-buffer fallback, failed sliced/file packing and both standard codecs
through memory and file blocks. No Marcus UI tests were added.

The conversion retains allocation-dependent same-file overlap behavior,
best-effort shrink diagnostics, packed access rules and normalized memory
storage after failed sliced packing. See the
[block/handle boundary](../code-map/library-split.md#block-io-and-opaque-handle-transactions).
General temporary-file selection/materialization and FileByteStream destination
window orchestration remain native services.

All 51 saved CLI baselines match: 45 movie/archive/legacy outputs or rejection
results and six asset indexes covering 20,568 entries. The report is
`tmp/c89-block-output-comparison.json`.

Both hosts were rebuilt and held fixed, with no concurrent builds during
capture. Evidence is in `tmp/c89-movie-data-regression/playback.sqlite`:

| Suite | Selection | Capture result / comparison verdict |
| --- | --- | --- |
| `20261112T091731Z-f92446d1` | `scrolling-text`, retail/SDL/Marcus seek and playback, one repetition | Six complete, no capture failures; verdict `different`, six different and five inconclusive components. |
| `20261112T092213Z-c11c8a6b` | `sampled-audio`, retail/SDL/Marcus playback, one repetition | Three complete, no capture failures; verdict `different`, five different and four inconclusive components. |
| `20261112T092451Z-0d872b87` | Targeted retail-only sampled-audio repeat after peak variation | One complete; verdict `inconclusive`, with no cross-player comparisons. |

Against the preceding piece-table suites `20261112T074206Z-11e2b8a2` and
`20261112T074704Z-feeb5715`, all 180 seek images match exactly. All nine main
runs retain the same 60 authored frame positions and ordered exact image
changes: scrolling text retail/SDL 328 and Marcus 321, sampled audio 60 for
each player. Only adjacent identical image/position records are collapsed;
raw timestamps and presentation samples remain available. The local report is
`tmp/c89-block-playback-comparison.json`.

SDL/Marcus sampled-audio PCM16 peaks remain 2176. Retail's first main recording
peaks at 4333, with that larger peak confined to its first second; its peak
after the first second remains 2380. The targeted repeat returns to the earlier
overall peak of 2380 and preserves the same frame/image sequence. Both retail
recordings report no discontinuities or timestamp errors. Both recordings are
retained; this is reference-capture variability, not evidence of exact PCM
preservation. The audit is `tmp/c89-block-retail-audio-audit.json`.

All ten captures retain unchanged movie, ISO-resource and Marcus-content hashes.
Existing cross-player pixel/audio differences and timing/alignment limitations
remain. These checks establish frame/image preservation against the preceding
build, not full retail parity, exact PCM equality or general repeatability.

## C89 owning DataBlock state and materialization (12 November 2026)

DataBlock now stores a C89 owning record for its file/HQ/range/packed state.
Clear, adoption, retention, replacement, temporary-storage selection, file
materialization and pack/unpack commits now live in C. Host callbacks retain
physical allocation, file creation/I/O, reference-count primitives and debug
diagnostics. This remains a slice of the shared backend conversion.

Twelve standalone C-only checks and twenty-four focused native checks pass.
The native pre-conversion snapshot covers 24 size/force/packed combinations,
including zero length, memory slices and both sides of the 8-MiB threshold.
All 17,240 captured bytes match the migrated native output exactly
(`tmp/c89-owner-before.bin`, `tmp/c89-owner-after.bin`). The standalone
`engine-block-owner` check verifies its frozen native hash independently.

Failure checks cover memory allocation plus temporary-file creation failure,
forced-file selection, failed materialization writes, output preservation,
file read/codec failure, balanced locks/references, returned-file lifetime after
clearing the block and normalized sliced storage after failed packing. They
also pass under MSVC AddressSanitizer. Native checks exercise actual failed
HQ allocation falling back to a file without extending it before writing.
No Marcus UI tests were added.

The strict below-8-MiB memory attempt, packed-state preservation, zero-memory
versus zero-file distinction and existing-file reference semantics remain.
See the [ownership boundary](../code-map/library-split.md#owning-datablock-state-and-materialization).
Write-window orchestration, primitive services, file-text translation and
container persistence remain further migration work.

All 51 saved CLI baselines match: 45 movie/archive/legacy outputs or rejection
results and six asset indexes covering 20,568 entries. The local report is
`tmp/c89-owner-output-comparison.json`.

Frozen Studio/Marcus builds were captured with no concurrent builds. Evidence
is in `tmp/c89-movie-data-regression/playback.sqlite`:

| Suite | Selection | Capture result / comparison verdict |
| --- | --- | --- |
| `20261112T095017Z-fdf75549` | `scrolling-text`, retail/SDL/Marcus seek and playback, one repetition | Six complete, no capture failures; verdict `different`, six different and five inconclusive components. |
| `20261112T095527Z-185c32e2` | `sampled-audio`, retail/SDL/Marcus playback, one repetition | Three complete, no capture failures; verdict `different`, five different and four inconclusive components. |

Against preceding block-I/O suites `20261112T091731Z-f92446d1` and
`20261112T092213Z-c11c8a6b`, all 180 seek images match exactly. All nine runs
retain the same 60 authored frame positions and ordered exact image changes:
scrolling text retail/SDL 328 and Marcus 321, sampled audio 60 for each player.
Only adjacent identical image/position records are collapsed; raw presentation
samples and timestamps remain available. The preservation report is
`tmp/c89-owner-playback-comparison.json`.

Observed PCM16 peaks are 2380 for retail and 2176 for SDL/Marcus. Retail matches
the preceding reference audit `20261112T092451Z-0d872b87`; the earlier full-suite
reference's first-second peak variation remains recorded above. These peaks
establish audible capture, not exact PCM preservation or cross-player equality.

All nine captures retain unchanged movie, ISO-resource and Marcus-content hashes.
Existing cross-player pixel/audio differences and timing/alignment limitations
remain. The results establish frame/image preservation against the preceding
build, not full retail parity or repeatability from a single repetition.

## C89 piece write windows and file-text policy (12 November 2026)

FileByteStream destination-window orchestration and FileLocation text conversion
now use the shared C89 backend. Native callbacks still provide file/handle
primitives and code-page conversion. Piece writes pin both storage sections and
restore the destination view on success or failure. Text conversion retains
legacy BOM detection, encoding precedence and ownership changes, including BOM
stripping before a subsequent conversion failure.

Two unsafe legacy paths are bounded explicitly: a final piece that would exceed
the requested destination window is rejected before I/O, and failed full-input
allocation uses the actual 512-byte stack capacity rather than reading 1024
bytes into it. This adds neither general overlapping-copy guarantees nor new
text encodings, malformed-input repair or newline normalization. See the
[dependency boundary](../code-map/library-split.md#piece-write-windows-and-file-text-conversion).

Thirteen standalone C checks and twenty-five focused native checks pass.
The frozen native baseline contains 240 text/BOM/encoding cases and sixteen
supported memory/file write windows. All 117,346 bytes match after conversion;
the standalone check independently reproduces hash `0xedc4d066`.
Allocation, file creation/read/write, conversion-size and packed/window failure
checks verify cleanup, balanced locks/references and preserved destination
views. These checks also pass under MSVC AddressSanitizer. No Marcus UI tests
were added.

All 51 CLI baselines match, including six asset indexes covering 20,568 entries.
Local reports are `tmp/c89-file-policy-output-comparison.json` and the native
snapshots `tmp/c89-file-policy-before.bin` / `tmp/c89-file-policy-after.bin`.

Frozen Studio/Marcus builds were captured with no concurrent builds. Evidence
is in `tmp/c89-movie-data-regression/playback.sqlite`:

| Suite | Selection | Capture result / comparison verdict |
| --- | --- | --- |
| `20261112T103136Z-1ea36fb4` | `scrolling-text`, retail/SDL/Marcus seek and playback, one repetition | Six complete, no capture failures; verdict `different`, six different and five inconclusive components. |
| `20261112T103649Z-359097ec` | `sampled-audio`, retail/SDL/Marcus playback, one repetition | Three complete, no capture failures; verdict `different`, five different and four inconclusive components. |

Against the preceding owning-DataBlock suite `20261112T095017Z-fdf75549`, all
180 seek images match exactly. Scrolling playback retains the same 60 authored
frame positions and ordered exact image changes: retail/SDL 328 and Marcus 321.
Only adjacent identical image/position records are collapsed; raw presentation
samples and timestamps remain available.

Sampled-audio runs preserve all 60 authored frame positions and exact image
changes against `20261112T095527Z-185c32e2`. Observed PCM16 peaks remain 2380
for retail and 2176 for SDL/Marcus; these establish audible capture, not exact
PCM preservation. The nine-run report is
`tmp/c89-file-policy-playback-comparison.json`.

All nine captures retain unchanged movie, ISO-resource and Marcus-content hashes.
Existing cross-player pixel/audio differences and timing/alignment limitations
remain. These checks establish frame/image preservation against the preceding
build, not full retail parity or repeatability from one repetition.

## C89 CHN2 index persistence (12 November 2026)

Studio and Marcus now use C89 policy for CHN2 index/free-map positioning,
index-then-map-then-prefix commit order and explicit 128-byte v5/back-v4 header
serialization. Existing C89 collection serialization remains shared; native
callbacks perform physical I/O. File relocation, naming and recovery remain
future work. See the [persistence boundary](../code-map/library-split.md#chunk-index-persistence).

The native baseline freezes twenty complete save outputs across five payload
sizes, interior free maps, shortened indexes, save-a-copy and read-only save-as.
All 10,570 bytes match. Standalone C checks reproduce the twenty original
prefixes (FNV32 `0x012f9b07`), round-trip both byte orders, inject each commit
stage's failure and check required callbacks, position overflow and untouched
guards. A serialized index is allowed to exceed an individual collection
section's 128-MiB limit when its signed file positions fit.

The writer retains trailing physical bytes and the legacy partial-write failure
contract: a failed body write suppresses the prefix callback, but a failed prefix
write can already have modified bytes. No flush or atomic replacement guarantee
was added. No Marcus UI tests were added.

Thirteen standalone C checks, twenty-five focused native checks and MSVC
AddressSanitizer pass. All 51 CLI baselines match, including six asset indexes
covering 20,568 entries. Local evidence includes `tmp/c89-chunk-save-before.bin`,
`tmp/c89-chunk-save-after.bin` and `tmp/c89-chunk-save-output-comparison.json`.

Final Studio/Marcus builds were held fixed during capture. Evidence is in
`tmp/c89-movie-data-regression/playback.sqlite`:

| Suite | Selection | Capture result / comparison verdict |
| --- | --- | --- |
| `20261112T105451Z-d1ed0d49` | `scrolling-text`, retail/SDL/Marcus seek and playback, one repetition | Six complete, no capture failures; verdict `different`, six different and five inconclusive components. |
| `20261112T105918Z-d36600ef` | `sampled-audio`, retail/SDL/Marcus playback, one repetition | Three complete, no capture failures; verdict `different`, five different and four inconclusive components. |

Against preceding file-policy suites `20261112T103136Z-1ea36fb4` and
`20261112T103649Z-359097ec`, all 180 seek images match exactly. All nine runs
preserve the same 60 authored frame positions and ordered exact image changes:
scrolling retail/SDL 328 and Marcus 321, sampled audio 60 for each player.
Only adjacent identical image/position records are collapsed; raw presentation
samples and timestamps remain available. The report is
`tmp/c89-chunk-save-playback-comparison.json`.

Observed PCM16 peaks remain 2380 for retail and 2176 for SDL/Marcus. These
establish audible capture, not exact PCM preservation. All nine captures retain
unchanged movie, ISO-resource and Marcus-content hashes. Existing cross-player
pixel/audio differences and timing/alignment limitations remain; this is
preservation against the preceding build, not full retail parity or general
repeatability from one repetition.

## C89 whole-file save relocation and recovery (12 November 2026)

The shared C89 backend now chooses direct index commits versus compacted
temporary output, copies chunks from main/extra storage, updates index locations,
transfers file/map references and selects rename or name-swap completion. Late
failures restore a valid old main and retain the new payload as extra storage;
without a valid old CHN2 main the new temporary remains attached. Both paths are
checked through a successful save retry. Native services resolve names and
perform file operations. See the [save boundary](../code-map/library-split.md#whole-file-save-relocation-and-recovery).

All 13,190 bytes of thirty pre-conversion saved files match after migration.
Cases include five payload sizes, free maps, shortened indexes, save-a-copy,
read-only save-as, modified extra data and explicit-current-name saves. Standalone
C checks inject each file-creation/resize/copy/commit/naming failure and verify
reference/map cleanup, unchanged early-failure state, recovered offsets/flags,
empty files, temporary status and overflow rejection. They add no scratch
allocation or second index to the save operation.

Fourteen standalone C checks, twenty-six focused native checks and MSVC
AddressSanitizer pass. All 51 CLI baselines match, including six asset indexes
covering 20,568 entries. Local evidence includes
`tmp/c89-chunk-relocate-before.bin`, `tmp/c89-chunk-relocate-after.bin` and
`tmp/c89-chunk-relocate-output-comparison.json`.

The recovery protocol preserves legacy partial-write behavior and does not
promise filesystem atomicity. Separate save-a-copy insertion, general editing
and free-space allocation remain native. No Marcus UI tests were added.

Frozen Studio/Marcus builds were captured without concurrent builds. Evidence
is in `tmp/c89-movie-data-regression/playback.sqlite`:

| Suite | Selection | Capture result / comparison verdict |
| --- | --- | --- |
| `20261112T112715Z-3899fba6` | `scrolling-text`, retail/SDL/Marcus seek and playback, one repetition | Six complete, no capture failures; verdict `different`, six different and five inconclusive components. |
| `20261112T113021Z-db156802` | `sampled-audio`, retail/SDL/Marcus playback, one repetition | Three complete, no capture failures; verdict `different`, five different and four inconclusive components. |

Against preceding index-commit suites `20261112T105451Z-d1ed0d49` and
`20261112T105918Z-d36600ef`, all 180 seek images match exactly. All nine runs
preserve the same 60 authored frame positions and ordered exact image changes:
scrolling retail/SDL 328 and Marcus 321, sampled audio 60 for each player.
Only adjacent identical image/position records are collapsed; raw presentation
samples and timestamps remain available. The report is
`tmp/c89-chunk-relocate-playback-comparison.json`.

Observed PCM16 peaks remain 2380 for retail and 2176 for SDL/Marcus. These
establish audible capture, not exact PCM preservation. All nine captures retain
unchanged movie, ISO-resource and Marcus-content hashes. Existing cross-player
pixel/audio differences and timing/alignment limitations remain; this is
preservation against the preceding build, not full retail parity or general
repeatability from one repetition.

## C89 save-a-copy and chunk heap policy (12 November 2026)

Save-a-copy now uses C89 for source pinning, checked compacted locations,
payload-copy/index-insertion order and the final commit after unpinning.
Original fixed metadata and variable child/name bytes are transferred unchanged,
with only offset/size/on-extra state replaced. Empty chunks still invoke copy
and retain the running destination offset, preserving the distinction from
whole-file relocation's zero offset.

First-fit free-space allocation, partial/exact hole consumption, adjacent-hole
coalescing and logical-tail trimming also use C89. File selection, lazy map
loading, primitive map operations and physical I/O remain native services.
Append reuses physical trailing bytes before extending a file; freeing never
truncates physical storage. Interior free-map allocation failure still drops the
space without making chunk deletion fail. See the
[dependency boundary](../code-map/library-split.md#save-a-copy-and-chunk-heap-policy).

The native snapshot matches all 548,265 bytes before and after conversion:
saved-file outputs with names/children plus every live chunk's location across
1,200 deterministic main/extra-storage add/replace/delete operations. Standalone
C heap checks compare 12,000 edits against an independent occupied-byte model
and validate the complete free map after each edit. Copy/insertion/commit,
file-growth and map-allocation failures verify source preservation, balanced
pinning, unchanged failed allocation outputs, nonfatal dropped free space and
position/overlap guards.

Fifteen standalone C checks, twenty-seven focused native checks and both MSVC
AddressSanitizer checks pass. All 51 CLI baselines match, including six asset
indexes covering 20,568 entries. Local evidence includes
`tmp/c89-chunk-heap-before.bin`, `tmp/c89-chunk-heap-after.bin` and
`tmp/c89-chunk-heap-output-comparison.json`. No Marcus UI tests were added.

Frozen Studio/Marcus builds were captured without concurrent builds. Evidence
is in `tmp/c89-movie-data-regression/playback.sqlite`:

| Suite | Selection | Capture result / comparison verdict |
| --- | --- | --- |
| `20261112T114441Z-31358a76` | `scrolling-text`, retail/SDL/Marcus seek and playback, one repetition | Six complete, no capture failures; verdict `different`, six different and five inconclusive components. |
| `20261112T114750Z-2bcfa83e` | `sampled-audio`, retail/SDL/Marcus playback, one repetition | Three complete, no capture failures; verdict `different`, five different and four inconclusive components. |

Against preceding relocation suites `20261112T112715Z-3899fba6` and
`20261112T113021Z-db156802`, all 180 seek images match exactly. All nine runs
preserve the same 60 authored frame positions and ordered exact image changes:
scrolling retail/SDL 328 and Marcus 321, sampled audio 60 for each player.
Only adjacent identical image/position records are collapsed; raw presentation
samples and timestamps remain available. The report is
`tmp/c89-chunk-heap-playback-comparison.json`.

Observed PCM16 peaks remain 2380 for retail and 2176 for SDL/Marcus. These
establish audible capture, not exact PCM preservation. All nine captures retain
unchanged movie, ISO-resource and Marcus-content hashes. Existing cross-player
pixel/audio differences and timing/alignment limitations remain; this is
preservation against the preceding build, not full retail parity or general
repeatability from one repetition.

## C89 chunk insertion, replacement and deletion policy (12 November 2026)

Unique-number selection, low-level add and replace/create rollback, payload
commit, runtime-ID invalidation selection, reference decrement and deletion
visitor decisions now use C89. Replacement preserves names, children, parent
counts and loner/packed/forest flags. Existing allocation failure retains an output block; write
failure clears it and releases new storage. Failed new writes remove the new
record. Deletion retains shared descendants and loners. Its low-memory fallback
drops child references in reverse order without recursively deleting those
children, retaining legacy behavior rather than adding orphan repair.

The native graph enumerator still supplies traversal events and stack storage;
child-link editing and other graph operations remain further migration work.
See the [mutation boundary](../code-map/library-split.md#chunk-insertion-replacement-and-deletion-policy).

All 550,737 baseline bytes match: the preceding main/extra heap and saved-file
cases plus named graphs with shared descendants, duplicate child links, retained
loner subtrees, replacement preserving children and cascading child deletion.
Standalone C checks inject insertion/allocation/write failures, assert exact
callback/output/runtime-reset order and verify deletion/reference decisions,
including a simulated traversal-memory failure.

Sixteen standalone C checks, twenty-eight focused native checks and MSVC
AddressSanitizer pass. All 51 CLI baselines match, including six asset indexes
covering 20,568 entries. Local evidence includes `tmp/c89-chunk-edit-before.bin`,
`tmp/c89-chunk-edit-after.bin` and `tmp/c89-chunk-edit-output-comparison.json`.
No Marcus UI tests were added.

Frozen Studio/Marcus builds were captured without concurrent builds. Evidence
is in `tmp/c89-movie-data-regression/playback.sqlite`:

| Suite | Selection | Capture result / comparison verdict |
| --- | --- | --- |
| `20261112T120259Z-5d079f4d` | `scrolling-text`, retail/SDL/Marcus seek and playback, one repetition | Six complete, no capture failures; verdict `different`, six different and five inconclusive components. |
| `20261112T120607Z-933904f5` | `sampled-audio`, retail/SDL/Marcus playback, one repetition | Three complete, no capture failures; verdict `different`, five different and four inconclusive components. |

Against preceding copy/heap suites `20261112T114441Z-31358a76` and
`20261112T114750Z-2bcfa83e`, all 180 seek images match exactly. All nine runs
preserve the same 60 authored frame positions and ordered exact image changes:
scrolling retail/SDL 328 and Marcus 321, sampled audio 60 for each player.
Only adjacent identical image/position records are collapsed; raw presentation
samples and timestamps remain available. The report is
`tmp/c89-chunk-edit-playback-comparison.json`.

Observed PCM16 peaks remain 2380 for retail and 2176 for SDL/Marcus. These
establish audible capture, not exact PCM preservation. All nine captures retain
unchanged movie, ISO-resource and Marcus-content hashes. Existing cross-player
pixel/audio differences and timing/alignment limitations remain; this is
preservation against the preceding build, not full retail parity or general
repeatability from one repetition.

## C89 graph traversal and child-link editing (12 November 2026)

The shared backend now owns graph traversal state, lazy stack storage, skip and
pre/post/root/error events, descendant searches, child adoption, count-limit
rollback, unlink/final-reference deletion and child-ID reordering. Native
facades retain record lookup/transfer and diagnostic services. The native
DynamicArray stack object allocation is removed; stack reserve/growth failure
still emits PRE|POST|ERROR and resumes siblings. Stack storage is released at
completion, root skip, reset and disposal.

Duplicate adoption leaves existing loner state unchanged, child-ID moves retain
(slot,type,number) order, and count-limit failures roll back inserted child bytes.
Descendant search retains the zero-reference shortcut and match-before-error
ordering. It adds no global shared-node deduplication or stronger cycle policy.
See the [graph boundary](../code-map/library-split.md#graph-traversal-and-child-link-editing).

All 568,649 pre-conversion baseline bytes match. Added cases record graph events
and parent identities over eight skip/reuse modes, shared subgraphs, child-ID
moves and a 48-node chain with cascading deletion. Invalid root parent/slot
fields are normalized, as they are not defined by the native API. Standalone C
checks compare traversal against an independent recursive reference, relocate
stack allocations, force initial/growth failure and SPEED fallback, and check
reset/early exit/cleanup. Child-link cases cover duplicates, both count limits,
insertion failure, cycle-risk/unknown results, order conflicts and final unlink.

Seventeen standalone C checks, twenty-nine focused native checks and both MSVC
AddressSanitizer checks pass. All 51 CLI baselines match, including six asset
indexes covering 20,568 entries. Local evidence includes
`tmp/c89-chunk-graph-before.bin`, `tmp/c89-chunk-graph-after.bin` and
`tmp/c89-chunk-graph-output-comparison.json`. No Marcus UI tests were added.

Frozen Studio/Marcus builds were captured without concurrent builds. Evidence
is in `tmp/c89-movie-data-regression/playback.sqlite`:

| Suite | Selection | Capture result / comparison verdict |
| --- | --- | --- |
| `20261112T123354Z-18aaa6c9` | `scrolling-text`, retail/SDL/Marcus seek and playback, one repetition | Six complete, no capture failures; verdict `different`, six different and five inconclusive components. |
| `20261112T123837Z-ed38be21` | `sampled-audio`, retail/SDL/Marcus playback, one repetition | Three complete, no capture failures; verdict `different`, five different and four inconclusive components. |

Against preceding mutation suites `20261112T120259Z-5d079f4d` and
`20261112T120607Z-933904f5`, all 180 seek images match exactly. All nine runs
preserve the same 60 authored frame positions and ordered exact image changes:
scrolling retail/SDL 328 and Marcus 321, sampled audio 60 for each player.
Only adjacent identical image/position records are collapsed; raw presentation
samples and timestamps remain available. The report is
`tmp/c89-chunk-graph-playback-comparison.json`.

Observed PCM16 peaks remain 2380 for retail and 2176 for SDL/Marcus. These
establish audible capture, not exact PCM preservation. All nine captures retain
unchanged movie, ISO-resource and Marcus-content hashes. Existing cross-player
pixel/audio differences and timing/alignment limitations remain; this is
preservation against the preceding build, not full retail parity or general
repeatability from one repetition.

## C89 swaps, moves and graph copy/clone (12 November 2026)

Payload/child swaps, identity moves with parent-link repair, graph copy/clone,
the owning source/destination number map, runtime-based matching and runtime-ID
allocation now use C89. Payload transfers retain existing packed-block policy;
name conversion and runtime-table primitives remain native services. Clones
avoid pre-existing destination reuse while retaining sharing inside the clone.
Matching retains candidate-number and traversal/RTI/slot/count policy rather
than introducing a stronger graph-isomorphism check. See the
[operation boundary](../code-map/library-split.md#chunk-swaps-moves-and-graph-copyclone).

All 572,367 native baseline bytes match, including repeated copy reuse, clones,
changed descendants, same-file operations, swaps and referenced moves in both
directions. Standalone checks cover sharing topology, packed/forest/loner flags,
metadata, debug/release parent scans, runtime invalidation/preservation, maximum
candidate IDs and best-effort runtime assignment. Payload/name/link/traversal and
initial/growth map failures preserve the legacy partial cleanup, including a
new loner left after first map failure and an unlinked node left after later
map growth failure. This is not an atomic destination transaction.

Eighteen standalone C checks, thirty focused native checks and MSVC
AddressSanitizer pass. All 51 CLI baselines match, including six asset indexes
covering 20,568 entries. Local evidence includes `tmp/c89-chunk-ops-before.bin`,
`tmp/c89-chunk-ops-after.bin` and `tmp/c89-chunk-ops-output-comparison.json`.
No Marcus UI tests were added.

Frozen Studio/Marcus builds were captured without concurrent builds. Evidence
is in `tmp/c89-movie-data-regression/playback.sqlite`:

| Suite | Selection | Capture result / comparison verdict |
| --- | --- | --- |
| `20261112T130924Z-129e07bf` | `scrolling-text`, retail/SDL/Marcus seek and playback, one repetition | Six complete, no capture failures; verdict `different`, six different and five inconclusive components. |
| `20261112T131443Z-a46b646a` | `sampled-audio`, retail/SDL/Marcus playback, one repetition | Three complete, no capture failures; verdict `different`, five different and four inconclusive components. |

Against preceding graph/link suites `20261112T123354Z-18aaa6c9` and
`20261112T123837Z-ed38be21`, all 180 seek images match exactly. All nine runs
preserve the same 60 authored frame positions and ordered exact image changes:
scrolling retail/SDL 328 and Marcus 321, sampled audio 60 for each player.
Only adjacent identical image/position records are collapsed; raw presentation
samples and timestamps remain available. The report is
`tmp/c89-chunk-ops-playback-comparison.json`.

Observed PCM16 peaks remain 2380 for retail and 2176 for SDL/Marcus. These
establish audible capture, not exact PCM preservation. All nine captures retain
unchanged movie, ISO-resource and Marcus-content hashes. Existing cross-player
pixel/audio differences and timing/alignment limitations remain; this is
preservation against the preceding build, not full retail parity or general
repeatability from one repetition.

## C89 runtime metadata, string wire policy and codec registry (12 November 2026)

Compact-index runtime-ID storage, chunk-name resize/copy/decode policy,
serialized String read/write policy and codec registration now use C89.
Reopen transfers cache ownership explicitly. Native code-page conversion,
expanded-index fixed records, codec dispatch and physical storage remain host
services. See the [implementation boundary](../code-map/library-split.md#runtime-ids-chunk-names-serialized-strings-and-codec-registry).

All 572,367 native chunk baseline bytes match. The new frozen serialized-string
snapshot also matches: 480 cases, 29,532 bytes, FNV32 `54d0f109`. It covers four
encodings, maximum-length strings, malformed/truncated data, trailing bytes,
optional ending offsets and real memory/file-backed blocks at nonzero offsets.
Standalone checks cover sorted runtime updates/deletion, ownership swaps,
pinned name edits and failed growth, codec priority/reference balance,
allocation failures/SPEED fallback, read/write failure stages and UTF-16/
big-endian output. The C owner-swap boundary is covered; forced native reopen
rollback is not exercised.

Nineteen standalone C checks, thirty-one focused native checks and MSVC
AddressSanitizer pass. All 51 CLI baselines match, including six asset indexes
covering 20,568 entries. Local evidence includes `tmp/c89-metadata-before.bin`,
`tmp/c89-metadata-after.bin`, `tmp/c89-string-wire-before.bin`,
`tmp/c89-string-wire-after.bin` and `tmp/c89-metadata-output-comparison.json`.
No Marcus UI tests were added.

Frozen Studio/Marcus builds were captured without concurrent builds. Evidence
is in `tmp/c89-movie-data-regression/playback.sqlite`:

| Suite | Selection | Capture result / comparison verdict |
| --- | --- | --- |
| `20261112T134043Z-39fa9d05` | `scrolling-text`, retail/SDL/Marcus seek and playback, one repetition | Six complete, no capture failures; verdict `different`, six different and five inconclusive components. |
| `20261112T134405Z-0e241982` | `sampled-audio`, retail/SDL/Marcus playback, one repetition | Three complete, no capture failures; verdict `different`, five different and four inconclusive components. |

Against preceding graph-operation suites `20261112T130924Z-129e07bf` and
`20261112T131443Z-a46b646a`, all 180 seek images match exactly. All nine runs
preserve the same 60 authored frame positions and ordered exact image changes:
scrolling retail/SDL 328 and Marcus 321, sampled audio 60 for each player.
Only adjacent identical image/position records are collapsed; raw presentation
samples and timestamps remain available. The report is
`tmp/c89-metadata-playback-comparison.json`.

Observed PCM16 peaks remain 2380 for retail and 2176 for SDL/Marcus. These
establish audible capture, not exact PCM preservation. All nine captures retain
unchanged movie, ISO-resource and Marcus-content hashes. Existing cross-player
pixel/audio differences and timing/alignment limitations remain; this is
preservation against the preceding build, not full retail parity or general
repeatability from one repetition.

## C89 embedded forest export/import (12 November 2026)

Embedded forest descriptor encoding/decoding, preorder serialization, the owning
import parent stack and child reconstruction now use C89. Native facades retain
destination container/file creation, payload transfer and index insertion
primitives. Copy imports retain the legacy PACKED-only flag policy; borrowed-data
imports retain PACKED|FOREST. Shared descendants become separate tree occurrences.
See the [implementation boundary](../code-map/library-split.md#embedded-forest-exportimport).

All 1,594 native forest baseline bytes match across both byte orders, packed and
empty payloads, shared descendants, copy/borrow imports, truncated streams,
invalid byte order/length/child counts, empty forests and opaque OSK values.
Native checks also assert success/rejection, counts and flags without snapshot
recording. The standalone export freezes 118 bytes (FNV32 `2c02e752`). C tests
cover parent/slot reconstruction, size-only traversal, read/add/adopt/write/copy
failures, allocation failures/SPEED fallback and cleanup, signed size/position
limits, and stack relocation/growth over a 200-level tree.

Twenty standalone C checks, thirty-two focused native checks and MSVC
AddressSanitizer pass. Existing 572,367-byte chunk and 29,532-byte string snapshots
also remain exact. All 51 CLI baselines match, including six asset indexes covering
20,568 entries. Evidence includes `tmp/c89-forest-before.bin`,
`tmp/c89-forest-after.bin`, `tmp/c89-forest-chunks-after.bin`,
`tmp/c89-forest-strings-after.bin` and `tmp/c89-forest-output-comparison.json`.
No Marcus UI tests were added.

Frozen Studio/Marcus builds were captured without concurrent builds. Evidence
is in `tmp/c89-movie-data-regression/playback.sqlite`:

| Suite | Selection | Capture result / comparison verdict |
| --- | --- | --- |
| `20261112T135253Z-4fcb1b89` | `scrolling-text`, retail/SDL/Marcus seek and playback, one repetition | Six complete, no capture failures; verdict `different`, six different and five inconclusive components. |
| `20261112T135623Z-445ef1e3` | `sampled-audio`, retail/SDL/Marcus playback, one repetition | Three complete, no capture failures; verdict `different`, five different and four inconclusive components. |

Against preceding metadata/registry suites `20261112T134043Z-39fa9d05` and
`20261112T134405Z-0e241982`, all 180 seek images match exactly. All nine runs
preserve the same 60 authored frame positions and ordered exact image changes:
scrolling retail/SDL 328 and Marcus 321, sampled audio 60 for each player.
Only adjacent identical image/position records are collapsed; raw presentation
samples and timestamps remain available. The report is
`tmp/c89-forest-playback-comparison.json`.

Observed PCM16 peaks remain 2380 for retail and 2176 for SDL/Marcus. These
establish audible capture, not exact PCM preservation. All nine captures retain
unchanged movie, ISO-resource and Marcus-content hashes. Existing cross-player
pixel/audio differences and timing/alignment limitations remain; this is
preservation against the preceding build, not full retail parity or general
repeatability from one repetition.

## C89 owning free maps and index materialization (12 November 2026)

Free-map ownership/storage, lazy loading/discard policy and serialization now use
C89. Save/reopen transitions transfer unique C map owners instead of native
DynamicArray references. Index loading reuses C scratch storage for child records
and encoded names; native callbacks retain SZS conversion and writable-group
insertion. The C++ vector/exception path for row assembly is removed. See the
[implementation boundary](../code-map/library-split.md#index-materialization-and-owning-free-maps).

All 2,192 new native lazy-load baseline bytes match over valid, swapped,
invalid-width/order, truncated and absent maps, including resulting allocation
positions and saves. The previous 572,367-byte chunk, 29,532-byte string and
1,594-byte forest snapshots also match. Standalone checks compare independent
map wire bytes in both byte orders, empty/null maps, lazy-state publication before
reads, malformed headers/lengths, failed I/O, allocation failures and SPEED
fallback. Materialization checks cover compact/expanded index rows, child packing,
pre-v3 names of 0/1/255/256/516 bytes and failed insertion/conversion/scratch
allocation. Matching-native-order fixtures retain the existing expanded-index
pre-swap compatibility policy; this does not establish new foreign-endian support.

Twenty-one standalone C checks, thirty-three focused native checks and MSVC
AddressSanitizer pass. All 51 CLI baselines match, including six asset indexes
covering 20,568 entries. Evidence includes `tmp/c89-loading-before.bin`,
`tmp/c89-loading-after.bin`, `tmp/c89-loading-chunks-after.bin`,
`tmp/c89-loading-strings-after.bin`, `tmp/c89-loading-forest-after.bin` and
`tmp/c89-loading-output-comparison.json`. No Marcus UI tests were added.

Frozen Studio/Marcus builds were captured without concurrent builds. Evidence
is in `tmp/c89-movie-data-regression/playback.sqlite`:

| Suite | Selection | Capture result / comparison verdict |
| --- | --- | --- |
| `20261112T141135Z-67446bce` | `scrolling-text`, retail/SDL/Marcus seek and playback, one repetition | Six complete, no capture failures; verdict `different`, six different and five inconclusive components. |
| `20261112T141639Z-32717cc3` | `sampled-audio`, retail/SDL/Marcus playback, one repetition | Three complete, no capture failures; verdict `different`, five different and four inconclusive components. |

Against preceding forest suites `20261112T135253Z-4fcb1b89` and
`20261112T135623Z-445ef1e3`, all 180 seek images match exactly. All nine runs
preserve the same 60 authored frame positions and ordered exact image changes:
scrolling retail/SDL 328 and Marcus 321, sampled audio 60 for each player.
Only adjacent identical image/position records are collapsed; raw presentation
samples and timestamps remain available. The report is
`tmp/c89-loading-playback-comparison.json`.

Observed PCM16 peaks remain 2380 for retail and 2176 for SDL/Marcus. These
establish audible capture, not exact PCM preservation. All nine captures retain
unchanged movie, ISO-resource and Marcus-content hashes. Existing cross-player
pixel/audio differences and timing/alignment limitations remain; this is
preservation against the preceding build, not full retail parity or general
repeatability from one repetition. After captures, a whitespace correction and
removal of an unreachable native cleanup label were rebuilt and rechecked;
no C policy or executed native logic changed.

## C89 storage selection and read-through caching (12 November 2026)

Main/extra allocation selection, empty-allocation rules, cache copy/publication,
failed-copy extent release and read-through fetch fallback now use C89. Native
callbacks provide physical file services, C heap access and fixed-record updates.
Only offset/on-extra change after a successful cache move; runtime IDs and other
metadata are preserved. The historical empty-cache extra-file behavior remains
explicit. See the [implementation boundary](../code-map/library-split.md#storage-selection-and-read-through-caching).

All 1,740 native routing baseline bytes match across read-only/write/read-through/
add-to-extra modes and subsequent saves. The previous 572,367-byte chunk,
2,192-byte loading, 29,532-byte string and 1,594-byte forest snapshots also match.
Native checks verify graph-copy reuse across a cache move. Standalone checks
cover add/force/unread/empty combinations, size guards, creation/reservation/copy
failures, failed-copy cleanup, retained extra files, repeated caching, metadata
preservation, read fallback/retry and diagnostic trace ordering.

Twenty-two standalone C checks, thirty-four focused native checks and MSVC
AddressSanitizer pass. Native statistics and expanded-index variants compile
with `/Zs`; a pre-existing duplicate mask alias was corrected in the expanded
branch. This does not establish expanded-index runtime compatibility. All 51 CLI
baselines match, including six asset indexes covering 20,568 entries. Evidence
includes `tmp/c89-routing-before.bin`, `tmp/c89-routing-after.bin`,
`tmp/c89-routing-chunks-after.bin`, `tmp/c89-routing-loading-after.bin`,
`tmp/c89-routing-strings-after.bin`, `tmp/c89-routing-forest-after.bin` and
`tmp/c89-routing-output-comparison.json`. No Marcus UI tests were added.

Frozen Studio/Marcus builds were captured without concurrent builds. Evidence
is in `tmp/c89-movie-data-regression/playback.sqlite`:

| Suite | Selection | Capture result / comparison verdict |
| --- | --- | --- |
| `20261112T142853Z-e891a5e5` | `scrolling-text`, retail/SDL/Marcus seek and playback, one repetition | Six complete, no capture failures; verdict `different`, six different and five inconclusive components. |
| `20261112T143448Z-5be8800c` | `sampled-audio`, retail/SDL/Marcus playback, one repetition | Three complete, no capture failures; verdict `different`, five different and four inconclusive components. |

Against preceding loading suites `20261112T141135Z-67446bce` and
`20261112T141639Z-32717cc3`, all 180 seek images match exactly. All nine runs
preserve the same 60 authored frame positions and ordered exact image changes:
scrolling retail/SDL 328 and Marcus 321, sampled audio 60 for each player.
Only adjacent identical image/position records are collapsed; raw presentation
samples and timestamps remain available. The report is
`tmp/c89-routing-playback-comparison.json`.

Observed PCM16 peaks remain 2380 for retail and 2176 for SDL/Marcus. These
establish audible capture, not exact PCM preservation. All nine captures retain
unchanged movie, ISO-resource and Marcus-content hashes. Existing cross-player
pixel/audio differences and timing/alignment limitations remain; this is
preservation against the preceding build, not full retail parity or general
repeatability from one repetition.

## C89 container options, flags and reopen ownership (12 November 2026)

Container-to-file option mapping, sticky options, mark/zero-reference cleanup,
packed/forest/loner mutation and reopen ownership sequencing now use C89. The
reopen transaction publishes detached or restored owners before index I/O or
cleanup, restores prior index/file/map/runtime owners on failure, and releases
replaced or rejected owners. Deferred-map offset/size fields retain the legacy
last-read behavior outside rollback. Native container construction/disposal,
registry storage and resource ownership remain. See the
[implementation boundary](../code-map/library-split.md#container-options-flags-and-reopen-ownership).

All 56 native lifecycle snapshot bytes match. Native checks cover no-op/changed
flag invalidation, graph-copy reuse, read-only reopen success versus writable
denial, mark retention, repeated open and final unmarked closure. The previous
572,367-byte chunk, 1,740-byte routing, 2,192-byte loading, 29,532-byte string and
1,594-byte forest snapshots also match. Standalone tests cover 262,144 option/
state/mask/file-flag/failure combinations, registry unlink order, release limits,
loner deletion, and forced read/validation failure in the C reopen transaction.
Allocated maps/runtime caches verify restoration and complete cleanup, with and
without a compact cache. Forced failure in the real native file/index services
is not covered; native integration verifies successful reopen and guard denial.

Twenty-three standalone C checks, thirty-five focused native checks and both
MSVC AddressSanitizer checks pass. Native statistics and expanded-index variants
compile with `/Zs`; expanded-index runtime remains unverified. All 51 CLI
baselines match, including six asset indexes covering 20,568 entries. Evidence
includes `tmp/c89-lifecycle-before.bin`, `tmp/c89-lifecycle-after.bin`,
`tmp/c89-lifecycle-chunks-after.bin`, `tmp/c89-lifecycle-routing-after.bin`,
`tmp/c89-lifecycle-loading-after.bin`, `tmp/c89-lifecycle-strings-after.bin`,
`tmp/c89-lifecycle-forest-after.bin` and `tmp/c89-lifecycle-output-comparison.json`.
No Marcus UI tests were added.

Frozen Studio/Marcus builds were captured without concurrent builds. Evidence
is in `tmp/c89-movie-data-regression/playback.sqlite`:

| Suite | Selection | Capture result / comparison verdict |
| --- | --- | --- |
| `20261112T145706Z-f201f768` | `scrolling-text`, retail/SDL/Marcus seek and playback, one repetition | Six complete, no capture failures; verdict `different`, six different and five inconclusive components. |
| `20261112T150023Z-b2f27712` | `sampled-audio`, retail/SDL/Marcus playback, one repetition | Three complete, no capture failures; verdict `different`, five different and four inconclusive components. |

Against preceding storage-routing suites `20261112T142853Z-e891a5e5` and
`20261112T143448Z-5be8800c`, all 180 seek images match exactly. All nine runs
preserve the same 60 authored frame positions and ordered exact image changes:
scrolling retail/SDL 328 and Marcus 321, sampled audio 60 for each player.
Only adjacent identical image/position records are collapsed; raw presentation
samples and timestamps remain available. The report is
`tmp/c89-lifecycle-playback-comparison.json`.

Observed PCM16 peaks remain 2380 for retail and 2176 for SDL/Marcus. These
establish audible capture, not exact PCM preservation. All nine captures retain
unchanged movie, ISO-resource and Marcus-content hashes. Existing cross-player
pixel/audio differences and timing/alignment limitations remain; this is
preservation against the preceding build, not full retail parity or general
repeatability from one repetition.

## C89 container construction, lookup and disposal (12 November 2026)

Open/create sequencing, existing-owner matching and associated-resource disposal
now use C89. Existing opens apply options before retaining; creation forces write,
creates index then file, and reserves the 128-byte prefix. Failed new construction
releases its partial owner before reporting failure. Initial option failure on a
new owner remains diagnostic-only, matching legacy release behavior. Disposal
clears/publishes the owner view, then releases main file/map, extra file/map,
index and runtime storage in legacy order. Native class/registry backing and
forest destination setup remain, along with resource-cache ownership. See the
[implementation boundary](../code-map/library-split.md#container-construction-lookup-and-disposal).

All 597 native construction baseline bytes match across ordinary, temporary and
same-directory temporary creation and saves. Native checks verify repeated open,
corrupt-file rejection and temporary deletion. The prior 572,367-byte chunk,
56-byte lifecycle, 1,740-byte routing, 2,192-byte loading, 29,532-byte string and
1,594-byte forest snapshots also match. Standalone checks cover owner/index/file/
resize/read/validation failures, including readers leaving partial resources,
existing-open failures and creation rejection, initial-option diagnostics,
shared file references, cleared-state publication, locked-owner guards and
repeated empty disposal. Lookup checks cover invalid-main wrappers and extra-file
collision diagnostics. Native shell allocation and name resolution stay adapters.

Twenty-four standalone C checks, thirty-six focused native checks and MSVC
AddressSanitizer pass. Native statistics and expanded-index variants compile
with `/Zs`; expanded-index runtime remains unverified. All 51 CLI baselines match,
including six asset indexes covering 20,568 entries. Evidence includes
`tmp/c89-container-before.bin`, `tmp/c89-container-after.bin`,
`tmp/c89-container-chunks-after.bin`, `tmp/c89-container-lifecycle-after.bin`,
`tmp/c89-container-routing-after.bin`, `tmp/c89-container-loading-after.bin`,
`tmp/c89-container-strings-after.bin`, `tmp/c89-container-forest-after.bin` and
`tmp/c89-container-output-comparison.json`. No Marcus UI tests were added.

Frozen Studio/Marcus builds were captured without concurrent builds. Evidence
is in `tmp/c89-movie-data-regression/playback.sqlite`:

| Suite | Selection | Capture result / comparison verdict |
| --- | --- | --- |
| `20261112T152255Z-f6ca2e8e` | `scrolling-text`, retail/SDL/Marcus seek and playback, one repetition | Six complete, no capture failures; verdict `different`, six different and five inconclusive components. |
| `20261112T152648Z-013c24c1` | `sampled-audio`, retail/SDL/Marcus playback, one repetition | Three complete, no capture failures; verdict `different`, five different and four inconclusive components. |

Against preceding lifecycle suites `20261112T145706Z-f201f768` and
`20261112T150023Z-b2f27712`, all 180 seek images match exactly. All nine runs
preserve the same 60 authored frame positions and ordered exact image changes:
scrolling retail/SDL 328 and Marcus 321, sampled audio 60 for each player.
Only adjacent identical image/position records are collapsed; raw presentation
samples and timestamps remain available. The report is
`tmp/c89-container-playback-comparison.json`.

Observed PCM16 peaks remain 2380 for retail and 2176 for SDL/Marcus. These
establish audible capture, not exact PCM preservation. All nine captures retain
unchanged movie, ISO-resource and Marcus-content hashes. Existing cross-player
pixel/audio differences and timing/alignment limitations remain; this is
preservation against the preceding build, not full retail parity or general
repeatability from one repetition.

## C89 resource caches and object ownership (12 November 2026)

Resource-cache tables, preload/fetch, eviction, cacheable-object attachment and
release policy, and resource-manager ownership/searches now use C89. Native
facades retain reader calls, virtual dispatch, class allocation and file/reference
primitives. Two-word signed arithmetic preserves wide resident accounting without
C99 integer types. See the [implementation boundary](../code-map/library-split.md#resource-caches-object-lifetime-and-manager-lookup).

The native resource test retains all 1,624 baseline bytes covering priority/age
and estimate/actual eviction, cascading releases, distinct reader identities,
scoped lookup, manager precedence/error stops and objects outliving their cache.
Both native and standalone tests hold 24 resources charged at 192 MiB without
allocating large payloads. Direct C checks cover recursive reads, relocating
storage, allocation fallback/failure, failed insertion lifetimes, signed priority
truncation, budget scan restart and manager query/disposal ordering. A frozen
Python arbitrary-precision oracle checks 65,536 wide-arithmetic pairs.

Twenty-five standalone C checks, thirty-eight focused native checks and MSVC
AddressSanitizer pass. Native statistics/expanded-index resource variants compile
with `/Zs`; expanded-index runtime remains unverified. All eight binary snapshots
match, including the prior 597-byte construction, 56-byte lifecycle, 1,740-byte
routing, 2,192-byte loading, 572,367-byte chunk, 29,532-byte string and 1,594-byte
forest baselines. All 51 CLI baselines match, including six asset indexes covering
20,568 entries. Evidence includes `tmp/c89-resource-before.bin`,
`tmp/c89-resource-after.bin`, `tmp/c89-resource-*-after.bin`,
`tmp/c89-resource-output-comparison.json` and `tmp/c89-resource-asan.log`.
No Marcus UI tests were added.

Frozen Studio/Marcus builds were captured without concurrent builds. Evidence
is in `tmp/c89-movie-data-regression/playback.sqlite`:

| Suite | Selection | Capture result / comparison verdict |
| --- | --- | --- |
| `20261112T160245Z-70316bea` | `scrolling-text`, retail/SDL/Marcus seek and playback, one repetition | Six complete, no capture failures; verdict `different`, six different and five inconclusive components. |
| `20261112T160706Z-94c41679` | `sampled-audio`, retail/SDL/Marcus playback, one repetition | Three complete, no capture failures; verdict `different`, five different and four inconclusive components. |

Against preceding container suites `20261112T152255Z-f6ca2e8e` and
`20261112T152648Z-013c24c1`, all 180 seek images match exactly. All nine runs
preserve the same 60 authored frame positions and ordered exact image changes:
scrolling retail/SDL 328 and Marcus 321, sampled audio 60 for each player.
Only adjacent identical image/position records are collapsed; raw presentation
samples and timestamps remain available. The report is
`tmp/c89-resource-playback-comparison.json`.

Observed PCM16 peaks remain 2380 for retail and 2176 for SDL/Marcus. These
establish audible capture, not exact PCM preservation. All nine captures retain
unchanged movie, ISO-resource and Marcus-content hashes. Existing cross-player
pixel/audio differences and timing/alignment limitations remain; this is
preservation against the preceding build, not full retail parity or general
repeatability from one repetition.

## C89 container registry and forest construction (12 November 2026)

Container links now live in explicit C contexts, replacing ChunkyFile's native
BaseLinkedList storage and registry head. Newest-first traversal and marked
zero-reference retention remain unchanged. Forest destination construction also
uses C: copied imports reserve a temporary file's 128-byte prefix; borrowed
imports retain source, set their logical end and invalid-main/add-to-extra state
before reading. All failure paths release the partial owner and its attachments.
Native allocation, typed owner/index state and file/index primitives remain
adapters. See the [implementation boundary](../code-map/library-split.md#container-registry-and-forest-destination-construction).

Twenty-five standalone C checks, thirty-eight focused native checks and the
expanded container MSVC AddressSanitizer check pass. Registry checks cover
independent contexts, head/middle/tail removal, duplicate attachment, transfer,
repeated detach and invalid-link guards. Forest construction checks cover
allocation/index/temp/resize/read failure, borrowed source references and partial
map/runtime cleanup ordering. Native statistics and expanded-index variants
compile with `/Zs`; expanded-index runtime remains unverified.

All nine binary snapshots match: the new 120-byte registry fixture plus the
prior 597-byte construction, 56-byte lifecycle, 1,740-byte routing, 2,192-byte
loading, 572,367-byte chunk, 29,532-byte string, 1,594-byte forest and 1,624-byte
resource-cache baselines. All 51 CLI baselines match, including six asset indexes
covering 20,568 entries. Evidence includes `tmp/c89-context-before.bin`,
`tmp/c89-context-registry-after.bin`, `tmp/c89-context-resource-after.bin`,
`tmp/c89-context-*-after.bin`, `tmp/c89-context-output-comparison.json` and
`tmp/c89-context-asan.log`. No Marcus UI tests were added.

Frozen Studio/Marcus builds were captured without concurrent builds. Evidence
is in `tmp/c89-movie-data-regression/playback.sqlite`:

| Suite | Selection | Capture result / comparison verdict |
| --- | --- | --- |
| `20261112T161703Z-903a37a1` | `scrolling-text`, retail/SDL/Marcus seek and playback, one repetition | Six complete, no capture failures; verdict `different`, six different and five inconclusive components. |
| `20261112T162023Z-248551ea` | `sampled-audio`, retail/SDL/Marcus playback, one repetition | Three complete, no capture failures; verdict `different`, five different and four inconclusive components. |

Against preceding resource-cache suites `20261112T160245Z-70316bea` and
`20261112T160706Z-94c41679`, all 180 seek images match exactly. All nine runs
preserve the same 60 authored frame positions and ordered exact image changes:
scrolling retail/SDL 328 and Marcus 321, sampled audio 60 for each player.
Only adjacent identical image/position records are collapsed; raw presentation
samples and timestamps remain available. The report is
`tmp/c89-context-playback-comparison.json`.

Observed PCM16 peaks remain 2380 for retail and 2176 for SDL/Marcus. These
establish audible capture, not exact PCM preservation. All nine captures retain
unchanged movie, ISO-resource and Marcus-content hashes. Existing cross-player
pixel/audio differences and timing/alignment limitations remain; this is
preservation against the preceding build, not full retail parity or general
repeatability from one repetition.

## C89 persistent container owner state (12 November 2026)

The persistent container record now uses C89 for file/index references, free maps,
compact runtime cache, flags and deferred-map positions. Native storage structs,
bitfields and duplicate reopen-state publication are removed. Reopen/disposal
operate on the live C record. Save publication, storage-policy views and file
error aggregation/reset now use C as well. Native GeneralGroup index objects,
chunk-record adapters and physical file/reference primitives remain dependencies.
See the [implementation boundary](../code-map/library-split.md#persistent-container-owner-state).

Twenty-six standalone C checks, thirty-nine focused native checks and MSVC
AddressSanitizer pass. The new owner test checks successful reopen, read/validation
rollback, writable/invalid-main guard denial, callback visibility, compact runtime
restoration, expanded-owner state, partial cleanup and repeated disposal. Save
publication preserves unrelated state; lock guards reject disposal before
mutation. Error checks cover 1,000 main/extra/reset combinations and callback
order. Native statistics and expanded-index variants compile with `/Zs`;
expanded-index runtime integration remains unverified.

All nine binary snapshots match: the 120-byte registry, 597-byte construction,
56-byte lifecycle, 1,740-byte routing, 2,192-byte loading, 572,367-byte chunk,
29,532-byte string, 1,594-byte forest and 1,624-byte resource-cache baselines.
All 51 CLI baselines match, including six asset indexes covering 20,568 entries.
Evidence includes `tmp/c89-owner-registry-after.bin`,
`tmp/c89-owner-resource-after.bin`, `tmp/c89-owner-*-after.bin`,
`tmp/c89-owner-output-comparison.json` and `tmp/c89-owner-asan.log`.
No Marcus UI tests were added.

Frozen Studio/Marcus builds were captured without concurrent builds. Evidence
is in `tmp/c89-movie-data-regression/playback.sqlite`:

| Suite | Selection | Capture result / comparison verdict |
| --- | --- | --- |
| `20261112T163041Z-fc0766de` | `scrolling-text`, retail/SDL/Marcus seek and playback, one repetition | Six complete, no capture failures; verdict `different`, six different and five inconclusive components. |
| `20261112T163404Z-47eeea2a` | `sampled-audio`, retail/SDL/Marcus playback, one repetition | Three complete, no capture failures; verdict `different`, five different and four inconclusive components. |

Against preceding context suites `20261112T161703Z-903a37a1` and
`20261112T162023Z-248551ea`, all 180 seek images match exactly. All nine runs
preserve the same 60 authored frame positions and ordered exact image changes:
scrolling retail/SDL 328 and Marcus 321, sampled audio 60 for each player.
Only adjacent identical image/position records are collapsed; raw presentation
samples and timestamps remain available. The report is
`tmp/c89-owner-playback-comparison.json`.

Observed PCM16 peaks remain 2380 for retail and 2176 for SDL/Marcus. These
establish audible capture, not exact PCM preservation. All nine captures retain
unchanged movie, ISO-resource and Marcus-content hashes. Existing cross-player
pixel/audio differences and timing/alignment limitations remain; this is
preservation against the preceding build, not full retail parity or general
repeatability from one repetition.

## C89 owning chunk tables and record access (12 November 2026)

The writable index now owns C table storage instead of a native GeneralGroup
object. C owns creation/disposal, mutation metadata, locking and GG wire output.
Compact/expanded fixed records are C layouts; C owns packed sizes/flags, masked
updates, count truncation, runtime-ID preservation, canonical entry/child access
and scratch-flag clearing. Native container code retains argument/result adapters,
file I/O and legacy name conversion. Tag/resource resolution, engine objects and
complete facade removal remain. See the [implementation boundary](../code-map/library-split.md#owning-chunk-tables-and-compactexpanded-records).

Twenty-seven standalone C checks, forty focused native checks and MSVC
AddressSanitizer pass. The new table check preserves a frozen hash over 2,188
pre-conversion native GG bytes across both widths, reverse insertion, variable
edits, moves, deletion, locking and output. Both debug-trash modes match. Checks
cover initial/growth failures, relocating allocation, fallback, locked-owner
guards, opposite-order writing and header/payload/location write failures,
including restored locations and balanced locks. Record checks cover packed
size/flag updates, count truncation, expanded runtime IDs and child bounds.
Native statistics/expanded-index variants compile with `/Zs`; expanded native
index runtime integration remains unverified.

All ten binary snapshots match: the new 2,188-byte GG oracle plus the prior
120-byte registry, 597-byte construction, 56-byte lifecycle, 1,740-byte routing,
2,192-byte loading, 572,367-byte chunk, 29,532-byte string, 1,594-byte forest and
1,624-byte resource-cache baselines. The standalone C table hash independently
matches the native GG oracle. All 51 CLI baselines match, including six asset
indexes covering 20,568 entries. Evidence includes `tmp/c89-table-before.bin`,
`tmp/c89-table-native-after.bin`, `tmp/c89-table-*-after.bin`,
`tmp/c89-table-output-comparison.json` and `tmp/c89-table-asan.log`.
No Marcus UI tests were added.

Frozen Studio/Marcus builds were captured without concurrent builds. Evidence
is in `tmp/c89-movie-data-regression/playback.sqlite`:

| Suite | Selection | Capture result / comparison verdict |
| --- | --- | --- |
| `20261112T165400Z-1eead350` | `scrolling-text`, retail/SDL/Marcus seek and playback, one repetition | Six complete, no capture failures; verdict `different`, six different and five inconclusive components. |
| `20261112T165721Z-da1661e1` | `sampled-audio`, retail/SDL/Marcus playback, one repetition | Three complete, no capture failures; verdict `different`, five different and four inconclusive components. |

Against preceding owner-state suites `20261112T163041Z-fc0766de` and
`20261112T163404Z-47eeea2a`, all 180 seek images match exactly. All nine runs
preserve the same 60 authored frame positions and ordered exact image changes:
scrolling retail/SDL 328 and Marcus 321, sampled audio 60 for each player.
Only adjacent identical image/position records are collapsed; raw presentation
samples and timestamps remain available. The report is
`tmp/c89-table-playback-comparison.json`.

Observed PCM16 peaks remain 2380 for retail and 2176 for SDL/Marcus. These
establish audible capture, not exact PCM preservation. All nine captures retain
unchanged movie, ISO-resource and Marcus-content hashes. Existing cross-player
pixel/audio differences and timing/alignment limitations remain; this is
preservation against the preceding build, not full retail parity or general
repeatability from one repetition.

## C89 tag ownership, dispatch and owning lists (12 November 2026)

Tag open/save/redirect/duplicate/close, embedded/source child lookup and fetch
dispatch now use C89. Publication preserves callback visibility and reference
ordering; unopened cache pointers are not read by open or disabled file caching.
C owns cache clearing, including media resolution before reopen and zero/restore
memory budgets. File caching remains disabled. Source catalogue, availability
and directory/media discovery/retry remain native provider policy. TagList now
owns C group storage; tags are borrowed, duplicate keys ignore cache identity,
child-cache flags latch true, and child entries retain duplicates. Cache passes
retain legacy child-reference behavior without implicit close. See the
[implementation boundary](../code-map/library-split.md#tag-ownership-dispatch-and-owning-lists).

Twenty-eight standalone C checks, forty-one focused native checks and MSVC
AddressSanitizer pass. Direct checks cover copy failures, retain/release/publication
order, same-cache redirects, source/media dispatch, missing hooks, cache clearing,
comparison, initial/growth failure, list ordering/duplicates and cache-pass stops.
Missing-parent diagnostics have a separate result from ordinary storage failures.
The final builds were checked after that distinction was corrected.

All eleven binary snapshots match: the new 336-byte tag fixture plus the prior
2,188-byte GG oracle, 120-byte registry, 597-byte construction, 56-byte lifecycle,
1,740-byte routing, 2,192-byte loading, 572,367-byte chunk, 29,532-byte string,
1,594-byte forest and 1,624-byte resource-cache baselines. All 51 CLI baselines
match, including six asset indexes covering 20,568 entries. Evidence includes
`tmp/c89-tag-before.bin`, `tmp/c89-tag-after.bin`, `tmp/c89-tag-*-after.bin`,
`tmp/c89-tag-output-comparison.json` and `tmp/c89-tag-asan.log`.
No Marcus UI tests were added.

Frozen Studio/Marcus builds were captured without concurrent builds. Final
evidence is in `tmp/c89-movie-data-regression/playback.sqlite`:

| Suite | Selection | Capture result / comparison verdict |
| --- | --- | --- |
| `20261112T172035Z-ecee3a77` | `scrolling-text`, retail/SDL/Marcus seek and playback, one repetition | Six complete, no capture failures; verdict `different`, six different and five inconclusive components. |
| `20261112T172353Z-da4a3e37` | `sampled-audio`, retail/SDL/Marcus playback, one repetition | Three complete, no capture failures; verdict `different`, five different and four inconclusive components. |

Against preceding table suites `20261112T165400Z-1eead350` and
`20261112T165721Z-da1661e1`, all 180 seek images match exactly. All nine runs
preserve the same 60 authored frame positions and ordered exact image changes:
scrolling retail/SDL 328 and Marcus 321, sampled audio 60 for each player.
Only adjacent identical image/position records are collapsed; raw presentation
samples and timestamps remain available. The report is
`tmp/c89-tag-playback-comparison.json`.

Observed PCM16 peaks remain 2380 for retail and 2176 for SDL/Marcus. These
establish audible capture, not exact PCM preservation. All nine captures retain
unchanged movie, ISO-resource and Marcus-content hashes. Existing cross-player
pixel/audio differences and timing/alignment limitations remain; this is
preservation against the preceding build, not full retail parity or general
repeatability from one repetition.

## C89 source catalogue, lookup and manager construction (12 November 2026)

The source catalogue now owns C records, opaque path handles and resource-manager
references, replacing the native SourceFileStructure array. C owns cached local
content/misses, provider denial before cached access, local/media selection,
retry/install sequencing, ordered manager construction and file routing. Work-path
publication retains legacy mutation points and failure behavior. Native source
names, long/short directory/drive discovery, physical filename operations and UI
prompts remain adapters. See the [implementation boundary](../code-map/library-split.md#source-catalogue-lookup-and-manager-construction).

Twenty-nine standalone C checks, forty-two focused native checks and MSVC
AddressSanitizer pass. Source checks cover local-only misses without prompts,
provider denial before cached managers, retry after installation, archived/local/
media sources, probe/manager failure, path/array failure, relocating allocation,
invalidation, locks and forward cleanup. They retain the legacy AVAILABLE-media
shortcut's unchanged output and the lack of media fallback when local-directory
lookup fails. Builder checks cover archive/directory selection and create/init/
open/add failures with partial file/manager cleanup.

All twelve binary snapshots match: the new 60-byte native prompt/retry trace plus
the prior 336-byte tag, 2,188-byte GG, 120-byte registry, 597-byte construction,
56-byte lifecycle, 1,740-byte routing, 2,192-byte loading, 572,367-byte chunk,
29,532-byte string, 1,594-byte forest and 1,624-byte resource-cache baselines.
All 51 CLI baselines match, including six asset indexes covering 20,568 entries.
Evidence includes `tmp/c89-source-before.bin`, `tmp/c89-source-after.bin`,
`tmp/c89-source-*-after.bin`, `tmp/c89-source-output-comparison.json` and
`tmp/c89-source-asan.log`. No Marcus UI tests were added.

Frozen Studio/Marcus builds were captured without concurrent builds. Evidence
is in `tmp/c89-movie-data-regression/playback.sqlite`:

| Suite | Selection | Capture result / comparison verdict |
| --- | --- | --- |
| `20261112T174305Z-0a5d4b28` | `scrolling-text`, retail/SDL/Marcus seek and playback, one repetition | Six complete, no capture failures; verdict `different`, six different and five inconclusive components. |
| `20261112T174706Z-519940f5` | `sampled-audio`, retail/SDL/Marcus playback, one repetition | Three complete, no capture failures; verdict `different`, five different and four inconclusive components. |

Against preceding tag suites `20261112T172035Z-ecee3a77` and
`20261112T172353Z-da4a3e37`, all 180 seek images match exactly. All nine runs
preserve the same 60 authored frame positions and ordered exact image changes:
scrolling retail/SDL 328 and Marcus 321, sampled audio 60 for each player.
Only adjacent identical image/position records are collapsed; raw presentation
samples and timestamps remain available. The report is
`tmp/c89-source-playback-comparison.json`.

Observed PCM16 peaks remain 2380 for retail and 2176 for SDL/Marcus. These
establish audible capture, not exact PCM preservation. All nine captures retain
unchanged movie, ISO-resource and Marcus-content hashes. Existing cross-player
pixel/audio differences and timing/alignment limitations remain; this is
preservation against the preceding build, not full retail parity or general
repeatability from one repetition.

## C89 source names and directory discovery (12 November 2026)

Source names now own C storage instead of a persistent native StringTable_GST.
SID/name lookup, merged-name splitting, first-ID-wins merging, provider dispatch
and GST serialization use C89. Movie saves write this table directly; Studio
enumerates managed source IDs without borrowing a native table. Directory
discovery policy also uses C, retaining local long-before-short and media
short-before-long order, floppy skipping and fixed paths for opened sources.
Physical path/file operations, platform error suppression, locale equality and
UI prompt construction remain host services. See the
[implementation boundary](../code-map/library-split.md#source-names-and-directory-discovery).

Thirty standalone C checks, forty-three focused native checks and MSVC
AddressSanitizer pass. Checks cover one/two-byte names, embedded zeros, empty
halves, duplicate IDs and ambiguous lookup, partial merge failure, swapped IDs,
provider mutation, failed wire writes, directory callback order and preservation
of output on misses. All thirteen binary snapshots match, including the new
192-byte source-name lookup/GST baseline and the twelve preceding snapshots.
All 51 CLI baselines match, including six asset indexes covering 20,568 entries.
Evidence includes `tmp/c89-names-before.bin`, `tmp/c89-names-after.bin`,
`tmp/c89-names-snapshot-comparison.json`, `tmp/c89-names-output-comparison.json`
and `tmp/c89-names-asan.log`. No Marcus UI tests were added.

Frozen Studio/Marcus builds were captured without concurrent builds. Evidence
is in `tmp/c89-movie-data-regression/playback.sqlite`:

| Suite | Selection | Capture result / comparison verdict |
| --- | --- | --- |
| `20261112T180529Z-30270838` | `scrolling-text`, retail/SDL/Marcus seek and playback, one repetition | Six complete, no capture failures; verdict `different`, six different and five inconclusive components. |
| `20261112T180849Z-d9fde1de` | `sampled-audio`, retail/SDL/Marcus playback, one repetition | Three complete, no capture failures; verdict `different`, five different and four inconclusive components. |

Against preceding source suites `20261112T174305Z-0a5d4b28` and
`20261112T174706Z-519940f5`, all 180 seek images match exactly. All nine runs
preserve the same 60 authored frame positions and ordered exact image changes:
scrolling retail/SDL 328 and Marcus 321, sampled audio 60 for each player.
Only adjacent identical image/position records are collapsed; raw presentation
samples and timestamps remain available. The report is
`tmp/c89-names-playback-comparison.json`.

Observed PCM16 peaks remain 2380 for retail and 2176 for SDL/Marcus. These
establish audible capture, not exact PCM preservation. All nine captures retain
unchanged movie, ISO-resource and Marcus-content hashes. Existing cross-player
pixel/audio differences and timing/alignment limitations remain; this is
preservation against the preceding build, not full retail parity or general
repeatability from one repetition.

## C89 movie ownership and roll-call persistence (12 November 2026)

MovieData now uses an owning C state for resource/table references, movie number,
actor-ID limit and scene count. C controls initialization, transactional load and
replacement, contiguous scene counting, save ordering and tag cleanup. Roll-call
reading/writing uses C row conversion and failure cleanup, preserving 28-byte
extras and zeroing the wire pointer slot. Native GST objects, title strings,
root/version adapters and physical I/O remain transitional host dependencies.
See the [implementation boundary](../code-map/library-split.md#movie-ownership-and-roll-call-persistence).

Thirty-one standalone C checks, forty-four focused native checks and MSVC
AddressSanitizer pass. New checks cover failed initialization/load, old-state
preservation, tag references, cleanup order, both byte orders, invalid rows and
extra widths, insertion failure and partial save publication. Studio retains its
existing early resource release during Movie destruction. Fourteen binary
snapshots match, including a new 736-byte native state/title/GST persistence
baseline and all thirteen preceding snapshots. All 51 CLI baselines match,
including six asset indexes covering 20,568 entries. No Marcus UI tests were added.

Evidence includes `tmp/c89-movie-owner-before.bin`,
`tmp/c89-movie-owner-after.bin`, `tmp/c89-movie-owner-snapshot-comparison.json`,
`tmp/c89-movie-owner-output-comparison.json` and `tmp/c89-movie-owner-asan.log`.

Frozen Studio/Marcus builds were captured without concurrent builds. Evidence
is in `tmp/c89-movie-data-regression/playback.sqlite`:

| Suite | Selection | Capture result / comparison verdict |
| --- | --- | --- |
| `20261112T182054Z-b432b76f` | `scrolling-text`, retail/SDL/Marcus seek and playback, one repetition | Six complete, no capture failures; verdict `different`, six different and five inconclusive components. |
| `20261112T182407Z-581585d3` | `sampled-audio`, retail/SDL/Marcus playback, one repetition | Three complete, no capture failures; verdict `different`, five different and four inconclusive components. |

Against preceding name suites `20261112T180529Z-30270838` and
`20261112T180849Z-d9fde1de`, all 180 seek images match exactly. All nine runs
preserve the same 60 authored frame positions and ordered exact image changes:
scrolling retail/SDL 328 and Marcus 321, sampled audio 60 for each player.
Only adjacent identical image/position records are collapsed; raw presentation
samples and timestamps remain available. The report is
`tmp/c89-movie-owner-playback-comparison.json`.

Observed PCM16 peaks remain 2380 for retail and 2176 for SDL/Marcus. These
establish audible capture, not exact PCM preservation. All nine captures retain
unchanged movie, ISO-resource and Marcus-content hashes. Existing cross-player
pixel/audio differences and timing/alignment limitations remain; this is
preservation against the preceding build, not full retail parity or general
repeatability from one repetition.

## C89 owning roll-call storage (12 November 2026)

Runtime and wire roll-call tables now own their name/extra buffers in C instead
of native StringTable_GST objects. Append/rename/delete/extra updates, dense GST
loading, text translation and GST serialization use C. Runtime records store
ThreeMMRollCallActor; wire extras remain 28 bytes. Storage mutation/destruction
does not retain or close tags, preserving movie transaction and edit-rollback
ownership. A temporary native RollCallTable adapter supplies BASE diagnostics and
typed conversions for existing callers. Title/root/version and physical services
remain migration work. See the
[implementation boundary](../code-map/library-split.md#owning-roll-call-storage).

Thirty-two standalone C checks, forty-five focused native checks and MSVC
AddressSanitizer pass. New checks cover relocation, partial allocation failure,
failed rename, deletion/extra updates, both byte orders, untouched extras,
foreign-width text conversion, read/write failures and locked disposal. All
fifteen binary snapshots match, including a new 405-byte native edit/serialization
baseline and fourteen prior snapshots. All 51 CLI baselines match, including
six asset indexes covering 20,568 entries. No Marcus UI tests were added.

Evidence includes `tmp/c89-rollcall-before.bin`, `tmp/c89-rollcall-after.bin`,
`tmp/c89-rollcall-snapshot-comparison.json`, `tmp/c89-rollcall-output-comparison.json`
and `tmp/c89-rollcall-asan.log`.

Frozen Studio/Marcus builds were captured without concurrent builds. Evidence
is in `tmp/c89-movie-data-regression/playback.sqlite`:

| Suite | Selection | Capture result / comparison verdict |
| --- | --- | --- |
| `20261112T183654Z-199a08ff` | `scrolling-text`, retail/SDL/Marcus seek and playback, one repetition | Six complete, no capture failures; verdict `different`, six different and five inconclusive components. |
| `20261112T184031Z-923ea395` | `sampled-audio`, retail/SDL/Marcus playback, one repetition | Three complete, no capture failures; verdict `different`, five different and four inconclusive components. |

Against preceding owner suites `20261112T182054Z-b432b76f` and
`20261112T182407Z-581585d3`, all 180 seek images match exactly. All nine runs
preserve the same 60 authored frame positions and ordered exact image changes:
scrolling retail/SDL 328 and Marcus 321, sampled audio 60 for each player.
Only adjacent identical image/position records are collapsed; raw presentation
samples and timestamps remain available. The report is
`tmp/c89-rollcall-playback-comparison.json`.

Observed PCM16 peaks remain 2380 for retail and 2176 for SDL/Marcus. These
establish audible capture, not exact PCM preservation. All nine captures retain
unchanged movie, ISO-resource and Marcus-content hashes. Existing cross-player
pixel/audio differences and timing/alignment limitations remain; this is
preservation against the preceding build, not full retail parity or general
repeatability from one repetition.

## C89 movie titles, roots and version selection (12 November 2026)

Movie titles now live in the C movie state instead of a native String. Title
selection, extension stripping, fallback and notification use C policy; native
filename/localized-text callbacks supply their values. Movie load/initialization
publish the title with the rest of the committed state. Cancelled save-dialog
callbacks retain their historical ability to mutate the title. Studio uses a
copied-name query instead of borrowing native title storage.

C also owns first-movie selection, version-error classification and root creation,
including explicit 8-byte prefix encoding. First selection publishes the movie
number before header rejection; failed verification preserves header output.
Write failure deletes a newly added root before publishing nil. Physical block/
file operations and frontend presentation remain host adapters. See the
[implementation boundary](../code-map/library-split.md#movie-titles-root-creation-and-version-checks).

Thirty-three standalone C checks, forty-six focused native checks and MSVC
AddressSanitizer pass. Checks cover both title widths, overlap/bounds, fallback
and notification order, extension edge cases, both prefix byte orders, selection
publication, I/O/add/write failures and failed-load title preservation. The native
save/load test covers mutation by a cancelled save callback. All sixteen binary
snapshots match, including a new 224-byte root/version baseline and fifteen prior
snapshots. All 51 CLI baselines match, including six asset indexes covering
20,568 entries. No Marcus UI tests were added.

Evidence includes `tmp/c89-title-root-before.bin`, `tmp/c89-title-root-after.bin`,
`tmp/c89-title-root-snapshot-comparison.json`, `tmp/c89-title-root-output-comparison.json`,
`tmp/c89-title-root-asan.log` and `tmp/c89-title-owner-asan.log`.

Frozen Studio/Marcus builds were captured without concurrent builds. Evidence
is in `tmp/c89-movie-data-regression/playback.sqlite`:

| Suite | Selection | Capture result / comparison verdict |
| --- | --- | --- |
| `20261112T185329Z-fb5455e5` | `scrolling-text`, retail/SDL/Marcus seek and playback, one repetition | Six complete, no capture failures; verdict `different`, six different and five inconclusive components. |
| `20261112T185640Z-e36976c1` | `sampled-audio`, retail/SDL/Marcus playback, one repetition | Three complete, no capture failures; verdict `different`, five different and four inconclusive components. |

Against preceding roll-call suites `20261112T183654Z-199a08ff` and
`20261112T184031Z-923ea395`, all 180 seek images match exactly. All nine runs
preserve the same 60 authored frame positions and ordered exact image changes:
scrolling retail/SDL 328 and Marcus 321, sampled audio 60 for each player.
Only adjacent identical image/position records are collapsed; raw presentation
samples and timestamps remain available. The report is
`tmp/c89-title-root-playback-comparison.json`.

Observed PCM16 peaks remain 2380 for retail and 2176 for SDL/Marcus. These
establish audible capture, not exact PCM preservation. All nine captures retain
unchanged movie, ISO-resource and Marcus-content hashes. Existing cross-player
pixel/audio differences and timing/alignment limitations remain; this is
preservation against the preceding build, not full retail parity or general
repeatability from one repetition.

## C89 scene state and event persistence (12 November 2026)

Scene metadata and counted names now use C state. C owns header read/write,
event-layout validation, frame/start child replacement, creation and cleanup.
Live Scene, Archive and Preview use these shared paths through temporary native
adapters. Group objects still wrap C collection storage; physical operations are
host callbacks. Runtime scene objects, event marshalling/playback/editing and
final facade removal remain migration work. See the
[implementation boundary](../code-map/library-split.md#scene-state-and-event-persistence).

Thirty-four standalone C checks, forty-seven focused native checks and MSVC
AddressSanitizer pass. Checks cover both byte orders, signed frame limits,
type/size rules, sound-array counts, metadata I/O/name failures, six event-list
replacement failures, allocation failures and ordered cleanup. Validation retains
legacy payload/frame-order acceptance. The existing native scene check still
exercises actual archive allocation failures and preservation of old child links.
All seventeen binary snapshots match, including a new 526-byte scene/name/header/
event baseline and sixteen prior snapshots. All 51 CLI baselines match, including
six asset indexes covering 20,568 entries. No Marcus UI tests were added.

Evidence includes `tmp/c89-scene-before.bin`, `tmp/c89-scene-after.bin`,
`tmp/c89-scene-snapshot-comparison.json`, `tmp/c89-scene-output-comparison.json`
and `tmp/c89-scene-asan.log`.

Frozen Studio/Marcus builds were captured without concurrent builds. Evidence
is in `tmp/c89-movie-data-regression/playback.sqlite`:

| Suite | Selection | Capture result / comparison verdict |
| --- | --- | --- |
| `20261112T191948Z-6d4ffbd8` | `scrolling-text`, retail/SDL/Marcus seek and playback, one repetition | Six complete, no capture failures; verdict `different`, six different and five inconclusive components. |
| `20261112T192401Z-3a37bc95` | `sampled-audio`, retail/SDL/Marcus playback, one repetition | Three complete, no capture failures; verdict `different`, five different and four inconclusive components. |

Against preceding title/root suites `20261112T185329Z-fb5455e5` and
`20261112T185640Z-e36976c1`, all 180 seek images match exactly. All nine runs
preserve the same 60 authored frame positions and ordered exact image changes:
scrolling retail/SDL 328 and Marcus 321, sampled audio 60 for each player.
Only adjacent identical image/position records are collapsed; raw presentation
samples and timestamps remain available. The report is
`tmp/c89-scene-playback-comparison.json`.

Observed PCM16 peaks remain 2380 for retail and 2176 for SDL/Marcus. These
establish audible capture, not exact PCM preservation. All nine captures retain
unchanged movie, ISO-resource and Marcus-content hashes. Existing cross-player
pixel/audio differences and timing/alignment limitations remain; this is
preservation against the preceding build, not full retail parity or general
repeatability from one repetition.

## C89 actor header, route and event persistence (12 November 2026)

C now owns actor header normalization and checked ID adjustment, route validation
and byte swapping, and all thirteen event wire/runtime layouts. Native file,
block and group operations remain temporary adapters. Track/tag lifetime and
actor evaluation/editing remain migration work. See the
[implementation boundary](../code-map/library-split.md#actor-header-route-and-event-persistence).

Thirty-five standalone C checks, forty-eight focused native checks and MSVC
AddressSanitizer pass. Checks cover both header sizes and byte orders, partial
failed reads, invalid-order output, ID overflow, empty/foreign routes, all event
layouts, mixed-width Add fields, unopened tag fields and every insertion failure.
All eighteen binary snapshots match, including a new 2,896-byte actor baseline
and seventeen prior snapshots. All 51 CLI baselines match, including six asset
indexes covering 20,568 entries. No Marcus UI tests were added.

Evidence includes `tmp/c89-actor-data-before.bin`, `tmp/c89-actor-data-after.bin`,
`tmp/c89-actor-data-snapshot-comparison.json`,
`tmp/c89-actor-data-output-comparison.json` and `tmp/c89-actor-data-asan.log`.

Frozen Studio/Marcus builds were captured without concurrent builds. Initial
scrolling suite `20261112T194604Z-aa6d9428` had five complete runs and one
incomplete SDL playback run: presentation capture reported failure, although
its authored-frame traversal completed. That failed attempt is retained. A full
repeat, `20261112T195016Z-384726c3`, completed all six runs without capture
failures (verdict `different`, six different and five inconclusive components).
Against preceding scene suite `20261112T191948Z-6d4ffbd8`, all 180 seek images
match exactly, and ordered exact image changes remain retail/SDL 328 and Marcus
321. Each run preserves the same 60 authored frame positions.

Sampled-audio suite `20261112T195348Z-84a9a960` completed all three players with
no capture failures (verdict `different`, five different and four inconclusive
components). Against preceding scene suite `20261112T192401Z-3a37bc95`, all
three runs preserve the same 60 authored positions and ordered exact image
changes. Observed PCM16 peaks remain 2380 for retail and 2176 for SDL/Marcus;
these establish audible capture, not exact PCM preservation.

All nine successful captures retain unchanged movie, ISO-resource and Marcus-
content hashes. Only adjacent identical image/position records are collapsed;
raw presentation samples and timestamps remain available. The report is
`tmp/c89-actor-data-playback-comparison.json`; captures and integrity records are
in `tmp/c89-movie-data-regression/playback.sqlite`. Existing cross-player pixel/
audio differences and timing/alignment limitations remain; this is preservation
against the preceding build, not full retail parity or general repeatability.

## C89 actor track and tag ownership (12 November 2026)

Actor route/event handles, creation/cloning, embedded-tag lookup, reference
ownership and ordered cleanup now use C89. Actor/preview code borrows typed table
handles from the C state. Physical table operations and actor evaluation/editing
remain migration work. See the
[implementation boundary](../code-map/library-split.md#actor-track-state-and-embedded-tag-ownership).

Thirty-six standalone C checks, forty-nine focused native checks and MSVC
AddressSanitizer pass. Checks cover unaligned tag bytes, all thirteen event kinds,
custom and source-tag acceptance, every create/clone allocation failure,
transactional replacement, retain/release order, idempotent cleanup and partial/
empty tracks. The native ownership check still injects real table allocation
failures and verifies resource reference counts. All nineteen binary snapshots
match, including a new 192-byte ownership/clone baseline and eighteen prior
snapshots. All 51 CLI baselines match, including six asset indexes covering
20,568 entries. No Marcus UI tests were added.

Evidence includes `tmp/c89-actor-track-before.bin`, `tmp/c89-actor-track-after.bin`,
`tmp/c89-actor-track-snapshot-comparison.json`,
`tmp/c89-actor-track-output-comparison.json` and `tmp/c89-actor-track-asan.log`.

Frozen Studio/Marcus builds were captured without concurrent builds. Evidence is
in `tmp/c89-movie-data-regression/playback.sqlite`:

| Suite | Selection | Capture result / comparison verdict |
| --- | --- | --- |
| `20261112T200239Z-60bfee16` | `scrolling-text`, retail/SDL/Marcus seek and playback, one repetition | Six complete, no capture failures; verdict `different`, six different and five inconclusive components. |
| `20261112T200710Z-c0d8bb16` | `sampled-audio`, retail/SDL/Marcus playback, one repetition | Three complete, no capture failures; verdict `different`, five different and four inconclusive components. |

Against preceding actor-data suites `20261112T195016Z-384726c3` and
`20261112T195348Z-84a9a960`, all 180 seek images match exactly. All nine runs
preserve the same 60 authored frame positions and ordered exact image changes:
scrolling retail/SDL 328 and Marcus 321, sampled audio 60 for each player.
Only adjacent identical image/position records are collapsed; raw presentation
samples and timestamps remain available. The report is
`tmp/c89-actor-track-playback-comparison.json`.

Observed PCM16 peaks remain 2380 for retail and 2176 for SDL/Marcus. These
establish audible capture, not exact PCM preservation. All nine captures retain
unchanged movie, ISO-resource and Marcus-content hashes. Existing cross-player
pixel/audio differences and timing/alignment limitations remain; this is
preservation against the preceding build, not full retail parity or general
repeatability from one repetition. The updated preview-scene fixture also builds.

## C89 actor value state, route and fixed-point arithmetic (12 November 2026)

Actor state initialization, transform reset, rewind/origin updates and pure value
events now execute in C89, alongside route advancement/clamping, interpolation and
guarded 16.16 arithmetic. The native state facade translates values into/out of the
C state record; sequencers, pose/timeline/seek evaluation, resource effects and
editing remain migration work. See the
[implementation boundary](../code-map/library-split.md#actor-value-state-and-route-evaluation).

Thirty-seven standalone C checks, fifty focused native checks and MSVC
AddressSanitizer pass. Checks cover route terminal/dwell semantics, event clamping,
aliasing, rewind preservation, pure/deferred event kinds, signed payload widths,
invalid zero-step locations and unchanged state on rejection. Native checks also
compare 4,096 scalar pairs against BRender multiplication and guarded division.
All twenty-one binary snapshots match, including new 33,384-byte route/arithmetic
and 4,142-byte state baselines plus nineteen prior snapshots. State snapshots
serialize fields rather than native padding. All 51 CLI baselines match, including
six asset indexes covering 20,568 entries. No Marcus UI tests were added.

Evidence includes `tmp/c89-actor-route-before.bin`, `tmp/c89-actor-route-after.bin`,
`tmp/c89-actor-state-before.bin`, `tmp/c89-actor-state-after.bin`,
`tmp/c89-actor-motion-snapshot-comparison.json`,
`tmp/c89-actor-motion-output-comparison.json` and `tmp/c89-actor-motion-asan.log`.

Frozen Studio/Marcus builds were captured without concurrent builds. Evidence is
in `tmp/c89-movie-data-regression/playback.sqlite`:

| Suite | Selection | Capture result / comparison verdict |
| --- | --- | --- |
| `20261112T202116Z-57a9fe56` | `scrolling-text`, retail/SDL/Marcus seek and playback, one repetition | Six complete, no capture failures; verdict `different`, six different and five inconclusive components. |
| `20261112T202443Z-9d7e49de` | `sampled-audio`, retail/SDL/Marcus playback, one repetition | Three complete, no capture failures; verdict `different`, five different and four inconclusive components. |

Against preceding track-ownership suites `20261112T200239Z-60bfee16` and
`20261112T200710Z-c0d8bb16`, all 180 seek images match exactly. All nine runs
preserve the same 60 authored frame positions and ordered exact image changes:
scrolling retail/SDL 328 and Marcus 321, sampled audio 60 for each player.
Only adjacent identical image/position records are collapsed; raw presentation
samples and timestamps remain available. The report is
`tmp/c89-actor-motion-playback-comparison.json`.

Observed PCM16 peaks remain 2380 for retail and 2176 for SDL/Marcus. These
establish audible capture, not exact PCM preservation. All nine captures retain
unchanged movie, ISO-resource and Marcus-content hashes. Existing cross-player
pixel/audio differences and timing/alignment limitations remain; this is
preservation against the preceding build, not full retail parity or general
repeatability from one repetition.

The existing `actor-events` movie also ran in all three players, in both seek and
continuous playback, to exercise rotation, scale, stretch, freeze and entrance/
exit behavior. Suite `20261112T202637Z-00e43c69` completed all six runs without
capture failures (verdict `different`, six different and five inconclusive
components). Against earlier actor-event captures `20261111T170550Z-0ec5aad2`
and the SDL playback recovery `20261111T172015Z-40d77fc0`, all 180 seek images
match exactly. All six runs preserve 60 authored frame positions and 60 ordered
image changes. Both baselines use the same movie SHA-256. Movie, ISO-resource and
Marcus-content hashes remain unchanged. The comparison is
`tmp/c89-actor-motion-events-comparison.json`. These six captures extend the nine
above; the actor-event baseline is older than the immediately preceding build.

## C89 actor frame sequencing (12 November 2026)

Actor one-frame sequencing now uses C-owned sequencer state: route advance,
entrance queries, event commands, acknowledgements, cel latches, sound filtering,
failure continuation/abort and final result flags execute in C89. Hosts still
apply requested effects; native state translation, typed commands/results and
physical table operations remain adapters. Pose/timeline/seek control and editing
remain migration work. See the
[implementation boundary](../code-map/library-split.md#actor-frame-command-sequencing).

Thirty-eight standalone C checks, fifty-one focused native checks and MSVC
AddressSanitizer pass. Checks cover entrance ordering, query/event protocol,
ordinary-failure cursor retention, entrance aborts, recording/rejoining, hidden/
suppressed ordinary sounds versus motion-match registration, initial freeze/Action
cel latches, vertical movement and terminal-route rules. All twenty-two binary
snapshots match, including a new 170,230-byte command/state/result/effect trace
from native sequential-playback and seek scenarios plus twenty-one prior
snapshots. The trace serializes fields without native padding. All 51 CLI
baselines match, including six asset indexes covering 20,568 entries. No Marcus
UI tests were added.

Evidence includes `tmp/c89-actor-frame-before.bin`, `tmp/c89-actor-frame-after.bin`,
`tmp/c89-actor-frame-snapshot-comparison.json`,
`tmp/c89-actor-frame-output-comparison.json` and `tmp/c89-actor-frame-asan.log`.

Frozen Studio/Marcus builds were captured without concurrent builds. Evidence is
in `tmp/c89-movie-data-regression/playback.sqlite`:

| Suite | Selection | Capture result / comparison verdict |
| --- | --- | --- |
| `20261112T205341Z-f0ca8c87` | `scrolling-text`, retail/SDL/Marcus seek and playback, one repetition | Six complete, no capture failures; verdict `different`, six different and five inconclusive components. |
| `20261112T205913Z-56bc5122` | `sampled-audio`, retail/SDL/Marcus playback, one repetition | Three complete, no capture failures; verdict `different`, five different and four inconclusive components. |

Against preceding motion-kernel suites `20261112T202116Z-57a9fe56` and
`20261112T202443Z-9d7e49de`, all 180 seek images match exactly. All nine runs
preserve the same 60 authored frame positions and ordered exact image changes:
scrolling retail/SDL 328 and Marcus 321, sampled audio 60 for each player.
Only adjacent identical image/position records are collapsed; raw presentation
samples and timestamps remain available. The report is
`tmp/c89-actor-frame-playback-comparison.json`.

Observed PCM16 peaks remain 2380 for retail and 2176 for SDL/Marcus. These
establish audible capture, not exact PCM preservation. All nine captures retain
unchanged movie, ISO-resource and Marcus-content hashes. Existing cross-player
pixel/audio differences and timing/alignment limitations remain; this is
preservation against the preceding build, not full retail parity or general
repeatability from one repetition.

The existing `actor-events` movie also ran in all three players, in both seek and
continuous playback, to exercise rotation, scale, stretch, freeze and entrance/
exit behavior. Suite `20261112T210026Z-7ffde7d1` completed all six runs without
capture failures (verdict `different`, six different and five inconclusive
components). Against preceding actor-event suite `20261112T202637Z-00e43c69`,
all 180 seek images match exactly. All six runs preserve 60 authored frame
positions and 60 ordered image changes. Movie, ISO-resource and Marcus-content
hashes remain unchanged. The report is
`tmp/c89-actor-frame-events-comparison.json`; these six captures extend the nine
above, for fifteen successful captures across the three cases.

## C89 actor timeline and seek control (12 November 2026)

Actor metadata-step policy, lifetime/cache calculation, stalling, backward steps,
quick backup and outer seek commands now execute in C89. C owns seek control
storage and partial-progress/rollback policy. Optional native-view publication
preserves callback observations of decremented cels and attempted lifetime frames.
Metadata reads, primitive table operations, diagnostics and typed native views are
adapters; pose, resource/effect/editing and final integration remain work. See the
[implementation boundary](../code-map/library-split.md#actor-timeline-metadata-steps-and-seek-control).

Thirty-nine standalone C checks, fifty-two focused native checks and MSVC
AddressSanitizer pass. Checks cover failed resolver outputs, callback observations,
partial caches, signed frame budgets, stalling comparisons, per-step/probe rollback,
retained quick-backup progress, vertical replay fallback, best-effort hide/costume
commands, failed frame continuation and seek protocol. The enabled retail BUG1960/
BUG1906 behavior and inherited before-first forward behavior remain. All twenty-four
binary snapshots match, including new 4,438-byte timeline/cache/callback and
26,564-byte seek command/state/effect baselines plus twenty-two prior snapshots.
The traces serialize fields without native padding. All 51 CLI baselines match,
including six asset indexes covering 20,568 entries. No Marcus UI tests were added.

Evidence includes `tmp/c89-actor-timeline-before.bin`, `tmp/c89-actor-timeline-after.bin`,
`tmp/c89-actor-seek-before.bin`, `tmp/c89-actor-seek-after.bin`,
`tmp/c89-actor-timeline-snapshot-comparison.json`,
`tmp/c89-actor-timeline-output-comparison.json` and `tmp/c89-actor-timeline-asan.log`.

Frozen Studio/Marcus builds were captured without concurrent builds. Evidence is
in `tmp/c89-movie-data-regression/playback.sqlite`:

| Suite | Selection | Capture result / comparison verdict |
| --- | --- | --- |
| `20261112T212906Z-83525be3` | `scrolling-text`, retail/SDL/Marcus seek and playback, one repetition | Six complete, no capture failures; verdict `different`, six different and five inconclusive components. |
| `20261112T213343Z-edaa6f22` | `sampled-audio`, retail/SDL/Marcus playback, one repetition | Three complete, no capture failures; verdict `different`, five different and four inconclusive components. |

Against preceding frame-sequencer suites `20261112T205341Z-f0ca8c87` and
`20261112T205913Z-56bc5122`, all 180 seek images match exactly. All nine runs
preserve the same 60 authored frame positions and ordered exact image changes:
scrolling retail/SDL 328 and Marcus 321, sampled audio 60 for each player.
Only adjacent identical image/position records are collapsed; raw presentation
samples and timestamps remain available. The report is
`tmp/c89-actor-timeline-playback-comparison.json`.

Observed PCM16 peaks remain 2380 for retail and 2176 for SDL/Marcus. These
establish audible capture, not exact PCM preservation. All nine captures retain
unchanged movie, ISO-resource and Marcus-content hashes. Existing cross-player
pixel/audio differences and timing/alignment limitations remain; this is
preservation against the preceding build, not full retail parity or general
repeatability from one repetition.

The existing `actor-events` movie also ran in all three players, in both seek and
continuous playback, to exercise rotation, scale, stretch, freeze and entrance/
exit behavior. Suite `20261112T213626Z-de47ded1` completed all six runs without
capture failures (verdict `different`, six different and five inconclusive
components). Against preceding actor-event suite `20261112T210026Z-7ffde7d1`,
all 180 seek images match exactly. All six runs preserve 60 authored frame
positions and 60 ordered image changes. Movie, ISO-resource and Marcus-content
hashes remain unchanged. The report is
`tmp/c89-actor-timeline-events-comparison.json`; these six captures extend the nine
above, for fifteen successful captures across the three cases.

## C89 actor pose and matrix math (12 November 2026)

Actor pose, weighted route orientation and vector rotation now execute in C89,
along with fixed matrix composition and BAM sine/cosine/atan policy. Two-word
integer products and original BRender tables preserve legacy rounding without
floating point or native arithmetic callbacks. Native code supplies table views,
value conversion and aliased-output publication. Resource effects, editing and
final backend integration remain migration work. See the
[implementation boundary](../code-map/library-split.md#actor-pose-and-fixed-matrixangle-math).

Forty standalone C checks, fifty-three focused native checks and MSVC
AddressSanitizer pass. Independent native comparisons cover all 65,536 sine/cosine
angles, 4,096 multiply-accumulate/atan cases and 128 matrix chains. Tests retain
zero-angle rotation rounding, signed fractional conversion, weighted tangents,
authored/current orientation, scaling, optional outputs and alias behavior.
All twenty-six binary snapshots match, including new 561,152-byte math and
111,872-byte pose traces with 256 varied poses. All 51 CLI baselines match,
including six asset indexes covering 20,568 entries. No Marcus UI tests were added.

Evidence includes `tmp/c89-actor-math-before.bin`, `tmp/c89-actor-math-after.bin`,
`tmp/c89-actor-pose-before.bin`, `tmp/c89-actor-pose-after.bin`,
`tmp/c89-actor-pose-snapshot-comparison.json`,
`tmp/c89-actor-pose-output-comparison.json` and `tmp/c89-actor-pose-asan.log`.

Frozen Studio/Marcus builds were captured without concurrent builds. Evidence is
in `tmp/c89-movie-data-regression/playback.sqlite`:

| Suite | Selection | Capture result / comparison verdict |
| --- | --- | --- |
| `20261112T221900Z-572f11a9` | `actor-events`, retail/SDL/Marcus seek and playback, one repetition | Six complete, no capture failures; verdict `different`, six different and five inconclusive components. |
| `20261112T222057Z-68aec496` | `scrolling-text`, retail/SDL/Marcus seek and playback, one repetition | Six complete, no capture failures; verdict `different`, six different and five inconclusive components. |
| `20261112T222422Z-e4a9fdc9` | `sampled-audio`, retail/SDL/Marcus playback, one repetition | Three complete, no capture failures; verdict `different`, five different and four inconclusive components. |

Against preceding timeline suites `20261112T213626Z-de47ded1` and
`20261112T212906Z-83525be3`, all 360 seek images match exactly. All twelve runs
preserve 60 authored frame positions and ordered exact image changes: actor events
60 for each player; scrolling retail/SDL 328 and Marcus 321. Only adjacent
identical image/position records are collapsed; raw presentation samples and
timestamps remain available. Reports are
`tmp/c89-actor-pose-events-comparison.json` and
`tmp/c89-actor-pose-playback-comparison.json`. Movie, ISO-resource and
Marcus-content hashes remain unchanged. Existing cross-player pixel/audio
differences and timing/alignment limitations remain; these results establish
preservation against the preceding build within this coverage.

Against preceding audio suite `20261112T213343Z-edaa6f22`, all three audio-case
runs preserve the same 60 authored frame positions and 60 ordered image changes.
Observed PCM16 peaks remain 2380 for retail and 2176 for SDL/Marcus, establishing
audible capture rather than exact PCM preservation. Movie, ISO-resource and
Marcus-content hashes remain unchanged. Together the three cases cover fifteen
successful captures; one repetition does not establish general repeatability.

## Shared C89 actor event effects (12 November 2026)

Studio, Marcus playback and Marcus timeline evaluation now call the same C89
event policy. C owns cached frame writes, pure/runtime dispatch, partial action
state, entrance costume rollback, route/orientation updates and material
fetch/apply/release ordering. Metadata-only evaluation skips body and sound
effects. Hosts retain primitive resource/body/audio operations and native observer
views; actor sound/motion-match policy, editing and final integration remain work.
See the [implementation boundary](../code-map/library-split.md#shared-actor-event-effects).

Forty-one standalone C checks, fifty-five focused native/preview checks and MSVC
AddressSanitizer pass. Failure injection checks retained cel-count writes, flag
failures, entrance rollback, stage observations and material reference cleanup.
All twenty-seven binary snapshots match, including an independent 13,824-byte
entrance-orientation comparison against native BRender across 256 cases. All 51
CLI baselines match, including six asset indexes covering 20,568 entries. No
Marcus UI tests were added.

Evidence includes `tmp/c89-actor-entrance-before.bin`,
`tmp/c89-actor-entrance-after.bin`, `tmp/c89-actor-effect-snapshot-comparison.json`,
`tmp/c89-actor-effect-output-comparison.json`,
`tmp/c89-actor-effect-standalone.log`, `tmp/c89-actor-effect-native-tests.log` and
`tmp/c89-actor-effect-asan.log`.

The headless scene-preview integration check also passes with the packaged ISO
resource file. It reads each actor's animation timeline twice and verifies
matching spans/events, valid frame bounds, and unchanged live pose, sounds and
rendered pixels. Its log is `tmp/c89-actor-effect-timeline.log`.

Frozen Studio/Marcus builds were captured without concurrent builds. Final shared
backend evidence is in `tmp/c89-movie-data-regression/playback.sqlite`:

| Suite | Selection | Capture result / comparison verdict |
| --- | --- | --- |
| `20261112T223822Z-b130805c` | `actor-events`, retail/SDL/Marcus seek and playback, one repetition | Six complete, no capture failures; verdict `different`, six different and five inconclusive components. |
| `20261112T224100Z-17b4c1ca` | `scrolling-text`, retail/SDL/Marcus seek and playback, one repetition | Six complete, no capture failures; verdict `different`, six different and five inconclusive components. |
| `20261112T224504Z-02af78e2` | `sampled-audio`, retail/SDL/Marcus playback, one repetition | Three complete, no capture failures; verdict `different`, five different and four inconclusive components. |

Against preceding pose suites `20261112T221900Z-572f11a9` and
`20261112T222057Z-68aec496`, all 360 seek images match exactly. All twelve runs
preserve 60 authored frame positions and ordered exact image changes: actor events
60 for each player; scrolling retail/SDL 328 and Marcus 321. Only adjacent
identical image/position records are collapsed; raw presentation samples and
timestamps remain available. Reports are
`tmp/c89-actor-effect-events-comparison.json` and
`tmp/c89-actor-effect-playback-comparison.json`. Movie, ISO-resource and
Marcus-content hashes remain unchanged. Existing cross-player pixel/audio
differences and timing/alignment limitations remain; these results establish
preservation against the preceding build within this coverage.

Against preceding audio suite `20261112T222422Z-e4a9fdc9`, all three audio-case
runs preserve the same 60 authored frame positions and 60 ordered image changes.
Observed PCM16 peaks remain 2380 for retail and 2176 for SDL/Marcus, establishing
audible capture rather than exact PCM preservation. Movie, ISO-resource and
Marcus-content hashes remain unchanged. Together the final shared suites cover
fifteen successful captures; one repetition does not establish general repeatability.

## Shared C89 actor runtime sound policy (12 November 2026)

Studio and Marcus now share C89 actor-sound dispatch and motion-match selection,
backed by C-owned motion-list storage. C owns replacement by cel/style, cloning,
deletion/clear, imported-tag update ordering, tool priority and resource queue/
release policy. Missing motion resources allow later matches; resolution or
enqueue failures stop. Failed motion resolution retains resolver output, while
ordinary events publish only successful resolution. Marcus emits borrowed cues
through the same selection policy. Native sound editing/query operations and
primitive resource/device services remain work. See the
[implementation boundary](../code-map/library-split.md#actor-runtime-sound-policy-and-motion-storage).

Forty-two standalone C checks, fifty-six native/preview checks, MSVC
AddressSanitizer, 27 harness checks and the headless scene/timeline integration
check pass. All twenty-eight snapshots match, including a new 43,328-byte
independent native DynamicArray/C list comparison across 256 varied operations.
All 51 CLI baselines match, including six asset indexes covering 20,568 entries.
No Marcus UI tests were added. Evidence is in
`tmp/c89-actor-sound-before.bin`, `tmp/c89-actor-sound-after.bin`,
`tmp/c89-actor-sound-snapshot-comparison.json`,
`tmp/c89-actor-sound-output-comparison.json`, `tmp/c89-actor-sound-asan.log`,
`tmp/c89-actor-sound-harness-tests.log` and `tmp/c89-actor-sound-timeline.log`.

The existing sampled-audio case authors scene cues. The new `actor-audio` case
authors two queued ordinary actor cues, two motion cels and a later replacement
of one binding at lower volume. It shares the actor track between two scenes,
with swapped sound-child mappings in the second scene. Embedded 600 Hz speech
and 1300 Hz effect samples make both cue types audible without external assets.
`prepare` now generates this case with explicit actor/motion sound coverage.
It has no preceding-build capture; it adds retail comparison coverage rather
than proving preservation against an older actor-audio build.

Prepared actor-audio manifests carry `actor_audio_contract: 1`. During playback
the harness checks the first scene's first PCM16 channel in 100 ms Hann windows
using Goertzel measurements at 600 and 1300 Hz. The observed final frame of scene
zero bounds the samples, using the recorded command/audio anchor. The later
scene's swapped mapping cannot hide a missing first-scene cue type. Both peak
tone amplitudes must exceed 50 PCM16
units; per-run metrics are saved as `playback/actor-audio.json`. Missing either
tone or silence fails the run. This checks cue-class presence, not exact queue
timing, binding-replacement timing, waveform equality or full retail audio parity.
The contract is also checked against all three recorded playback files below.

Frozen-build evidence is in `tmp/c89-movie-data-regression/playback.sqlite`:

| Suite | Selection | Capture result / comparison verdict |
| --- | --- | --- |
| `20261112T230520Z-b33780cf` | New `actor-audio`, retail/SDL/Marcus seek and playback, one repetition | Six complete, no capture failures; verdict `different`, nine different and four inconclusive components. |
| `20261112T230647Z-dfebe82c` | `actor-events`, retail/SDL/Marcus seek and playback, one repetition | Six complete, no capture failures; verdict `different`, six different and five inconclusive components. |
| `20261112T233629Z-cafe0917` | Retried `scrolling-text`, retail/SDL/Marcus seek and playback, one repetition | Six complete, no capture failures; verdict `different`, six different and five inconclusive components. |
| `20261112T234252Z-036262b4` | `sampled-audio` and `actor-audio`, retail/SDL/Marcus playback, one repetition; actor tone contract enabled | Six complete, no capture failures; verdict `different`, ten different and eight inconclusive components. |

The new case visits all 60 authored frame positions in each run. Both fixture
tones pass the permanent contract in all three players. Peak PCM16 values are
3192 retail and 4081 SDL/Marcus; these establish audible output, not equality.
Movies, ISO resources and Marcus content remain unchanged. Evidence is in
`tmp/c89-actor-sound-dedicated-comparison.json` and
`tmp/c89-actor-sound-tone-contract.json`.

Against event-effects suite `20261112T223822Z-b130805c`, all 180 actor-event seek
images match exactly; all six runs preserve the same 60 authored frame positions
and 60 ordered image changes. The report is
`tmp/c89-actor-sound-events-comparison.json`.

Scrolling-text attempt `20261112T230911Z-7973c6d3` was interrupted when C: filled
during retail playback. Its completed seek run and incomplete playback artifacts
are retained; the playback row is marked interrupted after confirming its process
terminated. SQLite's integrity check passes. This attempt is excluded from the
successful-capture totals. The final retry follows lossless compression of build
outputs and older temporary artifacts, with compression finished before capture.

Against preceding scrolling suite `20261112T224100Z-17b4c1ca`, all 180 seek images
match exactly; all six runs preserve 60 authored frame positions and the same
ordered image changes (retail/SDL 328, Marcus 321). Against preceding scene-audio
suite `20261112T224504Z-02af78e2`, all three runs preserve 60 authored frame
positions and 60 ordered image changes. Its PCM16 peaks remain 2380 retail and
2176 SDL/Marcus. The report is `tmp/c89-actor-sound-playback-comparison.json`.
Only adjacent identical image/position records are collapsed; original raw
presentation samples and timestamps remain available.

The three final actor-audio playback runs pass the permanent first-scene tone
contract in the capture harness itself and preserve the initial dedicated suite's
60 authored frame positions and ordered pixel changes for each player. Their
`playback/actor-audio.json` artifacts record the checked sample boundary and tone
amplitudes. The report is `tmp/c89-actor-sound-contract-comparison.json`.

The four successful suites total twenty-four complete captures: fifteen existing-
case preservation runs and nine actor-audio runs, including the three contract-
enabled follow-ups. Movie, ISO-resource and Marcus-content hashes remain unchanged.
Existing cross-player pixel/audio differences remain. One repetition per suite
does not establish general repeatability or full retail parity.

## C89 actor sound editing and event deletion (13 November 2026)

Actor sound setting/default insertion policy, volume edits, queries, deletions,
current-frame detection and bulk tag-resolution now execute in C89. General event
deletion owns cursor updates and current-scene sound-indicator ordering in C.
Typed table/resource/scene services remain native; general insertion/merge/undo
integration is still a temporary editor callback. See the
[implementation boundary](../code-map/library-split.md#actor-sound-editing-and-event-deletion).

The inherited signed-low-word queue identity and default-sound positional call
binding remain. Queries retain partial volume/loop outputs on later failure;
failed second metadata lookup retains event deletions without deleting motion
records or marking dirty. Setter reservation failure now releases the fetched
resource, fixing a reference leak without changing edit data. No other rollback
or cleanup order is changed intentionally.

Forty-three standalone C checks, fifty-seven focused native checks and MSVC
AddressSanitizer pass. All twenty-nine snapshots match, including a frozen
7,456-byte real-actor sound-editing trace captured before conversion across
fifteen query/volume/delete/removal/metadata-failure scenarios. Independent native
queue comparisons cover 655,360 low-word/style/actor combinations. All 51 CLI
baselines and the headless scene/timeline integration check pass. No Marcus UI
tests were added. Evidence includes `tmp/c89-sound-edit-before.bin`,
`tmp/c89-sound-edit-after-edit.bin`, `tmp/c89-sound-edit-snapshot-comparison.json`,
`tmp/c89-sound-edit-output-comparison.json`, `tmp/c89-sound-edit-native-tests.log`,
`tmp/c89-sound-edit-asan.log` and `tmp/c89-sound-edit-timeline.log`.

Frozen Studio/Marcus builds ran without concurrent builds or compaction. Evidence
is in `tmp/c89-movie-data-regression/playback.sqlite`:

| Suite | Selection | Capture result / comparison verdict |
| --- | --- | --- |
| `20261113T000624Z-f7dd3165` | `actor-audio`, retail/SDL/Marcus seek and playback | Six complete, no capture failures; verdict `different`, nine different and four inconclusive components. |
| `20261113T000903Z-0834f3d8` | `actor-events`, retail/SDL/Marcus seek and playback | Five complete; retail playback failed before frame observations with oracle `ambiguous_native_object`. Verdict `inconclusive`, four different and two inconclusive components. |
| `20261113T001209Z-90bfea81` | Fresh-process retry of retail `actor-events` playback | One complete, no capture failure. Verdict `inconclusive` because this reference-only suite has no cross-player comparisons. |
| `20261113T001332Z-19d8d202` | `scrolling-text`, retail/SDL/Marcus seek and playback | Six complete, no capture failures; verdict `different`, six different and five inconclusive components. |
| `20261113T001824Z-a0a8447d` | `sampled-audio`, retail/SDL/Marcus playback | Three complete, no capture failures; verdict `different`, five different and four inconclusive components. |

The failed retail oracle attempt and its partial artifacts remain separate from
the successful retry; discovery still requires a unique native object. The
actor-event comparison cohort uses the five completed initial runs and that
retry, all from the frozen builds. Against preceding suites
`20261112T230520Z-b33780cf` (actor-audio seek), `20261112T234252Z-036262b4`
(actor/scene audio playback), `20261112T230647Z-dfebe82c` (actor events) and
`20261112T233629Z-cafe0917` (scrolling), all twenty-one selected runs preserve
their 60 authored frame positions and ordered pixel changes. All 540 seek images
match exactly. Scrolling retains 328 ordered changes for retail/SDL and 321 for
Marcus; other cases retain 60 each. Only adjacent identical image/position records
are collapsed; original presentation samples and timestamps remain available.

All three actor-audio playback runs pass the permanent first-scene 600/1300 Hz
tone contract. Actor PCM16 peaks remain 3192 retail and 4081 SDL/Marcus; scene-audio
peaks remain 2380 and 2176 respectively. These establish audible coverage, not
exact waveform/timing preservation. Movie identities, movie/ISO-resource hashes
and Marcus content hashes remain unchanged. The combined report is
`tmp/c89-sound-edit-playback-actor-audio-actor-events-scrolling-text-sampled-audio.json`.
Existing cross-player differences remain; these captures do not prove full retail
parity or general repeatability from one repetition per suite.

## C89 general actor event editing (13 November 2026)

General actor event insertion, merge scanning and insert/merge/apply sequencing
now execute in C89. Sound setting calls the C editor directly, removing its native
`insert_apply` callback. Native callbacks supply table/resource/scene services and
an adapter to the already shared C event evaluator. Broader action/fill/subroute/
recording edits, undo, queue/runtime integration and final facade removal remain.
See the [implementation boundary](../code-map/library-split.md#actor-event-insertion-and-merging).

The conversion preserves cursor inequalities, NULL-payload reservation, scan and
early-return behavior, signed-low-word sound queue identity, motion-list removal,
32-bit Move accumulation and the enabled retail BUG1870 delta-only application.
Failed application retains partial edits/state; lifetime invalidation precedes
each first/last assignment. Same-location costume replacement intentionally fixes
the old reference overwrite bug by closing the displaced tag and retaining its
replacement before deleting the source row. Logical event payloads remain equal;
both standalone C and real actor/resource tests check ownership through teardown.

All thirty snapshots match their frozen native baselines, including a new
7,232-byte trace covering forty-five real-actor event-edit scenarios. All forty-four
standalone C-only Clang checks, including strict C89 syntax, pass. Fifty-eight
focused native checks, MSVC AddressSanitizer, all 51 CLI baselines and the headless
scene/timeline check pass. The regression harness's 27 tests also pass. No Marcus
UI tests were added. Evidence includes `tmp/c89-event-edit-before.bin`,
`tmp/c89-event-edit-after-events.bin`, `tmp/c89-event-edit-snapshot-comparison.json`,
`tmp/c89-event-edit-output-comparison.json`, `tmp/c89-event-edit-native-tests.log`,
`tmp/c89-event-edit-asan.log`, `tmp/c89-event-edit-standalone.log` and
`tmp/c89-event-edit-timeline.log`.

Frozen Studio/Marcus builds ran without concurrent builds or compaction. Captures
used the explicit `--sdl-cache` option with the existing package cache at
`tmp/c89-movie-data-regression/20261113T001824Z-a0a8447d/sampled-audio/sdl/playback/fresh-process/0/cache`.
The suite environment records this warm package-cache path. Processes, settings,
registry isolation and working files remained private to each run. This does not
claim cold-cache coverage; OS caching remains uncontrolled.

Evidence is in `tmp/c89-movie-data-regression/playback.sqlite` and adjacent suite
artifacts:

| Suite | Case | Completed captures |
| --- | --- | --- |
| `20261113T004516Z-48a25044` | actor-audio | six retail/SDL/Marcus seek/playback runs |
| `20261113T004642Z-793f1ba5` | actor-events | six retail/SDL/Marcus seek/playback runs |
| `20261113T004807Z-ef947a24` | scrolling-text | six retail/SDL/Marcus seek/playback runs |
| `20261113T005117Z-579a56bd` | sampled-audio | three retail/SDL/Marcus playback runs |

These twenty-one runs preserve each player's preceding authored frame positions
and pixels. All 540 seek images match exactly; playback compares ordered changes,
collapsing only adjacent identical position/image records while retaining raw
samples and timestamps. Actor cases retain 60 changes per player; scrolling
retains 328 retail/SDL and 321 Marcus. Actor-audio passes the permanent first-scene
600/1300 Hz checks and retains PCM16 peaks of 3192 retail and 4081 SDL/Marcus.
Peaks establish audible coverage, not exact waveform or timing parity.
Scene-audio retains 60 ordered changes per player and PCM16 peaks of 2380 retail
and 2176 SDL/Marcus. Every selected run completed and every suite integrity check
passes. The combined report is
`tmp/c89-event-edit-playback-actor-audio-actor-events-scrolling-text-sampled-audio.json`.

Baselines are actor-audio seek suite `20261112T230520Z-b33780cf` and playback suite
`20261113T000624Z-f7dd3165`, actor-events suite `20261113T000903Z-0834f3d8` with
its successful retail-playback retry `20261113T001209Z-90bfea81`, and scrolling
suite `20261113T001332Z-19d8d202`; scene-audio uses suite
`20261113T001824Z-a0a8447d`. Existing cross-player differences remain; one
repetition per suite does not establish general repeatability or full retail parity.

## C89 actor action and transform fill editing (13 November 2026)

Actor action changes, authored-step reservation/insertion, action fill and transform
fill now execute in C89. Action editing calls the existing C sound editor and event
evaluator directly. Native services supply metadata, template kind, table views and
scene/lifetime notifications. Costume fill/material lookup, subroute/recording,
general undo, queue/runtime integration and final facade removal remain. See the
[implementation boundary](../code-map/library-split.md#actor-action-and-transform-fill-editing).

The conversion preserves reservation-before-metadata ordering, old-motion-event
removal only on action change, action application before default sound insertion,
lifetime invalidation before freeze, static/text/end-of-subroute step decisions
and partial edits on every failure. Action fill compares identity independently
of cel, stops at stage entrances/third actions, and preserves final freezes.
Transform fill preserves its comparison scan excluding `first - 1`, old-to-new
comparison transition, masks and route boundaries. It borrows comparison pointers
under the original table lock, retaining aliasing as rows are deleted.

All 45 standalone C-only Clang checks (including strict C89 syntax), 59 focused
native checks, MSVC AddressSanitizer, all 51 CLI baselines and the headless scene/
timeline check pass. All 31 snapshots match. The new native trace was frozen before
conversion and matches all 196,600 bytes across 528 transform-fill/action-fill/
action-change scenarios, including metadata failures. Standalone tests add 64
static/text/end/freeze combinations, seven ordered failure stages, transform
termination and final-freeze preservation. No Marcus UI tests were added.
Evidence includes `tmp/c89-fill-edit-before.bin`, `tmp/c89-fill-edit-after.bin`,
`tmp/c89-fill-edit-snapshot-comparison.json`, `tmp/c89-fill-edit-native-tests.log`,
`tmp/c89-fill-edit-standalone.log`, `tmp/c89-fill-edit-asan.log`,
`tmp/c89-fill-edit-output-comparison.json` and `tmp/c89-fill-edit-timeline.log`.

Capture binaries remained frozen, with no concurrent builds or compaction. SDL
reused the explicitly recorded warm package cache from the preceding event-edit
slice; process workspaces, settings, registry isolation and working movies remained
separate. OS file caching remains uncontrolled. Evidence is in
`tmp/c89-movie-data-regression/playback.sqlite` and adjacent suite artifacts:

| Suite | Case | Completed captures |
| --- | --- | --- |
| `20261113T010422Z-230669bf` | actor-audio | six retail/SDL/Marcus seek/playback runs |
| `20261113T010616Z-5e5e6368` | actor-events | six retail/SDL/Marcus seek/playback runs |
| `20261113T010856Z-aedbe7ab` | scrolling-text | three complete retail seek/playback and SDL seek runs; three capture errors |
| `20261113T011229Z-78ab78f2` | scrolling-text | successful SDL playback retry |
| `20261113T011353Z-37941a26` | scrolling-text | successful Marcus seek/playback retries |
| `20261113T011523Z-a52f9236` | sampled-audio | three retail/SDL/Marcus playback runs |

The successful actor cases preserve each player's previous authored frame positions and pixels.
Their playback runs retain 60 ordered image changes per player, collapsing only
adjacent identical position/image records while keeping raw observations and
timestamps. Actor-audio passes the permanent first-scene 600/1300 Hz tone checks
and retains PCM16 peaks of 3192 retail and 4081 SDL/Marcus. Peaks establish audible
coverage, not exact waveform/timing parity.

The original scrolling suite retained three capture errors: SDL presentation
capture failed, Marcus seek could not write an image, and Marcus presentation
capture failed. Space was critically low during that suite. The runner was
confirmed terminal, obsolete build intermediates were removed, and only those
three runs were retried in fresh processes. All retries completed; capture policy
was unchanged and original failed observations remain. The six successful scrolling
runs match the preceding suite's 180 seek images, 60 authored positions per run,
and ordered playback changes (328 retail/SDL, 321 Marcus). Retry suites have no
cross-player peers, so their cross-player verdicts remain inconclusive.

All twenty-one successful captures preserve their preceding authored positions
and pixels, including all 540 seek images. Scene-audio retains 60 ordered changes
per player and PCM16 peaks of 2380 retail and 2176 SDL/Marcus. Movie identities
match the preceding captures; movie, ISO-resource and Marcus-content integrity
checks pass. The combined report is
`tmp/c89-fill-edit-playback-actor-audio-actor-events-scrolling-text-sampled-audio.json`.

Baselines are actor-audio suite `20261113T004516Z-48a25044`, actor-events suite
`20261113T004642Z-793f1ba5`, scrolling suite `20261113T004807Z-ef947a24` and
scene-audio suite `20261113T005117Z-579a56bd`.
Existing cross-player differences remain. One repetition
per suite does not establish general repeatability or full retail parity.

## C89 actor costume editing and fill (13 November 2026)

Costume setting and fill now execute in C89, including custom body-set resolution,
reservation, current-to-incoming costume comparison, event deletion/application
and ownership cleanup. Native adapters supply body/template/material lookup,
resource fetch/release and table/scene primitives. See the
[implementation boundary](../code-map/library-split.md#actor-costume-editing-and-fill).

The conversion retains pointer identity including NULL equality, exact custom-flag
values, unrelated body-set skipping, stage-entrance boundaries and partial edits
after failure. A transition event can be fetched twice. Borrowed current materials
are never released; owning candidate/comparison releases retain their distinct
orders. Custom costume lookup, body-set extraction and release precede event
reservation; the custom material determines its body set independently of the
clicked set. No material/resource behavior was intentionally repaired in this slice.

All 46 standalone C-only Clang checks (including strict C89 syntax), 60 focused
native checks, MSVC AddressSanitizer, all 51 CLI baselines and the headless scene/
timeline check pass. All 32 snapshots match. The new 47,536-byte native trace was
frozen before conversion and covers 160 real-material/custom-costume scenarios,
including final body identities and resource reference counts. Standalone tests
check the exact transition fetch/release trace, NULL matches, 42 type/fetch-failure
combinations, raw custom flags, early resolution failure, reservation ordering and
partial failed edits. No Marcus UI tests were added.

Evidence includes `tmp/c89-costume-edit-before.bin`, `tmp/c89-costume-edit-after.bin`,
`tmp/c89-costume-edit-snapshot-comparison.json`, `tmp/c89-costume-edit-native-tests.log`,
`tmp/c89-costume-edit-standalone.log`, `tmp/c89-costume-edit-asan.log`,
`tmp/c89-costume-edit-output-comparison.json` and `tmp/c89-costume-edit-timeline.log`.

Frozen Studio/Marcus binaries ran without concurrent builds or compaction. SDL
reused the explicitly recorded warm package cache from the preceding slices;
process workspaces, settings, registry isolation and working movies remained
separate. OS file caching remains uncontrolled. Evidence is in
`tmp/c89-movie-data-regression/playback.sqlite` and adjacent suite artifacts:

| Suite | Case | Completed captures |
| --- | --- | --- |
| `20261113T012702Z-d81906ec` | actor-audio | six retail/SDL/Marcus seek/playback runs |
| `20261113T012846Z-0fe30a43` | actor-events | six retail/SDL/Marcus seek/playback runs |
| `20261113T013023Z-e524888d` | scrolling-text | six retail/SDL/Marcus seek/playback runs |
| `20261113T013339Z-d6edfa4b` | sampled-audio | three retail/SDL/Marcus playback runs |

The actor cases preserve each player's preceding authored positions and pixels,
including 60 ordered playback changes per player. Actor-audio passes the permanent
first-scene 600/1300 Hz tone checks and retains PCM16 peaks of 3192 retail and
4081 SDL/Marcus. Peaks establish audible coverage, not exact waveform/timing parity.

Scrolling preserves all 180 seek images, 60 authored positions per run and ordered
playback changes (328 retail/SDL, 321 Marcus). Only adjacent identical position/
image records are collapsed; original raw samples and timestamps remain available.

All 21 captures completed without retries; all 540 seek images and each player's
preceding authored positions and ordered playback changes match. Scene-audio
retains 60 changes per player and PCM16 peaks of 2380 retail and 2176 SDL/Marcus.
Movie identities match the preceding captures, and every suite's movie,
ISO-resource and Marcus-content integrity checks pass. The combined report is
`tmp/c89-costume-edit-playback-actor-audio-actor-events-scrolling-text-sampled-audio.json`.

Baselines are actor-audio suite `20261113T010422Z-230669bf`, actor-events suite
`20261113T010616Z-5e5e6368`, and the preceding successful scrolling runs from
`20261113T010856Z-aedbe7ab`, `20261113T011229Z-78ab78f2` (SDL playback retry) and
`20261113T011353Z-37941a26` (Marcus seek/playback retries). Scene-audio uses suite
`20261113T011523Z-a52f9236`. Existing cross-player differences remain; one repetition
per suite does not establish general repeatability or full retail parity.
Subroute/recording edits, general undo, queue/runtime integration, resource/body
implementations and final C++ facade removal remain unfinished.

## C89 actor route and forward deletion editing (13 November 2026)

Route-point insertion, event relocation after node insertion/deletion, event
searches, reachable-subroute lookup, single-frame Add deletion, truncation and
private/public forward deletion now execute in C89. Hide/reset/rewind sequencing
also executes in C. Native adapters supply table primitives, body visibility and
default-costume operations, owner flags, scene notifications and diagnostics.
See the [implementation boundary](../code-map/library-split.md#actor-route-and-forward-deletion-editing).

The conversion preserves terminal zero-distance nodes, epsilon clamping, wrapped
offsets, caller-point mutation, shared-node protection and held-frame behavior.
Truncation retains its original length-write-before-interpolation ordering and
backward cache scan. Forward deletion retains enabled retail BUG1870/BUG1932
behavior, subroute/all-route boundaries, dirty notification before reset, false
alive publication before default-costume reset, and deliberate absence of range
invalidation for reductions. Public deletion preserves terminal reservation,
freeze/step failure diagnostics and final tweak ordering. Default costume reset
remains best effort; earlier partial edits are retained on failure.

All 47 standalone C-only Clang checks (including strict C89 syntax), 61 focused
native checks, MSVC AddressSanitizer, all 51 CLI baselines and the headless scene/
timeline check pass. All 33 snapshots match, including the 158,436-byte native
trace frozen before conversion across 480 route-edit scenarios. Standalone tests
check failed insertion, epsilon/wrapped arithmetic, endpoint ordering, absence
of range invalidation, alive/reset callback observations, terminal reservation/
application failures and tweak ordering. No Marcus UI tests were added.

Evidence includes `tmp/c89-route-edit-before.bin`, `tmp/c89-route-edit-after.bin`,
`tmp/c89-route-edit-snapshot-comparison.json`, `tmp/c89-route-edit-native-tests.log`,
`tmp/c89-route-edit-standalone.log`, `tmp/c89-route-edit-asan.log`,
`tmp/c89-route-edit-output-comparison.json` and `tmp/c89-route-edit-timeline.log`.

Studio/Marcus binaries remained frozen, without concurrent builds or compaction.
Older completed scrolling captures were transparently compressed before these
runs; their files remain available. SDL reused the explicitly recorded warm
package cache from the preceding slices, with separate process workspaces,
settings, registry isolation and working movies. OS file caching remains
uncontrolled. Evidence is in `tmp/c89-movie-data-regression/playback.sqlite` and
adjacent suite artifacts:

| Suite | Case | Completed captures |
| --- | --- | --- |
| `20261113T015420Z-6b85c78c` | actor-audio | six retail/SDL/Marcus seek/playback runs |
| `20261113T015559Z-d16e9367` | actor-events | six retail/SDL/Marcus seek/playback runs |
| `20261113T015818Z-0cad99e4` | scrolling-text | six retail/SDL/Marcus seek/playback runs |
| `20261113T020231Z-f0039f75` | sampled-audio | three retail/SDL/Marcus playback runs |

The actor cases preserve each player's preceding authored positions and pixels,
with 60 ordered playback changes per player. Actor-audio passes the permanent
first-scene 600/1300 Hz tone checks and retains PCM16 peaks of 3192 retail and
4081 SDL/Marcus. Peaks establish audible coverage, not exact waveform/timing parity.

Scrolling preserves all 180 seek images, 60 authored positions per run and ordered
playback changes (328 retail/SDL, 321 Marcus). Only adjacent identical position/
image records are collapsed; original raw samples and timestamps remain available.

Baselines are actor-audio suite `20261113T012702Z-d81906ec`, actor-events suite
`20261113T012846Z-0fe30a43` and scrolling suite `20261113T013023Z-e524888d`.
Scene-audio uses suite `20261113T013339Z-d6edfa4b`.
All 21 captures completed without retries, preserving all 540 seek images and
each player's preceding authored positions and ordered playback changes.
Scene-audio retains 60 changes per player and PCM16 peaks of 2380 retail and
2176 SDL/Marcus. Movie identities match the preceding captures; every suite's
movie, ISO-resource and Marcus-content integrity checks pass. The combined report
is `tmp/c89-route-edit-playback-actor-audio-actor-events-scrolling-text-sampled-audio.json`.
Existing cross-player differences remain; one repetition
per suite does not establish general repeatability or full retail parity.
Stage/event propagation, backward deletion, recording, undo, queue/runtime
integration, remaining body/resource implementations and final facade removal
remain unfinished.

## C89 backward deletion and neighboring-subroute propagation (13 November 2026)

Backward deletion, orientation storage/weighting, angle averaging, body positioning,
lifetime-cache decisions and neighboring-subroute initialization-event propagation
now execute in C89. C reuses the existing route/event editor, pose evaluator,
lifetime evaluator and action editor. Primitive metadata/body services and an
adapter into the already C-owned seek evaluator remain native. See the
[implementation boundary](../code-map/library-split.md#actor-backward-deletion-and-neighboring-subroutes).

The conversion retains BUG1870/BUG1866 behavior, matching-held-frame double
subtraction, rotation replacement, action-cel updates, merge/cursor ordering,
offstage node removal, alive/reset publication and final lifetime/invalidation
order. Metadata failure reports an error and continues. Recording returns the
cached last frame while leaving lifetime dirty; failed non-recording queries
retain partial caches/last frame and preserve independent output. Weighting keeps
the exclusive upper bound `[6553,65536)` and wraparound angle averaging. Primitive
BRender distance math remains external.

Previous propagation retains backward non-costume/forward costume scans, its
costume-mask early return, and prune/seek/delete/seek failure order. Later copying
stops at exact route-location boundaries, positions only after successful
transform copies, then selects the default action. Copied tags intentionally gain
an owning reference before merge/application, fixing the original borrowed-copy
bug. Logical payloads remain identical; failed application retains an owning row.

All 48 standalone C-only Clang checks (including strict C89 syntax), 62 focused
native checks, MSVC AddressSanitizer, all 51 CLI baselines and the headless scene/
timeline check pass. All 34 snapshots match, including the 282,784-byte native
trace frozen before conversion across 576 real-actor scenarios. Real-resource
checks also verify ordinary tagged copies in both directions and through failed
edits, including final teardown. Standalone tests cover lifetime failure/recording
outputs, held offsets, alive/reset observations, angle bounds/padding, owning
copies through failed application, the costume-mask early return and both seek
failure boundaries. No Marcus UI tests were added.

Evidence includes `tmp/c89-subroute-edit-before.bin`, `tmp/c89-subroute-edit-after.bin`,
`tmp/c89-subroute-edit-snapshot-comparison.json`, `tmp/c89-subroute-edit-native-tests.log`,
`tmp/c89-subroute-edit-standalone.log`, `tmp/c89-subroute-edit-asan.log`,
`tmp/c89-subroute-edit-output-comparison.json`, `tmp/c89-subroute-edit-timeline.log`
and `tmp/c89-subroute-edit-final-diagnostics-tests.log`. The final targeted check
also verifies that backward deletion continues through initial and final lifetime
query failures, with both diagnostics and final range invalidation preserved.

Frozen Studio/Marcus binaries ran without concurrent builds or compaction. Older
completed scrolling evidence was transparently compressed before capture; its
files remain available. SDL reused the explicitly recorded warm package cache
from preceding slices with separate workspaces, settings, registry isolation and
working movies. OS file caching remains uncontrolled. Evidence is in
`tmp/c89-movie-data-regression/playback.sqlite` and adjacent suite artifacts:

| Suite | Case | Completed captures |
| --- | --- | --- |
| `20261113T024142Z-e153f863` | actor-audio | six retail/SDL/Marcus seek/playback runs |
| `20261113T024515Z-66ecd428` | actor-events | six retail/SDL/Marcus seek/playback runs |
| `20261113T024710Z-56f88e0d` | scrolling-text | six retail/SDL/Marcus seek/playback runs |
| `20261113T025236Z-e11c7d7e` | sampled-audio | three retail/SDL/Marcus playback runs |

The actor cases preserve each player's preceding authored positions and pixels,
with 60 ordered playback changes per player. Actor-audio passes the permanent
first-scene 600/1300 Hz tone checks and retains PCM16 peaks of 3192 retail and
4081 SDL/Marcus. Peaks establish audible coverage, not exact waveform/timing parity.

Baselines are actor-audio suite `20261113T015420Z-6b85c78c` and actor-events suite
`20261113T015559Z-d16e9367`. Scrolling uses `20261113T015818Z-0cad99e4` and
scene-audio uses `20261113T020231Z-f0039f75`.

All 21 captures completed without retries, preserving all 540 seek images and
each player's preceding authored positions and ordered playback changes.
Scrolling retains 328 ordered changes for retail/SDL and 321 for Marcus;
scene-audio retains 60 per player with PCM16 peaks of 2380 retail and 2176
SDL/Marcus. Only adjacent identical position/image records are collapsed;
original raw samples and timestamps remain available. Movie identities match
preceding captures, and every suite's movie, ISO-resource and Marcus-content
integrity checks pass. The combined report is
`tmp/c89-subroute-edit-playback-actor-audio-actor-events-scrolling-text-sampled-audio.json`.

Existing cross-player differences remain; one repetition
per suite does not establish general repeatability or full retail parity.
Stage entrance/removal/placement, recording, remaining transform/time edits,
undo, queue/runtime integration, body/resource implementations and final facade
removal remain unfinished.

### C89 actor placement (13 November 2026)

`Actor::_GetNewOrigin` now delegates placement policy to `actor_placement.c`.
The native adapter only supplies authored coordinates, camera/clipping metadata
and the text-template flag. The C implementation preserves the all-zero authored
point sentinel, lazy camera query, 25-unit default distance, clipping midpoint,
floor projection, authored heights, text offset and wrapping scalar arithmetic.
Stage entrance/removal orchestration and recording remain C++.

The C-only placement test covers authored points with just one nonzero axis,
camera query counts, the exact clipping boundary, odd midpoint rounding,
projection/translation, text and scalar overflow. A native test independently
evaluates the original BRender formula for 4,096 combinations of coordinates,
text flags, clipping planes and fractional matrices; all results and camera
query counts match. These checks exercise placement policy directly. Existing
playback movies check retained playback behavior and do not exercise interactive
placement themselves.

Both Studio and Marcus rebuild successfully. All 49 standalone C checks,
including strict C89 syntax, and all 63 focused native checks pass. MSVC
AddressSanitizer passes. All 34 preceding snapshots and 51 CLI baselines match.
Logs are `tmp/c89-placement-native-tests.log`, `tmp/c89-placement-standalone.log`,
`tmp/c89-placement-asan.log`,
`tmp/c89-event-edit-movie-save-load-test-placement-snapshot.log`,
`tmp/c89-placement-snapshot-comparison.json` and
`tmp/c89-placement-output-comparison.json`.

The headless scene/timeline check also passes (`tmp/c89-placement-timeline.log`).
Frozen Studio/Marcus binaries ran the following suites without concurrent builds
or compaction. Older completed evidence was transparently compressed beforehand;
raw capture files remain available. SDL reused the recorded warm package cache
with private processes, settings, workspaces and working movies. OS file caching
remains uncontrolled.

| Suite | Case | Completed captures |
| --- | --- | --- |
| `20261113T030833Z-b6764518` | actor-audio | six retail/SDL/Marcus seek/playback runs |
| `20261113T031001Z-729f380d` | actor-events | six retail/SDL/Marcus seek/playback runs |
| `20261113T031149Z-562d7321` | scrolling-text | six retail/SDL/Marcus seek/playback runs |
| `20261113T031456Z-bd5204b0` | sampled-audio | three retail/SDL/Marcus playback runs |

All 21 captures completed without retries. Same-player baselines are the preceding
subroute-edit suites: actor-audio `20261113T024142Z-e153f863`, actor-events
`20261113T024515Z-66ecd428`, scrolling-text `20261113T024710Z-56f88e0d` and
sampled-audio `20261113T025236Z-e11c7d7e`. All 540 seek images, authored positions
and ordered playback changes match. Only adjacent identical position/image
records are collapsed; raw timestamps and samples remain available. Movie IDs
match and all movie/resource/content integrity checks pass.

Actor cases retain 60 playback changes per player. Scrolling retains 328 for
retail/SDL and 321 for Marcus. Actor audio retains the first-scene 600/1300 Hz
tone coverage and PCM16 peaks of 3192 retail and 4081 SDL/Marcus; scene audio
retains 60 changes and peaks of 2380 retail and 2176 SDL/Marcus. Audible coverage
and peaks do not establish exact waveform or timing parity. Existing cross-player
differences remain; one repetition does not establish general repeatability.
The database and suite artifacts remain under `tmp/c89-movie-data-regression`;
the combined comparison is
`tmp/c89-placement-playback-actor-audio-actor-events-scrolling-text-sampled-audio.json`.

Stage entrance/removal, recording, remaining transform/time edits, undo,
queue/runtime integration, body/resource implementations and final facade removal
remain unfinished. No Marcus UI tests were added.

### C89 actor stage entrance and removal (13 November 2026)

`actor_stage_edit.c` now owns stage entrance, removal and entire-subroute deletion.
The native actor methods delegate to it. Entrance preserves event-before-route
reservation, placement/origin/yaw, early lifetime-cache extension, obsolete-tail
deletion, point/event relocation, Add and neighboring-event inheritance, body
positioning, zero step and freeze. Final range invalidation is deferred until
success, while completed mutations remain on failure.

Offstage removal does nothing. At the entrance frame, removal deletes the subroute,
hides the body, restores default costume best-effort, rewinds recording/state and
seeks the saved frame if events remain. Empty tracks retain the invalid frame and
first-frame sentinel. Seeking can reveal an earlier onstage subroute, which then
receives Remove. Failed seeks retain partial changes. Native adapters still supply
table capacity, primitive body/resource/scene operations and the existing C seek
integration. Placement policy itself was converted in the preceding slice.

The native-before trace `tmp/c89-stage-edit-before.bin` contains 129,568 bytes
across 256 scenarios covering first/earlier/later entrances, active route
truncation, offstage/interior/entrance removal, text, recording flags, cached range
extensions and metadata failures. The converted trace matches after ignoring only
the Add payload's two unused ABI-padding bytes. No event fields or meaningful
payload bytes are ignored.

The standalone C test checks capacity requests and reservation short-circuiting,
route preservation on early failure, partial edits at apply/insert/reservation
failures, deferred invalidation, owning costume inheritance in both directions,
empty-track sentinels, revealed earlier subroutes and seek failure. The earlier
subroute tests share their unchanged table/service fixture with this test.
Studio and Marcus rebuild successfully. All 50 standalone C checks, including
strict C89 syntax, all 64 focused native checks and MSVC AddressSanitizer pass.
All 35 snapshots, 51 CLI baselines and the headless scene/timeline check pass.
Evidence is in `tmp/c89-stage-standalone.log`, `tmp/c89-stage-native-tests.log`,
`tmp/c89-stage-asan.log`, `tmp/c89-stage-snapshot-comparison.json`,
`tmp/c89-stage-output-comparison.json` and `tmp/c89-stage-timeline.log`.

Frozen Studio/Marcus binaries ran without concurrent builds or compaction.
Completed evidence was transparently compressed between suites to provide
headroom; raw captures and prior failed runs remain available. SDL reused the
recorded warm package cache with private processes, settings, workspaces and
working movies. OS file caching remains uncontrolled.

| Suite | Case | Completed captures |
| --- | --- | --- |
| `20261113T032810Z-c3670af5` | actor-audio | six retail/SDL/Marcus seek/playback runs |
| `20261113T032931Z-4f7d0c4c` | actor-events | six retail/SDL/Marcus seek/playback runs |
| `20261113T033130Z-d3270cae` | scrolling-text | six retail/SDL/Marcus seek/playback runs |
| `20261113T033449Z-146d21f9` | sampled-audio | three retail/SDL/Marcus playback runs |

All 21 captures completed without retries. Same-player baselines are the placement
suites: actor-audio `20261113T030833Z-b6764518`, actor-events
`20261113T031001Z-729f380d`, scrolling-text `20261113T031149Z-562d7321` and
sampled-audio `20261113T031456Z-bd5204b0`. All 540 seek images, authored positions
and ordered playback changes match. Only adjacent identical position/image records
are collapsed; raw timestamps and samples remain available. Movie IDs match and
all movie/resource/content integrity checks pass.

Actor cases retain 60 playback changes per player. Scrolling retains 328 for
retail/SDL and 321 for Marcus. Actor audio retains first-scene 600/1300 Hz coverage
and PCM16 peaks of 3192 retail and 4081 SDL/Marcus; scene audio retains 60 changes
and peaks of 2380 retail and 2176 SDL/Marcus. These audio checks establish audible
coverage, not exact waveform/timing parity. Existing cross-player differences
remain; one repetition does not establish general repeatability or full retail
parity. The captures preserve playback behavior; direct engine edit tests and the
native-before trace cover stage-edit policy itself.

Evidence remains under `tmp/c89-movie-data-regression`, with the combined report
`tmp/c89-stage-playback-actor-audio-actor-events-scrolling-text-sampled-audio.json`
and counts/peak verification in `tmp/c89-stage-playback-summary.json`.
Recording, remaining transform/time edits, undo, queue/runtime integration,
body/resource implementations and final facade removal remain unfinished.
No Marcus UI tests were added.

### C89 actor route recording (13 November 2026)

`actor_record.c` now owns recording validity and record-step sizing plus begin,
move and end policy. C owns mouse smoothing, wrapped time thresholds, sampled
point insertion, ground clipping, frame-only waits, gap/lifetime extension,
rejoining, displaced-event deletion/merging, action fill, freeze/step ordering
and failure decisions. BUG1961/BUG1973 behavior is preserved. Native callbacks
still provide BRender distance, scene seek and the temporary `_RestoreFromUndo`
transaction; undo/body/view integration remains C++ work.

The 439,824-byte trace `tmp/c89-record-before.bin` was captured before conversion
across 640 cases. All bytes match the converted trace. Cases cover begin, move,
finish, validity and complete sequences; replace/rejoin, partial-node starts,
nearby Remove, frozen/ground input, elapsed-time boundaries and wrap, frame-only
advancement, multiple displaced events, epsilon joins, future Add boundaries,
metadata failures and native undo restoration. An additional native test passes
the same bool pointer for both movement outputs on a frame-only advance.

Output callbacks publish at the original assignments before scene seek or undo.
Their assignment mask preserves native aliased-output behavior. The C test
checks that mask and output visibility before failed scene seeks. It also checks
untouched outputs on early exits, exact threshold boundaries, timestamp wrap,
smoothing overflow and truncation, record-step failure outputs, segment splits,
stub removal, failed lifetime refresh, reservation order, partial insertion,
ground clipping without changing authored distance, gap extension, tag ownership
through merges/deletion, zero-distance joins, Add boundaries and final mode/rejoin
cleanup. Poisoned old table handles verify no table access after restoration.

Final Studio and Marcus builds pass. All 51 standalone C checks (including strict
C89 syntax), 65 focused native checks and MSVC AddressSanitizer pass. All 36
snapshots and 51 CLI baselines match; the headless scene/timeline check passes.
Evidence includes `tmp/c89-record-standalone.log`, `tmp/c89-record-native-tests.log`,
`tmp/c89-record-asan.log`, `tmp/c89-record-snapshot-comparison.json`,
`tmp/c89-record-output-comparison.json` and `tmp/c89-record-timeline.log`.

Final Studio/Marcus binaries remained frozen, with no builds or compaction during
captures. Completed evidence was transparently compressed between suites for
headroom; raw files remain available. SDL reused the explicitly recorded warm
package cache with private processes, settings, workspaces and working movies.
OS file caching remains uncontrolled.

| Suite | Case | Captures |
| --- | --- | --- |
| `20261113T040127Z-ddcff15b` | actor-audio | six complete retail/SDL/Marcus seek/playback runs |
| `20261113T040301Z-83bdf6d0` | actor-events | six complete retail/SDL/Marcus seek/playback runs |
| `20261113T040519Z-8ccdbc49` | scrolling-text | six complete retail/SDL/Marcus seek/playback runs |
| `20261113T040827Z-29d273c9` | sampled-audio | retail/SDL complete; Marcus audio-capture error retained |
| `20261113T041118Z-cb6c1040` | sampled-audio | successful Marcus-only retry |

The original Marcus scene-audio attempt reported `Audio capture write failed`.
The runner and capture processes were confirmed stopped. Completed scrolling
evidence was compressed to increase headroom, then only Marcus was retried with
the same binaries and capture policy. The original WAV, packet log, error and
database row remain. The write-error cause was not established. The retry has no
cross-player peers, so its suite verdict is inconclusive despite its complete run.

An earlier six-run actor-audio suite, `20261113T035627Z-4012563d`, also remains.
It preceded the final output-assignment-mask adapter fix and is superseded by
`20261113T040127Z-ddcff15b`. Final comparisons require the explicitly recorded
`final assignment-mask revision` environment marker.

All 21 selected successful captures match their preceding same-player baselines:
actor-audio `20261113T032810Z-c3670af5`, actor-events `20261113T032931Z-4f7d0c4c`,
scrolling-text `20261113T033130Z-d3270cae` and scene audio
`20261113T033449Z-146d21f9`. All 540 seek images, authored positions and ordered
playback changes match. Only adjacent identical position/image records are
collapsed; raw observations and timestamps remain available. Movie identities
match and all movie/resource/content integrity checks pass.

Actor cases retain 60 playback changes per player; scrolling retains 328 for
retail/SDL and 321 for Marcus. Actor audio retains first-scene 600/1300 Hz coverage
and PCM16 peaks of 3192 retail and 4081 SDL/Marcus. Scene audio retains 60 changes
and peaks of 2380 retail and 2176 SDL/Marcus. Audible coverage and peaks do not
establish exact waveform/timing parity. Existing cross-player differences remain;
one successful repetition does not establish general repeatability or full retail
parity. Direct recording tests and the native-before trace cover editing policy;
the capture corpus checks retained playback behavior.

Evidence remains under `tmp/c89-movie-data-regression`, with combined report
`tmp/c89-record-playback-actor-audio-actor-events-scrolling-text-sampled-audio.json`
and count/peak verification in `tmp/c89-record-playback-summary.json`.
Remaining transform/time edits, undo, queue/runtime integration, body/resource
implementations and final facade removal remain unfinished. No Marcus UI tests
were added.

### C89 actor transform and time edits (13 November 2026)

`actor_transform_edit.c` now owns local/world rotation, normalization, scale,
stretch, route tweaks, composed movement and sooner/later edits. Native methods
adapt state and primitive services. The port preserves path-angle removal in
Z/Y/X order, local pre-rotation, world X/Z swapping/post-rotation/normalization,
the zero-world-rotation early return, exclusive upper bounds, transform fill
and the original scale-versus-factor tweak/position comparison.

Movement publishes requested movement before reservation and ground clipping;
that output remains true when clipping stops all actual movement. C owns weighted
entrance orientation, wrapped angular differences, bounded inverse speed, current
rotation event updates, scene/subroute precedence, origin refresh and dirty/body
sequencing. Sooner/later edits preserve gap absorption order, range/frame cache
publication before scene seeking, continuation after lifetime failure and partial
changes after failed seeking. Scrollbars update only on success.

The native-before trace `tmp/c89-transform-before.bin` contains 1,032,004 bytes
across 1,152 cases. It matches the converted trace exactly, including matrices,
positions, cached origins, frames/ranges, event payloads and return values. Cases
cover both rotation scopes, old current/path orientations, text, boundary scale/
stretch factors, ground clipping, all movement scopes, orienting movement, zero
deltas, positive/negative time shifts and failed lifetime queries. BRender's
distance and matrix normalization remain primitive callbacks into the existing
external C implementation; edit policy is C89.

The C test covers bounds, retained/removed tweaks, reservation and partial-apply
failure ordering, zero world rotation, primitive normalization, revert, output
publication before reservation, clipped movement, translation precedence, text/
orientation handling, partial orientation failure, tag ownership, gap absorption,
failed lifetime queries and failed scene seeks. Both hosts build successfully.
All 52 standalone C checks (including strict C89 syntax), 66 focused native checks
and MSVC AddressSanitizer pass. All 37 snapshots and 51 CLI baselines match, and
the headless scene/timeline check passes. Logs include
`tmp/c89-transform-standalone.log`, `tmp/c89-transform-native-tests.log`,
`tmp/c89-transform-asan.log`, `tmp/c89-transform-snapshot-comparison.json`,
`tmp/c89-transform-output-comparison.json` and `tmp/c89-transform-timeline.log`.

Frozen Studio/Marcus binaries ran without concurrent builds or compaction.
Completed older and current evidence was transparently compressed between suites
to provide headroom, including before the final scene-audio suite. All raw files
and prior failed-run records remain. SDL reused the recorded warm package cache
with private processes, settings, workspaces and working movies; OS file caching
remains uncontrolled. Final suites record `final transform/time revision`:

| Suite | Case | Completed captures |
| --- | --- | --- |
| `20261113T043400Z-f1048e22` | actor-audio | six retail/SDL/Marcus seek/playback runs |
| `20261113T043644Z-25feb92b` | actor-events | six retail/SDL/Marcus seek/playback runs |
| `20261113T044139Z-83a82af0` | scrolling-text | six retail/SDL/Marcus seek/playback runs |
| `20261113T044726Z-c4b76d53` | sampled-audio | three retail/SDL/Marcus playback runs |

All 21 completed without retries. Baselines are actor-audio
`20261113T040127Z-ddcff15b`, actor-events `20261113T040301Z-83bdf6d0`, scrolling
`20261113T040519Z-8ccdbc49`, retail/SDL scene audio
`20261113T040827Z-29d273c9` and the successful Marcus scene-audio retry
`20261113T041118Z-cb6c1040`. The latter suite is used only for Marcus; its preceding
failed attempt remains an error. No capture policy was weakened.

All 540 seek images, authored positions and ordered playback changes match each
player's preceding results. Only adjacent identical position/image records are
collapsed; raw observations/timestamps remain. Movie identities match and every
suite's movie/resource/content integrity checks pass. Actor cases retain 60
ordered changes per player; scrolling retains 328 for retail/SDL and 321 for
Marcus. Actor audio retains first-scene 600/1300 Hz coverage and PCM16 peaks of
3192 retail and 4081 SDL/Marcus. Scene audio retains 60 changes and peaks of 2380
retail and 2176 SDL/Marcus.

The combined report is
`tmp/c89-transform-playback-actor-audio-actor-events-scrolling-text-sampled-audio.json`,
with count/peak checks in `tmp/c89-transform-playback-summary.json`. Captures remain
under `tmp/c89-movie-data-regression`. Audible coverage/peaks do not establish
exact waveform/timing parity. Existing cross-player differences remain; one
repetition does not establish general repeatability or full retail parity. Native
edit traces and direct C tests cover editing policy; captures check retained
playback behavior. Undo, queue/runtime integration, body/resource implementations
and final facade removal remain unfinished. No Marcus UI tests were added.

### Actor ownership and restoration C89 conversion (13 November 2026)

`actor_owner.c` now owns actor runtime initialization/disposal, duplication,
reset, value copying, track/sound swaps and undo restoration. Native actors
marshal a plain C runtime and provide resource retain/release, C-backed table/
sound operations, scene seeking and view invalidation. The undo controller and
the underlying scene/body/resource implementations remain conversion work.

The native-before ownership trace `tmp/c89-owner-before.bin` contains 94,208
bytes across 128 cases, and `tmp/c89-undo-before.bin` contains 3,240 bytes across
32 real movie/scene undo cases. Both match after conversion. Coverage includes
shared body/template/cache references, reset versus ordinary duplication,
allocation failures, retained recording/rejoin metadata, partial undo seek
failure, undo creation, undo/redo scene replacement, names and actor removal.
Serialized pointers are normalized; reference counts and identity predicates
remain checked. The new C test checks clone short-circuiting, output publication
while the failed actor is destroyed, resource balance, reset, table swaps and
ignored seek failure. No Marcus UI tests were added.

Both hosts build. All 53 standalone C checks (including strict C89 syntax), 67
focused native checks and MSVC AddressSanitizer pass. All 39 snapshot comparisons
and 51 CLI baselines match; the headless scene/timeline check passes. Evidence
includes `tmp/c89-owner-standalone.log`, `tmp/c89-owner-native-checks.log`,
`tmp/c89-owner-asan.log`, `tmp/c89-owner-snapshot-comparison.json`,
`tmp/c89-owner-output-comparison.json` and `tmp/c89-owner-timeline.log`.

Both binaries were frozen before playback, with no concurrent builds or
compaction. SDL used the same recorded warm package cache with private processes,
settings and working movies; OS caching remains uncontrolled. Completed old and
current suites were transparently compressed between runs to retain headroom on
C:. Raw evidence and earlier failure records remain intact. The same-player
baselines are actor-audio `20261113T043400Z-f1048e22`, actor-events
`20261113T043644Z-25feb92b`, scrolling-text `20261113T044139Z-83a82af0` and
sampled-audio `20261113T044726Z-c4b76d53`.

| Suite | Case | Completed captures |
| --- | --- | --- |
| `20261113T051808Z-7594ae43` | actor-audio | six retail/SDL/Marcus seek/playback runs |
| `20261113T052216Z-6bb1efdf` | actor-events | six retail/SDL/Marcus seek/playback runs |
| `20261113T052423Z-f2d7b0db` | scrolling-text | six retail/SDL/Marcus seek/playback runs |
| `20261113T052909Z-08d1d9aa` | sampled-audio | three retail/SDL/Marcus playback runs |

All 21 captures completed on their first attempt. Movie identities and all
source/resource/content integrity checks pass. All 540 seek images, authored
positions and ordered playback changes match their preceding same-player
results. Only adjacent identical position/image observations are collapsed;
raw timestamps and observations remain. Actor cases retain 60 ordered changes
per player; scrolling retains 328 for retail/SDL and 321 for Marcus.

The strict peak-equality audit caught one difference: SDL actor audio peaked at
4080 rather than the preceding 4081. Retail/Marcus actor peaks remain 3192/4081;
scene-audio peaks remain 2380 retail and 2176 SDL/Marcus. Actor first-scene
600/1300 Hz coverage passes. A separate diagnostic repeat of only SDL actor-audio
playback, `20261113T053040Z-3e91e684`, completed using the same unchanged binary
and returned peak 4081, with matching ordered images and passing tone/integrity
checks. Thus peak variation occurs within this frozen revision. The original
4080 result and failed exact-peak assertion are retained, not replaced by the
repeat. No waveform/timing equality or cause for that variation is established.

The original 21-run report is
`tmp/c89-owner-playback-actor-audio-actor-events-scrolling-text-sampled-audio.json`.
`tmp/c89-owner-playback-peak-check.log` records the failed strict peak assertion;
`tmp/c89-owner-playback-audit.json` records all 540 matching seek images, ordered
changes, the explicit peak exception and diagnostic repeat. Existing cross-player
differences remain; these captures do not establish general repeatability or
full retail parity. Undo controller/edit wrappers, queue/runtime integration,
scene/body/resource implementations and final facade removal remain unfinished.

### Actor undo controller C89 conversion (13 November 2026)

`actor_undo.c` now owns undo creation, snapshot reference lifetime, toggle and
redo frame switching. C controls scene-switch/seek failure, outgoing-actor
retention, name capture and swap (including ignored rename failure), ID changes,
scene replacement/removal, view selection, range invalidation, snapshot reset
and sound queue play/flush selection. The native undo object supplies field
marshaling, temporary String scratch storage and primitive movie/scene calls.
Movie/scene/body/resource implementations and composite undo remain native work.

The 3,240-byte native-before undo trace across 32 real movie/scene cases and
94,208-byte ownership trace across 128 cases both remain identical. Direct C
tests additionally cover scene-switch failure, seek failure that destroys its
owner, actor insertion failure with retained name/ID mutations, missing actors,
text/view selection, failed name capture/rename, removal, references, allocation/
registration failure, optional creation names, redo dispatch and frame
restoration on success/failure. The seek-failure path clears undo and returns
without publishing into a potentially destroyed owner. Redo retains its original
caller-lifetime requirement and reads back changes before restoring only its
temporary frame.

Both hosts build. All 54 standalone C checks (including strict C89 syntax), 68
focused native checks and MSVC AddressSanitizer pass. All 39 snapshot comparisons
and 51 CLI baselines match; the headless scene/timeline check passes. Evidence
includes `tmp/c89-undo-controller-standalone.log`,
`tmp/c89-undo-controller-native-checks.log`, `tmp/c89-undo-controller-asan.log`,
`tmp/c89-undo-controller-snapshot-comparison.json`,
`tmp/c89-undo-controller-output-comparison.json` and
`tmp/c89-undo-controller-timeline.log`.

The native and Studio binaries were frozen before capture; no builds or
compaction ran concurrently. Older completed evidence was transparently
compressed before the captures, retaining raw files and prior failed runs on C:.
SDL reused the recorded warm package cache with private processes, settings and
working movies; OS caching remains uncontrolled. Same-player baselines are the
preceding ownership suites: actor-audio `20261113T051808Z-7594ae43`, actor-events
`20261113T052216Z-6bb1efdf`, scrolling-text `20261113T052423Z-f2d7b0db` and
sampled-audio `20261113T052909Z-08d1d9aa`.

| Suite | Case | Completed captures |
| --- | --- | --- |
| `20261113T053949Z-c358d10a` | actor-audio | six retail/SDL/Marcus seek/playback runs |
| `20261113T054149Z-272cc1de` | actor-events | six retail/SDL/Marcus seek/playback runs |
| `20261113T054310Z-ef4de7c2` | scrolling-text | six retail/SDL/Marcus seek/playback runs |
| `20261113T054623Z-03c510c6` | sampled-audio | three retail/SDL/Marcus playback runs |

All 21 captures completed without retries. Movie identities and all source/
resource/content integrity checks pass. All 540 seek images, authored positions
and ordered playback changes match each player's preceding results. Only
adjacent identical position/image observations are collapsed; raw timestamps
and observations remain. Actor cases retain 60 ordered changes per player;
scrolling retains 328 retail/SDL and 321 Marcus.

Actor first-scene 600/1300 Hz coverage passes. Retail/Marcus actor-audio peaks
remain 3192/4081. SDL peaks at 4081, one unit above the immediate baseline's
4080 and equal to the preceding unchanged-binary diagnostic repeat's 4081.
That earlier same-binary variation remains documented above; this audit records
the delta explicitly and does not claim exact peak equality. Scene-audio peaks
remain 2380 retail and 2176 SDL/Marcus. Audible coverage/peaks do not establish
waveform/timing parity, and the cause of the one-unit variation is not proven.

The combined report is
`tmp/c89-undo-controller-playback-actor-audio-actor-events-scrolling-text-sampled-audio.json`;
`tmp/c89-undo-controller-playback-audit.json` records image/change counts and all
before/after peaks, including the SDL delta. Raw captures remain under
`tmp/c89-movie-data-regression`. Existing cross-player differences remain; one
repetition does not establish general repeatability or full retail parity.
Composite undo, actor edit/clipboard wrappers, scene/body/resource implementations,
queue/runtime integration and final facade removal remain unfinished. No Marcus
UI tests were added.

### Composite actor undo C89 conversion (13 November 2026)

`actor_group_undo.c` now owns move/rename composite creation, entry disposal,
child references, traversal/context propagation and name swaps. Native owners
marshal fields, pointer/name records and String scratch values; arrays and GST
operations already delegate to the shared C storage implementation. Both redo
adapters retain virtual undo dispatch. Native object/scene/movie implementations
and remaining actor edit/clipboard wrappers still require conversion.

The 2,100-byte native-before trace `tmp/c89-group-undo-before.bin` matches after
conversion. It records 16 child-dispatch cases, eight allocation/factory attempts
and eight real-movie rename cases with three toggles each. Coverage includes
propagated scene/frame context, NULL rows, first/second child failure, references,
factory allocation failures, missing roll-call actors, duplicate rename IDs,
undo/redo name swaps and resource balance. C tests additionally check failed
append cleanup, release-before-clear ordering, immediate return when a failing
child destroys the group, partial rename failure and ignored name-storage
failure. The latter retains the native partial changes; it is not repaired by
the port. No Marcus UI tests were added.

Both hosts build. All 55 standalone C checks (including strict C89 syntax), 69
focused native checks and MSVC AddressSanitizer pass. All 40 snapshot comparisons
and 51 CLI baselines match, and the headless scene/timeline check passes. Evidence
includes `tmp/c89-group-undo-standalone.log`, `tmp/c89-group-undo-native-checks.log`,
`tmp/c89-group-undo-asan.log`, `tmp/c89-group-undo-snapshot-comparison.json`,
`tmp/c89-group-undo-output-comparison.json` and `tmp/c89-group-undo-timeline.log`.

Both binaries were frozen before playback, without concurrent builds or
compaction. Completed older/current captures were transparently compressed before
and between terminal suites to retain headroom on C:; raw evidence and prior
failed runs remain. SDL reused
the recorded warm package cache with private processes, settings and working
movies; OS file caching remains uncontrolled. Same-player baselines are the
preceding undo-controller suites: actor-audio `20261113T053949Z-c358d10a`,
actor-events `20261113T054149Z-272cc1de`, scrolling-text
`20261113T054310Z-ef4de7c2` and sampled-audio `20261113T054623Z-03c510c6`.

| Suite | Case | Completed captures |
| --- | --- | --- |
| `20261113T060152Z-42e96bd2` | actor-audio | six retail/SDL/Marcus seek/playback runs |
| `20261113T060346Z-632826ce` | actor-events | six retail/SDL/Marcus seek/playback runs |
| `20261113T060520Z-f5abd0ff` | scrolling-text | six retail/SDL/Marcus seek/playback runs |
| `20261113T060929Z-5a536734` | sampled-audio | three retail/SDL/Marcus playback runs |

All 21 completed without retries. Movie identities and all source/resource/
content integrity checks pass. All 540 seek images, authored positions and
ordered playback changes match their preceding same-player results. Only
adjacent identical position/image observations are collapsed; raw timestamps
and observations remain. Actor cases retain 60 ordered changes per player;
scrolling retains 328 for retail/SDL and 321 for Marcus.

Actor first-scene 600/1300 Hz coverage passes. Actor-audio PCM16 peaks match at
3192 retail and 4081 SDL/Marcus; scene-audio peaks match at 2380 retail and
2176 SDL/Marcus. The preceding one-unit SDL variation and its unchanged-binary
diagnostic remain preserved above; there is no peak delta in this set of runs.
Audible coverage/peaks do not establish exact waveform/timing parity.

The combined report is
`tmp/c89-group-undo-playback-actor-audio-actor-events-scrolling-text-sampled-audio.json`,
with counts and before/after peaks in `tmp/c89-group-undo-playback-audit.json`.
Raw evidence remains under `tmp/c89-movie-data-regression`. Existing cross-player
differences remain; one repetition does not establish general repeatability or
full retail parity. Actor edit/clipboard wrappers, queue/runtime integration,
scene/body/resource implementations and final facade removal remain unfinished.

### Actor edit transactions C89 conversion (13 November 2026)

`actor_transaction.c` owns action, stage add/remove, normalization, costume and
deletion transactions: duplicate, core apply, rollback, undo registration,
release and optional snapshot/alive output publication. Native adapters provide
representation transfers and calls to the already migrated C core edits. The
52,704-byte native-before trace `tmp/c89-edit-transaction-before.bin` matches
exactly across 112 scenarios: 80 action/stage/normalization/deletion cases and
32 valid material/custom-costume cases, with allocation failures, Add-frame
deletion, optional outputs, registration, rollback and reference balance.

Direct C tests additionally cover partial seeking, frame readback after deletion,
alive-output timing, null outputs, lifetime failure that continues, diagnostic
order and retained mutations. Two existing quirks are preserved explicitly:
costume continues after failed duplication, and failed removal core restores
without releasing its local snapshot. Nonallocating mocks check those sequences;
the native fixture avoids the invalid NULL-snapshot costume path and the leaked
failed-removal path. This conversion does not claim to repair either bug.

Both hosts build. All 56 standalone C checks (including strict C89 syntax), 70
focused native checks and MSVC AddressSanitizer pass. All 41 snapshot comparisons
and 51 CLI baselines match, and the headless scene/timeline check passes. Evidence
includes `tmp/c89-transaction-standalone.log`, `tmp/c89-transaction-native-checks.log`,
`tmp/c89-transaction-asan.log`, `tmp/c89-transaction-snapshot-comparison.json`,
`tmp/c89-transaction-output-comparison.json` and `tmp/c89-transaction-timeline.log`.

Both binaries were frozen before playback, without concurrent builds or
compaction. Completed captures were transparently compressed before/between
terminal suites to retain headroom on C:, preserving raw evidence and prior
failed runs. SDL reused the recorded warm package cache with private processes,
settings and working movies; OS file caching remains uncontrolled. Same-player
baselines are actor-audio `20261113T060152Z-42e96bd2`, actor-events
`20261113T060346Z-632826ce`, scrolling-text `20261113T060520Z-f5abd0ff` and
sampled-audio `20261113T060929Z-5a536734`.

| Suite | Case | Completed captures |
| --- | --- | --- |
| `20261113T062938Z-fda4f0e1` | actor-audio | six retail/SDL/Marcus seek/playback runs |
| `20261113T063106Z-7d5beb21` | actor-events | six retail/SDL/Marcus seek/playback runs |
| `20261113T063251Z-5cef53e5` | scrolling-text | six retail/SDL/Marcus seek/playback runs |
| `20261113T063715Z-290bcbd6` | sampled-audio | three retail/SDL/Marcus playback runs |

All 21 completed without retries. Movie identities and all source/resource/
content integrity checks pass. All 540 seek images, authored positions and
ordered playback changes match each player's preceding results. Only adjacent
identical position/image observations are collapsed; raw timestamps and
observations remain. Actor cases retain 60 ordered changes per player;
scrolling retains 328 retail/SDL and 321 Marcus.

Actor first-scene 600/1300 Hz coverage passes. PCM16 peaks match at 3192 retail
and 4081 SDL/Marcus for actor audio, and 2380 retail and 2176 SDL/Marcus for
scene audio. The earlier one-unit SDL variation and its unchanged-binary repeat
remain preserved in the ownership evidence; this set has no peak delta. These
coverage/peak checks do not establish exact waveform/timing parity.

The combined report is
`tmp/c89-transaction-playback-actor-audio-actor-events-scrolling-text-sampled-audio.json`,
with image/change counts and before/after peaks in
`tmp/c89-transaction-playback-audit.json`. Raw evidence remains under
`tmp/c89-movie-data-regression`. Existing cross-player differences remain; one
repetition does not establish general repeatability or full retail parity.
Copy/paste/clipboard, movie/scene/body/resource implementations, queue/runtime
integration and final facade removal remain unfinished. No Marcus UI tests
were added.

### Actor and route copying C89 conversion (13 November 2026)

`actor_copy.c` owns full-actor/route-only copy policy: event filtering, current
cel replacement, source sound-tag resolution/write ordering, relative event
locations, subroute boundaries, route slicing and release-before-output-clear
failure cleanup. Full copy preserves original node coordinates with shortened
stored distance; route-only copy interpolates coordinates and recomputes the
remaining distance, retaining epsilon 1 for zero length. Payloads stay locked
across insertion and unlock before merge/destruction. Source sound-tag updates
remain even if the sound is filtered out or a later copy operation fails.

The 405,760-byte native-before trace `tmp/c89-copy-before.bin` matches exactly
across 320 synthetic runtime scenarios in private movies. It covers both scopes,
node offsets/holds/endpoints, earlier/future event selection, cached templates,
owning material/sound tags, allocation/resolve failures, source/destination
tables, rewound state and references. Pointers and Add padding are normalized
only in serialization. Fixture sound chunks exercise mapping without decoding
audio. C tests additionally cover locked payload views, partial resolve/insert
failure, resize/reservation failure, current versus future action cels, BUG1950
filtering, hold clamping, zero-row bulk copy, unknown-event diagnostics and
interpolated/epsilon paths. No Marcus UI tests were added.

Coverage correction from the subsequent clipboard audit: that initial native
trace exercised factory failures in all 320 cases. A prefetched test template
uses a different reader key from `Template::FReadTmpl`; the dummy chunk could
not satisfy the normal actor factory. Thus its success-path coverage claim
above was overstated. The C tests remain separate evidence. The clipboard
conversion section below records a corrected, complete template/action graph,
explicit expected outcomes and a fresh original-C++ versus converted comparison.
The original trace is preserved; it is not silently replaced or relabeled.

Native actor/template creation and movie sound resolution remain C++ integration
dependencies. Paste/clipboard, scene/body/resource/runtime implementations and
facade removal still require conversion; this does not complete the backend.

Both hosts build. All 57 standalone C checks (including strict C89 syntax), 71
focused native checks and MSVC AddressSanitizer pass. All 42 snapshot comparisons
and 51 CLI baselines match, and the headless scene/timeline check passes. Evidence
includes `tmp/c89-copy-standalone.log`, `tmp/c89-copy-native-checks.log`,
`tmp/c89-copy-asan.log`, `tmp/c89-copy-snapshot-comparison.json`,
`tmp/c89-copy-output-comparison.json` and `tmp/c89-copy-timeline.log`.

Both binaries were frozen before capture, without concurrent builds or
compaction. Completed older captures and older build symbol/library files were
transparently compressed beforehand to retain headroom on C:. Their contents,
raw evidence and prior failed runs remain. SDL reused the recorded warm package
cache with private processes, settings and working movies; OS caching remains
uncontrolled. Same-player baselines are the preceding transaction suites:
actor-audio `20261113T062938Z-fda4f0e1`, actor-events
`20261113T063106Z-7d5beb21`, scrolling-text `20261113T063251Z-5cef53e5` and
sampled-audio `20261113T063715Z-290bcbd6`.

| Suite | Case | Completed captures |
| --- | --- | --- |
| `20261113T071739Z-44096303` | actor-audio | six retail/SDL/Marcus seek/playback runs |
| `20261113T071943Z-6d08a35a` | actor-events | six retail/SDL/Marcus seek/playback runs |
| `20261113T072143Z-04f39e28` | scrolling-text | six retail/SDL/Marcus seek/playback runs |
| `20261113T072500Z-62e4e35f` | sampled-audio | three retail/SDL/Marcus playback runs |

All 21 completed without retries. Movie identities and all source/resource/
content integrity checks pass. All 540 seek images, authored positions and
ordered playback changes match each player's preceding results. Only adjacent
identical position/image observations are collapsed; raw timestamps and
observations remain. Actor cases retain 60 ordered changes per player;
scrolling retains 328 retail/SDL and 321 Marcus.

Actor first-scene 600/1300 Hz coverage passes. Retail/Marcus actor peaks remain
3192/4081. SDL peaks at 4080 rather than the immediate baseline's 4081, within
the one-unit range already observed with an unchanged binary in the ownership
slice. The delta is retained explicitly; this does not prove its cause or exact
peak equality. Scene-audio peaks remain 2380 retail and 2176 SDL/Marcus.
Coverage/peaks do not establish exact waveform/timing parity.

The combined report is
`tmp/c89-copy-playback-actor-audio-actor-events-scrolling-text-sampled-audio.json`,
with image/change counts and all before/after peaks in
`tmp/c89-copy-playback-audit.json`. Raw evidence remains under
`tmp/c89-movie-data-regression`. Existing cross-player differences remain; one
repetition does not establish general repeatability or full retail parity.
Paste/clipboard, movie sound resolution, actor/template creation, scene/body/
resource/runtime implementations and final facade removal remain unfinished.

### Actor route paste C89 conversion (13 November 2026)

`actor_route_paste.c` owns reservation, node splitting/deletion, tagged-event
removal, later-subroute index shifts, translated insertion, floating/terminal
steps, dirty/range notification and final body positioning. It calls existing C
route, event, step and pose implementations directly. The native actor supplies
state marshaling, array reservation, external C BRender distance and diagnostics.
Underlying native movie/scene/body/resource representations remain conversion
dependencies; full actor paste and clipboard control are still C++.

The 167,168-byte native-before trace `tmp/c89-route-paste-before.bin` matches
exactly across 256 cases, including empty/short clipboard routes, node offsets/
holds, later subroutes, terminal nodes, material/sound tag ownership, state,
transforms and resource balance. Pointer fields and Add padding are normalized
only in serialization. The split temporary's undefined native distance is
initialized to nonzero epsilon; the temporary point is removed/overwritten
before the resulting route, whose recorded state is unchanged.

C tests cover unchanged clipboard data, reservation short-circuiting, split
failure after cursor publication, tagged-event closure, partial event/step/apply
failure, terminal ordering, epsilon, STATIC metadata and diagnostic-and-continue
point insertion failure. Failures retain prior edits for caller undo; the port
does not introduce rollback into this operation. No Marcus UI tests were added.

Both hosts build. All 58 standalone C checks (including strict C89 syntax), 72
focused native checks and MSVC AddressSanitizer pass. All 43 snapshot comparisons
and 51 CLI baselines match, and the headless scene/timeline check passes. Evidence
includes `tmp/c89-route-paste-standalone.log`, `tmp/c89-route-paste-native-checks.log`,
`tmp/c89-route-paste-asan.log`, `tmp/c89-route-paste-snapshot-comparison.json`,
`tmp/c89-route-paste-output-comparison.json` and `tmp/c89-route-paste-timeline.log`.

Both binaries were frozen before playback, without concurrent builds or
compaction. Build symbols/libraries, older binaries and completed captures were
transparently compressed before/between terminal suites to retain headroom on
C:, preserving their contents and all raw/failed-run evidence. SDL reused the
recorded warm package cache with private processes, settings and working movies;
OS caching remains uncontrolled.
Same-player baselines are actor-audio `20261113T071739Z-44096303`, actor-events
`20261113T071943Z-6d08a35a`, scrolling-text `20261113T072143Z-04f39e28` and
sampled-audio `20261113T072500Z-62e4e35f`.

| Suite | Case | Completed captures |
| --- | --- | --- |
| `20261113T075758Z-87357889` | actor-audio | six retail/SDL/Marcus seek/playback runs |
| `20261113T080021Z-dccfb394` | actor-events | six retail/SDL/Marcus seek/playback runs |
| `20261113T080240Z-bb5a7330` | scrolling-text | six retail/SDL/Marcus seek/playback runs |
| `20261113T080718Z-9168a4d6` | sampled-audio | three retail/SDL/Marcus playback runs |

All 21 completed without retries. Movie identities and all source/resource/
content integrity checks pass. All 540 seek images, authored positions and
ordered playback changes match their preceding same-player results. Only
adjacent identical position/image observations are collapsed; raw timestamps
and observations remain. Actor cases retain 60 ordered changes per player;
scrolling retains 328 retail/SDL and 321 Marcus.

Actor first-scene 600/1300 Hz coverage passes. Retail/Marcus actor peaks remain
3192/4081. SDL peaks at 4081 rather than the immediate baseline's 4080, within
the one-unit range already observed in unchanged-binary runs. That delta remains
explicit, without claiming its cause or exact peak equality. Scene-audio peaks
remain 2380 retail and 2176 SDL/Marcus. Coverage/peaks do not establish exact
waveform/timing parity.

The combined report is
`tmp/c89-route-paste-playback-actor-audio-actor-events-scrolling-text-sampled-audio.json`,
with image/change counts and before/after peaks in
`tmp/c89-route-paste-playback-audit.json`. Raw evidence remains under
`tmp/c89-movie-data-regression`. Existing cross-player differences remain; one
repetition does not establish general repeatability or full retail parity.
Full actor paste, clipboard control, movie sound resolution, actor/template
creation, scene/body/resource/runtime implementations and final facade removal
remain unfinished.

### Full actor paste C89 conversion (13 November 2026)

`actor_paste.c` owns the full actor-paste controller. `Actor::FPaste` now marshals
records and supplies primitive runtime/resource services. C controls text-template
transfer and ownership, lifetime and scene binding, placement, frame translation,
user-sound filtering/adoption, text entrance orientation and final translation/dirty
publication. Both hosts use this implementation. Native movie autosave/sound-child
lookup, template fetching, body creation and resource representations remain
conversion dependencies; this is not the final C-only backend boundary.

The 22,272-byte native-before trace `tmp/c89-actor-paste-before.bin` has SHA-256
`D03BE92FB33A9E6C87A8FC4879E39433B24F1E6A0101A83A6DFBC213035AC0E9`
and matches the converted implementation exactly. Its 64 cases use private
movies, actual serialized 3D-text templates, same/cross-resource transfers,
frame offsets, authored placement, sound tags/adoption, consecutive missing
sounds, media sounds, invalid first events, resulting rows and resource counts.
Only serialized cache pointers and Add padding are normalized. The fixture
retains its test body to isolate paste policy; text body creation/rendering is
not proved by this fixture. Dummy sound chunks exercise lookup/adoption without
decoding audio; the playback corpus supplies separate render/audio coverage.

C tests cover autosave/save/fetch failures, duplicate-tag cleanup and old-template
release order, replacement text flags, disabled BUG1888 migration, wrapped frame
arithmetic, no-op retiming, failed sound saves, child-adoption failure, partial
state and removal cursor changes. The native operation has no rollback: earlier
resource/frame/deletion edits remain. Child failure does not write the locally
adopted tag into the event or add cleanup; final route translation, text yaw and
dirty notification are skipped. No Marcus UI tests were added.

Both hosts build. All 59 standalone C checks (including strict C89 syntax), 73
focused native checks and MSVC AddressSanitizer pass. All 44 snapshot comparisons
and 51 freshly linked CLI baselines match, and headless scene/timeline playback
passes. Evidence includes `tmp/c89-actor-paste-standalone.log`,
`tmp/c89-actor-paste-native-tests.log`, `tmp/c89-actor-paste-asan.log`,
`tmp/c89-actor-paste-snapshot-comparison.json`,
`tmp/c89-actor-paste-output-comparison.json` and
`tmp/c89-actor-paste-preview-scene.log`.

Both binaries were frozen before playback. Completed old captures and build
symbols/libraries were transparently compressed beforehand, and completed current
suites between terminal captures, to retain headroom on C: while preserving their
contents. No build or compaction ran during captures.
SDL reused the recorded warm package cache with private processes, settings and
working movies; OS caching remains uncontrolled. Same-player baselines are
actor-audio `20261113T075758Z-87357889`, actor-events
`20261113T080021Z-dccfb394`, scrolling-text `20261113T080240Z-bb5a7330` and
sampled-audio `20261113T080718Z-9168a4d6`.

| Suite | Case | Completed captures |
| --- | --- | --- |
| `20261113T082527Z-60a851d4` | actor-audio | six retail/SDL/Marcus seek/playback runs |
| `20261113T082656Z-7c0ed243` | actor-events | six retail/SDL/Marcus seek/playback runs |
| `20261113T082845Z-140ed208` | scrolling-text | six retail/SDL/Marcus seek/playback runs |
| `20261113T083256Z-8eafb346` | sampled-audio | three retail/SDL/Marcus playback runs |

All 21 completed without retries. Movie identities and all source/resource/
content integrity checks pass. All 540 seek images, authored positions and
ordered playback changes match the preceding same-player results. Only adjacent
identical position/image observations are collapsed; raw observations and
timestamps remain. Actor cases retain 60 ordered changes per player; scrolling
retains 328 retail/SDL and 321 Marcus.

Actor first-scene 600/1300 Hz coverage passes. Measured actor peaks remain 3192
retail and 4081 SDL/Marcus; scene-audio peaks remain 2380 retail and 2176
SDL/Marcus. All before/after peak deltas are zero. The previously documented
SDL 4080/4081 variation remains part of the evidence; these observations do not
establish exact waveform/timing parity or general repeatability. Existing
cross-player differences and one-repeat inconclusive comparisons remain.

The combined report is
`tmp/c89-actor-paste-playback-actor-audio-actor-events-scrolling-text-sampled-audio.json`,
with counts and before/after peaks in `tmp/c89-actor-paste-playback-audit.json`.
Raw evidence remains under `tmp/c89-movie-data-regression`. Clipboard control,
remaining actor/template creation and movie sound integration, scene/body/
resource/runtime implementations and final C++ facade removal remain unfinished.

### Actor clipboard C89 conversion and copy-coverage correction (13 November 2026)

`actor_clipboard.c` owns clipboard creation, route/full copying, raw prefix
comparison, naming order, actor ownership, paste dispatch, reset duplication
and removal/release after rename failure. The native document facade stores the
plain C actor/route state and retains its String name representation. String
assignment/append, movie/view paste and movie/scene operations remain native
conversion dependencies. These are temporary adapters, not the final host API.

The 41,344-byte `tmp/c89-clipboard-before.bin` native trace has SHA-256
`EF8280D642EB068C595B035BBDA174497E375A2287A38A323AE81BB208DD1FB6`
and matches the converted controller exactly across 64 successful creations.
Cases cover full/subroute scopes, route-only copies, raw case-sensitive prefixes,
empty and embedded-zero prefixes, names beginning with the prefix without a
word boundary, copied/source state and tables, names and reference balance.
Headless full paste rejects the missing MovieView, and route paste rejects the
missing selection. This fixture does not claim native successful clipboard
paste coverage. C tests separately cover successful route/full paste, reset
selection, duplicate/paste/rename failures, removal before release, repeated ID
lookup after rename, visible ownership during release and stored-actor retention.
The full actor-paste native fixture remains separate transfer/edit evidence.

Name overflow preserves the existing native leak: the copied actor is assigned
to the clipboard only after name construction succeeds, so failed append
releases the empty clipboard and leaks the local actor. C tests exercise this
with nonallocating reference counters, including partial truncation and
append-space short-circuiting. The native fixture does not intentionally leak
allocated actors. This is recorded legacy behavior, not an unnoticed cleanup
change. No Marcus UI tests were added.

The clipboard fixture exposed a gap in the earlier native copy fixture. Resource
keys include reader identity, so prefetching the test reader did not satisfy the
normal `Template::FReadTmpl` factory request. All 320 cases in the original
405,760-byte `tmp/c89-copy-before.bin` were factory failures. That artifact and
its previous comparisons remain preserved; their success-path coverage claim
was incorrect. Direct C tests were separate evidence and remain valid.

The corrected fixture writes an actual named template, hierarchy/body-set/
costume tables, an action header, four cels and a transform table. The normal
native reader and actor factory must succeed. It now asserts each expected
outcome: 128 normal successes, 128 injected allocation failures, 32 invalid-child
full-copy failures and 32 invalid-child route-copy successes. Its new 449,472-byte
trace `tmp/c89-copy-valid-template-native-before.bin` has SHA-256
`4EE560D25882CC123B79951206B309A4B4DCFF58A906DAF6D8D93EEA2E9FDC5D`.
The original `Actor::FCopy`/`FCopyRte` implementations from `c789578c^` were
temporarily compiled against the same current primitives/fixture, captured and
then restored to the C-backed implementation. Both copy and clipboard traces
match exactly. This explicitly supersedes the inadequate copy baseline; it
does not relax a comparison or discard the old raw evidence. Dummy sound chunks
still test tag mapping/adoption without decoding audio.

Both hosts build. All 60 standalone C checks (including strict C89 syntax), 74
focused native checks and MSVC AddressSanitizer pass. All 45 snapshot comparisons
and 51 freshly linked CLI baselines match, and headless scene/timeline playback
passes. Evidence includes `tmp/c89-clipboard-standalone.log`,
`tmp/c89-clipboard-native-tests.log`, `tmp/c89-clipboard-asan.log`,
`tmp/c89-clipboard-snapshot-comparison.json`,
`tmp/c89-clipboard-output-comparison.json`, `tmp/c89-clipboard-preview-scene.log`
and `tmp/c89-copy-valid-template-native-before.log`.

Playback binary contents were frozen before capture, without concurrent builds
or compaction. Symbols/libraries were transparently compressed beforehand, and
completed captures/build executables between terminal suites, to retain headroom
on C:. Executable hashes remain unchanged. SDL reused the recorded warm package
cache with private processes, settings and working movies; OS caching remains
uncontrolled.
Same-player baselines are actor-audio `20261113T082527Z-60a851d4`, actor-events
`20261113T082656Z-7c0ed243`, scrolling-text `20261113T082845Z-140ed208` and
sampled-audio `20261113T083256Z-8eafb346`.

| Suite | Case | Completed captures |
| --- | --- | --- |
| `20261113T085701Z-0e308e23` | actor-audio | six retail/SDL/Marcus seek/playback runs |
| `20261113T085845Z-cf20758a` | actor-events | six retail/SDL/Marcus seek/playback runs |
| `20261113T090135Z-17ae6b6e` | scrolling-text | six retail/SDL/Marcus seek/playback runs |
| `20261113T090610Z-87806dc5` | sampled-audio | three retail/SDL/Marcus playback runs |

All 21 completed without retries. Movie identities and all source/resource/
content integrity checks pass. All 540 seek images, authored positions and
ordered playback changes match the preceding same-player results. Only adjacent
identical position/image observations are collapsed; raw observations and
timestamps remain. Actor cases retain 60 ordered changes per player; scrolling
retains 328 retail/SDL and 321 Marcus.

Actor first-scene 600/1300 Hz coverage passes. Actor PCM peaks remain 3192 retail
and 4081 SDL/Marcus; scene-audio peaks remain 2380 retail and 2176 SDL/Marcus.
All measured before/after peak deltas are zero. These observations do not prove
exact waveform/timing parity or general repeatability. The previously documented
SDL 4080/4081 variation, cross-player differences and one-repeat inconclusive
comparisons remain part of the evidence.

The combined report is
`tmp/c89-clipboard-playback-actor-audio-actor-events-scrolling-text-sampled-audio.json`,
with counts and before/after peaks in `tmp/c89-clipboard-playback-audit.json`.
Raw evidence remains under `tmp/c89-movie-data-regression`. Remaining actor/
template creation, native String mutations, movie sound/view integration,
scene/body/resource/runtime implementations and final C++ facade removal remain
unfinished. Clipboard control does not complete the shared C89 backend.

### Actor creation and scene binding C89 conversion (13 November 2026)

`actor_create.c` owns actor factory control and partial-failure release, template/
tag acquisition, track-before-sound initialization, shared rewind/recording reset,
action-zero flags and scene/body binding order. `_FInit`, `_FCreateGroups`,
`PactrNew` and `SetPscen` now marshal into this controller. The native adapter
still supplies object allocation, template fetch/metadata, existing C-backed
table/sound owners, body construction, world binding and diagnostics. Template/
body/resource implementations and native object representations remain conversion
dependencies, not final backend host services.

The 10,120-byte native-before trace `tmp/c89-create-before.bin` has SHA-256
`13324278492766ED716B1E77396919AD41A6C555EF2110A9DAE910E646AECE60`
and matches the converted controller exactly across 120 cases. The fixture uses
the complete named template/action graph established by the preceding coverage
correction. It tests cold/warm caches, invalid template data, missing action cels,
BASE allocation failure, state/tables, reference balance, repeated binding and
body/world binding. Every expected factory outcome is asserted: 29 creations
and 91 failures. Seven creations have an injected body-creation failure; their
assigned scene remains visible and their body remains NULL. The 22 other bodies
bind successfully; repeated binding preserves body identity and references.
Zero-part bodies isolate control policy rather than proving model/rig assembly.

C tests additionally exercise fetch/group/metadata short-circuiting, acquisition
and publication order, recording reset, failed-query output preservation, shared
C owner disposal, direct initialization/group cleanup responsibility, already
bound scene diagnostics and failed-body retry suppression. Direct initialization
retains partial ownership; only the factory releases on failure. No Marcus UI
tests were added. Headless scene playback and the captures below separately
cover real actor/prop/text content through the existing template/body adapters.

Both hosts build. All 61 standalone C checks (including strict C89 syntax), 75
focused native checks and MSVC AddressSanitizer pass. All 46 snapshot comparisons
and 51 freshly linked CLI baselines match; headless scene/timeline playback
passes. Evidence includes `tmp/c89-create-standalone.log`,
`tmp/c89-create-native-tests.log`, `tmp/c89-create-asan.log`,
`tmp/c89-create-snapshot-comparison.json`, `tmp/c89-create-output-comparison.json`
and `tmp/c89-create-preview-scene.log`.

Playback binary contents were frozen before capture without concurrent builds
or compaction. Older completed captures and build symbols/libraries/executables
were transparently compressed beforehand, and completed captures between terminal
suites, to retain headroom on C: while preserving their bytes and raw evidence.
SDL reused the recorded warm package cache with
private processes, settings and working movies; OS caching remains uncontrolled.
Same-player baselines are actor-audio `20261113T085701Z-0e308e23`, actor-events
`20261113T085845Z-cf20758a`, scrolling-text `20261113T090135Z-17ae6b6e` and
sampled-audio `20261113T090610Z-87806dc5`.

| Suite | Case | Completed captures |
| --- | --- | --- |
| `20261113T092935Z-1e509d17` | actor-audio | six retail/SDL/Marcus seek/playback runs |
| `20261113T093127Z-5028ba80` | actor-events | six retail/SDL/Marcus seek/playback runs |
| `20261113T093558Z-0bb8626f` | scrolling-text | six retail/SDL/Marcus seek/playback runs |
| `20261113T094247Z-09d05586` | sampled-audio | three retail/SDL/Marcus playback runs |

All 21 completed without retries. Movie identities and all source/resource/
content integrity checks pass. All 540 seek images, authored positions and
ordered playback changes match the preceding same-player results. Only adjacent
identical position/image observations are collapsed; raw observations and
timestamps remain. Actor cases retain 60 ordered changes per player; scrolling
retains 328 retail/SDL and 321 Marcus.

Actor first-scene 600/1300 Hz coverage passes. Actor peaks remain 3192 retail
and 4081 Marcus. SDL peaks at 4080 versus the immediately preceding 4081, within
the one-unit range previously observed with unchanged binaries. The delta is
explicitly retained; this does not establish its cause or exact peak equality.
Scene-audio peaks remain 2380 retail and 2176 SDL/Marcus. Coverage and peaks do
not establish exact waveform/timing parity. Existing cross-player differences
and one-repeat inconclusive comparisons remain.

The combined report is
`tmp/c89-create-playback-actor-audio-actor-events-scrolling-text-sampled-audio.json`,
with counts and before/after peaks in `tmp/c89-create-playback-audit.json`.
Raw evidence remains under `tmp/c89-movie-data-regression`. Movie sound
integration, template/body construction, native String mutations, remaining
scene/movie/resource/runtime implementations and final C++ facade removal
remain unfinished. Actor creation control does not complete the C89 backend.

### Movie sound integration C89 conversion (13 November 2026)

`movie_sound.c` owns sound tag resolution/saving, first-free scene/movie child
IDs, scene/movie adoption, graph copying, physical-file MIDI/WAV dispatch,
failure cleanup, GC marking and unused-sound discovery. Private C++ child/ID
helpers are removed. Adapters provide C-backed graph/tag operations, autosave/
temp switching, file save, source metadata, diagnostics and existing sound-format
factories. Audio decoding/encoding, sound garbage collection itself and native
file/resource representations remain conversion dependencies, not final backend
host services. No Marcus UI tests were added.

The 15,580-byte native-before trace `tmp/c89-movie-sound-before.bin` has SHA-256
`C17678F29FB493845362CCF85A2BA30C2F87E47716B580E546977C52480FB891`.
The 15,592-byte SDL-before trace `tmp/c89-movie-sound-sdl-before.bin` has SHA-256
`70868559E696693F64AB49DF00A14FCFF692955AC862B08ABF94D416D9E2779C`.
Both match their respective converted implementations exactly across 68 cases
each. The 64 graph/tag cases assert successful child lookup/adoption, repeated
lookup, tag resolution/saving, graph copy and movie adoption; they cover gaps,
other child types, cache identity, missing sound children, graph rows, reference
counts and unused checks. Dummy sound envelopes exercise ownership/mapping
without decoding audio.

Four additional physical-file cases use valid and truncated WAV/MIDI inputs.
The SDL configuration successfully imports both valid formats and rejects both
truncated inputs. The native Windows configuration without the original AudioMan
library uses its existing E_NOTIMPL fallback: WAV imports fail, while valid MIDI
imports succeed. That limitation is asserted and recorded, not counted as working
WAV import. Availability is derived from the configured AudioMan dependency.
The SDL headless round-trip runs with dummy video/audio drivers; it is not a
Marcus UI test or proof of playback through an audio device.

C tests cover changed-number open-before-close, equal-number cache retention,
explicit scene/cache selection, enum failures, current-scene requery, external
graph IDs chosen from the working file, output publication before failed
adoption, copy/import failures, deletion versus retained adoption, temp/autosave
state changes, captured destinations, BadSoundFile error suppression, GC order
and unlinked/invalid unused sounds. Missing-scene copy diagnostics retain native
continue behavior because that result was unused. Trusted sound enumeration
failure was undefined after a native assertion; C initializes the otherwise
undefined number to zero before diagnosing/continuing. Supported graph behavior
and normal failure outputs match the native traces. Scalar output bridges use
representation copies, preserving untouched outputs without reading potentially
uninitialized caller values as typed integers.

Both hosts build. All 62 standalone C checks (including strict C89 syntax), 76
focused native checks, the additional SDL headless round-trip and MSVC
AddressSanitizer pass. All 48 snapshot comparisons and 51 freshly linked CLI
baselines match; headless scene/timeline playback passes. Evidence includes
`tmp/c89-movie-sound-standalone.log`, `tmp/c89-movie-sound-native-tests.log`,
`tmp/c89-movie-sound-sdl-after.log`, `tmp/c89-movie-sound-asan.log`,
`tmp/c89-movie-sound-snapshot-comparison.json`,
`tmp/c89-movie-sound-output-comparison.json` and
`tmp/c89-movie-sound-preview-scene.log`.

Playback binary contents were frozen before capture without concurrent builds
or compaction. Build symbols/libraries/executables were transparently compressed
beforehand, and completed capture folders between terminal suites, to retain
headroom on C: while preserving their bytes. SDL reused the
recorded warm package cache with private processes, settings and working movies;
OS caching remains uncontrolled. Same-player baselines are actor-audio
`20261113T092935Z-1e509d17`, actor-events `20261113T093127Z-5028ba80`,
scrolling-text `20261113T093558Z-0bb8626f` and sampled-audio
`20261113T094247Z-09d05586`.

| Suite | Case | Completed captures |
| --- | --- | --- |
| `20261113T103101Z-714068a5` | actor-audio | six retail/SDL/Marcus seek/playback runs |
| `20261113T103237Z-bebaf194` | actor-events | six retail/SDL/Marcus seek/playback runs |
| `20261113T103515Z-b0b4e0b8` | scrolling-text | six retail/SDL/Marcus seek/playback runs |
| `20261113T103921Z-30d32ca3` | sampled-audio | three retail/SDL/Marcus playback runs |

All 21 completed without retries. Movie identities and all source/resource/
content integrity checks pass. All 540 seek images, authored positions and
ordered playback changes match the preceding same-player results. Only adjacent
identical position/image observations are collapsed; raw observations and
timestamps remain. Actor cases retain 60 ordered changes per player; scrolling
retains 328 retail/SDL and 321 Marcus.

Actor first-scene 600/1300 Hz coverage passes. Actor peaks remain 3192 retail,
4080 SDL and 4081 Marcus; scene-audio peaks remain 2380 retail and 2176
SDL/Marcus. All measured before/after peak deltas are zero. The previously
documented SDL 4080/4081 variation remains part of the evidence; coverage/peaks
do not establish exact waveform/timing parity. Existing cross-player differences
and one-repeat inconclusive comparisons remain.

The combined report is
`tmp/c89-movie-sound-playback-actor-audio-actor-events-scrolling-text-sampled-audio.json`,
with counts and before/after peaks in `tmp/c89-movie-sound-playback-audit.json`.
Raw evidence remains under `tmp/c89-movie-data-regression`. Template/body
construction, sound-format factories, sound garbage collection, native String
mutations, remaining movie/scene/resource/runtime implementations and final C++
facade removal remain unfinished. This does not complete the shared C89 backend.

## Body construction and shape disposal C89 conversion (13 November 2026)

`body_create.c` now owns the body factory, one-time highlight initialization
decision, hidden state, set-list duplication, set counting, storage allocation,
root/highlight/body-part hierarchy construction and shape disposal. C publishes
partial state before dependent callbacks. Factory failure releases the partial
body; direct initialization retains ownership for caller cleanup. Disposal hides
visible bodies before releasing part models, set materials, actor/material
storage and the set list. Model/material internals and BRender field adapters,
body duplication/reshaping, template/default-costume construction and native
Body representation remain unfinished dependencies.

The before trace `tmp/c89-body-create-before.bin` was captured from the native
implementation before replacing these four methods. Its 88,136 bytes match
`tmp/c89-body-create-after.bin` exactly; SHA-256 is
`7C190938100DB5C1C5E203EEAF2BD3BA2686E012CDD384D930AD4AC777E97C53`.
The 162 cases cover empty, single-part, chain, branch, disconnected and sparse
body-set hierarchies, actor fields/links/matrices and visible-body disposal.
Native debug injection exercises BASE, handle and pointer allocation schedules:
150 successes and 12 failures all restore allocation counts. The release SDL
headless round-trip passes 162 cases without debug injection. These construction
fixtures contain no part models/materials; the C service test covers ordered
model/material release, and the separate real-content scene/playback checks
exercise resource-backed bodies.

All 63 standalone C checks, strict C89 syntax, MSVC AddressSanitizer, 77 focused
native checks, 49 snapshot comparisons and 51 CLI baselines pass. Both hosts
build and headless real-content scene playback passes. The first native CTest
invocation inherited relative snapshot paths from the preceding capture script,
which failed from CTest's different working directory; the isolated invocation
passes all 77. The failed setup log is retained. Evidence includes
`tmp/c89-body-create-standalone.log`, `tmp/c89-body-create-asan.log`,
`tmp/c89-body-create-native-tests-isolated.log`, `tmp/c89-body-create-sdl-after.log`,
`tmp/c89-body-create-snapshot-comparison.json`,
`tmp/c89-body-create-output-comparison.json` and
`tmp/c89-body-create-preview-scene.log`. No Marcus UI tests were added.

Playback executable contents remained frozen throughout capture. Their SHA-256
values were `D2C60D1555B348D2C9248B9D0C72B9CD306961BCE0DA8F06353651C0EDF3B6B5`
(SDL) and `5CD6151C147032BF099A75C240A7DE7FE58C7ADEF1133256E277D8359EEDFD91`
(Marcus regression). Symbols/binaries and completed capture folders were
transparently compressed only outside active captures to retain C: headroom;
no builds or compaction ran during capture. SDL reused the recorded warm package
cache with private processes/settings/working movies; OS caching was uncontrolled.

Same-player baselines were actor-audio `20261113T103101Z-714068a5`, actor-events
`20261113T103237Z-bebaf194`, scrolling-text `20261113T103515Z-b0b4e0b8` and
sampled-audio `20261113T103921Z-30d32ca3`.

| Suite | Case | Completed captures |
| --- | --- | --- |
| `20261113T105510Z-18d03bb1` | actor-audio | six retail/SDL/Marcus seek/playback runs |
| `20261113T105634Z-cbb842c5` | actor-events | six retail/SDL/Marcus seek/playback runs |
| `20261113T105801Z-b672a006` | scrolling-text | six retail/SDL/Marcus seek/playback runs |
| `20261113T110220Z-b8735843` | sampled-audio | three retail/SDL/Marcus playback runs |

All 21 completed without retries. Movie identities and source/resource/content
integrity checks pass. All 540 seek images, authored positions and ordered
playback changes match the preceding same-player results. Only adjacent
identical position/image observations are collapsed; raw timestamps and rows
remain. Actor cases retain 60 ordered changes per player; scrolling retains
328 retail/SDL and 321 Marcus.

Actor first-scene 600/1300 Hz coverage passes. Actor peaks are 3192 retail and
4081 SDL/Marcus: SDL increased by one from the preceding 4080, consistent with
the previously observed 4080/4081 variation. This delta remains explicit in
the audit. Scene-audio peaks remain 2380 retail and 2176 SDL/Marcus. Coverage
and peak measurements do not prove waveform/timing parity. Existing cross-player
differences and one-repeat inconclusive comparisons remain.

The combined report is
`tmp/c89-body-create-playback-actor-audio-actor-events-scrolling-text-sampled-audio.json`,
with counts and unsuppressed audio deltas in
`tmp/c89-body-create-playback-audit.json`. Raw evidence remains under
`tmp/c89-movie-data-regression`. Remaining template/costume/body/resource/runtime
implementations and final C++ facade removal keep the full C89 backend unfinished.

## Body duplication, restoration and reshape C89 conversion (13 November 2026)

`body_edit.c` owns body duplication/reference sequencing, preserved hidden
counts, receiver identifier repair, reshape rollback, and restoration of
materials, models, matrices and highlighting. It reuses C shape construction
and disposal. A visible original hides before duplication and is re-shown before
failed-copy disposal. Model references precede duplicated set/material storage;
generic materials retain one reference per part and custom materials one per
set. Successful reshape restores visibility before material/model/matrix edits.
Failure restores the saved body and releases the displaced partial shape.

Native adapters retain BRender actor pointer rebasing, whole-object swapping,
field/bounds/binding copies, material internals and renderer notifications.
Restoration deliberately preserves the existing whole-object swap and repairs
only the receiving body's actor identifiers. Native Body representation and
these adapters are temporary C++ dependencies, not the final backend boundary.

The native before trace `tmp/c89-body-edit-before.bin` contains 1,880,872 bytes
and matches `tmp/c89-body-edit-after.bin` exactly; SHA-256 is
`B784A86F449D26510C7C8A7FE47CB001196700D45C45638E91C35D2C6F0D2AE9`.
The 1,404 cases cover empty/one/six-part sources, growing/shrinking targets,
shared/missing models, generic/custom materials, canonical actor links including
previous-link slots, matrices, bounds/hit metadata, visible/hidden/nested-hidden
bodies, renderer attachment and highlighting. Debug allocator injection produces
1,278 successes and 126 failures. Model/material reference counts and native
allocation counts return to their starting values. SDL release runs the same
1,404 cases successfully without debug injection.

Standalone C tests cover every duplicate/reshape allocation stage, exact
reference balance, original-show-before-failed-copy-release ordering, restore
identifiers and hidden counts, successful/failed reshape, preserved matrices and
highlighting. All 64 standalone checks, strict C89 syntax, MSVC AddressSanitizer,
78 focused native checks, the SDL round-trip, 50 snapshot comparisons and 51 CLI
baselines pass. Both hosts build and headless real-content scene playback passes.
Evidence includes `tmp/c89-body-edit-standalone.log`,
`tmp/c89-body-edit-native-tests.log`, `tmp/c89-body-edit-sdl-after.log`,
`tmp/c89-body-edit-asan.log`, `tmp/c89-body-edit-snapshot-comparison.json`,
`tmp/c89-body-edit-output-comparison.json` and
`tmp/c89-body-edit-preview-scene.log`. No Marcus UI tests were added.

Playback binary contents remained frozen throughout capture. SHA-256 values were
`0A75759C24724957801BEAB9257A3BA3834613108E1963AED2F52A0012294949` (SDL) and
`EFB2A1A43BB5FF7FF995B4D06D71632418B036E262069216D5054E87238B5259`
(Marcus regression). Symbols/binaries and completed capture folders were
transparently compressed outside active captures to retain C: headroom while
preserving bytes. No builds or compaction ran during capture. SDL reused the
recorded warm package cache with private processes/settings/working movies;
OS caching remained uncontrolled.

Same-player baselines were actor-audio `20261113T105510Z-18d03bb1`, actor-events
`20261113T105634Z-cbb842c5`, scrolling-text `20261113T105801Z-b672a006` and
sampled-audio `20261113T110220Z-b8735843`.

| Suite | Case | Completed captures |
| --- | --- | --- |
| `20261113T111704Z-8696baf6` | actor-audio | six retail/SDL/Marcus seek/playback runs |
| `20261113T111839Z-b61c19b2` | actor-events | six retail/SDL/Marcus seek/playback runs |
| `20261113T112011Z-76415179` | scrolling-text | six retail/SDL/Marcus seek/playback runs |
| `20261113T112414Z-161b040e` | sampled-audio | three retail/SDL/Marcus playback runs |

All 21 completed without retries. Movie identities and source/resource/content
integrity checks pass. All 540 seek images, authored positions and ordered
playback changes match the preceding same-player results. Only adjacent
identical position/image observations are collapsed; raw rows/timestamps remain.
Actor cases retain 60 ordered changes per player; scrolling retains 328
retail/SDL and 321 Marcus.

Actor first-scene 600/1300 Hz coverage passes. Actor peaks remain 3192 retail and
4081 SDL/Marcus; scene-audio peaks remain 2380 retail and 2176 SDL/Marcus. All
measured before/after peak deltas are zero. The earlier SDL 4080/4081 variation
remains documented, and coverage/peaks do not establish waveform/timing parity.
Existing cross-player differences and one-repeat inconclusive comparisons remain.

The combined report is
`tmp/c89-body-edit-playback-actor-audio-actor-events-scrolling-text-sampled-audio.json`,
with counts and actual audio deltas in `tmp/c89-body-edit-playback-audit.json`.
Raw evidence remains under `tmp/c89-movie-data-regression`. Template/default
costumes, remaining body/material/resource/runtime implementations, native
adapters and final C++ facade removal remain unfinished; this slice does not
complete the shared C89 backend.

## Body materials, visibility and costume ownership C89 conversion (13 November 2026)

`body_runtime.c` owns visibility counts/notification decisions, highlight updates,
model replacement, matrix/translation sequencing, generic/custom material
replacement and queries. Same-model assignment returns without a reference or
notification change. Generic materials retain per part; custom materials retain
before removal, supporting self-use. Accessory models update before materials;
absent accessory models preserve existing models. Spare custom material entries
remain allowed. Removing the last custom owner clears its raw part handles
without dereferencing those potentially freed handles.

Visibility counts publish before renderer callbacks. Hiding clears bounds and
detaches before notifying; showing attaches before notifying. Nested counts and
repeated highlight/material notifications retain native behavior. Renderer fields,
BRender math/link adapters and resource object implementations remain native
dependencies, as do remaining body geometry/render hooks and template factories.

`ThreeMMBodyCostume` owns persistent snapshot storage and references in C.
Capture drops the old snapshot before allocating its replacement, leaving an
empty owner on allocation failure. Application preserves exact-shape checking
or explicit smaller-count copying. Empty-owner count is now defined as zero;
native debug used poison in that unused field. Applying an uncaptured owner
remains outside the supported preconditions. Getter inactive outputs retain
native invalidation; pointer bridges and fixture recording copy representations
without typed reads of possibly uninitialized or poisoned inactive slots.

The 427,368-byte native trace `tmp/c89-body-runtime-before.bin` matches
`tmp/c89-body-runtime-after.bin` exactly; SHA-256 is
`25E17FEA3087D7CBB0861DB233E46E41A33097C1EE82B614F449B2CB42F4E715`.
Its 90 scenarios cover empty/two/six-part bodies, visible/hidden/nested counts,
same/different models, generic/custom self-assignment, accessory models, matrices,
renderer notifications, unused getter bytes, snapshot replacement and applying
to smaller/larger shapes. Debug injection produces 72 successful replacement
captures and 18 allocation failures; reference/allocation counts restore.
SDL release covers all 90 successfully without debug injection.

C tests verify release/assign/retain ordering, same-model early return, nested
visibility notification order, custom self-use, snapshot failure clearing,
different-shape copying and no dereference after final custom-owner release.
All 65 standalone C checks, strict C89 syntax, MSVC AddressSanitizer, 79 focused
native checks, the SDL headless round-trip, 51 snapshots and 51 CLI baselines
pass. Both hosts build and headless real-content scene playback passes. The
fixture's final inactive-output recording refinement was recompiled and rerun
in native and SDL round-trips; all snapshot comparisons still match. Evidence
includes `tmp/c89-body-runtime-standalone.log`,
`tmp/c89-body-runtime-native-tests.log`, `tmp/c89-body-runtime-native-after.log`,
`tmp/c89-body-runtime-sdl-after.log`, `tmp/c89-body-runtime-asan.log`,
`tmp/c89-body-runtime-snapshot-comparison.json`,
`tmp/c89-body-runtime-output-comparison.json` and
`tmp/c89-body-runtime-preview-scene.log`. No Marcus UI tests were added.

Playback binary contents remained frozen: SHA-256
`A1C03D86A2FA0C8C45F114F755845107966C4F0EC2B780B224A2F8E663C17606` (SDL) and
`530BE4DED18A217CA3F8A04B0F8D57E820C810A21081D333D4733A808620E933`
(Marcus regression). Same-player baselines are actor-audio
`20261113T111704Z-8696baf6`, actor-events `20261113T111839Z-b61c19b2`,
scrolling-text `20261113T112011Z-76415179` and sampled-audio
`20261113T112414Z-161b040e`.

| Suite | Case | Captures |
| --- | --- | --- |
| `20261113T115308Z-943f4642` | actor-audio | six retail/SDL/Marcus seek/playback complete |
| `20261113T115530Z-aad98120` | actor-events | six retail/SDL/Marcus seek/playback complete |
| `20261113T115719Z-24e8d014` | scrolling-text | five complete; Marcus playback image capture failed while C: reported zero free space |
| `20261113T122454Z-07d21f20` | scrolling-text | only failed Marcus playback retried, complete |
| `20261113T122702Z-7a400836` | sampled-audio | three retail/SDL/Marcus playback complete |

The failed attempt is retained. Superseded capture directories were pruned after
the terminal failure, preserving their observations/summaries/diagnostics, and
the retention/shared-cache/free-space fix was installed. The existing warm SDL
cache moved to `tmp/c89-movie-data-regression/sdl-cache` without copying; final
sampled audio exercised the new default path and reused its sole existing
package. Processes/settings/working movies remained private, OS caching
uncontrolled. No builds or compaction ran during capture. Harness storage
management changed between the failed attempt and retry; player binary contents
did not change.

All 21 selected runs ultimately completed (22 attempts including that failure).
Movie identities and source/resource/content integrity pass. All 540 seek images,
authored positions and ordered playback changes match preceding same-player
results. Only adjacent identical position/image observations are collapsed;
raw timestamps and observations remain. Actor cases retain 60 ordered changes
per player; scrolling retains 328 retail/SDL and 321 Marcus.

First-scene actor 600/1300 Hz coverage passes. Actor peaks remain 3192 retail and
4081 SDL/Marcus; scene-audio peaks remain 2380 retail and 2176 SDL/Marcus. All
measured peak deltas are zero. Earlier SDL 4080/4081 variation remains documented;
peaks/coverage do not prove waveform/timing parity. Existing cross-player
differences and one-repeat inconclusive comparisons remain.

The combined report is
`tmp/c89-body-runtime-playback-actor-audio-actor-events-scrolling-text-sampled-audio.json`,
with counts/actual audio deltas in `tmp/c89-body-runtime-playback-audit.json`.
Current raw evidence and its latest baselines/failure remain under
`tmp/c89-movie-data-regression`; superseded raw artifacts follow the retention
policy above. Template/default-costume construction, body geometry/render hooks,
resource/format/runtime implementations and final native facade removal keep
the full C89 backend migration unfinished.

## Template construction, headers and default costumes C89 conversion (13 November 2026)

`template_create.c` owns factory selection/size estimates/failure cleanup, template
header I/O, ordinary template list initialization, group disposal and body/default
costume construction. Header/parent/set/costume fields publish in native order.
Direct initialization failure retains partial ownership; factory failure releases
the partial object and preserves native output invalidation. Native block
lifetimes remain scoped through field/I/O adapters. ThreeDText initialization and
default-costume overrides retain virtual dispatch and remain native dependencies,
along with action/accessory evaluation, resource object internals, String adapters
and final Template representation/facade removal.

Parent/set payloads and costume counts/IDs swap in C for opposite byte order.
Costume/action counts retain dense-prefix lookup, while missing GGCM triggers
the legacy full-child rescan for each set through a temporary zero-budget cache.
Default costumes are all fetched before applying any; failure releases fetched
references without applying a partial costume. Temporary slots clear after
release, matching native cleanup. Body construction reuses the shared C body
implementation and releases a body when default costume application fails.

Two explicit normalizations are recorded. The header writer sets its formerly
uninitialized two-byte padding to zero. The legacy GGCM builder keeps its first
50 matching IDs per set; a 51st now diagnoses/fails instead of overflowing the
native stack array. No equivalence is claimed for native undefined padding or
overflow. Header naming deliberately retains TMPL lookup even for another
supplied chunk type; name failure deletes the added chunk while retaining its
published number.

`tmp/c89-template-create-before.bin` and `tmp/c89-template-create-after.bin`
match across 306,048 normalized bytes, SHA-256
`1A141E018561E66669EB801C1A442974D5D6C79C3B50A24CBB048A7ECBA04713`.
Only undefined header padding is masked. The 1,344 native cases cover empty and
six-part templates, opposite byte order, missing header/name/parent/set tables,
generated GGCM rows, missing default resources and injected allocation failures.
They record 613 successful loads and 478 bodies. SDL release records 672 loads
and 588 bodies without debug injection. Defined wire fields, counts, names,
groups, bodies and failure outcomes match the native trace.

C tests independently cover estimate-only reads, virtual dispatch, output
invalidation, direct partial ownership, header/name/add failures, opposite host
order, dense child gaps, fallback cleanup and its 50/51 boundary, preload-before-
apply order, and released/cleared reference slots. All 66 standalone C checks,
strict C89 syntax, MSVC AddressSanitizer, 80 focused native checks, the SDL
round-trip, 52 snapshot comparisons and 51 CLI baselines pass. Both hosts build
and headless real-content scene playback passes. The final cleanup-slot
refinement was rebuilt and included in these checks. Evidence includes
`tmp/c89-template-create-standalone.log`,
`tmp/c89-template-create-native-tests-final.log`,
`tmp/c89-template-create-sdl-after.log`, `tmp/c89-template-create-asan.log`,
`tmp/c89-template-create-snapshot-comparison.json`,
`tmp/c89-template-create-output-comparison.json` and
`tmp/c89-template-create-preview-scene-final.log`. No Marcus UI tests were added.

Playback binaries remained frozen throughout capture: SHA-256
`DCF3AD7788B4D624C052A7B7699C54FAEBFF031EAFD5542C7DA2A665B33E2964` (SDL) and
`A68A5C658BABC654EEE50985C3CCA9B62861D8942A960C59A623050E90774128`
(Marcus regression). Same-player baselines are actor-audio
`20261113T115308Z-943f4642`, actor-events `20261113T115530Z-aad98120`,
scrolling-text `20261113T115719Z-24e8d014` except Marcus playback from its
successful retry `20261113T122454Z-07d21f20`, and sampled-audio
`20261113T122702Z-7a400836`.

| Suite | Case | Captures |
| --- | --- | --- |
| `20261113T131229Z-ab43f9be` | actor-audio | six retail/SDL/Marcus seek/playback complete |
| `20261113T131500Z-ba37104a` | actor-events | six retail/SDL/Marcus seek/playback complete |
| `20261113T131758Z-3c742199` | scrolling-text | six retail/SDL/Marcus seek/playback complete |
| `20261113T132351Z-705ab775` | sampled-audio | three retail/SDL/Marcus playback complete |

All 21 runs completed without retries. Movie identities and source/resource/
content integrity pass. All 540 seek images, authored positions and ordered
playback changes match preceding same-player results. Only adjacent identical
position/image observations are collapsed; raw observations and timestamps remain.
Actor cases retain 60 ordered changes per player; scrolling retains 328 retail/SDL
and 321 Marcus. First-scene actor 600/1300 Hz coverage passes. Actor peaks remain
3192 retail and 4081 SDL/Marcus; scene-audio peaks remain 2380 retail and 2176
SDL/Marcus. All measured peak deltas are zero. Peaks/coverage do not establish
waveform/timing parity; existing cross-player differences and one-repeat
inconclusive comparisons remain.

These runs reused the sole package under the default shared SDL cache, with
private processes/settings/working movies and uncontrolled OS caching. No builds
or compaction ran during capture. Automatic retention pruned superseded media
while retaining observations, summaries, diagnostics, current results, latest
baselines and latest failures. Evidence is in
`tmp/c89-template-create-playback-actor-audio-actor-events-scrolling-text-sampled-audio.json`
and `tmp/c89-template-create-playback-audit.json`. The remaining native dependencies
listed above keep the full C89 backend migration unfinished.

## Template action and accessory C89 conversion (13 November 2026)

`template_runtime.c` owns template child-resource routing, action-name lookup,
negative/positive cel wrapping, pose/model prefetch and application, action
distance/count/flag/sound queries, costume indexing, accessory detection and
parent-set search. Both Template and CustomMaterial facades use the C graph
helpers for model-child existence/equality. Native services retain primitive
container/resource/field access, ActionDefinition getters and virtual action/model
fetches, including ThreeDText overrides. ActionDefinition construction/header/list
loading, sound-tag internals, resource implementations, ThreeDText construction
and final representation/facade removal remain unfinished.

All models are fetched before distance publication or body mutation. A missing
model/allocation/action leaves the body and distance unchanged. Nil model IDs
preserve accessory models while matrices still update. Cleanup releases/clears
model slots in part order, frees temporary storage and releases the action last.
Count/flag/sound outputs publish before action release; distance-query output
publishes after release. Sound existence clears before fetching, and failed
fetch leaves the tag untouched. Four-byte representation copies bridge native
Windows long/unsigned long output storage and C int/unsigned int safely.

Accessory detection requires a resource cache and a model at child slot zero of
the first costume. Search returns the first matching set/part in native order.
Model comparison stops at the first missing child ID, so models beyond a shared
gap are deliberately ignored. Failed searches leave output unchanged. Native
parent/set/matrix index and positive action-cel-count preconditions remain.

`tmp/c89-template-runtime-before.bin` and `tmp/c89-template-runtime-after.bin`
match across 241,552 bytes, SHA-256
`896B6D4649284DF791F93071F1E10700CB7C40D36D8251069FEC5F475A3485E1`.
The trace records 405 real native pose/query cases: zero/two/six parts, five
negative/positive cel numbers, missing action/model, BASE/HQ/Pv allocation
injection, optional distance output, matrix/model identity, reference counts,
virtual-fetch order, valid sound tables and absent sound tables. Debug outcomes
are 168 successful poses/237 failures; SDL release is 189/216. Five real chunk
graph variants cover empty/shared/different model prefixes, a child-ID gap,
missing costume edge, action names and all pairwise costume comparisons.
There is no new output masking or behavior normalization in this trace.

Native ActionDefinition::GetSnd has a pre-existing unsigned `chidSnd >= 0` check:
with a sound table present, `chidNil` attempts an invalid array index. The trace
uses valid sound IDs or no sound table. The existing ActionDefinition destructor
also does not release its optional sound array; the fixture detaches/releases
its explicitly supplied table. These behaviors were not changed or silently
claimed as covered; ActionDefinition ownership/sound handling is subsequent work.

Independent C tests cover 168 pose scenarios, extreme signed cel numbers,
zero parts, allocation/model/action failure, nil distance output, preload-before-
apply and release/clear/free order, query output visibility at action release,
resource/name lookup failure, no-cache templates and sparse accessory graphs.
All 67 standalone C checks (including strict C89 syntax), 81 focused native
checks, MSVC AddressSanitizer, SDL save/load round-trip, 53 snapshot comparisons
and 51 CLI baselines pass. Both hosts build and real-content scene/timeline
playback passes. No Marcus UI tests were added. Evidence includes
`tmp/c89-template-runtime-standalone-final.log`,
`tmp/c89-template-runtime-native-tests.log`,
`tmp/c89-template-runtime-asan-final.log`,
`tmp/c89-template-runtime-sdl-after.log`,
`tmp/c89-template-runtime-snapshot-comparison.json`,
`tmp/c89-template-runtime-output-comparison.json` and
`tmp/c89-template-runtime-preview-scene.log`.

Playback binary SHA-256 values remained frozen through all captures:
`FC15C52FC6044DFF1B07C6F399FB4AC1216B5F3E89BD621196BC049438B6315F` (SDL) and
`D26FFDE34B61B9D29E097F2A3D00942615E8E4ABDB42F97A0986288C1A20343A`
(Marcus regression). Same-player baselines are actor-audio
`20261113T131229Z-ab43f9be`, actor-events `20261113T131500Z-ba37104a`,
scrolling-text `20261113T131758Z-3c742199` and sampled-audio
`20261113T132351Z-705ab775`.

| Suite | Case | Captures |
| --- | --- | --- |
| `20261113T133903Z-3e7f8218` | actor-audio | six retail/SDL/Marcus seek/playback complete |
| `20261113T134106Z-a34e583e` | actor-events | six retail/SDL/Marcus seek/playback complete |
| `20261113T134227Z-9fffe1c5` | scrolling-text | six retail/SDL/Marcus seek/playback complete |
| `20261113T134531Z-c3d70049` | sampled-audio | three retail/SDL/Marcus playback complete |

All 21 runs completed without retries. Movie identities and source/resource/
content integrity pass. All 540 seek images, authored positions and ordered
playback changes match preceding same-player results. Only adjacent identical
position/image observations are collapsed; raw observations/timestamps remain.
Actor cases retain 60 ordered changes per player; scrolling retains 328 retail/SDL
and 321 Marcus. First-scene actor 600/1300 Hz coverage passes. Actor peaks remain
3192 retail and 4081 SDL/Marcus; scene-audio peaks remain 2380 retail and 2176
SDL/Marcus. All measured peak deltas are zero. Peaks/coverage do not establish
waveform/timing parity; existing cross-player differences and one-repeat
inconclusive comparisons remain.

Captures reused the single default shared SDL package cache, with private
processes/settings/working movies and uncontrolled OS caching. No builds or
compaction ran during capture. Automatic retention pruned superseded raw media,
preserving observations, summaries, diagnostics, current results and their
latest baselines/failures. The combined report is
`tmp/c89-template-runtime-playback-actor-audio-actor-events-scrolling-text-sampled-audio.json`;
counts and actual audio deltas are in `tmp/c89-template-runtime-playback-audit.json`.
The native dependencies listed above keep the full C89 backend migration active.

## ActionDefinition resources and lifetime C89 conversion (13 November 2026)

`action_definition.c` owns persistent action state, duplicate construction,
resource factory estimates/cleanup, header/group/list loading, payload swapping,
16-byte wire sound-tag marshalling, disposal and primitive cel/part/matrix/sound
queries. Native ActionDefinition methods now provide representation/container/
file/allocation adapters. The former C++ group/list/flags fields are replaced by
one C-owned `ThreeMMActionDefinition`. Resource classes, native container/TAG
adapters, ThreeDText/font construction, remaining formats/runtime and final facade
removal keep the full backend conversion unfinished.

Header flags and partial ownership publish in native order. Direct initialization
failure retains tables; factory failure releases the partial action before
invalidating outputs. Estimate-only reads do not unpack/allocate. Final estimates
retain the native exclusion of optional sound-list bytes. Cel/part/matrix outputs
remain 8/4/48 bytes. Sound wire pointer padding is ignored; runtime cache pointers
are NULL and borrowed, with no extra tag references to close. Scalar/pointer
outputs use representation copies across the C/native boundary.

`tmp/c89-action-definition-before.bin` and `tmp/c89-action-definition-after.bin`
match all 258,592 bytes without masking, SHA-256
`91BE1BB4EB2DC821D5D77F3EF292342C125599761C93736F0113B1529480C9BA`.
There are 2,268 native cases across zero/one/three cels, four-part poses,
native/opposite byte order, missing cel/matrix/header chunks, a short header,
invalid optional sound data, constructor duplication, direct partial loading,
factory loading and BASE/HQ/Pv allocation injection. Debug outcomes are 597
successful loads and 152 duplications; SDL release is 648/162. Empty actions use
native payload order even under a foreign header, avoiding the former invalid
element-zero request. The trace records fields, all table payloads, public query
results, source reference counts and factory estimates/failure outcomes.

Three explicit corrections are outside equivalence claims for old failures:

- Disposal releases cels, matrices and the optional sound list in that order,
  clearing each slot. The old destructor leaked the optional list. The baseline
  fixture manually detached/released it; the converted fixture leaves cleanup
  to the owner and asserts all allocator counts return to their starting values,
  including partially initialized/filled lists.
- Sound queries treat high-bit IDs as negative no-sound markers, matching the
  native comment's signed convention instead of its broken unsigned comparison.
  Only the output type clears; other tag fields remain untouched. C and native
  checks cover `0xffffffff` and `0x80000000`. Existing corpus motion-sound playback
  is checked against retail; a dedicated retail marker-edge capture is not claimed.
- Empty foreign matrix payloads do not request element zero. C tests cover empty
  cel/matrix/sound lists in opposite byte order; no native equivalence is claimed
  for the former debug precondition violation.

Independent C tests simulate both host byte orders, verify cel/part/matrix payload
swaps and tag unmarshalling, exercise every initialization/factory/duplication
failure phase, retain partial owners until disposal, verify temporary wire-list
release, factory publication/invalidation order, estimate-only behavior, repeated
disposal, no-sound outputs and bad-order diagnostics.

The first standalone run exposed a validation weakness: Release defined NDEBUG,
disabling assert-based checks/setup calls. All engine C test executables now
explicitly undefine NDEBUG with compiler-appropriate options in every build
configuration. All 68 standalone checks were rebuilt/rerun successfully with
assertions enabled, including strict C89 syntax. Earlier standalone Release pass
counts alone were insufficient evidence for assert-based checks; debug MSVC and
ASan coverage were separate. The assertion-enabled run is recorded in
`tmp/c89-action-definition-standalone-assertions.log`.

All 82 focused native checks, MSVC AddressSanitizer, SDL save/load round-trip,
54 snapshot comparisons and 51 freshly linked CLI baselines pass. Both hosts
build and headless real-content scene/timeline playback passes. Evidence includes
`tmp/c89-action-definition-native-tests.log`,
`tmp/c89-action-definition-asan.log`,
`tmp/c89-action-definition-sdl-after.log`,
`tmp/c89-action-definition-snapshot-comparison.json`,
`tmp/c89-action-definition-output-comparison.json` and
`tmp/c89-action-definition-preview-scene.log`. No Marcus UI tests were added.

Frozen playback binary SHA-256 values were
`D288B4FCDBBA170B250534BF9BB970C19633DD12D5864C79FDA9B264495B9DEF` (SDL) and
`42BD85AE4564D7FE15EED305BACB84A7A6639196E61CF119756720C589A8A404`
(Marcus regression). Same-player baselines are actor-audio
`20261113T133903Z-3e7f8218`, actor-events `20261113T134106Z-a34e583e`,
scrolling-text `20261113T134227Z-9fffe1c5` and sampled-audio
`20261113T134531Z-c3d70049`.

| Suite | Case | Captures |
| --- | --- | --- |
| `20261113T140719Z-a25a00ea` | actor-audio | six retail/SDL/Marcus seek/playback complete |
| `20261113T140843Z-213f0b98` | actor-events | six retail/SDL/Marcus seek/playback complete |
| `20261113T141027Z-acafcbc6` | scrolling-text | six retail/SDL/Marcus seek/playback complete |
| `20261113T141333Z-2f24d32a` | sampled-audio | three retail/SDL/Marcus playback complete |

All 21 captures completed without retries. Movie identities and source/resource/
content integrity pass. All 540 seek images, authored positions and ordered
playback changes match preceding same-player results. Only adjacent identical
position/image observations are collapsed; raw observations/timestamps remain.
Actor cases retain 60 ordered changes per player; scrolling retains 328 retail/SDL
and 321 Marcus. First-scene actor 600/1300 Hz coverage passes.

Actor peaks are 3192 retail, 4080 SDL and 4081 Marcus. SDL changed from 4081 to
4080 (delta -1), within the previously documented 4080/4081 variation. The audit
retains this delta and reports `audio_peaks_equal: false`; it is not suppressed
or labeled an exact audio match. Scene-audio peaks remain 2380 retail and 2176
SDL/Marcus; all other measured peak deltas are zero. Peaks/coverage do not prove
waveform/timing parity. Existing cross-player differences and one-repeat
inconclusive comparisons remain.

Captures reused the sole package in the default shared SDL cache, with private
processes/settings/working movies and uncontrolled OS caching. No builds or
compaction ran during capture. Automatic retention pruned superseded raw media
while retaining observations, summaries, diagnostics, current results and latest
baselines/failures. Evidence is in
`tmp/c89-action-definition-playback-actor-audio-actor-events-scrolling-text-sampled-audio.json`
and `tmp/c89-action-definition-playback-audit.json`.

## ThreeDFont resources and authoring C89 conversion (13 November 2026)

`font_resource.c` owns persistent font metric state, resource size estimates,
loading, authoring, disposal, width/height queries and glyph lookup. ThreeDFont
now wraps `ThreeMMFontResource` rather than separate C++ count/maximum/array
fields. Native services provide block/allocation operations, child lists/graph
edits, retained model fetches, bounds and diagnostics. ThreeDText construction,
list/action/matrix generation, shapes/animations, model/resource implementations,
other formats/runtime operations and final facade removal remain unfinished.

Loading publishes count before exact-length validation and maximum height after
it, then publishes each array allocation before reading it. Direct failure keeps
partial ownership. Font factory failure invalidates outputs BEFORE releasing the
partial owner, retaining its distinction from the action/template factories.
Successful size publication precedes object publication. Packed estimates assume
256 metric pairs; final estimates include owner plus both metric arrays.

Header byte order is decoded, but foreign metric arrays deliberately remain
unswapped. Native tested the already normalized header marker before attempting
the payload swap, so changing the array values would silently change defined
behavior. The new C loader retains that quirk explicitly. Dedicated foreign-font
retail rendering parity is not established by the byte comparisons.

Authoring keeps the last 'i' entry for zero-width-space metric substitution,
creates one zero-metric slot for empty input and retains the ignored native name
parameter behavior. Partial chunks and adopted edges remain on failure; success
publishes the chunk after array cleanup, and failure invalidates output after
cleanup. Glyph lookup keeps its unfiltered child-type search and missing-glyph
diagnostic. Two explicit corrections are recorded: a fetched model is released
when adoption fails (native leaked it), and negative/unrepresentable character
counts, child IDs or cache-size calculations fail before overflowing native
32-bit calculations. No equivalence is claimed for the former leak/overflow.

`tmp/c89-font-resource-before.bin` and `tmp/c89-font-resource-after.bin` match
all 210,880 bytes without masking, SHA-256
`AF77997A6F241C1B46F1303490BBC17129AFA8FF09A5E67B7E8A77050F293998`.
There are 1,248 native cases covering zero/one/four/128 character slots,
native/opposite byte order, packed data, short/oversized/count-mismatched blocks,
factory and direct partial loading, sparse/empty authoring, zero-width-space
substitution, missing models and BASE/HQ/Pv allocation injection. Debug outcomes
are 385 successful loads and 130 authored fonts; SDL release is 432/144.
All successful wire bytes are recorded. Failed authoring records chunk sizes,
references and adopted edges, excluding its undefined/unwritten payload bytes.
Baseline cleanup explicitly removed the leaked model reference; converted checks
require no extra reference and balanced allocation counts. Additional native/SDL
checks reload authored fonts, verify metrics and resolve actual glyph children.

Independent C tests simulate both header byte orders, retain foreign metric
bytes, cover every read/allocation/factory/authoring failure phase, zero-length
arrays, repeated disposal, output invalidation/publication order, partial graphs,
last-'i' selection, optional output pointers, unfiltered
glyph lookup and overflow rejection. All 69 standalone C checks pass with
assertions enabled, including strict C89 syntax. MSVC AddressSanitizer, 83 focused
native checks, SDL save/load round-trip, 55 snapshot comparisons and 51 freshly
linked CLI baselines pass. Both hosts build and headless real-content scene/
timeline playback passes. No Marcus UI tests were added. Evidence includes
`tmp/c89-font-resource-standalone.log`,
`tmp/c89-font-resource-native-tests.log`, `tmp/c89-font-resource-asan.log`,
`tmp/c89-font-resource-sdl-after.log`,
`tmp/c89-font-resource-snapshot-comparison.json`,
`tmp/c89-font-resource-output-comparison.json` and
`tmp/c89-font-resource-preview-scene.log`.

Playback binary SHA-256 values remained frozen:
`FA614C776F18490B7761671192630973A56F729862ED31C79CBBDAF22793B920` (SDL) and
`70C0257E5C6FAAC4B116916CAEC07DA65F91540EAD47F2FD4D130F61208368E4`
(Marcus regression). Same-player baselines are actor-audio
`20261113T140719Z-a25a00ea`, actor-events `20261113T140843Z-213f0b98`,
scrolling-text `20261113T141027Z-acafcbc6` and sampled-audio
`20261113T141333Z-2f24d32a`.

| Suite | Case | Captures |
| --- | --- | --- |
| `20261113T143605Z-6a799a7e` | actor-audio | six retail/SDL/Marcus seek/playback complete |
| `20261113T143738Z-c5c2a9b2` | actor-events | six retail/SDL/Marcus seek/playback complete |
| `20261113T143955Z-8fcfe9aa` | scrolling-text | six retail/SDL/Marcus seek/playback complete |
| `20261113T144359Z-bf5ff1b1` | sampled-audio | three retail/SDL/Marcus playback complete |

All 21 captures completed without retries. Movie identities and source/resource/
content integrity pass. All 540 seek images, authored positions and ordered
playback changes match preceding same-player results. Only adjacent identical
position/image observations are collapsed; raw observations/timestamps remain.
Actor cases retain 60 ordered changes per player; scrolling retains 328 retail/SDL
and 321 Marcus. First-scene actor 600/1300 Hz coverage passes.

Actor peaks are 3192 retail and 4081 SDL/Marcus. SDL changed from 4080 to 4081
(delta +1), the previously documented 4080/4081 variation. The audit retains this
delta and `audio_peaks_equal: false`; no exact audio match is claimed. Scene-audio
peaks remain 2380 retail and 2176 SDL/Marcus; all other measured peak deltas are
zero. Peaks/coverage do not prove waveform/timing parity. Existing cross-player
differences and one-repeat inconclusive comparisons remain.

The sole default shared SDL package cache was reused, with private processes,
settings and working movies, and uncontrolled OS caching. No builds or compaction
ran during capture. Automatic retention pruned superseded raw media while
preserving observations, summaries, diagnostics, current results and latest
baselines/failures. Evidence is in
`tmp/c89-font-resource-playback-actor-audio-actor-events-scrolling-text-sampled-audio.json`
and `tmp/c89-font-resource-playback-audit.json`.

## ThreeDText generation and animation C89 conversion (13 November 2026)

`text_generation.c` owns parent/set/costume construction, atomic generated-list/
material replacement, cel/matrix/action generation, the action cache, glyph fetch
control, body adjustment/default costumes and generated-resource disposal.
`ThreeMMTextGenerated` replaces the separate C++ material/action/cache-ID owner
fields. Native services provide primitive containers, retained font/metric/glyph
access, material/action factories, body operations and template field views.
String/tag construction, top-level change/save/load orchestration, localized
action names, remaining resources/formats/runtime and final facade removal keep
the full C89 backend unfinished.

`text_generation_math.c` implements all ten shapes and sixteen action formulas
using shared C fixed-point/BAM/matrix primitives, without native BRender linkage.
It retains the exact Pi/radian conversion constants, downward negative halves,
exclusive upper clamps, pre/post transform order and zero-angle rotations (whose
cosine is 65534 rather than 65536). Stretch adjusts position/total width before
shape application; Circle-Z applies its final offset after action transforms.
Cels retain sound ID zero, step 65536 for walk and 327680 otherwise. Matrices are
48 bytes; part model/matrix indices remain signed shorts. Native bounded string,
index and scalar-sum preconditions apply. Invalid cel-count queries return zero;
out-of-range action inputs were not a valid native domain.

Initialization acquires all replacements before publishing fields; failure
unwinds locals in native order without changing the old owner. Success replaces
parent/set/costume/material owners in order, then releases the cached action and
publishes counts/flags. Reused materials retain before their old owner releases.
Cache misses release the old action before building and leave NULL/id -1 on
failure. Hits retain without rebuilding; cache ID publishes after retain. Disposal
releases material then action, leaving the old ID as native destruction did.
Retained font providers remain external services and glyph lookup preserves
unsigned-byte character indexing. Existing native change orchestration exercises
the C initializer's failure atomicity and its string/tag rollback behavior.

`tmp/c89-text-generation-before.bin` and `tmp/c89-text-generation-after.bin`
match all 4,378,609 bytes without masking, SHA-256
`0B4A2F9FAE01D081ADFB7B2FEA37F3A7ADF5F4E2AF15728745E397B35404AA8F`.
There are 800 complete generation cases (five strings × ten shapes × sixteen
actions), including empty-string normalization, high byte characters, fractional
metrics and every generated cel/part/matrix. Four bound body poses per action
exercise first/middle/last/negative-wrap application and cached-action identity/
reference counts. A further 1,008 cases cover construction, change rollback/body
adjustment, cache misses, direct matrix/cel builders, missing fonts and BASE/HQ/Pv
allocation injection. Debug has 678 successful fault-test operations; SDL release
has 720. Allocation counts return to their starting values after cleanup. These
native comparisons do not claim dedicated retail captures for every shape/action.

C-only tests cover all initialization/action-build failure phases, partial
cleanup, existing-material retain/release order, unchanged owners on failure,
cache invalidation/hit behavior and cache-ID visibility at retain/release, missing fonts/
glyphs, empty lists, body boundaries, cel layouts and math invariants. All 70
standalone C checks pass with assertions enabled, including strict C89 syntax.
MSVC AddressSanitizer compiles the generation/math code and its shared arithmetic
dependencies as C. All 84 focused native checks, SDL save/load round-trip,
56 snapshot comparisons and 51 freshly linked CLI baselines pass. Both hosts
build and headless real-content scene/timeline playback passes. No Marcus UI
tests were added. Evidence includes `tmp/c89-text-generation-standalone.log`,
`tmp/c89-text-generation-native-tests.log`, `tmp/c89-text-generation-asan.log`,
`tmp/c89-text-generation-sdl-after.log`,
`tmp/c89-text-generation-snapshot-comparison.json`,
`tmp/c89-text-generation-output-comparison.json` and
`tmp/c89-text-generation-preview-scene.log`.

Playback binary SHA-256 values remained frozen:
`28EFF3FE1220F05F026B0489B061650F6250E0C9CE596E40037CA15574978126` (SDL) and
`CE6179DFE786AA5A9F2C0D9370F7A28790E6E015111F3EA0FCD356AB5DD585B0`
(Marcus regression). Same-player baselines are actor-audio
`20261113T143605Z-6a799a7e`, actor-events `20261113T143738Z-c5c2a9b2`,
scrolling-text `20261113T143955Z-8fcfe9aa` and sampled-audio
`20261113T144359Z-bf5ff1b1`.

| Suite | Case | Captures |
| --- | --- | --- |
| `20261113T153100Z-435bbe3f` | actor-audio | six retail/SDL/Marcus seek/playback complete |
| `20261113T153402Z-b5c56cdd` | actor-events | six retail/SDL/Marcus seek/playback complete |
| `20261113T153805Z-808f45a7` | scrolling-text | six retail/SDL/Marcus seek/playback complete |
| `20261113T154115Z-2320e37b` | sampled-audio | three retail/SDL/Marcus playback complete |

All 21 captures completed without retries. Movie identities and source/resource/
content integrity pass. All 540 seek images, authored positions and ordered
playback changes match preceding same-player results. Only adjacent identical
position/image observations are collapsed; raw observations/timestamps remain.
Actor cases retain 60 ordered changes per player; scrolling retains 328 retail/SDL
and 321 Marcus. First-scene actor 600/1300 Hz coverage passes. Actor peaks remain
3192 retail and 4081 SDL/Marcus; scene-audio peaks remain 2380 retail and 2176
SDL/Marcus. All measured peak deltas are zero. Previously documented 4080/4081
SDL variation remains a coverage limitation, and peaks/tone coverage do not prove
waveform/timing parity. Existing cross-player differences and one-repeat
inconclusive comparisons remain.

Captures reused the sole default shared SDL package cache, with private processes,
settings and working movies, and uncontrolled OS caching. No builds or compaction
ran during capture. Automatic retention pruned superseded raw media, preserving
observations, summaries, diagnostics, current results and latest baselines/
failures. Evidence is in
`tmp/c89-text-generation-playback-actor-audio-actor-events-scrolling-text-sampled-audio.json`
and `tmp/c89-text-generation-playback-audit.json`.

## ThreeDText lifecycle and resource C89 conversion (13 November 2026)

`text_resource.c` owns construction, duplication, optional text/shape/font changes
and rollback, info queries, provider selection, template/child read/write, tag
enumeration and action-name policy. Persistent shape/font state now resides in
`ThreeMMTextDescriptor`; the optional names owner uses `ThreeMMTextNames`.
ThreeDText supplies primitive object/String/block/chunk/provider/table services.
The C controller calls the shared C generated-list initializer directly. Remaining
String/file/container/render adapters, sound/resource/runtime operations and final
facade removal keep the full C-only backend unfinished.

Empty text becomes a space for construction/change, but not loading. Even a
no-op change rebuilds owners and invalidates its action cache. Failure restores
text, shape and font after generated-list failure leaves old owners untouched.
Duplication inherits provider hooks only after successful fresh construction.
Custom-provider failure does not fall back to the default provider. Runtime tags
preserve borrowed cache bits, including positive-SID tags; on-file tags discard
the pointer pad and load a NULL cache. The 24-byte record permits trailing bytes
and retains native opposite-order handling. Direct load failure retains fields
already published. Save failures retain the parent/output number; payload-write
failure deletes only its child. Tag error publication precedes list allocation
and failed-list release. Names publish before retain; first matching ID wins,
absent tables clear silently and missing entries warn before clearing.

The native baseline was captured twice before production changes. Both traces
and the converted trace match all 279,277 bytes without masking, SHA-256
`6C80CF373A4F7F8B6B749EF4CDA6BD7B92EAC799E58F882F9D96B64794EC9E94`.
The 2,400 cases include 1,440 lifecycle cases, 864 direct/factory/tag loading
cases and 96 writer cases. Lifecycle coverage includes empty/high-byte text,
shape extremes, every optional-change mask, BASE/HQ/Pv allocation injection,
rollback, cached-action references and duplicate provider inheritance. Loading
covers current/foreign order, short/missing child chunks, missing names and
packed extended records. Writer traces include successful wire bytes and failed
chunk graphs/output publication; undefined partial-save payloads are excluded.
Debug has 1,843 successful operations; SDL release has 2,016. Allocations balance.
Additional name checks cover absent tables, duplicate IDs and missing entries.
Native generation/body traces from the preceding conversion also remain exact.

All 71 standalone C checks pass with assertions enabled, including strict C89
syntax. The new C-only tests additionally cover both host byte orders,
error-before-release, parent retention, malformed-order diagnostics, no-op cache
invalidation, provider selection and names publication. MSVC AddressSanitizer
compiles/runs the resource controller and shared generation/math dependencies as
C. All 85 focused native checks, SDL save/load, all 57 snapshot comparisons and
51 freshly linked CLI baselines pass. Both hosts build and headless real-content
scene/timeline playback passes. No Marcus UI tests were added. Evidence includes
`tmp/c89-text-resource-standalone.log`, `tmp/c89-text-resource-native-tests.log`,
`tmp/c89-text-resource-asan.log`, `tmp/c89-text-resource-sdl-after.log`,
`tmp/c89-text-resource-snapshot-comparison.json`,
`tmp/c89-text-resource-output-comparison.json` and
`tmp/c89-text-resource-preview-scene.log`.

Playback binary SHA-256 values are frozen for the capture pass:
`48835F5B39C629181B4521155C913C26DFFF3085B350DA5DD4B924E613065DD4` (SDL) and
`59A0D2BDF902366DED0D4B8E49A9F1BBA5E42515C670154BF1B3BAA1F302B4D0`
(Marcus regression). Preceding same-player baselines are actor-audio
`20261113T153100Z-435bbe3f`, actor-events `20261113T153402Z-b5c56cdd`,
scrolling-text `20261113T153805Z-808f45a7` and sampled-audio
`20261113T154115Z-2320e37b`.

| Suite | Case | Captures |
| --- | --- | --- |
| `20261113T160754Z-ff148af2` | actor-audio | six retail/SDL/Marcus seek/playback complete |
| `20261113T160914Z-bee4ee37` | actor-events | six retail/SDL/Marcus seek/playback complete |
| `20261113T161033Z-b02176d7` | scrolling-text | six retail/SDL/Marcus seek/playback complete |
| `20261113T161343Z-60647ee2` | sampled-audio | three retail/SDL/Marcus playback complete |

All 21 captures completed without retries. Movie identities and source/resource/
content integrity pass. All 540 seek images, authored positions and ordered
playback changes match preceding same-player results. Only adjacent identical
position/image observations are collapsed; raw observations/timestamps remain.
Actor cases retain 60 ordered changes per player; scrolling retains 328 retail/SDL
and 321 Marcus. First-scene actor 600/1300 Hz coverage passes. Actor peaks remain
3192 retail and 4081 SDL/Marcus; scene-audio peaks remain 2380 retail and 2176
SDL/Marcus. All measured peak deltas are zero. Earlier 4080/4081 SDL variation
remains a coverage limitation, and peaks/tone coverage do not prove waveform or
timing parity. Existing cross-player differences and one-repeat inconclusive
comparisons remain. These suites do not exercise every shape/action in retail;
the 800-case native generation comparison remains separate evidence.

Both binary hashes remained unchanged throughout capture. The runs reused one
shared SDL package cache, with private processes/settings/working movies and
uncontrolled OS caching. No builds or compaction ran during capture. Automatic
retention pruned superseded raw media while retaining metadata/observations,
current results, preceding baselines and latest failures. C: retained about
59.2 GiB free after the pass. Evidence is in
`tmp/c89-text-resource-playback-actor-audio-actor-events-scrolling-text-sampled-audio.json`
and `tmp/c89-text-resource-playback-audit.json`.
