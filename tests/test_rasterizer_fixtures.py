"""Verify extraction against independent fixture hashes before publication."""

import contextlib
import hashlib
import importlib.util
import io
from pathlib import Path
import struct
import subprocess
import tempfile
import unittest
from unittest import mock


ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location("fixtures", ROOT / "tools/rasterizer_fixtures.py")
fixtures = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(fixtures)


def record(name, data, key="file"):
    return {key: name, "bytes": len(data), "sha256": hashlib.sha256(data).hexdigest()}


class FixtureChecks(unittest.TestCase):
    def setUp(self):
        self.palette = {"file": "palette/colours.bin", "byte_order": 1,
                        "platform": 771, "entry_bytes": 4, "count": 256}
        self.colours = bytes(range(256)) * 4
        self.bitmap = bytearray(54)
        self.bitmap[:2] = b"BM"
        struct.pack_into("<I", self.bitmap, 10, 1078)
        struct.pack_into("<IiiHH", self.bitmap, 14, 40, 1, 1, 1, 8)
        self.bitmap += self.colours
        self.expected_palette = struct.pack("<HHII", 1, 771, 4, 256) + self.colours

    def test_palette_preserves_all_original_colours(self):
        self.assertEqual(fixtures.palette_from_bitmap(self.bitmap, self.palette), self.expected_palette)

    def test_palette_rejects_truncation_and_wrong_pixel_format(self):
        wrong_format = bytearray(self.bitmap)
        struct.pack_into("<H", wrong_format, 28, 24)
        for data in (b"BM", self.bitmap[:-1], wrong_format):
            with self.subTest(data_length=len(data)), self.assertRaises(ValueError):
                fixtures.palette_from_bitmap(data, self.palette)

    def test_changed_and_missing_fixtures_fail(self):
        manifest = {"files": [record("model.bin", b"reference")]}
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory)
            with self.assertRaisesRegex(ValueError, "missing fixture"):
                fixtures.verify_fixtures(manifest, output)
            (output / "model.bin").write_bytes(b"different")
            with self.assertRaisesRegex(ValueError, "reference hash mismatch"):
                fixtures.verify_fixtures(manifest, output)

    def make_manifest(self, root):
        (root / "source.chk").write_bytes(b"synthetic archive")
        (root / "source.bmp").write_bytes(self.bitmap)
        return {
            "sources": {
                "templates": record("source.chk", b"synthetic archive", "path"),
                "palette_bitmap": record("source.bmp", self.bitmap, "path"),
            },
            "templates": [], "props": [{"file": "model.bin", "tag": "BMDL", "cno": "0x1b8"}],
            "actor_manifest": {"file": "actor.txt", "cno": "0x2010"}, "palette": self.palette,
            "files": [record("model.bin", b"reference"), record("actor.txt", b"actor\n"),
                      record(self.palette["file"], self.expected_palette)],
        }

    @staticmethod
    def extraction_result(arguments, **kwargs):
        if arguments[2] == "dump":
            Path(arguments[-1]).write_bytes(b"reference")
        return subprocess.CompletedProcess(arguments, 0, stdout=b"actor\r\n", stderr=b"")

    def test_generation_verifies_before_publishing_and_normalises_text(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            manifest = self.make_manifest(root)
            output = root / "fixtures"
            output.mkdir()
            (output / "unrelated.txt").write_text("keep")
            with mock.patch.object(fixtures, "ROOT", root), \
                    mock.patch.object(fixtures.subprocess, "run", side_effect=self.extraction_result), \
                    contextlib.redirect_stdout(io.StringIO()):
                fixtures.generate(manifest, Path("inspector"), output)
                fixtures.verify_fixtures(manifest, output)
            self.assertEqual((output / "actor.txt").read_bytes(), b"actor\n")
            self.assertEqual((output / "unrelated.txt").read_text(), "keep")

    def test_bad_input_is_rejected_before_extraction(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            manifest = self.make_manifest(root)
            (root / "source.chk").write_bytes(b"changed archive")
            with mock.patch.object(fixtures, "ROOT", root), \
                    mock.patch.object(fixtures.subprocess, "run") as run:
                with self.assertRaisesRegex(ValueError, "reference hash mismatch"):
                    fixtures.generate(manifest, Path("inspector"), root / "fixtures")
            run.assert_not_called()

    def test_failed_or_incomplete_extraction_cannot_overwrite_existing_fixtures(self):
        results = [subprocess.CompletedProcess([], 7, stdout=b"", stderr=b"failed"),
                   subprocess.CompletedProcess([], 0, stdout=b"actor\n", stderr=b"")]
        for result in results:
            with self.subTest(returncode=result.returncode), tempfile.TemporaryDirectory() as directory:
                root = Path(directory)
                manifest = self.make_manifest(root)
                output = root / "fixtures"
                output.mkdir()
                (output / "model.bin").write_bytes(b"keep existing")
                with mock.patch.object(fixtures, "ROOT", root), \
                        mock.patch.object(fixtures.subprocess, "run", return_value=result):
                    with self.assertRaises(ValueError):
                        fixtures.generate(manifest, Path("inspector"), output)
                self.assertEqual((output / "model.bin").read_bytes(), b"keep existing")


if __name__ == "__main__":
    unittest.main()
