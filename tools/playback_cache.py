"""Content-addressed playback observations; comparisons are always recomputed.

Only completed captures with a recorded input key and intact media are reusable.
Aliases copy SQLite observations, share immutable media and identify the original
run explicitly. Retention must keep that original suite while an alias is kept.
"""
import ast
import hashlib
from importlib.metadata import PackageNotFoundError, version
import json
import os
from pathlib import Path
import platform

from playback_store import digest, encode, frame_coverage

ROOT = Path(__file__).resolve().parents[1]
TABLES = ('scenes', 'frames', 'images', 'sound_events', 'audio')


class CaptureInputs:
    def __init__(self, args, iso_manifest, resources, content):
        self.args, self.iso = args, iso_manifest
        self.resources, self.content = Path(resources), Path(content)
        self.files = {}
        if self.file(args.iso) != iso_manifest['iso_sha256']:
            raise RuntimeError('ISO changed after input preparation')

    def file(self, path):
        path = Path(path).resolve()
        if path not in self.files:
            self.files[path] = digest(path)
        return self.files[path]

    def directory(self, directory, suffixes=None):
        directory = Path(directory)
        return {p.relative_to(directory).as_posix(): self.file(p)
                for p in sorted(directory.rglob('*'))
                if p.is_file() and (suffixes is None or p.suffix.lower() in suffixes)}

    def key(self, case, player, mode, preparation, repetition):
        args = self.args
        audio = mode == 'playback' or preparation == 'replay'
        # Hash capture behavior, not the scheduler, cache lookup, comparison code,
        # source revision or descriptive notes. A cache-only edit needs no replay.
        names = {'capabilities', 'scenes', 'seek_capture', 'playback_capture',
                 'import_presentations', 'verify_feature_observations',
                 'verify_text_acid', 'verify_actor_audio', 'open_capture',
                 'warmup_capture', 'preserve_warmup_presentations'}
        if player == 'marcus':
            names.add('MarcusSession')
        self.file(ROOT / 'tools/playback_regression.py')
        source = ast.parse((ROOT / 'tools/playback_regression.py').read_text(encoding='utf-8'))
        behavior = [ast.dump(node, include_attributes=False) for node in source.body
                    if isinstance(node, (ast.FunctionDef, ast.ClassDef)) and node.name in names]
        tools = {'capture_behavior': hashlib.sha256(encode(behavior).encode()).hexdigest()}
        self.file(ROOT / 'tools/playback_store.py')
        storage = ast.parse((ROOT / 'tools/playback_store.py').read_text(encoding='utf-8'))
        tools['storage_behavior'] = hashlib.sha256(encode([
            ast.dump(node, include_attributes=False) for node in storage.body
            if isinstance(node, (ast.FunctionDef, ast.ClassDef))]).encode()).hexdigest()
        dependencies = ['retail_oracle/session.py', 'retail_oracle/client.py'] if player != 'marcus' else []
        if player == 'sdl':
            dependencies += ['retail_oracle/compare.py', '../tests/studio_movie_workflow.py']
        if player == 'v3dmm':
            dependencies += ['retail_oracle/v3dmm.py']
        if audio:
            dependencies += ['retail_oracle/audio_compare.py']
            tools['audio_helper'] = self.file(args.bridge / 'oracle-audio-capture.exe')
        tools.update({name: self.file(ROOT / 'tools' / name) for name in dependencies})
        if player in ('retail', 'v3dmm'):
            tools.update({name: self.file(args.bridge / name) for name in
                          ('retail-oracle.dll', 'retail-oracle-launch.exe')})
        runtime = {}
        if player in ('sdl', 'marcus'):
            executable = getattr(args, player)
            if not executable:
                raise ValueError('--%s executable is required' % player)
            executable = Path(executable)
            runtime['executable'] = self.file(executable)
            runtime['dlls'] = {p.name: self.file(p) for p in sorted(executable.parent.glob('*'))
                               if p.is_file() and p.suffix.lower() == '.dll'}
        if player == 'marcus':
            runtime['resources'] = self.file(self.resources)
            runtime['content'] = {p.name: self.file(p) for p in sorted(self.content.iterdir())
                                  if p.is_file() and p.suffix.lower() == '.3cn'}
            runtime['environment'] = {key: os.environ.get(key) for key in
                                      ('KAUAI_SOUNDFONT', 'KAUAI_FONT_PATH', 'SDL_VIDEODRIVER', 'SDL_RENDER_DRIVER')}
        if player == 'v3dmm':
            if not args.v3dmm:
                raise ValueError('--v3dmm distribution is required')
            runtime['distribution'] = self.directory(args.v3dmm, {'.dll', '.xpatch'})
            runtime['libraries'] = {name: self.directory(path) for name, path in case['libraries'].items()}
        case_inputs = {key: value for key, value in case.items() if key not in ('path', 'libraries', 'players', 'reference')}
        try:
            pillow = version('Pillow')
        except PackageNotFoundError:
            pillow = None
        inputs = {'version': 1, 'case': case_inputs, 'player': player, 'mode': mode,
                  'preparation': preparation, 'repetition': repetition,
                  'iso': {'sha256': self.iso['iso_sha256'], 'files': self.iso['files']},
                  'runtime': runtime, 'tools': tools, 'viewport': args.viewport,
                  'audio': '48000 Hz stereo signed 16-bit' if audio else None,
                  'max_seconds': args.max_seconds if audio else None,
                  'environment': {'os': platform.platform(), 'machine': platform.machine(),
                                  'python': platform.python_version(), 'pillow': pillow,
                                  'capture_environment': getattr(args, 'capture_environment', '')}}
        return hashlib.sha256(encode(inputs).encode()).hexdigest(), inputs

    def unchanged(self):
        try:
            return all(path.is_file() and digest(path) == expected for path, expected in self.files.items())
        except OSError:
            return False


class CaptureCache:
    def __init__(self, store):
        self.store = store

    def record(self, run, key, inputs):
        self.store.insert('capture_cache', run_id=run, key=key, inputs_json=encode(inputs))

    def intact(self, run):
        store = self.store
        directory = store.resolve(run['artifact_dir'])
        if not directory.is_dir() or not directory.is_relative_to(store.path.parent):
            return False
        ranges = [dict(row) for row in store.db.execute('SELECT * FROM scenes WHERE run_id=? ORDER BY scene', (run['id'],))]
        frames = [dict(row) for row in store.db.execute('SELECT * FROM frames WHERE run_id=? ORDER BY sequence', (run['id'],))]
        images = list(store.db.execute('SELECT * FROM images WHERE run_id=? ORDER BY sequence', (run['id'],)))
        if not frame_coverage(ranges, frames) or not images:
            return False
        if run['mode'] == 'seek' and [(r['scene'], r['frame']) for r in images] != [(r['scene'], r['frame']) for r in frames]:
            return False
        artifacts = [(r['path'], r['artifact_sha256']) for r in images]
        audio = store.db.execute('SELECT * FROM audio WHERE run_id=?', (run['id'],)).fetchone()
        if run['mode'] == 'playback' and audio is None:
            return False
        if audio:
            artifacts.append((audio['path'], audio['sha256']))
        checked = {}
        for relative, expected in artifacts:
            path = store.resolve(relative)
            if not path.is_relative_to(directory):
                return False
            if path not in checked:
                checked[path] = digest(path)
            if checked[path] != expected:
                return False
        return True

    def lookup(self, key):
        for row in self.store.db.execute('''SELECT r.* FROM capture_cache c JOIN runs r ON r.id=c.run_id
                WHERE c.key=? AND r.status='complete' ORDER BY r.id DESC''', (key,)):
            try:
                if self.intact(row):
                    return row
            except (OSError, ValueError):
                pass
        return None

    def reuse(self, source, suite):
        store = self.store
        provenance = json.loads(source['provenance_json'])
        provenance['cache_reuse'] = {'source_run': source['id'], 'source_suite': source['suite_id']}
        fields = {key: source[key] for key in source.keys() if key != 'id'}
        fields.update(suite_id=suite, provenance_json=encode(provenance))
        with store.db:
            run = store.db.execute('INSERT INTO runs (' + ','.join(fields) + ') VALUES (' +
                                   ','.join('?' for _ in fields) + ')', tuple(fields.values())).lastrowid
            for table in TABLES:
                columns = [row[1] for row in store.db.execute('PRAGMA table_info(' + table + ')') if row[1] != 'run_id']
                names = ','.join(columns)
                store.db.execute('INSERT INTO ' + table + ' (run_id,' + names + ') SELECT ?,' + names +
                                 ' FROM ' + table + ' WHERE run_id=?', (run, source['id']))
        return run
