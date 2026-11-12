// Pure authored inputs: no GPU, sensor, environment mutation, renderer or game is needed.
#define _CRT_SECURE_NO_WARNINGS
#include "../gpu/vk/perf_monitor.h"

#include <algorithm>
#include <clocale>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <initializer_list>
#include <iterator>
#include <limits>
#include <new>
#include <string>
#include <vector>

namespace pm = mkwvk::perf_monitor;
#define CHECK(name, condition) do { if (!(condition)) { std::fprintf(stderr, "FAIL %s at line %d\n", name, __LINE__); return 1; } } while (0)

// Count allocations in the fully disabled configuration path, including its callback.
static size_t allocations = 0;
void* operator new(size_t n) {
    ++allocations;
    if (void* p = std::malloc(n ? n : 1)) return p;
    throw std::bad_alloc();
}
void* operator new[](size_t n) { return ::operator new(n); }
void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete(void* p, size_t) noexcept { std::free(p); }
void operator delete[](void* p, size_t) noexcept { std::free(p); }

struct EnvEntry { const char* name; const char* value; };
static EnvEntry environment[8]{};
static unsigned env_calls = 0;
static bool unexpected_env = false;
static const char* get_env(const char* name) {
    ++env_calls;
    bool known = false;
    for (const char* expected : {"RX_VK_PERF_LOG", "RX_VK_PERF_FIELDS", "RX_VK_PERF_INTERVAL_MS", "RX_VK_PERF_WINDOW_MS",
                                "RX_VK_PERF_UNITS", "RX_VK_PERF_COMPACT"}) known |= std::strcmp(name, expected) == 0;
    unexpected_env |= !known;
    for (const auto& entry : environment)
        if (entry.name && std::strcmp(entry.name, name) == 0) return entry.value;
    return nullptr;
}
static void env(std::initializer_list<EnvEntry> entries) {
    std::fill(std::begin(environment), std::end(environment), EnvEntry{});
    std::copy(entries.begin(), entries.end(), std::begin(environment));
    env_calls = 0;
    unexpected_env = false;
}
static bool near(double actual, double expected) {
    return std::isfinite(actual) && std::abs(actual - expected) <= std::max(1.0, std::abs(expected)) * 1e-12;
}

struct MetricCase {
    pm::Metric metric;
    const char* name;
    const char* unit;
    double number;
    const char* display;
    const char* csv;
    double maximum;
    bool integer;
};
constexpr double exact_integer = 9007199254740992.0;
static constexpr MetricCase cases[] = {
    {pm::kMeanMs, "mean_ms", "ms", 12.5, "12.50 ms", "12.5", 86400000, false},
    {pm::kMeanFps, "mean_fps", "fps", 80, "80.0 fps", "80", 1000000, false},
    {pm::kLow1Fps, "low_1_fps", "fps", 50, "50.0 fps", "50", 1000000, false},
    {pm::kLow01Fps, "low_0_1_fps", "fps", 40, "40.0 fps", "40", 1000000, false},
    {pm::kP50Ms, "p50_ms", "ms", 10.25, "10.25 ms", "10.25", 86400000, false},
    {pm::kP95Ms, "p95_ms", "ms", 16.75, "16.75 ms", "16.75", 86400000, false},
    {pm::kP99Ms, "p99_ms", "ms", 20.25, "20.25 ms", "20.25", 86400000, false},
    {pm::kMaxMs, "max_ms", "ms", 100, "100.00 ms", "100", 86400000, false},
    {pm::kOver167, "over16_7", "", 2, "2", "2", exact_integer, true},
    {pm::kOver20, "over20", "", 3, "3", "3", exact_integer, true},
    {pm::kOver25, "over25", "", 4, "4", "4", exact_integer, true},
    {pm::kOver50, "over50", "", 5, "5", "5", exact_integer, true},
    {pm::kOver100, "over100", "", 6, "6", "6", exact_integer, true},
    {pm::kFrames, "frames", "", 7, "7", "7", exact_integer, true},
    {pm::kGpuUse, "gpu_use_pct", "%", 42.5, "42.5 %", "42.5", 100, false},
    {pm::kGpuTemp, "gpu_temp_c", "C", 62.5, "62.5 C", "62.5", 150, false},
    {pm::kGpuPower, "gpu_power_w", "W", 100.5, "100.5 W", "100.5", 10000, false},
    {pm::kGpuFan, "gpu_fan_pct", "%", 0, "0.0 %", "0", 100, false},
    {pm::kGpuClock, "gpu_clock_mhz", "MHz", 2000.5, "2000.5 MHz", "2000.5", 100000, false},
    {pm::kGpuThrottle, "gpu_throttle_mask", "", 3, "0x3", "3", exact_integer, true},
    {pm::kGpuMemory, "gpu_memory_used_bytes", "bytes", 1048576, "1.0 MiB", "1048576", exact_integer, true},
    {pm::kVramUsed, "vram_used_bytes", "bytes", 1048576, "1.0 MiB", "1048576", exact_integer, true},
    {pm::kVramBudget, "vram_budget_bytes", "bytes", 1048576, "1.0 MiB", "1048576", exact_integer, true},
    {pm::kTextureUsed, "texture_used_bytes", "bytes", 1048576, "1.0 MiB", "1048576", exact_integer, true},
    {pm::kTextureBudget, "texture_budget_bytes", "bytes", 1048576, "1.0 MiB", "1048576", exact_integer, true},
    {pm::kGpuMs, "gpu_ms", "ms", 2.25, "2.25 ms", "2.25", 86400000, false},
    {pm::kCpuProcess, "cpu_process_pct", "%", 250, "250.0 %", "250", 1000000, false},
    {pm::kCpuThread, "cpu_thread_pct", "%", 75, "75.0 %", "75", 100, false},
    {pm::kRss, "rss_bytes", "bytes", 1073741824, "1.0 GiB", "1073741824", exact_integer, true},
    {pm::kCommit, "commit_bytes", "bytes", 1610612736, "1.5 GiB", "1610612736", exact_integer, true},
    {pm::kRamAvailable, "ram_available_bytes", "bytes", 1048576, "1.0 MiB", "1048576", exact_integer, true},
    {pm::kSwapUsed, "swap_used_bytes", "bytes", 0, "0.0 MiB", "0", exact_integer, true},
    {pm::kDiskRead, "disk_read_bytes", "bytes", 3145728, "3.0 MiB", "3145728", exact_integer, true},
    {pm::kCpuTemp, "cpu_temp_c", "C", 70.5, "70.5 C", "70.5", 150, false},
    {pm::kCpuClock, "cpu_clock_mhz", "MHz", 4800, "4800.0 MHz", "4800", 100000, false},
    {pm::kBattery, "battery_pct", "%", 50.5, "50.5 %", "50.5", 100, false},
    {pm::kAcOnline, "ac_online", "", 1, "1", "1", 1, true},
    {pm::kPipelines, "pipelines", "", 23, "23", "23", exact_integer, true},
    {pm::kLeftOut, "left_out", "", 17, "17", "17", exact_integer, true},
    {pm::kUploadBytes, "upload_bytes", "bytes", 2097152, "2.0 MiB", "2097152", exact_integer, true},
    {pm::kGpuWaitMs, "gpu_wait_ms", "ms", 1.25, "1.25 ms", "1.25", 86400000, false},
};
static_assert(sizeof(cases) / sizeof(cases[0]) == pm::kMetrics);

static int test_values() {
    const pm::Reason reasons[] = {pm::Reason::None, pm::Reason::Disabled, pm::Reason::Unsupported, pm::Reason::ReadError,
        pm::Reason::Warmup, pm::Reason::Invalid, pm::Reason::CounterReset, pm::Reason::NoDevice, pm::Reason::Overflow};
    const char* reason_names[] = {"none", "disabled", "unsupported", "read error", "warmup", "invalid", "counter reset", "no device", "overflow"};
    for (size_t i = 0; i < std::size(reasons); ++i)
        CHECK("reason spelling", std::strcmp(pm::reason_name(reasons[i]), reason_names[i]) == 0);
    CHECK("default unavailable reason", !pm::Value{}.available() && pm::Value{}.reason == pm::Reason::Unsupported);
    for (int i = 0; i < pm::kMetrics; ++i) {
        const auto& item = cases[i];
        const auto& info = pm::metric_info(item.metric);
        CHECK("frozen order", item.metric == i);
        CHECK("frozen name", std::strcmp(info.name, item.name) == 0);
        CHECK("canonical unit", std::strcmp(info.unit, item.unit) == 0);
        CHECK("unit limits", info.minimum == 0 && info.maximum == item.maximum);
        CHECK("authored available reading", pm::checked(item.metric, pm::Value::of(item.number)).available());
        CHECK("every metric display", pm::format(item.metric, pm::Value::of(item.number)) == item.display);
        CHECK("upper inclusive", pm::checked(item.metric, pm::Value::of(item.maximum)).available());
        CHECK("above upper invalid", pm::checked(item.metric, pm::Value::of(std::nextafter(item.maximum,
              std::numeric_limits<double>::infinity()))).reason == pm::Reason::Invalid);
        CHECK("negative invalid", pm::checked(item.metric, pm::Value::of(-1)).reason == pm::Reason::Invalid);
        bool temperature = item.metric == pm::kGpuTemp || item.metric == pm::kCpuTemp;
        CHECK("zero idle except temperature", pm::checked(item.metric, pm::Value::of(0)).available() == !temperature);
        CHECK("signed zero idle except temperature", pm::checked(item.metric, pm::Value::of(-0.0)).available() == !temperature);
        if (item.integer) CHECK("integer unit", pm::checked(item.metric, pm::Value::of(0.5)).reason == pm::Reason::Invalid);
        for (double bad : {std::numeric_limits<double>::quiet_NaN(), std::numeric_limits<double>::infinity(),
                           -std::numeric_limits<double>::infinity()}) {
            CHECK("nonfinite invalid", pm::checked(item.metric, pm::Value::of(bad)).reason == pm::Reason::Invalid);
            CHECK("nonfinite display empty", pm::format(item.metric, pm::Value::of(bad)).empty());
        }
        for (pm::Reason reason : reasons) {
            if (reason == pm::Reason::None) continue;
            pm::Value missing{123, reason};
            CHECK("reason retained", pm::checked(item.metric, missing).reason == reason);
            CHECK("unavailable display empty", pm::format(item.metric, missing).empty());
        }
        if (std::strcmp(item.unit, "bytes") == 0) {
            CHECK("every byte unit IEC mega", pm::format(item.metric, pm::Value::of(1572864)) == "1.5 MiB");
            CHECK("every byte unit IEC giga", pm::format(item.metric, pm::Value::of(1610612736)) == "1.5 GiB");
            CHECK("every byte unit SI mega", pm::format(item.metric, pm::Value::of(1500000), true) == "1.5 MB");
            CHECK("every byte unit SI giga", pm::format(item.metric, pm::Value::of(1500000000), true) == "1.5 GB");
            CHECK("byte unit boundary IEC", pm::format(item.metric, pm::Value::of(1073741824)) == "1.0 GiB");
            CHECK("byte unit boundary SI", pm::format(item.metric, pm::Value::of(1000000000), true) == "1.0 GB");
        } else CHECK("nonbyte SI unchanged", pm::format(item.metric, pm::Value::of(item.number), true) == item.display);
    }
    for (pm::Metric metric : {pm::kGpuTemp, pm::kCpuTemp}) {
        CHECK("255 temperature sentinel", pm::checked(metric, pm::Value::of(255)).reason == pm::Reason::Invalid);
        CHECK("positive temperature", pm::checked(metric, pm::Value::of(0.01)).available());
    }
    CHECK("GPU percent is bounded", !pm::checked(pm::kGpuUse, pm::Value::of(100.01)).available());
    CHECK("one thread percent is bounded", !pm::checked(pm::kCpuThread, pm::Value::of(100.01)).available());
    CHECK("process may use several cores", pm::checked(pm::kCpuProcess, pm::Value::of(3200)).available());
    CHECK("large counter remains exact", pm::format(pm::kFrames, pm::Value::of(exact_integer)) == "9007199254740992");
    CHECK("unknown metric rejected", pm::checked(pm::kMetrics, pm::Value::of(1)).reason == pm::Reason::Invalid);
    CHECK("negative zero normalized", pm::format(pm::kGpuUse, pm::Value::of(-0.0)) == "0.0 %");
    return 0;
}

static int test_config() {
    std::vector<std::string> errors;
    env({});
    size_t before = allocations;
    auto config = pm::configuration(get_env, errors);
    CHECK("off path has no allocation", allocations == before);
    CHECK("off path only asks supported switches", env_calls == 6 && !unexpected_env);
    CHECK("absent log disabled", config.valid && !config.enabled() && errors.empty());
    CHECK("configuration defaults", config.fields == pm::all_metrics && config.interval_ms == 1000 && config.window_ms == 5000 &&
          !config.compact && !config.si);
    env({{"RX_VK_PERF_LOG", "never-opened/path,with spaces.csv"}});
    config = pm::configuration(get_env, errors);
    CHECK("literal path enables without file I/O", config.enabled() && config.log_path == "never-opened/path,with spaces.csv" && errors.empty());
    env({{"RX_VK_PERF_LOG", "capture.csv"}, {"RX_VK_PERF_FIELDS", "mean_ms,gpu_temp_c,cpu_process_pct"},
         {"RX_VK_PERF_INTERVAL_MS", "100"}, {"RX_VK_PERF_WINDOW_MS", "60000"}, {"RX_VK_PERF_UNITS", "si"}, {"RX_VK_PERF_COMPACT", "1"}});
    config = pm::configuration(get_env, errors);
    CHECK("all supplied switches", config.enabled() && config.interval_ms == 100 && config.window_ms == 60000 && config.si && config.compact);
    CHECK("positive field set", config.fields == (pm::bit(pm::kMeanMs) | pm::bit(pm::kGpuTemp) | pm::bit(pm::kCpuProcess)));
    env({{"RX_VK_PERF_LOG", "capture.csv"}, {"RX_VK_PERF_UNITS", "iec"}, {"RX_VK_PERF_COMPACT", "0"},
         {"RX_VK_PERF_INTERVAL_MS", "60000"}, {"RX_VK_PERF_WINDOW_MS", "100"}});
    config = pm::configuration(get_env, errors);
    CHECK("opposite switch endpoints", config.enabled() && !config.si && !config.compact && config.interval_ms == 60000 && config.window_ms == 100);
    pm::Mask mask = 0;
    std::string error = "old error";
    CHECK("all fields", pm::field_mask("all", mask, error) && mask == pm::all_metrics && error.empty());
    CHECK("exclusions", pm::field_mask("all,-gpu_temp_c,-rss_bytes,-rss_bytes", mask, error) &&
          mask == (pm::all_metrics & ~pm::bit(pm::kGpuTemp) & ~pm::bit(pm::kRss)));
    CHECK("positive duplicates idempotent", pm::field_mask("mean_ms,mean_ms", mask, error) && mask == pm::bit(pm::kMeanMs));
    for (const auto& item : cases) {
        CHECK("every field individually selectable", pm::field_mask(item.name, mask, error) && mask == pm::bit(item.metric));
        std::string excluded = std::string("all,-") + item.name;
        CHECK("every field individually removable", pm::field_mask(excluded.c_str(), mask, error) && mask == (pm::all_metrics & ~pm::bit(item.metric)));
    }
    std::string nothing = "all";
    for (const auto& item : cases) nothing += std::string(",-") + item.name;
    CHECK("all exclusions may select zero", pm::field_mask(nothing.c_str(), mask, error) && mask == 0);
    env({{"RX_VK_PERF_LOG", "capture.csv"}, {"RX_VK_PERF_FIELDS", nothing.c_str()}});
    config = pm::configuration(get_env, errors);
    CHECK("zero selection is valid but disabled", config.valid && !config.enabled() && config.fields == 0 && errors.empty());
    for (const char* bad : {"", "none", "-mean_ms", "mean_ms,-gpu_use_pct", "all,mean_ms", "all,all", "mean_ms,all",
                            "all,-all", "all,-", "all,", ",mean_ms", "mean_ms,", "mean_ms,,gpu_use_pct", " mean_ms",
                            "mean_ms ", "MEAN_MS", "+mean_ms", "all, -mean_ms"}) {
        mask = pm::bit(pm::kGpuUse);
        CHECK("malformed field list", !pm::field_mask(bad, mask, error) && !error.empty());
        CHECK("bad field parse is transactional", mask == pm::bit(pm::kGpuUse));
        errors.clear();
        env({{"RX_VK_PERF_LOG", "capture.csv"}, {"RX_VK_PERF_FIELDS", bad}});
        config = pm::configuration(get_env, errors);
        CHECK("invalid fields disable logging", !config.valid && !config.enabled() && errors.size() == 1);
    }
    CHECK("null field input", !pm::field_mask(nullptr, mask, error));
    CHECK("unknowns rejected once", !pm::field_mask("mean_ms,unheard,other,unheard,other", mask, error) &&
          error == "unknown metric 'unheard'; unknown metric 'other'");
    CHECK("unknown exclusions rejected once", !pm::field_mask("all,-unheard,-unheard,-other", mask, error) &&
          error == "unknown metric 'unheard'; unknown metric 'other'");
    errors.clear();
    env({{"RX_VK_PERF_LOG", "capture.csv"}, {"RX_VK_PERF_FIELDS", "unheard,unheard,other"}});
    config = pm::configuration(get_env, errors);
    CHECK("one field diagnostic", !config.enabled() && errors.size() == 1 &&
          errors[0] == "RX_VK_PERF_FIELDS: unknown metric 'unheard'; unknown metric 'other'");
    for (const char* name : {"RX_VK_PERF_INTERVAL_MS", "RX_VK_PERF_WINDOW_MS"}) {
        for (const char* bad : {"", "0", "99", "60001", "-100", "+100", " 100", "100 ", "100\n", "100.0", "1e3",
                                "0x100", "100ms", "nan", "inf", "99999999999999999999999999999999999"}) {
            errors.clear();
            env({{"RX_VK_PERF_LOG", "capture.csv"}, {name, bad}});
            config = pm::configuration(get_env, errors);
            CHECK("strict numeric config", !config.valid && !config.enabled() && errors.size() == 1);
        }
    }
    for (const char* bad : {"", "yes", "true", "on", "2", "-1", "01", "1 ", " 0"}) {
        errors.clear();
        env({{"RX_VK_PERF_LOG", "capture.csv"}, {"RX_VK_PERF_COMPACT", bad}});
        config = pm::configuration(get_env, errors);
        CHECK("strict boolean config", !config.valid && !config.enabled() && errors.size() == 1);
    }
    for (const char* bad : {"", "IEC", "SI", "bytes", "iec ", " si"}) {
        errors.clear();
        env({{"RX_VK_PERF_LOG", "capture.csv"}, {"RX_VK_PERF_UNITS", bad}});
        config = pm::configuration(get_env, errors);
        CHECK("strict unit config", !config.valid && !config.enabled() && errors.size() == 1);
    }
    errors.clear();
    env({{"RX_VK_PERF_LOG", ""}});
    config = pm::configuration(get_env, errors);
    CHECK("empty supplied log is malformed", !config.valid && !config.enabled() && errors.size() == 1);
    errors.clear();
    env({{"RX_VK_PERF_LOG", ""}, {"RX_VK_PERF_FIELDS", "bad,bad"}, {"RX_VK_PERF_INTERVAL_MS", "-1"},
         {"RX_VK_PERF_WINDOW_MS", "1e3"}, {"RX_VK_PERF_UNITS", "IEC"}, {"RX_VK_PERF_COMPACT", "true"}});
    config = pm::configuration(get_env, errors);
    CHECK("all malformed switches diagnosed", !config.valid && !config.enabled() && errors.size() == 6);
    errors.clear();
    env({{"RX_VK_PERF_INTERVAL_MS", "wrong"}});
    config = pm::configuration(get_env, errors);
    CHECK("malformed switch diagnosed without log", !config.valid && !config.enabled() && errors.size() == 1);
    errors.clear();
    env({{"RX_VK_PERF_FIELDS", "all,-gpu_use_pct"}, {"RX_VK_PERF_UNITS", "si"}});
    config = pm::configuration(get_env, errors);
    CHECK("valid switches do not enable without log", config.valid && !config.enabled() && errors.empty());
    errors.emplace_back("unrelated earlier diagnostic");
    env({{"RX_VK_PERF_LOG", "capture.csv"}});
    config = pm::configuration(get_env, errors);
    CHECK("caller diagnostics not mistaken for invalid config", config.enabled() && errors.size() == 1);
    return 0;
}

static std::vector<double> authored_times(size_t ordinary, double last) {
    std::vector<double> result;
    for (size_t i = 1; i <= ordinary; ++i) result.push_back(static_cast<double>(i));
    result.push_back(last);
    return result;
}

static int test_statistics() {
    auto times = authored_times(100, 1000);
    std::reverse(times.begin(), times.end());
    const auto input = times;
    auto stats = pm::frame_statistics(times);
    const double expected[] = {6050.0 / 101, 101000.0 / 6050, 1000.0 / 550, 1, 51, 96, 100, 1000, 85, 81, 76, 51, 1, 101};
    for (int i = pm::kMeanMs; i <= pm::kFrames; ++i)
        CHECK("N=101 ceil lows and nearest ranks", stats[i].available() && near(stats[i].number, expected[i]));
    CHECK("statistics leave input alone", times == input);
    for (int i = pm::kGpuUse; i < pm::kMetrics; ++i)
        CHECK("statistics do not invent telemetry", stats[i].reason == pm::Reason::Unsupported);
    auto many = pm::frame_statistics(authored_times(1000, 10000));
    CHECK("0.1 percent uses ceil too", many[pm::kLow01Fps].available() && near(many[pm::kLow01Fps].number, 1000.0 / 5500));
    auto single = pm::frame_statistics({25});
    for (pm::Metric metric : {pm::kMeanMs, pm::kP50Ms, pm::kP95Ms, pm::kP99Ms, pm::kMaxMs})
        CHECK("one interval statistics", single[metric].available() && single[metric].number == 25);
    for (pm::Metric metric : {pm::kMeanFps, pm::kLow1Fps, pm::kLow01Fps})
        CHECK("one interval FPS", single[metric].available() && single[metric].number == 40);
    double tiny = std::numeric_limits<double>::denorm_min();
    auto small = pm::frame_statistics({tiny, tiny});
    CHECK("finite positive mean does not underflow during averaging", small[pm::kMeanMs].available() && small[pm::kMeanMs].number == tiny);
    auto boundaries = pm::frame_statistics({16.7, 20, 25, 50, 100});
    for (int i = 0; i < 5; ++i)
        CHECK("threshold comparison is strict", boundaries[pm::kOver167 + i].number == 4 - i);
    auto empty = pm::frame_statistics({});
    for (int i = pm::kMeanMs; i <= pm::kFrames; ++i)
        CHECK("empty has an unavailable reason", empty[i].reason == pm::Reason::Warmup);
    for (double bad : {0.0, -0.0, -1.0, std::numeric_limits<double>::quiet_NaN(), std::numeric_limits<double>::infinity(),
                       -std::numeric_limits<double>::infinity()}) {
        auto invalid = pm::frame_statistics({10, bad, 20});
        for (int i = pm::kMeanMs; i <= pm::kFrames; ++i)
            CHECK("one bad input invalidates all frame statistics", invalid[i].reason == pm::Reason::Invalid);
    }
    return 0;
}

static std::string contents(const char* path) {
    FILE* file = std::fopen(path, "rb");
    if (!file) return {};
    std::string result;
    char buffer[4096];
    while (size_t n = std::fread(buffer, 1, sizeof(buffer), file)) result.append(buffer, n);
    std::fclose(file);
    return result;
}
static std::string missing_metrics(int count = pm::kMetrics) {
    std::string result;
    for (int i = 0; i < count; ++i) result += ",na";
    return result;
}

static int write_csv_fixture(const char* path) {
    FILE* file = std::fopen(path, "wb");
    CHECK("open authored capture", file != nullptr);
    char buffer[8192];
    CHECK("fully buffered fixture", std::setvbuf(file, buffer, _IOFBF, sizeof(buffer)) == 0);
    pm::csv_header(file);
    std::string expected = "# hookshot_perf,1,kind,sample,monotonic_ns,elapsed_s,game_frame,renderer_present,frame,time_s,clock,thread_name,event,detail";
    for (const auto& item : cases) expected += std::string(",") + item.name;
    expected += '\n';
    CHECK("metadata write", pm::csv_started(file, "2026-11-12T00:00:00Z", "v1.2.3,\"fixture\""));
    expected += "# started,2026-11-12T00:00:00Z,\"v1.2.3,\"\"fixture\"\"\"\n";
    CHECK("exact header and started comment", contents(path) == expected);
    pm::Sample sample;
    sample.sample = 1;
    sample.monotonic_ns = 1234567890;
    sample.elapsed_s = 1.25;
    sample.game_frame = 100;
    sample.renderer_present = 200;
    sample.frame = 300;
    sample.time_s = 0.5;
    sample.clock = "headless-renderer";
    sample.thread_name = "worker,\"quoted\"\r\n\xc3\xa9";
    sample.detail = "path, \"scene\"";
    for (const auto& item : cases) sample.values[item.metric] = pm::Value::of(item.number);
    CHECK("all-metric sample write", pm::csv_row(file, sample));
    expected += "sample,1,1234567890,1.25,100,200,300,0.5,headless-renderer,\"worker,\"\"quoted\"\"\r\n\xc3\xa9\",,\"path, \"\"scene\"\"\"";
    for (const auto& item : cases) expected += std::string(",") + item.csv;
    expected += '\n';
    CHECK("exact quoted row and unscaled numeric cells flushed", contents(path) == expected);
    uint64_t sequence = 2;
    for (const char* event : {"capture_start", "loading_start", "loading_end", "map_change", "capture_end"}) {
        pm::Sample marker;
        marker.kind = "marker";
        marker.sample = sequence++;
        marker.clock = "swapchain";
        marker.event = event;
        marker.detail = "scene, \"fixture\"";
        CHECK("marker write", pm::csv_row(file, marker));
        expected += "marker," + std::to_string(marker.sample) + ",0,0,na,na,na,na,swapchain,," + event +
            ",\"scene, \"\"fixture\"\"\"" + missing_metrics() + '\n';
        CHECK("exact marker columns and missing cells flushed", contents(path) == expected);
    }
    for (int population : {101, 100, 1001}) {
        auto times = population == 1001 ? authored_times(1000, 10000) : authored_times(100, 1000);
        if (population == 100) times.pop_back();
        sample = pm::Sample{};
        sample.sample = sequence++;
        sample.elapsed_s = 0.1; // max_digits10 must retain the exact double rather than round to six decimals.
        sample.clock = "headless-renderer";
        sample.detail = "authored N=" + std::to_string(population);
        sample.values = pm::frame_statistics(times);
        CHECK("authored statistics CSV", pm::csv_row(file, sample));
    }
    sample = pm::Sample{};
    sample.sample = sequence++;
    sample.monotonic_ns = -1;
    sample.elapsed_s = std::numeric_limits<double>::quiet_NaN();
    sample.time_s = std::numeric_limits<double>::infinity();
    sample.detail = "invalid values";
    for (auto& value : sample.values) value = pm::Value::of(std::numeric_limits<double>::quiet_NaN());
    sample.values[pm::kGpuTemp] = pm::Value::of(255);
    sample.values[pm::kGpuUse] = pm::Value::of(101);
    sample.values[pm::kCpuProcess] = pm::Value::of(250);
    sample.values[pm::kRss] = {0, pm::Reason::Disabled};
    CHECK("invalid numeric cells write na", pm::csv_row(file, sample));
    expected = contents(path);
    std::string invalid = "sample,10,na,na,na,na,na,na,,,,invalid values";
    for (int i = 0; i < pm::kMetrics; ++i) invalid += i == pm::kCpuProcess ? ",250" : ",na";
    invalid += '\n';
    CHECK("invalid numeric exact spelling", expected.size() >= invalid.size() && expected.ends_with(invalid));
    sample = pm::Sample{};
    sample.kind = "summary";
    sample.sample = sequence++;
    sample.monotonic_ns = 3000000000;
    sample.elapsed_s = 3;
    sample.game_frame = 7;
    sample.renderer_present = 9;
    sample.clock = "headless-renderer";
    sample.event = "end";
    sample.detail = "complete";
    sample.values = pm::frame_statistics({25});
    CHECK("summary write", pm::csv_row(file, sample));
    expected += "summary,11,3000000000,3,7,9,na,na,headless-renderer,,end,complete,25,40,40,40,25,25,25,25,1,1,0,0,0,1" +
        missing_metrics(pm::kMetrics - 14) + '\n';
    CHECK("exact graceful summary flushed before close", contents(path) == expected);
    CHECK("close capture", std::fclose(file) == 0);
    FILE* read_only = std::fopen(path, "rb");
    CHECK("open read-only fixture", read_only != nullptr);
    CHECK("write error reported", !pm::csv_row(read_only, sample));
    std::clearerr(read_only);
    CHECK("metadata write error reported", !pm::csv_started(read_only, "2026-11-12T00:00:00Z", "fixture"));
    std::fclose(read_only);
    CHECK("null writer rejected", !pm::csv_row(nullptr, sample) && !pm::csv_started(nullptr, "", ""));
    return 0;
}

int main(int argc, char** argv) {
    CHECK("fixture output argument", argc == 2);
    if (test_values() || test_config() || test_statistics() || write_csv_fixture(argv[1])) return 1;
    // Exercise a comma-decimal C locale where the host provides one, without requiring locale installation.
    // Only the fixture changes it: production formatting and CSV must not touch process or thread locales.
    const char* current = std::setlocale(LC_NUMERIC, nullptr);
    std::string saved_locale = current ? current : "C";
    for (const char* candidate : {"de_DE.UTF-8", "fr_FR.UTF-8", "German_Germany.1252", "French_France.1252"}) {
        if (!std::setlocale(LC_NUMERIC, candidate)) continue;
        CHECK("decimal locale does not affect display", pm::format(pm::kMeanMs, pm::Value::of(12.5)) == "12.50 ms");
        std::string localized = std::string(argv[1]) + ".locale";
        if (write_csv_fixture(localized.c_str())) return 1;
        CHECK("decimal locale does not affect CSV", contents(localized.c_str()) == contents(argv[1]));
        CHECK("remove locale fixture", std::remove(localized.c_str()) == 0);
        break;
    }
    CHECK("restore fixture locale", std::setlocale(LC_NUMERIC, saved_locale.c_str()) != nullptr);
    std::puts("PASS: performance monitor values, strict configuration, statistics and flushed CSV");
    return 0;
}
