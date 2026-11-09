# Public archive API

Link `ThreeMM::Archive` and include `<three_mm/archive.h>`. This C++14, read-only
boundary is shared by `3mm dump` and the SDL3/ImGui inspector. Its public surface
contains standard C++ types and fixed-width identifiers; legacy engine, Kauai,
BRender and platform headers remain private implementation details.

The separate `ThreeMM::Preview` target provides headless asset rendering through
`<three_mm/preview.h>`. Linking the archive API alone does not initialize or link
the preview adapter into a consumer.

```cmake
target_link_libraries(my-tool PRIVATE ThreeMM::Archive)
```

```cpp
#include <three_mm/archive.h>

ThreeMM::Archive archive;
ThreeMM::Error error;
if (!archive.Open("movie.3mm", error)) {
    // Inspect error.code and display error.message.
    return 1;
}
for (std::size_t i = 0; i < archive.Chunks().size(); ++i) {
    ThreeMM::RecordHeader record;
    if (!archive.ReadRecord(i, record, error))
        continue;
    if (record.kind == ThreeMM::RecordKind::scene) {
        auto first = record.scene.firstFrame;
        auto last = record.scene.lastFrame;
        // Use first/last without depending on SceneData or legacy globals.
    }
}
```

## Asset previews

```cmake
target_link_libraries(my-preview-tool PRIVATE ThreeMM::Preview)
```

```cpp
#include <three_mm/preview.h>

ThreeMM::Preview preview;
ThreeMM::Error error;
if (!preview.Open("movie.3mm", "3dmovie.chk", error) ||
    !preview.AddContent(1, "content-files/tmpls.3cn", error) ||
    !preview.AddContent(1, "content-files/bkgds.3cn", error) ||
    !preview.AddContent(1, "content-files/mtrls.3cn", error) ||
    !preview.AddContent(1, "content-files/tdfs.3cn", error) ||
    !preview.Select({0x5343454e, 0}, error)) // SCEN / 0
    return 1;
ThreeMM::PreviewView view;
ThreeMM::PreviewImage image;
if (!preview.SetFrame(preview.Info().firstFrame, error) ||
    !preview.Render(view, image, error))
    return 1;
// image.rgba owns top-down RGBA8 pixels; no BRender or SDL handle escapes.
```

`Select` accepts `BMDL`, `TMPL` (actors, props and embedded 3D text), `ACTR`,
`SCEN`, and `BKGD`. Templates use the engine's costume and action readers;
`SetPose` selects an action/cel. Actor instances and scenes reuse the core route,
frame, seek and pose evaluators; `SetFrame` supports forward seeking and rewind.
Scenes resolve actors from their start events and apply authored camera changes.
`SetCamera` selects a background camera directly. `Info` exposes action names,
cel counts, frame ranges, geometry counts and camera counts.
Older 40-byte actor headers derive their missing lifetime through the engine
evaluator. Roll-call actors with no entrance remain valid and invisible.

Authored views render at the background's original raster dimensions, preserving
the camera, palette, lights and depth occlusion, then fit into the requested
image. Set `view.authoredCamera = false` for a free orbit around the evaluated
actor/prop geometry. Backgrounds are prerendered images with depth, so moving
the camera cannot reveal an arbitrary view of background geometry. Empty pixels
and letterboxing are transparent; authored background pixels are opaque.
Set `view.includeDepth = true` to also return camera-forward distances in
`image.depth`, with positive infinity for empty pixels. This lets hosts compose
transparent world-space surfaces against actor geometry without engine types.
With `frameSceneCamera`, a scene lacking a background receives a stable default
camera/display plane so its captions can still be inspected in 3D.

`Open` takes the original `3dmovie.chk` shading/palette resources and retains
the previous session on failure. `AddContent` explicitly associates each archive
with its TAG source ID; source 0 means the opened document. Missing sources fail
with their source ID and chunk identity instead of silently substituting content.
The adapter returns value types and owns all legacy resources. Inputs are opened
read-only, and neither the movie writer nor its wire formats are changed.

BRender remains process-wide: one open preview owns its runtime, on one thread,
and must not run alongside Studio's renderer in the same process. A second open
preview is rejected; `Close` permits a new session. Render buffers are limited to
64..1024 pixels on each axis. Preview archives are limited to 64 MiB stored or
decoded per chunk and 512 MiB total declared decoded bytes per archive. Scenes
accept up to 256 actors and evaluated actor lifetimes up to 100,000 frames.
These bounds do not make the legacy asset readers a complete untrusted-input
validator. Captions, sound and movie transitions are outside `Render`'s 3D asset
view. `ReadPresentation` exposes scene caption text/formatting, sound cues, pauses
and the outgoing transition as copied standard C++ records. `FrameSounds`
provides sound events for the evaluated actor frame, including motion-match
sounds. `ReadSound` resolves content/embedded child references and returns WAVE
bytes or parsed millisecond MIDI events; it never opens an audio device.
Marcus composes and schedules these records outside the renderer.

Build `preview-checks` and run `ctest -R asset-previews`. When the original content
fixtures are available, it renders all 45 actors, 21 props, 12 backgrounds and
188 cameras; checks an evaluated scene containing actors, a prop and embedded
3D text, an old actor header and an inactive actor; and verifies input hashes,
restart, failed-open isolation, frame changes and rewind. With
`BUILD_INSPECTOR=ON`, it also exercises the SDL/ImGui previews.

## Archive sessions

`Archive` owns the legacy file and decode cache through a private implementation.
It is non-copyable, closes resources on destruction, and needs no application
objects, native handles, reference-count operations or host diagnostic functions.
Failed `Open` retains the previous session. `Chunks()`/`Path()` return references
valid until a successful `Open`, `Close` or destruction. Page and record results
own their values and remain valid independently of the session.

- `Chunks()` provides sorted identities, flags, names and sorted child references.
  Missing targets are marked; callers choose whether to reject them.
- `ReadRecord()` returns typed movie, scene, actor or caption headers. `kind`
  identifies the meaningful member; unknown types return `opaque` successfully.
- `ReadPayload()` reads a caller-sized page, up to 1 MiB, retaining the full
  unpacked length. Reading beyond EOF returns an empty page at EOF.
- `ReadStrings()` returns up to 128 GST entries, including original indices,
  duplicate/empty strings and unchanged extra bytes. Reading past the last entry
  returns an empty page. Invalid GST layouts are rejected before the legacy reader.

Operations return `bool` and populate `Error` with a machine-readable `ErrorCode`,
a message and, where applicable, the chunk identity. Success clears the error;
failed reads clear their outputs. String decoding failures leave raw payload
access available. Public errors distinguish invalid arguments/paths, opening,
corrupt data, decompression, unsupported formats, resource budgets, allocation
and I/O failures where the underlying reader permits that distinction.

Private archive members provide defaults for the legacy diagnostic symbols.
They do not print or open dialogs; assertions encountered at the boundary become
operation failures. Existing hosts that define both legacy diagnostic symbols
can keep their definitions. These symbols and their calling conventions are not
part of the public header contract.

Paths and decoded text use UTF-8. The existing Windows file backend still needs
a lossless conversion to its ANSI repertoire, and the core path limit remains
255 bytes. This boundary isolates that restriction; it does not remove it.
`nameUnits` and `NameCodeUnitBits()` retain the current legacy reader's name units
for strict JSON compatibility. They are not a claim to preserve the on-disk name
encoding before the legacy reader translates it. Presentation escaping belongs
to the UI; API strings retain newlines and other valid characters.

Decompression remains whole-chunk. Set `Options::maxDecodedBytes` to cap both
the stored compressed input and decoded output; its default retains the legacy
CLI range. GST decoding has a separate 64 MiB default budget. The inspector uses
a 64 MiB decompression budget and caches the selected chunk. `3mm dump` keeps
its original schema, ordering, exact IDs, full bytes and strict failure behavior.
Neither consumer changes the `.3mm` writer or record versions.

This is an in-tree library, not an installed SDK or a playback/editing API.
Legacy process-wide state remains underneath; use one session at a time and do
not assume concurrent independent sessions are supported. SDL3/ImGui have no
dependency on the public library's implementation and are never required to
build it.

`archive-api` runs with `core-checks` on all platforms, without SDL. It tests
typed records/errors, budgets, failure isolation, raw payloads, GSTs and unchanged
input hashes. `tests/consumer` also builds `archive-consumer`, outside the root
project's directory, with no legacy includes or diagnostic replacements. Two
translation units check both include orders and reject leaked macros, compile
definitions and legacy include directories. Existing JSON fixtures continue to
check byte-for-byte dump compatibility.
