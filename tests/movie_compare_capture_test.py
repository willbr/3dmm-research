"""Headless checks for isolated retail expansion staging."""
import sys
import tempfile
import unittest
from pathlib import Path

sys.dont_write_bytecode = True
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
from movie_compare_capture import reference_content, digest


class ReferenceContentTest(unittest.TestCase):
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
