"""Generate BRender captures locally and check preserved reference hashes."""

import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys


ROOT = Path(__file__).resolve().parents[1]
MANIFEST = ROOT / "tests/golden/manifest.json"
DEFAULT_OUTPUT = ROOT / "tmp/bren-rasterizer-goldens"
DEFAULT_FIXTURES = ROOT / "tmp/bren-rasterizer-fixtures"


def image_info(data):
    """Read the binary PGM/PPM format emitted by the renderer harness."""
    magic, dimensions, maximum, pixels = data.split(b"\n", 3)
    if magic not in (b"P5", b"P6") or maximum != b"255":
        raise ValueError("expected a binary PGM/PPM with maximum 255")
    width, height = map(int, dimensions.split())
    channels = 1 if magic == b"P5" else 3
    if width <= 0 or height <= 0 or len(pixels) != width * height * channels:
        raise ValueError("image dimensions do not match the pixel data")
    return width, height


def check_capture(record, data):
    width, height = image_info(data)
    if (width, height) != (record["width"], record["height"]):
        raise ValueError(
            f"dimensions {width}x{height}; reference is "
            f"{record['width']}x{record['height']} (captured in {record['captured_in'][:12]})"
        )
    if len(data) != record["bytes"] or hashlib.sha256(data).hexdigest() != record["sha256"]:
        raise ValueError("image differs from the preserved reference hash")


def check(records, output):
    failures = 0
    for record in records:
        try:
            check_capture(record, (output / record["file"]).read_bytes())
        except (OSError, ValueError) as error:
            print(f"FAIL {record['file']}: {error}", file=sys.stderr)
            failures += 1
    print(f"{len(records) - failures}/{len(records)} reference images match")
    return 1 if failures else 0


def generate(manifest, records, executable, output, fixtures=DEFAULT_FIXTURES):
    """Render each scene once; never accept files left by an earlier run."""
    environment = os.environ.copy()
    environment["BREN_TEST_PALETTE"] = manifest["palette"].replace("{fixtures}", str(fixtures))
    scenes = list(dict.fromkeys((record["scene"], record["arch"]) for record in records))
    for scene, arch in scenes:
        pgm = output / f"{scene}.{arch}.pgm"
        for suffix in (".pgm", ".ppm"):
            pgm.with_suffix(suffix).unlink(missing_ok=True)
        arguments = [
            str(pgm) if arg == "{output}" else arg.replace("{fixtures}", str(fixtures))
            for arg in manifest["recipes"][scene]
        ]
        print(f"Rendering {scene}.{arch}", flush=True)
        result = subprocess.run(
            [str(executable), *arguments], cwd=ROOT, env=environment,
            stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=60,
        )
        log = output / f"{scene}.{arch}.log"
        log.write_bytes(result.stdout)
        if result.returncode:
            raise ValueError(f"renderer exited with {result.returncode}; see {log}")
        for record in records:
            if (record["scene"], record["arch"]) == (scene, arch):
                path = output / record["file"]
                if not path.is_file():
                    raise ValueError(f"renderer did not produce {path}; see {log}")
                image_info(path.read_bytes())
    print(f"Rendered {len(scenes)} scenes into {output}")


def restore(manifest, records, output):
    """Recover exact historical captures without running historical binaries."""
    revision = manifest["reference_revision"]
    for record in records:
        result = subprocess.run(
            ["git", "show", f"{revision}:tests/golden/{record['file']}"],
            cwd=ROOT, stdout=subprocess.PIPE, stderr=subprocess.PIPE, check=True,
        )
        check_capture(record, result.stdout)
        (output / record["file"]).write_bytes(result.stdout)
    print(f"Recovered {len(records)} reference images from {revision[:12]} into {output}")


def main(argv=None):
    manifest = json.loads(MANIFEST.read_text(encoding="utf-8"))
    if manifest["version"] != 1:
        raise ValueError("unsupported golden manifest version")
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="command", required=True)
    for name, help_text in (
        ("generate", "render scenes using a supplied harness executable"),
        ("check", "compare local images with the preserved hashes"),
        ("restore", "recover exact reference images from Git history"),
    ):
        command = commands.add_parser(name, help=help_text)
        command.add_argument("--output", type=Path, default=DEFAULT_OUTPUT)
        command.add_argument("--arch", choices=("x86", "x64"), required=name == "generate")
        command.add_argument("--scene", action="append", choices=sorted(manifest["recipes"]))
        if name == "generate":
            command.add_argument("--exe", type=Path, required=True)
            command.add_argument("--fixtures", type=Path, default=DEFAULT_FIXTURES)
            command.add_argument("--check", action="store_true", help="also check against reference hashes")
    args = parser.parse_args(argv)
    output = args.output.resolve()
    records = [
        record for record in manifest["captures"]
        if (args.arch is None or record["arch"] == args.arch)
        and (args.scene is None or record["scene"] in args.scene)
    ]
    if not records:
        parser.error("no reference captures match the requested architecture and scenes")
    if args.command == "check":
        return check(records, output)
    # Generated media belongs in the repository's ignored scratch directory.
    scratch = (ROOT / "tmp").resolve()
    if not output.is_relative_to(scratch) or output == scratch:
        parser.error("--output must be a subdirectory of the repository's ignored tmp/ directory")
    if args.command == "generate" and not args.exe.resolve().is_file():
        parser.error(f"renderer executable does not exist: {args.exe}")
    if args.command == "generate":
        subprocess.run([
            sys.executable, "-B", str(ROOT / "tools/rasterizer_fixtures.py"),
            "check", "--output", str(args.fixtures.resolve()),
        ], check=True)
    output.mkdir(parents=True, exist_ok=True)
    if args.command == "restore":
        restore(manifest, records, output)
        return 0
    generate(manifest, records, args.exe.resolve(), output, args.fixtures.resolve())
    return check(records, output) if args.check else 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (OSError, ValueError, subprocess.SubprocessError) as error:
        print(f"error: {error}", file=sys.stderr)
        sys.exit(1)
