"""Verify Release batch playback with scrolling captions and all pause types."""
import sys
sys.dont_write_bytecode = True
import argparse
import json
import pathlib
import subprocess
import tempfile
from types import SimpleNamespace

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1] / 'tools'))
from batch_movie_playback import test_movie
from studio_movie_workflow import Studio


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('exe', 'fixture', 'iso', 'artifacts'):
        parser.add_argument(name, type=pathlib.Path)
    args = parser.parse_args()
    args.artifacts.mkdir(parents=True, exist_ok=True)
    output = pathlib.Path(tempfile.mkdtemp(prefix='run-', dir=args.artifacts.resolve()))
    cache = args.artifacts.resolve() / 'cache'
    startup = SimpleNamespace(exe=args.exe, content=output, artifacts=output,
        launch_arguments=['--iso', str(args.iso.resolve())], environment={
            'THREEDMM_CONTENT_ROOT': None, 'THREEDMM_BATCH_TEST': '1',
            'THREEDMM_CACHE_DIR': str(cache), 'SDL_VIDEODRIVER': 'dummy',
            'SDL_AUDIODRIVER': 'dummy', 'SDL_RENDER_DRIVER': 'software'})
    studio = Studio(startup, None, 'bootstrap')
    try:
        content = pathlib.Path(studio.state(details=False)['iso_cache_root']) / '3D Movie Maker'
        studio.quit()
    finally:
        studio.close()

    def asset(name):
        return next(path for path in content.iterdir() if path.name.lower() == name)

    movie = output / 'scrolling-pauses.3mm'
    subprocess.run([str(args.fixture.resolve()), str(movie), str(asset('bkgds.3cn')),
                    str(asset('3dmovie.chk')), str(asset('tmpls.3cn')), '--batch'], check=True)
    result = test_movie(args.exe.resolve(), args.iso.resolve(), movie, output / 'playback', cache)
    assert result['status'] == 'passed', result
    progress = result['progress']
    assert not progress['assertions_enabled'], progress
    assert progress['frames'] == progress['expected_frames'] == 66, progress
    assert progress['scenes'] == progress['scene_count'] == 2, progress
    assert progress['first_frame'] == -2 and progress['last_frame'] == 30, progress
    assert progress['scroll_steps'] > 100, progress
    for kind in ('click', 'timed', 'sound'):
        assert progress['skipped_' + kind + '_pauses'] == 2, progress
    assert not result['copy_changed'], result
    (output / 'report.json').write_text(json.dumps(result, indent=2), encoding='utf-8')
    print('Release batch playback: 66 frames, two scenes, all pause types, '
          + str(progress['scroll_steps']) + ' rendered caption scroll updates passed')


if __name__ == '__main__':
    main()
