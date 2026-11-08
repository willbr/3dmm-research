"""Exercise optional patch features through Options, using an isolated generated movie."""
import sys
sys.dont_write_bytecode = True
import argparse
import base64
import configparser
import hashlib
import json
import pathlib
import subprocess
import tempfile
import time
import uuid
from types import SimpleNamespace
from studio_movie_workflow import Studio, signature
from studio_movie_only_workflow import remove_settings, wait, play
from mcp_png_test import decode_png


CONTROLS = {
    'allow_multiple_instances': 1034,
    'background_playback': 1035,
    'flat_shading': 1036,
    'skip_startup_screens': 1037,
    'mute_startup_sound': 1038,
    'mute_studio_sounds': 1039,
    'cut_transitions': 1040,
    'normal_key_repeat': 1041,
    'disable_audio_fades': 1042,
}


def dialog(studio, title):
    deadline = time.monotonic() + 10
    while True:
        value = json.loads(studio.call('get_sdl_dialog')['content'][0]['text'])
        if value['active'] and value['ready'] and value['title'] == title:
            return value
        assert time.monotonic() < deadline, ('Dialog did not open', title, value)
        time.sleep(.02)


def click(studio, current, control):
    studio.call('sdl_dialog_action', generation=current['generation'], action='click', control=control)


TAB_CONTROLS = {
    1043: ('flat_shading',),
    1044: ('mute_studio_sounds', 'background_playback', 'cut_transitions', 'disable_audio_fades', 'normal_key_repeat'),
    1045: ('allow_multiple_instances', 'skip_startup_screens', 'mute_startup_sound'),
}


def options(studio):
    studio.call('send_command', cid=40038)
    return dialog(studio, 'Options')


def select_tab(studio, current, control):
    click(studio, current, control)
    deadline = time.monotonic() + 10
    while True:
        current = dialog(studio, 'Options')
        if any(item['id'] == control and item['number'] for item in current['controls']):
            return current
        assert time.monotonic() < deadline, ('Tab did not activate', control, current)
        time.sleep(.02)


def close_options(studio, current, control):
    click(studio, current, control)
    return wait(studio, lambda state: not state['dialog']['present'], 'Options did not close')


def set_options(studio, desired, capture=None):
    current = options(studio)
    for tab, names in TAB_CONTROLS.items():
        current = select_tab(studio, current, tab)
        controls = {item['id']: item for item in current['controls']}
        for name in names:
            if name in desired and bool(controls[CONTROLS[name]]['number']) != desired[name]:
                click(studio, current, CONTROLS[name])
    if capture:
        for tab, name in ((1043, 'display'), (1044, 'playback'), (1045, 'startup'), (1046, 'about')):
            current = select_tab(studio, current, tab)
            studio.call('sdl_dialog_action', generation=current['generation'], action='screenshot',
                        text=str(capture.with_stem(capture.stem + '-' + name)))
    state = close_options(studio, current, 1)
    assert all(state['preferences'][name] == value for name, value in desired.items()), state
    return state


def resolution(studio, scale):
    current = options(studio)
    click(studio, current, {0: 1033, 1: 1030, 2: 1031, 4: 1032}[scale])
    state = close_options(studio, current, 1)
    expected_width, expected_height = (272, 153) if scale == 0 else (544 * scale, 306 * scale)
    assert (state['render_width'], state['render_height']) == (expected_width, expected_height), state
    assert state['render_reduced'] == (scale == 0), state
    if scale > 1:
        width, height = state['client_size']['w'], state['client_size']['h']
        assert width % 640 == 0 and height % 480 == 0 and width // 640 == height // 480, state
        assert 1 <= width // 640 <= scale, state
    return state


def live_rendering(studio, original, original_pixels):
    controls = {1026: 'wireframe', 1036: 'flat_shading', 1027: 'no_textures', 1028: 'show_actor_pose'}
    current = options(studio)
    for control, name in controls.items():
        click(studio, current, control)
        wait(studio, lambda state: state['rendering'][name], 'Rendering did not preview: ' + name)
        pixels = studio.capture('live-' + name)
        if name in ('wireframe', 'flat_shading'):
            assert pixels != original_pixels, 'The scene did not redraw while Options was open: ' + name
        click(studio, current, control)
        wait(studio, lambda state: not state['rendering'][name], 'Rendering did not toggle back: ' + name)
        assert studio.capture('live-restored-' + name) == original_pixels, name
    for escape, choice in ((False, 1031), (True, 1033)):
        for control in controls:
            click(studio, current, control)
        click(studio, current, choice)
        preview = wait(studio, lambda state: all(state['rendering'].values()) and
                       state['render_width'] == (272 if escape else 1088), 'Resolution did not preview')
        assert preview['dialog']['present'], preview['dialog']
        assert signature(preview) == signature(original), 'Preview changed cached authored materials'
        picture = base64.b64decode(studio.call('screenshot', presentation=True)['content'][0]['data'])
        dimensions, _ = decode_png(picture)
        assert dimensions == ((640, 480) if escape else (1280, 960)), dimensions
        if escape:
            studio.call('sdl_dialog_action', generation=current['generation'], action='escape')
            cancelled = wait(studio, lambda state: not state['dialog']['present'], 'Escape did not close Options')
        else:
            cancelled = close_options(studio, current, 2)
        assert cancelled['rendering'] == original['rendering']
        assert signature(cancelled) == signature(original)
        assert cancelled['preferences'] == original['preferences']
        assert cancelled['render_width'] == original['render_width']
        assert cancelled['render_height'] == original['render_height']
        assert cancelled['client_size'] == original['client_size']
        assert studio.capture('live-cancelled-' + str(escape)) == original_pixels
        if not escape:
            current = options(studio)


def check_save_directory(studio):
    main_window = studio.state()['sdl_window_id']
    studio.call('send_command', cid=104)
    deadline = time.monotonic() + 10
    while True:
        current = json.loads(studio.call('get_sdl_dialog')['content'][0]['text'])
        if current['active'] and current['ready'] and any(item['id'] == 10 for item in current['controls']):
            break
        assert time.monotonic() < deadline, 'Save portfolio did not open'
        time.sleep(.02)
    controls = {item['id']: item for item in current['controls']}
    assert current['embedded'] and current['window_id'] == main_window, 'Portfolio opened another window'
    documents = next((item['text'] for item in current['controls'] if item['label'] == 'Documents'), None)
    if documents:
        assert pathlib.Path(controls[10]['text']) == pathlib.Path(documents) / '3DMMForever', controls[10]
    close_options(studio, current, 2)


def timing(studio, rate, modifier, accept=True):
    current = select_tab(studio, options(studio), 1044)
    click(studio, current, 1062 + rate)
    click(studio, current, 1067 + modifier)
    return close_options(studio, current, 1 if accept else 2)


def playback_timing(studio):
    measurements = {}
    for choice, fps in ((1, 6), (2, 12), (3, 24), (2, 12 * 1.35805), (2, 12 * .918485)):
        modifier = 0 if fps in (6, 12, 24) else 1 if fps > 12 else 2
        state = timing(studio, choice, modifier)
        assert abs(state['playback_timing']['target_fps'] - fps) < .0001, state['playback_timing']
        studio.call('seek_movie', scene=1, frame=1)
        play(studio, True)
        first = wait(studio, lambda s: s['scene_frame'] >= 2, 'Paced movie did not advance')
        started = time.monotonic()
        if fps == 6:
            studio.call('key', vk=32)
            frozen = wait(studio, lambda s: s['movie_paused'], 'Paced movie did not pause')
            time.sleep(.2)
            assert studio.state()['scene_frame'] == frozen['scene_frame'], 'Pacer advanced during Space pause'
            studio.call('key', vk=32)
            wait(studio, lambda s: not s['movie_paused'], 'Paced movie did not resume')
            started = time.monotonic()
            first = studio.state()
        last = wait(studio, lambda s: s['scene_frame'] >= 8, 'Paced movie stalled')
        elapsed = time.monotonic() - started
        expected = (last['scene_frame'] - first['scene_frame']) / fps
        assert expected * .65 < elapsed < expected * 1.6 + .10, (fps, elapsed, expected)
        measurements[str(fps)] = elapsed
        studio.call('key', vk=27)
        wait(studio, lambda s: not s['movie_playing'], 'Paced movie did not stop')
    state = timing(studio, 4, 2)
    assert state['playback_timing']['target_fps'] == 0
    studio.call('seek_movie', scene=1, frame=1)
    play(studio, True)
    wait(studio, lambda s: s['scene_frame'] > 1 or not s['movie_playing'], 'Uncapped movie stalled')
    studio.call('key', vk=27)
    wait(studio, lambda s: not s['movie_playing'], 'Uncapped movie did not stop')
    accepted = timing(studio, 2, 1)['playback_timing']
    ini = pathlib.Path(studio.state()['settings_file'])
    saved_bytes = ini.read_bytes()
    cancelled = timing(studio, 3, 2, accept=False)['playback_timing']
    assert cancelled == accepted, (accepted, cancelled)
    assert ini.read_bytes() == saved_bytes, 'Cancel wrote settings.ini'
    return measurements


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('exe', 'fixture', 'iso', 'artifacts'):
        parser.add_argument(name, type=pathlib.Path)
    args = parser.parse_args()
    args.artifacts.mkdir(parents=True, exist_ok=True)
    output = pathlib.Path(tempfile.mkdtemp(prefix='run-', dir=args.artifacts.resolve()))
    run = str(uuid.uuid4())
    launch = SimpleNamespace(exe=args.exe, content=output, artifacts=output,
                             launch_arguments=['--iso', str(args.iso.resolve())],
                             environment={'THREEDMM_CONTENT_ROOT': None, 'THREEDMM_BATCH_TEST': '1',
                                          'KAUAI_TEST_SETTINGS_RUN': run, 'XDG_CONFIG_HOME': str(output / 'settings'),
                                          'SDL_AUDIODRIVER': 'dummy', 'SDL_VIDEODRIVER': 'dummy',
                                          'SDL_RENDER_DRIVER': 'software'})
    report = {}
    try:
        studio = Studio(launch, None, 'bootstrap')
        try:
            state = studio.state()
            assert not any(state['preferences'].values()), state['preferences']
            content = pathlib.Path(state['iso_cache_root']) / '3D Movie Maker'
            studio.quit()
        finally:
            studio.close()
        def asset(name):
            return next(path for path in content.iterdir() if path.name.lower() == name)
        movie = output / 'options.3mm'
        subprocess.run([str(args.fixture.resolve()), str(movie), str(asset('bkgds.3cn')),
                        str(asset('3dmovie.chk')), str(asset('tmpls.3cn')), '--dissolve'], check=True)
        digest = hashlib.sha256(movie.read_bytes()).hexdigest()
        studio = Studio(launch, movie, 'options')
        try:
            original = studio.state()
            original_pixels = studio.capture('original')
            report['playback_timing_seconds'] = playback_timing(studio)
            studio.call('seek_movie', scene=1, frame=1)
            original = studio.state()
            original_pixels = studio.capture('original-after-playback')
            live_rendering(studio, original, original_pixels)
            report['rendering_previews_live_and_cancel_restores'] = True
            current = options(studio)
            assert not any(item['id'] == 1010 for item in current['controls'])
            for tab, names in TAB_CONTROLS.items():
                current = select_tab(studio, current, tab)
                for name in names:
                    click(studio, current, CONTROLS[name])
            current = select_tab(studio, current, 1043)
            assert next(item['number'] for item in current['controls'] if item['id'] == 1036) == 1
            assert not any(item['id'] == 1039 for item in current['controls']), 'Hidden page remained interactive'
            click(studio, current, 1029)  # Cancel also rolls back the existing display controls.
            cancelled = close_options(studio, current, 2)
            assert cancelled['preferences'] == original['preferences'], cancelled
            assert cancelled['sdl_integer_scaling'] == original['sdl_integer_scaling']
            report['cancel_preserves_preferences'] = True
            report['tabs_preserve_edits_and_hide_other_pages'] = True
            enabled = set_options(studio, dict.fromkeys(CONTROLS, True),
                                  capture=output / 'options.bmp')
            ini = pathlib.Path(enabled['settings_file'])
            assert ini.name == 'settings.ini' and ini.parent.name == run, ini
            config = configparser.ConfigParser(interpolation=None)
            config.read(ini, encoding='utf-8')
            assert config.getint('User', 'PlaybackRate') == 2
            assert config.getint('User', 'PlaybackModifier') == 1
            assert config.getint('User', 'AllowMultipleInstances') == 1
            assert config.getint('User', 'MuteStudioSounds') == 1
            if sys.platform == 'win32':
                import winreg
                try:
                    with winreg.OpenKey(winreg.HKEY_CURRENT_USER,
                                        'Software\\3DMMForever\\RetailOracle\\SDL-' + run,
                                        0, winreg.KEY_READ | winreg.KEY_WOW64_32KEY):
                        raise AssertionError('SDL settings created a registry branch')
                except FileNotFoundError:
                    pass
            report['ini_settings_without_registry_writes'] = True
            assert signature(enabled) == signature(original), 'Options altered authored movie data'
            assert enabled['scene_transition'] == original['scene_transition']
            flat_pixels = studio.capture('flat-shading')
            assert flat_pixels != original_pixels, 'Flat shading did not change the rendered scene'
            saved = output / 'saved-with-options.3mm'
            current = options(studio)
            click(studio, current, 1027)
            untextured = close_options(studio, current, 1)
            assert signature(untextured) == signature(original), 'No-textures changed authored materials'
            check_save_directory(studio)
            report['documents_save_directory'] = True
            studio.save(saved)
            current = options(studio)
            click(studio, current, 1027)
            close_options(studio, current, 1)
            report['saving_with_no_textures_preserves_materials'] = True
            set_options(studio, {'flat_shading': False})
            assert studio.capture('restored-shading') == original_pixels, 'Original shading was not restored'
            report['flat_shading_is_reversible'] = True
            set_options(studio, {'flat_shading': True})
            for scale in (2, 4):
                resolution(studio, scale)
            report['super_resolution_fits_integer_window'] = True
            resolution(studio, 0)
            report['reduced_render_resolution'] = True
            # A second process must run independently when the option is saved.
            other = Studio(launch, movie, 'second-instance')
            try:
                reopened = other.state()
                assert reopened['playback_timing'] == enabled['playback_timing'], 'Playback timing was not saved'
                assert all(reopened['preferences'].values()), 'Saved options did not reach the second instance'
                assert reopened['render_reduced'] and reopened['render_width'] == 272 and reopened['render_height'] == 153
                other.quit()
            finally:
                other.close()
            report['multiple_instances_and_saved_preferences'] = True
            current = options(studio)
            click(studio, current, 1030)
            current = select_tab(studio, current, 1044)
            click(studio, current, CONTROLS['normal_key_repeat'])
            cancelled = close_options(studio, current, 2)
            assert cancelled['render_reduced'] and cancelled['preferences']['normal_key_repeat']
            studio.quit()
        finally:
            studio.close()
        # The saving process holds its output file for writing. Reopen only
        # after it exits; simultaneous instances above use a separate document.
        studio = Studio(launch, saved, 'reopened')
        try:
            reopened = studio.state()
            assert all(reopened['preferences'].values()), 'Cancelled edits overwrote saved defaults'
            assert signature(reopened) == signature(original), 'Saving with options changed movie semantics'
            assert reopened['scene_transition'] == original['scene_transition']
            assert reopened['render_reduced'] and reopened['render_width'] == 272 and reopened['render_height'] == 153
            report['reduced_resolution_persists'] = True
            report['ok_saves_automatically_and_cancel_discards'] = True
            resolution(studio, 1)
            report['movie_save_reopen_preserves_semantics'] = True
            # Screen skipping and startup sound are independent preferences.
            set_options(studio, {**dict.fromkeys(CONTROLS, False), 'skip_startup_screens': True})
            studio.quit()
        finally:
            studio.close()
        studio = Studio(launch, None, 'startup-sound')
        try:
            state = studio.state()
            assert state['preferences']['skip_startup_screens'] and not state['preferences']['mute_startup_sound']
            assert state['sdl_midi_queued'] > 0, 'Skipping startup screens also muted startup sound'
            report['startup_controls_are_independent'] = True
            studio.quit()
        finally:
            studio.close()
        assert hashlib.sha256(movie.read_bytes()).hexdigest() == digest, 'Options changed the input movie'
        report['movie_file_unchanged'] = True
        (output / 'report.json').write_text(json.dumps(report, indent=2) + '\n')
        print(json.dumps({'artifacts': str(output), **report}, indent=2))
    finally:
        remove_settings(run)


if __name__ == '__main__':
    main()
