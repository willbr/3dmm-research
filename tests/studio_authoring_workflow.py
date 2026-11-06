"""Exercise direct MCP authoring, event playback, validation and legacy save/reopen.

Requires an SDL Debug (or STUDIO_TEST_TOOLS) executable and the retail ISO.
All preferences and generated movies stay in the supplied artifact directory.
"""

import argparse
import contextlib
import json
import os
import pathlib
import tempfile
import uuid
from types import SimpleNamespace
from studio_movie_workflow import Studio
from studio_movie_only_workflow import remove_settings


@contextlib.contextmanager
def authoring_studio(exe, iso, artifacts, movie=None):
    artifacts = pathlib.Path(artifacts).resolve()
    artifacts.mkdir(parents=True, exist_ok=True)
    run = str(uuid.uuid4())
    if os.name == "nt":
        import winreg

        key = (
            "Software\\3DMMForever\\RetailOracle\\SDL-"
            + run
            + "\\User\\Software\\Microsoft\\Microsoft Kids\\3D Movie Maker"
        )
        with winreg.CreateKeyEx(
            winreg.HKEY_CURRENT_USER,
            key,
            0,
            winreg.KEY_ALL_ACCESS | winreg.KEY_WOW64_32KEY,
        ) as registry:
            winreg.SetValueEx(registry, "SkipSplashScreen", 0, winreg.REG_DWORD, 1)
    args = SimpleNamespace(
        exe=pathlib.Path(exe),
        content=artifacts,
        artifacts=artifacts,
        launch_arguments=["--iso", str(pathlib.Path(iso).resolve())],
        environment={
            "THREEDMM_CONTENT_ROOT": None,
            "KAUAI_TEST_SETTINGS_RUN": run,
            "XDG_CONFIG_HOME": str(artifacts / "settings"),
            "SDL_VIDEODRIVER": "dummy",
            "SDL_RENDER_DRIVER": "software",
            "SDL_AUDIODRIVER": "dummy",
        },
    )
    studio = None
    try:
        studio = Studio(args, pathlib.Path(movie) if movie else None, "studio-" + run)
        yield studio
    finally:
        if studio:
            studio.close()  # Only this owned authoring/test process.
        remove_settings(run)


def content(studio, tool, **arguments):
    text = studio.call(tool, **arguments)["content"][0]["text"]
    try:
        return json.loads(text)
    except ValueError:
        return text


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("exe", "iso", "artifacts"):
        parser.add_argument(name, type=pathlib.Path)
    args = parser.parse_args()
    args.artifacts.mkdir(parents=True, exist_ok=True)
    out = pathlib.Path(
        tempfile.mkdtemp(prefix="authoring-", dir=args.artifacts.resolve())
    )
    movie = out / "Authored movie.3mm"
    expected = {}
    with authoring_studio(args.exe, args.iso, out) as studio:

        def call(tool, **kw):
            return content(studio, tool, **kw)

        assets = call("list_assets", kind="props")
        assert any(a["cno"] == 4130 and a["name"] == "Cube" for a in assets), assets
        call("create_scene", background_cno=11)
        cube = call("create_actor", template_cno=4130, name="Cube part #1")["actor_id"]
        sphere = call("create_actor", template_cno=4131, name="Sphere part #1")[
            "actor_id"
        ]
        call(
            "transform_actor",
            actor_id=cube,
            position=[2, 3, -35],
            rotation=[0, 0, 90],
            scale=2,
            stretch=[1, 2, 1],
            color="#1b6c34",
        )
        pose = call("inspect_actor", actor_id=cube)
        matrix = pose["rotation_matrix"]
        assert (
            abs(matrix[0][0]) < 0.002
            and abs(matrix[0][1]) > 0.99
            and matrix[2][2] > 0.99
        ), matrix
        pose = call("transform_actor", actor_id=cube, offset=[1, 0, 0], scale=1.5)
        assert pose["position"] == [3, 3, -35] and pose["stretch"] == [1, 2, 1], pose
        call(
            "transform_actor",
            actor_id=cube,
            position=[2, 3, -35],
            rotation=[0, 0, 0],
            scale=1,
            stretch=[1, 1, 1],
        )
        initial = call("inspect_actor", actor_id=cube)
        for invalid in (
            {"position": [0, 0]},
            {"rotation": [0, "bad", 0]},
            {"scale": 11},
            {"position": [1, 2, 3], "offset": [1, 2, 3]},
            {"color": "#oops!!"},
            {"shaded": False},
            {"color": "#ffffff", "shaded": "no"},
        ):
            try:
                studio.call("transform_actor", actor_id=cube, **invalid)
            except AssertionError as error:
                assert "isError" in str(error), error
            else:
                raise AssertionError("Invalid input accepted: " + str(invalid))
            assert call("inspect_actor", actor_id=cube) == initial
        for frame, x in ((1, 2), (4, 6), (8, 10)):
            call("edit_frame", frame=frame)
            call(
                "transform_actors",
                edits=[
                    {"actor_id": cube, "position": [x, 3, -35]},
                    {"actor_id": sphere, "position": [-x, 6, -40], "color": "#100e0b"},
                ],
            )
            expected[frame] = (
                call("inspect_actor", actor_id=cube)["position"],
                call("inspect_actor", actor_id=sphere)["position"],
            )
        for frame in (1, 8, 4, 1):
            call("edit_frame", frame=frame)
            assert (
                call("inspect_actor", actor_id=cube)["position"],
                call("inspect_actor", actor_id=sphere)["position"],
            ) == expected[frame]
        call("edit_frame", frame=4)
        call("transform_actor", actor_id=sphere, color="#829bb2", shaded=False)
        for invalid in (
            {"text": "Bad bounds", "bounds": [0, 0, 545, 50]},
            {"text": "Bad size", "bounds": [0, 0, 100, 50], "font_size": 0},
            {"text": "Bad color", "bounds": [0, 0, 100, 50], "color": "blue"},
            {"text": "Bad lifetime", "bounds": [0, 0, 100, 50], "last_frame": 3},
            {"text": "Bad alignment", "bounds": [0, 0, 100, 50], "align": "diagonal"},
            {"text": "Missing font", "bounds": [0, 0, 100, 50], "font": "__MCP_MISSING_FONT__"},
        ):
            try:
                studio.call("create_text_box", **invalid)
            except AssertionError as error:
                assert "isError" in str(error), error
            else:
                raise AssertionError("Invalid text box accepted")
        assert studio.state()["scene_textbox_count"] == 0
        call("create_text_box", text="Native title", bounds=[24, 24, 360, 100],
             font_size=28, bold=True, color="#175d36", align="center", last_frame=6)
        for invalid in ({"scene": 0}, {"scene": 2}):
            try:
                studio.call("select_scene", **invalid)
            except AssertionError as error:
                assert "isError" in str(error), error
            else:
                raise AssertionError("Invalid scene accepted")
        call("create_scene", background_cno=11)
        call("edit_frame", frame=3)
        call("select_scene", scene=1)
        assert studio.state()["scene_textbox_count"] == 1
        call("save_movie", path=str(movie))
        try:
            studio.call("save_movie", path=str(movie))
        except AssertionError as error:
            assert "isError" in str(error), error
        else:
            raise AssertionError("Existing movie overwrite accepted")
    with authoring_studio(args.exe, args.iso, out, movie) as studio:
        content(studio, "select_scene", scene=2)
        assert studio.state()["scene_last_frame"] == 3
        content(studio, "select_scene", scene=1)
        captions = studio.state()["scene_textboxes"]
        assert len(captions) == 1 and captions[0]["text"].startswith("Native title"), captions
        assert captions[0]["first_frame"] == 4 and captions[0]["last_frame"] == 6, captions
        assert studio.state()["scene_actor_count"] == 2
        assert studio.state()["scene_last_frame"] == 8
        for frame in (8, 1, 4):
            content(studio, "edit_frame", frame=frame)
            assert studio.state()["scene_textboxes"][0]["visible"] == (frame == 4)
            assert (
                content(studio, "inspect_actor", actor_id=cube)["position"],
                content(studio, "inspect_actor", actor_id=sphere)["position"],
            ) == expected[frame]
    (out / "result.json").write_text(
        json.dumps({"status": "passed", "movie": str(movie)}, indent=2)
    )
    print(
        "MCP authoring: transforms, XYZ rotation, validation, keyframes, save/reopen passed:",
        out,
    )


if __name__ == "__main__":
    main()
