#!/usr/bin/env python3
"""Convert Nickelodeon disc content into 3DMM expansion assets using core libraries.

Import uses the Nickelodeon/base retail ISOs and local image fixups. A reference
expansion is needed only to build those fixups. Code performs the reproducible
chunk transformations; fixups supply the remaining historical image differences.
No image data or fixups are bundled with source. No original executable,
installer, whole-archive patch or external extractor is used.
"""

import argparse
import hashlib
import json
import os
from pathlib import Path
import struct
import subprocess
import re
import zipfile
import zlib
import sys
import tempfile

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
MAX_FILE = 256 * 1024 * 1024
SECTOR = 2048


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

    def files(self, directory, filenames):
        root = self.entries(self.root)
        if directory not in root:
            raise ValueError(f"This ISO does not contain the {directory} directory")
        entries = self.entries(root[directory])
        result = {}
        for source in filenames:
            entry = entries.get(source)
            if entry is None or entry[3] or not 0 < entry[2] <= MAX_FILE:
                raise ValueError(f"Missing or invalid disc file: {source}")
            result[source] = self.read(entry[1], entry[2])
        return result


SCHEMA = "3dmm-nick-image-fixups-1"
RULES = "nick-chunk-rules-1-pencil-palette"
MAX_IMAGE = 128 * 1024 * 1024
IMAGE_NAME = re.compile(r"[0-9]-(474c4352|4d424d50|544d4150)-[0-9a-f]{8}\.bin\Z")


def file_digest(path):
    result = hashlib.sha256()
    with open(path, "rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            result.update(block)
    return result.hexdigest()


def run(converter, *args):
    subprocess.run([str(converter), *map(str, args)], check=True)


def prepare(iso, base_iso, converter, root, expected_inputs=None):
    with Disc(iso) as disc:
        sources = disc.files("NICK3D", [source for source, _ in FILES] + ["NICK3D.CHK"])
    with Disc(base_iso) as disc:
        base = disc.files("3DMOVIE", ["3DMOVIE.CHK"])["3DMOVIE.CHK"]
    inputs = {name: digest(data) for name, data in sources.items()}
    inputs["BASE/3DMOVIE.CHK"] = digest(base)
    if expected_inputs is not None and inputs != expected_inputs:
        raise ValueError("ISO content does not match these image fixups")
    source_dir, baseline = root / "source", root / "baseline"
    source_dir.mkdir()
    baseline.mkdir()
    for name, data in sources.items():
        (source_dir / name).write_bytes(data)
    resources = root / "base.chk"
    resources.write_bytes(base)
    run(converter, "convert", "--nearest-palette", source_dir, resources, baseline)
    return baseline, inputs


def export_images(converter, directory, destination):
    destination.mkdir()
    run(converter, "export-images", directory, destination)
    paths = {path.name: path for path in destination.iterdir()}
    if not paths or any(not IMAGE_NAME.fullmatch(name) for name in paths):
        raise ValueError("Unexpected exported image identity")
    return paths


def state_digest(converter, directory, destination):
    run(converter, "non-image-state", directory, destination)
    return file_digest(destination)


def residual(source, target, mode):
    padded = source[:len(target)] + bytes(max(0, len(target) - len(source)))
    if mode == "xor":
        return bytes(a ^ b for a, b in zip(padded, target))
    return bytes((b - a) & 255 for a, b in zip(padded, target))


def restore(source, delta, mode):
    padded = source[:len(delta)] + bytes(max(0, len(delta) - len(source)))
    if mode == "xor":
        return bytes(a ^ b for a, b in zip(padded, delta))
    if mode == "add":
        return bytes((a + b) & 255 for a, b in zip(padded, delta))
    raise ValueError("Unsupported image residual mode")


def zip_write(archive, name, data):
    info = zipfile.ZipInfo(name, date_time=(1980, 1, 1, 0, 0, 0))
    info.compress_type = zipfile.ZIP_DEFLATED
    info.external_attr = 0o100644 << 16
    archive.writestr(info, data, compresslevel=9)


def build_fixups(iso, base_iso, converter, reference, destination):
    converter, reference = Path(converter).resolve(strict=True), Path(reference).resolve(strict=True)
    destination = Path(destination).absolute()
    if destination.exists() or destination.is_symlink():
        raise ValueError(f"Fixup file already exists: {destination}")
    with tempfile.TemporaryDirectory(prefix=".nick-fixups-", dir=destination.parent) as temp:
        root = Path(temp)
        baseline, inputs = prepare(iso, base_iso, converter, root)
        state = state_digest(converter, baseline, root / "baseline.state")
        if state != state_digest(converter, reference, root / "reference.state"):
            raise ValueError("Reference differs in non-image data/metadata; image fixups cannot hide those differences")
        originals = export_images(converter, baseline, root / "baseline-images")
        targets = export_images(converter, reference, root / "reference-images")
        if originals.keys() != targets.keys():
            raise ValueError("Reference contains different image chunk identities")
        manifest = {"schema": SCHEMA, "rules": RULES, "inputs": inputs,
                    "non_image_state_sha256": state, "images": []}
        changed = 0
        staged = root / "fixups.zip"
        with zipfile.ZipFile(staged, "w") as archive:
            for name in sorted(originals):
                source, target = originals[name].read_bytes(), targets[name].read_bytes()
                if not 0 < len(target) <= MAX_IMAGE:
                    raise ValueError(f"Invalid target image size: {name}")
                row = {"name": name, "size": len(target), "sha256": digest(target)}
                if source != target:
                    candidates = [(mode, residual(source, target, mode)) for mode in ("xor", "add")]
                    mode, delta = min(candidates, key=lambda pair: len(zlib.compress(pair[1], 9)))
                    if restore(source, delta, mode) != target:
                        raise ValueError(f"Cannot reconstruct image: {name}")
                    row.update({"mode": mode, "source_sha256": digest(source),
                                "delta_sha256": digest(delta)})
                    zip_write(archive, "images/" + name, delta)
                    changed += 1
                manifest["images"].append(row)
            zip_write(archive, "manifest.json", json.dumps(manifest, indent=2).encode() + b"\n")
        # Verify core chunk replacement against the reference independently of
        # the residual encoder. IDs, links and all decoded payloads must match.
        checked = root / "checked"
        checked.mkdir()
        run(converter, "replace-images", baseline, root / "reference-images", checked)
        run(converter, "compare", checked, reference)
        os.link(staged, destination)
    print(f"Created {destination} ({destination.stat().st_size:,} bytes; {changed} corrected image chunks)")


def valid_digest(value):
    return isinstance(value, str) and re.fullmatch(r"[0-9a-f]{64}", value) is not None


def read_manifest(archive):
    if archive.getinfo("manifest.json").file_size > 8 * 1024 * 1024:
        raise ValueError("Fixup manifest is too large")
    manifest = json.loads(archive.read("manifest.json"))
    if not isinstance(manifest, dict) or manifest.get("schema") != SCHEMA or manifest.get("rules") != RULES:
        raise ValueError("Unsupported image fixups or chunk-rule version")
    if not valid_digest(manifest.get("non_image_state_sha256")):
        raise ValueError("Invalid non-image state digest")
    inputs = manifest.get("inputs")
    expected_inputs = {name for name, _ in FILES} | {"NICK3D.CHK", "BASE/3DMOVIE.CHK"}
    if not isinstance(inputs, dict) or set(inputs) != expected_inputs or not all(map(valid_digest, inputs.values())):
        raise ValueError("Invalid source digest table")
    rows = manifest.get("images")
    if not isinstance(rows, list) or not 0 < len(rows) <= 20000:
        raise ValueError("Invalid image fixup table")
    names, members, total = set(), {"manifest.json"}, 0
    for row in rows:
        if not isinstance(row, dict):
            raise ValueError("Invalid image entry")
        name = row.get("name")
        if not isinstance(name, str) or not IMAGE_NAME.fullmatch(name) or name in names:
            raise ValueError("Invalid or duplicate image chunk identity")
        names.add(name)
        if type(row.get("size")) is not int or not 0 < row["size"] <= MAX_IMAGE or not valid_digest(row.get("sha256")):
            raise ValueError("Invalid image size or digest")
        if "mode" in row:
            if (row["mode"] not in ("xor", "add") or not valid_digest(row.get("source_sha256"))
                    or not valid_digest(row.get("delta_sha256"))):
                raise ValueError("Invalid image residual")
            member = "images/" + name
            if archive.getinfo(member).file_size != row["size"]:
                raise ValueError("Invalid image residual length")
            members.add(member)
            total += row["size"]
    if total > 512 * 1024 * 1024 or len(archive.namelist()) != len(members) or set(archive.namelist()) != members:
        raise ValueError("Unexpected, duplicate or oversized fixup members")
    return manifest


def import_expansion(iso, base_iso, converter, fixups, destination):
    converter = Path(converter).resolve(strict=True)
    destination = Path(destination).absolute()
    if destination.exists() or destination.is_symlink():
        raise ValueError(f"Output directory already exists: {destination}")
    with zipfile.ZipFile(fixups) as archive, tempfile.TemporaryDirectory(
            prefix=".nick-import-", dir=destination.parent) as temp:
        manifest = read_manifest(archive)
        root = Path(temp)
        baseline, _ = prepare(iso, base_iso, converter, root, manifest["inputs"])
        if state_digest(converter, baseline, root / "baseline.state") != manifest["non_image_state_sha256"]:
            raise ValueError("Chunk rules produced different non-image data/metadata")
        originals = export_images(converter, baseline, root / "baseline-images")
        if set(originals) != {row["name"] for row in manifest["images"]}:
            raise ValueError("Image chunk identities do not match the fixup manifest")
        corrected = root / "corrections"
        corrected.mkdir()
        for row in manifest["images"]:
            source = originals[row["name"]].read_bytes()
            target = source
            if "mode" in row:
                if digest(source) != row["source_sha256"]:
                    raise ValueError(f"Image baseline does not match: {row['name']}")
                delta = archive.read("images/" + row["name"])
                if digest(delta) != row["delta_sha256"]:
                    raise ValueError(f"Damaged image correction: {row['name']}")
                target = restore(source, delta, row["mode"])
                (corrected / row["name"]).write_bytes(target)
            if len(target) != row["size"] or digest(target) != row["sha256"]:
                raise ValueError(f"Historical image verification failed: {row['name']}")
        output = root / "output"
        output.mkdir()
        run(converter, "replace-images", baseline, corrected, output)
        # Reopen the saved files: checking in-memory patches is insufficient.
        if state_digest(converter, output, root / "output.state") != manifest["non_image_state_sha256"]:
            raise ValueError("Saving changed non-image data/metadata")
        saved = export_images(converter, output, root / "saved-images")
        if set(saved) != set(originals):
            raise ValueError("Saving changed image identities")
        for row in manifest["images"]:
            if file_digest(saved[row["name"]]) != row["sha256"]:
                raise ValueError(f"Saved historical image verification failed: {row['name']}")
        provenance = {"schema": SCHEMA, "rules": RULES, "image_fixups_sha256": file_digest(fixups),
                      "non_image_state_sha256": manifest["non_image_state_sha256"],
                      "historical_decoded_images_verified": True,
                      "archives": {name: file_digest(output / name) for _, name in FILES}}
        (output / "reconstruction.json").write_text(json.dumps(provenance, indent=2) + "\n", encoding="utf-8")
        if destination.exists() or destination.is_symlink():
            raise ValueError(f"Output directory already exists: {destination}")
        output.rename(destination)
    print(f"Created {destination}; all historical image chunks and non-image data verified.")


def main(argv=None):
    root = Path(__file__).resolve().parents[1]
    default_converter = root / "build/studio-portable-x64" / ("nick-expansion.exe" if os.name == "nt" else "nick-expansion")
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="command", required=True)
    for command in ("build-fixups", "import"):
        child = commands.add_parser(command)
        child.add_argument("--iso", type=Path, required=True, help="Nickelodeon retail ISO")
        child.add_argument("--base-iso", type=Path, required=True, help="Base 3DMM retail ISO (provides its palette)")
        child.add_argument("--converter", type=Path, default=default_converter, help="Built nick-expansion executable")
        child.add_argument("--fixups", type=Path, required=True, help="Local image-fixup ZIP; never bundled in source")
        if command == "build-fixups":
            child.add_argument("--reference", type=Path, required=True)
        else:
            child.add_argument("--output", type=Path, required=True, help="New directory; existing paths are refused")
    args = parser.parse_args(argv)
    try:
        if args.command == "build-fixups":
            build_fixups(args.iso, args.base_iso, args.converter, args.reference, args.fixups)
        else:
            import_expansion(args.iso, args.base_iso, args.converter, args.fixups, args.output)
    except (OSError, ValueError, KeyError, struct.error, zipfile.BadZipFile, RuntimeError, subprocess.SubprocessError) as error:
        print(f"nick-expansion: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
