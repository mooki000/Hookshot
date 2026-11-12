"""Optional local MCP adapter for hookshot_ctl sessions. Only stdio is served.

Install into the interpreter used by your MCP host:
    python -m pip install -r scripts/mcp_requirements.txt
    python -I scripts/hookshot_mcp.py --game-dir GAME_DIR

Nicholas Gower introduced Hookshot's MCP tools, real-SDK tests and guide in #1025.
This adapter uses the existing session API instead of a second game protocol.
"""
from __future__ import annotations

import argparse
import asyncio
import contextlib
import functools
import hashlib
import json
import socket
import sys
from dataclasses import dataclass, field
from importlib.metadata import version
from pathlib import Path
from typing import Annotated, Any, Literal

try:
    from fastmcp import FastMCP
    from fastmcp.exceptions import ToolError
    from fastmcp.tools import ToolResult
    from fastmcp.utilities.types import Image
    from pydantic import BaseModel, Field
except ImportError:
    print("Install: python -m pip install -r scripts/mcp_requirements.txt", file=sys.stderr)
    raise SystemExit(2)

if version("fastmcp") != "4.1.3":
    print("Install: python -m pip install -r scripts/mcp_requirements.txt", file=sys.stderr)
    raise SystemExit(2)

sys.path.insert(0, str(Path(__file__).resolve().parent))
import hookshot_ctl as ctl

MAX_JSON = 262144
MAX_IMAGE = 4 * 1024 * 1024
Seconds = Annotated[float, Field(gt=0, le=600, allow_inf_nan=False)]
Frames = Annotated[int, Field(ge=1, le=216000)]
Delay = Annotated[int, Field(ge=0, le=216000)]
Axis = Annotated[float, Field(ge=-1, le=1, allow_inf_nan=False)]
Name = Annotated[str, Field(min_length=1, max_length=4096)]
READ = {"readOnlyHint": True, "destructiveHint": False, "idempotentHint": True, "openWorldHint": False}
CHANGE = {"readOnlyHint": False, "destructiveHint": True, "idempotentHint": False, "openWorldHint": False}


class RunResult(BaseModel):
    run_id: str
    run: dict[str, Any]


class RunsResult(BaseModel):
    runs: list[RunResult]
    offset: int
    next_offset: int | None


class StatusResult(RunResult):
    bridge: dict[str, Any] | None


class ReplyResult(BaseModel):
    reply: dict[str, Any]


class LogResult(BaseModel):
    text: str
    cursor: int
    start: int
    more: bool
    truncated: bool
    matched: bool = False


class ScreenshotResult(ReplyResult):
    inline: bool
    bytes: int | None


class Step(BaseModel):
    at: Delay = 0
    frames: Frames = 8
    buttons: str | list[str] | None = None
    lx: Axis | None = None
    ly: Axis | None = None
    rx: Axis | None = None
    ry: Axis | None = None


@dataclass
class Config:
    root: Path = field(default_factory=lambda: Path(__file__).resolve().parents[1] / "work" / "control-runs")
    game_dir: str | None = None
    exe: str | None = None
    image: str | None = None
    romfs: str | None = None
    cwd: str | None = None
    env: dict[str, str] = field(default_factory=dict)
    wrapper: list[str] = field(default_factory=list)


def bounded(value):
    data = value.model_dump() if isinstance(value, BaseModel) else value
    if len(json.dumps(data, ensure_ascii=False).encode("utf-8")) > MAX_JSON:
        raise ctl.Failed("result exceeds 262144 bytes; use a smaller page or read the run artifacts", 2)
    return value


def errors(function):
    @functools.wraps(function)
    async def call(*args, **kwargs):
        try:
            value = await function(*args, **kwargs)
            return value if isinstance(value, ToolResult) else bounded(value)
        except ctl.Failed as exc:
            raise ToolError(json.dumps({"code": exc.code, "message": str(exc)[:4096]})) from exc
        except (OSError, ValueError) as exc:
            raise ToolError(json.dumps({"code": 2, "message": str(exc)[:4096]})) from exc
    return call


async def complete(function, *args, **kwargs):
    """Do not abandon a session lifecycle operation halfway through a manifest update."""
    task = asyncio.create_task(asyncio.to_thread(function, *args, **kwargs))
    try:
        return await asyncio.shield(task)
    except asyncio.CancelledError:
        with contextlib.suppress(Exception):
            await task
        raise


def disconnect(bridge):
    # Closing a socket alone need not wake another thread's recv on Linux.
    if bridge.sock:
        with contextlib.suppress(OSError):
            bridge.sock.shutdown(socket.SHUT_RDWR)
    bridge.close()


class Runs:
    def __init__(self, cfg):
        self.cfg, self.current = cfg, None
        self.root = cfg.root.resolve()
        self.sessions, self.locks = {}, {}

    def folder(self, run):
        path = Path(run)
        path = (path if path.is_absolute() else self.root / path).resolve()
        if path == self.root or not path.is_relative_to(self.root):
            raise ctl.Failed("run folder must be inside the configured root", 2)
        return path

    def remember(self, session):
        self.folder(session.folder)
        key = hashlib.sha256(str(session.folder).encode("utf-8")).hexdigest()[:16]
        self.sessions[key] = session
        self.locks.setdefault(key, asyncio.Lock())
        return key

    def select(self, run):
        if run is None:
            run = self.current
        if not run:
            raise ctl.Failed("no run selected; launch or attach first", 2)
        if run in self.sessions:
            return run, self.sessions[run]
        session = ctl.Session(self.folder(run))
        return self.remember(session), session

    async def local(self, run, method, **kwargs):
        key, session = self.select(run)
        async with self.locks[key]:
            return await complete(getattr(session, method), **kwargs)

    async def request(self, run, cmd, timeout=30, **kwargs):
        key, session = self.select(run)
        async with self.locks[key]:
            # Each call gets its own connection; the per-run lock also orders mutations.
            # A cancelled wait disconnects and joins its worker before releasing the lock.
            connecting = asyncio.create_task(asyncio.to_thread(session.connect))
            try:
                bridge = await asyncio.shield(connecting)
            except asyncio.CancelledError:
                with contextlib.suppress(Exception):
                    disconnect(await connecting)
                raise
            task = asyncio.create_task(asyncio.to_thread(bridge.request, cmd, timeout=timeout, **kwargs))
            try:
                return ctl.check(await asyncio.shield(task))
            except asyncio.CancelledError:
                disconnect(bridge)
                with contextlib.suppress(Exception):
                    await task
                raise
            finally:
                bridge.close()

    async def status(self, run):
        key, session = self.select(run)
        info = await self.local(key, "info")
        reply = None if info["state"] in ("finished", "failed") else await self.request(key, "status")
        return StatusResult(run_id=key, run=info, bridge=reply)

    async def image(self, run):
        info = await self.local(run, "info")
        shots = [a for a in info["artifacts"] if a.get("kind") == "shot"]
        if not shots:
            raise ctl.Failed("no screenshot in this run", 2)
        path = Path(shots[-1]["path"]).resolve()
        if not path.is_relative_to(Path(info["run"]).resolve() / "shots"):
            raise ctl.Failed("last screenshot is outside the run's shots folder; use its file path", 2)
        return await complete(image_bytes, path)


def image_bytes(path):
    with Path(path).open("rb") as source:
        data = source.read(MAX_IMAGE + 1)
    if len(data) > MAX_IMAGE:
        raise ctl.Failed("PNG exceeds the 4 MiB inline limit; use its file path", 2)
    if not data.startswith(b"\x89PNG\r\n\x1a\n"):
        raise ctl.Failed("screenshot is not a PNG", 2)
    return data


def create_server(cfg=None):
    cfg = cfg or Config()
    runs = Runs(cfg)
    mcp = FastMCP("hookshot", instructions=(
        "Local same-user game testing, not a sandbox. Launch a copied test profile or attach a run. "
        "Wait on state rather than guessed sleeps. Screenshots are files unless inline=true. "
        "Stop has no save barrier. Runs survive server exit; explicitly stop owned test runs."))

    @mcp.tool(annotations=CHANGE)
    @errors
    async def launch(game_dir: str | None = None, run: str | None = None, save: str | None = None,
                     slot: str | None = None, ready: Literal["bridge", "present", "play"] = "bridge",
                     timeout: Seconds = 180) -> RunResult:
        """Start a windowless, muted test profile with its own user folder; optionally copy a save/slot.

        Executable, environment, cache paths and admission wrapper are server configuration, not tool arguments.
        A configured game_dir cannot be changed by a tool; without one, the caller chooses what is executed.
        Run folders stay under the configured root. Save may be anywhere readable; it is only read and copied.
        Readiness is checked after admission. Play readiness does not press through menus for you.
        """
        game = game_dir or cfg.game_dir
        if not game:
            raise ctl.Failed("game_dir is required (or start the server with --game-dir)", 2)
        if cfg.game_dir and Path(game).resolve() != Path(cfg.game_dir).resolve():
            raise ctl.Failed("game_dir differs from the server's configured game folder", 2)
        run = str(runs.folder(run)) if run is not None else None
        session = await complete(ctl.start_session, game, run=run, root=str(runs.root), save=save, slot=slot,
                                 profile="test", ready=ready, timeout=timeout, exe=cfg.exe, image=cfg.image,
                                 romfs=cfg.romfs, cwd=cfg.cwd, env=cfg.env, wrapper=cfg.wrapper)
        key = runs.remember(session)
        runs.current = key
        return RunResult(run_id=key, run=await runs.local(key, "info"))

    @mcp.tool(name="runs", annotations=READ)
    @errors
    async def list_runs(offset: Annotated[int, Field(ge=0)] = 0,
                        limit: Annotated[int, Field(ge=1, le=100)] = 20) -> RunsResult:
        """Page through saved run manifests in the configured root; register their resource IDs."""
        infos = await complete(ctl.list_runs, str(cfg.root))
        page = [RunResult(run_id=runs.remember(ctl.Session(info["run"])), run=info)
                for info in infos[offset:offset + limit]]
        return RunsResult(runs=page, offset=offset, next_offset=offset + limit if offset + limit < len(infos) else None)

    @mcp.tool(annotations=READ)
    @errors
    async def show(run: str | None = None) -> RunResult:
        """Read a run manifest, including after exit. Run is a folder or a returned run_id; omitted means current."""
        key, _ = runs.select(run)
        return RunResult(run_id=key, run=await runs.local(key, "info"))

    @mcp.tool(annotations={**CHANGE, "destructiveHint": False})
    @errors
    async def attach(run: Name) -> RunResult:
        """Select an existing live session after checking its process identity; do not take over its ownership."""
        _, session = runs.select(run)
        session = await complete(ctl.Session.attach, session.folder)
        runs.current = key = runs.remember(session)
        return RunResult(run_id=key, run=await runs.local(key, "info"))

    @mcp.tool(annotations=READ)
    @errors
    async def status(run: str | None = None) -> StatusResult:
        """Combine the run manifest with the bridge's current status, without overwriting either set of fields."""
        return await runs.status(run)

    @mcp.tool(annotations=CHANGE)
    @errors
    async def press(buttons: str | list[str], frames: Frames = 8, at: Delay = 0,
                    exclusive: bool = True, wait: bool = True, timeout: Seconds = 60,
                    run: str | None = None) -> ReplyResult:
        """Hold buttons for game frames. Waited replies report completed start/end frames, not elapsed seconds."""
        return ReplyResult(reply=await runs.request(run, "press", timeout, buttons=buttons, frames=frames,
                                                   at=at, exclusive=exclusive, wait=wait))

    @mcp.tool(annotations=CHANGE)
    @errors
    async def stick(which: Literal["left", "right"] = "left", x: Axis = 0, y: Axis = 0, frames: Frames = 8,
                    at: Delay = 0, exclusive: bool = True, wait: bool = True, timeout: Seconds = 60,
                    run: str | None = None) -> ReplyResult:
        """Hold an analog stick; positive y is up. Centered sticks are supported."""
        return ReplyResult(reply=await runs.request(run, "stick", timeout, stick="L" if which == "left" else "R",
                                                   x=x, y=y, frames=frames, at=at, exclusive=exclusive, wait=wait))

    @mcp.tool(annotations=CHANGE)
    @errors
    async def sequence(steps: Annotated[list[Step] | None, Field(max_length=256)] = None,
                       script: Annotated[str | None, Field(max_length=60000)] = None,
                       units: Literal["frames", "hos"] = "frames", exclusive: bool = True,
                       wait: bool = True, timeout: Seconds = 60, run: str | None = None) -> ReplyResult:
        """Send frame steps OR a script. HOS scripts use seconds/ms at 60 Hz, nearest frame, halves up;
        positive holds are at least one frame. Frame generation does not count as input frames.
        """
        if (steps is None) == (script is None) or (steps is not None and (not steps or units != "frames")):
            raise ctl.Failed("give nonempty frame steps OR script with units=frames|hos", 2)
        args = {"steps": [s.model_dump(exclude_none=True) for s in steps]} if steps is not None else {
            "script": script, "units": units}
        return ReplyResult(reply=await runs.request(run, "sequence", timeout, exclusive=exclusive, wait=wait, **args))

    @mcp.tool(annotations=CHANGE)
    @errors
    async def release(all: bool = False, run: str | None = None) -> ReplyResult:
        """Release abandoned queued inputs; all=true also releases other connected clients' inputs."""
        return ReplyResult(reply=await runs.request(run, "release", all=all))

    @mcp.tool(annotations=CHANGE, output_schema=ScreenshotResult.model_json_schema())
    @errors
    async def screenshot(path: str | None = None, inline: bool = False, timeout: Seconds = 30,
                         run: str | None = None) -> ToolResult:
        """Capture a Vulkan PNG in the run's shots folder by default. Inline images are opt-in, at most 4 MiB.
        Explicit paths must be relative, without '..', and resolve inside the run folder; existing files may be replaced.
        """
        key, session = runs.select(run)
        if path is not None:
            relative = Path(path)
            if relative.is_absolute() or ".." in relative.parts:
                raise ctl.Failed("screenshot path must be relative without '..'", 2)
            path = str(session.folder / relative)
        args = session.prepare("shot", {"path": path})
        expected = Path(args["path"]).resolve()
        if not expected.is_relative_to(session.folder) or expected == session.folder:
            raise ctl.Failed("screenshot path must stay inside the run folder (no links out)", 2)
        args["path"] = str(expected)
        reply = await runs.request(key, "shot", timeout, **args)
        if Path(reply["path"]).absolute() != expected:
            raise ctl.Failed("game reported a different screenshot path; refusing to read it", 2)
        data = await complete(image_bytes, expected) if inline else None
        result = bounded(ScreenshotResult(reply=reply, inline=inline, bytes=len(data) if data is not None else None))
        return ToolResult(content=[Image(data=data, format="png")] if data is not None else [],
                          structured_content=result.model_dump())

    @mcp.tool(annotations=READ)
    @errors
    async def wait(event: Literal["present", "frame", "state", "in_play", "map", "loading_start",
                                  "loading_end", "change", "wall"] = "present",
                   frames: Frames = 1, frame: Delay = 0, state: str | None = None, map: str | None = None,
                   seconds: Seconds = 1, timeout: Seconds = 60, run: str | None = None) -> ReplyResult:
        """Wait for frames/state/map, or explicitly choose event=wall for elapsed seconds (not game readiness)."""
        if event == "wall":
            key, _ = runs.select(run)
            await runs.local(key, "require_live")
            await asyncio.sleep(seconds)
            return ReplyResult(reply=await runs.local(key, "info"))
        return ReplyResult(reply=await runs.request(run, "wait", timeout, event=event, frames=frames, frame=frame,
                                                   state=state, map=map))

    @mcp.tool(annotations=READ)
    @errors
    async def options(name: str | None = None, run: str | None = None) -> ReplyResult:
        """Read supported Insert-menu value rows, or one key; debug action rows are not options."""
        return ReplyResult(reply=await runs.request(run, "option_get", name=name))

    @mcp.tool(annotations=CHANGE)
    @errors
    async def set_option(name: Name, value: float | bool, run: str | None = None) -> ReplyResult:
        """Change and persist an option through the native menu setter; read back its actual applied value."""
        return ReplyResult(reply=await runs.request(run, "option_set", name=name, value=value))

    @mcp.tool(annotations=CHANGE)
    @errors
    async def teleport(to: str | int | None = None, timeout: Seconds = 30,
                       run: str | None = None) -> ReplyResult:
        """List destinations without to; otherwise ask the game to teleport. Acceptance is not arrival: wait on map."""
        return ReplyResult(reply=await runs.request(run, "teleport", timeout, to=to))

    @mcp.tool(annotations=CHANGE)
    @errors
    async def age(to: Literal["child", "adult"] | None = None, timeout: Seconds = 30,
                  run: str | None = None) -> ReplyResult:
        """Request an age or toggle it; acceptance is not reload completion. Wait on loading_end afterward."""
        return ReplyResult(reply=await runs.request(run, "age", timeout, to=to))

    @mcp.tool(annotations=READ)
    @errors
    async def saves(run: str | None = None) -> ReplyResult:
        """Read the current save folder; includes the slot metadata. This does not force a save or validate a checkpoint."""
        return ReplyResult(reply=await runs.request(run, "saves"))

    @mcp.tool(annotations=READ)
    @errors
    async def logs(lines: Annotated[int, Field(ge=0, le=2000)] = 100,
                   since: Annotated[int | None, Field(ge=0)] = None,
                   limit: Annotated[int, Field(ge=4, le=65536)] = 65536, run: str | None = None) -> LogResult:
        """Read a bounded log tail or bytes since a cursor; more=true means continue at the returned cursor."""
        return LogResult(**await runs.local(run, "log", lines=lines, since=since, limit=limit))

    @mcp.tool(annotations=READ)
    @errors
    async def expect_log(text: Name, since: Annotated[int, Field(ge=0)], timeout: Seconds = 10,
                         run: str | None = None) -> LogResult:
        """Wait for a literal, case-sensitive log substring after a required byte cursor (not an old match)."""
        return LogResult(**await runs.local(run, "expect_log", text=text, since=since, timeout=timeout, limit=65536))

    @mcp.tool(annotations=CHANGE)
    @errors
    async def stop(timeout: Seconds = 10, terminate: bool = False, run: str | None = None) -> RunResult:
        """Return the session owner's exact stop receipt. Quit is NOT save-safe. Force only with terminate=true.
        A quit timeout returns process_exited=false and leaves the game owned; inspect before deciding on force.
        """
        key, _ = runs.select(run)
        return RunResult(run_id=key, run=await runs.local(key, "stop", timeout=timeout, terminate=terminate))

    @mcp.tool(annotations={**CHANGE, "destructiveHint": False})
    @errors
    async def finish(run: str | None = None) -> RunResult:
        """Mark a dead orphan run finished; refuses live games/owners and never invents an exit code."""
        key, _ = runs.select(run)
        return RunResult(run_id=key, run=await runs.local(key, "finish"))

    @mcp.resource("hookshot://runs/{run_id}/status", mime_type="application/json")
    @errors
    async def status_resource(run_id: str) -> str:
        """The registered run's manifest and current bridge status."""
        if run_id not in runs.sessions:
            raise ctl.Failed("unknown resource run_id; call runs, launch or attach", 2)
        return bounded(await runs.status(run_id)).model_dump_json()

    @mcp.resource("hookshot://runs/{run_id}/log", mime_type="application/json")
    @errors
    async def log_resource(run_id: str) -> str:
        """The registered run's last 100 log lines, bounded to 65536 bytes, with its cursor."""
        if run_id not in runs.sessions:
            raise ctl.Failed("unknown resource run_id; call runs, launch or attach", 2)
        return json.dumps(await runs.local(run_id, "log", lines=100, limit=65536))

    @mcp.resource("hookshot://runs/{run_id}/last-screenshot", mime_type="image/png")
    async def last_screenshot_resource(run_id: str) -> bytes:
        """The registered run's last PNG in its shots folder, bounded to 4 MiB."""
        try:
            if run_id not in runs.sessions:
                raise ctl.Failed("unknown resource run_id; call runs, launch or attach", 2)
            return await runs.image(run_id)
        except (ctl.Failed, OSError) as exc:
            raise ToolError(json.dumps({"code": getattr(exc, "code", 2), "message": str(exc)[:4096]})) from exc

    return mcp


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=Config().root)
    for name in ("game-dir", "exe", "image", "romfs", "cwd"):
        parser.add_argument("--" + name)
    parser.add_argument("--wrapper", default="[]", help="server-owned admission argv as JSON; no shell expansion")
    parser.add_argument("--env", action="append", default=[], metavar="NAME=VALUE")
    args = parser.parse_args(argv)
    try:
        wrapper = json.loads(args.wrapper)
        if not isinstance(wrapper, list) or not all(isinstance(s, str) and s for s in wrapper):
            raise ValueError("wrapper must be a JSON argv array")
        env = {}
        for item in args.env:
            name, value = item.split("=", 1)
            if not name:
                raise ValueError("--env needs NAME=VALUE")
            env[name] = value
    except (ValueError, TypeError) as exc:
        parser.error(str(exc))
    cfg = Config(root=args.root, game_dir=args.game_dir, exe=args.exe, image=args.image,
                 romfs=args.romfs, cwd=args.cwd, env=env, wrapper=wrapper)
    create_server(cfg).run(transport="stdio", show_banner=False)


if __name__ == "__main__":
    main()
