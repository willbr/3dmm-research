"""Build real chunky input, including nested-header invalidation and failures."""
import argparse
import hashlib
import pathlib
import subprocess
import time


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("cmake")
    parser.add_argument("compiler")
    parser.add_argument("chomp")
    parser.add_argument("module", type=pathlib.Path)
    parser.add_argument("artifacts", type=pathlib.Path)
    parser.add_argument("--msvc", default="")
    args = parser.parse_args()
    root = args.artifacts.resolve() / "project with spaces"
    root.mkdir(parents=True, exist_ok=True)
    (root / "include").mkdir(exist_ok=True)
    (root / "include" / "outer.chh").write_text('#include "inner.chh"\n', encoding="utf-8")
    header = root / "include" / "inner.chh"
    header.write_text("#define VALUE 21\n", encoding="utf-8")
    source = root / "fixture.cht"
    source.write_text('''#include "include/outer.chh"
#if SELECT_VALUE != 20
#error Target compile definitions did not reach the preprocessor
#endif
CHUNK('TEST', 1, "pipeline fixture")
BYTE VALUE
ENDCHUNK
''', encoding="utf-8")
    module = args.module.resolve().as_posix()
    compiler = pathlib.Path(args.compiler).resolve().as_posix()
    chomp = pathlib.Path(args.chomp).resolve().as_posix()
    (root / "CMakeLists.txt").write_text(f'''cmake_minimum_required(VERSION 3.22)
project(ChompPipeline NONE)
set(CMAKE_CXX_COMPILER "{compiler}")
set(MSVC "{args.msvc}")
set(CHOMP_EXECUTABLE "{chomp}" CACHE FILEPATH "")
include("{module}")
add_custom_target(resources)
set_property(TARGET resources PROPERTY COMPILE_DEFINITIONS SELECT_VALUE=20)
target_chomp_sources(resources "${{CMAKE_CURRENT_SOURCE_DIR}}/fixture.cht")
''', encoding="utf-8")
    build = root / "build with spaces"
    subprocess.run([args.cmake, "-S", str(root), "-B", str(build), "-G", "Ninja"], check=True)
    command = [args.cmake, "--build", str(build), "--target", "resources"]
    subprocess.run(command, check=True)
    output = build / "3dmovie" / "fixture.chk"
    original = hashlib.sha256(output.read_bytes()).digest()
    timestamp = output.stat().st_mtime_ns
    subprocess.run(command, check=True)
    assert output.stat().st_mtime_ns == timestamp, "Unchanged inputs rebuilt the archive"
    time.sleep(0.02)
    header.write_text("#define VALUE 22\n", encoding="utf-8")
    subprocess.run(command, check=True)
    assert hashlib.sha256(output.read_bytes()).digest() != original, "Nested include edit did not rebuild the archive"
    source.write_text("THIS_IS_NOT_A_CHUNK\n", encoding="utf-8")
    result = subprocess.run(command, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    assert result.returncode != 0, "A failed chunky compilation reported success"
    print("Chomp pipeline: target definitions, paths with spaces, no-op build, nested includes and failure propagation passed")


if __name__ == "__main__":
    main()
