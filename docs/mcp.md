# Automated tests through MCP

`scripts/hookshot_mcp.py` is an optional [Model Context Protocol](https://modelcontextprotocol.io) server for local
in-game checks. It starts a built Hookshot, sends frame-timed controller input, reads state and options, and captures
PNG files. It is a separate stdio process, not part of the game. It opens no network listener.

This guide and the protocol-test approach come from Nicholas Gower's [#1025](https://github.com/NotRexed/Hookshot/pull/1025).
His guide and checks are retained in the import history. The adapter now calls the standard-library
[control client and sessions](control.md#sessions); it adds no game-side command, file transport or log parser.
The command-line client works without MCP. The scenario runner and contributor-process proposals from #1025 are
separate work, not requirements of this server.
This server is part 2 of 3, stacked on [#2904](https://github.com/NotRexed/Hookshot/pull/2904);
review only the commits above it. The third part is the separate scenario runner.

## Install

Use Python 3.12 or newer and a virtual environment. Install the exact optional pin, **FastMCP 4.1.3**, separately
from the game's requirements. Its dependencies are resolved by pip; the file is not a lock of every transitive package.

```sh
python -m venv work/mcp-venv
work/mcp-venv/bin/python -m pip install -r scripts/mcp_requirements.txt
work/mcp-venv/bin/python -I scripts/hookshot_mcp.py --game-dir GAME_DIR
```

On Windows the interpreter is `work\mcp-venv\Scripts\python.exe`. Use that same interpreter in your MCP host.
A missing or different SDK prints one install line to stderr and exits non-zero. Nothing installs itself.
The server's stdout contains only MCP messages; diagnostics use stderr.

For a host using `mcpServers`, use absolute paths:

```json
{
  "mcpServers": {
    "hookshot": {
      "command": "C:/src/Hookshot/work/mcp-venv/Scripts/python.exe",
      "args": ["-I", "C:/src/Hookshot/scripts/hookshot_mcp.py", "--game-dir", "C:/Games/Hookshot-test"]
    }
  }
}
```

The server defaults to `work/control-runs` under the checkout. `--root` selects another run parent.
`--exe`, `--image`, `--romfs`, `--cwd` and repeated `--env NAME=VALUE` configure the launch below the tools.
A builder folder uses its `bin` executable, not its root launcher. `--cwd` requires `--image`.
Use an owned game copy: settings and caches are not isolated by copying saves.

On a shared test machine, configure `--wrapper '["admission-program","argument"]'` on the **server**, never in a
tool call. The wrapper must run the remaining argv unchanged and wait for it. It holds admission around the session
supervisor's whole lifetime, not just the launch call. There is no shell expansion and no second GPU lock.
An already admitted driver can instead keep its admission alive around the server and all of its runs.

## Tools

All tools have result models and read-only/destructive hints. These hints describe effects; they are not permissions.
`run` is a returned `run_id` or a run folder. Omit it to use the most recently launched or attached run.
Results preserve the session or bridge's plain data, under `run` or `reply`; status keeps both `run` and `bridge`.

| Tool | Effect | What it does |
|---|---|---|
| `launch` | Changes files/processes | Start a windowless, muted test profile with an owned user folder; optionally copy `save` and `slot`; choose `ready=bridge`, `present` or `play`. |
| `runs` | Read-only | Page through saved manifests in the configured root (`offset`, `limit`, at most 100); return resource IDs. |
| `show` | Read-only | Read a manifest, even after exit. |
| `attach` | Selects a session | Select an existing live run after checking process identity; do not adopt or signal an arbitrary process. |
| `status` | Read-only | Return the manifest and bridge status separately, without overwriting their distinct states. |
| `press` | Changes game state | Hold buttons for `frames` after delay `at`; exclusive input and completion replies by default. |
| `stick` | Changes game state | Hold the `left` or `right` analog stick; x/y in -1..1, positive y up; centered sticks work. |
| `sequence` | Changes game state | Send up to 256 frame steps, or a script with explicit `units=frames` or `hos`. |
| `release` | Changes input state | Release abandoned queued inputs; `all=true` also releases other connected clients' inputs. |
| `screenshot` | Writes a file | Capture Vulkan PNG in the run's shots folder; `inline=false` by default; opt-in image at most 4 MiB. |
| `wait` | Read-only | Wait for `present` frames, absolute `frame`, `state`, `in_play`, `map`, loading changes, or explicit `event=wall` seconds. |
| `options` | Read-only | Read supported Insert-menu value rows, or one `name`; not the Debug tab's actions. |
| `set_option` | Persists settings | Change `name`/`value` through the menu setter; report the actual applied value. |
| `teleport` | Changes game state | Without `to`, list destinations; otherwise request a destination and then wait for its map. |
| `age` | Changes game state | Request `child` or `adult`, or toggle; then wait for reload completion. |
| `saves` | Read-only | Read the mounted save folder and slot metadata; no force-save, editing or checkpoint validation. |
| `slots` | Read-only | Return the same save-slot metadata as the command-line client's slots view. |
| `logs` | Read-only | Read a tail (`lines`) or bytes from `since`; return a byte cursor and truncation flags. |
| `expect_log` | Read-only | Wait for literal, case-sensitive `text` after a required `since` byte cursor. |
| `stop` | Ends a process | Return the owner's exact quit/exit receipt; force is opt-in with `terminate=true`; not save-safe. |
| `finish` | Updates metadata | Mark a dead orphan run finished; refuse live owners/games; never invent an exit code. |

Buttons: `A B X Y L R ZL ZR PLUS MINUS LS RS LEFT UP RIGHT DOWN`, plus bridge stick-direction names.
Presses and sticks use game-present frames, not wall time. A waited input returns `start` and `end` when completed;
`wait=false` acknowledges queueing only. Use `release` to cancel those abandoned queued inputs.
Each run's calls are serialized; calls on different runs can proceed independently. Cancelling a bridge wait closes
its dedicated connection and joins the worker before another call uses the run. It does not stop the game.
Local lifecycle/log operations finish their bounded session call before cancellation returns. Launch readiness has
its own timeout after admission; the admission wrapper owns the queue and total lifetime limits.

`sequence(steps=[...])` uses frame-valued `at` and `frames`, with `buttons` and optional `lx`, `ly`, `rx`, `ry`.
Alternatively `script="0.025:A/25,1:B/1", units="hos"` uses seconds before `:` and milliseconds after `/`.
The control client converts at 60 presents/second, nearest frame with halves up, and a positive hold is at least one
frame: this example becomes `(at=2, frames=2)` and `(at=60, frames=1)`. Frame generation adds no input frames.
No `settle_ms` is hidden in a press: use a frame/state wait, or an explicitly labelled wall wait.

Outputs are bounded to 262,144 JSON bytes. Logs return at most 65,536 source bytes and 2,000 tail lines; `more=true`
means continue at `cursor`. Oversized structured results are errors, not silently incomplete successes.
`Failed` errors retain the control client's category in the tool error text as JSON: `code=1` game refusal,
`2` usage/file/connection error, `3` timeout. A timed-out stop is different: it returns the owner's manifest with
`process_exited=false`, and the game stays owned. Inspect a result before retrying any mutation.

## Resources

Use the `run_id` from launch, attach, show or runs. After restarting the server, call runs or attach to register it.
These are resource templates, advertised by `resources/templates/list`.

| Resource template | Content |
|---|---|
| `hookshot://runs/{run_id}/status` | JSON manifest and current bridge status, as the status tool. |
| `hookshot://runs/{run_id}/log` | JSON tail of 100 lines, at most 65,536 source bytes, with its cursor. |
| `hookshot://runs/{run_id}/last-screenshot` | Last acknowledged PNG in the run's shots folder, at most 4 MiB. |

Images are files first. A screenshot's explicit relative path is relative to the run folder; an absolute path is
used as supplied and can replace an existing file. Inline reading requires the bridge to return the requested path.
The image resource refuses paths outside the run's shots folder; use the returned file path for those captures.
A missing or oversized image gives an error. There is no automatic resize and no desktop capture.

## A worked session

These are MCP tool calls, not Python functions. Start from a disposable game copy and your own save source:

```text
launch(save="SAVE_FOLDER", slot="03", ready="present")
status()
wait(event="state", state="menu")
press(buttons="A", frames=8)
wait(event="present", frames=60)
status()
```

Inspect the current state and picture, then advance the visible title/Continue/Load UI as needed. A copied save does
not bypass these menus. Once play is observed, the ordinary check can continue:

```text
wait(event="in_play")
stick(which="left", x=0.25, y=0, frames=6)
stick(which="right", x=0, y=0.25, frames=6)
sequence(steps=[{"at": 12, "buttons": "B", "frames": 8}])
release()
screenshot()
screenshot(inline=true)
options(name="fps_counter")
set_option(name="fps_counter", value=true)
options(name="fps_counter")
logs(lines=20)
saves()
stop()
stop()
```

Keep the option's old value and restore it in your owned copy when the check ends. To assert on a new log line, take
`logs(lines=0)` first and pass its cursor to `expect_log(text=..., since=...)` after the action. Use a line this
build actually prints. The full log and all screenshots remain in the run folder.

Bridge readiness only means a status reply. Present readiness can still be a black startup frame. Play readiness
requires presents, the draw-count play heuristic, no stall, and known-false event/loading values. It does not prove
snapshot freshness or drive the title. `state=play` alone is not proof an event is over; inspect `bridge.game` too.
Teleport acceptance is not arrival. An age change can reload the same map name, so use `loading_end`, not just map
identity. These are the [bridge's limits](control.md#known-limits), not new MCP guarantees.

## Driving the game

The following are **Nicholas Gower's observations from #1025**, not new universal timing or performance claims.
The control names are retained; the old fixed sleeps are replaced with state waits and inspected pictures.

### Start-up

- The first presented frame can be black while startup work is still in progress. Wait for the menu and inspect it.
- A populated save folder offers Continue, then Load / Cancel; another A confirms loading. An empty folder starts a
  new game and its introductory sequence. Wait for play and known-false event/loading before movement assertions.
- A copied save and an owned game/cache folder make repeatable checks easier. Cache warmth and competing runs affect
  startup; no fixed duration or speed ratio is promised here.

### Controls and pause

| Action observed | Input |
|---|---|
| Use item; quick select | `ZR`; `L`, `R` |
| Pause; map | `PLUS`; `MINUS` |
| Action/dash; put away | `A`; `B` |
| Jump; attack; target | `X`; `Y`; `ZL` |
| Camera reset; movement/camera | `RS`; left/right stick |

Nicholas observed that pause opens the last tab (Collection, Equipment, Items or System). D-pad movement at a row's
edge and ZR change tabs; a gold cell marks the equipped or quick-slot item. He verified equipment changes with A
and the visible model, but did not find selecting an Items cell to assign it. Pause stops the player update where
debug actions are taken, even while controller reads continue. Close pause and wait for play before such actions.
A completed bridge input proves its scheduled frames elapsed, not that a particular UI accepted the button.

### Persistence and iteration

Nicholas observed that reopening the pause menu refreshes its grid from saved game state. A transient memory poke is
not a lasting item change. Use the game's own UI/setter. This adapter exposes no guest-memory writes or cheat fallback.
Builds remain separate command-line work. Keep saves and caches out of git, copy saves per test, and keep pictures as
files until visual inspection is needed. No build-time or frame-rate estimate from #1025 is a promise for another PC.

## Not covered

This cutover is deliberately not full parity with #1025. No old capability disappears without naming it here.

| Original feature not carried | Why; native change needed |
|---|---|
| Cheats other than teleport and age: heart protection, flight, free camera, mark/return, time, pouch and inventory | The bridge does not expose these debug actions or their catalogues; they need typed native bridge queries/actions with game-thread acknowledgement. |
| WebGPU on-demand screenshots | The existing shot callback is Vulkan-only; WebGPU needs an implementation of the same native callback, not another request file. |
| Run-long exclusion of physical input | Bridge exclusivity covers each queued input's delay and hold, not idle gaps; a native input-boundary policy or lease is needed. |
| Save-safe quit | Neither the old live-input quit nor the current bridge waits for saving; a native save-I/O barrier and coordinated shutdown are needed. |

## Trust and lifetime

**It runs as you and trusts its MCP host.** This is a local same-user tool, not a sandbox. The configured executable,
environment and admission wrapper run with your authority. Tool-supplied game/save/run/screenshot paths also carry
that authority. Use only trusted hosts, agents and manifests. Do not expose it to an untrusted client. There is no
arbitrary-shell tool, network transport, memory-write tool, package installer or login operation.

The session supervisor, not the MCP connection, owns the game. End of input or server exit does not kill launched or
attached runs; their manifests remain discoverable. Explicitly stop your disposable runs before ending the client.
A failed readiness check is cleaned up by the session layer and leaves a failed manifest and log.
Stopping asks for quit and reports the real exit receipt. `save_drain` stays `unsupported`, never “clean”. Forced
termination needs `terminate=true` and the original owner; no attaching caller kills a PID read from a manifest.
A copied save limits damage to that copy but is not evidence of a safe checkpoint.

## Game-free checks

```sh
python -I -B tests/test_mcp_server.py
python -I -B tests/test_docs.py
python -I -B tests/run_all.py
```

Use the venv interpreter for SDK checks. The real SDK client exercises tools/resources in memory, and a real stdio
subprocess checks initialize, list, call, resource, error and end of input. The shared control fixture publishes an
authored tiny PNG, following Nicholas's test idea, and keeps option state. Tests cover frame conversion, concurrent
calls, cancellation, output bounds, failed launch cleanup and explicit force. General docs checks need no SDK.
`RUN_ALL_MODULES = ["fastmcp"]` lets the runner skip only the SDK checks without the package; `--strict` fails a
missing dependency. CI installs this optional dependency in a separate step on Linux and Windows. Game-free checks
are not proof of Windows gameplay, a visible window, WebGPU capture or save-safe shutdown.
