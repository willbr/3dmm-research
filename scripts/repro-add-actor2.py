"""repro-add-actor2.py -- click through to add scene, then open actor browser.

Sequence:
  1. Click Scene Choices (kidSettingsBrowser, hid 0x20042) -> scene browser opens.
  2. Find first browser frame (kidBrowserFrame, hid 0x21120) and click it -> scene added.
  3. Click Actors (kidActorsBrowser, hid 0x20043) -> actor browser opens.
  4. Find first actor frame and click -> actor added.

Polls list_dialogs at each step so any RTC/assert is reported promptly.
"""
from __future__ import annotations
import json, subprocess, sys, time, base64, tempfile
from pathlib import Path

EXE = Path(r"C:\Users\wjbr\src\3DMMForever\dist-x64\3dmovie.exe")

def p(*a, **k): print(*a, flush=True, **k)

def main():
    proc = subprocess.Popen([str(EXE), "--mcp-server"],
        stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
        text=True, encoding="utf-8", bufsize=1)
    next_id = [1]
    def send(method, params=None, *, notification=False, timeout=8.0):
        msg = {"jsonrpc":"2.0","method":method}
        if not notification: msg["id"] = next_id[0]; next_id[0] += 1
        if params is not None: msg["params"] = params
        proc.stdin.write(json.dumps(msg) + "\n"); proc.stdin.flush()
        if notification: return None
        deadline = time.time() + timeout
        while time.time() < deadline:
            line = proc.stdout.readline()
            if not line: return {"error":"stdout closed"}
            try: obj = json.loads(line)
            except Exception: continue
            if obj.get("id") == msg["id"]: return obj
        return {"error":"timeout"}
    def call(name, args=None, timeout=8.0):
        return send("tools/call", {"name":name,"arguments":args or {}}, timeout=timeout).get("result", {})
    def text(r): return (r.get("content") or [{}])[0].get("text", "")
    def find(hid):
        try: return json.loads(text(call("find_gob", {"hid": hid})))
        except Exception: return {"found": False}
    def shot(label):
        r = call("screenshot")
        try:
            data = base64.b64decode(r["content"][0]["data"])
            out = Path(tempfile.gettempdir())/"3dmm-mcp"/f"a2-{label}.png"
            out.parent.mkdir(exist_ok=True); out.write_bytes(data)
            p(f"  shot: {out}")
        except Exception as e: p(f"  shot err: {e}")
    def dialogs(label):
        d = json.loads(text(call("list_dialogs")))
        if d.get("count", 0):
            p(f"=== DIALOG @ {label} ===")
            p(json.dumps(d, indent=2))
            for dlg in d["dialogs"]:
                p(f"[dismiss ignore on {dlg['hwnd']}]")
                call("dismiss_dialog", {"hwnd": dlg["hwnd"], "button_text":"ignore"})
            return True
        return False
    def click_gob(hid, label):
        g = find(hid)
        if not g.get("found"):
            p(f"  gob {hex(hid)} NOT FOUND ({label})")
            return False
        p(f"  click {label} hid={hex(hid)} center=({g['center_x']},{g['center_y']})")
        call("click", {"x": g["center_x"], "y": g["center_y"], "button":"left"})
        call("wait_ms", {"ms": 1500})
        return True

    p("[wait 3s]"); time.sleep(3)
    send("initialize", {"protocolVersion":"2024-11-05","capabilities":{},
                        "clientInfo":{"name":"a2","version":"0"}})
    send("notifications/initialized", notification=True)
    dialogs("startup")
    shot("00-start")

    # Step 1: click Scene Choices.
    p("\n[1] click Scene Choices (kidSettingsBrowser 0x20042)")
    if not click_gob(0x20042, "scene-choices"): return
    if dialogs("after-scene-choices"): pass
    shot("01-scene-browser")
    bg = find(0x2003E)  # kidBrwsBackground -- the browser background
    p(f"  kidBrwsBackground: {bg}")

    # Step 2: click first scene thumbnail (kidBrowserFrame=0x21120, first slot).
    p("\n[2] click first scene thumbnail (kidBrowserFrame 0x21120)")
    if not click_gob(0x21120, "scene-thumbnail-0"): return
    if dialogs("after-scene-thumb"): pass
    shot("02-after-scene-pick")

    # Step 3: click Actors button (kidActorsBrowser=0x20043).
    p("\n[3] click Actors (kidActorsBrowser 0x20075)")
    if not click_gob(0x20075, "actors"): return
    if dialogs("after-actors"): pass
    shot("03-actor-browser")

    # Step 4: click first actor thumbnail.
    p("\n[4] click first actor thumbnail (kidBrowserFrame 0x21120)")
    if not click_gob(0x21120, "actor-thumbnail-0"): return
    if dialogs("after-actor-thumb"): pass
    shot("04-after-actor-pick")

    p("\n[done; killing]")
    try: proc.kill()
    except Exception: pass

if __name__ == "__main__":
    main()
