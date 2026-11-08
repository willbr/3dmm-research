"""Exercise saved favourite folders and remembered movie locations through the SDL portfolio."""
import sys
sys.dont_write_bytecode = True
import argparse
import configparser
import json
import pathlib
import tempfile
import time
import uuid
from types import SimpleNamespace
from studio_movie_workflow import Studio
from studio_options_workflow import dialog, click


def observe(studio, predicate):
    deadline = time.monotonic() + 15
    while True:
        current = json.loads(studio.call('get_sdl_dialog')['content'][0]['text'])
        if current['ready'] and predicate(current):
            return current
        assert time.monotonic() < deadline, current
        time.sleep(.02)


def location(current):
    return next(item['text'] for item in current['controls'] if item['id'] == 10)


def navigate(studio, current, folder):
    studio.call('sdl_dialog_action', generation=current['generation'], action='text', control=10, text=str(folder))
    observe(studio, lambda value: location(value) == str(folder))
    studio.call('sdl_dialog_action', generation=current['generation'], action='enter')
    return observe(studio, lambda value: location(value) == folder.name)


def open_movie(studio):
    studio.call('send_command', cid=101, hid=20005)
    return dialog(studio, 'Open a movie')


def menu(studio, current):
    click(studio, current, 13)
    return observe(studio, lambda value: any(item['id'] == 14 for item in value['controls']))


def cancel(studio, current):
    click(studio, current, 2)
    deadline = time.monotonic() + 15
    while json.loads(studio.call('get_sdl_dialog')['content'][0]['text'])['active']:
        assert time.monotonic() < deadline
        time.sleep(.02)


def saved(ini, key):
    document = configparser.ConfigParser(interpolation=None)
    document.read(ini, encoding='utf-8')
    encoding = document['User.Types'][key]
    value = document['User'][key]
    data = (int(value).to_bytes(1 if encoding == 'byte' else 4, 'little')
            if encoding in ('byte', 'int') else bytes.fromhex(value))
    return [part.decode('utf-8') for part in data.split(b'\0') if part]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('exe', 'iso', 'fixture', 'artifacts'):
        parser.add_argument('--' + name, type=pathlib.Path, required=True)
    args = parser.parse_args()
    args.artifacts.mkdir(parents=True, exist_ok=True)
    output = pathlib.Path(tempfile.mkdtemp(prefix='run-', dir=args.artifacts.resolve()))
    first, second = output / 'Favourite café', output / 'Second folder'
    first.mkdir(); second.mkdir()
    run = str(uuid.uuid4())
    launch = SimpleNamespace(exe=args.exe, content=output, artifacts=output,
        launch_arguments=['--iso', str(args.iso.resolve())], environment={
            'THREEDMM_CONTENT_ROOT': None, 'THREEDMM_BATCH_TEST': '1', 'KAUAI_TEST_SETTINGS_RUN': run,
            'THREEDMM_PACK_DIR': str(output / 'packs'), 'SDL_VIDEODRIVER': 'dummy',
            'SDL_AUDIODRIVER': 'dummy', 'SDL_RENDER_DRIVER': 'software'})
    for pass_number in range(2):
        studio = Studio(launch, args.fixture, 'pass-' + str(pass_number))
        try:
            ini = pathlib.Path(studio.state()['settings_file'])
            current = open_movie(studio)
            if pass_number == 0:
                current = navigate(studio, current, first)
                current = menu(studio, current)
                assert not {'Pictures', 'Music', 'Videos'} & {item['label'] for item in current['controls']}
                click(studio, current, 14)
                observe(studio, lambda value: not any(item['id'] == 14 for item in value['controls']))
                assert saved(ini, 'FavoriteFolders') == [str(first)]
                current = navigate(studio, current, second)
                cancel(studio, current)
                assert saved(ini, 'LastMovieFolder') == [str(second)]
                current = open_movie(studio)
                assert location(current) == second.name
                current = menu(studio, current)
                favorite = next(item for item in current['controls'] if item['id'] >= 0x200000)
                assert favorite['text'] == str(first)
                click(studio, current, favorite['id'])
                current = observe(studio, lambda value: location(value) == first.name)
            else:
                assert location(current) == first.name, 'Last folder did not survive a fresh process'
                current = menu(studio, current)
                assert any(item['id'] >= 0x200000 and item['text'] == str(first) for item in current['controls'])
                assert next(item['label'] for item in current['controls'] if item['id'] == 14).startswith('Remove')
                click(studio, current, 14)
                current = observe(studio, lambda value: not any(item['id'] == 14 for item in value['controls']))
                assert saved(ini, 'FavoriteFolders') == []
            cancel(studio, current)
            studio.quit()
        finally:
            studio.close()
    first.rmdir()
    studio = Studio(launch, args.fixture, 'missing-folder')
    try:
        current = open_movie(studio)
        assert location(current) != first.name, 'An unavailable remembered folder must not strand the picker'
        cancel(studio, current)
        assert saved(ini, 'LastMovieFolder') != [str(first)]
        studio.quit()
    finally:
        studio.close()
    print(json.dumps({'artifacts': str(output), 'status': 'passed',
        'checks': ['add/remove favourites through menu', 'UTF-8 path persistence', 'favourite navigation',
                   'last folder after cancellation', 'fresh process persistence', 'unavailable folder fallback',
                   'movie places filtering']}, indent=2))


if __name__ == '__main__': main()
