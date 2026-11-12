"""One cached system-random report id per process; independent of crash/stall code."""
import argparse
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile

RUN_ALL_ARGS = ["--cc", "gcc"]
ROOT = Path(__file__).resolve().parents[1]
FIXTURE = r'''
#define _CRT_SECURE_NO_WARNINGS
#include "hos_report_run.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
int main(void) {
    const char *id = hos_report_run_id();
    assert(strlen(id) == 32);
    assert(id == hos_report_run_id());
    puts(id);
    return 0;
}
'''


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cc", default=shutil.which("gcc") or "clang")
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix="hookshot-report-run-") as tmp:
        src, exe = Path(tmp) / "run.c", Path(tmp) / "run.exe"
        src.write_text(FIXTURE, encoding="utf-8")
        cmd = [args.cc, "-std=c11", "-O2", "-Wall", "-Wextra", "-Werror", "-I", str(ROOT / "host"),
               str(src), str(ROOT / "host/hos_report_run.c"), "-o", str(exe)]
        if os.name == "nt":
            cmd += ["-ladvapi32"]
        subprocess.run(cmd, check=True, timeout=60)
        ids = [subprocess.run([str(exe)], capture_output=True, text=True, check=True, timeout=10).stdout.strip()
               for _ in range(2)]
        assert all(re.fullmatch(r"[0-9a-f]{32}", value) for value in ids)
        assert ids[0] != ids[1], ids
    print("PASS: two starts have different 128-bit ids; each process keeps its original id")


if __name__ == "__main__":
    main()
