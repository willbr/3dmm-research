"""Headless tests; synthetic disc/content only, no licensed assets required."""

import contextlib
import importlib.util
import io
import json
from pathlib import Path
import random
import struct
import tempfile
import unittest
import zipfile


SPEC = importlib.util.spec_from_file_location(
    "nick_import", Path(__file__).resolve().parents[1] / "tools/recreate_nick_expansion.py")
nick = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(nick)


def record(name, sector, size, directory=False):
    name = name if isinstance(name, bytes) else name.encode("ascii")
    result = bytearray(33 + len(name) + (len(name) % 2 == 0))
    result[0] = len(result)
    struct.pack_into("<I", result, 2, sector)
    struct.pack_into(">I", result, 6, sector)
    struct.pack_into("<I", result, 10, size)
    struct.pack_into(">I", result, 14, size)
    result[25] = 2 if directory else 0
    result[28:32] = b"\x01\x00\x00\x01"
    result[32] = len(name)
    result[33:33 + len(name)] = name
    return result


def make_iso(path, sources):
    image = bytearray(64 * nick.SECTOR)
    descriptor = bytearray(nick.SECTOR)
    descriptor[:7] = b"\x01CD001\x01"
    struct.pack_into("<I", descriptor, 80, 64)
    struct.pack_into(">I", descriptor, 84, 64)
    descriptor[128:132] = b"\x00\x08\x08\x00"
    descriptor[156:190] = record(b"\0", 20, nick.SECTOR, True)
    image[16 * nick.SECTOR:17 * nick.SECTOR] = descriptor
    root = record(b"\0", 20, nick.SECTOR, True) + record("NICK3D", 21, nick.SECTOR, True)
    image[20 * nick.SECTOR:20 * nick.SECTOR + len(root)] = root
    entries = bytearray()
    sector = 22
    for name, data in sources.items():
        entries.extend(record(name + ";1", sector, len(data)))
        image[sector * nick.SECTOR:sector * nick.SECTOR + len(data)] = data
        sector += (len(data) + nick.SECTOR - 1) // nick.SECTOR
    image[21 * nick.SECTOR:21 * nick.SECTOR + len(entries)] = entries
    path.write_bytes(image)


class ReconstructionTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.iso = self.root / "nick.iso"
        self.reference = self.root / "reference"
        self.reference.mkdir()
        self.profile = self.root / "conversion.zip"
        self.output = self.root / "output"
        rng = random.Random(37)
        self.sources = {source: rng.randbytes(2500) for source, _ in nick.FILES}
        make_iso(self.iso, self.sources)
        self.targets = {}
        for index, (source, name) in enumerate(nick.FILES):
            data = self.sources[source]
            if index % 2:
                data = b"edited image/metadata" + data[17:] + b"additional bytes"
            self.targets[name] = data
            (self.reference / name).write_bytes(data)

    def build(self):
        with contextlib.redirect_stdout(io.StringIO()):
            nick.build_profile(self.iso, self.reference, self.profile)

    def convert(self):
        with contextlib.redirect_stdout(io.StringIO()):
            nick.import_expansion(self.iso, self.profile, self.output)

    def rewrite_profile(self, modify):
        with zipfile.ZipFile(self.profile) as archive:
            members = {name: archive.read(name) for name in archive.namelist()}
        modify(members)
        with zipfile.ZipFile(self.profile, "w") as archive:
            for name, data in members.items():
                nick.zip_write(archive, name, data)

    def test_complete_reconstruction_and_read_only_inputs(self):
        before_iso = self.iso.read_bytes()
        before_reference = {name: (self.reference / name).read_bytes() for name in self.targets}
        self.build()
        self.convert()
        for name, expected in self.targets.items():
            self.assertEqual((self.output / name).read_bytes(), expected)
        self.assertEqual(self.iso.read_bytes(), before_iso)
        self.assertEqual(before_reference, {name: (self.reference / name).read_bytes() for name in self.targets})
        self.assertEqual({p.name for p in self.output.iterdir()}, set(self.targets) | {"reconstruction.json"})

    def test_profile_is_deterministic(self):
        self.build()
        second = self.root / "second.zip"
        with contextlib.redirect_stdout(io.StringIO()):
            nick.build_profile(self.iso, self.reference, second)
        self.assertEqual(self.profile.read_bytes(), second.read_bytes())

    def test_no_reference_required_for_import(self):
        self.build()
        for name in self.targets:
            (self.reference / name).unlink()
        self.reference.rmdir()
        self.convert()
        self.assertEqual((self.output / nick.FILES[-1][1]).read_bytes(), self.targets[nick.FILES[-1][1]])

    def test_mismatched_iso_publishes_nothing(self):
        self.build()
        self.sources[nick.FILES[-1][0]] = b"wrong edition"
        make_iso(self.iso, self.sources)
        with self.assertRaisesRegex(ValueError, "does not match"):
            self.convert()
        self.assertFalse(self.output.exists())
        self.assertFalse(list(self.root.glob(".nick-import-*")))

    def test_damaged_patch_publishes_nothing(self):
        self.build()
        self.rewrite_profile(lambda members: members.update({"delta/9.bin": b"damaged"}))
        with self.assertRaisesRegex(ValueError, "Damaged conversion patch"):
            self.convert()
        self.assertFalse(self.output.exists())

    def test_wrong_output_hash_publishes_nothing(self):
        self.build()

        def modify(members):
            manifest = json.loads(members["manifest.json"])
            manifest["files"][-1]["sha256"] = "0" * 64
            members["manifest.json"] = json.dumps(manifest).encode()

        self.rewrite_profile(modify)
        with self.assertRaisesRegex(ValueError, "failed verification"):
            self.convert()
        self.assertFalse(self.output.exists())

    def test_refuses_paths_from_manifest(self):
        self.build()

        def modify(members):
            manifest = json.loads(members["manifest.json"])
            manifest["files"][0]["name"] = "../escape"
            members["manifest.json"] = json.dumps(manifest).encode()

        self.rewrite_profile(modify)
        with self.assertRaisesRegex(ValueError, "filename"):
            self.convert()
        self.assertFalse((self.root / "escape").exists())

    def test_existing_output_and_profile_are_preserved(self):
        self.build()
        original = self.profile.read_bytes()
        with self.assertRaisesRegex(ValueError, "already exists"):
            self.build()
        self.assertEqual(self.profile.read_bytes(), original)
        self.output.mkdir()
        marker = self.output / "keep"
        marker.write_bytes(b"original")
        with self.assertRaisesRegex(ValueError, "already exists"):
            self.convert()
        self.assertEqual(marker.read_bytes(), b"original")

    def test_missing_reference_does_not_create_profile(self):
        (self.reference / nick.FILES[-1][1]).unlink()
        with self.assertRaisesRegex(ValueError, "reference expansion file"):
            self.build()
        self.assertFalse(self.profile.exists())

    def test_truncated_iso_and_inconsistent_record(self):
        self.iso.write_bytes(self.iso.read_bytes()[:17 * nick.SECTOR])
        with self.assertRaisesRegex(ValueError, "Truncated"):
            nick.Disc(self.iso)
        make_iso(self.iso, self.sources)
        image = bytearray(self.iso.read_bytes())
        image[16 * nick.SECTOR + 84] ^= 1
        self.iso.write_bytes(image)
        with self.assertRaisesRegex(ValueError, "Inconsistent"):
            nick.Disc(self.iso)

    def test_missing_content_and_directory_traversal_rejected(self):
        make_iso(self.iso, {})
        with nick.Disc(self.iso) as disc, self.assertRaisesRegex(ValueError, "Missing"):
            disc.sources()
        with nick.Disc(self.iso) as disc, self.assertRaisesRegex(ValueError, "Unsafe"):
            disc.record(record("../escape", 22, 10))

    def test_delta_handles_shifted_reused_blocks(self):
        data = random.Random(24).randbytes(100_000)
        target = b"new header" + data + b"new footer"
        patch = nick.make_patch(data, target)
        self.assertLess(len(patch), len(target) // 2)
        self.assertEqual(nick.apply_patch(data, patch, len(target)), target)

    def test_invalid_delta_bounds(self):
        cases = [b"?", b"C", b"C" + struct.pack("<II", 2, 4),
                 b"A" + struct.pack("<I", 5) + b"xx", b"A" + struct.pack("<I", 0),
                 b"C" + struct.pack("<II", 0, 3)]
        for patch in cases:
            with self.subTest(patch=patch), self.assertRaises(ValueError):
                nick.apply_patch(b"abc", patch, 2)


if __name__ == "__main__":
    unittest.main()
