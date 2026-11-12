// (hookshot) Fixed-size hand-off from the renderer; sensor reads, formatting and file I/O belong to one worker.
#pragma once
#include "perf_monitor.h"
#include <atomic>
#include <condition_variable>
#include <mutex>
#include <thread>

namespace mkwvk::perf_monitor {
struct FrameSample {
    int64_t at_ns = 0, frame = -1, renderer_present = -1, game_frame = -1;
    double time_s = -1, ms = 0;
};
template <size_t N> class FrameQueue {
    static_assert(N > 1 && (N & (N - 1)) == 0);
    std::array<FrameSample, N> entries{};
    std::atomic<uint64_t> head{0}, tail{0}, lost{0};
public:
    // One producer (present_out, or the headless replay), one sampler consumer. Never waits or allocates.
    bool push(const FrameSample& frame) {
        const uint64_t h = head.load(std::memory_order_relaxed);
        if (h - tail.load(std::memory_order_acquire) == N) {
            lost.fetch_add(1, std::memory_order_relaxed);
            return false;
        }
        entries[h & (N - 1)] = frame;
        head.store(h + 1, std::memory_order_release);
        return true;
    }
    bool pop(FrameSample& frame) {
        const uint64_t t = tail.load(std::memory_order_relaxed);
        if (t == head.load(std::memory_order_acquire)) return false;
        frame = entries[t & (N - 1)];
        tail.store(t + 1, std::memory_order_release);
        return true;
    }
    uint64_t dropped() const { return lost.load(std::memory_order_relaxed); }
};
struct RenderSample {
    int64_t present = -1;
    // GPU time and slot wait are cumulative milliseconds; pipelines/left-out/uploads are cumulative counts/bytes.
    std::array<Value, kMetrics> values{};
};
using Report = void (*)(const char*);
using ReaderFactory = std::unique_ptr<Reader> (*)(const Device&);
// An atomic exclusively-created output; folder input selects perf.csv. No directory is created or existing file replaced.
FILE* create_log(const std::string& requested, std::string& actual, std::string& error);

class Sampler {
    Config config;
    Device device;
    std::string clock, version;
    Report report;
    ReaderFactory factory;
    std::atomic<bool>& collecting;
    std::thread worker;
    std::mutex sleep_mu, render_mu;
    std::condition_variable wake;
    bool stopping = false;
    std::unique_ptr<FrameQueue<65536>> queue;
    int64_t started_ns = 0;
    RenderSample renderer;
    std::atomic<int64_t> game_count{0}, replay_count{0};
    std::atomic<bool> snapshot_wanted{true};
    void run();
public:
    Sampler(Config settings, Device gpu, std::string frame_clock, std::string build_version, Report logger,
            std::atomic<bool>& active, ReaderFactory make = make_system_reader);
    ~Sampler();
    bool start();
    void stop();
    bool has_thread() const { return worker.joinable(); }
    Mask fields() const { return config.fields; }
    void game_present() { game_count.fetch_add(1, std::memory_order_relaxed); }
    void replay_present(int64_t present) { replay_count.store(present, std::memory_order_relaxed); }
    bool needs_snapshot() { return snapshot_wanted.exchange(false, std::memory_order_relaxed); }
    bool publish(const RenderSample& snapshot);
    void frame(int64_t ns, double ms, int64_t frame, double time_s) {
        queue->push({ns, frame, replay_count.load(std::memory_order_relaxed), game_count.load(std::memory_order_relaxed), time_s, ms});
    }
};
} // namespace mkwvk::perf_monitor
