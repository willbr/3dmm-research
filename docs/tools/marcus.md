# Marcus

Marcus is the first step toward a new Studio interface. It is currently a
read-only, silent visual player for original 3D Movie Maker `.3MM` files.
It also opens supported v1 `.vmm` bundles through the existing v3dmm library.
The dark layout has a camera viewport, per-object timeline, scene object list
and properties pane. The existing Studio application is unchanged.

## Build and launch

Use an x64 MSVC development shell on Windows, or a C/C++ build environment on
macOS/Linux. Windows x64 has been verified; macOS/Linux are not yet runtime-tested
for Marcus.

```console
cmake -S . -B build/marcus -G Ninja -DCMAKE_BUILD_TYPE=Debug -DBUILD_MARCUS=ON -DBUILD_STUDIO=OFF -DBUILD_PACKAGES=OFF
cmake --build build/marcus --target marcus
```

The executable is `build/marcus/src/marcus/marcus` (`marcus.exe` on Windows).
The current development build in this workspace uses `build/marcus-x64`.

```console
marcus movie.3mm --resources /path/to/3dmovie.chk --content-dir /path/to/content-files
```

Resources must contain the original `GLCR/0` palette and `TMAP/0` shade table.
A compiled `utest.chk` from a Studio build can also supply them. The content
directory should contain `tmpls.3cn`, `bkgds.3cn`, `mtrls.3cn` and `tdfs.3cn`.
Marcus looks for `3dmovie.chk` beside the executable or in the working directory,
then tries known Studio build paths in the source tree. It looks for
`content-files` beside the executable, then in the source tree. These are
development conveniences; CLI flags and Resources/Content buttons allow explicit
paths. Original content is not bundled or downloaded by Marcus.

Open a movie with **Open movie**, **Ctrl+O**, or drag and drop. Selecting
Resources or Content reloads that movie.

## Controls

| Control | Behaviour |
|---|---|
| Space / Play | Play or pause at the original six frames per second |
| D / F | Previous / next frame |
| Shift+D / Shift+F | First / last frame |
| S / G | Previous / next scene |
| Shift+S / Shift+G | First / last scene |
| Left / Right | Step one frame; Shift steps ten |
| Home / End | First / last frame of the current scene |
| Page Up / Page Down, or scene arrows | Previous / next scene |
| Four buttons before Play | First / previous / next / last frame |
| Four buttons beside the scene info | First / previous / next / last scene |
| Click or drag the timeline | Scrub the current scene |
| Click an object name or track | Select the actor/prop and inspect its evaluated metadata |
| Timeline `-`, `+`, Fit | Zoom out, zoom in, fit the scene |
| Timeline wheel | Scroll object tracks |
| Shift+wheel, horizontal wheel, or wheel over ruler | Pan frames |
| Ctrl+wheel over timeline | Zoom around the pointer |
| Camera / Orbit / Fly, or C | Switch authored camera / orbit / first-person flight |
| Left drag in Orbit / wheel | Orbit / zoom |
| Right drag in Orbit | Pan the view |
| Hold right mouse in Fly | Capture the mouse for first-person look and movement |
| WASD while holding right mouse | Fly forward/backward and strafe left/right |
| Q / E while holding right mouse | Move down / up in world space |
| Shift while flying | Four times normal movement speed |
| Wheel in Fly | Adjust flight speed |
| Release right mouse / Escape | Release mouse capture and stop flying |
| Reset / R | Reframe visible geometry; R also fits the timeline |
| Loop | Repeat the movie after the final scene |
| Settings / Ctrl+, | Open playback and keyboard shortcut settings |

Click a binding in Settings and press a key combination to replace it. Shift,
Ctrl, Alt and Command modifiers are supported. Escape cancels capture; Backspace
clears a binding. Duplicate combinations are rejected with the conflicting
action's name. Escape and Ctrl/Command+comma are reserved for the UI.

**Pause playback on focus loss** defaults to **Off**, so switching to another
window keeps the movie playing. Turn it on to pause when Marcus loses focus.
Focus loss always releases flight controls and stops mouse drags.

**Apply** activates and saves the edited settings; **Cancel** discards edits.
**Restore defaults** resets all settings, which still needs Apply to save.
Each first/previous/next/last action can be bound separately. Reset View defaults
to R because F is now Next Frame. Arrow/Home/End/Page Up/Page Down remain fallback
navigation keys. Flight keeps physical WASD/QE movement while right mouse is held.

Settings persist in `shortcuts.cfg` in SDL's Marcus preferences directory
(`%APPDATA%/3DMMForever/Marcus/` on Windows). The file is replaced through a sibling
temporary file when applying changes. Invalid configuration keeps defaults and
shows a message in Settings. `--settings-file path` allows an explicit profile;
`--show-settings` opens the dialog on launch.

Playback follows movie scene-child order, including scenes with no actors.
Both navigation groups use matching bar/triangle icons, with their action shown
in the status bar on hover. Buttons at the first/last boundary are disabled.
It stops at the final frame unless Loop is enabled. Focus loss pauses playback
only when enabled in Settings.
The timeline follows the playhead during playback. Frame counters and ruler
labels start at 1 in each scene, even when the movie stores negative frame
numbers. Playback and scrubbing retain the movie's original frame coordinates
internally.

Fly starts from the currently displayed camera or orbit view. Movement follows
the look direction, with upright strafing and world-space vertical movement.
It has no collision or gravity. Movement uses elapsed time, and diagonal input
is normalized. The flight position stays fixed when scrubbing or playing,
including automatic scene changes. Manual scene selection and Reset reframe the
geometry. Focus loss releases the mouse and clears movement keys. `--fly` opens
directly in flight mode.

Orbit and Fly show a compact amber camera marker for the active authored camera,
labelled with its camera number. A single rectangular frame connects back to the
camera position, with a triangle marking the top of the image. It preserves the
original camera-to-world transform, vertical field of view and background image
aspect, including roll. Its display length is fixed at 10 world units,
independent of the movie's near/far clipping planes and actor positions. No
clipping planes are drawn. The marker follows authored camera cuts when
scrubbing or playing and is hidden in the authored Camera view. It draws above
the scene geometry, with segments clipped to the viewing camera and viewport.
Orbit fits the camera guide together with visible actors where the renderer's
coordinate limits permit, once on first entering a scene's Orbit view. That
center and size remain fixed during playback, scrubbing and camera cuts, including
automatic scene changes and looping; rotating and zooming keep using the same
center. Right drag pans that center in the screen plane, with speed following
zoom and viewport size. The scene follows the drag; subsequent rotation and zoom
use the panned center. Manual scene selection and Reset explicitly fit the
current frame.
Fly retains its explicit position and uses the same fixed scene reference for
its clipping range; actor movement does not change that range while the flight
camera is stationary.

Object bars show **overall lifetime**, clipped to the current scene. They are
not individual animation keyframes and do not show every offstage interval.
Actors with no entrance remain listed without a bar. Lifetime properties use the
same scene-relative numbering, clipped to the scene; objects with no overlap show
"Outside scene".

Properties show roll-call name, actor/prop/3D-text kind, on-stage state, evaluated
world XYZ, actor ID, current action/cel indices, lifetime and template source/chunk.
Original coordinates are retained. Selection uses the list or timeline;
viewport geometry picking is not yet implemented.

## Current limits

- Visual playback only: no audio, editing or save commands.
- Actor/prop animation, costume changes, 3D text, authored cameras and
  prerendered backgrounds/depth use the existing preview evaluator.
- **2D captions, transitions and click/time/sound pause events are not yet
  presented.** Scenes play continuously with cuts. A title-only scene may appear
  blank. This is not yet a complete presentation of every movie.
- Orbit and Fly display actor geometry. Backgrounds are prerendered images,
  rather than explorable 3D scenery. Orbit automatically frames visible bodies;
  Fly uses an explicit world-space camera position.
- Base content, enabled Nickelodeon/Doraemon/Frankie libraries and portable VXP
  content share the C89 content VFS used by Studio. Movie source IDs are mapped
  by recorded product names, rather than assuming the installed numeric IDs.
  Marcus reads Studio's existing expansion preferences and managed pack store;
  `THREEDMM_PACK_DIR` selects the same optional store override. It also recognizes
  prepared expansion folders beside the selected base content.
- Supported v1 `.vmm` bundles use their declared package selection. Installed
  packages are reused by name; repeated embedded names also require a matching
  internal shortname (the CFG basename). Each declaration is retained in bundle
  order, so two distinct packages named `Stage` can both contribute content.
  Remaining embedded packages and the inner movie
  are materialized privately for that session. Ordinary `.3mm` movies use the
  installed portable defaults when they reference Virtual3dmm. Marcus does not
  install packages, edit Studio preferences or write back to movie inputs.
  Disabled/missing required libraries and unsupported package forms remain errors.
- The renderer retains the preview API's size/decode budgets and one-session,
  one-thread ownership rule. No Studio UI/runtime library is linked.
- UI text uses FreeType and an available system font: Segoe UI on Windows,
  Arial on macOS, DejaVu/Liberation Sans on Linux. `--font file.ttf` overrides it.
  The initial glyph atlas supports Latin-1; other characters display as `?`.
  SDL bitmap text is a fallback if no font can be opened.

## Older movies with expansion assets in source 2

Some older movies reference Doraemon or Nickelodeon assets as part of
`3D Movie Maker/3DMovie` (source 2). These require additional archives in the
base content folder. Installing the same library under its separate source
does not satisfy those references. Original v3dmm also fails with that layout;
there is no general automatic search across sources.

Prepare a separate content folder from local base/expansion files:

```console
python tools/prepare_legacy_content.py content-files tmp/legacy-content --overlay /path/to/doraemon --overlay /path/to/nickelodeon
marcus movie.3mm --resources /path/to/3dmovie.chk --content-dir tmp/legacy-content
```

Use the prepared, palette-compatible Nickelodeon expansion rather than its raw
standalone-game files. Select only the libraries needed for the older setup.
The tool copies `.3cn` and `.3th` files, records SHA-256 source identities in
`legacy-content.json`, and requires a new output folder. It rejects colliding
filenames before copying. Movies, installed libraries and source directories
are unchanged. Resources remain a separate `--resources` argument.

On 9 November 2026, a base-plus-Doraemon folder completed all 2,036 frames of
`Lethal Intention 2` and 17,088 frames of `JDR Revival`. A folder with both
Doraemon and Nickelodeon also completed those movies and all 14,509 frames of
`THE BIG MOVIE II`. With the earlier 145 complete results under the usual
content configuration, this establishes visual completion for 148/150 unique
movies across the tested configurations. This does not claim audiovisual or
image parity. The remaining native movie needs `TDF /00000005`; the other
unsupported input is a WebM video.

Excluding the WebM, this is 148/149 native movies across the tested content
configurations. Original v3dmm 0.14 was also checked on `The selected battle
person` with the unchanged movie and the base plus three separate libraries.
Native seeks succeed at scene 10/frame 1 and scene 17/frame 1, but fail at
scene 10/frame 32 and scene 16/frame 1 (stored frame 69), matching Marcus's two
missing-font fault points. This is selected-seek evidence, not full playback;
the additional `TDF /00000005` font is still unavailable in the checked content.

## C89 boundary and checks

`src/marcus/main.c` and `ui.c` implement the application and drawing in C89.
`player.h` exposes an opaque player, copied POD records, integer success results
and borrowed RGBA pixels. No C++, native platform or SDL types escape that API.
`player.cpp` owns the C++ Archive/Preview backend, while `content.cpp` adapts the
shared C89 VFS to the existing pack and bundle libraries. Exceptions stop at the
C boundary. `Preview::Objects()` supplies evaluated actor state.

This is **C89 application source over modern libraries**, not a strict-C89 port
of every dependency. SDL headers still use boolean/64-bit extensions; SDL and
FreeType headers are system headers during C89 checking. The backend is C++.

```console
cmake --build build/marcus --target marcus marcus-player-test preview-scene-fixture
ctest --test-dir build/marcus -R "^marcus-" --output-on-failure
```

With Clang or GCC installed, `marcus-c89` checks the frontend and C API test
using `-std=c89 -pedantic-errors` and rejects mixed declarations/statements.
With original content and `marcus_resources` pointing at a valid `.chk`,
`marcus-player` generates fixtures and checks backend metadata, motion, scene
switching, camera cuts, stable framing, rewind image identity, renderer lifecycle
and invalid inputs. Input hashes verify read-only behaviour. Marcus has no UI
smoke tests or input simulations.

For a manual capture, scenes start at 1 and object indices at 0:

```console
marcus movie.3mm --scene 2 --frame 10 --object 1 --orbit --screenshot marcus.bmp
```

`--frame` uses the same numbering as the player: 1 is the scene's first frame.

`--screenshot` exits after capturing the window and returns a failure status on
load or render errors.

For a directory-wide playback audit, explicitly build the headless C89 tool in
a Release configuration and run the Python wrapper:

```console
cmake -S . -B build/marcus-release -G Ninja -DCMAKE_BUILD_TYPE=Release -DBUILD_MARCUS=ON -DBUILD_STUDIO=OFF -DBUILD_PACKAGES=OFF
cmake --build build/marcus-release --target marcus-playback
python tools/check_marcus_movies.py movies --exe build/marcus-release/src/marcus/marcus-playback.exe --resources /path/to/3dmovie.chk --content-dir content-files --output tmp/marcus-playback
```

The wrapper includes movies inside ZIP/RAR archives, tests identical bytes once,
and runs read-only copies in isolated processes. RAR and older ZIP compression
methods require 7-Zip. Every frame is evaluated and rendered in the authored
camera at 480 x 360, without playback pacing or UI input simulation. A failed
scene is recorded and the tool continues with subsequent scenes. This does not
check image correctness or the unimplemented audio/caption/transition features.
Supported VMM bundles are included; the audit does not install expansion content.

`summary.txt` lists completed, incomplete and unsupported movies. `report.json`
contains exact errors, frame coverage and source aliases; per-movie journals
retain the last scene/frame before a crash or stall. Original file hashes are
checked after the run. `--workers` controls parallel processes. `--resume` reuses
completed results in that output directory; add `--retry-failures` to retry
failed inputs. Use a new output directory to repeat the entire audit.

On 9 November 2026, the VFS audit completed 141 of 150 unique movies. Matching
the original PhysicsFS duplicate-ZIP lookup then unblocked `Derelict.vmm`,
`magic2.vmm` and `The Sniper @ The 3DMM BB.vmm`: targeted headless retests
rendered all 20,040, 9,008 and 5,269 frames respectively. Together with the
unchanged earlier results, this raises visual completion to 144/150. Five
movies still have missing assets and one input is a WebM. This is frame
coverage evidence, subject to the presentation limits above.

Preserving repeated package names subsequently completed all 53 scenes and
4,899 frames of `A nightmare at freddys.vmm`, bringing combined coverage to
145/150. Its later `Stage` package contains `TMPL/af216bae`, which was previously
skipped because an earlier, different package has the same display name.
`marcus-content` is a native adapter regression for same-name packages with
different CFG basenames, installed-package reuse, ordered filename collisions,
invalid metadata, cleanup and unchanged inputs/settings. It does not drive UI.
