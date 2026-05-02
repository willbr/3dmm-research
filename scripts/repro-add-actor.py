"""repro-add-actor.py -- exercise the add-actor x64 path.

1. Spawn 3dmovie.exe --mcp-server.
2. cidNew (100) -- load blank movie.
3. cidBrowserReady(kidBrwsBackground) to bring up the scene browser, but
   we can't easily pick a scene from inside it via MCP yet, so instead:
4. cidNewScene (50006) -- add a fresh scene.
5. cidBrowserReady(kidBrwsActor) to open the actors browser.
6. Poll list_dialogs at each step to catch any RTC/assert.
"""
from __future__ import annotations
import json, subprocess, sys, time
from pathlib import Path

EXE = Path(r"C:\Users\wjbr\src\3DMMForever\dist-x64\3dmovie.exe")

def p(*args, **kw): print(*args, flush=True, **kw)

def main():
    proc = subprocess.Popen(
        [str(EXE), "--mcp-server"],
        stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
        text=True, encoding="utf-8", bufsize=1,
    )
    next_id = [1]

    def send(method, params=None, *, notification=False, timeout=10.0):
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

    def call(name, args=None, timeout=10.0):
        r = send("tools/call", {"name":name, "arguments": args or {}}, timeout=timeout)
        return r.get("result", r)

    def list_dialogs():
        r = call("list_dialogs")
        try: return json.loads(r["content"][0]["text"])
        except Exception: return {"count":0,"dialogs":[]}

    def report_dialogs(label):
        d = list_dialogs()
        if d.get("count", 0) > 0:
            p(f"=== DIALOG @ {label} ===")
            p(json.dumps(d, indent=2))
            for dlg in d["dialogs"]:
                p("[dismiss ignore on", dlg["hwnd"], "]")
                call("dismiss_dialog", {"hwnd": dlg["hwnd"], "button_text":"ignore"})
            return True
        return False

    p("[wait 3s for studio to come up]")
    time.sleep(3.0)
    init = send("initialize", {"protocolVersion":"2024-11-05","capabilities":{},
                               "clientInfo":{"name":"repro","version":"0"}})
    p("init:", init.get("result", init).get("serverInfo", "(none)"))
    send("notifications/initialized", notification=True)

    report_dialogs("startup")

    # Step 1: cidNew (load a blank movie).
    p("\n[step 1: cidNew (blank movie)]")
    r = call("send_command", {"cid":100, "hid":20005}, timeout=3.0)
    p("send_command:", r)
    time.sleep(2.0)
    if report_dialogs("after cidNew"): pass

    # Step 2: cidNewScene (add a fresh scene).
    p("\n[step 2: cidNewScene]")
    r = call("send_command", {"cid":50006, "hid":20005}, timeout=3.0)
    p("send_command:", r)
    time.sleep(2.0)
    if report_dialogs("after cidNewScene"): pass

    # Step 3: cidBrowserReady(kidBrwsActor) -- open actors browser.
    # kidBrwsActor=0x2003F, kidBrowserFrame=0x21120, kidBrowserPageFwd=0x21010,
    # lw3=(kdxpActorFrameBorder<<16)|kdypActorFrameBorder = 0x40004.
    p("\n[step 3: cidBrowserReady(kidBrwsActor)]")
    r = call("send_command", {"cid":50018, "hid":20005,
                              "lw0":0x2003F, "lw1":0x21120, "lw2":0x21010, "lw3":0x40004},
             timeout=3.0)
    p("send_command:", r)
    time.sleep(3.0)
    if report_dialogs("after actor-browser"): pass

    # Capture screenshot to confirm the browser is up.
    p("\n[final screenshot]")
    r = call("screenshot")
    try:
        import base64, tempfile
        data_b64 = r["content"][0]["data"]
        data = base64.b64decode(data_b64)
        out = Path(tempfile.gettempdir()) / "3dmm-mcp" / f"add-actor-{time.strftime('%H%M%S')}.png"
        out.parent.mkdir(exist_ok=True)
        out.write_bytes(data)
        p("screenshot saved:", out)
    except Exception as e:
        p("screenshot failed:", e)

    p("\n[done; killing child]")
    try: proc.kill()
    except Exception: pass

if __name__ == "__main__":
    sys.exit(main() or 0)
