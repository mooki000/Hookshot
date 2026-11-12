// (hookshot) Optional telemetry. Off: no worker, clock, file, sensor or library work; these guards are the only frame-path cost.
#pragma once
#include <atomic>
#include <cstdint>

namespace mkwvk {
extern std::atomic<bool> perf_collecting;
inline bool perf_on() { return perf_collecting.load(std::memory_order_relaxed); }
void perf_start();
void perf_stop();
void perf_game_present();
void perf_replay_present(int64_t present, int64_t gpu_ns, int64_t wait_ns, bool gpu_timing);
void perf_texture_budget(uint64_t used, uint64_t allowed, uint64_t textures, uint64_t budget, bool known);
void perf_present(int64_t ns, double ms, int64_t frame, double time_s);
} // namespace mkwvk
