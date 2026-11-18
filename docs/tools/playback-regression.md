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
  --capture-environment 'Stable GPU/driver, audio endpoint, MIDI bank and fonts identity' `
  --environment-notes 'Optional revision or experiment notes'
```

By default every case has separate **absolute-seek** and **normal-speed playback**
passes, each with two independent fresh-process repetitions. Matching retained
captures are reused by default; `--force-capture` requests new processes. Seek inspection never precedes
Play in the playback process. `--case NAME` is repeatable; `--players`, `--modes`
and `--preparations` accept comma-separated selections. `--repeat 1` is useful
for a smoke run but does not establish original repeatability.
When including Marcus, supply
`--marcus build/marcus-x64/src/marcus/marcus-regression.exe` as well.

Completed captures are looked up per case/player/mode/preparation/repetition
before any player starts. Keys contain movie and ISO hashes, the relevant player
executable/DLLs and content, capture adapters/helpers, capture/validation behavior,
viewport, playback/replay time limit, Python/Pillow and OS/machine identities,
and `--capture-environment`. Rebuilding SDL or Marcus does not invalidate retail.
Source revision, descriptive `--environment-notes`, selected player order,
comparison tolerances and the requested number of repetitions are excluded.
Increasing `--repeat` captures only the missing independent repetition indexes.

`CACHE HIT` names the original run and suite. A new suite copies its SQLite
observations and retains the original media paths without copying PNG/WAV files;
comparisons are recomputed with the current policy. Reused timing/audio are
historical observations, not a new repeatability or performance measurement.
`CACHE MISS` captures only that missing or invalidated run. `--cache-only`
reports misses and exits 2 without launching any player; it also skips automatic
pruning. `--force-capture` bypasses lookup. These two options are mutually exclusive.

Lookup verifies completion, full ordered frame coverage, seek-image coverage,
media existence and every image/audio artifact hash. Incomplete, changed or
pruned captures miss. Older runs without a recorded full cache identity remain
available for manual review but are not guessed into the automatic cache.
Use `--cache-only` to inspect eligibility without starting 3DMM. Changing fonts,
audio endpoints, drivers or MIDI banks requires an updated stable
`--capture-environment` tag or a forced capture. Descriptive notes do not substitute
for that tag. Captures whose binary/tool inputs change during the suite are
marked invalid and cannot be reused.

SDL package data is shared by default under `OUTPUT/sdl-cache`, avoiding a
fresh extracted copy for every player/mode/repetition. `--sdl-cache PATH` selects
another shared cache. Processes, settings, registry isolation and working movie
files remain private. The path is recorded as `sdl_package_cache`; this is warm
package-cache coverage, with OS file caching uncontrolled. The existing C89 warm
cache was moved into `tmp/c89-movie-data-regression/sdl-cache` without copying it.

Raw capture retention defaults to the latest two completed runs for each
case/player/mode/preparation/repetition key, plus the latest failure for each key,
active runs, the latest suite and explicitly pinned suites. A suite remains while
any of its runs is needed. Retained cache aliases also protect their original
media-owning suites. Cleanup runs after a suite, once reuse dependencies have been
recorded; it refuses to
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

Capture also requires 4096 MiB free before ISO extraction when needed and each
uncached player run (`--min-free-mib` adjusts this). On 13 November, superseded captures had
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
MMRollCallActor; wire extras remain 28 bytes. Storage mutation/destruction
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

`MMBodyCostume` owns persistent snapshot storage and references in C.
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
one C-owned `MMActionDefinition`. Resource classes, native container/TAG
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
now wraps `MMFontResource` rather than separate C++ count/maximum/array
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
`MMTextGenerated` replaces the separate C++ material/action/cache-ID owner
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
`MMTextDescriptor`; the optional names owner uses `MMTextNames`.
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

## Movie sound resource and dispatch C89 conversion (13 November 2026)

`sound_resource.c` owns MovieSoundMSND resource loading/factories, optional metadata
queries, MIDI/WAVE envelope and child writing, MIDI import orchestration,
invalidation, tool priorities/classes and sound playback dispatch. Persistent
`MMSoundResource` stores the weak source cache, child identity, style and
volume. Separate C silent/invalid slots preserve the native String placement and
328-byte x64 owner/cache estimate without packing pointers. Both hosts use this
controller through the MovieSoundMSND facade. Primitive String/object/block/
chunk/file services, MIDI parsing/serialization and sound-device operations
remain host adapters. Physical WAVE import/transcoding and sound payload
factories/parsing remain native work; this does not finish the full C89 backend.
Work pauses at the user's request after this slice.

The original foreign-envelope behavior is preserved: the reader used the
background byte-order mask, swapping only order/system markers and leaving
style/volume bytes in native order. Invalid sounds retain their previous child
and silent fields; direct failures preserve fields already published. Short
headers now return failure before the original out-of-range 16-byte read/assert.
Factory estimates publish before allocation, and failure invalidates outputs
before releasing a partial object. Cache links remain weak. Writers zero the
three undefined native padding bytes in the 16-byte envelope. Parent-add failure
retains that primitive's output behavior; later failures delete parent and child
while keeping the published output number. Invalidation writes the envelope and
removes the child before setting the in-memory flag. MIDI import preserves
provided/default names, reports bad-sound on parser failure and releases its
parsed stream on every path.

Playback retains signed queue identity, loop classes/repeat limits,
invalid-sound early return, queue/stop behavior and silent-sound stopping. An
active repeated MIDI child is suppressed using type/number and the previous
instance, including the native omission of cache identity from that comparison.
The last-MIDI state publishes after device operations. Device mixing, MIDI
parsing and WAVE transcoding are not claimed to have been converted by this
resource-controller change.

The authoritative native baseline was captured twice using the original native
implementation and a corrected fixture. Both baseline traces and the converted
trace match all 99,001 bytes, SHA-256
`E95D011B6E0F40EDFAF0D5E338DD845C15BD3569E3773B3FB4786E6AAD1CD899`.
The fixture obtains child identity by enumeration and reads its actual payload
with `FFind(type, number)`. The legacy type-relative enumeration payload argument
can refer to the parent's block; the preliminary paused 97,273-byte trace
therefore did not prove child-payload parity and is superseded. Only undefined
MSND padding bytes are excluded from the corrected trace; successful actual
MIDI/WAVE payloads, graph edges, names and metadata are compared.

There are 1,320 attempts: 864 direct/factory/info loads, 192 writer attempts, 192
MIDI import attempts and 72 invalidation attempts. Loads cover current/foreign
markers, invalid and silent sounds, missing resources/names, oversized headers,
explicitly packed 16-byte headers, optional metadata outputs and BASE/HQ/Pv
allocation injection. Debug outcomes are 524 successful loads, 192 writes,
84 MIDI imports and 36 invalidations; SDL release has 540 loads, 192 writes,
96 imports and 36 invalidations. Allocations balance after cleanup. None of the
native writer allocation injections failed for these small preallocated chunks;
writer failure/cleanup phases are covered explicitly by the standalone callback
fault tests rather than inferred from the 192 successful native writes. Native
short-header assertions are excluded from baseline capture; standalone and
converted-host tests explicitly require clean rejection.
Raw WAVE writer cases copy opaque source bytes to test file-copy integrity; the
separate SDL import/decode/play/reopen check supplies valid audio.

C-only tests cover both host byte orders, foreign scalar-byte preservation,
factory output invalidation before release, independent optional metadata
outputs, all writer cleanup phases including file-copy/name failures, MIDI
parser ownership, invalidation ordering and playback policy. A further 96
headless host combinations cover style/loop/queue/silent/invalid/actor flags,
plus priority/class queries, high-bit offsets and repeated-MIDI suppression.
These tests use a recording sound manager and restore the initial MIDI state;
they add no Marcus UI tests or retail MIDI-rendering claim.

All 72 standalone C checks pass with assertions enabled, including strict C89
syntax. MSVC AddressSanitizer compiles/runs the new controller as C. All 86
focused native checks, SDL save/load, the SDL WAVE import/decode/play/reopen test,
all 58 snapshot comparisons and 51 freshly linked CLI baselines pass. Both hosts
build; headless real-content scene/timeline playback passes. Evidence includes
`tmp/c89-sound-resource-standalone.log`, `tmp/c89-sound-resource-native-tests.log`,
`tmp/c89-sound-resource-asan.log`, `tmp/c89-sound-resource-sdl-after.log`,
`tmp/c89-sound-resource-wave-import.log`,
`tmp/c89-sound-resource-snapshot-comparison.json`,
`tmp/c89-sound-resource-output-comparison.json` and
`tmp/c89-sound-resource-preview-scene.log`.

Playback binary SHA-256 values are frozen for the capture pass:
`0D3FAE67646FDFA903FE9A25EEC0C0DD75C86EA98E371E29777AF68196160AD0` (SDL) and
`A45E2864A267FDE4CC15B743831D8392D0A632102E24AFD2E0D479C685CBC8B3`
(Marcus regression). Preceding same-player baselines are actor-audio
`20261113T160754Z-ff148af2`, actor-events `20261113T160914Z-bee4ee37`,
scrolling-text `20261113T161033Z-b02176d7` and sampled-audio
`20261113T161343Z-60647ee2`.

| Suite | Case | Captures |
| --- | --- | --- |
| `20261113T164426Z-3595489b` | actor-audio | six retail/SDL/Marcus seek/playback complete |
| `20261113T164613Z-1cfd43da` | actor-events | six retail/SDL/Marcus seek/playback complete |
| `20261113T164741Z-85298c8e` | scrolling-text | six retail/SDL/Marcus seek/playback complete |
| `20261113T165153Z-af14778b` | sampled-audio | three retail/SDL/Marcus playback complete |

All 21 captures completed without retries. Movie identities and source/resource/
content integrity pass. All 540 seek images, authored positions and ordered
playback changes match preceding same-player results. Only adjacent identical
position/image observations are collapsed; raw observations/timestamps remain.
Actor cases retain 60 ordered changes per player; scrolling retains 328 retail/SDL
and 321 Marcus. First-scene actor 600/1300 Hz coverage passes. Actor peaks remain
3192 retail and 4081 SDL/Marcus; scene-audio peaks remain 2380 retail and 2176
SDL/Marcus. All measured peak deltas are zero. Earlier 4080/4081 SDL variation
remains a limitation; peaks/tone coverage do not establish waveform or timing
parity. Existing cross-player differences and one-repeat inconclusive comparisons
remain. Dedicated retail MIDI rendering is outside this pass; headless dispatch
and parser/import checks provide separate, narrower evidence.

Both binary hashes remained unchanged throughout capture. Runs reused the sole
shared SDL package cache with private processes/settings/working movies and
uncontrolled OS caching. No builds or compaction ran during capture. Automatic
retention pruned superseded raw media while retaining metadata/observations,
current results, preceding baselines and latest failures. C: retained about
58.9 GiB free. Evidence is in
`tmp/c89-sound-resource-playback-actor-audio-actor-events-scrolling-text-sampled-audio.json`
and `tmp/c89-sound-resource-playback-audit.json`.


## Sound payload and physical WAVE import C89 conversion (13 November 2026)

The resumed sound work moves MIDI parsing/SMF import, sampled/MIDI payload
factory control, RIFF parsing and codec-selection rules, PCM/MS ADPCM encoding,
WAVE import selection and physical import transactions into `libraries/engine`.
Both hosts use these shared modules through temporary native facades. External
SDL resampling/decoding, GSM/MP3 codecs, AudioMan/ACM operations and physical
file/device APIs remain host adapters. Sound queues and scene/movie runtime
integration belong to later slices; the whole C89 backend remains incomplete.

A native trace was frozen before conversion: 160 SMF imports spanning formats
0/1/2/other, five divisions including SMPTE/drop-frame, running status, ignored
meta/SysEx, tempo changes, EOF/EOT and parser peeks; plus 80 PCM/ADPCM pairs
across short/partial/multiple blocks, two rates and five sample patterns.
All 314,180 bytes match after conversion, SHA-256
`C98FC38D594FD6DA2B36C312CBB888004F6AB6457A3BFD02ABAFA5C19149644A`.
The committed host test also checks FNV-1a `9323C635` on little-endian hosts.

The C-only tests cover parser exhaustion, wrapping timestamps, truncated SMF
and RIFF payloads, invalid divisions/chunk lengths, PCM/ADPCM wire fields,
output preservation, allocation failures, converted-sample release on failure,
factory estimates/publication/ownership and every fallible native import phase,
including each RIFF write. The native import mock checks the historical
payload-size comparison using an 88-byte physical file with an 80-byte RIFF
payload against an 85-byte original. These are controller checks, not proof of
a working AudioMan decoder on x64: that host still has its pre-existing
nonworking AudioMan stub. SDL/Windows ACM compatibility tests exercise real
encoded streams separately. Unsupported MIDI timing arithmetic overflow is
rejected instead of retaining the former assert/undefined-overflow path.

All 73 standalone C checks (including strict syntax), the MSVC AddressSanitizer
C build/run, five SDL release checks (payload trace, WAVE import, compressed
fixtures, movie sound import and movie save/load) pass. The 1,320-case resource
fixture retains its load/write/invalidation counts and balanced allocations;
debug MIDI import successes change from 84 to 90 because native parser/list
allocation points were removed. This expected injection-topology change does
not change successful payload bytes. A concurrent host-test pass encountered
a temporary-file creation collision; its focused serial rerun passed. The
final focused native pass runs serially and all 84 checks pass. The broad `core-checks` build is
blocked by the pre-existing `tests/mm_fixture.cpp:47` call to `SetSzs` on
`MMMovieTitle`; the unrelated fixture was left unchanged.

Both hosts build. Capture binaries were frozen at SHA-256
`5BC0EFC9F30FFFF79387561CED4B6B766C4BDFCCD5EE067D21CA013E1AECD38C`
(SDL release) and
`A656360CFB94A2A653AF8023A62D76E7A104467560FA32DB5A071A110C56766D`
(Marcus regression), with the same hashes checked after capture.

| Suite | Case | Captures |
| --- | --- | --- |
| `20261113T223634Z-4cfdde6d` | actor-audio | six retail/SDL/Marcus seek/playback complete |
| `20261113T223821Z-bec0f1d8` | sampled-audio | three retail/SDL/Marcus playback complete |

All nine captures complete without retries; movie/content/resource identities
remain unchanged. Against the preceding resource-controller suites
`20261113T164426Z-3595489b` and `20261113T165153Z-af14778b`, all 180 seek images,
authored frame observations and ordered playback image changes match for each
player. Only adjacent identical image/position observations are collapsed for
playback comparison. Actor peaks remain 3192 retail and 4081 SDL/Marcus; scene
sampled-audio peaks remain 2380 retail and 2176 SDL/Marcus. The actor first-scene
600/1300 Hz contract passes for all players. These narrower checks show no new
conversion regression; they do not establish waveform/timing parity. Both
suite reports retain pre-existing cross-player timing/PCM differences and
single-repeat inconclusive comparisons. Dedicated retail MIDI rendering was
not captured; the frozen native parser/import trace and mixer tests cover a
separate, narrower contract. No Marcus UI tests were added.

Evidence: `tmp/c89-sound-payload-standalone.log`,
`tmp/c89-sound-payload-asan.log`, `tmp/c89-sound-payload-native-tests.log`,
`tmp/c89-sound-payload-final-focused.log`,
`tmp/c89-sound-payload-release-tests.log`,
`tmp/c89-sound-payload-playback-actor-audio-sampled-audio.json` and the two
`tmp/c89-sound-payload-capture-*.log` files. Captures reused the shared SDL
package cache with private processes/settings/working movies; OS caching was
uncontrolled. No concurrent builds or tests ran during capture. Existing
retention pruned superseded raw media while preserving metadata and the
current results.


## Body geometry and render-control C89 conversion (14 November 2026)

`body_geometry.c` now owns the Body's persistent bounds/last-visible/hit state,
render-bound unions and prepare callbacks, highlight exclusion/restoration,
world-bounds transforms, rectangle/center/position queries and picking/search
policy. `BodyWorld` uses an explicit per-query C pick context rather than the
three former process-wide closest-hit fields. Native Body/World facades supply
actor fields and synchronous BRender traversal; BRender bounds primitives and
remaining model/material/actor-tree representations remain adapters/dependencies.
This completes a geometry/control slice, not the full C89 engine migration.

Before conversion, a real BRender fixture froze 144 cases: 1/3/6-part bodies,
six root transforms (identity, translation, reflection/scale, fractional shear,
rotation and translations outside +/-10000), highlighting on/off and four
initial rectangle states. It records local/world bounds, highlight matrices,
five render unions, centers/positions, visible/last-visible rectangles,
actor-to-body/set mapping, highlight/root/part search and closest-hit order.
All 76,608 bytes match after conversion, SHA-256
`0EA33C3F9F3B7D6F22B857B6FBAB336B0EDA558DF19F1FD4F22B1C79AD519327`.
The committed fixture checks FNV-1a `ECFB9185` on little-endian hosts. The prior
body-edit (1,880,872 bytes) and body-runtime (427,368 bytes) traces also match
exactly after moving their private-state fixture bridge to the C record.

C-only tests cover initialization, empty/inverted rectangles, unions and
last-visible fallback, wrapped center sums (including INT32_MIN),
C89-defined truncation of negative halves, highlight-off/query/restore order,
local/world bounds, in-place transforms, +/-10000 extrema, negative depths,
equal-depth first-hit retention, visibility filtering, independent pick contexts,
orphan actors and unchanged optional outputs after a miss. Search retains
highlight-found-without-stop and root-with-set--1 behavior. The C implementation
uses existing two-word/full-product fixed arithmetic and has no native SDK,
BRender or C++ dependency. MSVC AddressSanitizer compiles and runs the new tests
and their arithmetic dependencies as C.

All 74 standalone C checks (including strict C89 syntax), 88 focused native
checks, two SDL release checks (movie save/load and engine-core link), and all
51 freshly linked CLI baselines pass. The full native `core-checks` target now
builds: the stale `mm_fixture.cpp` scene-name call identified in the sound
slice was updated to `SceneData::SetName`, preserving its exact fixture string.
The new body fixture and earlier body fixtures run inside the existing headless
save/load test, including SDL release; no Marcus UI tests were added.

Both hosts build. Frozen capture SHA-256 values are
`085C5CC495D1BB3E4AEBEAC23F5F1D08A68A9EA7DCAF3FBD4030AEA90F6A0B64`
(SDL release) and
`C21F6427480B531AA1F56C679211BDADDE5B488E71E7E002CF825A01D0CF30F0`
(Marcus regression). Both hashes remain unchanged after capture.

| Suite | Case | Captures |
| --- | --- | --- |
| `20261114T000009Z-ca2a61d8` | actor-events | six retail/SDL/Marcus seek/playback complete |
| `20261114T000122Z-b4ba634c` | actor-audio | six retail/SDL/Marcus seek/playback complete |

All twelve captures complete without retries and retain movie/resource/content
integrity. Against `20261113T164613Z-1cfd43da` (actor-events) and
`20261113T223634Z-4cfdde6d` (actor-audio), all 360 seek images, authored frame
observations and ordered playback image changes match for every player.
Only adjacent identical image/position observations are collapsed for playback
comparison. The cases include actors, costumes, props, 3D text and routes;
actor-events also tags rotation/scale/stretch/freeze/entrance-exit coverage.
This does not prove every geometry or pointer-coordinate/UI path against retail.

First-scene 600/1300 Hz actor tone coverage passes. Actor peaks remain 3192
retail and 4081 SDL/Marcus. Earlier 4080/4081 SDL variation remains a coverage
limit; peaks/tone checks do not establish exact PCM/timing parity.
Cross-player timing/PCM differences and single-repeat inconclusive comparisons
remain in both suite reports. These narrower checks show no new geometry
regression, not complete retail playback equivalence.

Evidence includes `tmp/c89-body-geometry-before.bin`,
`tmp/c89-body-geometry-after.bin`, `tmp/c89-body-geometry-release.bin`,
`tmp/c89-body-geometry-standalone.log`,
`tmp/c89-body-geometry-asan.log`, `tmp/c89-body-geometry-native-build.log`,
`tmp/c89-body-geometry-native-tests.log`, `tmp/c89-body-geometry-sdl-tests.log`,
`tmp/c89-body-geometry-output-comparison.json` and
`tmp/c89-body-geometry-playback-actor-events-actor-audio.json`. Captures reuse the
shared SDL package cache with private processes/settings/working movies and
uncontrolled OS caching. No builds or tests run concurrently with capture.
Retention prunes superseded raw media while keeping metadata/current results.


### C89 sound runtime (14 November 2026)

The shared sound runtime now delegates manager dispatch, class filtering,
activation/suspend nesting, queue selection, instance/class pause/stop/resume,
completed-prefix cleanup and volume scaling to C89. The sampled mixer owns
queue/voice state in C, including transactional updates, offsets, loops, groups,
clipping and counters. Streamed MIDI construction/seeking, queue progression,
priority/tie selection, preemption, expiry, outstanding-buffer control and
frame-clock rendering also use C89. Unsigned two-word arithmetic preserves
64-bit positions, counters, delay products and fractional carry without C99.

Mutexes, worker/event primitives, shared sample references, native collection
facades, physical devices and SoundFont/SDL/AudioMan/ACM codecs remain host
adapters. In particular the optional classic direct-WinMM/AudioMan device paths
remain native device implementations; this is completion of the shared sound
runtime policy slice, not removal of every C++ sound file or completion of
final C-only host integration. The separate in-progress model-resource edits
were preserved and are included in the capture working tree.

Three pre-conversion native fingerprints are frozen in executable checks:

- `audio-runtime-trace`: 4,000 sampled mixer updates, PCM and statistics,
  FNV-1a `D1FA52C3`.
- `midi-runtime-trace`: deterministic synth double, scheduling, messages,
  offsets/repeats, gains, completion tags and statistics, `28316524`.
- `midi-mixer`: 2,200 cached MIDI events with every seek position, including
  many state-event batches, `6B4B5C94`.

The tests enforce their native little-endian/float/word-width conditions.
The stream baseline was captured with the original cached-stream/seek
implementation restored temporarily, then compared with the converted code.
These traces complement the existing real SoundFont and asynchronous device
checks; the synth double does not stand in for those real codec tests.

All 76 standalone C checks pass, including strict C89 syntax. The C-only audio
runtime test and its MSVC AddressSanitizer build/run cover arithmetic carry and
wrap, allocation failures, transactional PCM updates, groups, stopped entries,
nested pauses, manager references and queue reuse, MIDI delays/completions and
large seek batches with write failures. All 94 focused native checks pass,
including movie save/load, resource fixtures, Marcus playback/player and
asynchronous MIDI arbitration. All thirteen focused SDL release checks, including the SDL MIDI-device
check, pass. The real synth test also passes all 128 GM
programs, 47 percussion notes, controls and fractional-frame timing. No Marcus
UI tests were added.

The final capture executables were frozen at SHA-256
`3E8D8AFCFD21AAC99778C17BAB1179111B51A68E0C021A517EBDE88256D69DC9`
(SDL release) and
`BD153780ABE4AC60370118D1F6AC48D727E05D69DCA16B10C49591D020489B53`
(Marcus regression). The hashes were unchanged after capture.

| Suite | Case | Captures |
| --- | --- | --- |
| `20261114T013743Z-e5691581` | actor-audio | six retail/SDL/Marcus seek/playback complete |
| `20261114T013857Z-fd2a4ebe` | sampled-audio | three retail/SDL/Marcus playback complete |

All nine captures complete without retries and preserve movie identity and
suite integrity. Against `20261114T000122Z-b4ba634c` (actor audio) and
`20261113T223821Z-bec0f1d8` (sampled scene audio), authored frame observations,
all 180 seek images and ordered playback image changes match for each player.
Only adjacent identical position/image observations are collapsed for playback.
Actor PCM16 peaks remain 3192 retail and 4081 SDL/Marcus; sampled-scene peaks
remain 2380 retail and 2176 SDL/Marcus. The first-scene-only 600/1300 Hz actor
contract passes for every player. Captures use the shared warm SDL package
cache with private processes/settings/movies; OS caching is uncontrolled and
no builds or tests run concurrently with capture.

These checks show no observed sound conversion regression. They do not prove
retail waveform or timing parity: reports retain cross-player differences and
single-repeat inconclusive comparisons. Dedicated retail MIDI rendering was
not captured; frozen native scheduling/seek traces and real synth/device tests
cover a narrower MIDI contract.

Evidence: `tmp/c89-sound-runtime-standalone.log`,
`tmp/c89-sound-runtime-asan.log`, `tmp/c89-sound-runtime-native-tests.log`,
`tmp/c89-sound-runtime-sdl-tests.log`,
`tmp/c89-sound-runtime-midi-device-test.log`,
`tmp/c89-midi-stream-before.log`, `tmp/c89-midi-stream-after.log`,
`tmp/c89-sound-runtime-playback-actor-audio-sampled-audio.json` and the two
`tmp/c89-sound-runtime-capture-*.log` files.


### Sound conversion closeout audit (14 November 2026)

The sound resource/dispatch, payload/import and shared runtime conversions are
complete at `66dc67c1`. A fresh source audit found no further shared sound-policy
implementation to convert in this slice. This closeout changes documentation
only. Native object/collection facades, device and codec services, mutexes and
worker threads remain the final host-integration dependencies described above;
the whole C89 backend is still unfinished.

Fresh builds and runs pass all 16 selected native checks and all 24 selected SDL
release checks. Selection uses the CTest expression
`sound|audio|midi|wave|recording|movie-save-load`, covering the frozen mixer/MIDI
traces, real synth/device adapters, payloads, imports, recording, sound-resource
dispatch and save/reopen integration as available in each configuration. The
C-only audio runtime fixture also builds and passes directly with Clang
`-std=c89 -pedantic-errors -Wall -Wextra -Werror`.

The initial concurrent native/SDL runs produced one native save/load access
violation after the font-resource fixture. An isolated rerun, an isolated
debugger run and the subsequent full native selection all pass. Legacy
`Filename::FGetUnique` checks unreserved names in the shared temporary directory,
so concurrent suites can collide; this is a possible cause, not a confirmed
diagnosis. The initial failure is retained in the evidence rather than omitted.

No new retail captures were taken for this documentation closeout. The sound
conversion captures and waveform/timing/MIDI coverage limits above still apply.
The separate in-progress BMP changes were preserved and were present in the
fresh builds. Marcus itself was neither rebuilt nor stopped. No Marcus UI tests
were added.

Evidence: `tmp/c89-sound-finish-native-build.log`,
`tmp/c89-sound-finish-native-tests.log` (initial failure),
`tmp/c89-sound-finish-save-load-recheck.log`,
`tmp/c89-sound-finish-debug.log`,
`tmp/c89-sound-finish-native-serial-tests.log`,
`tmp/c89-sound-finish-sdl-build.log` and
`tmp/c89-sound-finish-sdl-tests.log`.

The sound closeout was reconfirmed on `679f6520`, after the World buffer
conversion: freshly rebuilt native and SDL selections pass all 16 and 24 checks,
respectively, run serially. The standalone audio runtime fixture also passes a
fresh Clang strict C89 build with warnings treated as errors. Evidence:
`tmp/c89-sound-current-native-tests.log` and
`tmp/c89-sound-current-sdl-tests.log`. No production changes or new retail
captures were needed; the host-integration and comparison limits above remain.

The requested sound finish was verified again on `d51b9222`, including the
subsequent World and Scene ownership conversions. Freshly rebuilt native and
SDL release selections pass all 16 and 24 checks, respectively, run serially.
The standalone audio runtime also compiles and passes with Clang
`-std=c89 -pedantic-errors -Wall -Wextra -Werror`, linking its shared integer
helpers from `wide.c`. No additional shared sound-policy conversion was found
necessary. Evidence: `tmp/c89-sound-final-native-build.log`,
`tmp/c89-sound-final-native-tests.log`, `tmp/c89-sound-final-sdl-build.log`,
`tmp/c89-sound-final-sdl-tests.log` and `tmp/c89-sound-final-strict.log`.
This verification adds no retail captures or PCM/timing coverage and does not
complete final host integration. Marcus itself was not rebuilt; no Marcus UI
tests were added.

The sound closeout was checked again against the existing `ad30b426` build,
including the subsequent Scene sound packet, editing/undo and loading/resolution
conversions. All 18 native and 24 SDL release checks selected by
`sound|audio|midi|wave|riff|pcm|adpcm` pass, run serially. Evidence:
`tmp/c89-sound-final-audit-native.log` and
`tmp/c89-sound-final-audit-sdl.log`. These runs use existing binaries; the
uncommitted Background camera changes were neither rebuilt nor tested by this
audit. No additional sound implementation changes, builds or retail captures
were required. Device/codec/thread adapters and final native facade integration
retain the limits above.

### C89 model resources and prelighting (14 November 2026)

`model_resource.c` now owns model creation, cache factories/estimates,
prepared/unprepared loading, header/face codecs, partial ownership, disposal,
wire writing, DAT import orchestration, glyph adjustment and extent queries.
`model_lighting.c` owns prelighting specification, sequencing and temporary
resource cleanup. The native Model class supplies object/block/file and typed
BRender field/allocation services. Material/texture resources and final native
facade removal remain conversion work.

The native comparison matches all 277,360 bytes, SHA-256
`29157E1B47FE4DA9E1B36DF511026715326661B816075D887F4EECAD986F51D3`.
The original capture, repeat and converted trace have the same digest. There
are 504 debug load attempts, 404 successful, over two geometry patterns, nine
wire variants, factory/direct initialization and native BASE/Pv fault injection.
The trace records partial ownership/flags, successful geometry/wire data,
repeated glyph adjustments and one real prelight. The native debug
little-endian test enforces FNV-1a `78C575E5` without masking successful bytes.
DAT import and a second prelight additionally check real SDK behavior and
balanced temporary allocations outside the frozen trace.

Wire headers remain 48 bytes, faces 32 bytes and opaque vertices 32 bytes.
Runtime faces are 36 bytes on x64 because their material pointer is wider;
they are never dumped directly to disk. The material slot and implicit face
padding serialize as zero; explicit face padding remains preserved. Radius
still selects the prepared path. Opposite-order headers normalize as before,
including the legacy consequence that the vertex/face payloads are not swapped.
Trailing data remains accepted. Negative signed wire counts and truncated
ranges now fail before native allocator/read assertions. Glyph math preserves
negative floor halves, wrapped arithmetic and the exact spacing threshold.

The native allocator now passes a nonempty pointer-sized identifier string to
BRender: clearing a larger local buffer still made BrResStrDup allocate only
one byte. Owner pointer storage/readback uses typed copies. Prelighting now
disables all enabled lights and frees its world, material, colour and depth
pixelmaps on every returning path, fixing the original temporary leaks while
retaining the frozen model result. C tests cover lights 1 through 9 and every
creation failure; native debug still diagnoses counts other than one. The
former direction input remains ignored.

The real DAT roundtrip exposed BRender's uninitialized non-count chunk-handler
argument under MSVC runtime checks. `DfChunksInterpret` now initializes it to
zero before conditionally reading counted chunks. Non-count handlers ignore
or overwrite that argument; counted handlers retain their original value.
This same dispatcher fix was applied to both native and converted baselines.

All 76 standalone C checks, including strict syntax, pass in the current tree.
The model C test's MSVC AddressSanitizer run passes. It covers both byte orders,
all truncation lengths, callback/allocation failures, publication order, writer
failures, partial owners, glyph boundaries and lighting cleanup. The existing
94-check focused native gate includes both model tests; after restoring the
converted source from the original-model control build, those two model tests
pass again with the frozen trace. Three SDL release model/save-load/link checks
and all 51 CLI baselines also passed during this model slice. The later sound
runtime checks/captures are recorded separately above. No Marcus UI tests were
added.

The final capture binaries were frozen at SHA-256
`3E8D8AFCFD21AAC99778C17BAB1179111B51A68E0C021A517EBDE88256D69DC9`
(SDL release) and
`6A7DF8F0BC870D5FD5A14A0A924F7CFDF93A661EADE72BCFC4C3D4F76CC6D323`
(Marcus regression). The original-model Marcus control is
`4D14938BA61100702567B0329D8A9D29AA445249AADD65FBAEB3A75800866E60`.
It was built by temporarily restoring HEAD's original `modl.cpp`/`modl.h`,
retaining the current captions, sound runtime and dispatcher fix. Converted
sources were restored byte-for-byte and rebuilt before capture. All three
executable hashes remain unchanged after capture.

| Suite | Case | Captures |
| --- | --- | --- |
| `20261114T014230Z-182704ae` | original-model scrolling-text control | two Marcus seek/playback complete |
| `20261114T014331Z-3c06dc64` | actor-events | six retail/SDL/Marcus seek/playback complete |
| `20261114T014446Z-76bc208b` | scrolling-text | six retail/SDL/Marcus seek/playback complete |

All fourteen captures complete without retries. Actor-events frame observations,
all seek images and ordered playback image changes match each player's
`20261114T000009Z-ca2a61d8` baseline. Retail and SDL scrolling-text retain their
`20261113T164741Z-85298c8e` results. Marcus's older scrolling baseline predates
caption fixes `e81d9238` and `4c39e529` (21:34 and 21:44 UTC on 13 November).
The current original-model control itself reproduces the image difference from
that older baseline while retaining the same authored frames. Converted Marcus
matches the current control in all 60 seek images and all 321 ordered playback
image changes. Thus that existing image difference is not introduced by the
model conversion; the older evidence is retained rather than silently accepted
as a new matching baseline. Only adjacent identical position/image observations
are collapsed for playback comparison.

The final reports retain existing cross-player differences and single-repeat
inconclusive comparisons. The Marcus-only control has no cross-player reference;
its images are compared directly with the converted capture. These checks show
no observed model conversion regression, not full retail timing/PCM parity.
Captures use the shared warm SDL package cache and private processes/settings/
working movies, with uncontrolled OS caching and no concurrent builds or tests.

Evidence: `tmp/c89-model-resource-before.bin`,
`tmp/c89-model-resource-before-repeat.bin`, `tmp/c89-model-resource-after.bin`,
`tmp/c89-model-resource-asan.log`, `tmp/c89-model-resource-native-tests.log`,
`tmp/c89-model-resource-final-focused.log`, `tmp/c89-model-resource-sdl-tests.log`,
`tmp/c89-model-resource-output-comparison.json`,
`tmp/c89-model-resource-playback-actor-events-scrolling-text.json`,
`tmp/c89-model-resource-native-control-capture.log` and the final capture logs.
The current 76/94-check gates are in `tmp/c89-sound-runtime-standalone.log` and
`tmp/c89-sound-runtime-native-tests.log`.


### C89 material and custom-material resources (14 November 2026)

Material construction, cache factories/estimates, header/texture/transform
loading, serialization, shade replacement, disposal and PIX/BMP import control
now use `material_resource.c`. `custom_material.c` owns persistent material/model
reference arrays, counts and body-part set identifiers, construction/retains,
child loading, estimates, queries and disposal. Native adapters retain typed
BRender fields/allocation, object references, DataBlock/file and TextureMap
primitives. TextureMap storage/codecs and final native facade removal remain
conversion work.

The two original native captures and the converted trace match all 102,316
bytes, SHA-256
`F31D5481A64AC6D49CA63F6E5FDD288D4C4C70246F196760B158B1E519775225`.
The debug little-endian test enforces FNV-1a `113D98C1` automatically. It covers
396 material and 192 custom-material load attempts over ordinary/foreign/legacy
zero-order headers, packed data, trailing/short records, textures and native/
foreign/short texture transforms, direct/factory initialization and BASE/Pv
fault injection. Additional cases cover default/explicit constructors, custom
reference counts, write/adoption failures and real PIX import. Successful
material/texture bytes and partial ownership/graph metadata are recorded;
undefined bytes in failed partial texture writes are not recorded.

Wire records remain 20-byte material headers, 8-byte custom headers and 28-byte
transforms. Material loading retains its narrow zero-order Windows exception.
Transform loading swaps every non-native marker before its diagnostic and is
skipped when there is no texture child. Writing retains native behavior,
including omitting transform children and deleting only the parent after a
texture writer fails, which can leave its partial child. An adoption failure
deletes child then parent. Material factory failures invalidate outputs before
release; custom factories release before invalidation. Direct loading retains
partial ownership except the existing local transform-failure cleanup.

Custom slot counts retain the maximum signed child ID across all child types;
missing material/model slots remain empty. Array allocation/retain order,
partial references and cache estimates match native. Sizes that cannot fit
signed allocation/cache arithmetic fail before overflow. The native debug
facade still requires complete material slots when validating successful
objects. The body runtime fixture now writes its model references through the
C-owned array rather than the removed native member.

BRender material identifiers now use a nonempty pointer-sized placeholder:
clearing a larger local string still made BrResStrDup allocate only one byte.
Typed pointer copies replace the aliased owner read. Failed PIX wrapping now
frees the raw pixelmap without first publishing it as a valid TextureMap to
material disposal. The pre-existing lifetime problem for successfully imported
PIX allocations remains in TextureMap and is a dependency of the next texture
conversion. The real PIX fixture explicitly frees that legacy SDK owner after
material disposal; it does not claim the old TextureMap lifetime is repaired.

All 77 standalone C checks, including strict syntax, pass. The C-only material
test and MSVC AddressSanitizer run cover endian codecs, legacy markers, sparse
children, invalid counts, partial owners, callback/allocation failures, factory
ordering, atomic shade replacement, import failures and write/adoption cleanup.
The full `core-checks` target and both hosts build; all 96 focused native checks,
four SDL release checks and 51 freshly linked CLI baselines pass. The final
focused native material/save-load/link rerun passes after restoring the native
debug adapter assertions. No Marcus UI tests were added.

The final capture executables were frozen at SHA-256
`B4C6F327AEB701CDA1E3B8E1026B30E077544965F23AB99FC1FC63F711052431`
(SDL release) and
`A8308902E3F69A6AAC6ACD8BACEE648B6BFF1694F3366CCF4094AF3B6C44420A`
(Marcus regression). Both hashes remain unchanged after capture.

| Suite | Case | Captures |
| --- | --- | --- |
| `20261114T021601Z-e526844f` | actor-events | six retail/SDL/Marcus seek/playback complete |
| `20261114T021714Z-a38f7abf` | scrolling-text | six retail/SDL/Marcus seek/playback complete |

All twelve captures complete without retries and retain movie identity and
suite integrity. Authored frame observations, all 360 seek images and ordered
playback image changes match each player's preceding model-resource suites
`20261114T014331Z-3c06dc64` and `20261114T014446Z-76bc208b`. Only adjacent
identical position/image observations are collapsed for playback comparison.
Both reports retain existing cross-player differences and single-repeat
inconclusive comparisons; these gates show no observed conversion regression,
not complete retail timing/PCM parity. Captures reuse the shared warm SDL
package cache with private processes/settings/working movies, uncontrolled OS
caching and no concurrent builds or tests.

Evidence: `tmp/c89-material-resource-before.bin`,
`tmp/c89-material-resource-before-repeat.bin`, `tmp/c89-material-resource-after.bin`,
`tmp/c89-material-resource-standalone.log`, `tmp/c89-material-resource-asan.log`,
`tmp/c89-material-resource-native-tests.log`,
`tmp/c89-material-resource-final-focused.log`, `tmp/c89-material-resource-sdl-tests.log`,
`tmp/c89-material-resource-output-comparison.json`,
`tmp/c89-material-resource-playback-actor-events-scrolling-text.json` and the two
`tmp/c89-material-resource-capture-*.log` files.


### C89 texture resources and imported PIX ownership (14 November 2026)

`texture_resource.c` owns persistent texture descriptors, pixel buffers and
imported renderer owners, construction, cache factories/estimates, header I/O,
chunk writes and standalone-file signature/packing/failure control.
`texture_import.c` owns bitmap import, palette/cache lifetime and nearest-colour
selection. The native TextureMap retains a BRender pixelmap view plus typed
allocation, file and bitmap-reader adapters. View synchronization preserves
legacy metadata/pixel-pointer edits before size queries, writing and disposal.
The existing `FReadBitmap`/`FWriteBitmap` codecs and final native facade removal
remain conversion dependencies; the uncompiled inverse-table code is unchanged.

Two original native captures and the converted trace match all 22,000 bytes,
SHA-256 `DC154CACA679AD177694778EDCEF74C2256BAD6EF81A23AA6C80DA9CE4C5FB43`.
The debug little-endian test enforces FNV-1a `C5DC711A` automatically. There are
192 native load attempts covering foreign/native headers, packed/trailing data,
pixel types/flags, direct/factory loads and BASE/Pv allocation injection. Further
cases cover pixel constructors, standalone writes, PIX wrapping and real bitmap
imports for widths 3/4/5/8 with no palette mapping or 0/1/2/7/256 destination
colours. The preceding 102,316-byte material trace still matches `113D98C1`.

Twenty-byte headers preserve the native swap-on-any-non-native-marker policy;
pixel payloads remain opaque and unswapped. Bounded reads/writes now reject
truncated spans before native range assertions, while preserving partial-prefix
and partial-chunk writes. Negative serialized row/height counts and unsupported
layouts fail before invalid allocation arithmetic. Packed-size estimates retain
the supplied block's original byte count. Standalone output retains native-order
`kaup`/`kapa` signatures, raw fallback when packing fails, error messages, temporary
file marking and cleanup order.

Palette matching retains the final equal-distance destination candidate and
leaves pixels unchanged for an empty destination palette. Optional cache
allocation failure falls back to direct matching. Its temporary DynamicArray
has become a C-owned integer array, so cache allocation-failure points change;
the frozen successful bitmap outputs are separate from those topology changes.
The old importer processes width*height contiguous bytes from its padded bitmap
buffer; this conversion preserves that behavior and does not silently repack
rows or reverse orientation.

PIX wrapping transfers ownership only on success. Disposal now frees the
original BRender pixelmap allocation through `BrPixelmapFree`, including its
resource children, rather than trying to free an interior pixel pointer. A real
SDK allocator check requires all imported allocations to balance after releasing
the wrapper, and requires failed wrapping to leave the raw owner with the
caller. The material PIX fixture no longer performs manual SDK cleanup in the
converted path. Failed bitmap construction now frees decoded pixels instead of
leaking them, and pixel constructors leave input ownership with the caller when
construction fails. A supplemental native test writes genuinely compressed
standalone output, decodes its packed body, and verifies all header/pixel bytes,
including a metadata change through the exposed renderer view.

All 78 standalone C checks, including strict syntax, pass. The C-only texture
check and MSVC AddressSanitizer run cover endian headers, all read truncations,
layout limits, transfer/failure ownership, palette ties/cache fallback, invalid
indices and standalone file/packing/write failures. Both hosts and `core-checks`
build; all 98 focused native checks, five SDL release checks and 51 freshly
linked CLI baselines pass. No Marcus UI tests were added.

The final capture executables were frozen at SHA-256
`F872EB1D2AD3F22FD06348EF27376D699CC7B8F3C52B12BB3A7FA4EADE651895`
(SDL release) and
`3A7429D115A3489514AFD434D89E4403B2DE150642B5B1D85ACC4A6C73B4626F`
(Marcus regression). Both hashes remain unchanged after capture.

| Suite | Case | Captures |
| --- | --- | --- |
| `20261114T025840Z-f721df5a` | actor-events | six retail/SDL/Marcus seek/playback complete |
| `20261114T025953Z-1d8e0c51` | scrolling-text | six retail/SDL/Marcus seek/playback complete |

All twelve captures complete without retries and retain movie identity and
suite integrity. Authored frames, all 360 seek images and ordered playback image
changes match each player's material-resource suites
`20261114T021601Z-e526844f` and `20261114T021714Z-a38f7abf`. Only adjacent
identical position/image observations are collapsed for playback comparison.
Reports retain existing cross-player differences and single-repeat inconclusive
comparisons. These checks show no observed texture conversion regression, not
complete retail timing/PCM parity. Captures use the shared warm SDL package
cache, private processes/settings/movies and uncontrolled OS caching; no builds
or tests run concurrently with capture.

Evidence: `tmp/c89-texture-resource-before.bin`,
`tmp/c89-texture-resource-before-repeat.bin`, `tmp/c89-texture-resource-after.bin`,
`tmp/c89-texture-resource-standalone.log`, `tmp/c89-texture-resource-asan.log`,
`tmp/c89-texture-resource-native-tests.log`, `tmp/c89-texture-resource-sdl-tests.log`,
`tmp/c89-texture-resource-output-comparison.json`,
`tmp/c89-texture-resource-playback-actor-events-scrolling-text.json` and the two
`tmp/c89-texture-resource-capture-*.log` files.

### C89 indexed BMP files and RLE8 (14 November 2026)

`bitmap_file.c` owns shared eight-bit BMP validation, layout, conditional palette
reads, RGB and RLE8 pixel reads, canonical BMP writing and transactional cleanup.
The native `FReadBitmap`/`FWriteBitmap` facade retains typed file, allocation and
palette-object services. Masked-bitmap ownership/codecs, depth buffers, World
and camera control and final native facade removal remain migration work.

Two original native traces and the final converted trace match all 633,064
bytes without masking, SHA-256
`231FC86D957EBF2AE588106203BF1A150D3D914CE1F5BA84F9B1E062FCB663AC`.
The debug little-endian executable enforces FNV-1a `C71364BE`. The trace covers
1,728 native read attempts: twelve source variants, eight output masks, BASE/Pv
allocation injection and nine failure positions. Sources include padded RGB,
negative height, image-size zero, encoded/literal RLE runs, deltas, early EOB,
wrong bit depth/size, conditional palette admission, overlapping offsets and
trailing bytes. Thirty emitted files cover five widths, three heights and both
row orders, retaining all palette bytes including the reserved byte.

Palette admission/read remains conditional on the requested palette output;
metadata-only queries need not read pixels or palette. Reads retain on-disk
padded rows and report orientation separately. RGB image-size zero uses the
computed layout and permits trailing bytes. RLE runs/deltas operate against
padded widths; EOL/EOB fill with the caller's transparent index, odd literals
consume their padding and EOB ignores trailing encoded bytes. Writes retain
positive height, image-size zero, the 1,078-byte header/palette prefix and
optional padded-row reversal, including the zero-width flipped no-write case.

The C implementation rejects signed layout/height overflow and out-of-file
reads before native range assertions. Malformed RLE now releases its destination
buffer instead of losing the allocation on failure; EOL beyond the output
buffer now fails before writing. These malformed-input safety changes are
explicitly outside the safe original-native trace. The C-only fixture checks
every truncated header, missing pixel bytes, signed layout limits, malformed
run/literal/EOL canaries, output invalidation, balanced allocation/file/palette
ownership and locks, failed write deletion and initial-open semantics.

All 79 standalone C checks pass with assertions enabled, including strict C89
syntax. The C-only bitmap check also passes MSVC AddressSanitizer. Both hosts
and `core-checks` build; all 100 focused native checks, seven focused SDL release
checks and 51 freshly linked CLI baselines pass. No Marcus UI tests were added.

The final capture executables were frozen at SHA-256
`1B5C7AB48BBAD4E715BDDEDE0E2F3E0CD7977421B24DBB33632D918F2AC57BA2`
(SDL release) and
`B7EF38D4C19C062C7D6C6D8334A8B492800B95E013E5C4CC35CBC15EA12682DC`
(Marcus regression). Both hashes remain unchanged after capture.

| Suite | Case | Captures |
| --- | --- | --- |
| `20261114T033915Z-68c30f75` | actor-events | six retail/SDL/Marcus seek/playback complete |
| `20261114T034028Z-dcc46a83` | scrolling-text | six retail/SDL/Marcus seek/playback complete |

All twelve captures complete without retries and retain movie identity and
suite integrity. Authored frames, all 360 seek images and ordered playback image
changes match each player's texture-resource suites
`20261114T025840Z-f721df5a` and `20261114T025953Z-1d8e0c51`. Only adjacent
identical position/image observations are collapsed for playback comparison.
Both reports retain existing cross-player differences and single-repeat
inconclusive comparisons. These gates show no observed BMP conversion
regression; they do not establish full retail timing or PCM parity. Captures
use the shared warm SDL package cache with private processes/settings/movies
and uncontrolled OS caching; no builds or tests run concurrently with capture.

Evidence: `tmp/c89-bitmap-file-before.bin`,
`tmp/c89-bitmap-file-before-repeat.bin`, `tmp/c89-bitmap-file-after.bin`,
`tmp/c89-bitmap-file-standalone.log`, `tmp/c89-bitmap-file-asan.log`,
`tmp/c89-bitmap-file-native-tests.log`, `tmp/c89-bitmap-file-sdl-tests.log`,
`tmp/c89-bitmap-file-output-comparison.json`,
`tmp/c89-bitmap-file-playback-actor-events-scrolling-text.json`,
`tmp/c89-bitmap-file-capture-hashes-before.json`,
`tmp/c89-bitmap-file-capture-hashes-after.json` and the two
`tmp/c89-bitmap-file-capture-*.log` files.

### C89 depth-buffer storage, wire I/O and drawing (14 November 2026)

`depth_bitmap.c` owns persistent Z-buffer bounds, row/byte counts and pixel
storage, creation/contiguous renderer copies, resource factory control, header
encoding/decoding, exact-size payload reads, chunk writes, clipping and full/half
drawing. The native `ZBMP` facade supplies typed object, allocation, block and
region-scanner services. Region representations, masked-bitmap ownership/codecs,
World/camera control and final native facade removal remain migration work.

Two original native traces and the final converted trace match all 65,880 bytes
without masking, SHA-256
`C9CA1CDA746BD0DB2ADA91D4AC6B196A6DB915C0255E81A804CD0F9FEEC735A2`.
The debug little-endian native executable enforces FNV-1a `1BE82F14`. The trace
covers 256 direct/factory load attempts with BASE/Pv allocation failures,
ordinary/packed/exact-size/empty resources, changed byte-order markers and
nonzero origins; 288 constructor/import attempts across four widths and three
heights; and 216 full/half drawing combinations across reference offsets,
rectangular/empty/absent clips and disjoint/empty/absent regions. Successful
serialized headers and pixel payloads are included.

The reader retains the original host-order short decoding and ignores stored
byte-order/system markers; it does not introduce swapping or repair. Pixels
remain opaque. Zero-sized serialized layouts remain accepted. New buffers fill
with `0xff`; renderer copies retain the old contiguous-copy behavior without
using renderer row stride. Writes preserve host-order fields and partial chunks
on header/pixel write failure. Factory estimates use packed size without loading
an object, and successful estimates include the native facade's size.

Drawing retains the original no-clip/no-region no-op. Reference offsets translate
the clipping bounds but do not shift absolute source/destination addressing.
Half mode doubles only the bottom bound and scans every other row from the
actual clipped top, including odd tops. C now checks size arithmetic, translated
bounds, scanner spans and source/destination ranges before each copy. Invalid
spans stop before that copy; preceding valid spans remain copied. Negative
serialized dimensions and unrepresentable new layouts fail before allocation.
Malformed-input guards are covered separately from the safe native trace.

All 80 standalone C checks pass with assertions enabled, including strict C89
syntax. The C-only depth fixture and MSVC AddressSanitizer run cover both host
byte orders, ignored markers, every truncated header/payload size, empty/negative
layouts, allocation/read/unpack failures, balanced owners, estimate overflow,
partial writes, full/half/region clipping, odd half-height limits and rejected
overflow/invalid scanner spans with canaries. Both hosts and `core-checks` build;
all 102 focused native checks, nine focused SDL release checks and 51 freshly
linked CLI baselines pass. No Marcus UI tests were added.

Evidence: `tmp/c89-depth-bitmap-before.bin`,
`tmp/c89-depth-bitmap-before-repeat.bin`, `tmp/c89-depth-bitmap-after.bin`,
`tmp/c89-depth-bitmap-standalone.log`, `tmp/c89-depth-bitmap-asan.log`,
`tmp/c89-depth-bitmap-native-tests.log`, `tmp/c89-depth-bitmap-sdl-tests.log`
and `tmp/c89-depth-bitmap-output-comparison.json`.

The final capture executables were frozen at SHA-256
`F98FEDF6030E8442C834F261A72A7ACD94724AF91CCB132AA6A609DC7D0BD6D9`
(SDL release) and
`F927439AAB6B38D69AE0E98DD0D69968F3AFE36A9413046B41EC22177955C28D`
(Marcus regression). Both hashes remain unchanged after capture.

| Suite | Case | Captures |
| --- | --- | --- |
| `20261114T035518Z-d76e61b8` | actor-events | six retail/SDL/Marcus seek/playback complete |
| `20261114T035631Z-4ab7a114` | scrolling-text | six retail/SDL/Marcus seek/playback complete |

All twelve captures complete without retries and retain movie identity and
suite integrity. Authored frames, all 360 seek images and ordered playback image
changes match each player's indexed-BMP suites
`20261114T033915Z-68c30f75` and `20261114T034028Z-dcc46a83`. Only adjacent
identical position/image observations are collapsed for playback comparison.
Both reports retain existing cross-player differences and single-repeat
inconclusive comparisons. These gates show no observed depth-buffer conversion
regression; they do not establish full retail timing or PCM parity. Captures
use the shared warm SDL package cache with private processes/settings/movies
and uncontrolled OS caching; no builds or tests run concurrently with capture.

Capture evidence: `tmp/c89-depth-bitmap-playback-actor-events-scrolling-text.json`,
`tmp/c89-depth-bitmap-capture-hashes-before.json`,
`tmp/c89-depth-bitmap-capture-hashes-after.json` and the two
`tmp/c89-depth-bitmap-capture-*.log` files.

### C89 masked-bitmap ownership and codecs (14 November 2026)

`masked_bitmap.c` owns persistent contiguous header/row-table/RLE storage,
construction/cropping, run encoding, byte-order normalization, bounded wire/row
validation, hit testing, cache factories, serialization and BMP import control.
The native `MaskedBitmapMBMP` facade retains typed allocation/block/BMP services;
its drawing adapters borrow the host-order header/row table and encoded bytes.
Persistent HQ storage and its resize sequence are removed. Native drawing,
region/pixel services, World/camera control and final facade removal remain
dependencies of the unfinished full C-only backend.

Two original native traces and the final converted trace match all 210,848 bytes
without masking, SHA-256
`A9AF8AC0605D7612E5175AFEA856765DC880627BFBC85A19B6996B9337B4D6F2`.
The debug little-endian fixture enforces FNV-1a `65C24F63`. It covers 800
constructors across widths 1/3/8/256/600, heights 1/5, mask/upside-down flags,
transparent/solid/sparse/edge patterns, crops and reference points; 24 direct/
factory loads across memory/file, native/foreign/bad headers and packed forms;
and 24 real BMP imports across padded widths, row reversal and mask/orientation
flags. Serialized bytes, cropped bounds, hit probes, factory estimates and
post-load source-block lengths are frozen.

Encoding preserves empty-edge trimming, interior empty rows, translated empty
reference points, zero continuation runs at 255-byte boundaries and omitted
opaque payloads in mask mode. C sizes and allocates one exact buffer, replacing
the native HQ grow/shrink sequence. Failed reinitialization retains its prior
owner. The writer canonicalizes header metadata before checking exact block
size, retaining the native mutation order and single-span byte output.

The reader accepts native/foreign byte-order markers, normalizes header and row
table words, preserves raw flag/fill bytes and validates exact payload accounting.
Memory-backed blocks are consumed after successful byte transfer and before
header validation/object creation; file-backed sources remain intact. Plain
buffer allocation or read failure leaves the source intact. This changes failure
topology from HQ adoption; the original successful bytes and block effects above
are separate from those new allocation points. Native BASE/HQ/Pv injection checks
verify balanced owners over 54 constructor/load attempts.

Signed row lengths, all run spans, total row accounting and coordinate/size
arithmetic are checked in C. Malformed RLE now fails in release builds too,
instead of depending on the old debug-only validation. Overlong encoded rows,
negative/inconsistent extents, truncated data and trailing bytes fail before
renderers/hit tests receive the resource. These safety guards are checked
separately from the safe native trace.

All 81 standalone C checks pass with assertions enabled, including strict C89
syntax. The C-only masked fixture and MSVC AddressSanitizer run cover both host
orders, long transparent/opaque continuations, masks, malformed rows, every
truncated source size, source consumption, allocation/read/unpack/create/import
failures, transaction rollback, exact-size writes and balanced cleanup. Both
hosts and `core-checks` build; all 104 focused native checks and 51 freshly linked
CLI baselines pass. Twelve of thirteen selected SDL release checks pass,
including the bitmap drawing adapter. The broader `pixel-canvas` check fails at
line 607 in scrolling, before its bitmap checks. Rebuilding/running that check
with the four original native masked-bitmap adapter files restored from
`e7a12bf5` reproduces the same failure. The converted files were restored and
rebuilt afterward. This baseline failure remains open; it is not claimed as a
passing check or diagnosed here. Native debug `pixel-canvas` passes. No Marcus
UI tests were added.

Evidence: `tmp/c89-masked-bitmap-before.bin`,
`tmp/c89-masked-bitmap-before-repeat.bin`, `tmp/c89-masked-bitmap-after.bin`,
`tmp/c89-masked-bitmap-standalone.log`, `tmp/c89-masked-bitmap-asan.log`,
`tmp/c89-masked-bitmap-native-tests.log`, `tmp/c89-masked-bitmap-sdl-tests.log`,
`tmp/c89-masked-bitmap-final-sdl-tests.log`,
`tmp/c89-masked-bitmap-output-comparison.json`,
`tmp/c89-masked-bitmap-sdl-pixel-baseline-build.log` and
`tmp/c89-masked-bitmap-sdl-pixel-baseline.log`.

The final capture executables were frozen at SHA-256
`C8227B534E7C783A655C2C111DE0D6994364833C6A1BE58E604BB8AAF14A30EA`
(SDL release) and
`233F5E53040399322CA1957A36C25A0993E62084473E4732C335856D8DD10482`
(Marcus regression). Both hashes remain unchanged after capture.

| Suite | Case | Captures |
| --- | --- | --- |
| `20261114T042014Z-c750368b` | actor-events | six retail/SDL/Marcus seek/playback complete |
| `20261114T042128Z-9aed56b4` | scrolling-text | six retail/SDL/Marcus seek/playback complete |

All twelve captures complete without retries and retain movie identity and
suite integrity. Authored frames, all 360 seek images and ordered playback image
changes match each player's depth-buffer suites
`20261114T035518Z-d76e61b8` and `20261114T035631Z-4ab7a114`. Only adjacent
identical position/image observations are collapsed for playback comparison.
Both reports retain existing cross-player differences and single-repeat
inconclusive comparisons. These captures show no observed masked-bitmap
conversion regression; they do not establish full retail timing or PCM parity,
and do not resolve the separate SDL pixel-canvas test failure above. Captures
use the shared warm SDL package cache with private processes/settings/movies
and uncontrolled OS caching; no builds or tests run concurrently with capture.

Capture evidence: `tmp/c89-masked-bitmap-playback-actor-events-scrolling-text.json`,
`tmp/c89-masked-bitmap-capture-hashes-before.json`,
`tmp/c89-masked-bitmap-capture-hashes-after.json` and the two
`tmp/c89-masked-bitmap-capture-*.log` files.

### SDL pixel-canvas scrolling reference closeout (14 November 2026)

The release failure recorded above is resolved in the test reference, with no
renderer or player changes. In the optimized MSVC 19.43 build, the reference
loop with `int` induction coordinates evaluated the destination guard row
`y = -1` as visible: for clip mode 2 and delta (-2, -1), it chose sample 10
instead of the unchanged guard sample 0. The rendered guard byte was correctly
unchanged. Diagnostic evaluation of the predicate at that point returned true
despite its `y >= 0` condition. The same failure had already been reproduced
with the pre-conversion masked-bitmap adapters.

Keeping both reference-loop coordinates signed 64-bit throughout the calculation
avoids that optimized-build failure. The coordinate domain, independent
visibility predicate, deltas, expected pixel values and 8/32-bit comparisons
are unchanged. A new assertion explicitly requires every guard-row/padding
coordinate to select its original sample. The matrix retains three clipping
modes, six deltas (including extreme signed values and zero), six rows and eight
columns. No compiler optimization is disabled.

An optimized AddressSanitizer build of the original test, `kauai-core` and the
C89 library also passes without a sanitizer diagnostic. That instrumented build
is separate evidence; the final tests use ordinary optimized release libraries.
All thirteen selected SDL release checks now pass, including `pixel-canvas`,
and the native debug pixel-canvas check passes. The full test continues beyond
scrolling through bitmap, polygon, indexed brush and coverage checks.

No new playback captures were taken for this test-only correction. Player
SHA-256 values remain exactly those of the masked-bitmap capture above:
`C8227B534E7C783A655C2C111DE0D6994364833C6A1BE58E604BB8AAF14A30EA`
(SDL release) and
`233F5E53040399322CA1957A36C25A0993E62084473E4732C335856D8DD10482`
(Marcus regression). Marcus was neither rebuilt nor stopped; no Marcus UI tests
were added. World/camera and the remaining rendering/host integration work keep
the full C89 migration active.

Evidence: `tmp/c89-masked-bitmap-pixel-scroll-diagnostic.log`,
`tmp/c89-pixel-scroll-visibility.log`, `tmp/c89-pixel-scroll-asan-build.log`,
`tmp/c89-pixel-scroll-asan.log`, `tmp/c89-scroll-check-final-sdl-build.log`,
`tmp/c89-scroll-check-final-sdl-tests.log`,
`tmp/c89-scroll-check-final-native-build.log` and
`tmp/c89-scroll-check-final-native-tests.log`.

### C89 World buffer ownership and background transactions (14 November 2026)

`world_buffers.c` owns persistent World colour/depth/stretch buffers, dirty
regions, background-cache references, mode dimensions and renderer descriptions.
It controls construction/disposal order, replacement transactions, background
fetch/priority/lifetime, colour-copy orchestration and depth resampling. Native
World fields and SDK pixelmaps are borrowed views published by typed services.
Graphics-port/region primitives and SDK scene setup remain host services.
Camera, render sequencing, picking, export policy and final native representation
removal remain work; this completes the World resource foundation, not the full
World conversion or whole C89 backend.

Two original native traces, an original SDL trace and the final native/SDL
traces match all 71,376 bytes without masking, SHA-256
`507B79FD5C425A459A0D16D15E87996AF4557EE82E491179EEC1DACC46D0FC19`.
Little-endian fixtures enforce FNV-1a `35BEFB80` in both debug and release.
Forty-eight snapshots cover four initial half-mode combinations, loaded
backgrounds, half-mode changes, scales 1/2/4, invalid/no-op requests and combined
resolution settings. They freeze dimensions, owner presence, SDK pixelmap fields,
dirty bounds and defined background colour/depth bytes through real GDI and SDL
graphics ports. Initial undefined colour-buffer contents are not recorded.

Working RGB keeps one renderer lock until replacement/disposal. Replacement
holds old buffers while new buffers and background reload are attempted;
failure restores owners, mode and renderer descriptions, while success releases
old buffers in native order and dirties working/screen regions. Dirty-region
and cache owners survive the buffer transaction. Cache replacement retains
before releasing, including replacement with the same cache. Invalid modes
allocate nothing; scale is 1/2/4 and rendered dimensions remain capped at 4,096.
Half flags reduce dimensions only at scale 1. The half-Y/full-X stretch port
allocation remains preserved even though the old drawing path does not use it.

Background loading retains priority/fetch/release order, full-mode depth
adoption and partial colour writes if later depth work fails. Reduced depth
samples every other column/row, while scaled depth is drawn into a native-size
intermediate and replicated without interpolation. Size/span checks stop before
an invalid copy and preserve the completed prefix. The reduced-mode temporary
colour-port failure now checks the newly allocated port; the original code
checked the existing destination and could construct an environment with a null
port. Failure returns cleanly and releases fetched resources.

All 82 standalone C checks pass with assertions enabled, including strict C89
syntax. The C-only World fixture and MSVC AddressSanitizer run cover 108
initialization attempts and 60 mode-change attempts with failure injection,
owner/lock balance, rollback, cache reuse, exact depth samples, no-op/invalid
modes and partial background writes. A final supplemental case uses a valid
depth resource shorter than the viewport: only available rows are copied,
the final row and guards remain intact, and the failed request publishes no
cache/dirty state. After capture, the supplemented C fixture was rerun standalone,
under AddressSanitizer and through both host build links. Player hashes did not
change. Both hosts and `core-checks` build; all 106 focused native checks,
fifteen focused SDL release checks and 51 freshly linked CLI baselines pass.
No Marcus UI tests were added.

The final capture executables were frozen at SHA-256
`2E8A469DDD15106409501E0336D49C293D1CBC9E21F93E507A1D7E0C588E5EC8`
(SDL release) and
`2DC7E42A6B0025818DE94450F25B69B0DE4DF18D8F239B6F94149C348F2A48D3`
(Marcus regression). Both hashes remain unchanged after capture and the final
supplemental C checks.

| Suite | Case | Captures |
| --- | --- | --- |
| `20261114T045551Z-b3e51c55` | actor-events | six retail/SDL/Marcus seek/playback complete |
| `20261114T045703Z-490c8fac` | scrolling-text | six retail/SDL/Marcus seek/playback complete |

All twelve captures complete without retries and retain movie identity and
suite integrity. Authored frames, all 360 seek images and ordered playback image
changes match each player's masked-bitmap suites
`20261114T042014Z-c750368b` and `20261114T042128Z-9aed56b4`. Only adjacent
identical position/image observations are collapsed for playback comparison.
Both reports retain existing cross-player differences and single-repeat
inconclusive comparisons. These gates show no observed World buffer conversion
regression; they do not establish full retail timing or PCM parity. Captures
use the shared warm SDL package cache with private processes/settings/movies
and uncontrolled OS caching; no builds or tests run concurrently with capture.

Evidence: `tmp/c89-world-buffers-before*.bin`, `tmp/c89-world-buffers-after*.bin`,
`tmp/c89-world-buffers-standalone.log`, `tmp/c89-world-buffers-final-standalone.log`,
`tmp/c89-world-buffers-final-asan.log`, `tmp/c89-world-buffers-native-tests.log`,
`tmp/c89-world-buffers-sdl-tests.log`,
`tmp/c89-world-buffers-final-focused-native-tests.log`,
`tmp/c89-world-buffers-final-focused-sdl-tests.log`,
`tmp/c89-world-buffers-output-comparison.json`,
`tmp/c89-world-buffers-playback-actor-events-scrolling-text.json` and the two
`tmp/c89-world-buffers-capture-*.log` files.

### C89 World camera and picking (14 November 2026)

`world_camera.c` now owns persistent camera matrix/near/far/FOV state and
nearest-hit state. It controls camera publication then working-region dirtying,
pick reset/dispatch, scale/half/origin coordinate conversion, strict nearest-hit
selection, actor-root owner traversal and inclusive-to-exclusive rendered bounds.
The native camera and pick fields are borrowed mirrors. Typed services retain
SDK field access and intersection queries, region mutation and client callback
dispatch. Render sequencing, temporary material overrides, dirty-region copying,
prerendering, drawing/export control and final native representation removal
remain work; neither the World conversion nor the whole C89 backend is complete.

Two original native runs, an original SDL run and converted native/SDL runs match
all 44,472 bytes without masking, SHA-256
`1C21E2C115C4777BC39977DCA67A05A55A27866FD7E880F4053C0D367436C63A`.
The little-endian host fixture enforces FNV-1a `AD744449`. It covers six full,
half-X, half-Y, half-both, 2x and 4x modes, three cameras and all eight optional
camera-output combinations, working-region/latch state, real SDK hits across
714 points per mode with two origins, synthetic MAX/near/equal/negative depth
candidates, nested actor-root routing and negative rendered-bound callbacks.
The points include 21/18/17/15/21/18 hits respectively, so the trace covers hits
as well as misses. Callback removal suppresses subsequent client publication.

Camera changes retain the native working-region dirtying without setting the
world-changed latch. Half-mode picking truncates negative halves toward zero;
high-resolution picking multiplies before subtracting the origin and does not
clamp to the viewport. Equal-depth candidates keep the first actor, MAX-depth
candidates do not select an actor, and searching always continues. Rendered
bounds retain the legacy `(exclusive + scale - 1) / scale` expression even for
negative coordinates. Checked arithmetic now rejects unrepresentable pick
coordinates and rendered bounds before SDK/client dispatch; optional outputs
and failed coordinate/rectangle outputs remain untouched. C89 division behavior
is explicit rather than dependent on the compiler's negative-division choice.

All 83 standalone C checks pass with assertions enabled, including strict C89
syntax. The new C-only fixture also builds/runs directly with Clang
`-std=c89 -pedantic-errors -Wall -Wextra -Werror` and passes MSVC AddressSanitizer.
It checks publication order, optional outputs, matrix aliasing, boundary
coordinates, overflow-output preservation, callback dispatch/reset, tie selection
and actor-root ownership. Both hosts and `core-checks` build; all 108 focused
native checks, seventeen focused SDL release checks and 51 freshly linked CLI
baselines pass. After explicit include/mirror-comment cleanup, four World native
checks and all seventeen SDL checks pass on the final rebuild. Marcus was stopped
before each rebuild and relaunched afterward. No Marcus UI tests were added.

Capture executables were frozen before/after capture at SHA-256
`1C17FA92507C686C6EAA8F3F2074B229BEFB0164AC8055274D967B99388B2734`
(SDL release) and
`648BD908D2E0D8FAD42D69E22C2DF294E992FA021096718B038E1FA76E54D90C`
(Marcus regression).

| Suite | Case | Captures |
| --- | --- | --- |
| `20261114T052551Z-996e1e80` | actor-events | six retail/SDL/Marcus seek/playback complete |
| `20261114T052705Z-ea4c2c0f` | scrolling-text | six retail/SDL/Marcus seek/playback complete |

All twelve captures complete without retries, with movie identity and suite
integrity preserved. Authored frames, all 360 seek images and ordered playback
image changes match each player's preceding World-buffer suites
`20261114T045551Z-b3e51c55` and `20261114T045703Z-490c8fac`. Playback comparison
collapses only adjacent identical position/image observations. Existing
cross-player differences and single-repeat inconclusive comparisons remain;
these gates show no observed camera/picking conversion regression and do not
establish complete retail timing or PCM parity. Captures use a shared warm SDL
package cache with private processes/settings/movies and uncontrolled OS caching.
No builds or tests run concurrently with capture. Picking itself is covered by
the native/C fixtures rather than by these noninteractive playback captures.

Evidence: `tmp/c89-world-camera-native-before*.bin`,
`tmp/c89-world-camera-sdl-before.bin`, `tmp/c89-world-camera-*-after.bin`,
`tmp/c89-world-camera-standalone.log`, `tmp/c89-world-camera-asan.log`,
`tmp/c89-world-camera-native-tests.log`,
`tmp/c89-world-camera-native-final-focused.log`,
`tmp/c89-world-camera-sdl-tests.log`,
`tmp/c89-world-camera-output-comparison.json`,
`tmp/c89-world-camera-playback-actor-events-scrolling-text.json`,
`tmp/c89-world-camera-capture-hashes-*.json` and the two
`tmp/c89-world-camera-capture-*.log` files.

### C89 World rendering and material scopes (14 November 2026)

`world_render.c` now owns the persistent changed latch, begin/clean/render/bounds
sequencing, recursive model-style overrides, temporary material snapshots and
restoration, clipped RGB dirty-region copy loops, depth-clean dispatch,
prerender snapshots, background-restoration dispatch and screen/draw control.
Native adapters retain SDK actor/material fields and registry enumeration,
rasterization, graphics-port/region/scanner primitives and application marking.
The native changed flag is a borrowed mirror. Bitmap export construction,
background scaling, palette lifetime, export transactions and final native
representations remain work; this does not complete the whole C89 backend.

Two original native traces, an original SDL trace and converted native/SDL
traces match all 528,048 bytes without masking, SHA-256
`3C1559759C1D70B0ED937F918C3E6773805AB615640CE2CED9CE3A1B759D5EEC`.
Little-endian fixtures enforce FNV-1a `4FC7CD7D`. The fixture covers six full,
half-X, half-Y, half-both, 2x and 4x modes; all eight wire/flat/no-texture options;
begin/rendered/root-rectangle callback order; material flags/textures/palette
ranges during and after rendering; dirty-region bounds; defined RGB/depth
buffers; redundant renders; prerender/unprerender and offset drawing. Its initial
smooth textured test material omitted both lighting and prelit inputs, producing
unstable SDK colour values. Adding the prelit flag corrected the fixture before
freezing the native baseline; no production renderer change was made for that.

Begin callbacks visit immediate NONE roots before cleanup. Working dirt is
unioned into screen dirt before clearing and again after SDK rendering and
restored materials. Root-rectangle callback availability remains dynamic.
The changed latch clears last, including when marked by render callbacks.
Style traversal leaves the world root and bounding-point/edge/face models
untouched while still traversing their descendants. Texture removal preserves
the retail solid grey ramp, base/range 15; flattening alone leaves textured
materials unchanged. Material restoration uses forward enumeration order.

The C material scope owns bounded snapshot storage. Allocation/size/capacity
failure restores the applied prefix, releases storage and skips SDK rendering;
the world remains dirty for retry. Prerendering commits no background snapshot
after render failure. The debug supersampled export path uses the same C scope
and restores its global bounds callback and RGB lock before returning failure.
This replaces an uncaught vector-allocation exception with bounded failure;
completed cleanup/style work before the failed allocation is retained.

RGB cleanup intersects region bounds with buffer dimensions and validates
pitches, available bytes and each relative scan span before copying. Completed
prefix copies remain after a later malformed span. Locks release working then
background; scanner references/locks release immediately afterward, before
dirty-region mutation. Review caught an initial adapter lifetime extension that
kept the scanner alive until the outer render returned. It was corrected after
the first capture set and all gates/captures were rerun. The C fixture now asserts
that no scanner owner survives into region union. The first capture metadata/logs
are retained as intermediate evidence, not used as the final gate.

Scaled actor rectangles and draw offsets reject unrepresentable arithmetic
before region publication/blitting. Drawing retains the original negative offset
and ignored clip argument; host blits clip. Screen-region Scale/Offset arithmetic
remains a host primitive, not a new general overflow guarantee.

All 84 standalone C checks pass with assertions enabled, including strict C89
syntax. The new C-only fixture also builds/runs directly with Clang
`-std=c89 -pedantic-errors -Wall -Wextra -Werror` and passes MSVC AddressSanitizer.
It covers all four flat/no-texture combinations, exact restoration order,
allocation/size/capacity failure and prefix rollback, distinct source/destination
pitches and guard bytes, empty/invalid scan paths, lock failure, scanner lifetime,
all three bounding styles and descendants, changed-latch/callback behavior,
option changes during begin callbacks, dynamic rectangle registration, failed
prerender/retry and screen/draw control. Both hosts and `core-checks` build;
all 110 focused native checks, nineteen focused SDL release checks and 51 freshly
linked CLI baselines pass after the scanner fix. A final callback-contract comment
clarification was followed by rebuilds, six native World checks and the nineteen
SDL checks. Marcus was stopped before each rebuild and relaunched afterward.
No Marcus UI tests were added.

Final capture executables were frozen before/after capture at SHA-256
`21F33AC2E4C3DD1A64DF723D71BB75AC9BA91E37B53AEA0060029B221885EC2E`
(SDL release) and
`1200E0883BB43BF755CB14F27B1FD46447338AF9734F559CF1B70BB7FF22BCD9`
(Marcus regression).

| Suite | Case | Captures |
| --- | --- | --- |
| `20261114T055809Z-bf2d3736` | actor-events | six retail/SDL/Marcus seek/playback complete |
| `20261114T055922Z-4f06e1ad` | scrolling-text | six retail/SDL/Marcus seek/playback complete |

All twelve final captures complete without retries, retaining movie identity and
suite integrity. Authored frames, all 360 seek images and ordered playback image
changes match each player's preceding camera/picking suites
`20261114T052551Z-996e1e80` and `20261114T052705Z-ea4c2c0f`. Playback comparison
collapses only adjacent identical position/image observations. Reports retain
existing cross-player differences and single-repeat inconclusive comparisons;
these gates show no observed render-policy regression and do not establish full
retail timing or PCM parity. Captures use a shared warm SDL package cache with
private processes/settings/movies and uncontrolled OS caching. No builds or tests
run concurrently with capture. The final comparison selects only environment
revision 2; intermediate pre-scanner-fix suites `20261114T054811Z-92599730` and
`20261114T054924Z-1d21cfc7` cannot satisfy the final gate.

Evidence: `tmp/c89-world-render-before*.bin`,
`tmp/c89-world-render-sdl-before.bin`, `tmp/c89-world-render-*-after.bin`,
`tmp/c89-world-render-after.bin`, `tmp/c89-world-render-standalone.log`,
`tmp/c89-world-render-asan.log`, `tmp/c89-world-render-native-tests.log`,
`tmp/c89-world-render-final-world-checks.log`, `tmp/c89-world-render-sdl-tests.log`,
`tmp/c89-world-render-output-comparison.json`,
`tmp/c89-world-render-playback-actor-events-scrolling-text.json`,
`tmp/c89-world-render-capture-hashes-*.json`,
`tmp/c89-world-render-initial-capture-*` and the two final
`tmp/c89-world-render-capture-*.log` files.

### C89 World bitmap export and palette lifetime (14 November 2026)

`world_export.c` now owns current/scaled export sizing, native-view background
fill, cache fetch/reference order, temporary colour/depth ownership, renderer
descriptions, style/material scope sequencing, bounds-callback suppression and
restoration, flush/lock ordering, palette lifetime and bitmap-writer dispatch.
Native facades retain typed graphics/file/palette-object and SDK primitives.
World's existing export entry points remain debug-only; the C export API is
available in all configurations. SDK/object/region representations and final
facade removal remain dependencies of the unfinished whole C89 backend.

Two original native runs, an original SDL debug run and converted native/SDL
debug runs match all 13,188,586 bytes without masking, SHA-256
`456793441243CD84817FB55F5BC5EF40B031B177D4040B459BC35CD8B2C2F246`.
Little-endian host fixtures enforce FNV-1a `E235ADA9`. The 243 exports cover
six full/half/scaled live modes; eight wire/flat/no-texture settings; current
buffer output and supersampling at 1/2/4/16; occluding depth samples; no-cache
fill; a 640x2 view clamped from 16x to 6x; and defined live buffer, camera,
dirty-region and callback-count preservation. Actual BMP files are read back
and included in the trace. Native and SDL debug fixtures verify live-state
fingerprints around each export; callbacks remain suppressed during temporary
rendering. The test also checks actor style outside the export loop.

Scale selection decrements within 1..16 until both axes fit 4096, ignoring live
half/scale settings. A 640x480 request at 16x therefore selects 6x, 3840x2880.
The legacy tight SDK row is preserved; the writer receives the actual port
pitch, and the native BMP adapter verifies four-byte row alignment. No-cache
exports retain default RGB-zero/depth-FF fill. Current export writes the existing
working buffer without rendering. Applied model styles remain applied, as in the
native exporter; the controller does not promise style rollback.

Both live and export depth copies now call `mm_world_depth_resample`.
The public helper supports factors 1..16; live mode admission remains 1/2/4.
It copies opaque two-byte samples without interpolation, supports padded rows
and checks all source/destination spans before each copy. Partial prefixes can
remain after malformed later source samples; temporary export resources are
then released and no file is written. Export depth reads direct source rows,
ignoring cached origins exactly as the original exporter did.

Owned palette copies release after unlocking on both success and failure.
This fixes the current-buffer export's palette leak. Palette-allocation failure
now returns false before the original null-palette writer assertion. Invalid
pixel/depth spans fail before unsafe rasterization/writing. Material and bounds
state restore before flush/unlock; output writing happens afterward, then RGB
releases before depth. Existing bitmap-writer file/error semantics are preserved;
this controller does not add atomic file replacement.

All 85 standalone C checks pass with assertions enabled, including strict C89
syntax. The new C-only fixture builds/runs directly with Clang
`-std=c89 -pedantic-errors -Wall -Wextra -Werror` and passes MSVC AddressSanitizer.
It checks every scale 1..16, scale clamping/invalid requests, padded depth rows
and guards, source truncation, ten scaled-export failure phases, three current
export failure phases, no-cache fill, distinct live/export dimensions, palette
release, retained renderer locks and callback/material cleanup. Supplemental
native and SDL debug fixtures each run 66 BASE/HQ/Pv allocation attempts and
require balanced owner counts and unchanged live state, including successful
current-buffer export. An initial counter increase was the default warning
logger's lazy persistent FileObject, not an exporter owner; routing test warnings
directly to stderr removes that observer allocation from the checks.

Both hosts and `core-checks` build. All 112 focused native checks, eight SDL debug
World checks, twenty SDL release checks and 51 freshly linked CLI baselines pass.
The SDL debug configuration exercises the actual export adapters; SDL release
exercises the C controller but has no native debug export entry point. The native
export CTest fixture returns skip code 77 in release configurations. Marcus was
stopped before rebuilding and relaunched afterward. No Marcus UI tests were added.

Capture executables were frozen before/after capture at SHA-256
`CF26D47044CCC92438CA599F3A84441EEF6B85B12BA0397B521ADAA7D754C572`
(SDL release) and
`2C9D45A794E1D44FEBD1B1FDB79772CD1DC4B7DFB669C44DFFC8941554A65449`
(Marcus regression).

| Suite | Case | Captures |
| --- | --- | --- |
| `20261114T063118Z-b337a43e` | actor-events | six retail/SDL/Marcus seek/playback complete |
| `20261114T063235Z-d38a8d14` | scrolling-text | six retail/SDL/Marcus seek/playback complete |

All twelve captures complete without retries, retaining movie identity and suite
integrity. Authored frames, all 360 seek images and ordered playback image changes
match each player's preceding World-render suites `20261114T055809Z-bf2d3736`
and `20261114T055922Z-4f06e1ad`. Playback comparison collapses only adjacent
identical position/image observations. Reports retain existing cross-player
differences and single-repeat inconclusive comparisons. These gates show no
observed playback regression from the shared depth-resampler change; export
behavior itself is covered by the debug native/SDL and C-only fixtures, not these
noninteractive captures. Full retail timing and PCM parity remain unproven.
Captures use a shared warm SDL package cache with private processes/settings/
movies and uncontrolled OS caching. No builds or tests run concurrently with
capture.

Evidence: `tmp/c89-world-export-native-before*.bin`,
`tmp/c89-world-export-sdl-before.bin`, `tmp/c89-world-export-*-after.bin`,
`tmp/c89-world-export-native-trace.log`, `tmp/c89-world-export-sdl-debug-trace.log`,
`tmp/c89-world-export-standalone.log`, `tmp/c89-world-export-asan.log`,
`tmp/c89-world-export-native-tests.log`, `tmp/c89-world-export-sdl-debug-tests.log`,
`tmp/c89-world-export-sdl-release-tests.log`,
`tmp/c89-world-export-output-comparison.json`,
`tmp/c89-world-export-playback-actor-events-scrolling-text.json`,
`tmp/c89-world-export-capture-hashes-*.json` and the two
`tmp/c89-world-export-capture-*.log` files.

### C89 Scene ownership and lifecycle (14 November 2026)

`scene_owner.c` now owns persistent references to frame/start event groups,
actor/text roll-call containers, selection extras, background, thumbnail and
background sound. The parent movie remains borrowed. Native pointer fields are
published views; every creation/load/replacement/release assignment for these
owners now routes through the C record. Construction order, event-cursor reset
timing, optional loaded notification, partial construction cleanup, destruction
and completed-prefix read cleanup, hide/show/close ordering and owned scene-sound
tag disposal now execute in C89. SceneData metadata remains its existing C record;
frame/event runtime, loading/marshalling/editing transactions, caption/background
implementations and final native facades remain unfinished dependencies. This
slice does not make the whole Scene implementation or backend C-only.

Two original native runs, an original SDL debug run and converted native/SDL
debug runs match 7,888 bytes without masking, SHA-256
`820411D4385C1BE0A624D4A76AD2384887E0A974C4F29786B354F39057D8F2C8`.
The SDL release before/after trace matches 8,688 bytes, SHA-256
`12CCD982D3A4E714550999609A5CD9AC97A167FB692DCD8BD3D6E1A59450B6E2`.
Little-endian fixtures enforce FNV-1a `0EE1BC65` (debug) and `2A12D054` (release).
Release omits allocation injection, so its successful-state trace differs from
debug. Seventy-eight factory cases cover notification availability, BASE/HQ/Pv
allocation positions, defaults, event/list layouts, error cleanup and balanced
owner counts. Twelve populated cases cover retained and final close, start-owned
actor destruction order relative to tag references, thumbnail/selection ownership
and frame/background sound cache-reference balance.

The native actor probes are deliberately unstaged partial actors used only for
destruction/reference order. Validity assertions are suspended around those
populated scenes' teardown; the probes never exercise body hiding/rendering.
Actual actor/text/light behavior is additionally covered by C callback tests,
the existing save/load integration suite and playback gates. Six supplemental
cases use real runtime sound packets and the native read-failure service bridge,
require tag-cache counts to return to one and do not alter the frozen trace.

Normal disposal walks start events forward, releasing their actor/text objects,
then releases the start group. It closes all frame-event sound tags before
releasing the frame group, then actor/text/selection containers, background,
thumbnail and background sound. Roll-call/selection entries are borrowed and do
not receive another release. Owned background/undo sound packets close only tags
with an open cache, free the allocation and publish NULL. Release callbacks see
the old owner until release completes, preserving native publication timing.
Close turns off lights, hides actors, seeks each text box with a valid lifetime
to its first frame minus one, releases the scene and clears the caller's handle.
A retained scene keeps its resources until the last reference is released.

Read-failure cleanup walks completed start and frame prefixes in reverse order,
reacquiring sound payloads before each tag close. The original code compared a
sound packet's variable byte count against fixed event bytes plus variable bytes;
the extra fixed-header size rejected valid packets and could skip their tag
cleanup. The C path validates the actual variable payload size, correcting that
error. Short/oversized event payloads and invalid owned-sound headers diagnose
before unsafe reads. A text lifetime beginning at signed MIN diagnoses failure
instead of overflowing on subtraction. These bounded malformed-input changes and
the prefix-size fix are separate from the frozen valid lifecycle behavior.

All 86 standalone C checks pass with assertions enabled, including strict C89
syntax. The new fixture also builds/runs directly with Clang
`-std=c89 -pedantic-errors -Wall -Wextra -Werror` and passes MSVC AddressSanitizer.
C-only checks cover each factory failure phase, notification after publication,
forward disposal and reverse unwind order, borrowed lists, retained close,
sound-tag policy, visibility/close effects, failed text seeks, signed bounds and
malformed event payloads. Both hosts and `core-checks` build. All 114 focused
native checks, ten SDL debug checks, twenty-two SDL release checks and 51 freshly
linked CLI baselines pass. Marcus was stopped before rebuilding and relaunched
afterward. No Marcus UI tests were added.

Final capture executables were frozen before/after capture at SHA-256
`937B8F0964615AB27AE3ECC561CCB7DC95D56B0CD56429F0A0C3AE17BEEEB7D9`
(SDL release) and
`3E65F3629BABB9603B42D7B15BFE41A6CCBBEEA1956ECCC303CD039D1F1381A0`
(Marcus regression).

| Suite | Case | Captures |
| --- | --- | --- |
| `20261114T071925Z-a255b901` | actor-events | six retail/SDL/Marcus seek/playback complete |
| `20261114T072040Z-d84dedda` | scrolling-text | six retail/SDL/Marcus seek/playback complete |

All twelve captures complete without retries, retaining movie identity and suite
integrity. Authored frames, all 360 seek images and ordered playback image changes
match each player's preceding World-export suites `20261114T063118Z-b337a43e`
and `20261114T063235Z-d38a8d14`. Playback comparison collapses only adjacent
identical position/image observations. Existing cross-player differences and
single-repeat inconclusive comparisons remain. These gates show no observed
Scene lifecycle regression; they do not establish full retail timing or PCM
parity. Captures use a shared warm SDL package cache with private processes,
settings and movies and uncontrolled OS caching. No builds or tests run
concurrently with capture.

Evidence: `tmp/c89-scene-owner-before*.bin`, `tmp/c89-scene-owner-after.bin`,
`tmp/c89-scene-owner-sdl-*-before.bin`, `tmp/c89-scene-owner-sdl-*-after.bin`,
`tmp/c89-scene-owner-supplemental.log`, `tmp/c89-scene-owner-early-integration.log`,
`tmp/c89-scene-owner-standalone.log`, `tmp/c89-scene-owner-asan.log`,
`tmp/c89-scene-owner-native-tests.log`, `tmp/c89-scene-owner-sdl-debug-tests.log`,
`tmp/c89-scene-owner-sdl-release-tests.log`,
`tmp/c89-scene-owner-output-comparison.json`,
`tmp/c89-scene-owner-playback-actor-events-scrolling-text.json`,
`tmp/c89-scene-owner-capture-hashes-*.json` and the two
`tmp/c89-scene-owner-capture-*.log` files.

### C89 Scene frame movement, replay and prerendering (14 November 2026)

`scene_runtime.c` now owns frame movement/replay, actor/text force loops,
frame-range extension and two-pass prerender policy. Its persistent runtime
record holds the event cursor, disabled-effects mask and background-sound frame;
all native editing/dispatch references use that record directly. SceneData's C
metadata remains live throughout callbacks. Native adapters supply collection
access, actor/caption operations, World snapshots and movie/client notifications.
Single-event play/unplay policy, background/caption implementation and remaining
Scene loading/editing remain native dependencies, as does final facade removal.

The original native trace, its repeat, the original SDL release trace and the
converted native trace match all 5,664 bytes, SHA-256
`1F686B330EB64C1D2A56290EF99AFDD9E170DA322507951C268ECCD353B58F7C`.
Converted native/SDL debug/SDL release fixtures enforce FNV-1a `3434DECD` and the
same length. The fixture uses a real Scene with empty actor/text lists and six
pause/sound events, including MIDI background-cache replacements. It records ten
forward/backward/same-frame seeks and four replay masks per seek under two
disabled-effects masks, including client pause/sound notifications, metadata,
cursor, flags and background-sound state. It does not freeze actual actor/caption
rendering or device audio; those use the other gates described below.

Backward seeks retain reverse same-frame event order. Forward seeks suppress
sound/pause effects for intervening events, and replay always suppresses cameras.
A failed event leaves the cursor at that event and reports the existing goto
error; actor/text force and actor replay failures return without another goto
error. The optional sound output is carried through every actor, including on
failure. Range extension moves first-frame cameras and extends metadata without
dirtying the movie. Prerendering retains its two render-need queries and repeated
show operations, with no snapshot when all actors must render; end-prerender
restores the World background even with no prerendered actors.

All 87 standalone C checks pass, including strict C89 syntax. The new fixture
also builds/runs directly with Clang `-std=c89 -pedantic-errors -Wall -Wextra
-Werror` and passes MSVC AddressSanitizer. C-only checks cover partial play/unplay
failures, live cursor observations, replay-mask complement, actor/text/replay
failures, dynamic actor counts, optional sound outputs, signed frame extremes,
camera range movement, lifetime failure and both prerender passes with changing
render-need answers. Both hosts and `core-checks` build; all 116 focused native,
12 SDL debug and 24 SDL release checks pass. All 51 CLI output baselines match.
Final adapter formatting was followed by host rebuilds and focused native/SDL
runtime reruns; SDL debug's full selected gate uses the final build. Marcus was
stopped before its rebuilds and relaunched afterward. No Marcus UI tests were
added.

Final capture executables were frozen before/after capture at SHA-256
`1EA710D7BD2348A8BDAA6C0BCFE77C8EA6A9B86781E5603C0D7C6C465419EF52`
(SDL release) and
`9DA2C3CB89DAF3ED1500681ECC7F848F4A47466CE3C665C235DAAA6343F21F99`
(Marcus regression).

| Suite | Case | Captures |
| --- | --- | --- |
| `20261114T074843Z-1e6cf808` | actor-events | six retail/SDL/Marcus seek/playback complete |
| `20261114T074956Z-622e38ae` | scrolling-text | six retail/SDL/Marcus seek/playback complete |

All twelve captures complete without retries, retaining movie identity and suite
integrity. Authored frames, all 360 seek images and ordered playback image changes
match each player's preceding Scene-owner suites `20261114T071925Z-a255b901`
and `20261114T072040Z-d84dedda`. Playback comparison collapses only adjacent
identical position/image observations. Existing cross-player differences and
single-repeat inconclusive comparisons remain. This establishes no observed
regression within this coverage, not full retail timing or PCM parity. Captures
use a shared warm SDL package cache with private processes, settings and movies
and uncontrolled OS caching. No builds or tests ran concurrently with capture.

Evidence: `tmp/c89-scene-runtime-before*.bin`,
`tmp/c89-scene-runtime-after.bin`, `tmp/c89-scene-runtime-final-*.bin`,
`tmp/c89-scene-runtime-standalone.log`, `tmp/c89-scene-runtime-asan.log`,
`tmp/c89-scene-runtime-native-tests.log`,
`tmp/c89-scene-runtime-sdl-debug-tests.log`,
`tmp/c89-scene-runtime-sdl-release-tests.log`,
`tmp/c89-scene-runtime-final-focus.log`,
`tmp/c89-scene-runtime-final-sdl-focus.log`,
`tmp/c89-scene-runtime-output-comparison.json`,
`tmp/c89-scene-runtime-playback-actor-events-scrolling-text.json`,
`tmp/c89-scene-runtime-capture-hashes-*.json` and the two
`tmp/c89-scene-runtime-capture-*.log` files.

### C89 Scene event dispatch (14 November 2026)

`scene_events.c` now owns single-event play/unplay and start-event sequencing.
It uses the C Scene owner/runtime/metadata records directly; native wrappers
provide payload access, collection/reference primitives and typed actor, text,
background and movie/client operations. SceneSoundEvent packet construction,
marshalling, duplication and per-tag playback helpers remain native policy to
convert, along with remaining loading/editing and caption/background operations.
This slice does not complete Scene or final C-only host integration.

The baseline uses the native dispatcher at `292bb35d`. Original/repeated/converted
native debug traces and the converted SDL debug trace
match all 6,928 bytes, SHA-256
`271A016C24D102C2587A5412884E30C49C58762CD55A0E017EAA6058EB3EAE66`,
FNV-1a `2B9558ED`. Original/converted SDL release traces match 6,928 bytes,
SHA-256 `1B98848A750DA650C7910296AE715E7A8444850ED06B9F92AFB0832C42A878BE`,
FNV-1a `17EBE56D`. The fixture retains the preceding frame/replay trace and adds
direct MIDI/SFX/pause events, all four pause reasons, no-op unplay kinds, cache
clearing, empty start dispatch and MIDI duplication allocation failures. Release
omits allocation injection, explaining its different fingerprint. Expected
allocation warnings are retained. Real packet tags and actor/text/background
rendering are not frozen by this fixture; C-only tests and integration/capture
gates supply the other coverage.

MIDI updates its owned cache and starting frame before sound suppression, but a
failed duplicate preserves the previous cache/frame. Pause notification occurs
even when pauses are disabled, and delay is read only after notification when
enabled. Background setup fetches a reference, sets camera zero, supplies a cut
transition if absent, publishes ownership, then publishes the weak tag. Failed
camera setup releases the new reference without changing ownership/transition.
Tag adaptation preserves all fields, including an unused cache pointer for a
non-cache source. Actor/text append failures do not seek; failed seeks remove
the appended entry, retaining the actor's scene binding and any completed start
prefix. Roll-call entries remain borrowed from the owning start events.

Rewind searches strictly before the current frame. Its first earlier sound event
may have any style; replay failure is deliberately ignored. Previous-camera
failure is diagnosed but returns success; failure of the camera-zero fallback
returns failure. Suppressed cameras still diagnose a missing background. Clearing
background sound leaves its prior frame value stale, preserving native behavior.
Unknown play kinds diagnose and return success; unknown unplay kinds remain
silent no-ops. Actors-only start dispatch never reads non-actor payloads.

Final source review found that the first native adapter read the frame field for
every event kind, while some native non-sound callers deliberately leave that
unused field unset. The adapter now reads it only for sound events and supplies
zero otherwise, including non-sound start records. C start dispatch initializes
its unused frame output too. A supplemental native case passes a pause event
with its frame unset, outside the frozen trace; the C start fixture leaves that
output unwritten for non-sound events. Initial captures passed their comparisons
but were superseded by fresh captures after this correction. The selected native
build explicitly includes save/load and each directly invoked test executable,
including targets outside `core-checks`, before the final gate.

All 88 standalone C checks pass, including strict C89 syntax. The new fixture
also builds/runs directly with Clang `-std=c89 -pedantic-errors -Wall -Wextra
-Werror` and passes MSVC AddressSanitizer. C tests cover every event kind, failure
phase, suppression and publication order, mutable pause delay, actor/text append
rollback, weak tag fields, completed start prefixes, strict rewind boundaries,
cache replacement/clearing and camera restore exceptions. All 118 focused native,
14 SDL debug and 26 SDL release checks pass. The initial SDL release run passed
with partial overlap with the end of the native gate; its full serial rerun also
passes, avoiding shared temporary-name contention. All 51 freshly linked CLI
output baselines match. Both hosts and `core-checks` build. Marcus was stopped
before rebuilding and relaunched afterward; no Marcus UI tests were added.

Final capture executables were frozen before/after capture at SHA-256
`4A2EA84DE4F9F516B84D24782749F5EFAD494EDE58EE6D5FA89942260CB2C458`
(SDL release) and
`730E633D7488AF8564F09E3CE2DD27CC29DB2E0D3A2B90534B15FE8CFB7FD8AF`
(Marcus regression).

| Suite | Case | Captures |
| --- | --- | --- |
| `20261114T082356Z-05ce3abb` | actor-events | six retail/SDL/Marcus seek/playback complete |
| `20261114T082510Z-491b3301` | scrolling-text | six retail/SDL/Marcus seek/playback complete |
| `20261114T082811Z-4efa8400` | actor-audio | six retail/SDL/Marcus seek/playback complete |
| `20261114T082925Z-02980c65` | sampled-audio | three retail/SDL/Marcus playback complete |

All 21 final captures complete, retaining movie identity and suite integrity.
Authored frames, all 540 seek images and ordered playback image changes match
each player's preceding suites: `20261114T074843Z-1e6cf808` (actor-events),
`20261114T074956Z-622e38ae` (scrolling-text), `20261113T160754Z-ff148af2`
(actor-audio) and `20261113T161343Z-60647ee2` (sampled-audio). Playback comparison
collapses only adjacent identical position/image observations. The actor-audio
first-scene-only tone contract passes for all players. PCM16 peaks remain 3,192,
4,081 and 4,081 for actor-audio and 2,380, 2,176 and 2,176 for sampled-audio
(retail, SDL, Marcus respectively); all exceed the nonzero-output threshold.
These checks do not prove complete PCM, MIDI or retail timing parity. Existing
cross-player differences and single-repeat inconclusive comparisons remain.
Captures use a shared warm SDL package cache with private processes, settings
and movies and uncontrolled OS caching. No builds or tests ran concurrently
with either capture pass. Marcus was verified running after the final gates/captures.

Evidence: `tmp/c89-scene-events-before*.bin`, `tmp/c89-scene-events-after.bin`,
`tmp/c89-scene-events-final-*.bin`, `tmp/c89-scene-events-selected-native-build.log`,
`tmp/c89-scene-events-native-tests.log`,
`tmp/c89-scene-events-sdl-debug-tests.log`,
`tmp/c89-scene-events-sdl-release-tests.log`,
`tmp/c89-scene-events-standalone.log`, `tmp/c89-scene-events-strict.log`,
`tmp/c89-scene-events-asan.log`, `tmp/c89-scene-events-output-comparison.json`,
`tmp/c89-scene-events-playback-actor-audio-actor-events-scrolling-text-sampled-audio.json`,
`tmp/c89-scene-events-capture-hashes-*.json` and four
`tmp/c89-scene-events-capture-*.log` files. The superseded first capture pass and
its report are retained under `tmp/c89-scene-events-initial-*`.

### C89 Scene sound packets (14 November 2026)

`scene_sound.c` now owns sound-packet allocation/construction, full and group
duplication, tag append ownership, bounded sizes, wire codecs and per-tag
playback. The native SceneSoundEvent facade inherits the four-word C metadata
record directly. Its native tag/child records remain typed adapter views,
32 bytes on x64; the wire tail stays 20 bytes per tag after a 16-byte header.
Scene sound editing/undo and read/open/tag-collection/save transactions remain
native policy. `Movie::FResolveSndTag`, weak background TAG state and final
native tag/packet facade removal remain backend integration dependencies.

The baseline dispatcher/packet code is from `0d2c6658`. Packet declarations were
extracted into `inc/scene_sound.h` and the existing wire helpers exposed with an
input byte count before taking the baseline; the native decoder initially
ignored that count. Native original/repeat/converted and converted SDL debug
traces match all 8,232 bytes, SHA-256
`253373FC27FC11F42C1D1FEB45EC05A9C616F7DCE7D3E71E52C346F3930D4D51`,
FNV-1a `7C5AE3AD`. Original/converted SDL release traces match 9,904 bytes,
SHA-256 `13CBF22F5249D4C6EEBCDA190493481781CD89AD60D1BE4A68C1AA9E3B738DEB`,
FNV-1a `01174345`; release omits fault injection. Hash guards apply to the
little-endian x64 record layout. C-only behavioral checks cover either native
record stride without using that x64 fingerprint.

Twelve native cases cover one/two/four tags, looped/unlooped packets and
allocation injection. They record initialized fields, byte counts, cache
presence, real cache reference counts and wire bytes; uninitialized native
padding and pointer values are deliberately excluded. Construction, duplicate,
append, encoding and both byte-order decode allocation failures are exercised.
Group duplication is compared in retaining and cache-clearing modes. Owned
references return to one after every case. Per-tag playback resource behavior
uses C callback tests and the integration/capture gates below, rather than
this native packet trace.

Construction/full duplication retain every tag. Group duplication retains only
cache-source tags or clears every cache pointer, reacquiring its payload after
allocation. Append retains only the added tag; the caller transfers the old
prefix and frees its old allocation raw. The original group helper copied into
NULL after failed allocation. It now returns NULL, and all four existing callers
already handle that result. Supplemental native checks exercise that failure
and truncated wire input outside the frozen trace. C size/span checks reject
negative/overflowing counts and truncated data before unsafe copies. Decode
requires positive wire counts; trailing bytes remain accepted. Valid swapped
input is normalized before allocation, even when allocation then fails. Decoded
caches are unopened/NULL. Encoding preserves the full four-byte loop field and
zeros reserved wire cache words. The unused native runtime-tag byte-swap method,
which assumed 32-bit TAG layout, was removed; active normalization uses the
fixed wire layout without swapping native pointer fields.

Playback skips failed cache-source resolutions, aborts on fetch failure, ignores
enqueue failure and releases every fetched sound. Original tag index controls
queue/start offset even if an earlier tag was skipped. Tool selection is fixed
on entry; other header fields remain live. Callbacks may not resize packet
storage or reenter packet operations. Group allocation may relocate storage
without changing its contents/count; other inputs must stay valid across
allocation.

All 89 standalone C checks pass, including strict C89 syntax. The new fixture
also builds/runs directly with Clang `-std=c89 -pedantic-errors -Wall -Wextra
-Werror` and passes MSVC AddressSanitizer. C tests cover ownership transfer,
zero-tag allocation/duplication, integer-size boundaries, group relocation and
allocation failures, positive-source cache clearing, all truncation lengths,
unaligned/opposite-order decode, reserved words, noncanonical loop values,
failed resolutions/fetches/enqueues, cached tool selection, live header fields
and fetched-reference balance. All 120 selected native, 16 SDL debug and 28 SDL
release checks pass, with native/SDL suites run serially. The native selected
build explicitly includes directly invoked test executables and save/load,
including targets outside `core-checks`. Both hosts and `core-checks` build; all
51 freshly linked CLI baselines match. Marcus was stopped before rebuilding
and relaunched afterward. No Marcus UI tests were added.

Final capture executables were frozen before/after capture at SHA-256
`8B65D4B205B18B192AEB40334DCF91D4D54C7C50D1AE06EDBD2C4F0FF73E0373`
(SDL release) and
`EF8358D4978EBD9CCA0B0BFEBB104EC3F42158B526B5D816A209437B0DB9E929`
(Marcus regression).

| Suite | Case | Captures |
| --- | --- | --- |
| `20261114T090331Z-6744804a` | actor-events | six retail/SDL/Marcus seek/playback complete |
| `20261114T090445Z-3b3cf040` | scrolling-text | six retail/SDL/Marcus seek/playback complete |
| `20261114T090749Z-43d9a20f` | actor-audio | six retail/SDL/Marcus seek/playback complete |
| `20261114T090903Z-0d72bd13` | sampled-audio | three retail/SDL/Marcus playback complete |

All 21 captures complete, retaining movie identity and suite integrity. Authored
frames, all 540 seek images and ordered playback image changes match each
player's preceding Scene-dispatch suites: `20261114T082356Z-05ce3abb`
(actor-events), `20261114T082510Z-491b3301` (scrolling-text),
`20261114T082811Z-4efa8400` (actor-audio) and `20261114T082925Z-02980c65`
(sampled-audio). Playback comparison collapses only adjacent identical
position/image observations. The actor-audio first-scene-only tone contract
passes for every player. PCM16 peaks remain 3,192, 4,081 and 4,081 for actor-audio
and 2,380, 2,176 and 2,176 for sampled-audio (retail, SDL, Marcus respectively),
all exceeding the nonzero-output threshold. These checks do not establish full
PCM/MIDI or retail timing parity. Existing cross-player differences and
single-repeat inconclusive comparisons remain. Captures use a shared warm SDL
package cache with private processes/settings/movies and uncontrolled OS caching.
No builds or tests ran concurrently with capture. Marcus was verified running
after the final gates/captures.

Evidence: `tmp/c89-scene-sound-before*.bin`, `tmp/c89-scene-sound-after.bin`,
`tmp/c89-scene-sound-final-*.bin`, `tmp/c89-scene-sound-selected-native-build.log`,
`tmp/c89-scene-sound-native-tests.log`,
`tmp/c89-scene-sound-sdl-debug-tests.log`,
`tmp/c89-scene-sound-sdl-release-tests.log`,
`tmp/c89-scene-sound-standalone.log`, `tmp/c89-scene-sound-strict.log`,
`tmp/c89-scene-sound-asan.log`, `tmp/c89-scene-sound-output-comparison.json`,
`tmp/c89-scene-sound-playback-actor-audio-actor-events-scrolling-text-sampled-audio.json`,
`tmp/c89-scene-sound-capture-hashes-*.json` and four
`tmp/c89-scene-sound-capture-*.log` files.

### Scene sound editing and undo C89 closeout (14 November 2026)

Baseline: `2e13e9de`, with the private SceneUndoSound declaration extracted to
`inc/scene_sound_undo.h` before recording. `scene_sound_edit.c` now owns Scene
sound insertion/replacement/queueing/removal, query/volume, preview/background
playback, indicators and sound-undo payload state/swap control. Common MovieUndo
navigation, undo-list/object lifetime, typed groups and resource/queue primitives
remain native services. Scene loading/tag-collection/save, caption/background
policy and final facade removal remain; this completes the sound editing slice.

The native fixture records current-frame edits across three styles, queued
append, query/volume, public add/remove with undo, seven allocation-injection
settings and two four-cycle direct undo sequences. Native original/repeat/final
and final SDL debug traces match all 3,796 bytes, FNV-1a `D3F41FF2`, SHA-256
`931B0597DFFBE43D6357FB81EBC3BF1E3560C6279CDB8E14A4697F0E554ED944`.
Original/final SDL release traces match 3,812 bytes, FNV-1a `52AE7716`, SHA-256
`4AF0BAEE207B519E3EFC32AEABCA65C272024DA8B5D848DB5DAE94FB4FCB736B`.
Hash guards apply to the little-endian x64 record layout. The fixture records
initialized event/cache/undo state and real resource reference counts, excluding
pointer values, padding and unused built-in child words. Release omits debug
allocation injection.

The fixture uses a zero-byte dummy MSND with real Movie child mapping and cache
ownership. Resource-load Error 500 and injected out-of-memory Error 1 messages
are expected; this trace does not freeze successful preview audio. Successful
preview protocol is exercised by C callback tests, and actual audio by the
playback captures. Supplemental native checks outside the frozen trace verify
failed queued group replacement preserves references, failed undo capture frees
its temporary packet, and remove-with-undo handles an older-frame event prefix.
These correct unsafe native failure paths. Query list-resize failure now cleans
both temporary owners, unsupported queued restore rejects an uninitialized
packet path, and release initializes unused built-in child IDs to zero while
retaining native debug poisoning.

C-only tests cover insert/put/allocation/child/undo failures, relocated group
payloads, shared-prefix ownership, requested preview metadata versus stored
queued metadata, skipped resolutions and continued fetch failures, input/frame
snapshots, nonfatal MIDI-cache failure, query borrowed tags/publication timing,
actor indicators, unchanged background-cache volume, missing-style diagnostics,
restore fetch abort, undo swap/destructive-prefix behavior and immediate object
destruction during clear. Clock tests cover 166/333 ms truncation, negative wrap
and signed overflow. The fixture passes Clang strict C89 with warnings as errors
and MSVC AddressSanitizer.

All 90 standalone C checks (including C89 syntax), 122 selected native, 18 SDL
debug and 30 SDL release checks pass. Native and SDL suites ran serially. Both
hosts and `core-checks` build; the native selected build explicitly rebuilds
direct test executables and save/load targets outside `core-checks`. All 51
freshly linked CLI baselines match. Marcus was stopped before rebuilding and
relaunched afterward. No Marcus UI tests were added.

Final capture executables were frozen before/after capture at SHA-256
`BEAF7AC35B01BB12F3844E9DF3DBB135E690FBC61656E66D60B92A120FBF2366`
(SDL release) and
`95C072FC81D1F0FCCF10C6AB304F236D79F58A4C1FDC23553695A165881F0DD2`
(Marcus regression).

| Suite | Case | Captures |
| --- | --- | --- |
| `20261114T101927Z-f1c88b9f` | actor-events | six retail/SDL/Marcus seek/playback complete |
| `20261114T102043Z-b84a1b4e` | scrolling-text | six retail/SDL/Marcus seek/playback complete |
| `20261114T102346Z-842decc8` | actor-audio | six retail/SDL/Marcus seek/playback complete |
| `20261114T102501Z-fcf6e6de` | sampled-audio | three retail/SDL/Marcus playback complete |

All 21 captures complete, retaining movie identity and suite integrity. Authored
frames, all 540 seek images and ordered playback image changes match each
player's preceding Scene sound-packet suites: `20261114T090331Z-6744804a`
(actor-events), `20261114T090445Z-3b3cf040` (scrolling-text),
`20261114T090749Z-43d9a20f` (actor-audio) and `20261114T090903Z-0d72bd13`
(sampled-audio). Playback comparison collapses only adjacent identical
position/image observations. The actor-audio first-scene-only tone contract
passes for every player. PCM16 peaks are 3,192, 4,080 and 4,081 for actor-audio
and 2,380, 2,176 and 2,176 for sampled-audio (retail, SDL, Marcus respectively).
The SDL actor-audio peak is one count below its preceding capture; all tone and
nonzero-output checks pass. These checks do not establish full PCM/MIDI or retail
timing parity. Existing cross-player differences and single-repeat inconclusive
comparisons remain. Captures use a shared warm SDL package cache with private
processes/settings/movies and uncontrolled OS caching. No builds or tests ran
concurrently with capture. Marcus was verified running after the final captures.

Evidence: `tmp/c89-scene-sound-edit-before*.bin`,
`tmp/c89-scene-sound-edit-after.bin`, `tmp/c89-scene-sound-edit-final-*.bin`,
`tmp/c89-scene-sound-edit-selected-native-build.log`,
`tmp/c89-scene-sound-edit-native-tests.log`,
`tmp/c89-scene-sound-edit-sdl-debug-tests.log`,
`tmp/c89-scene-sound-edit-sdl-release-tests.log`,
`tmp/c89-scene-sound-edit-standalone.log`,
`tmp/c89-scene-sound-edit-strict.log`, `tmp/c89-scene-sound-edit-asan.log`,
`tmp/c89-scene-sound-edit-output-comparison.json`,
`tmp/c89-scene-sound-edit-playback-actor-audio-actor-events-scrolling-text-sampled-audio.json`,
`tmp/c89-scene-sound-edit-capture-hashes-*.json` and four
`tmp/c89-scene-sound-edit-capture-*.log` files.

### Scene loading and sound resolution C89 closeout (14 November 2026)

Baseline: `ac5cc806`. `scene_load.c` now owns the Scene load transaction formerly
in `Scene::PscenRead` and actor/frame sound-resolution traversal formerly in
`Scene::FResolveAllSndTags`. It shares the existing C owner cleanup, packet codec
and Movie sound resolver. File/group lookup, native representation conversion,
actor/caption decoding and tag opening remain host primitives. Scene tag
collection/save, other editing, caption/background policy and final C-only host
integration remain conversion work.

The native fixture makes 966 load attempts across both group byte orders, seven
scene variants and three allocation fault families (base objects, HQ and Pv).
Variants cover valid frame/start events, absent frame/start groups, missing actor
and caption children, truncated sound data and a real rich-text caption. It
records initialized metadata/name, cursor, event layouts/fields, raw camera/pause
bytes and cache reference presence/counts, excluding pointers and native padding.
Every attempt returns the real cache to its original reference count. Expected
injected allocation/read diagnostics are cleared after each attempt. This fixture
does not freeze native actor construction or thumbnail decoding; C callbacks and
the existing host/capture gates cover those integration boundaries.

Original native/repeat/final and final SDL debug traces match all 56,000 bytes,
FNV-1a `662528E5`, SHA-256
`3F80835A9F23163AC278E65240A6BBAF454A22EFC9352B9D903A4E874D1B3DA0`.
Original/final SDL release traces match 66,240 bytes, FNV-1a `8940A7C9`, SHA-256
`788936A6207DDDBA702EEBD58D9149F32B366A3B0E32DFEB050B65EF7F408FEA`.
Release omits allocation injection; hash guards apply to the little-endian x64
record layout. C-only checks use their own representation without that hash.

The load transaction retains metadata-before-construction, optional thumbnail
failure, ordered frame/start construction and final name reload. Fixed headers,
child IDs and background tags decode into local values. Camera/pause variable
bytes still pass through unchanged even for opposite-order groups. Each sound's
tags open before insertion transfers their references. Failed opening/insertion
closes only the opened prefix, in reverse order, then frees the packet and
releases the wire group before unwinding completed start/frame prefixes. Existing
C owner cleanup supplies the final destruction order.

Resolution still visits actors before locking frame storage, captures actor and
event counts once, skips non-sound events/positive-source tags and preserves
resolved prefixes on failure. The native failure path previously left its frame
group locked. It now releases every acquired lock, including failed resolution.
A native supplemental check outside the frozen load trace verifies that failure
returns with zero storage locks. C-only checks exercise actor and tag failures,
mutation prefixes, all fallible load callbacks, exact current-packet cleanup
order, embedded-reference transfer, optional thumbnails and both byte orders.
They pass Clang strict C89 with warnings as errors and MSVC AddressSanitizer.

All 91 standalone C checks (including C89 syntax), 124 selected native, 20 SDL
debug and 32 SDL release checks pass. Native/SDL suites ran serially. Both hosts
and `core-checks` build; directly invoked native test executables and save/load
targets outside `core-checks` were explicitly rebuilt. All 51 freshly linked CLI
baselines match. Marcus was stopped before rebuilding and relaunched afterward.
No Marcus UI tests were added.

Final capture executables were frozen before/after capture at SHA-256
`71DCBA7E584347791C0AD31F751974CB0A043280DE70F105C4E6E91F8442195F`
(SDL release) and
`C4C5E5908729115C23D377C905E620B3872F285D334FEDB97C9F95665D287303`
(Marcus regression).

| Suite | Case | Captures |
| --- | --- | --- |
| `20261114T104305Z-42302f89` | actor-events | six retail/SDL/Marcus seek/playback complete |
| `20261114T104421Z-f2272398` | scrolling-text | six retail/SDL/Marcus seek/playback complete |
| `20261114T104727Z-5d5ca322` | actor-audio | six retail/SDL/Marcus seek/playback complete |
| `20261114T104842Z-4ffeb94d` | sampled-audio | three retail/SDL/Marcus playback complete |

All 21 captures complete, retaining movie identity and suite integrity. Authored
frames, all 540 seek images and ordered playback image changes match each
player's preceding Scene sound-editing suites: `20261114T101927Z-f1c88b9f`
(actor-events), `20261114T102043Z-b84a1b4e` (scrolling-text),
`20261114T102346Z-842decc8` (actor-audio) and `20261114T102501Z-fcf6e6de`
(sampled-audio). Playback comparison collapses only adjacent identical
position/image observations. The actor-audio first-scene-only tone contract
passes for every player. PCM16 peaks are 3,192, 4,081 and 4,081 for actor-audio
and 2,380, 2,176 and 2,176 for sampled-audio (retail, SDL, Marcus respectively).
SDL's actor-audio peak is one count above its preceding capture; all tone and
nonzero-output checks pass. These checks do not establish full PCM/MIDI or retail
timing parity. Existing cross-player differences and single-repeat inconclusive
comparisons remain. Captures use a shared warm SDL package cache with private
processes/settings/movies and uncontrolled OS caching. No builds or tests ran
concurrently with capture. Marcus was verified running after the final captures.

The native load fixture's fingerprint guard is limited to the captured
little-endian x64, one-byte-text layout; other host layouts still run its checks.
After that test-only guard was narrowed, all three fixture executables were
rebuilt and rerun with identical final traces. Capture executable hashes remain
unchanged.

Evidence: `tmp/c89-scene-load-before*.bin`, `tmp/c89-scene-load-after.bin`,
`tmp/c89-scene-load-final-*.bin`,
`tmp/c89-scene-load-selected-native-build.log`,
`tmp/c89-scene-load-native-tests.log`, `tmp/c89-scene-load-sdl-debug-tests.log`,
`tmp/c89-scene-load-sdl-release-tests.log`, `tmp/c89-scene-load-standalone.log`,
`tmp/c89-scene-load-strict.log`, `tmp/c89-scene-load-asan.log`,
`tmp/c89-scene-load-final-*-test-build.log`,
`tmp/c89-scene-load-output-comparison.json`,
`tmp/c89-scene-load-playback-actor-audio-actor-events-scrolling-text-sampled-audio.json`,
`tmp/c89-scene-load-capture-hashes-*.json` and four
`tmp/c89-scene-load-capture-*.log` files.

### Scene tag collection and save C89 closeout (14 November 2026)

Baseline: `50d9738b`. `scene_store.c` now owns Scene-level tag traversal formerly
in `Scene::FAddTagsToTagl` and save control formerly in `Scene::FWrite`. It shares
the C sound codec/reference helpers and SceneData event/header persistence.
Native adapters still supply actor tag discovery/serialization, caption
serialization, background tag collection, thumbnail generation and typed file/
group operations. These remain final integration dependencies; other Scene/Movie
editing/undo and caption/background policy also remain conversion work.

The native fixture makes 966 collection attempts (two group byte orders, seven
variants, three allocation fault families, 23 settings) and 324 save attempts
(two byte orders, two valid scenes, three fault families, 27 settings). It freezes
sorted tag values/cache presence/child-cache flags and explicit camera child
entries, including partial results after failed collection. Actor tag discovery
reads a real actor header with a positive-source template and an empty event
group. Saved scenes include repeated sound events, camera/pause data and a real
rich-text caption. A zero-byte destination MSND exercises sound adoption and
existing-child reuse; this fixture does not decode/play that dummy sound. The
trace records save status/output chunk number, scene presence, real resource
reference counts, saved metadata/name length, child count and wire event bytes.
It excludes pointer values, padding, full actor/caption bytes and thumbnail
rendering. Source cache references balance after every attempt. Injected native
allocation/read diagnostics are expected and cleared between attempts.

Native original/repeat/final and final SDL debug traces match 161,376 bytes,
FNV-1a `81894299`, SHA-256
`3E09F2C3C588F602B3027BE264AA5370C543E2C4E65C657B92BE6498F4FA18B6`.
Original/final SDL release traces match 171,264 bytes, FNV-1a `2F550AE5`, SHA-256
`63D9B35412E8423A5D2018EFCC5AAD12B7E67F918521C5566AC18649C87AACF2`.
Release omits allocation injection. Guards apply only to the captured
little-endian x64, one-byte-text layout; other host layouts still run the fixture.

Collection keeps start-before-frame order and the last successful background for
camera children. Actor tag lists and decoded sound allocations release before
their wire groups. Collected tags borrow cache references; decoded sounds never
open theirs. Failure retains the collected prefix. Null actor lists are valid
when no error is reported; error-plus-list releases both list and group. Unknown
kinds diagnose/skip; captions and pauses are ignored. Wrong fixed-entry widths
and wrong child/background/camera payload sizes now reject the former unsafe
native copy paths. C-only tests cover these bounded failures separately; sound
decode retains its established trailing-byte acceptance.

Saving publishes nil before scene allocation and retains an allocator-modified
output on failure. Each sound duplicates references, skips adoption for existing
child bindings or absent chunk numbers, encodes/inserts and then releases its
temporary. Actor and caption child counters are independent. Background saving
may mutate the original tag before a later failure; that mutation is retained.
Events write before thumbnail update; the thumbnail is reread from C owner state
after the callback. Failed thumbnail write/adoption deletes that chunk before
the partial scene. Success releases frame/start groups; failure deletes the
scene and releases start/frame groups.

C-only tests fail every fallible callback, verify temporary reference balance,
sound adoption/reuse/absence, independent actor/caption numbering, zero wire cache
words, retained background mutations, changed/unchanged allocation outputs,
thumbnail replacement/absence/cleanup, partial collection, callback ordering,
both byte orders, null/error actor lists, missing backgrounds and malformed
payloads. They pass Clang strict C89 with warnings as errors and MSVC
AddressSanitizer.

All 92 standalone C checks (including C89 syntax), 126 selected native, 22 SDL
debug and 34 SDL release checks pass. Native/SDL suites ran serially. Both hosts
and `core-checks` build; directly invoked native test executables and save/load
targets outside `core-checks` were explicitly rebuilt. Final traces were recorded
after the SDL debug suite completed. All 51 freshly linked CLI baselines match.
Marcus was stopped before rebuilding and relaunched afterward. No Marcus UI
tests were added.

Final capture executables were frozen before/after capture at SHA-256
`ACE61323FE4C2EAB83148356F0B50D5D94413BF619DA6FD0506D92D31B64CA55`
(SDL release) and
`808C66F751B4BD4FD8FF34B453839E4C3FB3515B98DF4E4162810B8D55D3A5DD`
(Marcus regression).

| Suite | Case | Captures |
| --- | --- | --- |
| `20261114T110802Z-10a5b45a` | actor-events | six retail/SDL/Marcus seek/playback complete |
| `20261114T110917Z-e8a2bb7f` | scrolling-text | six retail/SDL/Marcus seek/playback complete |
| `20261114T111235Z-694c2aa9` | actor-audio | six retail/SDL/Marcus seek/playback complete |
| `20261114T111349Z-485fb718` | sampled-audio | three retail/SDL/Marcus playback complete |

All 21 captures complete, retaining movie identity and suite integrity. Authored
frames, all 540 seek images and ordered playback image changes match each
player's preceding Scene loading suites: `20261114T104305Z-42302f89`
(actor-events), `20261114T104421Z-f2272398` (scrolling-text),
`20261114T104727Z-5d5ca322` (actor-audio) and `20261114T104842Z-4ffeb94d`
(sampled-audio). Playback comparison collapses only adjacent identical
position/image observations. The actor-audio first-scene-only tone contract
passes for every player. PCM16 peaks are 3,192, 4,080 and 4,081 for actor-audio
and 2,380, 2,176 and 2,176 for sampled-audio (retail, SDL, Marcus respectively).
SDL's actor-audio peak is one count below its preceding capture; all tone and
nonzero-output checks pass. These checks do not establish full PCM/MIDI or retail
timing parity. Existing cross-player differences and single-repeat inconclusive
comparisons remain. Captures use a shared warm SDL package cache with private
processes/settings/movies and uncontrolled OS caching. No builds or tests ran
concurrently with capture. Marcus was verified running after the final captures.

Evidence: `tmp/c89-scene-store-before*.bin`, `tmp/c89-scene-store-after.bin`,
`tmp/c89-scene-store-final-*.bin`,
`tmp/c89-scene-store-selected-native-build.log`,
`tmp/c89-scene-store-native-tests.log`, `tmp/c89-scene-store-sdl-debug-tests.log`,
`tmp/c89-scene-store-sdl-release-tests.log`, `tmp/c89-scene-store-standalone.log`,
`tmp/c89-scene-store-strict.log`, `tmp/c89-scene-store-asan.log`,
`tmp/c89-scene-store-output-comparison.json`,
`tmp/c89-scene-store-playback-actor-audio-actor-events-scrolling-text-sampled-audio.json`,
`tmp/c89-scene-store-capture-hashes-*.json` and four
`tmp/c89-scene-store-capture-*.log` files.

### Scene pause/camera/background editing C89 closeout (14 November 2026)

Baseline: `783c0720`, with the private pause/background undo declarations moved
to `inc/scene_edit_undo.h` before recording. `scene_edit.c` now owns pause swaps,
camera coalescing, background replacement, the empty-scene predicate, public
edit/undo transactions and persistent pause/background undo payload/swap control.
Native facades supply typed groups, background resources/names, client effects
and common MovieUndo navigation/list/object lifetime. Event effects reuse the C
dispatcher. Actor/caption/selection editing, other Scene/Movie operations,
background resource policy, thumbnail generation and final C-only integration
remain conversion work.

The native fixture builds real background/light/camera/RGB/depth resources. It
records pause swaps, camera edits, public background replacements, four cycles
each of pause/camera/background undo and 57 background replacement allocation
settings (three fault families, 19 settings). Initialized metadata/name, cursor,
event fields/payloads, background camera/tag state, cache reference counts, client
pause/scene-change publications and loaded-command presence are recorded. Pointer
values, native padding and the unused background start-frame field are excluded.
The fixture includes direct current-frame changes; full seek/navigation behavior
is gated separately by the existing host/capture suites. Injected resource/read
and allocation diagnostics are expected and cleared between attempts.

Native original/repeat/final and final SDL debug traces match 9,896 bytes,
FNV-1a `D6637959`, SHA-256
`76F37792046289F2226D6050A18314FF67758E697914355EBF5FB3491A12C67D`.
Original/final SDL release traces match 9,928 bytes, FNV-1a `1DF02C29`, SHA-256
`D6E0DA7519039910DC942F7A8C0C3234D489F07BFA65FD773FD8D4D0A79C767E`.
Release omits fault injection. Hash guards apply only to the captured
little-endian x64, one-byte-text layout; other layouts still run the fixture.

Pause nil-without-an-existing-pause still inserts an event; replacement/removal
retains native dirty behavior. Camera edits publish the old value before put/play,
preserve destructive replacement/play failure prefixes, and delete a redundant
future camera without changing the current cursor. Public camera undo failure
still retries the requested camera, retaining the legacy behavior. The empty-scene
predicate uses defined signed-word frame subtraction and filters frame kinds.

Background edits publish old tags before mutation, turn off lights/drop the old
owner before fetching the new one, and attempt event/background restoration after
play failure. Dirty state and failed-restoration prefixes remain. Name changes,
stale frame removal, cursor reset, scene-change and optional loaded notifications
keep their order. The first background's unused start frame is now zero; its old
tag output is invalid/unowned, retaining native debug poisoning while defining
release's previously unused value. Public background edits without an existing
background now reject the unsupported native path before mutation; Movie callers
already use core for the first background. Background undo without a current
background also fails rather than consuming an uninitialized camera.

Two reference corrections are outside the frozen trace. Background changes now
close frame-owned sound tags before deleting stale events; cached background
sound ownership stays intact. Camera preflight tags now close after caching,
before editing. Native supplemental checks exercise real cache references for
both paths. The frame-sound check uses a dummy MSND tag solely for ownership and
does not decode/play it. C-only checks verify these corrections, output publication
timing, failed play/restoration, disabled camera masks, notification suppression,
transaction failure phases, retained prefixes, cached packet ownership and
immediate undo destruction during failure clear. Background undo switches without
seeking; camera/pause undo seek. Failure flushes before clear and accesses no undo
state afterward. Saved background tags remain borrowed, as in the original.
The C-only fixture passes Clang strict C89 with warnings as errors and MSVC
AddressSanitizer.

All 93 standalone C checks (including C89 syntax), 128 selected native, 24 SDL
debug and 36 SDL release checks pass. Native/SDL suites and final traces ran
serially. Both hosts and `core-checks` build; directly invoked native test
executables and save/load targets outside `core-checks` were explicitly rebuilt.
All 51 freshly linked CLI baselines match. Marcus was stopped before rebuilding
and relaunched afterward. No Marcus UI tests were added.

Final capture executables were frozen before/after capture at SHA-256
`F3D236ABC08723A2264B1D1A59DE62D47C3877E60FA9CD82A1E930045EFB9334`
(SDL release) and
`A419414D8AA4C9668CA1FBD65A22750E5B12201E122801821669769E26CF36DD`
(Marcus regression).

| Suite | Case | Captures |
| --- | --- | --- |
| `20261114T114014Z-dde4b028` | actor-events | six retail/SDL/Marcus seek/playback complete |
| `20261114T114132Z-8342b37b` | scrolling-text | six retail/SDL/Marcus seek/playback complete |
| `20261114T114440Z-2b342fd0` | actor-audio | six retail/SDL/Marcus seek/playback complete |
| `20261114T114554Z-a7e99894` | sampled-audio | three retail/SDL/Marcus playback complete |

All 21 captures complete, retaining movie identity and suite integrity. Authored
frames, all 540 seek images and ordered playback image changes match each
player's preceding Scene persistence suites: `20261114T110802Z-10a5b45a`
(actor-events), `20261114T110917Z-e8a2bb7f` (scrolling-text),
`20261114T111235Z-694c2aa9` (actor-audio) and `20261114T111349Z-485fb718`
(sampled-audio). Playback comparison collapses only adjacent identical
position/image observations. The actor-audio first-scene-only tone contract
passes for every player. PCM16 peaks remain 3,192, 4,080 and 4,081 for actor-audio
and 2,380, 2,176 and 2,176 for sampled-audio (retail, SDL, Marcus respectively).
All tone and nonzero-output checks pass. These checks do not establish full
PCM/MIDI or retail timing parity. Existing cross-player differences and
single-repeat inconclusive comparisons remain. Captures use a shared warm SDL
package cache with private processes/settings/movies and uncontrolled OS caching.
No builds or tests ran concurrently with capture. Marcus was verified running
after the final captures.

Evidence: `tmp/c89-scene-edit-before*.bin`, `tmp/c89-scene-edit-after.bin`,
`tmp/c89-scene-edit-final-*.bin`,
`tmp/c89-scene-edit-selected-native-build.log`,
`tmp/c89-scene-edit-native-tests.log`, `tmp/c89-scene-edit-sdl-debug-tests.log`,
`tmp/c89-scene-edit-sdl-release-tests.log`, `tmp/c89-scene-edit-standalone.log`,
`tmp/c89-scene-edit-strict.log`, `tmp/c89-scene-edit-asan.log`,
`tmp/c89-scene-edit-output-comparison.json`,
`tmp/c89-scene-edit-playback-actor-audio-actor-events-scrolling-text-sampled-audio.json`,
`tmp/c89-scene-edit-capture-hashes-*.json` and four
`tmp/c89-scene-edit-capture-*.log` files.

### Scene actor/caption editing C89 closeout (14 November 2026)

Baseline: `ff121dcd`, with the private caption undo declaration extracted to
`inc/scene_content_undo.h` before recording. `scene_content.c` now owns Scene-level
actor/caption add/remove, actor on-stage/paste control, actor/caption lookup,
bulk roll-call traversal, movie rebinding and persistent actor/caption undo
ownership/control. Generic selection/UI, actor/caption internals, typed group
operations and common MovieUndo navigation/list/object lifetime remain adapters.
Scene selection, chop/other editing, other Movie operations and final C-only
integration remain conversion work.

The native fixture uses a minimal in-memory template/action reader, real native
body construction, resource-cache references and real background/camera/RGB/depth
resources. It freezes actor add/replacement/removal, replacement/add undo cycles,
real hidden-caption core/public add/remove and 48 actor-container allocation
settings (three fault families, 16 settings). Bodies bind before container fault
injection. The first unprepared-actor experiment exposed a native null-body crash
on later removal; that failure is covered separately below. Container allocation
can retry a failed speculative allocation, so injected diagnostics do not imply
a changed transaction outcome. Native caption undo navigation and visible caption
views are not frozen by this fixture; C-only protocol tests and existing host/
playback gates provide that coverage.

Initialized Scene metadata, object IDs/reference counts, start/list membership,
selection presence, caption lifetimes, cache reference counts and client effects
are recorded. Unused start-event frames, padding and pointer values are excluded.
Original native/repeat/final, original/final SDL release and final SDL debug
traces all match 5,020 bytes, FNV-1a `2F3EEF19`, SHA-256
`C6D1B2EB7DE593E0BF638272E5B6ADCDA26013CDA9C2C0B4CB18F7471FB9012D`.
Release omits allocation injection. Guards apply only to the captured
little-endian x64, one-byte-text layout; other layouts still run the checks.

Replacement retains/binds/seeks before publishing both actor pointers, then
selects before hiding/releasing the old actor. New actors insert before retaining,
registering and dispatching. Binding/ID changes and completed prefixes survive
later failure. Redo-add records the ID assigned during roll-call insertion.
Actor paste and bulk roll-call registration preserve their extra range/seek/client
ordering and reverse-prefix rollback. Caption binding precedes insertion; removal
clears selection before lookup, removes entries and seeks before lifetime start
before releasing the group reference. Start events now write zero to their unused
frame. Caption undo rebinds by index after scene changes, preserves asymmetric
redo/undo seek restoration and keeps a failed final seek after add nonfatal.

Failure corrections are separate from the frozen trace: failed caption dispatch
releases its newly retained reference; failed actor undo commit releases its undo
record; failed actor replacement frees its temporary duplicate. Caption undo no
longer releases a borrowed index result on failed removal. Missing lookups and
invalid lifetimes fail with bounded cleanup instead of uninitialized/unbounded
access. Orphaned start references close if the matching list entry is missing.
An actor whose body allocation failed now rejects before commit, avoiding the
later null-body removal crash. Actor replacement removes stale old-ID selection
extras before releasing the old actor, including with no view. Movie rebinding
updates the C owner before publication, preventing a later owner publish from
restoring the old Movie pointer. Native supplemental checks verify body-allocation
failure, failed caption playback reference balance, headless selection extras and
movie rebinding outside the frozen trace.

C-only tests cover container/registration/seek/body readiness failures, ownership
transfer, replacement selection/hide/release order, extra-selection cleanup,
on-stage rollback, paste, roll-call prefix rollback, caption lifetime/commit
failures, bounded indexing, assigned actor IDs, replacement duplicate cleanup,
caption identity rebinding and both undo directions/navigation asymmetries. Undo
payloads release before publishing NULL; failure flushes before clear, which may
destroy the record, and no state is accessed afterward. The fixture passes Clang
strict C89 with warnings as errors and MSVC AddressSanitizer.

All 94 standalone C checks (including C89 syntax), 130 selected native, 26 SDL
debug and 38 SDL release checks pass. Native/SDL suites and final traces ran
serially. Both hosts and `core-checks` build; directly invoked native test
executables and save/load targets outside `core-checks` were explicitly rebuilt.
Concurrent SDL resource compilation failed on file creation; the final SDL
resource builds ran serially and passed. All 51 freshly linked CLI baselines
match. Marcus was stopped before rebuilding and relaunched afterward. No Marcus
UI tests were added.

The final binaries stayed fixed through all 21 fresh-process captures (one
repeat, no concurrent builds/tests). SHA-256 before/after:

- SDL release: `589F5C0679D8D94B0979D4C7BACFD6B1469951C6D37A8A84CB12F9D06CE74FCA`.
- Marcus regression: `4F33C7901B2E33B98B2110E8BAA12D57D4EF86784755B0D6949C2F8B6D7584CD`.

| Final suite | Case | Completion |
| --- | --- | --- |
| `20261114T122500Z-e64c5843` | actor-events | six retail/SDL/Marcus seek/playback complete |
| `20261114T122619Z-f0793164` | scrolling-text | six retail/SDL/Marcus seek/playback complete |
| `20261114T123021Z-a339a009` | actor-audio | six retail/SDL/Marcus seek/playback complete |
| `20261114T123145Z-037e0ed2` | sampled-audio | three retail/SDL/Marcus playback complete |

Movie identity and suite integrity pass. All authored frame sequences and all
540 seek images match each player's preceding Scene-editing capture:
`20261114T114014Z-dde4b028` (actor-events), `20261114T114132Z-8342b37b`
(scrolling-text), `20261114T114440Z-2b342fd0` (actor-audio) and
`20261114T114554Z-a7e99894` (sampled-audio). Playback comparison collapses only
adjacent identical scene/frame/image samples. SDL and Marcus ordered image
changes match for every case. Retail actor-events and both audio cases match.
Retail scrolling playback differs: run 1493 contains 326 changes versus run
1472's 328, with exactly two intermediate images at scene 0/frame 3 omitted.
The new sequence is an ordered subsequence of the old, with no added image;
retail's executable is unchanged. This capture-sampling difference is retained
explicitly in the report, not counted as an exact playback-image match.

Actor-audio PCM16 peaks are 3192/4081/4081 and sampled-audio peaks are
2380/2176/2176 (retail/SDL/Marcus). All exceed 50; actor tone detection and the
first-scene-only contract pass in all three players. These checks do not prove
full PCM/MIDI or retail timing parity. Existing cross-player differences and
single-repeat inconclusive comparisons remain. Captures use a shared warm SDL
package cache with private processes, settings and working movies; OS caching
is uncontrolled.

Evidence: `tmp/c89-scene-content-{strict,asan,standalone,native-tests,sdl-debug-tests,sdl-release-tests}.log`,
`tmp/c89-scene-content-final-{native,sdl-debug,sdl-release}.bin`,
`tmp/c89-scene-content-output-comparison.json`,
`tmp/c89-scene-content-capture-{actor-events,scrolling-text,actor-audio,sampled-audio}.log`,
`tmp/c89-scene-content-capture-hashes-{before,after}.json` and
`tmp/c89-scene-content-playback-actor-audio-actor-events-scrolling-text-sampled-audio.json`.

### Scene selection C89 closeout (14 November 2026)

Baseline: `dc7591dc`. `scene_selection.c` now owns borrowed primary actor/caption
selection state and plain select/clear, count/index/membership, toggle/promotion,
select-all, tag/shared-tag selection and visible centroid control. Native primary
fields are published typed views; content editing reads/writes the same C state.
Container access/allocation, counted display names, body/view queries, highlights,
caption display effects and Movie menu/client callbacks remain typed services.

The Scene-content native fixture was extended before extraction with three real
actors and named tags. It records primary promotion, extra removal, clear,
select-all, tag/shared-tag queries, missing tags, null caption selection,
membership/count/index and centroid results in a headless Movie. It also retains
the preceding actor/caption editing, undo, ownership and container-fault checks.
The original and repeated native traces match 5,552 bytes, FNV-1a `3D1499BA`,
SHA-256 `CFA6C7EA4588A99F454A4DB0CBC691BB793D8B3E4FFBC40554F51F4AF633797F`.
The fixture checks that typed primary views and public C-backed getters agree.
Native view-dependent highlight/caption selection is not frozen by this headless
fixture; C-only effect-order tests and the existing host/playback gates cover it.
The fingerprint guard retains the little-endian x64, one-byte-text restriction.

Plain actor selection clears extra selections and caption effects only in an
actor-mode view. No-view/text-mode calls preserve extras; membership can still
find an extra while a null primary makes the selected count zero. Toggling
searches extras before installing a missing primary. Removing the primary
promotes the first extra without another highlight. Caption selection preserves
the actor pointer and invalidate/menu/clear ordering; its already-selected/null
early return skips caption publication and client notification. New allocation
and append failures preserve completed prefixes and an allocated empty list.

Tag selection retains ASCII folding and whole-token suffix boundaries, counted
byte/UTF-16 query units, acceptance of punctuation/non-ASCII query units and the
native 63-unit shared-seed truncation. Null actors are skipped by tag traversal.
Centroids skip null/missing-body/offscreen objects, use selection-order wrapped
16.16 addition/division and leave outputs untouched with no visible selection.
Invalid C selection indices now return null; native debug range checks remain.
There are no new Marcus UI tests. Scene chop/editing/undo, actor/caption internals,
other Scene/Movie operations and final C-only integration remain conversion work.

The final native, SDL debug and SDL release traces match the original/repeat
trace above byte for byte. All 95 standalone C checks (including C89 syntax),
131 selected native, 27 SDL debug and 39 SDL release checks pass. Final builds
and suites ran serially; `core-checks`, directly invoked native test/save-load
executables, both hosts and the CLI tools were rebuilt. Marcus was stopped
before rebuilding and relaunched afterward. Clang strict C89 compilation with
warnings as errors and MSVC AddressSanitizer pass the selection fixture. All 51
freshly linked CLI baselines match.

The final binaries stayed fixed through all 21 fresh-process captures (one
repeat, no concurrent builds/tests). SHA-256 before/after:

- SDL release: `CC4F22D81E288761E62385251AA097203607DCF863B6D188F684EC651F81E0C7`.
- Marcus regression: `13B41D8B78FD31DF3B73B8328CE13C18F3DF4F90F4FDA27ACFFA9B5621AFD01E`.

| Final suite | Case | Completion |
| --- | --- | --- |
| `20261114T125156Z-4fc55e94` | actor-events | six retail/SDL/Marcus seek/playback complete |
| `20261114T125314Z-50c18722` | scrolling-text | six retail/SDL/Marcus seek/playback complete |
| `20261114T125628Z-eb8ae07a` | actor-audio | six retail/SDL/Marcus seek/playback complete |
| `20261114T125804Z-dea50439` | sampled-audio | three retail/SDL/Marcus playback complete |

Movie identity and suite integrity pass. All authored frame sequences and all
540 seek images match each player's preceding Scene-content capture:
`20261114T122500Z-e64c5843` (actor-events), `20261114T122619Z-f0793164`
(scrolling-text), `20261114T123021Z-a339a009` (actor-audio) and
`20261114T123145Z-037e0ed2` (sampled-audio). Playback comparison collapses only
adjacent identical scene/frame/image samples. All SDL/Marcus playback sequences
match. Retail actor-events and both audio cases match. Retail scrolling run 1514
contains 328 changes versus run 1493's 326: it includes exactly the two images at
scene 0/frame 3 omitted in the preceding capture. Run 1493 is an ordered
subsequence of 1514, and 1514 matches earlier retail run 1472 exactly. This
sampling difference in the unchanged retail player remains explicit in the
report; it is not counted as an exact match against run 1493.

Actor-audio PCM16 peaks are 3192/4081/4081 and sampled-audio peaks are
2380/2176/2176 (retail/SDL/Marcus). All exceed 50; actor tone detection and the
first-scene-only contract pass for every player. These checks do not prove full
PCM/MIDI or retail timing parity. Existing cross-player differences and
single-repeat inconclusive comparisons remain. Captures use a shared warm SDL
package cache with private processes, settings and working movies; OS caching
is uncontrolled.

Evidence: `tmp/c89-selection-{strict-build,strict,asan,standalone,native-tests,sdl-debug-tests,sdl-release-tests}.log`,
`tmp/c89-selection-{native,native-repeat,final-native,final-sdl-debug,final-sdl-release}.bin`,
`tmp/c89-selection-output-comparison.json`,
`tmp/c89-selection-capture-{actor-events,scrolling-text,actor-audio,sampled-audio}.log`,
`tmp/c89-selection-capture-hashes-{before,after}.json` and
`tmp/c89-selection-playback-actor-audio-actor-events-scrolling-text-sampled-audio.json`.

### Scene chop/snapshot undo C89 closeout (14 November 2026)

Baseline: `0b19fbd3`, with the private chop undo declaration extracted to
`inc/scene_chop_undo.h` before recording. `scene_chop.c` now owns forward/backward
chop traversal, event pruning, frame limits, background-sound owner cleanup,
public snapshot undo transactions and persistent snapshot resource/save/swap
control. Actor/caption internals, typed group/object primitives, serialization,
Movie scene-list/navigation and menu/client effects remain adapters.

The native Scene-content fixture now includes actual empty-Scene save and four
alternating do/undo snapshot swaps, public forward/backward chop transactions,
camera/current-frame event retention and both directions of real actor trim.
The preceding selection, actor/caption editing, undo and allocation checks remain.
Original/repeat native traces match 6,340 bytes, FNV-1a `6FDCBF8B`, SHA-256
`7B203D16891AE5ECB3C0FB6980E63F24BDD6C801BFC77C1ACD665D848BCCFF3F`.
The fingerprint guard retains the little-endian x64, one-byte-text restriction.

Snapshot replay with a synthetic embedded-background tag exposed the native
null-cache fetch path; the actual frozen snapshot cycle uses an empty Scene.
Visible caption chopping attempted screen-gob creation in the headless fixture,
so caption branch/effect coverage is C-only. These fixture limits are not claims
of native background/caption replay parity. Existing host/save-load and retail
capture gates remain the wider integration evidence. Native supplemental checks
verify both chop directions close retained sound-event tag references, outside
the frozen trace. Relevant investigation logs are
`tmp/c89-chop-native-crash.log` and `tmp/c89-chop-caption-assert.log`.

Forward chop preserves its cursor and extends overlapping caption lifetimes via
seek-to-infinity/show/restore. Backward chop retains the newest past camera and
all current-frame events, then recomputes the cursor and drops a cached background
sound older than the new first frame. The diagnostic background frame retains
native debug poison/release no-op behavior. Removed sound tags now close before
frame-event erasure. Failed caption lifetime queries skip/diagnose instead of
reading undefined endpoints; failed removals that leave their object in place
advance instead of stalling. These corrections are separate from the frozen
successful behavior trace.

Snapshot resources stay owned after failed writes and release before NULL during
disposal. Save/create/commit precede editing. Swap preserves the read/play/switch/
retain/insert/close/delete/rewind/write/close/remove ordering and destructive
failure prefixes. Failed final switch/seek remain nonfatal. Failure flushes before
clear, which may destroy the undo record, then ends the long operation through
independent copied context. C-only tests destroy the record during clear and cover
allocation/save/commit and every fallible swap phase, caption lifetime/removal/
seek/show behavior, camera pruning, sound-tag closing and cached sound cleanup.
No Marcus UI tests were added. Remaining Scene name/transition/playback setup,
other Movie operations and final C-only integration remain conversion work.

Final native, SDL debug and SDL release traces match the original/repeat trace
above byte for byte. All 96 standalone C checks (including C89 syntax), 132
selected native, 28 SDL debug and 40 SDL release checks pass. Builds and file-heavy
suites ran serially; both hosts, `core-checks`, directly invoked native/save-load
executables and CLI targets were rebuilt. Marcus was stopped before rebuilding
and relaunched afterward. The C fixture passes Clang strict C89 with warnings as
errors and MSVC AddressSanitizer. All 51 freshly linked CLI baselines match.

The final binaries stayed fixed through all 21 fresh-process captures (one
repeat, no concurrent builds/tests). SHA-256 before/after:

- SDL release: `D5AF52AAF68F36C2B6C2DD64E4F6FAEB55419EC0A86476E0C34280472E6124E5`.
- Marcus regression: `39AF5769313DA68B0003305A063750B7D1E9E5061D726316E881C350B43AF64B`.

| Final suite | Case | Completion |
| --- | --- | --- |
| `20261114T131923Z-e57148d7` | actor-events | six retail/SDL/Marcus seek/playback complete |
| `20261114T132041Z-33e71421` | scrolling-text | six retail/SDL/Marcus seek/playback complete |
| `20261114T132404Z-020f3eb7` | actor-audio | six retail/SDL/Marcus seek/playback complete |
| `20261114T132523Z-1cbe0905` | sampled-audio | three retail/SDL/Marcus playback complete |

Movie identity and suite integrity pass. All authored frame sequences, all 540
seek images and ordered playback image changes match each player's preceding
Scene-selection capture: `20261114T125156Z-4fc55e94` (actor-events),
`20261114T125314Z-50c18722` (scrolling-text), `20261114T125628Z-eb8ae07a`
(actor-audio) and `20261114T125804Z-dea50439` (sampled-audio). Playback comparison
collapses only adjacent identical scene/frame/image samples. The preceding
retail scrolling sampling omission did not recur against this baseline.

Actor-audio PCM16 peaks are 3192/4081/4081 and sampled-audio peaks are
2380/2176/2176 (retail/SDL/Marcus). All exceed 50; actor tone detection and the
first-scene-only contract pass for every player. These playback regression checks
do not exercise retail editing commands or prove full PCM/MIDI or retail timing
parity. Existing cross-player differences and single-repeat inconclusive
comparisons remain. Captures use a shared warm SDL package cache with private
processes, settings and working movies; OS caching is uncontrolled.

Evidence: `tmp/c89-chop-{strict,asan,standalone,native-tests,sdl-debug-tests,sdl-release-tests}.log`,
`tmp/c89-chop-{native,native-repeat,final-native,final-sdl-debug,final-sdl-release}.bin`,
`tmp/c89-chop-output-comparison.json`,
`tmp/c89-chop-capture-{actor-events,scrolling-text,actor-audio,sampled-audio}.log`,
`tmp/c89-chop-capture-hashes-{before,after}.json` and
`tmp/c89-chop-playback-actor-audio-actor-events-scrolling-text-sampled-audio.json`.

### Scene property/playback lifecycle C89 closeout (14 November 2026)

Baseline: `80f4deb4`, with private property undo declarations extracted to
`inc/scene_control_undo.h` before recording. `scene_control.c` owns Scene name/
transition edit transactions, persistent C property undo payloads/swaps and
playback startup/shutdown traversal. `scene_data.c` now also owns transition-file
read/modify/write. Typed String/enum/file/group/object/view primitives and common
Movie undo/navigation effects remain adapters.

The Scene-content native fixture extends the preceding actor/caption/selection/
chop/undo and fault checks with four explicit name swaps and four transition
swaps, public edits, transition-file edits/readback (including invalid transition
and missing chunk), and startup/shutdown on a background Scene with real actors.
Original/repeat native traces match 6,748 bytes, FNV-1a `129FEB8A`, SHA-256
`1ADB16AEABA0D06DBF53F8D2E65446746158CFF805E915DD8F43D69B2F2E9FFB`.
The guard retains the little-endian x64, one-byte-text restriction. Visible caption
scroll/cleanup branches are not frozen in this headless fixture; C-only protocol
checks and the full playback captures provide that integration evidence.

Failure corrections are outside the frozen trace: failed name/transition commit
releases its local undo record, and public transition edit now captures the old
transition before commit. Supplemental native undo/redo verifies the transition
correction. The frozen explicit-payload transition undo cycles retain the original
seek/swap behavior. Missing caption startup views now diagnose/skip rather than
dereference null.

Name edits retain counted native text and introduce no additional dirty/menu/
client effects. Name undo switches without seeking; transition undo switches then
seeks. Both obtain current Scene data after navigation and swap persistent C
payloads. Failures flush before clear, which may destroy the record; no state
access follows clear. Playback startup refreshes actor lifetimes before caption
work, stops at first failure, scrolls visible stories/nonstories starting now and
forces prerendering. Stop ends prerendering before cleaning captions. Transition
file helpers retain full metadata/name read/write order and committed header
prefixes when name publication fails.

C-only tests cover undo allocation/commit failure, release-before-publication,
Scene identity after navigation, both undo failure paths with immediate record
destruction, empty/full byte/UTF-16 payloads, actor failure short-circuiting,
caption story/visibility/lifetime branches, missing views, prerender/cleanup
order, both file byte orders/text units and partial header/name write failures.
No Marcus UI tests were added. Scene thumbnails/dirty propagation, other Movie
operations and final C-only integration remain conversion work.

Final native, SDL debug and SDL release traces match the original/repeat trace
above byte for byte. All 97 standalone C checks (including C89 syntax), 133
selected native, 29 SDL debug and 41 SDL release checks pass. Builds and file-heavy
suites ran serially; both hosts, `core-checks`, directly invoked native/save-load
executables and CLI targets were rebuilt. Marcus was stopped before rebuilding
and relaunched afterward. The C fixture passes Clang strict C89 with warnings as
errors and MSVC AddressSanitizer. All 51 freshly linked CLI baselines match.

The final binaries stayed fixed through all 21 fresh-process captures (one
repeat, no concurrent builds/tests). SHA-256 before/after:

- SDL release: `C50B4BB27F6EAEFA240CDC3D819334114332C616BBE0196E0A479E3E9714140E`.
- Marcus regression: `9433A602620AC6DC74559A8B75F4CBA7C328BEDDBEBDC56CA05F9D64FD7BD202`.

| Final suite | Case | Completion |
| --- | --- | --- |
| `20261114T134541Z-c5e78336` | actor-events | six retail/SDL/Marcus seek/playback complete |
| `20261114T134700Z-488328cb` | scrolling-text | six retail/SDL/Marcus seek/playback complete |
| `20261114T135017Z-62e77d64` | actor-audio | six retail/SDL/Marcus seek/playback complete |
| `20261114T135136Z-72c2d768` | sampled-audio | three retail/SDL/Marcus playback complete |

Movie identity and suite integrity pass. All authored frame sequences, all 540
seek images and ordered playback image changes match each player's preceding
Scene-chop capture: `20261114T131923Z-e57148d7` (actor-events),
`20261114T132041Z-33e71421` (scrolling-text), `20261114T132404Z-020f3eb7`
(actor-audio) and `20261114T132523Z-1cbe0905` (sampled-audio). Playback comparison
collapses only adjacent identical scene/frame/image samples.

Actor-audio PCM16 peaks are 3192/4080/4081 and sampled-audio peaks are
2380/2176/2176 (retail/SDL/Marcus). All exceed 50; actor tone detection and the
first-scene-only contract pass for every player. The SDL actor peak differs by
one PCM16 unit from the preceding capture; this is not a full PCM equality test.
These playback regression checks do not exercise retail editing commands or
prove full PCM/MIDI or retail timing parity. Existing cross-player differences
and single-repeat inconclusive comparisons remain. Captures use a shared warm
SDL package cache with private processes, settings and working movies; OS caching
is uncontrolled.

Evidence: `tmp/c89-control-{strict-build,strict,asan,standalone,native-tests,sdl-debug-tests,sdl-release-tests}.log`,
`tmp/c89-control-{native,native-repeat,final-native,final-sdl-debug,final-sdl-release}.bin`,
`tmp/c89-control-output-comparison.json`,
`tmp/c89-control-capture-{actor-events,scrolling-text,actor-audio,sampled-audio}.log`,
`tmp/c89-control-capture-hashes-{before,after}.json` and
`tmp/c89-control-playback-actor-audio-actor-events-scrolling-text-sampled-audio.json`.

### Scene thumbnail/dirty C89 closeout (14 November 2026)

Baseline: `b9ef2139`. `scene_thumbnail.c` now owns thumbnail generation/cache
access and dirty propagation. Typed indexed-port creation, palette/view drawing,
pixel scaling/locking, bitmap construction and Movie dirty effects remain
adapters. Thumbnail ownership remains in the existing C Scene owner.

The native Scene-content fixture adds an actual offscreen MovieView, grayscale
palette and two rendered thumbnail images (144 by 81 pixels). It records decoded
pixels, bitmap wire sizes, old-owner reference counts across replacement,
clean-cache reuse, missing-view access, current frame/effect-mask restoration and
dirty false/true behavior. The preceding content/selection/chop/property/playback
and allocation checks remain. Original native/repeat and original SDL debug/
release traces all match 30,192 bytes, FNV-1a `21EF5458`, SHA-256
`355429DDD4C885DF9DC63AF3C0C2668AF9A9A79C22934D55B2242DF0E78559F7`.
The guard retains the little-endian x64, one-byte-text restriction. The view is an
engine offscreen fixture; no Marcus UI tests were added. Actual view integration
covers the actor-mode/null-primary path; remaining selection/error branches are
covered directly through the C services.

Thumbnail generation preserves saved-time/mute/view/gate/port/palette/mask/seek/
deselect/draw/restore/copy/drop/construct/publish/unlock/release/seek/mask/end-copy/
resume ordering. Graphics environments construct in stack placement storage,
retain/lock their ports across local releases and frame/mask restoration, then
destroy in reverse order before sound resumes. The bridge introduces no heap
allocation for those environments. The old thumbnail releases before constructing
its replacement, including on failure; empty ownership publishes before unlock.
Initial-seek failure keeps its partial frame change and restores masks/ports;
later frame restoration is nonfatal and only occurs for a saved frame in
`(first,last]`. Deselect/restore query mode separately and retain existing primary-
only/extra-selection behavior. Every exit resumes sound at the saved time. The
thumbnail accessor returns a borrowed C-owner pointer after updating. Dirty false
and detached owners are no-ops; attached owners use the existing SetDirty effect.

C-only tests cover background/view/clean gates, both port allocation failures,
initial/restoration seek failures, bitmap failure, actor/text/mode-changing
selection, masks/time restoration, old-owner release and publish-before-unlock,
port references across graphics-environment lifetime, saved-frame boundary cases
and detached/false/attached dirty propagation. Remaining Scene background-tag
query/glue audit, background resource policy, other Movie operations and final
C-only integration remain conversion work.

Final native, SDL debug and SDL release traces match all four original traces
above byte for byte. All 98 standalone C checks (including C89 syntax), 134
selected native, 30 SDL debug and 42 SDL release checks pass. Builds and file-heavy
suites ran serially; both hosts, `core-checks`, directly invoked native/save-load
executables and CLI targets were rebuilt. Marcus was stopped before rebuilding
and relaunched afterward. The C fixture passes Clang strict C89 with warnings as
errors and MSVC AddressSanitizer. All 51 freshly linked CLI baselines match.

The final binaries stayed fixed through all 21 fresh-process captures (one
repeat, no concurrent builds/tests). SHA-256 before/after:

- SDL release: `E85E7CEC07885CA724FE73CBC646CCFB26270004E8C4A2E0D26351000F1DDA04`.
- Marcus regression: `C16A0D1CEB6D99DDDE6B30AD65D93E11AF6BD36482123580BC8A45E8CAA70DE8`.

| Final suite | Case | Completion |
| --- | --- | --- |
| `20261114T141441Z-84e297e2` | actor-events | six retail/SDL/Marcus seek/playback complete |
| `20261114T141559Z-e36e5bce` | scrolling-text | six retail/SDL/Marcus seek/playback complete |
| `20261114T141915Z-96939dcb` | actor-audio | six retail/SDL/Marcus seek/playback complete |
| `20261114T142032Z-128eb9ec` | sampled-audio | three retail/SDL/Marcus playback complete |

Movie identity and suite integrity pass. All authored frame sequences, all 540
seek images and ordered playback image changes match each player's preceding
Scene-control capture: `20261114T134541Z-c5e78336` (actor-events),
`20261114T134700Z-488328cb` (scrolling-text), `20261114T135017Z-62e77d64`
(actor-audio) and `20261114T135136Z-72c2d768` (sampled-audio). Playback comparison
collapses only adjacent identical scene/frame/image samples.

Actor-audio PCM16 peaks are 3192/4081/4081 and sampled-audio peaks are
2380/2176/2176 (retail/SDL/Marcus). All exceed 50; actor tone detection and the
first-scene-only contract pass for every player. The SDL actor peak differs by
one PCM16 unit from the preceding capture; this is not a full PCM equality test.
These playback checks do not exercise retail thumbnail editing or prove full
PCM/MIDI or retail timing parity. Existing cross-player differences and
single-repeat inconclusive comparisons remain. Captures use a shared warm SDL
package cache with private processes, settings and working movies; OS caching is
uncontrolled.

Evidence: `tmp/c89-thumbnail-{strict-build,strict,asan,standalone,native-tests,sdl-debug-tests,sdl-release-tests}.log`,
`tmp/c89-thumbnail-{native,native-repeat,original-sdl-debug,original-sdl-release,final-native,final-sdl-debug,final-sdl-release}.bin`,
`tmp/c89-thumbnail-output-comparison.json`,
`tmp/c89-thumbnail-capture-{actor-events,scrolling-text,actor-audio,sampled-audio}.log`,
`tmp/c89-thumbnail-capture-hashes-{before,after}.json` and
`tmp/c89-thumbnail-playback-actor-audio-actor-events-scrolling-text-sampled-audio.json`.

### Scene query/group-gate C89 closeout (14 November 2026)

Baseline: `cc138746`. `scene_events.c` now owns first-background-start lookup and
click-to-actor traversal. `scene_data.c` now owns full/header-only loaded-group
acceptance, failure release and byte-order publication. Typed tag/short/object
conversions, world picking/body ownership, group decoding and file I/O remain
adapters. The Scene bridge audit is recorded in `docs/code-map/library-split.md`;
native debug/object/storage/front-end dependencies remain final integration work.

The native Scene-content fixture extends the preceding content/selection/chop/
property/playback/thumbnail evidence with borrowed background-tag lookup, unchanged
absence output despite a live background, a real world-pick miss/body-part output,
native/foreign collection serialization/readback, valid/invalid Scene layout,
optional byte-order output and backend read failure. Original native methods were
restored for the final baseline after the picking traversal was found by the
broader branch audit. Original native/repeat and original SDL debug/release traces
match 30,296 bytes, FNV-1a `2B757265`, SHA-256
`3DE81D80C7D261068B4F1281D75E6B6E2C072ED47E19785BCAC3303E6B603582`.
The guard retains the little-endian x64, one-byte-text restriction.

Background lookup stops at the first matching start event without fetching or
retaining its cache and does not fall back to the live background object. Picking
preserves the backend's part-output prefix, skips all actor work on a miss,
returns the first body owner and diagnoses an orphan without selection/lifetime
effects. Positive/orphan picking uses C-only protocol tests; the native trace
freezes the existing real backend's miss.

Loaded groups retain foreign-order Scene payloads after collection metadata is
decoded. Full acceptance checks order marker and start/frame schema, transfers
ownership on success and publishes optional order after validation. Failure
releases once and keeps output unchanged. Tag collection uses header-only mode:
it retains the original width check and acceptance of unknown events/payloads for
the collector's own diagnostics/prefix behavior, rather than imposing the full
schema validator. No wire versions, payload normalization or additional tag/cache
references were introduced.

C-only tests cover empty/duplicate background starts and absence output, picking
miss/first-owner/orphan/empty-list paths, forwarded coordinates and part output,
both host/order combinations, start/frame/header-only acceptance, read/marker/
layout failures, optional order, failure release and unchanged wire bytes. No
Marcus UI tests were added. Background resource policy, native object/facade
dependencies, Movie operations and final C-only integration remain work.

Final native, SDL debug and SDL release traces match all four original traces
above byte for byte. All 98 standalone C checks (including C89 syntax), 134
selected native, 30 SDL debug and 42 SDL release checks pass with the expanded
fixtures. Builds and file-heavy suites ran serially; both hosts, `core-checks`,
directly invoked native/save-load executables and CLI targets were rebuilt.
Marcus was stopped before rebuilding and relaunched afterward. Both expanded C
fixtures pass Clang strict C89 with warnings as errors and MSVC AddressSanitizer.
All 51 freshly linked CLI baselines match.

The final binaries stayed fixed through all 21 fresh-process captures (one
repeat, no concurrent builds/tests). SHA-256 before/after:

- SDL release: `F7E6EFE7BC52CAB5BC8379626697282ECF9EC181714F374A0B4C2F1C79A35E5C`.
- Marcus regression: `662C08B12D4E8E35479037432632C1B3413F243B9DE5F6AE126BD74AB7D022D3`.

| Final suite | Case | Completion |
| --- | --- | --- |
| `20261114T145218Z-815dfe36` | actor-events | six retail/SDL/Marcus seek/playback complete |
| `20261114T145333Z-67e62dbd` | scrolling-text | six retail/SDL/Marcus seek/playback complete |
| `20261114T145639Z-eaaf8c65` | actor-audio | six retail/SDL/Marcus seek/playback complete |
| `20261114T145759Z-cfa20af2` | sampled-audio | three retail/SDL/Marcus playback complete |

Movie identity and suite integrity pass. All authored frame sequences, all 540
seek images and ordered playback image changes match each player's preceding
Scene-thumbnail capture: `20261114T141441Z-84e297e2` (actor-events),
`20261114T141559Z-e36e5bce` (scrolling-text), `20261114T141915Z-96939dcb`
(actor-audio) and `20261114T142032Z-128eb9ec` (sampled-audio). Playback comparison
collapses only adjacent identical scene/frame/image samples.

Actor-audio PCM16 peaks are 3192/4081/4081 and sampled-audio peaks are
2380/2176/2176 (retail/SDL/Marcus). All exceed 50; actor tone detection and the
first-scene-only contract pass for every player. These playback checks do not
exercise retail picking/editing commands or prove full PCM/MIDI or retail timing
parity. Existing cross-player differences and single-repeat inconclusive
comparisons remain. Captures use a shared warm SDL package cache with private
processes, settings and working movies; OS caching is uncontrolled.

Evidence: `tmp/c89-scene-glue-{events-strict-build,events-strict,data-strict-build,data-strict,asan,standalone,native-tests,sdl-debug-tests,sdl-release-tests}.log`,
`tmp/c89-scene-glue-{native,native-repeat,original-sdl-debug,original-sdl-release,final-native,final-sdl-debug,final-sdl-release}.bin`,
`tmp/c89-scene-glue-output-comparison.json`,
`tmp/c89-scene-glue-capture-{actor-events,scrolling-text,actor-audio,sampled-audio}.log`,
`tmp/c89-scene-glue-capture-hashes-{before,after}.json` and
`tmp/c89-scene-glue-playback-actor-audio-actor-events-scrolling-text-sampled-audio.json`.

### Background camera/placement C89 conversion (15 November 2026)

Source baseline: `e91b98cc`. `background_camera.c` now decodes CAM headers and
appended placement points for both Studio and Marcus. Studio Background stores
camera metadata, placement cursors/list ownership and mouse-matrix state in a
persistent C record. C controls camera switching, placement cycling/reuse and
Euler/rotation/translation sequencing; native services retain BRender primitives,
file/list operations and world effects. Background resource loading/cache/light
policy, Preview image/mesh control and final native facade removal remain work.

The native Scene-content fixture adds camera switches, position/mouse/yaw output
and default/appended placement cycling/reuse to the preceding Scene trace.
Original native/repeat and original SDL debug/release traces match 30,976 bytes,
FNV-1a `34CB17AF`, SHA-256
`F3AA2B74879C45B4CA49FE235DA7016A2C5ABD209CF48A7F3F7C19D9F516C8BC`.
The exact guard retains the little-endian x64, one-byte-text restriction.

A supplemental native check writes a foreign-order camera and two appended
points, verifies the real block/list bridge, cycling and reuse, then restores
the original camera bytes and selection. Foreign-order points now decode from
their actual source offsets, removing the original swap past a stack-local
camera header. Failed point reads now unlock before releasing the new list.
Preview shares Studio's rejection of a partial appended point while retaining
its existing clip-plane/image admission checks. No stored versions change.

C-only tests cover both byte orders, signed scalar and unsigned angle extremes,
maximum wire lengths, invalid markers/tails/indexes, partial failed reads with
unchanged outputs, complete callback ordering and metadata publication, every
fallible service phase, allocation fallback, point-read cleanup, disposal and
placement cycling/reuse. No Marcus UI tests were added.

Final native, SDL debug and SDL release traces match all four original traces
above byte for byte. All 99 standalone C checks, 43 selected SDL release and 31
SDL debug checks pass. The 135 selected native checks pass after rebuilding
the initially stale `marcus-player-test` and `marcus-frame-previews-test` and
rerunning `marcus-player`; its old executable disagreed with the freshly linked
preview helper's rendering. The initial 134 passes and successful retry are
retained separately. Both hosts, `core-checks`, the indirectly invoked Marcus
test executables, CLI targets and native save/load executable were rebuilt.
Marcus was stopped before its rebuilds and relaunched afterward. The C-only
fixture passes Clang strict C89 with warnings as errors and MSVC AddressSanitizer.
The direct native save/close/reopen/close round trip and all 51 CLI baselines
pass. Builds and file-heavy suites ran serially.

The freshly linked existing background-preview check also passes the shipped
`bkgds.3cn` collection: 12 backgrounds, 188 cameras with palettes, 184 with finite
depth/meshes and 68 with static backdrops. It exercises authored/orbit rendering,
camera switching/cache reuse and out-of-range selection through Preview's real
shared-decoder bridge.

Fresh pre-conversion binaries were built from the source baseline before the
final captures, so these comparisons include the intervening renderer precision
and rasterization work rather than comparing against older Scene-only builds.
Both pre-conversion and converted binaries stayed fixed during their respective
21 fresh-process runs (one repeat, no concurrent builds/tests). Final SHA-256:

- SDL release: `82EDFC98EA25EB18750FB1EEBD79B741077B255C6BAE3CF29BEBB7AA89FBC40C`.
- Marcus regression: `00809F0DAE95AE464BAA8A78E524F952A3DF86B7E3920E1C28ED92B65B22E5E9`.

| Case | Original suite | Converted suite | Completion per suite |
| --- | --- | --- | --- |
| actor-events | `20261115T005723Z-df83c6d6` | `20261115T011324Z-b7264e54` | six retail/SDL/Marcus seek/playback |
| scrolling-text | `20261115T005837Z-3f0c517e` | `20261115T011438Z-aeac96c6` | six retail/SDL/Marcus seek/playback |
| actor-audio | `20261115T010140Z-054375b0` | `20261115T011740Z-578c3f97` | six retail/SDL/Marcus seek/playback |
| sampled-audio | `20261115T010254Z-d09c0325` | `20261115T011854Z-d11fe065` | three retail/SDL/Marcus playback |

Movie identity and suite integrity pass. All authored frame sequences, all 540
seek images and ordered playback image changes match each player's original
suite. Playback comparison collapses only adjacent identical scene/frame/image
samples. Actor-audio PCM16 peaks are 3192/4081/4081 and sampled-audio peaks are
2380/2176/2176 (retail/SDL/Marcus). All exceed 50; actor tone detection and the
first-scene-only contract pass for every player. Existing cross-player image
differences and single-repeat inconclusive timing comparisons remain. These
captures do not prove retail editing/failure behavior, full audio parity or
equivalent presentation markers. Captures use a shared warm SDL package cache
with private processes, settings and working movies; OS caching is uncontrolled.

Evidence: `tmp/c89-background-camera-{strict-build,strict,asan,standalone,native-tests,native-retry,sdl-debug-tests,sdl-release-tests,save-load,preview-backgrounds}.log`,
`tmp/c89-background-camera-*.bin`,
`tmp/c89-background-camera-trace-comparison.json`,
`tmp/c89-background-camera-output-comparison.json`,
`tmp/c89-background-camera-{before,final}-capture-{actor-events,scrolling-text,actor-audio,sampled-audio}.log`,
`tmp/c89-background-camera-{before,final}-capture-hashes-{before,after}.json` and
`tmp/c89-background-camera-playback-comparison.json`.

### Background resource C89 conversion (15 November 2026)

Source baseline: `4de43177`. `background_resource.c` owns Studio Background
construction/factory control, header/default-sound/light decoding, contiguous
camera enumeration, palette ownership/duplication, light lifetime and tag-list/
cache order. Marcus PreviewBackground uses the same loader and wire conversion
with its existing stricter visual admission profile. SDK/file/list primitives,
native object/string representations and Preview vector storage remain adapters;
Preview image-loading/mesh control and final native facade removal remain work.

The Scene-content fixture extends the camera trace with a foreign-order default
sound (including a nonzero wire pointer slot), foreign-order custom palette,
independent returned palette copies, a second light and on/off/repeated/leave-on
calls. Original native/repeat and original SDL debug/release traces match 31,048
bytes, FNV-1a `5E56C170`, SHA-256
`0BF35E79808D6A50ED79B1B8138AF4ED5138C1D94DB7A291FD1EE5160BFB2A89`.
Final native and both SDL traces match all four originals byte for byte. The
exact guard retains the little-endian x64, one-byte-text restriction.

Studio still tolerates optional palette decode failure and retains its stored
base, but fails an absent/unreadable required light list. It counts cameras
before metadata loading and retains partial palette/array owners for factory
teardown. Preview skips name/default-sound loading, normalizes fitting palettes,
admits at most sixteen lights, publishes entries/cameras in order and selects
camera zero last. Both profiles retain completed prefixes on failure. Preview
vector/camera allocation exceptions now become C failure results inside callbacks.
No Marcus UI tests were added.

Invalid byte-order markers, malformed palette/light strides and SDK-array size
overflow now reject before unsafe access. Default-sound loop decoding reads its
stored 32-bit flag explicitly; wire cache pointers stay ignored and tags unopened.
No valid wire versions or resource normalization are changed. Cache orchestration
preserves the original no-close lifetime of child tags acquired by the existing
tag-manager service, rather than silently changing reference behavior.
Read failures also preserve the native warning-before-temporary-list-release
prefix; the C-only fixture checks that order explicitly.

The C-only fixture covers both stream/host byte orders, signed values, stored
pointer exclusion, partial failed reads and unchanged decoder outputs, load and
factory failures, allocation overflow, owner cleanup and output publication order,
palette-read tolerance/profile admission, camera gaps/absence, light flags and
on/off/disposal ordering, and every tag insertion/child-build/cache failure step.
All 100 standalone C checks, 136 selected native, 44 SDL release and 32 SDL debug
checks pass on the final warning-order correction and exception guards. Both
hosts and core/CLI/test targets were rebuilt, including the indirect Marcus test
executables. The freshly linked existing background-preview sweep covers
12 shipped backgrounds and 188 cameras, including 184 with finite depth/meshes
and 68 with static backdrops. Native and SDL save/load checks pass, as do Clang
strict C89 with warnings as errors, MSVC AddressSanitizer and all 51 CLI baselines.
Builds and file-heavy suites ran serially. Marcus was stopped before its rebuilds
and relaunched afterward.

Final playback uses the preceding camera conversion's captures as its original
baseline. The original SDL/Marcus binaries were verified byte-identical to those
captures before rebuilding (`tmp/c89-background-resource-original-capture-hashes.json`).
Final binaries stayed fixed throughout all 21 fresh-process runs (one repeat,
no concurrent builds/tests). SHA-256 before/after:

- SDL release: `1DF6F30F260C5CB3419FA814B5B56B236295BD317B392DA685F0644DFFD0CC9D`.
- Marcus regression: `079259A10E397A1CBE2BD0D32B16F7E020BFAFB12CCB73329EB3926B01EDE139`.

| Case | Original suite | Final suite | Final completion |
| --- | --- | --- | --- |
| actor-events | `20261115T011324Z-b7264e54` | `20261115T015813Z-5d50bff8` | six retail/SDL/Marcus seek/playback |
| scrolling-text | `20261115T011438Z-aeac96c6` | `20261115T015926Z-3ba53e35` | six retail/SDL/Marcus seek/playback |
| actor-audio | `20261115T011740Z-578c3f97` | `20261115T020227Z-63212132` | six retail/SDL/Marcus seek/playback |
| sampled-audio | `20261115T011854Z-d11fe065` | `20261115T020340Z-5bc34309` | three retail/SDL/Marcus playback |

Movie identity and both original/final suite integrity pass. All authored frame
sequences, 540 seek images and ordered playback image changes match each player's
original suite. Playback comparison collapses only adjacent identical scene/frame/
image samples. Actor-audio peaks are 3192/4081/4081 and sampled-audio peaks are
2380/2176/2176 (retail/SDL/Marcus); actor tone detection and the first-scene-only
contract pass for every player. Original summaries moved by normal capture
retention were read from `retained-metadata`; immutable frame/image observations
remain in SQLite. Existing cross-player differences, inconclusive single-repeat
timing and presentation-marker limitations remain. Captures do not prove retail
editing/failure behavior or full PCM/MIDI parity. They use a shared warm SDL
package cache with private processes, settings and working movies; OS caching is
uncontrolled.

Evidence: `tmp/c89-background-resource-{strict-build,strict,asan,standalone,native-tests,sdl-debug-tests,sdl-release-tests,preview-backgrounds}.log`,
`tmp/c89-background-resource-*.bin`,
`tmp/c89-background-resource-{trace-comparison,output-comparison,playback-comparison}.json`,
`tmp/c89-background-resource-capture-{actor-events,scrolling-text,actor-audio,sampled-audio}.log` and
`tmp/c89-background-resource-capture-hashes-{before,after}.json`.

### Preview background image/mesh C89 conversion (15 November 2026)

Source baseline: `d5917a4b`. `background_preview.c` now owns camera/light arrays,
image buffers, decoded camera/image state, selection sequencing and the two-entry
mesh LRU. `background_mesh.c` owns depth reconstruction, discontinuity subdivision,
infinite-backdrop flood fill, ordered geometry generation, bounds, model chunking,
SDK preparation/rollback and registration lifetime. Persistent native vectors and
the native mesh-preparation owner were removed. The remaining native classes
translate SDK types and expose borrowed views. Broader Preview frame rendering/
depth fitting, Movie operations and final native facade removal remain work.

The existing mesh fixture adds a raw trace of vertices, triangles, texture and
infinite-backdrop bytes, dimensions/bounds and rendered colour/depth samples.
Original native and original SDL debug/release traces match 5,284,385 bytes,
FNV-1a `9B1D8E3E`, SHA-256
`A32C2EE4E1215C9EEC58B0A18A2CF0CFC2BF3827B5D92FC1667C1B8F045B76AA`.
The fixture retains analytic parallax/UV/occlusion/clip checks, discontinuity
holes, maximum-size multi-model preparation, cache reuse and SDK registration
balance. Its exact trace guard is restricted to Windows x64.

The shipped-background fixture adds every authored/orbit/free-camera RGBA/depth
image, including camera revisits after eviction. Original native and SDL debug/
release traces match 301,996,032 bytes, FNV-1a `E1A71005`, SHA-256
`C66A9715F7F6BF0B2396C5486185E1E99B673A928240A4020F4318F3C2FE24FE`.
It covers twelve backgrounds, 188 cameras, 184 with finite depth/meshes and 68 with
static backdrops. These traces use the local shipped `bkgds.3cn` and built
`utest.chk`; the test still accepts other resource collections without imposing
that collection's fingerprint. Final native and both SDL traces match all three
originals byte for byte. The existing Scene-content trace remains unchanged too.

C-only checks independently cover analytic plane geometry/UVs, topology order,
infinite fill, malformed/nonfinite inputs, geometry allocation failures,
map/material/model/preparation-storage failures, rollback and retry, bounded model
chunks, actor attachment prefixes and SDK disposal order. Image/cache checks cover
metadata copies, camera/image publication order, reference balance, every fetch/
lookup/image-allocation failure, malformed bounds, retained RGB/active-camera
prefixes, cache hits/eviction and failed creation/build preservation. Image-buffer
allocation failure now releases fetched bitmap references; the old native vector
exception path leaked them. Bounds reject before signed difference/negation
overflow, and nonfinite geometry rejects before unsafe float bounds conversion.
No valid input normalization or wire versions changed. No Marcus UI tests were
added; existing headless API/preview checks were extended.

All 102 standalone C checks, 139 selected native, 47 SDL release and 35 SDL debug
checks pass. Both hosts, core checks, directly/indirectly invoked test executables
and CLI targets were rebuilt. Both new C fixtures pass Clang strict C89 with
warnings as errors and MSVC AddressSanitizer. All 51 CLI baselines match. Builds
and file-heavy suites ran serially; Marcus was stopped before rebuilding and
relaunched afterward. Validation is native Windows x64 and SDL Windows x64
debug/release; it does not establish other platform/toolchain floating-point
identity or complete C-only host integration.

The original SDL/Marcus binaries were verified byte-identical to the preceding
Background-resource captures before rebuilding. Final binaries stayed fixed
through all 21 fresh-process captures (one repeat, no concurrent builds/tests).
SHA-256 before/after:

- SDL release: `F6BEB70D59B631384CAE5EAA232FDD6D3F1F9C17F1F0458C11941A8DE0393608`.
- Marcus regression: `4690DA24F4D95183CB6ABB018B105955B66E1DF7C1DC0E1ABD39E53765C9EBE6`.

| Case | Original suite | Final suite | Final completion |
| --- | --- | --- | --- |
| actor-events | `20261115T015813Z-5d50bff8` | `20261115T024948Z-38e6f099` | six retail/SDL/Marcus seek/playback |
| scrolling-text | `20261115T015926Z-3ba53e35` | `20261115T025106Z-25459966` | six retail/SDL/Marcus seek/playback |
| actor-audio | `20261115T020227Z-63212132` | `20261115T025427Z-dd4e7830` | six retail/SDL/Marcus seek/playback |
| sampled-audio | `20261115T020340Z-5bc34309` | `20261115T025546Z-639fde6b` | three retail/SDL/Marcus playback |

Original/final movie identity and suite integrity pass. All authored frame
sequences, 540 seek images and ordered playback image changes match each player's
original suite. Playback comparison collapses only adjacent identical scene/frame/
image samples. Actor-audio peaks are 3192/4081/4081 and sampled-audio peaks are
2380/2176/2176 (retail/SDL/Marcus); actor tone detection and first-scene-only
contracts pass. Summaries moved by normal retention are read from retained
metadata. Existing cross-player differences, inconclusive single-repeat timing
and presentation-marker limitations remain. These captures do not establish full
audio/timing parity, retail editing/failure behavior or free-camera visual parity
with retail. The independent native fixture covers free-camera geometry/rendering.
Captures use a shared warm SDL package cache with private processes, settings and
working movies; OS caching is uncontrolled.

Evidence: `tmp/c89-background-{mesh,preview}-asan.log`,
`tmp/c89-background-preview-{standalone,native-tests,sdl-debug-tests,sdl-release-tests}.log`,
`tmp/c89-background-{mesh,preview}-*.bin`,
`tmp/c89-background-{mesh,preview}-trace-comparison.json`,
`tmp/c89-background-preview-{output-comparison,playback-comparison}.json`,
`tmp/c89-background-preview-original-capture-hashes.json`,
`tmp/c89-background-preview-capture-{actor-events,scrolling-text,actor-audio,sampled-audio}.log` and
`tmp/c89-background-preview-capture-hashes-{before,after}.json`.

### Movie scene-container control and undo (15 November 2026)

`movie_scene.c` now controls the current scene reference/index, close/switch,
new/insert/remove, undoable removal and scene undo/redo. Native Movie and undo
objects supply typed Scene, archive, reference, sound, client and undo-list
operations. The C controller preserves previous-scene retry, callback/index
publication, child-ID traversal, rollback and dirty/notification prefixes. Failed
adoption retains the native count increment; removing the final scene returns
before marking dirty; undo allocation failure returns success without removal.
Failed removal reads retain the original null-close/debug-diagnostic path.
No wire versions or writer normalization changed. Background-driven scene
creation, common Movie editing/control/undo, autosave/save orchestration,
composition and final native representation/facade integration remain work.

The existing headless Movie save/load fixture now probes distinct scene content
through child-ID shifts, insertion/removal boundaries, nil/same-scene switching,
removal undo/redo and callback publication order. Its trace was captured from the
original native and SDL debug/release controller before conversion: 1,064 bytes,
FNV-1a `E62AAC8F`, SHA-256
`9249BE235D4B088EEF89FDC8D3EC0EC3F40E9ACD23945482F9A40D0309DF0712`.
All three final traces match that original digest exactly. The Windows x64 probe
checks the frozen trace by default. Existing Scene-content traces also retain
their 31,048-byte SHA-256
`0BF35E79808D6A50ED79B1B8138AF4ED5138C1D94DB7A291FD1EE5160BFB2A89`.

The standalone C fixture exercises autosave/resource/temp/write/adoption/read/
start/first-frame failures, retry exhaustion, destructive insertion prefixes,
scene/undo reference balance, sound enable/flush decisions, removal failures,
snapshot commit/cleanup and undo/redo failure/notification order. These fault
tests use deterministic C services; the native fixture checks successful
operations and persistence, not retail failure behavior. No Marcus UI tests were
added. All 103 standalone C checks, 140 selected native, 48 SDL release and 36 SDL
debug checks pass. The new fixture passes Clang strict C89 with warnings as errors
and MSVC AddressSanitizer. All 51 CLI format baselines match. Both hosts and their
test helpers were rebuilt; Marcus was stopped before rebuilding and relaunched.
Builds and file-heavy suites ran serially. Validation covers Windows x64 native
and SDL debug/release, not complete C-only host integration or other platforms.

The source SDL/Marcus binaries were verified identical to the preceding
Background-preview captures before rebuilding. Final binaries remained fixed
through all 21 fresh-process captures (one repeat; no concurrent builds/tests).
Final before/after SHA-256:

- SDL release: `AC9795B9DDB5CD6F71688467EAC474B0260BB9E839C33A5C1E6945FECBE3C0FC`.
- Marcus regression: `A45B2B21412591C02749D1F1A50906F1F4CBC1C6A5E1EF2F79CEC36CBCC6C583`.

| Case | Original suite | Final suite | Final completion |
| --- | --- | --- | --- |
| actor-events | `20261115T024948Z-38e6f099` | `20261115T031754Z-ac920e7b` | six retail/SDL/Marcus seek/playback |
| scrolling-text | `20261115T025106Z-25459966` | `20261115T031912Z-33ce1649` | six retail/SDL/Marcus seek/playback |
| actor-audio | `20261115T025427Z-dd4e7830` | `20261115T032225Z-4ca95050` | six retail/SDL/Marcus seek/playback |
| sampled-audio | `20261115T025546Z-639fde6b` | `20261115T032343Z-d4bd4cf8` | three retail/SDL/Marcus playback |

All original/final movie identities and suite integrity checks pass. Every
authored frame sequence, all 540 seek images and ordered playback image changes
match each player's original suite. Playback comparison collapses only adjacent
identical scene/frame/image samples. Actor-audio peaks are 3192/4080/4081 and
sampled-audio peaks are 2380/2176/2176 (retail/SDL/Marcus); tone detection and
first-scene-only contracts pass. This is preserved playback behavior, not a proof
of PCM identity, full cross-player audio/timing parity or retail editing/undo/
failure behavior. Existing cross-player differences, single-repeat timing
inconclusiveness and presentation-marker limitations remain. Captures share a
warm SDL package cache with private processes, settings and working movies; OS
caching is uncontrolled. Normal retention may move source summaries to retained
metadata; image/frame hashes remain in the database.

Evidence: `tmp/c89-movie-scenes-{standalone,native-tests,sdl-release-tests,sdl-debug-tests,asan}.log`,
`tmp/c89-movie-scenes-{final,scene-final}-{native,sdl-release,sdl-debug}.bin`,
`tmp/c89-movie-scenes-trace-comparison.json`,
`tmp/c89-movie-scenes-{output-comparison,playback-comparison}.json`,
`tmp/c89-movie-scenes-original-capture-hashes.json`,
`tmp/c89-movie-scenes-capture-{actor-events,scrolling-text,actor-audio,sampled-audio}.log` and
`tmp/c89-movie-scenes-capture-hashes-{before,after}.json`.

### Movie autosave, save publication and collection (15 November 2026)

`movie_save.c` now owns autosave/save transactions, temporary-file and zero-cache
resource setup, saved-file reference/flags, read-only save-as selection, VMM
format/publication policy, stored-scene tag traversal and material/template/sound
collection control. Source-name blocks release after the long operation ends,
before nested autosave returns to packaging, format selection or publication.
The blacklist uses C-owned collection storage. Empty-tag scans use an explicit
match flag, removing the native uninitialized temporary read while preserving
the intended deletion set. Three unused native control wrappers were removed.
Native callbacks retain physical file/path/block I/O, typed collection/Scene
operations, client/undo services and container packaging. Movie construction still
publishes loaded name/read-only metadata; common DocumentBase dirty state remains
a host field. This does not complete Movie editing/undo, background-driven scene
creation, composition or final C-only host/facade integration.

The original native and SDL debug/release save controller was captured before
conversion through the existing headless save/load fixture. Sixteen scenarios
cover clean/closing autosave, named/copy saves, file/name/dirty flags, ordered chunk
graphs, VMM packaging success/failure, working-file conversion and ForceSaveAs.
All three originals match 29,440 bytes, FNV-1a `696EAA7C`, SHA-256
`B61DE53BDF143332B237A9DED304518314F97EC43F57CB7E1D87AF48744BAC07`.
The fixture freezes that trace by default on Windows x64. Its deterministic
container double verifies publication/control, not physical ZIP output.

C-only fault checks cover root/scene/roll-call/source/title/physical-save failure,
cleanup order, retained old/new graph prefixes, irreversible actor pruning,
saved-file release/reacquisition, failed binding/name flags, borrowed output
publication, conservative format choice, packaging failure/title restoration,
allocator failure before deferred deletion, cache scope and reverse sound purge.
The source-block lifetime is checked across normal/failure/retry completion and
before packaging. Optional BUG1848 retry is capped to one retry and tested in C;
it remains disabled in the current native configuration. Native happy-path
traces and C service failures do not prove retail editing/failure equivalence.
No wire versions or writer normalization changed. No Marcus UI tests were added.

Final native and SDL debug/release save traces match all three original traces
byte for byte. Existing 31,048-byte Scene-content traces also remain unchanged,
SHA-256 `0BF35E79808D6A50ED79B1B8138AF4ED5138C1D94DB7A291FD1EE5160BFB2A89`.
All 104 standalone C, 141 selected native, 49 SDL release and 37 SDL debug checks
pass. The C fixture passes Clang strict C89 with warnings as errors and MSVC
AddressSanitizer; all 51 CLI format baselines match. Checks were rerun after
restoring the source-block cleanup boundary. Builds and file-heavy suites ran
serially; Marcus was stopped before rebuilds and relaunched afterward.

The existing Studio VMM workflow also passes all eight checks with the original
blank VMM and a local prop VXP, using private settings/store directories. It
verifies source-6 authoring through the save portfolio, ordinary 3MM output,
VMM save/reopen with matching actor state and viewport pixels, per-movie applied
sets, two-list package management and standalone install/apply/remove/save across
restarts. Reference inputs retain their original hashes. Its final report is
`tmp/c89-movie-save-vmm-workflow/run-qsl4pgwc/report.json`. The first invocation
supplied plain 3MM instead of blank VMM and stopped at the VMM-only applied-set
expectation; the unchanged workflow passes with the correct fixture. That input
mistake remains recorded in `tmp/c89-movie-save-vmm-workflow-plain-input.log`.
This validation covers Windows x64 native and SDL debug/release, not other
platforms, complete original-v3dmm/retail parity or C-only host integration.

Before rebuilding, the source SDL/Marcus binaries matched the preceding
Movie-scene captures. Final binaries remained fixed through all 21 fresh-process
retail/SDL/Marcus captures (one repeat, no concurrent builds/tests). Final
before/after SHA-256:

- SDL release: `DC72E30B3B09F3F2E38DD66C6DD69DDFC2C93796C495D4949F9DE2759D68A897`.
- Marcus regression: `9967030AD1B8079CB426F7F0C3643D1DD2496E3BA052CA69C3B58B18BBD7E87F`.

| Case | Original suite | Final suite | Final completion |
| --- | --- | --- | --- |
| actor-events | `20261115T031754Z-ac920e7b` | `20261115T042000Z-e95b1a1d` | six retail/SDL/Marcus seek/playback |
| scrolling-text | `20261115T031912Z-33ce1649` | `20261115T042118Z-25c90023` | six retail/SDL/Marcus seek/playback |
| actor-audio | `20261115T032225Z-4ca95050` | `20261115T042433Z-9c09e905` | six retail/SDL/Marcus seek/playback |
| sampled-audio | `20261115T032343Z-d4bd4cf8` | `20261115T042550Z-7d148573` | three retail/SDL/Marcus playback |

Original/final movie identities, suite integrity, all authored frame sequences,
540 seek images and ordered playback image changes match each player's original
suite. Playback comparison collapses only adjacent identical scene/frame/image
samples. Actor-audio peaks are 3192/4081/4081 and sampled-audio peaks are
2380/2176/2176 (retail/SDL/Marcus); tone detection and first-scene-only contracts
pass. Existing cross-player differences, single-repeat timing inconclusiveness
and presentation-marker limits remain; these results do not establish PCM
identity or full cross-player audio/timing parity. Captures use a shared warm
SDL package cache with private processes, settings and working movies; OS caching
is uncontrolled.

The first capture preflight stopped before player launch because free space was
below 4 GiB. The existing retention tool then pruned only superseded task capture
media, retaining metadata and database hashes. Final captures retain one complete
media suite per comparison group; source summaries are read from retained
metadata when necessary. No comparison assertions or integrity checks were
weakened. Log readers account for Windows PowerShell UTF-16 output.

Evidence: `tmp/c89-movie-save-{standalone,native-tests,sdl-release-tests,sdl-debug-tests,asan}.log`,
`tmp/c89-movie-save-{baseline,final,scene-final}-{native,sdl-release,sdl-debug}.bin`,
`tmp/c89-movie-save-trace-comparison.json`,
`tmp/c89-movie-save-{output-comparison,playback-comparison}.json`,
`tmp/c89-movie-save-vmm-workflow.log`, `tmp/c89-movie-save-prune.log`,
`tmp/c89-movie-save-original-capture-hashes.json`,
`tmp/c89-movie-save-capture-{actor-events,scrolling-text,actor-audio,sampled-audio}.log` and
`tmp/c89-movie-save-capture-hashes-{before,after}.json`.

### Movie roll call, actor edits and common undo (15 November 2026)

`movie_rollcall.c` now owns row/name lookup, rename notification, browser flags,
actor-ID publication, tag refresh and reference-count changes. Failed insertion
closes the tag opened for its uncommitted row, fixing a reference leak in the
original native path; its assigned actor ID and limit remain published on failure.
Existing-row tag replacement closes the old owner before duplicating the new one.
Shallow ChangeActrTag replacement retains its caller-managed ownership contract.

`movie_edit.c` owns selection/placement sequencing, actor rotate/pull/scale/time/
costume/sound dispatch, scene camera/pause/transition/sound dispatch, actor/path
paste, background-driven scene creation and common undo registration/notification.
Services call the existing C-backed Actor/Scene operations and retain native view,
client, undo-object and list primitives. Path paste rereads selection after nested
operations, including failed undo creation; it restores the snapshot only for that
failure. New-scene background/undo failures remove the new scene after preserving
their completed title/sound/mutation prefixes. Caption/text/template/material
Movie transactions, composition and final C-only host/facade integration remain.

The existing headless Movie save/load fixture captured the original native and
SDL debug/release controllers before conversion. Sixteen scenarios record actor
state/routes/events, roll-call names/rows/cache references, dirty flags and client/
undo notifications. All three originals and final converted traces match 61,552
bytes, FNV-1a `85C0C7B5`, SHA-256
`9B5962814AE9C4099AFD8E7357474DB62AEC960D8507DAEA14CE80E184D1B92D`.
The fixture enforces this fingerprint by default on Windows x64. Separate C-only
fixtures verify failure ordering, tag cleanup, snapshot transfer, mutable path-
paste selection, sound defaults and background rollback. No Marcus UI tests were
added. Native happy paths and C service failures do not prove retail edit/failure
equivalence.

All twelve native/SDL debug/release trace comparisons pass: the new edit trace,
29,440-byte Movie-save trace, 1,064-byte Movie-scene-control trace and 31,048-byte
Scene-content trace retain their original bytes. All 106 standalone C, 143 selected
native, 51 SDL release and 39 SDL debug checks pass. Both new C fixtures pass Clang
strict C89 with warnings as errors and MSVC AddressSanitizer. All 51 CLI format
baselines match. Builds and file-heavy checks ran serially; Marcus was stopped
before rebuilding and relaunched afterward.

The unchanged Studio VMM workflow passes all eight checks using the original
blank VMM and local prop VXP, private settings/package stores and fresh processes.
It verifies authoring/save/reopen actor state and pixels, ordinary 3MM output,
per-movie applied sets, two-list package management, standalone install/apply/
remove/save across restarts and unchanged reference inputs. Its report is
`tmp/c89-movie-edit-vmm-workflow/run-ut6k4is2/report.json`. This remains Windows
x64 evidence; other platforms, full retail/original-v3dmm edit parity and C-only
host integration remain unverified.

Before rebuilding, the source SDL/Marcus executables matched the preceding
Movie-save captures. Final binaries remained fixed through all 21 fresh-process
retail/SDL/Marcus captures (one repeat, no concurrent builds/tests). Final
before/after SHA-256:

- SDL release: `7895B1937EEE6B992A500FAF491890395547F112D9531CDE5ABBE28AEE547079`.
- Marcus regression: `B1E4461CD7DADF154AC77FBAFB7917CB230062AB9BAB819727461BC3C46A33C7`.

| Case | Original suite | Final suite | Final completion |
| --- | --- | --- | --- |
| actor-events | `20261115T042000Z-e95b1a1d` | `20261115T053209Z-e1c11c15` | six retail/SDL/Marcus seek/playback |
| scrolling-text | `20261115T042118Z-25c90023` | `20261115T053328Z-0e871cb3` | six retail/SDL/Marcus seek/playback |
| actor-audio | `20261115T042433Z-9c09e905` | `20261115T053646Z-933628f1` | six retail/SDL/Marcus seek/playback |
| sampled-audio | `20261115T042550Z-7d148573` | `20261115T053804Z-e1688d20` | three retail/SDL/Marcus playback |

Original/final movie identities, suite integrity, all authored frame sequences,
540 seek images and ordered playback image changes match each player's original
suite. Playback comparison collapses only adjacent identical scene/frame/image
samples. Actor-audio peaks are 3192/4081/4081 and sampled-audio peaks are
2380/2176/2176 (retail/SDL/Marcus); tone detection and first-scene-only contracts
pass. Existing cross-player differences, single-repeat timing inconclusiveness
and presentation-marker limits remain. This does not establish PCM identity or
full audio/timing parity. Captures use a shared warm SDL package cache with private
processes, settings and working movies; OS caching is uncontrolled.

Six older task-generated raw Preview traces were compressed in place before
capture to recover disk space; their content hashes remain unchanged. The existing
retention tool keeps one complete media suite per comparison group and prunes
superseded media after each suite. Source metadata and immutable database hashes
remain available. No comparison assertions or integrity checks were weakened.

Evidence: `tmp/c89-movie-edit-{standalone,native-tests,sdl-release-tests,sdl-debug-tests,asan}.log`,
`tmp/c89-movie-edit-{baseline,final}-{native,sdl-release,sdl-debug}.bin`,
`tmp/c89-movie-edit-{save-final,scene-control-final,scene-final}-{native,sdl-release,sdl-debug}.bin`,
`tmp/c89-movie-edit-trace-comparison.json`,
`tmp/c89-movie-edit-{output-comparison,playback-comparison}.json`,
`tmp/c89-movie-edit-vmm-workflow.log`, `tmp/c89-movie-edit-trace-compression.log`,
`tmp/c89-movie-edit-original-capture-hashes.json`,
`tmp/c89-movie-edit-capture-{actor-events,scrolling-text,actor-audio,sampled-audio}.log` and
`tmp/c89-movie-edit-capture-hashes-{before,after}.json`.

### Movie material, 3-D text and caption transactions (15 November 2026)

`movie_content.c` now owns material insertion, 3-D text authoring/insertion and
actor template replacement, caption insertion/removal/hiding and indexed visible
caption selection. It reuses the C Movie actor-edit and roll-call controllers;
native callbacks provide string/tag/bounds marshaling, resource/template/Actor/
Scene primitives, default font lookup and interactive mouse attachment. Movie
composition and final C-only host/facade integration remain work.

The original native controller leaked temporary text templates after resource/
temporary-file/write failure and an opened tag after failed actor insertion.
These references now release on failure while already-written chunks remain.
Caption insertion now stops after failed Scene add, preventing its subsequent
null selection/mouse-attachment call. Indexed caption selection advances through
storage and selects the requested visible caption; the original repeated one
slot, returned without selecting or looped indefinitely. These intentional fixes
are covered in C and do not claim retail editor/failure equivalence.

The existing headless Movie save/load fixture captured all three original native/
SDL debug/release controllers before conversion. Eight scenarios record material
tag/cache ownership, 3-D text insertion and shape/text/template/actor-ID changes,
blank-text removal, caption hide/remove results, roll-call rows/names, dirty/undo/
client state and ordered chunk graphs. All originals match 91,484 bytes, FNV-1a
`B21E0181`, SHA-256
`8C2891B41A5DEE68EE54C2B7873C3EC1E809899882ABE01BEF9F1FEBBB6CE0AF`.
The fixture freezes that fingerprint by default on Windows x64. It uses a
synthetic positive-source font and placement background; embedded-font and full
retail authoring parity are unverified. The caption insertion prefix is built
without interactive mouse attachment. C-only tests verify full insertion, failed
add cleanup, visible-index selection, material output publication, resource-file
capture order, tag/template/snapshot cleanup, all-space removal without inspecting
the font input, failed new-row
restoration and retained consumed ID/chunk prefixes. No Marcus UI tests were added.

All fifteen final native/SDL debug/release trace comparisons pass: the new
91,484-byte content trace, 61,552-byte Movie-edit trace, 29,440-byte Movie-save
trace, 1,064-byte Movie-scene-control trace and 31,048-byte Scene-content trace
retain their original bytes. Broad suites pass 107 standalone C, 144 selected
native, 52 SDL release and 40 SDL debug checks. After deferring font-input access,
final rebuilt native/SDL debug/release content, Scene-content, full Movie fixtures
and C89 syntax checks pass (four per host, plus two standalone). The C fixture
passes Clang strict C89 with warnings as errors and MSVC AddressSanitizer; all 51
CLI format baselines match. Builds and file-heavy checks ran serially. Marcus
was stopped before rebuilds and relaunched afterward.

The unchanged Studio VMM workflow passes all eight checks with the original blank
VMM and local prop VXP, private settings/package stores and fresh processes. It
verifies authoring/save/reopen state and pixels, ordinary 3MM output, per-movie
applied sets, package management and install/apply/remove/save across restarts.
Reference inputs retain their hashes. Its report is
`tmp/c89-movie-content-vmm-workflow/run-zqyfqujc/report.json`. This remains Windows
x64 evidence; other platforms, full retail/original-v3dmm edit parity and final
C-only host integration remain unverified.

Final captures compare against the preceding `b818ac16` Movie-edit revision.
Binaries remained fixed through all 21 fresh-process retail/SDL/Marcus captures
(one repeat, no concurrent builds/tests). Final before/after SHA-256:

- SDL release: `6B79615C47F14DB70DE42E61EDB2EF11CC4C57045F5D41546CB90569C278AD33`.
- Marcus regression: `1E7A45D97262D54FF25A28B99DE40A67724460D9C0D26D994642314E8F49C9C5`.

| Case | Original suite | Final suite | Final completion |
| --- | --- | --- | --- |
| actor-events | `20261115T053209Z-e1c11c15` | `20261115T061006Z-15e26c47` | six retail/SDL/Marcus seek/playback |
| scrolling-text | `20261115T053328Z-0e871cb3` | `20261115T061124Z-59ca83cf` | six retail/SDL/Marcus seek/playback |
| actor-audio | `20261115T053646Z-933628f1` | `20261115T061440Z-d2646ff5` | six retail/SDL/Marcus seek/playback |
| sampled-audio | `20261115T053804Z-e1688d20` | `20261115T061600Z-551a4dd7` | three retail/SDL/Marcus playback |

Original/final movie identities, suite integrity, all authored frame sequences,
540 seek images and ordered playback image changes match each player's original
suite. Playback comparison collapses only adjacent identical scene/frame/image
samples. Actor-audio peaks are 3192/4081/4081 and sampled-audio peaks are
2380/2176/2176 (retail/SDL/Marcus); tone detection and first-scene-only contracts
pass. Existing cross-player differences, single-repeat timing inconclusiveness
and presentation-marker limits remain. This does not establish PCM identity or
full audio/timing parity. Captures use a shared warm SDL package cache with private
processes, settings and working movies; OS caching is uncontrolled.

Generated debug files were compressed before capture to recover disk space, with
unchanged content hashes. The existing retention tool keeps one complete media
suite per comparison group and prunes superseded media after completion. Source
metadata and immutable database hashes remain available. Comparison assertions
and integrity checks are unchanged.

Evidence: `tmp/c89-movie-content-{standalone,native-tests,sdl-release-tests,sdl-debug-tests,asan}.log`,
`tmp/c89-movie-content-final-{native,sdl-release,sdl-debug,standalone}-tests.log`,
`tmp/c89-movie-content-{baseline,final}-{native,sdl-release,sdl-debug}.bin`,
`tmp/c89-movie-content-{edit-final,save-final,scene-control-final,scene-final}-{native,sdl-release,sdl-debug}.bin`,
`tmp/c89-movie-content-trace-comparison.json`,
`tmp/c89-movie-content-{output-comparison,playback-comparison}.json`,
`tmp/c89-movie-content-vmm-workflow.log`,
`tmp/c89-movie-content-{debug,extra-debug}-compression-hashes.json`,
`tmp/c89-movie-content-{debug,extra-debug}-compression.log`,
`tmp/c89-movie-content-original-capture-hashes.json`,
`tmp/c89-movie-content-capture-{actor-events,scrolling-text,actor-audio,sampled-audio}.log` and
`tmp/c89-movie-content-capture-hashes-{before,after}.json`.

### Movie composition and imported actor remapping (15 November 2026)

`movie_compose.c` now owns descriptor collection/insertion rollback, thumbnail/
cache reference transfer, composite disposal, storyboard application, temporary
import cleanup, roll-call ownership transfer and shared/nested actor-ID remapping.
It reuses the C Movie scene-owner close helper, C chunk walker and C collection
storage; its visited-ID set no longer needs a native DynamicArray object.
Native descriptor/list/bitmap/resource representations and physical file/Scene/
client primitives remain adapters. Final C-only host/facade integration remains.

Application retains completed scene/file/roll-call prefixes on failure and keeps
actor-start/registration import errors non-fatal. It now deletes an uncommitted
cloned Movie after failed roll-call merge, fixing the original cleanup omission.
The current-scene index now follows its live child slot, excluding deleted
descriptors, fixing invalid indexes when deleted descriptors precede a kept current
scene. A headless native/SDL check covers that documented ordering. Partial/empty
composite disposal is safe. These fixes do not establish retail editor/failure
equivalence. BUG1929's enabled temporary-file-before-autosave branch still requires
working resources; the empty-movie fixture prepares them explicitly.

The existing headless Movie save/load fixture captured the original native and
SDL debug/release controllers before conversion. Eight scenarios record imports,
interleaving/reordering, transition changes, current/other/all-scene deletion,
empty composition, descriptor thumbnail/cache ownership, client notifications,
file graphs and disposal. Four more cover native/foreign actor headers, shared
actors and nested actor chunks. All originals and converted host probes match
19,044 bytes, FNV-1a `8EECD466`, SHA-256
`A9811C569BF09619E616B26BACBF0233840122249E28876C6F44779D34AA65FE`.
The fixture freezes that fingerprint on Windows x64. Its imported scenes are
empty; separate actor-file/roll-call probes and C ownership tests do not prove
complete retail asset-import equivalence. No wire versions or writers changed.
No Marcus UI tests were added.

C fault checks cover collection allocation/read/insert rollback, retained autosave
effects, reverse disposal, cloned-root/cache cleanup after first/later merge
failure, retained adoption/transition prefixes, import diagnostics/tool ordering,
live current-scene indexes and wrapped actor-ID limits. Merge checks cover loaded
tag transfer/rollback, shared/nested deduplication, partial actor writes, visited-
set allocation failure and the native non-fatal traversal-error policy. The old
native loop calculated a skip flag but never passed it to traversal; C preserves
its descendant visits. Removing native temporary allocations changes allocator
attempt ordinals. The fault fixture's graph callback initially failed to report
end-of-children and hung; correcting the test double restores termination without
changing production traversal or assertions.

All eighteen final native/SDL debug/release trace comparisons pass: the new
19,044-byte composition trace, 91,484-byte content trace, 61,552-byte Movie-edit
trace, 29,440-byte Movie-save trace, 1,064-byte Movie-scene-control trace and
31,048-byte Scene-content trace retain their original bytes. All 108 standalone C,
145 selected native, 53 SDL release and 41 SDL debug checks pass. The C fixture
passes Clang strict C89 with warnings as errors and MSVC AddressSanitizer; all 51
CLI format baselines match. Builds and file-heavy checks ran serially. Marcus was
stopped before rebuilding and relaunched afterward.

The unchanged Studio VMM workflow passes all eight checks with the original blank
VMM and local prop VXP, private settings/package stores and fresh processes. It
verifies authoring/save/reopen state and pixels, ordinary 3MM output, per-movie
applied sets, package management and install/apply/remove/save across restarts.
Reference inputs retain their hashes. Its report is
`tmp/c89-movie-compose-vmm-workflow/run-7m29txog/report.json`. This remains Windows
x64 evidence; other platforms, full retail/original-v3dmm edit parity and final
C-only host integration remain unverified.

Final captures compare against the preceding `cbb93b5c` Movie-content revision.
Binaries remained fixed through all 21 fresh-process retail/SDL/Marcus captures
(one repeat, no concurrent builds/tests). Final before/after SHA-256:

- SDL release: `03464FF0BC1384354D45BEEEB428DC3A6229FAE9D3DAC725066B50878CB2AD92`.
- Marcus regression: `350CDD262461A4508C2B64056B9DA4C3D61260651FAB94F0347C1F17D0FB1FA3`.

| Case | Original suite | Final suite | Final completion |
| --- | --- | --- | --- |
| actor-events | `20261115T061006Z-15e26c47` | `20261115T065056Z-5582d832` | six retail/SDL/Marcus seek/playback |
| scrolling-text | `20261115T061124Z-59ca83cf` | `20261115T065214Z-850f3efb` | six retail/SDL/Marcus seek/playback |
| actor-audio | `20261115T061440Z-d2646ff5` | `20261115T065527Z-b054e9e8` | six retail/SDL/Marcus seek/playback |
| sampled-audio | `20261115T061600Z-551a4dd7` | `20261115T065645Z-cb107d0a` | three retail/SDL/Marcus playback |

Original/final movie identities, suite integrity, all authored frame sequences,
540 seek images and ordered playback image changes match each player's original
suite. Playback comparison collapses only adjacent identical scene/frame/image
samples. Actor-audio peaks are 3192/4081/4081 and sampled-audio peaks are
2380/2176/2176 (retail/SDL/Marcus); tone detection and first-scene-only contracts
pass. Existing cross-player differences, single-repeat timing inconclusiveness
and presentation-marker limits remain. This does not establish PCM identity or
full audio/timing parity. Captures use a shared warm SDL package cache with private
processes, settings and working movies; OS caching is uncontrolled.

The existing retention tool keeps one complete media suite per comparison group
and prunes superseded media after completion. Source metadata and immutable
database hashes remain available. Comparison assertions and integrity checks
are unchanged.

Evidence: `tmp/c89-movie-compose-{standalone,native-tests,sdl-release-tests,sdl-debug-tests,asan}.log`,
`tmp/c89-movie-compose-{baseline,probe,final}-{native,sdl-release,sdl-debug}.bin`,
`tmp/c89-movie-compose-{content-final,edit-final,save-final,scene-control-final,scene-final}-{native,sdl-release,sdl-debug}.bin`,
`tmp/c89-movie-compose-trace-comparison.json`,
`tmp/c89-movie-compose-{output-comparison,playback-comparison}.json`,
`tmp/c89-movie-compose-vmm-workflow.log`,
`tmp/c89-movie-compose-original-capture-hashes.json`,
`tmp/c89-movie-compose-capture-{actor-events,scrolling-text,actor-audio,sampled-audio}.log` and
`tmp/c89-movie-compose-capture-hashes-{before,after}.json`.


### Preview frame rendering and depth fitting (15 November 2026)

`preview_render.c` now owns frame admission, temporary world/pixelmap/light and
material-backup lifetimes, authored/output raster sizing, background blitting,
scene/body/model bounds, camera-guide fitting, stable-orbit state, axis/free/
orthographic camera construction, scene/headlight policy, stable body ordering,
material/edge overrides, clipped geometry depth fitting, raster dispatch and
indexed/depth image composition. `preview_geometry.c` owns iterative actor-tree
copying and six-plane triangle clipping/depth intervals. Native helpers expose
typed SDK primitives and borrowed data. Foreign rasterizer exceptions return to
C++ only after the C frame owner has restored shared materials, disabled lights,
reset the process-wide triangle sink and released its world and pixelmaps.
Light payloads now remain alive through world disposal. Native source actors,
models and materials retain their original ownership. Actor sibling order and
visibility-clock ties are preserved by C traversal and stable merging.

Before conversion, the existing headless Scene Preview fixture captured 168
scene renders spanning seven display modes and twelve authored/orbit/free/axis/
orthographic configurations, two frames, guide/stable/object framing, near-clip
overrides, output resolution and depth. Three more rasterizer cases cover success
and Begin/End failures, followed by CPU image/depth restoration checks. All three
original hosts match 18,029,304 bytes, FNV-1a `A31B99A6`, SHA-256
`7AF798CC29A5020C54BFDA84A8A74A6D8673E1976AE290C24F5BDB7E18CB0943`.
The fixture freezes this fingerprint on Windows x64. Four unrecorded foreign
allocation-exception cases also check Frame/Begin/End/RGBA cleanup and recovery.
No Marcus UI tests were added. Camera depth fitting retains double precision
until the SDK fixed conversion; prematurely rounding to float changed depth
pixels and one near-plane value and was corrected before final verification.

The standalone C fixture checks six-plane perspective/parallel clipping, reverse
sibling visitation with head insertion, stable visibility ordering, material
backup deduplication/restoration, depth inversion, aspect fitting/letterboxing,
headlights/scene lights, light-data lifetime and allocation/SDK/raster/output
failure cleanup. Allocation attempt ordinals change with C scratch ownership;
these tests establish the documented resource contracts, not retail failure or
editor equivalence. The remaining Preview picking, asset/scene admission,
camera interaction/presentation and final C-only host/facade integration remain
conversion work. Evidence here is Windows x64; broader platform and retail
editing equivalence remain unverified.


All 109 standalone C, 147 selected native, 55 SDL release and 43 SDL debug checks
pass. The affected C fixture, strict C89 syntax, mesh/tree and Scene Preview
checks also pass after final adapter cleanup; MSVC AddressSanitizer passes.
All 51 CLI baselines and all 24 original/final traces match: three hosts each
for Scene Preview rendering, background Preview rendering, Movie composition,
content/edit/save/scene control and Scene content. The background trace retains
301,996,032 bytes and FNV-1a `E1A71005`, covering 12 backgrounds and 188 cameras.
Marcus was stopped before rebuilding and relaunched afterward.

A redundant fresh retail/SDL/Marcus playback campaign was stopped at the user's
request. The actor-events suite `20261115T074322Z-cbcedaa9` completed six runs;
scrolling-text suite `20261115T074443Z-583af7d9` was interrupted. No full playback
campaign or new audio equivalence is claimed for this revision. Earlier retained
retail captures remain the reference; the byte-identical original/final Preview
traces establish preservation of the previously compared host outputs.

Reuse retained reference captures when executable, movie, content, capture tools
and relevant settings are unchanged. Rebuilds of SDL or Marcus do not invalidate
retail reference results. Check those identities before launching a player, and
run only missing or invalidated cases/players. A whole new retail campaign for
every C89 slice is unnecessary. This Preview revision recorded the workflow
correction; the subsequent cache implementation is described in Capture above.

Evidence: `tmp/c89-preview-render-{standalone,native-tests,sdl-release-tests,sdl-debug-tests,asan}.log`,
`tmp/c89-preview-render-final-{native,sdl-release,sdl-debug,standalone}-tests.log`,
`tmp/c89-preview-render-scene-{native,sdl-release,sdl-debug}.log`,
`tmp/c89-preview-render-background-{native,sdl-release,sdl-debug}.log`,
`tmp/c89-preview-render-trace-comparison.json` and
`tmp/c89-preview-render-output-comparison.json`.


### Automatic capture lookup (15 November 2026)

`playback_cache.py` adds a per-run input index to existing schema-1 databases.
Only successful keyed captures with intact media are selected; cache reuse creates
new run IDs and preserves source run/suite provenance. Original observations and
comparison history remain unchanged. Repetitions keep distinct source captures,
and reused runs resolve directly to original captures rather than alias chains.
Retention follows those references before deleting old raw media.

Nine cache tests and all 37 existing harness tests pass using tiny synthetic
movies, images, PCM and mocked sessions. Coverage includes a no-session second
suite, SDL-only invalidation after rebuilding SDL, fresh comparison policies,
explicit refresh/cache-only modes, media/input corruption, independent repetition
indexes, legacy misses, copied sound/audio metadata and original-media retention.
No 3DMM, SDL Studio or Marcus process was launched for this verification.

### Preview picking policy and scratch ownership (15 November 2026)

`preview_pick.c` now owns projection validation, viewport/letterbox coordinates,
authored/output raster sizing, camera construction and singular-camera rejection,
stable body copying and actor ownership, private prepared-face storage, hit
ordering, orthographic depth normalization, authored background occlusion and
indexed-texture transparency. Rendering and picking share stable body copying.
Typed adapters retain BRender field access, fixed-math face-plane repair, matrix
inversion/projection, UV transformation and SDK pick calls. The C owner frees its
world before its scratch storage on every exit. Failed picks publish object -1
and infinite distance. Source geometry, actors and textures remain borrowed.

Before conversion, the headless Scene Preview fixture recorded 2,288 sampled
picks and 80 invalid-projection results across two frames and eight authored,
output-resolution, orbit, free, axis and orthographic configurations. It also
checks that picking leaves subsequent rendered pixels unchanged. Original debug
and optimized SDK picking differed: unguarded native/SDL debug traces contain six
hits and FNV-1a `8F6FCC06`; unguarded SDL release contains 402 hits and FNV-1a
`09823C62`. The conversion exposed the cause: `ActorPick2D` ignored a failed
fixed-point inverse and passed an unwritten matrix into ray/bounds testing.
Orthographic normalization can underflow that inverse's determinant. Changed
stack layout changed the bogus hits, so those undefined results cannot serve as
a preservation gate.

The SDK now skips model/bounds ray tests after inverse failure and rejects a
failed camera inverse before traversal. The bounds assertion now checks the
current actor rather than an uninitialized iterator. A direct SDK test checks
singular models/cameras/bounds, orthographic determinant underflow and subsequent
valid-pick recovery. Ill-conditioned orthographic transforms yield no hit;
higher-precision ray construction is not introduced by this slice.

Both original and converted controllers are checked with the same SDK guard.
The corrected 74,816-byte original trace has six hits and FNV-1a `8F6FCC06` on
all three builds; the Windows MSVC fixture freezes this result. Raw unguarded
original traces are retained separately as
`tmp/c89-preview-pick-unguarded-original-{native,sdl-release,sdl-debug}.bin`.
The original release discrepancy is an explicitly diagnosed SDK correction,
not a relaxed comparison or proof of retail picking equivalence.

C-only tests cover stable draw-order ties, nearest/equal-depth hits, clip limits,
unowned mesh occlusion, transparent holes, padded texture rows/base coordinates,
negative UV wrapping, opaque/nonindexed maps, orthographic distance conversion,
authored depth sampling, fitted/output coordinates and letterboxes. They inject
every C allocation failure and world/camera/inverse/tree/mesh/renderer failures,
checking resource balance and unpublished results. Scratch reuse changes
allocation ordinals; these checks establish resource contracts rather than
retail allocation-failure equivalence.

No Marcus UI tests were added. This slice changes picking, with unchanged render
traces used to check the shared body-copy refactor. Retained retail playback
evidence remains the behavioral reference; no new retail playback or editing
equivalence is claimed. Camera interaction, asset/scene admission, presentation,
broader platform validation and final C-only host/facade integration remain work.

All 110 standalone C checks (including strict C89 syntax), 14 selected native,
10 SDL release and 10 SDL debug checks pass. The C fixture passes MSVC
AddressSanitizer. The SDK-dependent checks include the direct inverse test,
existing Marcus renderer/playback/player integration and Studio's existing
World-camera/picking trace. Both SDL Studio binaries were rebuilt; Marcus was
stopped before rebuilding and relaunched afterward.

All six original/converted trace comparisons match: picking is 74,816 bytes,
SHA-256 `A44EF24F0F4BAD61AFB9B1D3930B9684708D3B1C0AA93E52EBAC060AF7D80722`,
and rendering remains 18,029,304 bytes, SHA-256
`7AF798CC29A5020C54BFDA84A8A74A6D8673E1976AE290C24F5BDB7E18CB0943`,
on native, SDL release and SDL debug builds. The picking comparison uses the
guarded original controller described above; render traces retain the preceding
rendering revision's fingerprint. These are Windows x64 preservation checks,
with the explicit invalid-inverse correction above, not new retail parity claims.

Evidence: `tmp/c89-preview-pick-{baseline,final}-{native,sdl-release,sdl-debug}.{bin,log}`,
`tmp/c89-preview-pick-render-final-{native,sdl-release,sdl-debug}.bin`,
`tmp/c89-preview-pick-{native,sdl-release,sdl-debug,standalone}-tests.log`,
`tmp/c89-preview-pick-world-{native,sdl-release,sdl-debug}-tests.log`,
`tmp/c89-preview-pick-asan.log`, `tmp/c89-preview-pick-trace-comparison.json`
and input/final-binary hashes in `tmp/c89-preview-pick-identities.json`.

### Preview camera interaction and guide metadata (15 November 2026)

`preview_camera.c` now owns shot matching and letterboxed FOV/focus fitting,
orbit panning and world-limit clipping, reset, placement/shape anchor updates,
posed bounds corner accumulation, camera index admission and selection's
same-camera/failure/publication control. Native adapters provide typed bounds,
camera/bitmap fields, references and math callbacks. Float rounding and double
corner accumulation retain the original arithmetic order.

Camera guides now use a persistent C descriptor/status cache. Metadata queries
read camera headers and colour bitmap bounds without selecting a camera,
decoding depth or rebuilding scenery. Successful and failed reads are cached;
reset retains allocation capacity. The count publishes only after both buffers
are available. This fixes the native two-vector allocation failure case: a
descriptor resize followed by status-allocation failure could leave a short
status vector that the next query indexed out of bounds. C fault checks verify
safe retry, cache invalidation, cached read failures, bitmap reference balance,
and malformed bounds rejection before signed subtraction overflow. Allocation
ordinals differ; retail allocation-failure equivalence is not claimed.

The original native, SDL release and SDL debug headless Scene fixtures all
recorded the same 5,728,850-byte camera trace, FNV-1a `5A0D32E5`, SHA-256
`2A0227370CD0291C422080AC1891CE5E1B019E7AC445EE2C41D7D54367C1F4F2`.
It covers two scene frames, guide reads/repeats/invalid indexes, placement and
posed bounds, four output aspects, four perspective/orthographic/zoom/roll
configurations, three pan steps including world-limit clamping, invalid camera
inputs, and placement/shape edits followed by rendering. Queries verify that
the selected camera and evaluated frame remain unchanged. The Windows x64
fixture freezes the original fingerprint.

All nine original/converted camera, picking and rendering traces match across
the three builds. The existing picking trace remains 74,816 bytes/FNV-1a
`8F6FCC06`, and rendering remains 18,029,304 bytes/FNV-1a `A31B99A6`.
All 111 standalone C checks including strict C89 syntax, nine selected native,
five SDL release and five SDL debug checks pass. The C fixture passes MSVC
AddressSanitizer. The existing background checks retain 301,996,032 bytes/FNV-1a
`E1A71005` across 12 backgrounds and 188 cameras in each build. Both Studio
binaries and Marcus were rebuilt; Marcus was stopped before rebuilding and
relaunched afterward. Input movie/content/runtime-resource hashes are unchanged.

No Marcus UI tests were added. These are Windows x64 preservation checks, not
new retail playback/editing equivalence claims. Existing retail captures remain
the reference; no redundant capture campaign was launched. Preview asset/scene
admission, live definition/timeline editing, presentation and final C-only
host/facade integration remain work. Other platforms remain unverified here.

Evidence: `tmp/c89-preview-camera-{baseline,final}-{native,sdl-release,sdl-debug}.{bin,log}`,
`tmp/c89-preview-camera-{pick,render}-final-{native,sdl-release,sdl-debug}.bin`,
`tmp/c89-preview-camera-{native,sdl-release,sdl-debug,standalone}-tests.log`,
`tmp/c89-preview-camera-background-{native,sdl-release,sdl-debug}.log`,
`tmp/c89-preview-camera-asan.log`, `tmp/c89-preview-camera-trace-comparison.json`
and `tmp/c89-preview-camera-identities.json`.

### Preview live definitions, actor ownership and timeline control (15 November 2026)

`preview_live.c` now owns live scene/prop definitions, opaque actor handle arrays,
camera cues and original snapshots. It controls added-prop prefix reconciliation,
replacement/rollback, virtual scene removal, scene range and camera edits, and
seek failure/publication. The native owning actor/definition/camera vectors and
their control loops are removed. Native adapters marshal fields, supply typed
actor/resource effects and transport exceptions after the C owner has unwound.
Native actor views read C handles without aliasing a `void **` as typed pointers.
Stored actor resolvers capture the session owner, not a temporary service host.

Retained prop numbers keep the existing added prefix even when other definition
fields change. A mismatch creates and seeks a replacement suffix before releasing
the old suffix. Failure releases the current actor, then the pending prefix;
completed seek/visibility-clock effects remain. Ordinary reconciliation failure
restores both definition snapshots. Allocation/foreign exceptions retain completed
scene/prop assignment prefixes, including a published scene list when prop
allocation fails. Snapshot-preparation failure retains the old lists. Adoption
transfers ownership only after storage succeeds. Failed camera-cue adoption now
releases the native frame-event group before the asset loader reports allocation
failure; the previous vector exception path leaked that group.

Opening camera shifts reuse Studio's `mm_scene_move_first`, including prefix
rotation order. Camera edits restore original cues/range, reapply extensions,
replace equal-frame cues in input order, stable-sort and seek the clamped frame.
Sort scratch allocation failure uses a stable, allocation-free fallback. Original
cue lookup retains wire order and its last qualifying record. Renderer diagnostic
clearing/checking remains specific to frame seeks, rather than newly rejecting
reconciliation work. Frame/stats publish only after all actor/camera effects
succeed. Camera-edit preparation exceptions remain distinct from allocation
errors handled by its final nested frame seek.

The original native, SDL release and SDL debug fixtures all recorded 819,080
bytes, FNV-1a `C3B1C4A9`, SHA-256
`D0A9A094AB9ECDF2E34EF4ECE45B622A464AF13F14980C7BD837F1B481196F79`.
The trace covers range extensions/rejections, unsorted/repeated/sentinel camera
edits, seeks/replay, prop reuse/replacement/removal, failed reconciliation with a
created prefix, definition rollback, virtual scene lifetime, actor state and
rendered output. The Windows x64 fixture freezes this original fingerprint.

All twelve original/converted live, camera, picking and rendering traces match
across the three builds. The prior 5,728,850-byte camera, 74,816-byte picking and
18,029,304-byte rendering traces retain their hashes. All 112 standalone C checks
including strict C89 syntax, ten selected native checks, six SDL release and six
SDL debug checks pass. MSVC AddressSanitizer passes. C fault checks cover snapshots,
assignment prefixes, adoption/sealing, actor/camera failures and foreign exceptions,
allocation retry, stable-sort fallback, release order and unpublished frame/stats.
The background check retains 301,996,032 bytes/FNV-1a `E1A71005` across 12
backgrounds and 188 cameras in each build. Input hashes are unchanged. Both Studio
binaries and Marcus were rebuilt; Marcus was stopped before rebuilding and
relaunched afterward. No Marcus UI tests were added.

This is Windows x64 evidence within the stated fixtures. Allocation attempt
ordinals change with C storage; the tests establish explicit ownership/failure
contracts, not complete retail allocator or editing equivalence. Retained retail
captures remain the reference; no redundant capture campaign was launched.
PreviewActor construction/loading/evaluation and resource/track representations
remain native providers. Asset/source/scene admission, presentation and final
C-only host/facade integration remain work; other platforms remain unverified here.

Evidence: `tmp/c89-preview-live-{baseline,final}-{native,sdl-release,sdl-debug}.{bin,log}`,
`tmp/c89-preview-live-{camera,pick,render}-final-{native,sdl-release,sdl-debug}.bin`,
`tmp/c89-preview-live-{native,sdl-release,sdl-debug,standalone}-tests.log`,
`tmp/c89-preview-live-background-{native,sdl-release,sdl-debug}.log`,
`tmp/c89-preview-live-asan.log`, `tmp/c89-preview-live-trace-comparison.json`
and `tmp/c89-preview-live-identities.json`.

### Preview dependency budgets and asset/scene admission (15 November 2026)

`preview_admission.c` now controls archive dependency budgets, asset selection,
template action discovery/rest pose, scene start/frame traversal, actor adoption,
virtual scene admission, pose validation and geometry counts. Native code supplies
typed resource/group/actor/SDK primitives, normalized event fields, display strings
and status marshaling. Admission reuses the C live owner for actors, camera cues,
snapshots, reconciliation and seeking. Stored actor resolvers capture the session
owner by value rather than a temporary admission host.

Budgets retain packed-header-before-size-check ordering, inclusive 64 MiB stored/
decoded chunk and 512 MiB decoded archive limits, and the original chunk-marking
policy. Template discovery stops on failed/nonpositive cel count, selects the
first matching nonempty action name, applies the initial cel before the X/post-Y/
post-Z rest orientation, and preserves fallback display names. Scene admission
retains wire actor/camera order, last start background/opening camera, evaluated
standalone actor ranges and offstage-actor rejection. Geometry statistics retain
visible-body counting and the standalone model override.

Early failures retain completed metadata/resource prefixes; actor-load failures
save the source error before clearing the asset. Final dependency failures clear
and mark the selected chunk. Selection allocation failures clear; foreign
exceptions retain completed prefixes. Acquired groups and unadopted actors are
released before the typed adapter reports/rethrows failure. This closes the old
start-group leak when the actor-number vector threw during event traversal.
Pose exceptions retain state; an ordinary pose-effect failure clears ready.
Virtual selection retains its nested-seek diagnostic policy.

Before conversion, the native, SDL release and SDL debug API fixtures recorded
identical 17,862,716-byte traces, FNV-1a `D363793C`, SHA-256
`AB94395A3F9157BE62F2E81A0787C08F8ED63632A2D110E26CCA60235CF3FCFE`.
The trace includes all 45 actor and 21 prop templates, action names/cel counts,
geometry metadata, initial rendered poses, one final action/cel pose, raw model
output, invalid pose indices and failed selections. Its Windows x64 fingerprint
is frozen in the existing headless API fixture. All fifteen original/converted
admission/live/camera/picking/rendering trace pairs match across the three hosts.
The background check retains 301,996,032 bytes/FNV-1a `E1A71005` across 12
backgrounds and 188 cameras per build.

All 113 standalone C checks including strict C89 syntax, eleven selected native
checks, seven SDL release and seven SDL debug checks pass. MSVC AddressSanitizer
passes. C fault checks cover exact budget boundaries/header failures, action
truncation/naming/rest order, ordinary and allocation/foreign effect failures,
storage exhaustion, event-group cleanup, failed actor prefixes, adoption/sealing,
sync/seek failure, virtual scenes, pose readiness and statistics. Input hashes are
unchanged. Studio and Marcus were rebuilt; Marcus was stopped before rebuilding
and relaunched afterward. No Marcus UI tests were added.

This is Windows x64 evidence within these fixtures, not a new retail editing or
allocator-equivalence claim. C storage changes allocation attempt ordinals.
Retained retail captures remain the playback reference; no redundant capture
campaign was launched. PreviewActor construction/loading/evaluation and resource/
track representations remain native providers. Source registration/session setup,
presentation and final C-only host/facade integration remain work. Other platforms
remain unverified here.

Evidence: `tmp/c89-preview-admission-{baseline,final}-{native,sdl-release,sdl-debug}.bin`,
`tmp/c89-preview-admission-{live,camera,pick,render}-final-{native,sdl-release,sdl-debug}.bin`,
`tmp/c89-preview-admission-{api,background}-{native,sdl-release,sdl-debug}.log`,
`tmp/c89-preview-admission-{native,sdl-release,sdl-debug,standalone}-tests.log`,
`tmp/c89-preview-admission-asan.log`, `tmp/c89-preview-admission-trace-comparison.json`
and `tmp/c89-preview-admission-identities.json`.

### Preview actor ownership, loading and live playback (15 November 2026)

`preview_actor.c` now owns the actor header, evaluated state, saved orientation,
edit shape, original placement and template/body handles. It controls loading,
prop creation, live frame evaluation/seeking, positioning, translation, absolute
offsets and shape basis queries. Native PreviewActor keeps borrowed typed resource
views and supplies track/resource/SDK/effect primitives. It has no second evaluated
state or live replay/placement loop. Shared C actor timeline/frame/seek/effect/
sound/pose modules retain the Studio playback policy and fixed matrix arithmetic.

Loading preserves header/route/event/template/body prefixes on failure, reads
inactive roll-call actors without requiring templates, overrides embedded template
numbers only for the local source, and validates node references after body
creation. Old-header lifetimes use the same shared evaluator with the original
100,000-frame work bound. Prop creation retains template admission and stationary
route/entrance/action/zero-step insertion order, including partial insertion on
failure. Disposal releases the body before its template, followed by the existing
C-backed native track facade's authored tables.

Live replay retains cue clearing/display gates, costume reset, entrance and held-
rotation orientation resets, frame failure/retry, quick backup and full replay.
Shape editing composes onto saved orientation while retaining the current animation
stretch/scale. Placement validates all axes before publishing. Untouched retail
placements still bypass editor limits and requantization; zero-axis absolute
offsets preserve authored fixed bits. Completed actor state/effects remain after
provider failure; typed adapters transport allocation/foreign exceptions after
the C controller returns.

All fifteen original/converted admission/live/camera/picking/rendering trace pairs
match across native Debug, SDL Release and SDL Debug. Their prior 17,862,716,
819,080, 5,728,850, 74,816 and 18,029,304-byte outputs retain their recorded
fingerprints and SHA-256 hashes. This includes ordinary/old/inactive actors,
3-D text/font resolution, seeks/rewind, live prop replacement, camera edits,
held-orientation shape/placement edits, picking and subsequent rendered output.
Each background check retains 301,996,032 bytes/FNV-1a `E1A71005` across 12
backgrounds and 188 cameras.

All 114 standalone C checks including strict C89 syntax, twelve selected native
checks, eight SDL release and eight SDL debug checks pass. MSVC AddressSanitizer
passes. The C actor fixture covers ordinary/allocation/foreign failures at load,
create and replay effects; resource release order and idempotent disposal; inactive
and corrupt routes; lifetime boundaries and resolver exceptions; event insertion
prefixes; replay/backup/hide; orientation resets; atomic rejected movement; untouched
retail coordinates; shape edits and pose failure. Input identities are unchanged.
Studio and Marcus were rebuilt; Marcus was stopped before rebuilding and relaunched
afterward. No Marcus UI tests were added.

This is Windows x64 evidence within these fixtures, not complete retail allocator
or editing equivalence. Retained retail captures remain the playback reference;
no redundant capture campaign was launched. Typed effect/resource primitives,
track representations, visibility-clock/cue containers, timeline extraction/
presentation, source/session setup and final C-only host/facade integration remain
dependencies. Other platforms remain unverified here.

Evidence: `tmp/c89-preview-actor-{final,live-final,camera-final,pick-final,render-final}-{native,sdl-release,sdl-debug}.bin`,
`tmp/c89-preview-actor-{api,background}-{native,sdl-release,sdl-debug}.log`,
`tmp/c89-preview-actor-{native,sdl-release,sdl-debug,standalone}-tests.log`,
`tmp/c89-preview-actor-asan.log`, `tmp/c89-preview-actor-trace-comparison.json`
and `tmp/c89-preview-actor-identities.json`. Original traces are retained under
the admission/live/camera evidence paths recorded above.

### Preview session replacement, runtime lease and sources (15 November 2026)

`preview_session.c` now owns the Preview runtime lease, session running/root-cache
state, orbit state and ordered opaque source registry. It controls transferred/
empty session opening, resource/budget/palette admission, runtime initialization,
cache/shade setup, replacement publication, source registration/resolution and
ordered teardown. The native source vector/IDs and runtime/replacement/lookup
controllers are removed. Typed adapters retain archive/path/palette representations,
resource-cache and SDK primitives, asset-cleanup services and error text.

Input-transfer validation precedes diagnostic reset and candidate construction.
Candidates consume input archives before resource validation, while retaining the
active session. Palette admission preserves 256 four-byte entries, foreign-order
swapping, display-palette resolution and shade lookup. Resources/budgets/palette
validation precedes runtime-lease admission. Only a non-running owner starts the
runtime/depth backend; a different owner's lease rejects the candidate. Root cache
and shade setup precede ownership transfer, optional orbit copying, publication
and immediate disposal of the old owner. Failed replacements retain the active
owner and lease without rolling back completed global SDK effects.

Source registration requires a running session and positive ID, suppresses exact
ID/path duplicates, and transfers ownership to C storage only after open/budget/
cache checks. Failure releases the temporary source and retains the registered
prefix. Resolution checks root sid zero first, then matching registered IDs in
insertion order, continuing when an earlier archive lacks the requested chunk.
Disposal clears live assets/tables, releases sources forward and root cache, then
shared material/shade state, depth rendering and BRender. Ordinary errors are
formatted before candidate disposal; allocation/foreign exceptions are transported
after cleanup. C storage changes allocation attempt ordinals.

Before conversion, native Debug, SDL Release and SDL Debug recorded identical
89,578-byte session traces, FNV-1a `34267E40`, SHA-256
`C0D6B5FD6DCF426F3D19E16F93E2D8FAD1797E47AEC1778DB189F7BD623313F4`.
The new headless fixture freezes empty sessions, invalid transfers/source IDs,
missing archives/resources, failed replacement isolation, source ordering and
duplicates, successful replacement with orbit preservation, registry reset,
runtime ownership/handoff/restart, exact errors/metadata and rendered output.
All eighteen original/converted session/admission/live/camera/picking/rendering
trace pairs match across the three hosts. Earlier trace hashes remain unchanged.
Each background check retains 301,996,032 bytes/FNV-1a `E1A71005` across 12
backgrounds and 188 cameras.

All 115 standalone C checks including strict C89 syntax, thirteen selected native
checks, nine SDL release and nine SDL debug checks pass. MSVC AddressSanitizer
passes. C fault checks cover all fallible open/add stages, fresh versus replacement
lease ownership, error-before-disposal order, malformed/missing/foreign palettes,
framing preservation, source lookup/duplicates, release order, registry exhaustion
and retry. Input identities are unchanged. Studio and Marcus were rebuilt; Marcus
was stopped before rebuilding and relaunched afterward. No Marcus UI tests were
added. The regular `asset-previews` harness now runs the session fixture; its
headless native invocation passes with a freshly generated scene and unchanged
input hashes.

This is Windows x64 evidence within these fixtures, not complete retail allocator
or editing equivalence. Retained retail captures remain the playback reference;
no redundant capture campaign was launched. Typed resource/track/archive/path and
palette representations, asset-cleanup primitives, cue/timeline presentation and
final C-only host/facade integration remain dependencies. Other platforms remain
unverified here.

Evidence: `tmp/c89-preview-session-{baseline,final}-{native,sdl-release,sdl-debug}.{bin,log}`,
`tmp/c89-preview-session-{admission,live,camera,pick,render}-final-{native,sdl-release,sdl-debug}.bin`,
`tmp/c89-preview-session-{api,background}-{native,sdl-release,sdl-debug}.log`,
`tmp/c89-preview-session-{native,sdl-release,sdl-debug,standalone}-tests.log`,
`tmp/c89-preview-session-asan.log`, `tmp/c89-preview-session-trace-comparison.json`,
`tmp/c89-preview-session-regular-tests.log` and `tmp/c89-preview-session-identities.json`.
Prior traces remain at the evidence
paths recorded above.

### Preview timeline replay and presentation cue traversal (15 November 2026)

`preview_metadata.c` now controls private actor metadata replay, marker
classification, span splitting, replay/output budgets, scene presentation traversal,
actor/scene cue mapping, pause validation and stable cue ordering. Native adapters
provide cloned/validated tables, shared actor effects, caption decoding, display
formatting and public output containers. The native timeline/frame and scene-cue
loops are removed; shared C frame/step/effect evaluators retain Studio playback
policy in the private metadata host.

Metadata replay changes only a private track's frame caches, suppresses sound
effects and preserves live pose/cues/rendering. The replay budget remains 100,000
frames from the actor's first frame. The 100,000-marker check still precedes marker
classification, including ignored sound rows after the limit. Spans retain action/
entrance restart, visibility-gap and freeze boundaries; two-word ends represent
`INT32_MAX+1` without C99 types. Ordinary/exception failures retain completed output
prefixes and release the private clone before the typed host returns/rethrows.

Presentation visits start then frame groups, preserves sound-sheet/caption mode
gates and inactive actors, validates pause bounds, and retains scene packet queue
indices and actor ternary flags. Live actor cues and sound sheets use the same C
mapper. Actor event groups release before containing scene groups, closing the
previous raw-group leaks when output-vector allocation threw. Cue origin identity
publishes before a stable frame/scene-before-actor merge sort. Scratch failure uses
an allocation-free stable merge with bounded recursion, avoiding a quadratic
insertion fallback for large cue sheets.

Before conversion, the native Debug, SDL Release and SDL Debug metadata fixtures
all recorded 1,045,462 bytes, FNV-1a `0BD333B2`, SHA-256
`9C49B1E34B65B9AF9ADBB261EC4BDC03B9B8FA151B2CF8C7227A4315D112BD71`.
Five documents exercise batch captions/pauses, actor animation events, scene audio,
actor audio and text-acid captions/properties. The trace freezes all timeline
labels/details/spans, caption properties, pauses, sound-sheet ordering and audio
bytes, repeated seeks/frame cues, actor state and rendered output before/after
metadata reads. Its Windows x64 fingerprint is checked by the new headless fixture.
The regular asset-preview harness generates all five documents, runs this fixture
and verifies that input document hashes remain unchanged.

All twenty-one original/converted metadata/session/admission/live/camera/picking/
rendering trace pairs match across the three builds. Prior hashes remain unchanged.
The background check retains 301,996,032 bytes/FNV-1a `E1A71005` across 12
backgrounds and 188 cameras per build. All 116 standalone C checks including strict
C89 syntax, fourteen selected native checks, ten SDL release and ten SDL debug
checks pass. MSVC AddressSanitizer passes. C-only checks cover clone/live isolation,
ordinary/allocation/foreign effect failures, partial output, group release order,
inactive and clipped timelines, frame/marker budgets, `INT32_MAX+1` spans, pause
rejection, actor cue flags/packet queueing and sort stability with full/partial/no
scratch for every count from zero through 64. Input identities are unchanged.
Studio and Marcus were rebuilt; Marcus was stopped before rebuilding and relaunched
afterward. The expanded regular asset-preview harness passes headlessly in the
native build, including fresh generation and input-hash checks for all five
metadata documents. No Marcus UI tests were added.

This is Windows x64 evidence within these fixtures, not complete retail editing
or allocator equivalence. Clone/sort storage changes allocation attempt ordinals.
Retained retail captures remain the playback reference; no redundant capture
campaign was launched. Caption/audio loaders, label/font/numeric formatting,
name-cache/output/cue representations, physical providers and final C-only host/
facade integration remain work. Other platforms remain unverified here.

Evidence: `tmp/c89-preview-metadata-{baseline,final}-{native,sdl-release,sdl-debug}.{bin,log}`,
`tmp/c89-preview-metadata-{session,admission,live,camera,pick,render}-final-{native,sdl-release,sdl-debug}.bin`,
`tmp/c89-preview-metadata-{native,sdl-release,sdl-debug,standalone}-tests.log`,
`tmp/c89-preview-metadata-asan.log`, `tmp/c89-preview-metadata-trace-comparison.json`,
`tmp/c89-preview-metadata-regular-tests.log`
and `tmp/c89-preview-metadata-identities.json`. The five metadata input documents
and their generation logs are retained as `tmp/c89-preview-metadata-{batch,events,audio,actor-audio,text-acid}.3mm`
and matching `*-fixture.log` files. Prior original traces remain at the evidence
paths recorded above.

### Preview caption and audio loader control (15 November 2026)

`preview_media.c` now controls caption input/scratch ownership, text limits and
UTF-16 conversion sequencing, property positions/byte order, font-argument
admission and stable ordering. Native services retain rich-text/text-box
persistence, serialized font-name decoding, display conversion and output storage.
The C controller releases scratch before its input owner on all exits. Completed
output prefixes remain observable on failure; native exceptions rethrow after
cleanup. Caption scratch exhaustion keeps allocation-exception classification.
Property sorting preserves ties and formatted font names, with a bounded-recursion
stable in-place merge when either sort scratch allocation fails.

Audio loading now controls the single MSND envelope, byte-order/invalid checks,
silent success for invalid/missing children, 64 MiB payload limit and raw wave
publication in C. It reads MIDI into C-owned bytes and uses the shared C parser
directly, removing Preview's native MidiStream/parser/HQ dependency. Empty MIDI
and malformed tails retain parser-exhaustion success. The million-channel-event
and 24-hour budgets precede channel filtering; exactly 24 hours is accepted.
MIDI scratch exhaustion retains the former payload factory's decode-failure
classification. Device/codec policy is unchanged.

Public sound lookup also uses C control. Silent cues bypass readiness/source
publication; imported-scene children precede the first matching movie child.
Failed child lookups leave the requested sound number intact, and source identity
publishes only after successful decoding. A typed null-cache guard handles ready
sessions without a backing archive by reporting the missing source.

Before conversion, native Debug, SDL Release and SDL Debug recorded identical
1,275-byte media traces, FNV-1a `1B3D6107`, SHA-256
`40046FD25856E960E30A610CAB007C2F80BCF329F08002C88363055FC7155C4B`.
The new headless fixture freezes native/foreign/invalid/missing-child MSND,
wrong marker/size, raw wave bytes, MIDI running/system/tempo records, empty and
malformed tails, time boundaries, ANSI/UTF-16 captions, foreign properties,
font arguments and partial failure output. The regular asset-preview harness
runs this fixture as well as the existing five-document metadata probe.

All twenty-four original/converted media/metadata/session/admission/live/camera/
picking/rendering trace pairs match across the three builds. Prior hashes remain
unchanged. The background check retains 301,996,032 bytes/FNV-1a `E1A71005` across
12 backgrounds and 188 cameras per build. All 117 standalone C checks including
strict C89 syntax, fifteen selected native checks, eleven SDL release and eleven
SDL debug checks pass. MSVC AddressSanitizer passes. C-only failure checks cover
every fallible caption/audio effect, scratch exhaustion and release order,
conversion/publication prefixes, property byte order/arguments, stable sorting
with full/partial/no scratch for counts zero through 64, MIDI event/time limits,
silent/readiness gates, scene/movie remapping and successful-source publication.
Input identities are unchanged. Studio and Marcus were rebuilt; Marcus was
stopped before rebuilding and relaunched afterward. The expanded regular
asset-preview harness passes headlessly in the native build, including fresh
document generation and input-hash checks. No Marcus UI tests were added.

This is Windows x64 evidence within these fixtures, not complete retail editing
or allocator equivalence. Input/scratch storage changes allocation attempt
ordinals. Retained retail captures remain the playback reference; no redundant
capture campaign was launched. Rich-text/text-box persistence, label/font/numeric
formatting, name-cache/output/cue representations, physical providers and final
C-only host/facade integration remain work. Other platforms remain unverified here.

Evidence: `tmp/c89-preview-media-{baseline,final}-{native,sdl-release,sdl-debug}.{bin,log}`,
`tmp/c89-preview-media-{metadata,session,admission,live,camera,pick,render}-final-{native,sdl-release,sdl-debug}.bin`,
`tmp/c89-preview-media-{native,sdl-release,sdl-debug,standalone}-tests.log`,
`tmp/c89-preview-media-asan.log`, `tmp/c89-preview-media-trace-comparison.json`,
`tmp/c89-preview-media-regular-tests.log` and `tmp/c89-preview-media-identities.json`.
The media fixture creates `tmp/c89-preview-media-{native,sdl-release,sdl-debug}.3cn`;
the ten retained metadata/scene/content inputs have the identities recorded above.

### Stored rich-text and text-box persistence (15 November 2026)

`text_store.c` now implements the RTXT/TBOX wire codecs, stored rich-text owning
record, read replacement transaction, file/collection disposal and stored-write
rollback. Native data facades supply file/block, counted-string and C-backed
collection primitives. Studio's rich-text document reader and Marcus's caption
loader use the same C stored-text path; root creation also serves Studio's live
document writer. Native descriptor fields remain temporary representations.

TBOX retains its exact 36-byte requirement, both byte orders, `INT32_MIN` first-frame
rejection, zero/one story validation, ignored OS/reserved bytes and optional child
resolution. Backward lifetimes and `INT32_MIN` end frames remain accepted. Reads
publish current frame `first-1`; writes normalize reserved bytes and byte order.
RTXT retains a minimum 16-byte header, half-open font-height/width limits and empty/
unreadable-font fallback. TEXT's raw system marker, property/argument byte orders,
fixed words and payload bytes are preserved without content normalization.

The C reader retains the borrowed text-file reference before reading its marker,
checks GLMP's eight-byte entries and optional AGPA's four-byte fixed data, and
releases temporary owners on every failure. A successful read releases the old
file, properties and arguments, then publishes the complete replacement. Stored
writes validate before changing the caller's identity, publish RTXT before root
allocation and nil after failed creation, and write TEXT, GLMP and nonempty AGPA
in that order. An acquired text-copy destination releases before partial-tree
rollback. Native exceptions rethrow only after C cleanup. The native output adapter
uses an untouched marker and never reads an uninitialized caller output.

Before conversion, native Debug, SDL Release and SDL Debug recorded identical
4,998-byte stored-text traces, FNV-1a `7D32092F`, SHA-256
`ABDB53F25C473072E8FB468868E0FC4A372C70F0EAE20AEA94512CEFE8605D8C`.
The new headless fixture freezes decoded state and complete written root/child
payloads for native/foreign headers, malformed sizes/markers/layouts, font fallback,
missing children, preserved state, optional/empty arguments, exact wire bytes and
read/write/read round trips. Its argument payloads are explicitly aligned to avoid
recording uninitialized legacy collection padding; production data is unchanged.

All twenty-seven original/converted stored-text/media/metadata/session/admission/
live/camera/picking/rendering trace pairs match across the three hosts. Prior hashes
remain unchanged. Background checks retain 301,996,032 bytes/FNV-1a `E1A71005`
across 12 backgrounds and 188 cameras per host. All 118 standalone C checks,
including strict C89 syntax, pass. Twenty-three selected native checks and nineteen
checks in each SDL configuration pass, including existing Kauai/engine core-link,
scene load/store/content and movie save/load tests. MSVC AddressSanitizer passes.
C-only fault checks cover every read/write effect with ordinary/allocation/foreign
failures, owned outputs returned on failure, optional font/argument behavior,
temporary/old owner release order, copy cleanup and output identity publication,
both byte orders and one-/two-byte counted font units.

The regular asset-preview harness now runs the stored-text fixture and passes
headlessly in the native build, including fresh metadata document generation and
input-hash checks. Retained input identities are unchanged. Studio and Marcus were
rebuilt; the workspace Marcus was stopped before rebuilding and relaunched afterward.
No Marcus UI tests were added. Retained retail captures remain the playback
reference; no redundant capture campaign was launched.

This is Windows x64 evidence within these fixtures, not complete retail editing or
allocator equivalence. Live rich-text document save/redirection, editing/font/
argument transformations, native descriptor/output representations, physical
providers and final C-only host/facade removal remain work. Other platforms remain
unverified here.

Evidence: `tmp/c89-text-store-{baseline,final}-{native,sdl-release,sdl-debug}.{bin,log}`,
matching `*-errors.log` files for expected malformed-input diagnostics,
`tmp/c89-text-store-{media,metadata,session,admission,live,camera,pick,render}-final-{native,sdl-release,sdl-debug}.bin`,
`tmp/c89-text-store-{native,sdl-release,sdl-debug,standalone}-tests.log`,
`tmp/c89-text-store-asan.log`, `tmp/c89-text-store-trace-comparison.json`,
`tmp/c89-text-store-regular-tests.log` and `tmp/c89-text-store-identities.json`.

### Live rich-text chunk saves and text-box adoption (15 November 2026)

`text_live.c` now controls live rich-text chunk creation, native stream-window
sequencing, ordinary save-error publication, post-commit text redirection and
text-box adoption/metadata rollback. Native `RichTextDocument::FSaveToChunk` and
`TextBox::FWrite` marshal borrowed state and supply file/block/stream primitives.
Root wire encoding/creation uses the shared stored-text module.

Live saves publish RTXT before header validation. A number-changed flag allows
the facade to preserve an untouched, potentially uninitialized caller number on
early failure. Root creation retains nil on failure; later child rollback retains
the created number. TEXT always writes its native system marker and moves past it
before stream copying, including empty content, with the stream sentinel excluded.
GLMP and nonempty AGPA retain native order/system. Failed move diagnoses and
continues like `AssertDo`; ordinary save errors publish after tree rollback.
Negative exception results clean up before native rethrow without an ordinary
save error.

Redirection occurs after the tree commits. Ordinary replacement failure remains
best effort, exceptions retain the saved tree, and acquired locations release on
all exits. A failed location lookup now diagnoses/returns failure without using
a trashed native view. The file primitive still supplies a materialized text view,
which need not alias a later writable chunk view. Text-box saving resets the child
ID before saving, deletes an unlinked root after adoption failure and removes an
adopted link after metadata failure. The child-ID reset and partial metadata output
are not rolled back. Unexpected root types retain diagnostic/continue behavior.

Before conversion, the expanded existing engine movie save/load fixture recorded
2,856-byte live-caption traces in all three builds. Native Debug records FNV-1a
`8FB30D47`, SHA-256
`7E4F735BB4C7457D6A4CD1FCB2F245107084B52056F5D86FB8AE92F791010E5C`.
SDL Release and SDL Debug record FNV-1a `5D914817`, SHA-256
`38FC3C608A7FADABB3A029E9A89A740141A4CCF12F7ED9405CD1E2C3B2D471A7`.
The only host differences are the twelve persisted runtime font-table ordinals:
native assigns Courier New ID 92, SDL assigns ID 18. Each host is compared with
its own original trace; no ordinal normalization was added.

The fixture covers empty/single/embedded-null/CRLF/ANSI text, font/style arguments,
both redirect modes, live stream bytes after writes through the saved chunk view,
and subsequent text-box saves. Root/TEXT/GLMP payloads are recorded byte for byte;
AGPA payload/fixed data are recorded logically to exclude uninitialized alignment
padding. Authored stored inputs are loaded into live documents; editing/undo is
outside this probe's coverage.

All thirty original/converted live-text/stored-text/media/metadata/session/admission/
live/camera/picking/rendering trace pairs match across the three hosts. All prior
hashes remain unchanged. Background checks retain 301,996,032 bytes/FNV-1a
`E1A71005` across 12 backgrounds and 188 cameras per host. All 119 standalone C
checks including strict C89 syntax pass. Twenty-four selected native checks and
twenty checks in each SDL configuration pass, including existing core-link,
scene load/store/content, movie save/load and native playback/player regressions.
MSVC AddressSanitizer passes. C-only tests cover every fallible effect with ordinary,
allocation and foreign failures, output/error order, partial-tree rollback,
post-commit redirection, partially returned owned views, empty text/arguments,
both byte orders/native text-unit sizes and text-box reset/adoption/metadata order.

The regular asset-preview harness passes headlessly with fresh metadata documents
and input-hash checks. Its first concurrent invocation hit the legacy check-then-
create temporary-name race; a private process-local temporary directory isolates
the successful rerun. No production temp-name policy was changed. Retained input
identities are unchanged. Studio and Marcus were rebuilt; the workspace Marcus was
stopped before rebuilding and relaunched afterward. No Marcus UI tests were added.
Retained retail captures remain the playback reference; no redundant capture
campaign was launched.

Native document dimensions marshal explicitly into the fixed-width C fields. After
that final adapter review, both SDL Studios and all three movie save/load fixtures
were rebuilt; each movie fixture was rerun with a private temporary directory and
retained its original live-caption fingerprint.

This is Windows x64 evidence within these fixtures, not complete retail editing
or allocator equivalence. Document file-save/lifecycle, live editing/font/argument
transformations, native descriptor/output representations, physical providers and
final C-only host/facade removal remain work. Other platforms remain unverified here.

Evidence: `tmp/c89-text-live-{baseline,final}-{native,sdl-release,sdl-debug}.bin`,
baseline `*.log`/`*-errors.log` files and `tmp/c89-text-live-{native,sdl-release,sdl-debug}-tests.log`,
`tmp/c89-text-live-{store,media,metadata,session,admission,live,camera,pick,render}-final-{native,sdl-release,sdl-debug}.bin`,
`tmp/c89-text-live-standalone-tests.log`, `tmp/c89-text-live-asan.log`,
`tmp/c89-text-live-trace-comparison.json`, `tmp/c89-text-live-regular-tests.log`
and `tmp/c89-text-live-identities.json`. Final adapter rebuild/rerun logs are
`tmp/c89-text-live-marshal-{native,sdl-release,sdl-debug}{,-build,-errors}.log`.

### Text document file saves and resource lifecycle (15 November 2026)

`text_document.c` now controls plain/rich text file saves, base initialization/
loading, rich initialization, factory failure cleanup and stream/file/archive/
property/argument release. Native typed owner slots remain the storage adapter;
C controls reference transfers and publication. Virtual load/read dispatch is
preserved. The default load hook and chunk save delegate to converted C policies.
Stored-to-live rich property/font/argument materialization remains native work.

Base initialization opens its file before validating a borrowed stream, retains
before publishing that stream and appends a native CR sentinel to it. Published
partial owners remain until object destruction. Default loading retains a mutable
file location across translation and releases its final owner even when translation
replaces it or fails. Empty files skip translation; empty translated results skip
replacement. A virtual load hook may replace the published stream, so sentinel
insertion fetches its current owner again after the hook returns.

Rich open holds the archive privately until its root read succeeds. Ordinary
failure releases it before the read error, while partial live effects remain for
object cleanup. Fresh rich documents retain width-eight entries, grow ten and
height twelve independently of application text size. Factories release partial
objects on initialization/exception failure; chunk-read ordinary errors precede
object release, while constructor allocation failure does not report a read error.
Destruction retains property/argument/archive then base stream/file release order.

Both file-save profiles select the current name and force adoption for normal
saves. Copy saves preserve owner and dirty state. Plain name queries reject
temporary files, although direct normal save still accepts an existing temporary
owner. Plain writes omit the stream sentinel and prepend a native UTF-16 BOM for
two-byte units. Plain redirection and rich chunk redirection precede rename;
ordinary redirect failure remains best effort. Failure releases the temporary
owner without undoing prior stream redirection. Successful rename clears temporary
status, then either releases a copy or releases the old owner, transfers the new
one and clears the dirty bit. Only rich ordinary failures publish save errors.

Before conversion, the expanded existing engine movie save/load fixture recorded
1,612-byte document traces in all three builds. Native Debug records FNV-1a
`88276D87`, SHA-256
`4C7A104FEF7E18153E67C7E2C4238B1995039622A7B196163E69CAD355574A1D`.
SDL Release and SDL Debug record FNV-1a `85C06CCF`, SHA-256
`6CDE541DB16D080E9CF925B28F2D104BEE6EDB39D6CB56D65946EA7A6CB1DE17`.
Each host retains its own font-table baseline. The fixture covers empty and
embedded-null/CRLF/ANSI content, copy/adopt/normal saves, dirty/name semantics,
borrowed-stream reference/sentinel changes, raw file bytes, rich archive payloads,
close/reopen and fresh rich-document creation. Generated files are deleted only
after their owners close. Prior live-caption traces remain unchanged.

All thirty-three original/converted document/live-text/stored-text/media/metadata/
session/admission/live/camera/picking/rendering trace pairs match across the three
hosts. Prior hashes remain unchanged. Background checks retain 301,996,032 bytes/
FNV-1a `E1A71005` across 12 backgrounds and 188 cameras per host. All 120 standalone
C checks including strict C89 syntax pass. Twenty-five selected native checks and
twenty-one checks in each SDL configuration pass, including existing core-link,
scene load/store/content, movie save/load and native playback/player regressions.
MSVC AddressSanitizer passes.

C-only fault checks cover every fallible effect with ordinary/allocation/foreign
failures, partially returned/published owners, translated owner replacement,
redirection retained after failed rename, factory error/release ordering, name
policies, both native text-unit sizes and byte orders, BOM/sentinel bytes and
virtual-load stream replacement. After the final hook review, the affected C tests,
movie save/load fixtures and both SDL Studios were rebuilt and rechecked, retaining
their original document/live-caption traces. The standalone C suite was rerun.

The regular asset-preview harness passes headlessly with fresh metadata documents
and input-hash checks. Test processes use private temporary directories. Retained
input identities are unchanged. Studio and Marcus were rebuilt; the workspace
Marcus was stopped before rebuilding and relaunched afterward. No Marcus UI tests
were added. Retained retail captures remain the playback reference; no redundant
capture campaign was launched.

This is Windows x64 evidence within these fixtures, not complete retail editing
or allocator equivalence. Native `_FReadChunk`/`_FOpenArg` materialization,
editing/undo, descriptor/output representations, physical providers,
document/view/parent lifecycle and final C-only host/facade removal remain work.
Other platforms remain unverified here.

Evidence: `tmp/c89-text-document-{baseline,final}-{native,sdl-release,sdl-debug}.bin`,
baseline `*.log`/`*-errors.log` and `tmp/c89-text-document-{native,sdl-release,sdl-debug}-tests.log`,
`tmp/c89-text-document-{live-text,store,media,metadata,session,admission,live,camera,pick,render}-final-{native,sdl-release,sdl-debug}.bin`,
`tmp/c89-text-document-{standalone,hook-native,hook-sdl-release,hook-sdl-debug}-tests.log`,
`tmp/c89-text-document-hook-{native,sdl-release,sdl-debug}{,-build,-errors}.log`,
`tmp/c89-text-document-asan.log`, `tmp/c89-text-document-trace-comparison.json`,
`tmp/c89-text-document-regular-tests.log` and `tmp/c89-text-document-identities.json`.

### Rich-text materialization and default font arguments (15 November 2026)

`text_materialize.c` now controls stored-to-live rich-text loading, property and
argument fixed-word byte normalization, and default font argument opening.
Native adapters retain typed groups/streams, font and serialized-string primitives,
and virtual argument hooks. The loader calls the shared C stored-reader and base
initialization/translation services. Help-document object/group argument extensions
still receive their original virtual dispatch and remain native work.

Defaults publish before text translation. The separate translated file reference
releases before properties and arguments transfer. Copying is requested only if
translation preserves the original file identity. The legacy `CpMac()-1` remains
the replacement byte count, including two-byte units. Arguments open in descending
order, skipping free slots and normalizing each visited fixed word first. Failures
retain published defaults, text, groups and already visited argument changes.
Temporary owners release before native exception rethrow.

Default argument opening queries size even for unknown kinds. Font entries use
their native short system without collection-order swapping. Serialized-name
failure diagnoses while locked and then unlocks; mapping and font-ID replacement
occur unlocked. Decode exceptions unlock without an ordinary diagnostic.

Before conversion, ten engine movie save/load cases recorded 1,551-byte traces
covering foreign property/fixed-word data, embedded-null/CR text, UTF-16 translation,
missing/empty collections, translation failure, unknown argument kinds, subclass
rejection, free slots and both copy flags. Native Debug records FNV-1a `2573FB83`,
SHA-256 `1FD0792B27044B3B4B07AC673C17C9B4A0B272528AE8FB9925895F1D61CD7BED`.
SDL Release and Debug record FNV-1a `CFB08F3A`, SHA-256
`FAB0FBE5BD3E3729A6A3BE9FB9AF5E04E02F2C55B341F1ECACB9134D962E88EC`.
Each host is compared with its own original font-table baseline. Logical argument
payloads exclude native alignment padding.

C-only checks inject ordinary/allocation/foreign failures at fallible effects,
including owned group outputs returned on failure, translated file replacement,
partial default/group publication and normalization, descending virtual hooks,
free slots, copy identity and nonempty base-stream replacement counts. Both native
text-unit sizes and byte orders are covered in C. Font tests cover all unknown
kinds, short entries and zero-byte decode inputs, negative native system values, lock/diagnostic/
map ordering and exception cleanup. MSVC AddressSanitizer passes.

All thirty-six original/converted materialization/document/live-text/stored-text/
media/metadata/session/admission/live/camera/picking/rendering trace pairs match
across the three hosts. Prior hashes remain unchanged. Background checks retain
301,996,032 bytes/FNV-1a `E1A71005` across 12 backgrounds and 188 cameras per host.
All 121 standalone C checks, including strict C89 syntax, pass. Twenty-six selected
native checks and twenty-two checks in each SDL configuration pass, including the
existing core-link, scene load/store/content, movie save/load and native playback/
player regressions. Retained input identities are unchanged; identities for 28
rebuilt binaries are recorded.

The regular asset-preview harness passes headlessly with fresh metadata inputs.
Test processes use private temporary directories. No Marcus UI tests were added;
the workspace Marcus was stopped before rebuilding and relaunched afterward.
Retained retail captures remain the playback reference. This is Windows x64
fixture evidence, not complete retail editing or allocator equivalence. Other
platforms remain unverified. Editing/undo, help-document argument extensions,
native descriptor/output representations, physical providers, document/view/parent
lifecycle and final C-only host/facade removal remain conversion work.

Evidence: `tmp/c89-text-materialize-{baseline,final}-{native,sdl-release,sdl-debug}.bin`,
baseline/final logs, `tmp/c89-text-materialize-{native,sdl-release,sdl-debug}-tests.log`,
`tmp/c89-text-materialize-standalone-tests.log`,
`tmp/c89-text-materialize-{document,live-text,store,media,metadata,session,admission,live,camera,pick,render}-final-{native,sdl-release,sdl-debug}.bin`,
`tmp/c89-text-materialize-asan.log`, `tmp/c89-text-materialize-regular-tests.log`,
`tmp/c89-text-materialize-trace-comparison.json` and
`tmp/c89-text-materialize-identities.json`.

### Rich-text property maps and argument references (15 November 2026)

`text_properties.c` now controls sorted property lookup/range fetching, masked
range application, adjustment after text edits, shared argument interning and
reference counts. Native adapters supply typed collection primitives, cache slots,
borrowed change records and paragraph-end queries. Changes are marshalled one at
a time, without adding an allocation to property application.

Lookup retains predecessor/insertion indices and next-boundary behavior. Failed
fetches preserve outputs, and the native wrapper preserves debug poisoning for a
missing map entry. Range application clears both caches, processes changes backward,
merges masks, removes redundant boundaries and restores the following value.
Reference ownership transfers through the saved revert value; each input change
reference is consumed once. Unused-batch release processes references forward.
The caller still reserves twice the change count. Ordinary insertion failure
retains the legacy diagnostic-and-continue behavior; exception results stop with
partial effects retained.

Adjustment runs after text replacement. Character properties in the deleted
interval collapse to the insertion end. Paragraph properties at its start stay
there; other paragraph changes move to the next paragraph unless a surviving
boundary already covers it. Deleted object entries release their arguments;
following entries shift. Paragraph-query failure retains earlier mutations.
Argument matching scans backward, skips holes, compares payload bytes and kind,
and returns one-based references. Built-in properties retain the font-only rule;
even client/object kinds use arguments. Newly created groups remain document-owned
after a later insertion failure. Invalid reference indices diagnose without changes.

Before conversion, the expanded headless engine movie save/load fixture recorded
59,429-byte property traces in Native Debug, SDL Release and SDL Debug. All three
record FNV-1a `30CB2692`, SHA-256
`94D00376A816F5B35D80474247FE098BB47175560AAE5621352236DE25DD4841`.
The trace freezes lookup success/miss outputs, optional outputs, logical property/
argument records, cache invalidation, masks, redundant boundaries, shared argument
matching/deletion/reuse and eight insertion/deletion cases spanning paragraph and
object boundaries. It exercises low-level property policies over directly edited
streams; it does not establish complete public text-edit/undo equivalence.

C-only tests compare 400 deterministic masked-edit batches against a per-character
value model and 180 insert/delete combinations against a separate per-kind map
reconstruction. They cover all 256 kind classifications, all-kind lookup boundaries,
backward duplicate matching, holes, embedded-null payloads, reference transfers,
invalid indices/count diagnostics, owned creation failures and insertion/paragraph
exception results. The latter retain partial property/reference effects and clear
caches before returning to native rethrow. MSVC AddressSanitizer passes.

All thirty-nine original/converted property/materialization/document/live-text/
stored-text/media/metadata/session/admission/live/camera/picking/rendering trace
pairs match across the three hosts. Prior hashes remain unchanged. Background
checks retain 301,996,032 bytes/FNV-1a `E1A71005` across 12 backgrounds and 188 cameras
per host. All 122 standalone C checks, including strict C89 syntax, pass. Twenty-seven
selected native checks and twenty-three checks in each SDL configuration pass,
including existing scene/core/movie and native playback/player regressions.
The regular asset-preview harness passes headlessly with fresh metadata inputs.
Retained input identities are unchanged; identities for 28 rebuilt binaries are
recorded. Test processes use private temporary directories. Marcus was stopped
before rebuilding and relaunched afterward.

This is Windows x64 evidence within these fixtures. Retained retail captures
remain the playback reference; no fresh capture campaign or Marcus UI tests were
added. Character/paragraph conversion, property copying, remaining text replacement/
edit/undo control, help-document argument extensions, descriptor/output storage,
physical providers, document/view/parent lifecycle and final C-only host/facade
removal remain conversion work. Other platforms remain unverified here.

Evidence: `tmp/c89-text-properties-{baseline,final}-{native,sdl-release,sdl-debug}.bin`,
baseline/final logs, `tmp/c89-text-properties-{native,sdl-release,sdl-debug}-tests.log`,
`tmp/c89-text-properties-standalone-tests.log`,
`tmp/c89-text-properties-{materialize,document,live-text,store,media,metadata,session,admission,live,camera,pick,render}-final-{native,sdl-release,sdl-debug}.bin`,
`tmp/c89-text-properties-asan.log`, `tmp/c89-text-properties-regular-tests.log`,
`tmp/c89-text-properties-trace-comparison.json` and
`tmp/c89-text-properties-identities.json`.

### Character and paragraph formatting (16 November 2026)

`text_format.c` now controls character/paragraph delta packing, batch generation,
font argument descriptors, cached formatting fetches, paragraph-bound selection
and formatting apply transactions. Native adapters retain typed value records,
font-table/paragraph-navigation primitives and virtual undo/invalidation effects.
Font names use the shared C string codec. A direct C change-array entry point in
`text_properties.c` avoids adding native staging allocations to formatting edits.

Packing retains short height differences, byte offset/style masks, per-field
paragraph masks, short wrapping and 32-bit color differences from black/clear.
Font descriptors retain the native runtime ID/system and counted string. Batch
ordinary failures release their emitted prefix; exceptions retain partial effects.
The single-delta native wrapper preserves no-change/failure poisoning and the
full mask visible when font services throw.

Fetches intersect built-in property ranges and skip map/font work on cache hits.
Offset bytes follow native achar signedness, including unsigned wide-character
behavior. Font records outside six through 522 bytes warn and keep the default
font; valid-size records read the runtime ID without parsing the stored name.
Partial cache values/ranges publish before native exception rethrow.

Paragraph bounds retain expand/contain query order across CR/LF text. Apply
publishes those bounds before virtual undo setup. Empty paragraph ranges have no
edit effects. Native inputs/defaults are marshalled after undo setup, preserving
changes made by that hook, and application reads current stream length after
reservation. Ordinary delta/reservation failures cancel undo; reservation failure
also releases generated references. Success applies, commits, bumps the combine
counter, validates and invalidates/marks dirty. Identical character formatting
still follows this transaction. Exceptions retain partial effects without new
rollback. Native undo-record construction/combination remains conversion work.

Before conversion, the expanded headless engine movie save/load fixture recorded
21,343-byte formatting traces. Native Debug records FNV-1a `0B74543D`, SHA-256
`0AA4758B0D923D826C3199F6F47120E786D4F65D9D619399F887999E1F191360`.
SDL Release and Debug record FNV-1a `78F581BE`, SHA-256
`F2C901FC9A9F81A4FE180DE3699CC0BCFEF56D529FDCAE0774F1E00F07E4B89F`.
Each host retains its own font-table baseline. The fixture freezes scalar deltas,
no-change masks, generated batches, logical font arguments, cache/range outputs,
all ranges over a CR/LF sample, defaults changed under a valid cache, malformed
font-record sizes and public formatting calls with virtual undo probes. The probes
establish formatting transaction sequencing, not native undo-allocation equivalence.

C-only checks cover both native unit sizes/orders and offset signedness, all byte
style masks, short wrapping, field masks, empty/maximal/embedded-null font names,
codec bytes, cache-hit isolation, malformed sizes and partial cache/range outputs.
Ordinary/allocation/foreign failure probes cover font/argument services, paragraph
queries, undo setup, reservation, insertion, cancellation, commit and invalidation.
They check reference release ordering, partial effects, paragraph no-ops, identical
character-format dirty behavior, and inputs/defaults changed during undo setup.
MSVC AddressSanitizer passes.

All forty-two original/converted formatting/property/materialization/document/
live-text/stored-text/media/metadata/session/admission/live/camera/picking/rendering
trace pairs match across the three hosts. Prior hashes remain unchanged. Background
checks retain 301,996,032 bytes/FNV-1a `E1A71005` across 12 backgrounds and 188 cameras
per host. All 123 standalone C checks, including strict C89 syntax, pass. Twenty-eight
selected native checks and twenty-four checks in each SDL configuration pass,
including existing core/scene/movie and native playback/player regressions.
The regular asset-preview harness passes headlessly with fresh metadata inputs.
Retained input identities are unchanged; identities for 28 rebuilt binaries are
recorded. Test processes use private temporary directories. Marcus was stopped
before rebuilding and relaunched afterward. Strict syntax caught a signedness
mismatch in a test counter; after correcting it, the affected C test was rebuilt
and rerun on all three hosts, and the standalone suite and AddressSanitizer passed.

This is Windows x64 fixture evidence. Retained retail captures remain the playback
reference; no new capture campaign or Marcus UI tests were added. Property copying,
remaining text replacement/navigation/undo, help-document argument extensions,
native output/descriptor storage, physical providers, document/view/parent lifecycle
and final C-only host/facade removal remain conversion work. Other platforms remain
unverified here.

Evidence: `tmp/c89-text-format-{baseline,final}-{native,sdl-release,sdl-debug}.bin`,
baseline/final logs, `tmp/c89-text-format-{native,sdl-release,sdl-debug}-tests.log`,
`tmp/c89-text-format-standalone-tests.log`,
`tmp/c89-text-format-{properties,materialize,document,live-text,store,media,metadata,session,admission,live,camera,pick,render}-final-{native,sdl-release,sdl-debug}.bin`,
`tmp/c89-text-format-asan.log`, `tmp/c89-text-format-regular-tests.log`,
`tmp/c89-text-format-trace-comparison.json`, `tmp/c89-text-format-identities.json`,
`tmp/c89-text-format-syntax-first-tests.log` and
`tmp/c89-text-format-syntax-{native,sdl-release,sdl-debug}-tests.log`.

### Text access, navigation and replacement (16 November 2026)

`text_access.c` now controls 512-character cache filling/invalidation, character/
block reads, paragraph/word navigation and counted literal search. Native adapters
supply borrowed storage, stream reads, classification/case mapping and virtual
cache/character hooks. Views are re-read after virtual cache dispatch. Cache
updates preserve overlap, front-on-tie invalidation and metadata/data publication
order, including partial read failures. Navigation retains CR/LF/word behavior and
diagnostics. Case-insensitive search mutates the caller's pattern even on miss,
and wide search retains the original low-byte-only membership bitmap.

`text_replace.c` now controls base/rich raw, file-location and stream replacement,
including formatting, reference lifetime, virtual dispatch, undo effects, map/cache
adjustment and notification ordering. Base replacement scales positions to bytes,
invalidates virtually and notifies/marks dirty before releasing its translated
file. Copying depends on original file identity. Rich file replacement translates
before size/undo checks; its inner base operation still translates independently,
and the rich view releases before property adjustment/commit.

Formatted replacement generates deltas before undo, computes contained paragraph
ranges, reserves before setup and calls virtual unformatted replacement. It applies
formatting afterward, retaining the original nested undo and dirty/notification
calls and expanded undo extent. Ordinary failures cancel only established undo,
release unconsumed batches and retain prior text/dirty effects. Exceptions retain
partial edits/references without added cancellation/rollback, while releasing owned
file views. Typed apply boundaries retain native range assertions.

The formatted-location wrapper intentionally forwards the original byte count as
the core insertion count after translation, with its existing copy-identity test.
C tests freeze this legacy unit/count behavior; it is not silently repaired or
used to claim complete wide/translated formatted-edit equivalence. Rich-document/
property/object copying and native undo-record policy/storage remain further work.

Before conversion, the expanded headless engine fixture recorded 102,964 bytes
in Native Debug, SDL Release and SDL Debug, all with FNV-1a `C2C720A8`, SHA-256
`6DD1E2FFBAAF16D4D21CABA840ED67ACA2651AB046075A4C71338DD760469E5F`.
It freezes cache bounds/content, virtual call counts, navigation/search outputs,
mutated patterns, base/public rich raw/file/stream replacement, formatted edits,
and real undo/redo. The generic rich-document undo/redo path completes here; a
previous suspected caption-edit stall was not reproduced by this case. This does
not establish complete caption authoring or all undo-allocation equivalence.

C-only access checks cover both unit sizes, 2,000 generated cache reads, 1,600 edit
invalidations and 800 counted-search comparisons against independent content/range/
search models. They check paragraph/word boundaries, low-byte wide-character
collisions, replaced virtual cache views, chunked partial output and ordinary/
allocation/foreign failures. Replacement checks cover source kinds/unit scaling,
translated owner replacement/release timing, copy flags, nested base/virtual calls,
size limits, formatting generation/reservation, undo failure/cancellation, partial
dirty/text effects and legacy formatted-file counts. Formatting/replacement tests
share their primitive host; the existing formatting checks are retained. Both new
C modules pass MSVC AddressSanitizer.

All forty-five original/converted access/formatting/property/materialization/
document/live-text/stored-text/media/metadata/session/admission/live/camera/picking/
rendering trace pairs match across the three hosts. Prior hashes remain unchanged.
Background checks retain 301,996,032 bytes/FNV-1a `E1A71005` across 12 backgrounds
and 188 cameras per host. All 125 standalone C checks, including strict C89 syntax,
pass. Thirty selected native checks and twenty-six checks in each SDL configuration
pass, including core/scene/movie and existing native playback/player regressions.
The explicit wide-collision read-order check was also rebuilt and rechecked on
all three hosts; AddressSanitizer and the final standalone suite include it.
The regular asset-preview harness passes headlessly with fresh metadata inputs.
Retained input identities are unchanged; identities for 28 rebuilt binaries are
recorded. Test processes use private temporary directories. Marcus was stopped
before rebuilding and relaunched afterward.

Retained retail captures remain the playback reference. No new capture campaign
or Marcus UI tests were added. This is Windows x64 fixture evidence; other platforms
and complete wide/translated formatted editing remain unverified here. Rich-document/
property/object copying and edits, undo-record policy/storage, remaining lifecycle,
physical/classification/case providers and final C-only host/facade removal remain
conversion work.

Evidence: `tmp/c89-text-access-{baseline,final}-{native,sdl-release,sdl-debug}.bin`,
baseline/final logs, `tmp/c89-text-access-{native,sdl-release,sdl-debug}-tests.log`,
`tmp/c89-text-access-standalone-tests.log`, `tmp/c89-text-access-syntax-tests.log`,
`tmp/c89-text-access-{format,properties,materialize,document,live-text,store,media,metadata,session,admission,live,camera,pick,render}-final-{native,sdl-release,sdl-debug}.bin`,
`tmp/c89-text-access-asan.log`, `tmp/c89-text-access-regular-tests.log`,
`tmp/c89-text-access-trace-comparison.json`, `tmp/c89-text-access-identities.json`
and `tmp/c89-text-access-collision-{native,sdl-release,sdl-debug}-tests.log`.

### Rich-document copying and embedded objects (16 November 2026)

`text_copy.c` now controls rich-document replacement, property-range copying,
embedded-object fetch/exact-position render dispatch, object insertion and object
property edits. Native adapters retain typed validation, storage/allocation,
source argument locks and virtual replacement/undo/render/notification hooks.
The existing C formatting/property modules supply batches, interning, references
and masked application. No rendering or playback policy was changed.

Property clearing advances through the live destination map while mutating it,
retaining its index-shift/skip behavior. Source ranges are clipped; a value reaching
the source selection's end extends to the destination end. Different default font
heights remain unrebased. Ordinary reservation/import failures warn and continue.
Property imports read size before locking, whereas object imports lock before
reading size. The object pass imports every kind as an argument, including odd
client kinds treated inline by other property operations. Failed map insertion
releases the imported argument directly, including those odd kinds.

Document copying sets undo before protecting the final paragraph marker, uses
virtual stream replacement and copies character, contained paragraph and object
properties before commit/notification. A marker-only replacement still commits
and notifies; a zero-range copy still establishes its transaction. Ordinary
replacement failure cancels undo. Other partial effects remain without rollback.

Built-in object queries publish position and optional size before allocation.
They return found even on ordinary allocation failure, leaving allocation output
publication to the allocator. Any output published before an exception remains
caller-owned. Dispatch requires an exact built-in object position. Object edits
retain the entry/index found before undo setup; identical payloads still edit and
notify, with balanced shared references.

Insertion retains legacy staged-reference behavior: reservation failure releases
the object reference but leaves a successfully generated character batch staged;
failed virtual replacement releases that batch before the object and cancellation.
Ordinary reserved-map insertion failure diagnoses and continues to commit/notify.
Exception results preserve other partial edits/references. Two explicit ownership
boundaries avoid carrying undefined/leaked native state forward: an unacquired
object reference is initialized to zero, and source locks release after interning
even on an exception result. Native allocator-failure equivalence is not claimed.

Before conversion, the expanded headless engine movie save/load fixture recorded
36,132-byte copy/object traces. Native Debug records FNV-1a `9100DDD3`, SHA-256
`B0336EDD4469AECA664169C52C0373CDB01800BEE9BBC8B828FB07EA62AF55C9`.
SDL Release and Debug record FNV-1a `83E2797B`, SHA-256
`96B34FFC1236FDFA8A2714E22BE3FB4D7C0A3944D4EC49C16945DB8F229DD775`.
Each host keeps its own font-table baseline. The fixture freezes logical maps,
argument payloads/counts, text, formatting and object queries across clipped/
extended property ranges, differing defaults, ordinary/custom/odd object kinds,
whole-document/marker-only/empty copies, insertion, property edits, identical
payload edits and virtual undo failure. These undo probes establish sequencing,
not native undo-allocation equivalence.

C-only checks cover both text-unit sizes, live-map clearing, clipping/extension,
source immutability, size/lock ordering, unlocked failure exits, import/reservation
warnings, final-marker protection, zero-range transactions, size admission,
odd-kind object imports, reference release ordering, allocation publication and
exact-position draw/rectangle dispatch. Ordinary/allocation/foreign failures cover
argument imports, undo setup, reservation, virtual replacement and map insertion,
including staged font references and retained partial text effects. MSVC
AddressSanitizer passes.

All forty-eight original/converted copy/access/formatting/property/materialization/
document/live-text/stored-text/media/metadata/session/admission/live/camera/picking/
rendering trace pairs match across the three hosts. Prior hashes remain unchanged.
Background checks retain 301,996,032 bytes/FNV-1a `E1A71005` across 12 backgrounds
and 188 cameras per host. All 126 standalone C checks, including strict C89 syntax,
pass. Thirty-one selected native checks and twenty-seven checks in each SDL
configuration pass, including core/scene/movie and existing native playback/player
regressions. The regular asset-preview harness passes headlessly with fresh
metadata inputs. Retained input identities are unchanged; identities for 28 rebuilt
binaries are recorded. Test processes use private temporary directories. Marcus
was stopped before rebuilding and relaunched afterward.

Retained retail captures remain the playback reference. No new capture campaign
or Marcus UI tests were added. This is Windows x64 fixture evidence; other platforms
remain unverified. Undo-record policy/storage, help-document argument extensions,
native output/descriptor storage, remaining lifecycle, physical/classification/
case providers and final C-only host/facade removal remain conversion work.

Evidence: `tmp/c89-text-copy-{baseline,final}-{native,sdl-release,sdl-debug}.bin`,
baseline/final logs, `tmp/c89-text-copy-{native,sdl-release,sdl-debug}-tests.log`,
`tmp/c89-text-copy-standalone-tests.log`,
`tmp/c89-text-copy-{access,format,properties,materialize,document,live-text,store,media,metadata,session,admission,live,camera,pick,render}-final-{native,sdl-release,sdl-debug}.bin`,
`tmp/c89-text-copy-asan.log`, `tmp/c89-text-copy-regular-tests.log`,
`tmp/c89-text-copy-trace-comparison.json`, `tmp/c89-text-copy-identities.json`
and `tmp/c89-text-copy-targeted-native.log`.

### Rich-text undo records and transactions (16 November 2026)

`text_undo.c` now controls text undo counters, pending-record setup/cancel/commit,
rich snapshot construction/disposal, combination and undo/redo replay. Native
documents/undo objects store plain C `MMTextUndoControl`/`MMTextUndoRecord` data;
their adapters supply allocation, typed validation, virtual document/selection
hooks and generic document undo-list services. Redo retains virtual undo dispatch.

Setup increments suspension before nested/disabled/empty checks. Ordinary record
creation failure resumes undo; exceptions retain suspension. Cancel releases a
pending record only when virtual resume reaches zero. Commit tries the last
completed rich record, clears redo after successful combination, otherwise calls
generic stack admission. The original ignored ordinary admission failure remains
observable; pending releases after normal return. Exceptions retain pending and
prior effects without new rollback.

Factories sort the replaced range, initialize record position/count and make
internal, undo-suspended snapshots before copying. Same-counter adjacent typing
extends inserted count; repeated Delete appends saved text and Backspace prepends
it, moving the record position only after success. Other combinations with inserted
text are rejected. Snapshot content excludes its terminal marker when restored or
combined. Counter arithmetic is explicitly 32-bit, including wrap.

Replay validates the document/range, snapshots replaced text, suspends/hides the
target, copies saved text or deletes inserted text, resumes, selects quietly,
shows selection and collapses it. Snapshot ownership/count swap only after those
hooks finish. Ordinary edit failure releases the private snapshot before resume.
The native early-resume behavior on ordinary snapshot-copy failure is preserved,
including its decrement before target suspension. Ordinary document-factory
failure does not resume. Exceptions retain prior text/counter/record effects
without added resume or rollback. Private construction/replay owners now release
before exception rethrow; native exception leaks are not reproduced or used to
claim allocator-failure equivalence.

Before conversion, the expanded headless engine movie save/load fixture recorded
3,754 bytes on Native Debug, SDL Release and SDL Debug, all with FNV-1a `092FF546`,
SHA-256 `981BEDBD41C051CF2C15268663CC3FAD13CCEE4BF0C9073CA7ED3D70C9287819`.
It freezes text/properties, undo/redo counts, suspension/combine/pending state and
virtual selection events across adjacent typing, repeated Delete/Backspace,
object insertion/replay, redo invalidation, empty/nested/disabled transactions,
cancellation, reversed factory ranges and direct record-combination cases.

C checks use content-bearing owned snapshots and verify construction publication,
counter wrapping, reversed/empty ranges, adjacent/delete/backspace combination,
undo/redo snapshot exchange, invalid documents/ranges, selection order, nested
transactions, disabled/empty actions, non-rich preceding records, ignored ordinary
stack-admission failure and retained pending records on exceptions. Ordinary/
allocation/foreign failures cover allocation, internal/suspension hooks, copying,
deletion, resume, selection, combination and stack/redo services. Partial-owner and
partial-text probes check release order and unchanged record publication; early
resume is explicit. MSVC AddressSanitizer passes. Record comparisons use logical
fields rather than native structure padding.

All fifty-one original/converted undo/copy/access/formatting/property/materialization/
document/live-text/stored-text/media/metadata/session/admission/live/camera/picking/
rendering trace pairs match across the three hosts. Prior hashes remain unchanged.
Background checks retain 301,996,032 bytes/FNV-1a `E1A71005` across 12 backgrounds
and 188 cameras per host. All 127 standalone C checks, including strict C89 syntax,
pass. Thirty-two selected native checks and twenty-eight checks in each SDL
configuration pass, including core/scene/movie and existing native playback/player
regressions. The regular asset-preview harness passes headlessly with fresh
metadata inputs. Retained input identities are unchanged after verification;
identities for 28 rebuilt binaries are recorded. Test processes use private
temporary directories. Marcus was stopped before rebuilding and relaunched
afterward. After the padding-independent assertion change, the affected C target
was rebuilt on all hosts; final suites and AddressSanitizer include it.

Retained retail captures remain the playback reference. No new capture campaign
or Marcus UI tests were added. This is Windows x64 fixture evidence; other platforms
remain unverified. Generic document undo-list storage/control, help-document
argument extensions, native output/descriptor storage, remaining lifecycle,
physical/classification/case providers and final C-only host/facade removal remain
conversion work.

Evidence: `tmp/c89-text-undo-{baseline,final}-{native,sdl-release,sdl-debug}.bin`,
baseline/final logs, `tmp/c89-text-undo-{native,sdl-release,sdl-debug}-tests.log`,
`tmp/c89-text-undo-standalone-tests.log`,
`tmp/c89-text-undo-{copy,access,format,properties,materialize,document,live-text,store,media,metadata,session,admission,live,camera,pick,render}-final-{native,sdl-release,sdl-debug}.bin`,
`tmp/c89-text-undo-asan.log`, `tmp/c89-text-undo-regular-tests.log`,
`tmp/c89-text-undo-trace-comparison.json`, `tmp/c89-text-undo-identities.json`,
`tmp/c89-text-undo-targeted-native-errors.log`
and `tmp/c89-text-undo-final-c-build-{native,sdl-release,sdl-debug}.log`.

### Generic document undo lists (16 November 2026)

`document_undo.c` now controls generic document undo admission/replay, redo/all
clearing, limit trimming and disposal. `MMDocumentUndo` owns C collection storage,
record count, done cursor, maximum and lazy-preparation state. The native undo
`DynamicArray` and its duplicated cursor/limit fields are removed from Movie and
text documents. Prior-record access now reads the same C stack. Initialization
retains the ten-record default, 128/16-byte growth hints, native pointer width and
debug tail poisoning. Prepared empty stacks remain allocated.

Disabled admission returns success without allocation or retention. Otherwise it
prepares, calls virtual redo clearing, evicts the oldest item at the current limit,
appends and only then retains the new record. The done cursor publishes last.
Ordinary append allocation failure drops oldest records and retries; an allocator
exception stops before this fallback. Replay dispatches the borrowed record and
advances the live cursor only on success. Reentrant callback changes remain
observable, including the original unclamped cursor after successful replay clears
its own stack. That path is a C policy probe, not a claim of valid subsequent
native UI undo operations.

Clear releases newest first and resets done after all releases. Redo clearing
leaves done unchanged. Limit reduction first trims redo above the larger of
done/new maximum, then removes oldest completed records, decrementing done before
release. Disposal releases records and C buffers before document child teardown.
Exceptions retain partial collection/reference/cursor effects without rollback.
Typed adapters retain virtual clear/record/refcount hooks and native debug memory
validation/marking. A temporary allocator binding catches exceptions inside
resize/free callbacks, reports them to C and restores the prior binding on exit,
including nesting. Removing the native list object changes allocation granularity;
exact native allocator-failure equivalence is not claimed.

Before conversion, the expanded headless engine movie save/load fixture recorded
2,160 bytes in Native Debug, SDL Release and SDL Debug, all with FNV-1a `4BD922B5`,
SHA-256 `3DBF9D1A2005784F22CDE2043C48DF643535C80F206B62A0A8E0C86BEFA2E0B8`.
The fixture freezes list order, lazy state, limits, undo/redo counts, references,
values and virtual/destructor events across disabled admission, limit eviction,
redo invalidation, failed replay, mixed completed/redo trimming and clearing.
Earlier real rich-text undo/redo traces remain in the integration fixture.

C checks compare 3,000 generated operations against an independent list/reference/
cursor model. Focused checks cover speculative/exact allocation fallback, ordinary
append failure and eviction/retry, empty failure, exception stops before eviction,
reference acquisition after publication, partial release/retain effects, reverse
clearing and limit-trim release order. Reentrant replay/clear callbacks verify live
cursor/limit reads. Failure probes cover allocation, virtual clearing, replay and
ownership effects. MSVC AddressSanitizer and strict C89 syntax pass.

All fifty-four original/converted generic-undo/text-undo/copy/access/formatting/
property/materialization/document/live-text/stored-text/media/metadata/session/
admission/live/camera/picking/rendering trace pairs match across the three hosts.
Prior hashes remain unchanged. Background checks retain 301,996,032 bytes/FNV-1a
`E1A71005` across 12 backgrounds and 188 cameras per host. All 128 standalone C
checks, including strict C89 syntax, pass. Thirty-three selected native checks and
twenty-nine checks in each SDL configuration pass, including core/scene/movie and
existing native playback/player regressions. The regular asset-preview harness
passes headlessly with fresh metadata inputs. Retained input identities are
unchanged after verification; identities for 28 rebuilt binaries are recorded.
Test processes use private temporary directories. Marcus was stopped before
rebuilding and relaunched afterward.

Retained retail captures remain the playback reference. No new capture campaign
or Marcus UI tests were added. This is Windows x64 fixture evidence; other platforms
remain unverified. Native undo objects and physical allocators/providers, help
extensions, output/descriptor storage, remaining document/view lifecycle and final
C-only host/facade removal remain conversion work.

Evidence: `tmp/c89-document-undo-{baseline,final}-{native,sdl-release,sdl-debug}.bin`,
baseline/final logs, `tmp/c89-document-undo-{native,sdl-release,sdl-debug}-tests.log`,
`tmp/c89-document-undo-standalone-tests.log`,
`tmp/c89-document-undo-{text-undo,copy,access,format,properties,materialize,document,live-text,store,media,metadata,session,admission,live,camera,pick,render}-final-{native,sdl-release,sdl-debug}.bin`,
`tmp/c89-document-undo-asan.log`, `tmp/c89-document-undo-regular-tests.log`,
`tmp/c89-document-undo-trace-comparison.json`,
`tmp/c89-document-undo-identities.json`
and `tmp/c89-document-undo-targeted-native-errors.log`.

### Document registry and lifecycle control (16 November 2026)

`document_lifecycle.c` now controls document registration/unlinking, live subtree
enumeration, internal/clipboard status, release/destruction order, view closure,
root/file lookup and close/save dispatch. Native documents/enumerators store plain
C registry/node/iterator data. Native parent/sibling/child/internal/freeing fields
are removed; legacy tool parent reads use the public accessor. The affected
native `chelp` and `ched` targets compile without launching their UI.

Registration retains root/child prepend and insertion after sibling anchors;
unlink leaves the removed node's links intact. Enumeration preserves combined
pre/post leaf visits, root entry/final-post bits, subtree skipping, zero flags and
unchanged node output on miss. Dirty close queries run in postorder. Force ignores
ordinary save failure but still honors explicit cancel; assume-yes bypasses the
query. Root traversal observes live sibling links. Save preserves name selection,
normal/save-as adoption, save-copy isolation, ordinary write-error publication and
title notification after non-copy success.

Release decrements references before marking freeing, disposes undo, closes
children and re-reads the live first child. The unchanged-child fallback retains
its diagnostic/release/retry behavior, including surviving references. Destruction
retains twice before clearing a clipboard document, then unlinks and releases view
storage. View closure retains the document while repeatedly choosing the current
first view's window or the view itself. A window may remove multiple views. The
final balancing release may destroy the document; C does not access it afterward.
Exception results retain partial counters/tree/save effects without new rollback
or compensating releases. Window, clipboard, filename/dialog and title operations
remain typed native services, with exceptions captured before returning to C.

Before conversion, the expanded headless engine fixture recorded 1,900 bytes on
Native Debug, SDL Release and SDL Debug, all with FNV-1a `FE5F544A`, SHA-256
`69FCD72C1D6A25FC18DD231D3169884ECCE195B53D814F1433D002AF59DCEAA9`.
It freezes parent/sibling/child links, root order, traversal/skip/miss outputs,
internal/dirty state, close-query responses, normal/save-as/copy dispatch, name
lookup and explicit destructor events. Native save/query providers are headless
probes; no actual clipboard mutation or dialog/window interaction is added.

C checks compare 80 generated 24-node trees, links and subtree/skip visits against
an independent model, then unlink every node and check the surviving graph. Focused
checks cover protected/batched view closure, children disappearing during closure,
fallback release/retry, clipboard double retains, internal short circuit, last-
window decisions, postorder dirty queries, explicit cancel under force, assume-yes,
root-order early stops and name lookup. Allocation/foreign exception probes check
undo/view/refcount/clipboard/query/save boundaries and retained partial effects.
MSVC AddressSanitizer and strict C89 syntax pass.

All fifty-seven original/converted lifecycle/generic-undo/text-undo/copy/access/
formatting/property/materialization/document/live-text/stored-text/media/metadata/
session/admission/live/camera/picking/rendering trace pairs match across the three
hosts. Prior hashes remain unchanged. Background checks retain 301,996,032 bytes/
FNV-1a `E1A71005` across 12 backgrounds and 188 cameras per host. All 129 standalone
C checks, including strict C89 syntax, pass. Thirty-four selected native checks
and thirty checks in each SDL configuration pass, including core/scene/movie and
existing native playback/player regressions. The regular asset-preview harness
passes headlessly with fresh metadata inputs. Retained input identities are
unchanged after verification; identities for 30 rebuilt binaries, including the
two legacy tools, are recorded. Test processes use private temporary directories.
Marcus was stopped before rebuilding and relaunched afterward.

Retained retail captures remain the playback reference. No new capture campaign
or Marcus UI tests were added. This is Windows x64 fixture evidence. Complete
interactive clipboard/dialog/window-close equivalence and other platforms remain
unverified by this slice. Native view-list storage, title/naming/display/UI
representations, physical/clipboard/window/dialog providers, help extensions and
final C-only host/facade removal remain conversion work.

Evidence: `tmp/c89-document-lifecycle-{baseline,final}-{native,sdl-release,sdl-debug}.bin`,
baseline/final logs,
`tmp/c89-document-lifecycle-{native,sdl-release,sdl-debug}-tests.log`,
`tmp/c89-document-lifecycle-standalone-tests.log`,
`tmp/c89-document-lifecycle-{undo-list,text-undo,copy,access,format,properties,materialize,document,live-text,store,media,metadata,session,admission,live,camera,pick,render}-final-{native,sdl-release,sdl-debug}.bin`,
`tmp/c89-document-lifecycle-asan.log`,
`tmp/c89-document-lifecycle-regular-tests.log`,
`tmp/c89-document-lifecycle-tools-build.log`,
`tmp/c89-document-lifecycle-trace-comparison.json`,
`tmp/c89-document-lifecycle-identities.json`
and `tmp/c89-document-lifecycle-targeted-native-errors.log`.

### Document view storage and title policy (16 November 2026)

`document_views.c` now owns borrowed view-list storage, lookup/removal/promotion,
first-view active selection, window activation, title traversal and name selection.
`MMDocumentViews` replaces the native view `DynamicArray`; untitled counters/cache
move into C registry/node data. The legacy topic-name suffix uses the same C
counter helper. Native view-array and old untitled-counter fields are removed.

Insertion retains duplicates/order without taking references. Lookup/removal/
promotion use the first match; native failed-find poisoning and missing-promotion
diagnostics remain. Prepared empty lists keep capacity. Ordinary append failure
keeps existing entries, without undo-style eviction. Pointer width, 128/16-byte
growth hints and debug tail poisoning remain unchanged. Removing the native list
object changes allocation granularity; exact native allocator-failure equivalence
is not claimed.

Active selection tests only the first view, even when another is active. Window
activation chooses the first containing window. Titles read names in document
preorder, including nodes without views, then re-fetch views from the live list.
Removal/append during title callbacks retains the original index-shift/skip
behavior. A shared window can receive repeated updates. Clipboard names override
file names; untitled numbers allocate lazily, remain cached across later file
names and wrap explicitly at 32 bits. Exceptions retain partial storage/output/
title/counter effects without rollback. Native formatting still ignores the
ordinary formatter result, as before.

View and undo collections share an allocator exception wrapper, restoring previous
bindings after normal, exceptional and nested operations. Read-only presentation
does not bind storage: an activation callback may destroy its document without the
wrapper later touching freed storage. Window activation/title writes, clipboard/
filename lookup and string emission remain typed native providers.

Before conversion, the expanded headless engine fixture recorded 808 bytes in
Native Debug, SDL Release and SDL Debug, all with FNV-1a `48EF6639`, SHA-256
`D077DDEA1E078FA58ED9B7B0D12FBE7BC8108340FCCA73F11800781F87DC0535`.
It freezes lazy state, list order, lookup/index outputs, first-view active results,
document references, duplicate/removal/reinsertion, cached/file names and title-
traversal events. Its GOBs exist only in memory with no HWND, activation or drawing;
it does not add window interaction or a Marcus UI test.

C checks compare 4,000 operations against an independent ordered-list model,
including duplicate and borrowed-reference behavior, missing outputs and disposal.
Focused probes check speculative/exact allocation fallback, append failure without
eviction, exception stops/validation order, first-view selection, first-window
activation, owner destruction during activation, preorder naming, live removal/
append during title updates, clipboard/file priority, cached/wrapped counters and
partial title/name output. MSVC AddressSanitizer and strict C89 syntax pass.

All sixty original/converted view/title/lifecycle/generic-undo/text-undo/copy/
access/formatting/property/materialization/document/live-text/stored-text/media/
metadata/session/admission/live/camera/picking/rendering trace pairs match across
the three hosts. Prior hashes remain unchanged. Background checks retain
301,996,032 bytes/FNV-1a `E1A71005` across 12 backgrounds and 188 cameras per host.
All 130 standalone C checks, including strict C89 syntax, pass. Thirty-five selected
native checks and thirty-one checks in each SDL configuration pass, including
core/scene/movie and existing native playback/player regressions. The regular
asset-preview harness passes headlessly with fresh metadata inputs. Native `chelp`
and `ched` compile; `chelp` was recompiled after making the topic formatter's
fixed-width argument cast explicit. Retained input identities are unchanged after
verification; identities for 30 rebuilt binaries are recorded. Test processes use
private temporary directories. Marcus was stopped before rebuilding and relaunched
afterward.

Retained retail captures remain the playback reference. No new capture campaign
or Marcus UI tests were added. This is Windows x64 fixture evidence. Complete
interactive title/activation/clipboard/window/dialog equivalence and other
platforms remain unverified by this slice. Native string/display/UI representations,
physical/clipboard/window/dialog providers, help extensions and final C-only
host/facade removal remain conversion work.

Evidence: `tmp/c89-document-views-{baseline,final}-{native,sdl-release,sdl-debug}.bin`,
baseline/final logs, `tmp/c89-document-views-{native,sdl-release,sdl-debug}-tests.log`,
`tmp/c89-document-views-standalone-tests.log`,
`tmp/c89-document-views-{lifecycle,undo-list,text-undo,copy,access,format,properties,materialize,document,live-text,store,media,metadata,session,admission,live,camera,pick,render}-final-{native,sdl-release,sdl-debug}.bin`,
`tmp/c89-document-views-asan.log`, `tmp/c89-document-views-regular-tests.log`,
`tmp/c89-document-views-tools-build.log`, `tmp/c89-document-views-topic-build.log`,
`tmp/c89-document-views-trace-comparison.json`,
`tmp/c89-document-views-identities.json`
and `tmp/c89-document-views-targeted-native-errors.log`.

### Help document data and editing policy (16 November 2026)

`help_text.c` now owns help topic initialization, 28/36-byte wire decoding and
encoding, plain/forest chunk read sequencing, string expansion, metadata
publication, save/adoption rollback, object/group argument normalization,
picture/button payload construction, grouping/query and dirty publication.
`MMHelpTopic` replaces the stored native `Topic`; native get/set methods marshal
the public type. Existing C property/argument and document factory policies are
reused. Native callbacks retain physical I/O, string emission/lookup and virtual
rich-text/undo/effect hooks. The Help editor marshals numeric dialog values into
the C fields without aliasing native `long` pointers onto C `int` storage.

The codec accepts old headers with default sound zero/nil and ignores the system
short, as before. Foreign headers swap the first six topic words only; the sound
tail deliberately remains unswapped. Object argument normalization publishes a
swapped type before rejecting an unknown/truncated subtype. Extra payload bytes
remain untouched. Grouping sorts ranges before undo and reads the name afterward;
ordinary reserve failure releases the acquired argument and cancels undo. Query
publication remains end, target, name, group, start; ordinary name decoding failure
is ignored. Optional and aliased native outputs retain that ordering.

Expansion marks the document internal, leaves undo suspended, wraps decimal IDs
at 32 bits, skips inserted placeholders and warns on missing strings. Ordinary
replacement failure continues with the cached length. Metadata publishes after
successful reading/expansion. Save publishes the root type before allocation,
retains partial root-number publication and deletes child before parent on failed
adoption. Exceptions stop without added transaction rollback/error publication;
private file/payload owners and group locks release before native rethrow. This
cleanup intentionally fixes exceptional ownership leaks rather than claiming exact
legacy allocator/exception equivalence.

Before conversion, the headless engine fixture recorded 5,324 bytes on Native
Debug, SDL Release and SDL Debug: FNV-1a `AD5FE208`, SHA-256
`1864D61644B00F0F6AEA1201743F27EDC78A8E7ADBB2F6449CEDAB4FCEF72ADB`.
It freezes text, property maps, group ranges/names/targets, picture/button payloads,
save trees, old/full/native/foreign headers, expansion and partially normalized
argument bytes. No window interaction or rendering is needed by this fixture.

C tests exercise both byte orders and one/two-byte text, malformed headers,
inherited argument handling, partial tag swapping, sorted/empty/ungrouped ranges,
reference release/cancel/commit/notification failures, lock cleanup and name decode
failure, allocated payload ownership, borrowed/private forest references, failed
read stages, nonrecursive expansion, missing strings, ID overflow, metadata
publication and save/delete failure ordering. Strict C89 syntax and MSVC
AddressSanitizer checks pass.

All 63 original/converted Help/view/lifecycle/undo/text/media/metadata/session/
admission/live/camera/picking/rendering trace pairs match across the three hosts.
Prior hashes remain unchanged. Background checks retain 301,996,032 bytes/FNV-1a
`E1A71005` across 12 backgrounds and 188 cameras per host. All 131 standalone C
checks, including strict C89 syntax, pass. Thirty-six selected Native checks and
32 checks in each SDL configuration pass, including the existing Native
playback/player regressions. The regular headless asset-preview harness passes;
Native `chelp` and `ched` compile. Retained input identities remain unchanged after
verification; identities for 30 binaries are recorded. Test processes use private
temporary directories. Marcus was stopped before rebuilding and relaunched
afterward.

Retained retail captures remain the playback reference. No new capture campaign
or Marcus UI tests were added. This is Windows x64 data/fixture evidence; Help
bitmap/button bounds and drawing, `TopicGraphicsObject`, balloons/buttons,
interactive Help editor behavior and other platforms remain unverified by this
slice. Native string/display/UI representations and final C-only host/facade
removal remain conversion work.

Evidence: `tmp/c89-help-text-{baseline,final}-{native,sdl-release,sdl-debug}.bin`,
baseline/final logs, `tmp/c89-help-text-{native,sdl-release,sdl-debug}-tests.log`,
`tmp/c89-help-text-standalone-tests.log`,
`tmp/c89-help-text-{views,lifecycle,undo-list,text-undo,copy,access,format,properties,materialize,document,live-text,store,media,metadata,session,admission,live,camera,pick,render}-final-{native,sdl-release,sdl-debug}.bin`,
`tmp/c89-help-text-asan.log`, `tmp/c89-help-text-regular-tests.log`,
`tmp/c89-help-text-tools-build.log`, `tmp/c89-help-text-trace-comparison.json`,
`tmp/c89-help-text-identities.json` and `tmp/c89-help-text-targeted-native-errors.log`.

### Help embedded-object layout and drawing policy (16 November 2026)

`help_display.c` now owns native-order object-header dispatch, picture/button
resource selection, bitmap-child lookup, baseline geometry, edit-control font and
measurement sequencing, fill/draw/boxing order and cursor publication.
`MMHelpDisplay` replaces the native hidden-button field with plain C state.
Typed cache/file/bitmap/font/graphics primitives remain in `help_display_host.h`.
The physical renderers and font metrics are unchanged providers.

Only object kind 192 is handled; short arguments and unknown types retain their
failure behavior. Buttons use bitmap child `0x10000`. Bitmap bounds normalize left/
bottom to zero. Drawing aligns to the live cursor/baseline, fills nonclear
backgrounds, optionally draws, releases the bitmap, then boxes and advances.
Hidden buttons still resolve, measure, fill, frame and advance. Style is read
after bitmap release; foreground is read after pen setup and the live cursor
after framing. Edit controls always frame/fill, including zero/negative widths,
and publish their cursor before pen/frame/fill. Graphics-environment clipping
remains authoritative; the separate caller clip is unused. Coordinate arithmetic
wraps explicitly at 32 bits.

Exceptions retain partial graphics/cursor effects without added transaction
rollback. Private bitmap references release before native rethrow on every exit,
fixing exceptional ownership leaks. Native bounds callbacks publish raw outputs
before bitmap release, then receive the final normalized bounds after C returns.
Exact native allocator/exception equivalence is not claimed.

Before conversion, an offscreen engine fixture recorded 496,000 bytes per host:

| Host | FNV-1a | SHA-256 |
| --- | --- | --- |
| Native Debug | `79151715` | `28602BFE95975A7379548EF4FFE3676DFEBCC3E0C4EB7C72D1B38AD7FA6C9737` |
| SDL Release/Debug | `B1CE247D` | `7873DFF1288BA584965D9B4E33A32F85231ACFDECCAC0153EA6ABBC5D4421882` |

The fixture freezes return values, bounds, cursor outputs and full offscreen
pixels for pictures, buttons, edit controls, unknown types, missing resources,
hidden/boxed styles, clear/nonclear backgrounds, nonzero bitmap origins, negative/
zero edit widths and clipping. Each converted host is compared with its own
baseline; this does not assert Native/SDL raster or font-metric equality. No window
or Marcus UI test is involved.

C checks independently verify geometry and event order in both byte orders,
header rejection, button-child selection, hidden-button behavior, failure and
exception stops, partial owner release, bounds partial output, cursor-publication
timing, late style/color/cursor reads and 32-bit wrap. MSVC AddressSanitizer passes.

All 66 original/converted display/Help/text/preview trace pairs match across the
three hosts. Prior hashes remain unchanged. Background checks retain 301,996,032
bytes/FNV-1a `E1A71005` across 12 backgrounds and 188 cameras per host. All 132
standalone C checks, including strict C89 syntax, pass. Thirty-seven selected
Native checks and 33 checks in each SDL configuration pass, including the existing
Native playback/player regressions. The regular headless asset-preview harness
passes; Native `chelp` and `ched` compile. Retained input identities remain
unchanged after verification; identities for 30 binaries are recorded. Test
processes use private temporary directories. Marcus was stopped before rebuilding
and relaunched afterward.

Retained retail captures remain the playback reference; no new capture campaign
or Marcus UI tests were added. This is Windows x64 fixture evidence. Interactive
Help/topic/balloon/button behavior and other platforms remain unverified by this
slice. `TopicGraphicsObject`, balloons/buttons, native string/display/UI
representations, physical graphics/font/cache providers and final C-only host/
facade removal remain conversion work.

Evidence: `tmp/c89-help-display-{baseline,final}-{native,sdl-release,sdl-debug}.bin`,
baseline/final logs, `tmp/c89-help-display-{native,sdl-release,sdl-debug}-tests.log`,
`tmp/c89-help-display-standalone-tests.log`,
`tmp/c89-help-display-{text,views,lifecycle,undo-list,text-undo,copy,access,format,properties,materialize,document,live-text,store,media,metadata,session,admission,live,camera,pick,render}-final-{native,sdl-release,sdl-debug}.bin`,
`tmp/c89-help-display-asan.log`, `tmp/c89-help-display-regular-tests.log`,
`tmp/c89-help-display-tools-build.log`, `tmp/c89-help-display-trace-comparison.json`,
`tmp/c89-help-display-identities.json` and `tmp/c89-help-display-targeted-native-errors.log`.

### Help topic interaction and script lifetime policy (16 November 2026)

`help_interaction.c` now owns mouse tracking/release matching, bad-key admission,
mouse-move hot-text flags, hit/script/balloon dispatch, point-query sequencing,
cursor ancestor selection and script setup/run/release/identity checks.
`MMHelpInteraction` replaces native tracked-group/topic/flags/base-handler fields.
The base ID is still populated by native topic control construction, which remains
conversion work. Typed virtual command, point/text, resource/interpreter, world
identity and physical cursor services remain in `help_interaction_host.h`.

Tracking publishes adjusted command flags before group lookup, retains partial
group/topic outputs on misses, captures flags after the virtual cursor hook and
ends tracking before release hit testing. Both group and topic must match.
Mouse moves adjust a local flag value without rewriting the command. Bad keys
admit IDs from base+1 through base+256, wrap their group byte, and ignore the
virtual script's ordinary false return. Point queries preserve inherited bounds/
point/group order. Cursor traversal skips the topic itself, chooses the first
Kidspace ancestor and adds the child flag, or selects the application's default
cursor when no such ancestor exists.

Scripts clear optional return output before lookup and pass five signed 32-bit
parameters, narrowing character to its low byte. Missing scripts/interpreters mean
alive. Ordinary run failure clears return output again. World/grid/identity are
captured after interpreter creation; interpreter release precedes the identity
lookup and script release follows it. A run may destroy the topic: subsequent
services use captured data, and a false virtual run prevents hit dispatch from
accessing the owner again. Private script/interpreter references release before
native exception rethrow, fixing exceptional ownership leaks. Other partial
outputs/effects remain; exact native allocator/exception equivalence is not claimed.

Before conversion, the headless engine fixture recorded 1,148 bytes on Native
Debug, SDL Release and SDL Debug: FNV-1a `DDC10C3C`, SHA-256
`1D5E3BE37957A26D562297D6ADF91C91A0FBA6F4313ED0B114FFEDE5EA8CB505`.
It freezes adjusted commands, partial/mismatched group state, mouse/key/move/hit
dispatch events, cursor-hook command mutation, script parameters/setup failures,
return reset and actual topic destruction during a script call. It uses in-memory
GOBs, an offscreen text-formatting port, a command-manager tracking probe and a
virtual interpreter probe. No OS mouse capture, physical cursor/window interaction
or Marcus UI test is added. The interpreter probe verifies dispatch/lifetime, not
bytecode interpreter semantics; the interpreter itself is unchanged.

C checks independently cover key boundaries, partial tracking output, adjusted/
live command mutation, release matching, ignored virtual failure, owner destruction,
point/cursor ancestry order, script setup/run/identity ordering, optional returns,
partial owners and ordinary/exception failure prefixes. A heap-owned tracking
state is destroyed by a virtual script callback to check post-run access under
MSVC AddressSanitizer, which passes.

All 69 original/converted interaction/display/Help/text/preview trace pairs match
across the three hosts. Prior hashes remain unchanged. Background checks retain
301,996,032 bytes/FNV-1a `E1A71005` across 12 backgrounds and 188 cameras per host.
All 133 standalone C checks, including strict C89 syntax, pass. Thirty-eight
selected Native checks and 34 checks in each SDL configuration pass, including
the existing Native playback/player regressions. The regular headless asset-preview
harness passes; Native `chelp` and `ched` compile. Retained input identities remain
unchanged after verification; identities for 30 binaries are recorded. Test
processes use private temporary directories. Marcus was stopped before rebuilding
and relaunched afterward.

Retained retail captures remain the playback reference; no new capture campaign
or Marcus UI tests were added. This is Windows x64 fixture evidence. Complete
interactive cursor/tracking/Help behavior, arbitrary native virtual-override
observations and other platforms remain unverified by this slice. Topic control
construction, balloon/button lifecycle, native string/display/UI representations,
physical cursor/graphics/font/cache providers and final C-only host/facade removal
remain conversion work.

Evidence: `tmp/c89-help-interaction-{baseline,final}-{native,sdl-release,sdl-debug}.bin`,
baseline/final logs, `tmp/c89-help-interaction-{native,sdl-release,sdl-debug}-tests.log`,
`tmp/c89-help-interaction-standalone-tests.log`,
`tmp/c89-help-interaction-{display,text,views,lifecycle,undo-list,text-undo,copy,access,format,properties,materialize,document,live-text,store,media,metadata,session,admission,live,camera,pick,render}-final-{native,sdl-release,sdl-debug}.bin`,
`tmp/c89-help-interaction-asan.log`, `tmp/c89-help-interaction-regular-tests.log`,
`tmp/c89-help-interaction-tools-build.log`, `tmp/c89-help-interaction-trace-comparison.json`,
`tmp/c89-help-interaction-identities.json` and `tmp/c89-help-interaction-targeted-native-errors.log`.

### Help balloon and button lifecycle policy (16 November 2026)

`help_balloon.c` now owns topic override merging, balloon document loading/hiding,
duplicate-ID replacement, balloon/button factory sequencing, topic replacement/
rollback, sound-variable assignment order, target anchoring, parent pinning, topic
centering and button point/click dispatch. `MMHelpBalloon` replaces the native
topic-view pointer; `MMHelpButton` replaces native group/topic fields. Typed GOB/
control creation, document/resource, runtime-variable, geometry and representation
providers remain in `help_balloon_host.h`.

Overrides skip nil balloon/script IDs, zero handler/target IDs and incomplete sound
pairs, while adding offsets with explicit 32-bit wrap. Existing matching balloon
IDs release before replacement; another handler type diagnoses/fails. Optional
REVIEW same-source reuse remains supported. New private nodes release on failed
initialization, including exceptions. After initialization, enter/hit callbacks may
destroy committed nodes, so failure never releases them again. Initial hit uses a
previously captured grid for the later identity check. Private read documents
release on every exit. This deliberately fixes exceptional ownership leaks rather
than claiming exact native allocator/exception equivalence.

Topic creation publishes the new view before sound assignments. Ordinary failure
releases the currently published new view and restores the old one; exceptions
retain partial state. Success releases the old view before natural-size/anchor
work. Metadata is re-read at its native stages, including sound number, target and
final offsets. Anchoring uses target registration or rectangle center and maps
through global/parent coordinates. Layout retains native pinning without resizing
oversized rectangles, then centers the topic using late preferred dimensions.
Empty intersections normalize to zero before the reposition decision; signed
halves truncate toward zero. Coordinate arithmetic wraps at 32 bits.

Buttons publish group/topic before initialization and align left/bottom after
entering the initial state. Point testing first uses inherited hit testing, then
maps into the parent topic and requires group/topic equality. Clicks snapshot ID
before parent checking, read flags late and avoid owner access after virtual hit.

Before conversion, the headless engine fixture recorded 540 bytes on Native Debug,
SDL Release and SDL Debug: FNV-1a `EB200D33`, SHA-256
`7A0CB37C019890B3CE79ED166F34E9F92060D611ACCF46A78CE0867B1D96951B`.
It freezes GOB tree types/geometry, document references, topic replacement,
embedded button/edit construction, saved-topic loading and button queries/clicks
using generated descriptors and representation resources. It creates no HWNDs,
performs no input capture and adds no Marcus UI test. Actual enter/hit destruction
and optional REVIEW reuse branches are covered by C provider probes rather than
this native fixture.

C checks cover override combinations, duplicate IDs, partial owners, ordinary/
exception factory prefixes, committed-node destruction, optional reuse, read/hide/
build cleanup, replacement rollback/publication, live metadata, target fallback,
point/coordinate mapping and initialization/enter order. An independent geometry
model checks 1,232 pinning/centering cases plus empty rectangles and late preferred
dimensions. Callback failures check stopping before subsequent layout/hit effects.
A click callback frees its button state under MSVC AddressSanitizer, which passes.

All 72 original/converted balloon/interaction/display/Help/text/preview trace pairs
match across the three hosts. Prior hashes remain unchanged. Background checks
retain 301,996,032 bytes/FNV-1a `E1A71005` across 12 backgrounds and 188 cameras per
host. All 134 standalone C checks, including strict C89 syntax, pass. Thirty-nine
selected Native checks and 35 checks in each SDL configuration pass, including the
existing Native playback/player regressions. The regular headless asset-preview
harness passes; Native `chelp` and `ched` compile. Retained input identities remain
unchanged after verification; identities for 30 binaries are recorded. Test
processes use private temporary directories. Marcus was stopped before rebuilding
and relaunched afterward.

Retained retail captures remain the playback reference; no new capture campaign
or Marcus UI tests were added. This is Windows x64 fixture evidence. Complete
interactive balloon/button behavior, Native REVIEW configuration, arbitrary native
virtual-override observations and other platforms remain unverified by this slice.
Topic control construction, physical/native string/display/UI/resource
representations and final C-only host/facade removal remain conversion work.

Evidence: `tmp/c89-help-balloon-{baseline,final}-{native,sdl-release,sdl-debug}.bin`,
baseline/final logs, `tmp/c89-help-balloon-{native,sdl-release,sdl-debug}-tests.log`,
`tmp/c89-help-balloon-standalone-tests.log`,
`tmp/c89-help-balloon-{interaction,display,text,views,lifecycle,undo-list,text-undo,copy,access,format,properties,materialize,document,live-text,store,media,metadata,session,admission,live,camera,pick,render}-final-{native,sdl-release,sdl-debug}.bin`,
`tmp/c89-help-balloon-asan.log`, `tmp/c89-help-balloon-regular-tests.log`,
`tmp/c89-help-balloon-tools-build.log`, `tmp/c89-help-balloon-trace-comparison.json`,
`tmp/c89-help-balloon-identities.json` and `tmp/c89-help-balloon-targeted-native-errors.log`.

### Help topic control construction (16 November 2026)

`help_topic.c` now owns Help topic base initialization, group scanning, maximum-
group handler reservation/publication, embedded-control header decoding, geometry,
group/target fallback, handler collision choice and edit/button creation order.
The topic factory reuses `mm_text_document_create` with read-error publication
disabled. Typed document, font, point, runtime-variable and control constructors
remain in `help_topic_host.h`.

The scan uses the document captured before base initialization and re-reads its
length each iteration. Base ID resets after scanning, publishes before `_gidBase`
assignment and remains published on failure. The button resource cache is captured
once; object/group lookups use the current document. Ordinary scan/group false
results are ignored, while object-fetch false ends initialization successfully.
Missing payload allocations and unsupported/truncated records skip. Private object
payloads release before geometry/control callbacks on all exits, including
exceptions. Other partial controls/state remain without rollback. Shared factory
cleanup now releases a private topic object on initialization exceptions before
native rethrow. Exact native allocator/exception equivalence is not claimed.

Native-order control words ignore extra bytes. Edit geometry uses baseline plus
character offset, one-pixel horizontal insets and empty-font vertical metrics;
clear edit backgrounds become white. Explicit button targets override group
fallback; nil targets are supplied by the group. Group handler IDs use base+group
unless occupied; ungrouped/colliding controls allocate a fresh ID. Arithmetic wraps
at 32 bits. The obsolete native help-string marker constant is removed.

Before conversion, the headless fixture recorded 552 bytes on Native Debug, SDL
Release and SDL Debug: FNV-1a `8AB0C61F`, SHA-256
`EF25D07EAB1FB80FFF4D66661D58C5A1A5DEDEA882A21E2BAB766E59888BAF17`.
It freezes control-tree types/geometry, relative handler IDs, explicit/inherited
button target dispatch and document references. Cases include no groups/objects,
two controls sharing a group, an unused higher group affecting ID reservation,
forced ID collision, vertical offsets and clear/colored backgrounds. GOBs and
formatting ports exist in memory without windows, input capture or Marcus UI tests.

C checks independently verify both byte orders, edit geometry/style, header extras,
short/unknown records, missing and partially returned payload owners, ignored false
scan/group results, successful end-of-object scanning, handler collision/failure,
base publication, initial/live document access, captured button cache and partial
control-prefix effects. Payloads use actual heap allocations and are freed before
later callbacks under MSVC AddressSanitizer, which passes.

All 75 original/converted topic/balloon/interaction/display/Help/text/preview trace
pairs match across the three hosts. Prior hashes remain unchanged. Background
checks retain 301,996,032 bytes/FNV-1a `E1A71005` across 12 backgrounds and 188
cameras per host. All 135 standalone C checks, including strict C89 syntax, pass.
Forty selected Native checks and 36 checks in each SDL configuration pass,
including the existing Native playback/player regressions. The regular headless
asset-preview harness passes; Native `chelp` and `ched` compile. Retained input
identities remain unchanged after verification; identities for 30 binaries are
recorded. Test processes use private temporary directories. Marcus was stopped
before rebuilding and relaunched afterward.

The remaining `kidhelp.cpp` methods were audited as typed provider binding,
marshalling, physical reference operations and validation. Together with the
preceding Help slices, this moves Help-specific policy through C89; it does not
complete C-only backend integration. Needed native string/display/resource
representations, physical providers and final host/facade removal remain work.

Retained retail captures remain the playback reference; no new capture campaign
or Marcus UI tests were added. This is Windows x64 fixture evidence. Full
interactive control behavior, arbitrary native virtual-override observations and
other platforms remain unverified by this slice.

Evidence: `tmp/c89-help-topic-{baseline,final}-{native,sdl-release,sdl-debug}.bin`,
baseline/final logs, `tmp/c89-help-topic-{native,sdl-release,sdl-debug}-tests.log`,
`tmp/c89-help-topic-standalone-tests.log`,
`tmp/c89-help-topic-{balloon,interaction,display,text,views,lifecycle,undo-list,text-undo,copy,access,format,properties,materialize,document,live-text,store,media,metadata,session,admission,live,camera,pick,render}-final-{native,sdl-release,sdl-debug}.bin`,
`tmp/c89-help-topic-asan.log`, `tmp/c89-help-topic-regular-tests.log`,
`tmp/c89-help-topic-tools-build.log`, `tmp/c89-help-topic-trace-comparison.json`,
`tmp/c89-help-topic-identities.json` and `tmp/c89-help-topic-targeted-native-errors.log`.

### Shared rich-text formatter (16 November 2026)

`rich_text_layout.c` now owns character-run and line layout for both Studio text
views and Marcus captions. The former C++ template is a typed provider binding.
C state owns break opportunities, white-space overhang, long-word binary search,
128-unit fetch/retention, ignored-character lookahead, tabs, objects, indents,
justification, ascent/descent and line/paragraph spacing. Hosts retain native
character/property storage, document access, font measurement and drawing.

Object lookup mutates the private run style even on a miss. Ignore lookahead can
pass the current style/fetch limit. Reinitialization and EOF retain the original
flag behavior; freshly constructed C runs define previously uninitialized flags
as zero. C++ exceptions are caught at provider boundaries and rethrown after C
returns. Line horizontal offset/count/height publish before the final paragraph query, retaining
the original partial output on failure. Coordinate arithmetic wraps at 32 bits;
line fields wrap at signed 16 bits at the original stages. Negative division
truncates toward zero, with spacing products computed without C99 integer types.

Before conversion, the expanded headless formatter fixture captured 112,894,528
bytes on Native Debug, SDL Release and SDL Debug: FNV-1a `D3C462F6`, SHA-256
`49610DBAD92037CA0A1F0775EC269CEFB7EDBF8FE2C30691FE9100D29FE6040F`.
The 1,080 byte/UTF-16 configurations combine mixed styles, variable/kerning widths,
vertical offsets, object hits/misses/style mutation, all indent/justification modes,
five widths, wrapping, CR/LF, tabs and ignored characters across fetch/style limits.
Additional probes throw at every callback of a representative line. Captures
include callback arguments/order, measured prefixes, retained text, complete runs,
lines and partial line publication. The fixture checks its frozen fingerprint even
when no capture file is requested; ordinary runs retain only the rolling hash.

C-only checks independently verify all 65,536 control classifications, explicit
line/run examples, signed spacing/overflow, oversized and rejected objects,
reinitialization/EOF flags, forced tabs, UTF-16 fetch boundaries, lookahead beyond
style limits, invalid-style diagnostics and callback-error prefixes. The focused
suite also passes MSVC AddressSanitizer.

All 78 original/converted layout/Help/text/preview trace pairs match across the
three hosts. Prior hashes remain unchanged. Background checks retain 301,996,032
bytes/FNV-1a `E1A71005` across 12 backgrounds and 188 cameras per host. All 136
standalone C checks, including strict C89 syntax, pass. Forty-two selected Native
checks and 38 checks in each SDL configuration pass, including the existing Native
playback/player regressions. The regular headless asset-preview harness passes;
Native `chelp` and `ched` compile. Retained input identities remain unchanged;
identities for 33 binaries are recorded. Test processes use private temporary
directories. Marcus was stopped before rebuilding and relaunched afterward.

Retained retail captures remain the playback reference; no new capture campaign
or Marcus UI tests were added. This is Windows x64 fixture evidence. Arbitrary
font/graphics providers, interactive text drawing/hit mapping and other platforms
remain unverified by this slice. Typed native property/font/graphics providers,
drawing/output representations and final C-only host integration remain work.

Evidence: `tmp/c89-rich-text-layout-{native,sdl-release,sdl-debug}-{baseline,final}.bin`,
`tmp/c89-rich-text-layout-{native,sdl-release,sdl-debug}-tests.log`,
`tmp/c89-rich-text-layout-standalone-tests.log`,
`tmp/c89-rich-text-layout-{topic,balloon,interaction,display,text,views,lifecycle,undo-list,text-undo,copy,access,format,properties,materialize,document,live-text,store,media,metadata,session,admission,live,camera,pick,render}-final-{native,sdl-release,sdl-debug}.bin`,
`tmp/c89-rich-text-layout-asan.log`, `tmp/c89-rich-text-layout-regular-tests.log`,
`tmp/c89-rich-text-layout-tools-build.log`, `tmp/c89-rich-text-layout-trace-comparison.json`
and `tmp/c89-rich-text-layout-identities.json`.

### Text-box state and frame/view policy (16 November 2026)

`text_box_runtime.c` now owns text-box attachment, lifetime queries, visibility,
frame/view sequencing, core show/hide, selection, bounds/type changes, dirty
routing and scroll/text-selection dispatch. `TextBoxData` stores `MMTextBoxState`
directly, removing duplicate native fields and temporary metadata marshalling in
read/write. Scene and selection fields now live in `MMTextBoxRuntime`. Archive,
preview and fixture callers use that C metadata; Marcus uses the same visibility
and first-frame scrolling predicates as Studio.

Frame changes snapshot the old view before publishing current frame. New borders
deactivate during playback; surviving views retain their order. Visibility is
re-read after creation, with the old view still controlling release. Hide sets
the exclusive end before dirty notification and checks scene selection after
frame/view effects. Show preserves the strict `end < current` extension test,
including no extension at equality. Scene attachment reads the new scene's frame
twice, publishes current then first, and attaches the scene last. Bounds publish
after dirty notification; native input rectangles are re-read at that stage.
Lifetime rejects only `end < first`; equal bounds are a valid empty lifetime.
The infinite-end sentinel resolves through the live scene last frame. Arithmetic
wraps at 32 bits; provider errors stop without rollback, with partial lifetime
output preserved. C++ exceptions are caught at native boundaries and rethrown
after C returns.

Before conversion, a native engine fixture captured 4,480 bytes on Native Debug,
SDL Release and SDL Debug: FNV-1a `FB2BB26D`, SHA-256
`6FBFBF0A1430A7A218EC9EE23FA9C722251E20BD7BDB02FF3266CC3C04460CB0`.
It freezes viewless loaded metadata, invalid/empty/finite/infinite lifetime,
selection, type, bounds, invisible frame changes, strict show behavior, hide and
scene attachment. It creates no windows and adds no Marcus UI tests. Actual view
creation/deactivation/release and scroll/text-selection effects are covered by C
providers rather than this native fixture.

An independent C model checks 1,728 frame/view combinations across signed frame
boundaries, view presence, playback and constructor success/failure. Additional
checks cover attachment publication, partial output, same-value selection, multiple
views, strict show equality, wrapping, live dirty/create/input mutation and error
prefixes for all migrated operations. A frame-change view release frees the C
state under MSVC AddressSanitizer; no subsequent state access occurs and the
check passes.

All 81 original/converted text-box/layout/Help/text/preview trace pairs match
across the three hosts. Prior hashes remain unchanged. Background checks retain
301,996,032 bytes/FNV-1a `E1A71005` across 12 backgrounds and 188 cameras per host.
All 137 standalone C checks, including strict C89 syntax, pass. Forty-four
selected Native checks and 40 checks in each SDL configuration pass, including
existing Native playback/player regressions. Archive API checks also pass on all
three hosts, alongside the final affected scene/movie checks. The regular headless asset-preview
harness passes; Native `chelp` and `ched` compile. Retained input identities remain
unchanged; identities for 39 binaries are recorded. Test processes use private
temporary directories. Marcus was stopped before rebuilding and relaunched
afterward.

Retained retail captures remain the playback reference; no new capture campaign
or Marcus UI tests were added. This is Windows x64 fixture evidence. Full native
view interaction, arbitrary native callback/override observations and other
platforms remain unverified by this slice. Text-box factory, edit/undo/clipboard
policy, physical view implementations, Marcus paint-order storage and final
C-only host/facade integration remain conversion work.

Evidence: `tmp/c89-text-box-runtime-{baseline,final}-{native,sdl-release,sdl-debug}.bin`,
`tmp/c89-text-box-runtime-{native,sdl-release,sdl-debug}-tests.log`,
`tmp/c89-text-box-runtime-standalone-tests.log`,
`tmp/c89-text-box-runtime-{native,sdl-release,sdl-debug}-affected-tests.log`,
`tmp/c89-text-box-runtime-{layout,topic,balloon,interaction,display,text,views,lifecycle,undo-list,text-undo,copy,access,format,properties,materialize,document,live-text,store,media,metadata,session,admission,live,camera,pick,render}-final-{native,sdl-release,sdl-debug}.bin`,
`tmp/c89-text-box-runtime-asan.log`, `tmp/c89-text-box-runtime-regular-tests.log`,
`tmp/c89-text-box-runtime-tools-build.log`, `tmp/c89-text-box-runtime-trace-comparison.json`
and `tmp/c89-text-box-runtime-identities.json`.

### Text-box document transactions, undo and clipboard (16 November 2026)

`text_box_edit.c` now owns new/read factories, duplication, type/color/show/hide
edit transactions, movie/parent undo registration and clearing, scene-index
lookup, selection property application and formatting-mask queries. Clipboard
adoption, disposal and paste placement/insertion/selection also use C89.
`text_box_undo.c` owns replay and wrapped-record disposal. All five native undo
classes now keep saved values and wrapped references in `MMTextBoxUndo`;
clipboard references live in `MMTextBoxClipboard`. Native MovieUndo base context
access and physical document/scene/view services remain typed bindings.

Factory initialization precedes input-bound reads. Scene/type publish before
background clearing; one-level undo is set last. Metadata ordinary failure alone
adds the bad-file error. Duplication publishes its new owner before suspension
and replacement, releases before nulling failed output, copies defaults before
resuming undo, then copies background/type without copying lifetime/scene.
Exceptions preserve the original completed prefix; no new exceptional cleanup or
rollback is introduced. Native allocator/exception equivalence is not claimed.

Edit registration preserves the original movie-before-parent ordering and
asymmetric failure cleanup. Successful show still clears movie undo. Hide at or
before first delegates to scene removal. Replay switches scene, seeks and looks
up the saved text index. Initial ordinary failures flush then clear undo, which
may destroy the running record. Type/color saved values swap before selection;
size/lifetime swap afterward. Hide's final ordinary false seek remains ignored.
Wrapped edit false returns without the initial-failure cleanup. Disposal clears
parent undo only for the current owning scene, then releases the retained record.

Selection changes retain cleared properties, black color defaults and complement
masks. Selection queries return the first character's formatting and consistency
bits for font, height, bold and italic. Clipboard paste snapshots client dimensions
before duplication, moves to `(177,78)`, intersects the workspace and normalizes
empty bounds to zero. Coordinate arithmetic wraps at 32 bits.

Before conversion, the headless native fixture captured 1,244 bytes on Native
Debug, SDL Release and SDL Debug: FNV-1a `E86E47B5`, SHA-256
`9A982037D751C418528177D7BC0F83F3A8C2B81231B498C38566FE3598CEC21F`.
It freezes initialized/duplicated text, defaults, bounds, type/background, clipboard
adoption, no-view setters/selection queries and real type/color/text undo/redo.
An invisible document is registered in the scene list directly to isolate replay
from view construction. Cleanup clears undo and scene selection before removing
the borrowed fixture entry. No windows, input capture or Marcus UI tests are added.

C providers check factory allocation/read/init failures, publication/cleanup,
duplicate defaults/suspension, transaction ordinary/exception prefixes, parent undo
failure asymmetry, cleared properties/complement masks, reversed selections and
consistency bits. Independent paste dimensions cover clipping/empty results,
including signed coordinate boundaries. All five replay kinds are checked for
saved-value publication stages, failure cleanup, wrapped-edit false, the ignored
last hide seek and wrapped-record disposal. A failed replay clears/frees its
running C record under MSVC AddressSanitizer. Both focused C suites pass ASan.

All 84 original/converted text-box/layout/Help/text/preview trace pairs match
across the three hosts. Prior hashes remain unchanged. Background checks retain
301,996,032 bytes/FNV-1a `E1A71005` across 12 backgrounds and 188 cameras per host.
All 139 standalone C checks, including strict C89 syntax, pass. Forty-six selected
Native checks and 42 checks in each SDL configuration pass, including archive API
and existing Native playback/player regressions. The regular headless asset-
preview harness passes; Native `chelp` and `ched` compile. Retained input identities
remain unchanged; identities for 39 binaries are recorded. Tests use private
temporary directories. Marcus was stopped before rebuilding and relaunched
afterward.

Retained retail captures remain the playback reference; no new capture campaign
or Marcus UI tests were added. This is Windows x64 fixture evidence. Native
size/lifetime replay through real views, selection formatting through actual views,
clipboard paste into a visible scene, arbitrary native callback/override
observations and other platforms remain unverified by this slice. Physical views,
native MovieUndo base-context storage, Marcus paint-order storage and final
C-only host/facade integration remain conversion work.

Evidence: `tmp/c89-text-box-edit-{baseline,final}-{native,sdl-release,sdl-debug}.bin`,
`tmp/c89-text-box-edit-{native,sdl-release,sdl-debug}-tests.log`,
`tmp/c89-text-box-edit-standalone-tests.log`,
`tmp/c89-text-box-edit-{runtime,layout,topic,balloon,interaction,display,text,views,lifecycle,undo-list,text-undo,copy,access,format,properties,materialize,document,live-text,store,media,metadata,session,admission,live,camera,pick,render}-final-{native,sdl-release,sdl-debug}.bin`,
`tmp/c89-text-box-edit-asan.log`, `tmp/c89-text-box-edit-regular-tests.log`,
`tmp/c89-text-box-edit-tools-build.log`, `tmp/c89-text-box-edit-trace-comparison.json`
and `tmp/c89-text-box-edit-identities.json`.

### Persistent movie/actor undo context and storage (16 November 2026)

All MovieUndo subclasses now share a persistent `MMMovieUndoContext` containing
the borrowed movie handle and saved scene/frame. `movie_undo.c` publishes that
context directly during registration, preserving movie/scene publication,
validation, the second scene query, optional frame publication, commit and client
notification order. No current scene preserves the saved frame. Commit/clear
may destroy the undo record; notification does not access it afterward.
MovieSceneUndo's separate operation target index remains distinct.

Actor undo now keeps its owned snapshot reference, ID, flags, last frame and
counted actor name directly in C state. Move/rename composite undo table handles
also persist in C state. Native adapters no longer rebuild/publish temporary
copies of those fields for each operation. Actor/move toggle snapshots the base
context on entry; sooner/later redo restores its temporary frame after virtual
undo dispatch. Counted names retain up to 255 native one/two-byte units, including
embedded zeroes. Native strings still marshal name capture/assignment; typed
array/string-table, actor, movie and scene operations remain provider services.

Before conversion, Native Debug, SDL Release and SDL Debug captured actor, group
and movie integration traces. Actor/movie bytes match across all three hosts.
The group fixture injects allocation failures only in Debug, so its original
Release trace differs and is compared against its own baseline:

| Trace | Bytes | FNV-1a | SHA-256 |
| --- | ---: | --- | --- |
| Actor undo, 32 cases | 3,240 | `64C1B091` | `92642AD0BD1F8D3166F0363931D05CD8E4B07088E9CADC66903877F8DCB81E2B` |
| Composite undo, Debug child/factory/rename cases | 2,100 | `7FB9ADED` | `C5F8CCF0A78D8B8B7508A1918C1B14A66F02F5E2A46799439517CE8A2652B110` |
| Composite undo, SDL Release | 2,100 | `1722FDC5` | `FC2157A8BBFB1CB163AFE5A5D339792144028DFE93847E5FEE375D20BFC93830` |
| Movie editing and undo registration | 61,552 | `85C0C7B5` | `9B5962814AE9C4099AFD8E7357474DB62AEC960D8507DAEA14CE80E184D1B92D` |

The common C test checks zero initialization, distinct host/movie handles,
publication order, both scene queries, preserved frame sentinels, separate
operation indices and commit/clear destruction. Actor tests add maximum counted
byte/UTF-16 names, embedded zeroes, rejected input preservation and direct C state
without observer callbacks. Composite tests mutate the parent's context in a
child and verify subsequent children retain the entry context. All three focused
C suites pass MSVC AddressSanitizer, including heap-record destruction during
common commit/clear.

All 93 original/converted actor/group/movie/text-box/layout/Help/text/preview trace
pairs match across Native Debug, SDL Release and SDL Debug. Prior trace hashes
remain unchanged. Background checks retain 301,996,032 bytes/FNV-1a `E1A71005`
across 12 backgrounds and 188 cameras per host. All 140 standalone checks,
including strict C89 syntax, pass. Fifty selected Native checks and 46 in each
SDL configuration pass, including the existing Native playback/player regressions.
The regular headless asset-preview harness passes; Native `chelp` and `ched`
compile. Retained input identities remain unchanged; identities for 39 binaries
are recorded. Test processes use private temporary directories. Marcus was
stopped before rebuilding and relaunched afterward.

Retained retail captures remain the playback reference. No new capture campaign
or Marcus UI tests were added. This is Windows x64 fixture evidence; arbitrary
native callback/override observations, full view interaction and other platforms
remain unverified by this slice. Typed resource/table providers, physical views,
Marcus paint-order storage and final C-only host/facade integration remain
conversion work.

Evidence: `tmp/c89-undo-state-{actor,group,movie}-{baseline,final}-{native,sdl-release,sdl-debug}.bin`,
`tmp/c89-undo-state-{native,sdl-release,sdl-debug}-tests.log`,
`tmp/c89-undo-state-standalone-tests.log`,
`tmp/c89-undo-state-{text-edit,runtime,layout,topic,balloon,interaction,display,text,views,lifecycle,undo-list,text-undo,copy,access,format,properties,materialize,document,live-text,store,media,metadata,session,admission,live,camera,pick,render}-final-{native,sdl-release,sdl-debug}.bin`,
`tmp/c89-undo-state-asan.log`, `tmp/c89-undo-state-regular-tests.log`,
`tmp/c89-undo-state-tools-build.log`, `tmp/c89-undo-state-trace-comparison.json`
and `tmp/c89-undo-state-identities.json`.

### Persistent Body shape ownership (16 November 2026)

Body's actor/material allocation handles, duplicated set-list reference, part/set
counts and hidden count now live in an embedded `MMBodyShape`. Construction,
duplication/restoration/reshaping and runtime services access that C owner through
one borrowed state view. Native field read/write marshalling is removed; typed
actor, set-list and custom-material accessors expose the same storage.
`mm_body_shape_init` explicitly clears fresh ownership. It must not reinitialize
a live shape.

C value snapshots preserve publication points before resource and renderer
callbacks, including partial allocation failure, hide/show, freed pointer
publication and restored hidden counts. Renderer bindings, typed BRender field
operations, whole-object restoration swapping and the native Body lifetime
facade remain dependencies. Existing geometry/costume C owners are unchanged.

Original traces were captured separately on Native Debug, SDL Release and SDL
Debug before changing storage. Debug allocation-failure injection changes which
cases reach full output, so Release uses its own baseline. The original hashes
are:

| Trace | Configuration | Bytes | FNV-1a |
| --- | --- | ---: | --- |
| Construction | Native/SDL Debug | 88,136 | `B987104E` |
| Construction | SDL Release | 94,824 | `B68BBB56` |
| Duplication/restoration/reshape | Native/SDL Debug | 1,880,872 | `5D4D724C` |
| Duplication/restoration/reshape | SDL Release | 1,916,616 | `2FACFF0C` |
| Materials/visibility/costumes | Native/SDL Debug | 427,368 | `28BFCBC9` |
| Materials/visibility/costumes | SDL Release | 437,040 | `121CA853` |
| Geometry/render bounds/picking | All three | 76,608 | `ECFB9185` |

The fixtures cover partial construction/destruction, sparse sets and hierarchy
links; duplicate/reshape allocation failures and rollback; model/material
reference balance, hidden counts and renderer notifications; and bounds/hit
state. Existing C probes retain publication and release-order assertions through
the borrowed owner view, with an added fresh-state initialization check. All four
focused suites pass MSVC AddressSanitizer.

All 140 standalone checks, including strict C89 syntax, pass. Fifty-four selected
Native checks and 50 in each SDL configuration pass, including the existing
Native playback/player regressions. The regular headless asset-preview harness
passes; Native `chelp` and `ched` compile. Background checks retain 301,996,032
bytes/FNV-1a `E1A71005` across 12 backgrounds and 188 cameras per host. All ten
retained input identities are unchanged; identities for 39 binaries are recorded.
Test processes use private temporary directories. Marcus was stopped before
rebuilding and relaunched afterward.

Retained retail captures remain the playback reference; no new capture campaign
or Marcus UI tests were added. This is Windows x64 fixture evidence. Arbitrary
native callback observations and other platforms remain unverified. Renderer
bindings, resource/SDK providers, whole-object restoration and final C-only
host/facade integration remain conversion work.

All 105 original/converted body/undo/text-box/layout/Help/text/preview trace pairs
match across the three hosts, including all twelve Body captures. Prior hashes
remain unchanged. Final Body captures use `shape-*` filenames to distinguish
Body runtime from the separate text-box runtime trace.

Evidence: `tmp/c89-body-state-{create,edit,runtime,geometry}-baseline-{native,sdl-release,sdl-debug}.bin`,
`tmp/c89-body-state-shape-{create,edit,runtime,geometry}-final-{native,sdl-release,sdl-debug}.bin`,
`tmp/c89-body-state-shape-final-{native,sdl-release,sdl-debug}.log`,
`tmp/c89-body-state-{native,sdl-release,sdl-debug}-tests.log`,
`tmp/c89-body-state-standalone-tests.log`,
`tmp/c89-body-state-{create,edit,runtime,geometry}-asan.log`,
`tmp/c89-body-state-regular-tests.log`, `tmp/c89-body-state-tools-build.log`,
`tmp/c89-body-state-trace-comparison.json` and `tmp/c89-body-state-identities.json`.

### Persistent Template state shared with ThreeDText (16 November 2026)

Template parent/set/costume table ownership, rest orientation, flags and counts
now persist in `MMTemplateState`, initialized explicitly for fresh native owners.
Ordinary template loading, runtime operations and generated-text replacement use
borrowed views of that same C record. Native field read/write marshalling is
removed from both Template and ThreeDText services. Typed table accessors and
native getters/debug validation read the C owner directly. The native name String,
typed resource/container services and final facade integration remain dependencies.

C value snapshots retain publication order during partial initialization,
disposal and generated-list replacement. Failure keeps the previous generated
owners; success releases/clears/replaces parent, set and costume references before
updating generated material/action owners and counts. The generated-resource owner
remains distinct. Native rest-orientation getters keep their 16-bit BAM conversion;
header wire codecs and host-provided object/table size accounting are unchanged.
The persistent record stores rest angles as 16-bit unsigned values. An initial
widened representation increased the native text-object estimate by eight bytes;
the recorded resource trace exposed that mismatch and the field widths were
corrected before final verification.

Original native traces were captured separately on Native Debug, SDL Release and
SDL Debug before conversion. Allocation-failure injection changes the Release
case outputs, so each configuration is compared with its own original baseline:

| Trace | Configuration | Bytes | FNV-1a |
| --- | --- | ---: | --- |
| Template construction | Native/SDL Debug | 306,048 | `16CA3EB0` |
| Template construction | SDL Release | 376,992 | `DA9EBE3D` |
| Template runtime | Native/SDL Debug | 241,552 | `A1E50175` |
| Template runtime | SDL Release | 241,696 | `04C418B3` |
| Text generation | Native/SDL Debug | 4,378,609 | `B79A006F` |
| Text generation | SDL Release | 4,413,472 | `673CB1B2` |
| Text resources | Native/SDL Debug | 279,277 | `ABC5FA67` |
| Text resources | SDL Release | 284,193 | `CF87F4D7` |

Fixtures retain their existing normalization of undefined template-header padding;
defined wire fields, table contents, rest angles, counts, names, body state and
ownership are compared. Cases cover missing/opposite-order tables, generated
costume groups, default-resource failures, virtual action/model dispatch,
accessories, generated lists/actions/matrices, resource read/write/change/rollback
and font-provider inheritance. C providers retain partial-publication/release-order
checks and compare all defined saved-state fields after failed generation, without
depending on struct padding or removed publication callbacks. All four focused
suites pass MSVC AddressSanitizer.

All 117 original/converted template/text-resource/body/undo/text-box/layout/Help/
text/preview trace pairs match across the three hosts, including the original
cache estimates. Prior hashes remain unchanged. All 140 standalone checks,
including strict C89 syntax, pass. Fifty-eight selected Native checks and 54 in
each SDL configuration pass, including the existing Native playback/player
regressions. The regular headless asset-preview harness passes; Native `chelp`
and `ched` compile. Background checks retain 301,996,032 bytes/FNV-1a `E1A71005`
across 12 backgrounds and 188 cameras per host. All ten retained input identities
are unchanged; identities for 39 binaries are recorded. Test processes use private
temporary directories. Marcus was stopped before rebuilding and relaunched
afterward.

Retained retail captures remain the playback reference; no new capture campaign
or Marcus UI tests were added. This is Windows x64 fixture evidence. Arbitrary
native callback/override observations and other platforms remain unverified.
Native String/object lifetimes, typed resource/container/SDK providers, physical
views and final C-only host/facade integration remain conversion work.

Evidence: `tmp/c89-template-state-{tmpl-create,tmpl-runtime,text-generation,text-resource}-{baseline,final}-{native,sdl-release,sdl-debug}.bin`,
`tmp/c89-template-state-{native,sdl-release,sdl-debug}-tests.log`,
`tmp/c89-template-state-standalone-tests.log`,
`tmp/c89-template-state-{template-create,template-runtime,text-generation,text-resource}-asan.log`,
`tmp/c89-template-state-regular-tests.log`, `tmp/c89-template-state-tools-build.log`,
`tmp/c89-template-state-initial-cache-estimate-diff.json`,
`tmp/c89-template-state-trace-comparison.json` and `tmp/c89-template-state-identities.json`.

### Shared C reference ownership and cache counter views (16 November 2026)

`reference.c` now owns common reference initialization, retain/drop arithmetic,
ordinary final-release dispatch and marked-file release policy. Native BASE
embeds `MMReference`; its constructor, AddRef and Release delegate to C. The
counter is still four bytes on Windows, checked against the former native field
size. Cacheable objects and cache owners expose borrowed C counter views instead
of numeric read/write callbacks. Existing cache-specific release, detach,
notification and temporary owner-guard policy remains in `resource_cache.c`.

Cached/marked zero-count owners can be retained again. Ordinary release publishes
the decremented count before destruction. Marked-file release leaves a marked
zero-count owner open; invalid release diagnoses before clamping to zero without
destroying it. Destroy callbacks may free their own counter and release another
owner; C performs no later owner access. Document/graphics overrides use the same
C drop primitive while keeping their existing finalization services. Chunk-file
release still uses its existing C policy and publishes into the shared counter.
Native allocation, debug tracking, virtual destruction, physical views and COM
audio adapter counters remain dependencies.

The new C suite checks retained owners, cascade destruction after freeing the
outer record, marked zero-count retention/revival, diagnostic observation before
clamping, repeated retain/drop and defined 32-bit diagnostic arithmetic. Both it
and the cache suite pass MSVC AddressSanitizer. Counts remain non-atomic; callers
keep their existing synchronization requirements. Supported native comparisons
do not claim equivalence for signed overflow or invalid lifetime use.

Original `resource-cache-test` traces were captured on all three hosts before
conversion: 1,624 bytes, FNV-1a `AE51E913`, SHA-256
`2E6FDD86E329D725DD7AAA158BDF460564DBBEEE7972773675AC93A66D2177FB`.
The trace freezes cache/manager ordering, zero-count caching, detach/owner
references, cascading releases, reader/scope distinctions and failure stops. The
Native focused converted trace matches exactly. Full `core-checks` targets compile
on Native Debug, SDL Release and SDL Debug; the standalone C engine also builds.

The broader gate exposed missing C test dependencies in `core-checks`.
`engine-c89-checks` now automatically depends on every C engine test executable,
and root `core-checks` depends on that target. Native common-C tests were rebuilt
and rechecked after this correction. The optimized cache fixture also exposed
identical-code folding of its two empty dummy reader functions: both had one
address, producing reader ID 11 instead of 10. A diagnostic build passed with
folding disabled. Distinct volatile effects now preserve the intended identities
without changing production linker settings; the corrected suite passes in all
three host configurations and under ASan.

The final wider gate passes 203 Native checks, 198 SDL Release checks and 199 SDL
Debug checks. SDL Release additionally reports its existing debug-only
`world-export` fixture as skipped; that fixture is not a Release pass. Its C89
policy suite and the Native/SDL Debug integration fixtures pass. All 141
standalone checks, including strict C89 syntax, pass. Thus 741 of 742 selected
checks pass, with one declared skip. All 141 common-engine Native checks were
also rebuilt/rechecked after the dependency correction, and the corrected cache
fixture was rerun in each host configuration.

All 120 original/converted cache/template/text/body/undo/Help/preview trace pairs
match across the three hosts. Prior hashes and all ten retained input identities
are unchanged. Identities for 50 binaries are recorded. The regular headless
asset-preview harness passes; Native `chelp` and `ched` compile. Background checks
retain 301,996,032 bytes/FNV-1a `E1A71005` across 12 backgrounds and 188 cameras per
host. Asset tests use private temporary directories. Marcus was stopped before
rebuilding and relaunched afterward.

Retained retail captures remain the playback reference; no new capture campaign
or Marcus UI tests were added. This is Windows x64 fixture evidence. Arbitrary
native callback/override observations, a Release run of the debug-only export
fixture and other platforms remain unverified. Native object allocation,
debug/virtual destruction services, physical file/view implementations and final
C-only host/facade integration remain conversion work.

Evidence: `tmp/c89-reference-cache-{baseline,final}-{native,sdl-release,sdl-debug}.bin`,
`tmp/c89-reference-{native,sdl-release,sdl-debug}-tests.log`,
`tmp/c89-reference-native-complete-c-tests.log`,
`tmp/c89-reference-{native,sdl-release,sdl-debug}-cache-tests.log`,
`tmp/c89-reference-standalone-tests.log`,
`tmp/c89-reference-{reference,reference-cache}-asan.log`,
`tmp/c89-reference-reader-{diag,nofold}.log`,
`tmp/c89-reference-regular-tests.log`, `tmp/c89-reference-tools-build.log`,
`tmp/c89-reference-trace-comparison.json` and `tmp/c89-reference-identities.json`.


### Shared C counted-string mutation, parsing and escaping (16 November 2026)

`string_value.c` now owns counted-string copy/set/delete/append/insert, exact
comparison, number parsing and control expansion. Native String delegates to the
same C89 implementation for both character widths, using borrowed native buffers
and native copy diagnostics. Copy checks observe the existing length and the
already-shifted insertion tail before the new characters are copied. Native
String formatting/varargs, encoding adapters, direct-buffer access and final
facade removal remain work.

The conversion preserves insertion's unusual overflow retained-length calculation,
append truncation, automatic-base repeated-sign handling, digit-equal-to-radix
acceptance, empty hexadecimal prefixes and defined 32-bit wrap. Escaping keeps
CR-to-backslash-n, literal LF and partial publication only after an escape has
been emitted. Failed C parsing leaves its output untouched; the native adapter
retains zero/debug-trash output on failure.

Before conversion, the expanded `string-compat` fixture captured 122,840,792 bytes
on each of Native Debug, SDL Release and SDL Debug. All three original traces
have FNV-1a `C67CD6FD` and SHA-256
`5F85FEBAE3734CF811FB6BB5C4A758E3B01BAEABDA6FBAB28524E24EDFE99B15`.
The corpus covers copy/assignment, set/append truncation, every insertion/deletion
position at selected lengths/counts, out-of-range insertion clamping, each escaped
control at every position/length through capacity and automatic/explicit numeric
bases. It records defined results, lengths and characters, not unused storage or
failed debug-trash outputs. The standalone C fixture freezes that original
fingerprint for both widths and adds guard units, non-ASCII wide characters,
multiple-escape partial publication and copy-observation checks.

The final wider gate passes 204 Native checks, 199 SDL Release checks and 200 SDL
Debug checks. SDL Release additionally reports the existing debug-only
`world-export` fixture as skipped; its C policy and Native/SDL Debug integration
checks pass. All 142 standalone checks, including strict C89 syntax, pass. Thus
745 of 746 selected checks pass, with one declared skip. The new C suite passes
MSVC AddressSanitizer for both character widths.

All 123 original/converted string/cache/template/text/body/undo/Help/Preview trace
pairs match across the three hosts. All ten retained input identities are
unchanged; identities for 57 binaries are recorded. The regular headless
asset-preview harness passes; Native `chelp` and `ched` compile. Background checks
retain 301,996,032 bytes/FNV-1a `E1A71005` across 12 backgrounds and 188 cameras per
host. Test processes use private temporary directories. Marcus was stopped before
rebuilding and relaunched afterward.

Retained retail captures remain the playback reference; no new capture campaign
or Marcus UI tests were added. This is Windows x64 fixture evidence. The two-byte
C corpus checks common value semantics and wide characters, not a complete native
Unicode host build. Arbitrary native callback/override observations, invalid
lifetime/overlapping character-copy use and other platforms remain unverified.
The string and broader engine dependencies listed above remain conversion work.

Evidence: `tmp/c89-string-value-{baseline,final}-{native,sdl-release,sdl-debug}.bin`,
`tmp/c89-string-value-{native,sdl-release,sdl-debug}-tests.log`,
`tmp/c89-string-value-standalone-tests.log`, `tmp/c89-string-value-asan.log`,
`tmp/c89-string-value-regular-tests.log`,
`tmp/c89-string-value-trace-comparison.json` and `tmp/c89-string-value-identities.json`.


### Shared C counted-string formatter (16 November 2026)

`string_format.c` now owns format parsing, qualifier ordering, field widths,
signed/unsigned/hexadecimal conversion, four-byte tags, padding, output limits
and publication of the completed prefix. The native String bridge retains typed
varargs extraction, `va_copy`/`va_end`, String/zero-terminated-string validation
and debug diagnostics. C receives only 32-bit values or borrowed text/counts.
The previous C++ format evaluator has been removed.

The conversion retains rejection of positional `%<N>` notation before argument
fetch, order-sensitive left/zero padding, explicit signs on nonnumeric fields,
width-limited hexadecimal masking, zero tag-byte substitution with byte 1 and
the distinct field/literal capacity rules. A field that would exactly fill the
255-unit buffer fails before writing that field; literals may fill it. Argument
providers still run before the field capacity check. Malformed-format diagnostics
observe already-written characters and the old count before prefix publication.
C width accumulation now wraps explicitly at 32 bits; original signed-overflow
width inputs are undefined and are outside the native equivalence claim.

Before conversion, `string-compat` captured 19,662,900 bytes on each of Native
Debug, SDL Release and SDL Debug: FNV-1a `086D60CB`, SHA-256
`19F7FB4E4EFFD3A521D414F1B61425414883D63E27BFFEA89C0F607B57637C54`.
The shared corpus spans all supported controls, qualifier permutations, selected
widths/prefixes, integer extrema, counted/raw string sizes, every literal/field
capacity boundary, malformed formats, repeated qualifiers and mixed pointer/
numeric varargs. Debug fixtures also inspect malformed-format diagnostic timing;
Release retains the same return/output fingerprint without debug callbacks.
The standalone C fixture freezes that original fingerprint for both unit widths,
checks guard units and adds provider failure, fetch-before-overflow, sign/padding
copy observations, wide non-ASCII values and defined width arithmetic.

The final wider gate passes 205 Native checks, 200 SDL Release checks and 201 SDL
Debug checks. SDL Release additionally reports the existing debug-only
`world-export` fixture as skipped; its C policy and Native/SDL Debug integration
checks pass. All 143 standalone checks, including strict C89 syntax, pass. Thus
749 of 750 selected checks pass, with one declared skip. The new C formatter suite
passes MSVC AddressSanitizer for both character widths.

All 126 formatter/value/cache/template/text/body/undo/Help/Preview trace pairs
match across the three hosts. All ten retained input identities are unchanged;
identities for 61 binaries are recorded. The regular headless asset-preview
harness passes; Native `chelp` and `ched` compile. Background checks retain
301,996,032 bytes/FNV-1a `E1A71005` across 12 backgrounds and 188 cameras per host.
Test processes use private temporary directories. Marcus was stopped before
rebuilding and relaunched afterward.

Retained retail captures remain the playback reference; no new capture campaign
or Marcus UI tests were added. This is Windows x64 fixture evidence. The two-byte
C corpus is not a complete native Unicode host build. Arbitrary native callback/
override observations, invalid lifetime/overlapping character-copy use and other
platforms remain unverified. Native ABI/text services, String facade removal and
the broader C-only engine integration remain conversion work.

Evidence: `tmp/c89-string-format-{baseline,final}-{native,sdl-release,sdl-debug}.bin`,
`tmp/c89-string-format-value-final-{native,sdl-release,sdl-debug}.bin`,
`tmp/c89-string-format-{native,sdl-release,sdl-debug}-tests.log`,
`tmp/c89-string-format-standalone-tests.log`, `tmp/c89-string-format-asan.log`,
`tmp/c89-string-format-regular-tests.log`,
`tmp/c89-string-format-trace-comparison.json` and `tmp/c89-string-format-identities.json`.


### Shared C string validation, ordering and text-service control (16 November 2026)

`string_ops.c` now owns counted validation, unbounded zero-terminated length,
exact equality, length-first/sign-aware internal ordering, 255-unit equality
chunks, user-comparison fallback, legacy character flags, in-place translation
and ANSI convenience conversion. Native String wrappers retain diagnostics and
provider binding. C conversion functions report integer success/failure while
the native void methods keep their existing clearing/truncation behavior.
The previous C++ evaluators for these operations have been removed.

Case-insensitive equality still copies both chunks before uppercasing either,
compares their bytes and stops on the first failed chunk. It is not replaced by
locale collation. Collation failures still fall back to length-first internal
ordering. Count validation rejects oversized headers before payload checks;
terminated counted reads retain the native count-access diagnostic before testing
the final zero. Character flags and translation retain their existing narrow/
wide behavior, including lossy byte-map direction and conditional UTF-16 swapping.
ANSI conversion retains its 255-unit conversion capacity including termination,
import failure clearing count/first character, export failure clearing only byte
zero, and the existing UTF-16LE wide-export marker.

`MMTextCaseMaps` now holds the persistent upper/lower arrays in C. C initializes
both identity arrays, calls upper then lower providers and applies the selected
map. The native synchronized first-use guard remains. Native code-page, whole-
buffer case and locale calls remain providers; no portable CP1252 substitution
is imposed on Windows ACP/locale behavior. Empty-count comparison, native
signed-32-bit count limits and ranked comparison-result conversion now use C too.

Before conversion, `string-compat` captured identical primitive and user/text
results across Native Debug, SDL Release and SDL Debug:

| Trace | Bytes | FNV-1a | SHA-256 |
| --- | ---: | --- | --- |
| Primitive validation/length/equality/order/flags | 792,204 | `DEBC3794` | `B80467D256D51D98B9B2690482DBB52E83E8785565B44A70A638AC77DE109514` |
| User equality/collation, ANSI import/export, translation | 372,128 | `14755473` | `97B9A16D1A168B1AFE00098620F8F9C5927DAB5A0C51A709FF8524467D2963C1` |

The primitive corpus spans every byte-pair comparison, length-first cases,
unbounded length diagnostics, every counted/null position and character flags.
The user corpus covers flags, mutations at every position across the 255-unit
chunk boundaries, unequal counts, ANSI lengths through capacity and both byte-map
directions. User traces depend on the capture environment's code page/locale;
their hash is evidence for that environment, not a universal collation constant.
C fixtures additionally check unsigned wide ordering/validation, copy/case/range
callback order, first-chunk stops, comparison failures/fallback, map provider
observations, empty-before-overflow ordering and conversion providers/failures.
Existing native text-service tests retain direct Windows API comparisons and
concurrent first-use checks; portable text tests remain separate.

The final wider gate passes 206 Native checks, 201 SDL Release checks and 202 SDL
Debug checks. SDL Release additionally reports the existing debug-only
`world-export` fixture as skipped; its C policy and Native/SDL Debug integration
checks pass. All 144 standalone checks, including strict C89 syntax, pass. Thus
753 of 754 selected checks pass, with one declared skip. The new C suite passes
MSVC AddressSanitizer, including wide/provider failure and byte-import truncation
results. C functions report failure explicitly while native void methods retain
their existing output policy.

All 132 string/text/cache/template/body/undo/Help/Preview trace pairs match across
the three hosts. String formatter snapshots use `formatter-final` to distinguish
them from rich-text `format-final` dumps; the three string fixtures were
recaptured after correcting that verification output-name collision. No baseline
was changed. All ten retained input identities are unchanged; identities for 71
binaries are recorded. The recorded text environment is Windows 11 x64, ACP 1252,
user/thread LCID 2057 (`en-GB`); OEM CP 437 is recorded separately.

The regular headless asset-preview harness passes; Native `chelp` and `ched`
compile. Background checks retain 301,996,032 bytes/FNV-1a `E1A71005` across 12
backgrounds and 188 cameras per host. Test processes use private temporary
directories. Marcus was stopped before rebuilding and relaunched afterward.

Retained retail captures remain the playback reference; no new capture campaign
or Marcus UI tests were added. This is Windows x64 fixture evidence. C wide
ordering, validation and conversion tests are not a complete native Unicode host
build. Other code pages/locales/platforms, arbitrary native callback/override
observations and invalid lifetime/overlapping character-copy use remain
unverified. Native allocation/object diagnostics, typed OS/ABI services,
raw-view/facade removal and final C-only engine integration remain work.

Evidence: `tmp/c89-string-ops-{baseline,final}-{native,sdl-release,sdl-debug}.bin`,
`tmp/c89-string-ops-user-{baseline,final}-{native,sdl-release,sdl-debug}.bin`,
`tmp/c89-string-ops-{native,sdl-release,sdl-debug}-tests.log`,
`tmp/c89-string-ops-{native,sdl-release,sdl-debug}-recapture.log`,
`tmp/c89-string-ops-standalone-tests.log`, `tmp/c89-string-ops-asan.log`,
`tmp/c89-string-ops-regular-tests.log`, `tmp/c89-string-ops-text-environment.json`,
`tmp/c89-string-ops-trace-comparison.json` and `tmp/c89-string-ops-identities.json`.


### Shared C fixed-memory transactions, counters and error stacks (16 November 2026)

`memory.c` now owns fixed allocation/resize/free transaction control, clearing/
garbage fill, growth-only failure injection, allocation counters, maximum request
checks and memory-liberation retry/reentrancy policy. Native fixed allocators use
that implementation with borrowed raw heap/mutex/debug-header providers. Native
BASE and HQ statistics use the same persistent `MMMemoryStats` state and C
counter operations. Counter fields retain the checked Windows long ABI and
seven-word record size; supported native comparisons exclude signed overflow.
The C diagnostic arithmetic itself wraps explicitly at 32 bits.

Allocation publishes failure as nil before error reporting. Resize failure keeps
the original pointer, payload and footer, including failed shrink and injected
growth. Liberation runs outside the memory mutex, suppresses nested failed
liberation and retries only after a positive freed-byte result. Allocate requests
include debug overhead; resize requests retain their signed byte delta. Cleared/
garbage growth includes the old footer before a new footer is written. Free
unlinks/updates counters, poisons debug storage, publishes the raw pointer for
release, then nil. The C core passes a stable pre-reallocation address token to
physical relocation providers instead of reading an invalidated old pointer value.

`error_stack.c` now owns persistent plain/debug stack records, count, adjacent
duplicate suppression, oldest-entry eviction, stable flush, indexed/contains/pop/
clear operations and reporting/locking/output order. Every debug push reports
before locking, even when deduplicated. Overflow reports while full and locked
before eviction. Pop writes or debug-trashes output before unlocking; indexed
misses retain the legacy -1 sentinel. Native ErrorStack retains virtual dispatch,
its mutex and formatting/diagnostic providers. Source-file strings remain
borrowed. ErrorStack object sizes and allocator counter sizes match the original
native traces; neither record family changes a stored movie format.

The pre-conversion headless `memory-runtime` fixture captures error and allocation
behavior, including warning-time stack observations, virtual error dispatch,
failure-time owner publication, duplicate/full-stack/flush/underflow operations,
clear/garbage bytes, zero/grow/shrink/same-size transactions, failure injection,
request-limit errors and all seven counter fields:


| Configuration | Bytes | FNV-1a | SHA-256 |
| --- | ---: | --- | --- |
| Native/SDL Debug | 18,516 | `B78C4CC3` | `35071E0D6DB27FEBD769566DC89451EA103C0ED43C8B91500A437F6D2EAD6CE5` |
| SDL Release | 12,604 | `1443F1C9` | `70CE1EA69633E1302865DAB5062A72AB34E17C3B874A2EB4042D4B94454518BF` |

The new C suites cover both error-record layouts, retained diagnostic source/line,
warning/lock/output order and inactive-record lifetime; allocator providers force
moved allocations, failed grow/shrink, positive/zero/negative liberation results,
reentrant failed liberation, size/injection ordering, clear/garbage growth and
free-time pointer/stat observations. Default C heap services and defined counter
arithmetic are checked separately. Both suites pass MSVC AddressSanitizer. Native
trace comparisons normalize raw addresses and implementation source-line changes;
defined payload/counter/error values and native record sizes remain compared.

The final wider gate passes 209 Native checks, 204 SDL Release checks and 205 SDL
Debug checks. SDL Release additionally reports the existing debug-only
`world-export` fixture as skipped; its C policy and Native/SDL Debug integration
checks pass. All 146 standalone checks, including strict C89 syntax, pass. Thus
764 of 765 selected checks pass, with one declared skip.

All 135 memory/error/string/text/cache/template/body/undo/Help/Preview trace pairs
match across the three hosts. All ten retained input identities are unchanged;
identities for 82 binaries are recorded. The regular headless asset-preview
harness passes; Native `chelp` and `ched` compile. Background checks retain
301,996,032 bytes/FNV-1a `E1A71005` across 12 backgrounds and 188 cameras per host.
Test processes use private temporary directories. Marcus was stopped before
rebuilding and relaunched afterward.

Retained retail captures remain the playback reference; no new capture campaign
or Marcus UI tests were added. This is Windows x64 fixture evidence. Actual
native raw-heap OOM/reentrant-liberator observations, 32-bit diagnostic stack
captures, corrupted native headers/incorrect claimed old sizes, signed counter
overflow, arbitrary native callback/override behavior and other platforms remain
unverified. The C fake heap covers the retry/failure/provider contract without
claiming a native raw-heap failure campaign. Native heap alignment/size headers,
BASE object allocation/deletion, debug block-list ownership/mark scans and final
C-only host/facade integration remain conversion work.

Evidence: `tmp/c89-memory-{baseline,final}-{native,sdl-release,sdl-debug}.bin`,
`tmp/c89-memory-{native,sdl-release,sdl-debug}-tests.log`,
`tmp/c89-memory-standalone-tests.log`, `tmp/c89-memory-asan.log`,
`tmp/c89-error-stack-asan.log`, `tmp/c89-memory-regular-tests.log`,
`tmp/c89-memory-trace-comparison.json` and `tmp/c89-memory-identities.json`.

### Shared C raw heap and BASE allocation transactions (16 November 2026)

`heap.c` now owns raw allocate/reallocate/free policy and private logical-size
metadata. Checked size arithmetic retains a physical byte for zero-size requests;
failed reallocation preserves pointer, recorded size and payload. Native wrappers
supply the original `std::max_align_t` header width, preserving alignment and
header size. Standalone C uses its own fundamental C89 alignment profile. Size
metadata uses `memcpy`; neither profile is serialized into a movie. Null raw
providers use libc. Optional providers enable deterministic failure checks.

`object_memory.c` now owns BASE storage allocation/deletion transaction policy:
failure simulation under object-then-memory locks, cleared allocation, metadata
linking before counter publication, OOM reporting after unlocking, and unlink,
counter decrement, complete-block poisoning and release. Object allocation has
no memory-liberation retry. Native metadata/list operations, source/thread/stack
capture and lock primitives remain providers. Constructors, destructors, raw-list
attachment and debug marking remain native. No public BASE layout or movie format
changes. Defined C prefix-overflow rejection and null-free behavior extend the C
contract; these do not demonstrate original Debug equivalence for invalid inputs.

Before conversion, `heap-object-runtime` captured native logical sizes, payload
alignment/clearing/preservation, zero/grow/shrink transactions and overflow
rejection, plus direct raw BASE allocations, constructed objects, references,
virtual destruction, diagnostic counters and injected failure countdowns. The
fixture records diagnostic warning-time error-stack contents and excludes raw
addresses and implementation source-line changes. The original implementation
was revision `2412211e` with the fixture added before production changes.

| Configuration | Bytes | FNV-1a | SHA-256 |
| --- | ---: | --- | --- |
| Native/SDL Debug | 8,972 | `AAB1DB3F` | `496ACF072E32292D8784F60AFEBF1AA38A44580C833CCEB371F374046A9A8958` |
| SDL Release | 8,612 | `57C0516D` | `7E3B1491215521B3DE13C7B71B901438CB6C9397681A8877BD13740EBA20D3A0` |

`engine-heap` checks deterministic allocation/reallocation failure, preserved
size/data, checked size boundaries before provider calls, zero requests and libc
fallback. `engine-object-memory` checks failure countdowns, unretried raw failure,
lock/hook/counter ordering, cleared payloads, poisoning before release and C
prefix-overflow/null-free behavior. Both suites pass MSVC AddressSanitizer.

The wider gate passes 212 Native checks, 207 SDL Release checks and 208 SDL Debug
checks. SDL Release additionally reports the existing debug-only `world-export`
fixture as skipped. All 148 standalone checks, including strict C89 syntax,
pass: 775 of 776 selected checks, with one declared skip. The independent
standalone platform project builds with the C heap module and passes all five
checks. The regular headless asset-preview harness passes; Native `chelp` and
`ched` compile. Backgrounds retain 301,996,032 bytes/FNV-1a `E1A71005` across 12
backgrounds and 188 cameras per host. Marcus was stopped before rebuilding and
relaunched afterward; test processes use private temporary directories.

All 138 heap/object/memory/error/string/text/cache/template/body/undo/Help/Preview
trace pairs match across the three hosts. All ten retained input identities are
unchanged; identities for 98 binaries are recorded. Windows text settings remain
ACP 1252, OEMCP 437 and locale `en-GB`/2057.

Retained retail captures remain the playback reference; no new retail capture
campaign or Marcus UI tests were added. This is Windows x64 fixture evidence.
Actual native malloc/realloc exhaustion, arbitrary callback/override behavior,
32-bit diagnostic call stacks, corrupt headers/invalid object lifetimes, signed
counter overflow and other platforms remain unverified. Moving stack collection
into a native provider adds call frames on x86; exact diagnostic stack equivalence
is not claimed. Native object/debug graph ownership, constructor attachment and
mark scans, virtual destruction and final C-only facade integration remain work.

Evidence: `tmp/c89-heap-object-{baseline,final}-{native,sdl-release,sdl-debug}.bin`,
`tmp/c89-heap-object-{native,sdl-release,sdl-debug}-tests.log`,
`tmp/c89-heap-object-standalone-tests.log`, `tmp/c89-heap-asan.log`,
`tmp/c89-object-memory-asan.log`, `tmp/c89-heap-object-platform-tests.log`,
`tmp/c89-heap-object-regular-tests.log`,
`tmp/c89-heap-object-trace-comparison.json` and
`tmp/c89-heap-object-identities.json`.

### Shared C BASE diagnostic graph, attachment and marking (16 November 2026)

`object_debug.c` now owns persistent raw/constructed graph records and list heads,
source/line/size/thread/stack fields, previous-slot links, sixteen-bit marks and
the BASE kind word. Native hosts retain the original aligned header profile and
padding. C89 controls link/unlink validation, constructor attachment, object-kind,
reference-range/flag checks, mark dispatch/filtering, unmarking and lost scans.
The graph owns links; allocation transactions own storage. Graph/record addresses
remain fixed while linked because previous links point to owning slots.

Constructor attachment retains the initial raw-head probe before locking, then
refreshes after locking. An empty raw list avoids the mutex during global object
construction. This preserves the legacy bootstrap convention; the C contract
requires caller-serialized publication around that probe and does not establish
arbitrary concurrent construction safety. Recursive locking and validation order
remain. Native providers supply pointer checks, thread IDs, diagnostic formatting,
breakpoints, native validation and virtual mark calls. C records borrow source strings.
Constructors/destructors execute outside C callbacks; callbacks cannot unwind or
delete/move graph records during traversal.

Lost scans count candidates first and traverse oldest first, rechecking marks
after each callback. Marking descendants/cycles can reduce subsequent detail
reports relative to the initial summary count. Allocated mark stubs dispatch only
for unmarked records owned by the current thread; local objects always dispatch.
Unmarking only clears the current thread's records. No movie wire layout changes.

The pre-conversion `object-graph-runtime` fixture at revision `8827ec7c` records
public-API behavior for out-of-order raw attachment, a raw object freed without
construction, nonempty raw-list diagnostics, oldest-first lost reports, detailed/
summary-only reports, descendant and cyclic marking, local objects, repeated
stubs, virtual destruction, failed C++ constructor cleanup and synchronized
cross-thread ownership/filtering. It also records BASE/derived sizes. Summary
source lines and raw addresses are excluded; retained per-object source labels,
lines, class IDs, message text and diagnostic stack byte counts are compared.

| Configuration | Bytes | FNV-1a | SHA-256 |
| --- | ---: | --- | --- |
| Native/SDL Debug | 3,228 | `9F1F94E7` | `C73CA2DA3A4FDCC30124AE1245AFCCCF6518BEFE12A1EC7197912521A3A4A351` |
| SDL Release | 140 | `AD2E98C9` | `FD1E0E30D13C0D0AF9288EDFF7D06649D05F736F021D7FB76A17E2D831AF1523` |

SDL Release disables native diagnostics, so that trace covers ordinary object
lifetime and constructor cleanup. Its C suite still exercises the debug graph.

`engine-object-debug` checks head/middle/tail links and unlink cleanup, native
metadata fields, an emptied raw list after lock acquisition, no-lock local
attachment, recursive lock depth, kind publication before validation, ownership
filters, cyclic marking, initial-count versus detail ordering, readable invalid
metadata/link/flag/reference diagnostics and C mark wrap. These checks pass MSVC
AddressSanitizer. The wrap test specifies C behavior separately from native
signed-overflow equivalence.

The wider gate passes 214 Native checks, 209 SDL Release checks, 210 SDL Debug
checks and all 149 standalone checks, including strict C89 syntax: 782 of 783
selected checks pass. The remaining declared skip is the existing SDL Release
`world-export` Debug fixture. All 141 retained graph/heap/object/memory/error/
string/text/cache/template/body/undo/Help/Preview trace pairs match. All ten input
identities remain unchanged; identities for 100 binaries are recorded. Windows
text settings remain ACP 1252, OEMCP 437 and locale `en-GB`/2057.

The regular headless asset-preview harness passes, with 301,996,032 background
bytes/FNV-1a `E1A71005` across 12 backgrounds and 188 cameras per host. Native
`chelp` and `ched` compile. Marcus was stopped for rebuilding and resource-file
regeneration and relaunched afterward. Test processes use private temporary
directories. No Marcus UI tests or new retail capture campaign were added;
retained retail captures remain the playback reference.

This is Windows x64 fixture evidence. Arbitrary simultaneous construction,
bootstrap publication, callback-driven record deletion/relinking, invalid pointer
lifetimes, corrupted native addresses, diagnostic mark overflow, 32-bit stack
captures and other platforms remain unverified. Readable C metadata violations
and defined C wrap do not establish native equivalence for invalid lifetimes or
overflow. Fixed-memory debug graphs/marks, BaseLinkedList attachment, native
virtual destruction and final C-only host/facade integration remain conversion
work; this does not complete the backend migration.

Evidence: `tmp/c89-object-graph-{baseline,final}-{native,sdl-release,sdl-debug}.bin`,
`tmp/c89-object-graph-{native,sdl-release,sdl-debug}-tests.log`,
`tmp/c89-object-graph-standalone-tests.log`, `tmp/c89-object-debug-asan.log`,
`tmp/c89-object-graph-regular-tests.log`,
`tmp/c89-object-graph-trace-comparison.json` and
`tmp/c89-object-graph-identities.json`.

### Shared C fixed-memory diagnostic graph, relocation and marking (16 November 2026)

`memory_debug.c` now owns persistent allocation heads, doubly linked records,
source/line/total/thread/stack metadata, marks, magic values and footer policy.
Native headers retain their aligned size/profile; fields live directly in C
storage. C89 controls allocation publication, relocation, unlinking, header/footer
and claimed-size validation, expensive/suspended modes, marking and mark/report
scans. Native callbacks supply heap capacity, pointer checks, recursive locking,
thread IDs, stack capture and diagnostic formatting. The graph owns links, while
the fixed allocator owns storage. Source strings and the suspend counter are
borrowed. Callbacks cannot unwind or destroy/move records during traversal;
relocation/publication requires caller serialization with graph mutation.

Creation preserves the allocator's cleared or poisoned marks. Scans query the
thread before locking, run newest first and filter by allocation owner. Reporting
does not mark records, and callbacks can mark later records before they are
visited. Marking has no thread filter. Footer copies support unaligned addresses;
these private headers never reach stored movie formats.

Resize preparation captures a typed pointer representation while the original
block is live. Relocation compares captured address bytes without reconstructing
or dereferencing an invalidated pointer. Moved records unlink/relink at the head.
Unchanged-address success republishes fresh head/neighbour aliases under the
mutex without changing list position: an in-place realloc also invalidates old
pointer values. The host profile requires byte-token matching of pointer
representations; neither token is an address to dereference.

At revision `266abc2f`, the new headless `memory-graph-runtime` fixture captured
native block sizes/marks and heap capacity, cleared/poisoned creation, allocation
order, marking, repeated lost scans, callback marking, grow/shrink/zero resize,
header/footer/claimed-size diagnostics, suspension, cross-thread ownership and
foreign marking, and freeing all records. `memory-graph-expensive` compiles the
same fixture and allocator facade with expensive validation while linking the
existing shared core. All normal host caches have expensive checks disabled.
SDL Release disables native diagnostics in both profiles; its C suite still
exercises the full diagnostic policy.

Resize traversal order depends on physical allocator movement. The fixture
asserts the actual order against the observed address change, then sorts those
reports for capture comparison. Other reports preserve traversal order. Captures
exclude raw addresses, native thread IDs and implementation source lines; they
compare retained source labels/lines, messages, diagnostic payload widths,
initial marks and logical/physical sizes.

| Configuration | Bytes | FNV-1a | SHA-256 |
| --- | ---: | --- | --- |
| Native/SDL Debug, normal | 3,868 | `7A3B1575` | `A30B75633A6A7F52818FEC8B96DB993BBC2C699C44BC955DFF2A93CC655A107E` |
| Native/SDL Debug, expensive | 4,084 | `CBD39F37` | `8C13154D80AAD5107D1C06BC51ABC7D31DDA02979A5A07E0442DD716FA3D5BAC` |
| SDL Release, both profiles | 12 | `E23C62B5` | `15EC7BF0B50732B49F8228E07D24365338F9E3AB994B00AF08E5A3BFFE55FD8B` |

`engine-memory-debug` integrates diagnostics with C fixed transactions and forces
moved, unchanged-address and failed resizes. Old storage is freed before moved
link repair; unchanged-address tests poison external head/neighbour aliases and
require fresh publication without reordering. Checks cover metadata/mark/stack
retention, preservation on failure, source and ownership, scan/callback/lock order,
expensive/suspended validation, footer/claimed-size errors, C mark wrap, and full
poisoning before release. The suite passes MSVC AddressSanitizer. Defined C wrap
is specified separately from native overflow equivalence.

The wider gate passes 217 Native checks, 212 SDL Release checks, 213 SDL Debug
checks and all 150 standalone checks, including strict C89 syntax: 792 of 793
selected checks pass. The sole declared skip remains the existing SDL Release
`world-export` Debug fixture. All 147 retained memory-graph/object/heap/error/
string/text/cache/template/body/undo/Help/Preview trace pairs match. All ten input
identities are unchanged; identities for 110 binaries are recorded. Windows text
settings remain ACP 1252, OEMCP 437 and locale `en-GB`/2057.

The regular headless asset-preview harness passes. Backgrounds retain 301,996,032
bytes/FNV-1a `E1A71005` across 12 backgrounds and 188 cameras per host. Native
`chelp` and `ched` compile. Marcus was stopped through rebuilding and resource
regeneration, then relaunched. Test processes use private temporary directories.
No Marcus UI tests or new retail capture campaign were added; retained retail
captures remain the playback reference.

This is Windows x64 fixture evidence, with additional C fault-provider checks.
Arbitrary simultaneous graph mutation/relocation, callback-driven record deletion,
invalid pointer lifetimes or corrupt native addresses, native mark/counter
overflow, 32-bit diagnostic stacks and other pointer representations/platforms
remain unverified. Readable C metadata errors and defined C wrap do not establish
native equivalence for invalid lifetimes or overflow. HQ handle policy,
BaseLinkedList attachment, virtual destruction and final C-only host/facade
integration remain conversion work; this does not finish the backend migration.

Evidence: `tmp/c89-memory-graph-{baseline,final}-{runtime,expensive}-{native,sdl-release,sdl-debug}.bin`,
`tmp/c89-memory-graph-{native,sdl-release,sdl-debug}-tests.log`,
`tmp/c89-memory-graph-standalone-tests.log`, `tmp/c89-memory-debug-asan.log`,
`tmp/c89-memory-graph-regular-tests.log`,
`tmp/c89-memory-graph-trace-comparison.json` and
`tmp/c89-memory-graph-identities.json`.

### Shared C HQ records, allocation and lifetime policy (16 November 2026)

`handle_memory.c` now owns HQ allocation, resize, copy, free, size/pointer queries,
lock counts, validation and mark forwarding. Logical size and lock count live in
persistent C state directly inside each allocation; debug records add the magic
word. Native facades supply the original aligned header profile and fixed-memory
providers, preserving the HQ counter category, flags, priorities and caller source
metadata. Default C providers use libc without Kauai C++ dependencies. Release
pointer queries now also call C. No stored movie format changes.

Allocation failure reports the HQ error before clearing the public owner; the
underlying fixed provider's error precedes it. Failed resize retains the handle,
contents, size and locks. Locked resize reports without a raw allocation call.
Free clears the public owner before a lock warning and still releases the block.
Unlock at zero reports and remains zero. Copy owns distinct storage, preserving
source locks. Expensive validation checks magic first, then locks and the fixed
allocation. HQ magic validation remains active while fixed pointer checks are
suspended. Callbacks are synchronous borrowed views, cannot unwind or destroy the
operation, and observe those publication points. Callers supply synchronization.

At revision `3d4d661b`, the headless `handle-runtime` fixture captured original
normal behavior; `handle-expensive` compiles only the HQ/fixed facades with
expensive checks against the existing shared core. The cases cover zero/nonzero
sizes, physical header width and fields, cleared payloads and growth, copies,
nested locks, locked resize, excess unlock, grow/shrink/zero resize, locked free,
nil handling, injected allocate/growth/copy failures, shrink countdown retention,
magic diagnostics with/without suspension, marking, HQ counters, error sequencing
and opaque `MMHandleServices` use. Error callbacks record public-owner state and
error-stack contents before publication. Captures exclude raw addresses and
implementation source lines; error codes/messages, metadata, payloads, counter
fields and diagnostic byte spans are compared. Normal host caches have expensive
checks disabled. SDL Release disables native diagnostics in both fixture profiles
but still exercises C policy through the standalone tests.

| Configuration | Bytes | FNV-1a | SHA-256 |
| --- | ---: | --- | --- |
| Native/SDL Debug, normal | 5,640 | `0FBF4BD2` | `7795DD59E7424AB178F15B40BF413C4109258E9F78A76462E10CB6E27439BC3B` |
| Native/SDL Debug, expensive | 5,928 | `0EB2CEA5` | `C4A8A6FB0B7A78E3CF57CBFCA2D0AB8CB18EE326D08E339621367146C510812B` |
| SDL Release, both profiles | 3,448 | `E37C9FEF` | `CF6DE6ADA0A5DFEDFEA60A1DA7D989F97868523552904B5EF0012CC7B0CE79A7` |

`engine-handle-memory` forces raw allocation/resize failures and moved growth/
shrink, checks byte/flag/priority forwarding, initialization and public-owner
publication, locked operations, independent copies without source-lock changes,
metadata/nil validation, mark forwarding, C overflow/count-wrap behavior and libc
defaults. The suite passes MSVC AddressSanitizer. Checked C prefix arithmetic and
count wrap are specified separately from native undefined-overflow equivalence.

The wider gate passes 220 Native checks, 215 SDL Release checks, 216 SDL Debug
checks and all 151 standalone checks, including strict C89 syntax: 802 of 803
selected checks pass. The sole declared skip is the existing SDL Release
`world-export` Debug fixture. All 153 retained handle/memory-graph/object/heap/
error/string/text/cache/template/body/undo/Help/Preview trace pairs match. All ten
input identities are unchanged; identities for 120 binaries are recorded. Windows
text settings remain ACP 1252, OEMCP 437 and locale `en-GB`/2057.

The regular headless asset-preview harness passes. Backgrounds retain 301,996,032
bytes/FNV-1a `E1A71005` across 12 backgrounds and 188 cameras per host. Native
`chelp` and `ched` compile. Marcus was stopped for rebuilding and resource
regeneration, then relaunched. Test processes use private temporary directories.
No Marcus UI tests or new retail capture campaign were added; retained retail
captures remain the playback reference.

This is Windows x64 fixture evidence. Actual native heap exhaustion, arbitrary
callback/override behavior or simultaneous handle mutation, invalid owner/payload
lifetimes and corrupt native addresses, native signed size/count overflow,
historical Macintosh handles, 32-bit diagnostic stacks and other platforms remain
unverified. The C fault-provider/default tests do not establish equivalence for
native undefined overflow or invalid lifetimes. BaseLinkedList attachment,
virtual destruction and final C-only host/facade integration remain conversion
work; this does not finish the backend migration.

Evidence: `tmp/c89-handle-{baseline,final}-{runtime,expensive}-{native,sdl-release,sdl-debug}.bin`,
`tmp/c89-handle-{native,sdl-release,sdl-debug}-tests.log`,
`tmp/c89-handle-standalone-tests.log`, `tmp/c89-handle-memory-asan.log`,
`tmp/c89-handle-regular-tests.log`, `tmp/c89-handle-trace-comparison.json` and
`tmp/c89-handle-identities.json`.

### Shared C intrusive-list storage and attachment (17 November 2026)

`linked_list.c` now owns persistent next/previous links, initialization,
unlink-before-insert attachment, detach and link validation. `BaseLinkedList`
embeds the C node with a borrowed typed-owner token; its facade retains native
BASE validation and diagnostic callbacks. `FileObject` now stores a C node head
and uses typed owner accessors for lookup, marking, closing and shutdown. This
removes casts between derived/base owner-pointer slots. The extra owner field
changes private runtime object sizes; stored movie formats are unchanged.

Before conversion at revision `e182554b`, `linked-list-runtime` captured original
stack and reference-counted destruction, head/middle/tail moves between lists,
repeated head attachment and repeated detach. The original fixture used native
owner-pointer heads; the converted fixture changes only head storage/access and
adds independent checks of the real temporary-file registry. It verifies middle,
tail and head release and restoration of the original registry head.
`linked-list-expensive` compiles the BASE/list facade with expensive checks against
the shared core. Both profiles in Native, SDL Debug and SDL Release match the
original normal native capture: 424 bytes, FNV-1a `287C6F91`, SHA-256
`A092AE92A940A81A8B879C5D63DFBB9D795A9177C8C12464145E1943346BA117`.
Captures contain logical owner IDs, order and destructor observations, without
raw addresses, implementation lines or physical object sizes.

`engine-linked-list` links only C and exercises C head/internal slots, cross-list
moves, predecessor/successor repair, repeated detach, owner identity, ordered
address/check callbacks, readable link corruption and repeated mixed moves.
It passes MSVC AddressSanitizer. All 152 standalone checks pass, including strict
C89 syntax. Native, SDL Debug and SDL Release builds succeed. Marcus was stopped
for rebuilding and resource regeneration, then relaunched.

The wider gate passes 223 Native checks, 218 SDL Release checks, 219 SDL Debug
checks and all 152 standalone checks: 812 of 813 selected checks pass. The sole
declared skip is the existing SDL Release `world-export` Debug fixture. All 153
retained handle/memory/object/error/string/text/cache/template/body/undo/Help/
Preview trace pairs and all six list trace pairs match. All ten input identities
are unchanged; identities for 130 binaries are recorded. Windows text settings
remain ACP 1252, OEMCP 437 and locale `en-GB`/2057.

The regular headless asset-preview harness passes. Backgrounds retain 301,996,032
bytes/FNV-1a `E1A71005` across 12 backgrounds and 188 cameras per host. Native
`chelp` and `ched` compile. Test processes use private temporary directories.

Nodes, owners and head slots must remain live and at fixed addresses until
detached. Native registry locking remains with the caller. Diagnostic callbacks
are synchronous and cannot mutate links or unwind. Invalid addresses, attaching
inside one's own node, arbitrary callback mutation, concurrent unsynchronized
mutation and other platforms remain unverified. The normal native capture does
not establish equivalence for corrupt links; C fault checks specify the readable
corruption behavior independently. No Marcus UI tests or new retail capture
campaign were added; retained retail captures remain the playback reference.
Virtual destruction and final C-only host/facade integration remain conversion
work; this does not complete the backend migration.

Evidence: `tmp/c89-linked-list-before-native.bin`,
`tmp/c89-linked-list-after-{expensive-,}{native,sdl-release,sdl-debug}.bin`,
`tmp/c89-linked-list-{native,sdl-release,sdl-debug}-tests.log`,
`tmp/c89-linked-list-standalone-tests.log`, `tmp/c89-linked-list-asan.log`,
`tmp/c89-linked-list-regular-tests.log`,
`tmp/c89-linked-list-trace-comparison.json` and
`tmp/c89-linked-list-identities.json`.

### Shared C file-owner state, permission recovery and I/O (17 November 2026)

`file_owner.c` now owns persistent physical-handle, open/ever-open/written,
flags and error-severity state. C controls permission upgrade/restoration,
close/final temporary deletion, flush, seek, size, resize, read/write and flag/temp
mutation. `FileObject` embeds the C record; both hosts use the same controller.
Native callbacks supply the current Filename's physical services, recursive
locking, validation, diagnostics and error reporting. Private runtime layout
changes replace bitfields with C fields; stored formats are unchanged.

At revision `c45257eb`, `file-owner-runtime` captured the native fields and public
behavior before conversion in all three hosts. It covers real OS sharing
rejection and restored-handle reads, successful permission upgrade, open failure
and the critical latch, flags/marks/temp state, close/reopen, read/write/resize,
error thresholds, zero-byte I/O, EOF diagnostics and temporary cleanup. An
ErrorStack override records state during error publication. The converted
fixture changes only its protected-field access to the C record.
`file-owner-expensive` compiles the file facades with expensive validation against
the common core. Normal and expensive captures match each host's original normal
capture. Debug adds the original EOF diagnostic; Release omits it.

| Configuration | Bytes | FNV-1a | SHA-256 |
| --- | ---: | --- | --- |
| Native/SDL Debug, both profiles | 756 | `CEE01DFA` | `F4A7FB2DE3F9F824362A7CB233528E74140E2E22846FDA673956687E3AE5A632` |
| SDL Release, both profiles | 608 | `54BBB0B8` | `895BA564E96A471F20292AF68C9AE7BF302E7827DA4BC83C6CD199657E2203DA` |

`engine-file-owner` links only C and forces seek/read/write/resize/size failures,
permission restoration and double failure, deletion and ignored flush failure.
It checks callback publication before error escalation, written state before
physical writes, old/new permission forwarding, recursive lock and validation
order, debug-size failure, advisory write/EOF diagnostics, zero-byte I/O at
critical error, repeated final close and optional diagnostic/external-lock hooks.
It passes MSVC AddressSanitizer. The controller preserves sticky marks: even
`FSetGrffil(0, ffilMark)` leaves the mark set. This observed native behavior is
frozen rather than corrected by the conversion.

The wider gate passes 226 Native checks, 221 SDL Release checks, 222 SDL Debug
checks and all 153 standalone checks, including strict C89 syntax: 822 of 823
selected checks pass. The sole declared skip is the existing SDL Release
`world-export` Debug fixture. All 159 retained list/handle/memory/object/error/
string/text/cache/template/body/undo/Help/Preview trace pairs and all six file-owner
trace pairs match. All ten input identities are unchanged; identities for 140
binaries are recorded. Windows text settings remain ACP 1252, OEMCP 437 and locale
`en-GB`/2057.

The regular headless asset-preview harness passes. Backgrounds retain 301,996,032
bytes/FNV-1a `E1A71005` across 12 backgrounds and 188 cameras per host. Native
`chelp` and `ched` compile. Marcus was stopped for rebuilding and resource
regeneration, then relaunched. Test processes use private temporary directories.
Concurrent builds inflated some Debug test durations; a separate SDL Debug model
resource rerun passes in 1.73 seconds with the same 277,360-byte/`78C575E5` trace.
These runs establish correctness preservation, not playback timing equivalence.

The record owns its handle, not its native path or provider context; those stay
live throughout calls. Live owners must not be copied. Callers serialize mutation
with recursive locks, including nested close/flush, debug read/size and reopen.
Callbacks cannot unwind, destroy the owner or mutate state. The captures exclude
raw paths/addresses, implementation lines and physical object sizes. Defined
debug subtraction wrap is specified separately from native signed-overflow
equivalence. Actual native disk exhaustion, arbitrary callback overrides,
concurrent mutation, invalid lifetimes/arguments, historical Macintosh file
adapters and other platforms remain unverified. C fault checks do not establish
native fault equivalence beyond the captured sharing/EOF/open failures.

File/name factory and registry coordination, rename/swap/replacement control,
Filename representation, physical services, virtual destruction and final C-only
host/facade integration remain conversion work. No Marcus UI tests or new retail
capture campaign were added; retained retail captures remain the playback
reference. This does not complete the backend migration.

Evidence: `tmp/c89-file-owner-before-{native,sdl-release,sdl-debug}.bin`,
`tmp/c89-file-owner-after-{expensive-,}{native,sdl-release,sdl-debug}.bin`,
`tmp/c89-file-owner-{native,sdl-release,sdl-debug}-tests.log`,
`tmp/c89-file-owner-standalone-tests.log`, `tmp/c89-file-owner-asan.log`,
`tmp/c89-file-owner-regular-tests.log`,
`tmp/c89-file-owner-trace-comparison.json` and
`tmp/c89-file-owner-identities.json`.

### Shared C file factories, registries and name transactions (17 November 2026)

`file_ops.c` now controls cached open/reuse, fresh and temporary creation,
registry lookup/membership and mark/zero-reference/shutdown scans. It calls the
preceding C file-owner controller for permission recovery, opening and closing.
Explicit contexts bind registry and primitive providers. The temporary native
facade still supplies the process registry, typed allocation/constructors/
destructors, Filename storage and name/path primitives. Both hosts share C policy;
this is not a claim that FileObject's complete dependency closure is C-only.

C also controls rename, name swap/recovery and replacement. It retains native
validation, recursive lock and publication order: enter a then b, leave a then b,
then report the transaction error and validate owners. Unique-name failure does
not close handles. Physical moves leave logical names untouched until explicit
publication. Failed recovery publishes surviving physical locations before
reopening. Swap ignores reopen failures, while rename reports them. Replacement
marks the displaced owner temporary only after swap succeeds. Nonmissing/unknown
destination status triggers deletion whose result is ignored before rename.
Sticky marking remains unchanged.

At revision `e8762218`, the new `file-ops-runtime` fixture captured existing
behavior in Native, SDL Debug and SDL Release. It covers factory reuse/reference
counts, duplicate creation, missing-file open, real rename rejection, self-swap,
content-preserving swaps and replacement, closed-file replacement, temp factories,
registry order, shutdown/reopen and Debug allocation injection. Error callbacks
record logical registry entries at publication. `file-ops-expensive` overrides
the file facades with expensive validation against the shared core. Both profiles
match their host's original normal capture. Release omits Debug diagnostics and
allocation-injection cases. Captures use logical names/contents and counters,
excluding raw paths/addresses, implementation lines and private physical sizes.

| Configuration | Bytes | FNV-1a | SHA-256 |
| --- | ---: | --- | --- |
| Native/SDL Debug, both profiles | 1,448 | `860BB78D` | `EA73479DC9B5608E7CFE9ECBE47A4F0C927011D856E5D1A5DE1F4C43525CF1FA` |
| SDL Release, both profiles | 736 | `6361A03D` | `607CD5AEFB52D01739140998F6D33C4516979D465CA5018179027A211363EDCF` |

`engine-file-ops` links only C, with independent registries, C file/reference/link
records and a physical name/content model. It tests all 32 failure combinations
across three swap moves and two recovery legs, surviving-content/name invariants,
cached permission failures without extra open errors, construct/open failures,
temporary naming and exclusive-create failures, rename/reopen failures, replacement
and ignored deletion failure, directory diagnostics, lock/validation order and
sticky marks. Destroy poisons owner state; cleanup must save next and avoid
accessing destroyed owners. Swap success despite failed reopens is explicitly
frozen. The suite passes MSVC AddressSanitizer.

The wider gate passes 229 Native checks, 224 SDL Release checks, 225 SDL Debug
checks and all 154 standalone checks, including strict C89 syntax: 832 of 833
selected checks pass. The sole declared skip is the existing SDL Release
`world-export` Debug fixture. All 165 retained file-owner/list/handle/memory/
object/error/string/text/cache/template/body/undo/Help/Preview trace pairs and
all six file-operations pairs match. All ten input identities are unchanged;
identities for 150 binaries are recorded. Windows text settings remain ACP 1252,
OEMCP 437 and locale `en-GB`/2057.

The regular headless asset-preview harness passes. Backgrounds retain 301,996,032
bytes/FNV-1a `E1A71005` across 12 backgrounds and 188 cameras per host. Native
`chelp` and `ched` compile. Marcus was stopped for rebuilding and resource
regeneration, then relaunched. Tests use private temporary directories. These
results establish correctness preservation, not playback timing equivalence.

Callbacks are synchronous and cannot unwind, mutate unrelated owners/links or
delete owners outside designated destruction. Tokens, scratch names and provider
contexts remain live; scratch must not alias owner/input names. Locks are
recursive and callers serialize competing transactions. Callback effects are
limited to designated allocation/copy/physical operations. The C fault model
does not prove arbitrary native disk, rename or rollback failure equivalence.
Actual native exhaustion, cross-volume operations, unsynchronized mutation,
invalid lifetimes, arbitrary overrides and other platforms remain unverified.
Native object construction/destruction, default registry binding, Filename/path
representation and services, virtual dispatch and final C-only host/facade
integration remain conversion work. No Marcus UI tests or new retail captures
were added; retained retail captures remain the playback reference. This does
not complete the backend migration.

Evidence: `tmp/c89-file-ops-before-{native,sdl-release,sdl-debug}.bin`,
`tmp/c89-file-ops-after-{expensive-,}{native,sdl-release,sdl-debug}.bin`,
`tmp/c89-file-ops-{native,sdl-release,sdl-debug}-tests.log`,
`tmp/c89-file-ops-standalone-tests.log`, `tmp/c89-file-ops-asan.log`,
`tmp/c89-file-ops-regular-tests.log`, `tmp/c89-file-ops-trace-comparison.json` and
`tmp/c89-file-ops-identities.json`.

### Shared C Filename value, type and leaf rules (17 November 2026)

`filename_value.c` now implements clear, extension recognition, type derivation/
cleaning/change and leaf change/set/extraction. `Filename` embeds a persistent C
type record of the same width as FileType and supplies borrowed views of its
existing fixed counted String storage. Native callbacks provide code-page case
mapping, separators, copy diagnostics, validation and general-error reporting.
FileNameEnumerator filters use the same C cleaner. No stored formats change.

Recognition scans up to five units including the dot. The two-byte profile
retains the native signed-byte cast rule, accepting ASCII and `FF80..FFFF`.
Cleaning proceeds from the low byte, ignoring higher bytes after a zero.
Type-change overflow preserves both fields, retaining the original `>=255`
check and early equal-type success. Inner leaf-change overflow publishes the
parent directory; public set-leaf clears that state before its error callback.
Extraction preserves the original zero-terminated read. C word arithmetic defines
high-bit type operations independently of native signed-shift overflow.

The `filename-value-runtime` fixture initially captured the pre-conversion code
at `d3559505`. Its final path uses a fixed leaf under a native root, so capacity
results do not depend on the temp-directory prefix. Original normal and expensive
profiles were then rebuilt from that revision's `filename.cpp`, adapting only
member access from `_ftg` to the same-width `_nameState.type`. The adapted source
SHA-256 is `AD141FC77FCF593D9E1EDD73489B799AB68075690F0B7B044E4ECF08AB55B289`.
Temporary original build targets were removed; no second implementation is
committed. Original sources and executable identities are retained locally.

The expensive original profile diagnoses legacy noncanonical values such as
trailing-dot names and a directory path carrying a file type after extension
removal. These messages and stage order are captured as data and matched to the
same original profile, including the capacity cases. Normal and expensive
validation stay enabled in their respective profiles. The fixture records type,
directory status, relative path/leaf, return values, diagnostics and error stack.
Raw paths/addresses, source lines and object layout are excluded.

| Configuration | Bytes | FNV-1a | SHA-256 |
| --- | ---: | --- | --- |
| Native/SDL Debug, normal | 11,020 | `C571AB92` | `3D5611CC7E47B7E84BBAEDCC48EA5FA8D9F8A1F1548D9BC12097C55C3A36F1F1` |
| Native/SDL Debug, expensive | 13,612 | `C4BAFE42` | `43D080A6E93A03AE9396308A87C7EF25310DEE6EACE265D4FE29F07100516975` |
| SDL Release, both profiles | 10,976 | `E77636C2` | `7997EB0AF2779AE422B89C9AA6AB2142DB543DCF041354A163909C93CD3583A0` |

`engine-filename-value` links only C and covers both unit profiles, every byte
and wide extension unit, low-byte zero termination, high-bit type words, copy/
validation hooks, zero-terminated leaf extraction, capacity and partial-state
publication before failure reporting. It passes MSVC AddressSanitizer.

The final gate passes 232 Native checks, 227 SDL Release checks, 228 SDL Debug
checks and all 155 standalone checks, including strict C89 syntax: 842 of 843
selected checks pass. The sole declared skip is the existing SDL Release
`world-export` Debug fixture. All 171 retained file/value/memory/string/resource/
undo/Help/Preview pairs and six Filename pairs match. All ten input identities
are unchanged; identities for 166 binaries, including the six original-profile
executables, are recorded. Windows text settings remain ACP 1252, OEMCP 437 and
locale `en-GB`/2057.

SDL Debug resource regeneration initially met a sharing violation while the
native regression reader held `utest.chk`. An exclusive-open probe confirmed
Win32 sharing error 32. After the reader exited and Marcus was stopped, the build
succeeded; the final host gates use rebuilt binaries. Marcus was relaunched.
Native `chelp` and `ched` compile. The regular headless asset-preview harness
passes; all hosts retain 301,996,032 bytes/FNV-1a `E1A71005` across 12 backgrounds
and 188 cameras. Tests use private temp directories. This is correctness evidence,
not playback timing equivalence.

The C buffer/type/callback contracts require live storage, matching units,
nonaliasing input/output ranges and serialized mutation. Callbacks cannot unwind
or mutate the operation. Native Unicode runtime behavior, other platforms,
arbitrary invalid storage/lifetimes and native undefined overflow remain
unverified; dual-unit C tests do not establish those native profiles. Path
construction/search, unique/temp naming and context, filesystem/navigation/
case-resolution policy, enumeration, native object lifetime and final C-only
host/facade integration remain conversion work. No Marcus UI tests or new retail
captures were added; retained retail captures remain the playback reference.
This does not complete the backend migration.

Evidence: `tmp/c89-filename-value-{original,converted}-{runtime,expensive}-{native,sdl-release,sdl-debug}.bin`,
`tmp/filename-value-original.cpp`, `tmp/c89-filename-value-{native,sdl-release,sdl-debug}-tests.log`,
`tmp/c89-filename-value-standalone-tests.log`, `tmp/c89-filename-value-asan.log`,
`tmp/c89-filename-value-regular-tests.log`, `tmp/c89-filename-value-trace-comparison.json`
and `tmp/c89-filename-value-identities.json`.

### Shared C Filename path and navigation rules (17 November 2026)

`mm/filename_path.h` now controls path construction/search, existence/status
precedence, delete/rename dispatch, equality/same-directory, volume forwarding
and directory navigation/create/move policy. `Filename` delegates through native
canonicalization/search, filesystem, equality, copy and diagnostic providers,
sharing the preceding C filename value and counted String modules. Native class
layout is unchanged. C uses caller-owned views/scratch storage and stack buffers,
with no heap allocation or hidden globals.

The controller preserves the original input's trailing-dot suppression of default
extensions and dot-only directory tails. Failed build/search clears path/type
before reporting general failure. Existence strips one trailing directory
separator except for a one-unit root, including the existing Windows drive-root
behavior; type mismatch precedes hidden/system errors. Delete retains its advisory
open-file assertion and skips registry lookup without a diagnostic provider.
Rename changes no logical name, and equality retains native argument order.

Downward navigation checks a scratch path and tries creation on any non-yes
status when requested, including after an existing error; it publishes a move
only after success. Upward navigation canonicalizes an appended `..` and requires
a shorter path. Failed upward navigation preserves both the owner and optional
leaf output without adding an error. Native scratch copies retain owner metadata
as well as path/type. These rules preserve legacy policy, including its error and
publication ordering.

The native fixture captured both normal and expensive profiles directly before
conversion at `d8aea7fe`, on Native, SDL Release and SDL Debug. It creates a real
private directory and records paths relative to that root, return values, type,
directory flags, state observed during error reporting, diagnostics and error
order. Source locations, raw paths/addresses and object layout are excluded.
Normal and expensive diagnostics remain enabled in their respective profiles.
The original `filename.cpp` SHA-256 is
`AF30AA5CDFC9122BBE37A60A8CCC9B64214873C62C4D3A5FFE70ACC033F44888`.

| Configuration | Bytes | FNV-1a | SHA-256 |
| --- | ---: | --- | --- |
| Native/SDL Debug, both profiles | 1,556 | `10A3B84E` | `3717CF478FD0D13A0D91EBC5386D019C2C18D1525655430FE247EBAB47080300` |
| SDL Release, both profiles | 1,380 | `6EE50F92` | `03FD2B2EB748B57B5C416275F8ED40ABC9CF7BADF30919A8AB0009970CFB430E` |

`engine-filename-path` links only C and exercises both unit profiles, original
trailing-dot input, dot-only tails, clear-before-error failure publication,
status/error precedence, separator/root stripping, provider argument order,
advisory open checks, copy hooks, directory creation and optional movement,
upward root/provider/capacity failure, and raw non-ASCII leaf copying. Optional
diagnostic/copy hooks are also covered. MSVC AddressSanitizer passes.

The final gate passes all 235 Native checks, 230 SDL Release checks, all 231 SDL
Debug checks and all 156 standalone checks, including strict C89 syntax: 852 of
853 selected checks pass. The sole declared skip is the existing SDL Release
`world-export` Debug fixture. Native renderer, playback, player and C89 checks
pass; no Marcus UI tests were added.

All 177 retained file/value/memory/string/resource/undo/Help/Preview trace pairs
and six new path pairs match: 183 byte-identical pairs. The ten input identities
are unchanged; 176 executable identities are recorded. Windows text settings
remain ACP 1252, OEMCP 437 and locale `en-GB`/2057. The regular headless preview
harness passes. All three hosts retain 301,996,032 bytes/FNV-1a `E1A71005` across
12 backgrounds and 188 cameras. This is correctness evidence, not playback
timing equivalence.

The first broad build exhausted C: while growing an incremental-link cache.
Reproducible `.ilk` files from the four checked Debug build directories were
preserved under `D:/CodexBuildCacheBackups/3DMMForever-filename-path`, freeing
5,809,701,944 bytes. The local Debug CMake caches use `/debug /INCREMENTAL:NO`;
no repository build policy changed. All host builds then passed, including
native `chelp`, `ched` and Marcus. Marcus was stopped before rebuilding and
relaunched afterwards. Asset readers started after resource regeneration.

The C contracts require live initialized views, matching character units,
distinct inputs/outputs/scratch records and serialized mutation. Callbacks
cannot unwind, destroy owners or mutate unrelated records. Native Unicode
runtime behavior, other platforms and invalid storage/lifetimes remain
unverified. Temp/unique naming and context, case resolution, enumeration,
native object lifetime and final C-only host/facade integration remain conversion
work. No Marcus UI tests or new retail captures were added; retained retail
captures remain the playback reference. This does not complete the migration.

Evidence: `tmp/c89-filename-path-{original,converted}-{runtime,expensive}-{native,sdl-release,sdl-debug}.bin`,
`tmp/filename-path-original.cpp`, `tmp/c89-filename-path-{native,sdl-release,sdl-debug}-tests.log`,
`tmp/c89-filename-path-standalone-tests.log`, `tmp/c89-filename-path-asan.log`,
`tmp/c89-filename-path-regular-tests.log`, `tmp/c89-filename-path-trace-comparison.json`
and `tmp/c89-filename-path-identities.json`.

### Shared C Filename unique and temporary naming (17 November 2026)

`mm/filename_naming.h` now owns the shared unique-name sequence and controls
candidate formatting, retries, failure publication and temporary-directory
caching. `Filename` delegates through the existing native path/clock/temp-path/
copy/diagnostic providers. It shares C filename value/path and String formatting
operations. Native class layout and the public `vftgTemp` configuration binding
are unchanged; the existing `_fniTemp` owner supplies cache storage and metadata.
Default cache ownership/configuration binding and native object lifetime remain
integration work.

Each unique call reads the clock once, advances the sequence modulo 65,536 and
formats `Temp%04x` with the low 16 bits. It skips an exact old leaf before extension
replacement, tries at most twenty candidates and accepts only a missing result.
Failed setters and existing/maybe results continue; the final failed iteration
advances the sequence once more before clearing/reporting general failure.
Intermediate setter/status errors remain. An overlong parent can fail and clear
the value, then recover to a relative candidate on retry. The conversion retains
that legacy behavior and its normal/expensive diagnostic ordering.

Temporary naming initializes its supplied cache only after native provider
success. Provider failure leaves caller/cache unchanged and does not read the
clock. Later calls copy the cached directory and use the current temporary type;
later environment changes do not invalidate it. Unique failure clears the caller
but retains the cache. Copy providers preserve native owner metadata, with a C
type/path fallback. Naming does not create or reserve files. C has no hidden
globals or heap allocation; callers serialize shared state/cache access and
supply initialized, live, distinct views with matching character widths.

The native normal and expensive fixtures captured the original code directly
at `9e42309c`, before conversion, on all three hosts. The existing
`KauaiPlatform::SetTickSource` hook provides deterministic clock values without
modifying the original implementation. Cases include signed/high-bit clock
values, an exact old leaf, twenty physical collisions and subsequent recovery,
capacity failure/recovery, temp-provider failure, changed temporary type and a
cached directory after environment changes. Paths are recorded relative to a
private temp root; raw paths/addresses, source lines and layout are excluded.
Return values, type/directory state, clock calls, state at errors, error order
and normal/expensive messages are captured. Original profiles are compared with
their corresponding converted profiles.
The original `filename.cpp` is 29,638 bytes/SHA-256
`EC53D212236AEEB9BD3E876AA5D0FE77F33B940666BBD7156B9011A5D9D26B4B`.

| Configuration | Bytes | FNV-1a | SHA-256 |
| --- | ---: | --- | --- |
| Native/SDL Debug, normal | 1,296 | `A95D1075` | `5F8C1AA9439C207316F176AB3578E3A26FC8488FC52B74D53379A52C1B032E06` |
| Native/SDL Debug, expensive | 2,448 | `D8B4BC4F` | `D57747464967F3936309C476B58A693E21DB1CC4EF564DB5D427A8BCA1563D4D` |
| SDL Release, both profiles | 1,100 | `A34648AB` | `F3EEA2BE29C7A7C2AE4B022BCC8750FA4976A8319453F711A52F9D629E023C8E` |

`engine-filename-naming` links only C. It covers every one of 65,536 sequence
starts in both unit widths, clock/sequence wrapping, hexadecimal digits, exact
old-leaf comparison before extensions, existing/error/mismatch/hidden retries,
twenty-attempt exhaustion, final increment, clear-before-error publication,
setter failure/recovery, temp failure/cache/type behavior, copying a terminated
temp path independently of the provider's nonzero return value, and optional
copy/diagnostic hooks. MSVC AddressSanitizer passes. All 157 standalone checks,
including strict C89 syntax, pass.

A local native arithmetic probe retains the original short expressions and
checks all 65,536 starts, four clock values and twenty retry updates against
the C modulo-65,536 rule. Both MSVC `/Od /RTC1` and `/O2` profiles pass. This
verifies the native narrowing/wrapping assumption independently of the C test.
The probe and logs remain local evidence.

The final gate passes all 238 Native checks, 233 SDL Release checks, all 234 SDL
Debug checks and all 157 standalone checks: 862 of 863 selected checks pass.
The sole declared skip is the existing SDL Release `world-export` Debug fixture.
Native renderer, playback, player and C89 checks pass. All 183 retained trace
pairs and six new naming pairs match: 189 byte-identical pairs. All ten input
identities are unchanged; 186 executable identities are recorded. Windows text
settings remain ACP 1252, OEMCP 437 and locale `en-GB`/2057. The regular headless
preview harness passes, and all three hosts retain 301,996,032 bytes/FNV-1a
`E1A71005` across 12 backgrounds and 188 cameras.

New evidence uses the workspace junction `tmp/c89-filename-naming`, backed by
`D:/CodexBuildEvidence/3DMMForever-filename-naming`, to avoid exhausting C:.
After the Native build completed, 130 top-level test/tool linker PDB files were
preserved at `D:/CodexBuildCacheBackups/3DMMForever-filename-naming-native-symbols`,
freeing 2,624,356,352 bytes. Executables and Marcus's symbols were retained in
their build directories. Once C: space recovered, the saved symbols were restored
to the Native build, retaining their D: backups. The existing local
non-incremental Debug link settings remain in effect; repository build policy
is unchanged. All host builds pass, including native `chelp`, `ched` and Marcus.
Marcus was stopped before rebuilding
and relaunched afterwards; asset readers began after resource regeneration.
Native/SDL Release fixtures used D: scratch storage and completed slowly while
their I/O counters continued advancing. SDL Debug scratch storage stayed on C:.

Native Unicode execution, other platforms, concurrent shared-state mutation,
invalid storage/lifetimes and callbacks that mutate the operation remain
unverified. Case resolution, enumeration, native object lifetime and final
C-only host/facade integration remain conversion work. No Marcus UI tests or
new retail captures were added. Retained retail captures remain the playback
reference; these correctness checks do not prove playback timing equivalence
or complete the backend migration.

Evidence: `tmp/c89-filename-naming/{original,converted}-{runtime,expensive}-{native,sdl-release,sdl-debug}.bin`,
`tmp/c89-filename-naming/filename-original.cpp`, `tmp/c89-filename-naming/run-{native,sdl-release,sdl-debug}-tests.log`,
`tmp/c89-filename-naming/run-standalone-tests.log`, `tmp/c89-filename-naming/asan.log`,
`tmp/c89-filename-naming/short-probe-{debug,release}.log`,
`tmp/c89-filename-naming/run-regular-tests.log`, `tmp/c89-filename-naming/trace-comparison.json`
and `tmp/c89-filename-naming/identities.json`.

### Shared C Filename legacy case resolution (17 November 2026)

`mm/filename_case.h` now controls explicit legacy content case lookup through
native info/iterator/sensitivity and String diagnostic providers. Native found
returns success without rewriting the path/type or classifying directory,
hidden or system flags. Errors fail quietly. Missing on an insensitive host
also fails immediately. Sensitive hosts require a leading `/`, prefer exact
components, then accept one unambiguous ASCII case match. Non-ASCII units and
backslashes stay literal. Path/type publish only after the entire lookup
succeeds. Every enumeration attempt closes, including end/error/null handles;
a second match stops iteration and fails. Failure leaves the value unchanged
and adds no error-stack entry.

The C controller uses borrowed views, stack buffers and no globals or heap
allocation. Iterator providers return full terminated names in 1024-unit buffers.
Long names cannot be truncated into false matches. The optional terminated-name
length service retains native `CchSz` diagnostics; a null service uses C scanning.
The original warns about an oversized directory name before rejecting its length
as a mismatch. Normal and expensive native profiles retain that diagnostic.
Native class layout is unchanged. Case-sensitive policy now shares the C value,
String and type-derivation modules used by the rest of Filename.

Pre-conversion capture uses `fc15d13e`. Its `filename.cpp` is 29,405 bytes/SHA-256
`DFF5097820CCE2FACBD0AF51789F85D82DB9B074459EA5E57931CD72A7E6A10C`.
The case-sensitive C++ method body is extracted without changing its matching,
iteration or publication algorithm. The adapter binds `_stnFile` to a real native
String, type derivation to the existing C value service, the outer validation to
a borrowed-view hook, filesystem calls to controlled providers, and the platform
guard to the sensitivity flag. This permits both branches to be compared on
Windows. The 4,222-byte adapted source SHA-256 is
`E22037E1E6F96F38D1286436C0E8F5D6865594EE55A0D35F1E46F2F979CD6163`.
Adapter source and preparation identities are retained locally. Temporary
original capture targets were removed from CMake.

The same fixture also exercises the original Windows Filename facade directly
with physical existing, differently cased, missing and directory-spelled file
paths. Controlled cases cover exact success/error, missing on insensitive hosts,
relative/root paths, multiple components, exact-component preference, ambiguity,
no match, initial/iteration errors, partial resolution failure, trailing directory
separators, literal backslashes, equal/different non-ASCII bytes and oversized
entries followed by a maximum-length match. Traces retain provider arguments/
ordering, validation flags, iterator close/handle presence, return values, path/
type and native messages. Physical temp prefixes, addresses, source lines and
layout are excluded. Normal/expensive results match their original profiles.
The provider/validation adapters exercise the sensitive algorithm on Windows;
they do not establish native POSIX filesystem or Filename-class diagnostics.

| Configuration | Bytes | FNV-1a | SHA-256 |
| --- | ---: | --- | --- |
| Native/SDL Debug, both profiles | 13,152 | `AEFD8552` | `D879ADD177A1627E799447E960E3584FC0A5D4D4720B3C0B6EE483A0C847AEDD` |
| SDL Release, both profiles | 13,056 | `98179B0D` | `A5184752D91A1CF87705DA20CF387E8B81FE63175B67CD531D4CE898093905A5` |

`engine-filename-case` links only C and covers both unit widths, exact/error/
insensitive fast paths, relative/empty/root guards, ASCII matching, ambiguity,
iterator ownership, unchanged failure, 1023-unit entries, maximum-length output,
optional diagnostics/copy/length hooks and equal/different non-ASCII byte/Unicode
units. MSVC AddressSanitizer passes. All 158 standalone checks pass, including
strict C89 syntax.

The final gate passes 241 Native, 236 SDL Release, 237 SDL Debug and 158 standalone
checks: 872 of 873 selected checks pass. The sole declared skip is the existing
SDL Release `world-export` Debug fixture. All 189 retained trace pairs and six
case pairs match: 195 byte-identical pairs. All ten input identities are unchanged;
196 executable identities are recorded. Windows text settings remain ACP 1252,
OEMCP 437 and locale `en-GB`/2057. Native renderer, playback, player and C89 checks
pass. The regular headless preview harness passes; all three hosts retain
301,996,032 bytes/FNV-1a `E1A71005` across 12 backgrounds and 188 cameras.

All hosts, native `chelp`/`ched` and Marcus build. Marcus was stopped before the
rebuild and relaunched afterwards. Asset readers began after resource regeneration.
Private test scratch directories stayed on C:. No Marcus UI tests or new retail
captures were added; retained retail captures remain the playback reference.
These are correctness checks, not playback timing equivalence.

Providers must supply live initialized storage, matching units, terminated names,
serialized owner mutation and valid iterator ownership until close. Callbacks
cannot unwind or mutate the operation. Native POSIX/Unicode execution, invalid
storage/lifetimes and callback mutation remain unverified. Enumeration policy,
default cache/configuration bindings, native object lifetime and final C-only
host/facade integration remain conversion work. This does not complete the
backend migration.

Evidence: `tmp/c89-filename-case/{original,converted}-{runtime,expensive}-{native,sdl-release,sdl-debug}.bin`,
`tmp/c89-filename-case/filename-original.cpp`, `tmp/c89-filename-case/resolver-original.cpp`,
`tmp/prepare_filename_case_original.py`, `tmp/c89-filename-case/run-{native,sdl-release,sdl-debug}-tests.log`,
`tmp/c89-filename-case/run-standalone-tests.log`, `tmp/c89-filename-case/asan.log`,
`tmp/c89-filename-case/run-regular-tests.log`, `tmp/c89-filename-case/trace-comparison.json`
and `tmp/c89-filename-case/identities.json`.

### Shared C Filename enumeration ownership and traversal (17 November 2026)

`mm/filename_enum.h` now owns enumeration's inline/heap filters, initialized/
recurse/depth state and recursion storage. Cursor state is a C record holding
iterator ownership, roots/position and entry flags. The controller implements
init/free/next, backward type cleaning, filtering, descent, root walking, skips,
pre/post events, rollback and error/publication ordering. Its recursion stack
uses the existing C collection storage/array services, five initial frames and
the legacy growth hints. C storage removes the native DynamicArray object.

Filters gate descent too, while roots bypass filters. Hidden/system entries
skip before output copying. Dot directories and filtered entries can still
change the output filename on a false result; output flags stay unchanged.
Post events precede child close/pop. Skip closes/pops without the child's post
event. Links are emitted with both flags without descent. Recursion allocation
failure emits the accepted directory with both flags and reports general
enumeration failure. Failed child wildcard setup restores the parent frame
without closing its iterator. Root-build assertions remain advisory. Flags
publish through byte copying of a native 32-bit unsigned word, preserving
long/int profiles without reading uninitialized caller storage or delaying
publication until after provider callbacks.

Native FES snapshots retain Filename storage/metadata and a native-width entry
buffer, with C cursor fields. Views borrow that fixed current record after raw
push/pop copies. Standalone callers can use plain C filename records. Native
FileNameEnumerator embeds owning C state and marks allocations directly. Its
internal layout changes, requiring all consumers to rebuild; Filename layout
and movie wire formats are unchanged. Native filename/frame representation,
object lifetime and default/final facade integration remain work.

The unmodified algorithms at `e89f0eae` were captured directly before conversion,
in normal and expensive profiles on Native, SDL Release and SDL Debug. Original
`filename.cpp` is 28,932 bytes/SHA-256
`599F5E4465C32B579F1E4B836642CA510DD08D313C97FEA46C0CCDEF344BE3A6`.
The fixture creates a private physical tree with empty/nested directories,
different file types and a hidden file. It records flat/recursive traversal,
file-only/tree/no-match/heap filters, skip behavior, optional flags, active
reinitialization/destruction, uninitialized/after-done calls, empty/missing
directories, skip at base and a root with an excluding filter. Paths are recorded
relative to the fixture root; addresses, layout and source locations are excluded.
Return values, output state on false, flags, messages and error order remain data.
Normal/expensive traces match their corresponding originals.

| Configuration | Bytes | FNV-1a | SHA-256 |
| --- | ---: | --- | --- |
| Native/SDL Debug, both profiles | 3,404 | `9F9CAFEE` | `757CA480794741D2F4A35466DA06F3C20667931AAE871A681393C26D7E6B2B92` |
| SDL Release, both profiles | 2,804 | `9C555151` | `427E3FFB2B49B4DD47D1BC0B742007CFCE854610F1DD61FA1ADCD6EDB3CAA4AD` |

`engine-filename-enum` links only C and covers both unit widths, owning cleanup,
inline/heap filters, hidden/system/dot/filtered output publication, pre/post/skip
ordering, linked-directory suppression, iterator errors, initial/growth/type
allocation failures and retry, flag publication before errors, deep stack
ownership, root filtering/bounds/recursion/failure, advisory build failure,
wildcard init failure, failed wildcard rollback at a 255-unit child and active
reinitialization. MSVC AddressSanitizer passes. C tests distinguish the raw
rollback pop from the closing pop, preventing the parent iterator from being
closed during failed child setup.

State is owning and noncopyable. Callers supply live initialized views, matching
units, distinct frame/output/scratch storage, a byte-snapshot-compatible frame
format and serialized mutation. Providers own iterator resources until close;
callbacks cannot unwind, reenter or mutate the operation. Native allocators
retain filter/collection tags. C storage removes the DynamicArray object
allocation and changes allocation sizes/ordinals. Native object-allocation error
paths are not established by the native traces.
Operation-level allocation failure publication and cleanup are covered by the C
fault-injection tests. Native Unicode/POSIX execution, invalid storage/lifetimes
and callback mutation remain unverified. This does not complete the backend
migration.

The final gate passes 244 Native, 239 SDL Release, 240 SDL Debug and 159 standalone
checks: 882 of 883 selected checks pass. The sole declared skip is the existing
SDL Release `world-export` Debug fixture. Strict C89 syntax, native renderer,
playback, player and C89 checks pass. All 195 retained pairs and six enumeration
pairs match: 201 byte-identical pairs. All ten input identities are unchanged;
206 executable identities are recorded. Windows text settings remain ACP 1252,
OEMCP 437 and locale `en-GB`/2057. The regular headless preview harness passes.
All three hosts retain 301,996,032 bytes/FNV-1a `E1A71005` across 12 backgrounds
and 188 cameras. These are correctness checks, not playback timing equivalence.

All hosts, native `chelp`/`ched` and Marcus build after the internal layout change.
Marcus was stopped before rebuilding and relaunched afterwards. Asset readers
began after resource regeneration, and tests used private scratch directories
on C:. The final C edge cases were refreshed in all host binaries before the
gates. No Marcus UI tests or new retail captures were added; retained retail
captures remain the playback reference.

Evidence: `tmp/c89-filename-enum/{original,converted}-{runtime,expensive}-{native,sdl-release,sdl-debug}.bin`,
`tmp/c89-filename-enum/filename-original.cpp`, `tmp/c89-filename-enum/run-{native,sdl-release,sdl-debug}-tests.log`,
`tmp/c89-filename-enum/run-standalone-tests.log`, `tmp/c89-filename-enum/asan.log`,
`tmp/c89-filename-enum/run-regular-tests.log`, `tmp/c89-filename-enum/trace-comparison.json`
and `tmp/c89-filename-enum/identities.json`.

### C89 Windows physical file and path services (17 November 2026)

`mm/native_files.h` and `native_files_win.c` now implement the Windows file/path
backend in C89. The separate `mm-native-files` library exposes opaque handles,
native path units, plain options/metadata and SDK-free declarations. Separate
ANSI/UTF-16 C-only consumers link its implementation with system libraries.
`platform_files_bridge.cpp` forwards existing Kauai C++ calls, converting options,
enums and boolean metadata. The old Windows C++ implementation is removed.
The independent platform-services test project also uses the C backend.
`platform_files_posix.cpp` remains C++ and requires its own migration.

The backend retains access/share/create modes, default ACLs, exact transfers,
signed 32-bit size/seek/resize limits, unsigned 32-bit I/O counts, read-only/
no-clobber rename/delete and the distinction between a missing file and missing
parent. A failed Windows resize can move the file position before failing.
Info/size failures preserve caller metadata/size. Current/executable/leaf queries
clear nonempty output on failure; full/search/temp wrappers preserve their SDK
publication rules. Path comparison still uppercases equal-length 255-unit blocks.
Directory iteration retains SDK name-write extent and leaves the rest of the
1024-unit caller buffer alone. Roots, volume kinds and process media-error mode
retain the original rules. Recursive directory creation uses C heap storage,
separator conversion, the ownerless shell call and result-directory confirmation.
There is no new reservation, retry, normalization or encoding policy.

The runtime fixture captured the original implementation at `f1c5a03a` directly
before conversion, in ANSI and UTF-16 profiles on Native, SDL Release and SDL
Debug. Original `platform_files_win.cpp` is 10,943 bytes/SHA-256
`5AAB7DA30D0E5021BD5F7224E734E9254AAC9CDBB991A8CC0EDB63A192344041`.
No adapted original implementation is needed. Cases include sharing/denied
truncation, short EOF read and partial buffer state, seek/resize limits, failed
resize cursor state, read-only/missing/no-clobber paths, directory creation,
cwd changes/error codes, capacity/leaf publication, search, directory entries,
root bounds, media suppression and ANSI non-ASCII/UTF-16 Greek filenames.
Return values, selected native errors, metadata, bytes, names, iterator order
and unchanged outputs are recorded. Temp prefixes, addresses, source locations
and layout are excluded. Original and converted traces match per profile.

| Configuration | Bytes | FNV-1a | SHA-256 |
| --- | ---: | --- | --- |
| All hosts, ANSI | 924 | `D5F48A23` | `A35D54C6A7C8C488DD8C19AC50265E5E8C9E87874A3C1B58A3C54EA4CA523E98` |
| All hosts, UTF-16 | 924 | `21B7BBFD` | `B28E28B777A2F633AC92E8ADDF5FFE37BC76CC05C56D2C014655BD50D880331C` |

`engine-native-files-ansi` and `engine-native-files-wide` compile and link only C.
They exercise real OS handles, sharing, exact/partial transfers, bounds, failed
resize publication, metadata, path queries/cwd, long comparison blocks, root
guards and retained directory-name buffer tails. Both pass MSVC AddressSanitizer.
Separate ANSI/UTF-16 strict C89 checks include the implementation, test and real
Windows SDK headers, using pedantic/declaration-after-statement/warning errors.
All 163 standalone checks pass, including those checks and the complete engine
C89 syntax gate.

The final host/engine gate passes 250 Native, 245 SDL Release, 246 SDL Debug and
163 standalone checks: 904 of 905 selected checks pass. The sole declared skip
is the existing SDL Release `world-export` Debug fixture. All five independent
platform-services checks pass, making 909 passes of 910 selected checks overall.
Native renderer, playback, player and C89 gates pass. All 201 retained pairs
and six new native-file pairs match: 207 byte-identical pairs. All ten input
identities are unchanged; 225 executable identities are recorded. Windows text
settings remain ACP 1252, OEMCP 437 and locale `en-GB`/2057. The regular headless
preview harness passes. All three hosts retain 301,996,032 bytes/FNV-1a `E1A71005`
across 12 backgrounds and 188 cameras. These are correctness checks, not playback
timing equivalence.

All hosts, native `chelp`/`ched` and Marcus build. Marcus was stopped before the
rebuild and relaunched afterwards. Asset readers started after resource
regeneration; tests used private C: scratch directories. No Marcus UI tests or
new retail captures were added. Retained retail captures remain the playback
reference.

C clients must use the same ANSI/UTF-16 configuration, valid terminated input
and appropriately sized output storage, and close owned handles once. Process
cwd/error-mode changes require host coordination. Directory-creation allocation
ordinals/OOM, invalid storage/unsupported argument combinations, huge capacity
casts, all filesystem/locale variants and native POSIX execution remain
unverified. POSIX file services, native object/default bindings and final facade
integration remain conversion work. This does not complete the backend migration.

Evidence: `tmp/c89-native-files/{original,converted}-{runtime,wide}-{native,sdl-release,sdl-debug}.bin`,
`tmp/c89-native-files/platform-files-original.cpp`, `tmp/c89-native-files/run-{native,sdl-release,sdl-debug}-tests.log`,
`tmp/c89-native-files/run-standalone-tests.log`, `tmp/c89-native-files/asan-{ansi,wide}.log`,
`tmp/c89-native-files/platform-tests.log`, `tmp/c89-native-files/run-regular-tests.log`,
`tmp/c89-native-files/trace-comparison.json` and `tmp/c89-native-files/identities.json`.

### C89 POSIX physical file and path services (17 November 2026)

`native_files_posix.c` now implements the POSIX side of `mm/native_files.h`.
The old `platform_files_posix.cpp` is removed. Windows and POSIX both use
`platform_files_bridge.cpp`; its only additional work is seeding/copying metadata
on every result so that partial POSIX error outputs survive the translation.
`mm-native-files` remains separate from the platform-neutral C89 engine, builds
as C90 with extensions disabled, and links a C-only API test without Kauai or
the C++ runtime. Linux's executable depends only on `libc.musl-x86_64.so.1`.

C heap buffers replace strings/vectors and the directory object. Conversion
retains lexical normalization without symlink resolution, byte-sensitive
comparison, trailing separators, in-place full-path queries, growing cwd
buffers, empty search-list components, `PATH`, `TMPDIR` and `/tmp`. Handles
retain descriptor-zero support, signed 32-bit size/position limits and exact
retrying I/O. Cooperative locks precede truncation; no-clobber link/unlink
renames, read-only guards, dangling links, directory ownership and native
error/output publication remain. POSIX resize failure retains its cursor.
A failed symlink-target query still publishes cleared metadata with the link
bit set; iteration can publish a name and partial metadata before an error,
and callers still close the surviving handle explicitly.

The baseline is the unmodified POSIX implementation from `717cc23e`, captured
before comparing the C implementation. Its retained CRLF source is 15,118 bytes,
SHA-256 `7348C8807E92D34722DB9146EFC480A99DAC88C7877B066077996E9A1764AA8C`.
The LF Git blob has SHA-256
`1383B777B9D9D1542CF8E270E26B321810D13B2A3CA32142F528ABA3B97A3D12`;
newline normalization confirms the source identities match. A first fixture
recorded the randomized root's leaf name; that value was normalized and both
original profiles were freshly captured before comparison. No converted output
was accepted as the original baseline.

The Linux fixture covers interrupted open/lock/read/write/flush calls through
linker interposition, short transfers and partial EOF buffers, shared-reader
locks, denied truncation, read-only and no-overwrite operations, dot/dangling/
directory/looping symlinks, UTF-8 and literal backslash names, directory error
publication, output capacities, cwd errors, search/temp environments, aliased
and deep lexical paths and a cwd exceeding the initial 256-byte buffer.
Directory records are sorted for comparison; the generated root and its leaf
name are normalized. The C-only test also checks a sparse file larger than
2 GiB when `off_t` can represent it, and a successful open occupying descriptor
zero. The range-checked seek comparison explicitly uses `off_t`, avoiding an
unsigned comparison on 32-bit POSIX targets.

An isolated diskless QEMU guest runs Alpine 3.24.4, Linux 6.18.60-0-virt,
GCC 15.2.0 and Clang 22.1.3. Debug and Release original/C snapshots and the
Clang AddressSanitizer/UndefinedBehaviorSanitizer adapter snapshot are all
13,936 bytes, FNV `28F7A6D5`, SHA-256
`93483AFD2E67388B18C570CC0C67E1A2BDCBF4E2F90FDAFFB6FC245F67E85FD9`.
Both the C-only API and the adapter pass those sanitizers. GCC and Clang pass
strict C89 checks with pedantic, mixed-declaration and warning errors. The
existing file-services fixture also passes against the original implementation
in both profiles. Final guest source hashes match the workspace sources.

Final CTest gates:

| Configuration | Passed | Skipped |
| --- | ---: | ---: |
| Windows Native | 250 | 0 |
| Windows SDL Release | 245 | 1 existing world-export skip |
| Windows SDL Debug | 246 | 0 |
| Windows standalone C89 engine | 163 | 0 |
| Windows standalone platform, ANSI / UTF-16 | 5 / 5 | 0 |
| Linux standalone C89 engine | 161 | 0 |
| Linux standalone platform, Debug / Release | 7 / 7 | 0 |

That is 1,089 passed checks and one existing skip. All 207 retained Windows
trace pairs and the three Linux pairs match: 210 byte-identical comparisons.
All ten input identities and the Windows ACP/OEM/locale identity are unchanged.
Records contain 230 Windows executable identities and 173 Linux gated
executable identities. The normal headless preview harness passes; all three
Windows hosts retain 12 backgrounds, 188 cameras, 301,996,032 output bytes and
FNV `E1A71005`. Native/SDL applications and tools rebuild; Marcus is stopped
before its rebuild and relaunched afterward. No Marcus UI tests or new retail
captures are added.

The 32-bit Linux GNU target and ARM64 macOS branch also compile with strict
C89 flags. macOS links the C-only
test as a Mach-O executable using the existing Zig 0.16.0 cross toolchain.
Zig's syntax-only invocation reported a driver `FileNotFound`; separate strict
object compilation and linking succeed. This is compilation/link evidence,
not macOS runtime evidence. Timing equivalence, all filesystems/locales,
allocation-failure ordinals, signal schedules beyond the injected cases and
invalid caller lifetimes remain unverified. Native object/default-context
bindings and final facade removal still require conversion; the whole engine
migration is not complete.

Evidence: `tmp/c89-posix-files/trace-comparison.json`, `identities.json`,
`platform_files_original.cpp`, `run-{native,sdl-release,sdl-debug}-tests.log`,
`run-standalone-tests.log`, `platform-{ansi,wide}-tests.log`,
`run-regular-tests.log`, `linux-sanitizers-final.log`, `linux-final.log`,
`linux-results/evidence/` (original/C snapshots, platform/engine gates, source
and executable hashes, symbols and ELF dependencies), `linux-evidence.tar.gz`,
`native-final-tests.log` in the guest evidence records the affected C API and
syntax recheck after the width guard; `width-*-tests.log` records the Windows
C API rechecks. Cross evidence: `cross-c89.log`, `mac-c89.log`, `mac-link.log`
and `mac-native-c-test`.

### C89 owned filename storage, copying and validation (17 November 2026)

`mm/filename_owned.h` and `filename_owned.c` now own Filename's type and counted
path in plain `MMFilenameOwned8`/`MMFilenameOwned16` records. Native Filename no
longer owns a C++ String. Initialization, assignment/construction copying, path
import/export, counted-path checks and full filename validation execute in C89.
The facade preserves BASE metadata copying and supplies native filesystem,
memory and assertion services, including the existing assertion-depth guard.
Physical file opens/deletes and native dialog results use the same C record.

Initialization leaves unused storage untouched. Copy publishes type before
diagnostics, checks destination/source/destination for assignment or source/
destination for construction, and transfers only count+2 units. Self-copy keeps
its original overlap diagnostic. Export retains independent caller storage;
relocated C records need freshly bound views. Validation retains nil/name and
allowed-kind checks, full-path/equality early returns, directory separator and
leaf-pointer diagnostics. Invalid wide counts are diagnosed without scanning
outside the buffer; this does not make invalid storage a supported copy input.

The original native representation is `70e207ac`, captured before the facade
conversion. Retained `filename.cpp` is 26,288 bytes, SHA-256
`1D99F859956F0772F1FFD554406B8BEE3053506954A45E384BE3C26109DDFB8D`;
`fni.h` is 5,145 bytes, SHA-256
`50B23A808D88685930393BFF6CE708B6C6D1A60BF6DB9530FDEDFBAB19822E3E`.
New normal/expensive fixtures capture object sizes, copied reference counts,
path/type/leaf values, independent mutation, repeated assignment, self-copy,
near-capacity names, nil publication, diagnostics and errors.

| Profile, normal and expensive | Bytes | FNV | SHA-256 |
| --- | ---: | --- | --- |
| Native and SDL Debug | 4,832 | `833914C4` | `4439B5DA72E75334D926F5449034D21BA8FDB1891B07D9E811DC2DEECE8CDE2F` |
| SDL Release | 4,828 | `EECD005E` | `F97F886A655CFC187DDD66C064FEA2EEAC2FB42C78E30F2410A56FD2F048CCC3` |

All six original/converted pairs match. Captured sizes remain Filename 280,
String 257 and BASE 16 bytes on these hosts. The six new pairs and all 207
retained Windows pairs match: 213 byte-identical comparisons. A capture-script
filename collision initially overwrote three converted ownership snapshots
with physical-file snapshots. The original baselines were intact; all six
ownership snapshots were freshly recaptured under separate `owned-*` names
before recording the final comparisons. No implementation change or baseline
replacement was needed for that recording correction.

`engine-filename-owned` links only C and exercises both unit profiles, untouched
tails, type publication, truncation, self-copy, relocation/rebinding, export,
optional diagnostics and malformed/failed validation. It passes MSVC
AddressSanitizer. Strict Clang C89 checks include the new public header, C source
and test in the complete standalone syntax gate. Zig 0.16.0 also compiles strict
C89 objects for 32-bit Linux GNU, x64 Linux musl and ARM64 macOS; the latter two
link C-only executables. The Linux executable runs successfully in an isolated
QEMU guest using the retained Linux kernel/initramfs and powers off afterward.
Its SHA-256 is `CF6C4AEB839EA7330321A3D02AF2A2ACD35C913EAC3B95223071B2F2D6B02203`.
macOS execution and a full native Unicode Filename facade remain unverified;
the two-byte C API and Windows physical UTF-16 backend checks do execute.

The Native gate passes 253 checks, SDL Release passes 248 with the existing
world-export skip, SDL Debug passes 249, and the standalone C89 engine passes
164: 914 passed CTest checks and one existing skip. The regular preview harness
passes. All three hosts retain 12 backgrounds, 188 cameras, 301,996,032 output
bytes and FNV `E1A71005`. All ten input identities and the Windows ACP/OEM/locale
identity are unchanged; 230 Windows executable identities are recorded. Native
tools, Marcus and both SDL application profiles rebuild. Marcus is stopped
before its rebuild and relaunched afterward. No Marcus UI tests or new retail
captures are added.

These results establish correctness within the recorded coverage, not timing
equivalence or every invalid caller/storage/provider case. Interactive native
dialog selection and all filesystem/locale variants remain outside this check.
Native BASE/object/default-context bindings, the enumerator's outer frame
representation and final facade removal remain work; whole-engine conversion
is not complete.

Evidence: `tmp/c89-filename-owned/owned-{original,converted}-{runtime,expensive}-{native,sdl-release,sdl-debug}.bin`,
`filename-original.cpp`, `fni-original.h`, `run-{native,sdl-release,sdl-debug}-tests.log`,
`run-standalone-tests.log`, `run-regular-tests.log`, `asan.log`, `cross.log`,
`linux-runtime.log`, `trace-comparison.json`, `gates.json` and `identities.json`.

### C89 enumeration frame representation and metadata (17 November 2026)

`mm/filename_frame.h` and `filename_frame.c` now supply the current and suspended
enumeration frames. `MMFilenameFrame8`/`MMFilenameFrame16` contain C-owned filename
storage, copy metadata, cursor and entry buffer. The native FES struct and its
embedded C++ Filename/vtable are removed. The existing C enumeration controller
and collection stack operate on these plain records in both hosts.

Frame initialization sets nil path, count-one/local metadata and zero cursor/
entry while preserving the unused path tail. View rebinding supports relocation.
The C copy helper publishes reference-count/diagnostic-kind payload before
type/path, retaining prefix-only copying and optional metadata fields. These
are value-copy compatibility fields, not retained references or debug-graph
registrations. Native bindings expose actual Filename BASE fields for external
names; C frame values receive C path validation without pretending to be BASE
objects. The outer native enumerator remains a BASE facade. Native objects still
use their existing BASE validation. Wire formats and Filename layout are unchanged;
frame storage is smaller because it contains no vtable. Exact native allocation
failure thresholds/ordinals and invalid metadata/provider cases are unverified.

The baseline is the unmodified representation from `7dab8252`. Retained
`filename.cpp` is 29,209 bytes, SHA-256
`E93151B3D45CF685C19710C4A6F3440144137F3CDDAE283E333A60951734AFE4`;
`fni.h` is 5,367 bytes, SHA-256
`CF0BCF45A126DCB40418CA4FE09F0383E7B110F6890954C6F4A6E936D647C135`.
Normal/expensive native fixtures were captured before conversion. They observe
reference counts while callers mutate output counts, deep recursion exceeding
the initial five stack slots, pre/post and skip behavior, active reinit,
heap filters, root enumeration and destruction with an active iterator.

All six original/converted frame snapshots match across Native, SDL Release
and SDL Debug: 6,428 bytes, FNV `3D37141A`, SHA-256
`836ECBA985C61DA1F88A1B1E89A28A065A1CFC99607C3EEC0E4BA37A9A51C2DF`.
All 213 retained Windows pairs match too: 219 byte-identical comparisons.
All ten input identities and the Windows ACP/OEM/locale identity are unchanged;
236 Windows executable identities are recorded.

`engine-filename-enum` now uses the public C frame profiles for its full existing
ownership/filter/traversal/skip/link/error/allocation-failure suite. Added checks
cover poisoned-storage initialization, relocation/rebinding, optional metadata,
copied counts surviving child/post/pop operations and unchanged output counts
during root building. The complete C library/controller/test build passes
AddressSanitizer. Strict C89 builds pass for the standalone engine and Linux x64,
32-bit Linux GNU and ARM64 macOS frame/test objects. The C-only Linux executable
runs in the isolated QEMU guest and powers off afterward; SHA-256
`1D744A04BEA07035C86C543A07B54AB0D6FC4AD266696DC7CC90264BE505CE7A`.
An initial Zig/CMake link crashed with linker-generated dependency output;
disabling that output in the ignored cross-build configuration reused the
compiled objects and linked successfully. No implementation change was needed.
macOS runtime and the full native Unicode facade remain outside this check;
the two-byte C controller/frame tests execute.

The Native gate passes 255 checks, SDL Release passes 250 with the existing
world-export skip, SDL Debug passes 251, and the standalone C89 engine passes
164: 920 passed CTest checks and one existing skip. The regular headless preview
harness passes. All three hosts retain 12 backgrounds, 188 cameras, 301,996,032
output bytes and FNV `E1A71005`. Native applications/tools and both SDL profiles
rebuild. Marcus is stopped before its rebuild and relaunched afterward.
No Marcus UI tests or new retail captures are added. Native BASE/object/default
bindings and final facade removal remain; whole-engine conversion is incomplete.

Evidence: `tmp/c89-filename-frame/frame-{original,converted}-{runtime,expensive}-{native,sdl-release,sdl-debug}.bin`,
`filename-original.cpp`, `fni-original.h`, `run-{native,sdl-release,sdl-debug}-tests.log`,
`run-standalone-tests.log`, `run-regular-tests.log`, `asan-runtime.log`,
`asan-build.log`, `linux-build-initial.log`, `linux-build-final.log`,
`linux-runtime.log`, `cross.log`, `trace-comparison.json`, `gates.json` and
`identities.json`.

### C89 native account lookup and user-data roots (17 November 2026)

`mm-native-users` now implements account lookup and per-user storage roots in
C89 through `mm/native_users.h`. The Windows and POSIX C++ implementations and
`user_paths.cpp` are removed; `platform_users_bridge.cpp` translates existing
caller values. This platform library is separate from the platform-neutral
engine and links with a C toolchain, including ANSI/UTF-16 Windows profiles.
Caller-owned query records contain status, error and bounded native strings.
Windows retains WNet extended-provider details and shell-folder discovery.
POSIX retains effective-uid lookup, 1 KiB-to-1 MiB `ERANGE` retries and the
255-byte account-name limit. Shared C policy preserves absolute XDG/HOME
selection, macOS application-support paths, trailing-slash normalization and
capacity/failure publication. No directories or movie records are changed.

The baseline is `1285e3947e9fd0b52a4949a5fc1a649785d3b71e`. Original user-service
checks passed in Native, SDL Release and SDL Debug before conversion. Retained
original Windows implementations and the converted functions also run under the
same deterministic providers: six account/provider cases, 384 root-capacity
cases and 98 POSIX path cases match in both ANSI and UTF-16. The Linux guest runs
the original and converted user-service checks plus C-only native and provider
checks. Those cover missing/empty/overlong accounts, 255-byte admission, retry
success/ceiling and allocation/reallocation failure. The guest powers off after
the checks. Strict C89 builds pass for Windows ANSI/UTF-16, Linux x64 musl,
32-bit Linux GNU and ARM64 macOS objects. macOS runtime is unverified.

Native passes 261 selected CTest checks, SDL Release passes 256 with the existing
`world-export` skip, SDL Debug passes 257 and the standalone C89 engine passes
170. The two standalone Windows platform profiles pass five checks each;
AddressSanitizer passes all four new Windows C/provider checks. Total: 958
passed CTest checks and one existing skip. The regular headless preview harness
passes; all three host background traces retain 12 backgrounds, 188 cameras,
301,996,032 bytes and FNV `E1A71005`. All 219 retained trace pairs match byte for
byte. Ten input hashes and Windows ACP/OEM/locale identity remain unchanged;
258 executable identities are recorded. Native tools/Marcus and both SDL Studio
profiles rebuild. Marcus is stopped before rebuilding and relaunched afterward.

No Marcus UI tests or new retail captures are added. These checks establish
conversion parity for this service boundary, not new retail playback coverage.
Exact allocation-failure ordinals are unverified. Native UI/object integration,
the C++ value adapter and other backend facades remain conversion work; the
whole-engine migration is incomplete.

Evidence: `tmp/c89-native-users/{platform_users_win.cpp,platform_users_posix.cpp,user_paths.cpp}`,
`original-compare-{ansi,wide}.log`, `linux-runtime.log`, `linux-identities.json`,
`run-{native,sdl-release,sdl-debug,standalone}-tests.log`,
`platform-{ansi,wide}-tests.log`, `asan-runtime.log`, `run-regular-tests.log`,
`trace-comparison.json`, `gates.json` and `identities.json`.

### Standalone C89 movie root and authored scene slots (17 November 2026)

`engine-native-chunky-test FILE --movie` opens the file through the C native
stream and session, then prints the root ID and versions and each contiguous
authored slot's scene ID, frame bounds and transition. The legacy `3mm dump`
agrees for 439 slots across Street Bums, NUTSDVDINTRO, Ringworld Act One Teaser
Trailer, The Amazingly Pointless Adventures of Gustave!, DeletedScenes and
3dMm DIReCTOrS WaR Part 1. Street Bums starts with chunk 3 at slot 0 and
includes an authored start at frame -18.

The scene counts and frame bounds agree with retained retail seek runs 1813
(`actor-audio`), 1822 (`actor-events`) and 1828 (`scrolling-text`) in
`tmp/c89-movie-data-regression/playback.sqlite`. The inputs match the SHA-256
values in the database. This comparison covers the stored scene ranges;
actor/camera state, rendered frames, timing and audio require the later
session seek and playback work.

All 181 Windows standalone engine checks pass, including the strict C89
syntax check and synthetic cases for both byte orders, packed KCDC/KCD2
records, gaps, wrong child types, missing scene chunks and rejected roots.
The session also compiles under strict C89 for Linux x64 musl. Windows
runtime checks cover the standalone reader. Studio and Marcus continue to
open movies through their existing archive providers.

### Standalone C89 authored camera seeks (17 November 2026)

`engine-native-chunky-test FILE --camera SCENE FRAME SID CONTENT` runs the
camera part of an authored seek from a C-only executable. It follows the scene's
background tag and evaluates its camera cues before reading the selected resource.
The output camera slot matches all 240 authored frame positions in retained
retail seek runs 1813, 1822 and 1828 in `tmp/c89-movie-data-regression/playback.sqlite`
and run 1 (`features`) in `tmp/playback-regression/playback.sqlite`. All four
movies match their captured SHA-256. The C reader uses the checked-in retail
`bkgds.3cn`, SHA-256 `1af4bfd944354db266b8993dc70d4a93d75cc460a124584b2170634873e0809a`.
For background 0, both selected CAM numbers (65538 and 57), clip planes, FOV and
fixed-point matrices also match the links and bytes from native `inspect-chunks`.

The engine test exercises embedded and external backgrounds, both wire byte
orders and codecs, same-frame cuts in stored order, backward queries, invalid
group offsets, missing source, and unchanged output on failure. All 181 Windows
standalone checks pass under the required syntax configuration. The session
compiles as strict C89 for Linux x64 musl. The retained captures verify camera
selection. The matrix check uses resource bytes, and this slice does not
render pixels, evaluate actors or switch either host to the new session.

### Standalone C89 actor seeks (17 November 2026)

`engine-native-chunky-test FILE --actors SCENE FRAME SID CONTENT` follows
the start group's actor links and prints standalone C89 actor state. It
reads stored paths and events and uses the same seek, event-effect and pose
evaluators as Preview. Action step lengths, flags and cel counts come from
the resolved template, with procedural metadata shared for embedded 3D text.

On the four retained retail seek runs listed above, 960 actor observations
over 240 authored frames agree on cast, template identity, stage visibility,
action, cel, event cursor and fixed-point route position. All 710 captured
on-stage root matrices in the three newer runs agree. The older `features`
run supplies the other fields. The same fields agree in its v3dmm seek run 3.
Movie inputs match the recorded SHA-256 values. The external template archive
was `content-files/tmpls.3cn`, SHA-256
`f9a9316e5567504fa57083c35bbf6a594a7c283e89eea51ea4aaf6836412a464`.

All 181 Windows standalone engine checks pass, including the required C89
syntax gate and the actor fixture's little- and big-endian tracks, dynamic
step distance, backward query, missing source and frame/actor bounds. The
actor session source compiles with strict C89 for Linux x64 musl. The capture
comparison covers actor state and root transforms. Part matrices, costumes,
audio and host integration remain open.

### Standalone C89 content resolution (17 November 2026)

`engine-native-chunky-test FILE --seek SCENE FRAME RETAIL_CONTENT_DIRECTORY`
mounts the directory and resolves each authored source ID through the movie's
source-name GST and the shared VFS. A missing GST retains the retail SID 2
mapping. Mounted `.3cn` archives are searched by exact chunk identity and cached
for the duration of the seek. For the retained seek runs listed above, this
combined mode matches camera and actor state across all 240 retail frames and
60 v3dmm frames. The inputs match their capture hashes. Pointing the same mode
at a directory without retail archives reports the missing background asset.

The regression fixture checks byte and UTF-16 source names, non-default SIDs,
duplicate SID precedence, alias resolution, mixed-case archive names, exact
asset selection, cache ownership and missing-asset output. All 181 standalone
engine checks and the 11 engine, VFS and Marcus syntax checks in the full build
configuration pass. The resolver sources also compile under strict C89 for
Linux x64 musl. The C directory probe covers the retail installation files.
Expansion and bundle mounting and Studio/Marcus session integration remain
separate work.

### Standalone C89 body part transforms (17 November 2026)

`engine-native-chunky-test FILE --body SCENE FRAME RETAIL_CONTENT_DIRECTORY`
replays actor state and prints each on-stage body's local part transforms and
authored model bindings. Ordinary actors follow the template's parent and set
lists, selected cel and linked matrix table. Embedded 3D text uses the shared
matrix generator over its resolved font metrics and glyphs. The result records
the template or font that owns the part models.

All 6,280 part matrices across 710 on-stage actor observations match retained
retail seek runs 1813, 1822 and 1828 for 180 frames, including 360 generated
text parts. Ten off-stage observations return no parts. The inputs match their
recorded movie hashes and use the checked-in `tmpls.3cn` and `tdfs.3cn`.
The font archive SHA-256 is
`013664a9132f214f842a2f10554e4b664ff69f53542cc856b9dfc50c98fa1799`.
The older v3dmm capture has no part matrices for this comparison.

All 181 standalone engine checks and 11 full-configuration syntax checks pass.
The engine fixture checks both byte orders, wrapped cels, parent and material
set indices, model linkage, a cel that retains the old model, and unchanged
output on failure. The body source also compiles as strict C89 for Linux x64
musl. Costume application, model geometry, rendering and host integration
remain open.

### Standalone C89 body meshes and world transforms (17 November 2026)

`engine-native-chunky-test FILE --mesh SCENE FRAME RETAIL_CONTENT_DIRECTORY`
loads the distinct authored `BMDL` records for each body's template or font.
It prints each part's world matrix and mesh index, with `-1` for a cel that
retains its previous model. The mesh fingerprint covers the stored geometry,
including positions, mappings, prelit colours, normals and indexed faces.

An independent check used native `inspect-chunks` dumps of `tmpls.3cn` and
`tdfs.3cn` to verify 34 unique meshes and their decoded fields. It also used
the dumped template parent lists, generated text's flat hierarchy and the
recorded root and local matrices from retail seek runs 1813, 1822 and 1828 to
recompute all 6,280 world matrices across 180 frames. These include 360 text
parts and 180 parts marked to retain their previous models. The matrix
comparison shifts each fixed-point product before summing, as in retail
`BrMatrix34Mul`.

The engine fixtures check native payload ordering with both header byte orders,
truncated records, out-of-range face indices, forward parent references,
model reuse, cyclic graphs and preservation of output on failure. All 181
standalone checks, 11 Marcus-enabled C89 checks and the native
`model-resource` and `preview-actor-tree` checks pass. Both affected C
sources also compile as strict C89 for Linux x64 musl. Applying retained
bindings and costume overrides and verifying rendered output remain open.

### Standalone C89 body appearance replay (17 November 2026)

The body query now drives the shared actor effects through a standalone body.
It applies template defaults from `GGCM` or legacy costume children, resolves
custom `CMTL` and generic `MTRL` events, and updates models when the actor
selects a cel. A cel with a retain directive uses the model that survived
earlier action and costume effects. The `--mesh` probe reports both the
directive and the effective model and material bindings with their archive keys.

Fresh captures from the hash-pinned retail executable used the same
`actor-audio`, `actor-events` and `scrolling-text` movies as retained seek runs
1813, 1822 and 1828. All 6,280 visible parts across 180 observed frames matched
the selected model geometry and material properties, including 180 retained
model directives, 1,472 generic material assignments and 360 generated text
materials. A further 24 out-of-order and repeated seeks matched on 840 parts.
The comparison checked triangle positions and UVs from the native archive
dumps against live retail meshes. The triangle fingerprints allow for
BRender vertex welding and reordering. Material checks covered colour,
lighting coefficients, specular power, texture presence, and palette ramp
values for untextured materials.

The engine fixtures cover both byte orders, template defaults with and
without a costume group, costume geometry retained by a later cel, generic
materials, and a missing material that preserves the caller's previous result.
All 181 standalone checks, 11 Marcus-enabled C89 checks, and the native
`material-resource` and `preview-actor-tree` checks pass. The affected
sources compile as strict C89 for Linux x64 musl. The session exposes material
bindings for subsequent rendering work. Host integration and renderer
verification remain open.

Evidence is under `tmp/c89-appearance-oracle/`, including the retail
captures and both comparison logs.

### Shared C89 movie directory in SDL Studio and Marcus (17 November 2026)

SDL Studio now verifies movie roots and resolves authored scene slots through
the C89 movie directory over its current chunk graph. Marcus uses the same
directory through its archive adapter to validate the movie root and load
linked scene headers in child order. Its rootless scene fixture continues to
load through the existing standalone-scene path. The C fixture covers default
and explicit roots, sparse slots, mixed child types and read failures.

The `actor-events`, `dissolve` and `scrolling-text` seek captures matched the
retail scene count, scene order and authored frame ranges at all 180 positions
in each host. Separate continuous playback traces matched the 180 authored
frame commits per host and reached the same last frame as retail. The seek
images still show renderer differences. Audio-enabled playback also completed
for all three players and movies after updating the capture helper. The seek
and playback records are in `tmp/c89-host-directory-regression/playback.sqlite`
under suites `20261117T213806Z-87243a0d`, `20261117T214507Z-23c7255b` and
`20261117T214807Z-c8ece7a8`.

All 181 standalone and 11 Marcus-enabled C89 checks passed, along with
`archive-api` and `movie-save-load`. The session source compiled under strict
C89 for Linux x64 musl. Marcus and SDL Studio built successfully. Marcus was
relaunched from this workspace after the build.

### Shared C89 movie source-table loading (17 November 2026)

SDL Studio, Marcus and the standalone session now locate the movie's source
GST through the same C89 chunk view and decode its authored SID and name rows
with the same reader. The two hosts retain the native text conversion. The
standalone resolver and the host VFS adapter share case-insensitive archive
extension filtering and the existing mount order.

Test copies of `actor-events` and `scrolling-text` changed the retail source
ID from 2 to 29 in the source table, actors, roll call, backgrounds and 3D
text font tags. Retail, SDL Studio and Marcus each reproduced their original
seek states at all 120 authored positions and their original viewport
hashes at all 16 sampled positions. The standalone C89 probe reproduced
its actor, part, material and mesh observations across the same 120
positions after normalizing the authored source ID.
Original v3dmm matched its own 60 seek states and eight viewport hashes for
the `actor-events` pair in stock VMM envelopes.

The engine fixtures cover ANSI and UTF-16 source rows, duplicate IDs, empty
names, decode budgets, the absent-table retail default and missing assets.
The VFS fixture covers the extension filter with mixed-case names. All 181
standalone and 11 Marcus-enabled C89 checks passed, along with
`archive-api` and `movie-save-load`. The affected C files also compiled
as strict C89 for Linux x64 musl. Both hosts built successfully and Marcus
was relaunched from this workspace.

The SDL VMM workflow passed source-6 actor authoring, save and fresh-process
reopen, package management, per-movie activation and saved subsets. Marcus
opened the resulting bundled VMM and rendered its authored frame through
the headless player API.

The retagged fixtures and capture logs are under
`tmp/c89-host-sources-verify/`.

### Shared C89 movie resource lookup (17 November 2026)

The standalone content resolver, SDL Studio's resource manager and Marcus
preview now share the ordered C89 archive search. The standalone provider
opens `.3cn` files on demand. Studio fetches through each resource cache so
an existing cached object can satisfy the request. Marcus filters its
candidates by source ID and uses the movie cache for embedded tags.

The headless fixtures cover duplicate keys in eligible archives, a miss
before a match, an unreadable earlier archive, an absent asset, and the
embedded-versus-mounted boundary. All 181 standalone and 11 Marcus-enabled
C89 checks passed, along with `archive-api` and `movie-save-load`. The four
affected engine sources compiled as strict C89 for Linux x64 musl. Both
hosts built successfully and Marcus was relaunched from this workspace.

For `actor-events` and `scrolling-text`, the stock and SID 29 editions
produced 480 seek states and 64 viewport hashes across SDL Studio and
Marcus. They match the pre-change captures for each host, and the common
scene and frame metadata agree with the retained retail observations. The
standalone probe matched 120 pairs of camera, actor, appearance and mesh
observations after source ID normalization. The duplicate-key and archive
failure cases were exercised by engine fixtures.

Normal playback in both hosts matched the retained retail frame commit
order for `actor-events`, `dissolve` and `scrolling-text`, with 180 frames
per host and the expected final scene and frame. This comparison covers
frame order and completion.

The SDL VMM workflow passed source-6 authoring, package management, save
and fresh-process reopen. Marcus rendered the first authored frame of
the saved bundled VMM through its headless player.

Capture logs, settings and input and build hashes are under
`tmp/c89-resource-lookup/`. The live actor and camera evaluation cutover
remains in the [C89 plan](../../plan.md#immediate-next-work-and-completion).

### Shared C89 camera event selection (17 November 2026)

The standalone movie session and Marcus live seek select a camera through
the shared C89 event view. They use the last qualifying cue in group order,
including cues on the target frame. Studio rewinds through the same event
view with the already processed prefix and a strict frame boundary. Studio
then applies the target frame's effects through the live dispatcher. The
same rewind query selects the preceding sound, preserving MIDI cache effects.

Fresh retail captures for `actor-events` and `scrolling-text` crossed the
frame 15 cut repeatedly in both directions, including repeated seeks to the
cut and re-entry into both scenes. For the same sequence, SDL Studio and
Marcus produced 224 seek observations. Their scene, frame and camera state
agreed with retail, and each host matched its pre-change viewport hashes
at all 224 positions. The standalone C89 query matched the camera index
at all 112 retail positions.

Normal playback of `actor-events`, `dissolve` and `scrolling-text` matched
the retained retail frame order and final position at 180 commits per host.
The playback comparison measured frame order and completion.

Fixtures covered ties in event order, exclusion of an unprocessed Studio
event and Marcus seeks across a live camera edit. All 181 standalone and 15
host checks passed, including `marcus-storyboard`, `marcus-playback`,
`archive-api` and `movie-save-load`. The existing `marcus-playback` invalid
MVIE fixture exposed changed diagnostic wording from the movie directory
cutover. The archive adapter now reports the original record-header message.
The affected engine sources compiled under strict C89 for Linux x64 musl.
Both hosts built successfully and Marcus was relaunched.

Capture logs, build identities and test results are under `tmp/c89-live-camera/`.

### Shared C89 actor seek driver (17 November 2026)

Studio, Marcus and the standalone movie session now run actor seeks through
`mm_actor_seek_run`. The driver advances the seek state and delivers one
effect at a time. Studio's callbacks publish the C state into the live actor
before applying body, costume and audio effects and bring the resulting state
back to the driver. Marcus and the standalone session use the Preview actor
effect adapter. The Studio C++ seek wrapper is removed.

Repeated and reversed seeks in `actor-events` and `scrolling-text` produced
228 host observations and 180 sampled viewport hashes. Both hosts matched
their pre-change results. Studio matched 456 actor records from fresh retail
seeks for action, cel, stage visibility, event cursor, template and fixed-point
root and part matrices. Original v3dmm agreed on those fields in stock VMM
envelopes. The standalone session matched 456 actor identities and visibility
records, including 453 on-stage action, cel, event and route positions.

Normal playback of `actor-events`, `dissolve` and `scrolling-text` reproduced
the retained retail frame order and final position at 180 commits per host.
The six `sampled-audio` captures each completed all 60 frames. Studio matched
retail's four sound requests in both repetitions. PCM16 peaks remained 2380
in retail and 2176 in each host. Exact PCM and timing comparisons were
inconclusive because the two retail captures differed. The Marcus capture
API supplies mixed output and frame commits, with no sound-request trace.

The native core fixture reproduced all 26,564 bytes of its actor seek
command/state trace. The C fixture distinguishes a frame failure that allows
later frames to run from a negative effect or a sticky provider fault. All
181 standalone and 16 host checks passed, including `engine-core-link`,
`marcus-playback`, `marcus-storyboard`, `archive-api` and `movie-save-load`.
The changed C files also compiled under strict C89 for Linux x64 musl.
SDL Studio and Marcus rebuilt successfully and Marcus was relaunched.

Capture logs, input and build hashes, test results and the sampled-audio
database are under `tmp/c89-live-actor/`.

### Shared C89 actor frame effects (17 November 2026)

Studio, Marcus and the standalone session now use `mm_actor_frame_run` to
deliver entrance queries and event commands. `mm_actor_frame_display`
schedules motion sounds, body position, cel selection and intermediate pose
updates. The adapters supply actor and renderer operations. Studio's C++
frame evaluator is removed. The C fixture checks the effect order, terminal
event errors and the Studio and Preview failure policies.

The native frame trace matched all 170,230 bytes and the seek trace matched
all 26,564 bytes against the pre-change build. In `actor-events` and
`scrolling-text`, 228 host seek observations and 180 viewport hashes matched
the previous captures. Studio agreed with retail and v3dmm on all 456
compared actor records, including fixed-point root and part matrices. The
standalone session reproduced 453 on-stage route and animation records and
all 456 visibility and template records against retail.

Normal playback of `actor-events`, `dissolve` and `scrolling-text` matched
the retained retail frame order and final position at 180 commits per host.
Two `actor-audio` runs per player completed all 60 frames and contained both
authored tones (600 Hz and 1,300 Hz) in the first scene. Studio matched
retail's 12 sound requests in both runs. Exact PCM and timing comparisons
remain inconclusive because the retail repetitions differed. The full
presentation database import was stopped after the direct loopback check
covered the actor audio contract.

All 181 standalone and 16 host checks passed, including `engine-core-link`,
`marcus-playback`, `marcus-storyboard`, `archive-api` and `movie-save-load`.
The changed C sources compiled under strict C89 for Linux x64 musl. Both
hosts rebuilt successfully and Marcus was relaunched.

The logs, capture hashes, build identities and standalone link command are
under `tmp/c89-actor-frame/`.
This workspace path links to the evidence drive to keep space available for
Windows builds.

### Shared C89 scene seek transaction (17 November 2026)

Studio and Marcus now pass their live frame seeks through `mm_movie_seek_frame`.
The transaction runs the opening phase, seeks actors in scene order, then
runs the final phase. Studio dispatches scene events before the actors and
handles captions, prerendering and view notifications afterward. Marcus
applies its live camera and publishes the frame after the actors. The engine
fixtures retain failure coverage for both paths, including partial effects.

The standalone `mm_movie_session_seek` returns an owned camera and actor
snapshot after reading the scene header and start group once. The CLI produced
identical results at 300 positions across three stock movies and two SID 29
fixtures. A real seek with only the background archive mounted completed
camera resolution and returned the existing missing-actor-content error.
The CLI emitted no partial frame. The C fixture covers empty actor lists,
packed event groups in both byte orders, an out-of-range frame, and output
preservation after an invalid event group or missing content.

Both hosts matched 228 pre-change seek observations and 180 viewport
hashes. Studio's 456 actor records matched the retained retail and v3dmm
observations, including the fixed-point transforms. Normal playback of
`actor-events`, `dissolve` and `scrolling-text`, plus an in-process replay
of `actor-events`, matched retail's frame order and final position across
240 commits per host. Two direct loopback runs per host captured both
authored actor tones, and Studio matched retail's 12 sound requests in
each run. Timing and exact PCM remain inconclusive against the retained
retail repetitions.

All 181 standalone and 16 host checks passed. The four affected C files
compiled under strict C89 for Linux x64 musl. Both hosts built and Marcus
was relaunched. Evidence is under `tmp/c89-scene-seek/` on the evidence drive.

### C89 movie open owner (17 November 2026)

SDL Studio, Marcus and the standalone probe now retain the same C89 movie
session type after open. Studio resolves live scene slots through its owned
session. If save selects a new root, Studio validates the new session before
releasing the old one. Marcus retains its session with the archive installed
in Preview. Scene and source reads keep the archive's decode budget and
diagnostics. The standalone session still owns its immutable index.

Engine fixtures covered a root that moves to a new chunk position, a live
scene added after open, failed replacement retaining the previous session,
and successful rebinding after a root change. The C-only probe matched five
pre-change movie directories and all 300 camera and actor snapshots. The
missing-actor-content fixture returned the same error and emitted no partial
frame. All 181 standalone and 16 host checks passed, including the Marcus
invalid-header playback fixture, `archive-api` and `movie-save-load`. Five C
sources also compiled under strict C89 for Linux x64 musl. Both hosts built
successfully and Marcus was relaunched.

Across both hosts, 456 seek states and 360 viewport hashes matched the
pre-change captures for stock and SID-29 actor-events and scrolling-text
movies. Timed playback of actor-events, dissolve and scrolling-text matched
retail frame order and final position across 240 frame commits per host,
including an actor-events replay in the same process. The eight Studio VMM
workflow checks passed. The new plain save was byte-identical to the prior
save and reopened at the same scene, frame and camera in retail, Studio and
Marcus. Marcus also reopened the bundled save. The plain save check passed
in Marcus with an empty pack directory. With the configured pack directory
the same check reported a chunky archive open error.

Evidence is under `tmp/c89-movie-open/` on the evidence drive, including
the binary and fixture identities, seek and playback logs, and save/reopen
captures.

### C89 actor template and camera resource links

Studio, Marcus and the standalone session use the same exact chunk-link
lookup when binding an embedded actor template or an authored camera.
The colour and depth bitmap links resolve through one C89 function in
the archive that owns the selected camera. The fixture covers renumbered
templates, external SIDs, a missing linked target, duplicate typed links,
and absent or unreadable bitmap links. The standalone reader keeps its
stored-template fallback when a linked target is absent.

All 181 standalone and 21 distinct host checks passed. Both apps rebuilt
and Marcus relaunched. Six changed C files compiled under strict C89 for
Linux x64 musl. The standalone probe matched 300 camera and actor snapshots
and 50 model and material snapshots. Missing actor content returned the
same error without publishing a partial frame.

The stock and SID-29 actor-events and scrolling-text cases matched all
456 pre-change seek states and 360 viewport hashes across Studio and
Marcus. The shared camera slots matched retained retail captures, as did
456 Studio and 456 v3dmm actor records. The background harness rendered
188 cameras across all 12 stock backgrounds. Its 301,996,032 output bytes
had the retained FNV-1a fingerprint `E1A71005`.
Normal playback of actor-events, dissolve and scrolling-text matched
retail's frame order and final position over 240 frame commits per host,
including an actor-events replay in the same process.

Evidence is under `tmp/c89-frame-resources/` on the evidence drive.

### C89 cel and costume bindings

The standalone body evaluator and the live bodies in Studio and Marcus
use one C89 cel binding driver. It prepares replacements before applying
models and matrices in shape order. A keep-model cel still applies its own
matrix and leaves a model installed by an earlier cel or costume in place.
The shared material binder indexes custom costume slots by the matching
parts of a body set. Generic materials use the same traversal, including
on generated text.

The C fixture exercises a costume model followed by a keep-model cel,
then a cel and generic material replacement. A keep-model cel with an
invalid matrix rejects the query and preserves the previous snapshot.
Existing renderer service tests cover reference cleanup, failure before
model publication, and model and material assignment order. The rebuilt
standalone probe matched 300 actor and camera snapshots and 300 model and
material snapshots across three stock and two SID-29 movies.

All 181 standalone and 20 host checks passed. Five changed C files compiled
under strict C89 for Linux x64 musl. AddressSanitizer passed the chunky,
template runtime, and body runtime checks. Both hosts rebuilt and Marcus
relaunched. The two stock actor renders exercised default and spare custom
materials. The bundled VMM reopened at the same scene, frame and camera in
Studio and Marcus with the pack directory isolated. Marcus's image matched
the prior VMM capture.

The stock and SID-29 actor-events and scrolling-text cases matched all
456 pre-change seek states and 360 viewport hashes across both hosts. The
Studio and v3dmm actor observations matched 456 retail actor records each.
Normal playback of actor-events, dissolve and scrolling-text matched
retail's frame order and final position over 240 frame commits per host.
The actor-events case included replay in the same process.

Evidence is under `tmp/c89-body-bindings/` on the evidence drive.

### C89 default costume resolution

Studio, Marcus and the standalone session now pick each set's first
authored costume through the same C89 driver. The live path fetches all
defaults before updating the body. The standalone path stages the new
snapshot and preserves the caller's previous snapshot on failure. Older
templates share a scan of `CMTL` links in child order. The live editor
keeps the full costume catalog and the standalone reader takes the
first discovered costume for each set.

The C fixtures cover an authored default that differs from the first
linked costume, a missing authored default, and legacy table construction
with sparse child IDs. All 181 standalone and 22 host checks passed.
AddressSanitizer passed the chunky and template construction checks.
Both changed C files compiled under strict C89 for Linux x64 musl.
Studio and Marcus rebuilt and Marcus relaunched.

Across both hosts, stock and SID-29 actor-events and scrolling-text matched
456 prior seek states and 360 viewport hashes. Actor-events, dissolve and
scrolling-text matched retail frame order and final position in 240 frame
commits per host, including a same-process replay. The standalone probe
matched 300 actor and camera snapshots and 300 model and material snapshots.

After removing the `GGCM` links from two stock templates in a private
archive copy, both stock actor renders matched their prior bytes and
30 standalone model and material snapshots matched the pre-change probe.
Six actor-events seeks in each of retail, SDL Studio and Marcus matched
that player's stock states and viewport hashes. The retail bridge recorded
the modified `TMPLS.3CN` as an opened archive.
Evidence is under `tmp/c89-default-costumes/` on the evidence drive.

### C89 render frame ownership

Studio and Marcus pass a C frame record containing the world, camera and
colour/depth targets to BRender. Studio borrows its live targets through
that record. Marcus allocates and owns temporary render targets and the
world through the same C code. Picking uses the owner for its temporary
world and attached camera. Scaled export takes its world and camera from
the live frame and keeps separate output pixels.

The C fixtures check borrowed handles at renderer submission, partial
allocation, repeat disposal, and world/depth/colour release order. All
181 standalone and 26 host checks passed, including picking, native world
export, and the default and Vulkan raster parity cases. Four targeted
AddressSanitizer checks passed. Five changed C units compiled under strict
C89 for Linux x64 musl. Studio and Marcus rebuilt and Marcus was relaunched.

The native world-export trace matched all 13,188,586 retained bytes. The
background trace matched 301,996,032 bytes across 188 cameras and 12
backgrounds. The stock and SID-29 actor-events and scrolling-text cases
matched all 456 pre-change seek states and 360 viewport hashes across both
hosts. Normal playback of actor-events, dissolve and scrolling-text matched
retail's frame order and final position over 240 frame commits per host.
The actor-events case included replay in the same process.

Evidence is under `tmp/c89-render-frame/` on the evidence drive.

### C89 evaluated frame and preview resource lifetime

The standalone `--frame-bodies` probe resolves the selected camera and
every actor's effective body in one seek. All 300 snapshots of the stock
and SID-29 fixtures matched the previous camera and mesh queries, including
the resolved materials and world matrices. A directory containing the
required background but missing actor content returned an error before
printing a frame.

Marcus rendering and picking now hold references to live bodies and any
standalone model until their temporary world is released. The headless C
fixtures check those references during rendering and picking and balance
them on failure. All 181 standalone and 21 selected host checks passed,
including the C89 syntax gates and Vulkan raster parity. Three targeted
AddressSanitizer checks passed with the Visual Studio runtime environment
loaded. The three changed C engine units also compiled under strict C89
for Linux x64 musl. Studio and Marcus rebuilt and Marcus was relaunched.

The stock and SID-29 actor-events and scrolling-text cases matched all
456 prior host seek records and 360 viewport hashes. The 301,996,032-byte
background trace matched across all 188 cameras. Playback of actor-events,
dissolve and scrolling-text matched retail's frame order and final position
over 240 frame commits per host, including same-process actor-events replay.
The stock SDL Studio seeks and retained v3dmm seeks each matched 456 retail
actor records. The session's owned bindings are available to the standalone
probe. The hosts still use the live scene evaluation path.

Evidence is under `tmp/c89-owned-frame/` on the evidence drive.

### C89 frame evaluation over loaded chunk graphs

The frame, body and mesh readers now use the same evaluator with stand-alone
stream indexes and loaded chunk graphs. The graph reads decoded chunk
payloads and the original serialized names used by embedded 3D text. Its
resolver can supply loaded content graphs for external assets. The Studio
movie owner and Marcus's Archive directory reader supply graph adapters.
Both renderers still get their actor and camera state through live evaluation.

The native graph fixture matched camera and actor poses, effective models
and materials, world matrices, vertices and faces against the stream
evaluator on 300 frames of actor-events, dissolve and scrolling-text,
including the SID-29 variants. The standalone probe matched all 300
pre-change snapshots. The C fixture checks external graph resolution,
preservation of the previous snapshot on failure and snapshot lifetime
after the graph closes.

All 181 standalone and 22 selected host checks passed, including the C89
syntax gates, movie save/load and Vulkan raster parity. Two targeted
AddressSanitizer checks passed. The six changed C evaluator units also
compiled under strict C89 for Linux x64 musl. Studio and Marcus rebuilt.
Marcus was relaunched.

Both hosts matched all 456 pre-change seek records and 360 viewport hashes
on the stock and SID-29 fixtures. The 301,996,032-byte trace for 188 cameras
matched the retained background trace. In normal playback of actor-events,
dissolve and scrolling-text, each host matched retail's frame order and final
position over 240 commits, including actor-events replay. The stock Studio
seeks and retained v3dmm seeks each matched 456 retail actor records.

Evidence is under `tmp/c89-graph-frame/` on the evidence drive.

### C89 camera bindings and placement

Studio, Marcus and the session now use the same decoded camera binding
and exact colour and depth links. Session snapshots carry both link keys
when available. Marcus reads the camera and placement points from a single
payload and renders using the C camera data. The C owner publishes the
new placement cursor after both images draw successfully. Temporary camera
changes leave the placement cursor in effect.

The camera fixtures check little- and big-endian bindings, absent depth
links, single reads, placement cycling and reuse, temporary selections,
and cursor preservation after allocation, point-read and image failures.
All 300 native graph and stream frame comparisons included the bitmap
keys, and 300 standalone probe results matched the previous build.

All 181 standalone and 24 selected host checks passed, including camera
placement, C89 syntax, movie save/load, and Vulkan raster parity. The
storyboard test exercised prop placement against the three insertion
points read from the retail black camera, cursor reuse after moving a
prop, and camera switching. Four targeted AddressSanitizer checks passed.
The four changed C units compiled under strict C89 for Linux x64 musl.
The 301,996,032-byte background trace matched across 188 cameras. Studio
and Marcus rebuilt and Marcus was relaunched.

Both hosts matched the previous 456 seek records and 360 viewport hashes
on stock and SID-29 scenes. Normal playback of actor-events, dissolve
and scrolling-text matched retail's frame order and final position for
240 frame commits per host, including actor-events replay. The stock
Studio seeks and retained v3dmm seeks each matched 456 retail actor
records.

Evidence is under `tmp/c89-camera-binding/` on the evidence drive.

### C89 live actor state ownership in Studio

Studio actors now store the `MMActorState` record used by Marcus and the
session evaluator. Normal seeks, frame sequencing, event effects and pose
updates operate directly on that record. Studio route points, route
locations and stretch values use the C layouts. Native pose adapters
copy the resulting matrix into BRender at the SDK boundary. Actors still
use native resources and render effects.

All 22 native actor traces matched byte for byte across 3,370,910 bytes,
covering state, route, pose, frame, seek, edit, and rollback cases. The
480-byte headless save matched the previous build and reopened in Studio.
The retail bridge timed out on that synthetic blank fixture. A separate
Studio run applied a position edit to actor-events, saved the movie, and
reopened it in Studio and retail. Both matched the edited actor's stage,
root and part matrices, action and cel across four seeks including the
return to frame one.

All 181 standalone and 20 selected host checks passed, including the C89
syntax gates, scene content, save/load, and Vulkan raster parity. The stock
and SID-29 cases matched all 456 previous host seek records and 360 viewport
hashes. Actor-events, dissolve and scrolling-text matched retail's frame order
and final position over 240 playback commits per host, including actor-events
replay. Stock Studio and retained v3dmm seeks each matched 456 retail actor
records. The 188-camera background trace matched the previous build.
Studio and Marcus rebuilt and Marcus was relaunched.

Evidence is under `tmp/c89-actor-state-owner/` on the evidence drive.

### Live cameras consume the C89 session binding

Studio's world and the Marcus preview now apply the result of
`mm_movie_camera_graph` during normal camera selection. Their live
timelines choose the slot and the session evaluates it in the loaded
background graph. The camera and image keys reach both renderers through
that prepared binding. Placement reads the same decoded payload.

The C fixtures cover missing slots, malformed tails, consumer error
propagation, and applying a prepared binding without a second camera or
image-link lookup. They check Studio's fallback on placement allocation
failure and Preview's colour prefix and cursor when a depth key is absent.
All 181 standalone, 24 selected host, and three AddressSanitizer checks passed.
The three changed C sources compiled under strict C89 for Linux x64 musl.
The 300 graph and stream frame results matched, and all 300 standalone
probe snapshots matched the previous build. The 188-camera background
trace matched across all 301,996,032 bytes.

The stock and SID-29 actor-events and scrolling-text runs matched 456
previous seek records and 360 viewport hashes, including 48 in-scene
camera changes across forward and backward seeks. Actor-events, dissolve,
and scrolling-text matched retail's frame order and final position over
240 playback commits per host, including actor-events replay. Stock Studio
and retained v3dmm seeks each matched 456 retail actor records. Studio and
Marcus rebuilt and Marcus was relaunched.

Evidence is under `tmp/c89-camera-consumer/` on the evidence drive.

### Live actors apply the C89 cel binding

Studio, Marcus, and the standalone session use `mm_movie_cel_part` to
evaluate replacement models and local matrices. The live hosts use cached
action tables and stage every part before changing the body. Ordinary
templates share exact `BMDL` child lookup with the session. Generated 3D text
continues to use its font for glyph models. The runtime fixture checks that
each part is read once, matrix evaluation finishes before body publication,
and fetch failures leave the body and step distance unchanged.

All 181 standalone, 22 selected host, and two AddressSanitizer checks
passed. Both changed C engine sources compiled under strict C89 for
Linux x64 musl. The 300 native graph and stream results matched, along with
300 standalone frame snapshots against the previous build. All 22 native
actor traces matched across 3,370,910 bytes, including costume editing.
The 480-byte saved movie and the 301,996,032-byte background trace also
matched the previous build.

Stock and SID-29 actor-events and scrolling-text matched 456 previous seek
observations and 360 viewport hashes. Actor-events, dissolve, and
scrolling-text matched retail's frame order and final position across 240
commits per host, including actor-events replay. Stock Studio and retained
v3dmm seeks each matched 456 retail actor records. Retail reopened a movie
with an actor transform saved by Studio and reproduced the matrices, action,
cel, and stage state on four seeks. Marcus was rebuilt and relaunched.

Evidence is under `tmp/c89-cel-consumer/` on the evidence drive.

### Live costume caches consume C89 material and model bindings

Studio and Marcus resolve authored costume IDs through the session's exact
`CMTL` lookup. Their shared cache loader uses the same per-slot `MTRL` and
optional `BMDL` bindings as the session's evaluated body. The C fixture
checks sparse slots, an absent replacement model, a dangling model link,
and cleanup when the loader rejects a costume.

All 181 standalone, 25 selected host, and three AddressSanitizer checks
passed. Both changed C engine sources compiled under strict C89 for Linux
x64 musl. The 300 graph and stream results matched. The 300 standalone
body snapshots and 300 mesh snapshots matched the previous build.
Another 30 mesh snapshots matched with the legacy costume archive, and
both native legacy actor renders matched the retained images. The 22
native actor traces and the 102,316-byte material resource trace matched
byte for byte.

Stock and SID-29 seek runs matched 456 observations and 360 viewport
hashes. Both hosts matched retail's frame order and final position across
240 playback commits each. Stock Studio and retained v3dmm seeks each
matched 456 retail actor records. The 188-camera background trace matched
the previous build.

Retail, Studio, and Marcus each matched six actor poses and viewport
hashes from stock and retained captures when the legacy archive omitted
the authored `GGCM` links. Retail reopened a movie with an actor transform
saved by Studio and reproduced the matrices, action, cel, and stage state
on four seeks. Marcus was rebuilt and relaunched.

Evidence is under `tmp/c89-costume-consumer/` on the evidence drive.

### Live actors apply the C89 body pose

Studio, Marcus, and the standalone actor evaluator now produce
`MMActorBodyPose` from live motion state. `mm_actor_pose_root` composes the
root matrix for both native bodies and standalone meshes. The renderer
receives the composed value directly. The C body fixture covers an existing
translation in the orientation matrix, signed fixed-point wrapping, and
visible versus hidden body notifications.

All 181 standalone, 28 selected host, and six AddressSanitizer checks
passed. All six changed C engine sources compiled under strict C89 for
Linux x64 musl. The 300
native graph and stream frame results matched. All 300 standalone frame
snapshots and 300 mesh snapshots matched the previous build. Stock and
SID-29 fixtures matched 456 seek observations and 360 viewport hashes.
All 25 native actor and body traces matched across 5,755,758 bytes,
including body placement, duplication, restoration, and geometry. The
480-byte saved movie and the 188-camera background trace also matched.

Each host matched retail's frame order and final position across 240
playback commits. Stock Studio and retained v3dmm seeks each matched 456
retail actor records. Retail reopened the movie saved after a Studio
actor transform and reproduced the matrices, action, cel, and stage state
on four seeks. Marcus was rebuilt and relaunched.

Evidence is under `tmp/c89-pose-consumer/` on the evidence drive.

### Live bodies own evaluated bindings in C89

The live body record owns the root and local matrices, part parents,
retained models, and material bindings. The BRender tree receives changes
from that record. Duplication and reshaping copy the evaluated state from
the C record. Marcus also uses the C root and part matrices for object
and costume bounds. The native draw tree and bindings share an allocation,
preserving the original allocation and failure order.

All 181 standalone, 31 selected host, and seven AddressSanitizer checks
passed. The host checks included Marcus costume previews, actor insertion,
and the headless player suite. The three changed C engine sources compiled
under strict C89 for Linux x64 musl.
All 26 native actor and body traces matched across 5,843,894 bytes, including
allocation failure, restoration, and reshape cases. The 480-byte saved
movie and 188-camera background trace matched their retained results.
The 300 graph and stream frame results matched. The 300 standalone frame
snapshots and 300 mesh snapshots matched the previous build, and both
native legacy costume renders matched the retained images.

Stock and SID-29 fixtures matched 456 seek observations and 360 viewport
hashes. Both hosts matched retail on 240 playback commits each. Stock
Studio and retained v3dmm seeks each matched 456 retail actor records.
Retail reopened the movie saved after a Studio actor transform and matched
the edited actor on four seeks. Marcus was rebuilt and relaunched.

Evidence is under `tmp/c89-body-state/` on the evidence drive.

### Live renderers submit the C89 body bindings

Studio reapplies the evaluated root and part bindings before drawing
its attached bodies. Marcus builds its temporary render and picking
trees from those bindings in parent-first order. The C fixtures check
submission order, a stopped submission, picking ownership, and resource
cleanup after a failed draw or pick. The native body fixture changes the
renderer projection before drawing and checks that the C bindings restore
the root transform, model, and material.

All 181 standalone, 33 selected host, and five AddressSanitizer checks
passed. The host checks included world render and export, Marcus costume
previews and actor insertion, and the headless player suite. All three
changed C sources compiled under strict C89 for Linux x64 musl. The 300
standalone frame and 300 mesh snapshots matched the previous build, and
all 300 native graph and stream frame results matched. Stock and SID-29
seeks matched 456 observations and 360 viewport hashes.
All 26 native actor and body traces matched across 5,843,894 bytes.
The 480-byte saved movie and 188-camera background trace also matched.

Both hosts matched retail on 240 playback commits each. Stock Studio and
retained v3dmm seeks each matched 456 retail actor records. Retail reopened
the movie saved after a Studio actor transform and matched the edited
actor on four seeks. Marcus was rebuilt and relaunched.

Evidence is under `tmp/c89-draw-bindings/` on the evidence drive.

### C89 preference policy and native settings storage (17 November 2026)

`mm/settings.h` now owns typed get/set/default transactions and configuration-root
selection. `mm-native-settings` supplies Windows registry access/enumeration and
POSIX file storage through an explicit C context. The old `settings_store.cpp`,
`settings_paths.cpp`, `settings_store_win.cpp` and `settings_store_posix.cpp` are
removed. `settings_store_bridge.cpp` translates existing callers, C++ values and
callback exceptions; no exception crosses a C callback. Output notifications
execute in C's assignment order, retaining aliased flags, close-callback
observations/modifications and typed request updates between callbacks.

Windows retains HKCU/HKLM, the 32-bit registry view, raw payload sizes (including
legacy one-byte DWORD values), optional validated test isolation and read-only
enumeration. Unknown types and unnamed setup markers remain excluded from the
enumeration. POSIX retains ASCII-folded/hex names, 126-byte name limits, KPS1
records, 16 MiB payload admission, namespace locking, no-follow/regular-file
checks, permissions, exact/retrying I/O and temporary-file replacement. A context
owns its temporary sequence and mutex; two-word arithmetic preserves the prior
64-bit decimal numbering and wrap without C99 integer types. Directory sync
remains best effort. Existing directories, stored encodings and movie data are
not rewritten.

The original revision is `13e4d28648f9c8e710f4228c17ca97266296ccf0`. Original policy
and registry fixtures passed in Native, SDL Release and SDL Debug before
conversion. An original-versus-C policy grid matches in 163,840 cases per
character width (327,680 total), covering flags, open/read/write failures, types,
capacities and cleanup; 196 configuration-root combinations match per profile.
The focused callback fixture passes against both the original C++ function and
the adapter, including exceptions, flag publication before close, close-side
modifications, aliased outputs and callback changes to a request.

The isolated Linux guest passes eight original/converted/C-only/adapter programs.
The existing physical-store fixture covers persistence, malformed/truncated
records, symlinks/FIFOs, partial-write recovery, temporary cleanup and concurrent
defaults. New C checks cover native persistence and query sizes, allocation
failure cleanup, EINTR/short transfers, name limits, 32-bit sequence carry and
64-bit wrap. The standalone Linux engine/provider targets build and link with
the C toolchain under strict C89 diagnostics; the guest powers off after testing.
Strict 32-bit Linux GNU and ARM64 macOS C objects and the macOS C++ interfaces
also compile. macOS runtime and precise original allocation-failure ordinals
remain unverified.

Native passes 269 selected CTest checks, SDL Release passes 264 with the existing
`world-export` skip, SDL Debug passes 265, and the Windows standalone C89 engine
passes 175. The ANSI/UTF-16 standalone platform profiles pass nine each;
AddressSanitizer passes the three new C settings checks. Total: 994 passed CTest
checks and one existing skip. The regular headless preview harness passes.
All 219 retained trace pairs match; all three hosts retain 12 backgrounds,
188 cameras, 301,996,032 background bytes and FNV `E1A71005`. Ten input hashes
and the Windows ACP/OEM/locale identity remain unchanged; 279 executable
identities are recorded. Native tools/Marcus and both SDL Studio profiles rebuild;
Marcus is stopped before rebuilding and relaunched afterward. No Marcus UI tests
or new retail captures are added. The INI frontend, default C++ bindings and
other native facades remain work; this is not completion of the C89 migration.

Evidence: `tmp/c89-settings/{settings_store.cpp,settings_paths.cpp,settings_store_win.cpp,settings_store_posix.cpp}`,
`policy-compare-{ansi,wide}.log`, `adapter-{original,focused}.exe`,
`linux-{build-final,runtime}.log`, `linux-identities.json`,
`run-{native,sdl-release,sdl-debug,standalone}-tests.log`,
`platform-{ansi,wide}-tests.log`, `asan-runtime.log`, `run-regular-tests.log`,
`trace-comparison.json`, `gates.json` and `identities.json`.

### C89 core synchronization, thread tokens and clocks (17 November 2026)

`mm-native-runtime` now supplies recursive mutexes, coalescing auto-reset events,
per-thread diagnostic tokens, clock-source exchange and steady ticks through
`mm/native_runtime.h`. Windows uses native critical sections, condition variables,
TLS and QPC. POSIX uses pthread primitives and the existing host's monotonic clock
epoch (Linux `CLOCK_MONOTONIC`, macOS `CLOCK_MONOTONIC_RAW`). Runtime contexts own
their keys, token counter and clock hook. Zero remains a valid initialized token
after 32-bit wrap. Waits retain their deadline across spurious wakeups and consume
one pending latch. Clock callbacks execute outside the context lock. Token
queries preserve Windows last-error/POSIX errno, including initialization and
cached reads; original/converted cached-getter fixtures verify that behavior.

`runtime_math.c` supplies exact two-word/96-bit scaling, including the QPC fast
path, fraction truncation, overflow checks and millisecond wrap. Signed native
clock samples retain truncation toward zero. `core_platform.cpp` is removed;
`core_platform_bridge.cpp` translates legacy values/exceptions and preserves the
native clock's fatal failure behavior. Tick callback exceptions remain inside
the adapter until C has returned. Allocation continues through the existing C
heap policy with the original native prefix. The legacy mutex/event classes
reserve their former size/alignment for diagnostic allocation compatibility;
the standard-library types in those headers are compile-time layout profiles,
not constructed synchronization objects. Native state belongs to C owners.
Legacy clock/diagnostic entry points have no native-error channel; default-runtime
failures terminate instead of unwinding into C. Explicit C clients receive error
results. User tick-callback exceptions still propagate to their C++ caller after
the C dispatcher returns.

The original revision is `2c4d8472f27d859e15a7c33b6c4344cbac558874`. Existing platform
and MIDI-worker checks pass before and after conversion in the three Windows
hosts. The new adapter fixture passes against the original implementation too.
It brackets 1,000 default tick samples with the original steady clock and checks
callback exceptions/reset/reentrancy and legacy ownership/layout properties.
Original and converted mutex/event/heap profiles match for Windows x64/x86 MSVC,
Linux x64/x86, macOS ARM64 and macOS x64. These are compile-time ABI checks, not
six platform runtime validations. A separate 100,480-case arithmetic probe matches
Python integers for 96-bit intermediates, division/truncation, overflow and output
publication; output SHA-256 is
`F50F1DC60C1C31132216A527F95DB34E057CFB04530571BBDBA551F2793A188E`.

One SDL Debug rerun exposed a silent `file-ops-expensive` fixture failure. Fifty
focused reruns passed, but a controlled clock/collision reproducer identified
the setup flaw: independently chosen, unreserved filenames can alias. The old
fixture fails with `unreserved_alias=1`; the corrected fixture passes with
`unreserved_alias=0` and its original `860BB78D` fingerprint, using the same
runtime libraries. The fixture now checks names against its existing in-memory
set and reports failing lines. The one/two-byte C naming suite also preserves
the demonstrated legacy alias behavior; production naming policy is unchanged.
The initial failure and deterministic old/fixed probe logs are retained.

C-only fixtures exercise recursive cross-thread exclusion, single-waiter wake,
pending-signal coalescing, timed waits, independent contexts, stable thread tokens,
token-zero wrap, callback failure publication and unlocked callback invocation.
Provider checks inject allocation, mutex/key initialization and clock failures,
verify cleanup counts, inject spurious wakes, and exercise signed clock samples,
tick wrap and deadlines beyond 32-bit elapsed milliseconds. The isolated Linux
guest passes nine original/converted/C-only programs, then powers off. Its three
new standalone engine/runtime targets build/link with the C toolchain under
strict C89 diagnostics. Strict 32-bit Linux GNU and ARM64 macOS C objects and
macOS C++ adapter interfaces also compile; macOS runtime remains unverified.

Native passes 276 selected CTest checks, SDL Release passes 271 with the existing
`world-export` skip, SDL Debug passes 272, and the Windows standalone C89 engine
passes 180. Both standalone Windows platform profiles pass ten checks each;
AddressSanitizer passes the three new C runtime/math/provider checks. Total:
1,022 passed CTest checks and one existing skip. The regular headless preview
harness passes. All 219 retained trace pairs match byte for byte. Each host
retains 12 backgrounds, 188 cameras, 301,996,032 background bytes and FNV
`E1A71005`. Ten input identities and the Windows ACP/OEM/locale identity are
unchanged; 295 executable identities are recorded. Native tools/Marcus and both
SDL profiles rebuild. Marcus is stopped before rebuilding and relaunched afterward.

No Marcus UI tests or new retail captures are added. Exact native allocation/
resource-failure ordinals, exhaustive OS error codes and extreme recursion limits
are unverified. Opaque primitives introduce native allocations/keys; their failure
paths have explicit C results. The legacy default runtime retains process lifetime
so global destructors can still use diagnostics. Explicit C callers own/destroy
their contexts. Removing that C++ default binding and the remaining native facades
is still required; the whole-engine C89 migration is incomplete.

Evidence: `tmp/c89-runtime/{core_platform.cpp,core_platform.h}`,
`abi-profiles.json`, `math-{input,output}.bin`, `math-check.log`,
`adapter-original.exe`, `linux-{build-final,runtime}.log`, `linux-identities.json`,
`error-{original,converted}.log`,
`run-sdl-debug-tests-initial-failure.log`, `forced-fileops{,-old,-fixed}.log`,
`run-{native,sdl-release,sdl-debug,standalone}-tests.log`,
`platform-{ansi,wide}-tests.log`, `asan-runtime.log`, `run-regular-tests.log`,
`trace-comparison.json`, `gates.json` and `identities.json`.
