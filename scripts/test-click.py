"""test-click.py -- verify the PostMessage-based click triggers kidspace.

Click the Scene Choices button and check whether the scene browser opens
(observed via screenshot or by detecting browser-related gobs).
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
        return send("tools/call", {"name":name,"arguments":args or {}}, timeout=timeout).get("result", {})
    def shot(label):
        r = call("screenshot")
        try:
            data = base64.b64decode(r["content"][0]["data"])
            out = Path(tempfile.gettempdir())/"3dmm-mcp"/f"click-{label}.png"
            out.parent.mkdir(exist_ok=True); out.write_bytes(data)
            p(f"  shot: {out}")
        except Exception as e:
            p(f"  shot error: {e}")

    p("[wait 3s]"); time.sleep(3)
    send("initialize", {"protocolVersion":"2024-11-05","capabilities":{},
                        "clientInfo":{"name":"t","version":"0"}})
    send("notifications/initialized", notification=True)

    p("[before click]")
    shot("before")
    # Find the Scene Choices button (kidSettingsBrowser=0x20042) -- center.
    r = call("find_gob", {"hid": 0x20042})
    p("  scene-choices gob:", r.get("content", [{}])[0].get("text", ""))

    p("[click (49,21) - center of Scene Choices button]")
    r = call("click", {"x":49, "y":21, "button":"left"})
    p("  click:", r.get("content", [{}])[0].get("text", ""))

    time.sleep(2.0)
    p("[after click]")
    d = json.loads(call("list_dialogs")["content"][0]["text"])
    p(f"  dialogs: count={d.get('count', 0)}")
    if d.get("count", 0):
        p(f"  dialog detail: {json.dumps(d, indent=2)[:500]}")

    # Check if the browser is up by looking for kidBrwsBackground gob (0x2003E).
    r = call("find_gob", {"hid": 0x2003E})
    p("  kidBrwsBackground gob:", r.get("content", [{}])[0].get("text", ""))
    shot("after")

    proc.kill()

if __name__ == "__main__":
    main()
