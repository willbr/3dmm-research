"""Run Studio from an isolated package containing only the executable and ISO.

Checks cold/warm automatic caching, retail room colors, software MIDI without
an adjacent SoundFont, scene authoring, save/reopen, and explicit ISO selection.
The original ISO, executable, and existing installed content are never modified.
"""
import argparse
import hashlib
import json
import os
import pathlib
import shutil
import tempfile
from types import SimpleNamespace

from studio_building_workflow import capture, inspect, wait_for
from studio_movie_workflow import Studio


def digest(path):
    value = hashlib.sha256()
    with path.open('rb') as stream:
        while block := stream.read(1024 * 1024):
            value.update(block)
    return value.hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('exe', type=pathlib.Path)
    parser.add_argument('iso', type=pathlib.Path)
    parser.add_argument('artifacts', type=pathlib.Path)
    cli = parser.parse_args()
    cli.artifacts.mkdir(parents=True, exist_ok=True)
    output = pathlib.Path(tempfile.mkdtemp(prefix='run-', dir=cli.artifacts.resolve()))
    package = output / 'package'
    package.mkdir()
    executable = package / cli.exe.name
    image = package / 'Original Movie Maker.iso'
    shutil.copy2(cli.exe, executable)
    try:
        os.link(cli.iso.resolve(), image)
    except OSError:
        shutil.copy2(cli.iso, image)
    original_hash = digest(cli.iso)
    executable_hash = digest(executable)
    args = SimpleNamespace(exe=executable, content=package, artifacts=output, environment={
        'THREEDMM_CONTENT_ROOT': None, 'THREEDMM_ISO': None, 'KAUAI_SOUNDFONT': '', 'KAUAI_FONT_PATH': '',
        'THREEDMM_CACHE_DIR': str(output / 'cache')})
    report = {'package': str(package)}
    studio = Studio(args, None, 'cold')
    try:
        state = studio.state()
        assert state['iso_content'] and not state['iso_cache_reused'], state
        assert pathlib.Path(state['iso_source']).samefile(image), state
        cached = pathlib.Path(state['iso_cache_root'])
        assert cached.is_relative_to(output / 'cache'), cached
        decoder_notices = (cached.parent / 'licenses/audio-codecs-LICENSE.txt').read_text(encoding='utf-8')
        assert 'libgsm' in decoder_notices and 'dr_mp3' in decoder_notices and 'minimp3' in decoder_notices
        main_archive = next(p for p in (cached / '3D Movie Maker').iterdir() if p.name.lower() == '3dmovie.chk')
        stamp = main_archive.stat().st_mtime_ns
        report['cold'] = state
        studio.call('send_command', cid=40042)
        wait_for(studio, lambda: inspect(studio, 0x12052).get('visible', False), 'Map did not open')
        studio.control(0x12052) # Original lobby entry.
        wait_for(studio, lambda: inspect(studio, 0x10600).get('visible', False), 'Lobby did not open')
        studio.call('wait_ms', ms=750)
        report['lobby'] = capture(studio, output / 'lobby.png')
        studio.quit()
    finally:
        studio.close()

    saved = output / 'iso-only-movie.3mm'
    studio = Studio(args, None, 'warm')
    try:
        state = studio.state()
        assert state['iso_content'] and state['iso_cache_reused'], state
        assert pathlib.Path(state['iso_cache_root']) == cached and main_archive.stat().st_mtime_ns == stamp
        report['warm'] = state
        studio.call('click', x=48, y=21)
        wait_for(studio, lambda: inspect(studio, 0x2003e)['found'], 'Scene browser did not open')
        studio.call('click', x=170, y=130)
        wait_for(studio, lambda: studio.state()['movie_scene_count'] == 1, 'Original background did not load')
        studio.control(0x20007) # Original Sounds tab.
        wait_for(studio, lambda: inspect(studio, 0x2007a).get('visible', False), 'Music button did not appear')
        studio.control(0x2007a)
        wait_for(studio, lambda: inspect(studio, 0x21121).get('visible', False), 'Music browser did not open')
        before_music = studio.state()
        studio.control(0x21121) # Preview "funny moment 1", not the silent sound-stopper entry.
        def audible():
            state = studio.state()
            report['music'] = state
            return (state['sdl_midi_notes'] > before_music['sdl_midi_notes'] and
                    state['sdl_midi_audible_frames'] > before_music['sdl_midi_audible_frames'])
        try:
            wait_for(studio, audible, 'Bundled MIDI bank produced no music')
        finally:
            (output / 'music-state.json').write_text(json.dumps(report.get('music', {}), indent=2), encoding='utf-8')
        assert report['music']['midi_provider'] == 'sdl-soundfont'
        studio.control(0x21013) # Cancel preview without changing the movie.
        studio.save(saved)
        studio.quit()
    finally:
        studio.close()

    # An explicit ISO must override even an invalid configured extracted tree.
    args.launch_arguments = ['--iso', str(image)]
    args.environment['THREEDMM_CONTENT_ROOT'] = str(output / 'not-an-installed-tree')
    studio = Studio(args, saved, 'explicit-reopen')
    try:
        state = studio.state()
        assert state['iso_content'] and state['iso_cache_reused'] and state['movie_scene_count'] == 1, state
        assert pathlib.Path(state['iso_cache_root']) == cached
        report['explicit_reopen'] = state
        studio.quit()
    finally:
        studio.close()

    assert {p.name for p in package.iterdir()} == {executable.name, image.name}, 'Package gained extra dependencies'
    assert digest(cli.iso) == original_hash and digest(image) == original_hash, 'Original ISO changed'
    assert digest(executable) == executable_hash, 'Executable changed'
    report['features'] = dict.fromkeys(('two_file_package', 'original_iso_resources', 'cold_cache', 'warm_cache',
        'building_palette', 'bundled_midi', 'decoder_notices', 'scene_authoring', 'save_reopen', 'explicit_iso', 'input_unchanged'), 'passed')
    (output / 'report.json').write_text(json.dumps(report, indent=2), encoding='utf-8')
    print('Studio ISO workflow: ' + ', '.join(report['features']) + ' passed')
    print('Artifacts: ' + str(output))


if __name__ == '__main__':
    main()
