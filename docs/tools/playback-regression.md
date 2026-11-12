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
