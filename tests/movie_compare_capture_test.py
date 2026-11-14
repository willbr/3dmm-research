"""Headless checks for isolated retail expansion staging."""
import sys
import tempfile
import unittest
import threading
from unittest.mock import patch
from pathlib import Path

sys.dont_write_bytecode = True
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
from movie_compare_capture import reference_content, digest
import movie_compare_capture as capture


class ReferenceContentTest(unittest.TestCase):
    def test_retail_runs_ahead_of_marcus(self):
        with tempfile.TemporaryDirectory(prefix='movie-compare-test-') as temporary:
            job = capture.Job(Path(temporary))
            job.total = 3
            finished = threading.Event()
            retail_order, marcus_order = [], []
            def retail(reference, job, index, position):
                retail_order.append(index)
                job.counts[0] = index + 1
                if index == 2:
                    finished.set()
            def marcus(cpu, job, index, position, failures):
                self.assertTrue(finished.wait(2), 'Retail must progress while Marcus waits')
                marcus_order.append(index)
                job.counts[1:] = [index + 1] * 4
            with patch.object(capture, 'capture_retail_frame', retail), patch.object(capture, 'capture_marcus_frame', marcus):
                failures, timings = capture.capture_frames(None, None, job, [(1, i, i) for i in range(1, 4)])
            self.assertEqual((retail_order, marcus_order), ([0, 1, 2], [0, 1, 2]))
            self.assertEqual(job.counts, [3] * 5)
            self.assertFalse(failures)
            self.assertIn('Retail_seconds', timings)
            self.assertEqual((job.output / 'status.txt').read_text().splitlines()[2], '3 3 3 3 3')

    def test_worker_failure_is_joined_and_reported(self):
        with tempfile.TemporaryDirectory(prefix='movie-compare-test-') as temporary:
            job = capture.Job(Path(temporary))
            with patch.object(capture, 'capture_retail_frame', side_effect=RuntimeError('native failure')), \
                 patch.object(capture, 'capture_marcus_frame'):
                with self.assertRaisesRegex(RuntimeError, 'native failure'):
                    capture.capture_frames(None, None, job, [(1, 1, 1)])
            self.assertFalse(any(t.name == 'retail capture' for t in threading.enumerate()))

    def test_snapshot_and_cache_identity(self):
        with tempfile.TemporaryDirectory(prefix='movie-compare-test-') as temporary:
            root = Path(temporary)
            product = root / 'iso' / '3D Movie Maker'
            product.mkdir(parents=True)
            (product / '3DMOVIE.EXE').write_bytes(b'original executable')
            (product / 'bkgds.3cn').write_bytes(b'original backgrounds')
            installed = root / 'installed'
            folder = installed / 'EXPANSIONS'
            folder.mkdir(parents=True)
            archive = folder / 'SCENEPACK1.3CN'
            archive.write_bytes(b'expansion scenes')
            (folder / 'untrusted.exe').write_bytes(b'never copy executable')
            cache = root / 'cache'
            shadow, manifest = reference_content(product, installed, cache)
            self.assertNotEqual(product, shadow)
            self.assertEqual((shadow / '3DMOVIE.EXE').read_bytes(), b'original executable')
            staged = shadow.parent / 'EXPANSIONS' / archive.name
            self.assertEqual(digest(staged), manifest[0]['sha256'])
            self.assertFalse((staged.parent / 'untrusted.exe').exists())
            self.assertTrue((shadow.parent / 'Users' / 'Oracle').is_dir())
            self.assertEqual(reference_content(product, installed, cache)[0], shadow)
            archive.write_bytes(b'updated expansion')
            updated, _ = reference_content(product, installed, cache)
            self.assertNotEqual(updated, shadow)
            self.assertEqual(staged.read_bytes(), b'expansion scenes')
            (product / 'bkgds.3cn').write_bytes(b'updated base')
            self.assertNotEqual(reference_content(product, installed, cache)[0], updated)

    def test_without_expansions(self):
        with tempfile.TemporaryDirectory(prefix='movie-compare-test-') as temporary:
            root = Path(temporary)
            product = root / 'original'
            self.assertEqual(reference_content(product, root / 'missing', root / 'cache'),
                             (product, []))
            self.assertFalse((root / 'cache').exists())


if __name__ == '__main__':
    unittest.main()
