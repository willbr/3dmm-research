#!/usr/bin/env python3
"""Reconstruct the community Nickelodeon expansion using a read-only retail ISO.

The historical pack contains edited images and metadata, not just a palette
substitution. A local reference installation is used ONCE to build a lossless
conversion profile. Subsequent imports need only that profile and the ISO.
Profiles contain reference-derived data; they are not part of the source tree.
No original executables, registry writes, external extractors or codecs are used.
"""

import argparse
import hashlib
import io
import json
import os
from pathlib import Path
import struct
import sys
import tempfile
import zipfile


FILES = (
    ("ACTOR.NTH", "NICKACTOR.3TH"),
    ("BKGDS.3CN", "NICKBKGDS.3CN"),
    ("BKGDS.NTH", "NICKBKGDS.3TH"),
    ("MTRL.NTH", "NICKMTRL.3TH"),
    ("MTRLS.3CN", "NICKMTRLS.3CN"),
    ("PROP.NTH", "NICKPROP.3TH"),
    ("SNDS.3CN", "NICKSNDS.3CN"),
    ("SOUND.NTH", "NICKSOUND.3TH"),
    ("TDFS.3CN", "NICKTDFS.3CN"),
    ("TMPLS.3CN", "NICKTMPLS.3CN"),
)
SCHEMA = "3dmm-nick-expansion-profile-1"
MAX_FILE = 256 * 1024 * 1024
MAX_PATCH = MAX_FILE + 1024 * 1024
SECTOR = 2048
GEAR = tuple(int.from_bytes(hashlib.sha256(bytes([i])).digest()[:4], "little") for i in range(256))


def digest(data):
    return hashlib.sha256(data).hexdigest()


def both32(data, offset):
    little = struct.unpack_from("<I", data, offset)[0]
    if little != struct.unpack_from(">I", data, offset + 4)[0]:
        raise ValueError("Inconsistent ISO directory record")
    return little


class Disc:
    """Small ISO 9660 reader; only the ten known expansion inputs are read."""

    def __init__(self, path):
        self.file = open(path, "rb")
        try:
            self.size = os.fstat(self.file.fileno()).st_size
            for sector in range(16, 80):
                descriptor = self.read(sector * SECTOR, SECTOR)
                if descriptor[1:7] != b"CD001\x01":
                    raise ValueError("Expected an ISO 9660 disc image")
                if descriptor[0] == 255:
                    break
                if descriptor[0] != 1:
                    continue
                if descriptor[128:132] != b"\x00\x08\x08\x00":
                    raise ValueError("Unsupported ISO sector size")
                if both32(descriptor, 80) * SECTOR > self.size:
                    raise ValueError("Truncated ISO image")
                self.root = self.record(descriptor[156:190])
                if not self.root[3]:
                    raise ValueError("Invalid ISO root directory")
                return
            raise ValueError("ISO primary volume descriptor is missing")
        except BaseException:
            self.file.close()
            raise

    def __enter__(self):
        return self

    def __exit__(self, *args):
        self.file.close()

    def read(self, offset, size):
        if offset < 0 or size < 0 or offset + size > self.size:
            raise ValueError("ISO extent is outside the disc image")
        self.file.seek(offset)
        data = self.file.read(size)
        if len(data) != size:
            raise ValueError("Cannot read ISO contents")
        return data

    def record(self, data):
        if (len(data) < 34 or data[0] < 34 or data[0] > len(data)
                or data[32] > data[0] - 33 or data[1] or data[26] or data[27]
                or data[25] & 0x80):
            raise ValueError("Unsupported or corrupt ISO directory record")
        offset, size = both32(data, 2) * SECTOR, both32(data, 10)
        if offset + size > self.size:
            raise ValueError("ISO file extends beyond the disc image")
        raw = data[33:33 + data[32]]
        if raw in (b"\0", b"\1"):
            name = ""
        else:
            name = raw.decode("ascii")
            if ";" in name:
                name, version = name.split(";", 1)
                if version != "1":
                    raise ValueError("Unsupported ISO file version")
            name = name.removesuffix(".")
            if (not name or name in (".", "..")
                    or any(c not in "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789._- " for c in name)):
                raise ValueError("Unsafe ISO filename")
        return name.upper(), offset, size, bool(data[25] & 2)

    def entries(self, directory):
        if not directory[3] or directory[2] > 16 * 1024 * 1024:
            raise ValueError("Invalid ISO directory")
        data = self.read(directory[1], directory[2])
        result = {}
        offset = 0
        while offset < len(data):
            size = data[offset]
            if size == 0:
                offset = (offset // SECTOR + 1) * SECTOR
                continue
            if size > min(len(data) - offset, SECTOR - offset % SECTOR):
                raise ValueError("ISO directory record crosses a sector")
            entry = self.record(data[offset:offset + size])
            offset += size
            name = entry[0]
            if not name:
                continue
            if name in result or len(result) >= 4096:
                raise ValueError("Ambiguous or excessive ISO directory entries")
            result[name] = entry
        return result

    def sources(self):
        root = self.entries(self.root)
        if "NICK3D" not in root:
            raise ValueError("This ISO does not contain the Nickelodeon NICK3D directory")
        entries = self.entries(root["NICK3D"])
        result = {}
        for source, _ in FILES:
            entry = entries.get(source)
            if entry is None or entry[3] or not 0 < entry[2] <= MAX_FILE:
                raise ValueError(f"Missing or invalid Nickelodeon disc file: {source}")
            result[source] = self.read(entry[1], entry[2])
        return result


def blocks(data):
    """Content-defined blocks retain matches after archive offsets change."""
    start = 0
    rolling = 0
    for index, value in enumerate(data, 1):
        rolling = ((rolling << 1) + GEAR[value]) & 0xffffffff
        length = index - start
        if length >= 512 and (rolling & 4095 == 0 or length >= 16384):
            yield start, index
            start, rolling = index, 0
    if start < len(data):
        yield start, len(data)


def make_patch(source, target):
    """Copy/add delta. Literals are compressed by the enclosing ZIP file."""
    if source == target:
        return b"C" + struct.pack("<II", 0, len(source))
    lookup = {}
    for start, end in blocks(source):
        lookup.setdefault(hashlib.sha256(source[start:end]).digest(), (start, end))
    operations = []
    for start, end in blocks(target):
        chunk = target[start:end]
        match = lookup.get(hashlib.sha256(chunk).digest())
        if match is not None and source[match[0]:match[1]] == chunk:
            if operations and operations[-1][0] == "C" and operations[-1][1] + operations[-1][2] == match[0]:
                operations[-1][2] += end - start
            else:
                operations.append(["C", match[0], end - start])
        elif operations and operations[-1][0] == "A":
            operations[-1][1].extend(chunk)
        else:
            operations.append(["A", bytearray(chunk)])
    patch = io.BytesIO()
    for operation in operations:
        if operation[0] == "C":
            patch.write(b"C" + struct.pack("<II", operation[1], operation[2]))
        else:
            patch.write(b"A" + struct.pack("<I", len(operation[1])) + operation[1])
    return patch.getvalue()


def apply_patch(source, patch, expected_size):
    if not 0 < expected_size <= MAX_FILE:
        raise ValueError("Invalid reconstructed file size")
    result = bytearray()
    offset = 0
    while offset < len(patch):
        opcode = patch[offset:offset + 1]
        offset += 1
        needed = 8 if opcode == b"C" else 4
        if opcode not in (b"C", b"A") or len(patch) - offset < needed:
            raise ValueError("Invalid conversion instruction")
        if opcode == b"C":
            start, size = struct.unpack_from("<II", patch, offset)
            offset += 8
            if start + size > len(source):
                raise ValueError("Conversion copy extends beyond its source")
            data = source[start:start + size]
        else:
            size = struct.unpack_from("<I", patch, offset)[0]
            offset += 4
            if size > len(patch) - offset:
                raise ValueError("Truncated conversion literal")
            data = patch[offset:offset + size]
            offset += size
        if not size or size > expected_size - len(result):
            raise ValueError("Conversion exceeds its declared output size")
        result.extend(data)
    if len(result) != expected_size:
        raise ValueError("Incomplete reconstructed file")
    return bytes(result)


def zip_write(archive, name, data):
    info = zipfile.ZipInfo(name, date_time=(1980, 1, 1, 0, 0, 0))
    info.compress_type = zipfile.ZIP_DEFLATED
    info.external_attr = 0o100644 << 16
    archive.writestr(info, data, compresslevel=9)


def build_profile(iso, reference, destination):
    reference, destination = Path(reference).resolve(), Path(destination).absolute()
    if destination.exists():
        raise ValueError(f"Profile already exists: {destination}")
    with Disc(iso) as disc:
        sources = disc.sources()
    # Validate the whole reference before creating anything.
    targets = {}
    for _, name in FILES:
        path = reference / name
        if path.is_symlink() or not path.is_file() or not 0 < path.stat().st_size <= MAX_FILE:
            raise ValueError(f"Missing or invalid reference expansion file: {path}")
        targets[name] = path.read_bytes()
    manifest = {"schema": SCHEMA, "files": []}
    with tempfile.TemporaryDirectory(prefix=".nick-profile-", dir=destination.parent) as temp:
        staged = Path(temp) / "profile.zip"
        with zipfile.ZipFile(staged, "w") as archive:
            for index, (source, name) in enumerate(FILES):
                original, target = sources[source], targets[name]
                patch = make_patch(original, target)
                # Catch errors in the delta builder before publishing a profile.
                if apply_patch(original, patch, len(target)) != target:
                    raise ValueError(f"Cannot reproduce reference file: {name}")
                member = f"delta/{index}.bin"
                manifest["files"].append({"source": source, "name": name, "patch": member,
                                          "source_size": len(original), "source_sha256": digest(original),
                                          "size": len(target), "sha256": digest(target),
                                          "patch_sha256": digest(patch)})
                zip_write(archive, member, patch)
                print(f"Profiled {name}", flush=True)
            zip_write(archive, "manifest.json", json.dumps(manifest, indent=2).encode("utf-8") + b"\n")
        # Linking a staged file publishes without replacing an existing path.
        os.link(staged, destination)
    print(f"Created {destination} ({destination.stat().st_size:,} bytes)")


def read_manifest(archive):
    names = archive.namelist()
    expected = {"manifest.json"} | {f"delta/{i}.bin" for i in range(len(FILES))}
    if len(names) != len(expected) or set(names) != expected:
        raise ValueError("Unexpected or duplicate conversion profile members")
    if archive.getinfo("manifest.json").file_size > 1024 * 1024:
        raise ValueError("Conversion manifest is too large")
    manifest = json.loads(archive.read("manifest.json"))
    if not isinstance(manifest, dict) or manifest.get("schema") != SCHEMA:
        raise ValueError("Unsupported conversion profile version")
    rows = manifest.get("files")
    if not isinstance(rows, list) or len(rows) != len(FILES):
        raise ValueError("Incomplete conversion profile")
    for index, ((source, name), row) in enumerate(zip(FILES, rows)):
        if (not isinstance(row, dict) or row.get("source") != source
                or row.get("name") != name or row.get("patch") != f"delta/{index}.bin"):
            raise ValueError("Unexpected conversion filename")
        for field in ("source_size", "size"):
            if type(row.get(field)) is not int or not 0 < row[field] <= MAX_FILE:
                raise ValueError("Invalid conversion file size")
        for field in ("source_sha256", "sha256", "patch_sha256"):
            value = row.get(field)
            if not isinstance(value, str) or len(value) != 64 or any(c not in "0123456789abcdef" for c in value):
                raise ValueError("Invalid conversion digest")
        if archive.getinfo(row["patch"]).file_size > MAX_PATCH:
            raise ValueError("Conversion patch is too large")
    return manifest


def import_expansion(iso, profile, destination):
    destination = Path(destination).absolute()
    if destination.exists() or destination.is_symlink():
        raise ValueError(f"Output directory already exists: {destination}")
    with Disc(iso) as disc:
        sources = disc.sources()
    with zipfile.ZipFile(profile) as archive:
        manifest = read_manifest(archive)
        # No files are published until every source and reconstructed file passes.
        with tempfile.TemporaryDirectory(prefix=".nick-import-", dir=destination.parent) as temp:
            staged = Path(temp) / "N3DMM EXPANSION"
            staged.mkdir()
            for row in manifest["files"]:
                source = sources[row["source"]]
                if len(source) != row["source_size"] or digest(source) != row["source_sha256"]:
                    raise ValueError(f"ISO content does not match this profile: {row['source']}")
                patch = archive.read(row["patch"])
                if digest(patch) != row["patch_sha256"]:
                    raise ValueError(f"Damaged conversion patch: {row['name']}")
                data = apply_patch(source, patch, row["size"])
                if digest(data) != row["sha256"]:
                    raise ValueError(f"Reconstructed file failed verification: {row['name']}")
                (staged / row["name"]).write_bytes(data)
                print(f"Verified {row['name']}", flush=True)
            # Retain provenance beside the assets; Studio ignores this JSON file.
            (staged / "reconstruction.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
            if destination.exists() or destination.is_symlink():
                raise ValueError(f"Output directory already exists: {destination}")
            staged.rename(destination)
    print(f"Reconstructed all {len(FILES)} expansion archives in {destination}")


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="command", required=True)
    build = commands.add_parser("build-profile", help="One-time profile from a local reference expansion")
    build.add_argument("--iso", type=Path, required=True)
    build.add_argument("--reference", type=Path, required=True)
    build.add_argument("--profile", type=Path, required=True)
    convert = commands.add_parser("import", help="Recreate the expansion from the ISO and conversion profile")
    convert.add_argument("--iso", type=Path, required=True)
    convert.add_argument("--profile", type=Path, required=True)
    convert.add_argument("--output", type=Path, required=True, help="New directory; existing paths are refused")
    args = parser.parse_args(argv)
    try:
        if args.command == "build-profile":
            build_profile(args.iso, args.reference, args.profile)
        else:
            import_expansion(args.iso, args.profile, args.output)
    except (OSError, ValueError, KeyError, struct.error, zipfile.BadZipFile, RuntimeError) as error:
        print(f"nick-expansion: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
