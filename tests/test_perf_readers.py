"""Compile the complete sampler reader and its authored-input fixtures; no game, driver or sensor is queried.

python -I -B tests/test_perf_readers.py [--cxx g++]
"""
import argparse
import os
from pathlib import Path
import subprocess
import tempfile

RUN_ALL_ARGS = ["--cxx", "{cxx}"]
ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cxx", default="g++")
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix="perf-readers-") as tmp:
        exe = Path(tmp) / ("perf-readers.exe" if os.name == "nt" else "perf-readers")
        # Keep every production translation unit on the compiler command line: the Windows-target
        # fixture capture must see them, not only a generated/extracted subset of the reader.
        sources = [
            str(ROOT / "tests" / "test_perf_readers.cpp"),
            str(ROOT / "gpu" / "vk" / "perf_readers.cpp"),
            str(ROOT / "gpu" / "vk" / "perf_monitor.cpp"),
        ]
        compiler = Path(args.cxx).name.lower()
        if compiler in {"cl", "cl.exe", "clang-cl", "clang-cl.exe"}:
            command = [args.cxx, "/nologo", "/std:c++20", "/W4", "/WX", "/EHsc",
                       *sources, "/Fe:" + str(exe)]
        else:
            command = [args.cxx, "-std=c++20", "-Wall", "-Wextra", "-Werror", *sources, "-o", str(exe)]
            if os.name != "nt":
                command.append("-ldl")
        subprocess.run(command, cwd=tmp, check=True)
        subprocess.run([str(exe)], cwd=tmp, check=True)
    print("PASS: complete reader implementation and fake-input fixtures (no vendor link dependency)")


if __name__ == "__main__":
    main()
