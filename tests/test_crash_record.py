"""Game-free crash records, real fatal children, and locked unclean-end markers.

Uses the real writer, crash adapter, hos_fatal/logger, control quit and restart
functions. Linux uses the real POSIX exception adapter with core dumps disabled;
Windows uses the real exception filter and optionally DbgHelp on a tiny child.
"""
import argparse
from datetime import datetime, timedelta, timezone
import os
from pathlib import Path
import re
import shutil
import signal
import subprocess
import tempfile

RUN_ALL_ARGS = ["--cc", "gcc"]
ROOT = Path(__file__).resolve().parents[1]


def block(text, marker):
    assert text.count(marker) == 1, marker
    start = text.index(marker)
    end = text.index("{", start) + 1
    depth = 1
    while depth:
        depth += (text[end] == "{") - (text[end] == "}")
        end += 1
    return text[start:end]


def source_contract():
    glue = (ROOT / "host/mkw_glue.c").read_text(encoding="utf-8")
    threads = (ROOT / "host/hos_thread.c").read_text(encoding="utf-8")
    report = block(glue, "static LONG WINAPI report_crash(")
    assert report.index("record_exception(info);") < report.index("GetModuleHandleExA(")
    assert 'if (code == 0xC0000374 || code == 0xC0000409) report_crash(info);' in glue
    assert 'SetUnhandledExceptionFilter(report_crash);' in glue
    assert 'AddVectoredExceptionHandler(1, commit_on_touch);' in glue
    for line in ('CRASH: exception %#lx (%s %#llx, guest %#llx) at %s+%#llx',
                 'CRASH: host backtrace:', 'CRASH: thread %s, sp %#llx',
                 'CRASH: guest backtrace: lr %#llx',
                 'CRASH: [x%d+%#05x] %016llx %016llx %016llx %016llx'):
        assert line in report, line
    fatal = block(threads, "void hos_fatal(")
    assert fatal.index("hos_crash_record_fatal(") < fatal.index("vsnprintf(")
    assert "log_drain_raw();" in block(threads, "void hos_log_flush_crash(")
    renderer = (ROOT / "gpu/vk/vk_replay.cpp").read_text(encoding="utf-8")
    replay = block(renderer, "[[noreturn]] void replay_fatal(")
    assert replay.index('logf("vk: FATAL:') < replay.index("std::_Exit(3)")
    assert 'strstr(buf, "VK_ERROR_DEVICE_LOST") ? "device lost"' in threads
    main = (ROOT / "host/hos_main.c").read_text(encoding="utf-8")
    assert main.index("hos_crash_record_prepare();") < main.index("MoveFileExA(from, to, MOVEFILE_REPLACE_EXISTING);")
    assert "hos_crash_clean_end();" in block(main, "static DWORD WINAPI game_thread(")

    # An exhaustive inventory: a new process exit must be classified, not silently
    # start generating unclean records. Declarations and comments are not calls.
    expected = {
        "control.c": ["ExitProcess(0);"],
        "display.c": ["ExitProcess(1);"],
        "hos_main.c": ["ExitProcess(result);"],
        "hos_relaunch.c": ["ExitProcess(0);"],
        "hos_svc.c": ["ExitProcess(0);"],
        "hos_thread.c": ["ExitProcess(3);"],  # fatal: record, not a clean end
        "hos_update.c": ["ExitProcess(0);", "_exit(127);", "_exit(127);", "_exit(0);"],
        "posix/display_sdl.c": ["ExitProcess(1);"],
        "posix/win32_posix.c": ["_exit((int)code);", "ExitProcess(code);"],  # platform implementations
    }
    observed = {}
    for path in (ROOT / "host").rglob("*.c"):
        text = path.read_text(encoding="utf-8")
        text = re.sub(r"/\*.*?\*/|//[^\n]*", "", text, flags=re.S)
        calls = []
        for match in re.finditer(r"\b(?:ExitProcess|_exit|_Exit)\s*\([^;\n]*\);", text):
            prefix = text[text.rfind("\n", 0, match.start()) + 1:match.start()]
            if re.search(r"\bvoid\s*$", prefix):
                continue
            calls.append(match.group())
            rel = path.relative_to(ROOT / "host").as_posix()
            exempt = (rel == "hos_thread.c" or rel == "posix/win32_posix.c" or
                      (rel == "hos_update.c" and match.group() == "_exit(127);"))
            if not exempt:
                assert "hos_crash_clean_end();" in text[max(0, match.start() - 180):match.start()], rel
        if calls:
            observed[path.relative_to(ROOT / "host").as_posix()] = calls
    assert observed == expected, (observed, expected)
    updater = (ROOT / "host/hos_update.c").read_text(encoding="utf-8")
    assert "hos_crash_clean_end" not in block(updater, "        if (fork() == 0) {")
    assert "hos_crash_clean_end" not in block(updater, "    if (pid == 0) {")
    assert "hos_crash_clean_end();" in block(updater, "    if (pid > 0) {")
    print("PASS: every host process exit classified; control/restart/update parents mark, children do not")
    return glue, threads, main


def build(cc, temp, glue, threads, main):
    template = (ROOT / "tests/crash_record_test.c").read_text(encoding="utf-8")
    replacements = {
        "RECORD_EXCEPTION": block(glue, "static void record_exception("),
        "FATAL_ADAPTER": block(glue, "void hos_crash_record_fatal("),
        "REPORT_FATAL": block(glue, "static LONG CALLBACK report_fatal("),
        "HOS_LOG": block(threads, "void hos_log("),
        "HOS_FATAL": block(threads, "void hos_fatal("),
        "CONTROL_QUIT": block((ROOT / "host/control.c").read_text(encoding="utf-8"), "static void game_quit("),
        "WINDOW_EXIT": block((ROOT / "host/hos_relaunch.c").read_text(encoding="utf-8"), "void hos_window_closed_exit("),
        "GAME_THREAD": block(main, "static DWORD WINAPI game_thread("),
    }
    shim = (ROOT / "host/posix/win32_posix.c").read_text(encoding="utf-8")
    replacements["POSIX_ADAPTER"] = (shim[shim.index("/* ---- exceptions ---- */"):
                                             shim.index("/* ---- the window's state,")] +
                                     block(shim, "void ExitProcess(UINT code) {")) if os.name != "nt" else ""
    for name, text in replacements.items():
        template = template.replace("/* " + name + " */", text)
    src, exe = temp / "fixture.c", temp / "fixture.exe"
    src.write_text(template, encoding="utf-8")
    cmd = [cc, "-std=gnu11", "-O1", "-fno-omit-frame-pointer", "-Wall", "-Wextra", "-Werror",
           "-I", str(ROOT / "host"), str(src), str(ROOT / "host/hos_report_run.c"), "-o", str(exe)]
    if os.name == "nt":
        cmd += ["-ldbghelp", "-ladvapi32"]
    else:
        cmd += ["-I", str(ROOT / "host/posix"), "-pthread"]
    subprocess.run(cmd, check=True, timeout=120)
    return exe


def records(folder, kind="crash"):
    return sorted((folder / "bug-reports/pending").glob(kind + "-*.txt"))


def checked_record(path, kind="crash"):
    text = path.read_text(encoding="utf-8")
    assert text.startswith("Hookshot " + kind + " record 1\n"), text
    lines = text.splitlines()[1:]
    assert all(re.fullmatch(r"[a-z_]+: [^\r\n]*", line) for line in lines), text
    values = dict(line.split(": ", 1) for line in lines)
    for key in ("version", "when", "uptime_s", "reason", "thread", "module_offset", "last_present_age_ms", "log", "pid"):
        assert key in values, key
    assert values["version"] == "fixture-v1-g1234"
    assert values["record_complete"] == "yes"
    assert re.fullmatch(r"\d{8}T\d{6}Z", values["when"])
    assert re.fullmatch(r"[0-9a-f]{32}", values["run_id"])
    assert re.fullmatch(r"(?:crash|unclean)-\d{8}T\d{6}Z(?:-[\d-]+)?\.txt", path.name)
    for secret in ("private-user", "private-host", "/home/", "C:\\Users\\", os.environ.get("HOME", "")):
        if secret:
            assert secret not in text, (secret, text)
    return text, values


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cc", default=shutil.which("gcc") or "clang")
    args = parser.parse_args()
    glue, threads, host_main = source_contract()
    with tempfile.TemporaryDirectory(prefix="hookshot-crash-record-") as tmp:
        temp = Path(tmp)
        exe = build(args.cc, temp, glue, threads, host_main)
        env = os.environ.copy()
        env.update(USER="private-user", USERNAME="private-user", HOS_CRASH_MINIDUMP="0")
        count = 0

        def run(folder, mode, expected=0, extra=None):
            nonlocal count
            result = subprocess.run([str(exe), str(folder), mode], env=env | (extra or {}),
                                    stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, timeout=20)
            if expected is None:
                assert result.returncode != 0, (mode, result)
            else:
                assert result.returncode == expected, (mode, result.returncode, result.stderr)
            count += 1
            return result

        for mode in ("clean", "return", "control", "restart", "game-return", "denied"):
            folder = temp / mode; folder.mkdir()
            run(folder, mode)
            assert not records(folder) and not records(folder, "unclean")
            assert not list((folder / "bug-reports/running").glob("*.running"))
        print("PASS: clean return, explicit clean, control quit, restart, game return, denied storage")

        for mode in ("fake", "partial", "privacy", "bounds", "fatal", "device-lost", "crash", "signal", "abort", "heap"):
            folder = temp / mode; folder.mkdir()
            result = run(folder, mode, 3 if mode in ("fatal", "device-lost") else None if mode in ("crash", "signal", "abort", "heap") else 0)
            paths = records(folder); assert len(paths) == 1, (mode, paths)
            text, values = checked_record(paths[0])
            assert "[hos host] run_id: " + values["run_id"] + "\n" in result.stderr
            assert values["dump"] == "off"
            if mode == "bounds":
                assert values["thread"] == "unknown" and values["module_offset"] == "unknown"
                assert "guest_register:" not in text
            else:
                assert "+0x" in text and "host_backtrace:" in text
                if mode != "device-lost":
                    assert "guest_register: x0=0x1000" in text and "guest_backtrace: 0x80005678" in text, (mode, text)
            if mode == "privacy":
                assert values["thread"] == "redacted"
            if mode == "device-lost":
                assert values["reason"] == "device lost"
            if mode == "signal" and os.name != "nt":
                assert values["signal"] == str(signal.SIGSEGV)
            run(folder, "clean")
            assert not records(folder, "unclean") and records(folder) == paths
        print("PASS: allocation-poisoned writer, bounds, short writes/EINTR, privacy, fatal/device-lost and real fatal children")

        folder = temp / "disk-full"; folder.mkdir()
        run(folder, "disk-full")
        assert not records(folder)
        run(folder, "clean")
        assert len(records(folder, "unclean")) == 1
        for mode in ("staged", "published-marker"):
            folder = temp / mode; folder.mkdir()
            run(folder, mode, 3)
            run(folder, "clean")
            assert len(records(folder)) == 1 and not records(folder, "unclean")
        print("PASS: failed writes retain a marker; durable staged/published crashes never become unclean ends")

        folder = temp / "collision"; folder.mkdir()
        pending = folder / "bug-reports/pending"; pending.mkdir(parents=True)
        now = datetime.now(timezone.utc)
        reserved = []
        for delta in range(-2, 31):
            path = pending / ("crash-" + (now + timedelta(seconds=delta)).strftime("%Y%m%dT%H%M%SZ") + ".txt")
            path.write_text("preserved\n", encoding="utf-8"); reserved.append(path)
        run(folder, "fake")
        added = set(records(folder)) - set(reserved)
        assert len(added) == 1 and re.search(r"Z-\d+-\d+\.txt$", next(iter(added)).name)
        assert all(p.read_text(encoding="utf-8") == "preserved\n" for p in reserved)
        print("PASS: same-second publication cannot overwrite another record")

        folder = temp / "two-live"; folder.mkdir()
        children = []
        try:
            for _ in range(2):
                child = subprocess.Popen([str(exe), str(folder), "hold"], env=env, stdin=subprocess.PIPE,
                                         stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
                children.append(child)
                assert child.stdout.readline().strip() == "ready"
            run(folder, "clean")
            assert not records(folder, "unclean")
            children[0].kill(); children[0].wait(timeout=10)
            run(folder, "clean")
            paths = records(folder, "unclean"); assert len(paths) == 1
            _, values = checked_record(paths[0], "unclean")
            assert values["reason"] == "ended without a clean exit" and values["crash_handler"] == "not observed"
            assert values["pid"] == str(children[0].pid)
            children[1].communicate("\n", timeout=10)
            run(folder, "clean")
            assert records(folder, "unclean") == paths
        finally:
            for child in children:
                if child.poll() is None:
                    child.kill(); child.wait(timeout=10)
        folder = temp / "stale-live"; folder.mkdir()
        child = subprocess.Popen([str(exe), str(folder), "stale-live"], env=env, stdin=subprocess.PIPE,
                                 stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
        try:
            assert child.stdout.readline().strip() == "ready"
            run(folder, "clean")
            assert len(records(folder, "unclean")) == 1  # PID alive is not proof this old marker is owned
            child.communicate("\n", timeout=10)
        finally:
            if child.poll() is None:
                child.kill(); child.wait(timeout=10)
        print("PASS: two live owners, real forced kill recovery, and stale marker with a live reused PID")

        folder = temp / "old-log"; folder.mkdir()
        old_log = folder / "previous.log"
        with old_log.open("w+", encoding="utf-8") as log:
            child = subprocess.Popen([str(exe), str(folder), "hold"], env=env, stdin=subprocess.PIPE,
                                     stdout=subprocess.PIPE, stderr=log, text=True)
            try:
                assert child.stdout.readline().strip() == "ready"
                log.seek(0)
                old_id = re.search(r"run_id: ([0-9a-f]{32})", log.read()).group(1)
                log.seek(0, 2)
                log.write("[hos host] FATAL: private-user /home/private-user token\n")
                log.flush()
                child.kill(); child.wait(timeout=10)
            finally:
                if child.poll() is None:
                    child.kill(); child.wait(timeout=10)
        fresh = run(folder, "clean")
        text, values = checked_record(records(folder, "unclean")[0], "unclean")
        assert values["run_id"] == old_id and old_id not in fresh.stderr
        assert values["log_tail_fatal"] == "yes" and values["log_tail_crash"] == "no"
        assert int(values["log_size_bytes"]) == old_log.stat().st_size
        assert values["log"] == "previous.log" and "token" not in text
        print("PASS: unclean record keeps previous run id and bounded log facts, never private log text")

        if os.name == "nt":
            folder = temp / "native-dump"; folder.mkdir()
            run(folder, "crash", None, {"HOS_CRASH_MINIDUMP": "1"})
            text, values = checked_record(records(folder)[0])
            assert values["dump"] == "requested locally, not for sharing"
            dumps = list((folder / "bug-reports/local-only").glob("*.dmp"))
            assert len(dumps) == 1 and dumps[0].read_bytes()[:4] == b"MDMP"
            assert not list((folder / "bug-reports/pending").glob("*.dmp"))
            outcomes = list((folder / "bug-reports/local-only").glob("*.outcome.txt"))
            assert len(outcomes) == 1 and "written locally;" in outcomes[0].read_text(encoding="utf-8")
            print("PASS: native Windows tiny-child minidump, %d bytes, local-only" % dumps[0].stat().st_size)
        else:
            folder = temp / "read-only"; folder.mkdir(); folder.chmod(0o500)
            try:
                if os.geteuid() != 0:
                    result = subprocess.run([str(exe), str(folder), "read-only"], env=env, capture_output=True, text=True, timeout=20)
                    assert result.returncode == 0
                    assert not records(folder, "unclean")
            finally:
                folder.chmod(0o700)
            print("Windows DbgHelp execution: not run on Linux (native Windows case is part of this driver)")
        print("PASS: %d completed child runs plus locked-owner/kill cases" % count)


if __name__ == "__main__":
    main()
