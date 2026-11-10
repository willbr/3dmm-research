# Playback regression corpus

`tools/playback_regression.py` runs a hash-pinned movie corpus serially through
retail, original v3dmm, SDL Studio and Marcus on Windows. Observations and scoped
comparisons go into `playback.sqlite`; PNGs, WAVs, logs and original capture
journals stay alongside it. Nothing from the movie corpus is committed or
saved over. This is playback-quality evidence, not a declaration of parity.

The default comparison targets are **retail, v3dmm and SDL**, with SDL parity
the current priority. Add `--players retail,v3dmm,sdl,marcus` to include Marcus.

The initial generated corpus contains four two-scene movies (60 authored frames
each): general rendering/animation, actor event changes, a dissolve transition,
and sampled speech/effects with distinct quiet 600 Hz and 1300 Hz cues. The
feature catalog explicitly reports features that still lack cases. A feature
tag says the fixture exercises it; it does not say that feature passes.

## Build and prepare

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
observed authored-frame commit, seek images, Marcus presentation images, native
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
  250 ms of consecutive zero PCM, allowing startup sounds to finish. The full
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

Current gaps are explicit:

- Original/SDL continuous composed-image observers are still missing. Seeked
  images do not cover every transition/scrolling image. `continuous_pixels`
  therefore remains inconclusive, including when all seek images match.
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
