"""Interactive smoke check of the original English Studio through SDL.

Uses a fresh movie; never opens or saves a user document. --portfolio-files
saves and reopens only fixture movies in the artifact directory. Requires a
Debug STUDIO_SDL3 build (MCP enabled) and the original English content directory.
Desktop focus/minimize/resize checks are Windows-specific; the fullscreen,
editing, SDL dialog and save/reopen workflow also runs on Mac/Linux.
"""
import argparse
import base64
import json
import os
import pathlib
import queue
import subprocess
import threading
import time


def studio_window(pid):
    """Find only this test process's SDL-owned native window."""
    import ctypes
    from ctypes import wintypes
    user32 = ctypes.WinDLL("user32", use_last_error=True)
    callback_type = ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HWND, wintypes.LPARAM)
    user32.EnumWindows.argtypes = [callback_type, wintypes.LPARAM]
    user32.GetWindowThreadProcessId.argtypes = [wintypes.HWND, ctypes.POINTER(wintypes.DWORD)]
    user32.GetClassNameW.argtypes = [wintypes.HWND, wintypes.LPWSTR, ctypes.c_int]
    user32.SetWindowPos.argtypes = [wintypes.HWND, wintypes.HWND, ctypes.c_int, ctypes.c_int,
                                  ctypes.c_int, ctypes.c_int, wintypes.UINT]
    matches = []

    @callback_type
    def visit(hwnd, _):
        owner = wintypes.DWORD()
        user32.GetWindowThreadProcessId(hwnd, ctypes.byref(owner))
        name = ctypes.create_unicode_buffer(128)
        user32.GetClassNameW(hwnd, name, len(name))
        if owner.value == pid and name.value == "SDL_app":
            matches.append(hwnd)
        return True

    user32.EnumWindows(visit, 0)
    if len(matches) != 1:
        raise AssertionError("Could not find this test's SDL-owned Studio window")
    return matches[0], user32


def resize_studio(pid):
    hwnd, user32 = studio_window(pid)
    if not user32.SetWindowPos(hwnd, None, 0, 0, 1000, 700, 0x16):
        raise AssertionError("Could not resize this test's SDL-owned Studio window")


def focus_studio(pid):
    """Capture checks require foreground input; Windows may deny it on launch."""
    if os.name != "nt":
        return
    import ctypes
    from ctypes import wintypes
    hwnd, user32 = studio_window(pid)
    user32.GetForegroundWindow.restype = wintypes.HWND
    user32.GetWindowThreadProcessId.argtypes = [wintypes.HWND, ctypes.POINTER(wintypes.DWORD)]
    user32.GetWindowThreadProcessId.restype = wintypes.DWORD
    user32.SetForegroundWindow.argtypes = [wintypes.HWND]
    user32.AttachThreadInput.argtypes = [wintypes.DWORD, wintypes.DWORD, wintypes.BOOL]
    foreground = user32.GetForegroundWindow()
    if foreground == hwnd:
        return
    target_thread = user32.GetWindowThreadProcessId(hwnd, None)
    foreground_thread = user32.GetWindowThreadProcessId(foreground, None) if foreground else 0
    current_thread = ctypes.WinDLL("kernel32").GetCurrentThreadId()
    attached = []
    for thread in {foreground_thread, target_thread} - {0, current_thread}:
        if user32.AttachThreadInput(current_thread, thread, True):
            attached.append(thread)
    try:
        user32.BringWindowToTop(hwnd)
        user32.SetForegroundWindow(hwnd)
    finally:
        for thread in reversed(attached):
            user32.AttachThreadInput(current_thread, thread, False)
    if user32.GetForegroundWindow() != hwnd:
        raise AssertionError("Could not activate this test's Studio window for input/capture checks")


def keyboard_repeat_settings():
    """Observe desktop preferences without changing them."""
    if os.name != "nt":
        return None
    import ctypes
    user32 = ctypes.WinDLL("user32", use_last_error=True)
    values = []
    for action in (0x16, 0x0A):  # SPI_GETKEYBOARDDELAY / SPI_GETKEYBOARDSPEED.
        value = ctypes.c_uint()
        if not user32.SystemParametersInfoW(action, 0, ctypes.byref(value), 0):
            raise AssertionError("Could not read keyboard repeat preferences")
        values.append(value.value)
    return values


def check_integer_presentation(state):
    view = state["sdl_presentation"]
    scale = min(view["output_width"] // 640, view["output_height"] // 480)
    if scale < 1 or view["width"] != 640 * scale or view["height"] != 480 * scale:
        raise AssertionError("Studio presentation is not using whole-number pixel scaling: " + str(view))


def check_fullscreen_presentation(state):
    view = state["sdl_presentation"]
    scale = min(view["output_width"] / 640, view["output_height"] / 480)
    # SDL rounds the non-limiting dimension down to a physical pixel.
    if abs(view["width"] - 640 * scale) >= 1 or abs(view["height"] - 480 * scale) >= 1:
        raise AssertionError("Studio fullscreen canvas does not fit the display: " + str(view))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("exe", type=pathlib.Path)
    parser.add_argument("content", type=pathlib.Path)
    parser.add_argument("artifacts", type=pathlib.Path)
    parser.add_argument("--startup-timeout", type=float, default=60,
                        help="Startup response timeout in seconds, including native system font discovery")
    parser.add_argument("--resize", action="store_true", help="Exercise a wider, scaled and letterboxed window")
    parser.add_argument("--fullscreen", action="store_true", help="Exercise the original fullscreen/window mode controls")
    parser.add_argument("--baseline", type=pathlib.Path, help="Compare PNG captures with a previous run using the same encoder")
    parser.add_argument("--actor", action="store_true", help="Select and place an actor using the original Studio controls")
    parser.add_argument("--portable-fonts", action="store_true", help="Require FreeType glyph rendering with no GDI fallback")
    parser.add_argument("--sdl-cursors", action="store_true", help="Require original cursor resources created and selected through SDL")
    parser.add_argument("--portfolio", action="store_true", help="Open and cancel Studio's original portfolio, checking its custom preview controls")
    parser.add_argument("--portfolio-files", action="store_true", help="Save/reopen fresh artifact movies and verify overwrite decisions; requires --text")
    parser.add_argument("--portable-pixels", action="store_true", help="Require portable owned pixel storage during the Studio workflow")
    parser.add_argument("--sdl-audio", action="store_true", help="Require original-content WAVE sounds played through the SDL audio device")
    parser.add_argument("--sdl-midi", action="store_true", help="Preview original music through the SDL SoundFont backend")
    parser.add_argument("--sdl-video", action="store_true", help="Play an original project AVI through SDL video/audio and its original scripts")
    parser.add_argument("--sdl-recording", action="store_true", help="Exercise original recording easel using SDL's dummy capture/playback device")
    parser.add_argument("--text", action="store_true", help="Create a textbox, enter SDL text and use the original font picker")
    parser.add_argument("--playback", action="store_true", help="Extend the original timeline, then verify play, natural completion and stop")
    parser.add_argument("--portable-runtime", action="store_true", help="Require the build that excludes native GUI adapters")
    args = parser.parse_args()
    if os.name != "nt" and args.resize:
        parser.error("--resize currently uses Windows desktop APIs")
    if args.portfolio_files and not args.text:
        parser.error("--portfolio-files requires --text so the final save/discard check still has an edited movie")
    args.artifacts.mkdir(parents=True, exist_ok=True)
    responses = queue.Queue()
    logs = []
    process = subprocess.Popen(
        [str(args.exe.resolve()), "--mcp-server"], cwd=args.content.resolve(),
        stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
        text=True, encoding="utf-8",
        env={**os.environ, "KAUAI_TEST_FAIL_FAST": "1", **({"SDL_AUDIODRIVER": "dummy"} if args.sdl_recording else {})},
    )

    def read_responses():
        for line in process.stdout:
            try:
                responses.put(json.loads(line))
            except ValueError:
                logs.append(line)

    def read_errors():
        logs.extend(process.stderr.readlines())

    readers = [threading.Thread(target=read_responses, daemon=True),
               threading.Thread(target=read_errors, daemon=True)]
    for reader in readers:
        reader.start()
    sequence = 0

    def send(method, params):
        nonlocal sequence
        sequence += 1
        process.stdin.write(json.dumps({"jsonrpc": "2.0", "id": sequence,
                                       "method": method, "params": params}) + "\n")
        process.stdin.flush()
        deadline = time.monotonic() + (args.startup_timeout if method == "initialize" else 20)
        while time.monotonic() < deadline:
            try:
                response = responses.get(timeout=min(0.2, max(0.01, deadline - time.monotonic())))
            except queue.Empty:
                if process.poll() is not None:
                    raise RuntimeError("Studio exited with code " + str(process.returncode) + ": " + "".join(logs))
                continue
            if response.get("id") == sequence:
                if "error" in response:
                    raise RuntimeError(response["error"])
                return response["result"]
        raise TimeoutError(method)

    def call(name, **arguments):
        result = send("tools/call", {"name": name, "arguments": arguments})
        if result.get("isError"):
            raise RuntimeError(result)
        return result

    def settle():
        call("wait_ms", ms=500)

    def capture(name):
        result = call("screenshot")
        data = base64.b64decode(result["content"][0]["data"], validate=True)
        if data[:8] != b"\x89PNG\r\n\x1a\n" or data[16:24] != (640).to_bytes(4, "big") + (480).to_bytes(4, "big"):
            raise AssertionError("Expected the original 640x480 Studio canvas")
        (args.artifacts / (name + ".png")).write_bytes(data)
        if args.baseline and (args.baseline / (name + ".png")).read_bytes() != data:
            raise AssertionError("Studio capture changed from baseline: " + name)
        return data

    def check_portfolio_files(prefix="portfolio"):
        import shutil
        target = (args.artifacts / (prefix + "-roundtrip.other.3mm")).resolve()
        reopened = (args.artifacts / (prefix + "-reopened.3mm")).resolve()
        target.unlink(missing_ok=True)
        reopened.unlink(missing_ok=True)
        deadline = time.monotonic() + 35

        def wait_for(predicate, message):
            while True:
                result = predicate()
                if result:
                    return result
                if time.monotonic() >= deadline:
                    raise AssertionError(message)
                time.sleep(0.03)

        def portfolio():
            state = json.loads(call("get_sdl_dialog")["content"][0]["text"])
            return state if state["active"] and state["ready"] and any(c["id"] == 20 for c in state["controls"]) else None

        def action(state, kind, control=0, text=""):
            call("sdl_dialog_action", generation=state["generation"], action=kind, control=control, text=text)

        def enter_name(state):
            # Keep a non-default suffix; Studio must append .3mm to it.
            value = str(target)[:-4]
            action(state, "text", 11, value)
            wait_for(lambda: any(c["id"] == 11 and c["text"] == value for c in portfolio()["controls"]), "Save name did not update")
            action(state, "click", 1)

        call("send_command", cid=104)
        enter_name(wait_for(portfolio, "SDL Save As did not open"))
        wait_for(lambda: target.exists() and target.stat().st_size > 128, "Studio did not write its fixture movie")
        settle()
        original = target.read_bytes()
        call("send_command", cid=104)
        enter_name(wait_for(portfolio, "SDL Save As did not reopen"))
        wait_for(lambda: json.loads(call("find_gob", hid=0x2000016D)["content"][0]["text"])["found"], "Original overwrite topic did not appear")
        call("send_command", cid=100009, lw0=2)  # Original modal button 2: No.
        state = wait_for(portfolio, "Declining overwrite did not restore the SDL portfolio")
        if target.read_bytes() != original:
            raise AssertionError("Declining overwrite modified the movie")
        enter_name(state)
        wait_for(lambda: json.loads(call("find_gob", hid=0x2000016D)["content"][0]["text"])["found"], "Original overwrite topic did not reappear")
        call("send_command", cid=100009, lw0=1)  # Original modal button 1: Yes.
        settle()
        wait_for(lambda: not json.loads(call("get_sdl_dialog")["content"][0]["text"])["active"], "Confirmed save did not close")
        shutil.copyfile(target, reopened)
        call("send_command", cid=101, hid=20005)
        state = wait_for(portfolio, "SDL Open did not open")
        folder = str(reopened.parent)
        action(state, "text", 10, folder)
        wait_for(lambda: any(c["id"] == 10 and c["text"] == folder for c in portfolio()["controls"]), "Open folder did not update")
        action(state, "enter")

        def find_file():
            current = portfolio()
            if current:
                for control in current["controls"]:
                    if control["id"] >= 1000 and control["label"] == reopened.name:
                        return current, control["id"]
            return None

        state, control = wait_for(find_file, "SDL file list did not show the saved fixture")
        action(state, "click", control)
        state = wait_for(lambda: (current if any(c["id"] == 21 and c["number"] == 1 for c in current["controls"]) else None) if (current := portfolio()) else None, "Saved movie thumbnail did not render")
        image = (args.artifacts / (prefix + "-preview.bmp")).resolve()
        image.unlink(missing_ok=True)
        action(state, "screenshot", text=str(image))
        wait_for(image.exists, "SDL movie preview screenshot was not saved")
        before_open = reopened.read_bytes()
        action(state, "click", 1)
        wait_for(lambda: not json.loads(call("get_sdl_dialog")["content"][0]["text"])["active"], "SDL Open did not accept the fixture")
        settle()
        if reopened.read_bytes() != before_open:
            raise AssertionError("Opening the fixture modified its bytes")
        capture(prefix + "-reopened")
    def check_portfolio(cid, hid, name):
        # Exercise the real Studio EnsureInteractive path before each portfolio.
        # Only this test's fresh process is minimized through the desktop.
        if os.name == "nt":
            import ctypes
            from ctypes import wintypes
            hwnd, user32 = studio_window(process.pid)
            user32.ShowWindow.argtypes = [wintypes.HWND, ctypes.c_int]
            user32.IsIconic.argtypes = [wintypes.HWND]
            user32.ShowWindow(hwnd, 6)  # SW_MINIMIZE
            deadline = time.monotonic() + 5
            while not user32.IsIconic(hwnd):
                if time.monotonic() >= deadline:
                    raise AssertionError("Could not minimize this test's Studio window")
                time.sleep(0.03)
        call("send_command", cid=cid, hid=hid)
        deadline = time.monotonic() + 10
        while True:
            portfolio = json.loads(call("get_sdl_dialog")["content"][0]["text"])
            controls = {item["id"]: item for item in portfolio["controls"]}
            if portfolio["active"] and portfolio["ready"] and {1, 2, 3, 4, 10, 11, 12, 20, 21}.issubset(controls):
                break
            if time.monotonic() >= deadline:
                raise AssertionError("SDL portfolio controls did not appear: " + str(portfolio))
            time.sleep(0.03)
        if not controls[20]["number"]:
            raise AssertionError("SDL portfolio did not load its original artwork")
        if os.name == "nt" and user32.IsIconic(hwnd):
            raise AssertionError("Opening the SDL portfolio did not restore Studio")
        (args.artifacts / (name + "-dialog.json")).write_text(json.dumps(portfolio, indent=2), encoding="utf-8")
        image = (args.artifacts / (name + ".bmp")).resolve()
        image.unlink(missing_ok=True)
        call("sdl_dialog_action", generation=portfolio["generation"], action="screenshot", text=str(image))
        while not image.exists():
            if time.monotonic() >= deadline:
                raise AssertionError("SDL portfolio screenshot was not saved")
            time.sleep(0.03)
        call("sdl_dialog_action", generation=portfolio["generation"], action="click", control=2)
        while json.loads(call("get_sdl_dialog")["content"][0]["text"])["active"]:
            if time.monotonic() >= deadline:
                raise AssertionError("SDL portfolio did not cancel")
            time.sleep(0.03)
        settle()
        capture(name + "-cancelled")
    try:
        send("initialize", {"protocolVersion": "2024-11-05", "capabilities": {},
                            "clientInfo": {"name": "studio-sdl-smoke", "version": "1"}})
        settle()
        focus_studio(process.pid)
        settle()
        if args.fullscreen:
            desktop_size = None
            if os.name == "nt":
                import ctypes
                user32 = ctypes.WinDLL("user32")
                desktop_size = (user32.GetSystemMetrics(0), user32.GetSystemMetrics(1))
            sizes = []
            previous_state = json.loads(call("get_state")["content"][0]["text"])
            for fullscreen, integer_scaling in ((True, False), (True, True), (False, False)):
                call("send_command", cid=40038)  # Original Product Info command.
                deadline = time.monotonic() + 10
                while True:
                    dialog = json.loads(call("get_sdl_dialog")["content"][0]["text"])
                    if dialog["active"] and dialog["ready"] and dialog["title"] == "Product Info":
                        break
                    if time.monotonic() >= deadline:
                        raise AssertionError("Original Product Info did not open through SDL/ImGui")
                    time.sleep(0.03)
                def dialog_action(action, control=0, text=""):
                    call("sdl_dialog_action", generation=dialog["generation"], action=action, control=control, text=text)
                controls = {item["id"]: item for item in dialog["controls"]}
                if controls[1010]["number"]:
                    dialog_action("click", 1010)  # Keep persistent defaults unchecked.
                if bool(controls[1029]["number"]) != integer_scaling:
                    dialog_action("click", 1029)
                dialog_action("click", 1015 if fullscreen else 1016)
                expected = 0 if fullscreen else 1
                while True:
                    observed = json.loads(call("get_sdl_dialog")["content"][0]["text"])
                    values = {item["id"]: item for item in observed["controls"]}
                    if (values[1015]["number"] == expected and values[1010]["number"] == 0
                            and bool(values[1029]["number"]) == integer_scaling):
                        break
                    if time.monotonic() >= deadline:
                        raise AssertionError("SDL dialog controls did not update")
                    time.sleep(0.03)
                mode = "fullscreen" if fullscreen else "windowed"
                if integer_scaling:
                    mode += "-integer"
                (args.artifacts / ("product-info-" + mode + ".json")).write_text(json.dumps(observed, indent=2), encoding="utf-8")
                image = (args.artifacts / ("product-info-" + mode + ".bmp")).resolve()
                image.unlink(missing_ok=True)
                dialog_action("screenshot", text=str(image))
                while not image.exists():
                    if time.monotonic() >= deadline:
                        raise AssertionError("SDL dialog screenshot was not saved")
                    time.sleep(0.03)
                dialog_action("click", 2)  # The original OK control has IDCANCEL.
                while json.loads(call("get_sdl_dialog")["content"][0]["text"])["active"]:
                    if time.monotonic() >= deadline:
                        raise AssertionError("SDL Product Info did not close")
                    time.sleep(0.03)
                deadline = time.monotonic() + 10
                while True:
                    mode_state = json.loads(call("get_state")["content"][0]["text"])
                    changed_mode = previous_state["sdl_fullscreen"] != fullscreen
                    ready = (mode_state["sdl_fullscreen"] == fullscreen
                             and mode_state["sdl_integer_scaling"] == integer_scaling
                             and (not changed_mode or mode_state["client_size"] != previous_state["client_size"]))
                    if ready:
                        if fullscreen and not integer_scaling:
                            check_fullscreen_presentation(mode_state)
                        else:
                            check_integer_presentation(mode_state)
                        break
                    if time.monotonic() >= deadline:
                        raise AssertionError("SDL window transition did not finish: " + str(mode_state))
                    time.sleep(0.05)
                sizes.append({"fullscreen": fullscreen, "integer_scaling": integer_scaling,
                              "client_size": mode_state["client_size"], "presentation": mode_state["sdl_presentation"]})
                previous_state = mode_state
                if desktop_size is not None and desktop_size != (user32.GetSystemMetrics(0), user32.GetSystemMetrics(1)):
                    raise AssertionError("Studio changed the desktop resolution")
            if sizes[0]["client_size"] == sizes[-1]["client_size"]:
                raise AssertionError("Studio did not change between fullscreen and window mode")
            (args.artifacts / "window-modes.json").write_text(json.dumps(sizes, indent=2), encoding="utf-8")
        if args.resize:
            resize_studio(process.pid)
            settle()
            state = json.loads(call("get_state")["content"][0]["text"])
            if state["client_size"]["w"] <= 640:
                raise AssertionError("Studio rejected the SDL window resize")
            check_integer_presentation(state)
        before = capture("studio")
        if args.portfolio:
            check_portfolio(101, 20005, "portfolio-open")
        call("click", x=48, y=21)  # Original scene browser control.
        settle()
        browser = capture("scene-browser")
        info = json.loads(call("find_gob", hid=0x2003E)["content"][0]["text"])
        if not info["found"] or browser == before:
            raise AssertionError("SDL click did not open the original scene browser")
        call("click", x=170, y=130)  # First original-content background.
        settle()
        scene = capture("scene")
        if args.portfolio:
            check_portfolio(104, 0, "portfolio-save")
        if args.portfolio_files:
            check_portfolio_files()
        def click_control(hid, settle_after=True):
            info = json.loads(call("find_gob", hid=hid)["content"][0]["text"])
            if not info["found"] or info["w"] <= 0 or info["h"] <= 0:
                raise AssertionError("Original Studio control is missing: " + hex(hid))
            call("click", x=info["center_x"], y=info["center_y"])
            if settle_after:
                settle()

        if args.sdl_video:
            # Invoke Studio's original Movie Techniques entry script via its
            # existing alarm command, then use the original on-screen controls.
            call("send_command", cid=100005, hid=0x20002, lw2=0x10024)
            settle()
            time.sleep(2)
            (args.artifacts / "video-intro-state.json").write_text(json.dumps({"state": call("get_state"), "intro": call("find_gob", hid=0x30603)}, indent=2), encoding="utf-8")
            capture("video-intro")
            intro = json.loads(call("find_gob", hid=0x30603)["content"][0]["text"])
            call("click", x=intro["center_x"], y=intro["y"] + intro["h"] - 20)
            settle()
            time.sleep(1)
            capture("video-project")
            click_control(0x3033, False)  # Let any media prompt enter the normal command loop.
            time.sleep(1)
            capture("video-started")
            (args.artifacts / "video-start-state.json").write_text(json.dumps(call("get_state"), indent=2), encoding="utf-8")
            deadline = time.monotonic() + 35
            while True:
                video = json.loads(call("get_state")["content"][0]["text"])
                if video.get("sdl_video_presented", 0) and video.get("sdl_video_audible_frames", 0):
                    break
                if time.monotonic() >= deadline:
                    raise AssertionError("Original AVI did not draw and produce sound through SDL: " + str(video))
                time.sleep(0.05)
            first_video = capture("video-playing")
            time.sleep(1)
            if capture("video-advanced") == first_video:
                raise AssertionError("Original AVI did not advance its visible frames")
            while video.get("sdl_video_completed", 0) == 0:
                if time.monotonic() >= deadline:
                    raise AssertionError("Original AVI did not complete through its project script: " + str(video))
                time.sleep(0.1)
                video = json.loads(call("get_state")["content"][0]["text"])
            (args.artifacts / "video-state.json").write_text(json.dumps(video, indent=2), encoding="utf-8")
            capture("video-completed")
            # Same script as the original 'work on my own' confirmation choice.
            call("send_command", cid=100005, hid=0x3036, lw2=0x21)
            settle()
            time.sleep(1)
            capture("video-closed")
            if json.loads(call("find_gob", hid=0x3031)["content"][0]["text"])["found"]:
                raise AssertionError("Original project exit script did not close the gadget")
            farewell = json.loads(call("find_gob", hid=0x30603)["content"][0]["text"])
            if farewell["found"]:
                call("click", x=farewell["center_x"], y=farewell["y"] + farewell["h"] - 20)
                settle()
            focus_studio(process.pid)

        if args.sdl_midi:
            click_control(0x20007)  # Original Sounds tab.
            click_control(0x2007A)  # Original Music browser.
            capture("music-browser")
            before_midi = json.loads(call("get_state")["content"][0]["text"])
            click_control(0x21121)  # "funny moment 1"; the first entry is the silent "sound stopper".
            deadline = time.monotonic() + 10
            while True:
                midi = json.loads(call("get_state")["content"][0]["text"])
                if (midi.get("sdl_midi_notes", 0) > before_midi.get("sdl_midi_notes", 0)
                        and midi.get("sdl_midi_audible_frames", 0) > before_midi.get("sdl_midi_audible_frames", 0)):
                    break
                if time.monotonic() >= deadline:
                    raise AssertionError("Original music preview did not render MIDI through SDL: " + str(midi))
                time.sleep(0.05)
            if midi.get("midi_provider") != "sdl-soundfont":
                raise AssertionError("Original music preview used a native MIDI backend: " + str(midi))
            (args.artifacts / "midi-state.json").write_text(json.dumps(midi, indent=2), encoding="utf-8")
            capture("music-preview")
            click_control(0x21013)  # Cancel preview; do not add music to the test movie.
            stopped_midi = json.loads(call("get_state")["content"][0]["text"])
            if stopped_midi.get("sdl_midi_completed", 0) <= before_midi.get("sdl_midi_completed", 0):
                raise AssertionError("Closing the music browser did not return its MIDI buffers: " + str(stopped_midi))

        if args.sdl_recording:
            click_control(0x20007)  # Original Sounds tab.
            click_control(0x20079)  # Original Speech browser.
            click_control(0x21015)  # Original browser Record button.
            capture("recording-easel")
            click_control(0x2015B)  # Sound name edit control.
            call("type_text", text="SDL recording regression")
            settle()
            click_control(0x2015C)  # Record.
            time.sleep(0.3)
            click_control(0x2015C)  # Stop recording.
            click_control(0x2015D)  # Preview through SDL.
            time.sleep(0.5)
            settle()
            recorded = json.loads(call("get_state")["content"][0]["text"])
            if not all(recorded.get(name, 0) > 0 for name in (
                    "sdl_recording_opened", "sdl_recording_started", "sdl_recording_completed",
                    "sdl_recording_previewed", "sdl_recording_frames")):
                raise AssertionError("Original recorder did not capture and preview through SDL: " + str(recorded))
            (args.artifacts / "recording-state.json").write_text(json.dumps(recorded, indent=2), encoding="utf-8")
            capture("recording-preview")
            click_control(0x2015F)  # Original OK saves WAVE and imports MSND into this fresh test movie.
            if json.loads(call("find_gob", hid=0x20158)["content"][0]["text"])["found"]:
                raise AssertionError("Original recording easel did not accept the captured sound")
            capture("recording-saved")
            click_control(0x21013)  # Close the speech browser after accepting the take.

        if args.actor:
            if json.loads(call("get_state")["content"][0]["text"]).get("scene_actor_count", 0):
                raise AssertionError("Fresh test scene unexpectedly contains actors")
            keyboard_settings = keyboard_repeat_settings()

            call("click", x=205, y=21)  # Original Actors & Props tab.
            settle()
            capture("actor-panel")
            click_control(0x20075)  # Original Actors browser button.
            capture("actor-browser")
            browser_info = json.loads(call("find_gob", hid=0x2003F)["content"][0]["text"])
            if not browser_info["found"]:
                raise AssertionError("Original actor browser did not open")
            click_control(0x21120)  # First actor thumbnail.
            actor_input = json.loads(call("get_state")["content"][0]["text"])
            (args.artifacts / "actor-input-state.json").write_text(json.dumps(actor_input, indent=2), encoding="utf-8")
            if not all(actor_input[name] for name in ("sdl_capture_requested", "sdl_confinement_requested", "sdl_slow_key_repeat")):
                raise AssertionError("Actor placement did not request SDL tracking/confinement and local repeat")
            if not actor_input["sdl_capture_active"] or not actor_input["sdl_confinement_active"]:
                raise AssertionError("SDL did not capture and confine the pointer for actor placement")
            if keyboard_repeat_settings() != keyboard_settings:
                raise AssertionError("SDL actor placement changed desktop keyboard repeat preferences")
            call("click", x=300, y=300)  # Place the selected actor on the stage.
            settle()
            capture("actor-placed")
            if json.loads(call("get_state")["content"][0]["text"]).get("scene_actor_count", 0) == 0:
                state = json.loads(call("get_state")["content"][0]["text"])
                (args.artifacts / "actor-failure-state.json").write_text(json.dumps(state, indent=2), encoding="utf-8")
                raise AssertionError("SDL placement did not add an actor to the movie")
            actor_input = json.loads(call("get_state")["content"][0]["text"])
            if any(actor_input[name] for name in ("sdl_capture_requested", "sdl_confinement_requested", "sdl_slow_key_repeat")):
                raise AssertionError("Actor placement left SDL tracking or slow repeat enabled")
        if args.text:
            click_control(0x20008)  # Original Words tab.
            capture("text-panel")
            click_control(0x20046)  # Original Create Text control.
            click_control(0x2011E)  # Original story-box choice.
            call("click", x=300, y=240)  # Place the floating textbox before editing.
            settle()
            call("type_text", text="SDL3 text")
            settle()
            capture("text-entered")
            typed = json.loads(call("get_state")["content"][0]["text"])["selected_textbox"]
            if not typed["present"] or not typed.get("text", "").startswith("SDL3 text"):
                raise AssertionError("SDL text did not reach the selected movie textbox: " + str(typed))
            click_control(0x2002F)  # Original font picker.
            capture("font-menu")
            picker = json.loads(call("find_gob", hid=0x21541)["content"][0]["text"])
            if not picker["found"]:
                raise AssertionError("Original Studio font picker did not open")
            frames = [json.loads(call("find_gob", hid=hid)["content"][0]["text"]) for hid in range(0x21545, 0x21550)]
            (args.artifacts / "font-frames.json").write_text(json.dumps(frames, indent=2), encoding="utf-8")
            click_control(0x21546)  # Next family after the current font.
            call("type_text", text="!")  # Commit the new insertion font into the document.
            settle()
            capture("font-selected")
            font_state = json.loads(call("get_state")["content"][0]["text"])
            (args.artifacts / "font-state.json").write_text(json.dumps(font_state, indent=2), encoding="utf-8")
            changed = font_state["selected_textbox"]
            if (not changed["present"] or changed.get("last_character_font") == typed.get("font")
                    or not changed.get("text", "").startswith("SDL3 text!")):
                raise AssertionError("Original font picker did not change the textbox font: " + str(changed))

        def movie_state():
            return json.loads(call("get_state")["content"][0]["text"])

        def add_frame():
            # Original Ctrl+frame-forward command sequence, including release.
            call("send_command", cid=50045, lw0=22, lw1=1)
            call("send_command", cid=50021, lw0=22)

        if args.playback:
            click_control(0x20086)  # Last frame.
            original_last = movie_state()["scene_last_frame"]
            for _ in range(30):
                add_frame()
                call("wait_ms", ms=20)
            settle()
            if movie_state()["scene_last_frame"] != original_last + 30:
                raise AssertionError("Original frame controls did not extend the scene")
            click_control(0x20088)  # First frame.
            first = movie_state()["scene_first_frame"]
            click_control(0x20039, False)  # Original Play button.
            deadline = time.monotonic() + 20
            observed = []
            while time.monotonic() < deadline:
                state = movie_state()
                observed.append({key: state[key] for key in ("movie_playing", "scene_frame")})
                if any(item["movie_playing"] and item["scene_frame"] > first for item in observed) and not state["movie_playing"]:
                    break
                time.sleep(0.04)
            else:
                raise AssertionError("Movie did not advance and finish through the SDL event loop: " + str(observed))
            (args.artifacts / "timeline-playback.json").write_text(json.dumps(observed, indent=2), encoding="utf-8")
            click_control(0x20088)
            click_control(0x20039, False)
            time.sleep(0.15)
            if not movie_state()["movie_playing"]:
                raise AssertionError("Movie did not restart")
            click_control(0x20039, False)  # Same button stops playback.
            # Stop completes on the next render alarm after the normal idle
            # loop runs. Nested wait_ms pumping does not provide that idle.
            deadline = time.monotonic() + 2
            while movie_state()["movie_playing"]:
                if time.monotonic() >= deadline:
                    capture("timeline-stop-failed")
                    raise AssertionError("Original Play/Stop control did not stop playback")
                time.sleep(0.04)
            if movie_state()["scene_frame"] >= movie_state()["scene_last_frame"]:
                raise AssertionError("Stop test reached the end instead of stopping early")
            click_control(0x20088)
            capture("timeline-stopped")

        if args.portfolio_files:
            before_save = movie_state()
            check_portfolio_files("edited-movie")
            after_open = movie_state()
            for field in ("movie_scene_count", "scene_actor_count", "scene_textboxes", "scene_first_frame", "scene_last_frame"):
                if before_save[field] != after_open[field]:
                    raise AssertionError("Edited movie did not round-trip " + field + ": " + str((before_save[field], after_open[field])))
            (args.artifacts / "edited-roundtrip.json").write_text(json.dumps({"before": before_save, "after": after_open}, indent=2), encoding="utf-8")
            # Leave a deliberate unsaved edit for the original exit/save query.
            click_control(0x20086)
            add_frame()
            settle()

        call("key", vk=112)  # F1 must reach Studio's existing help binding.
        settle()
        help_book = capture("help")
        if scene == browser or help_book == scene:
            raise AssertionError("Scene selection or F1 did not update Studio")
        call("click", x=475, y=425)  # Original help book close button.
        settle()
        state = json.loads(call("get_state")["content"][0]["text"])
        if state.get("error_stack_depth", 0) or state["dialog"]["present"]:
            raise AssertionError("Studio reported an error or unexpected native dialog")
        if args.sdl_audio and (not state.get("sdl_audio_device") or not state.get("sdl_audio_decoded")
                               or not state.get("sdl_audio_started") or not state.get("sdl_audio_completed")):
            raise AssertionError("Original-content WAVE sounds did not complete through SDL audio: " + str(state))
        call("quit")
        # Let Studio's normal command loop enter the save modal. Pumping its
        # quit command inside wait_ms nests the modal under that MCP request,
        # which cannot finish until a subsequent request dismisses the query.
        deadline = time.monotonic() + 10
        while True:
            query = json.loads(call("find_gob", hid=0x20000163)["content"][0]["text"])
            if query["found"]:
                break
            if time.monotonic() >= deadline:
                raise AssertionError("Studio did not open its save query")
            time.sleep(0.05)
        capture("save-query")
        state = json.loads(call("get_state")["content"][0]["text"])
        (args.artifacts / "state.json").write_text(json.dumps(state, indent=2), encoding="utf-8")
        if state.get("error_stack_depth") != 0:
            raise AssertionError("Studio retained errors after the editing workflow: " + str(state))
        if args.portable_runtime and state.get("portable_runtime") is not True:
            raise AssertionError("This workflow requires STUDIO_PORTABLE_RUNTIME=ON")
        if args.sdl_cursors and (state.get("sdl_cursors_created", 0) == 0 or state.get("sdl_cursors_selected", 0) == 0):
            raise AssertionError("Studio did not create and select its original cursor resources through SDL: " + str(state))
        if args.portable_pixels and (state.get("portable_pixel_buffers", 0) == 0 or state.get("portable_pixel_bytes", 0) < 640 * 480 * 4):
            raise AssertionError("Studio did not retain its framebuffer in portable pixel storage: " + str(state))
        if args.portable_pixels and state.get("sdl_native_framebuffer_mirror") is not False:
            raise AssertionError("The tested Studio workflow allocated a native framebuffer bitmap: " + str(state))
        if args.portable_pixels and state.get("sdl_native_framebuffer_context") is not False:
            raise AssertionError("The tested Studio workflow allocated a native framebuffer context: " + str(state))
        if args.portable_pixels and state.get("native_screen_context") is not False:
            raise AssertionError("The tested Studio workflow retained a native screen context: " + str(state))
        if args.portable_pixels and (
            state.get("indexed_pixel_ports_created", 0) == 0
            or state.get("native_indexed_contexts_created") != 0
            or state.get("native_indexed_mirrors_created") != 0
        ):
            raise AssertionError("The tested Studio indexed surfaces required native resources: " + str(state))
        native_font_counters = (
            "native_font_faces", "font_fallbacks", "native_font_draws",
            "native_font_measurements", "native_font_bootstraps",
            "native_font_enumerations",
        )
        if args.portable_fonts and (
            state.get("font_provider") != "freetype"
            or state.get("portable_font_faces", 0) == 0
            or state.get("portable_font_enumerations", 0) == 0
            or any(state.get(counter) != 0 for counter in native_font_counters)
        ):
            raise AssertionError("Studio did not render all tested fonts through FreeType: " + str(state))
        call("click", x=300, y=249)  # Discard this test's new unsaved movie.
        process.stdin.close()
        if process.wait(timeout=15) != 0:
            raise AssertionError("Studio did not exit normally")
        detail = ", actor selection/placement" if args.actor else ""
        if args.text:
            detail += ", textbox input/font picker"
        if args.playback:
            detail += ", timeline play/restart/stop"
        if args.portfolio_files:
            detail += ", edited movie save/reopen"
        print("Original Studio: SDL scene selection" + detail + ", F1 help and save/discard exit passed.")
    finally:
        if process.poll() is None:
            # This tool runs on the reader thread even if the UI is blocked in
            # a native assertion/dialog, so retain actionable failure evidence.
            try:
                diagnostic = call("list_dialogs" if os.name == "nt" else "get_sdl_dialog")
                (args.artifacts / "dialogs.json").write_text(json.dumps(diagnostic, indent=2), encoding="utf-8")
            except (OSError, queue.Empty, TimeoutError):
                pass
        if process.poll() is None:
            process.kill()  # Only the fresh test-owned process, on test failure.
            process.wait(timeout=5)
        for reader in readers:
            reader.join(timeout=2)
        (args.artifacts / "studio.log").write_text("".join(logs), encoding="utf-8")


if __name__ == "__main__":
    main()
