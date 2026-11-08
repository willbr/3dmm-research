#!/usr/bin/env python3
"""Read-only inventory of local v3dmm v1 movie bundles for format research.

This does not install expansions, extract files or implement movie playback.
Run with --verify-zip to check every embedded archive's CRCs. JSON goes to
stdout; redirect it to tmp/ when retaining evidence. Uses only the stdlib.
"""

import argparse
from collections import Counter
import hashlib
import io
import json
from pathlib import Path
import struct
import zipfile


def inspect_movie(path, verify_zip=False):
    path = Path(path)
    total = path.stat().st_size
    entries = []
    member_types = Counter()
    expanded_bytes = 0
    with path.open("rb") as stream:

        def read_exact(length):
            if length < 0 or length > total - stream.tell():
                raise ValueError(f"{path}: out-of-bounds read at {stream.tell()}")
            data = stream.read(length)
            if len(data) != length:
                raise ValueError(f"{path}: short read")
            return data

        if read_exact(6) != b"v3dmm\x01":
            raise ValueError(f"{path}: expected v3dmm version 1")
        count = struct.unpack("<I", read_exact(4))[0]
        if count > (total - stream.tell()) // 6:
            raise ValueError(f"{path}: impossible dependency count")
        for _ in range(count):
            size, length = struct.unpack("<IH", read_exact(6))
            name_bytes = read_exact(length)
            entries.append(
                {
                    "name": name_bytes.decode("cp1252", errors="backslashreplace"),
                    "name_hex": name_bytes.hex(),
                    "size": size,
                }
            )
        header_bytes = stream.tell()
        for entry in entries:
            if not entry["size"]:
                continue
            entry["block_offset"] = stream.tell()
            if read_exact(2) != b"\xf0\x0b":
                raise ValueError(f"{path}: missing expansion start marker")
            archive_bytes = read_exact(entry["size"])
            if read_exact(2) != b"\xfe\xeb":
                raise ValueError(f"{path}: missing expansion end marker")
            entry["sha256"] = hashlib.sha256(archive_bytes).hexdigest()
            with zipfile.ZipFile(io.BytesIO(archive_bytes)) as archive:
                members = archive.infolist()
                if verify_zip:
                    bad = archive.testzip()
                    if bad is not None:
                        raise ValueError(f"{path}: CRC failure in {bad}")
                entry["members"] = [member.filename for member in members]
                entry["configs"] = {}
                for member in members:
                    member_types[Path(member.filename).suffix.lower()] += 1
                    expanded_bytes += member.file_size
                    if member.filename.lower().endswith(".cfg"):
                        entry["configs"][member.filename] = archive.read(member).decode(
                            "cp1252", errors="backslashreplace"
                        )
        movie_offset = stream.tell()
        movie_signature = read_exact(8)
        if movie_signature[:4] != b"CHN2":
            raise ValueError(f"{path}: movie payload does not begin with CHN2")
    return {
        "file": str(path),
        "total_bytes": total,
        "dependency_count": count,
        "embedded_count": sum(bool(entry["size"]) for entry in entries),
        "external_dependencies": [entry["name"] for entry in entries if not entry["size"]],
        "header_bytes": header_bytes,
        "movie_offset": movie_offset,
        "movie_bytes": total - movie_offset,
        "movie_signature_hex": movie_signature.hex(),
        "zip_member_types": dict(sorted(member_types.items())),
        "expanded_bytes": expanded_bytes,
        "zip_crc_verified": verify_zip,
        "dependencies": entries,
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("movies", nargs="+", type=Path)
    parser.add_argument("--verify-zip", action="store_true")
    args = parser.parse_args()
    try:
        reports = [inspect_movie(path, args.verify_zip) for path in args.movies]
    except (OSError, ValueError, zipfile.BadZipFile, RuntimeError) as error:
        parser.exit(1, f"v3dmm research: {error}\n")
    print(json.dumps(reports, indent=2, ensure_ascii=True))


if __name__ == "__main__":
    main()
