"""`scripts/mcp_scenario.py` end to end against the fake game.

    python -I -B tests/test_mcp_scenario.py

Runs a scenario (press, a log expectation, a screenshot, settings, a setting, a cheat) through the runner with
`tests/fake_game.py` in place of the game, then runs one with an expectation that cannot match and checks that it fails.
No build, GPU or game files.
"""
import json
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]

try:
    import mcp  # noqa: F401
except ModuleNotFoundError as exc:   # (the runner reports the missing package unless --strict; direct runs say so)
    raise SystemExit(f"SKIP: {exc}")
RUN_ALL_MODULES = ["mcp"]

RUNNER = ROOT / "scripts" / "mcp_scenario.py"
FAKE = ROOT / "tests" / "fake_game.py"


def run(folder: Path, scenario: dict, out: Path) -> subprocess.CompletedProcess:
    path = folder / "scenario.json"
    path.write_text(json.dumps(scenario), encoding="utf-8")
    return subprocess.run(
        [sys.executable, "-I", str(RUNNER), str(path),
         "--exe", sys.executable, "--image", str(FAKE),
         "--romfs", str(folder / "romfs"), "--user", str(folder / "user"), "--out", str(out)],
        capture_output=True, text=True, encoding="utf-8", errors="replace", timeout=180)


def scenario(steps: list[dict], name: str) -> dict:
    return {"name": name, "launch": {"force": True}, "steps": steps}


def main():
    with tempfile.TemporaryDirectory(prefix="hookshot-scenario-") as tmp:
        folder = Path(tmp)
        (folder / "romfs").mkdir()
        (folder / "user").mkdir()

        out = folder / "out"
        result = run(folder, scenario([
            {"press": {"buttons": ["A"], "hold_ms": 10, "settle_ms": 0}},
            {"expect_log": {"pattern": "hid: live input", "timeout_ms": 5000}},
            {"wait": {"ms": 50}},
            {"screenshot": {"path": "shot.png"}},
            {"settings": {}},
            {"set_setting": {"name": "text_speed", "value": 3}},
            {"cheat": {"name": "heart_protection", "value": 1}},
            {"expect_log": {"pattern": "mcp: cheat ok", "timeout_ms": 5000}},
        ], "pass"), out)
        if result.returncode:
            raise SystemExit(f"the scenario failed ({result.returncode}):\n{result.stdout}\n{result.stderr}")
        shots = sorted((out / "shots").glob("*.png"))
        if not shots or shots[0].stat().st_size <= 0:
            raise SystemExit(f"no screenshot was kept: {shots}")
        transcript = (out / "transcript.txt").read_text(encoding="utf-8")
        for needle in ("press 0:A/10", "set_setting text_speed=3", "cheat heart_protection=1", "stop "):
            if needle not in transcript:
                raise SystemExit(f"the transcript lacks {needle!r}:\n{transcript}")

        bad_out = folder / "bad"
        failed = run(folder, scenario([{"expect_log": {"pattern": "no such line", "timeout_ms": 400}}], "fail"), bad_out)
        if failed.returncode == 0:
            raise SystemExit(f"a scenario with an impossible expectation passed:\n{failed.stdout}")
        if "no log line matched" not in (failed.stdout + failed.stderr):
            raise SystemExit(f"the failure was not explained:\n{failed.stdout}\n{failed.stderr}")

    print("PASS: the scenario runner runs a passing scenario and fails an impossible expectation")


if __name__ == "__main__":
    main()
