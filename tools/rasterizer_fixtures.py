"""Extract verified BRender fixtures from the repository's original assets."""

import argparse
import hashlib
import json
from pathlib import Path
import struct
import subprocess
import sys
import tempfile


ROOT = Path(__file__).resolve().parents[1]
MANIFEST = ROOT / "tests/data/manifest.json"
DEFAULT_OUTPUT = ROOT / "tmp/bren-rasterizer-fixtures"


def verify_bytes(record, data):
    if len(data) != record["bytes"] or hashlib.sha256(data).hexdigest() != record["sha256"]:
        raise ValueError(f"{record.get('file', record.get('path'))}: reference hash mismatch")


def verify_fixtures(manifest, output):
    for record in manifest["files"]:
        path = output / record["file"]
        if not path.is_file():
            raise ValueError(f"missing fixture {path}; run rasterizer_fixtures.py generate first")
        verify_bytes(record, path.read_bytes())
    print(f"{len(manifest['files'])}/{len(manifest['files'])} fixture hashes match")


def palette_from_bitmap(data, palette):
    """Wrap the original BMP's 256 BGR0 colours in the legacy GLCR header."""
    if len(data) < 54 or data[:2] != b"BM":
        raise ValueError("expected a Windows BMP with a 256-colour palette")
    pixel_offset = struct.unpack_from("<I", data, 10)[0]
    header_bytes = struct.unpack_from("<I", data, 14)[0]
    planes, bits = struct.unpack_from("<HH", data, 26)
    compression = struct.unpack_from("<I", data, 30)[0]
    colours_used = struct.unpack_from("<I", data, 46)[0]
    start = 14 + header_bytes
    end = start + palette["entry_bytes"] * palette["count"]
    if (header_bytes < 40 or planes != 1 or bits != 8 or compression != 0
            or colours_used not in (0, 256) or end > pixel_offset or pixel_offset > len(data)):
        raise ValueError("expected an uncompressed 8-bit BMP with all 256 palette entries")
    header = struct.pack("<HHII", palette["byte_order"], palette["platform"],
                         palette["entry_bytes"], palette["count"])
    return header + data[start:end]


def generate(manifest, inspector, output):
    # Check original inputs before writing anything, even if old fixtures exist.
    for record in manifest["sources"].values():
        verify_bytes(record, (ROOT / record["path"]).read_bytes())
    templates = ROOT / manifest["sources"]["templates"]["path"]
    bitmap = ROOT / manifest["sources"]["palette_bitmap"]["path"]
    # A fresh staging directory prevents stale dumps masking extraction failures.
    with tempfile.TemporaryDirectory(prefix="rasterizer-fixtures-", dir=output.parent) as directory:
        staging = Path(directory)

        def inspect(*arguments):
            result = subprocess.run(
                [str(inspector), str(templates), *map(str, arguments)], cwd=ROOT,
                stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=60,
            )
            if result.returncode:
                raise ValueError(
                    f"inspect-chunks exited with {result.returncode}: "
                    + result.stderr.decode("utf-8", errors="replace")
                )
            return result.stdout

        for template in manifest["templates"]:
            destination = staging / template["directory"]
            destination.mkdir()
            inspect("dump-all", "TMPL", template["cno"], destination)
        for prop in manifest["props"]:
            inspect("dump", prop["tag"], prop["cno"], staging / prop["file"])
        actor = manifest["actor_manifest"]
        text = inspect("actor-manifest", "TMPL", actor["cno"]).replace(b"\r\n", b"\n")
        (staging / actor["file"]).write_bytes(text)
        palette = manifest["palette"]
        destination = staging / palette["file"]
        destination.parent.mkdir(parents=True, exist_ok=True)
        destination.write_bytes(palette_from_bitmap(bitmap.read_bytes(), palette))
        verify_fixtures(manifest, staging)
        # Publish only after all hashes pass. Preserve unrelated output files.
        for record in manifest["files"]:
            destination = output / record["file"]
            destination.parent.mkdir(parents=True, exist_ok=True)
            destination.write_bytes((staging / record["file"]).read_bytes())
    print(f"Generated verified fixtures into {output}")


def main(argv=None):
    manifest = json.loads(MANIFEST.read_text(encoding="utf-8"))
    if manifest["version"] != 1:
        raise ValueError("unsupported fixture manifest version")
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="command", required=True)
    for name in ("generate", "check"):
        command = commands.add_parser(name)
        command.add_argument("--output", type=Path, default=DEFAULT_OUTPUT)
        if name == "generate":
            command.add_argument("--inspect-chunks", type=Path, required=True)
    args = parser.parse_args(argv)
    output = args.output.resolve()
    if args.command == "check":
        verify_fixtures(manifest, output)
        return 0
    scratch = (ROOT / "tmp").resolve()
    if not output.is_relative_to(scratch) or output == scratch:
        parser.error("--output must be a subdirectory of the repository's ignored tmp/ directory")
    inspector = args.inspect_chunks.resolve()
    if not inspector.is_file():
        parser.error(f"inspect-chunks executable does not exist: {inspector}")
    output.parent.mkdir(parents=True, exist_ok=True)
    generate(manifest, inspector, output)
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (OSError, ValueError, subprocess.SubprocessError) as error:
        print(f"error: {error}", file=sys.stderr)
        sys.exit(1)
