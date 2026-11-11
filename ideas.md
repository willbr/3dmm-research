* point rendering mode
* BBOX rendering mode
* fps multiplier
* remove the word Microsoft from the app
* include MCP server in release build
* improve MCP server
* tidy up files in repo
* include some basic event logging for the last n events to help with debug
* convert assertion failure dialog to a normal dialog, so i cant copy and paste it
* increase the max buffer size in BRender? I want 4k and 1440p in software
* add gizmos
* free flying camera mode (for editing)
* free flying camera mode faked by moving all items in a scene
* hires screenshots? 4k?
* do we need to vendor the brender library?
* onion skinning? render actor path?
* import/export the scene? OBJ? Blender?
* remake the gui using imgui
* background editor in imgui
* actor editor in imgui
* prop editor in imgui
* use zig build?  or just a batch file?
* write some docs about the codebase
    * chunky files
    * 3mm file format
    * brender
    * 3mm file format
    * how the MCP server works
    * how the multi select works

* fix all virtual store problems
* wsprintfA does take the string length, all string functions should
* make names more verbose, same idea as the struct renames:
    1. variable names
    2. method names
    3. function names
* headless mode
    * do we need to split kauai into two for headless?

* add a menu items for:
    * exporting:
        * screenshots
        * gifs
        * videos (scene, movie with transactions)


* measure original 3DMM playback FPS in VMs or on another PC running Win9x, XP and Win7+ to calibrate compatibility speed modifiers
* render to video to test x86 vs x64
* write a full text plan that covers all features
* find an open source font to use
* design a new sqlite base file format
* rewrite to c, I hate c++
* update to use SIMD
* assets editor
* enable lighting using the pallete
* build a `3mm` CLI using the split core libraries
    * inspect and validate movies, dump JSON, compare movies, extract captions/assets and report dependencies
    * use stable, normalized JSON dumps for semantic regression tests and x86 vs x64 comparisons
    * do not assume load/save produces byte-identical `.3mm` files: physical layout and chunk numbering can change
    * keep normalization in the read-only dump tool; do not change the `.3mm` writer or format just to make tests deterministic
    * retain a strict view of IDs, references and format details so normalization cannot hide compatibility bugs
    * test semantic round trips separately from format versions, fixed-width record layouts and chunk-reference integrity
    * keep legacy movie fixtures and verify newly saved files in the original editor before claiming backwards compatibility

* long-term platform/UI: SDL3 and Dear ImGui, including macOS support
    * run the existing Studio UI and tools through SDL3; preserve Kauai controls and resource scripts
    * use ImGui for auxiliary tools where useful; a replacement Studio is not the porting goal
    * keep SDL3 and ImGui in host adapters; core libraries remain usable by CLI tools and regression tests
    * first remove native dialogs and registry/DLL spelling integration from core, then port allocation, synchronization, timing, files and text
    * isolate GDI regions/metafiles and preserve legacy `.3mm` data and encoding behavior during the migration

* selectable file/asset backends using SDL3 streams
    * support normal folders, direct ISO reads and embedded assets through `SDL_IOStream`
    * reuse the ISO directory parser and expose bounded streams for individual assets, avoiding bulk extraction
    * include file-existence checks and directory enumeration in backend selection; keep movie saves on the writable filesystem
    * allow loose files and expansion packs to override ISO assets through an explicit search order
    * evaluate a thin, generic, SDL-independent interface versus using SDL directly in core libraries and CLI tools
    * keep or rename `KauaiFiles` only where it provides useful contracts; remove unnecessary legacy behavior and redundant wrappers
    * retain caching as an optional fallback for components that still require real filenames, such as font discovery
    * compare startup time, disk usage and runtime reads, and require both backends to pass the same movie and building workflow tests

* open ZIP archives containing `.3mm` or `.vmm` movies
    * include movies in subfolders; open a single movie automatically or show a chooser when there are several
    * pass bundled `.vmm` movies through the existing expansion-loading path
    * extract only the selected movie, keep it available while the document is open, and clean up when it closes
    * use Save As to write an ordinary `.3mm` or `.vmm`; leave the source ZIP unchanged
    * reuse miniz with extraction limits and CRC checks; avoid accumulating duplicate temporary files
    * consider loose VXP packages alongside `.3mm` movies as a follow-up

* Marcus movie planning and animation tools inspired by Source Filmmaker
    * storyboard panels with sketches, imported images, notes and links to scenes/frame ranges
    * Grease Pencil-style viewport drawing with named layers, opacity, held drawings, keyframes and onion skinning; start in screen space and consider 3D strokes later
    * shot timeline with storyboard thumbnails; trim, duplicate and reorder shots before animation is finished
    * named object groups with a movable pivot, visibility, locking and solo controls
    * save selections as reusable prefabs containing relative transforms, costumes and optional animation; start with independent copies and consider linked instances with overrides later
    * pose bookmarks and a reusable pose/motion library for compatible actors and rigs
    * parenting and constraints: hats follow heads, cups follow hands, look-at targets and timed attachment changes
    * IK controls mapped to actor body-part hierarchies, with editable chains, targets, constraints and keyframes; investigate foot planting and hand contact
    * pose-to-pose blocking with stepped timing, followed by interpolation and animation curves for easing and overshoot
    * edit motion across a selected time range, with gradual blends at its boundaries
    * record puppeteering by dragging controls or steering objects during playback, then refine the recorded motion
    * keep the shot camera visible while navigating with an independent work camera
    * frame bookmarks and review notes linked to drawing overlays
    * performance prefabs combining props, actor placement, rig targets and reusable actions, such as a person typing at a desk
    * prioritize groups/prefabs, the shot timeline, pose bookmarks and parenting as foundations for IK and curve editing
    * store Marcus authoring data in separate versioned private chunks without changing standard movie records or file versions
    * retail should ignore private chunks and play the underlying movie; verify this with a retail fixture before claiming compatibility
    * loss of Marcus-only authoring data after saving in retail is acceptable
    * bake IK, constraints and edited motion into supported movie events/actions for retail playback; prototype the animation encoding first
    * investigate compatibility separately for animated cameras, 3D drawing and rendering effects, especially against prerendered backgrounds

* Marcus asset and animation workflow ideas inspired by Blender
    * searchable asset browser with thumbnails and tags for actors, props, prefabs, materials, poses and actions; drag assets into scenes
    * collection instances for reusable sets and object arrangements; edit shared prefab definitions or make individual instances independent
    * pose assets for whole rigs or selected body parts, with mirroring and adjustable blend strength
    * animation strips to trim, repeat, retime and blend actions; investigate combining locomotion with upper-body gestures on compatible rigs
    * dope sheet for moving blocks of keyframes and shifting entire performances together
    * motion paths with frame markers to inspect trajectories, speed changes and animation arcs
    * axis-constrained move, rotate and scale operations in world/local space, with numeric input while dragging
    * selectable transform pivots: selection centre, active object, individual origins or a placed cursor
    * floor and attachment snapping, object alignment and evenly spaced distribution
    * local view to temporarily isolate selected objects in crowded scenes
    * explicit linked versus independent duplication
    * searchable commands for finding tools without navigating menus
    * adjust the last operation's parameters, including duplicate count, spacing and rotation
    * simple procedural arrangements such as chairs around a table or fence sections along a path; retain editable recipes and generate ordinary props for retail playback
    * prioritize the asset browser, collection instances, transform/snapping tools and dope sheet
    * preserve editable animation strips, blends and procedural recipes in Marcus private data; bake playback results into supported retail data and verify animation-format limits
    * reference Blender's asset browser, pose library and nonlinear animation system:
        * https://docs.blender.org/manual/en/4.4/editors/asset_browser.html
        * https://docs.blender.org/manual/en/dev/animation/armatures/posing/editing/pose_library.html
        * https://developer.blender.org/docs/features/animation/nla/

* Marcus performance and prefab tools inspired by Maya and 3ds Max
    * Maya-style animation correction layers above stock actions, with mute, adjustable strength and additive/override behaviour for selected body parts
    * driven keys for meaningful prefab controls: a door-opening slider moves the door and handle; a grip slider poses compatible finger parts
    * actor ghosting at earlier/later frames with distinct colours, extending drawing onion skins to posed characters
    * quick viewport review videos (playblasts) with optional frame numbers, shot names, notes and storyboard overlays
    * 3ds Max-style motion mixing by body region, such as walking legs, a waving arm and a looking head, with blended transitions and foot-contact inspection
    * ready-made Biped/CAT-style rig presets for compatible actors, with walk controls, footstep placement and foot planting
    * parameter wiring to connect one prefab control to multiple objects, such as wheel rotation or an unfolding assembly
    * editable modifier stacks for custom props, including scale, bend, mirror and material changes, followed by generation of the final retail-compatible asset
    * named scene state sets for alternate costumes, prop visibility and camera choices; switch between takes without manually restoring each setting
    * expose reusable prefab controls: car steering/wheels/doors/suspension, desk chair/monitor/seated actor targets, character grip/gaze/posture, and crowd spacing/facing/timing offsets
    * prioritize actor ghosting, review videos, animation correction layers and prefab controls
    * retain editable controls in Marcus private data; bake generated walks, mixed motion, corrections and modified geometry into supported assets/events and verify retail playback
    * reference Autodesk's animation and scene tools:
        * https://help.autodesk.com/cloudhelp/2024/ENU/Maya-Animation/files/GUID-BBCA0BC3-7608-4E86-8E9F-B4099C316156.htm
        * https://help.autodesk.com/cloudhelp/2019/ENU/Maya-Animation/files/GUID-D2B1C7EF-F177-4B0E-9E41-B479CFF2AFD4.htm
        * https://help.autodesk.com/cloudhelp/2022/ENU/Maya-Animation/files/GUID-1EC3357B-62DD-424F-9595-277C373D133C.htm
        * https://help.autodesk.com/cloudhelp/2020/ENU/3DSMax-Animation/files/GUID-D2491163-80EB-49E7-8BFA-EFE5B41C07CD.htm
        * https://help.autodesk.com/cloudhelp/2022/ENU/3DSMax-Character-Animation/files/GUID-55060A51-4C7A-4A51-B2D0-DC3F87C80784.htm
        * https://help.autodesk.com/cloudhelp/2019/ENU/3DSMax-Animation/files/GUID-B084D14B-2925-4EA6-890F-4F2564BB9D9A.htm
        * https://help.autodesk.com/cloudhelp/2024/ENU/3DSMax-Modifiers/files/GUID-911C535D-9003-4925-A064-52835C581DAA.htm
        * https://help.autodesk.com/cloudhelp/2026/ENU/3DSMax-Manage-Scenes/files/GUID-6CA5DD56-9807-4E65-9A2E-14E6209B3062.htm

* dedicated timing-only loader for Marcus's Calculate duration button
    * avoid opening a full player just to calculate nominal movie duration; read the timeline, sound lengths, caption layout and transition timing without loading unrelated rendering assets or generating waveform displays
    * retain shared playback timing policy, including timed pauses, sound waits, scrolling captions and transitions; explicitly exclude viewer-dependent click waits
    * support ordinary movies, selected ZIP members and VMM dependencies while keeping calculation cancellable and off the UI thread
    * evaluate bounded VFS or memory-backed reads to reduce temporary-file staging where the readers support them; retain file staging where required by existing engine readers
    * profiling A Special Announcement.vmm on 11 November 2026 measured about 39.5 seconds in normal movie loading versus 0.85 seconds in the subsequent duration analysis; investigate loading phases before attributing the cost to decompression or disk access
    * compare results against the current player calculation for plain .3mm, ZIP, VMM and zipped VMM inputs before replacing the existing path
