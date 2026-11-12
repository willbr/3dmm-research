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
