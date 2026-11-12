/* Local stall records, independent of the renderer and its numbered interface.
   Linux: SIGRTMIN+4, distinct from posix/prof.c's +3 and glibc's reserved signals.
   The handler only copies PCs through checked process_vm_readv syscalls into fixed slots.
   In particular it never calls backtrace, libgcc, dladdr, stdio, a lock or an allocator.
   Frame-pointer walks can be partial in optimized code; the interrupted PC is always kept.
   Windows: context/stack bytes are copied while suspended, then the thread is resumed BEFORE
   StackWalk64, module lookup, formatting or I/O. No raw stack bytes ever reach a file. */
#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "hos_stall.h"
#include "hos_report_privacy.h"
#include "../gpu/vk/compat/hookshot_env.h"
#include "hos_report_run.h"
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <tlhelp32.h>
#include <dbghelp.h>
typedef HANDLE Record;
#define NO_RECORD INVALID_HANDLE_VALUE
#else
#include <fcntl.h>
#include <pthread.h>
#include <pwd.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/uio.h>
#include <ucontext.h>
#include <unistd.h>
typedef int Record;
#define NO_RECORD (-1)
#endif

enum { MAX_THREADS = 512, MAX_FRAMES = 48, MAX_MODULES = 1024 };
typedef struct {
    uint32_t tid;
    char name[96];
    uintptr_t pc[MAX_FRAMES];
    unsigned count;
    int done;
} ThreadStack;
typedef struct { uintptr_t lo, hi, base; char name[128]; } Module;
static ThreadStack stacks[HOS_STALL_CAP][MAX_THREADS]; /* Never reuse a late signal's slot. */
static Module modules[MAX_MODULES];
static unsigned module_count, collection;
static HosReportPrivacy privacy;
static char folder[4096], version_text[160];
static Record active_record = NO_RECORD;
static const char *run_id;
static uint64_t started_ms, progress_at_ms, progress_counts[8];
static uint64_t gpu_claim_until_ms;
static unsigned game_flags = HOS_STALL_UNKNOWN, progress_seen;
static int enabled, collect_stacks, headless;
static int64_t presented_ns;
static uint64_t block_at; /* Inert unless HOS_STALL_TEST_BLOCK_FRAME is set. */
static unsigned block_seconds;
extern volatile int64_t hos_present_count;
extern int64_t hos_now_ns(void);

static uint64_t now_ms(void) { return (uint64_t)hos_now_ns() / 1000000; }
static void sleep_ms(unsigned ms) {
#ifdef _WIN32
    Sleep(ms);
#else
    struct timespec t = {(time_t)(ms / 1000), (long)(ms % 1000) * 1000000L};
    while (nanosleep(&t, &t) && errno == EINTR) {}
#endif
}
static uint64_t age(uint64_t now, uint64_t then) { return now >= then ? now - then : 0; }

int hos_stall_tick(HosStallDetector *d, uint64_t now, uint64_t frame, unsigned flags,
                   uint64_t log_progress, uint64_t core_progress) {
    const int moved = d->initialized && frame != d->frame;
    const int resumed = moved && d->active;
    const int gap = d->initialized && (now < d->last_tick_ms || now - d->last_tick_ms > 5000);
    if (!d->initialized || moved || gap || flags != d->flags) d->eligible_ms = now;
    if (!d->initialized || moved || gap) d->last_present_ms = now;
    if (!d->initialized || gap || (core_progress != UINT64_MAX && core_progress != d->core_progress))
        d->progress_ms = d->core_progress_ms = now;
    if (log_progress > d->progress_ms && log_progress <= now) d->progress_ms = log_progress;
    d->frame = frame, d->last_tick_ms = now, d->flags = flags;
    if (core_progress != UINT64_MAX) d->core_progress = core_progress;
    d->initialized = 1;
    if (resumed) { d->active = 0; return HOS_STALL_RESUMED; }
    if (d->active || d->records >= HOS_STALL_CAP ||
        (flags & (HOS_STALL_MOVIE | HOS_STALL_PAUSED | HOS_STALL_UNFOCUSED | HOS_STALL_EXITING | HOS_STALL_GPU_LOSS)))
        return HOS_STALL_NONE;
    const int long_tier = flags & (HOS_STALL_UNKNOWN | HOS_STALL_LOADING | HOS_STALL_PREPARING);
    const uint64_t limit = long_tier ? HOS_STALL_LONG_MS : HOS_STALL_NORMAL_MS;
    uint64_t since = d->eligible_ms;
    if (long_tier && d->progress_ms > since) since = d->progress_ms;
    if (age(now, since) < limit) return HOS_STALL_NONE;
    d->active = 1, d->records++;
    return HOS_STALL_BEGIN;
}

/* Parsing stops at the count. Percentages, ETA, paths and arbitrary log text are never retained.
   The same count printed again, or a vibration/audio/vcore status line, is not progress. */
static int take(const char **p, const char *literal) {
    const size_t n = strlen(literal);
    if (strncmp(*p, literal, n)) return 0;
    *p += n;
    return 1;
}
static int number(const char **p, uint64_t *out) {
    uint64_t value = 0;
    unsigned digits = 0;
    while (**p >= '0' && **p <= '9' && digits < 12) value = value * 10 + (unsigned)(*(*p)++ - '0'), digits++;
    if (!digits || (**p >= '0' && **p <= '9')) return 0;
    *out = value;
    return 1;
}
/* vk_replay.cpp gpu_hang_watch_start: its 10 s warning claims its own >0 timeout.
   vk_draw.cpp checkpoints_report announces a direct device loss before any driver query.
   Wording assertions in test_stall_record.py make these log seams explicit. */
int hos_stall_gpu_wait(const char *line, uint64_t *wait_ms) {
    const char *p = line;
    uint64_t still, done, sent, limit;
    if (take(&p, "vk: device lost:")) { *wait_ms = UINT64_MAX; return 1; }
    p = line;
    if (take(&p, "vk: GPU hang: the timeline at ") && number(&p, &done) &&
        take(&p, ", ") && number(&p, &sent) && take(&p, " sent") && !*p) {
        *wait_ms = UINT64_MAX;
        return 1;
    }
    p = line;
    if (!take(&p, "vk: the GPU has finished nothing for ") || !number(&p, &still) ||
        !take(&p, " s: the timeline at ") || !number(&p, &done) || !take(&p, ", ") ||
        !number(&p, &sent) || !take(&p, " sent (RX_VK_GPU_HANG_SEC=") || !number(&p, &limit) ||
        !take(&p, ")") || *p || limit > 2147483647u) return 0;
    *wait_ms = limit ? ((limit > still ? limit - still : 0) + 10) * 1000 : 0;
    return 1;
}

int hos_stall_progress(const char *line, unsigned *stage, uint64_t *done) {
    static const char *const prefixes[] = {
        "vk: start-up: shaders ", "vk: start-up: predicted pipelines ",
        "vk: start-up: pipelines ", "vk: start-up: ",
        "vk: noted pipelines: ", "vk: compute pipelines: ",
        "vk: predicted pipelines: ", "vk: prepared "
    };
    for (unsigned i = 0; i < sizeof prefixes / sizeof *prefixes; i++) {
        const size_t n = strlen(prefixes[i]);
        if (strncmp(line, prefixes[i], n)) continue;
        const char *p = line + n;
        uint64_t v, total;
        if (!number(&p, &v)) continue;
        if (i == 7) { if (!take(&p, " shader programs")) continue; }
        else if (!take(&p, " of ") || !number(&p, &total) || v > total) continue;
        *stage = i, *done = v;
        return 1;
    }
    return 0;
}
void hos_stall_log(const char *line, int64_t ns) {
    if (!__atomic_load_n(&enabled, __ATOMIC_RELAXED)) return;
    uint64_t wait_ms;
    if (hos_stall_gpu_wait(line, &wait_ms) && wait_ms) {
        const uint64_t until = wait_ms == UINT64_MAX ? UINT64_MAX : (uint64_t)ns / 1000000 + wait_ms;
        uint64_t old = __atomic_load_n(&gpu_claim_until_ms, __ATOMIC_RELAXED);
        while (until > old && !__atomic_compare_exchange_n(&gpu_claim_until_ms, &old, until, 0,
                                                           __ATOMIC_RELAXED, __ATOMIC_RELAXED)) {}
    }
    unsigned stage;
    uint64_t done;
    if (!hos_stall_progress(line, &stage, &done)) return;
    const uint64_t previous = __atomic_exchange_n(&progress_counts[stage], done, __ATOMIC_RELAXED);
    const unsigned bit = 1u << stage;
    if (!(__atomic_fetch_or(&progress_seen, bit, __ATOMIC_RELAXED) & bit) || done != previous)
        __atomic_store_n(&progress_at_ms, (uint64_t)ns / 1000000, __ATOMIC_RELAXED);
}
void hos_stall_game_state(unsigned flags) { __atomic_store_n(&game_flags, flags, __ATOMIC_RELAXED); }
int64_t hos_stall_last_present_ns(void) { return __atomic_load_n(&presented_ns, __ATOMIC_RELAXED); }

/* A diagnostic block, not a process stop: other threads (including the watchdog) keep running.
   No clock read or branch beyond the zero check while unset. Used only on disposable test runs. */
static __attribute__((noinline)) void test_present_block_sleep(void) {
    sleep_ms(100);
    /* Keep this diagnostic call's frame link even in a release build. The Linux
       frame-pointer sampler can then identify the block, not only libc's sleep. */
    __asm__ volatile("" : : "r"(__builtin_frame_address(0)) : "memory");
}
static __attribute__((noinline)) void test_present_block(void) {
    const uint64_t until = now_ms() + (uint64_t)block_seconds * 1000;
    while (now_ms() < until) test_present_block_sleep();
}
void hos_stall_present(int64_t ns) {
    __atomic_store_n(&presented_ns, ns, __ATOMIC_RELAXED);
    if (block_at && (uint64_t)__atomic_load_n(&hos_present_count, __ATOMIC_RELAXED) >= block_at) {
        block_at = 0;
        test_present_block();
    }
}

static void close_record(Record f) {
#ifdef _WIN32
    if (f != NO_RECORD) CloseHandle(f);
#else
    if (f != NO_RECORD) close(f);
#endif
}
static int raw_write(Record f, const void *data, size_t size) {
    const char *p = (const char *)data;
    while (size) {
#ifdef _WIN32
        DWORD n = 0;
        if (!WriteFile(f, p, (DWORD)size, &n, NULL) || !n) return 0;
#else
        const ssize_t n = write(f, p, size);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) return 0;
#endif
        p += n, size -= (size_t)n;
    }
    return 1;
}
static int put(Record f, const char *fmt, ...) {
    char line[1024];
    va_list ap;
    va_start(ap, fmt);
    const int n = vsnprintf(line, sizeof line, fmt, ap);
    va_end(ap);
    return n >= 0 && (size_t)n < sizeof line && raw_write(f, line, (size_t)n);
}
static void log_line(const char *line) {
    /* No log/stdio lock: the frozen thread may own one. Only fixed, non-private lines here. */
#ifdef _WIN32
    raw_write(GetStdHandle(STD_ERROR_HANDLE), line, strlen(line));
#else
    raw_write(STDERR_FILENO, line, strlen(line));
#endif
}
static void clean(char *out, size_t cap, const char *in) {
    hos_report_privacy_redact(&privacy, out, cap, in, strlen(in));
}
static void thread_label(ThreadStack *s, const char *name) {
    /* Only names already assigned/recognized by the host and renderer. Linux comm truncates
       them to 15 bytes. Unknown driver/third-party names could contain private information. */
    static const char *const known[] = {
        "Game", "MainThread", "sead::MainThread", "Presentation Thread", "mkw_vk display",
        "mkw_vk worker", "mkw_vk serial builds", "mkw_vk capture copy", "mkw_vk imports",
        "mkw_vk recorder", "mkw_vk replay worker", "mkw_vk order", "mkw_vk gpu hang watch",
        "mkw_vk frame log", "mkw_vk presenter", "mkw_vk replay", "mkw_vk hang watch"
    };
    for (unsigned i = 0; i < sizeof known / sizeof *known; i++) {
        if (!strcmp(name, known[i]) || (strlen(name) == 15 && !strncmp(name, known[i], 15))) {
            /* The label is this literal, never the supplied bytes. */
            snprintf(s->name, sizeof s->name, "%s", known[i]);
            return;
        }
    }
    snprintf(s->name, sizeof s->name, "thread-%u", s->tid);
}
static void add_module(uintptr_t lo, uintptr_t hi, uintptr_t base, const char *name) {
    if (module_count == MAX_MODULES || hi <= lo) return;
    Module *m = &modules[module_count++];
    m->lo = lo, m->hi = hi, m->base = base;
    const char *b = name;
    for (const char *p = name; *p; p++) if (*p == '/' || *p == '\\') b = p + 1;
    clean(m->name, sizeof m->name, *b ? b : "module");
}
static void write_pc(Record f, unsigned k, uintptr_t pc) {
    for (unsigned i = 0; i < module_count; i++) {
        const Module *m = &modules[i];
        if (pc >= m->lo && pc < m->hi) {
            put(f, "  %u %s+0x%llx\n", k, m->name, (unsigned long long)(pc - m->base));
            return;
        }
    }
    put(f, "  %u unknown (address omitted)\n", k);
}

#ifndef _WIN32
#define STALL_SIGNAL (SIGRTMIN + 4)
static int signal_ready;
static pid_t process_id;
static void on_stack(int sig, siginfo_t *si, void *context) {
    (void)sig;
    const int saved = errno;
    if (si->si_code != SI_QUEUE || si->si_pid != process_id) goto done;
    const unsigned slot = (unsigned)si->si_value.sival_int;
    if (slot >= HOS_STALL_CAP * MAX_THREADS) goto done;
    ThreadStack *s = &stacks[slot / MAX_THREADS][slot % MAX_THREADS];
    if (s->tid != (uint32_t)syscall(SYS_gettid) || __atomic_load_n(&s->done, __ATOMIC_RELAXED)) goto done;
#if defined(__x86_64__)
    const ucontext_t *u = (const ucontext_t *)context;
    uintptr_t fp = (uintptr_t)u->uc_mcontext.gregs[REG_RBP];
    const uintptr_t sp = (uintptr_t)u->uc_mcontext.gregs[REG_RSP];
    s->pc[0] = (uintptr_t)u->uc_mcontext.gregs[REG_RIP];
    s->count = 1;
    while (s->count < MAX_FRAMES && fp >= sp && fp - sp < (8u << 20) && !(fp & (sizeof(uintptr_t) - 1))) {
        uintptr_t words[2];
        struct iovec local = {words, sizeof words}, remote = {(void *)fp, sizeof words};
        if (syscall(SYS_process_vm_readv, process_id, &local, 1, &remote, 1, 0) != (long)sizeof words) break;
        if (!words[1] || words[0] <= fp) break;
        s->pc[s->count++] = words[1];
        fp = words[0];
    }
#else
    (void)context;
#endif
    __atomic_store_n(&s->done, 1, __ATOMIC_RELEASE);
done:
    errno = saved;
}
static void collection_init(void) {
    struct sigaction old, sa;
    process_id = getpid();
    if (STALL_SIGNAL > SIGRTMAX || sigaction(STALL_SIGNAL, NULL, &old) || old.sa_handler != SIG_DFL) return;
    memset(&sa, 0, sizeof sa);
    sa.sa_sigaction = on_stack;
    sa.sa_flags = SA_SIGINFO | SA_RESTART;
    sigemptyset(&sa.sa_mask);
    if (sigaction(STALL_SIGNAL, &sa, NULL)) return;
    sigset_t set;
    sigemptyset(&set);
    sigaddset(&set, STALL_SIGNAL);
    pthread_sigmask(SIG_UNBLOCK, &set, NULL);
    signal_ready = 1;
}
static void read_modules(void) {
    /* No dladdr/dl_iterate_phdr: the stopped code may hold the loader's lock. */
    module_count = 0;
    int fd = open("/proc/self/maps", O_RDONLY | O_CLOEXEC);
    if (fd < 0) return;
    char line[8192], buf[8192];
    size_t at = 0;
    ssize_t n;
    while ((n = read(fd, buf, sizeof buf)) > 0) for (ssize_t i = 0; i < n; i++) {
        if (buf[i] != '\n') { if (at + 1 < sizeof line) line[at++] = buf[i]; continue; }
        line[at] = 0;
        unsigned long long lo, hi, offset;
        char perms[5];
        int used = 0;
        if (sscanf(line, "%llx-%llx %4s %llx %*s %*u %n", &lo, &hi, perms, &offset, &used) == 4 && used && strchr(perms, 'x'))
            add_module((uintptr_t)lo, (uintptr_t)hi, (uintptr_t)(lo - offset), line + used);
        at = 0;
    }
    close(fd);
}
struct DirEntry { uint64_t ino; int64_t off; unsigned short reclen; unsigned char type; char name[]; };
static unsigned take_stacks(unsigned generation) {
    if (!signal_ready) return 0;
    int fd = open("/proc/self/task", O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (fd < 0) return 0;
    char entries[8192];
    unsigned count = 0;
    long bytes;
    while ((bytes = syscall(SYS_getdents64, fd, entries, sizeof entries)) > 0) {
        for (long at = 0; at < bytes;) {
            struct DirEntry *e = (struct DirEntry *)(void *)(entries + at);
            if (!e->reclen || at + e->reclen > bytes) break;
            at += e->reclen;
            const uint32_t tid = (uint32_t)strtoul(e->name, NULL, 10);
            if (!tid || count == MAX_THREADS) continue;
            ThreadStack *s = &stacks[generation][count++];
            s->tid = tid;
            char path[80], name[96] = "unnamed";
            snprintf(path, sizeof path, "/proc/self/task/%u/comm", tid);
            const int nf = open(path, O_RDONLY | O_CLOEXEC);
            if (nf >= 0) {
                const ssize_t n = read(nf, name, sizeof name - 1);
                if (n > 0) name[n] = 0, name[strcspn(name, "\n")] = 0;
                close(nf);
            }
            thread_label(s, name);
            siginfo_t si;
            memset(&si, 0, sizeof si);
            si.si_signo = STALL_SIGNAL, si.si_code = SI_QUEUE, si.si_pid = process_id, si.si_uid = getuid();
            si.si_value.sival_int = (int)(generation * MAX_THREADS + count - 1);
            syscall(SYS_rt_tgsigqueueinfo, process_id, tid, STALL_SIGNAL, &si);
        }
    }
    close(fd);
    const uint64_t until = now_ms() + 500;
    for (;;) {
        unsigned done = 0;
        for (unsigned i = 0; i < count; i++) done += __atomic_load_n(&stacks[generation][i].done, __ATOMIC_ACQUIRE) != 0;
        if (done == count || now_ms() >= until) break;
        sleep_ms(1);
    }
    return count;
}
static Record new_file(const char *name) {
    const int root = open(folder, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (root < 0) return NO_RECORD;
    mkdirat(root, "bug-reports", 0700);
    const int reports = openat(root, "bug-reports", O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
    close(root);
    if (reports < 0) return NO_RECORD;
    mkdirat(reports, "pending", 0700);
    const int pending = openat(reports, "pending", O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
    close(reports);
    if (pending < 0) return NO_RECORD;
    const int file = openat(pending, name, O_WRONLY | O_CREAT | O_EXCL | O_APPEND | O_NOFOLLOW | O_CLOEXEC, 0600);
    close(pending);
    return file;
}
#else
/* One fixed stack snapshot reused only after its walk, never while a thread is suspended. */
static unsigned char stack_copy[256 * 1024];
static DWORD64 copy_base;
static SIZE_T copy_size;
static void collection_init(void) {}
static void read_modules(void) {
    module_count = 0;
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE, GetCurrentProcessId());
    if (snap == INVALID_HANDLE_VALUE) return;
    MODULEENTRY32W m;
    memset(&m, 0, sizeof m);
    m.dwSize = sizeof m;
    for (BOOL ok = Module32FirstW(snap, &m); ok; ok = Module32NextW(snap, &m)) {
        char name[512];
        if (!WideCharToMultiByte(CP_UTF8, 0, m.szModule, -1, name, sizeof name, NULL, NULL)) strcpy(name, "module");
        add_module((uintptr_t)m.modBaseAddr, (uintptr_t)m.modBaseAddr + m.modBaseSize, (uintptr_t)m.modBaseAddr, name);
    }
    CloseHandle(snap);
}
static BOOL CALLBACK walk_read(HANDLE process, DWORD64 address, PVOID out, DWORD bytes, LPDWORD got) {
    *got = 0;
    if (address >= copy_base && address - copy_base <= copy_size && bytes <= copy_size - (SIZE_T)(address - copy_base)) {
        memcpy(out, stack_copy + (SIZE_T)(address - copy_base), bytes);
        *got = bytes;
        return TRUE;
    }
    /* Unwind metadata only. Never fall back to a thread's now-changing stack or guest heap. */
    for (unsigned i = 0; i < module_count; i++) if (address >= modules[i].lo && address < modules[i].hi && bytes <= modules[i].hi - address) {
        SIZE_T read = 0;
        const BOOL ok = ReadProcessMemory(process, (void *)(uintptr_t)address, out, bytes, &read);
        *got = (DWORD)read;
        return ok;
    }
    return FALSE;
}
static PVOID CALLBACK walk_table(HANDLE process, DWORD64 pc) {
    (void)process;
    DWORD64 base;
    return RtlLookupFunctionEntry(pc, &base, NULL);
}
static DWORD64 CALLBACK walk_base(HANDLE process, DWORD64 pc) {
    (void)process;
    for (unsigned i = 0; i < module_count; i++) if (pc >= modules[i].lo && pc < modules[i].hi) return modules[i].base;
    return 0;
}
static void capture_thread(HANDLE h, ThreadStack *s) {
    CONTEXT ctx;
    memset(&ctx, 0, sizeof ctx);
    ctx.ContextFlags = CONTEXT_FULL;
    copy_size = 0;
    if (SuspendThread(h) == (DWORD)-1) return;
    const BOOL got = GetThreadContext(h, &ctx);
    if (got) {
        copy_base = ctx.Rsp;
        /* Read in pages: a stack's guard/uncommitted page must not discard the readable prefix. */
        while (copy_size < sizeof stack_copy) {
            SIZE_T n = 0;
            SIZE_T chunk = 4096 - ((copy_base + copy_size) & 4095);
            if (chunk > sizeof stack_copy - copy_size) chunk = sizeof stack_copy - copy_size;
            if (!ReadProcessMemory(GetCurrentProcess(), (void *)(uintptr_t)(copy_base + copy_size),
                                   stack_copy + copy_size, chunk, &n) || !n) break;
            copy_size += n;
        }
    }
    const DWORD resumed = ResumeThread(h); /* All exits from the suspended interval pass here. */
    if (resumed == (DWORD)-1 || !got) return;
    s->pc[s->count++] = (uintptr_t)ctx.Rip;
    STACKFRAME64 frame;
    memset(&frame, 0, sizeof frame);
    frame.AddrPC.Offset = ctx.Rip, frame.AddrStack.Offset = ctx.Rsp, frame.AddrFrame.Offset = ctx.Rbp;
    frame.AddrPC.Mode = frame.AddrStack.Mode = frame.AddrFrame.Mode = AddrModeFlat;
    while (s->count < MAX_FRAMES && StackWalk64(IMAGE_FILE_MACHINE_AMD64, GetCurrentProcess(), h, &frame, &ctx,
                                             walk_read, walk_table, walk_base, NULL)) {
        if (!frame.AddrPC.Offset) break;
        if (frame.AddrPC.Offset == s->pc[s->count - 1] && frame.AddrStack.Offset == copy_base) break;
        s->pc[s->count++] = (uintptr_t)frame.AddrPC.Offset;
    }
    memset(stack_copy, 0, sizeof stack_copy);
    s->done = 1;
}
static unsigned take_stacks(unsigned generation) {
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (snap == INVALID_HANDLE_VALUE) return 0;
    const DWORD pid = GetCurrentProcessId(), self = GetCurrentThreadId();
    THREADENTRY32 te;
    memset(&te, 0, sizeof te);
    te.dwSize = sizeof te;
    unsigned count = 0;
    for (BOOL ok = Thread32First(snap, &te); ok; ok = Thread32Next(snap, &te)) {
        if (te.th32OwnerProcessID != pid || count == MAX_THREADS) continue;
        ThreadStack *s = &stacks[generation][count++];
        s->tid = te.th32ThreadID;
        thread_label(s, "");
        HANDLE h = OpenThread(THREAD_QUERY_LIMITED_INFORMATION | THREAD_GET_CONTEXT | THREAD_SUSPEND_RESUME, FALSE, s->tid);
        if (!h) continue;
        PWSTR name = NULL;
        if (SUCCEEDED(GetThreadDescription(h, &name)) && name) {
            char utf8[384];
            if (WideCharToMultiByte(CP_UTF8, 0, name, -1, utf8, sizeof utf8, NULL, NULL)) thread_label(s, utf8);
            LocalFree(name);
        }
        if (s->tid != self) capture_thread(h, s);
        else {
            s->count = CaptureStackBackTrace(0, MAX_FRAMES, (void **)s->pc, NULL);
            s->done = 1;
        }
        CloseHandle(h);
    }
    CloseHandle(snap);
    return count;
}
static Record new_file(const char *name) {
    char path[4352];
    wchar_t wide[4352];
    if (snprintf(path, sizeof path, "%s/bug-reports", folder) >= (int)sizeof path ||
        !MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path, -1, wide, 4352)) return NO_RECORD;
    CreateDirectoryW(wide, NULL);
    DWORD attrs = GetFileAttributesW(wide);
    if (attrs == INVALID_FILE_ATTRIBUTES || !(attrs & FILE_ATTRIBUTE_DIRECTORY) || (attrs & FILE_ATTRIBUTE_REPARSE_POINT)) return NO_RECORD;
    strcat(path, "/pending");
    if (!MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path, -1, wide, 4352)) return NO_RECORD;
    CreateDirectoryW(wide, NULL);
    attrs = GetFileAttributesW(wide);
    if (attrs == INVALID_FILE_ATTRIBUTES || !(attrs & FILE_ATTRIBUTE_DIRECTORY) || (attrs & FILE_ATTRIBUTE_REPARSE_POINT)) return NO_RECORD;
    const size_t at = strlen(path);
    if (snprintf(path + at, sizeof path - at, "/%s", name) >= (int)(sizeof path - at) ||
        !MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path, -1, wide, 4352)) return NO_RECORD;
    return CreateFileW(wide, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_DELETE, NULL, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL);
}
#endif

static int write_record(HosStallDetector *d, uint64_t now) {
    if (collection >= HOS_STALL_CAP) return 0;
    const time_t wall = time(NULL);
    struct tm tm;
#ifdef _WIN32
    if (gmtime_s(&tm, &wall)) return 0;
#else
    if (!gmtime_r(&wall, &tm)) return 0;
#endif
    char stamp[32], name[80], stack_name[80];
    if (!strftime(stamp, sizeof stamp, "%Y%m%dT%H%M%SZ", &tm)) return 0;
    snprintf(name, sizeof name, "stall-%s.txt", stamp);
    snprintf(stack_name, sizeof stack_name, "stall-%s.stacks.txt", stamp);
    close_record(active_record);
    active_record = new_file(name);
    if (active_record == NO_RECORD) return 0;
    const int long_tier = d->flags & (HOS_STALL_UNKNOWN | HOS_STALL_LOADING | HOS_STALL_PREPARING);
    const int64_t last_log = hos_stall_last_log_ns();
    const int64_t last_present = hos_stall_last_present_ns();
    put(active_record, "Hookshot stall record 1\nversion: %s\nwhen: %s\nuptime_s: %llu\n"
        "reason: %s\nthread: watchdog\nmodule_offset: unknown\nstate: %s\n"
        "timeout_s: %u\npresents: %llu\n", version_text, stamp, (unsigned long long)(age(now, started_ms) / 1000),
        long_tier ? "no present or loading progress" : "no present", long_tier ? "loading-or-unknown" : "gameplay",
        (long_tier ? HOS_STALL_LONG_MS : HOS_STALL_NORMAL_MS) / 1000, (unsigned long long)d->frame);
    put(active_record, "run_id: %s\n", run_id);
    if (last_present > 0) put(active_record, "last_present_age_ms: %llu\n", (unsigned long long)age(now, (uint64_t)last_present / 1000000));
    else put(active_record, "last_present_age_ms: unknown\n");
    if (last_log > 0) put(active_record, "last_log_age_ms: %llu\n", (unsigned long long)age(now, (uint64_t)last_log / 1000000));
    else put(active_record, "last_log_age_ms: unknown\n");
    const uint64_t log_progress = __atomic_load_n(&progress_at_ms, __ATOMIC_RELAXED);
    if (log_progress) put(active_record, "log_progress_age_ms: %llu\n", (unsigned long long)age(now, log_progress));
    else put(active_record, "log_progress_age_ms: unknown\n");
    put(active_record, "vcore_progress_age_ms: %llu\nlog: hookshot_log.txt (not copied)\n",
        (unsigned long long)age(now, d->core_progress_ms));
    log_line("[hos host] STALL: no presents; local record in bug-reports/pending (nothing sent)\n");
    const unsigned generation = collection++;
    if (!collect_stacks) { put(active_record, "stacks: not collected (switch off)\n"); return 1; }
    Record out = new_file(stack_name);
    if (out == NO_RECORD) { put(active_record, "stacks: unavailable\n"); return 1; }
    put(active_record, "stacks: %s\n", stack_name);
    /* Write the text record before sampling: even a failed collector leaves useful evidence. */
    read_modules();
    const unsigned n = take_stacks(generation);
    put(out, "Hookshot stall stacks 1\nthreads: %u (limit %u; best effort, not simultaneous)\n", n, MAX_THREADS);
    uint32_t owners[8] = {0};
    const uint64_t progress = hos_stall_vcores(owners);
    if (progress == UINT64_MAX) put(out, "virtual cores: unavailable (lock busy)\n");
    else for (unsigned i = 0; i < 8; i++) put(out, "virtual core %u: host thread %u\n", i, owners[i]);
    for (unsigned i = 0; i < n; i++) {
        ThreadStack *s = &stacks[generation][i];
        const int done = __atomic_load_n(&s->done, __ATOMIC_ACQUIRE);
        put(out, "\nthread %u \"%s\" (%s)\n", s->tid, s->name, done && s->count ? "partial stack" : "unavailable or watchdog");
        if (done) for (unsigned k = 0; k < s->count; k++) write_pc(out, k, s->pc[k]);
    }
    close_record(out);
    log_line("[hos host] STALL: thread collection finished (partial stacks are marked)\n");
    return 1;
}
static void record_resumed(uint64_t seconds) {
    if (active_record == NO_RECORD) return;
    put(active_record, "resumed: yes, after %llu s\n", (unsigned long long)seconds);
    close_record(active_record), active_record = NO_RECORD;
    char line[128];
    snprintf(line, sizeof line, "[hos host] STALL: resumed: yes, after %llu s\n", (unsigned long long)seconds);
    log_line(line);
}
int hos_stall_focused(void) {
    if (headless) return 1;
#ifdef _WIN32
    HWND window = GetForegroundWindow();
    DWORD owner = 0;
    if (window) GetWindowThreadProcessId(window, &owner);
    return owner == GetCurrentProcessId() && !IsIconic(window);
#else
    extern volatile int hos_posix_window[4];
    return __atomic_load_n(&hos_posix_window[0], __ATOMIC_RELAXED) != 0;
#endif
}
static unsigned current_flags(uint64_t now) {
    unsigned flags = __atomic_load_n(&game_flags, __ATOMIC_RELAXED);
    if (__atomic_load_n(&gpu_claim_until_ms, __ATOMIC_RELAXED) > now) flags |= HOS_STALL_GPU_LOSS;
    if (!hos_stall_focused()) flags |= HOS_STALL_UNFOCUSED;
    const int64_t menu = hos_stall_menu_seen_ns(), movie = hos_stall_movie_seen_ns();
    if (menu > 0) flags |= age(now, (uint64_t)menu / 1000000) <= 2000 ? HOS_STALL_PAUSED : HOS_STALL_UNKNOWN;
    if (movie > 0 && age(now, (uint64_t)movie / 1000000) <= 2000) flags |= HOS_STALL_MOVIE;
    return flags;
}
#ifdef _WIN32
static DWORD WINAPI watchdog(void *unused) {
#else
static void *watchdog(void *unused) {
#endif
    (void)unused;
    HosStallDetector detector = {0};
    uint64_t stall_started = 0;
    for (;;) {
        sleep_ms(1000);
        const uint64_t frame = (uint64_t)__atomic_load_n(&hos_present_count, __ATOMIC_RELAXED);
        const uint64_t now = now_ms();
        /* Normal play: one sleep and one existing counter read; no thread scan, lock or file access. */
        if (detector.initialized && frame != detector.frame && !detector.active) {
            detector.frame = frame, detector.last_tick_ms = detector.last_present_ms = detector.eligible_ms = now;
            continue;
        }
        uint32_t owners[8];
        const uint64_t progress = hos_stall_vcores(owners);
        const uint64_t before = detector.last_present_ms;
        const int action = hos_stall_tick(&detector, now, frame, current_flags(now),
                                         __atomic_load_n(&progress_at_ms, __ATOMIC_RELAXED), progress);
        if (action == HOS_STALL_BEGIN) {
            stall_started = before;
            if (!write_record(&detector, now)) log_line("[hos host] STALL: local record could not be created\n");
        }
        if (action == HOS_STALL_RESUMED) record_resumed(age(now, stall_started) / 1000);
    }
#ifdef _WIN32
    return 0;
#else
    return NULL;
#endif
}
static int setup(const char *game_folder, const char *version) {
    if (strlen(game_folder) >= sizeof folder) return 0;
    strcpy(folder, game_folder);
    char user[256] = "", host[256] = "";
    const char *home;
#ifdef _WIN32
    DWORD user_size = sizeof user, host_size = sizeof host;
    home = getenv("USERPROFILE");
    const int identities = GetUserNameA(user, &user_size) && GetComputerNameA(host, &host_size);
#else
    struct passwd pwd, *found = NULL;
    char account[8192];
    const int have_user = !getpwuid_r(getuid(), &pwd, account, sizeof account, &found) && found;
    if (have_user) snprintf(user, sizeof user, "%s", found->pw_name);
    home = getenv("HOME");
    if (!home && have_user) home = found->pw_dir;
    const int identities = have_user && !gethostname(host, sizeof host);
    host[sizeof host - 1] = 0;
#endif
    hos_report_privacy_init(&privacy, home, user, host);
    if (!identities) privacy.incomplete = 1;
    clean(version_text, sizeof version_text, version);
    run_id = hos_report_run_id();
    started_ms = now_ms();
    headless = env_on("HOS_HEADLESS");
#ifdef _WIN32
    collect_stacks = env_on("HOS_STALL_STACKS"); /* Until Windows runtime evidence: text records only by default. */
#else
    collect_stacks = !getenv("HOS_STALL_STACKS") || env_on("HOS_STALL_STACKS");
#endif
    if (collect_stacks) collection_init();
    return 1;
}
void hos_stall_start(const char *game_folder, const char *version) {
    if ((getenv("HOS_STALL_WATCHDOG") && !env_on("HOS_STALL_WATCHDOG")) || !setup(game_folder, version)) return;
    const char *test = getenv("HOS_STALL_TEST_BLOCK_FRAME");
    if (test && *test) {
        char *end;
        block_at = strtoull(test, &end, 10);
        if (*end || !block_at) block_at = 0;
        block_seconds = 40;
    }
    __atomic_store_n(&enabled, 1, __ATOMIC_RELAXED);
#ifdef _WIN32
    HANDLE thread = CreateThread(NULL, 0, watchdog, NULL, 0, NULL);
    if (thread) CloseHandle(thread);
#else
    pthread_t thread;
    if (!pthread_create(&thread, NULL, watchdog, NULL)) pthread_detach(thread);
#endif
}
#ifdef HOS_STALL_TEST
int hos_stall_test_record(const char *game_folder, const char *version, unsigned flags) {
    if (!setup(game_folder, version)) return 0;
    HosStallDetector d = {0};
    d.flags = flags, d.frame = 100;
    return write_record(&d, now_ms());
}
void hos_stall_test_resume(unsigned seconds) { record_resumed(seconds); }
#endif
