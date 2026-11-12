// (hookshot) Optional sampled diagnostics, independent of Vulkan and the menu. All OS I/O runs on the sampler.
#pragma once

#include <array>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

namespace mkwvk::perf_monitor {

enum Metric {
    kMeanMs, kMeanFps, kLow1Fps, kLow01Fps, kP50Ms, kP95Ms, kP99Ms, kMaxMs,
    kOver167, kOver20, kOver25, kOver50, kOver100, kFrames,
    kGpuUse, kGpuTemp, kGpuPower, kGpuFan, kGpuClock, kGpuThrottle, kGpuMemory,
    kVramUsed, kVramBudget, kTextureUsed, kTextureBudget, kGpuMs,
    kCpuProcess, kCpuThread, kRss, kCommit, kRamAvailable, kSwapUsed, kDiskRead,
    kCpuTemp, kCpuClock, kBattery, kAcOnline, kPipelines, kLeftOut, kUploadBytes, kGpuWaitMs, kMetrics
};
static_assert(kMetrics < 64);
using Mask = uint64_t;
constexpr Mask bit(Metric m) { return Mask{1} << m; }
constexpr Mask all_metrics = (Mask{1} << kMetrics) - 1;

enum class Reason { None, Disabled, Unsupported, ReadError, Warmup, Invalid, CounterReset, NoDevice, Overflow };
struct Value {
    double number = 0;
    Reason reason = Reason::Unsupported;
    bool available() const { return reason == Reason::None; }
    static Value of(double n) { return {n, Reason::None}; }
};
struct MetricInfo { const char* name; const char* unit; double minimum, maximum; };
const MetricInfo& metric_info(Metric metric);
const char* reason_name(Reason reason);
Value checked(Metric metric, Value value);

struct Config {
    std::string log_path;
    Mask fields = all_metrics;
    unsigned interval_ms = 1000, window_ms = 5000;
    bool compact = false, si = false, valid = true;
    bool enabled() const { return valid && !log_path.empty() && fields != 0; }
};
using GetEnv = const char* (*)(const char*);
Config configuration(GetEnv get, std::vector<std::string>& errors);
bool field_mask(const char* text, Mask& mask, std::string& error);
// Format an available reading only. Empty for unavailable. CSV remains in its canonical units.
std::string format(Metric metric, Value value, bool si = false);

struct Sample {
    std::string kind = "sample", clock, thread_name, event, detail;
    uint64_t sample = 0;
    int64_t monotonic_ns = 0;
    double elapsed_s = 0;
    int64_t game_frame = -1, renderer_present = -1, frame = -1;
    double time_s = -1;
    std::array<Value, kMetrics> values{};
};
void csv_header(FILE* file);
bool csv_started(FILE* file, const char* utc, const char* version);
bool csv_row(FILE* file, const Sample& sample);
// Authored positive finite completed intervals. Invalid input invalidates all frame statistics.
std::array<Value, kMetrics> frame_statistics(const std::vector<double>& ms);

struct Device {
    uint32_t vendor_id = 0;
    std::string uuid;          // NVML form GPU-xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx, selected Vulkan device only
    std::string pci_address;   // domain:bus:device.function; empty if Vulkan exposes no exact identity
};
// begin() snapshots only requested groups; read() returns each independent reading or its reason.
// One reader object belongs to one sampler thread. No global probes or static library initialization.
class Reader {
public:
    virtual ~Reader() = default;
    virtual void begin(int64_t monotonic_ns, Mask requested) = 0;
    virtual Value read(Metric metric) const = 0;
    virtual std::string busiest_thread_name() const = 0;
};
std::unique_ptr<Reader> make_system_reader(const Device& device);
void lower_sampler_priority();
int64_t monotonic_now_ns();
int64_t sampler_thread_cpu_ns(); // This calling thread's CPU time, -1 when unavailable.

} // namespace mkwvk::perf_monitor
