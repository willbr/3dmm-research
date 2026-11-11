"""Regression-harness checks, using tiny independent images and observations.

These tests do not launch Marcus or simulate application UI.
"""
import sys
sys.dont_write_bytecode = True
import json
from pathlib import Path
import tempfile
import unittest
try:
    from PIL import Image
except ImportError:
    Image = None

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
from playback_store import Store, digest, frame_coverage, timing_metrics
from playback_regression import compare_runs, compare_suite, load_manifest, review, capabilities, verify_feature_observations, import_presentations


class Measurements(unittest.TestCase):
    @unittest.skipIf(Image is None, 'Pillow required')
    def test_repeated_presentations_reuse_pixels_without_losing_timestamps(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'frame.ppm'
            Image.new('RGB', (2, 1), (12, 34, 56)).save(path)
            observations = []
            class Sink:
                def image(self, run, sequence, scene, frame, image, **metadata):
                    self.assertions(image)
                    observations.append((sequence, metadata['timestamp_ns'], str(image)))
                @staticmethod
                def assertions(image):
                    with Image.open(image) as pixels:
                        assert pixels.getpixel((0, 0)) == (12, 34, 56)
            trace = {'presentations': [dict(path=str(path),scene=0,frame=3,monotonic_ns=t) for t in (10,20)]}
            import_presentations(Sink(), 1, trace)
            self.assertEqual([o[:2] for o in observations], [(0,10),(1,20)])
            self.assertEqual(observations[0][2], observations[1][2])
            self.assertFalse(path.exists())

    def test_checked_in_text_acid_manifest_is_hash_pinned(self):
        path = Path(__file__).parent / 'fixtures/textbox-acid.json'
        case = load_manifest(path)['cases'][0]
        self.assertEqual(case['text_acid_contract'], 1)
        self.assertEqual(case['reference'], 'retail')
        self.assertIn('scrolling-text', case['features'])


    def test_acid_requires_all_pages_boxes_and_credit_hold(self):
        case = {'features': ['2d-text', 'scrolling-text'], 'text_acid_contract': 1}
        report = {'text_observations': [{'scene': i, 'count': n} for i, n in enumerate((12, 11, 4, 4))]}
        verify_feature_observations(case, 'retail', 'seek', report)
        report['text_observations'][3]['count'] = 3
        with self.assertRaises(RuntimeError):
            verify_feature_observations(case, 'retail', 'seek', report)
        report['text_observations'].pop()
        with self.assertRaises(RuntimeError):
            verify_feature_observations(case, 'retail', 'seek', report)
        playback = {'expected_scene_count': 4, 'trace': {'frames': [
            {'scene': 2, 'frame': 3, 'monotonic_ns': 0},
            {'scene': 2, 'frame': 4, 'monotonic_ns': 166000000}]}}
        with self.assertRaises(RuntimeError):
            verify_feature_observations(case, 'retail', 'playback', playback)
        playback['trace']['frames'][1]['monotonic_ns'] = 2000000000
        verify_feature_observations(case, 'retail', 'playback', playback)


    def test_text_fixture_requires_content_and_visibility_boundaries(self):
        case = {'features': ['2d-text'], 'text_contract':
                {'first_frame': 3, 'max_frame': 25, 'prefix': 'Static words'}}
        observations = [{'scene': 0, 'frame': frame, 'count': 1,
                         'boxes': [{'text': 'Static words 1', 'visible': 3 <= frame < 25}]}
                        for frame in (2, 3, 24, 25)]
        report = {'text_observations': observations}
        verify_feature_observations(case, 'sdl', 'seek', report)
        observations[-1]['boxes'][0]['visible'] = True
        with self.assertRaises(RuntimeError):
            verify_feature_observations(case, 'sdl', 'seek', report)
        observations[-1]['boxes'][0]['visible'] = False
        observations[1]['boxes'][0]['text'] = ''
        with self.assertRaises(RuntimeError):
            verify_feature_observations(case, 'sdl', 'seek', report)
        observations[0]['count'] = 0
        with self.assertRaises(RuntimeError):
            verify_feature_observations(case, 'retail', 'seek', report)


    def test_scrolling_fixture_cannot_silently_skip_credits(self):
        case = {'features': ['2d-text', 'scrolling-text'],
                'text_contract': {'first_frame': 3, 'minimum_scroll_hold_ms': 1000}}
        report = {'expected_scene_count': 1, 'trace': {'frames': [
            {'scene': 0, 'frame': 3, 'monotonic_ns': 100},
            {'scene': 0, 'frame': 4, 'monotonic_ns': 1500000100}]}}
        verify_feature_observations(case, 'retail', 'playback', report)
        report['trace']['frames'][1]['monotonic_ns'] = 166000100
        with self.assertRaises(RuntimeError):
            verify_feature_observations(case, 'retail', 'playback', report)

    def test_declared_transition_needs_actual_transition_images(self):
        case = {'features': ['transition-dissolve']}
        with self.assertRaises(RuntimeError):
            verify_feature_observations(case, 'marcus', 'playback', {'trace': {'presentations': [{'transition': 0}]}})
        verify_feature_observations(case, 'marcus', 'playback', {'trace': {'presentations': [{'transition': 1}]}})

    def test_missing_middle_duplicate_and_reorder_fail_coverage(self):
        scenes = [{'scene': 0, 'first_frame': -2, 'last_frame': 0},
                  {'scene': 1, 'first_frame': 10, 'last_frame': 11}]
        frames = [{'scene': s, 'frame': f} for s, f in ((0, -2), (0, -1), (0, 0), (1, 10), (1, 11))]
        self.assertTrue(frame_coverage(scenes, frames))
        self.assertFalse(frame_coverage(scenes, frames[:1] + frames[2:]))
        self.assertFalse(frame_coverage(scenes, frames + frames[-1:]))
        self.assertFalse(frame_coverage(scenes, frames[::-1]))
        self.assertFalse(frame_coverage([], []))

    def test_timing_preserves_drift_and_ignores_only_global_start(self):
        def frames(start, offsets):
            return [{'scene': 0, 'frame': i, 'timestamp_ns': start + offset} for i, offset in enumerate(offsets)]
        reference = frames(1000, [0, 100, 200, 300])
        self.assertEqual(timing_metrics(reference, frames(9000, [0, 100, 200, 300]), 10)['status'], 'matched')
        result = timing_metrics(reference, frames(9000, [0, 105, 215, 330]), 10)
        self.assertEqual(result['status'], 'different')
        self.assertEqual(result['end_drift_ns'], 30)
        self.assertEqual(result['first_different_sequence'], 2)
        self.assertEqual(timing_metrics(reference, frames(0, [0, 100, 90, 300]), 10)['status'], 'inconclusive')


class Database(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.root = Path(self.temporary.name)
        self.store = Store(self.root / 'playback.sqlite')
        self.store.insert('suites', id='suite', started='2026-11-10',
            manifest_json=json.dumps({'schema': 1, 'cases': [{'name': 'fixture', 'reference': 'retail', 'expected_audio': 'silent'}]}),
            environment_json='{}', source_revision='test')
        self.movie = self.root / 'test.3mm'
        self.movie.write_bytes(b'independent movie identity')
        self.store.add_case('suite', {'name': 'fixture', 'path': self.movie, 'reference': 'retail', 'features': ['camera-cuts']})

    def tearDown(self):
        self.store.close()
        self.temporary.cleanup()

    def run_record(self, player, mode='seek', status='complete', preparation='fresh-process', repetition=0, case_name='fixture'):
        run = self.store.insert('runs', suite_id='suite', case_name=case_name, player=player, mode=mode,
            preparation=preparation, repetition=repetition, artifact_dir='.', capabilities_json=json.dumps(capabilities(player, mode)))
        self.store.finish(run, status)
        return run

    def observation(self, run, colour=(10, 20, 30), extension='.png', frame=1, sequence=0):
        path = self.root / ('capture-%d-%d%s' % (run, sequence, extension))
        Image.new('RGB', (3, 2), colour).save(path)
        self.store.insert('frames', run_id=run, sequence=sequence, scene=0, frame=frame, timestamp_ns=None, state_json='{}')
        self.store.image(run, sequence, 0, frame, path)
        return path

    @unittest.skipUnless(Image, 'Image checks require Pillow; standard-library measurement checks still run')
    def test_pixel_hash_is_independent_of_container(self):
        a, b = self.run_record('retail'), self.run_record('marcus')
        self.observation(a)
        self.observation(b, extension='.ppm')
        self.assertEqual(compare_runs(self.store, a, b)['status'], 'matched')
        self.assertEqual(self.store.db.execute('SELECT count(DISTINCT pixel_sha256) FROM images').fetchone()[0], 1)
        self.assertEqual(self.store.db.execute('SELECT equal_pixels FROM image_review').fetchone()[0], 1)

    @unittest.skipUnless(Image, 'Image checks require Pillow')
    def test_single_pixel_difference_has_review_evidence(self):
        a, b = self.run_record('retail'), self.run_record('marcus')
        self.observation(a)
        self.observation(b, colour=(10, 20, 31))
        self.assertEqual(compare_runs(self.store, a, b)['status'], 'different')
        row = self.store.db.execute("SELECT metrics_json FROM comparisons WHERE component='seek_pixels'").fetchone()
        metrics = json.loads(row[0])
        self.assertEqual(metrics['changed_pixels'], 6)
        self.assertEqual(metrics['max_channel_error'], 1)
        self.assertTrue(self.store.resolve(metrics['first_difference']['difference']).is_file())
        self.assertEqual(self.store.db.execute('SELECT equal_pixels FROM image_review').fetchone()[0], 0)

    @unittest.skipUnless(Image, 'Image checks require Pillow')
    def test_missing_frame_and_empty_pass_cannot_match(self):
        a, b = self.run_record('retail'), self.run_record('marcus')
        self.assertEqual(compare_runs(self.store, a, b)['status'], 'different')
        self.observation(a)
        self.assertEqual(compare_runs(self.store, a, b)['status'], 'different')

    @unittest.skipUnless(Image, 'Image checks require Pillow')
    def test_changed_artifact_is_inconclusive_even_if_stored_pixel_hash_matches(self):
        a, b = self.run_record('retail'), self.run_record('marcus')
        self.observation(a)
        path = self.observation(b)
        path.write_bytes(b'corrupted after capture')
        self.assertEqual(compare_runs(self.store, a, b)['status'], 'inconclusive')
        self.assertEqual(review(self.store)['status'], 'inconclusive')

    @unittest.skipUnless(Image, 'Image checks require Pillow')
    def test_partial_capture_cannot_pass(self):
        a, b = self.run_record('retail'), self.run_record('marcus', status='error')
        self.observation(a)
        self.observation(b)
        self.assertEqual(compare_runs(self.store, a, b)['status'], 'inconclusive')

    @unittest.skipUnless(Image, 'Image checks require Pillow')
    def test_unstable_original_does_not_establish_candidate_regression(self):
        a, b = self.run_record('retail'), self.run_record('marcus')
        self.observation(a)
        self.observation(b, colour=(10, 20, 31))
        result = compare_runs(self.store, a, b, reference_repeatability={'seek_pixels': 'different'})
        self.assertEqual(result['status'], 'inconclusive')
        metrics = json.loads(self.store.db.execute("SELECT metrics_json FROM comparisons WHERE component='seek_pixels'").fetchone()[0])
        self.assertEqual(metrics['observed_status'], 'different')
        self.assertEqual(metrics['different_images'], 1)

    @unittest.skipUnless(Image, 'Image checks require Pillow')
    def test_new_policy_preserves_history_and_review_uses_latest(self):
        a, b = self.run_record('retail'), self.run_record('marcus')
        self.observation(a)
        self.observation(b)
        compare_runs(self.store, a, b)
        compare_runs(self.store, a, b)
        self.assertEqual(self.store.db.execute('SELECT count(*) FROM comparisons').fetchone()[0], 6)
        self.assertEqual(len(review(self.store)['comparisons']), 3)

    @unittest.skipUnless(Image, 'Image checks require Pillow')
    def test_suite_recomparison_checks_originals_before_attributing_differences(self):
        a = self.run_record('retail')
        b = self.run_record('retail', repetition=1)
        c = self.run_record('marcus')
        d = self.run_record('marcus', repetition=1)
        for run in (a, b, c, d):
            self.observation(run, colour=(10, 20, 31) if run == b else (10, 20, 30))
        compare_suite(self.store, 'suite')
        results = self.store.db.execute("SELECT candidate_run,status,metrics_json FROM comparisons WHERE component='seek_pixels'").fetchall()
        self.assertEqual([row['status'] for row in results], ['different', 'inconclusive', 'inconclusive'])
        self.assertEqual(json.loads(results[-1]['metrics_json'])['observed_status'], 'different')

    def test_preparation_mismatch_cannot_compare(self):
        a, b = self.run_record('retail'), self.run_record('marcus', preparation='replay')
        with self.assertRaises(ValueError):
            compare_runs(self.store, a, b)

    def test_self_comparison_and_port_reference_are_rejected(self):
        a, b = self.run_record('retail'), self.run_record('marcus')
        with self.assertRaises(ValueError):
            compare_runs(self.store, a, a)
        with self.assertRaises(ValueError):
            compare_runs(self.store, b, a)

    @unittest.skipUnless(Image, 'Image checks require Pillow')
    def test_an_unpaired_case_keeps_suite_inconclusive(self):
        a, b = self.run_record('retail'), self.run_record('marcus')
        self.observation(a)
        self.observation(b)
        compare_runs(self.store, a, b)
        self.store.add_case('suite', {'name': 'unpaired', 'path': self.movie, 'reference': 'retail', 'features': []})
        self.run_record('marcus', case_name='unpaired')
        self.assertEqual(review(self.store)['status'], 'inconclusive')

    def test_reference_only_is_unverified(self):
        self.run_record('retail')
        self.assertEqual(review(self.store)['status'], 'inconclusive')

    def test_matching_frame_commits_do_not_prove_continuous_picture_or_audio(self):
        a, b = self.run_record('retail', mode='playback'), self.run_record('marcus', mode='playback')
        trace = {'frames': [{'scene': 0, 'frame': 1, 'first': 1, 'last': 1, 'monotonic_ns': 500}], 'sounds': []}
        self.store.trace(a, trace)
        self.store.trace(b, trace)
        self.assertEqual(compare_runs(self.store, a, b)['status'], 'inconclusive')
        components = dict(self.store.db.execute('SELECT component,status FROM comparisons'))
        self.assertEqual(components['frame_sequence'], 'matched')
        self.assertEqual(components['continuous_pixels'], 'inconclusive')
        self.assertEqual(components['mixed_pcm'], 'inconclusive')

    def test_database_preserves_raw_frame_and_sound_observations(self):
        run = self.run_record('retail', mode='playback')
        self.store.trace(run, {'frames': [{'scene': 0, 'frame': -3, 'first': -3, 'last': 0, 'monotonic_ns': 500}],
                               'sounds': [{'scene': 0, 'frame': -3, 'number': 123, 'start': 47}]})
        self.assertEqual(self.store.db.execute('SELECT frame FROM frames').fetchone()[0], -3)
        event = json.loads(self.store.db.execute('SELECT event_json FROM sound_events').fetchone()[0])
        self.assertEqual(event['start'], 47)
        self.assertIsNone(self.store.db.execute('SELECT timestamp_ns FROM sound_events').fetchone()[0])
        self.assertEqual(self.store.db.execute('SELECT feature FROM feature_coverage').fetchone()[0], 'camera-cuts')

    def test_manifest_requires_identity_and_known_features(self):
        case = {'name': 'fixture', 'path': self.movie.name, 'sha256': digest(self.movie),
                'reference': 'retail', 'features': ['camera-cuts']}
        path = self.root / 'suite.json'
        path.write_text(json.dumps({'schema': 1, 'cases': [case]}))
        self.assertEqual(load_manifest(path)['cases'][0]['path'], self.movie)
        self.movie.write_bytes(b'changed')
        with self.assertRaisesRegex(ValueError, 'hash'):
            load_manifest(path)
        case['sha256'] = digest(self.movie)
        case['features'] = ['invented-feature']
        path.write_text(json.dumps({'schema': 1, 'cases': [case]}))
        with self.assertRaisesRegex(ValueError, 'Unknown features'):
            load_manifest(path)


if __name__ == '__main__':
    unittest.main()
