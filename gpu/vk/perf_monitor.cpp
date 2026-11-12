// (hookshot) Pure sampled-monitor values and serialization; no sensor, Vulkan or clock access.
#include "perf_monitor.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstring>
#include <limits>
#include <string_view>

namespace mkwvk::perf_monitor {
namespace {

// Generous unit sanity limits, not device capabilities. A process may use many logical CPUs.
constexpr double exact_integer = 9007199254740992.0; // 2^53: every preceding integer fits in a double.
constexpr double max_ms = 86400000.0; // A single interval of up to one day.
constexpr double max_fps = 1000000.0;
constexpr double max_process_pct = 1000000.0;
constexpr MetricInfo metrics[] = {
    {"mean_ms", "ms", 0, max_ms},
    {"mean_fps", "fps", 0, max_fps},
    {"low_1_fps", "fps", 0, max_fps},
    {"low_0_1_fps", "fps", 0, max_fps},
    {"p50_ms", "ms", 0, max_ms},
    {"p95_ms", "ms", 0, max_ms},
    {"p99_ms", "ms", 0, max_ms},
    {"max_ms", "ms", 0, max_ms},
    {"over16_7", "", 0, exact_integer},
    {"over20", "", 0, exact_integer},
    {"over25", "", 0, exact_integer},
    {"over50", "", 0, exact_integer},
    {"over100", "", 0, exact_integer},
    {"frames", "", 0, exact_integer},
    {"gpu_use_pct", "%", 0, 100},
    {"gpu_temp_c", "C", 0, 150},
    {"gpu_power_w", "W", 0, 10000},
    {"gpu_fan_pct", "%", 0, 100},
    {"gpu_clock_mhz", "MHz", 0, 100000},
    {"gpu_throttle_mask", "", 0, exact_integer},
    {"gpu_memory_used_bytes", "bytes", 0, exact_integer},
    {"vram_used_bytes", "bytes", 0, exact_integer},
    {"vram_budget_bytes", "bytes", 0, exact_integer},
    {"texture_used_bytes", "bytes", 0, exact_integer},
    {"texture_budget_bytes", "bytes", 0, exact_integer},
    {"gpu_ms", "ms", 0, max_ms},
    {"cpu_process_pct", "%", 0, max_process_pct},
    {"cpu_thread_pct", "%", 0, 100},
    {"rss_bytes", "bytes", 0, exact_integer},
    {"commit_bytes", "bytes", 0, exact_integer},
    {"ram_available_bytes", "bytes", 0, exact_integer},
    {"swap_used_bytes", "bytes", 0, exact_integer},
    {"disk_read_bytes", "bytes", 0, exact_integer},
    {"cpu_temp_c", "C", 0, 150},
    {"cpu_clock_mhz", "MHz", 0, 100000},
    {"battery_pct", "%", 0, 100},
    {"ac_online", "", 0, 1},
    {"pipelines", "", 0, exact_integer},
    {"left_out", "", 0, exact_integer},
    {"upload_bytes", "bytes", 0, exact_integer},
    {"gpu_wait_ms", "ms", 0, max_ms},
};
static_assert(sizeof(metrics) / sizeof(metrics[0]) == kMetrics);
constexpr char common_columns[] =
    "kind,sample,monotonic_ns,elapsed_s,game_frame,renderer_present,frame,time_s,clock,thread_name,event,detail";

bool byte_metric(Metric metric) {
    return (metric >= kGpuMemory && metric <= kTextureBudget) ||
        (metric >= kRss && metric <= kDiskRead) || metric == kUploadBytes;
}

bool integer_metric(Metric metric) {
    return byte_metric(metric) || (metric >= kOver167 && metric <= kFrames) ||
        metric == kGpuThrottle || metric == kAcOnline || metric == kPipelines || metric == kLeftOut;
}

Metric find_metric(std::string_view name) {
    for (int i = 0; i < kMetrics; ++i)
        if (name == metrics[i].name) return static_cast<Metric>(i);
    return kMetrics;
}

bool milliseconds(const char* text, unsigned& result) {
    if (!*text) return false;
    unsigned n = 0;
    for (const char* p = text; *p; ++p) {
        if (*p < '0' || *p > '9') return false;
        unsigned digit = static_cast<unsigned>(*p - '0');
        if (n > (60000 - digit) / 10) return false;
        n = n * 10 + digit;
    }
    if (n < 100) return false;
    result = n;
    return true;
}

// to_chars is locale-independent; snprintf only joins text/units, never formats a decimal float.
std::string display_number(double number, int decimals, const char* unit) {
    char digits[96];
    auto converted = std::to_chars(digits, digits + sizeof(digits) - 1, number,
                                   std::chars_format::fixed, decimals);
    if (converted.ec != std::errc{}) return {};
    *converted.ptr = 0;
    char text[128];
    int n = std::snprintf(text, sizeof(text), "%s%s%s", digits, *unit ? " " : "", unit);
    return n >= 0 && static_cast<size_t>(n) < sizeof(text) ? std::string(text, static_cast<size_t>(n)) : std::string{};
}

bool csv_text(FILE* file, std::string_view text) {
    if (text.find_first_of(",\"\r\n") == std::string_view::npos)
        return text.empty() || std::fwrite(text.data(), 1, text.size(), file) == text.size();
    if (std::fputc('"', file) == EOF) return false;
    size_t start = 0;
    for (size_t i = 0; i < text.size(); ++i) {
        if (text[i] != '"') continue;
        if (i != start && std::fwrite(text.data() + start, 1, i - start, file) != i - start) return false;
        if (std::fputs("\"\"", file) == EOF) return false;
        start = i + 1;
    }
    if (start != text.size() && std::fwrite(text.data() + start, 1, text.size() - start, file) != text.size() - start)
        return false;
    return std::fputc('"', file) != EOF;
}

template <class T> bool csv_integer(FILE* file, T value) {
    char text[32];
    auto converted = std::to_chars(text, text + sizeof(text), value);
    return converted.ec == std::errc{} &&
        std::fwrite(text, 1, static_cast<size_t>(converted.ptr - text), file) == static_cast<size_t>(converted.ptr - text);
}

bool csv_number(FILE* file, double value, bool integral = false) {
    if (!std::isfinite(value) || value < 0) return std::fputs("na", file) != EOF;
    // Avoid negative-zero spellings for idle readings.
    if (value == 0) value = 0;
    char text[64];
    auto converted = integral ? std::to_chars(text, text + sizeof(text), value, std::chars_format::fixed, 0) :
                               std::to_chars(text, text + sizeof(text), value, std::chars_format::general,
                                             std::numeric_limits<double>::max_digits10);
    return converted.ec == std::errc{} &&
        std::fwrite(text, 1, static_cast<size_t>(converted.ptr - text), file) == static_cast<size_t>(converted.ptr - text);
}

bool csv_optional_integer(FILE* file, int64_t value) {
    return value < 0 ? std::fputs("na", file) != EOF : csv_integer(file, value);
}

bool comma(FILE* file) { return std::fputc(',', file) != EOF; }

bool finish_row(FILE* file) {
    if (std::fputc('\n', file) == EOF) return false;
    // A completed sample must survive an ungraceful process exit; there is no render-thread I/O.
    int flushed = std::fflush(file);
    return flushed == 0 && std::ferror(file) == 0;
}

} // namespace

const MetricInfo& metric_info(Metric metric) { return metrics[metric]; }

const char* reason_name(Reason reason) {
    switch (reason) {
    case Reason::None: return "none";
    case Reason::Disabled: return "disabled";
    case Reason::Unsupported: return "unsupported";
    case Reason::ReadError: return "read error";
    case Reason::Warmup: return "warmup";
    case Reason::Invalid: return "invalid";
    case Reason::CounterReset: return "counter reset";
    case Reason::NoDevice: return "no device";
    case Reason::Overflow: return "overflow";
    }
    return "invalid";
}

Value checked(Metric metric, Value value) {
    if (!value.available()) return value;
    if (static_cast<unsigned>(metric) >= static_cast<unsigned>(kMetrics)) return {0, Reason::Invalid};
    const auto& info = metrics[metric];
    double n = value.number;
    if (!std::isfinite(n) || n < info.minimum || n > info.maximum ||
        ((metric == kGpuTemp || metric == kCpuTemp) && n == 0) ||
        (integer_metric(metric) && std::trunc(n) != n)) return {0, Reason::Invalid};
    return value;
}

bool field_mask(const char* text, Mask& mask, std::string& error) {
    error.clear();
    if (!text || !*text) {
        error = "expected a nonempty metric list";
        return false;
    }
    std::string_view remaining(text);
    bool all = remaining.substr(0, remaining.find(',')) == "all";
    Mask selected = all ? all_metrics : 0;
    bool first = true, bad_syntax = false;
    std::vector<std::string_view> unknown;
    for (;;) {
        size_t end = remaining.find(',');
        std::string_view token = remaining.substr(0, end);
        if (!(first && all)) {
            bool minus = !token.empty() && token.front() == '-';
            std::string_view name = minus ? token.substr(1) : token;
            if (name.empty() || name == "all" || minus != all) bad_syntax = true;
            if (!name.empty() && name != "all") {
                Metric metric = find_metric(name);
                if (metric == kMetrics) {
                    if (std::find(unknown.begin(), unknown.end(), name) == unknown.end()) unknown.push_back(name);
                } else if (minus) selected &= ~bit(metric);
                else selected |= bit(metric);
            }
        }
        if (end == std::string_view::npos) break;
        remaining.remove_prefix(end + 1);
        first = false;
    }
    if (bad_syntax) error = "expected 'all,-name,...' or 'name,name,...'";
    for (std::string_view name : unknown) {
        if (!error.empty()) error += "; ";
        error += "unknown metric '";
        error.append(name);
        error += '\'';
    }
    if (!error.empty()) return false;
    mask = selected; // All exclusions may deliberately select zero fields; 'none' is not a metric.
    return true;
}

Config configuration(GetEnv get, std::vector<std::string>& errors) {
    Config config;
    const char* log = get("RX_VK_PERF_LOG");
    const char* fields = get("RX_VK_PERF_FIELDS");
    const char* interval = get("RX_VK_PERF_INTERVAL_MS");
    const char* window = get("RX_VK_PERF_WINDOW_MS");
    const char* units = get("RX_VK_PERF_UNITS");
    const char* compact = get("RX_VK_PERF_COMPACT");
    // The ordinary off path does not parse, allocate, probe files/sensors, or read any clock.
    if (!log && !fields && !interval && !window && !units && !compact) return config;
    auto invalid = [&](const char* name, const char* detail) {
        config.valid = false;
        errors.emplace_back(std::string(name) + ": " + detail);
    };
    if (log) {
        if (*log) config.log_path = log; // Literal UTF-8 path; opening/creation belongs to the sampler.
        else invalid("RX_VK_PERF_LOG", "expected a nonempty path");
    }
    if (fields) {
        std::string error;
        if (!field_mask(fields, config.fields, error)) invalid("RX_VK_PERF_FIELDS", error.c_str());
    }
    if (interval && !milliseconds(interval, config.interval_ms)) invalid("RX_VK_PERF_INTERVAL_MS", "expected decimal 100..60000");
    if (window && !milliseconds(window, config.window_ms)) invalid("RX_VK_PERF_WINDOW_MS", "expected decimal 100..60000");
    if (units) {
        if (std::strcmp(units, "si") == 0) config.si = true;
        else if (std::strcmp(units, "iec") != 0) invalid("RX_VK_PERF_UNITS", "expected iec or si");
    }
    if (compact) {
        if (std::strcmp(compact, "1") == 0) config.compact = true;
        else if (std::strcmp(compact, "0") != 0) invalid("RX_VK_PERF_COMPACT", "expected 0 or 1");
    }
    return config;
}

std::string format(Metric metric, Value value, bool si) {
    value = checked(metric, value);
    if (!value.available()) return {};
    double n = value.number == 0 ? 0 : value.number;
    if (byte_metric(metric)) {
        double mega = si ? 1000000.0 : 1048576.0;
        double giga = si ? 1000000000.0 : 1073741824.0;
        return display_number(n / (n < giga ? mega : giga), 1, n < giga ? (si ? "MB" : "MiB") : (si ? "GB" : "GiB"));
    }
    if (metric == kGpuThrottle) {
        char text[32];
        int length = std::snprintf(text, sizeof(text), "0x%llx", static_cast<unsigned long long>(n));
        return length >= 0 && static_cast<size_t>(length) < sizeof(text) ? std::string(text, static_cast<size_t>(length)) : std::string{};
    }
    const auto& info = metrics[metric];
    int decimals = integer_metric(metric) ? 0 : (std::strcmp(info.unit, "ms") == 0 ? 2 : 1);
    return display_number(n, decimals, info.unit);
}

void csv_header(FILE* file) {
    if (!file || std::fprintf(file, "# hookshot_perf,1,%s", common_columns) < 0) return;
    for (const auto& info : metrics)
        if (std::fprintf(file, ",%s", info.name) < 0) return;
    std::fputc('\n', file);
}

bool csv_started(FILE* file, const char* utc, const char* version) {
    return file && std::fputs("# started,", file) != EOF && csv_text(file, utc ? utc : "") && comma(file) &&
        csv_text(file, version ? version : "") && finish_row(file);
}

bool csv_row(FILE* file, const Sample& sample) {
    if (!file || !csv_text(file, sample.kind) || !comma(file) || !csv_integer(file, sample.sample) ||
        !comma(file) || !csv_optional_integer(file, sample.monotonic_ns) ||
        !comma(file) || !csv_number(file, sample.elapsed_s) ||
        !comma(file) || !csv_optional_integer(file, sample.game_frame) ||
        !comma(file) || !csv_optional_integer(file, sample.renderer_present) ||
        !comma(file) || !csv_optional_integer(file, sample.frame) ||
        !comma(file) || !csv_number(file, sample.time_s) ||
        !comma(file) || !csv_text(file, sample.clock) ||
        !comma(file) || !csv_text(file, sample.thread_name) ||
        !comma(file) || !csv_text(file, sample.event) ||
        !comma(file) || !csv_text(file, sample.detail)) return false;
    for (int i = 0; i < kMetrics; ++i) {
        Metric metric = static_cast<Metric>(i);
        Value value = checked(metric, sample.values[i]);
        if (!comma(file)) return false;
        if (value.available()) {
            if (!csv_number(file, value.number, integer_metric(metric))) return false;
        } else if (std::fputs("na", file) == EOF) return false;
    }
    return finish_row(file);
}

std::array<Value, kMetrics> frame_statistics(const std::vector<double>& ms) {
    std::array<Value, kMetrics> result{};
    auto unavailable = [&](Reason reason) {
        for (int i = kMeanMs; i <= kFrames; ++i) result[i] = {0, reason};
        return result;
    };
    if (ms.empty()) return unavailable(Reason::Warmup);
    for (double n : ms)
        if (!std::isfinite(n) || n <= 0) return unavailable(Reason::Invalid);
    std::vector<double> ordered(ms);
    std::sort(ordered.begin(), ordered.end());
    size_t count = ordered.size();
    auto mean_slowest = [&](size_t n) {
        // Scale before summing: neither a large sum nor dividing tiny intervals first may lose the mean,
        // including on Windows where long double has double's range.
        long double scale = ordered.back(), sum = 0, correction = 0;
        for (size_t i = count - n; i < count; ++i) {
            long double part = static_cast<long double>(ordered[i]) / scale - correction;
            long double next = sum + part;
            correction = (next - sum) - part;
            sum = next;
        }
        return static_cast<double>((sum / n) * scale);
    };
    auto set = [&](Metric metric, double n) { result[metric] = checked(metric, Value::of(n)); };
    double mean = mean_slowest(count);
    set(kMeanMs, mean);
    set(kMeanFps, 1000 / mean);
    set(kLow1Fps, 1000 / mean_slowest(count / 100 + (count % 100 != 0)));
    set(kLow01Fps, 1000 / mean_slowest(count / 1000 + (count % 1000 != 0)));
    auto percentile = [&](size_t p) {
        size_t rank = (count / 100) * p + ((count % 100) * p + 99) / 100;
        return ordered[rank - 1];
    };
    set(kP50Ms, percentile(50));
    set(kP95Ms, percentile(95));
    set(kP99Ms, percentile(99));
    set(kMaxMs, ordered.back());
    constexpr double thresholds[] = {16.7, 20, 25, 50, 100};
    for (int i = 0; i < 5; ++i) {
        auto first = std::upper_bound(ordered.begin(), ordered.end(), thresholds[i]);
        set(static_cast<Metric>(kOver167 + i), static_cast<double>(ordered.end() - first));
    }
    set(kFrames, static_cast<double>(count));
    return result;
}

} // namespace mkwvk::perf_monitor
