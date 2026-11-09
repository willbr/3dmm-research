# 3mm Inspector

A read-only SDL3 / Dear ImGui desktop browser for `.3mm`, `.3cn` and other
chunky archives. It uses the same archive readers as Studio and `3mm dump`.
It also renders actors, props, 3D models and scenes through `ThreeMM::Preview`.

Build from an initialized MSVC shell on Windows, or a C/C++ build environment
on macOS/Linux:

```console
cmake -S . -B out/inspector -DCMAKE_BUILD_TYPE=Debug -DBUILD_INSPECTOR=ON -DBUILD_STUDIO=OFF -DBUILD_PACKAGES=OFF
cmake --build out/inspector --config Debug --target three-mm-inspector --parallel 4
ctest --test-dir out/inspector -C Debug -R inspector-data --output-on-failure
```

With Ninja or Makefiles the executable is
`out/inspector/src/inspector/3mm-inspector` (`.exe` on Windows). Visual Studio
generators add `Debug/` before the executable name. Pass an archive path on the
command line, use **Browse**, paste a path and press **Open**, press **Ctrl+O**,
or drop a file into the window.

The left pane shows descriptive chunk types such as **General String Table (GST)**
and **Movie (MVIE)**, retaining the original codes. Filters match descriptions,
codes, hexadecimal IDs and names. Unknown types retain their raw codes. Select a row to
view flags, stored/unpacked sizes and decoded movie, scene, actor or caption
headers. Child references are buttons that navigate to the target chunk. IDs
are retained exactly; repeated targets and cycles do not require recursive
tree expansion. The payload view shows 4096-byte pages with hexadecimal bytes
and ASCII. Packed data is decoded using the core codec and cached for the
selected chunk. Packed previews are limited to 64 MiB of stored/decoded data;
uncompressed data is read in pages. Unknown payloads remain inspectable.

**General String Table (GST)** chunks also have a **String entries** view. It
shows the original zero-based entry indices, decoded strings and extra bytes
as hex. Duplicate and empty strings remain separate rows; newlines, tabs,
backslashes and other control characters are escaped for display. GST's legacy
Pascal strings forbid embedded null characters, so those are reported as an
invalid table with the raw bytes still available.

Entry pages contain up to 128 rows and use separate Previous/Next buttons from
the hex view. Extra data remains opaque and retains its original byte order;
the table shows up to 64 extra bytes per row (hover for the full preview), while
the payload view retains all bytes. Structural checks precede the core reader,
and structured GST previews are limited to 64 MiB. Invalid or unsupported tables
show an explanation while leaving the raw hex view available.

Files are opened without write permission. There are no save or mutation
commands. A failed open leaves the previous archive available, and a failed
payload decode leaves the chunk index available. This is an inspector, not
a complete movie validator or editor.

Selecting a `TMPL`, `BMDL`, `ACTR`, `SCEN` or `BKGD` opens its **3D view** tab.
The **Chunk data** tab retains the original fields, child references and hex.
Drag over an actor/prop preview to orbit, use the wheel to zoom, and use
**Reset view** to reframe it. Templates have action and cel controls. Actor
instances and scenes have a frame slider; scenes also expose their background
cameras and an **Authored camera and background** checkbox. Embedded 3D text
resolves its font through the same explicit content sources.

The authored scene view uses its camera, palette, lighting and background depth
for actor occlusion. Orbiting includes the actor/prop geometry and a textured
background mesh reconstructed from the active camera's image and depth. Moving
away from the authored camera exposes gaps where depth is missing or surfaces
were hidden; the reconstruction does not combine multiple cameras.
The scenery is cached as BRender models with opaque, perspective-correct
textures and rendered in the same scene/depth buffer as actors and props.
Background chunks support their stored camera views. This is a static frame
inspector; it does not play sound, draw captions or perform movie transitions.

The **Resource locations** section accepts the path to `3dmovie.chk` and the
original source 1 content folder containing `tmpls.3cn`, `bkgds.3cn`, `mtrls.3cn`
and `tdfs.3cn`. The inspector finds the repository's resources automatically
when launched from the repository or its normal build directory. Missing
content reports its source ID and chunk identity; raw chunk inspection remains
available. Other source IDs can be registered through the public library API.

```console
3mm-inspector --resources "path/to/3dmovie.chk" --content-dir "path/to/content-files" --select SCEN:0 movie.3mm
3mm-inspector --select TMPL:2010 content-files/tmpls.3cn
```

`--select` uses the four-character type and hexadecimal chunk number. In smoke
mode an explicit selection must produce a rendered preview, or the check fails.

The UI accepts UTF-8. Windows paths are checked for a lossless conversion to
the core's existing ANSI file API; unsupported paths report an error instead
of opening a substituted filename. The existing 255-byte core path limit
still applies. Names use the core's legacy text conversion policies.

`BUILD_INSPECTOR` defaults to `OFF`. Enabling it downloads SHA256-pinned SDL
3.4.8 and Dear ImGui 1.92.5 source archives through CMake FetchContent; SDL is
linked statically. Core-only builds do not download or link either dependency.
The upstream [SDL renderer example](https://github.com/ocornut/imgui/tree/v1.92.5/examples/example_sdl3_sdlrenderer3)
documents the backend pairing used here. Dependency licenses remain in their
downloaded source directories (`SDL/LICENSE.txt`, `imgui/LICENSE.txt`);
redistributed binaries must include these notices.

`inspector-data` is a presentation adapter over the shared
[`ThreeMM::Archive`](../../libraries/archive/README.md) API. The archive library
owns all legacy objects, decoding, text conversion and diagnostic defaults.
The adapter formats typed records for display; it includes no legacy headers.
The `three-mm-inspector` application owns SDL events, file dialogs, ImGui and
rendering. It links no Studio/Kauai GUI runtime and adds no SDL types to core.
`ThreeMM::Preview` separately owns the BRender runtime and returns RGBA pixels;
its public headers expose only standard C++ types. Template, pose, costume,
resource resolution and scene rendering stay behind that boundary.

CTest checks archive queries, packed payloads, failed-open isolation and exact
input preservation. GitHub workflow files remain disabled locally.
GST fixtures cover empty/duplicate strings, extra data, entry paging, compressed
opposite-endian tables, UTF-16 and malformed headers/offsets/strings.
The SDL dummy video driver and software renderer run eight ImGui frames and
write a screenshot, so the startup/render path is checked without a display:

```console
3mm-inspector --smoke-test --screenshot preview.bmp movie.3mm
```

Smoke mode exits after eight frames, returns failure for an invalid input, and
can also render the empty state when no path is supplied. macOS/Linux GUI
runtime results require their CI runners; local Windows results do not imply
that native dialogs or hardware rendering were tested on those systems.
