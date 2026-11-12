/* A local, bounded record when presents stop. No renderer ABI and no game memory in the files. */
#ifndef HOS_STALL_H
#define HOS_STALL_H
#include <stdint.h>
#include <stddef.h>

enum {
    HOS_STALL_UNKNOWN = 1, HOS_STALL_LOADING = 2, HOS_STALL_PREPARING = 4,
    HOS_STALL_MOVIE = 8, HOS_STALL_PAUSED = 16, HOS_STALL_UNFOCUSED = 32,
    HOS_STALL_EXITING = 64, HOS_STALL_GPU_LOSS = 128,
    HOS_STALL_NORMAL_MS = 30000, HOS_STALL_LONG_MS = 900000, HOS_STALL_CAP = 3
};
enum { HOS_STALL_NONE, HOS_STALL_BEGIN, HOS_STALL_RESUMED };
typedef struct {
    uint64_t frame, last_tick_ms, last_present_ms, eligible_ms, progress_ms, core_progress, core_progress_ms;
    unsigned flags, records;
    int initialized, active;
} HosStallDetector;

/* Pure policy, also used with a fake clock. A scheduling gap of >5 s disarms the interval
   (suspend/resume or a machine too busy to sample). Progress postpones only the long tier. */
int hos_stall_tick(HosStallDetector *d, uint64_t now_ms, uint64_t frame, unsigned flags,
                   uint64_t log_progress_ms, uint64_t core_progress);
/* Only known numeric preparation counters qualify. Repeated counts are not progress. */
int hos_stall_progress(const char *line, unsigned *stage, uint64_t *done);
int hos_stall_gpu_wait(const char *line, uint64_t *wait_ms);

void hos_stall_start(const char *game_folder, const char *version);
void hos_stall_present(int64_t now_ns);
void hos_stall_game_state(unsigned flags);
void hos_stall_log(const char *line, int64_t now_ns);
int64_t hos_stall_last_present_ns(void);

/* Snapshots supplied by the host. They do not wait on game locks. A core ID is a host TID,
   not guest registers or pointers; UINT64_MAX progress means the lock was busy. */
uint64_t hos_stall_vcores(uint32_t owners[8]);
int64_t hos_stall_last_log_ns(void);
int64_t hos_stall_menu_seen_ns(void);   /* -1: platform has no menu observer */
int64_t hos_stall_movie_seen_ns(void);
int hos_stall_focused(void);

#ifdef HOS_STALL_TEST
/* The same collector/writer as the watchdog, without a timed game launch. */
int hos_stall_test_record(const char *folder, const char *version, unsigned flags);
void hos_stall_test_resume(unsigned seconds);
#endif
#endif
