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
import json, subprocess, sys, threading, time, base64, queue, tempfile
from pathlib import Path

EXE = Path(r"C:\Users\wjbr\src\3DMMForever\dist-x64\3dmovie.exe")

def p(*a, **k): print(*a, flush=True, **k)

def main():
    proc = subprocess.Popen([str(EXE), "--mcp-server"],
        stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
        text=True, encoding="utf-8", bufsize=1)
    # stdout reader thread + queue. proc.stdout.readline() blocks even with a
    # timeout outside it, so any in-flight tool that wedges 3dmovie (e.g.
    # Tool_Click is queued for the GUI thread, GUI thread blocks on a kauai
    # assert dialog, no response ever lands on stdout) would hang the test.
    # Pump readlines into a queue so send() can wait with an actual deadline.
    out_q: "queue.Queue[str | None]" = queue.Queue()
    def _reader():
        try:
            for line in proc.stdout:
                out_q.put(line)
        finally:
            out_q.put(None)  # EOF sentinel
    threading.Thread(target=_reader, daemon=True).start()
    next_id = [1]
    def _abort_comms_dead(reason):
        # If 3dmovie's pipes break or replies dry up while a dialog might be
        # sitting modal on screen, killing 3dmovie also takes the dialog down.
        # Avoids the test "hanging" with a dialog visible after the script
        # has lost the ability to talk to the server.
        p(f"  *** comms with 3dmovie dead ({reason}) -- killing process and aborting ***")
        try: proc.kill()
        except Exception: pass
        sys.exit(2)
    def send(method, params=None, *, notification=False, timeout=8.0):
        msg = {"jsonrpc":"2.0","method":method}
        if not notification: msg["id"] = next_id[0]; next_id[0] += 1
        if params is not None: msg["params"] = params
        try:
            proc.stdin.write(json.dumps(msg) + "\n"); proc.stdin.flush()
        except (OSError, ValueError):
            _abort_comms_dead("stdin write failed")
        if notification: return None
        # Block on the reader queue with a real deadline. This is the
        # behaviour the original proc.stdout.readline() loop pretended to
        # have but didn't -- readline() is synchronous and ignores any
        # outer time budget.
        deadline = time.time() + timeout
        while True:
            remaining = deadline - time.time()
            if remaining <= 0:
                _abort_comms_dead(f"timeout waiting for {method}")
            try:
                line = out_q.get(timeout=remaining)
            except queue.Empty:
                _abort_comms_dead(f"timeout waiting for {method}")
            if line is None: _abort_comms_dead("stdout closed")
            try: obj = json.loads(line)
            except Exception: continue
            if obj.get("id") == msg["id"]: return obj
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
    def fatal_dialogs(d, label):
        # Centralised "we hit an error dialog" handler: log it, dismiss to
        # release any modal so the process can exit cleanly, then kill 3dmm
        # and abort the test run. The point of catching the dialog is to
        # capture the text + crash log entry; once we have that, sitting
        # around with the dialog up just wastes time.
        p(f"  === DIALOG @ {label} ===")
        for dlg in d["dialogs"]:
            p(f"    title: {dlg.get('title','?')}  hwnd={dlg['hwnd']}")
            for kid in dlg.get("children", []):
                p(f"    kid: {kid[:200]}")
            p(f"    [dismiss ignore on {dlg['hwnd']}]")
            call("dismiss_dialog", {"hwnd": dlg["hwnd"], "button_text":"ignore"}, timeout=2.0)
        time.sleep(0.5)
        p(f"  *** error dialog detected at {label} -- killing 3dmovie and aborting ***")
        try: proc.kill()
        except Exception: pass
        sys.exit(1)
    def dialogs(label):
        d = json.loads(text(call("list_dialogs")))
        if d.get("count", 0):
            fatal_dialogs(d, label)
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
        # Catch modals that pop up *after* the click. list_dialogs is
        # worker-thread-direct so it responds even when the GUI thread
        # is blocked in a modal. Anything more catastrophic (Tool_Click
        # itself wedged because the click triggered a kauai-modal that
        # is invisible to EnumWindows) is caught by the send() timeout
        # in _abort_comms_dead.
        for _ in range(4):
            d = json.loads(text(call("list_dialogs", timeout=2.0)))
            if d.get("count", 0):
                fatal_dialogs(d, f"click({label})")
            else:
                break
            time.sleep(0.25)
        if wait_target is not None:
            # Poll in 250ms slices so we can also intercept assertion dialogs
            # that pop up DURING the wait window (a long blocking wait_for_gob
            # would let the dialog sit there until it times out).
            time.sleep(0.5)
            slice_ms = 250; total_ms = 0; r = {"found": False}
            while total_ms < wait_timeout_ms:
                r = json.loads(text(call("wait_for_gob",
                    {"hid": wait_target, "timeout_ms": slice_ms, "mode": wait_mode})))
                total_ms += slice_ms
                if r.get("found") and (wait_mode != "visible" or r.get("visible")):
                    break
                d = json.loads(text(call("list_dialogs", timeout=2.0)))
                if d.get("count", 0):
                    fatal_dialogs(d, f"wait({label}) after {total_ms}ms")
            p(f"  wait_for_gob {hex(wait_target)} mode={wait_mode}: "
              f"found={r.get('found',False)} visible={r.get('visible',False)} "
              f"polled={total_ms}ms")
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

    # The Actors / Sounds / Texts / Settings buttons in the top tool bar are
    # *mode covers* (kid*Cover). Clicking a cover switches the lower toolbar
    # to that mode; only then does the corresponding picker button (kid*Browser)
    # become live. Forgot this before -- clicking kidActorsBrowser without the
    # mode switch hits dead air and the studio drifts into a bad state.
    p("\n[3a] Actors mode (cover tab)")
    if not click_gob(0x20006, "actors-cover"): return
    time.sleep(0.5); shot("03a-actors-mode")

    p("\n[3b] Open actor browser")
    if not click_gob(0x20075, "actors", wait_target=0x2003F, wait_mode="visible"): return
    dialogs("after-actors"); shot("03-actor-browser")
    if proc.poll() is not None:
        p(f"  *** process exited with code {proc.returncode} ***")
        return

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
