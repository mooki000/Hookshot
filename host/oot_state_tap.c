/* (hookshot) OoT's read-only samples for the control bridge (docs/control.md).
   status.game holds the last snapshot; subscribe events:["oot"] samples every n presents, with an optional watch list
   and changed flags. There is no file channel. HOS_CONTROL unset costs one early return.

   The guest reads stay in hos_oot_lift_player, under cheats_lock: Link's update is the point at which the actor,
   managers and camera are used by the game, not pointers kept for a later socket-thread read. The lock serializes
   successive guest workers and debug tools; it does not freeze every game thread, so every individual read is also
   range-checked and missing/non-finite values are null. state_lock hands only copied JSON to the bridge. No lock is
   held over socket I/O. Samples stop when Link is not updated (title, pause, load); their frame and t expose staleness.

   pos is in metres; facing and camera in degrees, 0 toward +z and 90 toward +x; life/max_life in quarter hearts.
   A watched Enum is "#" followed by the hash of its value's name. Changed Bool/Int/Enum/UInt flags are keyed by hash;
   the first sample establishes a baseline, at most 32 changes are sent, and more counts any left out.

   GameDataMgr's typed containers have their count at +8, entries at +16, and a {u32 hash,u32 entry} table at +24,
   probed from hash % size (+36), as host/hos_main.c's debug tools use them. Both lookup and scan cap the table size. */
#include "hos.h"
#include "control.h"
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>   /* (LONG64, InterlockedCompareExchange64; on Linux host/posix's) */

extern volatile LONG64 hos_present_count;   /* (host/nvn_native.c: the game's presents) */

#define STATE_GAMEDATA_SLOT 0x84c9e180ULL   /* GameDataMgr's holder (host/hos_main.c OOT_GAMEDATA_SLOT) */
#define STATE_EVENTMGR_SLOT 0x84c9e090ULL   /* EventMgr's (OOT_EVENTMGR_SLOT) */
#define STATE_CAMERAS_SLOT 0x84ca6bc0ULL    /* the camera manager's (host/hos_main.c oot_gameplay_view) */
#define STATE_VIEW_VTABLE 0x84c18a88ULL     /* a camera's view (hos_oot_free_view) */
#define STATE_LINK_POS 0x31c                /* Link's position in the actor hos_oot_lift_player is given */

#define STATE_MAP 0x1d6189dau        /* Sequence_CurrentBanc (String64) */
#define STATE_LOADING 0xef2b60c2u    /* Sequence_IsLoadingBanc (Bool) */
#define STATE_CHILD 0xcea220d3u      /* Sequence_IsChildhood (Bool) */
#define STATE_ROT 0xa4aee8cdu        /* Player_Rot_For_Map (Float) */
#define STATE_LIFE 0x31ab5580u       /* PlayerStatus.Life (Int) */
#define STATE_MAX_LIFE 0xfbe01da1u   /* PlayerStatus.MaxLife (Int) */
#define STATE_RUPEES 0xa77921d7u     /* PlayerStatus.CurrentRupee (Int) */
#define STATE_PROMPT 0x9824206cu     /* the A button's action (String64; its name is not known) */
#define STATE_MUSIC 0xcad3ffe5u      /* the music playing (String64; its name is not known) */

enum { KIND_BOOL, KIND_INT, KIND_FLOAT, KIND_ENUM, KIND_TEXT, KIND_UINT, KINDS };
/* each container's place in GameDataMgr, the size of its entries and where an entry keeps its value (a String64's: a
   pointer to the text) */
static const struct { uint32_t at, stride, value; } state_kind[KINDS] = {
    {0x008, 0x18, 0x16}, {0x0c8, 0x28, 0x1c}, {0x188, 0x28, 0x1c}, {0x248, 0x38, 0x1c}, {0x608, 0x108, 0x68}, {0x788, 0x28, 0x1c}};

/* n guest bytes at a into dst: 0 for a null pointer's page or anything outside the guest window */
static int state_peek(uint64_t a, void *dst, size_t n) {
    const void *p = a >= 0x100000 ? hos_guest_range(a, n) : NULL;
    if (!p) return 0;
    memcpy(dst, p, n);
    return 1;
}
static uint64_t state_u64(uint64_t a) {
    uint64_t v = 0;
    return state_peek(a, &v, 8) ? v : 0;
}
static uint32_t state_u32(uint64_t a) {
    uint32_t v = 0;
    return state_peek(a, &v, 4) ? v : 0;
}

/* MurmurHash3 x86 32 with seed 0: the hash the game names its flags by */
static uint32_t state_murmur(const char *s, size_t n) {
    uint32_t h = 0, k;
    size_t i = 0;
    for (; i + 4 <= n; i += 4) {
        k = (uint32_t)(unsigned char)s[i] | (uint32_t)(unsigned char)s[i + 1] << 8 | (uint32_t)(unsigned char)s[i + 2] << 16 |
            (uint32_t)(unsigned char)s[i + 3] << 24;
        k *= 0xcc9e2d51u, k = k << 15 | k >> 17, k *= 0x1b873593u;
        h ^= k, h = h << 13 | h >> 19, h = h * 5 + 0xe6546b64u;
    }
    k = 0;
    switch (n & 3) {
    case 3: k ^= (uint32_t)(unsigned char)s[i + 2] << 16; /* fall through */
    case 2: k ^= (uint32_t)(unsigned char)s[i + 1] << 8; /* fall through */
    case 1: k ^= (uint32_t)(unsigned char)s[i]; k *= 0xcc9e2d51u, k = k << 15 | k >> 17, k *= 0x1b873593u, h ^= k;
    }
    h ^= (uint32_t)n;
    h ^= h >> 16, h *= 0x85ebca6bu, h ^= h >> 13, h *= 0xc2b2ae35u, h ^= h >> 16;
    return h;
}

/* The entry of the flag hash in container kind of GameDataMgr mgr, 0 when it has none */
static uint64_t state_entry(uint64_t mgr, int kind, uint32_t hash) {
    const uint64_t box = mgr + state_kind[kind].at, entries = state_u64(box + 16), table = state_u64(box + 24);
    const uint32_t count = state_u32(box + 8), size = state_u32(box + 36);
    if (!mgr || !entries || !table || !size || size > (1u << 22) || !hash) return 0;
    for (uint32_t probe = 0, slot = hash % size; probe < size; probe++, slot = slot + 1 == size ? 0 : slot + 1) {
        uint32_t pair[2];
        if (!state_peek(table + slot * 8ULL, pair, sizeof pair) || !pair[0]) return 0;
        if (pair[0] == hash) return pair[1] < count ? entries + (uint64_t)pair[1] * state_kind[kind].stride : 0;
    }
    return 0;
}

/* ---- the line */

typedef struct {
    /* 64 watched strings, each at most 63 bytes escaped to six JSON bytes, plus names, core fields and 32 changes. */
    char text[HOS_CONTROL_OOT_JSON];
    size_t n;
} StateLine;

static void state_put(StateLine *l, const char *fmt, ...) {
    if (l->n + 1 >= sizeof l->text) return;
    va_list a;
    va_start(a, fmt);
    const int k = vsnprintf(l->text + l->n, sizeof l->text - l->n, fmt, a);
    va_end(a);
    if (k > 0) l->n = l->n + (size_t)k < sizeof l->text ? l->n + (size_t)k : sizeof l->text - 1;
}

/* a text as a JSON string: quotes and backslashes escaped, control and non-ASCII bytes as \u00XX */
static void state_put_text(StateLine *l, const char *s) {
    state_put(l, "\"");
    for (; *s; s++) {
        const unsigned char ch = (unsigned char)*s;
        if (ch == '"' || ch == '\\') state_put(l, "\\%c", ch);
        else if (ch < 0x20 || ch > 0x7e) state_put(l, "\\u%04x", ch);
        else state_put(l, "%c", ch);
    }
    state_put(l, "\"");
}

/* A String64 flag's text into buf (64 bytes), 0 when it cannot be read */
static int state_text(uint64_t mgr, uint32_t hash, char buf[64]) {
    const uint64_t e = state_entry(mgr, KIND_TEXT, hash), p = e ? state_u64(e + state_kind[KIND_TEXT].value) : 0;
    buf[0] = 0;
    if (!p || !state_peek(p, buf, 64)) return 0;
    buf[63] = 0;
    return 1;
}

/* A number flag (Bool, Int, Enum or UInt) into *v, 0 when it has none of these */
static int state_number(uint64_t mgr, int kind, uint32_t hash, int64_t *v) {
    const uint64_t e = state_entry(mgr, kind, hash), at = e + state_kind[kind].value;
    uint8_t b;
    uint32_t u;
    if (!e) return 0;
    if (kind == KIND_BOOL) return state_peek(at, &b, 1) && (*v = b, 1);
    if (!state_peek(at, &u, 4)) return 0;
    *v = kind == KIND_INT ? (int64_t)(int32_t)u : (int64_t)u;
    return 1;
}

static int state_float(uint64_t mgr, uint32_t hash, float *f) {
    const uint64_t e = state_entry(mgr, KIND_FLOAT, hash);
    return e && state_peek(e + state_kind[KIND_FLOAT].value, f, 4) && isfinite(*f);
}

/* ,"<key>":<number flag> or null */
static void state_put_number(StateLine *l, uint64_t mgr, const char *key, int kind, uint32_t hash) {
    int64_t v;
    if (state_number(mgr, kind, hash, &v)) state_put(l, ",\"%s\":%lld", key, (long long)v);
    else state_put(l, ",\"%s\":null", key);
}

/* ,"<key>":"<String64 flag>" or null */
static void state_put_flag_text(StateLine *l, uint64_t mgr, const char *key, uint32_t hash) {
    char t[64];
    state_put(l, ",\"%s\":", key);
    if (state_text(mgr, hash, t)) state_put_text(l, t);
    else state_put(l, "null");
}

/* a flag's value in whichever container has it: a number, a text, an Enum's value as "#<hash>", null */
static void state_put_any(StateLine *l, uint64_t mgr, uint32_t hash) {
    int64_t v;
    float f;
    char t[64];
    if (state_number(mgr, KIND_BOOL, hash, &v) || state_number(mgr, KIND_INT, hash, &v)) state_put(l, "%lld", (long long)v);
    else if (state_float(mgr, hash, &f)) state_put(l, "%.3f", f);
    else if (state_number(mgr, KIND_ENUM, hash, &v)) state_put(l, "\"#%08x\"", (unsigned)v);
    else if (state_text(mgr, hash, t)) state_put_text(l, t);
    else if (state_number(mgr, KIND_UINT, hash, &v)) state_put(l, "%lld", (long long)v);
    else state_put(l, "null");
}

/* An angle in degrees in [-180, 180) */
static double state_wrap(double d) {
    d = fmod(d + 180.0, 360.0);
    return (d < 0 ? d + 360.0 : d) - 180.0;
}

/* The gameplay camera's yaw (degrees, 0 toward +z, 90 toward +x), 0 when it cannot be read */
static int state_camera(double *yaw) {
    const uint64_t manager = state_u64(state_u64(STATE_CAMERAS_SLOT));
    const uint64_t cameras = manager && state_u32(manager + 32) ? state_u64(manager + 40) : 0;
    const uint64_t camera = cameras ? state_u64(cameras) : 0, view = camera ? state_u64(camera + 120) : 0;
    double eye[3], at[3];
    if (!view || state_u64(view) != STATE_VIEW_VTABLE || !state_peek(view + 96, eye, sizeof eye) ||
        !state_peek(view + 120, at, sizeof at))
        return 0;
    const double x = at[0] - eye[0], z = at[2] - eye[2];
    if (!(x * x + z * z > 1e-9) || !isfinite(x) || !isfinite(z)) return 0;
    *yaw = atan2(x, z) * 57.29577951308232;
    return 1;
}

/* ---- per-connection watches and baselines ---- */


static const int state_tracked[] = {KIND_BOOL, KIND_INT, KIND_ENUM, KIND_UINT};
#define STATE_TRACKED (int)(sizeof state_tracked / sizeof state_tracked[0])
typedef struct {
    uint64_t table;
    uint32_t size;
    uint32_t *key, *value;
} StateSeen;

/* ,"changed":{"0x<hash>":<value>,...} for the flags whose value changed since the last call; a table seen for the first
   time (or moved, or resized: a save loaded) is only remembered */
static void state_put_changes(StateLine *l, uint64_t mgr, StateSeen seen[STATE_TRACKED]) {
    int shown = 0, more = 0;
    state_put(l, ",\"changed\":{");
    for (int t = 0; t < STATE_TRACKED; t++) {
        const int kind = state_tracked[t];
        const uint64_t box = mgr + state_kind[kind].at, entries = state_u64(box + 16), table = state_u64(box + 24);
        const uint32_t count = state_u32(box + 8), size = state_u32(box + 36);
        if (!mgr || !entries || !table || !size || size > (1u << 22)) continue;
        int fresh = 0;
        if (seen[t].table != table || seen[t].size != size) {
            uint32_t *key = (uint32_t *)realloc(seen[t].key, size * sizeof *key);
            uint32_t *value = key ? (uint32_t *)realloc(seen[t].value, size * sizeof *value) : NULL;
            if (key) seen[t].key = key;
            if (!key || !value) continue;
            seen[t].value = value, seen[t].table = table, seen[t].size = size;
            fresh = 1;
        }
        for (uint32_t slot = 0; slot < size; slot++) {
            uint32_t pair[2] = {0, 0}, v = 0;
            uint8_t b = 0;
            int valid = state_peek(table + slot * 8ULL, pair, sizeof pair) && pair[0] && pair[1] < count;
            if (valid) {
                const uint64_t at = entries + (uint64_t)pair[1] * state_kind[kind].stride + state_kind[kind].value;
                if (kind == KIND_BOOL) valid = state_peek(at, &b, 1), v = b;
                else valid = state_peek(at, &v, 4);
            }
            if (!fresh && valid && seen[t].key[slot] == pair[0] && seen[t].value[slot] != v) {
                if (shown < 32) {
                    if (kind == KIND_INT) state_put(l, "%s\"0x%08x\":%d", shown ? "," : "", pair[0], (int)(int32_t)v);
                    else if (kind == KIND_ENUM) state_put(l, "%s\"0x%08x\":\"#%08x\"", shown ? "," : "", pair[0], v);
                    else state_put(l, "%s\"0x%08x\":%u", shown ? "," : "", pair[0], v);
                    shown++;
                } else {
                    more++;
                }
            }
            seen[t].key[slot] = valid ? pair[0] : 0, seen[t].value[slot] = v;
        }
    }
    state_put(l, "}");
    if (more) state_put(l, ",\"more\":%d", more);
}

static SRWLOCK state_lock = SRWLOCK_INIT;
static StateLine state_latest;
static struct {
    HosControlOotWatch watch;
    uint32_t hashes[HOS_CONTROL_OOT_FLAGS];
    StateSeen seen[STATE_TRACKED];
    StateLine line;
    int active, ready;
    int64_t last;
} state_sub[HOS_CONTROL_OOT_CLIENTS];

int hos_oot_sample(char *out, size_t cap) {
    AcquireSRWLockShared(&state_lock);
    const int have = state_latest.n != 0 && state_latest.n < cap;
    if (have) memcpy(out, state_latest.text, state_latest.n + 1);
    ReleaseSRWLockShared(&state_lock);
    return have;
}

void hos_oot_watch(int slot, const HosControlOotWatch *watch) {
    if (slot < 0 || slot >= HOS_CONTROL_OOT_CLIENTS) return;
    AcquireSRWLockExclusive(&state_lock);
    for (int t = 0; t < STATE_TRACKED; t++) {
        free(state_sub[slot].seen[t].key);
        free(state_sub[slot].seen[t].value);
    }
    memset(&state_sub[slot], 0, sizeof state_sub[slot]);
    if (watch) {
        state_sub[slot].watch = *watch;
        state_sub[slot].active = 1;
        state_sub[slot].last = -1;
        for (int i = 0; i < watch->nflags; i++) {
            const char *name = watch->flags[i];   /* validated by the bridge */
            state_sub[slot].hashes[i] = name[0] == '0' && name[1] == 'x' ?
                (uint32_t)strtoul(name + 2, NULL, 16) : state_murmur(name, strlen(name));
        }
    }
    ReleaseSRWLockExclusive(&state_lock);
}

int hos_oot_poll(int slot, char *out, size_t cap) {
    if (slot < 0 || slot >= HOS_CONTROL_OOT_CLIENTS) return 0;
    AcquireSRWLockExclusive(&state_lock);
    const int have = state_sub[slot].ready && state_sub[slot].line.n < cap;
    if (have) {
        memcpy(out, state_sub[slot].line.text, state_sub[slot].line.n + 1);
        state_sub[slot].ready = 0;
    }
    ReleaseSRWLockExclusive(&state_lock);
    return have;
}

/* Each update of Link (actor: the one whose +0x31c is his position). Only this function reads guest memory. */
void hos_oot_state_tap(uint64_t actor) {
    if (!hos_control_on) return;
    static int64_t last = -1;
    const int64_t frame = (int64_t)InterlockedCompareExchange64(&hos_present_count, 0, 0);
    if (frame == last) return;
    last = frame;

    static StateLine line;
    StateLine *l = &line;
    const uint64_t mgr = state_u64(state_u64(STATE_GAMEDATA_SLOT));
    l->n = 0;
    state_put(l, "{\"t\":%.3f,\"frame\":%lld", (double)hos_now_ns() / 1e9, (long long)frame);
    state_put_flag_text(l, mgr, "map", STATE_MAP);
    float p[3], rot;
    if (state_peek(actor + STATE_LINK_POS, p, sizeof p) && isfinite(p[0]) && isfinite(p[1]) && isfinite(p[2]))
        state_put(l, ",\"pos\":[%.2f,%.2f,%.2f]", p[0], p[1], p[2]);
    else state_put(l, ",\"pos\":null");
    if (state_float(mgr, STATE_ROT, &rot)) state_put(l, ",\"facing\":%.1f", state_wrap(rot + 180.0));
    else state_put(l, ",\"facing\":null");
    double yaw;
    if (state_camera(&yaw)) state_put(l, ",\"camera\":%.1f", yaw);
    else state_put(l, ",\"camera\":null");
    const uint64_t events = state_u64(state_u64(STATE_EVENTMGR_SLOT));
    uint64_t event;
    state_put(l, ",\"event\":%s", events && state_peek(events + 104, &event, sizeof event) ?
              (event ? "true" : "false") : "null");
    int64_t value;
    state_put(l, ",\"loading\":%s", state_number(mgr, KIND_BOOL, STATE_LOADING, &value) ?
              (value ? "true" : "false") : "null");
    state_put(l, ",\"age\":%s", state_number(mgr, KIND_BOOL, STATE_CHILD, &value) ?
              (value ? "\"child\"" : "\"adult\"") : "null");
    state_put_number(l, mgr, "life", KIND_INT, STATE_LIFE);
    state_put_number(l, mgr, "max_life", KIND_INT, STATE_MAX_LIFE);
    state_put_number(l, mgr, "rupees", KIND_INT, STATE_RUPEES);
    state_put_flag_text(l, mgr, "prompt", STATE_PROMPT);
    state_put_flag_text(l, mgr, "music", STATE_MUSIC);
    state_put(l, "}");

    AcquireSRWLockExclusive(&state_lock);
    state_latest.n = l->n;
    memcpy(state_latest.text, l->text, l->n + 1);
    for (int slot = 0; slot < HOS_CONTROL_OOT_CLIENTS; slot++) {
        const HosControlOotWatch *w = &state_sub[slot].watch;
        if (!state_sub[slot].active || (state_sub[slot].last >= 0 && frame - state_sub[slot].last < w->every)) continue;
        /* A slow bridge keeps the pending sample, including its changes, until consumed. Sampling resumes after it;
           no changes are silently overwritten while socket output is backlogged. */
        if (state_sub[slot].ready) continue;
        StateLine *s = &state_sub[slot].line;
        s->n = l->n - 1;   /* append optional fields inside the copied object */
        memcpy(s->text, l->text, s->n);
        if (w->nflags) {
            state_put(s, ",\"flags\":{");
            for (int i = 0; i < w->nflags; i++) {
                state_put(s, "%s\"%s\":", i ? "," : "", w->flags[i]);
                state_put_any(s, mgr, state_sub[slot].hashes[i]);
            }
            state_put(s, "}");
        }
        if (w->changes) state_put_changes(s, mgr, state_sub[slot].seen);
        state_put(s, "}");
        state_sub[slot].last = frame;
        state_sub[slot].ready = 1;
    }
    ReleaseSRWLockExclusive(&state_lock);
}
