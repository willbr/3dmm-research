"""repro-add-all.py -- drive: add scene, actor, 3D text, prop, sound effect.

Sequence (each step polls list_dialogs and dismisses asserts so we can
collect every distinct x64 bug in one run):

  1. Click Scene Choices (kidSettingsBrowser 0x20042) -> scene browser opens.
  2. Click first scene thumbnail -> scene added to movie.
  3. Click Actors (kidActorsBrowser 0x20075) -> actor browser opens.
  4. Click first actor thumbnail -> actor added.
  5. Click 3D Text (kidActorsSpletters 0x20077) -> 3D text easel.
  6. (close easel via Escape so we can move on)
  7. Click Props (kidActorsPropBrowser 0x20076) -> prop browser opens.
  8. Click first prop thumbnail -> prop added.
  9. Click Sound Effects (kidSoundsEfxBrowser 0x20078) -> sfx browser.
 10. Click first sfx thumbnail -> sound added.
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
            out = Path(tempfile.gettempdir())/"3dmm-mcp"/f"all-{label}.png"
            out.parent.mkdir(exist_ok=True); out.write_bytes(data)
            p(f"  shot: {out.name}")
        except Exception as e: p(f"  shot err: {e}")
    def dialogs(label):
        d = json.loads(text(call("list_dialogs")))
        if d.get("count", 0):
            p(f"  === DIALOG @ {label} ===")
            for dlg in d["dialogs"]:
                title = dlg.get("title","?")
                kids = dlg.get("children", [])
                p(f"    title: {title}")
                for kid in kids:
                    p(f"    kid: {kid[:200]}")
                p(f"    [dismiss ignore on {dlg['hwnd']}]")
                call("dismiss_dialog", {"hwnd": dlg["hwnd"], "button_text":"ignore"})
            return True
        return False
    def click_gob(hid, label, wait_target=None, wait_mode="visible", wait_timeout_ms=4000, click_mode="positioned"):
        # Use find_gob (single shot) instead of wait_for_gob here. The pre-
        # click polling pump cycle interferes with foreground state and the
        # subsequent SendInput goes nowhere -- empirically the browser only
        # opens when find_gob is called once before the click, not repeatedly.
        g = json.loads(text(call("find_gob", {"hid": hid})))
        if not g.get("found") or g.get("w",0) == 0:
            p(f"  gob {hex(hid)} NOT READY ({label}): {g}"); return False
        cx, cy = g["center_x"], g["center_y"]
        p(f"  click {label} hid={hex(hid)} center=({cx},{cy})")
        call("click", {"x": cx, "y": cy, "button":"left"}, timeout=4.0)
        # Catch modals (worker-direct: works even while the GUI thread is
        # in a modal MessageBox).
        for _ in range(4):
            d = json.loads(text(call("list_dialogs", timeout=3.0)))
            if d.get("count", 0):
                p(f"  === DIALOG @ click({label}) ===")
                for dlg in d["dialogs"]:
                    p(f"    title: {dlg.get('title','?')}")
                    for kid in dlg.get("children", []):
                        p(f"    kid: {kid[:200]}")
                    p(f"    [dismiss ignore on {dlg['hwnd']}]")
                    call("dismiss_dialog", {"hwnd": dlg["hwnd"], "button_text":"ignore"}, timeout=3.0)
                time.sleep(0.5)
            else:
                break
        if wait_target is not None:
            # Give the studio a chunk of wallclock time to process the click's
            # downstream commands before our Drain-bound wait_for_gob starts
            # blocking the main loop.
            time.sleep(2.0)
            r = json.loads(text(call("wait_for_gob",
                {"hid": wait_target, "timeout_ms": wait_timeout_ms, "mode": wait_mode})))
            p(f"  wait_for_gob {hex(wait_target)} mode={wait_mode}: "
              f"found={r.get('found',False)} visible={r.get('visible',False)} "
              f"waited={r.get('waited_ms',0)}ms")
        else:
            time.sleep(0.8)
        return True
    def key(vk):
        call("key", {"vk": vk})
        call("wait_ms", {"ms": 800})

    p("[wait 3s]"); time.sleep(3)
    send("initialize", {"protocolVersion":"2024-11-05","capabilities":{},
                        "clientInfo":{"name":"all","version":"0"}})
    send("notifications/initialized", notification=True)
    dialogs("startup")
    shot("00-start")

    # Open the scene browser, then wait for the first browser frame to be
    # laid out before trying to click it.
    # Wait targets: kidBrwsBackground=0x2003E (visible when scene browser
    # opens). After thumbnail pick the browser releases itself; we then wait
    # for the actors button to be visible again (it always is, but waiting
    # gives the browser teardown a beat to finish).
    p("\n[1] Scene Choices")
    if not click_gob(0x20042, "scene-choices", wait_target=0x2003E, wait_mode="visible"): return
    dialogs("after-scene-choices"); shot("01-scene-browser")

    p("\n[2] pick first scene thumbnail")
    # Browser frame uses positioned mode -- rcCur is set after MoveAbsGob
    # but rcVis only updates on next paint.
    if not click_gob(0x21120, "scene-thumb-0", click_mode="positioned",
                     wait_target=0x20075, wait_mode="visible", wait_timeout_ms=6000): return
    dialogs("after-scene-thumb"); shot("02-after-scene-pick")

    p("\n[3] Actors")
    if not click_gob(0x20075, "actors", wait_target=0x2003F, wait_mode="visible"): return
    dialogs("after-actors"); shot("03-actor-browser")

    p("\n[4] pick first actor thumbnail")
    if not click_gob(0x21120, "actor-thumb-0", click_mode="positioned"): return
    dialogs("after-actor-thumb"); shot("04-after-actor-pick")

    p("\n[5] 3D Text (Spletters)")
    if not click_gob(0x20077, "spletters"): return
    dialogs("after-spletters"); shot("05-spletters")

    p("\n[6] press Escape to close easel")
    key(0x1B)  # VK_ESCAPE
    dialogs("after-escape"); shot("06-after-escape")

    p("\n[7] Props")
    if not click_gob(0x20076, "props", wait_target=0x20040, wait_mode="visible"): return
    dialogs("after-props"); shot("07-prop-browser")

    p("\n[8] pick first prop thumbnail")
    if not click_gob(0x21120, "prop-thumb-0", click_mode="positioned"): return
    dialogs("after-prop-thumb"); shot("08-after-prop-pick")

    p("\n[9] Sound Effects")
    if not click_gob(0x20078, "sfx", wait_target=0x20044, wait_mode="visible"): return
    dialogs("after-sfx"); shot("09-sfx-browser")

    p("\n[10] pick first sfx thumbnail")
    if not click_gob(0x21120, "sfx-thumb-0", click_mode="positioned"): return
    dialogs("after-sfx-thumb"); shot("10-after-sfx-pick")

    p("\n[done; killing]")
    try: proc.kill()
    except Exception: pass

if __name__ == "__main__":
    main()
