"""Run a scripted scenario against a built game through the MCP server's own session code.

    python -I scripts/mcp_scenario.py scenario.json [--game-dir DIR] [--user DIR] [--out DIR] [--build]

The scenario is a JSON object:

    {
      "name": "title-and-menu",
      "launch": {"cheats": true, "wait_ready": true},
      "steps": [
        {"press": {"buttons": ["A"], "hold_ms": 200}},
        {"wait": {"ms": 2000}},
        {"expect_log": {"pattern": "hid: live input"}},
        {"screenshot": {"path": "title.png"}},
        {"settings": {}},
        {"set_setting": {"name": "text_speed", "value": 3}},
        {"cheat": {"name": "heart_protection", "value": 1}}
      ]
    }

`launch` takes the fields of `LaunchOptions` (docs/mcp.md). A step is one of `press`, `stick`, `sequence`, `wait`,
`screenshot`, `settings`, `set_setting`, `cheat` or `expect_log`. The run fails on the first press or sequence the game
does not acknowledge, on a screenshot that never arrives and on an `expect_log` that does not match.

Everything the run makes goes to the output folder (default `work/mcp/scenarios/<name>/`): `transcript.txt` and the
screenshots; `work/` is ignored by git. `--build` first rebuilds the game folder with `nx2/make_game.py --update` and
`HOOKSHOT_KEEP_BUILD_FILES=1` (the build files are kept for the next run); it needs the toolchain the builder uses in
the environment. One game at a time: launch refuses another `hookshot` unless the scenario's `force` is set.
"""
from __future__ import annotations

import argparse
import dataclasses
import json
import os
import subprocess
import sys
from datetime import datetime
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))

import hookshot_mcp as server  # noqa: E402  (the server's own session code: one implementation)

STEP_KEYS = ("press", "stick", "sequence", "wait", "screenshot", "settings", "set_setting", "cheat", "expect_log")
DEFAULT_TIMEOUT_MS = 5000


class ScenarioError(Exception):
    pass


def load_scenario(path: Path) -> dict:
    try:
        data = json.loads(path.read_text(encoding="utf-8"))
    except OSError as exc:
        raise ScenarioError(f"cannot read the scenario {path}: {exc}") from exc
    except ValueError as exc:
        raise ScenarioError(f"{path} is not JSON: {exc}") from exc
    if not isinstance(data, dict):
        raise ScenarioError(f"{path}: the scenario must be a JSON object")
    steps = data.get("steps")
    if not isinstance(steps, list) or not steps:
        raise ScenarioError(f"{path}: needs a non-empty \"steps\" list")
    for number, step in enumerate(steps, 1):
        if not isinstance(step, dict) or len(step) != 1 or next(iter(step)) not in STEP_KEYS:
            raise ScenarioError(f"{path}: step {number} must hold exactly one of {', '.join(STEP_KEYS)}")
    return data


def launch_options(args, scenario: dict) -> server.LaunchOptions:
    """The scenario's launch fields, with the command line's paths and flags on top."""
    given = scenario.get("launch") or {}
    if not isinstance(given, dict):
        raise ScenarioError("\"launch\" must be an object of LaunchOptions fields")
    known = {field.name for field in dataclasses.fields(server.LaunchOptions)}
    unknown = sorted(set(given) - known)
    if unknown:
        raise ScenarioError(f"unknown launch field(s): {', '.join(unknown)} (see docs/mcp.md)")
    options = dict(given)
    overrides = {"game_dir": args.game_dir, "user": args.user, "exe": args.exe, "image": args.image,
                 "romfs": args.romfs}
    options.update({key: value for key, value in overrides.items() if value})
    if args.force:
        options["force"] = True
    if args.cache_dir:
        options["cache_dir"] = args.cache_dir
    return server.LaunchOptions(**options)


def scenario_dir(args, scenario: dict) -> Path:
    if args.out:
        return Path(args.out).expanduser().resolve()
    name = str(scenario.get("name") or datetime.now().strftime("%Y%m%d-%H%M%S"))
    safe = "".join(char if char.isalnum() or char in "-_" else "-" for char in name)
    return (ROOT / "work" / "mcp" / "scenarios" / safe).resolve()


def build(game_dir: str | None, language: str | None) -> None:
    """`nx2/make_game.py --update`: rebuild the folder in place, keeping the build files for the next run."""
    if not game_dir:
        raise ScenarioError("--build needs --game-dir (or a launch.game_dir) naming the game folder to update")
    command = [sys.executable, "-I", str(ROOT / "nx2" / "make_game.py"), "--update", str(game_dir)]
    if language:
        command += ["--language", language]
    print("building: " + " ".join(command), flush=True)
    result = subprocess.run(command, cwd=str(ROOT), env=dict(os.environ, HOOKSHOT_KEEP_BUILD_FILES="1"))
    if result.returncode:
        raise ScenarioError(f"the build stopped ({result.returncode})")


def step_line(step: dict) -> str:
    name, value = next(iter(step.items()))
    return f"{name} {json.dumps(value, sort_keys=True)}"


def run_step(session: server.GameSession, out: Path, step: dict, timeout_ms: int) -> str:
    name, value = next(iter(step.items()))
    value = value or {}
    if not isinstance(value, dict):
        raise ScenarioError(f"{name} takes an object")

    if name == "press":
        result = session.press(value.get("buttons"), value.get("hold_ms", 200), value.get("settle_ms", 100),
                               value.get("wait", True))
        if not result["acked"]:
            raise ScenarioError(f"the game did not acknowledge the press {result['line']!r}")
        return f"press {result['line']}"

    if name == "stick":
        result = session.move_stick(value.get("which", "left"), value.get("x", 0.0), value.get("y", 0.0),
                                    value.get("hold_ms", 200), value.get("settle_ms", 100), value.get("wait", True))
        if not result["acked"]:
            raise ScenarioError(f"the game did not acknowledge the stick {result['line']!r}")
        return f"stick {result['line']}"

    if name == "sequence":
        steps = value.get("steps")
        if not isinstance(steps, list) or not steps:
            raise ScenarioError("sequence needs a \"steps\" list")
        result = session.sequence([server.Step(**item) for item in steps], value.get("settle_ms", 100),
                                  value.get("wait", True))
        if not result["acked"]:
            raise ScenarioError(f"the game did not acknowledge the sequence {result['line']!r}")
        return f"sequence {result['line']}"

    if name == "wait":
        session._sleep_ms(int(value.get("ms", 1000)))
        return f"wait {value.get('ms', 1000)} ms"

    if name == "screenshot":
        path = value.get("path")
        if path and not Path(path).is_absolute():
            path = str(out / "shots" / path)
        info = session.screenshot(path=path)
        return f"screenshot {info['path']} ({info['bytes']} bytes)"

    if name == "settings":
        rows = session.read_settings()
        return (f"settings {len(rows['settings'])} settings, cheats "
                f"{'on' if rows['cheats_enabled'] else 'off'}, {len(rows['cheats'])} cheat rows")

    if name == "set_setting":
        result = session.set_setting(value["name"], int(value["value"]))
        return f"set_setting {result['name']}={result['value']}"

    if name == "cheat":
        result = session.cheat(value["name"], int(value.get("value", 0)), value.get("item"))
        return f"cheat {result['name']}={result['value']}"

    if name == "expect_log":
        pattern = str(value.get("pattern", ""))
        if not pattern:
            raise ScenarioError("expect_log needs a \"pattern\"")
        timeout = int(value.get("timeout_ms", timeout_ms)) / 1000.0
        line = session.log_stream.wait_for(lambda text: pattern.lower() in text.lower(), timeout, after=0)
        if line is None:
            raise ScenarioError(f"no log line matched {pattern!r} within {value.get('timeout_ms', timeout_ms)} ms")
        return f"expect_log {pattern!r} -> {line.strip()}"

    raise ScenarioError(f"unknown step {name!r}")


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("scenario", help="the scenario JSON file")
    parser.add_argument("--game-dir", default=os.environ.get("HOOKSHOT_MCP_GAME_DIR"))
    parser.add_argument("--user")
    parser.add_argument("--exe")
    parser.add_argument("--image")
    parser.add_argument("--romfs")
    parser.add_argument("--cache-dir")
    parser.add_argument("--out", help="output folder (default work/mcp/scenarios/<name>)")
    parser.add_argument("--timeout", type=int, default=DEFAULT_TIMEOUT_MS, metavar="MS",
                        help="the default timeout of an expect_log step")
    parser.add_argument("--build", action="store_true", help="rebuild the game folder first (make_game.py --update)")
    parser.add_argument("--language", help="the language --build writes")
    parser.add_argument("--force", action="store_true", help="start even if another hookshot runs")
    args = parser.parse_args(argv)

    try:
        scenario = load_scenario(Path(args.scenario).expanduser())
        options = launch_options(args, scenario)
        out = scenario_dir(args, scenario)
        if args.build:
            build(options.game_dir, args.language)
    except ScenarioError as exc:
        print(f"FAIL: {exc}", file=sys.stderr)
        return 2

    out.mkdir(parents=True, exist_ok=True)
    config = server.Config(work_dir=out / "work")
    session = server.GameSession(config, options)
    transcript: list[str] = []
    failed = None
    try:
        launched = session.launch()
        transcript.append(f"launch pid {launched['pid']} run {launched['run_dir']}")
        print(transcript[-1], flush=True)
        for number, step in enumerate(scenario["steps"], 1):
            try:
                line = run_step(session, out, step, args.timeout)
            except (ScenarioError, server.ToolError) as exc:
                failed = str(exc)
                transcript.append(f"step {number} {step_line(step)}: FAILED: {failed}")
                print(transcript[-1], flush=True)
                break
            transcript.append(f"step {number} {line}")
            print(transcript[-1], flush=True)
    except server.ToolError as exc:
        failed = str(exc)
        transcript.append(f"launch FAILED: {failed}")
        print(transcript[-1], flush=True)
    finally:
        if session.proc is not None:
            stopped = session.stop()
            transcript.append(f"stop {stopped}")
            print(transcript[-1], flush=True)
        (out / "transcript.txt").write_text("\n".join(transcript) + "\n", encoding="utf-8")

    print(f"{'FAILED' if failed else 'PASS'}: {len(scenario['steps'])} steps, {out}", flush=True)
    if failed:
        print(f"FAIL: {failed}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
