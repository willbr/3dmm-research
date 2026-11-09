# Marcus

Marcus is the first step toward a new Studio interface. It is currently a
read-only, silent visual player for original 3D Movie Maker `.3MM` files.
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
| Drag in orbit view / wheel | Orbit / zoom |
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

Orbit and Fly show an amber wireframe guide for the active authored camera,
labelled with its camera number. It uses the original camera-to-world transform,
vertical field of view, background image aspect and near plane, including roll.
The outline uses a fixed display depth of 100 world units, clamped between four
times the near plane and the far plane. Actor positions never set its length;
it is a viewport guide rather than a drawing of the complete far clipping range.
The guide follows authored camera cuts when scrubbing or playing and is hidden
in the authored Camera view. It draws above the scene geometry, with segments
clipped against the viewing camera's near/far planes and viewport edges.
Orbit fits the camera guide together with visible actors where the renderer's
coordinate limits permit, once on first entering a scene's Orbit view. That
center and size remain fixed during playback, scrubbing and camera cuts, including
automatic scene changes and looping; dragging and zooming keep using the same
center. Manual scene selection and Reset explicitly fit the current frame.
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
- Original content sources are mapped from the movie source table. Unavailable
  expansion sources report their missing ID. VMM/VXP import and expansion
  installation are outside this first version.
- The renderer retains the preview API's size/decode budgets and one-session,
  one-thread ownership rule. No Studio UI/runtime library is linked.
- UI text uses FreeType and an available system font: Segoe UI on Windows,
  Arial on macOS, DejaVu/Liberation Sans on Linux. `--font file.ttf` overrides it.
  The initial glyph atlas supports Latin-1; other characters display as `?`.
  SDL bitmap text is a fallback if no font can be opened.

## C89 boundary and checks

`src/marcus/main.c` and `ui.c` implement the application and drawing in C89.
`player.h` exposes an opaque player, copied POD records, integer success results
and borrowed RGBA pixels. No C++, native platform or SDL types escape that API.
`player.cpp` owns the C++ Archive/Preview backend and catches exceptions at the
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
