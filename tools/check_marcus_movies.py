"""Exercise every movie frame through Marcus's C API, without driving its UI.

Includes .3mm/.vmm files in ZIP and RAR archives, deduplicates identical bytes,
and runs read-only copies in isolated child processes. No expansion installation
or format conversion is performed. Journals identify the scene/frame of a crash.
"""
import argparse
from collections import Counter
from concurrent.futures import ThreadPoolExecutor, wait, FIRST_COMPLETED
import hashlib
import json
import os
from pathlib import Path
import shutil
import stat
import subprocess
import threading
import time
import zipfile
import zlib

MOVIE_TYPES = {'.3mm', '.vmm', '.webm'}
PREFIX = 'MARCUS_BATCH '
NO_WINDOW = subprocess.CREATE_NO_WINDOW if os.name == 'nt' else 0


def digest(path):
    result = hashlib.sha256()
    with path.open('rb') as stream:
        while block := stream.read(1024 * 1024):
            result.update(block)
    return result.hexdigest()


def save_json(path, data):
    temporary = path.with_suffix(path.suffix + '.tmp')
    temporary.write_text(json.dumps(data, indent=2, ensure_ascii=True), encoding='utf-8')
    temporary.replace(path)


def inventory(root, output):
    movies, sources, problems = {}, {}, []
    inputs = output / 'inputs'
    inputs.mkdir(parents=True, exist_ok=True)

    def add(data, name):
        checksum = hashlib.sha256(data).hexdigest()
        if checksum in movies:
            movies[checksum]['sources'].append(name)
            return
        path = inputs / (checksum + Path(name).suffix.lower())
        if not path.exists():
            path.write_bytes(data)
            path.chmod(stat.S_IREAD)
        elif digest(path) != checksum:
            raise RuntimeError('An existing playback copy has changed: ' + str(path))
        movies[checksum] = {
            'sha256': checksum, 'input': str(path.resolve()), 'sources': [name],
            'format': 'vmm' if data.startswith(b'v3dmm') else
                      'webm' if data.startswith(b'\x1aE\xdf\xa3') else 'native',
        }

    seven_zip = shutil.which('7z')
    paths = [root] if root.is_file() else sorted(p for p in root.rglob('*') if p.is_file())
    for path in paths:
        extension = path.suffix.lower()
        if extension not in MOVIE_TYPES | {'.zip', '.rar'}:
            continue
        sources[str(path)] = digest(path)
        try:
            if extension in MOVIE_TYPES:
                add(path.read_bytes(), str(path))
            elif extension == '.zip':
                with zipfile.ZipFile(path) as archive:
                    for member in archive.infolist():
                        if not member.is_dir() and Path(member.filename).suffix.lower() in MOVIE_TYPES:
                            try:
                                data = archive.read(member)
                            except NotImplementedError:
                                if not seven_zip:
                                    raise RuntimeError('7-Zip is needed for this ZIP compression method')
                                extracted = subprocess.run([seven_zip, 'x', '-so', '-y', '-bd', str(path),
                                                            member.filename], capture_output=True,
                                                           creationflags=NO_WINDOW)
                                data = extracted.stdout
                                if (extracted.returncode not in (0, 1) or len(data) != member.file_size or
                                        zlib.crc32(data) != member.CRC):
                                    raise RuntimeError('7-Zip extraction failed its size/CRC check')
                            add(data, str(path) + ' :: ' + member.filename)
            else:
                if not seven_zip:
                    raise RuntimeError('7-Zip is needed to read RAR movies')
                listing = subprocess.check_output([seven_zip, 'l', '-slt', str(path)], text=True,
                                                  creationflags=NO_WINDOW)
                for block in listing.split('\n\n'):
                    fields = dict(line.split(' = ', 1) for line in block.splitlines() if ' = ' in line)
                    member = fields.get('Path', '')
                    if Path(member).suffix.lower() in MOVIE_TYPES and fields.get('Folder') != '+':
                        data = subprocess.check_output([seven_zip, 'x', '-so', '-y', '-bd', str(path), member],
                                                       creationflags=NO_WINDOW)
                        add(data, str(path) + ' :: ' + member)
        except Exception as error:
            problems.append({'source': str(path), 'error': str(error)})
    return list(movies.values()), sources, problems


def checkpoint(path):
    try:
        with path.open('rb') as stream:
            stream.seek(max(0, path.stat().st_size - 4096))
            lines = stream.read().decode('utf-8', errors='replace').splitlines()
        for line in reversed(lines):
            if line.startswith(PREFIX):
                try:
                    return json.loads(line[len(PREFIX):])
                except ValueError:
                    continue
    except (OSError, ValueError):
        pass
    return None


def run_movie(movie, args):
    started = time.monotonic()
    log = args.output / (movie['sha256'] + '.jsonl')
    result = dict(movie, journal=str(log), status='failed', errors=[])
    with log.open('wb') as stream:
        process = subprocess.Popen([str(args.exe), movie['input'], str(args.resources), str(args.content_dir)],
                                   stdout=stream, stderr=subprocess.STDOUT, creationflags=NO_WINDOW)
        result['pid'] = process.pid
        size, last_progress = 0, time.monotonic()
        while process.poll() is None:
            current_size = log.stat().st_size
            if current_size != size:
                size, last_progress = current_size, time.monotonic()
            if time.monotonic() - last_progress > args.stall_timeout:
                process.kill()
                process.wait()
                result['status'] = 'stalled'
                break
            threading.Event().wait(0.5)
        result['exit_code'] = process.returncode
    result['last_checkpoint'] = checkpoint(log)
    done = None
    completed, attempted = 0, 0
    with log.open(encoding='utf-8', errors='replace') as stream:
        for line in stream:
            if not line.startswith(PREFIX):
                continue
            try:
                event = json.loads(line[len(PREFIX):])
            except ValueError:
                continue  # A terminated child may leave its final line incomplete.
            if event['event'] == 'error':
                result['errors'].append(event)
            elif event['event'] == 'scene_done' and event['complete']:
                completed += 1
            elif event['event'] == 'frame':
                attempted += 1
            elif event['event'] == 'done':
                done = event
    result.update(completed_scenes=completed, attempted_frames=attempted)
    if done:
        result.update({key: value for key, value in done.items() if key not in ('event', 'errors')})
    if process.returncode == 0 and done and not result['errors']:
        result['status'] = 'passed'
    elif movie['format'] == 'webm':
        result['status'] = 'unsupported'
    elif not result['errors']:
        result['errors'].append({'phase': 'process', 'message': 'Playback process did not complete',
                                 'checkpoint': result['last_checkpoint']})
    result['seconds'] = round(time.monotonic() - started, 2)
    result['copy_unchanged'] = digest(Path(movie['input'])) == movie['sha256']
    if not result['copy_unchanged']:
        result['status'] = 'failed'
        result['errors'].append({'phase': 'integrity', 'message': 'The playback copy changed'})
    save_json(args.output / (movie['sha256'] + '.json'), result)
    return result


def summary(report, output):
    counts = report['status_counts']
    lines = ['Marcus visual playback audit', '',
             f"Unique movies: {report['unique_movies']} ({report['source_entries']} source entries)",
             f"Played completely: {counts.get('passed', 0)}",
             f"Failed or incomplete: {sum(n for status, n in counts.items() if status not in ('passed', 'unsupported'))}",
             f"Unsupported formats: {counts.get('unsupported', 0)}",
             f"Frames rendered: {sum(r.get('frames_rendered', 0) for r in report['results'])}",
             f"Scenes completed: {sum(r.get('completed_scenes', 0) for r in report['results'])}",
             f"Original files unchanged: {report['originals_unchanged']}", '',
             'Method: optimized Marcus C API, authored-camera rendering at 480 x 360,',
             'without UI automation or playback pacing. Every frame is evaluated and',
             'rendered until a scene fails; remaining scenes are still attempted.',
             'This checks visual playback, not audio, captions, transitions or image correctness.', '',
             'FAILED / INCOMPLETE MOVIES', '']
    for result in sorted(report['results'], key=lambda r: r['sources'][0].lower()):
        if result['status'] in ('passed', 'unsupported'):
            continue
        lines.extend([result['sources'][0],
                      f"  Completed scenes: {result.get('completed_scenes', 0)} / {result.get('scenes', '?')}",
                      f"  Rendered frames: {result.get('frames_rendered', 0)} / {result.get('frames_expected', '?')}"])
        errors = result.get('errors', [])
        if errors:
            first = errors[0]
            lines.append(f"  First error: scene {first.get('scene', '?')}, phase {first.get('phase', '?')}")
        for message in sorted({error.get('message', '') for error in errors}):
            lines.append('  ' + message)
        lines.append('')
    lines.extend(['COMPLETE PLAYTHROUGHS', ''])
    for result in sorted(report['results'], key=lambda r: r['sources'][0].lower()):
        if result['status'] == 'passed':
            lines.append(result['sources'][0] + f" ({result.get('frames_rendered', 0)} frames)")
    lines.extend(['', 'UNSUPPORTED INPUTS', ''])
    for result in sorted(report['results'], key=lambda r: r['sources'][0].lower()):
        if result['status'] == 'unsupported':
            lines.append(result['sources'][0] + ' [' + result['format'] + ']')
    lines.extend(['', 'Exact scene/raw-frame checkpoints and source aliases are in report.json.',
                  'Per-movie journals and SHA-256 input identities are in this directory.'])
    (output / 'summary.txt').write_text('\n'.join(lines) + '\n', encoding='utf-8')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('movies', type=Path)
    parser.add_argument('--exe', type=Path, required=True)
    parser.add_argument('--resources', type=Path, required=True)
    parser.add_argument('--content-dir', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--workers', type=int, default=4)
    parser.add_argument('--stall-timeout', type=float, default=120)
    parser.add_argument('--resume', action='store_true', help='Reuse completed results from this output directory')
    parser.add_argument('--retry-failures', action='store_true', help='With --resume, rerun failed movies')
    args = parser.parse_args()
    if not args.movies.exists():
        parser.error('Movie input does not exist')
    if not args.exe.is_file() or not args.resources.is_file() or not args.content_dir.is_dir():
        parser.error('The executable, resources file and content directory must exist')
    if args.workers < 1 or not 0 < args.stall_timeout < float('inf'):
        parser.error('Workers and stall timeout must be positive')
    args.movies = args.movies.resolve()
    args.output = args.output.resolve()
    args.output.mkdir(parents=True, exist_ok=True)
    started = time.monotonic()
    movies, sources, problems = inventory(args.movies, args.output)
    if not movies:
        parser.error('No movie inputs found')
    report = {'movies': str(args.movies), 'exe': str(args.exe.resolve()),
              'resources': str(args.resources.resolve()), 'content_dir': str(args.content_dir.resolve()),
              'unique_movies': len(movies), 'source_entries': sum(len(m['sources']) for m in movies),
              'inventory_errors': problems, 'results': []}
    save_json(args.output / 'manifest.json', {'movies': movies, 'source_hashes': sources})
    save_json(args.output / 'report.json', report)
    print(f"Inventory: {len(movies)} unique movies, {report['source_entries']} source entries, "
          f"{len(problems)} archive errors", flush=True)
    queued = []
    for movie in movies:
        result_file = args.output / (movie['sha256'] + '.json')
        if args.resume and result_file.exists():
            cached = json.loads(result_file.read_text(encoding='utf-8'))
            if args.retry_failures and cached['status'] not in ('passed', 'unsupported'):
                queued.append(movie)
            else:
                cached.update(sources=movie['sources'])
                report['results'].append(cached)
        else:
            queued.append(movie)
    report['status_counts'] = dict(Counter(r['status'] for r in report['results']))
    save_json(args.output / 'report.json', report)
    if args.resume:
        print(f"Reused {len(report['results'])} completed results; {len(queued)} movies remain", flush=True)
    with ThreadPoolExecutor(max_workers=args.workers) as workers:
        pending = {workers.submit(run_movie, movie, args): movie for movie in queued}
        while pending:
            ready, _ = wait(pending, timeout=30, return_when=FIRST_COMPLETED)
            for future in ready:
                movie = pending.pop(future)
                try:
                    result = future.result()
                except Exception as error:
                    result = dict(movie, status='runner_error', errors=[{'message': str(error)}])
                report['results'].append(result)
                report['status_counts'] = dict(Counter(r['status'] for r in report['results']))
                report['seconds'] = round(time.monotonic() - started, 2)
                save_json(args.output / 'report.json', report)
                print(f"{len(report['results'])}/{len(movies)} {result['status']}: {movie['sources'][0]} "
                      f"({result.get('frames_rendered', 0)} frames, {result.get('seconds', 0)}s)", flush=True)
            if not ready:
                active = []
                for movie in pending.values():
                    log = args.output / (movie['sha256'] + '.jsonl')
                    progress = checkpoint(log)
                    if progress:
                        active.append({'movie': movie['sources'][0], 'checkpoint': progress})
                print('Progress: ' + json.dumps(active[:args.workers], ensure_ascii=True), flush=True)
    report['originals_unchanged'] = all(digest(Path(path)) == checksum for path, checksum in sources.items())
    report['seconds'] = round(time.monotonic() - started, 2)
    save_json(args.output / 'report.json', report)
    summary(report, args.output)
    print('Finished: ' + json.dumps(report['status_counts']), flush=True)
    print('Report: ' + str(args.output / 'report.json'), flush=True)
    return bool(problems or not report['originals_unchanged'] or
                any(r['status'] not in ('passed', 'unsupported') for r in report['results']))


if __name__ == '__main__':
    raise SystemExit(main())
