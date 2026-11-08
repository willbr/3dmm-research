"""Record owned children through process loopback and compare their actual PCM.

Two passes run at normal speed. No acceleration, sound substitution, gain
normalization, per-cue shifting or time warping is performed.
"""
import ctypes
from ctypes import wintypes as w
import hashlib
import json
import pathlib
import queue
import subprocess
import threading
import time
import uuid
import wave
import numpy as np
from client import OracleError


def pcm(path):
    with wave.open(str(path), 'rb') as stream:
        if (stream.getnchannels(), stream.getsampwidth(), stream.getframerate(), stream.getcomptype()) != (2, 2, 48000, 'NONE'):
            raise OracleError('Audio capture must be 48 kHz stereo signed 16-bit PCM')
        return np.frombuffer(stream.readframes(stream.getnframes()), dtype='<i2').reshape(-1, 2)


def compare_pcm(left, right, *, alignment_frames=0, sample_tolerance=0):
    """Exact comparison by default; one explicitly bounded global shift only."""
    left, right = np.asarray(left, dtype=np.int16), np.asarray(right, dtype=np.int16)
    if left.ndim != 2 or right.ndim != 2 or left.shape[1] != 2 or right.shape[1] != 2:
        raise ValueError('Expected stereo arrays')
    if alignment_frames < 0 or not 0 <= sample_tolerance <= 32767:
        raise ValueError('Invalid audio tolerance')
    energies = [np.flatnonzero(np.any(value != 0, axis=1)) for value in (left, right)]
    report = {'reference_frames': len(left), 'sdl_frames': len(right), 'rate': 48000,
        'sample_tolerance': sample_tolerance, 'allowed_global_alignment_frames': alignment_frames,
        'reference_sha256': hashlib.sha256(left.tobytes()).hexdigest(),
        'sdl_sha256': hashlib.sha256(right.tobytes()).hexdigest(),
        'reference_audible_frames': len(energies[0]), 'sdl_audible_frames': len(energies[1]),
        'reference_first_audible_frame': int(energies[0][0]) if len(energies[0]) else None,
        'sdl_first_audible_frame': int(energies[1][0]) if len(energies[1]) else None}
    if not len(energies[0]) and not len(energies[1]):
        report['status'] = 'unverified_silence'
        return report
    lag = 0
    if alignment_frames and all(len(value) for value in energies):
        # Align on the initial audible material, then compare the entire stream.
        # An omitted/repeated later cue remains a difference.
        onset = min(int(energies[0][0]), int(energies[1][0]))
        start = max(alignment_frames, onset)
        end = min(len(left), len(right)) - alignment_frames
        end = min(end, start + 48000 * 2)
        if end > start:
            channel = int(np.argmax(np.sum(np.abs(left[start:end].astype(np.int32)), axis=0)))
            def score(offset, stride=8):
                delta = left[start:end:stride, channel].astype(np.float32) - right[start+offset:end+offset:stride, channel]
                return float(np.dot(delta, delta))
            step = 8
            coarse = min(range(-alignment_frames, alignment_frames + 1, step), key=score)
            lag = min(range(max(-alignment_frames, coarse-step), min(alignment_frames, coarse+step)+1),
                      key=lambda offset: score(offset, 1))
    # Place both complete streams on one axis. Padding preserves any sound
    # near the leading/trailing edges rather than discarding unmatched samples.
    left_start, right_start = max(lag, 0), max(-lag, 0)
    count = max(left_start + len(left), right_start + len(right))
    changed, squared, peak = 0, 0., 0
    first_mismatch = None
    for offset in range(0, count, 48000):
        stop = min(count, offset + 48000)
        a, b = np.zeros((stop-offset, 2), dtype=np.int32), np.zeros((stop-offset, 2), dtype=np.int32)
        for source, origin, target in ((left, left_start, a), (right, right_start, b)):
            low, high = max(offset, origin), min(stop, origin + len(source))
            if low < high:
                target[low-offset:high-offset] = source[low-origin:high-origin]
        delta = np.abs(a-b)
        mask = np.any(delta > sample_tolerance, axis=1)
        changed += int(np.count_nonzero(mask))
        if first_mismatch is None and np.any(mask):
            first_mismatch = offset + int(np.flatnonzero(mask)[0])
        squared += float(np.sum(delta.astype(np.float64)**2))
        peak = max(peak, int(np.max(delta)))
    report.update(status='matched' if changed == 0 else 'different', global_alignment_frames=lag,
        different_stereo_frames=changed, compared_frames=count, first_different_frame=first_mismatch,
        max_sample_error=peak, rms_sample_error=(squared / max(1, count*2))**.5)
    return report


def kernel():
    api = ctypes.WinDLL('kernel32', use_last_error=True)
    api.OpenProcess.argtypes = [w.DWORD, w.BOOL, w.DWORD]
    api.OpenProcess.restype = w.HANDLE
    api.GetProcessTimes.argtypes = [w.HANDLE] + [ctypes.POINTER(w.FILETIME)]*4
    api.CreateEventW.argtypes = [ctypes.c_void_p, w.BOOL, w.BOOL, w.LPCWSTR]
    api.CreateEventW.restype = w.HANDLE
    api.SetEvent.argtypes = [w.HANDLE]
    api.CloseHandle.argtypes = [w.HANDLE]
    return api


class Capture:
    def __init__(self, executable, pid, destination, seconds):
        self.api = kernel()
        process = self.api.OpenProcess(0x1000, False, pid)
        times = [w.FILETIME() for _ in range(4)]
        try:
            if not process or not self.api.GetProcessTimes(process, *(ctypes.byref(item) for item in times)):
                raise OracleError('Cannot verify owned audio target')
            created = (times[0].dwHighDateTime << 32) | times[0].dwLowDateTime
        finally:
            if process: self.api.CloseHandle(process)
        name = 'Local\\3DMMForever-Audio-' + uuid.uuid4().hex
        self.event = self.api.CreateEventW(None, True, False, name)
        if not self.event: raise OracleError('Cannot create capture stop event')
        self.path = destination
        self.log = destination.with_suffix('.capture.log').open('w', encoding='utf-8')
        self.process = None
        try:
            self.process = subprocess.Popen([str(executable.resolve()), str(pid), str(created),
                str(destination.resolve()), name, str(seconds)], stdout=subprocess.PIPE, stderr=self.log,
                text=True, encoding='utf-8', creationflags=subprocess.CREATE_NO_WINDOW)
            self.replies = queue.Queue()
            def read():
                for line in self.process.stdout:
                    self.replies.put(json.loads(line))
                self.replies.put(None)
            self.reader = threading.Thread(target=read, daemon=True)
            self.reader.start()
            self.ready = self.replies.get(timeout=15)
            if not self.ready or not self.ready.get('ready') or self.ready['created'] != created:
                raise OracleError('Process loopback did not initialize; inspect ' + self.log.name)
        except BaseException:
            self.close()
            raise

    def finish(self):
        self.api.SetEvent(self.event)
        result = self.replies.get(timeout=10)
        code = self.process.wait(timeout=10)
        if not result or not result.get('complete') or code:
            raise OracleError('Audio capture incomplete; inspect ' + self.log.name)
        result['identity'] = self.ready
        return result

    def close(self):
        if self.event: self.api.SetEvent(self.event)
        if self.process:
            try: self.process.wait(timeout=12)
            except subprocess.TimeoutExpired:
                self.process.kill() # Only the helper created by this instance.
                self.process.wait(timeout=5)
        if self.event:
            self.api.CloseHandle(self.event)
            self.event = None
        self.log.close()


def record(session, helper, output, limit):
    output.mkdir(parents=True, exist_ok=False)
    first = session.seek(1, 1)
    if not first['success']: raise OracleError('Cannot rewind movie before audio playback')
    original = session.state()
    native = hasattr(session, 'oracle')
    pid = session.identity['pid'] if native else session.studio.process.pid
    trace = lambda action: (session.oracle.request(5 if action == 'begin' else 6) if native else
        json.loads(session.studio.call('playback_trace', action=action)['content'][0]['text']) if action == 'end' else
        session.studio.call('playback_trace', action=action))
    trace('begin')
    capture = Capture(helper, pid, output / 'audio.wav', limit + 5)
    report = {'observations': [], 'complete': False, 'expected_scene_count': original['movie_scene_count']}
    try:
        time.sleep(.5) # Capture startup discontinuities precede the movie.
        report['command_before_ns'] = time.perf_counter_ns()
        if native: session.oracle.command(50005)
        else: session.studio.call('send_command', cid=50005)
        report['command_after_ns'] = time.perf_counter_ns()
        deadline = time.monotonic() + limit
        advanced = False
        while time.monotonic() < deadline:
            before = time.perf_counter_ns()
            state = session.state()
            after = time.perf_counter_ns()
            (output / 'progress.json').write_text(json.dumps({key: state[key] for key in
                ('movie_scene', 'scene_frame', 'scene_last_frame', 'movie_playing')}))
            advanced |= state['movie_playing'] or state['movie_scene'] > 0 or state['scene_frame'] > state['scene_first_frame']
            report['observations'].append({'before_ns': before, 'after_ns': after, **{key: state[key] for key in
                ('movie_scene', 'movie_scene_count', 'scene_frame', 'scene_first_frame', 'scene_last_frame', 'movie_playing', 'error_stack_depth')}})
            if state['error_stack_depth']: raise OracleError('Movie playback reported an engine error')
            if not state['movie_playing'] and (advanced or after - report['command_after_ns'] > 1000000000):
                report['complete'] = state['movie_scene'] + 1 == original['movie_scene_count'] and state['scene_frame'] == state['scene_last_frame']
                break
            time.sleep(.25) # Frequent bridge RPCs can starve the retail idle renderer.
        report['trace'] = trace('end')
        report['capture_end_before_ns'] = time.perf_counter_ns()
        while state['movie_sound_playing'] and time.monotonic() < deadline:
            time.sleep(.25)
            state = session.state()
        report['sound_tail_complete'] = not state['movie_sound_playing']
        if not report['sound_tail_complete']: raise OracleError('Movie sound tail exceeded the capture time limit')
        time.sleep(.25) # Drain the capture pipeline after the last movie voice.
        report['capture'] = capture.finish()
        if not report['complete']: raise OracleError('Playback stopped early or exceeded its time limit')
        if report['trace']['overflow']: raise OracleError('Continuous frame trace overflowed')
        if not report['trace']['frames']: raise OracleError('No continuous playback frame commits were observed')
        packets = [json.loads(line) for line in pathlib.Path(str(capture.path)+'.packets.jsonl').read_text().splitlines()]
        during = [packet for packet in packets if packet['qpc_100ns']*100 >= report['command_before_ns']]
        if any(packet['flags'] & 5 for packet in during):
            raise OracleError('Audio discontinuity or invalid timestamp during movie playback')
        # Both streams are anchored to the engine's first frame commit, using
        # WASAPI's QPC packet timestamp; command/RPC delays remain diagnostic.
        anchor = report['trace']['frames'][0]['monotonic_ns']
        valid = [packet for packet in packets if not packet['flags'] & 4 and packet['qpc_100ns']*100 <= anchor]
        if not valid: raise OracleError('No valid audio timestamp precedes the playback anchor')
        packet = valid[-1]
        sample = packet['sample_offset'] + round((anchor - packet['qpc_100ns']*100) * 48000 / 1e9)
        report['audio_anchor_sample'] = sample
        if sample < 0 or sample >= report['capture']['frames']:
            raise OracleError('Playback anchor is outside audio capture')
        data = pcm(capture.path)[sample:]
        return data.copy(), report
    except BaseException as error:
        report['error'] = str(error)
        raise
    finally:
        capture.close()
        (output / 'playback.json').write_text(json.dumps(report, indent=2))


def compare_playback(args, reference, output):
    from session import RetailSession
    from compare import SDLSession
    helper = args.bridge / 'oracle-audio-capture.exe'
    if not helper.is_file(): raise OracleError('Build oracle-audio-capture before running audio comparisons')
    output.mkdir()
    native, port = None, None
    report = {'scope': 'complete movie, normal continuous playback', 'status': 'error',
        'capture_backend': 'Windows process loopback, owned PID plus creation time',
        'helper_sha256': hashlib.sha256(helper.read_bytes()).hexdigest()}
    try:
        native = reference.relaunch(output / 'reference-session') if hasattr(reference, 'relaunch') else RetailSession(
            args.bridge, reference.product, reference.source, output / 'reference-session')
        print('Audio: reference, normal-speed continuous playback', flush=True)
        a, left = record(native, helper, output / 'reference', args.audio_max_seconds)
        report['reference_provenance'] = native.validate_assets()
        native.close(); native = None
        report['reference_repeatability'] = {'status': 'not_run'}
        if getattr(args, 'audio_repeat_reference', False):
            native = reference.relaunch(output / 'repeat-session') if hasattr(reference, 'relaunch') else RetailSession(
                args.bridge, reference.product, reference.source, output / 'repeat-session')
            print('Audio: reference repeatability pass', flush=True)
            repeated, repetition = record(native, helper, output / 'reference-repeat', args.audio_max_seconds)
            report['repeat_provenance'] = native.validate_assets()
            report['reference_repeatability'] = compare_pcm(a, repeated, alignment_frames=round(args.audio_alignment_ms*48),
                sample_tolerance=args.audio_sample_tolerance)
            report['reference_repeatability']['sound_events_match'] = left['trace']['sounds'] == repetition['trace']['sounds']
            if not report['reference_repeatability']['sound_events_match']:
                report['reference_repeatability']['status'] = 'different'
            native.close(); native = None
        port = SDLSession(args.sdl, args.iso, getattr(reference, 'bundle', reference.movie), output / 'sdl-session',
            args.output / 'sdl-cache', packs=getattr(reference, 'pack_root', None),
            pack_settings=getattr(reference, 'pack_settings', None), audio=True)
        print('Audio: SDL, normal-speed continuous playback', flush=True)
        b, right = record(port, helper, output / 'sdl', args.audio_max_seconds)
        report['pcm'] = compare_pcm(a, b, alignment_frames=round(args.audio_alignment_ms*48),
                                    sample_tolerance=args.audio_sample_tolerance)
        report['sound_events_match'] = left['trace']['sounds'] == right['trace']['sounds']
        report['sound_event_counts'] = {'reference': len(left['trace']['sounds']), 'sdl': len(right['trace']['sounds'])}
        keys = lambda trace: [(frame['scene'], frame['frame']) for frame in trace['frames']]
        report['frame_commits_match'] = keys(left['trace']) == keys(right['trace'])
        # Every frame in each declared scene range must be observed, not just
        # the final frame. Duplicate timeline refreshes are deduplicated.
        def coverage(value):
            trace = value['trace']
            expected = []
            ranges = {}
            for frame in trace['frames']: ranges[frame['scene']] = (frame['first'], frame['last'])
            for scene, (first, last) in sorted(ranges.items()):
                expected.extend((scene, frame) for frame in range(first, last+1))
            return list(sorted(ranges)) == list(range(value['expected_scene_count'])) and keys(trace) == expected
        report['reference_complete_frame_commits'] = coverage(left)
        report['sdl_complete_frame_commits'] = coverage(right)
        for label, value in (('reference', left), ('sdl', right)):
            frames = value['trace']['frames']
            report[label] = {'frames': len(frames), 'first_commit_ns': frames[0]['monotonic_ns'],
                'frame_span_ms': (frames[-1]['monotonic_ns'] - frames[0]['monotonic_ns']) / 1e6}
        report['status'] = report['pcm']['status'] if (report['sound_events_match'] and report['frame_commits_match'] and
            report['reference_complete_frame_commits'] and report['sdl_complete_frame_commits']) else 'different'
        if report['frame_commits_match']:
            a, b = left['trace']['frames'], right['trace']['frames']
            differences = [abs((x['monotonic_ns'] - a[0]['monotonic_ns']) -
                (y['monotonic_ns'] - b[0]['monotonic_ns'])) / 1e6 for x, y in zip(a, b)]
            report['frame_timing'] = {'max_difference_ms': max(differences),
                'tolerance_ms': args.frame_time_tolerance_ms,
                'different_frames': sum(value > args.frame_time_tolerance_ms for value in differences)}
            if report['frame_timing']['different_frames']: report['status'] = 'different'
        if report['reference_repeatability']['status'] not in ('not_run', 'matched'):
            report['status'] = 'unverified_reference_pcm'
    except Exception as error:
        report['error'] = str(error)
    finally:
        if port: port.close()
        if native: native.close()
        (output / 'report.json').write_text(json.dumps(report, indent=2))
    return report
