"""repro-scene-choices.py -- one-shot repro for the scene-choices x64 crash.

Spawns 3dmovie.exe --mcp-server, fires cidBrowserReady directly (the same cid
that clicking the Scene Choices button enqueues), then polls list_dialogs
(worker-thread-safe so it works even when the main thread is blocked in a
modal MessageBox) and reports the dialog text. Prints every line flushed.
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
        msg = {"jsonrpc": "2.0", "method": method}
        if not notification:
            msg["id"] = next_id[0]; next_id[0] += 1
        if params is not None: msg["params"] = params
        proc.stdin.write(json.dumps(msg) + "\n")
        proc.stdin.flush()
        if notification: return None
        deadline = time.time() + timeout
        while time.time() < deadline:
            line = proc.stdout.readline()
            if not line: return {"error": "stdout closed"}
            try: obj = json.loads(line)
            except Exception: continue
            if obj.get("id") == msg["id"]: return obj
        return {"error": "timeout"}

    p("[wait 3s for studio to come up]")
    time.sleep(3.0)
    init = send("initialize", {"protocolVersion": "2024-11-05",
                               "capabilities": {}, "clientInfo": {"name":"repro","version":"0"}})
    p("init:", json.dumps(init.get("result", init)))
    send("notifications/initialized", notification=True)

    # Sanity check: no dialogs before.
    r = send("tools/call", {"name":"list_dialogs","arguments":{}})
    p("dialogs before:", r.get("result", r))

    # Fire cidBrowserReady directly: same as clicking kidSettingsBrowser.
    # cid=50018, target=khidStudio(20005), lw0=kidBrwsBackground(0x2003E),
    # lw1=kidBrowserFrame(0x21120), lw2=kidBrowserPageFwd(0x21010),
    # lw3=(kdxpSceneFrameBorder<<16)|kdypSceneFrameBorder = 0x40004.
    p("[fire cidBrowserReady ...]")
    r = send("tools/call", {"name":"send_command","arguments":{
        "cid":50018,"hid":20005,
        "lw0":0x2003E,"lw1":0x21120,"lw2":0x21010,"lw3":0x40004,
    }}, timeout=3.0)
    p("send_command response:", r.get("result", r))

    # Now poll list_dialogs every 500ms for up to 10s.
    p("[poll list_dialogs ...]")
    deadline = time.time() + 10.0
    while time.time() < deadline:
        r = send("tools/call", {"name":"list_dialogs","arguments":{}}, timeout=3.0)
        result = r.get("result", {})
        # Extract the JSON in the text content.
        try:
            text = result["content"][0]["text"]
            data = json.loads(text)
        except Exception:
            p("  unparseable list_dialogs:", r); time.sleep(0.5); continue
        if data.get("count", 0) > 0:
            p("=== DIALOG CAUGHT ===")
            p(json.dumps(data, indent=2))
            # Try to dismiss it via Ignore.
            for d in data["dialogs"]:
                p("[dismiss ignore on hwnd", d["hwnd"], "]")
                rr = send("tools/call", {"name":"dismiss_dialog","arguments":{
                    "hwnd": d["hwnd"], "button_text": "ignore",
                }}, timeout=3.0)
                p("dismiss:", rr.get("result", rr))
            break
        time.sleep(0.5)
    else:
        p("no dialog seen within 10s")

    p("[done; killing child]")
    try: proc.kill()
    except Exception: pass

if __name__ == "__main__":
    sys.exit(main() or 0)
