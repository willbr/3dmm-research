# Marcus

Marcus is the first step toward a new Studio interface. It is currently a
movie player with initial scene and prop authoring tools for original 3D Movie Maker `.3MM` files, including
sampled sound, MIDI music, 2D captions, transitions and authored pauses.
It also opens supported v1 `.vmm` bundles through the existing v3dmm library.
The dark layout has a camera viewport, object and sound dope sheets, scene object list
and properties pane. The existing Studio application is unchanged.

Imported templates can use sparse costume child IDs, such as 0 and 2. The shared
engine resolves those IDs directly instead of asserting against a contiguous
costume count. This fixes Batman Sneak Peek stopping at scene 1/frame 3 in debug
builds. A headless native regression covers applying a sparse costume and looking
up a missing ID. The rebuilt Marcus sought and rendered all 1,362 frames across
the movie's 15 scenes with zero errors; the local journal is
`tmp/batman-playback-fixed.jsonl`. Error dialogs also retain the initiating failure
when later redraw attempts fail.

Playback compatibility targets retail 3D Movie Maker, with v3dmm as an additional
comparison baseline. Marcus should reuse the SDL port's shared engine policy
rather than independently approximate it. That policy can be migrated to C
behind its existing boundaries without postponing behavior fixes.

Scrolling now uses a portable adapter to the same `MoviePlaybackEvaluator`
scrolling branch used by SDL Studio. It moves each eligible credits box one
pixel per five-tick alarm (nominally 12 steps/second), discards late-alarm debt,
and holds the authored frame through the final completion-check alarm. Pausing
freezes the remaining interval; seeks reset it. Caption line layout now shares
the SDL port's rich-text formatter; font rasterization and the remaining
presentation policies still require parity verification.
Timeline durations describe nominal scheduling; a slow host can take longer.

Marcus and SDL now use `kauai/src/rich_text_layout.h` for character runs, tabs,
word wrapping, paragraph alignment/spacing and baseline calculation. The original
formatter was extracted from `rtxt2.cpp` behind document/font measurement hosts;
Marcus no longer approximates it with a separate per-character layout loop.
Blank lines and paragraph-ending characters contribute actual font metrics.
The portable font catalog also preserves native bitmap strikes instead of
doubling the missing-font System fallback; enlarged caption rendering preserves
those pixels while outline fonts can still be rerasterized at higher resolution.

Native 544x306 Stranger captures verified the title (scene 4/frame 15) and ending
(scene 46/frame 55) as exact RGB matches to retail across BRender CPU, BRender
compute and SDL GPU. Warning text at scene 1/frame 3 dropped from 5,601,450 to
1,602 summed absolute RGB channel error (99.97% reduction); a few glyph pixels
still differ. These are still-frame observations, not continuous-playback parity.
Legacy synthetic bold now uses retail's one-pixel horizontal overstrike and
advance adjustment instead of FreeType's size-dependent emboldening. In Gustave,
this prevents the Impact title from wrapping or clipping: scene 3/frame 5's text
region drops from 5,848,042 to 9,130 summed RGB error across all renderers, and
scene 5/frame 19's upper region drops from 1,028,240 to 5,036 with BRender CPU.
Overlapping captions now follow SDL/retail `TextBox::FGotoFrame`: a box moves to
the front when it becomes visible, while surviving boxes keep their order across
seeks. A fixed caption-list order previously let older text or blank boxes cover
newer text. Rebirth of 3DMM scene 2/frame 7 (stored frame 6), captured in order,
drops from 692,374 to 6,176 summed RGB error with both CPU backends. Headless
checks cover staggered activation, rewind, direct seeks and retained stacking.
Headless shared-formatter checks cover CRLF/blank lines, styled paragraph ends,
tabs, wrapping, spacing and UTF-16 buffer movement. No Marcus UI test was added.

The headless playback regression covers first-alarm delay, a ten-second late
callback moving only one pixel, no catch-up debt, the final completion-check
alarm, reset/no-overflow behavior, and pause/resume without a scrolling jump.
The existing shared evaluator tests also pass. No Marcus UI test is involved.

Windows 11 capture `tmp/text-acid-shared-scroll-results/playback.sqlite`
(suite `20261111T100144Z-6de14c55`) records the rebuilt Marcus and retail playing
Text Acid: both complete all 240 authored frames, with 1,292 Marcus and 610
retail presentation observations. The scene-index-2/frame-3 hold measures
11,508.87 ms in Marcus versus 11,081.91 ms in retail. Frame timing still differs
and continuous visual parity remains inconclusive. This single-repeat capture
does not establish repeatability or eliminate the remaining layout/pacing gaps.

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

Marcus uses the original media automatically when it finds
`dist/3DMOVIE100.iso` in the workspace. It also picks up
`dist/DORAEMON100.iso`, the community Nickelodeon expansion at
`expansions/n3dmmexpb1a-1.zip`, and Frankie's Expansion Pack 1 at
`expansions/3dmmexp1v018.zip` when present. For a standalone layout, put
the media files beside the executable. To select files explicitly:

```powershell
.\build\marcus-x64\src\marcus\marcus.exe --iso .\dist\3DMOVIE100.iso --doraemon-iso .\dist\DORAEMON100.iso --expansion-zip .\expansions\n3dmmexpb1a-1.zip --frankie-zip .\expansions\3dmmexp1v018.zip
```

Marcus uses 7-Zip to read the content archives inside each community ZIP's
NSIS payload as data. To use the reconstructed
Nickelodeon expansion instead, select the retail disc and matching fixups:

```powershell
.\build\marcus-x64\src\marcus\marcus.exe --iso .\dist\3DMOVIE100.iso --nick-iso .\dist\NICK3DMM.ISO --nick-fixups .\expansions\nickelodeon-image-fixups.zip
```

In media mode, the VFS mounts the selected base game and expansion files.
Marcus prepares a private cache for the legacy seekable
archive readers and shows the original media files in **Settings**. Source
files stay read-only. Use `--content-dir` to select the folder-based mode,
which uses Studio's managed expansions.

SDL 3.4.8 is built from `vendor/SDL3` by the shared dependency module, including
for Studio and the inspector. Marcus's transform controls use the vendored
ImGuizmo core in `vendor/ImGuizmo` with the existing pinned Dear ImGui dependency.
The complete Marcus application UI uses cimgui 1.92.5, pinned at
`bfd30140a9c5832b5e0dcf179d6e1e5c69373d5a`, over that same ImGui implementation
and context. Configuration checks that the bindings' ImGui headers match the
shared dependency; no second ImGui implementation or SDL backend is built.
Both vendor directories record their upstream revision, archive checksum and
license. Their original source/license notices are preserved; runtime licenses
are copied beside Marcus in `licenses/`.

Keep `marcus-preview` (`marcus-preview.exe` on Windows) beside the executable;
the Marcus build target builds this headless preview helper automatically.

```console
marcus movie.3mm --resources /path/to/3dmovie.chk --content-dir /path/to/content-files
```

Resources must contain the original `GLCR/0` palette and `TMAP/0` shade table.
A compiled `utest.chk` from a Studio build can also supply them. The content
directory should contain `tmpls.3cn`, `bkgds.3cn`, `mtrls.3cn` and `tdfs.3cn`.
Marcus looks for `3dmovie.chk` beside the executable or in the working directory,
then tries known Studio build paths in the source tree. It looks for
`content-files` beside the executable, then in the source tree. These are
development conveniences; CLI flags and **Settings > Content locations** allow
explicit paths through **Palette & shading...** (the `.chk` file) and
**3DMM content folder...** (actors, props, backgrounds, fonts, materials and
sounds). Original content is not bundled or downloaded by Marcus.

Open a movie with **Open movie**, **Ctrl+O**, or drag and drop. Selecting
Resources or Content reloads that movie.

Use **New movie** or **Ctrl+N** (**Cmd+N** on macOS) to start a blank, unsaved
**Untitled movie** session. This stops playback and clears the previous movie,
scene/object/sound lists, timeline and viewport images while retaining Resources,
Content and preferences. The new movie starts with no scenes; use **New scene**
to add one. New movie's shortcut can be changed in Settings.

The top ribbon follows retail's **Scenes**, **Actors & props**, **Sounds** and
**Words** categories, with coloured icons above action labels. New/Open movie
and Settings stay in the title row; Resources and Content remain available on
every tab. Scene/frame organisers, object selection and sound track controls
use the existing player actions. The Words tab edits 2D text boxes and
creates and selects 3D text objects.

**New scene** opens a chooser for a **Storyboard - draw a background** option and the original retail backgrounds, with a rendered
preview. Use the arrows, Page Up/Down, Home/End or mouse wheel to browse, then
**Add scene**, Enter or double-click to confirm. Escape/Cancel leaves the movie
unchanged. Preview rendering runs in a cancellable helper process and does not
replace the live movie. Marcus warms all retail background previews in the
background at startup and keeps their textures when the chooser closes, so
browsing and reopening use the ready images immediately. Successful previews
are also saved in `background-previews` under Marcus's preferences folder for
reuse across launches. Changing the resource file, background archive or preview
helper (path, size or modification time) invalidates that cache. The first launch
with new content may briefly show loading while previews are generated; missing
or corrupt cache files are regenerated automatically. Retail choices require the original `bkgds.3cn` and normal
Resources/Content paths. Storyboards use the black scene's original palette from
`bkgds.3cn`, so actors and props retain their normal colours.

Adding a retail scene appends and selects a one-frame scene with the chosen background,
camera 0, a cut transition and no actors, captions or sound cues. This works for
a new movie and an opened movie, including ZIP/VMM inputs. Changes live in
memory; original movies, ZIPs and content files are not overwritten.
Resource/Content reloads retain the added scenes. An **UNSAVED** indicator marks
the working movie. **Save copy** (Ctrl+S / Cmd+S) exports the current working
movie, including scene additions, camera cuts, prop edits and extended frame ranges.
The C89 frame-edge transaction prepares the new range and sound timing before
publishing the edit as one undo step. Prepending moves the opening camera to
the new first frame and preserves the authored positions of the other events.
Ordinary movies save as `.3mm`; movies containing storyboards or V3DMM content
require `.vmm`. Storyboard VMMs include their generated backgrounds as a Portable
VXP package alongside the native movie and retain existing portable dependencies.
Reopen that copy to continue
editing it. The working session retains its original source and **UNSAVED** marker;
opening another movie, New movie or exiting discards unexported edits. Saving a
copy does not package external Library expansions or external fonts.

**Storyboard** in the Scenes ribbon enters painting mode for the current storyboard
scene, or creates a new one when another background is selected. Choosing
**Storyboard - draw a background** in New scene also creates the scene and enters
this mode. Draw directly in the main viewport, with **Paint**, **Camera** and
**Lights** tools in the normal right sidebar. The ribbon, timeline, frame/scene
navigation, prop insertion and Save copy remain available. **Done**, Escape or
clicking Storyboard again returns to the normal object tools. Starting playback
also leaves painting mode.

The 544×306 canvas starts light grey, with soft blue selected as the default pen.
Paint with black, white, soft blue, soft red, soft yellow, soft green, dark grey,
grey, light grey or the canvas grey using an opaque, hard pixel brush. Paint colours are
mapped into the unchanged black-scene palette, using a small ordered pixel dither
for soft colours. Swatches show the mixture's average colour; the draft preview
and exported background share the same pixel pattern. Choose a brush size,
use **[ / ]** to resize, **1–9** to choose colours, and **0** for canvas grey.
**Pick colour (I)** samples a painted background colour on the next click, then
returns to the brush; **Alt+click** samples directly. Sampling uses the stored
paint colour, unaffected by foreground props or dither pixels, and creates no
undo entry. Escape cancels an armed picker. Paint with canvas grey to cover marks.
The Paint panel uses OkHSL controls for hue, saturation and perceptual
lightness. Below the picker, the paint sample shows the actual shared-palette
dither. The same dither is used in the viewport and saved VMM; actor and prop
palette entries remain unchanged.

Custom colours join the scene's paint palette when first used for a stroke or
grid bake. Selecting a colour alone does not modify the movie. Sampling, undo,
reload and Save copy retain each custom colour. Up to 256 paint colours can be
in use across a scene's angles; unused custom slots can be reused. Older
storyboard formats remain readable.
**Clear angle** clears only that drawing. Ctrl+Z / Cmd+Z (or **Undo** in Paint)
uses the movie's normal undo history for strokes, clears, camera settings, angle
additions and light edits. Each completed stroke or adjustment joins the working
movie immediately; there is no Apply/Cancel dialog. Escape or Ctrl+Z during an
unfinished paint/value gesture cancels that gesture. Leaving the canvas during a stroke
breaks the line instead of drawing across the gap. Switching frames/scenes or
undoing another edit refreshes the canvas from the current movie.

New storyboard scenes start with the black background's first camera position
(0, 20, 20), downward pitch (about 5.71 degrees), FOV (about 41.53 degrees) and
near/far clipping (1/5000). New props cycle through the black scene's three
camera-relative insertion points, adapted to the current pose and lens so they
start in frame. The extra anchors are also written into exported native CAMs.
Existing drawings, cameras and prop positions remain authored as saved.
**New angle** creates a blank grey drawing using the selected camera's settings.
The current Paint, Camera or Lights tab stays selected.
Switch angles using the arrows. In **Camera**, drag numeric values or use their
minus/plus buttons to change position, yaw, pitch, roll, vertical FOV and clipping
planes. Shift makes value drags finer. Navigation uses the same controls and mouse
sensitivity as the main Fly camera: hold the right mouse button over the viewport
to capture the mouse and look around, with continuous ESDF movement and W/R down/up.
Either Shift key gives four times the speed. The wheel adjusts flight speed without
moving the camera; the sidebar shows the current speed. Releasing right mouse or
pressing Escape releases capture and commits the camera move as one undo step.
**Bake grid** in Paint, below **Pick colour**, paints the current
perspective ground grid into this angle's background using the selected colour
(**1–9** or the colour picker), ready to draw over. Grid lines are one pixel
wide and stay continuous, with crowded distant lines omitted at the midpoint of
the live grid's fade at the painting's 544×306 resolution. The bake uses solid ink
without stippled gaps; repeating a bake retains the same lines.
The lines retain the current camera perspective and stay fixed if you move the
camera later. Baking is one undo step and survives Save copy, without adding
depth. The live guide, crosshair and guide label appear in Paint, Camera and
Lights as temporary overlays.
The storyboard toolbar's **Grid: on/off** toggle shows or hides the live guide
in Paint, Camera and Lights. It leaves baked grid lines and the movie unchanged.
The live ground grid uses the preview's scene depth, so actors and props hide
grid lines behind their surfaces. It fades as perspective compresses cells toward
single-pixel spacing, keeping the distant grid from merging into a solid band.
Baked lines remain part of the background.
**Use work camera** copies the current
work-view pose and lens into this angle, retaining its drawing.
The main Orbit and Fly toolbar's **Set angle** applies the perspective
work view to the active storyboard angle as one undoable edit. Existing
drawings, lights, and object positions are retained.

**Lights** adds, selects, deletes and adjusts up to sixteen directional or point
lights. Its 3D inspection view shows selectable light widgets and the shot's camera
frustum. Point lights use spherical markers; directional lights show an arrow.
Dark outlines and label backgrounds keep these markers readable over paintings
and the ground grid.
Click a marker to select it. Lights use the same ImGuizmo controls as props:
**Move** provides coloured axis arrows, plane handles and a centre handle;
**Rotate** provides rings for aiming directional lights. Hold Ctrl to snap moves
to one unit or rotations to 15 degrees. Numeric fields stay available for
precise values and intensity. Releasing a drag commits one movie undo step;
Escape or Ctrl+Z during a drag restores its starting values.
**Frame lights** fits the lights, visible objects and shot camera in the inspection
view. Hold right mouse with ESDF and W/R to inspect, with Shift for faster movement
and the wheel to change speed. Inspection never changes the movie's camera.
**Shot view** previews the active angle; **3D light view** returns to inspection.
Position, direction and intensity affect 3D actors/props across all angles;
the paintings keep their flat colours. Existing scene objects appear over the
painting, with camera/light changes previewed during a drag and committed on release.
Painting and lights belong to the scene; each angle has its own image and camera.
Choosing an angle authors its camera at the current frame, retaining later cuts.
The Camera angles chooser offers **Edit storyboard** and **New angle** for these scenes.

Storyboard images carry all-infinite depth, matching the default black background's
no-scenery-depth policy: they cannot hide actors and create no reconstructed scenery
mesh in Orbit/Fly. They are ordinary native BKGD/CAM/MBMP/ZBMP/GLCR/GLLT resources
in a Portable VXP inside the exported VMM; a versioned STBD scene child retains
editable drawings/settings. The inner native movie references the backgrounds
through the standard portable content source (SID 6).
The storyboard working copy and VMM export share a C89 editor for those scene
references. Export accepts existing v3dmm declarations for SID 6 and rejects a
conflicting library before publishing the copy. Adding the declaration preserves
the existing source table's byte order, string encoding and rows.
No retail content files are changed. Resources are still required; creating a
storyboard requires the original black-scene palette.

Storyboard copies target Marcus and v3dmm, rather than stock retail. Retail does
not open embedded background tags in the movie resource cache; the previous
embedded `.3mm` export failed during loading. Save copy now rejects that format
for storyboard movies. A `.vmm` is a real v1 container carrying VXP bytes, not a
renamed `.3mm`; original v3dmm resolves the packaged backgrounds normally.
Opening an older Marcus storyboard rebuilds its palette from the original black
scene without changing the source file. New package names and resource IDs avoid
stale installed-package reuse and collisions with existing portable content.
C89 checks the existing portable indexes and assigns high even numbers by chunk
type. It rewrites the generated index and all its child links together. Only
the private staging archive receives the rewritten index.
C89 also writes the package identity and CFG and orders the CFG, content and
thumbnail members. The VMM save publishes the package after ZIP close succeeds.

The original v3dmm 0.14 runtime loaded an exported two-angle storyboard, displayed
its drawing and prop, and sought forward/backward across both camera cuts with
an empty error stack. Diagnostic captures and source-preservation checks are in
`tmp/storyboard-vmm-final-1794817266473529300/`. Headless checks cover editable
round trips, all Marcus rendering backends, unchanged retail palette bytes,
replacement of generated packages on resave and normal `.3mm` saving for ordinary
movies. This does not establish stock retail support for VMM files.

After the C89 change, original v3dmm again sought frames 1, 2 and 1 with one
prop and an empty error stack. The captured images matched the previous
export. Studio now accepts the valid empty thumbnail archive in these packages.
Both storyboard frames matched original v3dmm in scene state and viewport
pixels. Captures and logs are in `tmp/c89-portable-wire/` and
`tmp/c89-pack-archives/`.
After moving the index rewrite into C89, the same frame comparison passed.
The two-angle, resaved and multi-page packages also matched the previous
exports' payload and child-graph signatures after normalizing chunk numbers.
Evidence is in `tmp/c89-portable-rekey/`.
After the ZIP migration, both storyboard frames again matched original v3dmm.
The C-only ZIP round trip passed on x86 and x64 with AddressSanitizer.
Evidence is in `tmp/c89-vxp-zip/`.

**Scenes > Camera angles** shows a grid of rendered thumbnails for the current
background's authored cameras, including actors and captions at the current frame.
Use the arrow keys, Page Up/Down or wheel to browse. **Use angle**, Enter or
double-click applies the selected angle at that frame; Escape/Cancel leaves the
movie unchanged. Selecting the current angle also leaves it unchanged. Thumbnail
rendering uses the isolated preview helper and does not alter the live camera.

Camera choices are authored changes in the same live session as new
scenes. A choice replaces any camera changes at that exact frame and lasts until
the next authored camera change. Other frame events and later camera cuts are
retained. A cut at the opening follows the first frame when a frame is
prepended. A successful edit marks the movie **UNSAVED** and **Save copy**
exports the cuts.

**Scenes > All cameras / Active only** toggles the other authored camera guides
in the work view (Orbit/Fly, including the work pane in a split view). The active
camera stays enabled regardless of the toggle, but its guide is hidden while
the work view is matched to that camera, so it does not cover its own shot. Each guide
uses that angle's actual position, orientation and lens, and is labelled
**Camera 01**, **Camera 02**, etc. The active angle is gold; other angles are blue.
Guides are clipped to the visible viewport and never drawn over movie playback.
All cameras are shown by default; the choice is saved across movies/restarts.

**Actors & props > Add object** opens one gallery of original actors and props,
sorted together by name. The **All / Actors / Props** filter toggles show both
catalogues or just one type; the filter lasts for this Marcus session. Switching
filters retains cached previews and keeps the selected object if it remains visible.
The responsive gallery shows rendered 3D model cards with names below each
model; the tooltip identifies its type. Click a card or use Left/Right and Up/Down to select a model. The wheel
scrolls rows, Page Up/Down moves by a page, and Home/End jumps to the first/last
model. A scrollbar is available on the right. The confirmation button changes to
**Add actor** or **Add prop** for the selected model; Enter or double-click also adds it.
Cancel/Escape leaves the movie unchanged. The chooser requires `tmpls.3cn` in the
Content folder. Model previews load progressively through one isolated helper
process, prioritizing the selected and visible cards. Successful renders are
saved in `model-previews` under Marcus's preferences folder and reused across
reopenings and launches. The cache holds up to 256 previews and tracks the
resource file, template archive, and preview helper. Missing or unreadable
entries are regenerated. A failed preview is marked unavailable and leaves the
other cards usable. New scene keeps its background
list and larger selected preview.

Select an actor, prop, or 3D text object and use **Copy object** and **Paste
object** on **Actors & props**, or Ctrl+C/Ctrl+V (Cmd+C/Cmd+V on macOS). The
object clipboard follows the open Marcus window across scenes and movies.
Paste selects a new object and aligns the copied playhead with the destination
playhead, keeping the track's placement, animation, appearance, and object
sounds. External content still needs to be available in the destination.
Undo removes one paste. Focused text fields use normal text copy and paste.

**Words > New 2D text** places an editable box in the movie camera. Type
directly in the box, drag the top border to move it, and drag the handles to
resize it. The live draft uses the movie renderer for fonts, colours,
alignment and wrapping. A new box starts at the playhead and stays visible
through the end of the scene by default. Open **Default font** to search and
preview available font families. The **Text** and **Fill** swatches open the
same OkHSL controls used for storyboard paint. The side panel also sets
alignment, scrolling, frame range, and exact position and size in 544×306
movie coordinates. The last frame is inclusive.

**Words > Edit 2D text** opens the active box. Use the box list in the side panel
to switch boxes. Clicking outside the box and its side panel applies the draft
and exits edit mode. **Done** and Ctrl+Enter (Cmd+Enter on macOS) also apply it.
**Cancel** or Escape discards
its draft. **Delete box** removes the selection. Each committed change is
one Undo step. **Save copy** writes the boxes into the native movie.

The C89 engine resolves the scene's text boxes in authored order and updates
the scene links and start events when a box is added, edited or deleted. A
replacement keeps its slot and a new box uses the first free text slot.
Five native saves matched their pre-conversion files byte for byte, covering
insertion, replacement, removal and slot reuse. Retail reopened all five
saves with the expected boxes. At frame 25, SDL Studio matched retail's
scene and actor state for all five saves and matched four viewports pixel
for pixel. The replacement differed from retail by one pixel in both SDL
builds. Captures and checks are in `tmp/c89-caption-archive/`.

The draft and save paths share C89 validation for box bounds and frame
ranges. C89 normalizes line endings and updates the changed UTF-16 span,
including first-character formatting, before the native text is written.
Five styled-caption saves, including a Unicode caption with mixed line
endings, matched their pre-conversion files byte for byte. SDL Studio and
retail matched scene and actor state and viewport pixels at frame 25 in
each save. The comparisons are in `tmp/c89-caption-properties/`.

C89 also places editor caret stops from the rendered layout and computes
draft scrolling. It keeps both positions where a line wraps and skips
positions inside UTF-16 surrogate pairs and CRLF breaks.
The headless comparison matched 107 caret stops, their coordinates and
scroll offsets, and five saved movies before and after the change.
SDL Studio matched retail state and viewport pixels at frame 25 in each
movie. The results are in `tmp/c89-caption-caret/`.

The C89 presentation stack also controls caption drawing order, hit-testing,
and draft placement. Credits scroll on their first authored frame using
the measured height from the same layout used to render them.
The migration matched 22 rendered-frame checksums and hit results across
drafts, overlapping boxes, a scrolling hold, and a seek reset. Five saved
movies matched their prior files byte for byte. SDL Studio matched retail
state and viewport pixels at the first authored frame of each movie.
The captures and comparisons are in `tmp/c89-caption-stack/`.

**Words > New 3D text** opens a text entry dialog. Enter up to 50 characters on
one line, then choose an original 3D font and one of the ten retail shapes from
the visual pickers. Each selected font, shape and material has a rendered sample;
click its image to open a thumbnail gallery filling the app window. The grid
prioritizes readability: cards prefer at least 320 pixels of image width and
grow wider for longer wording, using fewer columns and vertical scrolling.
Card height follows the rendered text's proportions, with bounded padding,
rather than forcing long sentences into large 4:3 boxes. Gallery samples render
at 960x720 before transparent margins are trimmed independently on each axis;
the cropped native pixels retain their detail and aspect ratio. The complete
wording stays visible, including dark materials whose geometry is detected by
alpha rather than colour. The main interactive preview remains 480x360.
The separate Choose buttons are
removed; Back or Escape returns to the dialog. Generated material labels such
as "Material 25344" are hidden in the tiles and tooltips; authored names remain.
Samples render your current wording using the real retail font, shape or
material, including texture patterns. Font and shape samples use a neutral material, and shape
samples follow the selected font. The 50-character limit follows retail's text
editor (`kcchMaxTdt` in `src/studio/esl.cpp`); it is not a file-format limit.
The dialog is implemented in C89 using standard cimgui widgets in the same
ImGui context as the transform gizmos. Click in the text field to position the
caret, drag or Shift+arrow to select, and use Ctrl+A, copy/cut/paste, cursor
navigation, Backspace and Delete. Tab switches between controls; Enter in the
text field adds/saves, and Cancel or the title-bar close button discards changes.
Escape follows ImGui's focused-control/popup handling before closing the dialog.
Empty wording or more than 50 characters disables Add/Save and shows an
explanation while allowing the wording to be corrected. Characters must
be supported by the original Windows-ANSI fonts in `tdfs.3cn`. Invalid input
stays in the dialog with an explanation. Creating text uses the native ThreeDText
engine; its generated template is embedded in the saved movie and its font
continues to reference original retail content.

The shared New/Edit dialog also offers native material and colour choices from
`mtrls.3cn`. Colour swatches choose existing palette materials; the Material
gallery also includes textured patterns. Ambient, diffuse, specular and shininess
adjust both solid and textured materials' response to lighting. Solid materials
show lighting controls across the full properties width. Only textured materials
show the additional U/V scale, rotation and U/V offsets alongside lighting;
selecting a material updates this layout immediately. Drag numeric values or Ctrl+click to
type an exact value. The live 3D preview updates as settings change: drag it to
rotate or scroll over it to zoom. Its view is separate from the object's position
and rotation in the movie. Previewing does not edit
the movie or add undo entries; Cancel discards every pending setting.

Horizontal dragging turns the text with the pointer. The Rotate/Zoom/Reset
button row and the instruction line below the image have been removed.
Rotation and zoom submit camera requests every UI frame
to a persistent `marcus-preview --text-live` session. That helper keeps the
template, resources and renderer loaded, and coalesces queued views to the
latest request. Camera changes do not rebuild a template, launch a process or
wait for rendering on the UI thread. Only text/font/material edits retain the
180 ms rebuild throttle. Preview frames update one streaming SDL texture, and
the open dialog uses the normal vsync cadence without the extra 10 ms idle
delay. Closing/replacing a session invalidates in-flight frames; process
teardown happens on the worker thread.

Gallery thumbnails load only when their tiles are visible and stay cached for
the current wording. Editing the text cancels obsolete thumbnail work and
invalidates all font/shape/material samples; a short typing debounce avoids
rebuilding them for every keystroke. Empty or over-limit input leaves the
samples blank until valid wording is entered.
A separate thumbnail worker leaves the interactive preview's
camera/session intact. It reuses one renderer process across template loads;
native template preparation stays on the player owner thread. Changing fonts
invalidates the dependent shape/material samples. The properties pane scrolls
independently so the main preview remains visible while adjusting lighting and
texture mapping.
The dialog uses the available window height instead of a fixed 760-pixel cap.
It recenters when the app window is resized or maximized, while allowing manual
placement between resizes. The other centered Marcus dialogs already calculate
their placement from the current window dimensions each frame.
The main preview reserves the image button's frame padding inside its column,
so its right edge remains visible and the loading placeholder has the same size.

The 17 November Debug headless check renders 20 alternating warm views in
0.121 seconds (166.0 views/second), including exact pixel comparisons against
the one-shot renderer. This measures preview/IPC throughput, not displayed UI
FPS. Before this change, camera previews were capped at 5.6 updates/second and
restarted the renderer for each update. The insertion check also exercises
burst coalescing, invalid-camera recovery and stale-result rejection without
creating a UI or changing the live movie.

To edit existing 3D text, select it and choose **Edit 3D text** in its object
properties or the **Words** ribbon. This reuses the creation dialog, prefilled
with that object's wording, font and shape. **Save changes** updates the existing
object and keeps its placement, rotation, scale, entrance and animation track.
Cancel/Escape leaves it unchanged. The edit is one undo step; saving unchanged
settings adds no undo entry. Text reopened from native movies is editable too.
The current imported font is retained unless a different font is selected.
Saved copies embed the replacement template while retaining the object's track.
Appearance changes apply to the entire text object across its entrances and
frames, replacing earlier material changes on that word. The material, texture
and native texture-transform chunk are embedded with the text actor, so lighting
and pattern placement survive save/reopen and work through the native playback
path. Font choices remain the existing fonts; this dialog does not create fonts.
Headless checks cover material properties, transformed patterns, repeated edits,
undo and native save/reopen. Retail captures of patterned, restyled and newly
styled words at 544x306 match all text pixels; each frame has the same single
differing pixel on the separate actor. The retail error stack remains empty.

Select a visible actor or prop while paused, then choose **Costume** in its
object properties. The C89/cimgui dialog shows visual choices grouped by body
part, with separate accessory choices where the template provides them. Its
preview shows the current pose, supports drag rotation and wheel zoom, and
updates as clothing, surfaces or accessories are selected. It uses the available
window height and recenters when the window is resized. Thumbnails are cached;
only one uncached visible thumbnail is rendered per frame.
The initial view uses the template's rest orientation with a front three-quarter
camera, independent of its rotation in the movie. Each camera angle fits the
complete model with a small margin; zooming stops at that fit so the head and
feet remain visible. The main preview renders at its panel's aspect ratio.
Items with no alternative costumes show a disabled **No costumes** button,
including templates with only their current default costume.
Costume thumbnails focus on the changed body-part set, such as a shirt,
trousers, shoes or hat. Each row shares framing across its variants, including
removed accessories, and its cards follow the cropped region's aspect ratio.
The main preview keeps the whole actor, trims unused image margins and sizes
its frame to the visible model. Isolated previews fit their depth planes to
visible triangles to preserve depth precision between overlapping surfaces.

**Apply costume** changes the chosen body parts at the current frame in a single
undo step. Cancel/Escape leaves the movie unchanged; applying unchanged choices
adds no history entry. Actors and props reopened from movies are editable too.
The shared C89 retail costume editor preserves earlier frames, later distinct
costume changes and entrance boundaries. Private event tracks are retained in
session history and embedded into **Save copy**, alongside the existing actor
template and animation. The BRender ASM worker receives a materialized native
movie for costume edits. This picker selects the template's existing costume
variants; it does not create new costumes or import textures.

Headless native checks cover actor and prop choices, isolated previews, edits
before later costume events, combined body-part changes, undo, repeated seeking,
further edits after reopening, and exact preview pixels after native save/reopen.
They also check stable body-part framing, crop bounds, fitted depth ranges and
that previewing retains the movie's exact rendered pixels.
No Marcus UI test was added.

Actors, props and 3D text appear at the current frame, at the active background
camera's next insertion point, and remain through the scene's last frame.
Insertions cycle through the default point and any extra points in retail order,
then wrap. Changing camera angles or scenes restarts the sequence; camera
thumbnails and metadata reads leave it untouched. Moving an object manually
releases the last point for reuse. Failed insertions also leave that point
available. Each object starts with its default pose/action on a stationary track
and is selected in the object list. Each insertion has a unique movie-wide
object ID and roll-call name. Other scenes and existing tracks are retained.
Insertions share the **UNSAVED** session with scene and camera edits, support
Undo and the existing movement/transform controls, and are included in **Save copy**.

**Open movie** opens Marcus's cimgui movie browser. Thumbnail cards and
a larger detail preview use saved scene thumbnails, with a list view available.
The sidebar offers recent movies, a stable Movie folder shortcut, Home and up to twelve
favourite folders. Recent entries remember the exact movie name inside a ZIP.
Movie folder uses `Documents/3DMMForever`, matching SDL Studio's My movies
location and following OS Documents redirection. Browsing or opening movies
elsewhere does not change this shortcut. If Documents cannot be used, Marcus
falls back to its working directory.
The browser saves the last folder, selection, view, sort order and file filter
in `movie-browser.cfg` beside the normal per-user Marcus preferences.

Use the location field and **Go**, **Back**, **Up**, or double-click folders to
navigate. Search filters names in the current location; the format selector offers
all supported files, `.3mm`, `.vmm` and ZIP. Sort by name or newest first.
Arrow keys, Page Up/Down, Home/End and the wheel navigate the results. The results
scrollbar uses ImGui's standard scrolling controls. Enter opens,
Escape cancels, Alt+Up goes to the parent, Alt+Left goes back, Ctrl+L focuses the
location, Ctrl+F focuses search, Tab moves between the list and text fields, and
F5 refreshes. Text fields use standard ImGui editing, selection and clipboard input.
Dropping a folder opens that location. `marcus --browse folder-or-zip` starts
directly in the browser; selecting a movie still requires an explicit Open.

Folder/ZIP listing, ZIP extraction and preparation of preview files run on a
worker thread. Navigating elsewhere or cancelling invalidates pending work;
extraction checks cancellation between output blocks, including the legacy
7-Zip helper path. Partial files are removed. Browser opens retain the verified
extraction so the main thread does not decompress the same movie again.
The footer shows **Preparing movie** while a chosen movie is unpacked.

Saved thumbnails and scene/frame counts are decoded in a cancellable
`marcus-preview` subprocess launched by the browser worker. The separate process
isolates legacy archive globals from the active player. It returns bounded
metadata and RGBA pixels; the main thread only creates the display texture.
Preview file handles and temporary-file cleanup also stay on the worker.
Scrolling pauses preview queuing until 250 ms after the last scroll input;
dragging the scrollbar suspends it until release. A bounded cache evicts the
least recently used thumbnail individually instead of clearing every preview.
Initial engine loading and content resolution still run on the main thread after
an explicit Open. Whole-movie timing runs in an isolated background helper;
not all movie loading is asynchronous.
The selected movie has preview priority; other visible movies up to 32 MiB are
inspected one at a time. ZIP previews may temporarily extract the inspected
member using the existing size/CRC checks. VMM previews copy only the inner
movie, without installing dependency packages.

The details pane estimates duration from the summed scene frame counts at the
engine's six frames per second, rounded to the nearest second and labelled
**Est. ... (frames only)**. It excludes additional time for pauses, sound waits,
scrolling captions and transitions. It uses metadata from the preview helper
and does not load playback or add UI-thread file reads. A missing
thumbnail displays a placeholder; first-frame rendering,
dependency availability badges and bundled-font lists are not implemented
in the browser. Dependencies are resolved through the normal opening path.

**Calculate duration** runs the same movie-wide timing analysis used by the
player, on demand in the isolated helper. It includes timed pauses, scrolling
captions, sound waits and transitions; click-to-continue waits assume immediate
acknowledgement. This is nominal playback duration, not a guarantee of wall-clock
time on a slow machine. The result shows both calculated duration and analysis
time. It is cached with the movie's preview for this browser visit. Calculating
is silent and leaves the active movie unchanged; **Cancel calculation**, closing
the browser or selecting another movie cancels it. Scrolling stays available.
Analysis requires the movie's normal content dependencies. The helper has a
five-minute time limit, separate from the ordinary preview's thirty-second limit.

On 11 November 2026, this workspace's debug build analyzed
`3dMm DIReCTOrS WaR Part 2.3mm` in 3.8 seconds (5:52 nominal duration) and
`Litterbug 2.3mm` in 12.2 seconds (11:19 nominal duration), including helper
startup. These are single-run observations; archive extraction, content, audio
decoding and font availability affect the time.

The same headless calculation also passed for `iknow1.zip` (1.1 seconds,
1:43 nominal duration), `A Special Announcement.vmm` (40.4 seconds, 3:55), and
a temporary ZIP containing that VMM (42.9 seconds, also 3:55). The ZIP/VMM
checks exercise archive extraction and the normal bundle/content loader, not
simulated UI input. Their results are recorded locally in
`tmp/marcus-duration-validation/results.json`.

Previewing does not replace the active movie or play audio; cancel leaves it
paused. The SDL Studio picker and movie format are unchanged.

Opening a movie or changing scenes clears object and sound selection, including
scene changes during playback. Objects are selected only by explicit input,
such as a click or the `--object` command-line option.

The **Scene Objects** list has a draggable scrollbar when there are more objects
than visible rows. Clicking above or below its thumb moves one page; the wheel
continues to scroll the list. Drag the horizontal grip above **Object Properties**
to change the list height. The row count, scroll range and thumb size update with
the divider, and object/sound properties move with it. Use the wheel over the
properties pane to read fields that no longer fit. The divider position lasts
for the Marcus session and is constrained to keep both panes usable.

The playback controls show the current movie position and total duration as
`m:ss / m:ss`, or `h:mm:ss / h:mm:ss` for movies lasting an hour or more.
Frame and scene navigation, readouts, Play/Pause/Continue, Loop, Mute and
movie seeking use standard cimgui buttons and sliders. Frame/scene readout
buttons open their organisers. The readouts, navigation arrows and Play button
draw the original 3DMM-style SVG artwork on their ImGui buttons: beveled silver
surrounds, dark arrows, green Play/Continue and red Pause symbols. Installed Arial
Black and Comic Sans MS supply the readout labels and numbers where available,
with ordinary ImGui fonts as fallbacks. Windows font discovery accepts any casing
of the `windir` environment variable. These two fonts use ImGui's FreeType
loader with nominal pixel sizes, native hinting and the original 14-pixel
baseline; ordinary UI fonts retain their existing renderer. Interaction, disabled
state, focus and keyboard activation remain owned by ImGui. The main UI, inspector and debug overlays share
ImGui's font atlas; `--font file.ttf` supplies a custom UI font.
Click the **SCENE** readout to open the scene organiser. It pauses playback and
shows a scrollable cimgui grid in movie order, with saved thumbnails and
scene names. Hover a card to see its frame count and outgoing transition.
Select a card and press Enter
or **Open scene**, or double-click it, to jump to that scene's first frame.
Arrow keys, Page Up/Down, Home/End, the wheel and the draggable scrollbar navigate
the grid. Escape or Cancel closes it without seeking; playback stays paused.
Missing thumbnails use a placeholder. Thumbnail decoding leaves the live scene
and cameras untouched, loads at most one saved image per draw, and uses a bounded
texture cache. The organiser currently supports browsing and scene selection;
scene reordering, deletion and movie saving are not implemented.
Transition details display the authored setting without editing it.

Click **FRAME** to open the same organiser for every authored frame in the current
scene. It highlights the current frame; Enter, **Open frame** or a double-click seeks to
the selected frame. The grid uses the same keyboard and scrolling controls as
the scene organiser. Escape/Cancel closes without seeking and leaves playback
paused. Frame tiles contain only the preview image, with a thin outline for
selection or hover. The size slider, Ctrl+wheel and minus/plus keys resize the previews.
At 100% previews use their
native pixels; zooming out packs more frames into the window. The minimum zoom is
10%, with previews kept at least 16 pixels wide and 9 pixels high. The chosen
size lasts for this Marcus session. The wheel without Ctrl still scrolls.
**Fit** chooses the largest preview size within the zoom range that shows every
frame in the selected scope together, accounting for rows, columns and spacing,
and never enlarges beyond native resolution. Fit remains active as the window
or Whole movie scope changes; adjusting zoom manually leaves Fit mode. If the
minimum preview size is reached, remaining frames stay available by scrolling.

The **Whole movie** toggle switches the grid between one scene and every authored
frame across all scenes, in movie order. It keeps the selected frame in view and
retains the zoom setting. Turning it off shows the scene containing the selection.
Confirming a frame in another scene changes scenes and seeks to that exact frame;
cancelling still leaves the live movie unchanged. The scope choice lasts for this
Marcus session. Thumbnails remain generated on demand, with scene and frame
identities kept separate so equal frame numbers in different scenes cannot collide.

Frame thumbnails render silently within 160x120 through the authored camera,
including captions at their seek position. The renderer's surrounding letterbox
canvas is removed using the camera aspect ratio, preserving black areas inside
the authored shot. The grid uses that image's dimensions with two pixels between
previews. An isolated `marcus-preview` process keeps the
active playhead, camera and rendered images unchanged. Only visible missing
thumbnails are queued, with the selected frame first; scrolling replaces pending
requests. The helper opens the movie once per organiser visit, uses the normal
ZIP/VMM/content loading path, and is cancelled on close. A bounded cache holds up
to 4,096 frame textures, enough for the densest grid. Indexed cache lookups keep
the denser grid from scanning every cached texture for each visible frame.
Pending previews are blank;
if the helper fails, frame
selection remains available with an unavailable-thumbnail message. Thumbnails
show authored frames, rather than extra playback states during scrolling holds
or transitions between scenes.

The timeline playhead uses Studio's original frame sprite, including the
musical-note version for frames with sound cues. The movie-wide scrubber uses
Studio's original green scene sprite and recessed track in both the editor and
expanded movie view. The build uses `fthm.bmp`, `fthms.bmp`, `stmb.bmp` and
`sscl.bmp` from the Studio resources. ImGui handles seeking and keyboard focus
for the movie scrubber.

Time spans all scenes and follows seeking, playback and scene looping. It includes
scrolling text, timed pauses, sound waits and transitions between scenes. Click
waits assume immediate acknowledgement, so time spent waiting for a click is
excluded. Timing is analyzed silently in the background when opening a movie.
The display shows **Calculating...** while the current scene remains usable for
seeking, playback and inspection. Whole-movie scrubbing becomes available when
the result arrives. A failed helper leaves scene playback available, without
falling back to a blocking calculation. With no movie loaded, the display reads
`--:-- / --:--`.

ZIP files can be opened through the same controls or passed on the command line.
Marcus searches every folder in the ZIP for `.3mm` and `.vmm` entries, ignoring
extension case. While listing a folder, the browser inspects ZIP directories
without extracting their contents. A ZIP containing one movie appears as that
movie's card with a **3MM / ZIP** or **VMM / ZIP** label, supports saved previews
and opens the movie directly. Search matches both movie and archive filenames;
the detail pane retains the archive path. ZIPs with several movies remain
collection cards. Explicitly browsing a ZIP (including drag and drop) shows its
members; a single movie passed directly on the command line still opens
automatically. ZIP members appear with their internal paths
visible: select a card or row and
click Open, or use the keyboard. The wheel scrolls the results; Escape cancels and
keeps the previous movie paused. The window title identifies the ZIP and selected
member. Resources/Content reloads retain that selection.

```console
marcus movies.zip --zip-movie 2 --resources /path/to/3dmovie.chk --content-dir /path/to/content-files
```

`--zip-movie` is a one-based index in the ZIP's original archive order, independent
of browser sorting. Use it when combining a multi-movie
ZIP with command-line scene/frame selection or screenshot capture. The C API
provides a separate listing/selection interface; ordinary `marcus_open` opens
single-movie ZIPs and reports that an explicit selection is needed for multiples.

Listing extracts nothing; the visual browser can extract members to read saved
previews as described above. Opening extracts the selected movie and bundled fonts into a private
directory, verifies its declared size and CRC, and retains it while the player
needs it. Reloading an already selected entry reuses that extraction. Closing or
replacing the movie removes its private file, after renderer/content teardown.
Zipped `.vmm` files then follow the existing bundle/package loading path. Original
ZIPs remain unchanged, and extraction failures remove partial output. Entries with
unsafe names, encryption, or movie sizes above 512 MiB are rejected. A ZIP may
contain at most 100,000 members and 10,000 movie candidates.
Marcus uses the shared C ZIP reader for movie archives. C89 retains each
selected member's original directory slot and checks names and extraction
limits. Duplicate names in legacy-compressed movie or font entries are
rejected because the external decoder selects by name.

Fonts anywhere in the selected ZIP are loaded automatically for its movie's
2D captions. Supported extensions are `.ttf`, `.otf`, `.ttc`, `.otc`, `.fon`,
`.fnt` and `.dfont`, matched by internal family and style rather than filename.
The private cache mounts in the content VFS's `movie-fonts` namespace; FreeType's
catalog reads its bytes through the VFS. Matching bundled faces take precedence
over installed fonts without installing anything or changing Marcus's interface
font. Plain movies retain normal installed-font lookup. All movies selected from
the same ZIP can use its bundled fonts.

Font scope follows the selected movie lease through playback, scrolling-layout
analysis, Calculate duration and frame previews. The private cache is removed
when the last player/helper lease closes. Replacing the movie clears its caption
face/raster caches, so bundled fonts cannot affect the next movie. Font files
receive the same declared-size/CRC checks as the movie; invalid or encrypted
fonts fail explicitly. Limits are 128 font files, 16 MiB per file and 64 MiB in
total. Files are cached under generated names, never ZIP-provided filesystem
paths. This uses the existing file-backed VFS; it does not add a ZIP streaming
provider or change three-dimensional TDF font assets.

Stored/deflated entries use the existing miniz dependency. Legacy compression
uses an installed `7z`/`7zz` executable, with standard Windows 7-Zip locations as
fallbacks. The helper runs without a console, extracts the exact selected name to
a bounded pipe, and has a 60-second timeout. Both decoders use the same C89
size and CRC checks.
No 7-Zip installation is needed for ordinary stored/deflated ZIPs.

## Controls

| Control | Behaviour |
|---|---|
| Space / Play | Play or pause at the configured movie frame rate (6 FPS by default) |
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
| Ctrl+click Previous Frame at the first frame | Prepend one frame and select it |
| Ctrl+click Next Frame at the last frame | Append one frame and select it |
| Four buttons in the Scene row | First / previous / next / last scene |
| Click or drag the timeline | Scrub the current scene |
| Click or drag the scrubber beside the scene name | Scrub across the entire movie; ticks mark scene boundaries |
| Click an object name or track | Select the actor/prop and inspect its evaluated metadata |
| Objects / Sounds / Both dropdown | Choose which tracks appear in the dope sheet |
| Click a sound cue marker | Seek silently to its frame and inspect its owner, volume, loop, queue and trigger |
| Sound layer S / M | Solo or mute the layer throughout the open movie |
| Reset mix beside the track dropdown | Clear all layer solos and mutes, including layers absent from the current scene |
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
| W / E / R with an object selected | Switch its gizmo to Move / Rotate / Size |
| Arrow beside an object track | Expand/collapse Transform, Appearance, Timing and Visibility event lanes |
| Click an animation event diamond | Select its actor and seek silently to the evaluated event frame |
| Right drag in Orbit / wheel | Orbit / zoom |
| Shift + right drag in Orbit | Pan the view |
| Hold right mouse in Fly | Capture the mouse for first-person look and movement |
| E / D / S / F while holding right mouse | Fly forward / backward / left / right |
| W / R while holding right mouse | Move down / up in world space |
| Shift while flying | Four times normal movement speed |
| Wheel in Fly | Adjust flight speed |
| Release right mouse | Release mouse capture and stop flying |
| Escape | Return from movie-only or fullscreen mode to the windowed editor; otherwise clear selection/camera capture, or pause playback |
| Reset, or R when the object gizmo is unavailable | Match the current movie camera. R also fits the timeline |
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
The same dialog offers **Textured (authored)**, **Solid**, **Flat shading**, and
**Wireframe** display modes using BRender's existing APIs. Authored 3DMM
materials already use smooth/Gouraud shading: lighting intensity is interpolated
and then resolved through the indexed shade table and palette. There is no
separate Smooth choice; old Smooth selections return to Textured (authored).
GPU backends also offer **Depth** and **Face normals**, an independent **Wire
overlay**, **Nearest / Bilinear** texture filtering, and **Off / 2x / 4x SSAA**.
Filtering interpolates resolved palette colours, including transparent texels;
it does not interpolate palette indices. Diagnostic display modes hide the
original background bitmap; Depth also visualizes its authored depth map.
Flat shading affects geometry, since
background lighting is already baked into the artwork.

SSAA renders at twice or four times the chosen width and height, then averages
the completed frame back to the chosen resolution. It must fit within BRender's
4096-pixel limit in both dimensions; Apply explains incompatible combinations.
The CPU renderer disables GPU-only settings when selected. No BRender engine,
driver or vendor source changes are needed for these controls.

The work camera additionally offers **Perspective / Orthographic** projection
and **Free / Front / Side / Top** presets. Front, Side and Top select
orthographic views around the current inspection focus. Orbiting releases the
axis preset while retaining the projection. Reset clears the axis preset and
matches the movie pose while keeping the selected projection. Picking, camera
guides, object outlines and caption planes use that same projection.

The native 3DMM movie canvas is **544x306**. Its scale presets are **1/2x
(272x153)**, **1x / 3DMM (544x306, default)**, **2x (1088x612)**, **3x
(1632x918)**, and **4x (2176x1224)**. All presets use the movie's **16:9** ratio.
Other available sizes are **320x180**, **640x360**, **960x540**, **1024x576**,
**1280x720 (720p)**, **1600x900**, **1920x1080**, **2560x1440**, **2880x1620**, **3840x2160**, and
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
The work view fits its near/far depth range to visible scene geometry each frame
to reduce 16-bit depth fighting. It leaves clearance around those surfaces and
adapts as you fly closer. The movie camera retains its authored projection.
This improves precision without moving surfaces or changing their draw order;
truly coincident faces can still fight. Picking and caption occlusion use the
same fitted projection as the rendered work view.
Choose Fly in the work view, then hold the right mouse button and use ESDF and
W/R to fly while the movie continues. C switches the work view between Orbit and Fly.
The **Orbit** button activates the current orbit mode. While Orbit is selected,
its target dropdown appears underneath, offering **Scene centre** and **Selection**. Scene centre
fits the scene; Selection fits the selected actor/prop's posed bounds and follows
its movement during playback or scrubbing. Selecting another object changes the
pivot. Selection is available for on-stage objects; clearing the selection or an
object leaving the stage falls back to the scene centre. Switching orbit modes
refits the view, while right drag, Shift+right drag and the wheel still rotate,
pan and zoom around the chosen pivot.
While Fly mode is active, its current speed appears below **Fly** in world units per second. It updates
as the wheel changes speed and shows the four-times speed while Shift flight is active.
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

The purple frame-slider thumb displays a musical note on frames containing a
scene or actor sound cue, including silence cues, as in retail 3DMM. The note
marks the cue frame rather than the full duration of a playing sound.

Picking uses the displayed camera, evaluated model faces, texture transparency and depth;
3D text is selectable on its letter surfaces, with gaps between letters left empty.
Ray tests use face planes in the prepared vertices' coordinate system, including
model pivot offsets, so text fronts remain selectable after model preparation.
Background scenery and caption overlays block objects behind them. Backgrounds,
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

Each sound layer has **S** (solo) and **M** (mute) buttons. Multiple layers can
be soloed together; when any solo is active, only soloed, unmuted layers are
audible. Mute takes precedence over solo. Silenced layer names are dimmed.
Changes affect already-playing sampled audio and MIDI without pausing playback,
seeking, restarting voices or changing their positions. Queues, looping, MIDI
events and authored sound waits continue progressing while silenced. The global
Mute button still silences the complete output independently of layer controls.

Layer choices last for the open movie and follow the layer's owner and sound
type across scenes: muting **Scene / Music** keeps scene music muted in later
scenes, including music continuing through a cut. Actor layers follow their movie
actor ID and sound type. Seeking, scene looping, background timing analysis and
FPS changes retain these choices. A solo remains active when its layer is absent
from the current scene; **Reset mix** beside the track dropdown clears every
layer's solo/mute state, including absent layers. Opening or reopening a movie
starts with all layers unmuted and no solos. These temporary choices are never
written to movie files or saved application settings.

Headless playback checks cover live MIDI mute/solo, multiple solos, mute taking
precedence, muted sampled queues completing sound waits, carried layers, scene
and FPS changes, absent-layer reset, background analysis and reopening. The
playback/player regressions and Marcus C89 check pass; no Marcus UI tests were added.

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

While paused, choose **Sounds > Add sound...** to import WAV/MP3 effects or
speech, or a standard MIDI file for music. Use the native file chooser or enter
a path, choose the scene frame, volume, loop and queue settings, and audition
with **Preview** before adding. The same button becomes **Stop preview** while
audio is playing, and returns to **Preview** when it finishes. Audio is embedded in the
movie; it does not depend on the original file after saving. Music uses MIDI
to retain retail compatibility. Import currently accepts files up to 64 MiB.

The dialog also shows the original effects, speech and music from `snds.3cn`,
even when the scene has no sound cues. **Show built-in sounds** is on by
default; turn it off to hide the library while working with scene sounds,
imports or recordings. Search and the **All / Effects / Speech / Music**
filters narrow the library. Selecting a named sound automatically previews it,
stopping the previous preview. The dialog's scene list behaves the same way.
Use **Stop preview** to stop it or **Preview** to replay, then **Add sound** at
the chosen frame. Addition embeds the original audio
and name without changing the library, and is one undo step. Existing scene
cues remain listed separately when the library is hidden.

**Record microphone...** captures your system's default microphone. The dialog
shows elapsed recording time and stops automatically after three minutes, or
you can choose **Stop recording**. Preview the waveform and audio, choose
speech or effects, and use **Add sound** to embed the take at the selected
scene frame. **Record again** replaces the pending take; **Discard recording**
returns to file import. Closing the dialog or selecting another sound discards
an unadded take and releases the microphone. Recording itself leaves the
movie unchanged; adding the take is one undo step. Capture uses Studio's shared
recorder at 48 kHz mono PCM with its retail-compatible processing, through
a narrow C interface; private WAV files are removed after use.

Select a cue and choose **Edit sound...**, or select it from the dialog's
searchable scene list. Scene sounds support timing, volume and loop changes;
sampled sounds can switch between effects and speech. Frame, category, volume
and loop belong to the whole native queued packet. **Remove sound** deletes
only the selected queued record. Actor sounds retain their animation/cel
binding and category while allowing volume/loop changes or removal. Carried
cues edit their original scene, identified in the dialog. Waveforms reflect
the selected volume; auditioning leaves the document and timeline position
unchanged and follows the movie's mute/solo settings.

Each add, edit or removal is one undo step. Earlier actor/text/costume,
placement, camera, frame and storyboard edits are retained, and sounds save
to native `.3mm` movies or storyboard `.vmm` bundles. The controls use C89 and
cimgui in the shared ImGui context. The headless `marcus-sound-edit` regression
covers import, native save/reopen, queues, actor bindings, audition and undo;
no Marcus UI tests were added.

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

The sheet uses the selected movie rate's presentation clock, including
scrolling-text holds, authored time/sound waits and outgoing transitions. Scrolling uses playback's
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

**6 FPS (Original)** is the default movie frame rate. **12 FPS** and **24 FPS**
set faster rates. **Uncapped** removes the movie-frame delay. The
**Compatibility speed modifier** defaults to **Normal (1x)**. **Win98 -> XP
(1.35805x)** and **XP -> Win98 (0.918485x)** multiply the selected rate.
Uncapped ignores the modifier.

The editor status bar has **Frame rate** and **Speed** dropdowns that apply
and save changes immediately. In Settings, **Apply** saves both choices,
**Cancel** discards edits, and **Restore defaults** selects **6 FPS (Original)**
and **Normal (1x)**. Saved **Original** and **6 FPS** choices both load as the
merged option. Normal 6 FPS uses the original ten ticks per frame on a 60 Hz
clock. Faster and modified rates use Studio's fractional millisecond pacer
and rate calculation. Marcus advances one authored frame per due update and
starts a fresh interval if a poll arrives more than one original clock tick
late. Pause and resume preserve the remaining interval. Actual throughput depends on
rendering and display updates, including in Uncapped mode.

These controls change movie-frame advancement without interpolation or changing
audio pitch/rate. Click, sound and timed waits, scrolling captions and transitions
retain their normal timing, so audiovisual synchronization can change. Fixed-rate
duration calculations, sound-sheet waveforms and audio seek offsets follow the
selected rate, including in the background timing helper. Uncapped keeps nominal
six-FPS duration/seek previews because its actual throughput depends on the host.
The browser's frame-count estimate and Calculate duration remain nominal six-FPS
previews, independent of the active player's settings. Preferences never modify
movie or asset data.

Headless playback checks cover all 15 rate/modifier combinations, ten seconds of
simulated 60 Hz display updates, pause/resume, late callbacks, authored click/time
waits, unchanged dissolve timing and matching foreground/background duration
calculations. Settings checks cover persistence, legacy defaults and invalid or
duplicate entries. Marcus playback/player/renderer checks, both C89 syntax checks
and `engine-core-link` pass; Marcus and SDL Studio build successfully. No Marcus
UI tests were added.

**App debug overlay (FPS / frame time)** defaults to **Off**. Enable it and
Apply to show display FPS and frame time in milliseconds, with a separate live
graph for each, in both editor and expanded movie views. App and camera panels
also show the current numeric FPS above the graphs. The values average the
latest second and the graphs retain the last 180 individual frames, with an
automatic scale shown above each graph. Timing includes rendering, presentation
and frame pacing; it measures Marcus's display loop, not the movie's authored
frame rate. Open a camera's **cog** to inspect its performance panel in that
camera's rendering settings dialog. Each dialog also has a saved **Show stats
over camera view** toggle, defaulting to **Off**, to display that camera's live
stats and graphs over its view. The two camera toggles work independently of
each other and of the app overlay. The app panel sits at the right of the window;
camera panels sit inside their respective views. Their **Render FPS** counts
actual image renders per second;
reusing a cached image does not count as a render. **Render time** averages the
image generation calls for that camera, including renderer setup and synchronous
GPU completion, but excluding texture upload, display wait and frame pacing.
Each camera has its own FPS and render time graphs, sampled on the app frame
clock; a zero render time point means the cached image was reused. After a second
without renders, the panel shows **Cached** and retains the last render time.
Opening a camera's settings captures its recent stats and graphs before pausing
playback. That snapshot stays visible until the dialog closes, and describes the
applied settings rather than pending edits. Reopen the dialog after applying a
change and playing the movie to capture fresh measurements.
Camera measurements are recorded even when the overlays are off, so their
settings dialogs always have recent stats. Enabling an overlay requests a fresh
render of the visible cameras so paused views also have a measurement. The
toggles are saved with the other settings.

**Apply** activates and saves the edited settings; **Cancel** discards edits.
**Restore defaults** resets all settings, which still needs Apply to save.
Each first/previous/next/last action can be bound separately. Reset View defaults
to R when the object gizmo is unavailable. Arrow/Home/End/Page Up/Page Down remain
fallback navigation keys. Flight uses the physical ESDF and W/R keys while right
mouse is held.

The **Movie camera settings** and **Work camera settings** buttons in Settings
open rendering controls. Their renderer selector offers **BRender (CPU C)**, **BRender (compute)**, **SDL GPU** and
**BRender (CPU ASM x86)**. Apply
updates each camera and saves its choices with the other settings. Switching preserves the current
scene, frame, camera and inspection framing. Cancel discards the edited choice;
Restore defaults selects BRender CPU. Existing settings files without a renderer
entry continue to use CPU rendering.

You can override both cameras' saved renderers for a launch:

```console
marcus movie.3mm --renderer brender-compute
marcus movie.3mm --renderer sdl-gpu
marcus movie.3mm --renderer brender
marcus movie.3mm --renderer brender-asm
```

BRender CPU remains the default. The two GPU backends use SDL3's GPU API, with
bundled SPIR-V and DXIL shaders for Vulkan and Direct3D 12. BRender still evaluates
models, transforms, lighting and clipping. **BRender (compute)** rasterizes the
resulting triangles in a compute shader, using 16-by-16 pixel tiles and ordered
depth comparisons. **SDL GPU** sends those triangles through the GPU graphics
pipeline. Both retain indexed textures, shade tables, authored background depth
and perspective scenery, then feed the common caption/transition compositor.
The SDL interface renderer is independent of this movie-renderer setting.

On Windows, **CPU ASM x86** uses a persistent 32-bit helper linked to the original
BRender MASM routines, including geometry/math fast paths and scan converters.
The x64 player sends scene/frame and camera requests over binary pipes and gets
RGBA, depth and projection data back. Captions, scrolling, audio and transitions
remain in the host's common compositor; no PNG files are exchanged. Both movie
and work cameras support this backend, with the same display options as CPU C.
This is an additional comparison baseline, not a guarantee of retail parity.

Build the helper from an **x86 MSVC developer shell**, in its own build directory:

```console
cmake -S . -B build/marcus-x86 -G Ninja -DCMAKE_BUILD_TYPE=Debug -DBUILD_MARCUS=ON -DBUILD_STUDIO=OFF -DBUILD_PACKAGES=OFF
cmake --build build/marcus-x86 --target marcus-asm-worker
```

Then build/run the normal x64 Marcus from its x64 developer shell. The workspace
build finds `build/marcus-x86/src/marcus/marcus-asm-worker.exe`; a distributed app
should put that helper beside `marcus.exe`. `MARCUS_ASM_WORKER` overrides the path.
Rebuild the x86 helper whenever updating Marcus's engine or live authoring
code; rebuilding only `marcus.exe` does not rebuild the separate x86 tree.
The ASM6 handshake requires live actor insertion support. Helpers from the
earlier prop-only implementation are rejected with an explicit rebuild message,
rather than failing to render an actor just added to a black scene. The
headless `marcus-asm` check covers that sequence, another actor insertion,
work-camera rendering and undo, through the real helper process.
A missing helper causes an explicit selection error; startup retains the existing
CPU fallback policy. The helper verifies a versioned 32-bit handshake, retains
the loaded scene between frames, and closes when its player closes or opens a
different movie. The original x86 assembly backend is Windows-only.

CPU parity repairs preserve each Body's actor hierarchy, local transform types,
inherited materials/styles and sibling order instead of flattening parts into
world space. Actor attachment order follows the shared evaluator's actual Show
events; hiding/re-entering an actor can change which equal-depth surface wins.
Both SDL and Marcus also use retail's scalar multiply/add policy: truncate each
16.16 product before adding, with 32-bit wrap. This differs from the standalone
full-accumulation `BrFixedMac*` assembly primitives, which remain unchanged.
The pinned ISO executable's `BrMatrix34Mul` at RVA `0x76ac4` and
`BrMatrix4Pre34` at `0x7c84a` confirm the individual shifts. The C89 actor math
and both BRender scalar headers use the same policy. Actor root/part matrices
were checked directly against the retail bridge, including generated 3D text.

The portable one-sided on-screen projector now follows `mesh386.asm`: fold the
viewport into the matrix, keep 32.32 translation and full product accumulation,
then divide before narrowing. The former generic C path rounded clip-space
coordinates earlier. Two-sided/clipped models retain their corresponding
retail paths, including the on-screen overflow fallback. A headless actor-tree
test checks pixels, depth, sibling ordering, inherited state and embedded actor
ownership. Its projection hash comes independently from the x86 assembly run;
restoring the former C projector fails that check. No Marcus UI tests were added.

Ordered captures at 544x306, comparing exact RGB pixels against existing retail
captures, produced these results (totals count differing pixels across frames):

| Movie | Frames | CPU C before → after | CPU ASM before → after |
| --- | ---: | ---: | ---: |
| Rebirth of 3DMM | 952 | 154,155 → 10,363 | 154,710 → 10,362 |
| Ringworld Act One Teaser Trailer | 153 | 28,617 → 260 | 28,787 → 260 |
| The Amazingly Pointless Adventures of Gustave! | 404 | 76,136 → 3,856 | 76,535 → 3,856 |

C and ASM are pixel-identical in 1,508 of these 1,509 frames; the remaining
frame has one pixel different between them. Rebirth scene 4/frame 16 improves
from 1,134 differing pixels to 4, scene 8/frame 63 from 1,601 to 3, and scene
19/frame 7 from 1,725 (C) / 1,722 (ASM) to 1. Ringworld's Paybox logo now matches
retail exactly. The worst remaining frame in these runs differs at 122 of
166,464 pixels. This is a measured improvement, not complete retail parity:
small residuals remain, and 25 Rebirth frames have more RGB error than before
(Ringworld and Gustave have none). Capture history matters: a direct jump can
give a different retail z-fighting result from visiting every preceding frame.
Local metrics and decoded frames are under `tmp/cpu-project-parity-*-full/`;
retail matrix/order probes and the disassembly evidence are in
`tmp/cpu-retail-order/` and `tmp/cpu-retail-*.txt`.

The compute and graphics renderers now follow the integer Gouraud and affine
texture scan converters for the default movie view. The triangle sink supplies
original fixed-point coordinates and varying values alongside float values;
float conversion alone loses low depth bits. CPU triangle setup prepares the
native edge reciprocals, gradients, winding/tie order and half-X bias. The GPU
shaders derive each scanline's span and parameter values with 32-bit integer
carry/wrap arithmetic, then perform texture lookup, shading and depth tests.
Untextured faces reject equal depth; texture maps accept it. Negative UV wrap
uses unsigned magnitudes to give matching DXIL and SPIR-V results.

Compute visits the ordered triangle lists for each tile. Graphics draws each
triangle's bounding rectangle and discards fragments outside its integer span;
this avoids hardware triangle coverage clipping away legacy pixels. Contiguous
runs use strict or inclusive depth pipelines in original submission order, with
an explicit triangle-buffer base per run. Palette filtering, AA, wireframe,
depth and normals options remain available. Perspective-specific converters
retain floating-point interpolation; parity of those work-camera/scenery paths
and enhanced settings is not established by the default movie captures.

Paired captures using the corrected CPU renderer before and after the GPU
changes, at 544x306 with nearest sampling, gave these exact RGB differing-pixel
totals against the same retail images:

| Movie | Frames | Compute before | Graphics before | After, each GPU backend |
| --- | ---: | ---: | ---: | ---: |
| Rebirth of 3DMM | 952 | 11,804,130 | 11,805,096 | 10,363 |
| Ringworld Act One Teaser Trailer | 153 | 1,591,371 | 1,591,355 | 260 |
| The Amazingly Pointless Adventures of Gustave! | 404 | 4,573,930 | 4,574,800 | 3,856 |

Both GPU backends match the CPU C output exactly in all 1,509 frames. CPU pixels
are unchanged, and no GPU frame has increased RGB error against retail. The
remaining retail differences are the CPU residuals described above. All seven
selected movie samples also match CPU on Vulkan, including the now-exact
Paybox logo. Full metrics and decoded frames are in `tmp/gpu-parity-*-full/`;
Vulkan samples are in `tmp/gpu-parity-vulkan-samples/`.

`marcus-raster-parity-test` compares indexed colour and depth directly with
BRender's scan converters across 120 synthetic frames per graphics API. Cases
cover fractional coordinates, flat tops/bottoms, both windings, viewport edges,
negative/pitched/non-power-of-two textures, transparency and interleaved equal-
depth triangles. The Windows Vulkan test selects `SDL_GPU_DRIVER=vulkan` to
exercise the SPIR-V binaries separately from the default Direct3D 12 path.
Both API tests also pass in the x86 build against the native assembly scan
converters, as well as the x64 build against their portable C counterparts.
Renderer content integration also exercises work cameras, orthographic views,
picking and display modes. These are headless checks; no Marcus UI tests were
added. SPIR-V modules validate against Vulkan 1.0 with `spirv-val`.

Completed GPU frames are downloaded for the shared caption compositor and RGBA
player API. These captures establish image parity, not a rendering speedup.
Metal shader binaries are not supplied. Unsupported GPU initialization keeps
the previous renderer when switching in Settings. A saved GPU choice that
cannot initialize at startup falls back to CPU with a message in Settings;
an explicitly requested `--renderer` fails with a diagnostic.

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
The Frame row sits above the Scene row, with matching navigation groups.
The Play button spans both rows beside the navigation controls and changes to
Pause or Continue as playback changes. Ctrl+clicking the previous/next frame
button at a scene boundary adds a frame. Shift+Play enters the expanded movie
view. Boundary buttons otherwise disable unavailable actions.

The transport, camera toolbar, track filter, layer Solo/Mute controls, object
list, inspector, split handles and status bar use cimgui. Scene objects and
properties have standard ImGui scrolling. Timeline markers, waveforms, animation
lanes and camera/light guides use ImGui drawing APIs. `transport_skin.c` draws
the SVG transport artwork from `src/marcus/icons/` using ImGui drawing APIs on
standard buttons. Regenerate its checked-in triangles with
`python tools/compile_marcus_vectors.py`. The former SDL controls and their
standalone `marcus-dialog-capture` renderer remain retired.

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
families try the selected ZIP's private font catalog before installed fonts and
the System fallback. Enlarged views rasterize font outlines at
the render resolution with supersampled edge coverage, instead of enlarging the
original glyph pixels. Layout, line breaks, baselines and scrolling still use
the original 544x306 workspace metrics; native-size movie rendering retains its retail
glyph masks. Repeated high-resolution glyphs share a bounded 64 MiB cache.
Saved text bounds map from that workspace independently of background bitmap
dimensions, so captions near the right edge remain visible at any render size.
Scrolling credits hold their first frame
and move one original pixel every five 60 Hz ticks until the overflow is visible,
then playback continues. Scrubbing resets their scroll position. Orbit and Fly
render the same captions onto a transparent quad at the authored camera's amber
display frame, 10 world units in front of that camera. The quad preserves its
field of view, aspect, roll and camera cuts. It stays with the movie camera while
the inspection camera moves, and nearer actor geometry can occlude it. Its caption
texture scales with the work render resolution, up to 4096 pixels wide. Both sides
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
The guide depth range is independent of the tighter planes used to render
scene geometry.
Orbit places its pivot in front of the matched camera, using visible actors to
choose the focus distance. After navigation that pivot stays fixed during
playback, scrubbing and camera cuts. Shift + right drag pans the pivot in the
screen plane, with speed following zoom, lens and viewport size. Subsequent
rotation and zoom use the panned pivot. Inspection views use a far clipping
distance of 30,000 world units, independent of cached actor bounds.

**Ground grid** in the Orbit and Fly toolbar projects the world XZ plane at
Y=0 over the work view. It stays aligned to world units as you move and is
available in both perspective and orthographic work views. The grid uses the
same clipping range as the camera markers and the work view's rendered depth
buffer, so foreground objects and scenery hide it.
The movie image is rendered without the guide.

**Copy to camera** moves and reorients the current scene's objects from the
playhead onward so the fixed movie camera sees the framing from the paused
perspective work view. Earlier frames retain their original composition. The
work viewpoint aligns with the movie camera after the copy while Orbit or Fly
stays active, so you can set a different view on the next frame and copy again.
It works on retail backgrounds, including Black, and on
storyboards. The camera, background, lights, and captions stay in place. The
edit retains route timing, saves with the movie, and is one undo step.

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

In **Object Properties**, click **Animation...** to browse the selected
object's actions. Pick a thumbnail to see the action play in the preview.
Drag the preview to rotate it and use the mouse wheel to zoom. **Start cel**
counts from one. **Freeze pose** holds that cel. Click **Apply action** to
start the action at the current playhead.

To record motion, pause where it should start, select the object, and click
**Draw path** under **Path**. Drag in the viewport to trace a path from the
object's current position in the plane facing the camera. Release to apply it or
press Esc to cancel. The action's stride controls how much distance each frame
covers. Stationary actions and props use a fixed distance per frame. With
**Replace remaining path** cleared, Marcus attempts to rejoin the following
path. Recording can extend the scene. **Edit path point** adjusts the object's
world XYZ at the playhead. Each edit is one Undo step, and **Save copy** writes
the action, path and position adjustments into the native movie.

The C89 engine drives action changes, path sampling and lifetime updates using
the actor editing primitives shared with Studio. It installs the edited route
and event chunks on the selected scene's actor and extends the scene when
required. The animation regression produced the same native movie bytes as
before the migration. A fresh retail run matched the retained captures at
frames 1, 2, 25, 75, 125 and 150 pixel for pixel. Rebuilt SDL Studio matched
retail's scene and actor state and the movie viewport at all six seeks.
The captures and comparisons are in `tmp/c89-animation/`.

Select a visible actor, prop or 3D text while paused to show its world-axis translation handles.
The viewport's **Move / Rotate / Size** toolbar and **W / E / R** choose the active
tool. **Rotate**
shows local rotation rings; **Size** shows per-axis handles and a centre handle
for uniform sizing. Object and storyboard light gizmos use 1.5× scale for
their handles and grab areas. Rotation and size apply to the whole object track
while preserving its position, path, action/cel timing and lifetime. Size uses positive
multipliers from 1% to 1000% of the authored dimensions. Foreshortened scale axes
keep a minimum projected length; exactly end-on axes use a ring around the centre
handle and drag vertically. All scale handles remain visible during a drag.
Hold Ctrl to snap rotation to 15 degrees or size to 10% increments. The properties
fields follow the selected tool, showing world position, rotation in degrees or
size percentages. Drag a field or Ctrl+click to type a precise value. Escape or
focus loss cancels a preview; release commits one Ctrl+Z undo step. Preview,
commit and undo remain in memory, retain the duration cache and hold the work
camera fixed. Scene changes and resource reloads preserve committed transforms.
Native thumbnail snapshots encode these edits as rotation and stretch events;
size changes preserve the retail movement speed instead of changing the step size.

Drag an axis or plane to move it; hold Ctrl for a one-world-unit snap.
Arrows retain the real positive world-axis directions. Foreshortened arrows have
a minimum pickable length along their actual projection; exactly end-on axes
show their circular end cap at the gizmo origin, draggable up/down for depth.
All three plane squares remain visible from the fixed movie camera and work
camera, including during a drag. Narrow or edge-on squares expand around their
projected centers to retain a minimum pickable thickness. Their hit regions
match the displayed squares, and dragging uses the expanded geometry to avoid
unstable ray/plane intersections. Finite coordinates stop at the +/-32700
world-unit limits. Parallel or behind-camera drag intersections hold the last
valid pose instead of reversing movement; dragging back resumes movement.
Non-finite coordinates also retain the last valid value. Releasing commits the
limited pose.
When the object's origin projects outside the viewport, its transform handles
stay inside a padded screen boundary. This moves only the displayed controls;
the object's actual position and rotation pivot are retained. Moving the handle
applies its full world-space delta to the object, which can move off-screen.
During a drag only the gizmo drawing is pinned to the viewport edge; axis/plane
constraints and snapping keep their original drag reference. The
drag-start marker stays fixed, with its feedback line ending at the pinned
handle. Its coordinate readout continues to show the actual world-space delta.
This framing works for perspective and orthographic views, including camera roll
and the movie camera's letterboxing. Resize or focus loss cancels an active drag.
The World Position X/Y/Z fields also support dragging, or Ctrl+click to type a precise
coordinate. Escape cancels the current move, and losing window focus cancels it.
The selected object updates live; releasing the handle or finishing a field commits one
move to the session. The placement commits in the live scene without copying
or reopening the movie, seeking, restarting a timing helper, or clearing the
duration, animation, caption and waveform caches. Undoing a placement on the same
base movie also keeps those caches. The current frame and inspection framing
are retained. Orbit selection keeps its camera and pivot fixed while placing an
object, including release, cancellation and undo. Animation playback still follows
the selected object's posed bounds; refitting explicitly recenters the view.
Native private snapshots are materialized lazily for frame/camera thumbnail
helpers. In the x86 ASM renderer, live and committed
placements use the same existing worker over IPC, retaining its orbit framing
throughout a drag; moving an object does not restart that worker.

Gizmo and position-field input is applied before rendering either viewport, so
the object and its handles use the current drag update in the same presented frame.
The editor follows the normal vsync cadence without an additional idle delay;
holding a stationary handle does not trigger redundant scene renders.

A headless Debug-build check on 15 November 2026 measured 100 release commits at
0.008 ms median and 0.100 ms maximum on the synthetic two-scene fixture. These
measure commit work, excluding scene rendering. It also verifies that duration
is immediately available after every commit and undo, rather than reanalyzed.
Headless movement checks also cover animated actors and 3D text: preview/cancel,
undo, unchanged poses and timing, whole-path offsets across frames, scene
isolation, reload and native thumbnail materialization, with portable and ASM
CPU rendering. No application UI input is simulated.

A move offsets the object's entire track in this scene, preserving its animation,
costumes, route shape and lifetime. Other scenes that share its actor chunk keep
their original placement. Off-stage objects are read-only.
Coordinates are limited to +/-32700 world units. Original movies, ZIPs and content
archives remain unchanged. These session edits still have no Save/Export command.

Ctrl+click Previous Frame at the beginning or Next Frame at the end extends the
scene by one frame and selects the new frame. Ctrl+click inside the existing
range steps normally. The shortcut is available while paused; on macOS, Cmd
also works. Prepended frames use earlier absolute frame numbers, including zero
and negative numbers, while the scene counter still starts at 1. Existing actor
entrances, animation, captions and sound cues retain their authored frame numbers.
The opening camera cue follows the new beginning using retail's shared scene
policy; later camera cuts retain their timing. Each added frame is one undo step.
Frame additions and their undo update the live scene range and a small session
journal. They do not copy/reopen the movie, reload content, restart the ASM worker
or launch another timing analysis. Duration and sound scheduling reuse cached
cue lengths, pauses, scrolling holds and transitions. A timing helper already
running against the original movie finishes normally and applies the latest
range edits when its result arrives.

A headless Debug-build check measured 100 frame additions at 0.321 ms median and
0.407 ms maximum on the synthetic two-scene fixture, excluding scene rendering.
Duration remained immediately available after every addition and undo. Native
frame ranges are written in one deferred snapshot alongside object placements
when a thumbnail helper needs a native movie.

Scene creation, prop insertion, authored camera changes and their undo also use
the live evaluator. Scene and prop definitions and camera cues use small shared
session journals; undo retains those records rather than copies of the movie.
Adding a prop creates its instance while keeping existing actors and loaded
templates. Creating a scene loads its chosen background through the open content
caches. Camera changes replace only the cue data. These actions preserve timing
analysis and the running ASM worker, which receives the edited definitions over
IPC. New movies need no initial movie file. Asset loading and journal updates
still allocate memory; whole-movie copies and content reloads are deferred.

A headless Debug-build check on the synthetic two-scene fixture measured:

| Warm edit | Median | Maximum |
| --- | ---: | ---: |
| Camera cue, 100 changes | 0.257 ms | 0.420 ms |
| Prop instance, 20 insertions | 0.163 ms | 0.171 ms |
| Scene, 10 creations | 0.496 ms | 0.865 ms |

These measurements exclude rendering and initial catalogue/template loading.
Duration remained ready throughout, including after undo. Headless checks also
cover edits during timing analysis, mixed undo/reload, portable and ASM rendering,
new movies without a base file, and matching pixels from deferred native snapshots.

Frame/camera thumbnail helpers receive one cached native snapshot containing all
scene, prop, camera, placement and frame-range edits. The snapshot is rebuilt
only after further edits, and its creation does not replace the live movie.

Ctrl+Z (Cmd+Z on macOS) undoes the most recent completed costume change, object move, rotation, resize, prop insertion,
frame addition, camera change, storyboard stroke/adjustment or new scene. Each drag or completed position-field edit is one
step. Up to 64 steps are retained in the current session, including after a
resource reload. Undo restores the movie and the pre-edit scene/frame, retaining
the inspection view. Undoing the first scene returns an untitled movie to its
empty state. Returning to the original state clears the unsaved marker.
Opening or creating a new movie clears history. Playback, selection and navigation do not add steps.
During a drag, Ctrl+Z cancels that drag; while typing a position, it uses the
field's local text undo. Original movie and content files remain unchanged.

## Current limits

- Scene creation, frame additions, camera changes, storyboard authoring, prop insertion and object transforms are session
  edits; native working copies are materialized as needed. **Save copy** exports
  native movies; overwriting the active document in place and packaging content are not implemented.
- Storyboards support up to 64 angles, with a separate flat drawing per angle.
  Drawing has no layers or depth-painting tools. Existing retail background resources
  are not edited by the storyboard tools.
- Actor/prop animation, costume changes, 3D text, authored cameras and
  prerendered backgrounds/depth use the existing preview evaluator.
- Caption glyphs use bundled ZIP fonts or available host fonts, and MIDI uses a software instrument
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
- UI text uses the shared ImGui font atlas. `--font file.ttf` overrides its
  default font. No proprietary UI font is bundled. FreeType remains in native
  movie font services; it no longer implements Marcus controls or text editing.

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

`src/marcus/main.c` and `ui.c` implement the application shell and ImGui
drawing helpers in C89. `text_dialog.c` owns the New/Edit 3D text dialog's C89 state,
catalogues, material/texture controls, transactions and cimgui widgets. Its
manual caret, selection, clipboard and field hit-testing code has been removed.
`text_preview.h` exposes opaque native preview services: template preparation
stays on the player owner thread, while the existing isolated worker decodes
the preview. `text_preview.cpp` adapts those services and third-party palette/
colour conversion without owning UI state. The existing insertion check also
verifies C preview cancellation/restart, single-result publication, unchanged
document identity and exact pixel equality with the direct worker, including
font/shape/material gallery variants loaded into a persistent session. No Marcus
UI tests were added.

`dialog_views.c` and `dialog_widgets.c` own the picker widgets and
input in C89: Open movie, New scene, Add actor or prop, scene/frame organisers
and camera angles. `scene_organiser.c` owns organiser selection, single-scene
and movie-wide frame indexing, zoom, and thumbnail caching. It looks up cached
textures through a sorted index and submits up to 32 visible missing frames
through the existing C preview-worker interface. Results carry authored scene
and frame identifiers so an in-flight image can be mapped after a scope change.
The existing headless preview check verifies frame identity across scenes,
pixel equality with the authored camera, and cancellation. The actor and
scrolling-text fixtures retained all 180 SDL Studio and Marcus viewport hashes.
Evidence is in `tmp/c89-scene-organiser`.

`camera_picker.c` owns angle selection, storyboard actions and requests for
the selected and visible angles. `background_browser.c` owns background and
model selection, actor/prop filtering, thumbnail priority and texture limits.
Each model keeps its preview identity when the filter changes. Background
warm-up uses the existing disk cache through `background_preview.h`, which
also opens the isolated model worker. SDL uploads each result on the main
thread and retires textures after ImGui submits the frame.
The headless preview check covers camera and model requests. A C probe of
the background adapter matched repeated results to the direct worker and
received both actor and prop images with their requested card identities.
Evidence is in `tmp/c89-camera-asset-pickers`.

`movie_browser.c` owns navigation, sorting, filtering, selection, recent and
favourite locations, and the thumbnail cache. Pending work and cached thumbnails
use the listing row's identity even when filters or sorting change its position.
The worker interface supplies directory and ZIP entries, transfers a prepared
movie input, and returns preview images and duration results. Replacing a listing cancels
obsolete preparation. `movie_browser_preferences.c` preserves the quoted
`movie-browser.cfg` fields and writes updates through the temporary file.
The headless worker probe exercised a single-member ZIP thumbnail and open,
multi-member ZIP navigation and duration lookup, and a VMM inner-movie preview.
It also round-tripped legacy paths, quotes, and browser preferences. Evidence
is in `tmp/c89-movie-browser`.

Settings, camera rendering settings, ZIP member selection
and errors use standard cimgui controls in `main.c`. The dialogs stay centred
and fit the available window when resized. Browsers retain asynchronous loading,
isolated preview workers and bounded caches through the copied data/command
interface in `dialog_host.h`; their C++ files contain backend services rather
than widget drawing or manual text editing. Cached SDL textures are retired
after the shared ImGui frame has been submitted. Native OS file/folder choosers
remain platform dialogs. `ribbon.c`, the editor panels in `main.c`, `prop_fields.c` and
`storyboard_view.c` implement all remaining controls in C89/cimgui. Standard
widgets own buttons, tabs, lists, scrolling, colour selection, numeric fields,
menus and debug plots. `ui.c` now contains cimgui layout helpers and drawing
primitives for viewport/timeline artwork, with no FreeType controls or manual
field editing. `storyboard_editor.c` owns camera and light values, paint
gestures, field edits and the decision to commit a draft or change shots.
`storyboard_edit_data.c` supplies brush strokes, paint-slot allocation,
projection and light transforms. The C++ backend supplies draft pixel storage,
palette matching, preview geometry, serialization and native movie edits
through a C interface. `storyboard_view.h` carries copied control state and
borrowed preview geometry to the cimgui view.
`prop_gizmo.c` owns the shared cimgui context and frame, transform gestures,
and prop preview, commit and restore decisions. Prop and storyboard light
controls share clipping, projection and snapping in `transform_gizmo.c`.
`imgui_bindings.cpp` connects the C callers to the SDL backends and ImGuizmo.
The native binding runs Dear ImGui's C++ layout check. The generated C fields
used by the controller have matching offsets. A full selected-object screenshot
matched byte for byte before and after the conversion, including the gizmo and
numeric fields. Evidence is in `tmp/c89-gizmo`.

The storyboard conversion passed the existing headless storyboard authoring
and VMM round-trip test. A headless probe exercised the C draft interface for
inking, adding a shot and a light, rendering, exporting, undoing and reloading.
The selected-object viewport capture also matched the previous build byte
for byte. The two seek fixtures matched the previous build across 228 Studio
and Marcus states and 180 viewport hashes. The 456 Studio actor records in
these fixtures matched the retained retail and V3DMM captures. Evidence is in
`tmp/c89-storyboard`.

`player.h` exposes an opaque player, copied POD records, integer success results
and borrowed RGBA pixels. No C++, native platform or SDL types escape that API.
`player.cpp` owns the C++ Archive/Preview backend. `playback.cpp` adapts SDL
audio output, decoded clips, SoundFont synthesis, caption layout and transition
snapshots for the C89 playback controller and movie mixer.
`content.cpp` adapts the
shared C89 VFS to the existing pack and bundle libraries. Exceptions stop at the
C boundary. `Preview::Objects()` supplies evaluated actor state.
The C89 `mm-engine-c89` library supplies playback and startup policy, frame
pacing and retail scrolling. Marcus polls its playback controller. Studio uses
its event-driven evaluator. See the [engine boundary](../code-map/library-split.md#c89-engine-backend).
Movie opening now also uses the shared C89 MVIE-version and roll-call record
decoders. Archive's GST wire-layout validation runs through the C89 bounded
stream interface; legacy string conversion and chunk/resource ownership remain
behind the C++ host adapter.
The C89 movie mixer owns voice position, queue order, track controls and
absolute MIDI event dispatch. It uses the same PCM accumulation and clipping
as Studio. The SDL callback mixes previously decoded audio and advances its
prepared synths. Archive reads and MIDI preroll run before publishing a voice.
The C89 frame dispatcher selects earlier scene music when restoring it,
plays current scene and actor cues in order, and selects the last authored
wait on the frame. An explicit actor cue takes precedence over motion
matches for that actor and sound style.
The C89 caption renderer keeps the visible stack across seeks, using the
same lifetime rule as Studio. It fits the 544×306 workspace into the movie
view, samples font masks, blends caption colours, and projects the composed
layer through the work camera with depth occlusion.
Caption layout decodes authored font, style, colour and paragraph changes in
C89 and uses Studio's C89 rich-text formatter for wrapping and placement.
The font host measures and rasterizes the UTF-16 runs. Caption scrolling uses
the height from that same layout pass.
The C89 transition engine prepares the outgoing movie-camera snapshot and
stores the dissolve palette indices. Snapshot preparation and Studio's
indexed transitions use the same authored palette overlay.
The C89 movie poller coordinates audio restore, frame advance, scene looping
and outgoing capture before selection. It publishes playback status after
host effects complete. Transition completion releases the outgoing image
and dissolve indices together.
The C89 scene cursor coordinates first open, scene switches and seeks.
Selection publishes after playback, objects and sounds are ready. Switches
apply edited ranges, cameras and placements in order, and a successful
seek resets playback before refreshing the objects.

This is **C89 application source over modern libraries**, not a strict-C89 port
of every dependency. SDL headers still use boolean/64-bit extensions; SDL and
generated cimgui headers are system headers during C89 checking.
Dear ImGui, cimgui's generated implementation and ImGuizmo still require C++.
Engine conversion is
incremental; a C89 frontend does not establish whole-backend C89 compatibility.

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

Bundled-font checks use a local font with deliberately widened advances to
verify precedence through changed caption pixels and scrolling duration,
matching frame-helper pixels, consistent background timing and restored host
fonts after reopening a plain movie. ZIP checks cover nested/renamed font files,
VFS mounting, invalid font/size/path rejection, cache cleanup and unchanged ZIP
bytes. No font binaries or Marcus UI tests are added to the repository.

On 9 November 2026 the native ZIP input extracted all 103 movie entries from the
87 ZIPs containing native movies in `movies`, including the Shrink-compressed
`aacw01.zip`. All selected entries passed size/CRC checks, original ZIP bytes
remained unchanged, and temporary directories were removed. Two other ZIPs had
no `.3mm`/`.vmm` entries. This verifies extraction, not playback of every movie.

After the C89 migration the corpus check covered 130 movie entries in 104
archives, including `aacw01.zip`. ZIP inputs for the actor-event and scrolling
caption fixtures matched the plain movies across 114 seeks and 90 viewport
hashes. Evidence is in `tmp/c89-movie-zip/`.

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

## Opening profile: 2004 awards movie

On 11 November 2026, two temporary instrumented executables linked the current
`build/marcus-x64` Debug libraries and opened `movies/3dmmcom_awards2004.zip`.
The ZIP contains one 101.45 MiB VMM, `2004 3dmm.com Awards Movie.vmm`. Its inner
movie is 99.12 MiB with 1,435 scenes; the bundle declares 26 dependencies,
including 23 embedded packages with 46 content files totalling 10.75 MiB.

| Phase | Run 1 | Run 2 |
| --- | ---: | ---: |
| ZIP directory listing | 0.0015 s | 0.0015 s |
| ZIP extraction | 0.780 s | 0.810 s |
| VMM content setup | 43.366 s | 45.499 s |
| Movie archive index | 6.096 s | 6.175 s |
| Movie metadata | 6.668 s | 6.754 s |
| Preview runtime opening | 3.552 s | 4.095 s |
| Content mounting | 1.407 s | 1.676 s |
| First scene selection | 0.071 s | 0.077 s |
| All-scene duration analysis | 30.813 s | 32.201 s |
| First CPU render, 544x306 | 0.339 s | 0.297 s |
| Total, including setup and teardown | 93.470 s | 97.986 s |

The detailed run spent 45.231 seconds in the first embedded-package lookup.
`MarcusContent::Impl::Package` calls `Catalog`, which constructs a
`StudioV3dmm::Store`. That constructor eagerly reads every installed catalog
entry; `Store::Read` hashes each VXP archive and its extracted content. This
machine's catalog contains 756 packages and 2,268 validated files, approximately
1,938 MiB in total. This is work for the entire installed library, including
packages not required by this movie. Subsequent package lookups took between
0.0001 and 0.023 seconds; copying the inner movie took 0.136 seconds. The outer
ZIP decompression is a small part of this profile.

The browser's Open handoff reuses its prepared ZIP
extraction, but still runs normal content setup and movie loading.
`main.c::refresh` then calls `marcus_movie_time`, which reads the timing of every
scene before the first displayed frame. That explains a further 31–32 seconds
of UI blocking here. The measured nominal duration was 4,291.06 seconds
(1:11:31), with click-wait time excluded.

The next optimization candidates are lazy validation of only the selected VXP
packages, avoiding repeated validation of unchanged installed content, deferring
whole-movie timing from the first-frame path, and replacing repeated linear
chunk-ID searches in movie metadata loading with an index. These measurements
do not quantify the eventual savings or Release-build performance. The first
render above uses BRender CPU; it is not a GPU startup measurement.

Local evidence is in `tmp/marcus-awards-profile/run-1.log`, `run-2.log`, their
JSON summaries, `bundle.json`, and the temporary profiling sources. Both runs
completed successfully. Instrumentation was compiled into separate profiling
executables; the production playback sources were not edited for this audit.

The shared installed-package store subsequently moved to a SQLite metadata and
validation cache. Repeating the same Debug profile reduced VMM content setup to
1.153 seconds on first migration and 0.292 seconds with the persisted cache,
versus 43.366–45.499 seconds above. Opening before whole-movie timing took
18.560/17.873 seconds; totals through analysis and first render were
49.418/48.512 seconds. Both runs retained 1,435 scenes, 16 first-scene actors
and the same 4,291.06-second nominal duration. The original JSON was retained;
the first new run started without a SQLite catalog. Evidence is in
`tmp/marcus-awards-profile/sqlite-run-1.log`, `sqlite-run-2.log` and
`sqlite-runs.json`.

This removes the eager whole-library hash scan. It does not remove the separate
archive/index/metadata costs or the roughly 30-second synchronous duration pass
for this movie. Package resolution still uses legacy names, CFG shortnames and
mount order; selected unchanged files reuse their validation fingerprints.

### Full-opening review after the SQLite change

A deeper instrumented run on 11 November compared Debug (`build/marcus-x64`)
with Release (`build/studio-portable-release-x64`) at `e2b6a2e3`. Both used the
same awards ZIP, resources and content library, with a warm SQLite catalog.
These are separate headless profiling executables following the player open,
whole-movie timing and first CPU render path, not measurements of window/GPU
startup. Each configuration was measured once; these are warm-cache observations,
not cold-disk benchmarks.

| Phase | Debug | Release |
| --- | ---: | ---: |
| ZIP extraction | 0.759 s | 0.612 s |
| VMM content setup | 0.268 s | 0.177 s |
| Movie archive index | 5.306 s | 0.387 s |
| Movie metadata | 6.171 s | 1.041 s |
| Preview runtime opening | 3.593 s | 0.277 s |
| Content mounting | 1.484 s | 0.209 s |
| Open total before whole-movie timing | 17.772 s | 2.731 s |
| Whole-movie duration analysis | 30.287 s | 5.734 s |
| First CPU render, 544x306 | 0.280 s | 0.152 s |
| Total including setup and teardown | 48.577 s | 8.664 s |

The main remaining cost is waveform generation during duration calculation.
`main.c::refresh` synchronously calls `marcus_movie_time`; `ReadSoundTimeline`
visits all 1,435 scenes. Its duration callback requests a waveform column solely
to obtain `info.seconds`. The waveform cache nevertheless decodes/resamples the
audio and constructs its envelope. There were 1,212 waveform requests, 965 cache
misses, 204 hits, 794 decodes and 793 envelope builds. These counts do not establish
how many distinct sounds were decoded again after eviction.

| Nested cost across opening and timing | Debug | Release |
| --- | ---: | ---: |
| Sampled-audio decoding | 19.824 s | 3.602 s |
| Waveform envelope construction | 7.434 s | 1.385 s |
| Movie chunk enumeration, two snapshots | 6.658 s | 0.546 s |
| Linear metadata chunk lookups | 1.547 s | 0.202 s |
| Sound-cue presentation reads during timing | 2.451 s | 0.575 s |
| Additional presentation reads during timing | 0.279 s | 0.070 s |
| Caption layout during timing | 0.012 s | 0.001 s |

These costs overlap the phases above and must not be added to their totals.
Envelope construction processed 324,933,824 stereo sample frames, approximately
2.42 GiB of float PCM, from 37.05 MiB of sound resources. MIDI duration comes
from event timing; MIDI synthesis is not the cause here. Only one scrolling
caption hold was found among 903 captions, and caption layout is negligible for
this movie.

The player and preview each build a complete archive snapshot, enumerating the
same 193,880 movie chunks twice. Metadata loading also makes 1,437 linear chunk
searches, totalling 247,399,531 comparisons. The existing chunk array is already
sorted by unsigned type and number, so binary search can replace those scans.
Timing reads each scene's presentation through both `ReadSoundCues` and
`ReadPresentation`, another opportunity to share already-read data.

Instrumented engine `ReadFile` calls took 0.670 seconds in Debug and 0.541 seconds
in Release for 451.89 MiB across 269,169 reads. This counter excludes ZIP stream
I/O, SQLite I/O and temporary-file writes, but supports CPU processing as the
dominant measured cost. Outer ZIP decompression remains a small part of opening.

The next changes, in priority order, are:

1. Remove whole-movie timing from the first-frame path and calculate it lazily or
   in an isolated worker. Removing only this pass projects about 2.9 seconds to
   first CPU frame in this Release run; that altered path has not been measured.
2. Give sound lengths a separate cache/API, without generating waveforms. Preserve
   codec/resampling rounding and MIDI timing; generate envelopes when requested
   for display. Merely deferring the existing work would otherwise move the stall.
3. Share the movie archive snapshot and replace linear chunk searches with binary
   searches. Reuse scene presentation reads during timing.

Release substantially reduces the costs, but still spends about five seconds
decoding audio and building waveforms for the duration display. Both builds
reported 4,291.06 seconds and first-frame FNV64 pixel hash
`13146854581947680197`, with 1,435 scenes and 16 first-scene actors. This checks
the measured duration and one rendered frame, not full playback parity.

Local evidence and instrumentation are in `tmp/marcus-full-open-review/`, including
`debug.log`, `release.log`, `runs.json`, and `build_profiles.py`. Production
playback sources were unchanged by this review.

### Opening optimizations and background timing

The subsequent implementation transfers the player's already indexed archive to
the preview, replaces linear chunk searches with binary search, and reads each
scene's captions, pauses and scene/actor sound cues together. Clip lengths have
a separate metadata cache. They use the existing source decoders and SDL's
flushed-stream length accounting, without resampling to float PCM or building
waveform envelopes. This retains compressed fact trimming, resampler rounding
and MIDI event timing. Waveforms are built when requested for display.

The waveform cache and peak tree run in C89. Clip lengths use a separate cache
so timing analysis can measure WAVE data without building peaks. The peak tree
retains both stereo extrema in 128-frame buckets. Timeline requests look up
the envelope once, then sample each column on the movie clock. Asset and
recording previews use the same peak reduction.

The C89 sound scheduler owns cue spans, active lanes and the mapping between
authored frames and movie time. It keeps authored cue order within each frame,
accounts for caption scrolling and pauses, and applies voice replacement and
carryover at scene boundaries. The sound sheet, timing queries, waveform
placement and resume after seeking use the same schedule. When a frame range
changes, the C89 engine rebuilds the schedule from its retained cues, clip
lengths and holds. Extending the range can bring previously clipped cues
back into the schedule without decoding their media again.

The scheduler builds the sound sheet's track order, cue counts and extents.
Scene and actor lanes use separate keys, including when the actor ID is zero.
It also computes chapter boundaries, clamps movie progress to the current
frame's presentation span, and resolves time seeks during an incoming
transition to the new scene's first frame. Marcus resolves display names and
applies the seek to the player.

Playback, timing analysis and the sound sheet read presentation events from
the live C89 movie graph. Marcus shares the scene and actor wire readers with
Studio. The readers validate both byte orders before interpreting each group.
Stored captions use the graph for text-box bounds, rich-text defaults, and
formatting records. C89 reads TEXT from its stored extent and converts the
descriptor through the same routine used for edit previews. Marcus supplies
legacy code-page and font-name conversion and publishes the preview records.

The graph resolves local sound links against the cue's scene before looking
for the first matching movie-root link. For each resolved source, C89 reads
the first slot-zero child of the MSND envelope and streams its decoded data
into the WAVE buffer or the MIDI parser. Marcus and Studio share the wire
decoder and retain their handling of invalid markers and flags.
Studio's resource loader and Marcus action and path editing use the same
graph reader for motion-match sounds. The built-in sound catalog reads headers
from the archive it already opened for names. The catalog accepts WAVE or MIDI
at any child slot, while playback resolves the first child at slot zero.
Sound import uses the shared C89 stream writer. The MIDI path converts the
standard file to the millisecond track payload in C89 and writes it from
memory. WAV and MP3 import supplies decoded WAVE bytes to the same writer.
A failed child write or name update removes the new MSND and its child.
Marcus and Studio share the first-free-slot calculation for adopting sound
chunks into a scene or movie. Sound links can share a slot with another
chunk type. Studio reads the graph directly to resolve user tags and find
unused sounds by their parent counts.

The UI starts/polls a timing job instead of calling the synchronous whole-movie
API. The job launches `marcus-preview --timing` with a lease on the materialized
movie, keeping ZIP/VMM inputs alive. Engine globals remain isolated in the child
process. A host worker supplies the file stream to the C89 decoder, which
validates the snapshot against the expected ordered scene IDs and frame ranges.
The player adopts the schedule on its owner thread without seeking or
restarting audio. Reopening a movie or destroying the
player cancels and joins the old job, preventing stale results from replacing
another movie's timeline. The helper has a five-minute timeout. Its temporary
MMT4 snapshot uses fixed-width little-endian fields and binary64 durations,
with limits of 64 MiB and one million timing records. Marcus and its helper
exchange it between matching builds.

The C89 codec matched the decoded timing of three MMT3 baseline snapshots.
The five-scene playback fixture retained its 17 cues, five credit scroll
holds and 15 authored pauses at both 6 and 24 FPS. The snapshot for each
rate went from 5,952 to 1,640 bytes. The C codec produced identical fixture
bytes on x86 and x64. Rebuilt SDL Studio matched the retained retail state
and viewport capture for the credits fixture's opening frame.
Evidence is in `tmp/c89-timing-snapshot/`.

Moving the cached timing records into C89 preserved all three MMT4 snapshots
byte for byte. The comparison with the prior scheduler also matched cue spans,
frame/time mappings and resumed sounds across 480 generated scenes.
Rebuilt SDL Studio matched the retained retail state and opening viewport
for the credits movie. The comparisons are in `tmp/c89-sound-sources/`.

Moving sound sheet and movie clock queries into C89 preserved the published
results across three native movies with 10 scenes, 18 tracks, 28 cues and 84
time seeks. Rebuilt SDL Studio matched the retail seek state at frames 1 and 4
of the actor-sound movie. Each viewport differed by one pixel out of 169,092.
The comparisons are in `tmp/c89-sound-sheet/`.

The graph reader preserved three timing snapshots and the published sound
sheet and seek results across three movies byte for byte. The asset-preview
checks retained the caption, pause, and cue metadata fingerprint. Fresh retail
captures matched SDL Studio's seek state at all three sampled frames. The
credits viewport matched exactly. The two actor-sound viewports each differed
by one pixel out of 169,092. Evidence is in `tmp/c89-presentation-graph/`.

Reading stored captions through the graph preserved 22 render and hit-test
observations across five movies and three timing snapshots byte for byte.
The asset-preview checks retained the media and metadata fingerprints and
verified that TEXT uses its stored bytes even with the packed flag set.
SDL Studio matched the seek state and viewport pixels in all five retained
retail caption captures. Evidence is in `tmp/c89-stored-captions/`.

The sound graph reader retained the media and metadata fingerprints and
three timing snapshots. The archive tests also decoded a packed WAVE from an
MSND child. In fresh actor-audio captures, Marcus, SDL Studio and retail
committed the same 60 frames in each mode and played both fixture tones in
the first scene only. SDL Studio's sound requests matched retail. Marcus and
SDL Studio reproduced their prior seek images, with two pixels differing from
retail in one frame. The mixed PCM still differed from retail in both ports.
Evidence is in `tmp/c89-sound-graph/`.

After Studio and the catalog adopted the shared graph reader, all 711 Marcus
catalog entries matched the prior build byte for byte. The asset-preview
fingerprints also matched. Fresh actor-audio captures again matched retail's
60-frame sequence and confined both tones to the first scene. SDL Studio's
sound requests matched retail. Each port reproduced its prior seek images,
including the two pixels that differ from retail in one frame. The mixed PCM
comparison still differs from retail. Evidence is in `tmp/c89-sound-resources/`.

The shared stream writer preserved Studio's 99,336-byte Release sound-resource
trace and three Marcus-authored movies byte for byte. The Debug fixture passed
all 96 valid MIDI import attempts under native allocation injection and checked
that failed imports left no sound chunks. Retail opened the three authored
movies. SDL Studio matched all five sampled seek states. One viewport matched
exactly and the other four differed by one pixel each. Evidence is in
`tmp/c89-sound-import/`.

The direct sound-link queries preserved both native integration traces byte
for byte. The traces cover local tag resolution, external archives, unused
sounds and sound adoption. Three Marcus-authored movie files also matched
the prior build byte for byte. In fresh actor-audio captures, both ports matched
retail's 60-frame sequence and confined both tones to the first scene. SDL
Studio matched retail's sound requests. Mixed PCM still differed in both
ports, and SDL Studio's frame timing still differed. Evidence is in
`tmp/c89-sound-links/`.

After Studio save and close switched to the shared sound graph, both the SDL
Release and native Debug save/reopen fixtures reproduced their save trace, sound
trace and saved movie byte for byte. The C save test exercised the purge prompt,
slot-zero payload checks, and reverse traversal through index compaction. Fresh
actor-audio captures matched retail's 60-frame sequence in both ports. Both
tones played only in the first scene, and SDL Studio matched retail's sound
requests. Mixed PCM differed in both ports, and SDL Studio's frame timing
differed. Evidence is in `tmp/c89-save-graph/`.

Moving Studio's save index queries into C89 preserved both build profiles'
save and sound traces and saved movie byte for byte. The save tests covered
refreshing the movie root after save, collecting scene tags up to the first
empty slot, and retaining a newly written scene numbered zero when the old
scene link is missing. The chunk-save and v3dmm format tests also passed.
Fresh actor-audio seek and playback captures matched retail's 60-frame
sequence in both ports. All seek images matched their prior captures, including
the two pixels that differ from retail. SDL Studio matched retail's sound
requests. Mixed PCM differed in both ports, and SDL Studio's frame timing
differed. Evidence is in `tmp/c89-save-index/`.

Scene save and tag collection now resolve event groups, actors, and sounds
through the shared C89 graph. Both native scene-store traces matched their
pre-change runs byte for byte, including the Debug allocation-failure trace.
The Debug fixture's stored fingerprint was refreshed from two identical runs
of `c29454ea` before the change. Both native save/reopen traces and saved
movies also matched. Both ports matched retail's 60-frame actor-audio seek
and playback sequences, and all seek images matched their prior captures.
SDL Studio matched retail's sound requests. Mixed PCM differed in both ports,
and SDL Studio's frame timing differed. Evidence is in
`tmp/c89-scene-save-graph/`.

The shared scene reader now supplies Studio's save-time tag scan. Its tests
cover unknown events and reject bad row offsets and fixed widths in packed
groups in both byte orders. Native Debug and Release save/reopen traces and
movies matched their prior builds. The full Release scene-store trace also
matched. In Debug, all 324 scene writes and all 42 tag scans without injected
faults matched. Removing native event-group allocation changed 42 injected
fault cases. The revised Debug trace reproduced on rerun. Both ports matched
retail's 60-frame actor-audio seek and playback sequences. The seek images
matched their prior captures. SDL Studio matched retail's sound requests.
Mixed PCM differed in both ports, and SDL Studio's frame timing differed.
Evidence is in `tmp/c89-scene-tag-reader/`.

Studio now collects actor dependencies through the C89 movie graph and event
decoder. Tests cover local and external templates, the fallback for a missing
template link, 3D text fonts with trailing descriptor bytes, custom costume
exclusion, malformed input, and a rejected tag insertion. Native Debug
and Release save/reopen traces and movie files matched their prior builds.
The full Release scene trace matched. All 324 Debug writes and 42 tag scans
without injected faults matched. Four Debug fault-injection cases changed
when the native actor group and list allocations were removed. The new
trace reproduced on rerun. Both ports matched retail's 60-frame seek and
playback sequences and reproduced their prior seek images. SDL Studio
matched retail's sound requests. Mixed PCM still differed in both ports,
and SDL Studio's frame timing differed. Evidence is in
`tmp/c89-actor-tag-reader/`.

Studio's scene collector now inserts background, camera, and sound
dependencies directly into the C89 tag list. The shared sound parser checks
tag spans in both byte orders, and tag collection reads those spans without
allocating or rewriting a packet. Native Debug and Release save/reopen
traces and movie files matched their prior builds. The full Release scene
trace matched. All 324 Debug writes and 42 tag scans without injected faults
matched. Removing the temporary sound packets changed 16 Debug
fault-injection cases and the new trace reproduced on rerun. The v3dmm
format test passed. Both ports matched retail's 60-frame seek and playback
sequences and reproduced their prior seek images. SDL Studio matched
retail's sound requests. Mixed PCM differed in both ports, and SDL Studio's
frame timing differed. Evidence is in `tmp/c89-scene-wire-tags/`.

Studio movie open, save-format selection, and asset cleanup now build and
dispose of the dependency list in C89. A failed scene scan discards the
partial list, and asset cleanup skips deletion in that case. Native Debug
and Release scene, save, and sound traces and saved movies matched their
prior builds byte for byte. SDL Studio also saved and reopened a storyboard
VMM with its embedded package, scene state, and viewport pixels intact.
The native core link and v3dmm format tests passed. Both ports matched
retail's 60-frame seek and playback sequences and reproduced their prior seek
images. SDL Studio matched retail's sound requests. Mixed PCM differed in
both ports, and SDL Studio's frame timing differed. Evidence is in
`tmp/c89-movie-dependencies/`.

The shared roll-call writer now serializes actor names and extras directly
from C89 storage. The writer tests cover renaming, the zero cache pointer
field, and cleanup after allocation, chunk write, or adoption failures.
SDL Studio saved and reopened the two-scene actor-audio fixture with its
four actor entries, poses, and first viewport intact. The saved roll-call
chunk was byte-identical to one produced by the earlier Studio writer.

Marcus initially failed while opening installed v3dmm content referenced by
both Studio saves. Its GG index omitted some row padding. The shared C89
reader now accepts that layout and checks physical extents for overlap.
Marcus opened both saves after the fix. Retail, SDL Studio, and Marcus
completed seek and playback captures of the new save with matching 60-frame
sequences. SDL Studio matched retail's sound requests. Both ports differed
from retail in mixed PCM. Evidence is in `tmp/c89-rollcall-write/`.

Studio and headless loads now build their actor rows directly from the C89
GST reader. Tests exercise both byte orders and verify that a failed insert
closes the new tag before earlier rows. The before/after roll-call and movie
owner snapshots match byte for byte. SDL Studio reopened the two-scene
fixture with the same actor names, poses, and first viewport, and all 44
chunk payloads and graph metadata matched the previous save. Retail, SDL
Studio, and Marcus each matched their previous 60 seek viewports. All three
players produced the same 60-frame playback sequence. SDL Studio matched
retail's sound requests, and both ports still differed in mixed PCM.
Evidence is in `tmp/c89-rollcall-read/`.

The awards ZIP was measured again with headless drivers linked against the
updated production libraries, using the same warm catalog/resources/content:

| Measurement | Debug | Release |
| --- | ---: | ---: |
| Synchronous open, then timing, then first CPU frame | 18.066 s | 3.821 s |
| Open and first CPU frame with background timing | 12.264 s | 2.494 s |
| Background timing ready, measured from opening start | 30.182 s | 5.506 s |
| Slowest timing poll/handoff on the player thread | 0.050 ms | 0.040 ms |

The background driver repeatedly sought and rendered scene one while polling.
The worker opens its own engine session, so background completion includes that
extra open cost; it does not reuse live native pointers across processes. Initial
archive/content/first-scene loading remains synchronous. These are single
warm-cache measurements of CPU rendering, not window/GPU startup benchmarks.
All four runs retained the 4,291.06-second duration and first-frame FNV64 hash
`13146854581947680197`. Evidence is in
`tmp/marcus-full-open-review/optimized-{debug,release}-{sync,async}.log`, with
`optimized_open.cpp` and `build_optimized.py` alongside them.

Headless regressions cover asynchronous completion while seeking/rendering,
unchanged scene/audio state, cancellation on reopen, terminal worker failure,
snapshot round trips and truncated/trailing data, carried/queued sounds after
import, and the existing scrolling/wait/transition clock checks. Audio tests
compare length-only accounting with decoded PCM for fractional sample-rate
ratios, short PCM/MS ADPCM clips, GSM and MP3. No Marcus UI tests were added.
