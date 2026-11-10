# Marcus

Marcus is the first step toward a new Studio interface. It is currently a
read-only movie player for original 3D Movie Maker `.3MM` files, including
sampled sound, MIDI music, 2D captions, transitions and authored pauses.
It also opens supported v1 `.vmm` bundles through the existing v3dmm library.
The dark layout has a camera viewport, object and sound dope sheets, scene object list
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
Opening a movie or changing scenes clears object and sound selection, including
scene changes during playback. Objects are selected only by explicit input,
such as a click or the `--object` command-line option.

The playback controls show the current movie position and total duration as
`m:ss / m:ss`, or `h:mm:ss / h:mm:ss` for movies lasting an hour or more.
Time spans all scenes and follows seeking, playback and scene looping. It includes
scrolling text, timed pauses, sound waits and transitions between scenes. Click
waits assume immediate acknowledgement, so time spent waiting for a click is
excluded. Timing is analyzed silently when opening a movie. With no movie loaded,
the display reads `--:-- / --:--`.

ZIP files can be opened through the same controls or passed on the command line.
Marcus searches every folder in the ZIP for `.3mm` and `.vmm` entries, ignoring
extension case. A single movie opens automatically. Multiple movies appear in a
chooser in archive order: select a row and click Open, or use Up/Down, Page
Up/Page Down, Home/End and Enter. The wheel scrolls the list; Escape cancels and
keeps the previous movie paused. The window title identifies the ZIP and selected
member. Resources/Content reloads retain that selection.

```console
marcus movies.zip --zip-movie 2 --resources /path/to/3dmovie.chk --content-dir /path/to/content-files
```

`--zip-movie` is a one-based movie-list index. Use it when combining a multi-movie
ZIP with command-line scene/frame selection or screenshot capture. The C API
provides a separate listing/selection interface; ordinary `marcus_open` opens
single-movie ZIPs and reports that an explicit selection is needed for multiples.

Listing extracts nothing. Opening extracts only the selected movie into a private
directory, verifies its declared size and CRC, and retains it while the player
needs it. Reloading an already selected entry reuses that extraction. Closing or
replacing the movie removes its private file, after renderer/content teardown.
Zipped `.vmm` files then follow the existing bundle/package loading path. Original
ZIPs remain unchanged, and extraction failures remove partial output. Entries with
unsafe names, encryption, or movie sizes above 512 MiB are rejected. A ZIP may
contain at most 100,000 members and 10,000 movie candidates.

Stored/deflated entries use the existing miniz dependency. Legacy compression
uses an installed `7z`/`7zz` executable, with standard Windows 7-Zip locations as
fallbacks. The helper runs without a console, extracts the exact selected name to
a bounded pipe, and has a 60-second timeout; Marcus still verifies size and CRC.
No 7-Zip installation is needed for ordinary stored/deflated ZIPs.

## Controls

| Control | Behaviour |
|---|---|
| Space / Play | Play or pause at the original six frames per second |
| Alt+Enter | Toggle fullscreen for the window |
| Shift+click Play | Start or continue playback with only the movie filling the window |
| D / F | Previous / next frame |
| Shift+D / Shift+F | First / last frame |
| S / G | Previous / next scene |
| Shift+S / Shift+G | First / last scene |
| Left / Right | Step one frame; Shift steps ten |
| Home / End | First / last frame of the current scene |
| Page Up / Page Down, or scene arrows | Previous / next scene |
| Four buttons in the Frame row | First / previous / next / last frame |
| Four buttons in the Scene row | First / previous / next / last scene |
| Click or drag the timeline | Scrub the current scene |
| Click or drag the scrubber beside the scene name | Scrub across the entire movie; ticks mark scene boundaries |
| Click an object name or track | Select the actor/prop and inspect its evaluated metadata |
| Objects / Sounds / Both dropdown | Choose which tracks appear in the dope sheet |
| Click a sound cue marker | Seek silently to its frame and inspect its owner, volume, loop, queue and trigger |
| Click or drag a waveform | Select its sound and scrub silently at the pointer |
| Timeline `-`, `+`, Fit | Zoom out, zoom in, fit the scene |
| Timeline wheel | Scroll tracks in the active dope sheet |
| Shift+wheel, horizontal wheel, or wheel over ruler | Pan frames |
| Ctrl+wheel over timeline | Zoom around the pointer |
| Camera / Orbit / Fly, or C | Switch authored camera / orbit / first-person flight |
| Arrow strip beside the properties panel | Expand or collapse the work camera viewport |
| Divider between viewports | Drag to resize the movie and work views |
| Handle above the dope sheets | Drag up/down to change track-area height; both sheets share the height |
| Left click in a viewport | Select the visible object; empty image space clears selection |
| Arrow beside an object track | Expand/collapse Transform, Appearance, Timing and Visibility event lanes |
| Click an animation event diamond | Select its actor and seek silently to the evaluated event frame |
| Right drag in Orbit / wheel | Orbit / zoom |
| Shift + right drag in Orbit | Pan the view |
| Hold right mouse in Fly | Capture the mouse for first-person look and movement |
| WASD while holding right mouse | Fly forward/backward and strafe left/right |
| Q / E while holding right mouse | Move down / up in world space |
| Shift while flying | Four times normal movement speed |
| Wheel in Fly | Adjust flight speed |
| Release right mouse | Release mouse capture and stop flying |
| Escape | Return from movie-only or fullscreen mode to the windowed editor; otherwise clear selection/camera capture, or pause playback |
| Reset / R | Match the current movie camera; R also fits the timeline |
| Loop | Repeat the current scene from its first frame |
| Mute / Unmute | Silence output while audio and authored sound waits keep progressing |
| Click the viewport / Play shortcut during a click wait | Continue the authored pause; the Play button reads Next |
| Settings / Ctrl+, | Open playback and keyboard shortcut settings |

**Alt+Enter** toggles fullscreen independently of the editor layout.
**Shift+click Play** starts or continues playback in a movie-only view, hiding
the editor panels and showing the authored movie camera at its original aspect
ratio with black bars as needed. It works in either a normal or fullscreen
window. Playback shortcuts and click-to-continue pauses still work. **Escape**
restores the windowed editor while keeping the current playback position and
play/pause state. The work camera and editor layout are preserved.

In the movie-only view, DVD-style controls fade in when the mouse moves and stay
visible while paused, seeking or waiting for a click. During playback they fade
out after 2.5 seconds without input. The cursor also hides after inactivity,
including while paused; moving the mouse restores it. Returning to the editor
restores normal cursor behavior.

The controls show the current scene and movie-wide elapsed / total runtime.
Click or drag the scrubber to preview frames anywhere in the movie; small ticks
mark scene boundaries. Seeking is silent and snaps to authored frames, including
frames held for scrolling text or waits. Releasing the scrubber resumes playback
if it was playing before the drag, and keeps a paused movie paused. Previous/next
scene, ten-second skip, play/pause, mute, fullscreen and editor buttons are also
available. Clicking the movie itself pauses or resumes it, or continues an
authored click wait.

The editor also has the movie-wide scrubber in the existing playback header,
beside the scene name and elapsed / total runtime. The scrubber fills the
playback header's height, with the timeline's minus, plus and Fit controls on
its right. It uses
the same scene ticks, hover-time preview and silent seeking as the expanded
view. Releasing a drag resumes a previously playing movie and keeps a paused
movie paused. The track ruler below continues to scrub within the current scene.

Each camera header has a **cog** for its own rendering settings. The movie and
work cameras save independent renderer and resolution choices. The top-right
**cog** in the movie-only view edits the movie camera's settings without leaving
the movie. Choose **BRender CPU**, **BRender compute**, or **SDL GPU** and select
a **render resolution**, then click **Apply** to save that camera's choices.
The native 3DMM movie canvas is **544x306**. Its scale presets are **1/2x
(272x153)**, **1x / 3DMM (544x306, default)**, **2x (1088x612)**, **3x
(1632x918)**, and **4x (2176x1224)**. All presets use the movie's **16:9** ratio.
Other available sizes are **320x180**, **640x360**, **960x540**, **1024x576**,
**1600x900**, **1920x1080**, **2560x1440**, **2880x1620**, **3840x2160**, and
**4096x2304**. Saved 4:3 presets migrate to the same width at 16:9, independently
for each camera. The bundled BRender build supports up to
**4096x4096** output.
These choices control geometry rasterization for the selected camera. Each
viewport, including the expanded player, preserves its selected resolution's
aspect ratio. In the movie camera, actors render at the
selected size inside the authored shot's letterbox; the original background
image and depth map scale together to preserve framing and occlusion. Background
bitmap detail remains limited by the original artwork. Applying a new size
updates that camera immediately; the expanded player uses the movie camera's
profile. The editor's Settings dialog also offers both cameras' renderer and
resolution choices. Older settings files with shared choices initialize both
cameras to those choices until you change them separately.
Opening settings pauses playback; Cancel or Escape closes the dialog and keeps
the expanded view. The cog fades
with the playback controls, and the cursor stays visible while settings are open.
Ctrl/Command+comma also opens this dialog in the movie-only view.

The **arrow strip** beside the properties panel opens an authored movie camera on the left and an independent
work camera on the right, both showing the same movie frame during playback.
Orbit and Fly keep the movie scene's authored lights in world space, so moving
the work camera does not move the lighting. Isolated assets without a background
use a preview light; reconstructed scenery retains its original baked colours.
Use Orbit or Fly in the work view; hold its right mouse button and use WASD/QE to
fly while the movie continues. C switches that work view between Orbit and Fly.
Drag the divider to resize either viewport. The strip's arrow collapses and
reopens the work camera; its pose and divider position are kept.
Only the movie view consumes viewport clicks for click-to-continue pauses.
Otherwise, left click picks actors, props and 3D text in either viewport. The
selected object is revealed in the scene list and Objects dope sheet, with a
3DMM-style wireframe bounding box in both views. The box follows the current pose
and actor rotation, using the original selection palette colour; off-stage
objects have no box. Selecting from the scene list or dope sheet shows the same
box, and clicking empty space or pressing Escape clears it. Escape also clears
selected sound cues/tracks, and clearing a selection keeps movie playback running.

Picking uses the displayed camera, evaluated model faces, texture transparency and depth;
background scenery and caption overlays block objects behind them. Backgrounds,
camera guides and 2D captions are not selectable scene objects. Movie-view
picking waits until a scene transition finishes. Right drag captures the mouse
for Orbit rotation, like Fly look; release it or press Escape to release capture.
Use `--second-view` to launch in this layout (`--orbit` starts its work view in
Orbit). These are resizable panes in Marcus's SDL interface.

The dope sheet's mode dropdown offers **Objects**, **Sounds**, and **Both**.
Both stacks an Objects section and a Sounds section on the same frame grid,
sharing the ruler, playhead, zoom and vertical scrolling. Scroll down to reach
sound tracks below the objects, or increase the track area's height with its
resize handle. Object selection and picking keep Both active; the properties
panel follows the last object or sound selected. Each mode retains its scroll
position. The dropdown supports Up/Down and Enter; Escape or clicking outside
closes it without changing the mode.

The sound sheet groups authored cues into scene and actor lanes, with separate
music, speech and effects tracks. Sampled clips use their clickable waveform
without square cue overlays. Diamonds mark motion-match bindings; sounds without
waveforms retain a thin cue line. Looping waveforms have an underline, and queue
settings are shown in the inspector. Queued sounds share their authored frame;
their eventual playback time depends on preceding sounds. Repeated clicks cycle
overlapping sounds.
Motion bindings can produce repeated sounds when the actor reaches the matching
cel. Sound identities are shown as source ID and chunk number.
Selecting or scrubbing a cue is silent; Play uses the existing audio playback.
Use `--sounds` or `--both` to open in that mode, including with `--screenshot`.

Both dope sheets mark the boundary after the final scene frame with a violet
vertical line. It follows timeline zoom and pan; hovering identifies the boundary.

Sampled sounds show a waveform from their decoded audio, scaled by cue volume;
the inspector includes the clip duration. Peaks from both stereo channels are
preserved as you zoom. Decoding is silent, with a movie-local 32 MiB
peak cache; decoded sample buffers are released after building the peaks.
The sheet follows sounds across scene boundaries, including when jumping straight
to a later scene. A teal arrow marks a carried clip; its inspector identifies the
source scene and frame. Its waveform starts at the remaining source offset,
including audio imported under a different child ID in its original scene.
Unfinished sounds and MIDI continue; sampled loops stop at the cut, matching
playback. Queued clips follow earlier sounds in their lane, replacements truncate
earlier clips, and sampled loops repeat their waveform. MIDI continuation has a
labelled band because it contains notes rather than samples.

The sheet uses a six-fps presentation clock that includes scrolling-text holds,
authored time/sound waits and outgoing transitions. Scrolling uses playback's
actual rich-text layout and scroll speed, rather than an estimate from the frame
count. Waveforms and restart positions share this clock. Click pauses have no
authored duration, so a direct jump assumes immediate acknowledgement; repeated
motion matches still follow live animation cels. These are timeline previews
rather than a rendered soundtrack.
Silent or unavailable sounds retain markers; the inspector explains unavailable
waveforms. Scene history and clip durations are cached as scenes are visited;
reading them does not seek, render prior scenes, or start audio playback.

Click a binding in Settings and press a key combination to replace it. Shift,
Ctrl, Alt and Command modifiers are supported. Escape cancels capture; Backspace
clears a binding. Duplicate combinations are rejected with the conflicting
action's name. Escape, Alt+Enter and Ctrl/Command+comma are reserved for the UI.

**Pause playback on focus loss** defaults to **Off**, so switching to another
window keeps the movie playing. Turn it on to pause when Marcus loses focus.
Focus loss always releases flight controls and stops mouse drags.

**Apply** activates and saves the edited settings; **Cancel** discards edits.
**Restore defaults** resets all settings, which still needs Apply to save.
Each first/previous/next/last action can be bound separately. Reset View defaults
to R because F is now Next Frame. Arrow/Home/End/Page Up/Page Down remain fallback
navigation keys. Flight keeps physical WASD/QE movement while right mouse is held.

The **Movie camera renderer** and **Work camera renderer** buttons in Settings
cycle between **BRender (CPU)**, **BRender (compute)** and **SDL GPU**. Apply
updates each camera and saves its choices with the other settings. Switching preserves the current
scene, frame, camera and inspection framing. Cancel discards the edited choice;
Restore defaults selects BRender CPU. Existing settings files without a renderer
entry continue to use CPU rendering.

You can override both cameras' saved renderers for a launch:

```console
marcus movie.3mm --renderer brender-compute
marcus movie.3mm --renderer sdl-gpu
marcus movie.3mm --renderer brender
```

BRender CPU remains the default. The two GPU backends use SDL3's GPU API, with
bundled SPIR-V and DXIL shaders for Vulkan and Direct3D 12. BRender still evaluates
models, transforms, lighting and clipping. **BRender (compute)** rasterizes the
resulting triangles in a compute shader, using 16-by-16 pixel tiles and ordered
depth comparisons. **SDL GPU** sends those triangles through the GPU graphics
pipeline. Both retain indexed textures, shade tables, authored background depth
and perspective scenery, then feed the common caption/transition compositor.
The SDL interface renderer is independent of this movie-renderer setting.

These are initial GPU implementations: floating-point interpolation and triangle
edge coverage can differ from the CPU renderer. Completed frames are downloaded
for the existing caption compositor and RGBA player API; performance improvements
have not been established. Metal shader binaries are not supplied. Unsupported
GPU initialization keeps the previous renderer when switching in Settings. A
saved GPU choice that cannot initialize at startup falls back to CPU with a
message in Settings; an explicitly requested `--renderer` fails with a diagnostic.

Shader sources live in `src/marcus/shaders/raster.hlsl`. Regenerate their bundled
binaries after shader changes with `python tools/compile_marcus_shaders.py`
using DXC (`--dxc path/to/dxc` selects the compiler). Building/running Marcus does
not require a shader compiler. SDL GPU resource binding conventions are described
in the [compute pipeline](https://wiki.libsdl.org/SDL3/SDL_CreateGPUComputePipeline)
and [graphics shader](https://wiki.libsdl.org/SDL3/SDL_CreateGPUShader) documentation.

Settings persist in `shortcuts.cfg` in SDL's Marcus preferences directory
(`%APPDATA%/3DMMForever/Marcus/` on Windows). The file is replaced through a sibling
temporary file when applying changes. Invalid configuration keeps defaults and
shows a message in Settings. `--settings-file path` allows an explicit profile;
`--show-settings` opens the dialog on launch.

Playback follows movie scene-child order, including scenes with no actors.
Both navigation groups use matching bar/triangle icons, with their action shown
in the status bar on hover. The Frame row sits above the Scene row, with aligned
counters and navigation buttons like retail 3DMM. A tall Play button spans both
rows beside the navigation controls, with Mute beneath Loop and the movie time
and scene name to their right.
Its layered grey bevels and shaded green triangle pay tribute to the retail transport.
The symbol switches to a red square during playback and an arrow with a bar to
continue a click wait. Hover/pressed feedback and a muted disabled state use the
same vector artwork. Buttons at the first/last boundary are disabled.

Editable transport artwork lives in `src/marcus/icons/*.svg`. Marcus draws native
SDL triangles from the checked-in `src/marcus/ui_vectors.h`, keeping the artwork
scalable without a runtime SVG loader or bitmap icon. After editing an SVG, run
`python tools/compile_marcus_vectors.py`; `--check` verifies the generated data.
The current asset compiler accepts explicit RGB fills on rectangles and convex
polygons, and rejects unsupported SVG features such as paths, strokes and
gradients. Extending the compiler is required before using those features.

With Loop off, playback advances through scenes and stops at the movie's final
frame. With Loop on, the current scene repeats, resetting its animation, captions
and audio while retaining selection, timeline zoom and work-camera position.
Authored waits and scrolling finish before the scene wraps. Focus loss pauses
playback only when enabled in Settings.
The timeline follows the playhead during playback. Frame counters and ruler
labels start at 1 in each scene, even when the movie stores negative frame
numbers. Playback and scrubbing retain the movie's original frame coordinates
internally.

Movie playback presents 2D captions in the authored Camera view, using their
stored lifetime, bounds, font sizes/styles, colours and paragraph formatting.
Text is decoded from the stored Windows, Mac or UTF-16 encoding. Missing font
families fall back to the system font. Scrolling credits hold their first frame
and move one original pixel every five 60 Hz ticks until the overflow is visible,
then playback continues. Scrubbing resets their scroll position. Orbit and Fly
render the same captions onto a transparent quad at the authored camera's amber
display frame, 10 world units in front of that camera. The quad preserves its
field of view, aspect, roll and camera cuts. It stays with the movie camera while
the inspection camera moves, and nearer actor geometry can occlude it. Both sides
are visible; text viewed from behind is mirrored. Caption lifetimes and scrolling
apply in all three views. Scenes without a background/camera use a stable default
caption plane at world Z=0, which Orbit includes when fitting the scene.

Scene and actor sound events use the movie's mapped content sources, including
embedded sounds referenced by scene child IDs. Sampled clips share Studio's
portable PCM/ADPCM/GSM/MP3-in-WAVE decoders; MIDI uses the shared SoundFont
synthesizer. Actor motion-match sounds follow evaluated animation cels. Sound
queues, looping, volume, and silent stop cues are honoured. Pausing freezes audio
and presentation clocks. Seeking stops old sounds; Play rebuilds unfinished
frame-cued scene/actor sounds, MIDI and pending queues at the selected position,
including clips from earlier scenes. Completed clips are omitted. Source offsets
include preceding scrolling, authored waits and transitions, and imported clips
resolve against their original scene. Scrubbing alone stays silent, and ordinary
pause/resume retains the existing voices rather than restarting them.
Automatic scene changes preserve music and unfinished non-looping clips, stop
looping sampled effects, and avoid restarting a MIDI clip that is already playing.

Click pauses wait for a viewport click or the Play shortcut (Space by default).
Timed pauses honour their stored duration; sound pauses wait for non-looping
voices, including queued clips, while looped music continues. The status bar
identifies the current wait or scrolling/transition phase.

Transitions use the outgoing scene's setting: cut, black, dissolve, or a
two-stage pixel dissolve through black/white. Dissolve and black/white transitions
take four seconds, matching the original timing; a black transition holds black
for one movie frame. Manual scene selection and seeking show the requested frame
immediately. Scene looping wraps directly to the current scene's first frame;
outgoing scene transitions are used when advancing normally with Loop off.

The build copies `assets/GeneralUserGS.sf2` and dependency licences beside Marcus.
Keep those folders with the executable. MIDI also accepts the configured build
bank as a development fallback. Audio output opens lazily when a sound is played;
device/decoding failures are reported through the existing error display.

Orbit and Fly initially match the movie camera's position, orientation (including
roll), lens and image. Reset returns either mode to the current movie camera.
The work view follows the shot during playback and scrubbing until you navigate;
after that it stays independent, including across automatic scene changes.
Manual scene selection also returns the work view to the new shot.

Fly starts from the currently displayed camera or orbit view. Movement follows
the look direction, with upright strafing and world-space vertical movement.
It has no collision or gravity. Movement uses elapsed time, and diagonal input
is normalized. The flight position stays fixed when scrubbing or playing,
including automatic scene changes, after you navigate. Focus loss releases the
mouse and clears movement keys. `--fly` opens directly in flight mode.

After navigation, Orbit and Fly show a compact amber camera marker for the active
authored camera, labelled with its camera number. A single rectangular frame
connects back to the camera position, with a triangle marking the top of the
image. It preserves the original camera-to-world transform, vertical field of view and background image
aspect, including roll. Its display length is fixed at 10 world units,
independent of the movie's near/far clipping planes and actor positions. No
clipping planes are drawn. The marker follows authored camera cuts when
scrubbing or playing and is hidden in the authored Camera view. It draws above
the scene geometry, with segments clipped to the viewing camera and viewport.
Orbit places its pivot in front of the matched camera, using visible actors to
choose the focus distance. After navigation that pivot stays fixed during
playback, scrubbing and camera cuts. Shift + right drag pans the pivot in the
screen plane, with speed following zoom, lens and viewport size. Subsequent
rotation and zoom use the panned pivot. Inspection views use a far clipping
distance of 30,000 world units, independent of cached actor bounds.

Orbit and Fly reconstruct the active camera's background as textured scenery
from its colour image and ZBMP depth, placed using the authored camera transform.
Cached BRender models and materials place it in the same scene render as the
actors, sharing clipping and the depth buffer, so counters, floors and walls can
occlude actors, props and caption quads.
Texture mapping preserves perspective and the background palette, without
relighting the original artwork. Both sides of the reconstructed surface are
visible. Camera mode continues to use the original image and depth directly.

Pixels with infinite depth, including skies and solid black/white backgrounds,
form a static backdrop behind the Orbit and Fly scenery. Nearby infinite pixels
extend through the finite scenery's mask, keeping the original ground and walls
out of that backdrop. It stays fixed during rotation and translation, with no
depth or selectable geometry, across all three renderers.

This is a reconstruction of one view, not the original environment model.
Depth edges are split down to individual pixels and triangles across jumps are
omitted; missing depth and surfaces hidden from the authored camera stay as
gaps. Moving far from that camera exposes those gaps. Camera cuts replace the
scenery with the new camera's reconstruction; views are not stitched together.
The two most recently inspected camera meshes are cached per loaded background.
Models are split into chunks of at most 4,096 faces to stay within BRender's
vertex/edge index limits. An opt-in opaque, unlit perspective texture material
supports arbitrary background sizes and palette index zero. Existing movie
materials keep their previous rendering paths. There is no separate scenery
rasterization pass in the preview adapter.

Object bars show **evaluated on-stage action spans**, labelled with the action
name and clipped to the scene. Gaps show off-stage intervals. Action changes,
restarts and freeze/unfreeze changes split spans; frozen spans have a distinct
tint. Actors with no entrance remain listed without a bar. Hover a span to see
its action and frame range. No extra square is drawn at its start.

The arrow beside an object expands four event lanes: **Transform** (rotation,
size, stretch, movement and path adjustments), **Appearance** (costumes and
materials), **Timing** (movement speed and animation freeze/unfreeze), and
**Visibility** (entrances/exits). Diamonds mark authored changes, with coincident
events staggered vertically. Hover for details; click to select the actor and
seek to the evaluated frame. The Visibility lane also shows on-stage spans.
Only the selected actor's event lanes expand, keeping other rows compact.

Timing is evaluated lazily and cached per actor in the current scene. A private
track copy runs through the playback sequencer, including action step distances,
size scaling and holds. Reading the sheet does not seek the movie, change its
pose, play sounds or trust cached event frame numbers. Actor sound events remain
in the Sounds sheet. Automatic cel progression does not add markers.

Lifetime properties retain the authored overall lifetime, using scene-relative
numbering clipped to the scene; objects with no overlap show "Outside scene".

Properties show roll-call name, actor/prop/3D-text kind, on-stage state, evaluated
world XYZ, actor ID, current action/cel indices, lifetime and template source/chunk.
Original coordinates are retained. Selection uses viewport picking, the list or
the timeline.

## Current limits

- Read-only playback: no editing or movie save commands.
- Actor/prop animation, costume changes, 3D text, authored cameras and
  prerendered backgrounds/depth use the existing preview evaluator.
- Caption glyphs use available host fonts, and MIDI uses a software instrument
  bank. Font substitution, rasterization and synthesis can differ from the
  original machine. Full audiovisual/image parity has not been established.
- Captions are limited to 65,536 UTF-16 units and 4,096-pixel bounds per box;
  cached glyph masks have a 128 MiB scene budget. Decoded audio has a 256 MiB
  cache budget; active voices retain their clips until stopped or completed.
- Orbit and Fly display actor geometry and scenery reconstructed from the
  active background camera's image/depth. Hidden surfaces and absent depth
  remain gaps; cameras are not combined into a complete environment.
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
`player.cpp` owns the C++ Archive/Preview backend; `playback.cpp` implements audio,
caption composition and presentation timing. `content.cpp` adapts the
shared C89 VFS to the existing pack and bundle libraries. Exceptions stop at the
C boundary. `Preview::Objects()` supplies evaluated actor state.
The audio callback uses decoded clips and synthesizers, never the renderer,
archive readers or UI. The renderer and legacy readers retain their single-thread
ownership rule.

This is **C89 application source over modern libraries**, not a strict-C89 port
of every dependency. SDL headers still use boolean/64-bit extensions; SDL and
FreeType headers are system headers during C89 checking. The backend is C++.

```console
cmake --build build/marcus --target marcus marcus-player-test marcus-playback-test marcus-content-test marcus-zip-test marcus-caption-quad-test marcus-renderer-test preview-scene-fixture
ctest --test-dir build/marcus -R "^marcus-" --output-on-failure
```

With Clang or GCC installed, `marcus-c89` checks the frontend and C API test
using `-std=c89 -pedantic-errors` and rejects mixed declarations/statements.
With original content and `marcus_resources` pointing at a valid `.chk`,
`marcus-player` generates fixtures and checks backend metadata, motion, scene
switching, camera cuts, stable framing, rewind image identity, renderer lifecycle
and invalid inputs. Input hashes verify read-only behaviour. Marcus has no UI
smoke tests or input simulations.

CTest's `marcus-playback` regression (`marcus-playback-test`) uses generated
title-only and actor scenes. It checks Unicode captions, lifetime/rewind, scrolling, authored
click/time/sound waits, pausing/resuming, queued sampled clips, audible MIDI/WAVE
output with SDL's dummy audio device, motion-match precedence, mute, outgoing
transitions and looping. It also verifies unchanged movie/resource bytes.
Its caption checks also cover Orbit/Fly visibility, movement relative to the
inspection camera, title-only scenes, lifetime and authored camera cuts.

`marcus-caption-quad` checks perspective mapping, alpha blending, actor-depth
occlusion, camera roll, back faces and near/far clipping without a window. The
preview scene regression checks optional camera-forward depth and rewind identity.
The background regression also requests depth without actors and requires finite
authored distances, catching a missing ZBMP copy independently of actor rendering.
It renders all 188 shipped cameras in Orbit and Fly, verifies unchanged authored
images and deterministic reconstruction after cache eviction. All 184 cameras
with finite depth produce visible scenery in both inspection modes.
The 68 cameras containing infinite pixels retain opaque static backdrops; fully
infinite backgrounds stay identical when the inspection camera moves or turns.
`preview-background-mesh` checks reconstruction, analytic perspective texture
mapping, camera transforms/parallax, palette index zero, depth ordering, back
faces, missing depth, backdrop mask extension, discontinuities, clipping and
padded buffer strides without a window. These checks run through BRender's scene pipeline, including a
1024-by-1024 depth fixture whose 131,072 triangles require multiple cached models;
rebuilding the mesh releases its model, material and map registrations.
`marcus-renderer` checks both GPU pipelines without creating an application
window: flat shading, indexed/pitched textures, shade tables, perspective UVs,
zero transparency, opaque palette index zero, authored depth, equal-depth
submission order, frame clearing, padded output rows, independent camera settings
and migration from shared settings.
It reports a CTest skip if neither GPU backend can initialize. To also exercise
loaded-movie switching and Camera/Orbit/Fly rendering, pass a movie, resources
and content directory directly to `marcus-renderer-test`. The generated
`src/marcus/test-data/marcus.3mm` from `marcus-player` is a suitable fixture.
The integration checks also alternate backends and resolutions between camera
views, including reuse of initialized GPU devices. These checks do not drive
the Marcus UI.

Both GPU backends have passed these native checks on Windows x64 through
Direct3D 12 and Vulkan, including loaded-movie switching and retaining the CPU
renderer/movie after an unavailable-driver error. Headless captures of the diner
in `I Know compilation 1`, scene 1/frame 49, were also compared in Camera and
Orbit views; actor/counter occlusion and reconstructed scenery remain visible.
This is selected-frame evidence, not pixel equality or a full movie parity audit.
The diner at scene 1/frame 49 of `iknow1.zip` was also inspected in
Orbit and moved Fly views; local captures are in `tmp/iknow-depth-check/mesh-*.png`.

On 9 November 2026 moving-camera headless benchmarks of that diner at 960-by-720
compared the previous separate rasterizer with the shared BRender scene path.
Across 30 changing poses per mode in the same x64 Debug build, mean Orbit render
time fell from 70.0 to 34.0 ms (14.3 to 29.4 frames/second), and Fly from 93.9 to
51.1 ms (10.6 to 19.6 frames/second). These are native render timings, excluding
UI/presentation overhead, and are not a general performance guarantee. Local
benchmark source, raw timings and before/after captures are under
`tmp/brender-scenery/`.

On 9 November 2026, `iknow1.zip` / `I Know compilation 1.3mm`, scene 1,
frame 49 was checked in the unchanged retail US executable using the retail
oracle (successful seek, no engine errors). Retail correctly hides Gustave behind
the diner counter. Marcus had passed no clip rectangle to `ZBMP::Draw`, which
returns without copying when both clip arguments are absent. Supplying the full
viewport clip restores that occlusion in the same frame. The new background
regression fails before this fix and passes afterward: 184 of the 188 shipped
cameras return finite camera-forward distances in the background-only check.
Local captures are under `tmp/iknow-depth-check/`. This is a visual occlusion
comparison, not a claim of pixel equality across the two renderers.

`marcus-zip` checks nested and duplicate movie entries, explicit selection, cached
extraction leases, CRC and size rejection, cleanup, zipped VMM content and unchanged
inputs. The C API player regression also opens generated single/multi-movie ZIPs,
renders after the input-list object is destroyed, and verifies original ZIP hashes.
These checks are headless; they do not drive the chooser or other UI controls.

On 9 November 2026 the native ZIP input extracted all 103 movie entries from the
87 ZIPs containing native movies in `movies`, including the Shrink-compressed
`aacw01.zip`. All selected entries passed size/CRC checks, original ZIP bytes
remained unchanged, and temporary directories were removed. Two other ZIPs had
no `.3mm`/`.vmm` entries. This verifies extraction, not playback of every movie.

On 9 November 2026 all six Marcus checks passed, along with the headless
preview regressions for 45 actors, 21 props, 12 backgrounds and 188 cameras.
An additional full visual pass of `TheSnowman` rendered all 3,383 frames in 145
scenes. Sampling the first five frames of every scene in `TheSnowman`, `Trenches`,
`I Know Xmas Compilation` and `Liberation Day Teaser Trailer` exercised 129 WAVE
voice starts and 20 MIDI voice starts without decoding errors. Sampling is not a
full audiovisual playback comparison.

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
check image correctness, audio output, scrolling timing or transitions. Current
builds composite the captions visible at each seek; the historical coverage
figures below predate caption presentation.
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
