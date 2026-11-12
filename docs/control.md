# The control bridge

A local socket inside the game process that tools, tests and coding agents use to drive a running game and read its
state. Today they write presses into a file the game polls (`HOS_INPUT_LIVE`), take screenshots with `RX_VK_SHOTS` and
read the game's state from the log (the `vk: N presents, F fps; a frame: D draws` line). The bridge does the same things
through one connection, with replies. Presses are timed in frames, and every reply carries the frame it was made at.

- The game side is `host/control.c`. Hooks: `host/nn_hid.c` (the controller's read), `gpu/vk/vk_replay.cpp` (shots at a
  present), `gpu/vk/vk_menu.cpp` (the Insert menu's settings, through its own setter), `host/hos_thread.c` (log lines),
  `host/hos_main.c` (the start, and for OoT the current map and the debug teleport), `host/hos_mem.c` (guest memory's
  bounds), `host/nn_fs.c` (the save folder), `host/rx_backend.c` (the renderer's side, `MkwVkRenderer`).
- The client is `scripts/hookshot_ctl.py` (standard library only).
- Tests: `tests/test_control.py` (the bridge against a fake game) and `tests/test_hookshot_ctl.py` (the client against a
  fake bridge).

## Turning it on

```
HOS_CONTROL=/run/user/1000/hookshot/control.sock ./hookshot
```

It is off unless `HOS_CONTROL` is set. When it is on, the log says so once:
`control: listening on <path> (HOS_CONTROL: ...)`. On Windows the value is a named pipe, `\\.\pipe\<name>`; a bare name
gets that prefix.

`hookshot_ctl.py launch <game folder>` starts a game folder this way. It makes a persistent run folder (mode 0700 on
Linux) with a manifest, `log.txt` and `shots/`. The socket stays in a separate short private folder; the manifest records
it. The default waits until the bridge answers, then prints the socket's path. See [Sessions](#sessions).

## Security

- **Opt-in.** There is no socket unless `HOS_CONTROL` names one.
- **Local only.** There is a Unix domain socket on Linux and a named pipe on Windows, and never TCP. The pipe refuses
  remote clients.
- **This user only.** The socket is created with mode 0600 (the umask is set around `bind` too), and a client whose
  `SO_PEERCRED` user differs from the game's is turned away. The pipe's DACL grants the current user alone.
  `FILE_FLAG_FIRST_PIPE_INSTANCE` keeps another process from owning the name first.
- **A folder of this user's.** The socket's folder has to be owned by the game's user and writable by no one else
  (not a shared `/tmp`): otherwise the bridge stays off and the log says why. Another user could otherwise put a link
  or a socket of theirs where the bridge binds, removes a stale socket, or writes a shot by a relative path.
  `hookshot_ctl.py launch` makes such a folder (0700, in `$XDG_RUNTIME_DIR`). The path is always a file system path:
  there is no abstract socket name.
- **Nothing in the way is replaced.** A file at the path that is not a socket is left alone and the bridge stays off. A
  socket that a live game answers on is left alone too. A stale one, left by a game that ended, is removed.
- **Guest memory is a second opt-in.** It is off unless `HOS_CONTROL_MEM` is set, apart from `HOS_CONTROL`:
  `HOS_CONTROL_MEM=ro` allows reads, `rw` allows reads and writes (aliases: `r`/`read` for `ro`, `readwrite` for `rw`;
  case does not matter; any other value logs once and leaves it off). See [Guest memory](#guest-memory).
- **What a client can do.** Anything the player can do with a controller and the Insert menu's settings (not its debug
  tools). It can also write a PNG to any path the game's user can write, list the save folder, and end the game.
- **Bounded messages.** A request line is at most 64 KB (longer: `too_long`, and the rest of the line is skipped); an
  `id` at most 200 characters as JSON; at most 8 clients, 64 requests waiting, 256 inputs queued, 8 memory requests of
  1 MB each. Replies a client does not read are dropped with the client once 8 MB wait.
- **The game never waits for a client.** The bridge's own thread does all the socket I/O. The controller's read takes
  one short lock that the bridge never holds over I/O. A client that sends nonsense gets errors. A client that stops
  reading is dropped once 8 MB waits for it, and one that goes away mid-command has its presses let go.

**The client trusts the game no further than it must.** `hookshot_ctl.py` may run outside a sandbox the game is in, so
what it does with the game's replies is checked: `slots --thumbs` takes only digit slot names and a bounded slice of
that slot's `caption.sav`, and plain output shows control characters from the game (a log line's escape sequences) as
`?`. `launch --save` refuses a save folder that holds links or anything but plain files and folders: a save from
someone else could hold a link to one of this user's files, which the copy would read and the game then write through.

**With a Landlock sandbox of the game's process** (an open proposal, #882): the bridge's socket is made before the
restriction (it starts with the game), so the sandbox need not allow making sockets. The bridge's thread is started
before the restriction too, so it is covered only where the restriction covers every thread at once (Landlock ABI 8,
`LANDLOCK_RESTRICT_SELF_TSYNC`); on an older kernel it would stay outside the sandbox, like the window's main thread
there, and a game taken over could act through it. If both land, start the bridge after the restriction (with its
folder allowed) or keep it off under the sandbox without ABI 8. The other way round there is nothing to gain: a client
already runs as the game's user outside the sandbox, so reaching the game through the bridge gives it nothing it did not
have. The socket's folder must stay writable for shots taken there.

## Protocol

The protocol is newline-delimited JSON. Each request is one JSON object on one line, at most 64 KB. Each reply is one
line, and it gives back the request's `id` (any JSON value; `null` when there is none) and the current `frame` (the
game's presents so far):

```
> {"id": 1, "cmd": "status"}
< {"id":1,"ok":true,"frame":5321,"protocol":1,"fps":59.8,"state":"play","stalled":false,"draws":2410.3,...}
```

Requests on one connection may be answered out of order: a `status` sent while a `wait` is pending is answered first.
Match replies by `id`. Lines with an `"event"` key and no `"ok"` key are subscription events (see `subscribe`).

A client may shut its sending side once its requests are sent (`printf '...\n' | socat - UNIX-CONNECT:<path>`): the
bridge reads no more from it, ends its subscription, answers what it waits for, and closes the connection once those
replies are out. (Linux; a Windows pipe has no half-close.)

### Errors

```
< {"id":7,"ok":false,"frame":5400,"error":"bad_request","message":"frames: 1 to 216000"}
```

| error | when |
|---|---|
| `bad_json` | the line is not JSON (`id` is `null`) |
| `too_long` | a line over 64 KB; the rest of it is skipped and the connection goes on |
| `bad_request` | not an object, no `cmd`, or a bad argument |
| `unknown_cmd` | no such command |
| `timeout` | a `wait`, `shot` or input did not finish before its `timeout` (seconds) |
| `busy` | too many requests waiting (64), inputs queued (256) or clients (8) |
| `unavailable` | the renderer is not up yet, or is not the Vulkan one (shots, settings); or a memory command that is not served |
| `forbidden` | a memory command while `HOS_CONTROL_MEM` does not allow it |
| `out_of_range` | a memory address or range outside mapped guest memory |
| `not_found` | no such setting |
| `failed` | a shot could not be written |
| `refused` | the game turned a teleport down (an event playing, a map loading, ...); the message says why |
| `cancelled` | a press that was waited on was let go by `release`, or a teleport was replaced by a later one |

### Frames and inputs

A frame is one present of the game's own; frames made by frame generation do not count. An input starts at the first
controller read after its request arrives, plus `at` frames, and is held for `frames` frames. The game reads the
controller once a frame, so `"frames": 8` is 8 game frames. The bridge's buttons are added to the player's. On the
axes it sets, its sticks replace a script's (`HOS_INPUT_SCRIPT`); the player's own pad, keys and mouse still count after
it, the larger deflection winning, unless the input is exclusive. `"exclusive": true` keeps the player's own pad, keyboard and mouse out from
the request until the input ends (compare `HOS_INPUT_EXCLUSIVE`).

The client's `press` and `stick` take frame counts unchanged. For `sequence --hos`, it converts seconds (`at`) and
milliseconds (hold) at **60 game presents per second, rounding to the nearest frame with halves up**, and clamps a
positive hold to at least one frame, just as the bridge's HOS-script parser does. The fake receives those integer
`steps`, not a seconds/ms string: `0.025:A/25,1:B/1` becomes `(at=2, frames=2), (at=60, frames=1)`.
These are game-clock times, not a promise of elapsed wall time; the bridge still accepts HOS scripts from other clients.

Buttons use the names `HOS_INPUT_SCRIPT` uses: `A B X Y L R ZL ZR PLUS MINUS LS RS LEFT UP RIGHT DOWN`. The stick
directions are `LUP LDOWN LLEFT LRIGHT RUP RDOWN RLEFT RRIGHT`, and `LX= LY= RX= RY=` set an axis from -32767 to 32767
(positive y is up). Names are given as `"A+B"` or `["A", "B"]`.

The reply comes once the input is over, with the frames it covered: `{"start": 5400, "end": 5408}` (`end` is the first
frame without it). With `"wait": false` the reply is immediate: `{"queued": 1, "frames": 8}`.

### Commands

**status**: the frame, the frame rate over the last second, the draws a frame over the last ~300 ms, the state, the
renderer's presents (`replayed`: what a shot's `present` counts in), the picture's size, the map the game is in, the
version.

```
> {"id":1,"cmd":"status"}
< {"id":1,"ok":true,"frame":5321,"protocol":1,"fps":59.8,"state":"play","stalled":false,"draws":2410.3,"replayed":5320,
   "window":{"width":1280,"height":720},"uptime":95.2,"clients":1,"input":{"queued":0,"exclusive":false},
   "map":"KokiriForest_Child","game":{"event":false,"loading":false,"age":"child"},"version":"1.4.0-abc123",
   "renderer":"Vulkan: ..."}
```

`map` is the game's own name for the map (`Sequence_CurrentBanc`), read on the game's thread with the player's update
every 250 ms: the last one known, so it stays through loads and menus, and it is `null` until Link was first seen. `game`
(OoT, read with it) says whether an event is playing (a cutscene, a map change's), whether a map is loading
(`Sequence_IsLoadingBanc`) and Link's age: `{"event":false,"loading":false,"age":"child"}`, `null` until then. A script
that steps through places waits for `event` and `loading` to be false before its next step, instead of a log line.

`state` is a guess from the draws a frame, the way test scripts read the log:

| state | meaning |
|---|---|
| `boot` | nothing presented yet |
| `intro` | under 11 draws, before any menu was seen |
| `menu` | 11 to 149 draws |
| `loading` | under 11 draws, after a menu or play |
| `play` | 150 or more |
| `unknown` | no Vulkan renderer to count draws |

A new state has to hold for 300 ms before it is taken. `stalled` is true when no frame has come for a second.

**press**: `buttons`, `frames` (default 8), `at`, `exclusive`, `wait`, `timeout`.

```
> {"id":2,"cmd":"press","buttons":"A","frames":8}
< {"id":2,"ok":true,"frame":5410,"start":5401,"end":5409}
```

**stick**: `stick` (`L` default, or `R`), `x` and `y` from -1 to 1, then as press. Buttons can go with it.

```
> {"id":3,"cmd":"stick","stick":"L","x":0,"y":1,"frames":120,"exclusive":true}
```

**sequence**: `steps`, a list of press or stick objects each with its own `at`. Or `script`, `HOS_INPUT_SCRIPT`'s syntax
in frames, `<at>:<names>[/<frames held>],...` (8 frames held unless given). With `"units": "hos"` the script's times are
in seconds and its holds in milliseconds, as `HOS_INPUT_SCRIPT` and `HOS_INPUT_LIVE` take them (60 frames a second; 200
ms held unless given). The reply comes when the last step is over: `start` is the first step's start and `end` the last
step's end.

```
> {"id":4,"cmd":"sequence","script":"0:A/8,60:A/8"}
> {"id":5,"cmd":"sequence","script":"0:LUP/2000,2:A","units":"hos"}
> {"id":6,"cmd":"sequence","steps":[{"at":0,"buttons":"ZL","frames":30},{"at":10,"lx":-1,"frames":20}]}
```

**release**: the caller's queued inputs are let go, and those that clients which have gone left behind (sent with
`"wait": false`): `{"released": 3, "all": false}`. Another connected client's inputs stay. With `"all": true`, every
client's. Presses that were waited on reply `cancelled`.

An input sent with `"wait": false` stays queued after its client goes (so a one-shot `hookshot_ctl.py press --no-wait`
works) and is freed once it is over. An input that is waited on is let go when its client goes.

**shot**: a PNG of the first present after the request, or after frame `after`. It is the size `RX_VK_SHOTS` writes
(1280x720, or `RX_VK_SHOT_W` x `RX_VK_SHOT_H`); the reply's `width` and `height` are read from the file. `path` defaults to `shots/shot_<n>_f<frame>.png` in the socket's folder (made 0700). A relative path is
taken from the socket's folder, not the game's (on Windows, where a pipe has no folder, from `%TEMP%\hookshot-control-<pipe name>\`, which also holds `shots\`). A shot is the game's own picture: the Insert menu and the FPS counter, drawn over it on the way to the window, are not in it. The file is written whole under another name, then renamed, so the
path in the reply is complete. The `timeout` default is 30 s. Shots need the Vulkan renderer.

```
> {"id":7,"cmd":"shot","path":"/tmp/run/kokiri.png"}
< {"id":7,"ok":true,"frame":5440,"present":5432,"path":"/tmp/run/kokiri.png","width":1280,"height":720}
```

**wait**: `event`, plus a `timeout` in seconds (default 60). The reply has the state and the frames waited:
`{"state": "play", "waited": 1203}`. A timeout replies `timeout`, with the state at that time.

| event | until |
|---|---|
| `present` | `frames` more frames (default 1) |
| `frame` | frame number `frame` |
| `in_play` | the state is `play` (at once if it is) |
| `state` | the state is `state` |
| `loading_start` | the next change into `loading` |
| `loading_end` | the next change out of `loading` |
| `change` | the next change of state |
| `map` | the map is `map` (any case; at once if it is); without `map`, the next change of map. The reply has `map` |

**option_get**: the Insert menu's settings, or one of them by `name`. A name is the setting's key in
`hookshot_options.ini` (`text_speed`) or its row's name (`"Text speed"`), in any case, with `-` taken as `_`. The
settings are the rows with a value that the menu shows on this PC (a row for what the GPU cannot run is not listed),
not the section headings and not the Debug tab's tools. The bridge finds a row by its key each time: a key stays the
same when rows come and go or are renamed, a place in the list does not. `restart` marks a row that takes effect at the
next start; `fixed`, one with nothing to choose on this PC (the menu shows it greyed).

```
> {"id":8,"cmd":"option_get","name":"text_speed"}
< {"id":8,"ok":true,"frame":5500,"option":{"key":"text_speed","name":"Text speed","tab":"gameplay","value":0,
   "min":0,"max":3,"step":1,"restart":false,"fixed":false,"text":"Normal"}}
```

**option_set**: `name` and `value` (a number, or `true`/`false`). The value is set through the menu's own setter, as
Left and Right set it: cut to the row's range, the Quality preset turned to Custom unless the row is one it leaves
alone, a restart noted for the restart question, and kept in `hookshot_options.ini`. The rows the arrows step through
a list of their own (the Upscaler, Render resolution, the output resolution) are walked in the arrows' order until they
reach the value; one that none of their steps reach on this PC leaves them, the Quality preset and the rest as they
were, and replies `failed`. A `fixed` row replies `failed` too. The reply has the value it had before and the setting as it is now. The menu's lock keeps a change apart from the menu's own frame.

```
> {"id":9,"cmd":"option_set","name":"FPS counter","value":true}
< {"id":9,"ok":true,"frame":5510,"was":0,"option":{"key":"fps_counter",...,"value":1,"text":"On"}}
```

**subscribe**: a stream of events on this connection until `unsubscribe` or a disconnect; another `subscribe` replaces
it. Arguments:

- `events`: any of `present`, `state`, `log` and `map`. The default is `["present", "state"]`.
- `every`: frames between present events (default 60, 1 to 216000).
- `log`: only log lines that contain this text (it also turns `log` on).

The events carry `sub`, the subscribe request's `id`:

```
< {"event":"present","sub":10,"frame":5600,"fps":59.9,"state":"play","draws":2405.0}
< {"event":"state","sub":10,"frame":5720,"state":"loading","from":"play","draws":5.0}
< {"event":"log","sub":10,"frame":5721,"line":"[hos sead::MainThread] ..."}
< {"event":"map","sub":10,"frame":5900,"map":"HyruleField_Child","from":"KokiriForest_Child"}
< {"event":"log_dropped","sub":10,"lines":40}
```

**teleport** (OoT): the Debug tab's teleport, by destination: `to`, the destination's index or one of its maps' names
(any case). Without `to`, the destinations: `{"destinations":[{"index":0,"child":"KokiriForest_Child","adult":
"KokiriForest_Adult"},...]}` (a destination with no map for an age has `null` there). It is what the Cheats page's
Teleport list asks for: the game takes it at the player's next update, in play, and goes to the map of Link's age.

The reply comes once the game took it (within `timeout`, default 10 s):

- `"sent": true` and `map`, the map it goes to. `wait map` then tells when it arrived. `accepted` is the game's
  event manager's own answer to the request: `false` was seen when the map then did not change (an arrival event of
  the previous teleport still playing).
- `refused`, with the game's reason: an event playing (a cutscene, a map change's), a map loading, another teleport
  in the last 8 s, or no map for Link's age there.
- `timeout` when the game did not take it: Link is not being updated (the title, the file select, a pause, a load).
  The request is then withdrawn, so it does not happen later behind the client's back.
- `cancelled` when a later `teleport` replaced it before the game took it (one waits at a time).

With `"wait": false` the reply is at once, with `"sent": null`. The game logs each teleport (`cheats: teleport ...`).
It changes the game as the menu's teleport does, and a save made after it keeps the new place.

```
> {"id":12,"cmd":"teleport","to":"HyruleField_Child"}
< {"id":12,"ok":true,"frame":6003,"teleport":1,"child":"HyruleField_Child","adult":"HyruleField_Adult","sent":true,
   "map":"HyruleField_Child"}
> {"id":13,"cmd":"wait","event":"map","map":"HyruleField_Child","timeout":30}
< {"id":13,"ok":true,"frame":6190,"state":"loading","waited":187,"map":"HyruleField_Child"}
```

**age** (OoT): the Debug tab's Switch age, so a script can reach the adult's maps (or the child's) without a save of
that age: `to`, `"child"` or `"adult"`, none the other age. The game takes it as it takes a teleport, at the player's
next update in play, and does it as the menu does: Link's age flipped and the current map reloaded in the other age
(the Market's, when this map has no destination). It uses the teleport's tickets, so a later teleport or age switch
replaces one not taken yet (`cancelled`), and the replies are the teleport's:

- `"age"` (the age asked for), `"sent": true` and `map`, the map it reloads; `wait loading_end` then tells when it
  arrived. Not `wait map`: in a dungeon the map has the same name for both ages (`ForestTemple`), so a wait for it is met
  before the reload starts. `accepted` as for a teleport.
- `"sent": false` when Link is that age already: nothing is done. The game checks the age asked for when it takes it, so
  an old sample of Link's age in `status` (it is read every 250 ms, and not during a load) never flips him the wrong way.
  With no `to`, the other age is decided from that sample.
- `refused`, with the game's reason (as a teleport's, and Link's age not found or not written in the game's data);
  `timeout` when the game did not take it (not in play); `unavailable` before a save is loaded (the age is not known).

With `"wait": false` the reply is at once, with `"sent": null`. The game logs it (`cheats: age toggle: ...`).

```
> {"id":14,"cmd":"age","to":"adult"}
< {"id":14,"ok":true,"frame":7310,"age":"adult","sent":true,"map":"KokiriForest_Adult","accepted":true}
```

**quit**: the reply, then the game exits as when its window is closed (the log flushed, exit code 0). The socket is
removed.

## Saves

**saves** (no opt-in needed: it lists files, reads no game memory): the folder the game's saves are mounted from
(`<data>/hookshot/save/<user id>` on Linux), read on the bridge's thread. A slot the game is writing at that moment may
show a size or time between two writes. `unavailable` until the game has opened its saves.

```
> {"id":50,"cmd":"saves"}
< {"id":50,"ok":true,"frame":900,"path":"/tmp/run/data/hookshot/save/5a3c9e1f0b7d24680000000000000001","account":true,
   "option":true,"slots":[{"slot":"03","mtime":1762900000,"newest":true,"files":[{"name":"progress.sav","size":120724,
   "mtime":1762900000},{"name":"caption.sav","size":42239,"mtime":1762900000}],"caption":{"scene":"KokiriForest",
   "map":"KokiriForest/KokiriForest_Child","thumbnail":{"format":"jpeg","offset":1396,"size":40843}}}]}
```

Slots come newest first; `newest` marks the one the title's Continue loads. Times are Unix seconds. `caption` is there
only when the slot's `caption.sav` has the layout seen in this game's saves (the file select's preview): its scene and
map names, and where its picture (a 543x306 JPEG) is in the file. Those offsets were checked on Kokiri Forest saves
only.

The client:

- `slots` prints one line a slot (last written, sizes, scene; `*` the newest). `slots --thumbs DIR` writes each slot's
  picture as `DIR/slot_NN.jpg`, read from the save folder on this PC.
- `launch --save FOLDER [--slot NN]` copies a save folder (`account.sav`, `option.sav`, `slot_NN/`) into the run's
  data folder before the game starts: `--data`, or `data/` in the run folder. With `--slot` (`03` or `slot_03`) only
  that slot is copied, so Continue loads it. The source is only read. A destination that already holds saves is
  refused. On Windows, use `--profile test` or an explicit `--user DIR`: the game supports `--user` on both platforms.
  With either, the copy goes into `DIR/save/<user id>` instead of the Linux data folder.

## Guest memory

Off unless `HOS_CONTROL_MEM` is set (apart from `HOS_CONTROL`):

| `HOS_CONTROL_MEM` | commands | aliases |
|---|---|---|
| unset / empty | none (`mem_*` reply `forbidden` with how to turn it on) | |
| `ro` | `mem_read`, `mem_read_many` | `r`, `read` |
| `rw` | the reads, and `mem_write` | `readwrite` |

Case does not matter. Any other value logs a line once and leaves memory off (it never falls back to a more permissive
level).

**This is a debugging tool, not a safe sandbox.**

- `ro` exposes all of the game's memory to a client. That includes values the runtime keeps for itself, not only the
  game's own.
- `rw` lets a client change that memory. A write can corrupt or crash the game and the runtime, not only move the game
  along, and a corrupted state can be saved to disk. **Use a throwaway save.**
- The socket is already this user's only (0600, `SO_PEERCRED`), so a client of the bridge can reach no memory that any
  other process of the same user could not already reach. The opt-in is about what the bridge itself will do, not about
  who can connect.

Addresses are guest addresses (the image is at `0x80000000`, the heap at `0x1000000000`). An address or range outside
the memory the runtime has mapped — the image, the system region in use, the heap, or an address region — is an error
(`out_of_range`), never a fault. Each request reads or writes at most 1 MB.

Reads and writes are done on the game thread, at a controller read, so a read is one frame's memory and a write lands
between frames. `mem_read_many` reads all its ranges in that one pass, so they are consistent with each other. Every
write is logged in `hookshot_log.txt` (`control: wrote N bytes to guest 0x...`).

A write to memory the GPU reads goes through the same soft write-watch the game's own CPU writes use (the page is marked
and synced into the renderer's VRAM mirror on the next frame's memory sync), so it reaches the GPU like any other CPU
write; this has not been checked against the GPU's output in a scene.

**mem_read**: `addr` and `size`: whole numbers, or strings of decimal digits or `0x` and hex digits (a string for an
address past 2^53, which a JSON number cannot hold exactly; no sign, no octal). `format` is `hex` (default) or
`base64`.

```
> {"id":30,"cmd":"mem_read","addr":2147483648,"size":8}
< {"id":30,"ok":true,"frame":5000,"format":"hex","addr":2147483648,"size":8,"data":"030000141c300000"}
```

**mem_read_many**: `ranges`, a list of `{addr, size}`, read in one pass. The reply has a `ranges` array, each with its
`addr`, `size` and `data`.

**mem_write** (`rw` only): `addr`, and the bytes as `hex` or `base64`.

```
> {"id":31,"cmd":"mem_write","addr":4300000000,"hex":"2a000000"}
< {"id":31,"ok":true,"frame":5001,"wrote":4,"addr":4300000000}
```

TODO: named game values (scene and room id, Link's position, health, rupees, ...) through `mem get NAME` and a
`symbols` command: planned, once addresses are located and verified for this build. `symbols` replies `unavailable` for
now. Slot details from caption.sav or progress.sav beyond the scene and the picture (the player's name, hearts, play
time): planned.

## The client

```
export HOS_CONTROL=$(python -I scripts/hookshot_ctl.py launch ~/Games/Hookshot --data /tmp/oot-data --headless)
# (a first start prepares the shaders before the first frame: the game reads no controller until then)
python -I scripts/hookshot_ctl.py wait frame --frame 1 --timeout 900
# (the title and the file select need A: press it until the save is loaded)
until [ "$(python -I scripts/hookshot_ctl.py --json status | jq -r .state)" = play ]; do
    python -I scripts/hookshot_ctl.py press A --frames 8; python -I scripts/hookshot_ctl.py wait present --frames 60
done
python -I scripts/hookshot_ctl.py status
python -I scripts/hookshot_ctl.py stick L 0 1 --frames 90 --exclusive
python -I scripts/hookshot_ctl.py press A --frames 8
python -I scripts/hookshot_ctl.py sequence '0:A/8,60:A/8'
python -I scripts/hookshot_ctl.py sequence '0:LUP/2000,2:A' --hos       # HOS_INPUT_LIVE's line, as it is
python -I scripts/hookshot_ctl.py shot out.png
python -I scripts/hookshot_ctl.py option get
python -I scripts/hookshot_ctl.py option set text_speed 3
python -I scripts/hookshot_ctl.py teleport                              # the destinations
python -I scripts/hookshot_ctl.py teleport 1 --arrive 30                # sent (or refused, and why), then arrived
python -I scripts/hookshot_ctl.py age adult --arrive 30                 # the Debug tab's Switch age, then reloaded
python -I scripts/hookshot_ctl.py release                               # this client's inputs and abandoned ones (--all)
python -I scripts/hookshot_ctl.py watch --events present,state --log "hid:"
python -I scripts/hookshot_ctl.py raw '{"cmd": "wait", "event": "frame", "frame": 9000}'
python -I scripts/hookshot_ctl.py mem read 0x80000000 8               # needs HOS_CONTROL_MEM=ro or rw
python -I scripts/hookshot_ctl.py mem read 0x1000000000 16 --format u32
python -I scripts/hookshot_ctl.py mem write 0x1000000000 2a000000      # needs HOS_CONTROL_MEM=rw
python -I scripts/hookshot_ctl.py quit
```

Each command prints plain lines; `--json` prints the replies. `--sock` overrides `$HOS_CONTROL`. The exit status is 0
when done, 1 for an error reply, 2 when there is no game or the usage is bad, and 3 for a timeout. `launch --data`
sets `XDG_DATA_HOME` for the run, so it plays from saves of its own. `--env NAME=VALUE` adds switches.

`launch` finds `bin/hookshot.exe` on Windows or `bin/hookshot` on Linux before the folder's top level. When `bin/`
exists but its game is missing it refuses: the root executable is the Builder launcher, not a fallback game.
`--exe FILE` selects a development executable. `--image FILE`, `--romfs DIR` and `--user DIR` pass the game's own
arguments (the image is positional), on both platforms; see [the runtime parser](../host/hos_main.c) and
[the game's command line](../readme.md#running-the-game-from-the-command-line).

### Sessions

The default profile keeps the existing window, audio, inherited environment and save-location choices. A launch now
also leaves a run folder under `work/control-runs` in the caller's folder. `--root DIR` chooses that parent; `--run DIR`
chooses one new or empty folder. A non-empty folder is never reused. The short socket folder is independent of the run
path's length. `--json launch` still returns `socket`, `pid`, `run` and `log`, now with the rest of the manifest.

```sh
python -I scripts/hookshot_ctl.py --json launch GAME_DIR --profile test --run work/my-run --ready present
python -I scripts/hookshot_ctl.py runs work/control-runs
python -I scripts/hookshot_ctl.py --run work/my-run show
python -I scripts/hookshot_ctl.py --run work/my-run press A --frames 8
python -I scripts/hookshot_ctl.py --run work/my-run shot
python -I scripts/hookshot_ctl.py --run work/my-run stop --timeout 10
```

`--run DIR` re-attaches from any later invocation, without depending on the first client's memory or environment.
It is an alternative to `--sock`. The client checks the game's pid **and birth identity** before connecting. A small
background instance of the same script remains the parent of the game, waits for it, and records its actual exit code,
even after the launching shell or a server goes away. `finish` marks an orphaned run whose game and owner are both gone;
it does not invent an exit code. `show` and `log` work after exit without connecting to the game.

`--profile test` sets `HOS_HEADLESS=1`, `HOS_AUDIO=0` and `HOS_ENV_FILE` to the run's owned empty file, and passes
`--user <run>/user`. These profile choices win over conflicting `--env` entries. The runtime reads that file
**instead of** the ambient `hookshot_env.txt` (`load_hookshot_env` in `host/hos_main.c`), not as another layer.
The profile starts fresh, or `--save FOLDER [--slot NN]` makes a plain-file copy using the existing source checks.
An ordinary symlinked parent (such as `/home` on Linux) is allowed; source/destination overlap is checked by resolved
paths. Existing `save/` and user-id entries inside the chosen user root must be real folders, not links.
It refuses `--data` and `--user`: neither can redirect a test run onto the source save. The same explicit runtime
`--user` argument makes isolation possible on Windows; the old Windows `--save` without it still refuses.
Inherited environment, the game's own options and driver caches are not isolated by this profile.

Inputs sent **by run folder** default to `exclusive:true` for test runs; `--no-exclusive` opts out. This covers each
queued input's delay and hold, not the gaps between requests or an unrelated `--sock` client. Raw JSON is sent as given.
It is not a run-long physical-device lock. No new game command or native input policy is added.

`--ready bridge|present|play` selects what launch waits for:

| Level | Required status |
|---|---|
| `bridge` (default) | A successful `status` reply |
| `present` | A successful reply with at least one game-present frame |
| `play` | A presented frame, `state=play`, `stalled=false`, and known-false `game.event` and `game.loading` |

`play` does not press through the title or load a save for you. Another `--run` invocation can send inputs while
launch waits, or the game can have an explicit input script. The state is still the bridge's draw-count heuristic;
main's `game` snapshot has no sample timestamp, so this is **not a freshness guarantee**. `--timeout` bounds readiness
after admission. A failed start or readiness timeout terminates and reaps the game it started, leaves a failed
manifest and log, and explains the last condition. It never silently abandons a timed-out game.

`stop` asks the original owner to send the bridge's `quit`, then waits for the game, supervisor and optional wrapper
to exit and close their log handles. `finish` also refuses while any of those identities is still live. A quit timeout
returns status 3 with `process_exited:false`; the game stays owned and running. Only `--terminate` allows the original
owner to terminate its child after that timeout (and kill it after five more seconds). The manifest reports the method
and actual exit code. An attaching client never sends an OS signal to a pid from disk; if the owner is gone it refuses
forced stopping. There is no command to adopt an arbitrary `--sock` game as an owned process.
The bridge's `game_quit` flushes the log and exits, **but has no save-in-progress barrier**. `save_drain` is
`unsupported`, or `unknown` for an observed orphan exit, never `clean`. A copied save is not a validated checkpoint.

For a development build, `--cwd DIR --image FILE --romfs DIR --user DIR` separates working/cache files from the image
and user files. `--cwd` needs `--image`: without an image argument the runtime changes back to the game folder.
`--wrapper '["admission-program","argument"]'` puts an optional command before the **lifetime supervisor**, not before
the short-lived launch client. It must run the remaining argv unchanged and wait for it, so its admission remains held
until the game exits. Queue time is separate from the readiness timeout. The wrapper supplies machine policy and any
wall-time bound; the client contains no GPU locks or machine-specific paths. No shell parses the JSON argv.

### Run log

```sh
python -I scripts/hookshot_ctl.py --json --run work/my-run log --lines 40
python -I scripts/hookshot_ctl.py --json --run work/my-run log --since 1234
python -I scripts/hookshot_ctl.py --json --run work/my-run log --since 1234 --expect 'some new line' --timeout 10
python -I scripts/hookshot_ctl.py --run work/my-run log --since 1234 --expect 'input.*completed' --regex
```

The cursor is a **byte offset**, returned with `text`, `start`, `more` and `truncated`; keep it before sending the input
whose log you want to observe. `--expect` requires `--since` and cannot match text before it. Matching is case-sensitive;
`--regex` uses Python regular expressions, supplied by the trusted caller. Read/output defaults to 65,536 bytes
(`--bytes`, at most 1 MiB); the tail is at most 10,000 lines. `more` means continue at `cursor`; `truncated` means the
tail's byte bound omitted older text. Very long matches beyond the bounded search window are not promised. A cursor
past EOF refuses, rather than treating an old cursor as a new file. No bridge log subscription is needed.
A read ending inside a UTF-8 character leaves its at-most-three trailing bytes for the next read; `cursor` points
before those bytes. This applies at a live log's current EOF too. Only a finished run's true EOF replaces an incomplete
character. A byte limit smaller than one encoded character cannot advance over it; use at least four bytes for UTF-8.

### Manifest version 1

`manifest.json` is UTF-8 JSON, atomically replaced under `manifest.lock`; readers hold the same lock so they cannot
prevent replacement on Windows. Consumers reject unknown schema versions. Paths use the platform's absolute spelling
(a Windows short name is not a link). Times are UTC ISO-8601; unavailable values are JSON `null`. Only explicitly set environment names
and values are recorded, not the inherited environment (explicit `--env` values can themselves be private).

| Field | Meaning |
|---|---|
| `schema_version`, `client_version` | Schema integer `1`; client session-format release string `1` |
| `run`, `game`, `executable`, `command`, `cwd` | Run folder, game folder, resolved executable, exact game argv, working folder |
| `environment`, `profile` | Effective environment additions/overrides made by the client; `default` or `test` |
| `user`, `save_source`, `slot` | Effective user folder, copied source or null, selected slot or null |
| `socket`, `socket_dir`, `log` | Unix socket or Windows pipe name, allocated socket folder or null, run log |
| `pid`, `process_start` | Game pid and birth identity: Linux boot ID/start ticks, Windows process-creation FILETIME |
| `owner`, `launcher` | Supervisor and optional wrapper process `pid`/`birth` identities; never authority to signal an arbitrary process |
| `created_at`, `started_at`, `ended_at` | Run creation, game start and final receipt times |
| `state` | `queued`, `starting`, `running`, `stopping`, `finished` or `failed` |
| `ready`, `ready_timeout`, `ready_reply` | Requested level, seconds after admission, observed successful status when available |
| `wrapper` | Optional lifetime admission argv prefix |
| `exit_code`, `process_exited`, `stop_method`, `save_drain` | Child receipt and honest shutdown result; a signal is a negative exit code on Linux |
| `artifacts` | Acknowledged shots: `kind`, `path`, game `frame`, renderer `present`, `width`, `height` |
| `error`, `error_code` | Failure explanation and client exit category, when available |
| `stop_request`, `stop_result` | Owner mailbox and last reply, matched by `id`; reply has `method`, `process_exited`, `forced`, `save_drain`, and any error |

**Trust:** the client starts programs and writes files with the user's authority. A run folder is not a sandbox.
Do not load manifests, wrapper commands or executable paths from an untrusted caller. Use an owned game copy if options
or cache writes must not affect ordinary play.

The standard-library API for a future thin server is `start_session(...)`, `list_runs(root)`, `Session.attach(folder)`,
`Session.info()`, `Session.request(cmd, timeout=None, **args)`, `Session.log(...)`, `Session.expect_log(...)`,
`Session.stop(timeout=10, terminate=False)` and `Session.finish()`. `Session.connect()` returns a separate `Bridge`
for streaming; serialize requests on a connection, and keep subscriptions on their own connection.
`start_session` and `Session.attach` return a session handle; its methods return ordinary dictionaries, and `list_runs`
returns a list of manifest dictionaries. Filesystem, JSON and argument failures at these API boundaries raise `Failed`
with `code=2`; a game refusal has code 1 and a timeout code 3. A timed-out graceful stop instead returns the manifest
with its explicit `process_exited:false` result, so callers can decide whether to request force.


### Scenarios

`scripts/hookshot_scenario.py` runs a versioned JSON scenario directly through the session layer, with the standard
library alone. It adapts Nicholas Gower's runner from [#1025](https://github.com/NotRexed/Hookshot/pull/1025), with his
original files retained in the import history. This is part 3 of 3 of that takeover, stacked on the session layer
[#2904](https://github.com/NotRexed/Hookshot/pull/2904); it neither imports nor starts an MCP server.

```sh
python -I scripts/hookshot_scenario.py check.json --game GAME_DIR --out work/check --json
```

One small authored example (no save or game data):

```json
{
  "version": 1,
  "name": "capture-check",
  "launch": {"ready": "present"},
  "steps": [
    {"press": {"buttons": ["A"], "hold_ms": 200}},
    {"wait": {"frames": 60}},
    {"screenshot": {"path": "next.png"}},
    {"expect_log": {"pattern": "(the control bridge)", "timeout_ms": 5000}}
  ]
}
```

`launch` defaults to the `test` profile and `ready=bridge`. Its explicit `ready=bridge|present|play` is always waited
for, not just recorded. It accepts the session's `exe`, `image`, `romfs`, `cwd`, `user`, `data`, `save`, `slot`, `profile`,
`headless`, `env`, `ready` and `timeout` fields; readiness `timeout` and `stop_timeout` are **seconds**.
The command line's `--save DIR --slot NN` override the copy source/slot. `--wrapper JSON` is the session's lifetime
admission argv. No process scan or `force` bypass is added.

To use an existing run, `launch` instead holds `{"attach":"RUN_DIR","ready":"bridge"}` (optionally `timeout`).
The runner waits for that level too, but never stops an attached game, on success or failure. Save/wrapper overrides
cannot be applied to an attachment. Empty/whitespace-only paths, an attachment that resolves to the output folder,
and attachment mixed with owning-only launch fields are usage errors before a process or output folder is created.

Every step holds exactly one verb and an object. The complete file is checked before launching; errors name the
one-based step index (`step 0` is the file/schema/launch). Unknown fields are errors, not silently ignored.

| Verb | Fields / meaning |
|---|---|
| `press` | `buttons` string or list; `hold_ms` (200), `at_ms` (0), `settle_ms` (100), `exclusive`, `wait` (true), `timeout_ms` |
| `stick` | `which` (`left`/`right` or `L`/`R`), `x`/`y` in -1..1, optional buttons; timing fields as press |
| `sequence` | Non-empty `steps`: each has `at_ms`, `hold_ms`, buttons and/or `stick` with x/y; outer settle/wait/exclusive/timeout as above |
| `wait` | Exactly one of `frames` (additional presents), `state`, `map`, or wall `ms`; optional bridge `timeout_ms` |
| `screenshot` | Optional relative `path` under the output's `shots/`, `after` frame, `timeout_ms`; the acknowledged artifact is retained |
| `settings` | Optional setting `name`; otherwise the bridge's ordinary option list, not a cheat catalogue |
| `set_setting` | `name`, integer/boolean `value`; uses the real bridge option setter |
| `teleport` | `to` map or index; optional `timeout_ms`, `arrive_ms` to wait for the target map after it is taken |
| `age` | Optional `to` child/adult (none toggles); `arrive_ms` waits for loading to end, not an unchanged map name |
| `cheat` | Recognized for migration: only `name=teleport` or `name=age`; `to` or legacy `value` (age: 0 child/1 adult). Every other name fails clearly as “not on the bridge” |
| `expect_log` | `pattern`, optional `regex`, `timeout_ms` (5000), `since` (`previous` by default, or `start`) |

Input fields ending in `_ms` really are **milliseconds**. They become integer game frames at 60 Hz, nearest frame
with halves up, a positive hold at least one frame: `at_ms=1000, hold_ms=100` means offset 60 and hold 6.
No seconds/ms script string is assembled. `settle_ms` and `wait.ms` are wall-clock waits that check the process remains
live; they are not frame waits. The native fixture tests the actual controller frames, not the encoded request text.

By default, `expect_log` begins at the cursor taken **before the immediately preceding step**. That catches a shot
or input's synchronous log while refusing an old startup line; as the first step it starts after readiness.
`since:"start"` explicitly searches the whole run. Matching is case-sensitive, with the session layer's bounds and
UTF-8 cursor rules. A step inserted between an action and its expectation changes that default cursor.

Output defaults to a unique folder under `work/control-scenarios`; an explicit `--out` must be new or empty.
The output root is resolved once; screenshot requests use that absolute root and artifacts retain the bridge's returned
absolute path, so a Windows short-name input may be recorded with its long spelling.
`transcript.json` is the plain result, `transcript.jsonl`/`transcript.txt` retain events, and the owned session keeps
its manifest, log and default shots under `run/`. A named shot is under the output's `shots/`. The first failed step
stops the sequence; these files remain. Owned runs are always quit and waited for in `finally`, with termination
allowed after the configured stop timeout; this still does **not** promise save-draining shutdown.

Exit status: **0** pass, **1** a failed step, **2** usage, **3** timeout. `--json` prints one complete result.
For a server, call `run_scenario(scenario, *, game_dir, out=None, wrapper=None, save=None, slot=None, on_event=None)`.
It returns `{version,name,out,run,owned,ok,exit_code,failed_step,error,artifacts,transcript}`; `on_event` receives each
plain event dictionary. Invalid input raises the session layer's `Failed(2)` before launch; ordinary step failures
are in the returned transcript.

Version 1 deliberately removes the original runner's `--build` and MCP-specific launch fields: **building is a
separate command**, with its own admission and evidence, never a scenario side effect.

## Known limits

- **Windows** is built and its pipe tested in CI only (the windows-2022 runner): the bridge inside a Windows game build,
  `launch` on Windows, and refusing remote pipe clients (`PIPE_REJECT_REMOTE_CLIENTS`, which needs a second machine)
  are not tested.
- **The state is a guess** from the draws a frame (above). A scene with unusual counts can be misread; for OoT, `game`'s
  `event` and `loading` and `map` are the game's own.
- **A teleport the game sent but that did not move Link**: `accepted` (the event manager's answer) is reported, not turned into an error: `false` was seen once, when the map did not change, but that it always means no move is not established. Otherwise `wait map` times out.
- **The first `option_get` or `option_set`** after start can wait, on the bridge's thread, for the start-up check of
  the upscalers (NVIDIA's NGX start: about 3 s). The game does not wait for it; other replies to every client wait
  that long.
- **Named game values** (`mem get NAME`, `symbols`) are not served yet (see Guest memory).
- `HOS_CONTROL_MEM=rw` writes were checked against the game's memory (written, read back, put back), not against the
  picture the GPU draws from a written value.

## Tests

`tests/test_control.py` builds the bridge with a fake game and drives it over its real transport: the Unix socket on
Linux, the named pipe on Windows (the CI's windows-2022 runner). On Windows it also checks the pipe's access control:
another first instance of the name and a second bridge on it are refused, and the DACL, read back through a client's
handle, is protected with a single ACE for the current user. `tests/test_hookshot_ctl.py` drives the client against a
fake bridge on the same transports.

What is not tested is under Known limits.
