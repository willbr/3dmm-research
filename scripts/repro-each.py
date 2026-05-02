"""repro-each.py -- test each of: 3D text, props browser, sfx browser
opening on x64. Skips the (unreliable) thumbnail-pick step and just
verifies the entry-point command/click doesn't crash.

For each tested feature we:
  - reset to a fresh process,
  - cidNew (blank movie),
  - fire the entry cid (or click the entry button),
  - poll list_dialogs (worker-direct) for assertions,
  - take a screenshot.
"""
from __future__ import annotations
import json, subprocess, sys, time, base64, tempfile
from pathlib import Path

EXE = Path(r"C:\Users\wjbr\src\3DMMForever\dist-x64\3dmovie.exe")

def p(*a, **k): print(*a, flush=True, **k)

class Driver:
    def __init__(self):
        self.proc = subprocess.Popen([str(EXE), "--mcp-server"],
            stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
            text=True, encoding="utf-8", bufsize=1)
        self.next_id = 1
        time.sleep(3)
        self.send("initialize", {"protocolVersion":"2024-11-05","capabilities":{},
                                  "clientInfo":{"name":"each","version":"0"}})
        self.send("notifications/initialized", notification=True)
    def send(self, method, params=None, *, notification=False, timeout=6.0):
        msg = {"jsonrpc":"2.0","method":method}
        if not notification: msg["id"] = self.next_id; self.next_id += 1
        if params is not None: msg["params"] = params
        try:
            self.proc.stdin.write(json.dumps(msg) + "\n"); self.proc.stdin.flush()
        except OSError:
            return {"error":"stdin closed"}
        if notification: return None
        deadline = time.time() + timeout
        while time.time() < deadline:
            line = self.proc.stdout.readline()
            if not line: return {"error":"stdout closed"}
            try: obj = json.loads(line)
            except Exception: continue
            if obj.get("id") == msg["id"]: return obj
        return {"error":"timeout"}
    def call(self, name, args=None, timeout=6.0):
        return self.send("tools/call", {"name":name,"arguments":args or {}}, timeout=timeout).get("result", {})
    def text(self, r): return (r.get("content") or [{}])[0].get("text", "")
    def dialogs(self):
        r = self.call("list_dialogs")
        try: return json.loads(self.text(r))
        except Exception: return {"count":0,"dialogs":[]}
    def report_and_dismiss(self, label):
        d = self.dialogs()
        if d.get("count", 0):
            p(f"  === DIALOG @ {label} ===")
            for dlg in d["dialogs"]:
                p(f"    title: {dlg.get('title','?')}")
                for kid in dlg.get("children", []):
                    p(f"    kid: {kid[:200]}")
                self.call("dismiss_dialog", {"hwnd": dlg["hwnd"], "button_text":"ignore"})
            return True
        return False
    def shot(self, label):
        r = self.call("screenshot")
        try:
            data = base64.b64decode(r["content"][0]["data"])
            out = Path(tempfile.gettempdir())/"3dmm-mcp"/f"each-{label}.png"
            out.parent.mkdir(exist_ok=True); out.write_bytes(data)
            p(f"  shot: {out.name} ({len(data)}B)")
        except Exception: p("  shot: skipped")
    def kill(self):
        try: self.proc.kill()
        except Exception: pass

def run_test(label, fn):
    p(f"\n========== {label} ==========")
    d = Driver()
    try:
        d.report_and_dismiss("startup")
        d.shot(f"{label}-00-start")
        fn(d)
    finally:
        d.kill()

def test_spletter(d):
    p("[fire cidNewSpletter (50029) khidStudio]")
    r = d.call("send_command", {"cid":50029, "hid":20005, "lw0":0, "lw1":0, "lw2":0, "lw3":0})
    p(f"  send: {d.text(r)}")
    time.sleep(2)
    d.report_and_dismiss("after-cidNewSpletter")
    d.shot("spletter-01-after")

def test_props_browser(d):
    p("[fire cidBrowserReady kidBrwsProp (0x20040)]")
    # cid=cidBrowserReady=50018, target=khidStudio(20005),
    # lw0=kidBrwsProp(0x20040), lw1=kidBrowserFrame(0x21120),
    # lw2=kidBrowserPageFwd(0x21010), lw3=(kdxpPropFrameBorder<<16)|kdypPropFrameBorder
    r = d.call("send_command", {"cid":50018, "hid":20005,
                                 "lw0":0x20040, "lw1":0x21120, "lw2":0x21010, "lw3":0x40004})
    p(f"  send: {d.text(r)}")
    time.sleep(2)
    d.report_and_dismiss("after-cidBrowserReady-prop")
    d.shot("prop-01-after")

def test_sfx_browser(d):
    p("[fire cidBrowserReady kidBrwsFX (0x20044)]")
    r = d.call("send_command", {"cid":50018, "hid":20005,
                                 "lw0":0x20044, "lw1":0x21120, "lw2":0x21010, "lw3":0x40004})
    p(f"  send: {d.text(r)}")
    time.sleep(2)
    d.report_and_dismiss("after-cidBrowserReady-fx")
    d.shot("sfx-01-after")

def main():
    run_test("props", test_props_browser)
    run_test("sfx", test_sfx_browser)
    # 3D Text (cidNewSpletter) currently crashes inside BRender's
    # BrMatrix34ApplyBounds on x64 -- separate from the kauai/engine layout
    # bugs we're tracking here. Filed as BRender x64 enablement work.

if __name__ == "__main__":
    main()
