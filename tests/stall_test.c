/* A real multithreaded child, no game or GPU. The blocked thread must be sampled in
   read/NtWaitForSingleObject, not in the sampler. Other threads remain live. */
#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "hos_stall.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
static HANDLE block_event;
#else
#include <dlfcn.h>
#include <pthread.h>
#include <time.h>
#include <sys/syscall.h>
#include <unistd.h>
static int block_pipe[2];
volatile int hos_posix_window[4] = {1, 0, 0, 0};
#endif
volatile int64_t hos_present_count;
static uint32_t blocked_tid;
static unsigned other_ticks, stop_other;
int64_t hos_stall_last_log_ns(void) { return 0; }
int64_t hos_stall_menu_seen_ns(void) { return 0; }
int64_t hos_stall_movie_seen_ns(void) { return 0; }
uint64_t hos_stall_vcores(uint32_t owners[8]) { memset(owners, 0, 8 * sizeof *owners); return 0; }
static uint64_t clock_ms(void) {
#ifdef _WIN32
    return GetTickCount64();
#else
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (uint64_t)t.tv_sec * 1000 + (uint64_t)t.tv_nsec / 1000000;
#endif
}
int64_t hos_now_ns(void) { return (int64_t)clock_ms() * 1000000; }
static void pause_ms(unsigned ms) {
#ifdef _WIN32
    Sleep(ms);
#else
    struct timespec t = {(time_t)(ms / 1000), (long)(ms % 1000) * 1000000};
    nanosleep(&t, NULL);
#endif
}
#ifdef _WIN32
static DWORD WINAPI blocked(void *unused) {
    (void)unused;
    SetThreadDescription(GetCurrentThread(), L"mkw_vk worker");
    __atomic_store_n(&blocked_tid, GetCurrentThreadId(), __ATOMIC_RELEASE);
    WaitForSingleObject(block_event, INFINITE);
    return 0;
}
static DWORD WINAPI other(void *unused) {
    (void)unused;
    SetThreadDescription(GetCurrentThread(), L"private-user@example.test");
    while (!__atomic_load_n(&stop_other, __ATOMIC_RELAXED)) {
        __atomic_fetch_add(&other_ticks, 1, __ATOMIC_RELAXED);
        pause_ms(1);
    }
    return 0;
}
#else
static void *blocked(void *unused) {
    (void)unused;
    pthread_setname_np(pthread_self(), "mkw_vk worker");
    __atomic_store_n(&blocked_tid, (uint32_t)syscall(SYS_gettid), __ATOMIC_RELEASE);
    char byte;
    const ssize_t n = syscall(SYS_read, block_pipe[0], &byte, 1);
    assert(n == 1);
    return NULL;
}
static void *other(void *unused) {
    (void)unused;
    pthread_setname_np(pthread_self(), "private-user");
    while (!__atomic_load_n(&stop_other, __ATOMIC_RELAXED)) {
        __atomic_fetch_add(&other_ticks, 1, __ATOMIC_RELAXED);
        pause_ms(1);
    }
    return NULL;
}
#endif
static void policy(void) {
    unsigned cases = 0;
    HosStallDetector d = {0};
    for (uint64_t ms = 0; ms < HOS_STALL_NORMAL_MS; ms += 1000)
        assert(hos_stall_tick(&d, ms, 1, 0, 0, 0) == HOS_STALL_NONE);
    assert(hos_stall_tick(&d, HOS_STALL_NORMAL_MS, 1, 0, 0, 0) == HOS_STALL_BEGIN);
    assert(hos_stall_tick(&d, 31000, 1, 0, 0, 0) == HOS_STALL_NONE);
    assert(hos_stall_tick(&d, 32000, 2, 0, 0, 0) == HOS_STALL_RESUMED); cases++;
    const unsigned excluded[] = {HOS_STALL_MOVIE, HOS_STALL_PAUSED, HOS_STALL_UNFOCUSED, HOS_STALL_EXITING, HOS_STALL_GPU_LOSS};
    for (unsigned i = 0; i < sizeof excluded / sizeof *excluded; i++) {
        memset(&d, 0, sizeof d);
        for (uint64_t ms = 0; ms <= 3600000; ms += 1000)
            assert(hos_stall_tick(&d, ms, 1, excluded[i], 0, 0) == HOS_STALL_NONE);
        assert(!d.records); cases++;
    }
    const unsigned long_states[] = {HOS_STALL_LOADING, HOS_STALL_PREPARING, HOS_STALL_UNKNOWN};
    for (unsigned i = 0; i < sizeof long_states / sizeof *long_states; i++) {
        memset(&d, 0, sizeof d);
        for (uint64_t ms = 0; ms < HOS_STALL_LONG_MS; ms += 1000)
            assert(hos_stall_tick(&d, ms, 0, long_states[i], 0, 0) == HOS_STALL_NONE);
        assert(hos_stall_tick(&d, HOS_STALL_LONG_MS, 0, long_states[i], 0, 0) == HOS_STALL_BEGIN); cases++;
        memset(&d, 0, sizeof d);
        for (uint64_t ms = 0; ms <= 6 * 3600000; ms += 1000)
            assert(hos_stall_tick(&d, ms, 0, long_states[i], ms / 10000 * 10000, 0) == HOS_STALL_NONE);
        assert(!d.records); cases++;
        memset(&d, 0, sizeof d);
        for (uint64_t ms = 0; ms <= 3600000; ms += 1000)
            assert(hos_stall_tick(&d, ms, 0, long_states[i], 0, ms / 15000) == HOS_STALL_NONE);
        assert(!d.records); cases++;
    }
    memset(&d, 0, sizeof d);
    for (uint64_t ms = 0; ms <= 3600000; ms += 1000)
        assert(hos_stall_tick(&d, ms, ms / 29000 + 1, 0, 0, 0) == HOS_STALL_NONE);
    assert(!d.records); cases++;
    memset(&d, 0, sizeof d);
    unsigned starts = 0, resumes = 0;
    for (uint64_t ms = 0; ms <= 3600000; ms += 1000) {
        const int action = hos_stall_tick(&d, ms, ms / 32000 + 1, 0, 0, 0);
        starts += action == HOS_STALL_BEGIN;
        resumes += action == HOS_STALL_RESUMED;
    }
    assert(starts == HOS_STALL_CAP && resumes == HOS_STALL_CAP); cases++;
    memset(&d, 0, sizeof d);
    assert(!hos_stall_tick(&d, 0, 1, 0, 0, 0));
    assert(!hos_stall_tick(&d, 3600000, 1, 0, 0, 0));
    assert(!hos_stall_tick(&d, 2000, 1, 0, 0, 0)); cases++;
    unsigned stage;
    uint64_t done;
    assert(hos_stall_progress("vk: start-up: shaders 123 of 456, 27% of all", &stage, &done) && done == 123);
    assert(hos_stall_progress("vk: noted pipelines: 1403 of 10000 pipelines", &stage, &done) && done == 1403);
    assert(hos_stall_progress("vk: prepared 38809 shader programs in play", &stage, &done) && done == 38809);
    const char *bad[] = {"hid: 36864 vibration values", "vcores: 5017 takes", "audio: alive", "vk: start-up: waiting",
                         "vk: start-up: 1234567890123 of 1", "vk: start-up: secret of 1"};
    for (unsigned i = 0; i < sizeof bad / sizeof *bad; i++) assert(!hos_stall_progress(bad[i], &stage, &done));
    cases++;
    printf("PASS: %u detector scenarios (fake clock, six-hour progress, exclusions, recovery, cap, slow frames)\n", cases);
    uint64_t wait_ms;
    assert(hos_stall_gpu_wait("vk: the GPU has finished nothing for 10 s: the timeline at 120, 124 sent (RX_VK_GPU_HANG_SEC=30)", &wait_ms) && wait_ms == 30000);
    assert(hos_stall_gpu_wait("vk: the GPU has finished nothing for 10 s: the timeline at 120, 124 sent (RX_VK_GPU_HANG_SEC=0)", &wait_ms) && wait_ms == 0);
    assert(hos_stall_gpu_wait("vk: GPU hang: the timeline at 120, 124 sent", &wait_ms) && wait_ms == UINT64_MAX);
    assert(hos_stall_gpu_wait("vk: device lost: reading the checkpoints", &wait_ms) && wait_ms == UINT64_MAX);
    assert(!hos_stall_gpu_wait("vk: the GPU has finished nothing for secret", &wait_ms));
    puts("PASS: GPU watcher positive timeout, log-only mode, terminal hang, and direct device loss remain its own path");
}
int main(int argc, char **argv) {
    assert(argc >= 2);
    if (!strcmp(argv[1], "policy")) { policy(); return 0; }
    assert(argc == 3);
    if (!strcmp(argv[1], "slow")) {
        hos_stall_game_state(0);
        hos_stall_start(argv[2], "test-version");
        for (unsigned i = 0; i < 32; i++) {
            __atomic_fetch_add(&hos_present_count, 1, __ATOMIC_RELAXED);
            hos_stall_present((int64_t)clock_ms() * 1000000);
            pause_ms(1000);
        }
        puts("PASS: real child, one present a second for 32 seconds; watchdog stayed quiet");
        return 0;
    }
    if (!strcmp(argv[1], "text")) {
        assert(hos_stall_test_record(argv[2], "test-version", 0));
        hos_stall_test_resume(40);
        puts("PASS: text record and recovery");
        return 0;
    }
#ifdef _WIN32
    block_event = CreateEventW(NULL, TRUE, FALSE, NULL);
    assert(block_event);
    HANDLE a = CreateThread(NULL, 0, blocked, NULL, 0, NULL), b = CreateThread(NULL, 0, other, NULL, 0, NULL);
    assert(a && b);
    HMODULE module = GetModuleHandleW(L"ntdll.dll");
    const uintptr_t function = (uintptr_t)(void *)GetProcAddress(module, "NtWaitForSingleObject");
    printf("MODULE=ntdll.dll OFFSET=%llx\n", (unsigned long long)(function - (uintptr_t)module));
#else
    assert(!pipe(block_pipe));
    pthread_t a, b;
    assert(!pthread_create(&a, NULL, blocked, NULL) && !pthread_create(&b, NULL, other, NULL));
    Dl_info info;
    assert(dladdr((void *)(uintptr_t)&syscall, &info));
    const char *name = strrchr(info.dli_fname, '/');
    printf("MODULE=%s OFFSET=%llx\n", name ? name + 1 : info.dli_fname,
           (unsigned long long)((uintptr_t)(void *)&syscall - (uintptr_t)info.dli_fbase));
#endif
    while (!__atomic_load_n(&blocked_tid, __ATOMIC_ACQUIRE)) pause_ms(1);
    pause_ms(50);
    printf("BLOCKED=%u\n", blocked_tid);
    const unsigned before = __atomic_load_n(&other_ticks, __ATOMIC_RELAXED);
    assert(hos_stall_test_record(argv[2], "test-version", 0));
    hos_stall_test_resume(40);
    pause_ms(20);
    assert(__atomic_load_n(&other_ticks, __ATOMIC_RELAXED) > before);
    __atomic_store_n(&stop_other, 1, __ATOMIC_RELAXED);
#ifdef _WIN32
    SetEvent(block_event);
    assert(WaitForSingleObject(a, 5000) == WAIT_OBJECT_0 && WaitForSingleObject(b, 5000) == WAIT_OBJECT_0);
    CloseHandle(a), CloseHandle(b), CloseHandle(block_event);
#else
    assert(write(block_pipe[1], "x", 1) == 1);
    assert(!pthread_join(a, NULL) && !pthread_join(b, NULL));
    close(block_pipe[0]), close(block_pipe[1]);
#endif
    puts("PASS: real blocked thread sampled; other thread stayed live and both joined");
    return 0;
}
