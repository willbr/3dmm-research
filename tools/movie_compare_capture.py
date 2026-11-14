"""Capture every authored frame for the native movie comparison viewer."""
import argparse
import json
import os
from pathlib import Path
import queue
import subprocess
import sys
import threading
import time
import zipfile

from PIL import Image

sys.dont_write_bytecode = True
ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools' / 'retail_oracle'))
from session import RetailSession, prepare_iso, digest


class Cancelled(Exception):
    pass


class Job:
    def __init__(self, output):
        self.output = output.resolve()
        self.output.mkdir(parents=True, exist_ok=True)
        self.counts = [0, 0, 0, 0]
        self.total = 0

    def check(self):
        if (self.output / 'cancel').exists():
            raise Cancelled()

    def atomic_text(self, name, text):
        temporary = self.output / (name + '.new')
        temporary.write_text(text, encoding='utf-8')
        # Windows readers can briefly hold the previous status file without
        # FILE_SHARE_DELETE. Keep publication atomic and retry that short race.
        for attempt in range(100):
            try:
                temporary.replace(self.output / name)
                break
            except PermissionError:
                if attempt == 99:
                    raise
                time.sleep(.01)

    def status(self, state, message):
        message = message.replace('\n', ' ').replace('\r', ' ')
        self.atomic_text('status.txt', '%s\n%d\n%s\n%s\n' %
                         (state, self.total, ' '.join(map(str, self.counts)), message))

    def save(self, image, backend, sequence):
        directory = self.output / backend
        directory.mkdir(exist_ok=True)
        temporary = directory / ('%06d.new.png' % sequence)
        image.save(temporary)
        temporary.replace(directory / ('%06d.png' % sequence))


class Marcus:
    def __init__(self, executable, movie, resources, content, output, renderer='brender'):
        environment = os.environ.copy()
        environment['SDL_AUDIODRIVER'] = 'dummy'
        self.log = (output / ('marcus-' + renderer + '.log')).open('w', encoding='utf-8')
        self.process = subprocess.Popen(
            [str(executable), str(movie), str(resources), str(content), str(output), '544', '306',
             '--renderer', renderer], stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=self.log,
            text=True, env=environment, creationflags=subprocess.CREATE_NO_WINDOW)
        self.responses = queue.Queue()
        self.reader = threading.Thread(target=self.read, daemon=True)
        self.reader.start()
        try:
            self.response()
        except BaseException:
            self.close()
            raise

    def read(self):
        for line in self.process.stdout:
            if line.startswith('REGRESSION '):
                try:
                    self.responses.put(json.loads(line[11:]))
                except ValueError:
                    self.responses.put({'error': 'Invalid Marcus capture response'})
        self.responses.put({'error': 'Marcus capture process exited; see capture log'})

    def response(self):
        try:
            result = self.responses.get(timeout=90)
        except queue.Empty:
            raise RuntimeError('Marcus capture timed out') from None
        if 'error' in result:
            raise RuntimeError(result['error'])
        return result

    def command(self, command):
        self.process.stdin.write(command + '\n')
        self.process.stdin.flush()
        return self.response()

    def close(self):
        if self.process.stdin and not self.process.stdin.closed:
            try:
                self.process.stdin.close()
            except OSError:
                pass
        try:
            self.process.wait(timeout=10)
        except subprocess.TimeoutExpired:
            self.process.kill()
            self.process.wait()
        self.log.close()


def source_movie(movie, output):
    destination = output / 'source'
    destination.mkdir(exist_ok=True)
    if zipfile.is_zipfile(movie):
        with zipfile.ZipFile(movie) as archive:
            movies = [item for item in archive.infolist() if not item.is_dir() and item.filename.lower().endswith('.3mm')]
            if len(movies) != 1:
                raise RuntimeError('Choose a ZIP containing exactly one .3mm movie')
            if movies[0].file_size > 256 * 1024 * 1024:
                raise RuntimeError('Movie exceeds the 256 MiB extraction limit')
            target = destination / Path(movies[0].filename).name
            target.write_bytes(archive.read(movies[0]))
            return target
    if movie.suffix.lower() != '.3mm':
        raise RuntimeError('Choose a .3mm movie or a ZIP containing one .3mm movie')
    return movie


def capture(args, job):
    movie = args.movie.resolve()
    original_hash = digest(movie)
    source = source_movie(movie, job.output)
    executable = args.marcus or ROOT / 'build/marcus-x64/src/marcus/marcus-regression.exe'
    resources = args.resources or ROOT / 'build/studio-portable-x64/3dmovie/utest.chk'
    content = args.content or ROOT / 'content-files'
    bridge = args.bridge or ROOT / 'tmp/retail-oracle/build'
    product = args.product or ROOT / 'tmp/retail-oracle/package/Microsoft Kids/3D Movie Maker'
    for path in (executable, resources, content, bridge / 'retail-oracle.dll', bridge / 'retail-oracle-launch.exe'):
        if not path.exists():
            raise RuntimeError('Required capture dependency is missing: ' + str(path))
    job.status('rendering', 'Reading movie scenes')
    cpu = Marcus(executable, source, resources, content, job.output)
    frames = []
    try:
        state = cpu.command('state')
        for scene in range(1, state['movie_scene_count'] + 1):
            job.check()
            info = cpu.command('seek %d 1' % scene)['state']
            frames.extend((scene, frame, info['scene_first_frame'] + frame - 1)
                          for frame in range(1, info['scene_last_frame'] - info['scene_first_frame'] + 2))
        if args.limit:
            frames = frames[:args.limit]
        job.total = len(frames)
        job.atomic_text('frames.tsv', ''.join('%d %d %d\n' % frame for frame in frames))
        if not (product / '3DMOVIE.EXE').exists():
            job.status('rendering', 'Preparing the retail ISO resources')
            product, _ = prepare_iso(ROOT / 'dist/3DMOVIE100.iso', product.parents[1])
        reference = RetailSession(bridge, product, source, job.output / 'retail-session', background=True)
        failures = {}
        try:
            if reference.state()['movie_scene_count'] != state['movie_scene_count']:
                raise RuntimeError('Retail and Marcus disagree about the scene count')
            for index, (scene, frame, raw) in enumerate(frames):
                job.check()
                # Retain the scene for ascending frame seeks; do not reload its
                # actors, captions and assets before every frame.
                result = reference.oracle.request(2, scene, frame, 1)
                actual = result['state']
                if (not result['success'] or actual['movie_scene'] != scene - 1 or
                        actual['scene_frame'] != raw or actual['error_stack_depth']):
                    raise RuntimeError('Retail failed to evaluate scene %d frame %d' % (scene, frame))
                bitmap = job.output / 'retail-session/frame.bmp'
                reference.oracle.capture(bitmap)
                with Image.open(bitmap) as image:
                    if image.size != (640, 480):
                        raise RuntimeError('Unexpected retail client size: ' + str(image.size))
                    job.save(image.convert('RGB').crop((48, 100, 592, 406)), 'retail', index)
                bitmap.unlink()
                job.counts[0] = index + 1
                prefix = job.output / 'capture'
                result = cpu.command('capture-frame %d %d %s' % (scene, frame, json.dumps(str(prefix))))
                actual = result['state']
                if actual['movie_scene'] != scene - 1 or actual['scene_frame'] != raw:
                    raise RuntimeError('Marcus returned the wrong scene/frame')
                for backend, item in enumerate(result['renderers'], 1):
                    renderer = item['id']
                    if not item['success']:
                        failures.setdefault(renderer, item['error'])
                        continue
                    ppm = Path(str(prefix) + '-' + renderer + '.ppm')
                    if renderer not in failures:
                        with Image.open(ppm) as image:
                            if image.size != (544, 306):
                                raise RuntimeError('Unexpected Marcus canvas size')
                            job.save(image.convert('RGB'), renderer, index)
                        job.counts[backend] = index + 1
                    ppm.unlink()
                job.status('rendering', 'Scene %d / frame %d: retail and all Marcus renderers' % (scene, frame))
            reference.validate_assets()
        finally:
            reference.close()
        failures = [renderer + ': ' + error for renderer, error in failures.items()]
        if digest(movie) != original_hash:
            raise RuntimeError('The source movie changed during capture')
        metadata = {'movie': str(movie), 'movie_sha256': original_hash, 'source_sha256': digest(source),
                    'marcus_sha256': digest(executable), 'bridge_sha256': digest(bridge / 'retail-oracle.dll'),
                    'resources_sha256': digest(resources), 'frames': len(frames), 'counts': job.counts,
                    'retail_viewport': [48, 100, 592, 406], 'mode': 'ordered authored-frame seeks, retaining each scene',
                    'failures': failures}
        job.atomic_text('capture.json', json.dumps(metadata, indent=2))
        job.status('complete' if not failures else 'partial',
                   'All frames captured' if not failures else '; '.join(failures))
    finally:
        if cpu is not None:
            cpu.close()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--movie', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    for option in ('marcus', 'resources', 'content', 'bridge', 'product'):
        parser.add_argument('--' + option, type=Path)
    parser.add_argument('--limit', type=int, default=0, help='Diagnostic capture limit; zero captures every frame')
    args = parser.parse_args()
    job = Job(args.output)
    try:
        capture(args, job)
        return 0
    except Cancelled:
        job.status('cancelled', 'Capture cancelled')
        return 0
    except Exception as error:
        job.status('error', str(error))
        return 1


if __name__ == '__main__':
    raise SystemExit(main())
