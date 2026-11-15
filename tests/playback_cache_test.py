"""Playback cache checks using synthetic observations; never launch a player."""
import sys
sys.dont_write_bytecode = True
import contextlib
import io
import json
import tempfile
import wave
from pathlib import Path
from types import SimpleNamespace
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
from playback_cache import CaptureCache, CaptureInputs
from playback_store import Store, digest
import playback_regression as harness
try:
    from PIL import Image
except ImportError:
    Image = None


@unittest.skipIf(Image is None, 'Pillow is required for synthetic images')
class Cache(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.root = Path(self.temporary.name)
        self.output = self.root / 'output'
        self.product = self.output / 'iso-package/product'
        self.product.mkdir(parents=True)
        for name in ('3DMOVIE.EXE', '3DMOVIE.CHK', 'BKGDS.3CN'):
            (self.product / name).write_bytes(name.encode())
        self.iso = self.root / 'retail.iso'
        self.iso.write_bytes(b'ISO identity')
        self.bridge = self.root / 'bridge'
        self.bridge.mkdir()
        for name in ('retail-oracle.dll', 'retail-oracle-launch.exe', 'oracle-audio-capture.exe'):
            (self.bridge / name).write_bytes(name.encode())
        self.sdl = self.root / 'sdl/studio.exe'
        self.marcus = self.root / 'marcus/capture.exe'
        for path in (self.sdl, self.marcus):
            path.parent.mkdir()
            path.write_bytes(b'executable')
            path.with_suffix('.dll').write_bytes(b'runtime dependency')
        self.movie = self.root / 'fixture.3mm'
        self.movie.write_bytes(b'movie')
        self.manifest = self.root / 'suite.json'
        self.manifest.write_text(json.dumps({'schema': 1, 'cases': [{
            'name': 'fixture', 'path': self.movie.name, 'sha256': digest(self.movie),
            'reference': 'retail', 'players': ['retail', 'sdl', 'marcus'],
            'features': ['background'], 'expected_audio': 'silent'}]}))
        self.iso_manifest = {'iso_sha256': digest(self.iso), 'files': {
            'product/' + p.name: digest(p) for p in self.product.iterdir()}}
        self.content = self.root / 'content'
        self.content.mkdir()
        (self.content / 'BKGDS.3CN').write_bytes(b'Marcus content')
        self.args = SimpleNamespace(manifest=self.manifest, iso=self.iso, bridge=self.bridge,
            output=self.output, resources=self.product / '3DMOVIE.CHK', content_dir=self.content,
            sdl=self.sdl, marcus=self.marcus, v3dmm=None, players='retail,sdl', modes='seek',
            preparations='fresh-process', repeat=1, max_seconds=120, viewport=[48, 100, 592, 406],
            frame_tolerance_ms=25, sample_tolerance=0, environment_notes='first revision',
            capture_environment='controlled device/fonts', keep_captures=0, case=None,
            min_free_mib=4096, cache_only=False, force_capture=False)
        self.launches = []
        self.case = harness.load_manifest(self.manifest)['cases'][0]

    def tearDown(self):
        self.temporary.cleanup()

    def inputs(self):
        return CaptureInputs(self.args, self.iso_manifest, self.args.resources, self.content)

    def key(self, player='retail', mode='seek', preparation='fresh-process', repetition=0):
        return self.inputs().key(self.case, player, mode, preparation, repetition)[0]

    def suite(self):
        owner = self
        class FakeSession:
            def __init__(self, player):
                owner.launches.append(player)
            def close(self):
                pass
        def seek(store, run, session, directory, player, viewport):
            store.insert('scenes', run_id=run, scene=0, first_frame=1, last_frame=1)
            store.insert('frames', run_id=run, sequence=0, scene=0, frame=1,
                         timestamp_ns=None, state_json='{}')
            path = directory / 'image.png'
            Image.new('RGB', (3, 2), (10, 20, 30)).save(path)
            store.image(run, 0, 0, 1, path)
            return {}
        modules = (SimpleNamespace(prepare_iso=lambda *a, **kw: (self.product, self.iso_manifest),
                                   RetailSession=lambda *a: FakeSession('retail')),
                   SimpleNamespace(SDLSession=lambda *a, **kw: FakeSession('sdl')), None, None)
        with patch.object(harness, 'legacy', return_value=modules), \
             patch.object(harness, 'os', SimpleNamespace(name='nt')), \
             patch.object(harness, 'seek_capture', side_effect=seek), \
             patch.object(harness, 'require_space') as space, contextlib.redirect_stdout(io.StringIO()) as log:
            status = harness.run_suite(self.args)
        summary = json.loads(Path(json.loads((self.output / 'latest.json').read_text())['summary']).read_text())
        return status, summary, space.call_count, log.getvalue()

    def test_second_suite_uses_cache_without_sessions_or_space_for_new_captures(self):
        status, first, guards, _ = self.suite()
        self.assertEqual(status, 0)
        self.assertEqual(self.launches, ['retail', 'sdl'])
        self.assertEqual(guards, 2)
        status, second, guards, log = self.suite()
        self.assertEqual(status, 0)
        self.assertEqual(self.launches, ['retail', 'sdl'])
        self.assertEqual(guards, 0)
        self.assertEqual(second['cache'], {'hits': 2, 'misses': 0, 'forced': False})
        self.assertEqual(log.count('CACHE HIT'), 2)
        with contextlib.closing(Store(self.output / 'playback.sqlite')) as store:
            original = store.db.execute('SELECT * FROM images WHERE run_id=?', (first['runs'][0]['id'],)).fetchone()
            reused = store.db.execute('SELECT * FROM images WHERE run_id=?', (second['runs'][0]['id'],)).fetchone()
            self.assertEqual(original['path'], reused['path'])
            provenance = json.loads(store.db.execute('SELECT provenance_json FROM runs WHERE id=?',
                                   (second['runs'][0]['id'],)).fetchone()[0])
            self.assertEqual(provenance['cache_reuse']['source_run'], first['runs'][0]['id'])
            self.assertNotEqual(first['runs'][0]['id'], second['runs'][0]['id'])
            self.assertTrue(second['comparisons'])

    def test_rebuilt_sdl_only_recaptures_sdl_and_recomparison_does_not_capture(self):
        self.suite()
        self.sdl.write_bytes(b'rebuilt SDL')
        _, summary, _, _ = self.suite()
        self.assertEqual(self.launches, ['retail', 'sdl', 'sdl'])
        self.assertEqual(summary['cache']['hits'], 1)
        self.args.environment_notes = 'another code revision'
        self.args.frame_tolerance_ms = 99
        self.args.sample_tolerance = 4
        _, summary, guards, _ = self.suite()
        self.assertEqual(guards, 0)
        self.assertEqual(summary['cache']['hits'], 2)

    def test_force_and_cache_only_are_explicit(self):
        self.args.cache_only = True
        status, summary, guards, log = self.suite()
        self.assertEqual(status, 2)
        self.assertEqual(guards, 0)
        self.assertEqual(self.launches, [])
        self.assertEqual(summary['cache']['misses'], 2)
        self.assertIn('no players launched', log)
        self.args.cache_only = False
        self.suite()
        self.args.force_capture = True
        _, summary, guards, _ = self.suite()
        self.assertEqual(self.launches, ['retail', 'sdl', 'retail', 'sdl'])
        self.assertTrue(summary['cache']['forced'])
        self.assertEqual(guards, 2)

    def test_missing_changed_and_failed_artifacts_are_not_reused(self):
        _, summary, _, _ = self.suite()
        with contextlib.closing(Store(self.output / 'playback.sqlite')) as store:
            row = store.db.execute('SELECT path FROM images WHERE run_id=?', (summary['runs'][0]['id'],)).fetchone()
            store.resolve(row[0]).write_bytes(b'tampered image')
            store.finish(summary['runs'][1]['id'], 'interrupted')
        self.args.cache_only = True
        _, summary, _, _ = self.suite()
        self.assertEqual(summary['cache']['hits'], 0)
        self.assertEqual(self.launches, ['retail', 'sdl'])

    def test_repetitions_are_independent_and_aliases_keep_original_sources(self):
        _, first, _, _ = self.suite()
        self.args.repeat = 2
        _, second, _, _ = self.suite()
        self.assertEqual(second['cache']['hits'], 2)
        self.assertEqual(self.launches, ['retail', 'sdl', 'retail', 'sdl'])
        _, third, _, _ = self.suite()
        self.assertEqual(third['cache']['hits'], 4)
        with contextlib.closing(Store(self.output / 'playback.sqlite')) as store:
            roots = [json.loads(r[0])['cache_reuse']['source_run'] for r in store.db.execute(
                'SELECT provenance_json FROM runs WHERE suite_id=? ORDER BY id', (third['suite'],))]
            self.assertEqual(roots[0], first['runs'][0]['id'])
            self.assertNotEqual(roots[0], roots[1])

    def test_capture_settings_and_player_dependencies_invalidate_only_relevant_keys(self):
        retail, sdl, marcus = [self.key(p) for p in ('retail', 'sdl', 'marcus')]
        (self.content / 'BKGDS.3CN').write_bytes(b'changed Marcus content')
        self.assertEqual(self.key(), retail)
        self.assertEqual(self.key('sdl'), sdl)
        self.assertNotEqual(self.key('marcus'), marcus)
        (self.bridge / 'retail-oracle.dll').write_bytes(b'changed bridge')
        self.assertNotEqual(self.key(), retail)
        self.assertEqual(self.key('sdl'), sdl)
        playback = self.key(mode='playback')
        seek = self.key()
        self.args.max_seconds += 1
        self.assertEqual(self.key(), seek)
        self.assertNotEqual(self.key(mode='playback'), playback)
        self.assertNotEqual(self.key(repetition=1), self.key())
        self.assertNotEqual(self.key(preparation='replay'), self.key())
        self.args.viewport[2] -= 1
        self.assertNotEqual(self.key(), seek)
        seek = self.key()
        self.args.capture_environment = 'different device/fonts'
        self.assertNotEqual(self.key(), seek)

    def test_inputs_modified_during_capture_are_not_cacheable(self):
        inputs = self.inputs()
        inputs.key(self.case, 'sdl', 'seek', 'fresh-process', 0)
        self.assertTrue(inputs.unchanged())
        self.sdl.write_bytes(b'changed during capture')
        self.assertFalse(inputs.unchanged())

    def test_playback_audio_and_sound_observations_are_shared_and_checked(self):
        _, summary, _, _ = self.suite()
        with contextlib.closing(Store(self.output / 'playback.sqlite')) as store:
            source = summary['runs'][0]['id']
            key, inputs = self.inputs().key(self.case, 'retail', 'playback', 'fresh-process', 0)
            store.db.execute("UPDATE runs SET mode='playback' WHERE id=?", (source,))
            store.db.execute('DELETE FROM capture_cache WHERE run_id=?', (source,))
            store.db.commit()
            cache = CaptureCache(store)
            cache.record(source, key, inputs)
            self.assertIsNone(cache.lookup(key))
            directory = store.resolve(store.db.execute('SELECT artifact_dir FROM runs WHERE id=?', (source,)).fetchone()[0])
            audio = directory / 'audio.wav'
            with wave.open(str(audio), 'wb') as wav:
                wav.setparams((2, 2, 48000, 0, 'NONE', 'not compressed'))
                wav.writeframes(b'\0' * 16)
            store.insert('audio', run_id=source, path=store.relative(audio), sha256=digest(audio),
                         anchor_sample=2, metadata_json='{"recorded":true}')
            store.insert('sound_events', run_id=source, sequence=0, scene=0, frame=1,
                         timestamp_ns=7, event_json='{"sound":42}')
            row = cache.lookup(key)
            self.assertIsNotNone(row)
            suite = 'copy'
            store.insert('suites', id=suite, started='later', manifest_json='{}', environment_json='{}', source_revision='test')
            store.add_case(suite, self.case)
            alias = cache.reuse(row, suite)
            self.assertEqual(store.db.execute('SELECT anchor_sample FROM audio WHERE run_id=?', (alias,)).fetchone()[0], 2)
            self.assertEqual(store.db.execute('SELECT event_json FROM sound_events WHERE run_id=?', (alias,)).fetchone()[0], '{"sound":42}')
            audio.unlink()
            self.assertIsNone(cache.lookup(key))

    def test_legacy_unkeyed_and_incomplete_rows_are_not_guessed(self):
        _, summary, _, _ = self.suite()
        with contextlib.closing(Store(self.output / 'playback.sqlite')) as store:
            store.db.execute('DELETE FROM capture_cache')
            store.db.commit()
        self.args.cache_only = True
        _, summary, _, _ = self.suite()
        self.assertEqual(summary['cache']['hits'], 0)
        self.assertEqual(summary['cache']['misses'], 2)
        self.assertEqual(self.launches, ['retail', 'sdl'])


if __name__ == '__main__':
    unittest.main()
