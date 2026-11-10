# Playback regression corpus

`tools/playback_regression.py` runs a hash-pinned movie corpus serially through
retail, original v3dmm, SDL Studio and Marcus on Windows. Observations and scoped
comparisons go into `playback.sqlite`; PNGs, WAVs, logs and original capture
journals stay alongside it. Nothing from the movie corpus is committed or
saved over. This is playback-quality evidence, not a declaration of parity.

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
  --marcus build/marcus-x64/src/marcus/marcus-regression.exe `
  --output tmp/playback-regression `
  --environment-notes 'Record GPU/driver, audio endpoint, MIDI mapper/bank and fonts here'
```

By default every case has separate **absolute-seek** and **normal-speed playback**
passes, each repeated in two fresh processes. Seek inspection never precedes
Play in the playback process. `--case NAME` is repeatable; `--players`, `--modes`
and `--preparations` accept comma-separated selections. `--repeat 1` is useful
for a smoke run but does not establish original repeatability.

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
- Native/SDL requested sound identities, queue, volume, repeats, offset, priority
  and class; unavailable timestamps remain null.
- Per-process 48 kHz stereo signed-16-bit PCM, packet validity and an anchor
  sample tied to the first frame commit. The original oracle rejects capture
  discontinuities, timestamp errors and incomplete playback.
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
  observe the engine timeline callback. These are not identical instrumentation
  points, and image-copy overhead can affect timing. Use optimized capture
  builds when assessing performance.
- Initial movies do not cover all text, waits, transitions, audio combinations,
  expansion assets or long-run cases. `features_without_cases` lists omissions.
- Organiser preparation, click-wait input scripts, XP and Win98 capture adapters
  remain future work. SQLite observations/review do not depend on Windows APIs.

These gaps deliberately prevent a full playback-parity pass.

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
