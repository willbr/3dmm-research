"""Capture a movie corpus in retail, original v3dmm, SDL Studio and Marcus.

Run with --help. SQLite contains raw observations and scoped comparisons.
All media, working copies and original-runtime resources remain local.
"""
import sys
sys.dont_write_bytecode = True
import argparse
import array
from datetime import datetime, timezone
import json
import itertools
import math
import os
from pathlib import Path
import platform
import queue
import re
import struct
import subprocess
import threading
import time
import uuid
import wave

from playback_store import Store, digest, encode, frame_coverage, timing_metrics
from playback_retention import prune, shared_cache, require_space

ROOT = Path(__file__).resolve().parents[1]
PLAYERS = ('retail', 'v3dmm', 'sdl', 'marcus')
DEFAULT_PLAYERS = ('retail', 'v3dmm', 'sdl')
FEATURES = (
    'background', 'depth-occlusion', 'camera-cuts', 'actor-animation', 'routes',
    'rotation', 'scale', 'stretch', 'freeze', 'entrance-exit', 'costume', 'props',
    'textures', 'transparency', 'lighting', '3d-text', '2d-text', 'scrolling-text',
    'transition-cut', 'transition-fade', 'transition-dissolve', 'transition-black',
    'sampled-speech', 'sampled-effects', 'actor-sounds', 'motion-sounds', 'overlapping-sounds', 'sound-queues',
    'sound-offsets', 'sound-volume', 'sound-loops', 'sound-tail', 'midi',
    'timed-wait', 'sound-wait', 'click-wait', 'v3dmm-assets', 'long-playback',
)


def legacy():
    # Import only for Windows capture; SQLite review and tests are portable.
    sys.path.insert(0, str(ROOT / 'tools/retail_oracle'))
    import session
    import compare
    import audio_compare
    import v3dmm
    return session, compare, audio_compare, v3dmm


def load_manifest(path):
    path = Path(path).resolve()
    data = json.loads(path.read_text(encoding='utf-8'))
    if data.get('schema') != 1 or not isinstance(data.get('cases'), list) or not data['cases']:
        raise ValueError('Expected schema 1 and a nonempty cases list')
    names = set()
    for case in data['cases']:
        name = case.get('name', '')
        if not re.fullmatch(r'[a-zA-Z0-9_-]+', name) or name in names:
            raise ValueError('Case names must be unique simple identifiers: ' + str(name))
        names.add(name)
        if case.get('reference') not in ('retail', 'v3dmm'):
            raise ValueError('Each case needs a retail or v3dmm reference')
        if not isinstance(case.get('features'), list) or not all(isinstance(f, str) for f in case['features']):
            raise ValueError('Each case needs a features list')
        unknown = set(case['features']) - set(FEATURES)
        if unknown:
            raise ValueError('Unknown features: ' + ', '.join(sorted(unknown)))
        if 'text_acid_contract' in case and case['text_acid_contract'] != 1:
            raise ValueError('Unsupported Text Acid contract version')
        if 'actor_audio_contract' in case and case['actor_audio_contract'] != 1:
            raise ValueError('Unsupported actor audio contract version')
        case['path'] = (path.parent / case['path']).resolve()
        if not case['path'].is_file():
            raise ValueError('Missing movie: ' + str(case['path']))
        if case.get('sha256') != digest(case['path']):
            raise ValueError('Movie hash missing or changed: ' + name)
        players = case.get('players', list(PLAYERS))
        if not isinstance(players, list) or not players or len(set(players)) != len(players) or set(players) - set(PLAYERS):
            raise ValueError('Invalid players for ' + name)
        if case['reference'] not in players:
            raise ValueError('Case players must include its reference')
        case['players'] = players
        case['libraries'] = {name.lower(): (path.parent / value).resolve()
                             for name, value in case.get('libraries', {}).items()}
        if case.get('expected_audio', 'audible') not in ('audible', 'silent'):
            raise ValueError('expected_audio must be audible or silent')
    return data


class MarcusSession:
    def __init__(self, executable, movie, resources, content, output, width, height):
        output.mkdir(parents=True, exist_ok=False)
        self.output = output
        self.log = (output / 'runtime.log').open('w', encoding='utf-8')
        environment = dict(os.environ)
        environment['SDL_AUDIODRIVER'] = 'wasapi'
        self.process = subprocess.Popen([str(executable), str(movie), str(resources), str(content),
                                         str(output), str(width), str(height)], stdin=subprocess.PIPE,
                                        stdout=subprocess.PIPE, stderr=self.log, text=True,
                                        encoding='utf-8', env=environment, creationflags=subprocess.CREATE_NO_WINDOW)
        self.replies = queue.Queue()
        def read():
            try:
                for line in self.process.stdout:
                    if line.startswith('REGRESSION '):
                        self.replies.put(json.loads(line[len('REGRESSION '):]))
            except Exception as error:
                self.replies.put({'error': str(error)})
            finally:
                self.replies.put(None)
        self.reader = threading.Thread(target=read, daemon=True)
        self.reader.start()
        self.studio = self # Existing PCM capture uses this process/call interface.
        try:
            ready = self.replies.get(timeout=45)
            if not ready or not ready.get('ready'):
                raise RuntimeError('Marcus startup failed; inspect ' + str(output / 'runtime.log'))
            self.capture_features = ready.get('capture_features', {})
        except BaseException:
            self.close()
            raise

    def request(self, command):
        self.process.stdin.write(command + '\n')
        self.process.stdin.flush()
        result = self.replies.get(timeout=45)
        if result is None or result.get('error'):
            raise RuntimeError('Marcus capture failed: ' + (str(result.get('error')) if result else 'process exited'))
        return result

    def state(self):
        return self.request('state')

    def seek(self, scene, frame):
        return self.request('seek %d %d' % (scene, frame))

    def capture(self, label):
        from PIL import Image
        path = self.output / (label + '.ppm')
        self.request('capture ' + json.dumps(path.as_posix()))
        with Image.open(path) as source:
            return source.convert('RGB')

    def call(self, name, **arguments):
        if name == 'playback_trace':
            result = self.request(arguments['action'])
        elif name == 'send_command' and arguments['cid'] == 50005:
            result = self.request('play')
        else:
            raise ValueError('Unsupported Marcus capture command: ' + name)
        return {'content': [{'type': 'text', 'text': encode(result)}]}

    def close(self):
        if not self.process.stdin.closed:
            try:
                self.process.stdin.close()
            except OSError:
                pass
        if self.process.poll() is None:
            try:
                self.process.wait(timeout=20)
            except subprocess.TimeoutExpired:
                self.process.kill()
                self.process.wait(timeout=5)
        self.reader.join(timeout=5)
        self.process.stdout.close()
        self.log.close()


def capabilities(player, mode):
    return {'seek_images': mode == 'seek', 'frame_commits': mode == 'playback',
            'presentation_images': mode == 'playback',
            'sound_requests': mode == 'playback' and player != 'marcus',
            'mixed_pcm': mode == 'playback', 'voice_positions': False,
            'actual_display_times': False, 'audio_tail_state': player != 'marcus',
            'frame_commit_semantics': 'host update after authored evaluation' if player == 'marcus' else 'FCmdRender frame presentation callback',
            'capture_scope': 'headless C API' if player == 'marcus' else
                             'headless SDL Studio' if player == 'sdl' else 'original desktop process'}


def scenes(session):
    """Seek pass only: enumerate each scene's actual stored frame range."""
    count = session.state()['movie_scene_count']
    if count <= 0:
        raise RuntimeError('Movie has no scenes')
    result = []
    for index in range(count):
        first = session.seek(index + 1, 1)
        if not first['success']:
            raise RuntimeError('Cannot select scene ' + str(index + 1))
        state = first['state']
        result.append({'scene': index, 'first_frame': state['scene_first_frame'],
                       'last_frame': state['scene_last_frame']})
    return result


def seek_capture(store, run, session, output, player, viewport):
    ranges = scenes(session)
    for scene in ranges:
        store.insert('scenes', run_id=run, **scene)
    sequence = 0
    text_observations = []
    for scene in ranges:
        for frame in range(scene['first_frame'], scene['last_frame'] + 1):
            result = session.seek(scene['scene'] + 1, frame - scene['first_frame'] + 1)
            if not result['success'] or result['state']['error_stack_depth']:
                raise RuntimeError('Seek failed: ' + encode(result))
            label = 'scene-%04d-frame-%06d' % (scene['scene'], frame)
            image = session.capture(label)
            if player != 'marcus':
                if not (0 <= viewport[0] < viewport[2] <= image.width and
                        0 <= viewport[1] < viewport[3] <= image.height):
                    raise RuntimeError('Movie canvas outside client image')
                image = image.crop(viewport)
            path = output / (label + '-canvas.png')
            image.save(path)
            state = result['state']
            text_observations.append({'scene': scene['scene'], 'frame': frame,
                'count': state.get('scene_textbox_count'), 'boxes': state.get('scene_textboxes')})
            store.insert('frames', run_id=run, sequence=sequence, scene=scene['scene'], frame=frame,
                         timestamp_ns=None, state_json=encode(state))
            store.image(run, sequence, scene['scene'], frame, path, seek_result=state)
            sequence += 1
    return {'expected_frames': sequence, 'scope': 'absolute seeks, not continuous presentation',
            'text_observations': text_observations}


def playback_capture(store, run, session, output, audio_module, helper, limit, viewport=(48, 100, 592, 406)):
    # record() uses normal Play, process-loopback PCM and every observed commit.
    # No scene traversal/seek screenshot warm-up happens in this process.
    _, report = audio_module.record(session, helper, output / 'playback', limit,
                                   capture_images=not isinstance(session, MarcusSession), viewport=viewport)
    if isinstance(session, MarcusSession):
        report['sound_tail_complete'] = None
        report['sound_tail_observation'] = 'Unavailable in the Marcus C API; recording observes output until the player stops'
        (output / 'playback/playback.json').write_text(encode(report), encoding='utf-8')
    session_trace = report['trace']
    store.trace(run, session_trace)
    ranges = {}
    for frame in session_trace['frames']:
        ranges[frame['scene']] = {'scene': frame['scene'], 'first_frame': frame['first'], 'last_frame': frame['last']}
    observed = [dict(row) for row in store.db.execute('SELECT * FROM frames WHERE run_id=? ORDER BY sequence', (run,))]
    if (len(ranges) != report['expected_scene_count'] or sorted(ranges) != list(range(report['expected_scene_count'])) or
            not frame_coverage([ranges[key] for key in sorted(ranges)], observed)):
        raise RuntimeError('Continuous playback did not visit every authored frame in order')
    if session_trace.get('error'):
        raise RuntimeError(session_trace['error'])
    if session_trace.get('presentation_error') or not session_trace.get('presentations'):
        raise RuntimeError('Presentation capture failed, overflowed, or produced no images')
    path = output / 'playback/audio.wav'
    store.insert('audio', run_id=run, path=store.relative(path), sha256=digest(path),
                 anchor_sample=report['audio_anchor_sample'], metadata_json=encode(report))
    return report


def import_presentations(store, run, trace):
    for index, event in enumerate(trace.get('presentations', [])):
        data = dict(event)
        data['presentation_scope'] = trace.get('presentation_scope', 'headless C API host update')
        data['sampling_period_ns'] = trace.get('presentation_sampling_period_ns')
        path = Path(data.pop('path'))
        scene, frame = data.pop('scene'), data.pop('frame')
        data['timestamp_ns'] = data.pop('monotonic_ns')
        png = path.with_suffix('.png')
        from PIL import Image
        if path.exists():
            with Image.open(path) as source:
                source.save(png)
            path.unlink()
        store.image(run, index, scene, frame, png, **data)


def verify_text_acid(player, mode, report):
    if player == 'marcus':
        return # This adapter does not expose native text-box state yet.
    if mode == 'playback':
        frames = {(f['scene'], f['frame']): f['monotonic_ns'] for f in report['trace']['frames']}
        if (report['expected_scene_count'] != 4 or (2, 3) not in frames or (2, 4) not in frames or
                frames[2, 4] - frames[2, 3] < 1000000000):
            raise RuntimeError('Text Acid did not exercise its four pages and scrolling hold')
        return
    observations = report.get('text_observations', [])
    counts = [12, 11, 4, 4]
    if {item['scene'] for item in observations} != set(range(4)):
        raise RuntimeError('Text Acid page missing')
    for item in observations:
        page = item['scene']
        if item['count'] != counts[page]:
            raise RuntimeError('Text Acid box missing')
        if player != 'sdl':
            continue
        boxes = item.get('boxes') or []
        if len(boxes) != counts[page] or not boxes[0]['text'].startswith('TEXT ACID / '):
            raise RuntimeError('Text Acid content missing')
        if page == 0:
            for prefix, alignment in [('LEFT', 'left'), ('CENTER', 'center'), ('RIGHT', 'right')]:
                if not any(b['text'].startswith(prefix) and b['align'] == alignment for b in boxes):
                    raise RuntimeError('Text Acid paragraph alignment changed')
            if not any(b['text'].startswith('Normal |') and b['first_font'] == 'Arial' and
                       b['last_font'] == 'Courier New' for b in boxes):
                raise RuntimeError('Text Acid mixed font run missing')
        if page == 1:
            for label, first, end in [('Frames 1-19', 1, 20), ('Frames 20-39', 20, 40), ('Frames 40-60', 40, 61)]:
                if not any(b['text'].strip() == label and b['visible'] == (first <= item['frame'] < end) for b in boxes):
                    raise RuntimeError('Text Acid lifetime boundary changed')
        if page == 3 and not any(b['text'].startswith('Missing font sample:') and b['visible'] for b in boxes):
            raise RuntimeError('Text Acid lost the missing-font sample')


def verify_actor_audio(path, report=None):
    """Require both authored fixture tones; this is not PCM/timing equivalence."""
    peaks = {600: 0.0, 1300: 0.0}
    with wave.open(str(path), 'rb') as source:
        if source.getsampwidth() != 2 or source.getnchannels() < 1 or source.getframerate() <= 2600:
            raise RuntimeError('Actor audio contract requires PCM16 above 2600 Hz')
        channels, rate = source.getnchannels(), source.getframerate()
        stop = source.getnframes()
        if report is not None:
            first_scene = [f['monotonic_ns'] for f in report['trace']['frames'] if f['scene'] == 0]
            if not first_scene:
                raise RuntimeError('Actor audio contract needs first-scene frame observations')
            # The second scene swaps child mappings. Its ordinary cue must not
            # conceal a missing motion tone in the first scene.
            stop = min(stop, report['audio_anchor_sample'] +
                       round((max(first_scene) - report['command_before_ns']) * rate / 1e9))
        size = rate // 10
        window = [.5 - .5 * math.cos(2 * math.pi * i / (size - 1)) for i in range(size)]
        coefficients = {tone: 2 * math.cos(2 * math.pi * tone / rate) for tone in peaks}
        while source.tell() + size <= stop:
            samples = array.array('h', source.readframes(size))
            if sys.byteorder != 'little':
                samples.byteswap()
            if len(samples) != size * channels:
                break
            weighted = [samples[i * channels] * window[i] for i in range(size)]
            for tone, coefficient in coefficients.items():
                previous = older = 0.0
                for sample in weighted:
                    current = sample + coefficient * previous - older
                    older, previous = previous, current
                power = max(0, previous * previous + older * older - coefficient * previous * older)
                peaks[tone] = max(peaks[tone], 2 * math.sqrt(power) / sum(window))
    if any(value <= 50 for value in peaks.values()):
        raise RuntimeError('Actor audio fixture lost an ordinary or motion tone: ' + encode(peaks))
    return {'window_ms': 100, 'minimum_pcm16_amplitude': 50, 'end_sample': stop,
            'first_scene_only': report is not None,
            'tone_peak_amplitudes': {str(tone): value for tone, value in peaks.items()}}


def verify_feature_observations(case, player, mode, report):
    if case.get('text_acid_contract'):
        verify_text_acid(player, mode, report)
    if case.get('text_contract') and mode == 'seek' and player != 'marcus':
        observations = report.get('text_observations', [])
        if not observations or any(item['count'] != 1 for item in observations):
            raise RuntimeError('Text fixture must load exactly one text box in every inspected frame')
        if player == 'sdl':
            contract = case['text_contract']
            for item in observations:
                boxes = item.get('boxes') or []
                visible = contract['first_frame'] <= item['frame'] < contract['max_frame']
                if (len(boxes) != 1 or not boxes[0]['text'].startswith(contract['prefix']) or
                        boxes[0]['visible'] != visible):
                    raise RuntimeError('Text content or visibility does not match the authored fixture')

    minimum_hold = case.get('text_contract', {}).get('minimum_scroll_hold_ms')
    if minimum_hold and mode == 'playback' and player != 'marcus':
        frames = {(frame['scene'], frame['frame']): frame['monotonic_ns']
                  for frame in report['trace']['frames']}
        first = case['text_contract']['first_frame']
        for scene in range(report['expected_scene_count']):
            before, after = frames.get((scene, first)), frames.get((scene, first + 1))
            if before is None or after is None or after - before < minimum_hold * 1000000:
                raise RuntimeError('Scrolling fixture advanced before its credit hold was observed')
    if player == 'marcus' and mode == 'playback':
        animated = set(case['features']) & {'transition-fade', 'transition-dissolve', 'transition-black'}
        if animated and not any(event['transition'] for event in report['trace'].get('presentations', [])):
            raise RuntimeError('Declared transition fixture produced no observed transition images')


def add_comparison(store, reference, candidate, component, metrics, policy):
    return store.insert('comparisons', reference_run=reference, candidate_run=candidate,
                        component=component, status=metrics['status'], policy_json=encode(policy),
                        metrics_json=encode(metrics))


def compare_runs(store, reference, candidate, *, frame_tolerance_ms=25, sample_tolerance=0,
                 expected_audio='audible', reference_repeatability=None):
    a, b = [store.db.execute('SELECT * FROM runs WHERE id=?', (run,)).fetchone() for run in (reference, candidate)]
    if any(run is None for run in (a, b)):
        raise ValueError('Unknown run ID')
    if reference == candidate:
        raise ValueError('A run cannot establish its own reference')
    if any(a[key] != b[key] for key in ('suite_id', 'case_name', 'mode', 'preparation')):
        raise ValueError('Comparisons need the same case, mode and preparation in one suite')
    original = store.db.execute('SELECT reference FROM cases WHERE suite_id=? AND name=?',
                                (a['suite_id'], a['case_name'])).fetchone()[0]
    if a['player'] != original:
        raise ValueError('Reference run must use the original declared by the case: ' + original)
    policy = {'version': 2, 'frame_tolerance_ms': frame_tolerance_ms,
              'sample_tolerance': sample_tolerance, 'pixel_tolerance': 0,
              'global_alignment_samples': 0, 'expected_audio': expected_audio}
    def result(component, **metrics):
        if (reference_repeatability and component in reference_repeatability and
                reference_repeatability[component] != 'matched'):
            metrics.setdefault('observed_status', metrics['status'])
            metrics['status'] = 'inconclusive'
            metrics['reference_repeatability'] = reference_repeatability[component]
            metrics['reason'] = 'Original reference did not repeat within this comparison policy'
        add_comparison(store, reference, candidate, component, metrics, policy)
        return metrics
    if a['status'] != 'complete' or b['status'] != 'complete':
        return result('completion', status='inconclusive', reference_status=a['status'], candidate_status=b['status'])
    rows = lambda table, run: [dict(row) for row in store.db.execute(
        'SELECT * FROM ' + table + ' WHERE run_id=? ORDER BY sequence', (run,))]
    left, right = rows('frames', reference), rows('frames', candidate)
    for run in (reference, candidate):
        artifacts = [(row['path'], row['artifact_sha256']) for row in rows('images', run)]
        artifacts += [(row['path'], row['sha256']) for row in store.db.execute('SELECT * FROM audio WHERE run_id=?', (run,))]
        for path, expected in artifacts:
            try:
                valid = digest(store.resolve(path)) == expected
            except OSError:
                valid = False
            if not valid:
                return result('artifact_integrity', status='inconclusive', path=path, reason='Capture artifact missing or changed')
    sequence_match = bool(left) and [(f['scene'], f['frame']) for f in left] == [(f['scene'], f['frame']) for f in right]
    results = [result('frame_sequence', status='matched' if sequence_match else 'different',
                      reference_frames=len(left), candidate_frames=len(right))]
    if a['mode'] == 'seek':
        from PIL import Image, ImageChops, ImageStat
        li, ri = rows('images', reference), rows('images', candidate)
        mismatches, first = 0, None
        max_error, changed_pixels = 0, 0
        comparable = sequence_match and len(li) == len(left) and len(ri) == len(right)
        for x, y in zip(li, ri):
            if (x['scene'], x['frame'], x['width'], x['height']) != (y['scene'], y['frame'], y['width'], y['height']):
                comparable = False
                continue
            if x['pixel_sha256'] == y['pixel_sha256']:
                continue
            mismatches += 1
            with Image.open(store.resolve(x['path'])) as src, Image.open(store.resolve(y['path'])) as dst:
                delta = ImageChops.difference(src.convert('RGB'), dst.convert('RGB'))
                red, green, blue = delta.split()
                mask = ImageChops.lighter(ImageChops.lighter(red, green), blue)
                changed_pixels += delta.width * delta.height - mask.histogram()[0]
                max_error = max(max_error, max(high for low, high in delta.getextrema()))
                if first is None:
                    path = store.resolve(y['path']).with_name('first-difference.png')
                    delta.save(path)
                    first = {'scene': x['scene'], 'frame': x['frame'], 'sequence': x['sequence'],
                             'bbox': delta.getbbox(), 'mean_absolute_channel_error': sum(ImageStat.Stat(delta).mean) / 3,
                             'reference': x['path'], 'candidate': y['path'], 'difference': store.relative(path)}
        results.append(result('seek_pixels', status='different' if mismatches else 'matched' if comparable else 'inconclusive',
                              comparable=comparable, different_images=mismatches, changed_pixels=changed_pixels,
                              max_channel_error=max_error, first_difference=first))
    else:
        caps = [json.loads(run['capabilities_json']) for run in (a, b)]
        timing = timing_metrics(left, right, round(frame_tolerance_ms * 1000000))
        markers = [cap.get('frame_commit_semantics') for cap in caps]
        timing['markers'] = markers
        if not all(markers) or markers[0] != markers[1]:
            timing['observed_status'] = timing['status']
            timing['status'] = 'inconclusive'
            timing['reason'] = 'Equivalent authored-frame timing markers have not been established'
        results.append(result('frame_timing', **timing))
        if all(cap['sound_requests'] for cap in caps):
            events = [[json.loads(row['event_json']) for row in rows('sound_events', run)] for run in (reference, candidate)]
            results.append(result('sound_requests', status='matched' if events[0] == events[1] else 'different',
                                  reference_events=len(events[0]), candidate_events=len(events[1])))
        else:
            results.append(result('sound_requests', status='inconclusive', reason='Sound request observer unavailable'))
        audio_rows = [store.db.execute('SELECT * FROM audio WHERE run_id=?', (run,)).fetchone() for run in (reference, candidate)]
        if all(audio_rows):
            sys.path.insert(0, str(ROOT / 'tools/retail_oracle'))
            from audio_compare import pcm, compare_pcm
            arrays = [pcm(store.resolve(row['path']))[row['anchor_sample']:] for row in audio_rows]
            metrics = compare_pcm(*arrays, sample_tolerance=sample_tolerance)
            if expected_audio == 'silent' and metrics['status'] == 'unverified_silence':
                metrics['status'] = 'matched'
                metrics['scope'] = 'explicitly expected silence; audio-device operation not established'
            results.append(result('mixed_pcm', **metrics))
        else:
            results.append(result('mixed_pcm', status='inconclusive', reason='Missing mixed audio recording'))
        # GDI blits and host presents occur at different boundaries and rates.
        # Time-aligned reviewer pairs are diagnostic, not an exact parity proof.
        results.append(result('continuous_pixels', status='inconclusive',
                              reason='Presentation images require temporal alignment; raw image sequences are not equivalent',
                              reference_images=len(rows('images', reference)),
                              candidate_images=len(rows('images', candidate))))
    status = 'different' if any(r['status'] == 'different' for r in results) else (
        'inconclusive' if any(r['status'] != 'matched' for r in results) else 'matched')
    return result('seek_fidelity' if a['mode'] == 'seek' else 'playback_fidelity', status=status)


def compare_suite(store, suite, *, frame_tolerance_ms=25, sample_tolerance=0):
    row = store.db.execute('SELECT manifest_json FROM suites WHERE id=?', (suite,)).fetchone()
    if row is None:
        raise ValueError('Unknown suite ID')
    manifest = json.loads(row[0])
    all_runs = [dict(row) for row in store.db.execute('SELECT * FROM runs WHERE suite_id=? ORDER BY id', (suite,))]
    for case in manifest['cases']:
        for mode in sorted({r['mode'] for r in all_runs if r['case_name'] == case['name']}):
            relevant = [r for r in all_runs if r['case_name'] == case['name'] and r['mode'] == mode]
            references = [r for r in relevant if r['player'] == case['reference']]
            # Recompare originals first using this policy, then gate attribution.
            for candidate in sorted(relevant, key=lambda r: r['player'] != case['reference']):
                same_preparation = [r for r in references if r['preparation'] == candidate['preparation']]
                reference = next((r for r in same_preparation if r['repetition'] == candidate['repetition']), None)
                if candidate['player'] == case['reference']:
                    reference = same_preparation[0] if same_preparation and candidate != same_preparation[0] else None
                if not reference:
                    continue
                stability = {}
                if candidate['player'] != case['reference'] and len(same_preparation) > 1:
                    baseline = same_preparation[0]['id']
                    for repeated in same_preparation[1:]:
                        for row in store.db.execute('SELECT component,status FROM comparisons WHERE reference_run=? AND candidate_run=? '
                            'AND id IN (SELECT max(id) FROM comparisons GROUP BY reference_run,candidate_run,component)',
                            (baseline, repeated['id'])):
                            if stability.get(row['component'], 'matched') == 'matched':
                                stability[row['component']] = row['status']
                    if stability.get('completion') or stability.get('artifact_integrity'):
                        stability.update({key: 'inconclusive' for key in
                            ('frame_sequence', 'seek_pixels', 'frame_timing', 'sound_requests', 'mixed_pcm')})
                compare_runs(store, reference['id'], candidate['id'], frame_tolerance_ms=frame_tolerance_ms,
                             sample_tolerance=sample_tolerance, expected_audio=case.get('expected_audio', 'audible'),
                             reference_repeatability=stability)
    return review(store, suite)


def preserve_warmup_presentations(trace, process_directory, warmup_directory):
    """Move each unique raw image once while retaining all event timestamps."""
    moved = {}
    process_directory = Path(process_directory).resolve()
    for index, event in enumerate(trace.get('presentations', [])):
        source = Path(event['path']).resolve()
        if not source.is_relative_to(process_directory):
            raise RuntimeError('Warm-up image is outside its capture directory')
        if source not in moved:
            target = Path(warmup_directory) / ('presentation-%d.ppm' % index)
            source.replace(target)
            moved[source] = target
        event['path'] = str(moved[source])


def maintain_captures(store, args):
    keep = getattr(args, 'keep_captures', 2)
    if keep:
        try:
            retained = prune(store, keep, getattr(args, 'pin_suite', ()),
                             getattr(args, 'sdl_cache', None), apply=True)
            if retained['removed']:
                print('Pruned %d superseded capture suites; metadata retained' % len(retained['removed']))
        except (OSError, RuntimeError, ValueError) as error:
            print('Capture retention deferred: ' + str(error), file=sys.stderr)


def run_suite(args):
    if os.name != 'nt':
        raise ValueError('Live capture currently requires Windows; review and SQLite are portable')
    session_module, compare_module, audio_module, v3dmm_module = legacy()
    manifest = load_manifest(args.manifest)
    selected = [case for case in manifest['cases'] if not args.case or case['name'] in args.case]
    if not selected or (args.case and set(args.case) - {case['name'] for case in selected}):
        raise ValueError('Unknown or empty case selection')
    players = args.players.split(',')
    if len(set(players)) != len(players) or set(players) - set(PLAYERS):
        raise ValueError('Invalid player selection')
    modes = args.modes.split(',')
    if len(set(modes)) != len(modes) or set(modes) - {'seek', 'playback'}:
        raise ValueError('Invalid capture modes')
    preparations = args.preparations.split(',')
    if len(set(preparations)) != len(preparations) or set(preparations) - {'fresh-process', 'replay'}:
        raise ValueError('Invalid preparations; organiser preparation is not implemented yet')
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    if 'sdl' in players:
        args.sdl_cache = shared_cache(output, getattr(args, 'sdl_cache', None))
    store = Store(output / 'playback.sqlite')
    suite = datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%SZ-') + uuid.uuid4().hex[:8]
    destination = output / suite
    destination.mkdir()
    try:
        revision = subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=ROOT, text=True).strip()
        dirty = subprocess.check_output(['git', 'status', '--porcelain'], cwd=ROOT, text=True).splitlines()
        store.insert('suites', id=suite, started=datetime.now(timezone.utc).isoformat(),
                     manifest_json=encode(json.loads(args.manifest.read_text(encoding='utf-8'))),
                     environment_json=encode({'os': platform.platform(), 'python': platform.python_version(),
                         'machine': platform.machine(), 'os_file_cache': 'uncontrolled', 'run_order': players,
                         'viewport': args.viewport, 'notes': args.environment_notes, 'working_tree': dirty,
                         'sdl_package_cache': str(args.sdl_cache.resolve()) if getattr(args, 'sdl_cache', None) else 'per-run',
                         'audio_format': '48000 Hz stereo signed 16-bit', 'capture_version': 2}), source_revision=revision)
        maintain_captures(store, args)
        require_space(output, getattr(args, 'min_free_mib', 4096))
        product, iso_manifest = session_module.prepare_iso(args.iso, output / 'iso-package')
        resources = args.resources or product / '3DMOVIE.CHK'
        content = args.content_dir or product
        identities = {'iso': iso_manifest}
        if args.sdl:
            identities['sdl_sha256'] = digest(args.sdl)
        if args.marcus:
            identities['marcus_capture_sha256'] = digest(args.marcus)
        identities['bridge_sha256'] = digest(args.bridge / 'retail-oracle.dll')
        identities['capture_sources'] = {name: digest(ROOT / 'tools' / name) for name in
            ('playback_regression.py', 'playback_store.py', 'playback_retention.py', 'marcus_regression.cpp', 'presentation_capture.h',
             'marcus_capture_audio.h')}
        content_inputs = {str(resources.resolve()): digest(resources)}
        for path in content.iterdir():
            if path.is_file() and path.suffix.lower() == '.3cn':
                content_inputs[str(path.resolve())] = digest(path)
        identities['marcus_content'] = content_inputs
        created = []
        errors = []
        for case in selected:
            source_hash = store.add_case(suite, case)
            for player in players:
                if player not in case['players']:
                    continue
                for mode in modes:
                    for repetition, preparation in itertools.product(range(args.repeat), preparations):
                        require_space(output, getattr(args, 'min_free_mib', 4096))
                        directory = destination / case['name'] / player / mode / preparation / str(repetition)
                        directory.mkdir(parents=True)
                        caps = capabilities(player, mode)
                        run = store.insert('runs', suite_id=suite, case_name=case['name'], player=player,
                            mode=mode, preparation=preparation, repetition=repetition,
                            artifact_dir=store.relative(directory), capabilities_json=encode(caps))
                        created.append(run)
                        active = None
                        report = {}
                        try:
                            print('%s %s %s %s repeat=%d' % (case['name'], player, mode, preparation, repetition), flush=True)
                            movie = case['path']
                            # A .3mm can also exercise the actual patched v3dmm runtime
                            # using a dependency-free v1 envelope. Never convert VMMs.
                            if player == 'v3dmm' and movie.suffix.lower() == '.3mm':
                                wrapped = directory / 'stock.vmm'
                                wrapped.write_bytes(b'v3dmm\x01' + struct.pack('<IIH', 1, 0, 4) + b'3dmm' + movie.read_bytes())
                                movie = wrapped
                            if player == 'retail':
                                if movie.suffix.lower() != '.3mm':
                                    store.finish(run, 'unsupported', error='Retail cannot open VMM containers')
                                    continue
                                active = session_module.RetailSession(args.bridge, product, movie, directory / 'process')
                            elif player == 'v3dmm':
                                if not args.v3dmm:
                                    raise ValueError('--v3dmm distribution is required')
                                active = v3dmm_module.V3dmmSession(args.bridge, product, movie, directory / 'process',
                                                                  args.v3dmm, case['libraries'])
                            elif player == 'sdl':
                                if not args.sdl:
                                    raise ValueError('--sdl executable is required')
                                active = compare_module.SDLSession(args.sdl, args.iso, movie, directory / 'process',
                                    args.sdl_cache if getattr(args, 'sdl_cache', None) else directory / 'cache',
                                    audio=mode == 'playback' or preparation == 'replay')
                            else:
                                if not args.marcus:
                                    raise ValueError('--marcus capture executable is required')
                                active = MarcusSession(args.marcus, movie, resources, content, directory / 'process',
                                    args.viewport[2] - args.viewport[0], args.viewport[3] - args.viewport[1])
                            if preparation == 'replay':
                                _, warmup = audio_module.record(active, args.bridge / 'oracle-audio-capture.exe',
                                                                 directory / 'warmup', args.max_seconds)
                                # The adapter reuses its presentation filenames
                                # on Begin. Preserve the first pass before replay.
                                preserve_warmup_presentations(warmup['trace'], directory / 'process', directory / 'warmup')
                                (directory / 'warmup/playback.json').write_text(encode(warmup), encoding='utf-8')
                            if mode == 'seek':
                                report = seek_capture(store, run, active, directory, player, args.viewport)
                            else:
                                report = playback_capture(store, run, active, directory, audio_module,
                                                          args.bridge / 'oracle-audio-capture.exe', args.max_seconds, args.viewport)
                            verify_feature_observations(case, player, mode, report)
                            if case.get('actor_audio_contract') and mode == 'playback':
                                metrics = verify_actor_audio(directory / 'playback/audio.wav', report)
                                (directory / 'playback/actor-audio.json').write_text(encode(metrics), encoding='utf-8')
                            provenance = dict(identities)
                            if player == 'marcus':
                                provenance['capture_features'] = active.capture_features
                            if hasattr(active, 'validate_assets'):
                                provenance['runtime'] = active.validate_assets()
                            active.close()
                            active = None
                            if mode == 'playback':
                                import_presentations(store, run, report['trace'])
                            if digest(case['path']) != source_hash:
                                raise RuntimeError('Source movie changed')
                            provenance['source_unchanged'] = True
                            store.finish(run, 'complete', provenance=provenance)
                        except Exception as error:
                            errors.append(str(error))
                            # Failed captures still retain their last observations
                            # in SQLite as well as their original artifact journal.
                            partial_path = directory / 'playback/playback.json'
                            if partial_path.is_file() and not store.db.execute('SELECT 1 FROM frames WHERE run_id=?', (run,)).fetchone():
                                partial = json.loads(partial_path.read_text(encoding='utf-8'))
                                if 'trace' in partial:
                                    store.trace(run, partial['trace'])
                            failure_provenance = dict(identities)
                            if active is not None and hasattr(active, 'capture_features'):
                                failure_provenance['capture_features'] = active.capture_features
                            store.finish(run, 'error', error=str(error), provenance=failure_provenance)
                            print('  ERROR: ' + str(error), flush=True)
                        finally:
                            if active:
                                try:
                                    active.close()
                                except Exception as error:
                                    errors.append(str(error))
                                    store.finish(run, 'error', error='Cleanup failed: ' + str(error))
                            if case['path'].is_file() and digest(case['path']) != source_hash:
                                errors.append('Source movie changed: ' + case['name'])
                                store.finish(run, 'error', error=errors[-1])
        compare_suite(store, suite, frame_tolerance_ms=args.frame_tolerance_ms, sample_tolerance=args.sample_tolerance)
        source_ok = all(digest(case['path']) == case['sha256'] for case in selected)
        iso_ok = all(digest(output / 'iso-package' / path) == expected for path, expected in iso_manifest['files'].items())
        content_ok = all(Path(path).is_file() and digest(path) == expected for path, expected in content_inputs.items())
        if not source_ok or not iso_ok or not content_ok:
            errors.append('Movie or extracted ISO inputs changed')
            store.db.execute("UPDATE runs SET status='error',error='Input integrity failure' WHERE suite_id=?", (suite,))
            store.db.execute("UPDATE comparisons SET status='inconclusive' WHERE candidate_run IN (SELECT id FROM runs WHERE suite_id=?)", (suite,))
            store.db.commit()
        summary = review(store, suite)
        summary['integrity'] = {'movies_unchanged': source_ok, 'iso_resources_unchanged': iso_ok,
                                'marcus_content_unchanged': content_ok}
        (destination / 'summary.json').write_text(encode(summary), encoding='utf-8')
        (output / 'latest.json').write_text(encode({'suite': suite, 'database': str(store.path),
                                                   'summary': str(destination / 'summary.json')}), encoding='utf-8')
        print('Suite: ' + suite)
        print('Database: ' + str(store.path))
        print('Status: %s; runs: %d complete, %d incomplete; comparisons: %d different, %d inconclusive' % (
            summary['status'], sum(r['status'] == 'complete' for r in summary['runs']),
            sum(r['status'] != 'complete' for r in summary['runs']),
            sum(c['status'] == 'different' for c in summary['comparisons']),
            sum(c['status'] == 'inconclusive' for c in summary['comparisons'])))
        maintain_captures(store, args)
        if errors:
            return 2
        return 0 if summary['status'] == 'matched' else 1
    except BaseException:
        store.db.execute("UPDATE runs SET status='interrupted' WHERE suite_id=? AND status='running'", (suite,))
        store.db.commit()
        raise
    finally:
        store.close()


def review(store, suite=None):
    if suite is None:
        row = store.db.execute('SELECT id FROM suites ORDER BY started DESC LIMIT 1').fetchone()
        if row is None:
            raise ValueError('Database contains no suites')
        suite = row[0]
    runs = [dict(row) for row in store.db.execute('SELECT * FROM run_review WHERE suite_id=? ORDER BY id', (suite,))]
    comparisons = [dict(row) for row in store.db.execute('SELECT * FROM latest_comparison_review WHERE suite_id=? ORDER BY id', (suite,))]
    covered = {row[0] for row in store.db.execute('SELECT DISTINCT feature FROM feature_coverage WHERE suite_id=?', (suite,))}
    aggregate = [c for c in comparisons if c['component'] in ('seek_fidelity', 'playback_fidelity', 'completion', 'artifact_integrity')]
    status = 'different' if any(c['status'] == 'different' for c in aggregate) else 'inconclusive'
    if runs and all(r['status'] == 'complete' for r in runs) and aggregate and all(c['status'] == 'matched' for c in aggregate):
        # A suite containing only references or unpaired cases is not verified.
        comparison_ids = [c['id'] for c in aggregate]
        paired = set()
        for comparison in comparison_ids:
            row = store.db.execute('SELECT reference_run,candidate_run FROM comparisons WHERE id=?', (comparison,)).fetchone()
            paired.update(row)
        if all(r['id'] in paired for r in runs) and any(c['reference_player'] != c['candidate_player'] for c in aggregate):
            status = 'matched'
    if any(r['status'] != 'complete' for r in runs):
        status = 'inconclusive'
    return {'suite': suite, 'database': str(store.path), 'status': status, 'runs': runs,
            'comparisons': comparisons, 'features_in_manifest': sorted(covered),
            'features_without_cases': sorted(set(FEATURES) - covered)}


def prepare(args):
    session_module, _, _, _ = legacy()
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    manifest_path = output / 'suite.json'
    if manifest_path.exists():
        raise ValueError('Refusing to replace an existing suite manifest')
    product, _ = session_module.prepare_iso(args.iso, output / 'iso-package')
    cases = []
    basic = ['background', 'camera-cuts', 'actor-animation', 'routes', 'costume', 'props', 'textures', '3d-text', 'transition-cut']
    for name, option, features in (
        ('features', None, basic),
        ('actor-events', '--events', basic + ['rotation', 'scale', 'stretch', 'freeze', 'entrance-exit']),
        ('dissolve', '--dissolve', basic + ['transition-dissolve']),
        ('sampled-audio', '--audio', basic + ['sampled-speech', 'sampled-effects']),
        ('actor-audio', '--actor-audio', basic + ['sampled-speech', 'sampled-effects', 'actor-sounds', 'motion-sounds', 'sound-queues']),
        ('text-acid', '--text-acid', ['background', '2d-text', 'scrolling-text', 'transition-cut']),
        ('static-text', '--static-text', basic + ['2d-text']),
        ('scrolling-text', '--scrolling-text', basic + ['2d-text', 'scrolling-text']),
    ):
        movie = output / (name + '.3mm')
        command = [str(args.fixture.resolve()), str(movie), str(product / 'BKGDS.3CN'),
                   str(product / '3DMOVIE.CHK'), str(product / 'TMPLS.3CN')]
        if option:
            command.append(option)
        with (output / (name + '.log')).open('w', encoding='utf-8') as log:
            subprocess.run(command, stdout=log, stderr=log, check=True, timeout=60)
        cases.append({'name': name, 'path': movie.name, 'sha256': digest(movie),
                      'reference': 'retail', 'features': features, 'players': list(PLAYERS),
                      'expected_audio': 'audible' if option in ('--audio', '--actor-audio') else 'silent'})
        if name == 'text-acid':
            cases[-1]['text_acid_contract'] = 1
        if name == 'actor-audio':
            cases[-1]['actor_audio_contract'] = 1
        if name in ('static-text', 'scrolling-text'):
            cases[-1]['text_contract'] = {'first_frame': 3, 'max_frame': 25 if name == 'static-text' else 31,
                                         'prefix': 'Static words' if name == 'static-text' else 'Scrolling credits'}
            if name == 'scrolling-text':
                cases[-1]['text_contract']['minimum_scroll_hold_ms'] = 1000
    manifest_path.write_text(json.dumps({'schema': 1, 'cases': cases}, indent=2), encoding='utf-8')
    print('Prepared ' + str(manifest_path))
    return 0


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest='command', required=True)
    init = commands.add_parser('prepare', help='Generate the initial feature corpus using the existing native fixture author')
    init.add_argument('--fixture', type=Path, required=True)
    init.add_argument('--iso', type=Path, required=True)
    init.add_argument('--output', type=Path, required=True)
    run = commands.add_parser('run', help='Capture players serially and compare in SQLite')
    for name in ('manifest', 'iso', 'bridge', 'output'):
        run.add_argument('--' + name, type=Path, required=True)
    for name in ('sdl', 'marcus', 'v3dmm', 'resources', 'content-dir'):
        run.add_argument('--' + name, type=Path)
    run.add_argument('--players', default=','.join(DEFAULT_PLAYERS), help='Defaults to retail,v3dmm,sdl; include marcus explicitly when needed')
    run.add_argument('--modes', default='seek,playback')
    run.add_argument('--sdl-cache', type=Path,
                     help='Shared package cache (default: OUTPUT/sdl-cache); processes/settings/working movies remain private')
    run.add_argument('--keep-captures', type=int, default=2,
                     help='Keep latest N completed captures per case/player/mode/preparation/repetition, plus latest failures; 0 disables pruning')
    run.add_argument('--pin-suite', action='append', default=[], help='Protect an additional suite from retention pruning')
    run.add_argument('--min-free-mib', type=int, default=4096, help='Required free space before preparing inputs and each capture (default: 4096 MiB)')
    run.add_argument('--preparations', default='fresh-process', help='Comma-separated fresh-process,replay; replay first plays the whole movie in the same process')
    run.add_argument('--case', action='append')
    run.add_argument('--repeat', type=int, default=2, help='Independent fresh processes, including original repeatability')
    run.add_argument('--max-seconds', type=int, default=120)
    run.add_argument('--viewport', type=int, nargs=4, default=[48, 100, 592, 406], help='Exact movie canvas in the original 640x480 client; no scaling')
    run.add_argument('--frame-tolerance-ms', type=float, default=25)
    run.add_argument('--sample-tolerance', type=int, default=0)
    run.add_argument('--environment-notes', default='', help='GPU/driver, audio device, MIDI mapper/bank, fonts and other controlled settings')
    report = commands.add_parser('review', help='Print the most recent suite or an explicit suite ID')
    report.add_argument('--database', type=Path, required=True)
    report.add_argument('--suite')
    retention = commands.add_parser('prune', help='Preview or remove superseded raw captures, preserving metadata and observations')
    retention.add_argument('--database', type=Path, required=True)
    retention.add_argument('--keep-captures', type=int, default=2)
    retention.add_argument('--pin-suite', action='append', default=[])
    retention.add_argument('--sdl-cache', type=Path)
    retention.add_argument('--apply', action='store_true')
    comparison = commands.add_parser('compare', help='Append a new comparison policy without replaying captured observations')
    comparison.add_argument('--database', type=Path, required=True)
    comparison.add_argument('--reference', type=int, required=True)
    comparison.add_argument('--candidate', type=int, required=True)
    comparison.add_argument('--frame-tolerance-ms', type=float, default=25)
    comparison.add_argument('--sample-tolerance', type=int, default=0)
    comparison.add_argument('--expected-audio', choices=('audible', 'silent'), default='audible')
    suite_comparison = commands.add_parser('compare-suite', help='Recompare a captured suite, including original repeatability')
    suite_comparison.add_argument('--database', type=Path, required=True)
    suite_comparison.add_argument('--suite')
    suite_comparison.add_argument('--frame-tolerance-ms', type=float, default=25)
    suite_comparison.add_argument('--sample-tolerance', type=int, default=0)
    args = parser.parse_args()
    try:
        if args.command == 'prepare':
            return prepare(args)
        if args.command == 'run':
            if (args.repeat < 1 or args.keep_captures < 0 or args.min_free_mib < 0 or not 1 <= args.max_seconds <= 3595 or not math.isfinite(args.frame_tolerance_ms) or
                    args.frame_tolerance_ms < 0 or not 0 <= args.sample_tolerance <= 32767 or
                    not (0 <= args.viewport[0] < args.viewport[2] and 0 <= args.viewport[1] < args.viewport[3])):
                parser.error('Invalid repetition, limit, tolerance or viewport')
            for key, value in vars(args).items():
                if isinstance(value, Path):
                    setattr(args, key, value.resolve())
            return run_suite(args)
        if not args.database.is_file():
            raise ValueError('Database does not exist: ' + str(args.database))
        store = Store(args.database)
        try:
            if args.command == 'prune':
                print(json.dumps(prune(store, args.keep_captures, args.pin_suite, args.sdl_cache, args.apply), indent=2))
                return 0
            if args.command in ('compare', 'compare-suite'):
                if (not math.isfinite(args.frame_tolerance_ms) or args.frame_tolerance_ms < 0 or
                        not 0 <= args.sample_tolerance <= 32767):
                    parser.error('Invalid comparison tolerance')
                if args.command == 'compare-suite':
                    suite_id = args.suite or review(store)['suite']
                    metrics = compare_suite(store, suite_id, frame_tolerance_ms=args.frame_tolerance_ms,
                                            sample_tolerance=args.sample_tolerance)
                    print(encode({'suite': suite_id, 'status': metrics['status'], 'comparisons': len(metrics['comparisons'])}))
                else:
                    metrics = compare_runs(store, args.reference, args.candidate,
                        frame_tolerance_ms=args.frame_tolerance_ms, sample_tolerance=args.sample_tolerance,
                        expected_audio=args.expected_audio)
                    print(json.dumps(metrics, indent=2))
                return 0 if metrics['status'] == 'matched' else 1
            print(json.dumps(review(store, args.suite), indent=2))
        finally:
            store.close()
        return 0
    except (ValueError, OSError, RuntimeError, ImportError, subprocess.SubprocessError) as error:
        print(str(error), file=sys.stderr)
        return 2


if __name__ == '__main__':
    raise SystemExit(main())
