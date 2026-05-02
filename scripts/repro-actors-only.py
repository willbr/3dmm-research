"""repro-actors-only.py -- click ONLY the Actors button (no prior scene
pick). Tests whether the COMDLG32 / OpenHookProc AV is downstream of the
scene-pick path, or fires from a clean-state actors click on its own.
"""
from __future__ import annotations
import json, subprocess, time, base64, tempfile
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
    def shot(label):
        r = call("screenshot")
        try:
            data = base64.b64decode(r["content"][0]["data"])
            out = Path(tempfile.gettempdir())/"3dmm-mcp"/f"actrs-{label}.png"
            out.parent.mkdir(exist_ok=True); out.write_bytes(data)
            p(f"  shot: {out.name}")
        except Exception as e: p(f"  shot err: {e}")
    def dialogs(label):
        d = json.loads(text(call("list_dialogs")))
        if d.get("count", 0):
            p(f"  === DIALOG @ {label} ===")
            for dlg in d["dialogs"]:
                p(f"    title: {dlg.get('title','?')}  hwnd={dlg['hwnd']}")
                for kid in dlg.get("children", []):
                    p(f"    kid: {kid[:200]}")
                p(f"    [dismiss ignore on {dlg['hwnd']}]")
                call("dismiss_dialog", {"hwnd": dlg["hwnd"], "button_text":"ignore"})
            return True
        return False

    p("[wait 3s]"); time.sleep(3)
    send("initialize", {"protocolVersion":"2024-11-05","capabilities":{},
                        "clientInfo":{"name":"actors-only","version":"0"}})
    send("notifications/initialized", notification=True)
    dialogs("startup")
    shot("00-start")

    p("\n[1] Click Actors directly (no scene first)")
    g = json.loads(text(call("find_gob", {"hid": 0x20075})))
    p(f"  find_gob 0x20075: {g}")
    if not g.get("found"):
        p("  not found, abort"); return
    cx, cy = g["center_x"], g["center_y"]
    p(f"  click ({cx},{cy})")
    call("click", {"x": cx, "y": cy, "button":"left"})

    # Poll for state changes for 10s
    for i in range(20):
        time.sleep(0.5)
        if dialogs(f"poll-{i}"):
            shot(f"01-dialog-{i}")
        g = json.loads(text(call("find_gob", {"hid": 0x2003F})))
        if g.get("found") and g.get("visible"):
            p(f"  actor browser visible at poll-{i}: {g}")
            shot(f"02-browser-{i}")
            break
    else:
        p("  actor browser never appeared")
        shot("99-final")

    p("[done; killing]")
    try: proc.kill()
    except Exception: pass

if __name__ == "__main__":
    main()
