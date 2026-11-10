"""Portable checks that the audio comparator never hides missing/reordered sound."""
import sys
sys.dont_write_bytecode = True
import pathlib
import unittest
import numpy as np
import threading
from types import SimpleNamespace
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1] / 'tools/retail_oracle'))
from audio_compare import compare_pcm, Capture, OracleError


class PCMComparison(unittest.TestCase):
    def test_preplay_requires_observed_zero_pcm(self):
        capture = object.__new__(Capture)
        capture.meter_changed = threading.Condition()
        capture.process = SimpleNamespace(poll=lambda: None)
        capture.meter = {'quiet_frames': 12000, 'peak': 0}
        result = capture.wait_for_quiet(timeout=0)
        self.assertEqual(result['threshold'], 0)
        capture.meter = {'quiet_frames': 0, 'peak': 1}
        with self.assertRaises(OracleError):
            capture.wait_for_quiet(timeout=0)

    def setUp(self):
        self.signal = np.zeros((24000, 2), dtype=np.int16)
        self.signal[2000:6000] = np.random.default_rng(1).integers(-2000, 2001, (4000, 2), dtype=np.int16)
        self.signal[12000:16000] = np.random.default_rng(2).integers(-2000, 2001, (4000, 2), dtype=np.int16)

    def test_identical(self):
        self.assertEqual(compare_pcm(self.signal, self.signal)['status'], 'matched')

    def test_silence_is_not_evidence(self):
        empty = np.zeros((1000, 2), dtype=np.int16)
        self.assertEqual(compare_pcm(empty, empty)['status'], 'unverified_silence')
        self.assertEqual(compare_pcm(self.signal, np.zeros_like(self.signal))['status'], 'different')

    def test_global_alignment_is_explicit(self):
        delayed = np.pad(self.signal, ((40, 0), (0, 0)))
        self.assertEqual(compare_pcm(self.signal, delayed)['status'], 'different')
        result = compare_pcm(self.signal, delayed, alignment_frames=80)
        self.assertEqual(result['status'], 'matched')
        self.assertEqual(result['global_alignment_frames'], 40)

    def test_missing_or_shifted_second_cue_fails(self):
        changed = self.signal.copy()
        changed[12000:] = 0
        self.assertEqual(compare_pcm(self.signal, changed, alignment_frames=80)['status'], 'different')
        changed[12100:16100] = self.signal[12000:16000]
        self.assertEqual(compare_pcm(self.signal, changed, alignment_frames=80)['status'], 'different')

    def test_channel_swaps_and_trailing_sound_fail(self):
        self.assertEqual(compare_pcm(self.signal, self.signal[:, ::-1])['status'], 'different')
        changed = np.pad(self.signal, ((0, 100), (0, 0)))
        changed[-1] = 100
        self.assertEqual(compare_pcm(self.signal, changed)['status'], 'different')

    def test_sample_tolerance_is_explicit(self):
        changed = self.signal.copy(); changed[2000, 0] += 1
        self.assertEqual(compare_pcm(self.signal, changed)['status'], 'different')
        self.assertEqual(compare_pcm(self.signal, changed, sample_tolerance=1)['status'], 'matched')


if __name__ == '__main__': unittest.main()
