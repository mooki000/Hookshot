"""host/coop.c, co-op's session layer, and host/coop_net.c, its sockets, threads and random bytes, without the game
(tests/coop_session_test.c, compiled with host/coop_proto.c and Monocypher; on Linux with AddressSanitizer and UBSan
when the compiler has them). Six runs at once, each its own process:

  unit        the operating-system layer on loopback, coop_server_parse, a stop's order, and the process's session off
              (no thread, no socket) unless HOS_COOP or the options file's coop=1 switches it on
  unit, menu  co-op off at start, switched on and off by coop_configure (the menu's Co-op tab) while the game runs
  mock 0/30   two sessions through the test's own relay, which also acts as a hostile one (changed, forged and replayed
              datagrams, garbage, forged rosters, lies), with 0 % and 30 % of the received datagrams thrown away
  relay 0/30  the same two through scripts/coop_relay.py on 127.0.0.1, restarted midway, plus the process's session
              started from the settings by coop_init and, on Linux, a child process ended by SIGTERM that says BYE first

Also checks the wiring: the game starts co-op beside the Discord status on both platforms (host/hos_main.c), says
goodbye when its window closes (host/hos_relaunch.c), and links Winsock and BCrypt on Windows (nx2/build.py).

    python -I -B tests/test_coop_session.py [--cc gcc] [--only unit|mock|relay]   (COOP_TEST_VERBOSE=1: every log line)
"""
import argparse
import concurrent.futures
import os
from pathlib import Path
import shutil
import socket
import subprocess
import sys
import tempfile
import threading
import time

RUN_ALL_ARGS = ["--cc", "gcc"]

ROOT = Path(__file__).resolve().parents[1]
MONOCYPHER = ROOT / "third_party" / "monocypher"
RELAY = ROOT / "scripts" / "coop_relay.py"


def compile_fixture(cc, folder):
    """The fixture's path; with sanitizers on Linux when this compiler links them."""
    run = lambda cmd: subprocess.run(cmd, capture_output=True, text=True, timeout=300)
    exe = folder / ("coop_session_test.exe" if os.name == "nt" else "coop_session_test")
    if os.name == "nt":   # (mingw's printf takes %zu with its own stdio; Winsock and BCryptGenRandom)
        tries, libs = [["-D__USE_MINGW_ANSI_STDIO=1"]], ["-lws2_32", "-lbcrypt"]
    else:
        tries, libs = [["-fsanitize=address,undefined", "-fno-sanitize-recover=undefined"], []], ["-pthread"]
    for i, extra in enumerate(tries):
        mono = folder / "monocypher.o"
        r = run([cc, "-std=c99", "-O2", "-w", "-c", str(MONOCYPHER / "monocypher.c"), "-o", str(mono)] + extra)
        if r.returncode == 0:
            r = run([cc, "-std=c11", "-D_GNU_SOURCE", "-O1", "-g", "-Wall", "-Wextra", "-Werror", "-I" + str(ROOT / "host"),
                     str(ROOT / "tests" / "coop_session_test.c"), str(ROOT / "host" / "coop.c"), str(ROOT / "host" / "coop_net.c"),
                     str(ROOT / "host" / "coop_proto.c"), str(mono), "-o", str(exe)] + extra + libs)
        if r.returncode == 0:
            print("compiled%s" % (" with " + " ".join(extra) if extra else ""))
            return exe
        if i + 1 == len(tries):
            print(r.stdout + r.stderr)
            raise SystemExit("FAIL: the fixture does not compile")
        print("(not with %s: %s)" % (" ".join(extra), ((r.stdout + r.stderr).strip().splitlines() or [""])[-1]))


def block(text, start):
    """From start to the end of its braces."""
    i = text.index(start)
    depth, j = 0, text.index("{", i)
    while True:
        depth += {"{": 1, "}": -1}.get(text[j], 0)
        j += 1
        if depth == 0:
            return text[i:j]


def check_wiring():
    main_c = (ROOT / "host" / "hos_main.c").read_text(encoding="utf-8")
    starts = [i for i in range(len(main_c)) if main_c.startswith("hos_discord_start();", i)]
    if len(starts) != 2:
        raise SystemExit("FAIL: host/hos_main.c starts the Discord status %d times (two expected: Windows, Linux)" % len(starts))
    for at in starts:
        after = main_c[at:at + 200]
        if "coop_init();" not in after or after.index("coop_init();") > after.index("}"):
            raise SystemExit("FAIL: host/hos_main.c does not start co-op (coop_init) beside hos_discord_start at offset %d" % at)
    goodbye = block((ROOT / "host" / "hos_relaunch.c").read_text(encoding="utf-8"), "void hos_window_closed_exit(void) {")
    if "coop_shutdown();" not in goodbye or goodbye.index("coop_shutdown();") > goodbye.index("hos_log_flush();"):
        raise SystemExit("FAIL: host/hos_relaunch.c hos_window_closed_exit does not say goodbye (coop_shutdown) before the log's flush")
    build = (ROOT / "nx2" / "build.py").read_text(encoding="utf-8").splitlines()
    at = next(i for i, line in enumerate(build) if line.startswith("HOST_LIBS = "))
    end = next(i for i in range(at + 1, len(build)) if not build[i].startswith(" "))   # (the statement's continued lines)
    windows = " ".join(build[at:end]).split(" else ", 1)[-1]
    if '"-lws2_32"' not in windows or '"-lbcrypt"' not in windows:
        raise SystemExit("FAIL: nx2/build.py's Windows HOST_LIBS lack -lws2_32 and -lbcrypt (host/coop_net.c: Winsock, BCryptGenRandom)")
    applet = (ROOT / "host" / "nn_applet.c").read_text(encoding="utf-8")
    if "int hos_ini_text(const char *key, char *out, size_t size)" not in applet:
        raise SystemExit("FAIL: host/nn_applet.c has no hos_ini_text (host/coop.c reads coop_name= and coop_server= with it)")


def free_udp_port():
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    s.bind(("127.0.0.1", 0))
    port = s.getsockname()[1]
    s.close()
    return port


class RelayProc:
    """scripts/coop_relay.py as a process on a fixed port, so that it can be stopped and started again. Its log is kept
    across restarts, for a failure's message."""

    def __init__(self, port):
        self.port, self.p, self.log, self.reader = port, None, [], None

    def _read(self, out):
        try:
            for line in out:
                self.log.append(line.rstrip())
        except (OSError, ValueError):
            pass   # closed by stop()

    def start(self):
        self.p = subprocess.Popen([sys.executable, "-I", str(RELAY), "--host", "127.0.0.1", "--port", str(self.port), "--log-joins",
                                   "--log-addrs"], stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
        line = self.p.stdout.readline()
        self.log.append(line.rstrip())
        if "listening" not in line:
            raise RuntimeError("the relay did not start: " + line)
        self.reader = threading.Thread(target=self._read, args=(self.p.stdout,), daemon=True)
        self.reader.start()

    def stop(self):
        if self.p:
            self.p.kill()
            self.p.wait()
            self.reader.join(5)
            self.p.stdout.close()
            self.p = None
            self.log.append("(the relay stopped)")


def run_mode(exe, name, args, relay_drop=None, work=None):
    """One run of the fixture: (name, passed, its output)."""
    relay = None
    cmd = [str(exe)] + (args or [])
    if relay_drop is not None:
        relay = RelayProc(free_udp_port())
        relay.start()
        cmd += ["--relay", "127.0.0.1:%d" % relay.port, "--drop", str(relay_drop), "--work", str(work)]
    started = time.monotonic()
    p = subprocess.Popen(cmd, stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, cwd=str(ROOT))
    lines, restarted = [], False
    timer = threading.Timer(480, p.kill)
    timer.start()
    try:
        for line in p.stdout:
            lines.append(line.rstrip())
            if "coop-session-test: restart the relay now" in line and relay:
                relay.stop()
                time.sleep(0.3)
                relay.start()
                restarted = True
                p.stdin.write("restarted\n")
                p.stdin.flush()
        code = p.wait()
    finally:
        timer.cancel()
        p.stdin.close()
        p.stdout.close()
        if relay:
            relay.stop()
    text = "\n".join(lines)
    ok = code == 0 and "coop-session-test: passed" in text and (relay is None or restarted)
    head = "--- %s: %s in %.1f s (exit %d)" % (name, "passed" if ok else "FAILED", time.monotonic() - started, code)
    if relay and (not ok or os.environ.get("COOP_TEST_VERBOSE")):
        text += "\n--- the relay's log ---\n" + "\n".join(relay.log[-300:])
    return name, ok, head + "\n" + text


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--cc", default=os.environ.get("CC") or shutil.which("gcc") or shutil.which("clang") or "gcc")
    p.add_argument("--only", choices=("unit", "mock", "relay"))
    a = p.parse_args()
    check_wiring()
    with tempfile.TemporaryDirectory(prefix="hookshot-coop-session-") as temp:
        temp = Path(temp)
        exe = compile_fixture(a.cc, temp)
        runs = [("unit", ["--unit"], None), ("unit, menu", ["--unit-menu"], None), ("mock, 0 % lost", ["--mock", "--drop", "0"], None),
                ("mock, 30 % lost", ["--mock", "--drop", "30"], None), ("relay, 0 % lost", None, 0), ("relay, 30 % lost", None, 30)]
        runs = [r for r in runs if not a.only or r[0].startswith(a.only)]
        works = []
        for i in range(len(runs)):
            works.append(temp / ("work%d" % i))
            works[-1].mkdir()
        with concurrent.futures.ThreadPoolExecutor(len(runs)) as pool:
            results = list(pool.map(lambda ir: run_mode(exe, ir[1][0], ir[1][1], ir[1][2], works[ir[0]]), enumerate(runs)))
    failed = [name for name, ok, _ in results if not ok]
    for _, _, text in results:
        print(text)
    if failed:
        print("FAIL: " + ", ".join(failed))
        return 1
    print("PASS: the co-op session layer, %d runs" % len(results))
    return 0


if __name__ == "__main__":
    sys.exit(main())
