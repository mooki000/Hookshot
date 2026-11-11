/* (hookshot) HOS_OOT_STATE_FILE=<file>: for a script that plays the game through HOS_INPUT_LIVE, a line of JSON with the
   game's state every HOS_OOT_STATE_EVERY presents (6), so that it knows where Link is and what the game is doing before
   its next press. Nothing in the game is written.

   host/hos_main.c calls hos_oot_state_tap from hos_oot_lift_player, the update of Link (nx2/oot_patches.py
   patch_movement), so lines come only while Link is in a map: none at the title, while a map loads or while the game is
   paused. A line:

     {"t":512.25,"frame":30144,"map":"KokiriForest_Child","pos":[-20.10,0.00,6.80],"facing":135.0,"camera":120.5,
      "event":0,"loading":0,"child":1,"life":12,"max_life":12,"rupees":0,"prompt":"Open","music":"BGM_House"}

   t        seconds of the host's clock
   frame    the game's presents so far (the frame clock of HOS_INPUT_FRAME_CLOCK)
   map      Sequence_CurrentBanc
   pos      Link's position, metres (his actor +0x31c, where the debug tools' mark reads it)
   facing   Link's yaw in degrees, 0 toward +z, 90 toward +x (the Float flag Player_Rot_For_Map keeps it less 180)
   camera   the gameplay camera's yaw, the same way, from its eye to its target (the view the free camera replaces)
   event    1 while an event plays: a cutscene, a conversation, a map change's (EventMgr +104, as the debug teleport asks)
   loading  Sequence_IsLoadingBanc;  child  Sequence_IsChildhood
   life, max_life  quarter hearts (PlayerStatus.Life, .MaxLife);  rupees  PlayerStatus.CurrentRupee
   prompt   the A button's action as the game names it (Open, Talk, Crawl...); music  the music playing (BGM_House)
   A value that cannot be read is null.

   HOS_OOT_STATE_FLAGS=<name>,... adds "flags": those GameData flags by name (the game names a flag by the MurmurHash3 of
   its name) or as 0x and 8 hex digits, at most 64, each looked up in the Bool, Int, Float, Enum, String64 and UInt
   containers in that order (an Enum's value is "#" and the hash of the value's name). HOS_OOT_STATE_CHANGES=1 adds
   "changed": every Bool, Int, Enum and UInt flag whose value differs from the line before, by hash (at most 32 a line,
   "more" counts the rest), to find out which flags an action sets.

   The flags are read in GameDataMgr's own tables, as oot_gamedata_set_bool (host/hos_main.c) writes the bools: each
   typed container holds the count of its entries at +8, the entries at +16, and at +24 a table of {u32 hash, u32 entry}
   probed from hash % its size (+36). */
#include "hos.h"
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
    char text[8192];
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

/* ---- the switches */

#define STATE_WATCH_MAX 64
static struct {
    uint32_t hash;
    char name[48];
} state_watch[STATE_WATCH_MAX];
static int state_nwatch;

/* HOS_OOT_STATE_FLAGS: names of [A-Za-z0-9_.] (at most 47 characters) or 0x and 8 hex digits, separated by commas */
static void state_watch_parse(const char *s) {
    while (s && *s) {
        const size_t n = strcspn(s, ",");
        int ok = n > 0 && n < sizeof state_watch[0].name;
        for (size_t i = 0; ok && i < n; i++) {
            const char ch = s[i];
            ok = (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9') || ch == '_' || ch == '.';
        }
        if (!ok || state_nwatch == STATE_WATCH_MAX) {
            hos_log("state: flag \"%.*s\" left out (a name of letters, digits, _ and ., or 0x and 8 hex digits; %d at most)",
                    (int)(n < 64 ? n : 64), s, STATE_WATCH_MAX);
        } else {
            char *end = NULL;
            const unsigned long h = n == 10 && s[0] == '0' && s[1] == 'x' ? strtoul(s + 2, &end, 16) : 0;
            state_watch[state_nwatch].hash = end == s + n ? (uint32_t)h : state_murmur(s, n);
            memcpy(state_watch[state_nwatch].name, s, n);
            state_watch[state_nwatch].name[n] = 0;
            state_nwatch++;
        }
        s += n;
        if (*s == ',') s++;
    }
}

/* ---- HOS_OOT_STATE_CHANGES: the values of the last line, slot by slot of each container's table */

static const int state_tracked[] = {KIND_BOOL, KIND_INT, KIND_ENUM, KIND_UINT};
#define STATE_TRACKED (int)(sizeof state_tracked / sizeof state_tracked[0])
static struct {
    uint64_t table;
    uint32_t size;
    uint32_t *key, *value;
} state_seen[STATE_TRACKED];

/* ,"changed":{"0x<hash>":<value>,...} for the flags whose value changed since the last call; a table seen for the first
   time (or moved, or resized: a save loaded) is only remembered */
static void state_put_changes(StateLine *l, uint64_t mgr) {
    int shown = 0, more = 0;
    state_put(l, ",\"changed\":{");
    for (int t = 0; t < STATE_TRACKED; t++) {
        const int kind = state_tracked[t];
        const uint64_t box = mgr + state_kind[kind].at, entries = state_u64(box + 16), table = state_u64(box + 24);
        const uint32_t count = state_u32(box + 8), size = state_u32(box + 36);
        if (!mgr || !entries || !table || !size || size > (1u << 22)) continue;
        int fresh = 0;
        if (state_seen[t].table != table || state_seen[t].size != size) {
            uint32_t *key = (uint32_t *)realloc(state_seen[t].key, size * sizeof *key);
            uint32_t *value = key ? (uint32_t *)realloc(state_seen[t].value, size * sizeof *value) : NULL;
            if (key) state_seen[t].key = key;
            if (!key || !value) continue;
            state_seen[t].value = value, state_seen[t].table = table, state_seen[t].size = size;
            fresh = 1;
        }
        for (uint32_t slot = 0; slot < size; slot++) {
            uint32_t pair[2] = {0, 0}, v = 0;
            uint8_t b = 0;
            state_peek(table + slot * 8ULL, pair, sizeof pair);
            if (pair[0] && pair[1] < count) {
                const uint64_t at = entries + (uint64_t)pair[1] * state_kind[kind].stride + state_kind[kind].value;
                if (kind == KIND_BOOL) v = state_peek(at, &b, 1) ? b : 0;
                else state_peek(at, &v, 4);
            }
            if (!fresh && pair[0] && state_seen[t].key[slot] == pair[0] && state_seen[t].value[slot] != v) {
                if (shown < 32) {
                    if (kind == KIND_INT) state_put(l, "%s\"0x%08x\":%d", shown ? "," : "", pair[0], (int)(int32_t)v);
                    else if (kind == KIND_ENUM) state_put(l, "%s\"0x%08x\":\"#%08x\"", shown ? "," : "", pair[0], v);
                    else state_put(l, "%s\"0x%08x\":%u", shown ? "," : "", pair[0], v);
                    shown++;
                } else {
                    more++;
                }
            }
            state_seen[t].key[slot] = pair[0], state_seen[t].value[slot] = v;
        }
    }
    state_put(l, "}");
    if (more) state_put(l, ",\"more\":%d", more);
}

/* ---- the hook: each update of Link (actor: the one whose +0x31c is his position) */

void hos_oot_state_tap(uint64_t actor) {
    static int looked, changes;
    static FILE *out;
    static int64_t every = 6, last = -1;
    if (!looked) {
        looked = 1;
        const char *path = getenv("HOS_OOT_STATE_FILE");
        if (!path || !*path) return;
        out = fopen(path, "w");
        if (!out) {
            hos_log("state: could not open %s", path);
            return;
        }
        const char *e = getenv("HOS_OOT_STATE_EVERY");
        if (e && atoi(e) > 0) every = atoi(e);
        const char *c = getenv("HOS_OOT_STATE_CHANGES");
        changes = c && atoi(c) != 0;
        state_watch_parse(getenv("HOS_OOT_STATE_FLAGS"));
        hos_log("state: a line every %lld presents to %s (%d flags watched%s)", (long long)every, path, state_nwatch,
                changes ? ", changed flags" : "");
    }
    if (!out) return;
    const int64_t frame = (int64_t)InterlockedCompareExchange64(&hos_present_count, 0, 0);
    if (last >= 0 && frame - last < every) return;
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
    else
        state_put(l, ",\"pos\":null");
    if (state_float(mgr, STATE_ROT, &rot)) state_put(l, ",\"facing\":%.1f", state_wrap(rot + 180.0));
    else state_put(l, ",\"facing\":null");
    double yaw;
    if (state_camera(&yaw)) state_put(l, ",\"camera\":%.1f", yaw);
    else state_put(l, ",\"camera\":null");
    const uint64_t events = state_u64(state_u64(STATE_EVENTMGR_SLOT));
    if (events) state_put(l, ",\"event\":%d", state_u64(events + 104) ? 1 : 0);
    else state_put(l, ",\"event\":null");
    state_put_number(l, mgr, "loading", KIND_BOOL, STATE_LOADING);
    state_put_number(l, mgr, "child", KIND_BOOL, STATE_CHILD);
    state_put_number(l, mgr, "life", KIND_INT, STATE_LIFE);
    state_put_number(l, mgr, "max_life", KIND_INT, STATE_MAX_LIFE);
    state_put_number(l, mgr, "rupees", KIND_INT, STATE_RUPEES);
    state_put_flag_text(l, mgr, "prompt", STATE_PROMPT);
    state_put_flag_text(l, mgr, "music", STATE_MUSIC);
    if (state_nwatch) {
        state_put(l, ",\"flags\":{");
        for (int i = 0; i < state_nwatch; i++) {
            state_put(l, "%s\"%s\":", i ? "," : "", state_watch[i].name);
            state_put_any(l, mgr, state_watch[i].hash);
        }
        state_put(l, "}");
    }
    if (changes) state_put_changes(l, mgr);
    state_put(l, "}\n");
    fputs(l->text, out);
    fflush(out);
}
