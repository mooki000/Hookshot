#define _CRT_SECURE_NO_WARNINGS
#include "../gpu/vk/perf_sampler.h"
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <thread>

namespace pm = mkwvk::perf_monitor;
#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #x); std::exit(1); } } while (0)
static std::atomic<unsigned> made{0}, sampled{0};
static std::atomic<bool> wrong_thread{false};
static std::atomic<unsigned> invalid_temp{0}, invalid_use{0}, cpu_reports{0};
static std::thread::id caller;
struct Fake final : pm::Reader {
    void begin(int64_t, pm::Mask) override {
        if (std::this_thread::get_id() == caller) wrong_thread = true;
        sampled++;
    }
    pm::Value read(pm::Metric metric) const override {
        if (metric == pm::kCpuProcess) return pm::Value::of(175);
        if (metric == pm::kRss) return pm::Value::of(123456789);
        if (metric == pm::kGpuTemp) return pm::Value::of(255);
        if (metric == pm::kGpuUse) return pm::Value::of(101);
        return {};
    }
    std::string busiest_thread_name() const override { return "thread, \"quoted\""; }
};
static std::unique_ptr<pm::Reader> make_fake(const pm::Device&) { made++; return std::make_unique<Fake>(); }
static void report(const char* text) {
    if (std::strstr(text, "gpu_temp_c: invalid")) invalid_temp++;
    if (std::strstr(text, "gpu_use_pct: invalid")) invalid_use++;
    if (std::strstr(text, "sampler CPU ")) cpu_reports++;
    std::puts(text);
}
static void await_samples(unsigned count) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (sampled < count && std::chrono::steady_clock::now() < deadline) std::this_thread::sleep_for(std::chrono::milliseconds(2));
    CHECK(sampled >= count);
}
int main(int argc, char** argv) {
    CHECK(argc == 2);
    caller = std::this_thread::get_id();
    std::atomic<bool> active{false};
    pm::Config off;
    pm::Sampler disabled(off, {}, "headless-renderer", "fixture", report, active, make_fake);
    CHECK(!disabled.start() && !disabled.has_thread() && !active && made == 0 && sampled == 0);
    off.log_path = argv[1]; off.fields = 0;
    pm::Sampler empty(off, {}, "headless-renderer", "fixture", report, active, make_fake);
    CHECK(!empty.start() && !empty.has_thread() && !active && made == 0);
    pm::FrameQueue<2> queue;
    pm::FrameSample frame;
    CHECK(queue.push({1, 0, 1, 1, 0, 0}));
    CHECK(queue.push({2, 1, 2, 2, 1, 10}));
    CHECK(!queue.push({3, 2, 3, 3, 2, 20}) && queue.dropped() == 1);
    CHECK(queue.pop(frame) && frame.at_ns == 1);
    CHECK(queue.push({3, 2, 3, 3, 2, 20}));
    CHECK(queue.pop(frame) && frame.at_ns == 2);
    CHECK(queue.pop(frame) && frame.at_ns == 3 && !queue.pop(frame));

    pm::Config config;
    config.log_path = argv[1]; config.interval_ms = 100; config.window_ms = 5000;
    pm::Sampler sampler(config, {}, "headless-renderer", "test-build,quoted", report, active, make_fake);
    CHECK(sampler.start());
    await_samples(1);
    const int64_t start = pm::monotonic_now_ns();
    sampler.game_present(); sampler.replay_present(1); sampler.frame(start, 0, 0, 0);
    sampler.game_present(); sampler.replay_present(2); sampler.frame(start + 10000000, 10, 1, .01);
    sampler.game_present(); sampler.replay_present(3); sampler.frame(start + 30000000, 20, 2, .03);
    pm::RenderSample snapshot;
    snapshot.present = 3;
    snapshot.values[pm::kTextureUsed] = pm::Value::of(4096);
    snapshot.values[pm::kTextureBudget] = pm::Value::of(8192);
    CHECK(sampler.publish(snapshot));
    await_samples(2);
    // Read from another stream while the sampler is alive: a completed sample is flushed, not waiting for close.
    namespace fs = std::filesystem;
    const auto path = fs::path(reinterpret_cast<const char8_t*>(argv[1])) / "perf.csv";
    bool visible = false;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (!visible && std::chrono::steady_clock::now() < deadline) {
        std::ifstream input(path);
        std::string line;
        while (std::getline(input, line)) visible |= line.rfind("sample,2,", 0) == 0;
        if (!visible) std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    CHECK(visible && active && !wrong_thread && made == 1);
    sampler.stop();
    CHECK(!sampler.has_thread() && !active);
    CHECK(invalid_temp == 1 && invalid_use == 1 && cpu_reports == 1);
    // Existing output and directories cannot be overwritten, including names containing non-ASCII text.
    std::string actual, error;
    FILE* second = pm::create_log(config.log_path, actual, error);
    CHECK(second != nullptr);
    CHECK(fs::canonical(fs::path(reinterpret_cast<const char8_t*>(actual.c_str()))) == fs::canonical(path.parent_path() / "perf-1.csv"));
    CHECK(std::fputs("kept\n", second) >= 0 && std::fclose(second) == 0);
    FILE* third = pm::create_log(config.log_path + "/perf.csv", actual, error);
    CHECK(third != nullptr && fs::canonical(fs::path(reinterpret_cast<const char8_t*>(actual.c_str()))) ==
          fs::canonical(path.parent_path() / "perf-2.csv"));
    CHECK(std::fclose(third) == 0);
    FILE* unicode = pm::create_log(config.log_path + "/t\xc3\xa9l\xc3\xa9.csv", actual, error);
    CHECK(unicode != nullptr && fs::canonical(fs::path(reinterpret_cast<const char8_t*>(actual.c_str()))) ==
          fs::canonical(path.parent_path() / u8"télé.csv"));
    CHECK(std::fclose(unicode) == 0);
    CHECK(pm::create_log(config.log_path + "/missing/perf.csv", actual, error) == nullptr && !error.empty());
    std::cout << "PASS: sampler fake, disabled worker, bounded queue, independent flush and no-overwrite paths\n";
}
