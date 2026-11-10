"""Check that the golden-image tool preserves an independent baseline."""

import contextlib
import hashlib
import importlib.util
import io
from pathlib import Path
import subprocess
import tempfile
import unittest
from unittest import mock


ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location("goldens", ROOT / "tools/rasterizer_goldens.py")
goldens = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(goldens)


class GoldenChecks(unittest.TestCase):
    def setUp(self):
        self.image = b"P5\n2 1\n255\n\x00\xff"
        self.record = {
            "file": "triangle.x64.pgm", "scene": "triangle", "arch": "x64",
            "width": 2, "height": 1, "bytes": len(self.image),
            "sha256": hashlib.sha256(self.image).hexdigest(), "captured_in": "a" * 40,
        }
        self.manifest = {
            "reference_revision": "b" * 40, "palette": "{fixtures}/palette/3dmm_global.bin",
            "recipes": {"triangle": ["triangle", "{output}"]},
        }

    def test_rejects_one_changed_pixel_without_changing_expected_hash(self):
        expected = self.record["sha256"]
        with self.assertRaisesRegex(ValueError, "reference hash"):
            goldens.check_capture(self.record, self.image[:-1] + b"\xfe")
        self.assertEqual(self.record["sha256"], expected)

    def test_rejects_resolution_changes_and_truncated_images(self):
        for image in (b"P5\n1 2\n255\n\x00\xff", self.image[:-1]):
            with self.subTest(image=image), self.assertRaises(ValueError):
                goldens.check_capture(self.record, image)

    def test_check_fails_for_missing_images(self):
        with tempfile.TemporaryDirectory() as directory, contextlib.redirect_stderr(io.StringIO()), \
                contextlib.redirect_stdout(io.StringIO()):
            self.assertEqual(goldens.check([self.record], Path(directory)), 1)

    def test_matching_pgm_and_ppm_are_accepted(self):
        goldens.check_capture(self.record, self.image)
        self.assertEqual(goldens.image_info(b"P6\n1 1\n255\n\x00\x80\xff"), (1, 1))

    def test_generation_cannot_pass_using_stale_files(self):
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory)
            fixture_directory = output / "custom fixtures"
            self.manifest["recipes"]["triangle"].append("{fixtures}/model.bin")
            (output / self.record["file"]).write_bytes(self.image)
            (output / "triangle.x64.ppm").write_bytes(b"stale")
            result = subprocess.CompletedProcess([], 0, stdout=b"no image written")
            with mock.patch.object(goldens.subprocess, "run", return_value=result) as run, \
                    contextlib.redirect_stdout(io.StringIO()):
                with self.assertRaisesRegex(ValueError, "did not produce"):
                    goldens.generate(self.manifest, [self.record], Path("renderer"), output, fixture_directory)
            self.assertFalse((output / self.record["file"]).exists())
            self.assertFalse((output / "triangle.x64.ppm").exists())
            self.assertEqual(run.call_args.kwargs["env"]["BREN_TEST_PALETTE"],
                             self.manifest["palette"].replace("{fixtures}", str(fixture_directory)))
            self.assertEqual(Path(run.call_args.args[0][-1]), fixture_directory / "model.bin")

    def test_fixture_check_failure_prevents_rendering(self):
        with mock.patch.object(goldens, "generate") as generate, \
                mock.patch.object(goldens.subprocess, "run", side_effect=subprocess.CalledProcessError(1, "check")):
            with self.assertRaises(subprocess.CalledProcessError):
                goldens.main(["generate", "--arch", "x64", "--exe", str(Path(__file__).resolve())])
        generate.assert_not_called()

    def test_renderer_failure_is_reported(self):
        with tempfile.TemporaryDirectory() as directory:
            result = subprocess.CompletedProcess([], 7, stdout=b"render failed")
            with mock.patch.object(goldens.subprocess, "run", return_value=result), \
                    contextlib.redirect_stdout(io.StringIO()):
                with self.assertRaisesRegex(ValueError, "exited with 7"):
                    goldens.generate(self.manifest, [self.record], Path("renderer"), Path(directory))

    def test_restore_rejects_corrupt_historical_bytes_before_writing(self):
        result = subprocess.CompletedProcess([], 0, stdout=self.image[:-1] + b"\xfe")
        with tempfile.TemporaryDirectory() as directory, \
                mock.patch.object(goldens.subprocess, "run", return_value=result):
            with self.assertRaises(ValueError):
                goldens.restore(self.manifest, [self.record], Path(directory))
            self.assertFalse((Path(directory) / self.record["file"]).exists())


if __name__ == "__main__":
    unittest.main()
