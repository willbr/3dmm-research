# Original v3dmm regression oracle

The retail oracle now has a `--v3dmm` profile for the original v3dmm 0.14 US
runtime. It compares an unchanged VMM against SDL Studio using:

- Absolute scene/frame seeks, native return values, actor state and viewport
  pixels, as in the existing retail harness.
- Separate **normal-speed continuous playback** passes, observing every frame
  commit and every sound request (chunk type/number, queue, frame, volume,
  repeat count, start offset, priority and class).
- Per-process recordings of the actual mixed output, including Windows MIDI
  synthesis, compared as 48 kHz stereo signed 16-bit PCM.

This is a comparison tool, not a declaration of parity. A difference, shared
seek failure, incomplete playback or missing audio capture never passes.

## Build and inputs

The reference requires an interactive Windows desktop. Process audio capture
requires Windows build **20348 or later** (including Windows 11), a working
audio device and MIDI mapper. Python **3.11+**, Pillow, NumPy and 7-Zip are
developer dependencies. Use a separate x86 MSVC shell for the bridge:

```powershell
cmake -S tools/retail_oracle -B tmp/v3dmm-oracle/build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build tmp/v3dmm-oracle/build --target retail-oracle retail-oracle-launch oracle-audio-capture oracle-audio-probe
```

Build the SDL executable with `STUDIO_TEST_TOOLS=ON`. For example, in an x64
MSVC shell, using the repository's portable SDL configuration:

```powershell
cmake --preset x64:msvc:portable-sdl-debug -B tmp/studio-oracle-release -DCMAKE_BUILD_TYPE=Release -DSTUDIO_TEST_TOOLS=ON
cmake --build tmp/studio-oracle-release --target studio
```

Release follows the original retail failure returns. Debug additionally stops
on assertions; record those as failures rather than suppressing them.

Supply the original base ISO and the extracted `Virtual3DMM` distribution
containing `System/VFS.dll` and `Patches/v3dmm.xpatch`. See
[the investigation](v3dmm-research.md#reproducing-the-investigation) for local
extraction. The harness rejects changed DLLs, an unsupported base EXE or a
patch whose expected original bytes do not match. It applies only the shipped
US executable patch, producing SHA-256
`bd1b7e6d81c23bfec5050887617e6cb4e85b375aa762c599b57fe0bec36af39f`.
The silent-startup resource patch is not applied.

Movie and package artwork, the original binaries and recorded media stay local
under ignored `tmp/`. Nothing from `movies/` is made writable or saved over.
Keep private test paths within the original engine's ANSI/255-character limits.

## Run

```powershell
python -B tools/retail_oracle/compare.py `
  --sdl tmp/studio-oracle-release/3dmovie-sdl.exe `
  --iso dist/3DMOVIE100.iso `
  --bridge tmp/v3dmm-oracle/build `
  --v3dmm tmp/v3dmm-research/unpacked/Virtual3DMM `
  --movie 'movies/Batman Sneak Peek.vmm' `
  --library '3DMM Expansion Pack=C:/local/prepared-frankie' `
  --output tmp/v3dmm-oracle/batman `
  --scenes all --frames all --audio-max-seconds 420
```

`--library NAME=FOLDER` is repeatable. Standard external names are
`Nickelodeon`, `Doraemon` and `3DMM Expansion Pack`, using source IDs 3, 4 and 5.
Both engines receive private copies of the same library files. Embedded
Portable VXPs remain byte-identical ZIPs in the reference: original PhysicsFS
mounts them directly. The port imports its own copies through its normal code.

`--audio` defaults to **required** with `--v3dmm`, and to **skip** for the
existing stock retail profile. Use `--audio skip` explicitly for a visual-only
run. Visual `--scenes` / `--frames` selections affect only the seek comparison;
an enabled audio pass always plays the **entire movie from its first frame**.
The time limit includes playback and draining any remaining movie voices.
Click pauses, persistent loops, failures and timeouts are reported; the harness
does not skip pauses or accelerate the clock.

Before Play, both sides stop pre-existing preview/UI voices. Playback uses the
original command and normal renderer/sound scheduling. Test observers do not
replace movie methods or synthesize reference events. They are bounded at
100,000 frame commits and 10,000 sound requests; overflow is a failure.

## Reports and comparison policy

`latest.json` points to a fresh run's `report.json`. The report contains input
hashes, original process identity, loaded runtime DLL paths, actual PhysicsFS
mount order, per-frame state/pixel results and the audio report. Both clients'
PNGs and a streaming `frames.jsonl` are retained. Audio artifacts include the
two WAVs, packet timestamps/flags, continuous frame/sound traces and progress
files. Failed runs preserve diagnostics and terminate only their own verified
children. Registry preferences and named semaphores are private to each child.

Pixels and PCM samples require **exact equality** by default. Actor positions
retain the retail harness's documented small floating-point tolerance. Audio
is anchored to the first engine frame commit using WASAPI's QPC timestamps.
The complete frame sequence and sound request records must also match. Frame
commit times are compared relative to the first commit, with a stated default
allowance of **25 ms** for scheduling jitter.

Native trace version 2 distinguishes the actual `FCmdRender` presentation
callback (return RVA `0x000a1131`, checked in both hash-pinned executables) from
the same callback's Play refresh and scene-preload callers. All callbacks remain
in the raw journal; only presentation commits anchor comparisons. Rebuild old
bridges: the recorder rejects the earlier trace contract, whose scene preload
could falsely appear to be a very early first frame in the next scene.

The capture helper requests 32-bit float mixer samples, then deterministically
rounds/clamps them to the stored 48 kHz stereo PCM16 WAV. This removes random
one-LSB dither introduced by Windows' PCM conversion without changing comparison
tolerances. Before Play, capture waits for 250 ms of consecutive zero PCM
(ten-second timeout), confirming quiet output. The full pre-roll and
measured wait are retained. Silence is judged at the stored PCM16 precision;
it does not prove that audible output works.

Explicit controls are `--max-different-pixels`, `--audio-sample-tolerance`,
`--audio-alignment-ms` (one global shift, default zero) and
`--frame-time-tolerance-ms`. There is no gain normalization, per-cue alignment,
time warping or omission of unmatched trailing sound. Two silent captures are
`unverified_silence`, not proof that audio works. Packet discontinuities or
invalid timestamps during playback invalidate the recording.

Use `--audio-repeat-reference` to record an additional original run. Its PCM
and cue repeatability results help distinguish host/synthesizer variation from
port differences. A non-repeatable reference does not establish a port PCM
regression, and does not yield a passing parity result. Sound IDs and cue
parameters remain useful independently of a waveform difference.

Exit codes: **0** matched; **1** differences, shared failures or unverified
sound; **2** infrastructure error. A partial visual range is labeled by its
actual frame records. Successful harness/calibration tests do not waive
failures in the parity report.

The visual phase compares absolute seeks, not every continuously presented
transition or scrolling-caption image. The audio phase observes every normal
frame commit and its timing but does not take a screenshot at each commit.
Those presentation paths need additional visual scenarios. This profile also
does not test the manager's installation UI, cached-package selection or save
dialogs; it reproduces its environment handoff to the original VFS runtime.

## Regression tests and current evidence

```powershell
python -B tests/oracle_audio_metrics_test.py
python -B tests/oracle_audio_workflow.py --bridge tmp/v3dmm-oracle/build --output tmp/v3dmm-oracle/audio-calibration
python -B tests/v3dmm_oracle_workflow.py --sdl tmp/studio-oracle-release/3dmovie-sdl.exe --iso dist/3DMOVIE100.iso --bridge tmp/v3dmm-oracle/build --v3dmm tmp/v3dmm-research/unpacked/Virtual3DMM --blank tmp/v3dmm-research/unpacked/Virtual3DMM/System/blank.vmm --vxp C:/local/prop.vxp --output tmp/v3dmm-oracle/workflow
```

The metrics tests reject silence, channel swaps, missing/moved later cues and
unmatched tails. The capture calibration runs two simultaneous wave children,
proves each PID excludes the other's tone, and separately proves native MIDI
appears in process loopback. It also checks float capture and exact-zero silent
pre-roll. The movie workflow authors a 60-frame custom-prop
movie with two quiet speech cues and checks native/port frame coverage, cue
semantics, audible recordings and unchanged source/preferences.

The standalone CMake project can register these three tests with
`V3DMM_ORACLE_INTEGRATION_TESTS=ON`, `RETAIL_ORACLE_ISO`, `RETAIL_ORACLE_SDL`,
`V3DMM_ORACLE_DISTRIBUTION`, `V3DMM_ORACLE_BLANK`, and `V3DMM_ORACLE_VXP`.
Set `Python3_EXECUTABLE` to the Python installation containing Pillow/NumPy.
These desktop/audio tests run serially and are opt-in.

On 8 November 2026, all **1,362 Batman frames across 15 scenes** had successful
native and port seeks with matching compared engine state. **66** had exact
viewport pixels; **1,296** differed. The synthetic audio workflow exposed a
serialization bug: the scene sound header used a one-byte `bool` with three
uninitialized padding bytes where retail reads a four-byte loop flag. The
header now uses `int32_t`, retaining its 16-byte layout. The workflow checks
that both engines request the two speech clips once, with matching parameters.
Rendered-pixel and mixed-PCM parity remain separate measured results, not
assumptions based on successful loading.

The final 60-frame workflow passed its harness checks, and the existing retail
workflow and core save/load checks also passed. A second original playback
retained identical sound requests but produced different PCM on this host;
the repeatability report correctly returned `unverified_reference_pcm`.
Continuous native/port frame timing also differed beyond the default allowance.
Exact mixed-audio parity is therefore not established by these recordings.

The audio backend uses Microsoft's
[process-loopback activation API](https://learn.microsoft.com/en-us/windows/win32/api/audioclientactivationparams/ne-audioclientactivationparams-audioclient_activation_type),
as demonstrated by its
[application loopback sample](https://github.com/microsoft/Windows-classic-samples/tree/main/Samples/ApplicationLoopback).
It never opens a microphone or a system-wide audio capture stream.

Startup/loading and Studio UI sounds are muted in oracle test processes.
The native bridge installs its filter before the application starts, then
allows movie sound classes 0?2 after the first ready state. SDL uses private
`MuteStartupSound` and `MuteStudioSounds` settings. The adapters verify the
policy, so rebuild old native bridges. Movie speech, effects and music remain
enabled; personal preferences are not modified. Native provenance includes
suppressed-request counts and the applied audio policy.
