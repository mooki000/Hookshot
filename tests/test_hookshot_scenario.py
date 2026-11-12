"""The scenario runner against the client's fake bridge and the real C bridge's frame recorder.

    python -I -B tests/test_hookshot_scenario.py [--cc gcc]

Passing/failing scenarios keep transcripts and authored PNGs. Readiness, step-local log cursors,
usage errors and ownership are game-free. The C fixture records what its controller reads:
sequence milliseconds must become the actual scheduled frames, not just a plausible script string.
Adapted from Nicholas Gower's tests in #1025; no SDK, GPU, dump or real save is needed.
"""
import argparse
import csv
import importlib.util
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

RUN_ALL_ARGS = ["--cc", "gcc"]
ROOT = Path(__file__).resolve().parents[1]
RUNNER = Path(os.environ.get("HOOKSHOT_SCENARIO_SOURCE", ROOT / "scripts" / "hookshot_scenario.py"))
COMPILER = "gcc"


def load_module(path, name):
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def run(folder: Path, scenario: dict, out: Path, *extra):
    path = folder / "scenario.json"
    path.write_text(json.dumps(scenario), encoding="utf-8")
    return subprocess.run([sys.executable, "-I", "-B", str(RUNNER), str(path), "--game", str(folder), "--out", str(out),
                           "--json", *extra], capture_output=True, text=True, encoding="utf-8", errors="replace", timeout=60)


class Scenarios(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory(prefix="scenario-")
        self.addCleanup(self.tmp.cleanup)
        self.folder = Path(self.tmp.name)
        self.m = load_module(RUNNER, "hookshot_scenario")
        fake = load_module(ROOT / "tests" / "test_hookshot_ctl.py", "scenario_fake")
        self.fake = self.folder / "fake.py"
        self.fake.write_text(fake.FAKE, encoding="utf-8")
        self.requests = self.folder / "requests.jsonl"

    def scenario(self, steps, **launch):
        defaults = {"exe": sys.executable, "image": str(self.fake), "ready": "play",
                    "env": {"FAKE_LOG": str(self.requests), "FAKE_SESSION": "1"}}
        defaults.update(launch)
        return {"version": 1, "name": "authored scenario", "launch": defaults, "steps": steps}

    def execute(self, data, name="out", **kwargs):
        return self.m.run_scenario(data, game_dir=str(self.folder), out=str(self.folder / name), **kwargs)

    def assert_stopped(self, result):
        info = self.m.control.Session(result["run"]).info()
        self.assertTrue(info["process_exited"])
        self.assertFalse(self.m.control.same_process(info["pid"], info["process_start"]))
        self.assertFalse(self.m.control.owners_alive(info))
        return info

    def test_passing_scenario_and_retained_png(self):
        data = self.scenario([
            {"press": {"buttons": ["A"], "hold_ms": 25, "settle_ms": 0}},
            {"expect_log": {"pattern": "fake: input press completed", "timeout_ms": 1000}},
            {"stick": {"which": "right", "x": 0.5, "y": -0.25, "hold_ms": 50, "settle_ms": 0}},
            {"sequence": {"steps": [{"at_ms": 0, "buttons": ["B"], "hold_ms": 25},
                                     {"at_ms": 100, "buttons": ["A"], "hold_ms": 50}], "settle_ms": 0}},
            {"wait": {"frames": 2}}, {"wait": {"state": "play"}}, {"wait": {"map": "AuthoredMap"}},
            {"wait": {"ms": 10}}, {"screenshot": {"path": "capture.png"}},
            {"expect_log": {"pattern": "(the control bridge)", "timeout_ms": 1000}},
            {"settings": {"name": "text_speed"}}, {"set_setting": {"name": "text_speed", "value": 2}},
            {"settings": {"name": "text_speed"}}, {"teleport": {"to": 1, "arrive_ms": 1000}},
            {"age": {"to": "child"}}, {"cheat": {"name": "age", "to": "adult"}},
            {"set_setting": {"name": "text_speed", "value": 3}},
        ])
        events = []
        result = self.execute(data, on_event=events.append)
        self.assertTrue(result["ok"], result)
        self.assertEqual(result["exit_code"], 0)
        self.assertEqual(events, result["transcript"])
        self.assertEqual(len([r for r in events if r["event"] == "step"]), len(data["steps"]))
        settings = [r["result"] for r in events if r.get("step", {}).get("settings")]
        self.assertEqual([r["option"]["value"] for r in settings], [3, 2])
        shot = self.folder / "out" / "shots" / "capture.png"
        self.assertTrue(shot.read_bytes().startswith(b"\x89PNG\r\n\x1a\n"))
        self.assertEqual(result["artifacts"][0]["path"], str(shot))
        saved = json.loads((self.folder / "out" / "transcript.json").read_text())
        self.assertEqual(saved, result)
        self.assertIn("set_setting", (self.folder / "out" / "transcript.txt").read_text())
        self.assert_stopped(result)

    def test_impossible_expectation_keeps_artifacts_and_stops_owned_game(self):
        data = self.scenario([{"screenshot": {"path": "before.png"}},
                              {"expect_log": {"pattern": "impossible authored marker", "timeout_ms": 30}},
                              {"set_setting": {"name": "text_speed", "value": 1}}])
        proc = run(self.folder, data, self.folder / "bad")
        self.assertEqual(proc.returncode, 3, proc.stderr)
        result = json.loads(proc.stdout)
        self.assertFalse(result["ok"])
        self.assertEqual(result["failed_step"], 2)
        self.assertIn("timed out", result["error"])
        self.assertTrue((self.folder / "bad" / "shots" / "before.png").is_file())
        self.assertTrue((self.folder / "bad" / "transcript.jsonl").is_file())
        self.assertNotIn('"cmd": "option_set"', self.requests.read_text())
        self.assert_stopped(result)

    def test_unsupported_cheat_is_not_dropped(self):
        result = self.execute(self.scenario([{"cheat": {"name": "heart_protection", "value": 1}}]))
        self.assertEqual((result["ok"], result["exit_code"], result["failed_step"]), (False, 1, 1))
        self.assertIn("not on the bridge", result["error"])
        self.assert_stopped(result)

    def test_ready_level_is_really_waited_for(self):
        data = self.scenario([{"press": {"buttons": ["A"], "hold_ms": 25, "settle_ms": 0}}])
        data["launch"]["env"].update(FAKE_PRESENT_AFTER="0.1", FAKE_PLAY_AFTER="0.25")
        result = self.execute(data)
        self.assertTrue(result["ok"], result)
        launched = result["transcript"][0]["result"]
        self.assertEqual(launched["ready"], "play")
        self.assertFalse(launched["ready_reply"]["game"]["loading"])
        self.assertFalse(launched["ready_reply"]["game"]["event"])
        self.assert_stopped(result)
        data["launch"]["env"]["FAKE_PLAY_AFTER"] = "99"
        data["launch"]["timeout"] = 0.2
        failed = self.execute(data, "not-ready")
        self.assertEqual((failed["ok"], failed["exit_code"], failed["failed_step"]), (False, 3, 0))
        self.assertIn("ready=play timed out", failed["error"])
        self.assertFalse(any(row["event"] == "step" for row in failed["transcript"]))
        self.assert_stopped(failed)

    def test_expect_is_previous_step_local_unless_start_is_explicit(self):
        steps = [{"wait": {"ms": 1}}, {"expect_log": {"pattern": "fake: started", "timeout_ms": 20}}]
        failed = self.execute(self.scenario(steps), "local")
        self.assertEqual(failed["exit_code"], 3)
        steps[1]["expect_log"]["since"] = "start"
        passed = self.execute(self.scenario(steps), "whole")
        self.assertTrue(passed["ok"], passed)

    def test_attached_game_is_not_stopped_on_failure(self):
        session = self.m.control.start_session(str(self.folder), exe=sys.executable, image=str(self.fake),
                                               root=str(self.folder / "sessions"), profile="test", ready="play",
                                               env={"FAKE_LOG": str(self.requests), "FAKE_SESSION": "1"})
        self.addCleanup(session.stop, 1, True)
        data = {"version": 1, "launch": {"attach": str(session.folder), "ready": "play"},
                "steps": [{"expect_log": {"pattern": "absent", "timeout_ms": 20}}]}
        result = self.execute(data)
        self.assertEqual(result["exit_code"], 3)
        self.assertFalse(result["owned"])
        self.assertFalse(any(row["event"] == "stop" for row in result["transcript"]))
        self.assertTrue(session.request("status")["ok"])
        self.assertNotIn('"cmd": "quit"', self.requests.read_text())

    def test_malformed_steps_report_index_before_launch(self):
        bad_steps = [{"press": {"buttons": ["A"]}, "wait": {"ms": 1}}, {"stick": {"x": 2}},
                     {"wait": {"ms": 1, "frames": 1}}, {"press": None}, {"sequence": {"steps": []}},
                     {"screenshot": {"path": "../outside.png"}}, {"expect_log": {"pattern": "x", "since": "now"}},
                     {"set_setting": {"name": "text_speed"}}, {"press": {"buttons": ["A"], "hold_ms": -1}}]
        for i, bad in enumerate(bad_steps):
            with self.subTest(step=bad):
                proc = run(self.folder, self.scenario([{"wait": {"ms": 0}}, bad]), self.folder / ("bad-%d" % i))
                self.assertEqual(proc.returncode, 2, proc.stdout + proc.stderr)
                self.assertIn("step 2", proc.stderr)
                self.assertFalse(self.requests.exists())
        path = self.folder / "broken.json"
        path.write_text("{", encoding="utf-8")
        proc = subprocess.run([sys.executable, "-I", str(RUNNER), str(path), "--game", str(self.folder)],
                              capture_output=True, text=True, timeout=10)
        self.assertEqual(proc.returncode, 2)
        self.assertIn("step 0", proc.stderr)

    def test_output_collision_and_version_refuse(self):
        out = self.folder / "occupied"
        out.mkdir()
        (out / "keep").write_text("kept")
        data = self.scenario([{"wait": {"ms": 0}}])
        with self.assertRaisesRegex(self.m.control.Failed, "new or empty"):
            self.m.run_scenario(data, game_dir=str(self.folder), out=str(out))
        self.assertEqual((out / "keep").read_text(), "kept")
        data["version"] = 2
        proc = run(self.folder, data, self.folder / "version")
        self.assertEqual(proc.returncode, 2)
        self.assertIn("step 0: version", proc.stderr)
        proc = run(self.folder, self.scenario([{"wait": {"ms": 0}}]), self.folder / "build", "--build")
        self.assertEqual(proc.returncode, 2)
        self.assertIn("unrecognized arguments", proc.stderr)

    def test_cli_save_slot_overrides_are_copied(self):
        source = self.folder / "source"
        (source / "slot_00").mkdir(parents=True)
        (source / "slot_03").mkdir()
        (source / "account.sav").write_bytes(b"authored")
        (source / "slot_03" / "progress.sav").write_bytes(b"authored progress")
        proc = run(self.folder, self.scenario([{"wait": {"ms": 0}}]), self.folder / "saved", "--save", str(source), "--slot", "03")
        self.assertEqual(proc.returncode, 0, proc.stdout + proc.stderr)
        result = json.loads(proc.stdout)
        info = self.assert_stopped(result)
        copied = Path(info["user"]) / "save" / self.m.control.SAVE_USER
        self.assertEqual(sorted(p.name for p in copied.iterdir()), ["account.sav", "slot_03"])
        self.assertEqual((source / "slot_03" / "progress.sav").read_bytes(), b"authored progress")


class NativeFrames(unittest.TestCase):
    def test_millisecond_schedule_reaches_real_controller_frames(self):
        m = load_module(RUNNER, "native_scenario")
        with tempfile.TemporaryDirectory(prefix="scenario-native-") as tmp:
            folder = Path(tmp)
            exe = folder / ("fixture.exe" if os.name == "nt" else "fixture")
            libs = ["-ladvapi32"] if os.name == "nt" else ["-lpthread", "-lm"]
            command = [COMPILER, "-std=c11", "-O1", "-g", "-Wall", "-Wextra", "-Werror", "-Wno-missing-field-initializers",
                       "-Wno-format-truncation", "-D_GNU_SOURCE", "-DHOS_CONTROL_NO_GAME",
                       str(ROOT / "tests" / "scenario_control_test.c"), str(ROOT / "host" / "control.c"), "-o", str(exe)] + libs
            subprocess.run(command, check=True)
            receipt = folder / "frames.csv"
            scenario = {"version": 1, "launch": {"exe": str(exe), "ready": "bridge", "timeout": 5,
                                                  "env": {"SCENARIO_FRAME_FILE": str(receipt)}}, "steps": [
                {"press": {"buttons": ["A"], "hold_ms": 25, "settle_ms": 0, "timeout_ms": 1000}},
                {"stick": {"which": "right", "x": 1, "y": -1, "hold_ms": 50, "settle_ms": 0, "timeout_ms": 1000}},
                {"sequence": {"steps": [{"at_ms": 0, "buttons": ["B"], "hold_ms": 25},
                                         {"at_ms": 1000, "buttons": ["A"], "hold_ms": 100},
                                         {"at_ms": 1250, "stick": "left", "x": -1, "hold_ms": 50}],
                              "settle_ms": 0, "timeout_ms": 1000}},
                {"wait": {"frames": 2}},
            ]}
            result = m.run_scenario(scenario, game_dir=str(folder), out=str(folder / "out"))
            self.assertTrue(result["ok"], result)
            replies = [r["result"] for r in result["transcript"] if r["event"] == "step"]
            with receipt.open(newline="") as f:
                frames = {int(row["frame"]): {k: int(v) for k, v in row.items()} for row in csv.DictReader(f)}
            press, stick, sequence = replies[:3]
            self.assertEqual(press["end"] - press["start"], 2)
            self.assertEqual([frames[n]["buttons"] for n in range(press["start"], press["end"])], [1, 1])
            self.assertEqual(frames[press["end"]]["buttons"], 0)
            self.assertEqual(stick["end"] - stick["start"], 3)
            for n in range(stick["start"], stick["end"]):
                self.assertEqual((frames[n]["rx"], frames[n]["ry"]), (32767, -32767))
            start = sequence["start"]
            self.assertEqual(sequence["end"] - start, 78)
            self.assertEqual([frames[n]["buttons"] for n in range(start, start + 2)], [2, 2])
            self.assertTrue(all(frames[n]["buttons"] == 0 for n in range(start + 2, start + 60)))
            self.assertEqual([frames[n]["buttons"] for n in range(start + 60, start + 66)], [1] * 6)
            self.assertTrue(all(frames[n]["lx"] == -32767 for n in range(start + 75, start + 78)))
            self.assertEqual(frames[start + 78]["lx"], 0)
            self.assertTrue(all(frames[n]["exclusive"] for n in range(start, start + 78)))
            print("PASS: real controller frames: press 2, stick 3, sequence offsets 0/60/75, holds 2/6/3", flush=True)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(add_help=False)
    parser.add_argument("--cc", default="gcc")
    args, rest = parser.parse_known_args()
    COMPILER = args.cc
    unittest.main(argv=[sys.argv[0]] + rest)
