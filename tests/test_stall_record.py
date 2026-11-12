"""Game-free stall policy and real multithreaded child; runs on Linux and Windows.

The first frame sampled from the blocked worker must fall inside syscall(SYS_read)
on Linux / ntdll NtWaitForSingleObject on Windows. A second worker stays live and both join afterwards. A real
32-second child presents once a second and must leave no report. No GPU or game.
"""
import argparse
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile

RUN_ALL_ARGS = ["--cc", "gcc"]
ROOT = Path(__file__).resolve().parents[1]


def run(exe, mode, folder, env):
    result = subprocess.run([str(exe), mode, str(folder)], text=True, capture_output=True,
                            timeout=55, env=env)
    print(result.stdout, end="")
    print(result.stderr, end="")
    if result.returncode:
        raise AssertionError(f"{mode} child exit {result.returncode}")
    return result.stdout


def records(folder):
    return sorted((folder / "bug-reports" / "pending").glob("stall-*.txt"))


def function(source, marker):
    start = source.index(marker)
    brace = source.index("{", start)
    depth, end = 1, brace + 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cc", default=shutil.which("gcc") or "clang")
    parser.add_argument("--source", type=Path, default=ROOT, help="source tree to check, including an unmodified base")
    parser.add_argument("--output", type=Path, help="retain this run's authored fixture and reports")
    args = parser.parse_args()
    # Log seams, not an API bump. These exact format literals are gpu_hang_watch_start's
    # 10/30-second warning and terminal line (vk_replay.cpp), and checkpoints_report's
    # first line (vk_draw.cpp), before querying a possibly unresponsive driver.
    replay = (args.source / "gpu" / "vk" / "vk_replay.cpp").read_text(encoding="utf-8")
    draw = (args.source / "gpu" / "vk" / "vk_draw.cpp").read_text(encoding="utf-8")
    assert '"vk: the GPU has finished nothing for %d s: the timeline at %llu, %llu sent (RX_VK_GPU_HANG_SEC=%d)"' in replay
    assert '"vk: GPU hang: the timeline at %llu, %llu sent"' in replay
    assert '"vk: device lost: reading %s"' in draw
    with tempfile.TemporaryDirectory(prefix="stall-record-") as temp:
        out = Path(temp)
        source = (args.source / "host" / "hos_stall.c").read_text(encoding="utf-8")
        capture = function(source, "static void capture_thread(")
        fake = (ROOT / "tests" / "stall_suspend_test.c").read_text(encoding="utf-8").replace("@CAPTURE@", capture)
        fake_src, fake_exe = out / "suspend.c", out / "suspend.exe"
        fake_src.write_text(fake, encoding="utf-8")
        subprocess.run([args.cc, "-std=c11", "-O2", "-Wall", "-Wextra", "-Werror", str(fake_src), "-o", str(fake_exe)], check=True)
        exe = out / "stall-fixture.exe"
        flags = ["-std=c11", "-O2", "-g", "-fno-omit-frame-pointer", "-Wall", "-Wextra", "-Werror",
                 "-D_CRT_SECURE_NO_WARNINGS", "-DHOS_STALL_TEST", "-I", str(args.source / "host")]
        libs = ["-ldbghelp", "-ladvapi32"] if os.name == "nt" else ["-pthread", "-ldl"]
        command = [args.cc, *flags, str(ROOT / "tests" / "stall_test.c"), str(args.source / "host" / "hos_stall.c"),
                   str(args.source / "host" / "hos_report_run.c"), "-o", str(exe), *libs]
        print("COMPILE:", " ".join(command))
        subprocess.run(command, check=True)
        subprocess.run([str(fake_exe)], check=True, timeout=10)
        base_env = {k: v for k, v in os.environ.items() if not k.startswith("HOS_STALL_")}
        base_env["HOS_HEADLESS"] = "1"
        policy = subprocess.run([str(exe), "policy"], check=True, text=True, capture_output=True, timeout=10, env=base_env)
        print(policy.stdout, end="")
        blocked = out / "private-user-child"
        blocked.mkdir()
        text = run(exe, "stacks", blocked, dict(base_env, HOS_STALL_STACKS="1"))
        files = records(blocked)
        assert len(files) == 2, files
        record = next(p for p in files if not p.name.endswith(".stacks.txt"))
        stack_file = record.with_suffix(".stacks.txt")
        report, stacks = record.read_text(encoding="utf-8"), stack_file.read_text(encoding="utf-8")
        assert report.startswith("Hookshot stall record 1\n")
        assert "resumed: yes, after 40 s\n" in report
        assert re.search(r"^run_id: [0-9a-f]{32}$", report, re.M)
        for name in ("version", "when", "uptime_s", "reason", "thread", "module_offset", "last_present_age_ms", "log"):
            assert re.search(rf"^{name}: .+$", report, re.M), name
        tid = int(re.search(r"BLOCKED=(\d+)", text).group(1))
        expected = re.search(r"MODULE=(\S+) OFFSET=([0-9a-f]+)", text)
        wanted_module, wanted_offset = expected[1], int(expected[2], 16)
        block = re.search(rf'^thread {tid} "mkw_vk worker" \(partial stack\)\n((?:  .*\n)+)', stacks, re.M)
        assert block, stacks
        frames = re.findall(r"^  \d+ (\S+)\+0x([0-9a-f]+)$", block[1], re.M)
        assert frames and frames[0][0].lower() == wanted_module.lower(), (frames, expected.groups())
        actual = int(frames[0][1], 16)
        assert wanted_offset <= actual < wanted_offset + 1024, (hex(actual), hex(wanted_offset), frames)
        assert 3 <= len(re.findall(r"^thread ", stacks, re.M)) <= 512
        assert "virtual core 0: host thread 0" in stacks
        assert "private-user" not in report + stacks and "@example.test" not in report + stacks
        assert str(blocked) not in report + stacks
        assert not re.search(r"[A-Za-z]:[\\/]|/home/|/Users/", report + stacks)
        print("PASS: blocked worker's interrupted PC is inside", wanted_module, "+", hex(actual), "(blocking call)")
        print("PASS: names outside the host allow list omitted; no home path, raw stack bytes, or private labels")
        # Windows text-only default, Linux's explicit stack opt-out, share identical record behavior.
        text_only = out / "text-only"
        text_only.mkdir()
        env = base_env if os.name == "nt" else dict(base_env, HOS_STALL_STACKS="0")
        run(exe, "text", text_only, env)
        only = records(text_only)
        assert len(only) == 1 and "stacks: not collected (switch off)" in only[0].read_text(encoding="utf-8")
        slow = out / "slow"
        slow.mkdir()
        run(exe, "slow", slow, base_env)
        assert not records(slow)
        if args.output:
            args.output.mkdir(parents=True, exist_ok=False)
            shutil.copytree(out, args.output / "child-evidence")
    print("PASS: stall detector, real stack collector, recovery, privacy, text-only mode, and slow-child no-record")


if __name__ == "__main__":
    main()
