"""Opt-in Windows calibration: capture owned wave/MIDI children and exclude another PID."""
import sys
sys.dont_write_bytecode = True
import argparse
import json
import pathlib
import subprocess
import tempfile
import time
import numpy as np
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1] / 'tools/retail_oracle'))
from audio_compare import Capture, pcm


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--bridge', type=pathlib.Path, required=True)
    parser.add_argument('--output', type=pathlib.Path, required=True)
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    output = pathlib.Path(tempfile.mkdtemp(prefix='run-', dir=args.output.resolve()))
    children, logs, captures = [], [], []
    report = {}
    def child(mode, frequency):
        log = (output / (mode+str(frequency)+'.log')).open('w'); logs.append(log)
        process = subprocess.Popen([str((args.bridge / 'oracle-audio-probe.exe').resolve()), mode, str(frequency)],
            stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=log, text=True, creationflags=subprocess.CREATE_NO_WINDOW)
        children.append(process)
        assert process.stdout.readline().strip() == 'ready'
        return process
    try:
        a, b = child('wave', 600), child('wave', 1300)
        for name, process in (('target', a), ('other', b)):
            capture = Capture(args.bridge / 'oracle-audio-capture.exe', process.pid, output / (name+'.wav'), 15)
            captures.append(capture)
        for capture in captures:
            assert capture.ready.get('source_format') == 'float', 'Rebuild the deterministic float capture helper'
            quiet = capture.wait_for_quiet()
            assert quiet['threshold'] == 0 and quiet['quiet_frames'] >= 12000
        for process in (a, b): process.stdin.write('p'); process.stdin.flush()
        for process in (a, b): assert process.stdout.readline().strip() == 'played'
        time.sleep(.3)
        for capture in captures:
            result = capture.finish()
            samples = pcm(capture.path)
            assert not np.any(samples[:12000]), 'Idle capture contains conversion noise before either tone starts'
            assert np.count_nonzero(samples) > 48000, 'Wave loopback captured silence'
            spectrum = np.abs(np.fft.rfft(samples[:, 0].astype(np.float64)))
            frequencies = np.fft.rfftfreq(len(samples), 1/48000)
            power = lambda hz: float(np.max(spectrum[np.abs(frequencies-hz) < 2]))
            desired, rejected = (600, 1300) if capture.path.stem == 'target' else (1300, 600)
            assert power(desired) > power(rejected)*100, 'Audio from the other PID leaked into capture'
            report[capture.path.stem] = {**result, 'wanted_tone': desired, 'rejected_tone': rejected,
                'wanted_power': power(desired), 'rejected_power': power(rejected)}
            capture.close()
        captures.clear()
        midi = child('midi', 440)
        capture = Capture(args.bridge / 'oracle-audio-capture.exe', midi.pid, output / 'midi.wav', 15)
        captures.append(capture)
        time.sleep(.5)
        midi.stdin.write('p'); midi.stdin.flush()
        assert midi.stdout.readline().strip() == 'played', 'Native MIDI mapper is unavailable'
        time.sleep(.3)
        result = capture.finish()
        samples = pcm(capture.path)
        assert np.count_nonzero(samples) > 1000, 'Native MIDI synthesis was not included in process loopback'
        report['midi'] = {**result, 'audible_samples': int(np.count_nonzero(samples))}
        capture.close(); captures.clear()
    finally:
        for capture in captures: capture.close()
        for process in children:
            if process.poll() is None:
                process.stdin.write('q'); process.stdin.flush()
                try: process.wait(timeout=5)
                except subprocess.TimeoutExpired: process.kill(); process.wait(timeout=5)
        for log in logs: log.close()
        (output / 'report.json').write_text(json.dumps(report, indent=2))
    print(json.dumps({'artifacts': str(output), 'wave_pid_isolation': 'passed', 'native_midi_capture': 'passed'}, indent=2))


if __name__ == '__main__': main()
