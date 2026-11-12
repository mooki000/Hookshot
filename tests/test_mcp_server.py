"""The Hookshot MCP server: protocol shape, input lines, and a full fake-game run.

The fake game is a Python script: it reads the live input file and the screenshot request file and writes the same log
lines the real host does. No game build, GPU or game files are involved.

    python -I -B tests/test_mcp_server.py
"""
import asyncio
import json
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))

try:
    from mcp import Client
    from mcp.types import ImageContent
except ModuleNotFoundError as exc:   # (the runner reports the missing package unless --strict; direct runs say so)
    raise SystemExit(f"SKIP: {exc}")
RUN_ALL_MODULES = ["mcp"]
import hookshot_mcp as hookshot

def run_async(coro):
    return asyncio.run(coro)


def protocol_checks():
    async def checks():
        async with Client(hookshot.mcp) as client:
            tools = await client.list_tools()
            names = {tool.name for tool in tools.tools}
            expected = {"launch", "stop", "status", "press", "stick", "sequence", "screenshot", "wait", "logs",
                        "settings", "set_setting", "cheat"}
            if names != expected:
                raise SystemExit(f"unexpected tools: {sorted(names)}")
            resources = {resource.uri for resource in (await client.list_resources()).resources}
            if resources != {"hookshot://log", "hookshot://last-screenshot", "hookshot://status"}:
                raise SystemExit(f"unexpected resources: {sorted(resources)}")
            status = await client.call_tool("status", {})
            if status.is_error or status.structured_content != {"launched": False, "running": False}:
                raise SystemExit(f"status before launch: {status}")
            logs = await client.call_tool("logs", {})
            if not logs.is_error or "no run yet" not in logs.content[0].text:
                raise SystemExit(f"logs before launch: {logs}")
            stopped = await client.call_tool("stop", {})
            if stopped.is_error or stopped.structured_content != {"stopped": False, "already_stopped": True}:
                raise SystemExit(f"stop before launch: {stopped}")
    run_async(checks())


def stdio_checks():
    """The real transport: stdout must carry JSON-RPC and nothing else."""
    with tempfile.TemporaryDirectory(prefix="hookshot-mcp-stdio-") as tmp:
        proc = subprocess.Popen(
            [sys.executable, "-I", "-B", str(ROOT / "scripts" / "hookshot_mcp.py"), "--work-dir", tmp],
            stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, encoding="utf-8")

        def send(message):
            assert proc.stdin is not None
            proc.stdin.write(json.dumps(message) + "\n")
            proc.stdin.flush()

        def receive():
            assert proc.stdout is not None
            line = proc.stdout.readline()
            if not line:
                raise SystemExit("the MCP server closed stdout")
            return json.loads(line)

        try:
            send({"jsonrpc": "2.0", "id": 1, "method": "initialize", "params": {
                "protocolVersion": "2025-06-18", "capabilities": {},
                "clientInfo": {"name": "hookshot-test", "version": "1"}}})
            initialized = receive()
            if initialized.get("result", {}).get("serverInfo", {}).get("name") != "hookshot":
                raise SystemExit(f"initialize over stdio: {initialized}")
            send({"jsonrpc": "2.0", "method": "notifications/initialized"})
            send({"jsonrpc": "2.0", "id": 2, "method": "tools/list", "params": {}})
            listed = receive()
            names = {tool["name"] for tool in listed.get("result", {}).get("tools", [])}
            if "screenshot" not in names or "launch" not in names:
                raise SystemExit(f"tools/list over stdio: {listed}")
            assert proc.stdin is not None
            proc.stdin.close()
            proc.wait(timeout=10)
        finally:
            if proc.poll() is None:
                proc.terminate()
                try:
                    proc.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    proc.kill()


def native_checks():
    """The game-side seams the server depends on are still there."""
    vk = (ROOT / "gpu" / "vk" / "vk_replay.cpp").read_text(encoding="utf-8")
    menu = (ROOT / "gpu" / "vk" / "vk_menu.cpp").read_text(encoding="utf-8")
    backend = (ROOT / "host" / "rx_backend.c").read_text(encoding="utf-8")
    webgpu = (ROOT / "gpu" / "render" / "rx_core.cpp").read_text(encoding="utf-8")
    hid = (ROOT / "host" / "nn_hid.c").read_text(encoding="utf-8")
    main = (ROOT / "host" / "hos_main.c").read_text(encoding="utf-8")
    for needle in ("RX_VK_SHOT_FILE", "read_shot_request", "written to", "menu_settings_dump", "menu_cheat"):
        if needle not in vk:
            raise SystemExit(f"gpu/vk/vk_replay.cpp lost {needle}")
    for needle in ("mcp: dump end", "menu_settings_dump", "menu_cheat", "HOS_OOT_CHEATS"):
        if needle not in menu:
            raise SystemExit(f"gpu/vk/vk_menu.cpp lost {needle}")
    for needle in ("rx_settings_dump", "rx_setting_set", "rx_cheat"):
        if needle not in backend:
            raise SystemExit(f"host/rx_backend.c lost {needle}")
    for needle in ("!settings", "!setting ", "rx_cheat", "mcp: cheat ok"):
        if needle not in hid:
            raise SystemExit(f"host/nn_hid.c lost {needle}")
    if "RX_SCREENSHOT_FILE" not in webgpu:
        raise SystemExit("gpu/render/rx_core.cpp lost RX_SCREENSHOT_FILE")
    for needle in ("HOS_INPUT_SCRIPT_ONLY", "LX=", "!quit"):
        if needle not in hid:
            raise SystemExit(f"host/nn_hid.c lost {needle}")
    if "HOOKSHOT_NO_ENV_FILE" not in main:
        raise SystemExit("host/hos_main.c lost HOOKSHOT_NO_ENV_FILE")


def line_checks():
    assert hookshot.normalize_buttons("A+LUP") == ["A", "LUP"]
    assert hookshot.normalize_buttons(["plus", "left"]) == ["PLUS", "LEFT"]
    assert hookshot.stick_tokens("left", 1.0, 0.0) == ["LX=32767"]
    assert hookshot.stick_tokens("right", 0.0, -1.0) == ["RY=-32767"]
    try:
        hookshot.stick_tokens("left", 0.0, 0.0)
    except hookshot.ToolError:
        pass
    else:
        raise SystemExit("a centred stick was accepted")
    steps = [hookshot.Step(at_ms=1000, buttons=["A"]), hookshot.Step(at_ms=0, buttons=["PLUS"], hold_ms=100)]
    line, duration = hookshot.sequence_line(steps)
    if line != "0:PLUS/100,1000:A/200" or duration != 1200:
        raise SystemExit(f"sequence line: {line!r} {duration}")
    try:
        hookshot.normalize_buttons("NOPE")
    except hookshot.ToolError:
        pass
    else:
        raise SystemExit("an unknown button was accepted")


def env_checks():
    with tempfile.TemporaryDirectory(prefix="hookshot-mcp-env-") as tmp:
        folder = Path(tmp)
        exe = folder / ("hookshot.exe" if __import__("os").name == "nt" else "hookshot")
        exe.write_bytes(b"")
        (folder / "hookshot_env.txt").write_text(
            "\ufeff# a note\nHOS_TEST=1\nHOS_AUDIO=1\nHOS_HEADLESS=0\nRX_VK_SHOT_FILE=bad\n", encoding="utf-8")
        values = hookshot.read_env_file(exe)
        if values != {"HOS_TEST": "1", "HOS_AUDIO": "1", "HOS_HEADLESS": "0", "RX_VK_SHOT_FILE": "bad"}:
            raise SystemExit(f"env file parse: {values}")
        image = folder / "image.bin"
        image.write_bytes(b"")
        romfs = folder / "romfs"
        romfs.mkdir()
        cfg = hookshot.Config(work_dir=folder / "mcp", exe=exe, image=image, romfs=romfs, env_file=True)
        session = hookshot.GameSession(cfg, hookshot.LaunchOptions(force=True, extra_env={"RX_VK_FLASH": "1"}))
        env = session.build_env()
        if env.get("HOOKSHOT_NO_ENV_FILE") != "1" or env.get("HOS_HEADLESS") != "1":
            raise SystemExit("the game would read hookshot_env.txt a second time")
        if env.get("HOS_AUDIO") != "0" or env.get("RX_VK_FLASH") != "1" or env.get("HOS_TEST") != "1":
            raise SystemExit("the server's control switches did not override the env file")
        if env.get("HOS_INPUT_SCRIPT_ONLY") != "1" or env.get("HOS_INPUT_LIVE") != str(session.input_path):
            raise SystemExit("script-only input is not configured")
        if env.get("RX_VK_SHOT_FILE") != str(session.request_path) or env.get("RX_SCREENSHOT_FILE") != str(session.request_path):
            raise SystemExit("screenshot request paths are not configured")


def game_dir_checks():
    """A folder made by the builder or make_game.py keeps the game in bin\\, with the launcher at the top: --game-dir must
    choose the game, not the launcher that would open the builder instead (builder/setup/setup.c)."""
    with tempfile.TemporaryDirectory(prefix="hookshot-mcp-paths-") as tmp:
        folder = Path(tmp) / "game"
        (folder / "bin").mkdir(parents=True)
        game = folder / "bin" / hookshot.exe_name()
        game.write_bytes(b"")
        (folder / hookshot.exe_name()).write_bytes(b"")   # (the launcher)
        (folder / "image.bin").write_bytes(b"")
        (folder / "romfs").mkdir()
        paths = hookshot.resolve_paths(hookshot.Config(work_dir=Path(tmp) / "mcp", game_dir=folder),
                                       hookshot.LaunchOptions())
        if paths["exe"] != game.resolve():
            raise SystemExit(f"game_dir chose {paths['exe']}, not the game in bin")
        # (a development build, which has no bin\ folder, keeps the game at the top)
        (folder / "bin" / hookshot.exe_name()).unlink()
        (folder / "bin").rmdir()
        paths = hookshot.resolve_paths(hookshot.Config(work_dir=Path(tmp) / "mcp", game_dir=folder),
                                       hookshot.LaunchOptions())
        if paths["exe"] != (folder / hookshot.exe_name()).resolve():
            raise SystemExit(f"a development build: game_dir chose {paths['exe']}")


def fake_game_checks():
    with tempfile.TemporaryDirectory(prefix="hookshot-mcp-run-") as tmp:
        folder = Path(tmp)
        fake = ROOT / "tests" / "fake_game.py"
        romfs = folder / "romfs"
        romfs.mkdir()
        cfg = hookshot.Config(work_dir=folder / "mcp", exe=Path(sys.executable), image=fake, romfs=romfs,
                              env_file=False)
        session = hookshot.GameSession(cfg, hookshot.LaunchOptions(force=True))
        old_controller = hookshot.CONTROLLER
        controller = hookshot.Controller(cfg)
        controller.session = session
        hookshot.CONTROLLER = controller
        try:
            launched = session.launch()
            if not launched.get("pid"):
                raise SystemExit(f"launch did not return a process: {launched}")
            press = session.press(["A"], hold_ms=10, settle_ms=0)
            if not press["acked"]:
                raise SystemExit("the fake game did not acknowledge the live input")
            async def screenshot_tool():
                async with Client(hookshot.mcp) as client:
                    result = await client.call_tool("screenshot", {"inline": True, "timeout_ms": 5000})
                    if result.is_error:
                        raise SystemExit(f"screenshot tool: {result.content}")
                    if not any(isinstance(item, ImageContent) for item in result.content):
                        raise SystemExit(f"screenshot did not return an image block: {result.content}")
                    if len(result.content) < 1 or "present 7" not in result.content[0].text:
                        raise SystemExit(f"screenshot text: {result.content}")
                    status = await client.call_tool("status", {})
                    if not status.structured_content or not status.structured_content.get("ready"):
                        raise SystemExit(f"status after a screenshot: {status}")
            run_async(screenshot_tool())

            async def settings_tools():
                async with Client(hookshot.mcp) as client:
                    listed = await client.call_tool("settings", {})
                    got = listed.structured_content or {}
                    if listed.is_error or len(got.get("settings", [])) != 2 or not got.get("cheats_enabled"):
                        raise SystemExit(f"settings tool: {listed.content}")
                    if not any(s["name"] == "text_speed" and s["text"] == "Normal" and s["tab"] == "gameplay"
                               for s in got["settings"]):
                        raise SystemExit(f"settings rows: {got['settings']}")
                    if not any(c["name"] == "teleport" and got["teleports"][1]["name"] == "Hyrule Field"
                               for c in got["cheats"]):
                        raise SystemExit(f"cheat rows: {got['cheats']} {got['teleports']}")
                    changed = await client.call_tool("set_setting", {"name": "frame_generation", "value": 3})
                    if changed.is_error or (changed.structured_content or {}).get("value") != 3:
                        raise SystemExit(f"set_setting: {changed.content}")
                    acted = await client.call_tool("cheat", {"name": "teleport", "item": "Hyrule Field"})
                    if acted.is_error or (acted.structured_content or {}).get("value") != 1:
                        raise SystemExit(f"cheat: {acted.content}")
                    wrong = await client.call_tool("cheat", {"name": "teleport", "item": "Nowhere"})
                    if not wrong.is_error or "unknown teleport destination" not in wrong.content[0].text:
                        raise SystemExit(f"a bad destination was accepted: {wrong.content}")
            run_async(settings_tools())

            stopped = session.stop()
            if not stopped["stopped"]:
                raise SystemExit(f"stop: {stopped}")
        finally:
            if session.alive():
                session.stop(force=True)
            hookshot.CONTROLLER = old_controller


def main():
    protocol_checks()
    stdio_checks()
    native_checks()
    line_checks()
    env_checks()
    game_dir_checks()
    fake_game_checks()
    print("PASS: MCP protocol (in memory and stdio), native seams, input lines, environment, game_dir, settings and cheats, fake-game screenshot")


if __name__ == "__main__":
    main()
