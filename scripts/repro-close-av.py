"""repro-close-av.py -- exercise the studio close / shutdown path via MCP.

The user reports a `Assert (utilstr.cpp:979): missing termination byte`
fires when closing 3D Movie Maker. That assert is in `String::AssertValid`
checking the Pascal-string invariant -- some String somewhere has been
zero'd or trimmed without restoring the trailing null. The shutdown
path runs Studio teardown, MovieView teardown, Movie::~Movie, and
the kauai docb close-with-save dialog -- any of which manipulates
strings (titles, paths, undo descriptions...).

Strategy: spawn 3dmovie under the embedded MCP server, walk through
a minimal session, then drive cidQuit and capture every assert dialog
+ the crash log. The more cleanly we can pin which step fires the
assert, the easier it is to bisect.

Usage:
  py scripts/repro-close-av.py [--exe PATH] [--with-scene]

`--with-scene`: also do cidNewScene before quitting (matches the user's
typical session shape; the bare blank-movie close path may not repro).
"""
from __future__ import annotations

import argparse
import base64
import json
import subprocess
import sys
import tempfile
import time
from pathlib import Path

# kauai cid constants pulled from kauai/src/framedef.h + inc/stdiodef.h.
CID_NEW = 100
CID_QUIT = 102

CID_NEW_SCENE = 50006

# kauai handler ids.
HID_APP = 1
HID_STUDIO = 20005


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--exe",
                    default=r"C:\Users\wjbr\src\3DMMForever\dist-x64\3dmovie.exe",
                    help="3dmovie.exe to spawn (must support --mcp-server, ie DEBUG build).")
    ap.add_argument("--launch-wait", type=float, default=4.0)
    ap.add_argument("--inter-step-wait", type=float, default=2.0)
    ap.add_argument("--with-scene", action="store_true",
                    help="cidNewScene before quitting (matches a populated session).")
    args = ap.parse_args()

    exe = Path(args.exe)
    if not exe.exists():
        print(f"!!! exe not found: {exe}", file=sys.stderr)
        return 2

    out_dir = Path(tempfile.gettempdir()) / "3dmm-mcp"
    out_dir.mkdir(exist_ok=True)
    print(f"[setup] launching: {exe} --mcp-server")

    proc = subprocess.Popen(
        [str(exe), "--mcp-server"],
        stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
        text=True, encoding="utf-8", bufsize=1,
    )
    next_id = [1]

    def send(method, params=None, *, notification=False, timeout=15.0):
        msg = {"jsonrpc": "2.0", "method": method}
        if not notification:
            msg["id"] = next_id[0]
            next_id[0] += 1
        if params is not None:
            msg["params"] = params
        try:
            proc.stdin.write(json.dumps(msg) + "\n")
            proc.stdin.flush()
        except (OSError, BrokenPipeError) as e:
            return {"error": f"stdin closed ({e})"}
        if notification:
            return None
        deadline = time.time() + timeout
        while time.time() < deadline:
            line = proc.stdout.readline()
            if not line:
                return {"error": "stdout closed (server crashed?)"}
            try:
                obj = json.loads(line)
            except Exception:
                print(f"# unparseable line: {line.rstrip()!r}", file=sys.stderr)
                continue
            if obj.get("id") == msg["id"]:
                return obj
        return {"error": "timeout"}

    def call(name, args=None, timeout=15.0):
        r = send("tools/call", {"name": name, "arguments": args or {}}, timeout=timeout)
        return r.get("result", r)

    def text_of(result):
        try:
            return result["content"][0]["text"]
        except Exception:
            return None

    def list_dialogs():
        r = call("list_dialogs")
        try:
            return json.loads(r["content"][0]["text"])
        except Exception:
            return {"count": 0, "dialogs": []}

    def report_dialogs(label):
        d = list_dialogs()
        if d.get("count", 0) > 0:
            print(f"=== DIALOG @ {label} ===")
            print(json.dumps(d, indent=2))
            return d
        return None

    def screenshot(tag):
        try:
            r = call("screenshot")
            data = base64.b64decode(r["content"][0]["data"])
            out = out_dir / f"close-{time.strftime('%H%M%S')}-{tag}.png"
            out.write_bytes(data)
            print(f"  -> screenshot: {out} ({len(data)} bytes)")
        except Exception as e:
            print(f"  !! screenshot {tag} failed: {e}")

    def crash_log(label):
        try:
            r = call("read_crash_log")
            t = text_of(r) or ""
            if t.strip():
                print(f"=== CRASH LOG @ {label} ===")
                print(t)
            return t
        except Exception as e:
            print(f"  !! crash_log {label} failed: {e}")
            return ""

    print(f"[wait {args.launch_wait}s for studio]")
    time.sleep(args.launch_wait)

    init = send("initialize", {
        "protocolVersion": "2024-11-05",
        "capabilities": {},
        "clientInfo": {"name": "repro-close-av", "version": "0.1"},
    })
    si = init.get("result", init).get("serverInfo")
    if si is None:
        print(f"!!! initialize failed: {init}")
        proc.kill()
        return 1
    print(f"[init] serverInfo={si}")
    send("notifications/initialized", notification=True)

    print("\n[step 0] startup probe")
    state = call("get_state")
    print(f"  state: {text_of(state)}")
    report_dialogs("startup")
    crash_log("startup")
    screenshot("00-startup")

    print(f"\n[step 1] cidNew ({CID_NEW})")
    r = call("send_command", {"cid": CID_NEW, "hid": HID_STUDIO}, timeout=5.0)
    print(f"  send_command: {text_of(r)}")
    time.sleep(args.inter_step_wait)
    d = report_dialogs("after cidNew")
    if d:
        for dlg in d["dialogs"]:
            call("dismiss_dialog", {"hwnd": dlg["hwnd"], "button_text": "ignore"})
    crash_log("after cidNew")

    if args.with_scene:
        print(f"\n[step 2] cidNewScene ({CID_NEW_SCENE})")
        r = call("send_command", {"cid": CID_NEW_SCENE, "hid": HID_STUDIO}, timeout=5.0)
        print(f"  send_command: {text_of(r)}")
        time.sleep(args.inter_step_wait)
        d = report_dialogs("after cidNewScene")
        if d:
            for dlg in d["dialogs"]:
                call("dismiss_dialog", {"hwnd": dlg["hwnd"], "button_text": "ignore"})
        crash_log("after cidNewScene")

    # ---- The actual close: post WM_CLOSE via the `quit` tool. -----------
    # This matches what the user does (clicks X / Alt-F4) more closely than
    # an enqueued cidQuit; it goes through the wndproc -> docb close-with-
    # save dialog -> shutdown sequence.
    print("\n[step 3] quit (WM_CLOSE)")
    r = call("quit", timeout=5.0)
    print(f"  quit: {text_of(r)}")
    time.sleep(args.inter_step_wait)
    d = report_dialogs("during quit (1)")
    if d:
        for dlg in d["dialogs"]:
            print(f"  -> dismiss(no) hwnd={dlg['hwnd']} ({dlg.get('title')})")
            call("dismiss_dialog", {"hwnd": dlg["hwnd"], "button_text": "no"})
    crash_log("during quit (1)")
    time.sleep(args.inter_step_wait)

    # Second pass after dismissing the save? prompt -- the assert may
    # fire after the user picks "Don't Save" and Movie::~Movie runs.
    if proc.poll() is None:
        d = report_dialogs("during quit (2)")
        if d:
            for dlg in d["dialogs"]:
                print(f"  -> dismiss(ignore) hwnd={dlg['hwnd']} ({dlg.get('title')})")
                call("dismiss_dialog", {"hwnd": dlg["hwnd"], "button_text": "ignore"})
        crash_log("during quit (2)")
        time.sleep(args.inter_step_wait)

    final_log = crash_log("final")
    print("\n[final]")
    if proc.poll() is None:
        print("  process still alive -- terminating")
        proc.terminate()
        try:
            proc.wait(timeout=5)
        except subprocess.TimeoutExpired:
            proc.kill()
    rc = proc.returncode
    nice = {
        -1073741819: "STATUS_ACCESS_VIOLATION (the AV)",
        -1073740940: "STATUS_HEAP_CORRUPTION",
        -1073741676: "STATUS_STACK_OVERFLOW",
    }.get(rc, "")
    print(f"  exit code: {rc} {nice}")

    return 0 if rc in (0, None) and not final_log.strip() else 1


if __name__ == "__main__":
    sys.exit(main() or 0)
