"""Headless expansion UI: staged installs, INI persistence, source policy and owned removal."""
import sys
sys.dont_write_bytecode = True
import argparse
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
from studio_options_workflow import options, select_tab, click, close_options, dialog
from studio_movie_only_workflow import remove_settings


def controls(studio):
    return {c['id']: c for c in dialog(studio, 'Options')['controls']}


def expect(studio, predicate):
    deadline = time.monotonic() + 90
    while True:
        current = json.loads(studio.call('get_sdl_dialog')['content'][0]['text'])
        assert current['title'] == 'Options', current
        items = {c['id']: c for c in current['controls']}
        if predicate(items):
            return items
        assert time.monotonic() < deadline, current
        time.sleep(.02)


def path(studio, current, text, control=1084):
    studio.call('sdl_dialog_action', generation=current['generation'], action='text', control=control, text=str(text.resolve()))
    expect(studio, lambda items: items[control]['text'] == str(text.resolve()))


def page(studio):
    return select_tab(studio, options(studio), 1071)


def install(studio, current, source, index):
    path(studio, current, source)
    click(studio, current, 1078 + index)
    expect(studio, lambda items: items[1072 + index]['enabled'] and not items[1078 + index]['enabled'])


def preferences(ini):
    config = configparser.ConfigParser(interpolation=None)
    config.read(ini, encoding='utf-8')
    return json.loads(json.loads(config.get('User', 'ExpansionPacks')))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('exe', 'fixture', 'iso', 'nick_folder', 'dora_iso', 'frankie_folder', 'artifacts'):
        parser.add_argument(name, type=pathlib.Path)
    args = parser.parse_args()
    args.artifacts.mkdir(parents=True, exist_ok=True)
    output = pathlib.Path(tempfile.mkdtemp(prefix='run-', dir=args.artifacts.resolve()))
    run = str(uuid.uuid4())
    launch = SimpleNamespace(exe=args.exe, content=output, artifacts=output, response_timeout=120,
                             launch_arguments=['--iso', str(args.iso.resolve())],
                             environment={'THREEDMM_CONTENT_ROOT': None, 'THREEDMM_BATCH_TEST': '1',
                                          'KAUAI_TEST_SETTINGS_RUN': run, 'THREEDMM_PACK_DIR': str(output / 'packs'),
                                          'SDL_AUDIODRIVER': 'dummy', 'SDL_VIDEODRIVER': 'dummy',
                                          'SDL_RENDER_DRIVER': 'software'})
    report = {}
    sources = (args.nick_folder, args.dora_iso, args.frankie_folder)
    try:
        studio = Studio(launch, None, 'bootstrap')
        try:
            state = studio.state()
            base = pathlib.Path(state['iso_cache_root']) / '3D Movie Maker'
            ini = pathlib.Path(state['settings_file'])
            studio.quit()
        finally:
            studio.close()
        def asset(name):
            return next(p for p in base.iterdir() if p.name.lower() == name)
        movie = output / 'packs.3mm'
        subprocess.run([str(args.fixture.resolve()), str(movie), str(asset('bkgds.3cn')),
                        str(asset('3dmovie.chk')), str(asset('tmpls.3cn'))], check=True)
        original_hash = hashlib.sha256(movie.read_bytes()).hexdigest()
        studio = Studio(launch, movie, 'install')
        try:
            original = studio.state()
            current = page(studio)
            expect(studio, lambda items: not any(items[1072+i]['enabled'] for i in range(3)))
            install(studio, current, sources[0], 0)
            assert list((output / 'packs/.pending').glob('*/pack.json'))
            close_options(studio, current, 2)
            assert not list((output / 'packs/.pending').iterdir())
            assert not preferences(ini)['nickelodeon']['generation']
            report['cancel_discards_staged_files'] = True
            current = page(studio)
            for index, source in enumerate(sources):
                install(studio, current, source, index)
            studio.call('sdl_dialog_action', generation=current['generation'], action='screenshot', text=str(output / 'expansions.bmp'))
            state = close_options(studio, current, 1)
            assert not any(p['available'] for p in state['expansions']), state['expansions']
            saved = preferences(ini)
            for key, pref in saved.items():
                assert pref['enabled'] and pref['generation'] and not pref['remove'], saved
                assert (output / 'packs' / key / pref['generation'] / 'pack.json').is_file()
            report['ok_publishes_and_persists_next_launch'] = True
            studio.quit()
        finally:
            studio.close()
        studio = Studio(launch, movie, 'enabled')
        try:
            state = studio.state()
            assert all(p['available'] for p in state['expansions']), state['expansions']
            thumbnail_ids = {f['source_id'] for f in state['thumbnail_files']}
            assert {2, 3, 4, 5} <= thumbnail_ids, state['thumbnail_files']
            assert signature(state) == signature(original)
            report['managed_sources_appear_in_content_browsers'] = True
            current = page(studio)
            click(studio, current, 1072)
            close_options(studio, current, 2)
            assert preferences(ini)['nickelodeon']['enabled']
            current = page(studio)
            click(studio, current, 1072)
            state = close_options(studio, current, 1)
            assert state['expansions'][0]['available'], state['expansions']
            studio.quit()
        finally:
            studio.close()
        studio = Studio(launch, movie, 'disabled')
        try:
            state = studio.state()
            assert not state['expansions'][0]['enabled'] and not state['expansions'][0]['available']
            assert 3 not in {f['source_id'] for f in state['thumbnail_files']}
            report['toggle_is_saved_and_applies_after_restart'] = True
            nick = output / 'packs/nickelodeon' / preferences(ini)['nickelodeon']['generation']
            (nick / 'my-notes.txt').write_text('preserve this unowned file')
            changed = nick / 'NICKACTOR.3TH'
            with changed.open('ab') as stream:
                stream.write(b'changed')
            current = page(studio)
            for i in range(3):
                click(studio, current, 1081+i)
            expect(studio, lambda items: all('Removal' in items[1075+i]['text'] for i in range(3)))
            click(studio, current, 1083)  # Undo Frankie's removal, then queue it again.
            expect(studio, lambda items: 'Removal' not in items[1077]['text'] and items[1074]['number'])
            click(studio, current, 1083)
            close_options(studio, current, 1)
            assert all(p['remove'] for p in preferences(ini).values())
            assert all((output / 'packs' / key / p['generation'] / 'pack.json').exists() for key, p in saved.items())
            report['removal_waits_until_archives_close'] = True
            studio.quit()
        finally:
            studio.close()
        studio = Studio(launch, movie, 'removed')
        try:
            state = studio.state()
            assert not any(p['managed'] or p['available'] for p in state['expansions'])
            assert not any(p['generation'] or p['remove'] for p in preferences(ini).values())
            assert (nick / 'my-notes.txt').read_text() == 'preserve this unowned file'
            assert changed.exists() and not (nick / 'NICKTMPLS.3CN').exists()
            assert not (output / 'packs/doraemon' / saved['doraemon']['generation']).exists()
            assert not (output / 'packs/frankie' / saved['frankie']['generation']).exists()
            assert args.nick_folder.is_dir() and args.dora_iso.is_file() and args.frankie_folder.is_dir()
            assert signature(state) == signature(original)
            report['uninstall_preserves_changed_unknown_and_original_files'] = True
            studio.quit()
        finally:
            studio.close()
        assert hashlib.sha256(movie.read_bytes()).hexdigest() == original_hash
        (output / 'report.json').write_text(json.dumps(report, indent=2) + '\n')
        print(json.dumps({'artifacts': str(output), **report}, indent=2))
    finally:
        remove_settings(run)


if __name__ == '__main__':
    main()
