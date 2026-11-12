"""Real FastMCP clients and stdio against the shared, game-free control fixture.

Adapted from Nicholas Gower's #1025 in-memory, stdio and live-doc-catalogue checks.
No SDK replacement, game build, GPU or game data is used.
    python -I -B tests/test_mcp_server.py
"""
import asyncio
import base64
import contextlib
import json
import os
from pathlib import Path
import queue
import re
import subprocess
import sys
import tempfile
import threading
import time
import unittest

RUN_ALL_MODULES = ["fastmcp"]
ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))
sys.path.insert(0, str(ROOT / "tests"))
try:
    from fastmcp import Client
except ImportError:
    raise unittest.SkipTest("install scripts/mcp_requirements.txt for MCP protocol tests")
import hookshot_mcp as server
from test_hookshot_ctl import FAKE


class Fixture:
    def __init__(self, root):
        self.root = Path(root)
        self.fake = self.root / "fake.py"
        self.fake.write_text(FAKE, encoding="utf-8")
        self.requests = self.root / "requests.jsonl"
        self.cfg = server.Config(root=self.root / "runs", game_dir=str(self.root), exe=sys.executable,
                                 image=str(self.fake), env={"FAKE_LOG": str(self.requests), "FAKE_SESSION": "1"})

    def rows(self):
        return [json.loads(line) for line in self.requests.read_text(encoding="utf-8").splitlines()] \
            if self.requests.exists() else []

    def cleanup(self):
        for info in server.ctl.list_runs(self.cfg.root):
            session = server.ctl.Session(info["run"])
            if info["state"] not in ("finished", "failed"):
                session.stop(timeout=0.2, terminate=True)


class InMemory(unittest.IsolatedAsyncioTestCase):
    async def asyncSetUp(self):
        self.tmp = tempfile.TemporaryDirectory(prefix="mcp-test-")
        self.fixture = Fixture(self.tmp.name)
        self.client = Client(server.create_server(self.fixture.cfg))
        await self.client.__aenter__()

    async def asyncTearDown(self):
        await self.client.__aexit__(None, None, None)
        await asyncio.to_thread(self.fixture.cleanup)
        self.tmp.cleanup()

    async def call(self, tool_name, **args):
        result = await self.client.call_tool(tool_name, args)
        return result.structured_content

    async def error(self, tool_name, code, **args):
        result = await self.client.call_tool(tool_name, args, raise_on_error=False)
        self.assertTrue(result.is_error, result)
        self.assertIn('"code": %d' % code, result.content[0].text)
        return result

    async def until(self, predicate):
        deadline = time.monotonic() + 5
        while not predicate():
            self.assertLess(time.monotonic(), deadline, "fake did not reach expected condition")
            await asyncio.sleep(0.01)

    async def test_catalogue_matches_docs_and_models(self):
        tools = await self.client.list_tools()
        templates = await self.client.list_resource_templates()
        doc = (ROOT / "docs" / "mcp.md").read_text(encoding="utf-8")
        section = doc.split("## Tools\n", 1)[1].split("\n## ", 1)[0]
        resources = doc.split("## Resources\n", 1)[1].split("\n## ", 1)[0]
        self.assertEqual({t.name for t in tools}, set(re.findall(r"^\| `([^`]+)` \|", section, re.M)))
        self.assertEqual({t.uri_template for t in templates},
                         set(re.findall(r"^\| `([^`]+)` \|", resources, re.M)))
        self.assertEqual(await self.client.list_resources(), [])
        for tool in tools:
            self.assertIsNotNone(tool.output_schema, tool.name)
            self.assertIsNotNone(tool.annotations.read_only_hint, tool.name)
            self.assertIsNotNone(tool.annotations.destructive_hint, tool.name)
        launch = next(t for t in tools if t.name == "launch")
        self.assertNotIn("wrapper", launch.input_schema["properties"])
        self.assertNotIn("exe", launch.input_schema["properties"])
        self.assertEqual(server.version("fastmcp"), "4.1.3")
        await self.error("status", 2)

    async def test_full_session_and_resources(self):
        source = self.fixture.root / "source"
        (source / "slot_03").mkdir(parents=True)
        (source / "account.sav").write_bytes(b"authored account")
        (source / "slot_03" / "progress.sav").write_bytes(b"authored save")
        launched = await self.call("launch", save=str(source), slot="03", ready="play")
        key, info = launched["run_id"], launched["run"]
        self.assertEqual(info["profile"], "test")
        self.assertEqual(info["environment"]["HOS_HEADLESS"], "1")
        self.assertEqual(info["environment"]["HOS_AUDIO"], "0")
        self.assertEqual(Path(info["user"]).parent, Path(info["run"]))
        self.assertEqual((source / "slot_03" / "progress.sav").read_bytes(), b"authored save")
        self.assertEqual((await self.call("runs"))["runs"][0]["run_id"], key)
        self.assertEqual((await self.call("show"))["run"], info)
        self.assertEqual((await self.call("attach", run=info["run"]))["run_id"], key)
        status = await self.call("status")
        self.assertEqual(status["bridge"]["state"], "play")
        self.assertEqual(status["run"]["pid"], info["pid"])
        await self.call("press", buttons="A", frames=3, at=2)
        await self.call("stick", which="left", x=0.25, y=-0.5, frames=4)
        await self.call("stick", which="right", x=0, y=0, frames=4)
        seq = (await self.call("sequence", script="0.025:A/25,1:B/1", units="hos"))["reply"]
        self.assertEqual(seq["end"] - seq["start"], 59)
        await self.call("sequence", steps=[{"at": 12, "frames": 5, "lx": 0.1}])
        encoded = [r for r in self.fixture.rows() if r["cmd"] == "sequence"]
        self.assertEqual(encoded[0]["steps"], [{"at": 2, "frames": 2, "buttons": "A"},
                                               {"at": 60, "frames": 1, "buttons": "B"}])
        self.assertTrue(encoded[0]["exclusive"])
        await self.call("release")
        await self.call("wait", frames=3)
        await self.call("wait", event="state", state="play")
        await self.call("wait", event="map", map="authored-map")
        await self.call("wait", event="wall", seconds=0.01)
        self.assertEqual((await self.call("options", name="fps_counter"))["reply"]["option"]["value"], 0)
        await self.call("set_option", name="fps_counter", value=True)
        self.assertEqual((await self.call("options", name="fps_counter"))["reply"]["option"]["value"], 1)
        self.assertIn("destinations", (await self.call("teleport"))["reply"])
        await self.call("age", to="child")
        self.assertEqual(len((await self.call("saves"))["reply"]["slots"]), 2)
        cursor = (await self.call("logs", lines=0))["cursor"]
        shot = await self.client.call_tool("screenshot", {})
        self.assertFalse(any(c.type == "image" for c in shot.content))
        path = Path(shot.structured_content["reply"]["path"])
        self.assertEqual(path.parent, Path(info["run"]) / "shots")
        self.assertTrue(path.read_bytes().startswith(b"\x89PNG\r\n\x1a\n"))
        inline = await self.client.call_tool("screenshot", {"inline": True})
        picture = next(c for c in inline.content if c.type == "image")
        self.assertTrue(base64.b64decode(picture.data).startswith(b"\x89PNG"))
        await self.call("expect_log", text="(the control bridge)", since=cursor)
        tail = await self.call("logs", limit=32)
        self.assertLessEqual(len(tail["text"].encode()), 32)
        history = await self.call("logs", since=cursor)
        self.assertIn("written to", history["text"])
        for kind in ("status", "log", "last-screenshot"):
            resource = await self.client.read_resource(f"hookshot://runs/{key}/{kind}")
            self.assertTrue(resource)
        await self.error("set_option", 1, name="nope", value=0)
        await self.error("wait", 3, event="loading_end", timeout=0.1)
        await self.error("sequence", 2, steps=[])
        stopped = (await self.call("stop"))["run"]
        self.assertTrue(stopped["process_exited"])
        self.assertEqual(stopped["save_drain"], "unsupported")
        self.assertEqual((await self.call("stop"))["run"], stopped)
        self.assertEqual((await self.call("finish"))["run"], stopped)
        self.assertIsNone((await self.call("status"))["bridge"])

    async def test_serialization_and_wait_cancellation(self):
        await self.call("launch")
        wait = asyncio.create_task(self.client.call_tool("wait", {"event": "state", "state": "never", "timeout": 0.2},
                                                        raise_on_error=False))
        await self.until(lambda: any(r["cmd"] == "_wait_begin" for r in self.fixture.rows()))
        pending_status = asyncio.create_task(self.call("status"))
        self.assertTrue((await wait).is_error)
        await pending_status
        rows = self.fixture.rows()
        end = next(i for i, r in enumerate(rows) if r["cmd"] == "_wait_end")
        self.assertTrue(any(r["cmd"] == "status" for r in rows[end + 1:]))
        wait = asyncio.create_task(self.call("wait", event="state", state="never", timeout=60))
        await self.until(lambda: sum(r["cmd"] == "_wait_begin" for r in self.fixture.rows()) == 2)
        wait.cancel()
        with self.assertRaises(asyncio.CancelledError):
            await wait
        await asyncio.wait_for(self.call("status"), timeout=3)
        await self.until(lambda: any(r.get("cancelled") for r in self.fixture.rows()))
        await self.call("press", buttons="B")

    async def test_failed_launch_has_receipt_and_no_live_process(self):
        self.fixture.cfg.env["FAKE_NO_BRIDGE"] = "1"
        await self.error("launch", 3, timeout=0.2)
        run = (await self.call("runs"))["runs"][0]["run"]
        self.assertEqual(run["state"], "failed")
        self.assertTrue(run["process_exited"])
        self.assertFalse(server.ctl.same_process(run["pid"], run["process_start"]))
        self.assertFalse(server.ctl.owners_alive(run))
        self.assertTrue(Path(run["log"]).exists())

    async def test_stop_does_not_force_without_opt_in(self):
        self.fixture.cfg.env["FAKE_REFUSE_QUIT"] = "1"
        await self.call("launch")
        timed = (await self.call("stop", timeout=0.1))["run"]
        self.assertFalse(timed["stop_result"]["process_exited"])
        self.assertFalse(timed["stop_result"]["forced"])
        stopped = (await self.call("stop", timeout=0.1, terminate=True))["run"]
        self.assertTrue(stopped["stop_result"]["forced"])
        self.assertEqual(stopped["save_drain"], "unsupported")

    async def test_configured_path_boundaries(self):
        outside = self.fixture.root / "outside"
        outside.mkdir()
        await self.error("launch", 2, game_dir=str(outside))
        for path in ("../escape", str(outside / "run")):
            await self.error("launch", 2, run=path)
            await self.error("show", 2, run=path)
            await self.error("attach", 2, run=path)
            await self.error("stop", 2, run=path, terminate=True)
        self.assertFalse((outside / "run").exists())
        launched = await self.call("launch", run="chosen")
        folder = Path(launched["run"]["run"])
        self.assertEqual(folder, self.fixture.cfg.root / "chosen")
        for path in ("../escape.png", "shots/../inside.png", str(folder / "absolute.png")):
            await self.error("screenshot", 2, path=path)
        self.assertFalse(any(r["cmd"] == "shot" for r in self.fixture.rows()))
        shot = await self.call("screenshot", path="relative.png")
        self.assertEqual(Path(shot["reply"]["path"]), folder / "relative.png")
        await self.call("stop")
        self.fixture.cfg.game_dir = None
        chosen = await self.call("launch", game_dir=str(outside))
        self.assertEqual(chosen["run"]["game"], str(outside))

    async def test_symlinks_cannot_escape_paths(self):
        outside = self.fixture.root / "outside"
        outside.mkdir()
        self.fixture.cfg.root.mkdir()
        link = self.fixture.cfg.root / "linked"
        try:
            link.symlink_to(outside, target_is_directory=True)
        except OSError as exc:
            if os.name == "nt" and getattr(exc, "winerror", None) == 1314:
                self.skipTest("this Windows account cannot create symbolic links")
            raise
        await self.error("launch", 2, run=str(link / "run"))
        await self.error("show", 2, run=str(link / "run"))
        await self.error("attach", 2, run=str(link / "run"))
        await self.error("stop", 2, run=str(link / "run"), terminate=True)
        game_link = self.fixture.root / "game-link"
        game_link.symlink_to(outside, target_is_directory=True)
        await self.error("launch", 2, game_dir=str(game_link))
        launched = await self.call("launch")
        (Path(launched["run"]["run"]) / "out").symlink_to(outside, target_is_directory=True)
        await self.error("screenshot", 2, path="out/escape.png")
        self.assertFalse((outside / "escape.png").exists())
        self.assertFalse(any(r["cmd"] == "shot" for r in self.fixture.rows()))

    async def test_images_and_json_are_bounded(self):
        launched = await self.call("launch")
        shot = await self.call("screenshot")
        path = Path(shot["reply"]["path"])
        path.write_bytes(b"\x89PNG\r\n\x1a\n" + b"x" * server.MAX_IMAGE)
        with self.assertRaises(server.ctl.Failed):
            server.image_bytes(path)
        with self.assertRaises(Exception):
            await self.client.read_resource(f"hookshot://runs/{launched['run_id']}/last-screenshot")
        session = server.ctl.Session(launched["run"]["run"])
        session.update(extra="x" * server.MAX_JSON)
        await self.error("show", 2)
        session.update(extra=None)


class Stdio(unittest.TestCase):
    def test_initialize_list_call_resource_error_and_eof(self):
        with tempfile.TemporaryDirectory(prefix="mcp-wire-") as tmp:
            fixture = Fixture(tmp)
            with open(Path(tmp) / "server.stderr", "w+", encoding="utf-8") as err:
                args = [sys.executable, "-I", "-B", str(ROOT / "scripts" / "hookshot_mcp.py"),
                        "--root", str(fixture.cfg.root), "--game-dir", tmp, "--exe", sys.executable,
                        "--image", str(fixture.fake)]
                for key, value in fixture.cfg.env.items():
                    args += ["--env", key + "=" + value]
                proc = subprocess.Popen(args, stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=err,
                                        text=True, encoding="utf-8")
                incoming = queue.Queue()

                def reader():
                    for line in proc.stdout:
                        incoming.put(json.loads(line))  # any stdout diagnostics fail the transport test
                    incoming.put(None)

                thread = threading.Thread(target=reader, daemon=True)
                thread.start()
                rid = 0

                def send(method, params=None, notify=False):
                    nonlocal rid
                    msg = {"jsonrpc": "2.0", "method": method, "params": params or {}}
                    if not notify:
                        rid += 1
                        msg["id"] = rid
                    proc.stdin.write(json.dumps(msg) + "\n")
                    proc.stdin.flush()
                    if notify:
                        return None
                    while True:
                        reply = incoming.get(timeout=30)
                        self.assertIsNotNone(reply)
                        if reply.get("id") == rid:
                            return reply

                try:
                    hello = send("initialize", {"protocolVersion": "2025-06-18", "capabilities": {},
                                                "clientInfo": {"name": "hookshot-test", "version": "1"}})
                    self.assertEqual(hello["result"]["serverInfo"]["name"], "hookshot")
                    send("notifications/initialized", notify=True)
                    tools = send("tools/list")["result"]["tools"]
                    self.assertIn("launch", {t["name"] for t in tools})
                    templates = send("resources/templates/list")["result"]["resourceTemplates"]
                    self.assertEqual(len(templates), 3)
                    launched = send("tools/call", {"name": "launch", "arguments": {}})["result"]
                    self.assertFalse(launched.get("isError", False), launched)
                    key = launched["structuredContent"]["run_id"]
                    status = send("tools/call", {"name": "status", "arguments": {}})["result"]
                    self.assertEqual(status["structuredContent"]["bridge"]["state"], "play")
                    resource = send("resources/read", {"uri": f"hookshot://runs/{key}/status"})
                    self.assertIn("contents", resource["result"])
                    error = send("tools/call", {"name": "set_option", "arguments": {"name": "nope", "value": 1}})
                    self.assertTrue(error["result"]["isError"])
                    self.assertIn('"code": 1', error["result"]["content"][0]["text"])
                    stopped = send("tools/call", {"name": "stop", "arguments": {}})
                    self.assertTrue(stopped["result"]["structuredContent"]["run"]["process_exited"])
                    proc.stdin.close()
                    proc.wait(timeout=15)
                    self.assertEqual(proc.returncode, 0)
                    self.assertIsNone(incoming.get(timeout=5))
                finally:
                    if proc.poll() is None:
                        proc.kill()
                        proc.wait()
                    proc.stdout.close()
                    if not proc.stdin.closed:
                        proc.stdin.close()
                    fixture.cleanup()


if __name__ == "__main__":
    unittest.main()
