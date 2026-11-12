// (hookshot) Sampler-only OS readers. No Vulkan calls, vendor headers or mandatory vendor library.
#define _CRT_SECURE_NO_WARNINGS
#include "perf_monitor.h"

#include <algorithm>
#include <charconv>
#include <cstring>
#include <filesystem>
#include <limits>
#include <string_view>
#include <utility>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <psapi.h>
#include <tlhelp32.h>
#else
#include <dlfcn.h>
#include <pthread.h>
#include <sys/resource.h>
#include <sys/syscall.h>
#include <time.h>
#include <unistd.h>
#endif

namespace mkwvk::perf_monitor {
namespace {

constexpr Mask gpu_fields = bit(kGpuUse) | bit(kGpuTemp) | bit(kGpuPower) | bit(kGpuFan) |
                            bit(kGpuClock) | bit(kGpuThrottle) | bit(kGpuMemory);
Value unavailable(Reason reason) { return {0, reason}; }

std::string_view trim(std::string_view s) {
    const auto first = s.find_first_not_of(" \t\r\n");
    if (first == s.npos) return {};
    return s.substr(first, s.find_last_not_of(" \t\r\n") - first + 1);
}
std::string_view token(std::string_view& s) {
    s = trim(s);
    const auto end = s.find_first_of(" \t\r\n");
    const auto result = s.substr(0, end);
    s = end == s.npos ? std::string_view{} : s.substr(end);
    return result;
}
bool unsigned_number(std::string_view text, uint64_t& value, int base = 10) {
    text = trim(text);
    if (text.empty()) return false;
    const auto result = std::from_chars(text.data(), text.data() + text.size(), value, base);
    return result.ec == std::errc{} && result.ptr == text.data() + text.size();
}
bool numbered(std::string_view name, std::string_view prefix, std::string_view suffix = {}) {
    if (!name.starts_with(prefix) || !name.ends_with(suffix) || name.size() <= prefix.size() + suffix.size()) return false;
    name = name.substr(prefix.size(), name.size() - prefix.size() - suffix.size());
    return name.find_first_not_of("0123456789") == name.npos;
}
std::string pci_identity(std::string_view text) {
    // Accept only a complete PCI function, never a bus/device prefix or a filesystem path.
    if (text.size() != 12 || text[4] != ':' || text[7] != ':' || text[10] != '.') return {};
    for (size_t i = 0; i < text.size(); ++i)
        if (i != 4 && i != 7 && i != 10 && std::string_view("0123456789abcdefABCDEF").find(text[i]) == std::string_view::npos) return {};
    uint64_t domain = 0, bus = 0, device = 0, function = 0;
    if (!unsigned_number(text.substr(0, 4), domain, 16) || !unsigned_number(text.substr(5, 2), bus, 16) ||
        !unsigned_number(text.substr(8, 2), device, 16) || !unsigned_number(text.substr(11, 1), function, 16) ||
        device > 31 || function > 7) return {};
    char result[13];
    std::snprintf(result, sizeof result, "%04x:%02x:%02x.%x", unsigned(domain), unsigned(bus), unsigned(device), unsigned(function));
    return result;
}
bool gpu_uuid(std::string_view text) {
    if (text.size() != 40 || !text.starts_with("GPU-")) return false;
    for (size_t i = 4; i < text.size(); ++i) {
        if (i == 12 || i == 17 || i == 22 || i == 27) {
            if (text[i] != '-') return false;
        } else if (std::string_view("0123456789abcdefABCDEF").find(text[i]) == std::string_view::npos) return false;
    }
    return true;
}

struct Counter {
    uint64_t id = 0, birth = 0, ticks = 0;
    Reason reason = Reason::ReadError;
};
struct ThreadCounter { Counter cpu; std::string name; };
struct Capture {
    std::array<Value, kMetrics> values{};
    Counter process;
    std::vector<ThreadCounter> threads;
    Reason threads_reason = Reason::ReadError;
    uint64_t ticks_per_second = 0;
};

bool proc_stat(std::string_view text, Counter& counter, std::string* name = nullptr) {
    const auto open = text.find('('), close = text.rfind(')');
    if (open == text.npos || close == text.npos || close <= open || close + 1 >= text.size() || text[close + 1] != ' ') return false;
    uint64_t id = 0, user = 0, kernel = 0, birth = 0;
    if (!unsigned_number(text.substr(0, open), id) || !id) return false;
    std::string_view fields = text.substr(close + 1);
    for (unsigned field = 3; field <= 22; ++field) {
        const auto item = token(fields);
        if (item.empty()) return false;
        if (field == 3 && item.size() != 1) return false;
        if (field == 14 && !unsigned_number(item, user)) return false;
        if (field == 15 && !unsigned_number(item, kernel)) return false;
        if (field == 22 && !unsigned_number(item, birth)) return false;
    }
    if (user > std::numeric_limits<uint64_t>::max() - kernel) return false;
    counter = {id, birth, user + kernel, Reason::None};
    if (name) name->assign(text.substr(open + 1, close - open - 1));
    return true;
}
Value keyed_number(std::string_view text, std::string_view key, bool kib) {
    while (!text.empty()) {
        const auto end = text.find('\n');
        auto line = trim(text.substr(0, end));
        text = end == text.npos ? std::string_view{} : text.substr(end + 1);
        if (!line.starts_with(key) || line.size() <= key.size() || line[key.size()] != ':') continue;
        line.remove_prefix(key.size() + 1);
        uint64_t number = 0;
        if (!unsigned_number(token(line), number) || (kib && token(line) != "kB") || !trim(line).empty())
            return unavailable(Reason::Invalid);
        if (kib && number > std::numeric_limits<uint64_t>::max() / 1024) return unavailable(Reason::Overflow);
        return Value::of(double(number) * (kib ? 1024.0 : 1.0));
    }
    return unavailable(Reason::Unsupported);
}
Value cpu_delta(const Counter& current, const Counter& previous, int64_t now, int64_t before, uint64_t frequency) {
    if (current.reason != Reason::None) return unavailable(current.reason);
    if (frequency == 0 || now < 0 || before < 0) return unavailable(Reason::Invalid);
    if (previous.reason != Reason::None || current.id != previous.id || current.birth != previous.birth)
        return unavailable(Reason::Warmup);
    if (now <= before || current.ticks < previous.ticks) return unavailable(Reason::CounterReset);
    // CPU 100% is one logical CPU; process usage may exceed 100%.
    return Value::of(double(current.ticks - previous.ticks) / double(frequency) *
                     (100.0 * 1e9 / double(now - before)));
}

// This input boundary also lets the fixture drive the real Reader without probing its host.
class Inputs {
public:
    virtual ~Inputs() = default;
    virtual void capture(Mask requested, Capture& result) = 0;
};
class SystemReader final : public Reader {
    std::unique_ptr<Inputs> inputs;
    Capture current;
    Counter previous_process;
    int64_t process_time = 0, thread_time = 0;
    std::vector<Counter> previous_threads, next_threads;
    std::string busiest;
public:
    explicit SystemReader(std::unique_ptr<Inputs> source) : inputs(std::move(source)) {}
    void begin(int64_t now, Mask requested) override {
        current.values.fill(unavailable(Reason::Unsupported));
        current.process = {};
        current.threads.clear();
        current.threads_reason = Reason::ReadError;
        current.ticks_per_second = 0;
        busiest.clear();
        if (requested) inputs->capture(requested, current);
        if (requested & bit(kCpuProcess)) {
            current.values[kCpuProcess] = cpu_delta(current.process, previous_process, now, process_time, current.ticks_per_second);
            previous_process = current.process;
            process_time = now;
        } else previous_process = {};
        if (requested & bit(kCpuThread)) {
            Value best = unavailable(current.threads_reason == Reason::None ? Reason::Warmup : current.threads_reason);
            std::sort(current.threads.begin(), current.threads.end(), [](const ThreadCounter& a, const ThreadCounter& b) {
                return a.cpu.id < b.cpu.id;
            });
            next_threads.clear();
            size_t old = 0;
            for (const auto& thread : current.threads) {
                while (old < previous_threads.size() && previous_threads[old].id < thread.cpu.id) ++old;
                const Counter missing;
                const Counter& previous = old < previous_threads.size() && previous_threads[old].id == thread.cpu.id ? previous_threads[old] : missing;
                const Value use = checked(kCpuThread, cpu_delta(thread.cpu, previous, now, thread_time, current.ticks_per_second));
                if (use.available() && (!best.available() || use.number > best.number)) {
                    best = use;
                    busiest = thread.name;
                } else if (!best.available() && use.reason != Reason::Warmup) best = use;
                if (thread.cpu.reason == Reason::None) next_threads.push_back(thread.cpu);
            }
            if (current.threads_reason != Reason::None) {
                // A failed enumeration has no coherent population to compare next time.
                next_threads.clear();
                best = unavailable(current.threads_reason);
                busiest.clear();
            }
            previous_threads.swap(next_threads);
            thread_time = now;
            current.values[kCpuThread] = best;
        } else previous_threads.clear();
        for (unsigned i = 0; i < kMetrics; ++i)
            current.values[i] = requested & bit(Metric(i)) ? checked(Metric(i), current.values[i]) : unavailable(Reason::Disabled);
    }
    Value read(Metric metric) const override { return current.values[metric]; }
    std::string busiest_thread_name() const override { return busiest; }
};

// Minimal declarations of the public NVML ABI, authored here; no NVIDIA SDK source or headers.
// NVML uses the C calling convention (including on Windows).
struct NvDeviceOpaque;
using NvDevice = NvDeviceOpaque*;
struct NvUtilization { unsigned gpu, memory; };
struct NvMemory { unsigned long long total, free, used; };
using NvInit = int (*)();
using NvHandle = int (*)(const char*, NvDevice*);
using NvUtil = int (*)(NvDevice, NvUtilization*);
using NvUnsigned = int (*)(NvDevice, unsigned*);
using NvSelectedUnsigned = int (*)(NvDevice, unsigned, unsigned*);
using NvThrottle = int (*)(NvDevice, unsigned long long*);
using NvMem = int (*)(NvDevice, NvMemory*);
using Symbol = void (*)();
class Library {
public:
    virtual ~Library() = default;
    virtual bool open() = 0;
    virtual Symbol symbol(const char* name) = 0;
};
class NativeLibrary final : public Library {
#ifdef _WIN32
    HMODULE handle = nullptr;
#else
    void* handle = nullptr;
#endif
public:
    ~NativeLibrary() override {
        if (!handle) return;
#ifdef _WIN32
        FreeLibrary(handle);
#else
        dlclose(handle);
#endif
    }
    bool open() override {
#ifdef _WIN32
        handle = LoadLibraryExW(L"nvml.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
#else
        handle = dlopen("libnvidia-ml.so.1", RTLD_NOW | RTLD_LOCAL);
#endif
        return handle != nullptr;
    }
    Symbol symbol(const char* name) override {
#ifdef _WIN32
        const auto address = GetProcAddress(handle, name);
#else
        const auto address = dlsym(handle, name);
#endif
        Symbol result = nullptr;
        static_assert(sizeof result == sizeof address);
        std::memcpy(&result, &address, sizeof result);
        return result;
    }
};
Reason nv_reason(int status) {
    if (status == 0) return Reason::None;
    if (status == 3 || status == 13) return Reason::Unsupported; // not supported / function not found
    if (status == 6 || status == 15) return Reason::NoDevice; // not found / GPU lost
    return Reason::ReadError;
}
class Nvml {
    Device selected;
    std::unique_ptr<Library> library;
    bool attempted = false, initialized = false;
    Reason state = Reason::Unsupported;
    NvDevice device = nullptr;
    NvInit shutdown = nullptr;
    NvUtil utilization = nullptr;
    NvSelectedUnsigned temperature = nullptr, clock = nullptr;
    NvUnsigned power = nullptr, fan = nullptr;
    NvThrottle throttle = nullptr;
    NvMem memory = nullptr;
    template<class T> T load(const char* name) {
        const Symbol address = library->symbol(name);
        T result = nullptr;
        static_assert(sizeof result == sizeof address);
        std::memcpy(&result, &address, sizeof result);
        return result;
    }
    void initialize() {
        attempted = true;
        // The runtime contract uses only the selected Vulkan UUID for NVML.
        if (!gpu_uuid(selected.uuid)) { state = Reason::NoDevice; return; }
        if (!library->open()) return;
        const auto init = load<NvInit>("nvmlInit_v2");
        shutdown = load<NvInit>("nvmlShutdown");
        if (!init || !shutdown) return;
        state = nv_reason(init());
        if (state != Reason::None) return;
        initialized = true;
        state = Reason::NoDevice;
        if (const auto by_uuid = load<NvHandle>("nvmlDeviceGetHandleByUUID"))
            state = nv_reason(by_uuid(selected.uuid.c_str(), &device));
        if (state != Reason::None || !device) { device = nullptr; if (state == Reason::None) state = Reason::NoDevice; return; }
        utilization = load<NvUtil>("nvmlDeviceGetUtilizationRates");
        temperature = load<NvSelectedUnsigned>("nvmlDeviceGetTemperature");
        power = load<NvUnsigned>("nvmlDeviceGetPowerUsage");
        fan = load<NvUnsigned>("nvmlDeviceGetFanSpeed");
        clock = load<NvSelectedUnsigned>("nvmlDeviceGetClockInfo");
        throttle = load<NvThrottle>("nvmlDeviceGetCurrentClocksThrottleReasons");
        memory = load<NvMem>("nvmlDeviceGetMemoryInfo");
    }
public:
    Nvml(const Device& selection, std::unique_ptr<Library> source) : selected(selection), library(std::move(source)) {}
    ~Nvml() { if (initialized) shutdown(); }
    Value read(Metric metric) {
        if (selected.vendor_id != 0x10de) return unavailable(Reason::Unsupported);
        if (!attempted) initialize();
        if (state != Reason::None) return unavailable(state);
        unsigned number = 0;
        int status = 3;
        switch (metric) {
        case kGpuUse: {
            NvUtilization result{};
            if (utilization) status = utilization(device, &result);
            number = result.gpu;
            break;
        }
        case kGpuTemp: if (temperature) status = temperature(device, 0, &number); break;
        case kGpuPower: if (power) status = power(device, &number); break;
        case kGpuFan: if (fan) status = fan(device, &number); break;
        case kGpuClock: if (clock) status = clock(device, 0, &number); break; // graphics clock
        case kGpuThrottle: {
            unsigned long long mask = 0;
            if (throttle) status = throttle(device, &mask);
            if (status == 0 && mask > (uint64_t{1} << 53)) return unavailable(Reason::Overflow);
            return status == 0 ? Value::of(double(mask)) : unavailable(nv_reason(status));
        }
        case kGpuMemory: {
            NvMemory result{};
            if (memory) status = memory(device, &result);
            if (status == 0 && (result.used > result.total || result.free > result.total)) return unavailable(Reason::Invalid);
            return status == 0 ? Value::of(double(result.used)) : unavailable(nv_reason(status));
        }
        default: return unavailable(Reason::Unsupported);
        }
        return status == 0 ? Value::of(double(number) / (metric == kGpuPower ? 1000.0 : 1.0)) : unavailable(nv_reason(status));
    }
};

// Files is deliberately small: fake proc/sysfs trees use the same parsers on both host OSes.
class Files {
public:
    virtual ~Files() = default;
    virtual bool read(const std::string& path, std::string& text) = 0;
    virtual bool list(const std::string& path, std::vector<std::string>& names) = 0;
};
#ifndef _WIN32
class NativeFiles final : public Files {
public:
    bool read(const std::string& path, std::string& text) override {
        text.clear();
        FILE* file = std::fopen(path.c_str(), "rb");
        if (!file) return false;
        char buffer[4096];
        size_t count = 0;
        while ((count = std::fread(buffer, 1, sizeof buffer, file)) != 0) text.append(buffer, count);
        const bool ok = !std::ferror(file);
        std::fclose(file);
        return ok;
    }
    bool list(const std::string& path, std::vector<std::string>& names) override {
        names.clear();
        std::error_code error;
        std::filesystem::directory_iterator it(path, error), end;
        while (!error && it != end) {
            names.push_back(it->path().filename().string());
            it.increment(error);
        }
        return !error;
    }
};
#endif

class LinuxInputs final : public Inputs {
    Device selected;
    std::unique_ptr<Files> files;
    Nvml nvml;
    uint64_t frequency;
    std::string text;
    bool cpu_sensors_found = false, gpu_found = false;
    std::vector<std::string> cpu_temperatures, cpu_frequencies;
    bool cpu_frequency_found = false;
    std::string gpu_path, gpu_hwmon, intel_clock;
    Reason gpu_state = Reason::NoDevice;

    Value number(const std::string& path, double scale = 1) {
        if (!files->read(path, text)) return unavailable(Reason::ReadError);
        uint64_t result = 0;
        if (!unsigned_number(text, result)) return unavailable(Reason::Invalid);
        return Value::of(double(result) * scale);
    }
    Value maximum(Metric metric, const std::vector<std::string>& paths, double scale) {
        Value result = unavailable(Reason::Unsupported);
        for (const auto& path : paths) {
            const Value value = checked(metric, number(path, scale));
            if (value.available()) {
                if (!result.available() || value.number > result.number) result = value;
            } else if (!result.available()) result = value;
        }
        return result;
    }
    void discover_cpu_temperature() {
        cpu_sensors_found = true;
        std::vector<std::string> monitors, entries;
        if (!files->list("/sys/class/hwmon", monitors)) return;
        for (const auto& monitor : monitors) {
            if (!numbered(monitor, "hwmon")) continue;
            const std::string path = "/sys/class/hwmon/" + monitor;
            if (!files->read(path + "/name", text)) continue;
            const bool intel = trim(text) == "coretemp", amd = trim(text) == "k10temp";
            if ((!intel && !amd) || !files->list(path, entries)) continue;
            for (const auto& entry : entries) {
                if (!numbered(entry, "temp", "_label") || !files->read(path + "/" + entry, text)) continue;
                const auto label = trim(text);
                // Tctl may include an artificial control offset; Tdie is the package die temperature.
                if ((intel && numbered(label, "Package id ")) || (amd && label == "Tdie"))
                    cpu_temperatures.push_back(path + "/" + entry.substr(0, entry.size() - 6) + "_input");
            }
        }
    }
    void discover_cpu_frequency() {
        cpu_frequency_found = true;
        std::vector<std::string> cores;
        if (!files->list("/sys/devices/system/cpu", cores)) return;
        for (const auto& core : cores)
            if (numbered(core, "cpu")) cpu_frequencies.push_back("/sys/devices/system/cpu/" + core + "/cpufreq/scaling_cur_freq");
    }
    void discover_gpu() {
        gpu_found = true;
        const auto pci = pci_identity(selected.pci_address);
        if (pci.empty()) return;
        gpu_path = "/sys/bus/pci/devices/" + pci;
        if (!files->read(gpu_path + "/vendor", text)) { gpu_state = Reason::ReadError; return; }
        auto vendor_text = trim(text);
        if (vendor_text.starts_with("0x")) vendor_text.remove_prefix(2);
        uint64_t vendor = 0;
        if (!unsigned_number(vendor_text, vendor, 16)) { gpu_state = Reason::Invalid; return; }
        if (vendor != selected.vendor_id) return;
        gpu_state = Reason::None;
    }
    void discover_gpu_hwmon() {
        // A single named monitor beneath the exact selected PCI device, never a global hwmon guess.
        std::vector<std::string> monitors;
        if (!files->list(gpu_path + "/hwmon", monitors)) return;
        for (const auto& monitor : monitors) {
            if (!numbered(monitor, "hwmon")) continue;
            const auto path = gpu_path + "/hwmon/" + monitor;
            if (!files->read(path + "/name", text)) continue;
            const auto name = trim(text);
            if ((selected.vendor_id == 0x1002 && name == "amdgpu") ||
                (selected.vendor_id == 0x8086 && (name == "i915" || name == "xe"))) {
                if (!gpu_hwmon.empty()) { gpu_hwmon.clear(); return; }
                gpu_hwmon = path;
            }
        }
    }
    Value gpu(Metric metric) {
        if (selected.vendor_id == 0x10de) return nvml.read(metric);
        if (selected.vendor_id != 0x1002 && selected.vendor_id != 0x8086) return unavailable(Reason::Unsupported);
        // No portable Intel utilization or dedicated-VRAM reading; no synthesized global busy value.
        if (metric == kGpuThrottle || (selected.vendor_id == 0x8086 && metric != kGpuClock && metric != kGpuTemp))
            return unavailable(Reason::Unsupported);
        if (!gpu_found) discover_gpu();
        if (gpu_state != Reason::None) return unavailable(gpu_state);
        if (metric == kGpuUse) return number(gpu_path + "/gpu_busy_percent");
        if (metric == kGpuMemory) return number(gpu_path + "/mem_info_vram_used");
        if (metric == kGpuClock && selected.vendor_id == 0x8086) {
            if (intel_clock.empty()) {
                std::vector<std::string> cards;
                if (!files->list(gpu_path + "/drm", cards)) return unavailable(Reason::ReadError);
                for (const auto& card : cards) {
                    if (!numbered(card, "card")) continue;
                    if (!intel_clock.empty()) { intel_clock.clear(); return unavailable(Reason::Unsupported); }
                    intel_clock = gpu_path + "/drm/" + card + "/gt_act_freq_mhz";
                }
            }
            return intel_clock.empty() ? unavailable(Reason::Unsupported) : number(intel_clock);
        }
        if (gpu_hwmon.empty()) discover_gpu_hwmon();
        if (gpu_hwmon.empty()) return unavailable(Reason::Unsupported);
        if (metric == kGpuTemp) return number(gpu_hwmon + "/temp1_input", 0.001);
        if (metric == kGpuPower) return number(gpu_hwmon + "/power1_average", 0.000001);
        if (metric == kGpuClock) return number(gpu_hwmon + "/freq1_input", 0.000001); // amdgpu GFX clock, Hz
        if (metric == kGpuFan) {
            // amdgpu documents PWM duty and its maximum; fan1_input is RPM, not a percentage.
            const Value pwm = number(gpu_hwmon + "/pwm1"), maximum_pwm = number(gpu_hwmon + "/pwm1_max");
            if (!pwm.available()) return pwm;
            if (!maximum_pwm.available()) return maximum_pwm;
            if (maximum_pwm.number <= 0 || pwm.number > maximum_pwm.number) return unavailable(Reason::Invalid);
            return Value::of(100.0 * pwm.number / maximum_pwm.number);
        }
        return unavailable(Reason::Unsupported);
    }
    void battery(Mask requested, Capture& result) {
        std::vector<std::string> supplies;
        if (!files->list("/sys/class/power_supply", supplies)) {
            result.values[kBattery] = result.values[kAcOnline] = unavailable(Reason::ReadError);
            return;
        }
        unsigned packs = 0, mains = 0;
        bool unknown_type = false, ac_failed = false;
        Value capacity = unavailable(Reason::NoDevice), online = unavailable(Reason::NoDevice);
        for (const auto& supply : supplies) {
            const auto path = "/sys/class/power_supply/" + supply;
            if (!files->read(path + "/type", text)) { unknown_type = true; continue; }
            const auto type = trim(text);
            if ((requested & bit(kBattery)) && type == "Battery") {
                ++packs;
                capacity = number(path + "/capacity");
            } else if ((requested & bit(kAcOnline)) && type == "Mains") {
                ++mains;
                const auto value = checked(kAcOnline, number(path + "/online"));
                if (!value.available()) ac_failed = true;
                else if (!online.available() || value.number == 1) online = value;
            }
        }
        result.values[kBattery] = unknown_type ? unavailable(Reason::ReadError) :
            packs > 1 ? unavailable(Reason::Unsupported) : capacity;
        result.values[kAcOnline] = (unknown_type || (ac_failed && (!online.available() || online.number != 1))) ?
            unavailable(Reason::ReadError) : mains ? online : unavailable(Reason::NoDevice);
    }
public:
    LinuxInputs(const Device& device, std::unique_ptr<Files> source, uint64_t ticks, std::unique_ptr<Library> library)
        : selected(device), files(std::move(source)), nvml(device, std::move(library)), frequency(ticks) {}
    void capture(Mask requested, Capture& result) override {
        result.ticks_per_second = frequency;
        if (requested & bit(kCpuProcess)) {
            if (files->read("/proc/self/stat", text) && !proc_stat(text, result.process)) result.process.reason = Reason::Invalid;
        }
        if (requested & bit(kCpuThread)) {
            std::vector<std::string> ids;
            result.threads_reason = files->list("/proc/self/task", ids) ? Reason::None : Reason::ReadError;
            for (const auto& id : ids) {
                uint64_t tid = 0;
                if (!unsigned_number(id, tid) || !tid) continue;
                ThreadCounter thread;
                thread.cpu.id = tid;
                const auto path = "/proc/self/task/" + id;
                if (files->read(path + "/stat", text)) {
                    if (!proc_stat(text, thread.cpu, &thread.name) || thread.cpu.id != tid) thread.cpu.reason = Reason::Invalid;
                    if (thread.cpu.reason == Reason::None && files->read(path + "/comm", text)) {
                        if (!text.empty() && text.back() == '\n') text.pop_back();
                        thread.name = text;
                    }
                }
                result.threads.push_back(std::move(thread));
            }
        }
        if (requested & bit(kRss)) result.values[kRss] = files->read("/proc/self/status", text) ?
            keyed_number(text, "VmRSS", true) : unavailable(Reason::ReadError);
        // VmSize is not private commit, and is intentionally not used for kCommit.
        if (requested & (bit(kRamAvailable) | bit(kSwapUsed))) {
            if (!files->read("/proc/meminfo", text)) result.values[kRamAvailable] = result.values[kSwapUsed] = unavailable(Reason::ReadError);
            else {
                if (requested & bit(kRamAvailable)) result.values[kRamAvailable] = keyed_number(text, "MemAvailable", true);
                if (requested & bit(kSwapUsed)) {
                    const auto total = keyed_number(text, "SwapTotal", true), free = keyed_number(text, "SwapFree", true);
                    result.values[kSwapUsed] = !total.available() ? total : !free.available() ? free :
                        free.number > total.number ? unavailable(Reason::Invalid) : Value::of(total.number - free.number);
                }
            }
        }
        if (requested & bit(kDiskRead)) result.values[kDiskRead] = files->read("/proc/self/io", text) ?
            keyed_number(text, "read_bytes", false) : unavailable(Reason::ReadError); // cumulative physical storage bytes
        if (requested & bit(kCpuTemp)) {
            if (!cpu_sensors_found) discover_cpu_temperature();
            result.values[kCpuTemp] = maximum(kCpuTemp, cpu_temperatures, 0.001); // hottest named package
        }
        if (requested & bit(kCpuClock)) {
            if (!cpu_frequency_found) discover_cpu_frequency();
            result.values[kCpuClock] = maximum(kCpuClock, cpu_frequencies, 0.001);
        }
        if (requested & (bit(kBattery) | bit(kAcOnline))) battery(requested, result);
        if (requested & gpu_fields)
            for (unsigned i = kGpuUse; i <= kGpuMemory; ++i)
                if (requested & bit(Metric(i))) result.values[i] = gpu(Metric(i));
    }
};

#ifdef _WIN32
uint64_t filetime_ticks(const FILETIME& time) { return (uint64_t(time.dwHighDateTime) << 32) | time.dwLowDateTime; }
Counter windows_counter(uint64_t id, const FILETIME& birth, const FILETIME& kernel, const FILETIME& user) {
    const uint64_t k = filetime_ticks(kernel), u = filetime_ticks(user);
    if (u > std::numeric_limits<uint64_t>::max() - k) return {id, 0, 0, Reason::Overflow};
    return {id, filetime_ticks(birth), k + u, Reason::None};
}
template<class T> T kernel_proc(const char* name) {
    const auto address = GetProcAddress(GetModuleHandleW(L"kernel32.dll"), name);
    T result = nullptr;
    static_assert(sizeof result == sizeof address);
    std::memcpy(&result, &address, sizeof result);
    return result;
}
class WindowsInputs final : public Inputs {
    Nvml nvml;
    using MemoryInfo = BOOL (WINAPI*)(HANDLE, PPROCESS_MEMORY_COUNTERS, DWORD);
    using ThreadDescription = HRESULT (WINAPI*)(HANDLE, PWSTR*);
    MemoryInfo memory_info = nullptr;
    ThreadDescription thread_description = nullptr;
    bool memory_resolved = false, description_resolved = false;
    void threads(Capture& result) {
        if (!description_resolved) {
            description_resolved = true;
            thread_description = kernel_proc<ThreadDescription>("GetThreadDescription");
        }
        HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
        if (snapshot == INVALID_HANDLE_VALUE) return;
        THREADENTRY32 entry{};
        entry.dwSize = sizeof entry;
        const DWORD process = GetCurrentProcessId();
        BOOL found = Thread32First(snapshot, &entry);
        while (found) {
            if (entry.th32OwnerProcessID == process) {
                ThreadCounter thread;
                thread.cpu.id = entry.th32ThreadID;
                HANDLE handle = OpenThread(THREAD_QUERY_LIMITED_INFORMATION, FALSE, entry.th32ThreadID);
                if (handle) {
                    // The TID can be recycled after the snapshot; never inspect another process's thread.
                    if (GetProcessIdOfThread(handle) == process) {
                        FILETIME birth{}, exit{}, kernel{}, user{};
                        if (GetThreadTimes(handle, &birth, &exit, &kernel, &user))
                            thread.cpu = windows_counter(entry.th32ThreadID, birth, kernel, user);
                        if (thread_description) {
                            PWSTR description = nullptr;
                            if (SUCCEEDED(thread_description(handle, &description)) && description) {
                                const int size = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, description, -1, nullptr, 0, nullptr, nullptr);
                                if (size > 1) {
                                    thread.name.resize(size_t(size));
                                    if (WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, description, -1, thread.name.data(), size, nullptr, nullptr))
                                        thread.name.pop_back();
                                    else thread.name.clear();
                                }
                            }
                            if (description) LocalFree(description);
                        }
                    }
                    CloseHandle(handle);
                }
                result.threads.push_back(std::move(thread));
            }
            entry.dwSize = sizeof entry;
            found = Thread32Next(snapshot, &entry);
        }
        result.threads_reason = GetLastError() == ERROR_NO_MORE_FILES ? Reason::None : Reason::ReadError;
        CloseHandle(snapshot);
    }
public:
    explicit WindowsInputs(const Device& device) : nvml(device, std::make_unique<NativeLibrary>()) {}
    void capture(Mask requested, Capture& result) override {
        result.ticks_per_second = 10000000; // FILETIME units, not QPC units
        if (requested & bit(kCpuProcess)) {
            FILETIME birth{}, exit{}, kernel{}, user{};
            if (GetProcessTimes(GetCurrentProcess(), &birth, &exit, &kernel, &user))
                result.process = windows_counter(GetCurrentProcessId(), birth, kernel, user);
        }
        if (requested & bit(kCpuThread)) threads(result);
        if (requested & (bit(kRss) | bit(kCommit))) {
            if (!memory_resolved) {
                memory_resolved = true;
                memory_info = kernel_proc<MemoryInfo>("K32GetProcessMemoryInfo");
            }
            PROCESS_MEMORY_COUNTERS_EX memory{};
            memory.cb = sizeof memory;
            if (!memory_info) result.values[kRss] = result.values[kCommit] = unavailable(Reason::Unsupported);
            else if (!memory_info(GetCurrentProcess(), reinterpret_cast<PPROCESS_MEMORY_COUNTERS>(&memory), sizeof memory))
                result.values[kRss] = result.values[kCommit] = unavailable(Reason::ReadError);
            else {
                result.values[kRss] = Value::of(double(memory.WorkingSetSize));
                result.values[kCommit] = Value::of(double(memory.PrivateUsage));
            }
        }
        if (requested & bit(kRamAvailable)) {
            MEMORYSTATUSEX memory{};
            memory.dwLength = sizeof memory;
            result.values[kRamAvailable] = GlobalMemoryStatusEx(&memory) ? Value::of(double(memory.ullAvailPhys)) : unavailable(Reason::ReadError);
        }
        // ullTotalPageFile/ullAvailPageFile are commit accounting, not physical pagefile occupancy.
        // Windows CPU temperature/frequency and AMD/Intel telemetry have no supported source here.
        if (requested & bit(kDiskRead)) {
            IO_COUNTERS io{};
            result.values[kDiskRead] = GetProcessIoCounters(GetCurrentProcess(), &io) ?
                Value::of(double(io.ReadTransferCount)) : unavailable(Reason::ReadError);
            // Cumulative logical I/O, including cached/non-disk reads; unlike Linux read_bytes.
        }
        if (requested & (bit(kBattery) | bit(kAcOnline))) {
            SYSTEM_POWER_STATUS power{};
            if (!GetSystemPowerStatus(&power)) result.values[kBattery] = result.values[kAcOnline] = unavailable(Reason::ReadError);
            else {
                result.values[kBattery] = power.BatteryFlag == 255 ? unavailable(Reason::ReadError) :
                    (power.BatteryFlag & 128) ? unavailable(Reason::NoDevice) :
                    power.BatteryLifePercent == 255 ? unavailable(Reason::ReadError) : Value::of(power.BatteryLifePercent);
                result.values[kAcOnline] = power.ACLineStatus == 255 ? unavailable(Reason::ReadError) : Value::of(power.ACLineStatus);
            }
        }
        if (requested & gpu_fields)
            for (unsigned i = kGpuUse; i <= kGpuMemory; ++i)
                if (requested & bit(Metric(i))) result.values[i] = nvml.read(Metric(i));
    }
};
#endif

} // namespace

#ifndef HOOKSHOT_PERF_READERS_TEST
std::unique_ptr<Reader> make_system_reader(const Device& device) {
#ifdef _WIN32
    return std::make_unique<SystemReader>(std::make_unique<WindowsInputs>(device));
#else
    const long ticks = sysconf(_SC_CLK_TCK);
    return std::make_unique<SystemReader>(std::make_unique<LinuxInputs>(device, std::make_unique<NativeFiles>(),
        ticks > 0 ? uint64_t(ticks) : 0, std::make_unique<NativeLibrary>()));
#endif
}

void lower_sampler_priority() {
#ifdef _WIN32
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_LOWEST);
    using SetDescription = HRESULT (WINAPI*)(HANDLE, PCWSTR);
    if (const auto describe = kernel_proc<SetDescription>("SetThreadDescription"))
        describe(GetCurrentThread(), L"perf-sampler");
#else
    // Linux nice values are per thread: do not lower the whole process's priority.
    setpriority(PRIO_PROCESS, id_t(syscall(SYS_gettid)), 19);
    pthread_setname_np(pthread_self(), "perf-sampler");
#endif
}

int64_t sampler_thread_cpu_ns() {
#ifdef _WIN32
    FILETIME birth{}, exit{}, kernel{}, user{};
    if (!GetThreadTimes(GetCurrentThread(), &birth, &exit, &kernel, &user)) return -1;
    const uint64_t k = filetime_ticks(kernel), u = filetime_ticks(user);
    if (u > uint64_t(INT64_MAX) / 100 || k > uint64_t(INT64_MAX) / 100 - u) return -1;
    return int64_t((u + k) * 100);
#else
    timespec time{};
    if (clock_gettime(CLOCK_THREAD_CPUTIME_ID, &time) != 0 || time.tv_sec < 0 ||
        uint64_t(time.tv_sec) > uint64_t((INT64_MAX - 999999999) / 1000000000)) return -1;
    return int64_t(time.tv_sec) * 1000000000 + time.tv_nsec;
#endif
}

int64_t monotonic_now_ns() {
#ifdef _WIN32
    static const int64_t frequency = [] {
        LARGE_INTEGER value{};
        return QueryPerformanceFrequency(&value) && value.QuadPart > 0 ? int64_t(value.QuadPart) : int64_t{0};
    }();
    LARGE_INTEGER counter{};
    if (!frequency || !QueryPerformanceCounter(&counter) || counter.QuadPart < 0) return 0;
    // Divide first to avoid overflowing the counter * 1e9 product.
    const int64_t seconds = counter.QuadPart / frequency;
    if (seconds > (std::numeric_limits<int64_t>::max() - 999999999) / 1000000000) return 0;
    const int64_t fraction = int64_t(double(counter.QuadPart % frequency) / double(frequency) * 1e9);
    return seconds * 1000000000 + std::min<int64_t>(fraction, 999999999);
#else
    timespec time{};
    if (clock_gettime(CLOCK_MONOTONIC, &time) != 0 || time.tv_sec < 0 ||
        uint64_t(time.tv_sec) > uint64_t((std::numeric_limits<int64_t>::max() - 999999999) / 1000000000)) return 0;
    return int64_t(time.tv_sec) * 1000000000 + time.tv_nsec;
#endif
}
#endif

} // namespace mkwvk::perf_monitor
