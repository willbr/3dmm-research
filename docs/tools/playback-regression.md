# Playback regression corpus

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
  policy exists. Playback Review offers same-frame phase alignment within 50 ms
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
the corresponding authored commit, with the offset visible and a 50 ms limit.
Missing or too-distant references remain unpaired. In the first SDL typography
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
numbers, absent timestamps and out-of-window references.

The coloured grid on the generated feature/dissolve actor is an intentional test
material, not a missing-texture fallback. The fixture writes texture pixels to
a `TMAP` chunk, a referencing `MTRL` chunk, and actor costume events into the
`.3mm` file. Retail loads those through its ordinary movie format; its installed
model/texture resources are not replaced.
