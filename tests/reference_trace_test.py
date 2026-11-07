"""Synthetic geometry and motion regressions, without private reference media."""
import math
import pathlib
import sys
import unittest

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1] / 'tools'))
from mcp_rig import Pose
from reference_trace import fit_camera, movement_report, project, sample_track, vanishing_point, write_viewer


class ReferenceTraceTest(unittest.TestCase):
    def test_camera_recovers_architecture_pose(self):
        camera = Pose.xyz((7, 18, 24), (-8, 17, 2))
        points = [(-20, 0, -55), (20, 0, -55), (-20, 35, -55), (20, 35, -55),
                  (-20, 0, -15), (20, 0, -15), (-20, 35, -15), (20, 35, -15)]
        spec = dict(focal=404, image_size=[544, 306],
                    initial_camera=dict(position=[12, 16, 30], rotation=[-5, 12, 0]),
                    landmarks=[dict(name=str(i), world=p, reference=project(camera, p, 404, (544, 306))) for i, p in enumerate(points)])
        result = fit_camera(spec)
        self.assertLess(result['rms_px'], 1e-5)
        self.assertLess(math.dist(result['position'], camera.position), 1e-4)

    def test_planar_camera_with_axis_lines(self):
        camera = Pose.xyz((3, 12, 15), (-3, 12, 1))
        points = [(-10, 0, -30), (10, 0, -30), (-10, 20, -30), (10, 20, -30)]
        spec = dict(focal=404, image_size=[544, 306],
                    initial_camera=dict(position=[4, 13, 18], rotation=[-1, 10, 0]),
                    landmarks=[dict(name=str(i), world=p, reference=project(camera, p, 404, (544, 306))) for i, p in enumerate(points)])
        spec['lines'] = [dict(axis='x', points=[project(camera, p, 404, (544, 306)) for p in points[:2]]),
                         dict(axis='y', points=[project(camera, p, 404, (544, 306)) for p in points[::2]])]
        self.assertLess(fit_camera(spec)['rms_px'], 1e-5)

    def test_rejects_degenerate_or_behind_camera(self):
        with self.assertRaises(ValueError):
            project(Pose.xyz(), (0, 0, 1), 404, (544, 306))
        spec = dict(focal=404, image_size=[544, 306], initial_camera=dict(position=[0, 0, 0], rotation=[0, 0, 0]),
                    landmarks=[dict(name=str(i), world=(i, 0, -20), reference=(i, 0)) for i in range(4)])
        with self.assertRaisesRegex(ValueError, 'collinear'):
            fit_camera(spec)

    def test_vanishing_points_finite_and_at_infinity(self):
        result = vanishing_point([[(0, 0), (10, 10)], [(10, 0), (0, 10)], [(5, 0), (5, 10)]])
        self.assertLess(math.dist(result['point'], (5, 5)), 1e-8)
        self.assertTrue(vanishing_point([[(0, 0), (10, 0)], [(0, 3), (10, 3)]])['at_infinity'])
        with self.assertRaises(ValueError):
            vanishing_point([[(0, 0), (0, 0)], [(0, 1), (1, 1)]])

    def test_track_does_not_invent_occluded_joints(self):
        track = dict(keys=[dict(frame=1, reference=[1, 4]), dict(frame=3, reference=[3, 8]),
                           dict(frame=4, visible=False), dict(frame=5, reference=[8, 8])])
        self.assertIsNone(sample_track(track, 0, 2))
        self.assertIsNone(sample_track(track, 2))
        self.assertEqual(sample_track(track, 2, 2)['reference'], [2, 6])
        self.assertIsNone(sample_track(track, 4, 2))
        self.assertIsNone(sample_track(track, 4.5, 2))

    def test_motion_detects_timing_error_and_contact_motion(self):
        spec = dict(fps=6, tracks=[dict(name='ankle', keys=[
            dict(frame=1, reference=[10, 20], rendered=[10, 20], contact=True),
            dict(frame=2, reference=[10, 20], rendered=[13, 24], contact=True),
            dict(frame=3, visible=False)])])
        result = movement_report(spec)
        self.assertEqual(result['count'], 2)
        self.assertEqual(result['max_error_px'], 5)
        self.assertEqual(result['checks'][1]['rendered_contact_motion_px'], 5)
        self.assertEqual(result['checks'][1]['velocity_error_px_per_second'], 30)

    def test_viewer_escapes_embedded_script_text(self):
        import tempfile
        with tempfile.TemporaryDirectory() as directory:
            path = pathlib.Path(directory) / 'trace.html'
            write_viewer(dict(image_size=[544, 306], landmarks=[dict(name='</script>')]), path)
            html = path.read_text(encoding='utf-8')
            self.assertIn('\\u003c/script>', html)
            self.assertNotIn('"name": "</script>"', html)


if __name__ == '__main__':
    unittest.main()
