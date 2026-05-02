# MCP server

DEBUG builds of `3dmovie.exe` embed a small **Model Context Protocol**
server that lets an AI agent (Claude Code, etc.) drive the studio
headlessly: take screenshots, click points, send kauai commands,
inspect dialogs, read crash logs, and post WM_CLOSE for shutdown.

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

Newline-delimited JSON-RPC 2.0 over stdin / stdout. Standard MCP
methods: `initialize`, `notifications/initialized`, `tools/list`,
`tools/call`.

## Threading

```
+-----------+   ReadFile     +-----------------+   Drain (per kauai tick)
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

Worker-safe tools (`list_dialogs`, `dismiss_dialog`) bypass the queue
and run directly on the worker thread, so they still work when the
main thread is blocked in a modal box. Source:
[`src/studio/mcpserv.cpp`](../../src/studio/mcpserv.cpp).

## Tools exposed

From the `kTools` table in
[`src/studio/mcpserv.cpp:1380`](../../src/studio/mcpserv.cpp):

| Tool             | Worker-safe | Purpose                                                              |
|------------------|:-----------:|----------------------------------------------------------------------|
| `screenshot`     | no          | Capture the main window's client area as PNG.                        |
| `click`          | no          | Click a point in client coords (`button` = `left` / `right`).        |
| `key`            | no          | Send a Win32 virtual-key down+up to the foreground window.           |
| `send_command`   | no          | Enqueue a kauai command (`cid`, optional `hid`, `lw0..lw3`) into `vpcex`. |
| `find_gob`       | no          | Look up a kidspace gob by `hid`; report current rect, visible rect, visible bool. |
| `wait_for_gob`   | no          | Poll `find_gob` until the gob meets a readiness signal (`visible` / `positioned` / `exists`); pumps Win messages between probes. |
| `get_state`      | no          | Main-window state, foreground status, modal dialog (if any), `ErrorStack` depth. |
| `wait_ms`        | no          | Pump Win messages for N ms (capped 30000) so the GUI can settle.     |
| `read_crash_log` | no          | Return the contents of `%TEMP%\3dmmforever-crash.txt`.               |
| `quit`           | no          | Post `WM_CLOSE` to the main window (graceful shutdown).              |
| `list_dialogs`   | **yes**     | Enumerate top-level dialogs / message boxes / RTC popups owned by the process. Returns hwnd, class, title, child-control texts. |
| `dismiss_dialog` | **yes**     | Dismiss a dialog by `hwnd` + standard button id (`abort`/`retry`/…) or `button_text` substring match. |

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
