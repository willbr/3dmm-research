"""repro-open-movie-av.py -- exercise studio's open-movie / save-on-close path
via MCP, looking for the access violation the user reported in the app.

The standalone movie-save-load-test only exercises the engine in isolation
(blank Movie, no PWorld view, no Studio doc layer). The studio at runtime
also runs the kidspace UI binding, the document framework, and the
autosave/save-on-close path -- any of which can be the AV site.

Strategy: spawn 3dmovie under the embedded MCP server, drive it through
the most common save/load entry points, and after every cid poll
list_dialogs + read_crash_log so an AV (which surfaces as either a
kauai assert dialog or a Win32 RTC popup) can't slip past.

Sequence:
  1. Wait for studio init.
  2. Capture initial state + screenshot.
  3. cidNew                  -- load a fresh blank movie via the doc layer
  4. cidNewScene             -- give it a scene (matches what the standalone
                                  test does, but through the doc layer)
  5. Take a screenshot, list dialogs, read crash log.
  6. cidQuit                 -- triggers the save-on-close path: this is
                                  where Movie::~Movie + Studio::FShutdown
                                  + autosave finalization all run.
  7. Final screenshot, dialogs, crash log.
  8. Force-kill if the process is still alive (it shouldn't be).

Any RTC dialog / kauai assert / read_crash_log entry is reported with
the cid that preceded it, so the AV can be pinned to a specific step.

Usage:
  py scripts/repro-open-movie-av.py [--exe PATH]

The release build is the default target since the user reported the AV
"in the app" -- meaning a normally-built studio. Override with --exe to
test debug builds, x64, etc.
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
CID_OPEN = 101
CID_SAVE = 103
CID_SAVE_AS = 104
CID_QUIT = 102

CID_NEW_SCENE = 50006
CID_LOAD_PROJECT_MOVIE = 50090

# kauai handler ids. khidStudio=20005 routes the engine/doc cids to the
# Studio command map; khidApp=1 routes app-global cids (quit, etc).
HID_APP = 1
HID_STUDIO = 20005


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--exe",
                    default=r"C:\Users\wjbr\src\3DMMForever\dist-x64\3dmovie.exe",
                    help="3dmovie.exe to spawn (must support --mcp-server, ie DEBUG build). "
                         "Defaults to dist-x64 because the existing repro-*.py scripts target it.")
    ap.add_argument("--launch-wait", type=float, default=4.0,
                    help="Seconds to let the studio settle after launch before talking.")
    ap.add_argument("--inter-step-wait", type=float, default=2.0,
                    help="Seconds between cid sends so the GUI can settle.")
    ap.add_argument("--no-quit", action="store_true",
                    help="Skip the final cidQuit step (leave the studio running).")
    args = ap.parse_args()

    exe = Path(args.exe)
    if not exe.exists():
        print(f"!!! exe not found: {exe}", file=sys.stderr)
        return 2

    out_dir = Path(tempfile.gettempdir()) / "3dmm-mcp"
    out_dir.mkdir(exist_ok=True)
    print(f"[setup] output dir: {out_dir}")
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
        proc.stdin.write(json.dumps(msg) + "\n")
        proc.stdin.flush()
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
                # Server may have logged a non-JSON line; show and skip.
                print(f"# unparseable line from server: {line.rstrip()!r}", file=sys.stderr)
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
            for dlg in d["dialogs"]:
                print(f"  -> dismiss(ignore) on hwnd={dlg['hwnd']}")
                call("dismiss_dialog", {"hwnd": dlg["hwnd"], "button_text": "ignore"})
            return True
        return False

    def screenshot(tag):
        r = call("screenshot")
        try:
            data = base64.b64decode(r["content"][0]["data"])
            out = out_dir / f"open-movie-{time.strftime('%H%M%S')}-{tag}.png"
            out.write_bytes(data)
            print(f"  -> screenshot: {out} ({len(data)} bytes)")
        except Exception as e:
            print(f"  !! screenshot {tag} failed: {e}")

    def crash_log(label):
        r = call("read_crash_log")
        t = text_of(r) or ""
        if t.strip():
            print(f"=== CRASH LOG @ {label} ===")
            print(t)
        return t

    print(f"[wait {args.launch_wait}s for studio to come up]")
    time.sleep(args.launch_wait)

    init = send("initialize", {
        "protocolVersion": "2024-11-05",
        "capabilities": {},
        "clientInfo": {"name": "repro-open-movie-av", "version": "0.1"},
    })
    si = init.get("result", init).get("serverInfo")
    if si is None:
        print(f"!!! initialize failed: {init}")
        proc.kill()
        return 1
    print(f"[init] serverInfo={si}")
    send("notifications/initialized", notification=True)

    # ---- Step 0: startup probe -----------------------------------------------
    print("\n[step 0] startup state")
    state = call("get_state")
    print(f"  state: {text_of(state)}")
    report_dialogs("startup")
    screenshot("00-startup")
    crash_log("startup")

    # ---- Step 1: cidNew (blank movie via doc layer) --------------------------
    print(f"\n[step 1] cidNew ({CID_NEW}) hid=khidStudio({HID_STUDIO})")
    r = call("send_command", {"cid": CID_NEW, "hid": HID_STUDIO}, timeout=5.0)
    print(f"  send_command: {text_of(r)}")
    time.sleep(args.inter_step_wait)
    if report_dialogs("after cidNew"):
        screenshot("01-cidNew-dialog")
    else:
        screenshot("01-cidNew-clean")
    crash_log("after cidNew")

    # ---- Step 2: cidNewScene (add a scene) -----------------------------------
    print(f"\n[step 2] cidNewScene ({CID_NEW_SCENE})")
    r = call("send_command", {"cid": CID_NEW_SCENE, "hid": HID_STUDIO}, timeout=5.0)
    print(f"  send_command: {text_of(r)}")
    time.sleep(args.inter_step_wait)
    if report_dialogs("after cidNewScene"):
        screenshot("02-cidNewScene-dialog")
    else:
        screenshot("02-cidNewScene-clean")
    crash_log("after cidNewScene")

    # ---- Step 3: cidSave (autosave -> on-disk roundtrip) ---------------------
    # Hits Movie::FAutoSave + the docb save path. May pop a Save-As dialog
    # since cidNew left the movie pathless; if so list_dialogs will see it
    # and we capture rather than try to drive it.
    print(f"\n[step 3] cidSave ({CID_SAVE})")
    r = call("send_command", {"cid": CID_SAVE, "hid": HID_STUDIO}, timeout=5.0)
    print(f"  send_command: {text_of(r)}")
    time.sleep(args.inter_step_wait)
    if report_dialogs("after cidSave"):
        screenshot("03-cidSave-dialog")
    else:
        screenshot("03-cidSave-clean")
    crash_log("after cidSave")

    # ---- Step 4: cidQuit -- exercises the save-on-close path -----------------
    if args.no_quit:
        print("\n[step 4] skipped (--no-quit)")
    else:
        print(f"\n[step 4] cidQuit ({CID_QUIT}) hid=khidApp({HID_APP})")
        r = call("send_command", {"cid": CID_QUIT, "hid": HID_APP}, timeout=5.0)
        print(f"  send_command: {text_of(r)}")
        time.sleep(args.inter_step_wait)
        if report_dialogs("during quit"):
            screenshot("04-quit-dialog")
        crash_log("during quit")

        # Quit may pop a "save changes?" prompt. Acknowledge with No (don't save).
        d = list_dialogs()
        for dlg in d.get("dialogs", []):
            print(f"  -> dismiss(no) on hwnd={dlg['hwnd']}")
            call("dismiss_dialog", {"hwnd": dlg["hwnd"], "button_text": "no"})
        time.sleep(args.inter_step_wait)
        crash_log("after quit-prompt-no")

    # ---- Final state ---------------------------------------------------------
    print("\n[final] state, dialogs, crash log")
    if proc.poll() is None:
        try:
            state = call("get_state", timeout=3.0)
            print(f"  state: {text_of(state)}")
        except Exception as e:
            print(f"  state probe failed: {e}")
        report_dialogs("final")
        screenshot("99-final")
    else:
        print(f"  process already exited (rc={proc.returncode})")

    final_log = crash_log("final")

    # ---- Cleanup -------------------------------------------------------------
    if proc.poll() is None:
        print("\n[cleanup] terminating studio")
        proc.terminate()
        try:
            proc.wait(timeout=5)
        except subprocess.TimeoutExpired:
            proc.kill()
    rc = proc.returncode
    print(f"\n[done] studio exit code: {rc}")
    if rc not in (0, None):
        # Common Windows exit codes:
        #  -1073741819 / 0xC0000005 = STATUS_ACCESS_VIOLATION
        #  -1073740940 / 0xC0000374 = STATUS_HEAP_CORRUPTION
        #  -1073741676 / 0xC00000FD = STATUS_STACK_OVERFLOW
        nice = {
            -1073741819: "STATUS_ACCESS_VIOLATION (the AV)",
            -1073740940: "STATUS_HEAP_CORRUPTION",
            -1073741676: "STATUS_STACK_OVERFLOW",
            -1073741510: "STATUS_CONTROL_C_EXIT",
        }.get(rc, "")
        if nice:
            print(f"        ^^ {nice}")

    if final_log.strip():
        print("\n[summary] crash log captured -- see CRASH LOG sections above")
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main() or 0)
