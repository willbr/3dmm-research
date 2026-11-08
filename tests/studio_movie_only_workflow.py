"""Exercise Shift+Play, Space pause/resume, captions and restoration using generated movies.

Uses original ISO assets and fresh, isolated Studio processes. By default SDL's
dummy driver makes this headless; --desktop also exercises the real display.
"""
import sys
sys.dont_write_bytecode = True
import argparse
import base64
import hashlib
import json
import os
import pathlib
import subprocess
import tempfile
import time
import uuid
from types import SimpleNamespace
from mcp_png_test import decode_png
from studio_movie_workflow import Studio


def remove_settings(run):
    if os.name != 'nt':
        return
    import winreg
    def remove(parent, name):
        try:
            with winreg.OpenKey(parent, name, 0, winreg.KEY_ALL_ACCESS | winreg.KEY_WOW64_32KEY) as key:
                while True:
                    try:
                        child = winreg.EnumKey(key, 0)
                    except OSError:
                        break
                    remove(key, child)
            winreg.DeleteKeyEx(parent, name, winreg.KEY_WOW64_32KEY, 0)
        except FileNotFoundError:
            pass
    remove(winreg.HKEY_CURRENT_USER, 'Software\\3DMMForever\\RetailOracle\\SDL-' + run)


def wait(studio, predicate, message, timeout=15):
    deadline = time.monotonic() + timeout
    while True:
        state = studio.state()
        if predicate(state):
            return state
        assert time.monotonic() < deadline, (message, state)
        time.sleep(.02)


def play(studio, shifted=False):
    button = json.loads(studio.call('find_gob', hid=0x20039)['content'][0]['text'])
    assert button['found'], button
    studio.call('click', x=button['center_x'], y=button['center_y'], shift=shifted)
    return wait(studio, lambda state: state['movie_playing'], 'Play did not start')


def render_scale(studio, scale, capture=None):
    studio.call('send_command', cid=40038)
    deadline = time.monotonic() + 10
    while True:
        dialog = json.loads(studio.call('get_sdl_dialog')['content'][0]['text'])
        if dialog['active'] and dialog['ready']:
            break
        assert time.monotonic() < deadline, 'Product Info did not open'
        time.sleep(.02)
    studio.call('sdl_dialog_action', generation=dialog['generation'], action='click',
                control={1: 1030, 2: 1031, 4: 1032}[scale])
    if capture:
        studio.call('sdl_dialog_action', generation=dialog['generation'], action='screenshot', text=str(capture))
    studio.call('sdl_dialog_action', generation=dialog['generation'], action='click', control=1)
    return wait(studio, lambda s: s['render_scale'] == scale and s.get('render_width') == 544 * scale and
                s.get('render_height') == 306 * scale and s['application_foreground'],
                'Render size or foreground activation did not apply')


def presentation(studio, path, scale):
    png = base64.b64decode(studio.call('screenshot', presentation=True)['content'][0]['data'])
    path.write_bytes(png)
    dimensions, raw = decode_png(png)
    assert dimensions == (544 * scale, 306 * scale), dimensions
    return raw


def presentation_crop(raw, scale, box):
    width = 544 * scale
    left, top, right, bottom = (value * scale for value in box)
    return b''.join(raw[y * (width * 3 + 1) + 1 + left * 3:
                         y * (width * 3 + 1) + 1 + right * 3] for y in range(top, bottom))


def fitted_presentation_crop(raw, scale, box):
    # Independent nearest-neighbor fit of a full-resolution capture, matching
    # the palette-preserving presentation used inside Studio's movie viewport.
    left, top, right, bottom = box
    stride = 544 * scale * 3 + 1
    return b''.join(raw[(y * scale + scale // 2) * stride + 1 + (x * scale + scale // 2) * 3:
                       (y * scale + scale // 2) * stride + 4 + (x * scale + scale // 2) * 3]
                    for y in range(top, bottom) for x in range(left, right))


def subpixel_edges(raw, scale, box=(0, 215, 544, 306)):
    # Below the pause banner, native background art consists of repeated NxN
    # blocks. Finer differences here prove the 3D foreground was rerasterized.
    width = 544 * scale
    stride = width * 3 + 1
    count = 0
    left, top, right, bottom = box
    for y in range(top, bottom):
        for x in range(left, right):
            offset = (y * scale) * stride + 1 + x * scale * 3
            color = raw[offset:offset + 3]
            if any(raw[offset + row * stride + column * 3:offset + row * stride + column * 3 + 3] != color
                   for row in range(scale) for column in range(scale)):
                count += 1
    return count


def studio_presentation(studio, output, label, scale):
    png = base64.b64decode(studio.call('screenshot', presentation=True)['content'][0]['data'])
    (output / (label + '-studio-presentation.png')).write_bytes(png)
    dimensions, raw = decode_png(png)
    assert dimensions == (640 * scale, 480 * scale), dimensions
    view = studio.state()['sdl_presentation']
    assert (view['source_width'], view['source_height']) == (640, 480), view
    assert (view['frame_width'], view['frame_height']) == dimensions, view
    def gob(hid):
        result = json.loads(studio.call('find_gob', hid=hid)['content'][0]['text'])
        assert result['found'], result
        return result
    workspace, button = gob(0x2005D), gob(0x20039)
    stride = dimensions[0] * 3 + 1
    x, y = workspace['x'] * scale, workspace['y'] * scale
    movie = b''.join(b'\0' + raw[(y + row) * stride + 1 + x * 3:
                                 (y + row) * stride + 1 + (x + 544 * scale) * 3]
                     for row in range(306 * scale))
    # The scrolling fixture starts before actors enter; its text is the
    # foreground detail available in the idle editor at that first frame.
    edges = subpixel_edges(movie, scale, (40, 24, 220, 72)) if label == 'scrolling' else subpixel_edges(movie, scale)
    assert edges > 20, 'Studio still enlarged a movie reduced to native resolution'
    native = picture(studio, output / (label + '-studio-canvas.png'))
    x, y, w, h = (button[key] for key in ('x', 'y', 'w', 'h'))
    expected = b''.join(b''.join(native[(row * 640 + column) * 3:(row * 640 + column) * 3 + 3] * scale
                                for column in range(x, x + w)) * scale for row in range(y, y + h))
    actual = b''.join(raw[row * stride + 1 + x * scale * 3:
                         row * stride + 1 + (x + w) * scale * 3] for row in range(y * scale, (y + h) * scale))
    assert actual == expected, 'Studio bitmap controls did not upscale with nearest-neighbor sampling'
    return edges


def fullscreen(studio):
    previous = studio.state()
    if previous['sdl_fullscreen']:
        return previous
    studio.call('send_command', cid=40038)
    deadline = time.monotonic() + 10
    while True:
        dialog = json.loads(studio.call('get_sdl_dialog')['content'][0]['text'])
        if dialog['active'] and dialog['ready']:
            break
        assert time.monotonic() < deadline, 'Product Info did not open'
        time.sleep(.02)
    for control in (1015, 1):
        studio.call('sdl_dialog_action', generation=dialog['generation'], action='click', control=control)
    return wait(studio, lambda s: s['sdl_fullscreen'] and s['client_size'] != previous['client_size'],
                'Product Info did not select fullscreen')


def picture(studio, path):
    png = base64.b64decode(studio.call('screenshot')['content'][0]['data'])
    path.write_bytes(png)
    (width, height), raw = decode_png(png)
    assert (width, height) == (640, 480), (width, height)
    return b''.join(raw[y * (width * 3 + 1) + 1:(y + 1) * (width * 3 + 1)] for y in range(height))


def crop(pixels, box):
    left, top, right, bottom = box
    return b''.join(pixels[(y * 640 + left) * 3:(y * 640 + right) * 3] for y in range(top, bottom))


def banner_corner(state):
    view = state['sdl_presentation']
    x, y = view['source_x'] + 28, view['source_y'] + view['source_height'] // 2 - 51
    return x, y, x + 8, y + 8


def frozen_state(state):
    return {name: state[name] for name in ('movie_scene', 'scene_frame', 'scene_camera',
                                         'scene_actors', 'scene_textboxes')}


def paused(studio, output, label):
    studio.call('key', vk=32)
    state = wait(studio, lambda s: s['movie_paused'], 'Space did not pause')
    assert state['movie_playing'] and state['movie_only'], state
    first = picture(studio, output / (label + '-paused.png'))
    assert not any(crop(first, banner_corner(state))), 'Pause banner did not draw over the movie'
    time.sleep(.65)
    second = studio.state()
    image = picture(studio, output / (label + '-still-paused.png'))
    assert frozen_state(second) == frozen_state(state), 'Movie advanced while paused'
    assert image == first, 'Paused frame or scrolling text changed'
    # Paused clicks cannot dismiss an authored click pause or operate hidden controls.
    studio.call('click', x=320, y=240)
    time.sleep(.1)
    assert studio.state()['movie_paused'], 'A click resumed a Space pause'
    return state, first


def restored(studio, original):
    state = wait(studio, lambda s: not s['movie_playing'] and not s['movie_only'] and
                 s['sdl_fullscreen'] == original['sdl_fullscreen'] and s['client_size'] == original['client_size'],
                 'Studio was not restored', timeout=60)
    assert not state['movie_paused'] and state['sdl_fullscreen'] == original['sdl_fullscreen'], state
    assert state['client_size'] == original['client_size'], state
    assert state['sdl_integer_scaling'] == original['sdl_integer_scaling'], state
    view = state['sdl_presentation']
    assert (view['source_x'], view['source_y'], view['source_width'], view['source_height']) == (0, 0, 640, 480), view
    return state


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('exe', 'fixture', 'iso', 'artifacts'):
        parser.add_argument(name, type=pathlib.Path)
    parser.add_argument('--desktop', action='store_true')
    parser.add_argument('--render-scale', type=int, choices=(1, 2, 4), default=1)
    args = parser.parse_args()
    args.artifacts.mkdir(parents=True, exist_ok=True)
    output = pathlib.Path(tempfile.mkdtemp(prefix='run-', dir=args.artifacts.resolve()))
    run = str(uuid.uuid4())
    environment = {'THREEDMM_CONTENT_ROOT': None, 'THREEDMM_BATCH_TEST': '1',
                   'KAUAI_TEST_SETTINGS_RUN': run, 'XDG_CONFIG_HOME': str(output / 'settings'),
                   'SDL_AUDIODRIVER': 'dummy', 'SDL_VIDEODRIVER': None if args.desktop else 'dummy',
                   'SDL_RENDER_DRIVER': None if args.desktop else 'software'}
    launch = SimpleNamespace(exe=args.exe, content=output, artifacts=output,
                             launch_arguments=['--iso', str(args.iso.resolve())], environment=environment)
    report = {}
    try:
        studio = Studio(launch, None, 'bootstrap')
        try:
            content = pathlib.Path(studio.state()['iso_cache_root']) / '3D Movie Maker'
            studio.quit()
        finally:
            studio.close()
        def asset(name):
            return next(path for path in content.iterdir() if path.name.lower() == name)
        for scrolling in (False, True):
            label = 'scrolling' if scrolling else 'animated'
            movie = output / (label + '.3mm')
            command = [str(args.fixture.resolve()), str(movie), str(asset('bkgds.3cn')),
                       str(asset('3dmovie.chk')), str(asset('tmpls.3cn'))]
            subprocess.run(command + (['--batch'] if scrolling else ['--dissolve']), check=True)
            digest = hashlib.sha256(movie.read_bytes()).hexdigest()
            studio = Studio(launch, movie, label)
            try:
                original = studio.state()
                if args.render_scale != 1:
                    if scrolling:
                        assert original['render_scale'] == args.render_scale, 'Saved rendering size was not restored'
                    else:
                        original = render_scale(studio, args.render_scale,
                                                capture=output / (label + '-info.bmp'))
                    if scrolling:
                        # In the idle editor, only the movie pixels may change.
                        # This white text box isolates 2D glyphs from the 3D scene.
                        workspace = json.loads(studio.call('find_gob', hid=0x2005D)['content'][0]['text'])
                        assert workspace['found'], workspace
                        x, y = workspace['x'], workspace['y']
                        bounds = (x, y, x + workspace['w'], y + workspace['h'])
                        caption = (x + 42, y + 26, x + 218, y + 70)
                        saved_state = frozen_state(studio.state())
                        render_scale(studio, 1)
                        native = picture(studio, output / 'studio-native-text.png')
                        render_scale(studio, args.render_scale)
                        larger = picture(studio, output / 'studio-super-size-text.png')
                        assert frozen_state(studio.state()) == saved_state
                        changed = sum(a != b for a, b in zip(crop(native, caption), crop(larger, caption)))
                        assert changed > 20, 'Studio text still drew at native resolution'
                        for outside in ((0, 0, 640, y), (0, bounds[3], 640, 480),
                                        (0, y, x, bounds[3]), (bounds[2], y, 640, bounds[3])):
                            assert crop(native, outside) == crop(larger, outside), 'Super size changed Studio controls'
                        report['studio_text_changed_channels'] = changed
                    report[label + '_studio_subpixel_edges'] = studio_presentation(
                        studio, output, label, args.render_scale)
                if args.desktop and os.name == 'nt':
                    from studio_sdl_smoke import focus_studio
                    focus_studio(studio.process.pid)
                if scrolling:
                    original = fullscreen(studio)  # Preserve an already-fullscreen window too.
                else:
                    assert not original['sdl_fullscreen'], original
                if not scrolling:
                    normal = play(studio)
                    assert not normal['movie_only'] and not normal['movie_paused'], normal
                    if args.render_scale > 1:
                        # Exercise the large Studio surface through ordinary
                        # playback and a dissolve, without switching to movie-only.
                        seen = {normal['movie_scene']}
                        deadline = time.monotonic() + 60
                        while normal['movie_playing']:
                            normal = studio.state()
                            seen.add(normal['movie_scene'])
                            assert not normal['movie_only'], normal
                            assert time.monotonic() < deadline, 'Studio playback did not complete'
                            time.sleep(.02)
                        assert seen == {0, 1} and normal['movie_scene'] == 1, normal
                        report['studio_playback_scenes'] = sorted(seen)
                    else:
                        studio.call('send_command', cid=50005)
                        wait(studio, lambda s: not s['movie_playing'], 'Ordinary stop did not finish')
                    studio.call('seek_movie', scene=1, frame=1)
                playing = play(studio, True)
                assert playing['movie_only'] and playing['sdl_fullscreen'] == original['sdl_fullscreen'], playing
                assert playing['client_size'] == original['client_size'], 'Shift+Play changed the window size'
                view = playing['sdl_presentation']
                assert (view['source_width'], view['source_height']) == (544, 306), view
                assert (view['frame_width'], view['frame_height']) == (544 * args.render_scale, 306 * args.render_scale), view
                state, image = paused(studio, output, label)
                high = presentation(studio, output / (label + '-render.png'), args.render_scale)
                if scrolling and args.render_scale > 1:
                    x, y = view['source_x'], view['source_y']
                    assert crop(image, (x + 42, y + 26, x + 218, y + 70)) == \
                           fitted_presentation_crop(high, args.render_scale, (42, 26, 218, 70)), \
                           'Studio and movie-only text did not use the same rendered image'
                    report['shared_text_pixels'] = True
                if not scrolling and args.render_scale > 1:
                    report['subpixel_edges'] = subpixel_edges(high, args.render_scale)
                    assert report['subpixel_edges'] > 20, 'Foreground only repeated the native pixels'
                time.sleep(.2)
                assert presentation(studio, output / (label + '-render-frozen.png'), args.render_scale) == high
                report[label + '_paused'] = state
                if args.desktop and os.name == 'nt':
                    import ctypes
                    from ctypes import wintypes
                    from PIL import ImageGrab
                    from studio_sdl_smoke import studio_window
                    hwnd, user32 = studio_window(studio.process.pid)
                    user32.GetClientRect.argtypes = [wintypes.HWND, ctypes.POINTER(wintypes.RECT)]
                    user32.ClientToScreen.argtypes = [wintypes.HWND, ctypes.POINTER(wintypes.POINT)]
                    client, origin = wintypes.RECT(), wintypes.POINT()
                    assert user32.GetClientRect(hwnd, ctypes.byref(client))
                    assert user32.ClientToScreen(hwnd, ctypes.byref(origin))
                    ImageGrab.grab(bbox=(origin.x, origin.y, origin.x + client.right,
                                        origin.y + client.bottom)).save(output / (label + '-desktop.png'))
                studio.call('key', vk=32)
                wait(studio, lambda s: not s['movie_paused'], 'Space did not resume')
                resumed = picture(studio, output / (label + '-resumed.png'))
                assert any(crop(resumed, banner_corner(state))), 'Pause banner remained after resuming'
                if scrolling:
                    # The credits are above the pause banner. They must advance
                    # after resuming even while the movie frame number is fixed.
                    x, y = view['source_x'], view['source_y']
                    box = (x + 40, y + 24, x + 220, y + 72)
                    before = crop(image, box)
                    deadline = time.monotonic() + 3
                    while crop(picture(studio, output / 'scrolling-resumed.png'), box) == before:
                        assert time.monotonic() < deadline, 'Scrolling credits did not resume'
                        time.sleep(.05)
                    high_after = presentation(studio, output / 'scrolling-resumed-render.png', args.render_scale)
                    assert presentation_crop(high_after, args.render_scale, (40, 24, 220, 72)) != \
                           presentation_crop(high, args.render_scale, (40, 24, 220, 72)), \
                           'The high-resolution credits did not resume'
                    paused(studio, output, 'scrolling-again')
                    studio.call('key', vk=27)
                report[label + '_restored'] = restored(studio, original)
                if not scrolling:
                    # Starting at the end still rewinds; Escape also stops a
                    # user-paused movie whose playback clock is frozen.
                    play(studio, True)
                    paused(studio, output, 'escape')
                    studio.call('key', vk=27)
                    restored(studio, original)
                    if args.render_scale > 1:
                        saved_state = frozen_state(studio.state())
                        for size in (1, 2, 4, args.render_scale):
                            render_scale(studio, size)
                            assert frozen_state(studio.state()) == saved_state, 'Changing rendering size changed the movie'
                studio.quit()
            finally:
                studio.close()
            assert hashlib.sha256(movie.read_bytes()).hexdigest() == digest, 'Source movie changed'
        (output / 'report.json').write_text(json.dumps(report, indent=2), encoding='utf-8')
        print('Movie-only playback: Shift+Play, Space pause/resume, frozen captions, natural completion, '
              'Escape and window/scaling restoration passed: ' + str(output))
    finally:
        remove_settings(run)


if __name__ == '__main__':
    main()
