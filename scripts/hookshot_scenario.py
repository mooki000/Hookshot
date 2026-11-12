"""Run a versioned scenario through the command-line session layer, without an MCP package.

    python -I scripts/hookshot_scenario.py scenario.json --game GAME_DIR [--out DIR]
           [--wrapper JSON] [--save DIR --slot NN] [--json]

A scenario has version 1, a name, a launch object and a non-empty list of one-key steps.
Press/stick holds and sequence at_ms/hold_ms are milliseconds, converted at 60 game presents
per second, nearest frame with halves up and a positive hold of at least one frame.
The first failure stops the steps; transcripts, the run log and screenshots remain in the
output folder. Owned games are stopped in finally; an attached game is never stopped.
Building is a separate operation, not a scenario step or flag. See docs/control.md.

Adapted from Nicholas Gower's scenario runner in NotRexed/Hookshot#1025; its unchanged
files and author are retained in the preceding import commit.
"""
from __future__ import annotations

import argparse
import json
import math
import re
import sys
import tempfile
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(Path(__file__).resolve().parent))
import hookshot_ctl as control

STEP_KEYS = ("press", "stick", "sequence", "wait", "screenshot", "settings", "set_setting", "teleport", "age",
             "cheat", "expect_log")
DEFAULT_TIMEOUT_MS = 5000
LAUNCH_KEYS = {"exe", "image", "romfs", "cwd", "user", "data", "save", "slot", "profile", "headless", "env",
               "ready", "timeout", "attach", "stop_timeout"}
INPUT_KEYS = {"hold_ms", "at_ms", "settle_ms", "wait", "exclusive", "timeout_ms"}
STEP_FIELDS = {
    "press": INPUT_KEYS | {"buttons"},
    "stick": INPUT_KEYS | {"which", "x", "y", "buttons"},
    "sequence": {"steps", "settle_ms", "wait", "exclusive", "timeout_ms"},
    "wait": {"ms", "frames", "state", "map", "timeout_ms"},
    "screenshot": {"path", "after", "timeout_ms"},
    "settings": {"name"},
    "set_setting": {"name", "value"},
    "teleport": {"to", "arrive_ms", "timeout_ms"},
    "age": {"to", "arrive_ms", "timeout_ms"},
    "cheat": {"name", "value", "to", "arrive_ms", "timeout_ms"},
    "expect_log": {"pattern", "since", "regex", "timeout_ms"},
}


def usage(number, message):
    raise control.Failed("step %d: %s" % (number, message), 2)


def number(value, label, step, low=0, high=3600000, integer=False):
    if isinstance(value, bool) or not isinstance(value, (int, float)) or not math.isfinite(value) or not low <= value <= high:
        usage(step, "%s must be a finite number in %s..%s" % (label, low, high))
    if integer and int(value) != value:
        usage(step, "%s must be a whole number" % label)
    return value


def milliseconds_frames(value, *, hold=False):
    """Milliseconds -> native game frames: nearest, halves up, with a minimum one-frame positive hold"""
    frames = math.floor(value * 60 / 1000 + 0.5)
    return max(1, frames) if hold else frames


def input_step(value, step, stick=None):
    number(value.get("at_ms", 0), "at_ms", step)
    number(value.get("hold_ms", 200), "hold_ms", step, low=0.001)
    result = {"at": milliseconds_frames(value.get("at_ms", 0)),
              "frames": milliseconds_frames(value.get("hold_ms", 200), hold=True)}
    buttons = value.get("buttons")
    if buttons == [] and stick is not None:
        buttons = None
    if buttons is not None:
        if not (isinstance(buttons, str) and buttons or isinstance(buttons, list) and buttons and
                all(isinstance(b, str) and b for b in buttons)):
            usage(step, "buttons must be a non-empty string or list of names")
        result["buttons"] = buttons
    if stick is not None:
        if stick not in ("left", "right", "L", "R", "l", "r"):
            usage(step, "stick must be left or right (L or R)")
        result.update(stick="L" if stick in ("left", "L", "l") else "R",
                      x=number(value.get("x", 0), "x", step, -1, 1),
                      y=number(value.get("y", 0), "y", step, -1, 1))
    if buttons is None and stick is None:
        usage(step, "an input needs buttons or a stick")
    return result


def validate_scenario(data):
    if not isinstance(data, dict):
        usage(0, "the scenario must be a JSON object")
    if type(data.get("version")) is not int or data["version"] != 1:
        usage(0, "version must be 1")
    if set(data) - {"version", "name", "launch", "steps"}:
        usage(0, "unknown scenario fields: %s" % ", ".join(sorted(set(data) - {"version", "name", "launch", "steps"})))
    if "name" in data and (not isinstance(data["name"], str) or not data["name"].strip()):
        usage(0, "name must be a non-empty string")
    launch = data.get("launch", {})
    if not isinstance(launch, dict) or set(launch) - LAUNCH_KEYS:
        usage(0, "launch must be an object of documented session fields")
    for key in LAUNCH_KEYS - {"env", "headless", "timeout", "stop_timeout"}:
        if key in launch and not isinstance(launch[key], str):
            usage(0, "launch.%s must be a string" % key)
    if launch.get("ready", "bridge") not in ("bridge", "present", "play"):
        usage(0, "launch.ready must be bridge, present or play")
    if launch.get("profile", "test") not in ("default", "test"):
        usage(0, "launch.profile must be default or test")
    if "headless" in launch and type(launch["headless"]) is not bool:
        usage(0, "launch.headless must be a boolean")
    for key in ("timeout", "stop_timeout"):
        if key in launch:
            number(launch[key], "launch." + key, 0, 0.001, 3600)
    if "env" in launch and (not isinstance(launch["env"], dict) or
                            not all(isinstance(k, str) and isinstance(v, str) for k, v in launch["env"].items())):
        usage(0, "launch.env must be an object of string names and values")
    if "attach" in launch and set(launch) - {"attach", "ready", "timeout"}:
        usage(0, "an attached run takes only attach, ready and timeout; it is not launched or stopped")
    steps = data.get("steps")
    if not isinstance(steps, list) or not steps:
        usage(0, 'needs a non-empty "steps" list')
    for index, step in enumerate(steps, 1):
        if not isinstance(step, dict) or len(step) != 1 or next(iter(step)) not in STEP_KEYS:
            usage(index, "must hold exactly one of %s" % ", ".join(STEP_KEYS))
        name, value = next(iter(step.items()))
        if not isinstance(value, dict):
            usage(index, "%s takes an object" % name)
        if set(value) - STEP_FIELDS[name]:
            usage(index, "%s: unknown fields %s" % (name, ", ".join(sorted(set(value) - STEP_FIELDS[name]))))
        for key in ("wait", "exclusive", "regex"):
            if key in value and type(value[key]) is not bool:
                usage(index, "%s must be a boolean" % key)
        for key in ("timeout_ms", "arrive_ms"):
            if key in value:
                number(value[key], key, index, 0.001)
        if "settle_ms" in value:
            number(value["settle_ms"], "settle_ms", index)
        if name == "press":
            input_step(value, index)
        elif name == "stick":
            input_step(value, index, value.get("which", "left"))
        elif name == "sequence":
            items = value.get("steps")
            if not isinstance(items, list) or not 1 <= len(items) <= 256:
                usage(index, "sequence needs 1..256 steps")
            for item in items:
                if not isinstance(item, dict) or set(item) - {"at_ms", "hold_ms", "buttons", "stick", "x", "y"}:
                    usage(index, "each sequence item needs only at_ms, hold_ms, buttons, stick, x and y")
                input_step(item, index, item.get("stick"))
        elif name == "wait":
            kinds = set(value) & {"ms", "frames", "state", "map"}
            if len(kinds) != 1:
                usage(index, "wait needs exactly one of ms, frames, state or map")
            key = next(iter(kinds))
            if key in ("ms", "frames"):
                number(value[key], key, index, 0 if key == "ms" else 1, 3600000 if key == "ms" else 216000,
                       integer=key == "frames")
            elif not isinstance(value[key], str) or not value[key]:
                usage(index, "wait.%s must be a non-empty string" % key)
        elif name == "screenshot":
            if "path" in value:
                path = value["path"]
                if not isinstance(path, str) or not path or Path(path).is_absolute() or ".." in Path(path).parts:
                    usage(index, "screenshot.path must stay relative to the output shots folder")
            if "after" in value:
                number(value["after"], "after", index, integer=True)
        elif name in ("settings", "set_setting", "cheat"):
            if name != "settings" or "name" in value:
                if not isinstance(value.get("name"), str) or not value["name"]:
                    usage(index, "%s needs a name" % name)
            if name == "set_setting" and not isinstance(value.get("value"), (int, bool)):
                usage(index, "set_setting needs an integer or boolean value")
        if name == "teleport" or name == "cheat" and value["name"] == "teleport":
            target = value.get("to", value.get("value"))
            if target is None or isinstance(target, bool) or not isinstance(target, (str, int)):
                usage(index, "teleport needs to (a map name or index)")
        if name == "age" or name == "cheat" and value["name"] == "age":
            if "value" in value and (type(value["value"]) is not int or value["value"] not in (0, 1)):
                usage(index, "cheat age.value must be 0 (child) or 1 (adult)")
            target = value.get("to", {0: "child", 1: "adult"}.get(value.get("value")))
            if target is not None and target not in ("child", "adult"):
                usage(index, "age.to must be child or adult")
        if name == "expect_log":
            if not isinstance(value.get("pattern"), str) or not value["pattern"]:
                usage(index, "expect_log needs a non-empty pattern")
            if value.get("since", "previous") not in ("previous", "start"):
                usage(index, 'expect_log.since must be "previous" or "start"')
            if value.get("regex"):
                try:
                    re.compile(value["pattern"])
                except re.error as exc:
                    usage(index, "expect_log regex: %s" % exc)
    return data


def load_scenario(path: Path) -> dict:
    try:
        data = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, ValueError) as exc:
        usage(0, "cannot read scenario %s: %s" % (path, exc))
    return validate_scenario(data)


def scenario_dir(out, scenario):
    if out is not None:
        folder = Path(out).absolute()
        if folder.is_symlink() or folder.exists() and (not folder.is_dir() or any(folder.iterdir())):
            usage(0, "output folder must be new or empty: %s" % folder)
        folder.mkdir(parents=True, mode=0o700, exist_ok=True)
        return folder.resolve()
    name = scenario.get("name", "scenario")
    safe = "".join(char if char.isalnum() or char in "-_" else "-" for char in name)[:80]
    root = Path.cwd() / "work" / "control-scenarios"
    root.mkdir(parents=True, exist_ok=True)
    return Path(tempfile.mkdtemp(prefix=safe + "-", dir=root)).resolve()


def sleep_ms(session, ms):
    end = time.monotonic() + ms / 1000
    while True:
        session.require_live()
        left = end - time.monotonic()
        if left <= 0:
            return {"wall_ms": ms}
        time.sleep(min(left, 0.05))


def attached_ready(session, level, timeout):
    end = time.monotonic() + timeout
    while time.monotonic() < end:
        reply = session.request("status", reply_timeout=max(0.001, min(1, end - time.monotonic())))
        if control.ready_status(reply, level):
            return reply
        time.sleep(0.05)
    raise control.Failed("attached ready=%s timed out" % level, 3)


def step_line(step):
    name, value = next(iter(step.items()))
    return "%s %s" % (name, json.dumps(value, sort_keys=True))


def run_step(session, out, step, index, previous_cursor):
    name, value = next(iter(step.items()))
    timeout = value.get("timeout_ms")
    timeout = timeout / 1000 if timeout is not None else None
    if name in ("press", "stick", "sequence"):
        args = {"wait": value.get("wait", True), "exclusive": value.get("exclusive")}
        if name == "sequence":
            args["steps"] = [input_step(item, index, item.get("stick")) for item in value["steps"]]
        else:
            args.update(input_step(value, index, value.get("which", "left") if name == "stick" else None))
        result = session.request(name, timeout=timeout, **args)
        if value.get("settle_ms", 100):
            sleep_ms(session, value.get("settle_ms", 100))
        return result
    if name == "wait":
        if "ms" in value:
            return sleep_ms(session, value["ms"])
        kind = next(k for k in ("frames", "state", "map") if k in value)
        return session.request("wait", timeout=timeout or 60, event="present" if kind == "frames" else kind,
                               **{kind: value[kind]})
    if name == "screenshot":
        path = out / "shots" / value["path"] if value.get("path") else None
        if path:
            path.parent.mkdir(parents=True, exist_ok=True)
        return session.request("shot", timeout=timeout, path=str(path) if path else None, after=value.get("after"))
    if name == "settings":
        return session.request("option_get", name=value.get("name"))
    if name == "set_setting":
        return session.request("option_set", name=value["name"], value=value["value"])
    if name == "cheat":
        name = value["name"]
        if name not in ("teleport", "age"):
            raise control.Failed("cheat %r is not on the bridge; only teleport and age are available" % name, 1)
    if name in ("teleport", "age"):
        target = value.get("to", value.get("value") if name == "teleport" else {0: "child", 1: "adult"}.get(value.get("value")))
        result = session.request(name, timeout=timeout or 10, to=target)
        if value.get("arrive_ms") and result.get("sent"):
            args = {"event": "map", "map": result.get("map")} if name == "teleport" else {"event": "loading_end"}
            result = dict(result, arrived=session.request("wait", timeout=value["arrive_ms"] / 1000, **args))
        return result
    if name == "expect_log":
        return session.expect_log(value["pattern"], 0 if value.get("since") == "start" else previous_cursor,
                                  timeout or DEFAULT_TIMEOUT_MS / 1000, regex=value.get("regex", False))
    raise control.Failed("unknown step %r" % name, 2)


@control.session_call
def run_scenario(scenario, *, game_dir, out=None, wrapper=None, save=None, slot=None, on_event=None) -> dict:
    """Run and retain a plain transcript. Usage errors raise Failed(2); operational failures are in the returned result"""
    scenario = validate_scenario(scenario)
    if on_event is not None and not callable(on_event):
        usage(0, "on_event must be callable")
    launch = dict(scenario.get("launch", {}))
    attached = launch.pop("attach", None)
    if attached and any(v is not None for v in (wrapper, save, slot)):
        usage(0, "wrapper/save/slot cannot change an attached run")
    if not attached and not game_dir:
        usage(0, "a game folder is required")
    folder = scenario_dir(out, scenario)
    transcript, artifacts = [], []
    session, failed, code, failed_step = None, None, 0, None
    owned = attached is None
    stop_timeout = launch.pop("stop_timeout", 10)

    def event(kind, **fields):
        row = dict(event=kind, **fields)
        transcript.append(row)
        with (folder / "transcript.jsonl").open("a", encoding="utf-8", newline="\n") as f:
            f.write(json.dumps(row) + "\n")
        with (folder / "transcript.txt").open("a", encoding="utf-8", newline="\n") as f:
            f.write("%s %s\n" % (kind, json.dumps(fields, sort_keys=True)))
        if on_event:
            on_event(row)

    try:
        if attached:
            session = control.Session.attach(attached)
            ready = attached_ready(session, launch.get("ready", "bridge"), launch.get("timeout", 600))
            event("attach", ok=True, result=session.info(), ready=ready)
        else:
            launch.setdefault("profile", "test")
            launch.setdefault("ready", "bridge")
            if save is not None:
                launch["save"] = save
            if slot is not None:
                launch["slot"] = slot
            session = control.start_session(game_dir, run=str(folder / "run"), wrapper=wrapper, **launch)
            event("launch", ok=True, result=session.info())
        previous_cursor = session.log(lines=0)["cursor"]
        for index, step in enumerate(scenario["steps"], 1):
            cursor = session.log(lines=0)["cursor"]
            try:
                reply = run_step(session, folder, step, index, previous_cursor)
            except control.Failed as exc:
                failed, code, failed_step = str(exc), 3 if exc.code == 3 else 1, index
                event("step", index=index, step=step, ok=False, error=failed, exit_code=code)
                break
            event("step", index=index, step=step, ok=True, result=reply)
            if "screenshot" in step:
                artifacts.append({k: reply.get(k) for k in ("path", "frame", "present", "width", "height")})
            previous_cursor = cursor
    except (control.Failed, OSError) as exc:
        failed, code = str(exc), exc.code if isinstance(exc, control.Failed) else 2
        failed_step = 0 if session is None else len([r for r in transcript if r["event"] == "step"]) + 1
        event("error", index=failed_step, ok=False, error=failed, exit_code=code)
    except BaseException as exc:
        failed, code = str(exc) or type(exc).__name__, 1
        raise   # (still stop an owned game and retain a failed transcript if the caller is interrupted)
    finally:
        if session is not None and owned:
            try:
                stopped = session.stop(timeout=stop_timeout, terminate=True)
                event("stop", ok=stopped.get("process_exited", False), result=stopped)
            except control.Failed as exc:
                event("stop", ok=False, error=str(exc), exit_code=exc.code)
                if failed is None:
                    failed, code = str(exc), 3 if exc.code == 3 else 1
        result = {"version": 1, "name": scenario.get("name", "scenario"), "out": str(folder), "owned": owned,
                  "run": str(session.folder) if session else str(folder / "run") if owned else attached,
                  "ok": failed is None, "exit_code": code, "failed_step": failed_step, "error": failed,
                  "artifacts": artifacts, "transcript": transcript}
        control.write_json(folder / "transcript.json", result)
    return result


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("scenario", help="the versioned JSON file")
    parser.add_argument("--game", required=True, help="game folder (ignored only for an explicit launch.attach)")
    parser.add_argument("--out", help="a new or empty output folder")
    parser.add_argument("--wrapper", help="JSON argv prefix around the session lifetime owner")
    parser.add_argument("--save", help="save folder copied into the owned test profile")
    parser.add_argument("--slot", help="copy this slot only")
    parser.add_argument("--json", action="store_true", help="one JSON transcript result")
    args = parser.parse_args(argv)
    try:
        scenario = load_scenario(Path(args.scenario).expanduser())
        wrapper = json.loads(args.wrapper) if args.wrapper else None
        result = run_scenario(scenario, game_dir=args.game, out=args.out, wrapper=wrapper, save=args.save, slot=args.slot,
                              on_event=None if args.json else lambda row: print(json.dumps(row), flush=True))
    except (control.Failed, OSError, ValueError) as exc:
        print("hookshot_scenario: %s" % exc, file=sys.stderr)
        return exc.code if isinstance(exc, control.Failed) else 2
    print(json.dumps(result) if args.json else "%s: %s" % ("PASS" if result["ok"] else "FAILED", result["out"]))
    return result["exit_code"]


if __name__ == "__main__":
    raise SystemExit(main())
