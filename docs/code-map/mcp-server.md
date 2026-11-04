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

## Verification

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
