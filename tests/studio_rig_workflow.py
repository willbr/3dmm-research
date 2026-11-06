"""Bake a generic articulated assembly through real MCP and verify save/reopen."""

import argparse
import pathlib
import sys
import tempfile

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1] / "tools"))
from mcp_rig import Rig, Baker, rotate, sample
from studio_authoring_workflow import authoring_studio, content


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("exe", "iso", "artifacts"):
        parser.add_argument(name, type=pathlib.Path)
    args = parser.parse_args()
    args.artifacts.mkdir(parents=True, exist_ok=True)
    out = pathlib.Path(tempfile.mkdtemp(prefix="rig-", dir=args.artifacts.resolve()))
    rig = Rig()
    rig.group("root", position=(0, 12, -40))
    rig.group("shoulder", "root", position=(2, 0, 0))
    rig.group("elbow", "shoulder", position=(0, -5, 0))
    rig.group("hand", "elbow", position=(0, -4, 0))
    rig.group("receiver", position=(-10, 5, -35), rotation=(0, 30, 0))
    rig.prop(
        "Upper segment #1",
        4130,
        (2, 5, 2),
        "#1b6c34",
        "shoulder",
        position=(0, -2.5, 0),
    )
    rig.prop(
        "Lower segment #1", 4131, (2, 4, 2), "#cc4433", "elbow", position=(0, -2, 0)
    )
    rig.prop("End part #1", 4131, (2.5, 2.5, 2.5), "#e5cfaa", "hand")
    rig.prop(
        "Transfer object #2", 4130, (3, 3, 3), "#3344aa", "hand", position=(0, 0, 2)
    )
    rig.key("root", 0)
    rig.key("root", 2, position=(8, 0, 0), rotation=(0, 25, 0), scale=1.1)
    rig.key("shoulder", 0)
    rig.key("shoulder", 2, rotation=(0, 0, 65))
    rig.key("elbow", 0)
    rig.key("elbow", 2, rotation=(0, 0, 35))
    rig.key("receiver", 0)
    rig.key("receiver", 2, position=(5, 2, 0), rotation=(0, -25, 0))
    rig.reparent("Transfer object #2", "receiver", 1)
    movie = out / "Jointed assembly.3mm"
    with authoring_studio(args.exe, args.iso, out) as studio:
        call = lambda tool, **kw: content(studio, tool, **kw)
        call("create_scene", background_cno=11)
        baker = Baker(rig, call)
        baker.create()
        baker.bake(9, 2)
        call("save_movie", path=str(movie))
    with authoring_studio(args.exe, args.iso, out, movie) as studio:
        assert studio.state()["scene_actor_count"] == 4
        assert studio.state()["scene_last_frame"] == 9
        for frame in list(range(1, 10)) + [5, 1, 9]:
            time = (frame - 1) / 4
            content(studio, "edit_frame", frame=frame)
            world = rig.evaluate(time)
            for name, actor in baker.actors.items():
                actual = content(studio, "inspect_actor", actor_id=actor)
                expected = world[name]
                assert (
                    max(
                        abs(a - b)
                        for a, b in zip(actual["position"], expected.position)
                    )
                    < 0.03
                ), (frame, name, actual, expected)
                for axis in ((1, 0, 0), (0, 1, 0), (0, 0, 1)):
                    matrix = actual["rotation_matrix"]
                    actual_axis = tuple(
                        sum(axis[i] * matrix[i][j] for i in range(3)) for j in range(3)
                    )
                    assert (
                        max(
                            abs(a - b)
                            for a, b in zip(
                                actual_axis, rotate(expected.rotation, axis)
                            )
                        )
                        < 0.004
                    ), (frame, name, matrix)
                size = sample(rig.nodes[name].size_keys, time, rig.nodes[name].size)
                actual_size = tuple(
                    actual["scale"] * stretch * base
                    for stretch, base in zip(actual["stretch"], baker.bases[name])
                )
                assert (
                    max(abs(a - b * expected.scale) for a, b in zip(actual_size, size))
                    < 0.03
                ), (frame, name, actual_size)
            assert studio.state()["error_stack_depth"] == 0
    print(
        "Script-side rig: joint pivots, inherited transforms, group scale, handoff and legacy save/reopen passed:",
        out,
    )


if __name__ == "__main__":
    main()
