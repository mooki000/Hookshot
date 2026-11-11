"""HOS_OOT_STATE_FILE: a line of the game's state for scripted runs, written from the update of Link.

host/oot_state_tap.c reads Link's position from the actor hos_oot_lift_player is given, the map, loading, age, life,
rupees, the A prompt, the music and watched flags from GameDataMgr's typed tables, the camera's yaw from the gameplay
view and whether an event plays from EventMgr; host/hos_main.c calls it under the debug tools' lock. Here the whole file
runs against a guest memory of its own (a GameDataMgr with every container, two flags in one table slot, an EventMgr,
a camera, an actor), and the lines it writes are read back as JSON: one every HOS_OOT_STATE_EVERY presents, the
values, the watched flags by name and by hash, the changed flags, null for what cannot be read. The names' hashes are
checked against MurmurHash3 here and against the constants host/hos_main.c already uses.

python -I -B tests/test_oot_state_tap.py [--cc gcc]
"""
import argparse
import json
import os
import re
import subprocess
import sys
import tempfile
from pathlib import Path

RUN_ALL_ARGS = ["--cc", "gcc"]

ROOT = Path(__file__).resolve().parents[1]

FIXTURE_HEAD = r'''#include <math.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "FAIL:%d %s\n", __LINE__, #x); exit(1); } } while (0)
typedef long long LONG64;
volatile LONG64 hos_present_count;
static LONG64 InterlockedCompareExchange64(volatile LONG64 *p, LONG64 x, LONG64 c) { (void)x; (void)c; return *p; }
static int64_t hos_now_ns(void) { return 5000000000LL + hos_present_count * 16666667LL; }
static void hos_log(const char *fmt, ...) {
    va_list a;
    va_start(a, fmt);
    vfprintf(stderr, fmt, a);
    va_end(a);
    fputc('\n', stderr);
}
/* (the guest: main's data around the singletons' slots, and a heap) */
#define SLOTS 0x84c00000ull
#define HEAP 0x10000000ull
static unsigned char slots[0x100000], heap[0x100000];
static void *hos_guest_range(uint64_t a, uint64_t n) {
    if (a >= SLOTS && n <= sizeof slots && a - SLOTS <= sizeof slots - n) return slots + (a - SLOTS);
    if (a >= HEAP && n <= sizeof heap && a - HEAP <= sizeof heap - n) return heap + (a - HEAP);
    return NULL;
}
'''

FIXTURE_MAIN = r'''
static void w64(uint64_t a, uint64_t v) { memcpy(hos_guest_range(a, 8), &v, 8); }
static void w32(uint64_t a, uint32_t v) { memcpy(hos_guest_range(a, 4), &v, 4); }
static void w8(uint64_t a, uint8_t v) { memcpy(hos_guest_range(a, 1), &v, 1); }
static void wf(uint64_t a, float v) { memcpy(hos_guest_range(a, 4), &v, 4); }
static void wd(uint64_t a, double v) { memcpy(hos_guest_range(a, 8), &v, 8); }
static uint64_t r64(uint64_t a) { uint64_t v; memcpy(&v, hos_guest_range(a, 8), 8); return v; }
static uint32_t r32(uint64_t a) { uint32_t v; memcpy(&v, hos_guest_range(a, 4), 4); return v; }
static uint64_t brk = HEAP + 0x1000;
static uint64_t alloc(uint64_t n) { const uint64_t a = brk; brk += (n + 15) & ~15ull; CHECK(brk < HEAP + sizeof heap); return a; }
static uint64_t text(const char *s) { const uint64_t a = alloc(strlen(s) + 1); memcpy(hos_guest_range(a, strlen(s) + 1), s, strlen(s) + 1); return a; }
static uint32_t H(const char *s) { return state_murmur(s, strlen(s)); }

/* a container of GameDataMgr: entries of `stride` bytes, a table of `size` {hash, entry} slots */
static void container(uint64_t mgr, int kind, uint32_t size) {
    const uint64_t b = mgr + state_kind[kind].at;
    w64(b + 16, alloc((uint64_t)state_kind[kind].stride * size));
    w64(b + 24, alloc(8ull * size));
    w32(b + 36, size);
    w32(b + 8, 0);
}
/* a flag added to it (linear probing, as the game's table): its entry's value field */
static uint64_t flag(uint64_t mgr, int kind, uint32_t hash) {
    const uint64_t b = mgr + state_kind[kind].at, table = r64(b + 24);
    const uint32_t size = r32(b + 36), count = r32(b + 8);
    uint32_t slot = hash % size;
    while (r32(table + slot * 8ull)) slot = (slot + 1) % size;
    w32(table + slot * 8ull, hash);
    w32(table + slot * 8ull + 4, count);
    w32(b + 8, count + 1);
    return r64(b + 16) + (uint64_t)count * state_kind[kind].stride + state_kind[kind].value;
}

int main(void) {
    static const char *const names[] = {"Sequence_CurrentBanc", "PlayerStatus.Life", "abc", "ab", "a", "", "Test.Flag"};
    for (unsigned i = 0; i < sizeof names / sizeof names[0]; i++) printf("murmur %s=%08x\n", names[i], H(names[i]));

    const uint64_t mgr = alloc(0x800), holder = alloc(16);
    w64(STATE_GAMEDATA_SLOT, holder), w64(holder, mgr);
    for (int k = 0; k < KINDS; k++) container(mgr, k, 16);
    w64(flag(mgr, KIND_TEXT, STATE_MAP), text("KokiriForest_Child"));
    w64(flag(mgr, KIND_TEXT, STATE_PROMPT), text("Open"));
    w64(flag(mgr, KIND_TEXT, STATE_MUSIC), text("BGM_\"House\"\x01"));
    w8(flag(mgr, KIND_BOOL, STATE_LOADING), 0);
    w8(flag(mgr, KIND_BOOL, STATE_CHILD), 1);
    const uint64_t test_flag = flag(mgr, KIND_BOOL, H("Test.Flag"));
    w8(test_flag, 1);
    /* two flags in one slot of a 16-slot table: the second is found past the first */
    const uint32_t first = 0x00000100u, second = 0x00000110u;
    w8(flag(mgr, KIND_BOOL, first), 0);
    w8(flag(mgr, KIND_BOOL, second), 1);
    w32(flag(mgr, KIND_INT, STATE_LIFE), 12);
    w32(flag(mgr, KIND_INT, STATE_MAX_LIFE), 12);
    const uint64_t rupees = flag(mgr, KIND_INT, STATE_RUPEES);
    w32(rupees, 5);
    w32(flag(mgr, KIND_INT, H("Test.Negative")), (uint32_t)-3);
    wf(flag(mgr, KIND_FLOAT, STATE_ROT), -71.43f);
    w32(flag(mgr, KIND_ENUM, H("Step.Test")), 0xdeadbeefu);
    w32(flag(mgr, KIND_UINT, H("Test.Unsigned")), 3000000000u);

    const uint64_t events = alloc(128), eholder = alloc(16);
    w64(STATE_EVENTMGR_SLOT, eholder), w64(eholder, events), w64(events + 104, 1);

    const uint64_t cholder = alloc(16), cmgr = alloc(64), cams = alloc(16), cam = alloc(128), view = alloc(160);
    w64(STATE_CAMERAS_SLOT, cholder), w64(cholder, cmgr), w32(cmgr + 32, 1), w64(cmgr + 40, cams), w64(cams, cam);
    w64(cam + 120, view), w64(view, STATE_VIEW_VTABLE);
    wd(view + 96, 0.0), wd(view + 104, 5.0), wd(view + 112, 0.0);    /* eye */
    wd(view + 120, 1.0), wd(view + 128, 4.0), wd(view + 136, 1.0);   /* target: 45 degrees, from +z toward +x */

    const uint64_t actor = alloc(0x400);
    wf(actor + STATE_LINK_POS, -20.1f), wf(actor + STATE_LINK_POS + 4, 0.0f), wf(actor + STATE_LINK_POS + 8, 6.8f);

    hos_present_count = 100;
    hos_oot_state_tap(actor);   /* a line */
    hos_present_count = 103;
    hos_oot_state_tap(actor);   /* none: 3 presents since */
    hos_present_count = 106;
    w8(test_flag, 0), w32(rupees, 7);
    hos_oot_state_tap(actor);   /* a line, with the two changes */
    hos_present_count = 112;
    w64(view, 0);                                                   /* not a view: no camera */
    w64(r64(mgr + state_kind[KIND_TEXT].at + 16) + state_kind[KIND_TEXT].value, 0x7000000000ull);   /* the map's text: no guest memory */
    w64(events + 104, 0);
    hos_oot_state_tap(actor);
    hos_present_count = 118;
    w64(STATE_GAMEDATA_SLOT, 0);                                    /* no GameDataMgr: no flags */
    hos_oot_state_tap(actor);
    return 0;
}
'''


def murmur(s):
    """MurmurHash3 x86 32, seed 0"""
    data, h = s.encode(), 0
    n = len(data) // 4 * 4
    for i in range(0, n, 4):
        k = int.from_bytes(data[i:i + 4], "little")
        k = (k * 0xcc9e2d51) & 0xffffffff
        k = ((k << 15) | (k >> 17)) & 0xffffffff
        k = (k * 0x1b873593) & 0xffffffff
        h ^= k
        h = ((h << 13) | (h >> 19)) & 0xffffffff
        h = (h * 5 + 0xe6546b64) & 0xffffffff
    k = int.from_bytes(data[n:], "little") if len(data) > n else 0
    if len(data) > n:
        k = (k * 0xcc9e2d51) & 0xffffffff
        k = ((k << 15) | (k >> 17)) & 0xffffffff
        k = (k * 0x1b873593) & 0xffffffff
        h ^= k
    h ^= len(data)
    h ^= h >> 16
    h = (h * 0x85ebca6b) & 0xffffffff
    h ^= h >> 13
    h = (h * 0xc2b2ae35) & 0xffffffff
    return h ^ (h >> 16)


def check(cond, what):
    if not cond:
        raise SystemExit("FAIL: " + what)


def block(text, head):
    """The function that starts with head, to its closing brace at the start of a line"""
    i = text.index(head)
    return text[i:text.index("\n}\n", i) + 3]


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--cc", default="gcc")
    a = p.parse_args()

    tap = (ROOT / "host" / "oot_state_tap.c").read_text(encoding="utf-8")
    main_c = (ROOT / "host" / "hos_main.c").read_text(encoding="utf-8")
    patches = (ROOT / "nx2" / "oot_patches.py").read_text(encoding="utf-8")
    docs = (ROOT / "docs" / "development.md").read_text(encoding="utf-8")

    # (wiring) each update of Link, under the debug tools' lock; the update is patched into the game
    lift = block(main_c, "void hos_oot_lift_player(Ctx *c, uint64_t actor) {")
    took, call, gave = (lift.find("TryAcquireSRWLockExclusive(&cheats_lock)"), lift.find("hos_oot_state_tap(actor);"),
                        lift.find("ReleaseSRWLockExclusive(&cheats_lock)"))
    check(0 <= took < call < gave, "hos_oot_lift_player does not call hos_oot_state_tap under its lock")
    check("hos_oot_lift_player(c, c->x[19]);" in patches, "Link's update does not call hos_oot_lift_player")
    check("void hos_oot_state_tap(uint64_t actor) {" in tap, "the tap's hook has another signature")
    for name in ("HOS_OOT_STATE_FILE", "HOS_OOT_STATE_EVERY", "HOS_OOT_STATE_FLAGS", "HOS_OOT_STATE_CHANGES"):
        check('getenv("%s")' % name in tap, "the tap does not read " + name)
        check("`%s" % name in docs, "docs/development.md does not describe " + name)

    # (addresses) the same singletons, view and position the debug tools use
    defs = dict(re.findall(r"#define (STATE_\w+) (0x[0-9a-fA-F]+)", tap))
    check(re.search(r"#define OOT_GAMEDATA_SLOT %sULL\b" % defs["STATE_GAMEDATA_SLOT"], main_c) is not None,
          "GameDataMgr's slot differs from hos_main.c's")
    check(re.search(r"#define OOT_EVENTMGR_SLOT %sULL\b" % defs["STATE_EVENTMGR_SLOT"], main_c) is not None,
          "EventMgr's slot differs from hos_main.c's")
    check("recomp_load64(c, %sULL)" % defs["STATE_CAMERAS_SLOT"] in block(main_c, "static uint64_t oot_gameplay_view("),
          "the camera manager's slot differs from oot_gameplay_view's")
    check("recomp_load64(c, view) != %sULL" % defs["STATE_VIEW_VTABLE"] in main_c, "the view's vtable differs from hos_oot_free_view's")
    check(int(defs["STATE_LINK_POS"], 16) == 796 and "actor + 796" in main_c, "Link's position is not where the mark reads it")

    # (hashes) the named flags are the MurmurHash3 of their names; the Sequence ones as hos_main.c has them
    named = {"STATE_MAP": "Sequence_CurrentBanc", "STATE_LOADING": "Sequence_IsLoadingBanc", "STATE_CHILD": "Sequence_IsChildhood",
             "STATE_ROT": "Player_Rot_For_Map", "STATE_LIFE": "PlayerStatus.Life", "STATE_MAX_LIFE": "PlayerStatus.MaxLife",
             "STATE_RUPEES": "PlayerStatus.CurrentRupee"}
    for macro, name in named.items():
        value = int(defs[macro], 16)
        check(value == murmur(name), "%s is not the hash of %s" % (macro, name))
        if name.startswith("Sequence_"):
            check("0x%08xu" % value in main_c, "hos_main.c has another hash for " + name)

    body = "".join(line for line in tap.splitlines(keepends=True) if not line.startswith("#include"))
    with tempfile.TemporaryDirectory(prefix="state-tap-") as tmp:
        src, exe, out = Path(tmp) / "fixture.c", Path(tmp) / "fixture", Path(tmp) / "state.jsonl"
        src.write_text(FIXTURE_HEAD + body + FIXTURE_MAIN, encoding="utf-8")
        subprocess.run([a.cc, "-std=c11", "-O2", "-Wall", "-Wextra", "-Werror", str(src), "-o", str(exe), "-lm"], check=True)
        env = dict(os.environ, HOS_OOT_STATE_FILE=str(out), HOS_OOT_STATE_EVERY="6", HOS_OOT_STATE_CHANGES="1",
                   HOS_OOT_STATE_FLAGS="Test.Flag,0x00000110,Bad-Name,Test.Negative,Step.Test,Missing.Flag,"
                                       "Player_Rot_For_Map,Test.Unsigned,Sequence_CurrentBanc")
        run = subprocess.run([str(exe)], check=True, capture_output=True, text=True, env=env)
        for name, value in re.findall(r"murmur (.*)=([0-9a-f]{8})", run.stdout):
            check(int(value, 16) == murmur(name), "the C MurmurHash3 of %r is %s" % (name, value))
        check('flag "Bad-Name" left out' in run.stderr, "a malformed flag name was not refused")
        lines = [json.loads(x) for x in out.read_text(encoding="utf-8").splitlines()]

    check([x["frame"] for x in lines] == [100, 106, 112, 118], "lines at presents %s, not every 6" % [x["frame"] for x in lines])
    one, two, three, four = lines
    want = {"map": "KokiriForest_Child", "pos": [-20.1, 0.0, 6.8], "facing": 108.6, "camera": 45.0, "event": 1, "loading": 0,
            "child": 1, "life": 12, "max_life": 12, "rupees": 5, "prompt": "Open", "music": 'BGM_"House"\x01'}
    for k, v in want.items():
        check(one.get(k) == v, "%s is %r, not %r" % (k, one.get(k), v))
    check(abs(one["t"] - 5 - 100 / 60) < 0.01, "t is not the host's clock in seconds")
    check(one["flags"] == {"Test.Flag": 1, "0x00000110": 1, "Test.Negative": -3, "Step.Test": "#deadbeef", "Missing.Flag": None,
                           "Player_Rot_For_Map": -71.43, "Test.Unsigned": 3000000000, "Sequence_CurrentBanc": "KokiriForest_Child"},
          "watched flags: %r" % one["flags"])
    check(one["changed"] == {}, "a first look reported changes: %r" % one["changed"])
    check(two["flags"]["Test.Flag"] == 0 and two["rupees"] == 7, "the second line does not show the new values")
    check(two["changed"] == {"0x%08x" % murmur("Test.Flag"): 0, "0x%08x" % murmur("PlayerStatus.CurrentRupee"): 7},
          "changed: %r" % two["changed"])
    check(three["map"] is None and three["camera"] is None and three["event"] == 0, "a text or view out of reach was read: %r" % three)
    check(three["life"] == 12 and three["prompt"] == "Open" and three["changed"] == {}, "the third line lost what it could read")
    for k in ("map", "facing", "loading", "child", "life", "max_life", "rupees", "prompt", "music"):
        check(four[k] is None, "%s is %r without a GameDataMgr, not null" % (k, four[k]))
    check(four["pos"] == [-20.1, 0.0, 6.8] and set(four["flags"].values()) == {None}, "the fourth line: %r" % four)
    print("PASS: the state line for scripted runs reads Link, the map, the camera, the event and the flags")


if __name__ == "__main__":
    main()
