"""Geometry/continuity checks for script-side rigs; no Studio assets needed."""

import math
import pathlib
import sys
import unittest

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1] / "tools"))
from mcp_rig import Rig, Pose, quaternion, euler, rotate, retail_dimensions


class RigTest(unittest.TestCase):
    def close(self, actual, expected, tolerance=1e-7):
        self.assertLess(max(abs(a - b) for a, b in zip(actual, expected)), tolerance)

    def test_joint_pivot_and_child_lengths(self):
        rig = Rig()
        rig.group("body", position=(10, 20, 30), rotation=(0, 35, 0), scale=2)
        rig.group("shoulder", "body", position=(3, 4, 0))
        rig.group("elbow", "shoulder", position=(0, -5, 0))
        rig.group("hand", "elbow", position=(0, -4, 0))
        rig.key("shoulder", 0)
        rig.key("shoulder", 1, rotation=(40, 0, 90))
        rig.key("elbow", 0)
        rig.key("elbow", 1, rotation=(0, 0, 75))
        for step in range(21):
            pose = rig.evaluate(step / 20)
            self.close(pose["shoulder"].position, (14.9149122657, 28, 26.5585413819))
            self.assertAlmostEqual(
                math.dist(pose["shoulder"].position, pose["elbow"].position), 10
            )
            self.assertAlmostEqual(
                math.dist(pose["elbow"].position, pose["hand"].position), 8
            )

    def test_reparent_has_no_jump_and_follows_new_hand(self):
        rig = Rig()
        rig.group("left", position=(3, 4, 5), rotation=(10, 30, 20), scale=2)
        rig.group("right", position=(-8, 2, -1), rotation=(-20, 10, 5), scale=0.5)
        rig.group("gift", "left", position=(1, 2, 3))
        rig.key("right", 0)
        rig.key("right", 2, position=(10, 0, 0), rotation=(0, 0, 30))
        before = rig.evaluate(1)["gift"]
        rig.reparent("gift", "right", 1)
        after = rig.evaluate(1)["gift"]
        self.close(before.position, after.position)
        for point in ((0, 0, 0), (1, 0, 0), (0, 2, 1)):
            self.close(before.point(point), after.point(point))
        world = rig.evaluate(2)
        local = rig.evaluate(1)["right"].inverted().compose(after)
        self.close(world["gift"].position, world["right"].compose(local).position)
        rig.reparent("gift", None, 2)
        self.close(rig.evaluate(3)["gift"].position, world["gift"].position)

    def test_shortest_rotation_and_singular_euler_round_trip(self):
        rig = Rig()
        rig.group("head")
        rig.key("head", 0, rotation=(0, 170, 0), easing="linear")
        rig.key("head", 2, rotation=(0, -170, 0))
        self.close(rotate(rig.evaluate(1)["head"].rotation, (0, 0, 1)), (0, 0, -1))
        for angles in ((13, 27, -62), (20, 90, 40), (-30, -90, 55), (170, 89.9, -125)):
            q = quaternion(angles)
            restored = quaternion(euler(q))
            for point in ((1, 0, 0), (0, 1, 0), (0, 0, 1)):
                self.close(rotate(q, point), rotate(restored, point))

    def test_cycle_rejected_without_corrupting_tree(self):
        rig = Rig()
        rig.group("root")
        rig.group("child", "root", position=(1, 0, 0))
        with self.assertRaises(ValueError):
            rig.reparent("root", "child", 1)
        self.close(rig.evaluate(2)["child"].position, (1, 0, 0))

    def test_retail_dimensions_respect_limits(self):
        for size in ((1, 1, 16), (112, 8, 88), (0.6, 0.85, 0.5)):
            scale, stretch = retail_dimensions(size, (5, 5, 5))
            self.assertTrue(0.25 <= scale <= 10)
            self.assertTrue(all(0.25 - 1e-9 <= s <= 4 + 1e-9 for s in stretch))
            self.close(tuple(scale * s * 5 for s in stretch), size)
        with self.assertRaises(ValueError):
            retail_dimensions((1, 1, 80), (5, 5, 5))
        for size, base in (
            ((1, 0, 1), (5, 5, 5)),
            ((1, 1, 1), (5, 0, 5)),
            ((1, float("nan"), 1), (5, 5, 5)),
        ):
            with self.assertRaises(ValueError):
                retail_dimensions(size, base)


if __name__ == "__main__":
    unittest.main()
