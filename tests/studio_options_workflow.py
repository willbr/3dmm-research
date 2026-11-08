"""Exercise optional patch features through Options, using an isolated generated movie."""
import sys
sys.dont_write_bytecode = True
import argparse
import hashlib
import json
import pathlib
import subprocess
import tempfile
import time
import uuid
from types import SimpleNamespace
from studio_movie_workflow import Studio, signature
from studio_movie_only_workflow import remove_settings, wait


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


def more_options(studio):
    studio.call('send_command', cid=40038)
    click(studio, dialog(studio, 'Product Info'), 1033)
    return dialog(studio, 'More Options')


def close_options(studio, current, control):
    click(studio, current, control)
    click(studio, dialog(studio, 'Product Info'), 2)
    return wait(studio, lambda state: not state['dialog']['present'], 'Options did not close')


def set_options(studio, desired, save=False, capture=None):
    current = more_options(studio)
    controls = {item['id']: item for item in current['controls']}
    assert all(control in controls for control in CONTROLS.values()), current
    for name, value in desired.items():
        if bool(controls[CONTROLS[name]]['number']) != value:
            click(studio, current, CONTROLS[name])
    if save:
        click(studio, current, 1010)
    if capture:
        studio.call('sdl_dialog_action', generation=current['generation'], action='screenshot', text=str(capture))
    state = close_options(studio, current, 1)
    assert all(state['preferences'][name] == value for name, value in desired.items()), state
    return state


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
            current = more_options(studio)
            for control in CONTROLS.values():
                click(studio, current, control)
            cancelled = close_options(studio, current, 2)
            assert cancelled['preferences'] == original['preferences'], cancelled
            report['cancel_preserves_preferences'] = True
            enabled = set_options(studio, dict.fromkeys(CONTROLS, True), save=True,
                                  capture=output / 'more-options.bmp')
            assert signature(enabled) == signature(original), 'Options altered authored movie data'
            assert enabled['scene_transition'] == original['scene_transition']
            flat_pixels = studio.capture('flat-shading')
            assert flat_pixels != original_pixels, 'Flat shading did not change the rendered scene'
            saved = output / 'saved-with-options.3mm'
            studio.save(saved)
            set_options(studio, {'flat_shading': False})
            assert studio.capture('restored-shading') == original_pixels, 'Original shading was not restored'
            report['flat_shading_is_reversible'] = True
            # A second process must run independently when the option is saved.
            other = Studio(launch, movie, 'second-instance')
            try:
                reopened = other.state()
                assert all(reopened['preferences'].values()), 'Saved options did not reach the second instance'
                other.quit()
            finally:
                other.close()
            report['multiple_instances_and_saved_preferences'] = True
            set_options(studio, {'normal_key_repeat': False})
            studio.quit()
        finally:
            studio.close()
        # The saving process holds its output file for writing. Reopen only
        # after it exits; simultaneous instances above use a separate document.
        studio = Studio(launch, saved, 'reopened')
        try:
            reopened = studio.state()
            assert all(reopened['preferences'].values()), 'Unsaved session changes overwrote defaults'
            assert signature(reopened) == signature(original), 'Saving with options changed movie semantics'
            assert reopened['scene_transition'] == original['scene_transition']
            report['movie_save_reopen_preserves_semantics'] = True
            # Screen skipping and startup sound are independent preferences.
            set_options(studio, {**dict.fromkeys(CONTROLS, False), 'skip_startup_screens': True}, save=True)
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
