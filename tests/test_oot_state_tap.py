"""OoT snapshots through the real control bridge, without a game or renderer.

The complete sampler runs against bounded, synthetic guest memory and links with host/control.c. Its Link updates
and presents are stepped separately over stdin; all status/subscription assertions use the real Unix socket or
Windows named pipe. Direct helper checks retain the Murmur reference and both table-size boundaries. No flag list,
map data or code is taken from a dump. --source-root selects production files for a negative checkout run.
--mutate injects one named regression into temporary C copies; it must fail, and never edits the checkout. The
drop-* modes cover each core field, and the other modes isolate cadence, baselines, bounds, hashing and lifecycle.

python -I -B tests/test_oot_state_tap.py [--cc gcc] [--source-root OTHER_CHECKOUT] [--mutate MODE]
"""
import argparse
import importlib.util
import json
import os
import queue
import re
import subprocess
import tempfile
import threading
import time
from contextlib import ExitStack
from pathlib import Path

RUN_ALL_ARGS = ["--cc", "gcc"]
ROOT = Path(__file__).resolve().parents[1]

FIXTURE_HEAD = r'''#include "control.h"
#include <math.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <pthread.h>
typedef long long LONG64;
typedef pthread_mutex_t SRWLOCK;
#define SRWLOCK_INIT PTHREAD_MUTEX_INITIALIZER
#define AcquireSRWLockExclusive(p) ((void)pthread_mutex_lock(p))
#define ReleaseSRWLockExclusive(p) ((void)pthread_mutex_unlock(p))
#define AcquireSRWLockShared(p) AcquireSRWLockExclusive(p)
#define ReleaseSRWLockShared(p) ReleaseSRWLockExclusive(p)
static LONG64 InterlockedCompareExchange64(volatile LONG64 *p, LONG64 x, LONG64 c) {
    __atomic_compare_exchange_n(p, &c, x, 0, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
    return c;
}
#endif
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "FAIL:%d %s\n", __LINE__, #x); exit(1); } } while (0)
volatile LONG64 hos_present_count;
static int64_t hos_now_ns(void) { return 5000000000LL + __atomic_load_n(&hos_present_count, __ATOMIC_ACQUIRE) * 16666667LL; }
/* main's singleton slots and a separate, bounded fake heap; every out-of-range read fails */
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
static void w64(uint64_t a, uint64_t v) { CHECK(hos_guest_range(a, 8)); memcpy(hos_guest_range(a, 8), &v, 8); }
static void w32(uint64_t a, uint32_t v) { CHECK(hos_guest_range(a, 4)); memcpy(hos_guest_range(a, 4), &v, 4); }
static void w8(uint64_t a, uint8_t v) { CHECK(hos_guest_range(a, 1)); memcpy(hos_guest_range(a, 1), &v, 1); }
static void wf(uint64_t a, float v) { CHECK(hos_guest_range(a, 4)); memcpy(hos_guest_range(a, 4), &v, 4); }
static void wd(uint64_t a, double v) { CHECK(hos_guest_range(a, 8)); memcpy(hos_guest_range(a, 8), &v, 8); }
static uint64_t r64(uint64_t a) { uint64_t v; CHECK(hos_guest_range(a, 8)); memcpy(&v, hos_guest_range(a, 8), 8); return v; }
static uint32_t r32(uint64_t a) { uint32_t v; CHECK(hos_guest_range(a, 4)); memcpy(&v, hos_guest_range(a, 4), 4); return v; }
static uint64_t heap_next = HEAP + 0x1000;
static uint64_t alloc(uint64_t n) {
    const uint64_t a = heap_next;
    heap_next += (n + 15) & ~15ull;
    CHECK(heap_next < HEAP + sizeof heap);
    return a;
}
static uint64_t text(const char *s) {
    CHECK(strlen(s) <= 63);
    const uint64_t a = alloc(64);
    memcpy(hos_guest_range(a, 64), s, strlen(s) + 1);
    return a;
}
static uint32_t H(const char *s) { return state_murmur(s, strlen(s)); }
/* Entries and linear-probed {hash, entry} slots, as in the old file-tap fixture. Twice the table storage permits an
   in-place resize case without changing its address. */
static void container(uint64_t mgr, int kind, uint32_t size) {
    const uint64_t b = mgr + state_kind[kind].at;
    w64(b + 16, alloc((uint64_t)state_kind[kind].stride * size));
    w64(b + 24, alloc(16ull * size));
    w32(b + 36, size);
    w32(b + 8, 0);
}
static uint64_t flag(uint64_t mgr, int kind, uint32_t hash) {
    const uint64_t b = mgr + state_kind[kind].at, table = r64(b + 24);
    const uint32_t size = r32(b + 36), count = r32(b + 8);
    CHECK(hash && count < size);
    uint32_t slot = hash % size;
    while (r32(table + slot * 8ull)) slot = (slot + 1) % size;
    w32(table + slot * 8ull, hash);
    w32(table + slot * 8ull + 4, count);
    w32(b + 8, count + 1);
    return r64(b + 16) + (uint64_t)count * state_kind[kind].stride + state_kind[kind].value;
}
static void table_caps(void) {
    const uint64_t mgr = alloc(0x800);
    container(mgr, KIND_UINT, 16);
    const uint64_t value = flag(mgr, KIND_UINT, 1), box = mgr + state_kind[KIND_UINT].at;
    w32(value, 42);
    w32(box + 36, 1u << 22);
    CHECK(state_entry(mgr, KIND_UINT, 1) == value - state_kind[KIND_UINT].value);
    w32(box + 36, (1u << 22) + 1);
    CHECK(state_entry(mgr, KIND_UINT, 1) == 0);
    StateSeen seen[STATE_TRACKED] = {0};
    StateLine line = {0};
    state_put_changes(&line, mgr, seen);
    for (int i = 0; i < STATE_TRACKED; i++) CHECK(!seen[i].size && !seen[i].key && !seen[i].value);
    w32(box + 36, 1u << 22);
    line.n = 0;
    state_put_changes(&line, mgr, seen);
    CHECK(!strcmp(line.text, ",\"changed\":{}"));
    for (int i = 0; i < STATE_TRACKED; i++) {
        CHECK(seen[i].size == (state_tracked[i] == KIND_UINT ? 1u << 22 : 0));
        free(seen[i].key);
        free(seen[i].value);
    }
}

static uint64_t mgr, holder, events, eholder, view, actor, map_value, map_text, prompt, music;
static uint64_t test_flag, rupees, child, loading, rotation, bursts[40], escaped_text, resize_guard;
static SRWLOCK map_lock = SRWLOCK_INIT;
static char last_map[64];
static int poll_held;
static unsigned watch_mask;
static int64_t fake_frame(void) { return __atomic_load_n(&hos_present_count, __ATOMIC_ACQUIRE); }
static int fake_map(char *out, size_t cap) {
    AcquireSRWLockShared(&map_lock);
    const int have = last_map[0] != 0;
    if (have) snprintf(out, cap, "%s", last_map);
    ReleaseSRWLockShared(&map_lock);
    return have;
}
static int fake_poll(int slot, char *out, size_t cap) {
    return __atomic_load_n(&poll_held, __ATOMIC_ACQUIRE) ? 0 : hos_oot_poll(slot, out, cap);
}
static void fake_watch(int slot, const HosControlOotWatch *watch) {
    hos_oot_watch(slot, watch);
    if (watch) __atomic_fetch_or(&watch_mask, 1u << slot, __ATOMIC_RELEASE);
    else {
        char pending[HOS_CONTROL_OOT_JSON];
        CHECK(!hos_oot_poll(slot, pending, sizeof pending));
        __atomic_fetch_and(&watch_mask, ~(1u << slot), __ATOMIC_RELEASE);
    }
}
static void fake_log(const char *line) { fprintf(stderr, "%s\n", line); }
static HosControlGame game = {
    .frame = fake_frame, .map = fake_map, .oot_sample = hos_oot_sample,
    .oot_watch = fake_watch, .oot_poll = fake_poll, .log = fake_log, .version = "state-fixture"
};
static void setup(void) {
    mgr = alloc(0x800), holder = alloc(16);
    w64(STATE_GAMEDATA_SLOT, holder), w64(holder, mgr);
    for (int k = 0; k < KINDS; k++) container(mgr, k, k == KIND_TEXT || k == KIND_INT ? 128 : 16);
    map_value = flag(mgr, KIND_TEXT, STATE_MAP), map_text = text("KokiriForest_Child");
    w64(map_value, map_text);
    prompt = flag(mgr, KIND_TEXT, STATE_PROMPT), music = flag(mgr, KIND_TEXT, STATE_MUSIC);
    w64(prompt, text("Open")), w64(music, text("BGM_\"House\"\x01"));
    loading = flag(mgr, KIND_BOOL, STATE_LOADING), child = flag(mgr, KIND_BOOL, STATE_CHILD);
    w8(loading, 0), w8(child, 1);
    test_flag = flag(mgr, KIND_BOOL, H("Test.Flag"));
    w8(test_flag, 1);
    /* The second hash collides with the first at slot zero of the 16-slot bool table. */
    w8(flag(mgr, KIND_BOOL, 0x00000100u), 0);
    w8(flag(mgr, KIND_BOOL, 0x00000110u), 1);
    resize_guard = flag(mgr, KIND_INT, 0x60000070u);   /* slot 112 before and after the 128 -> 256 resize */
    w32(flag(mgr, KIND_INT, STATE_LIFE), 12), w32(flag(mgr, KIND_INT, STATE_MAX_LIFE), 12);
    rupees = flag(mgr, KIND_INT, STATE_RUPEES);
    w32(rupees, 5);
    w32(flag(mgr, KIND_INT, H("Test.Negative")), (uint32_t)-3);
    rotation = flag(mgr, KIND_FLOAT, STATE_ROT);
    wf(rotation, -71.43f);
    w32(flag(mgr, KIND_ENUM, H("Step.Test")), 0xdeadbeefu);
    w32(flag(mgr, KIND_UINT, H("Test.Unsigned")), 3000000000u);
    for (int i = 0; i < 40; i++) bursts[i] = flag(mgr, KIND_INT, 0x60000000u + (unsigned)i);
    char escaped[64];
    memset(escaped, 0x80, 63), escaped[0] = 1, escaped[63] = 0;
    escaped_text = text(escaped);
    for (int i = 0; i < HOS_CONTROL_OOT_FLAGS; i++) {
        char name[48];
        snprintf(name, sizeof name, "Test.Escaped.%034u", (unsigned)i);
        CHECK(strlen(name) == 47);
        w64(flag(mgr, KIND_TEXT, H(name)), escaped_text);
    }
    events = alloc(128), eholder = alloc(16);
    w64(STATE_EVENTMGR_SLOT, eholder), w64(eholder, events), w64(events + 104, 1);
    const uint64_t cholder = alloc(16), cmgr = alloc(64), cams = alloc(16), cam = alloc(128);
    view = alloc(160);
    w64(STATE_CAMERAS_SLOT, cholder), w64(cholder, cmgr), w32(cmgr + 32, 1), w64(cmgr + 40, cams), w64(cams, cam);
    w64(cam + 120, view), w64(view, STATE_VIEW_VTABLE);
    wd(view + 96, 0.0), wd(view + 104, 5.0), wd(view + 112, 0.0);
    wd(view + 120, 1.0), wd(view + 128, 4.0), wd(view + 136, 1.0);
    actor = alloc(0x400);
    wf(actor + STATE_LINK_POS, -20.1f), wf(actor + STATE_LINK_POS + 4, 0.0f), wf(actor + STATE_LINK_POS + 8, 6.8f);
}
static void resize_int_table(void) {
    const uint64_t box = mgr + state_kind[KIND_INT].at, table = r64(box + 24);
    uint32_t pairs[128][2];
    CHECK(r32(box + 36) == 128);
    /* Other flags may rehash to different slots, which by itself suppresses their changes even if the resize
       baseline is broken. Change one whose key stays in the same slot to make that regression observable. */
    CHECK(r32(table + 112 * 8) == 0x60000070u);
    w32(resize_guard, r32(resize_guard) + 1);
    memcpy(pairs, hos_guest_range(table, sizeof pairs), sizeof pairs);
    memset(hos_guest_range(table, 256 * 8), 0, 256 * 8);
    for (int i = 0; i < 128; i++) {
        if (!pairs[i][0]) continue;
        uint32_t slot = pairs[i][0] % 256;
        while (r32(table + slot * 8ull)) slot = (slot + 1) % 256;
        w32(table + slot * 8ull, pairs[i][0]), w32(table + slot * 8ull + 4, pairs[i][1]);
    }
    w32(box + 36, 256);
    CHECK(r32(table + 112 * 8) == 0x60000070u);
}
int main(int argc, char **argv) {
    CHECK(argc == 2);
    table_caps();
    setup();
    char scratch[HOS_CONTROL_OOT_JSON];
    CHECK(!hos_oot_sample(scratch, sizeof scratch));
    hos_oot_watch(-1, NULL), hos_oot_watch(HOS_CONTROL_OOT_CLIENTS, NULL);
    CHECK(!hos_oot_poll(-1, scratch, sizeof scratch));
    CHECK(!hos_oot_poll(HOS_CONTROL_OOT_CLIENTS, scratch, sizeof scratch));
    CHECK(hos_control_serve(argv[1], &game));
    static const char *const names[] = {"Sequence_CurrentBanc", "PlayerStatus.Life", "abc", "ab", "a", "", "Test.Flag"};
    printf("{\"ready\":true,\"murmur\":{");
    for (unsigned i = 0; i < sizeof names / sizeof names[0]; i++)
        printf("%s\"%s\":%u", i ? "," : "", names[i], H(names[i]));
    puts("}}");
    fflush(stdout);
    char command[128];
    while (fgets(command, sizeof command, stdin)) {
        long long f;
        int v, r;
        if (sscanf(command, "sample %lld", &f) == 1) {
            __atomic_store_n(&hos_present_count, f, __ATOMIC_RELEASE);
            hos_oot_state_tap(actor);
            char map[64];
            if (state_text(state_u64(state_u64(STATE_GAMEDATA_SLOT)), STATE_MAP, map)) {
                AcquireSRWLockExclusive(&map_lock);
                memcpy(last_map, map, sizeof map);
                ReleaseSRWLockExclusive(&map_lock);
            }
        } else if (sscanf(command, "present %lld", &f) == 1) {
            __atomic_store_n(&hos_present_count, f, __ATOMIC_RELEASE);
        } else if (sscanf(command, "values %d %d", &v, &r) == 2) {
            w8(test_flag, (uint8_t)v), w32(rupees, (uint32_t)r);
        } else if (sscanf(command, "holdpoll %d", &v) == 1) {
            __atomic_store_n(&poll_held, v, __ATOMIC_RELEASE);
        } else if (sscanf(command, "burst %d", &v) == 1) {
            for (int i = 0; i < 40; i++) w32(bursts[i], (uint32_t)v);
        } else if (!strcmp(command, "unreadable\n")) {
            w64(view, 0), w64(map_value, 0x7000000000ull), w64(events + 104, 0);
        } else if (!strcmp(command, "no_mgr\n")) {
            w64(STATE_GAMEDATA_SLOT, 0);
        } else if (!strcmp(command, "restore\n")) {
            w64(STATE_GAMEDATA_SLOT, holder), w64(STATE_EVENTMGR_SLOT, eholder), w64(eholder, events);
            w64(view, STATE_VIEW_VTABLE), w64(map_value, map_text), w64(events + 104, 1);
            wf(actor + STATE_LINK_POS, -20.1f), wf(rotation, -71.43f);
        } else if (!strcmp(command, "adult\n")) {
            w8(child, 0), w8(loading, 1);
        } else if (!strcmp(command, "nulls\n")) {
            wf(actor + STATE_LINK_POS, NAN), wf(rotation, INFINITY), w64(eholder, 0x7000000000ull);
        } else if (!strcmp(command, "move\n")) {
            const uint64_t box = mgr + state_kind[KIND_INT].at, old = r64(box + 24);
            const uint32_t size = r32(box + 36);
            const uint64_t replacement = alloc(16ull * size);
            memcpy(hos_guest_range(replacement, 8ull * size), hos_guest_range(old, 8ull * size), 8ull * size);
            w64(box + 24, replacement);
        } else if (!strcmp(command, "resize\n")) {
            resize_int_table();
        } else if (!strcmp(command, "escape\n")) {
            w64(map_value, escaped_text), w64(prompt, escaped_text), w64(music, escaped_text);
        } else if (sscanf(command, "expect_watches %d", &v) == 1) {
            CHECK(__builtin_popcount(__atomic_load_n(&watch_mask, __ATOMIC_ACQUIRE)) == v);
        } else CHECK(!"unknown fixture command");
        puts("{\"stepped\":true}");
        fflush(stdout);
    }
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
        raise AssertionError(what)


def block(text, head):
    i = text.index(head)
    return text[i:text.index("\n}\n", i) + 3]


def wiring(root, tap, mutation):
    main_c = (root / "host" / "hos_main.c").read_text(encoding="utf-8")
    patches = (root / "nx2" / "oot_patches.py").read_text(encoding="utf-8")
    control = (root / "host" / "control.c").read_text(encoding="utf-8")
    docs = (root / "docs" / "development.md").read_text(encoding="utf-8")
    protocol = (root / "docs" / "control.md").read_text(encoding="utf-8")
    lift = block(main_c, "void hos_oot_lift_player(Ctx *c, uint64_t actor) {")
    if mutation == "hook":
        lift = lift.replace("hos_oot_state_tap(actor);", "")
    took, call, gave = (lift.find("TryAcquireSRWLockExclusive(&cheats_lock)"), lift.find("hos_oot_state_tap(actor);"),
                        lift.find("ReleaseSRWLockExclusive(&cheats_lock)"))
    check(0 <= took < call < gave, "hos_oot_lift_player does not call hos_oot_state_tap under its lock")
    check("hos_oot_lift_player(c, c->x[19]);" in patches, "Link's update does not call hos_oot_lift_player")
    check("void hos_oot_state_tap(uint64_t actor) {" in tap, "the tap's hook has another signature")
    for name in ("HOS_OOT_STATE_FILE", "HOS_OOT_STATE_EVERY", "HOS_OOT_STATE_FLAGS", "HOS_OOT_STATE_CHANGES"):
        check(name not in tap and name not in docs, "obsolete file tap switch remains: " + name)
    for name in ("oot_sample", "oot_watch", "oot_poll"):
        check(".%s = hos_%s" % (name, name) in control, "production control callback missing: " + name)
    check("control.md" in docs, "development guide no longer points to the control protocol")
    for name in ("oot", "flags", "changes", "every", "game", "frame"):
        check("`%s`" % name in protocol or '"%s"' % name in protocol, "control protocol does not describe " + name)

    defs = dict(re.findall(r"#define (STATE_\w+) (0x[0-9a-fA-F]+)", tap))
    check(re.search(r"#define OOT_GAMEDATA_SLOT %sULL\b" % defs["STATE_GAMEDATA_SLOT"], main_c) is not None,
          "GameDataMgr's slot differs from hos_main.c's")
    check(re.search(r"#define OOT_EVENTMGR_SLOT %sULL\b" % defs["STATE_EVENTMGR_SLOT"], main_c) is not None,
          "EventMgr's slot differs from hos_main.c's")
    check("recomp_load64(c, %sULL)" % defs["STATE_CAMERAS_SLOT"] in block(main_c, "static uint64_t oot_gameplay_view("),
          "the camera manager's slot differs from oot_gameplay_view's")
    check("recomp_load64(c, view) != %sULL" % defs["STATE_VIEW_VTABLE"] in main_c,
          "the view's vtable differs from hos_oot_free_view's")
    check(int(defs["STATE_LINK_POS"], 16) == 796 and "actor + 796" in main_c, "Link's position differs from the mark's")
    named = {"STATE_MAP": "Sequence_CurrentBanc", "STATE_LOADING": "Sequence_IsLoadingBanc", "STATE_CHILD": "Sequence_IsChildhood",
             "STATE_ROT": "Player_Rot_For_Map", "STATE_LIFE": "PlayerStatus.Life", "STATE_MAX_LIFE": "PlayerStatus.MaxLife",
             "STATE_RUPEES": "PlayerStatus.CurrentRupee"}
    for macro, name in named.items():
        value = int(defs[macro], 16)
        check(value == murmur(name), "%s is not the hash of %s" % (macro, name))
        if name.startswith("Sequence_"):
            check("0x%08xu" % value in main_c, "hos_main.c has another hash for " + name)


CORE_FIELDS = ("t", "frame", "map", "pos", "facing", "camera", "event", "loading", "age", "life", "max_life",
               "rupees", "prompt", "music")
MUTATIONS = tuple("drop-" + field for field in CORE_FIELDS) + (
    "cadence", "default-cadence", "independent", "null-range", "nonfinite", "lookup-cap", "changes-cap", "hash",
    "watch-hash", "collision", "changes", "changes-limit", "changes-more", "overflow-baseline", "moved-table",
    "resized-table", "replace", "unsubscribe", "disconnect", "pending", "retained", "json-cap", "flag-validation",
    "changes-validation", "hook")


def mutate_sources(tap, control, mode):
    """Explicit source mutations, required to match; an unmatched target is not a behavioral negative result."""
    def once(source, old, new):
        check(source.count(old) == 1, "MUTATION TARGET MISMATCH (%s): %r" % (mode, old))
        return source.replace(old, new, 1)

    def within(source, head, old, new):
        part = block(source, head)
        return once(source, part, once(part, old, new))

    if mode.startswith("drop-"):
        field = mode[5:]
        escaped, literal = r'\"%s\"' % field, '"%s"' % field
        check(escaped in tap or literal in tap, "MUTATION TARGET MISMATCH: " + mode)
        tap = tap.replace(escaped, r'\"missing_%s\"' % field).replace(literal, '"missing_%s"' % field)
    elif mode == "cadence":
        tap = once(tap, "frame - state_sub[slot].last < w->every", "frame - state_sub[slot].last < 1")
    elif mode == "default-cadence":
        control = within(control, "static void cmd_subscribe(", 'num_arg(d, req, "every", 60, &bad)',
                         'num_arg(d, req, "every", 6, &bad)')
    elif mode == "independent":
        tap = once(tap, "state_put_changes(s, mgr, state_sub[slot].seen)", "state_put_changes(s, mgr, state_sub[0].seen)")
    elif mode == "null-range":
        tap = within(tap, "static int state_peek(", "if (!p) return 0;", "if (!p) { memset(dst, 0, n); return 1; }")
    elif mode == "nonfinite":
        tap = once(tap, "isfinite(p[0]) && isfinite(p[1]) && isfinite(p[2])", "1")
    elif mode in ("lookup-cap", "changes-cap"):
        head = "static uint64_t state_entry(" if mode == "lookup-cap" else "static void state_put_changes("
        tap = within(tap, head, "size > (1u << 22)", "0")
    elif mode == "hash":
        tap = within(tap, "static uint32_t state_murmur(", "return h;", "return h ^ 1u;")
    elif mode == "watch-hash":
        tap = once(tap, "(uint32_t)strtoul(name + 2, NULL, 16)", "state_murmur(name, strlen(name))")
    elif mode == "collision":
        tap = within(tap, "static uint64_t state_entry(", "probe < size", "probe < 1")
    elif mode == "changes":
        tap = within(tap, "static void state_put_changes(", "seen[t].value[slot] != v", "0 && seen[t].value[slot] != v")
    elif mode == "changes-limit":
        tap = within(tap, "static void state_put_changes(", "shown < 32", "shown < 31")
    elif mode == "changes-more":
        tap = within(tap, "static void state_put_changes(", "more++;", "more += 0;")
    elif mode == "overflow-baseline":
        tap = within(tap, "static void state_put_changes(",
                     "seen[t].key[slot] = valid ? pair[0] : 0, seen[t].value[slot] = v;",
                     "if (!more) seen[t].key[slot] = valid ? pair[0] : 0, seen[t].value[slot] = v;")
    elif mode == "moved-table":
        tap = within(tap, "static void state_put_changes(", "seen[t].table != table || seen[t].size != size",
                     "seen[t].size != size")
    elif mode == "resized-table":
        tap = within(tap, "static void state_put_changes(", "int fresh = 0;",
                     "const int moved = seen[t].table != table;\n        int fresh = 0;")
        tap = within(tap, "static void state_put_changes(", "fresh = 1;", "fresh = moved;")
    elif mode == "replace":
        control = once(control, "if (G.game->oot_watch) G.game->oot_watch(ci, oot ? &watch : NULL);",
                       "if (G.game->oot_watch && !c->sub_oot) G.game->oot_watch(ci, oot ? &watch : NULL);")
    elif mode == "unsubscribe":
        control = within(control, "static void request(", "if (G.game->oot_watch) G.game->oot_watch(ci, NULL);",
                         "/* mutation: unsubscribe forgot the sampler */")
    elif mode == "disconnect":
        for head in ("static void conn_reset(", "static void conn_gone(", "static void conn_eof("):
            control = within(control, head, "if (G.game->oot_watch) G.game->oot_watch((int)(c - G.conns), NULL);",
                             "/* mutation: disconnect forgot the sampler */")
    elif mode == "pending":
        tap = once(tap, "if (state_sub[slot].ready) continue;", "/* mutation: overwrite pending samples */")
    elif mode == "retained":
        tap = within(tap, "int hos_oot_sample(", "if (have) memcpy(out, state_latest.text, state_latest.n + 1);",
                     "if (have) { memcpy(out, state_latest.text, state_latest.n + 1); state_latest.n = 0; }")
    elif mode == "json-cap":
        tap = once(tap, "char text[HOS_CONTROL_OOT_JSON];", "char text[8192];")
    elif mode == "flag-validation":
        control = within(control, "static int oot_watch_args(", "|| s[j] == '_' || s[j] == '.'",
                         "|| s[j] == '_' || s[j] == '.' || s[j] == '-'")
    elif mode == "changes-validation":
        control = within(control, "static int oot_watch_args(",
                         "if (changes && changes->type != J_TRUE && changes->type != J_FALSE) return 0;",
                         "/* mutation: accept non-boolean changes */")
    return tap, control


class Fixture:
    def __init__(self, exe, address, env, errors):
        self.proc = subprocess.Popen([str(exe), address], stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                                     stderr=errors, text=True, env=env)
        self.output = queue.Queue()
        threading.Thread(target=self.collect, daemon=True).start()
        try:
            ready = self.receive()
            check(ready.get("ready") is True, "sampler fixture did not start: %r" % ready)
            names = ("Sequence_CurrentBanc", "PlayerStatus.Life", "abc", "ab", "a", "", "Test.Flag")
            check(ready["murmur"] == {name: murmur(name) for name in names}, "C MurmurHash3 differs from the reference")
        except BaseException:
            self.close()
            raise

    def collect(self):
        for line in self.proc.stdout:
            self.output.put(line)
        self.output.put(None)

    def receive(self):
        try:
            line = self.output.get(timeout=30)
        except queue.Empty:
            raise AssertionError("sampler fixture did not acknowledge its step") from None
        check(line is not None, "sampler fixture exited before acknowledging its step")
        return json.loads(line)

    def step(self, command):
        self.proc.stdin.write(command + "\n")
        self.proc.stdin.flush()
        check(self.receive() == {"stepped": True}, "fixture did not finish " + command)

    def close(self):
        self.proc.stdin.close()
        try:
            self.proc.wait(timeout=5)
        except subprocess.TimeoutExpired:
            self.proc.kill()
            self.proc.wait()
        self.proc.stdout.close()


class Client:
    def __init__(self, module, address):
        self.module = module
        self.bridge = module.Bridge(address)
        self.serial = 0
        self.closed = False

    def close(self):
        if not self.closed:
            self.bridge.close()
            self.closed = True

    def ask(self, cmd, *, rid=None, ok=True, **args):
        self.serial += 1
        rid = self.serial if rid is None else rid
        self.bridge.send(dict(args, id=rid, cmd=cmd))
        answer = self.bridge.line(5)
        check(answer is not None and answer.get("id") == rid and answer.get("ok") is ok,
              "%s reply (events must not be silently discarded): %r" % (cmd, answer))
        return answer

    def event(self, sub, frame, reply_frame=None):
        answer = self.bridge.line(5)
        check(answer is not None and answer.get("event") == "oot" and answer.get("sub") == sub,
              "expected OoT event for %r: %r" % (sub, answer))
        check(answer["game"]["frame"] == frame and answer["frame"] >= frame,
              "sample present and reply present disagree: %r" % answer)
        if reply_frame is not None:
            check(answer["frame"] == reply_frame, "event envelope did not use the reply present: %r" % answer)
        return answer["game"]

    def quiet(self):
        try:
            answer = self.bridge.line(0.08)
        except self.module.Failed as error:
            check(error.code == 3, "connection failed instead of remaining quiet: %s" % error)
        else:
            raise AssertionError("unexpected event or closed connection: %r" % answer)


def socket_cases(fixture, module, address):
    with ExitStack() as stack:
        def connect():
            client = Client(module, address)
            stack.callback(client.close)
            return client

        def status_at(frame):
            deadline = time.monotonic() + 5
            while True:
                result = admin.ask("status")
                if result["frame"] == frame:
                    return result
                check(time.monotonic() < deadline, "bridge never reached present %d: %r" % (frame, result))
                time.sleep(0.005)

        def clients_at(count):
            deadline = time.monotonic() + 5
            while True:
                result = admin.ask("status")
                if result["clients"] == count:
                    return
                check(time.monotonic() < deadline, "disconnected slot was not released: %r" % result)
                time.sleep(0.005)

        admin, fast, slow, default = connect(), connect(), connect(), connect()
        initial = admin.ask("status")
        check(initial["protocol"] == 1 and initial["game"] is None and initial["map"] is None,
              "protocol or pre-Link status changed: %r" % initial)
        flags = ["Test.Flag", "0x00000110", "Test.Negative", "Step.Test", "Missing.Flag", "Player_Rot_For_Map",
                 "Test.Unsigned", "Sequence_CurrentBanc"]
        check(fast.ask("subscribe", rid="fast", events=["oot"], every=6, flags=flags, changes=True)["events"] == ["oot"],
              "OoT subscription was not acknowledged")
        slow.ask("subscribe", rid="slow", events=["oot"], every=12, flags=["Test.Flag"], changes=True)
        check(default.ask("subscribe", rid="default", events=["oot"])["every"] == 60, "default cadence is not 60")
        fixture.step("present 99")
        check(status_at(99)["game"] is None, "presents without Link created a snapshot")
        for client in (fast, slow, default):
            client.quiet()
        fixture.step("sample 100")
        one = fast.event("fast", 100)
        check(slow.event("slow", 100)["changed"] == {}, "second subscriber did not begin with an empty baseline")
        plain = default.event("default", 100)
        check("flags" not in plain and "changed" not in plain and "more" not in plain,
              "unrequested fields leaked to a default subscriber")
        want = {"map": "KokiriForest_Child", "pos": [-20.1, 0.0, 6.8], "facing": 108.6, "camera": 45.0,
                "event": True, "loading": False, "age": "child", "life": 12, "max_life": 12, "rupees": 5,
                "prompt": "Open", "music": 'BGM_"House"\x01'}
        for key, value in want.items():
            check(one.get(key) == value, "%s is %r, not %r" % (key, one.get(key), value))
        check(one["event"] is True and one["loading"] is False, "event/loading must be JSON booleans, not numbers")
        check(abs(one["t"] - 5 - 100 / 60) < 0.01, "t is not the host clock in seconds")
        check(one["flags"] == {"Test.Flag": 1, "0x00000110": 1, "Test.Negative": -3, "Step.Test": "#deadbeef",
                               "Missing.Flag": None, "Player_Rot_For_Map": -71.43, "Test.Unsigned": 3000000000,
                               "Sequence_CurrentBanc": "KokiriForest_Child"}, "typed names/hash/collision values: %r" % one)
        check(one["changed"] == {} and "more" not in one, "first sample reported changes")
        core = {key: value for key, value in one.items() if key not in ("flags", "changed")}
        check(status_at(100)["game"] == core, "status did not copy the complete core snapshot")
        fixture.step("sample 103")
        for client in (fast, slow, default):
            client.quiet()
        check(status_at(103)["game"]["frame"] == 103, "status was incorrectly throttled by watch cadence")
        fixture.step("values 0 7")
        fixture.step("sample 106")
        two = fast.event("fast", 106)
        expected_changes = {"0x%08x" % murmur("Test.Flag"): 0, "0x%08x" % murmur("PlayerStatus.CurrentRupee"): 7}
        check(two["flags"]["Test.Flag"] == 0 and two["rupees"] == 7 and two["changed"] == expected_changes,
              "second sample lost values or changes: %r" % two)
        slow.quiet()
        default.quiet()
        fixture.step("unreadable")
        fixture.step("sample 112")
        three = fast.event("fast", 112)
        check(three["map"] is None and three["camera"] is None and three["event"] is False,
              "unreadable text/view or stopped event: %r" % three)
        check(three["life"] == 12 and three["prompt"] == "Open" and three["changed"] == {},
              "unreadable map discarded readable fields")
        check(slow.event("slow", 112)["changed"] == expected_changes, "subscribers shared a changes baseline")
        fixture.step("no_mgr")
        fixture.step("sample 118")
        four = fast.event("fast", 118)
        for key in ("map", "facing", "loading", "age", "life", "max_life", "rupees", "prompt", "music"):
            check(four[key] is None, "%s is not null without GameDataMgr" % key)
        check(four["pos"] == [-20.1, 0.0, 6.8] and set(four["flags"].values()) == {None}, "missing-manager snapshot: %r" % four)
        retained = status_at(118)
        check(retained["map"] == "KokiriForest_Child" and retained["game"]["map"] is None,
              "top-level map did not retain the last known map")
        fixture.step("present 150")
        retained = status_at(150)
        check(retained["game"]["frame"] == 118 and retained["game"]["t"] == four["t"],
              "presents without Link refreshed the retained snapshot")
        for client in (fast, slow, default):
            client.quiet()
        fast.ask("unsubscribe")
        slow.ask("unsubscribe")
        fixture.step("expect_watches 1")
        fixture.step("sample 159")
        default.quiet()
        fixture.step("sample 160")
        check(default.event("default", 160)["map"] is None, "default subscription ignored its 60-present cadence")
        default.ask("unsubscribe")
        fixture.step("expect_watches 0")
        print("PASS: socket status, all typed flags, collision, nulls, 6/12/default-60 cadence, independent baselines")

        fixture.step("restore")
        fast.ask("subscribe", rid="valid", events=["oot"], every=1, flags=["Test.Flag"], changes=True)
        fixture.step("sample 200")
        check(fast.event("valid", 200)["changed"] == {}, "resubscribe retained changes")
        invalid = [{"flags": value} for value in (None, "Test.Flag", 1, {}, [1], [None], [True], [""], ["Bad-Name"],
                   ["Bad Name"], ["Bad/Name"], ["Bad,Name"], ["Bad\\Name"], ['Bad"Name'], ["Bad\tName"], ["Bad\nName"],
                   ["é"], ["x" * 48], ["0x"], ["0x1234567"], ["0x123456789"], ["0x1234567g"], ["x"] * 65)]
        invalid += [{"changes": value} for value in (None, 0, 1, "true", [], {})]
        invalid += [{"every": value} for value in (0, -1, 216001, 1e300, "six", [], {}, False)]
        invalid += [{"events": value} for value in ("oot", ["unknown"], [1])]
        invalid += [{"events": ["present"], "flags": ["Test.Flag"]}, {"events": [], "changes": False}]
        for args in invalid:
            reply = fast.ask("subscribe", ok=False, **dict({"events": ["oot"]}, **args))
            check(reply["error"] == "bad_request", "invalid subscription was not refused: %r -> %r" % (args, reply))
        fast.bridge.send({"id": "nul", "cmd": "subscribe", "events": ["oot"], "flags": ["Test.\x00Hidden"]})
        reply = fast.bridge.line(5)
        check(reply is not None and reply.get("id") is None and reply.get("ok") is False and reply.get("error") == "bad_json",
              "embedded NUL did not produce the protocol's id:null bad_json reply: %r" % reply)
        fixture.step("values 1 9")
        fixture.step("sample 201")
        valid = fast.event("valid", 201)
        check(valid["flags"] == {"Test.Flag": 1} and valid["changed"] == {
            "0x%08x" % murmur("Test.Flag"): 1, "0x%08x" % murmur("PlayerStatus.CurrentRupee"): 9},
            "a rejected replacement changed the valid subscription or its baseline")
        fast.ask("subscribe", rid="false", events=["oot"], every=1, flags=["a", "_", ".", "0x00000110"], changes=False)
        fixture.step("sample 202")
        false = fast.event("false", 202)
        check(false["flags"] == {"a": None, "_": None, ".": None, "0x00000110": 1} and "changed" not in false,
              "valid shortest names, explicit hash, or changes:false failed")
        print("PASS: invalid subscription arguments are atomic; name/hash/count bounds and changes:false")

        # Pending samples must be discarded on replacement/unsubscribe/disconnect, not relabelled with a new sub id.
        fixture.step("holdpoll 1")
        fixture.step("sample 203")
        fast.ask("subscribe", rid="replacement", events=["oot"], every=6, changes=True)
        fixture.step("holdpoll 0")
        fast.quiet()
        fixture.step("values 0 10")
        fixture.step("sample 204")
        check(fast.event("replacement", 204)["changed"] == {}, "replacement did not reset its baseline")
        fixture.step("holdpoll 1")
        fixture.step("sample 210")
        fast.ask("unsubscribe")
        fixture.step("expect_watches 0")
        fixture.step("holdpoll 0")
        fast.quiet()
        fixture.step("values 1 11")
        fixture.step("sample 216")
        fast.quiet()
        fast.ask("subscribe", rid="after-unsubscribe", events=["oot"], every=1, changes=True)
        fixture.step("sample 217")
        check(fast.event("after-unsubscribe", 217)["changed"] == {}, "unsubscribe kept a baseline")
        fixture.step("holdpoll 1")
        fixture.step("sample 218")
        fast.ask("subscribe", events=[])
        fixture.step("expect_watches 0")
        fixture.step("holdpoll 0")
        fast.quiet()
        fast.ask("subscribe", rid="before-disconnect", events=["oot"], every=1, changes=True)
        fixture.step("sample 219")
        check(fast.event("before-disconnect", 219)["changed"] == {}, "non-OoT replacement kept a baseline")
        fixture.step("holdpoll 1")
        fixture.step("sample 220")
        fast.close()
        clients_at(3)
        fixture.step("expect_watches 0")
        fixture.step("values 0 12")
        fast = connect()
        fast.ask("subscribe", rid="new-connection", events=["oot"], every=1, changes=True)
        fixture.step("holdpoll 0")
        fast.quiet()
        fixture.step("sample 221")
        check(fast.event("new-connection", 221)["changed"] == {}, "reused connection slot kept another client's baseline")
        print("PASS: replacement, non-OoT replacement, unsubscribe and disconnect clear pending samples and baselines")

        fixture.step("burst 1")
        fixture.step("sample 222")
        burst = fast.event("new-connection", 222)
        check(len(burst["changed"]) == 32 and burst["more"] == 8, "40 changes did not produce 32 + more:8")
        check(set(burst["changed"]) <= {"0x%08x" % (0x60000000 + i) for i in range(40)} and
              set(burst["changed"].values()) == {1}, "changes cap returned unrelated flags")
        fixture.step("sample 223")
        unchanged = fast.event("new-connection", 223)
        check(unchanged["changed"] == {} and "more" not in unchanged, "overflow changes were not baselined")
        fixture.step("burst 2")
        fixture.step("move")
        fixture.step("sample 224")
        check(fast.event("new-connection", 224)["changed"] == {}, "moved table did not establish a fresh baseline")
        fixture.step("burst 3")
        fixture.step("resize")
        fixture.step("sample 225")
        check(fast.event("new-connection", 225)["changed"] == {}, "in-place resized table did not reset its baseline")
        fixture.step("burst 4")
        fixture.step("sample 226")
        check(fast.event("new-connection", 226)["more"] == 8, "changes did not resume after the table reset")
        fixture.step("adult")
        fixture.step("sample 227")
        adult = fast.event("new-connection", 227)
        check(adult["age"] == "adult" and adult["loading"] is True, "adult/loading booleans were not represented")
        fixture.step("nulls")
        fixture.step("sample 228")
        nulls = fast.event("new-connection", 228)
        check(nulls["pos"] is None and nulls["facing"] is None and nulls["event"] is None,
              "non-finite coordinates or unreadable EventMgr leaked into JSON")
        fast.ask("unsubscribe")
        print("PASS: 32-change cap + more, moved/resized table baselines, adult and unreadable/non-finite fields")

        fixture.step("restore")
        fast.ask("subscribe", rid="pending", events=["oot"], every=6, changes=True)
        fixture.step("holdpoll 1")
        fixture.step("sample 300")
        fixture.step("values 1 13")
        fixture.step("sample 306")
        check(status_at(306)["game"]["frame"] == 306, "pending subscriber stalled the shared status snapshot")
        fixture.step("holdpoll 0")
        pending = fast.event("pending", 300, reply_frame=306)
        check(pending["rupees"] == 12 and pending["changed"] == {}, "pending sample was overwritten before consumption")
        fast.quiet()
        fixture.step("sample 312")
        resumed = fast.event("pending", 312)
        check(resumed["changed"] == {"0x%08x" % murmur("Test.Flag"): 1,
                                     "0x%08x" % murmur("PlayerStatus.CurrentRupee"): 13},
              "pending sample discarded changes while the bridge could not poll")
        fast.ask("unsubscribe")
        print("PASS: pending sample is retained, status stays current, and polling resumes without losing changes")

        # Worst allowed payload: 64 distinct 47-character keys, 63 six-byte escapes per value, the same escaped core
        # strings, 32 changed entries and a longest allowed request id. Parsing the actual line catches truncation.
        long_names = ["Test.Escaped.%034d" % i for i in range(64)]
        # The existing bridge guard is b.n < 200, counting the JSON quotes, not the decoded string length.
        fast.bridge.send({"id": "q" * 198, "cmd": "subscribe", "events": ["oot"]})
        too_long = fast.bridge.line(5)
        check(too_long is not None and too_long.get("id") is None and too_long.get("ok") is False and
              too_long.get("error") == "bad_request", "a 200-byte JSON id passed the bridge's existing bound")
        long_id = "q" * 197   # quotes make the longest accepted 199 JSON bytes
        fast.ask("subscribe", rid=long_id, events=["oot"], every=1, flags=long_names, changes=True)
        fixture.step("escape")
        fixture.step("sample 400")
        escaped = fast.event(long_id, 400)
        want_text = "\x01" + "\x80" * 62
        check(escaped["flags"] == dict.fromkeys(long_names, want_text), "longest escaped watched strings were truncated")
        check(all(escaped[key] == want_text for key in ("map", "prompt", "music")), "longest escaped core strings were truncated")
        fixture.step("burst 5")
        fixture.step("sample 401")
        maximum = fast.event(long_id, 401)
        check(maximum["flags"] == dict.fromkeys(long_names, want_text) and len(maximum["changed"]) == 32 and maximum["more"] == 8,
              "full escaped flags plus changes did not survive the transport")
        check(28000 < len(json.dumps(maximum, separators=(",", ":"))) < 32768,
              "fixture no longer exercises the large escaped JSON boundary")
        fast.ask("unsubscribe")
        fixture.step("expect_watches 0")
        fast.quiet()
        print("PASS: 64 longest names, maximally escaped strings, changes and a 199-byte id form complete socket JSON")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cc", default="gcc")
    parser.add_argument("--source-root", type=Path, default=ROOT,
                        help="production sampler/control/header/wiring/docs from this checkout (negative run)")
    parser.add_argument("--mutate", choices=MUTATIONS, default="", help="inject this regression in temporary source; must fail")
    args = parser.parse_args()
    root = args.source_root.resolve()
    tap = (root / "host" / "oot_state_tap.c").read_text(encoding="utf-8")
    if args.mutate == "hook":
        print("MUTATION: hook (expected source-wiring failure, not a compiled negative)", flush=True)
    wiring(root, tap, args.mutate)
    control = (root / "host" / "control.c").read_text(encoding="utf-8")
    if args.mutate:
        tap, control = mutate_sources(tap, control, args.mutate)
        print("MUTATION: %s (temporary production source; expected behavioral failure)" % args.mutate, flush=True)
    # Only platform/game declarations are substituted: every production helper and callback compiles unchanged.
    body = "".join(line for line in tap.splitlines(keepends=True)
                   if not line.startswith('#include "hos.h"') and not line.startswith("#include <windows.h>"))
    spec = importlib.util.spec_from_file_location("state_tap_client", ROOT / "scripts" / "hookshot_ctl.py")
    client_module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(client_module)
    windows = os.name == "nt"
    short = windows or len(tempfile.gettempdir()) <= 48 or not os.path.isdir("/tmp")
    with tempfile.TemporaryDirectory(prefix="oot-", dir=None if short else "/tmp") as folder:
        tmp = Path(folder)
        src, exe = tmp / "fixture.c", tmp / ("fixture.exe" if windows else "fixture")
        bridge_src = tmp / "control.c"
        bridge_src.write_text(control, encoding="utf-8")
        src.write_text(FIXTURE_HEAD + body + FIXTURE_MAIN, encoding="utf-8")
        warnings = ["-Wno-format-truncation"] if "clang" not in Path(args.cc).name else []
        libraries = ["-ladvapi32", "-lm"] if windows else ["-lpthread", "-lm"]
        subprocess.run([args.cc, "-std=c11", "-O2", "-Wall", "-Wextra", "-Werror", "-Wno-missing-field-initializers",
                        "-D_GNU_SOURCE", "-DHOS_CONTROL_NO_GAME", "-I", str(root / "host"), str(src),
                        str(bridge_src), "-o", str(exe)] + warnings + libraries, check=True)
        address = "hookshot-oot-test-%d" % os.getpid() if windows else str(tmp / "control.sock")
        # Legacy switches deliberately set: none may create a file, change cadence, flags or changed-field opt-in.
        old_file = tmp / "obsolete.jsonl"
        env = dict(os.environ, HOS_OOT_STATE_FILE=str(old_file), HOS_OOT_STATE_EVERY="1", HOS_OOT_STATE_FLAGS="Bad-Name",
                   HOS_OOT_STATE_CHANGES="1")
        env.pop("HOS_CONTROL_MEM", None)
        with (tmp / "stderr.txt").open("w+") as errors:
            fixture = None
            try:
                fixture = Fixture(exe, address, env, errors)
                socket_cases(fixture, client_module, address)
            except BaseException:
                errors.seek(0)
                print(errors.read(), end="")
                raise
            finally:
                if fixture is not None:
                    fixture.close()
            check(fixture.proc.returncode == 0, "sampler fixture failed on shutdown")
        check(not old_file.exists(), "the removed file switch still wrote a state file")
    print("PASS: OoT socket sampler, preserved hash/constants/lock wiring, both table caps and no file switches")


if __name__ == "__main__":
    main()
