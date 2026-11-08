"""Headless reconstruction tests; only programmatically generated fixtures."""

import argparse
import contextlib
import importlib.util
import io
import json
import os
from pathlib import Path
import random
import shutil
import struct
import subprocess
import sys
import tempfile
import unittest
from unittest import mock
import zipfile

ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location("nick_import", ROOT / "tools/recreate_nick_expansion.py")
nick = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(nick)
CONVERTER = ROOT / "build/studio-portable-x64" / ("nick-expansion.exe" if os.name == "nt" else "nick-expansion")
FIXTURES = CONVERTER.with_name("nick-expansion-test.exe" if os.name == "nt" else "nick-expansion-test")


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


def make_iso(path, sources, folder="NICK3D"):
    image = bytearray(128 * nick.SECTOR)
    descriptor = bytearray(nick.SECTOR)
    descriptor[:7] = b"\x01CD001\x01"
    struct.pack_into("<I", descriptor, 80, 128)
    struct.pack_into(">I", descriptor, 84, 128)
    descriptor[128:132] = b"\x00\x08\x08\x00"
    descriptor[156:190] = record(b"\0", 20, nick.SECTOR, True)
    image[16 * nick.SECTOR:17 * nick.SECTOR] = descriptor
    root = record(b"\0", 20, nick.SECTOR, True) + record(folder, 21, nick.SECTOR, True)
    image[20 * nick.SECTOR:20 * nick.SECTOR + len(root)] = root
    entries = bytearray()
    sector = 22
    for name, data in sources.items():
        entries.extend(record(name + ";1", sector, len(data)))
        image[sector * nick.SECTOR:sector * nick.SECTOR + len(data)] = data
        sector += (len(data) + nick.SECTOR - 1) // nick.SECTOR
    if sector > 128:
        raise ValueError("Synthetic disc fixture is too large")
    image[21 * nick.SECTOR:21 * nick.SECTOR + len(entries)] = entries
    path.write_bytes(image)


class PrimitiveTests(unittest.TestCase):
    def test_residual_modes_and_different_dimensions(self):
        rng = random.Random(37)
        for source_size, target_size in [(0, 10), (10, 4), (10, 20), (10000, 10000)]:
            source, target = rng.randbytes(source_size), rng.randbytes(target_size)
            for mode in ("xor", "add"):
                self.assertEqual(nick.restore(source, nick.residual(source, target, mode), mode), target)
        with self.assertRaises(ValueError):
            nick.restore(b"a", b"b", "unknown")

    def test_image_identities_cannot_address_geometry_or_paths(self):
        self.assertTrue(nick.IMAGE_NAME.fullmatch("9-544d4150-00000001.bin"))
        for name in ["../escape", "0-424d444c-00000001.bin", "10-544d4150-00000001.bin"]:
            self.assertFalse(nick.IMAGE_NAME.fullmatch(name))


@unittest.skipUnless(CONVERTER.exists() and FIXTURES.exists(), "Build nick-expansion and nick-expansion-test")
class ReconstructionTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        subprocess.run([str(FIXTURES), "--fixtures", str(self.root)], check=True, capture_output=True)
        self.reference = self.root / "reference"
        self.iso, self.base_iso = self.root / "nick.iso", self.root / "base.iso"
        self.sources = {p.name: p.read_bytes() for p in (self.root / "disc").iterdir()}
        make_iso(self.iso, self.sources)
        make_iso(self.base_iso, {"3DMOVIE.CHK": (self.root / "base.chk").read_bytes()}, "3DMOVIE")
        self.fixups, self.output = self.root / "fixups.zip", self.root / "output"
        quiet = mock.patch.object(nick, "run", side_effect=lambda converter, *args:
                                  subprocess.run([str(converter), *map(str, args)], check=True, capture_output=True))
        quiet.start()
        self.addCleanup(quiet.stop)

    def build(self):
        with contextlib.redirect_stdout(io.StringIO()):
            nick.build_fixups(self.iso, self.base_iso, CONVERTER, self.reference, self.fixups)

    def convert(self):
        with contextlib.redirect_stdout(io.StringIO()):
            nick.import_expansion(self.iso, self.base_iso, CONVERTER, self.fixups, self.output)

    def rewrite_fixups(self, modify):
        with zipfile.ZipFile(self.fixups) as archive:
            members = {name: archive.read(name) for name in archive.namelist()}
        modify(members)
        with zipfile.ZipFile(self.fixups, "w") as archive:
            for name, data in members.items():
                nick.zip_write(archive, name, data)

    def modify_manifest(self, modify):
        def update(members):
            manifest = json.loads(members["manifest.json"])
            modify(manifest)
            members["manifest.json"] = json.dumps(manifest).encode()
        self.rewrite_fixups(update)

    def test_historical_chunks_match_after_reopening(self):
        before_iso, before_base = self.iso.read_bytes(), self.base_iso.read_bytes()
        before_reference = {p.name: p.read_bytes() for p in self.reference.iterdir()}
        self.build()
        self.convert()
        subprocess.run([str(CONVERTER), "compare", str(self.output), str(self.reference)], check=True, capture_output=True)
        self.assertEqual(self.iso.read_bytes(), before_iso)
        self.assertEqual(self.base_iso.read_bytes(), before_base)
        self.assertEqual(before_reference, {p.name: p.read_bytes() for p in self.reference.iterdir()})
        self.assertEqual({p.name for p in self.output.iterdir()}, {name for _, name in nick.FILES} | {"reconstruction.json"})

    def test_fixups_contain_only_two_changed_images(self):
        self.build()
        with zipfile.ZipFile(self.fixups) as archive:
            manifest = nick.read_manifest(archive)
            self.assertEqual(len([row for row in manifest["images"] if "mode" in row]), 2)
            self.assertEqual(len(archive.namelist()), 3)

    def test_fixups_are_deterministic(self):
        self.build()
        first = self.fixups.read_bytes()
        self.fixups.unlink()
        self.build()
        self.assertEqual(self.fixups.read_bytes(), first)

    def test_import_needs_no_reference(self):
        self.build()
        self.assertEqual(self.reference.resolve().parent, self.root.resolve())
        shutil.rmtree(self.reference)  # Synthetic files owned by this TemporaryDirectory.
        self.convert()
        self.assertTrue(json.loads((self.output / "reconstruction.json").read_text())["historical_decoded_images_verified"])

    def test_non_image_difference_cannot_be_hidden_by_fixups(self):
        path = self.reference / "NICKMTRLS.3CN"
        data = path.read_bytes()
        self.assertEqual(data.count(b"\x01\x03\x05\x07"), 1)
        path.write_bytes(data.replace(b"\x01\x03\x05\x07", b"\x01\x03\x05\x08"))
        with self.assertRaisesRegex(ValueError, "non-image"):
            self.build()
        self.assertFalse(self.fixups.exists())

    def test_wrong_iso_publishes_nothing(self):
        self.build()
        self.sources["SNDS.3CN"] = self.sources["SNDS.3CN"].replace(b"\x01\x03\x05\x07", b"\x01\x03\x05\x08")
        make_iso(self.iso, self.sources)
        with self.assertRaisesRegex(ValueError, "ISO content"):
            self.convert()
        self.assertFalse(self.output.exists())
        self.assertFalse(list(self.root.glob(".nick-import-*")))

    def test_damaged_residual_is_rejected(self):
        self.build()
        def change(members):
            name = next(name for name in members if name.startswith("images/"))
            data = members[name]
            members[name] = bytes([data[0] ^ 1]) + data[1:]
        self.rewrite_fixups(change)
        with self.assertRaisesRegex(ValueError, "Damaged image correction"):
            self.convert()
        self.assertFalse(self.output.exists())

    def test_historical_image_hash_is_required(self):
        self.build()
        self.modify_manifest(lambda manifest: manifest["images"][0].update(sha256="0" * 64))
        with self.assertRaisesRegex(ValueError, "Historical image verification"):
            self.convert()
        self.assertFalse(self.output.exists())

    def test_rules_and_metadata_digest_are_required(self):
        self.build()
        self.modify_manifest(lambda manifest: manifest.update(non_image_state_sha256="0" * 64))
        with self.assertRaisesRegex(ValueError, "non-image"):
            self.convert()
        self.assertFalse(self.output.exists())

    def test_paths_and_non_image_chunk_types_are_refused(self):
        self.build()
        self.modify_manifest(lambda manifest: manifest["images"][0].update(name="0-424d444c-00000001.bin"))
        with self.assertRaisesRegex(ValueError, "image chunk identity"):
            self.convert()
        self.assertFalse(self.output.exists())

    def test_existing_files_are_preserved(self):
        self.build()
        original = self.fixups.read_bytes()
        with self.assertRaisesRegex(ValueError, "already exists"):
            self.build()
        self.assertEqual(self.fixups.read_bytes(), original)
        self.output.mkdir()
        marker = self.output / "keep"
        marker.write_bytes(b"original")
        with self.assertRaisesRegex(ValueError, "already exists"):
            self.convert()
        self.assertEqual(marker.read_bytes(), b"original")

    def test_iso_validation(self):
        self.iso.write_bytes(self.iso.read_bytes()[:17 * nick.SECTOR])
        with self.assertRaisesRegex(ValueError, "Truncated"):
            nick.Disc(self.iso)
        make_iso(self.iso, self.sources)
        data = bytearray(self.iso.read_bytes())
        data[16 * nick.SECTOR + 84] ^= 1
        self.iso.write_bytes(data)
        with self.assertRaisesRegex(ValueError, "Inconsistent"):
            nick.Disc(self.iso)
        make_iso(self.iso, {})
        with nick.Disc(self.iso) as disc, self.assertRaisesRegex(ValueError, "Missing"):
            disc.files("NICK3D", [name for name, _ in nick.FILES])
        with nick.Disc(self.iso) as disc, self.assertRaisesRegex(ValueError, "Unsafe"):
            disc.record(record("../escape", 22, 10))


if __name__ == "__main__":
    parser = argparse.ArgumentParser(add_help=False)
    parser.add_argument("--converter", type=Path)
    parser.add_argument("--fixtures", type=Path)
    args, rest = parser.parse_known_args()
    if args.converter and args.fixtures:
        CONVERTER, FIXTURES = args.converter.resolve(), args.fixtures.resolve()
        ReconstructionTests.__unittest_skip__ = False
    unittest.main(argv=[sys.argv[0], *rest])
