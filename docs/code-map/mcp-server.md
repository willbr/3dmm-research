# MCP server

DEBUG builds of `3dmovie.exe` embed a small **Model Context Protocol**
server that lets an AI agent (Claude Code, etc.) drive the studio
headlessly: take screenshots, click points, send kauai commands,
inspect dialogs, read crash logs, and request graceful shutdown. The SDL server
uses portable stdio, timing and PNG encoding. Full Mac/Linux SDL Studio builds are
enabled, and Linux startup/edit/save/shutdown checks pass. Native macOS arm64
build, editing/playback/save/reopen, PNG capture and quit/EOF shutdown checks
also pass; see the [build checkpoint](../tools/studio-sdl.md#maclinux-studio-build-4-november-2026).

## Enabling

```
3dmovie.exe --mcp-server
```

The repo root has a [`.mcp.json`](../../.mcp.json) that registers the
`3dmovie` server with Claude Code:

```json
{
  "mcpServers": {
    "3dmovie": {
      "command": "C:/Users/wjbr/src/3DMMForever/dist-x64/3dmovie.exe",
      "args": ["--mcp-server"]
    }
  }
}
```

A Claude Code session opened in the repo discovers it automatically.

## Wire protocol

Newline-delimited JSON-RPC 2.0 over inherited stdin / stdout, without sockets or
SDL_net. Standard MCP
methods: `initialize`, `notifications/initialized`, `tools/list`,
`tools/call`.

## Threading

```
+-----------+   stdio read   +-----------------+   Drain (per kauai tick)
|  stdin    |--------------->| worker thread   |---->+----------------+
+-----------+                | (parses JSON)   |     | main GUI thread|
                             +-----------------+     +----------------+
                                                             |
                                                             v
                             +-----------------+     +----------------+
                             | request queue   |<----| dispatch tool  |
                             | (mutex-guarded) |     | (synchronous)  |
                             +-----------------+     +----------------+
                                                             |
                             +-----------------+             v
                             |  stdout         |<-- response (mutex-guarded write)
                             +-----------------+
```

Worker-safe SDL tools (`get_sdl_dialog`, `sdl_dialog_action`) bypass the queue
using the synchronized dialog snapshot/action queue. On Windows, `list_dialogs`
and `dismiss_dialog` also run on the worker so native diagnostics stay accessible
while the main thread is blocked in a modal box. Source:
[`src/studio/mcpserv.cpp`](../../src/studio/mcpserv.cpp).

[`mcp_io.cpp`](../../src/studio/mcp_io.cpp) borrows stdio and reads with bounded
waits (POSIX poll, Windows pipe availability checks). Shutdown joins the reader
while stdin stays open. Writes handle partial output and disconnected pipes;
POSIX SIGPIPE handling affects only the writing thread. Notifications receive no
replies. On EOF, Drain handles queued requests before enqueueing normal quit.

[`mcp_png.cpp`](../../src/studio/mcp_png.cpp) encodes the top-down XRGB canvas as
RGB PNG using stored DEFLATE blocks, without a platform imaging dependency.
Screenshots stay lossless but are larger than GDI+ output. Window state comes
from SDL; legacy `hwnd_*` field names remain for client compatibility.
`client_size` is in window units; screenshots/clicks use canvas coordinates.
`error_codes` exposes retained error codes alongside `error_stack_depth`.
GDI+ remains only in the non-SDL Windows screenshot backend.

## Tools exposed

From the `kTools` table in
[`src/studio/mcpserv.cpp`](../../src/studio/mcpserv.cpp):

| Tool             | Worker-safe | Purpose                                                              |
|------------------|:-----------:|----------------------------------------------------------------------|
| `screenshot`     | no          | Capture the main window's client area as PNG.                        |
| `click`          | no          | Click a point in client coords (`button` = `left` / `right`).        |
| `key`            | no          | Queue down/up through SDL using legacy key codes; native backend uses SendInput. |
| `type_text`      | no          | Queue UTF-8 text through SDL input. |
| `get_sdl_dialog` | **yes**     | Observe active SDL dialog, generation and control IDs. |
| `sdl_dialog_action` | **yes**  | Queue generation-checked SDL modal input, including during modal loops. |
| `send_command`   | no          | Enqueue a kauai command (`cid`, optional `hid`, `lw0..lw3`) into `vpcex`. |
| `find_gob`       | no          | Look up a kidspace gob by `hid`; report current rect, visible rect, visible bool. |
| `wait_for_gob`   | no          | Poll `find_gob` until the gob meets a readiness signal (`visible` / `positioned` / `exists`); pumps Win messages between probes. |
| `get_state`      | no          | Main-window state, foreground status, modal dialog (if any), `ErrorStack` depth. |
| `wait_ms`        | no          | Pump host events and Kauai commands for N ms (capped 30000). |
| `read_crash_log` | no          | Read `3dmmforever-crash.txt` from the platform temporary directory. |
| `quit`           | no          | Enqueue normal Studio quit, preserving save prompts. |
| `list_dialogs`   | **yes**     | Enumerate top-level dialogs / message boxes / RTC popups owned by the process. Returns hwnd, class, title, child-control texts. |
| `dismiss_dialog` | **yes**     | Dismiss a dialog by `hwnd` + standard button id (`abort`/`retry`/…) or `button_text` substring match. |

`list_dialogs` and `dismiss_dialog` are advertised only on Windows. The SDL
dialog tools are available on every SDL platform. `wait_for_gob` pumps host
events and commands; nested waits omit the idle render tick, so clients should
poll state through the normal loop when observing movie playback.

## Direct SDL authoring

SDL Debug builds (or builds with `STUDIO_TEST_TOOLS`) also expose direct
scene/prop authoring. These main-thread tools require an idle Studio movie;
stop playback and finish mouse tracking before editing. They use normal engine
operations, authored events, undo and the original movie writer.

| Tool | Arguments and behavior |
|------|------------------------|
| `list_assets` | `kind`: `props` (default), `actors`, `backgrounds`, `materials`; returns original names, `sid` and `cno`. |
| `create_scene` | `background_cno`, optional `sid` (default 2); appends a scene and returns its camera matrix/FOV. Retail black background is 11. |
| `select_scene` | Existing one-based `scene`; switches the idle movie for authoring without extending its timeline. |
| `set_frame_pause` | Native pause at the current frame: `kind` is `none`, `sound`, `click` or `time`. `time` requires `ms` in 1–3600000. A scene's final `sound` pause lets its cue finish before the next scene. |
| `edit_text_box` | One-based `index`, replacement `text`, optional `align`; preserves bounds, lifetime and first-character styling. LF, CRLF and CR inputs become native paragraph breaks. Creation also normalises line endings and applies paragraph alignment after insertion. |
| `create_text_box` | `text`, `bounds: [left,top,right,bottom]` on the 544×306 movie canvas; optional `font_size` (6–120, default 18), `bold`, `color: #RRGGBB` (white), installed `font` name, and `align` (`left`, `center`, `right`). Inserts transparent story text from the current frame through optional inclusive `last_frame`, which must already exist in this scene; defaults to scene end. Uses the legacy text encoding. |
| `create_actor` | `template_cno`, optional `sid` (2) and `name`; commits placement and returns `actor_id`, world position, size and model dimensions. Cube is 4130; Sphere is 4131. |
| `inspect_actor` | `actor_id`; reads world position, rotation matrix, scale, stretch, name and model bounds. |
| `transform_actor` | `actor_id` and any of `position`, `offset`, `rotation`, `scale`, `stretch`, `color`. With `color`, optional `shaded: false` uses one nearest palette entry for a flat backdrop colour; shaded retail ramps remain the default. |
| `transform_actors` | `edits`: 1?256 transform argument objects; one repaint, native undo per edit. On failure, earlier edits remain; error includes `completed`. |
| `edit_frame` | One-based `frame` (1?100000); selects an authoring frame and extends the scene when necessary. Marks the document dirty. |
| `import_sound` | `path` to a WAVE file; imports and places a non-looping scene speech cue at the current frame. |
| `save_movie` | `path` to a **new** destination; saves through the normal legacy writer. Refuses existing or uncheckable destinations. |

`position: [x,y,z]` is absolute world position; `offset` is a relative world
translation. Supply one or the other. `rotation: [x,y,z]` uses world-axis Euler
degrees, applied X then Y then Z; absolute rotation starts from the template's
default orientation. Size uses uniform `scale` (0.25?10) and
per-axis `stretch` (each 0.25?4), matching retail editing limits. Rotation and
size are absolute by default; `relative: true` composes them with the current
pose. Omitting a size component preserves it. `color: "#RRGGBB"` paints every
body set using the nearest original indexed palette ramp; RGB is approximate,
with dark facial details using the nearest dark entry. This creates ordinary
MTRL resources, readable by retail.

Edits apply at the current frame. To animate prop assemblies, select successive
frames with `edit_frame` and submit each pose with `transform_actors`. No
mouse dragging or serialized-file rewriting is involved. A single failed edit
restores its actor snapshot; a batch is intentionally sequential, not atomic.
Actor names must fit the legacy movie encoding; filenames use native path
conversion. Keep reference movies immutable and write experiments under `tmp/`.

`tests/studio_authoring_workflow.py EXE ISO ARTIFACTS` verifies transforms,
axis order, rejected edits, forward/backward frame poses and save/reopen in a
fresh process. CTest registers it as `studio-authoring-workflow` when the ISO
and SDL test tools are available.

## Verification

### Script-side groups and joints

[`tools/mcp_rig.py`](../../tools/mcp_rig.py) provides a small, standard-library
authoring layer over the direct tools. Groups and joints are script nodes;
only leaf geometry becomes ordinary props in the saved movie. No engine or
movie-format extensions are required.

```python
from mcp_rig import Rig, Baker

rig = Rig()
rig.group("body", position=(0, 12, -40))
rig.group("shoulder", "body", position=(3, 0, 0))
rig.group("elbow", "shoulder", position=(0, -5, 0))
rig.prop("Upper segment #1", 4130, (2, 5, 2), "#1b6c34",
         "shoulder", position=(0, -2.5, 0))
rig.prop("Lower segment #1", 4131, (2, 4, 2), "#cc4433",
         "elbow", position=(0, -2, 0))
rig.key("shoulder", 0)
rig.key("shoulder", 2, rotation=(0, 0, 65))
rig.key("elbow", 0)
rig.key("elbow", 2, rotation=(0, 0, 35))

# call(tool, **arguments) returns the decoded JSON/text tool result.
# Start with an idle scene at authoring frame 1.
baker = Baker(rig, call)
baker.create()
baker.bake(frames=15, duration=2)
```

Group and prop transforms are local to their parent. `key` values are offsets
from the bind pose; position/scale interpolate numerically and rotations use
quaternion interpolation along the shorter arc. Easing is `smooth` by default,
with `linear` and `hold` alternatives; it applies to the interval after that
key. Prop dimensions can have independent `size` keys. Hierarchy scale is
uniform; per-axis dimensions affect the prop geometry without stretching its
children. This avoids shear and respects retail's size limits.

`rig.reparent(node, new_parent, time, keep_world=True)` switches attachments
at a timeline time while preserving the object's world transform at that
instant. Subsequent poses follow the new parent; `None` detaches to world space.
With `keep_world=False`, the node attaches at the new parent's origin before
its keyed offsets. Cycles are rejected. Keep prop labels short enough for the
legacy name field; script group names are not serialized.

The baker reads model dimensions from MCP, converts world poses into native
position/rotation/scale/stretch events, and batches up to 256 edits per request.
It emits only changed transform components at each sampled frame, preserving
retail playback semantics. Unchanged poses produce no actor edits. A failed
batch leaves its cache unchanged so that the pose can be retried. A duration
controls keyframe sampling; it does not change the engine's six-frame-per-second
playback rate. This improves articulation and easing within the
native frame cadence. There is no mesh skinning or inverse-kinematics solver.

`mcp-rig` checks pivot geometry, inherited scale, shortest rotations, Euler
conversion, attachment continuity and cycle rejection without assets.
`studio-rig-workflow` bakes a generic articulated assembly through actual MCP,
then verifies every saved pose's position, orientation and dimensions after
reopening, including reverse seeks. All generated movies stay under `tmp/`.

`studio-runtime-compile-check` includes the actual Debug server on Linux/macOS.
`mcp-io` and `mcp-png` CTests verify transport and independently decoded PNG
pixels. `tests/studio_mcp_stdio_test.py EXE CONTENT_DIR` drives real Studio through
fragmented UTF-8/CRLF, large requests, notifications, errors, screenshots, quit
with stdin open and EOF with a final pending request. It opens no movie.
`tests/studio_sdl_smoke.py` covers original editing, SDL input/dialogs and save
prompts. See the [portable MCP checkpoint](../tools/studio-sdl.md#portable-mcp-checkpoint-4-november-2026).

## Helper scripts (`scripts/`)

| Script                       | Purpose                                                            |
|------------------------------|--------------------------------------------------------------------|
| `mcp-driver.py`              | Generic interactive driver — REPL on top of the MCP server.        |
| `repro-add-actor.py`         | Repro probe: `cidNew` → `cidNewScene` → roll-call add an actor.    |
| `repro-add-actor2.py`        | Variant of the above.                                              |
| `repro-add-all.py`           | Walks every actor template in the roll-call browser.               |
| `repro-actors-only.py`       | Adds actors only (skips backgrounds / props).                      |
| `repro-close-av.py`          | Repro probe for the studio close / WM_CLOSE shutdown path.         |
| `repro-each.py`              | Iterates over a list of (cid, hid) pairs and reports per-step state. |
| `repro-open-movie-av.py`     | Repro probe for the portfolio-Open AV (the `lCustData` x64 bug).   |
| `repro-scene-choices.py`     | Walks scene-choice buttons.                                        |
| `test-click.py`              | Smoke test for the `click` tool.                                   |
| `run-3dmm.ps1`               | PowerShell launcher for ad-hoc `--mcp-server` runs.                |

Each script follows the same shape: `subprocess.Popen` of `3dmovie.exe
--mcp-server`, send `initialize` + `notifications/initialized`, walk a
sequence of `send_command` / `wait_for_gob` / `screenshot` calls, and
report any dialogs / crash-log content along the way. Read
`repro-close-av.py` first for a small, well-commented example.

## Safety notes

- The MCP server is `#ifdef DEBUG`-only; release builds don't include
  the parser, the worker thread, or the `--mcp-server` flag handling.
- `mcpserv.cpp` carefully `#undef`s kauai's legacy `size(x)` macro
  after kauai headers have been included — STL containers like
  `std::vector::size()` would otherwise be mangled by the preprocessor.

## See also

- [`studio.md`](studio.md) — the application surface the MCP tools poke at.
- [`../../scripts/`](../../scripts/) — driver + repro scripts.
- The `mcpserv.cpp` header comment is the most up-to-date description
  of the threading model.
