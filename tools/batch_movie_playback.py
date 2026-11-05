"""Play every frame of .3mm movies in isolated, unpaced Release test processes.

Requires STUDIO_TEST_TOOLS=ON. Tests copies only; never saves a movie. Results,
frame journals and source copies live below the selected artifact directory.
"""
import sys
sys.dont_write_bytecode = True
import argparse
import base64
from collections import Counter
from concurrent.futures import ThreadPoolExecutor, as_completed
import hashlib
import json
import pathlib
import re
import shutil
import stat
import subprocess
import tempfile
import threading
import time
from types import SimpleNamespace

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1] / 'tests'))
from studio_movie_workflow import Studio


def digest(path):
    result = hashlib.sha256()
    with path.open('rb') as stream:
        while block := stream.read(1024 * 1024):
            result.update(block)
    return result.hexdigest()


def last_checkpoint(tail, progress=None):
    checkpoints = re.findall(r'BATCH_(?:FRAME|SCENE_LOAD) scene=(-?\d+)(?: frame=(-?\d+))?', tail)
    if not checkpoints:
        return None
    scene, frame = checkpoints[-1]
    scene = int(scene)
    frame = int(frame) if frame else None
    ranges = {int(scene): int(first) for scene, first in
              re.findall(r'BATCH_SCENE scene=(-?\d+) first=(-?\d+) last=-?\d+', tail)}
    first = ranges.get(scene)
    if first is None and progress and progress.get('scene') == scene:
        first = progress.get('first_frame')
    # Preserve engine coordinates for existing consumers; explicit UI numbers
    # account for scenes whose stored frame range starts somewhere other than 1.
    return {'scene': scene, 'frame': frame, 'scene_number': scene + 1,
            'frame_number': frame - first + 1 if frame is not None and first is not None else None,
            'first_frame': first}


def test_movie(executable, iso, source, output, cache, timeout=120, max_frames=1000000):
    output.mkdir(parents=True, exist_ok=True)
    copied = output / source.name
    source_hash = digest(source)
    shutil.copy2(source, copied)
    copied.chmod(copied.stat().st_mode | stat.S_IWUSR)
    result = {'movie': str(source.resolve()), 'sha256': source_hash, 'artifacts': str(output.resolve()),
              'status': 'error', 'progress': None}
    args = SimpleNamespace(exe=executable, content=output, artifacts=output, response_timeout=timeout + 5,
        launch_arguments=['--iso', str(iso.resolve())], environment={
            'THREEDMM_CONTENT_ROOT': None, 'THREEDMM_ISO': None, 'KAUAI_TEST_FAIL_FAST': None,
            'THREEDMM_BATCH_TEST': '1', 'THREEDMM_CACHE_DIR': str(cache.resolve()),
            'SDL_VIDEODRIVER': 'dummy', 'SDL_AUDIODRIVER': 'dummy', 'SDL_RENDER_DRIVER': 'software'})
    studio = None
    timer = None
    timed_out = threading.Event()
    start = time.monotonic()
    try:
        studio = Studio(args, copied, 'runtime')
        result['pid'] = studio.process.pid
        def deadline():
            if studio.process.poll() is None:
                timed_out.set()
                studio.process.kill() # Only the child created for this movie.
        timer = threading.Timer(timeout, deadline)
        timer.start()
        state = studio.state(details=False)
        result['scene_count'] = state['movie_scene_count']
        result['load_seconds'] = round(time.monotonic() - start, 3)
        def advance(action):
            return json.loads(studio.call('test_movie_playback', action=action,
                              max_frames=max(1, min(256, max_frames - (result['progress'] or {}).get('frames', 0))),
                              max_ms=200)['content'][0]['text'])
        progress = advance('start')
        if progress['assertions_enabled']:
            raise RuntimeError('Use a Release build: debug assertions are enabled')
        with (output / 'progress.jsonl').open('w', encoding='utf-8') as journal:
            while True:
                result['progress'] = progress
                journal.write(json.dumps(progress) + '\n')
                journal.flush()
                if not progress['active']:
                    break
                if progress['frames'] >= max_frames:
                    advance('stop')
                    result['status'] = 'frame_limit'
                    break
                progress = advance('step')
        if result['status'] != 'frame_limit':
            if (progress['complete'] and not progress['failed'] and progress['frames'] == progress['expected_frames']
                    and progress['scenes'] == progress['scene_count']):
                result['status'] = 'passed'
            else:
                result['status'] = 'failed'
                result['error'] = progress['error']
        if studio.process.poll() is None:
            screenshot = studio.call('screenshot')['content'][0]
            (output / 'last-frame.png').write_bytes(base64.b64decode(screenshot['data']))
    except Exception as error:
        if result['status'] == 'passed':
            result['status'] = 'error'
        result['error'] = str(error)
        # EOF can arrive just before the OS publishes the child's exit status.
        if studio and studio.process.poll() is None:
            try:
                studio.process.wait(timeout=1)
            except subprocess.TimeoutExpired:
                pass
    finally:
        if timer:
            timer.cancel()
        if studio:
            result['exit_code_before_cleanup'] = studio.process.poll()
            studio.close() # Isolated copies are discarded, never saved or confirmed through a dialog.
        result['wall_seconds'] = round(time.monotonic() - start, 3)
        log = output / 'runtime.log'
        if log.exists():
            with log.open('rb') as stream:
                stream.seek(max(0, log.stat().st_size - 12000))
                tail = stream.read().decode('utf-8', errors='replace')
            result['log_tail'] = tail
            result['error_codes'] = sorted(set(int(code) for code in re.findall(r'BATCH_ERROR_CODE (\d+)', tail)))
            checkpoint = last_checkpoint(tail, result.get('progress'))
            if checkpoint:
                result['last_checkpoint'] = checkpoint
            if 'BATCH_MISSING_' in tail:
                result['status'] = 'asset_error'
            elif timed_out.is_set():
                result['status'] = 'timeout'
            elif result.get('exit_code_before_cleanup') not in (None, 0, 3, 4):
                result['status'] = 'crash'
            elif result.get('exit_code_before_cleanup') == 3:
                result['status'] = 'failed'
        if digest(source) != source_hash:
            result['status'] = 'source_changed'
        result['copy_changed'] = digest(copied) != source_hash
        progress = result.get('progress') or {}
        if progress.get('elapsed_ms'):
            result['playback_fps'] = round(progress['frames'] * 1000 / progress['elapsed_ms'], 2)
            result['scroll_updates_per_second'] = round(progress['scroll_steps'] * 1000 / progress['elapsed_ms'], 2)
        (output / 'result.json').write_text(json.dumps(result, indent=2), encoding='utf-8')
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('exe', type=pathlib.Path)
    parser.add_argument('movies', type=pathlib.Path)
    parser.add_argument('artifacts', type=pathlib.Path)
    parser.add_argument('--iso', type=pathlib.Path, required=True)
    parser.add_argument('--timeout', type=float, default=120)
    parser.add_argument('--max-frames', type=int, default=1000000)
    parser.add_argument('--jobs', type=int, default=2)
    args = parser.parse_args()
    if args.timeout <= 0 or args.max_frames <= 0 or not 1 <= args.jobs <= 8:
        parser.error('Require --timeout > 0, --max-frames > 0 and --jobs between 1 and 8')
    movies = sorted((p for p in args.movies.rglob('*') if p.is_file() and p.suffix.lower() == '.3mm'),
                    key=lambda p: str(p).lower())
    if not movies:
        parser.error('No .3mm files found')
    args.artifacts.mkdir(parents=True, exist_ok=True)
    run = pathlib.Path(tempfile.mkdtemp(prefix='run-', dir=args.artifacts.resolve()))
    results = []
    started = time.monotonic()
    with ThreadPoolExecutor(max_workers=args.jobs) as workers:
        futures = {}
        for index, movie in enumerate(movies):
            folder = run / ('%02d-' % (index + 1) + re.sub(r'[^a-zA-Z0-9_-]', '_', movie.stem)[:60])
            future = workers.submit(test_movie, args.exe.resolve(), args.iso.resolve(), movie.resolve(), folder,
                                    args.artifacts.resolve() / 'content-cache', args.timeout, args.max_frames)
            futures[future] = movie
        for future in as_completed(futures):
            result = future.result()
            results.append(result)
            progress = result.get('progress') or {}
            print(f'[{len(results)}/{len(movies)}] {result["status"]}: {futures[future].name} '
                  f'({progress.get("frames", 0)} frames, {progress.get("scroll_steps", 0)} scroll updates)', flush=True)
            report = {'movies': len(movies), 'finished': len(results), 'counts': dict(Counter(r['status'] for r in results)),
                      'wall_seconds': round(time.monotonic() - started, 3), 'results': results}
            (run / 'report.json').write_text(json.dumps(report, indent=2), encoding='utf-8')
            (args.artifacts / 'latest.json').write_text(json.dumps({'report': str(run / 'report.json')}, indent=2), encoding='utf-8')
    print('Report: ' + str(run / 'report.json'))
    return 0 if all(r['status'] == 'passed' for r in results) else 1


if __name__ == '__main__':
    raise SystemExit(main())
