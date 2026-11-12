#define _CRT_SECURE_NO_WARNINGS
#define HOOKSHOT_PERF_READERS_TEST
#include "../gpu/vk/perf_readers.cpp"

#include <cassert>
#include <cmath>
#include <map>
#include <set>

using namespace mkwvk::perf_monitor;

namespace {
void expect(const Reader& reader, Metric metric, double number) {
    const Value value = reader.read(metric);
    assert(value.available());
    assert(std::abs(value.number - number) < 0.000001);
}
void reason(const Reader& reader, Metric metric, Reason expected) { assert(reader.read(metric).reason == expected); }

struct FakeFiles final : Files {
    std::map<std::string, std::string> contents;
    std::map<std::string, std::vector<std::string>> directories;
    std::vector<std::string> reads, lists;
    bool read(const std::string& path, std::string& text) override {
        reads.push_back(path);
        const auto found = contents.find(path);
        text = found == contents.end() ? "" : found->second;
        return found != contents.end();
    }
    bool list(const std::string& path, std::vector<std::string>& names) override {
        lists.push_back(path);
        const auto found = directories.find(path);
        names = found == directories.end() ? std::vector<std::string>{} : found->second;
        return found != directories.end();
    }
    void clear_calls() { reads.clear(); lists.clear(); }
};
struct NeverLibrary final : Library {
    bool open() override { assert(false && "unit test attempted a vendor library load"); return false; }
    Symbol symbol(const char*) override { assert(false); return nullptr; }
};
struct LinuxFixture {
    FakeFiles* files;
    std::unique_ptr<Reader> reader;
    explicit LinuxFixture(Device device = {}) {
        auto source = std::make_unique<FakeFiles>();
        files = source.get();
        reader = std::make_unique<SystemReader>(std::make_unique<LinuxInputs>(device, std::move(source), 100,
            std::make_unique<NeverLibrary>()));
    }
    void begin(int64_t ns, Mask mask) { reader->begin(ns, mask); }
};
std::string stat_line(uint64_t id, const std::string& name, uint64_t user, uint64_t kernel, uint64_t birth) {
    std::string result = std::to_string(id) + " (" + name + ") R";
    for (unsigned i = 4; i <= 22; ++i) {
        const uint64_t value = i == 14 ? user : i == 15 ? kernel : i == 22 ? birth : 0;
        result += " " + std::to_string(value);
    }
    return result + " 1234 99\n";
}
void put_thread(FakeFiles& files, uint64_t id, uint64_t ticks, uint64_t birth, const std::string& name) {
    const auto path = "/proc/self/task/" + std::to_string(id);
    files.contents[path + "/stat"] = stat_line(id, name, ticks, 0, birth);
    files.contents[path + "/comm"] = name + "\n";
}

void parsers() {
    Counter counter;
    std::string name;
    assert(proc_stat(stat_line(7, "render ) weird (thread))", 120, 30, 55), counter, &name));
    assert(counter.id == 7 && counter.ticks == 150 && counter.birth == 55);
    assert(name == "render ) weird (thread))");
    assert(!proc_stat("7 (truncated) R 1 2 3", counter));
    assert(!proc_stat("7 no parentheses R", counter));
    assert(!proc_stat(stat_line(7, "overflow", UINT64_MAX, 1, 55), counter));
    auto malformed = stat_line(7, "bad", 10, 0, 55);
    malformed.replace(malformed.find(" 10 "), 4, " -1 ");
    assert(!proc_stat(malformed, counter));
    assert(keyed_number("VmRSS: 12 kB\n", "VmRSS", true).number == 12288);
    assert(keyed_number("RssAnon: 12 kB\n", "VmRSS", true).reason == Reason::Unsupported);
    assert(keyed_number("VmRSS: -1 kB\n", "VmRSS", true).reason == Reason::Invalid);
    assert(keyed_number("VmRSS: 12 MB\n", "VmRSS", true).reason == Reason::Invalid);
    assert(keyed_number("VmRSS: 12 kB junk\n", "VmRSS", true).reason == Reason::Invalid);
    assert(keyed_number("VmRSS: 18446744073709551615 kB\n", "VmRSS", true).reason == Reason::Overflow);
    assert(pci_identity("0000:AF:1F.7") == "0000:af:1f.7");
    for (const auto* invalid : {"", "01:00.0", "0000:01:20.0", "0000:01:00.8", "0000: 1:00.0", "../../card0", "0000:01:00.0/x"})
        assert(pci_identity(invalid).empty());
    assert(gpu_uuid("GPU-01234567-89ab-cdef-0123-456789abcdef"));
    assert(!gpu_uuid("GPU-01234567-89ab-cdef-0123-456789abcdeg"));
    assert(!gpu_uuid("GPU-01234567"));
    const Counter before{1, 7, 100, Reason::None}, after{1, 7, 150, Reason::None};
    assert(cpu_delta(after, before, 1000000000, 0, 100).number == 50);
    assert(cpu_delta(after, before, 1000000000, 0, 0).reason == Reason::Invalid);
    assert(cpu_delta(after, before, INT64_MAX, INT64_MIN, 100).reason == Reason::Invalid);
    assert(cpu_delta({1, 8, 9000, Reason::None}, before, 1000000000, 0, 100).reason == Reason::Warmup);
    assert(cpu_delta({1, 7, 99, Reason::None}, before, 1000000000, 0, 100).reason == Reason::CounterReset);
}

void proc_counters() {
    LinuxFixture fixture;
    auto& files = *fixture.files;
    auto& reader = *fixture.reader;
    fixture.begin(1000000000, 0);
    assert(files.reads.empty() && files.lists.empty());
    reason(reader, kRss, Reason::Disabled);
    files.contents["/proc/self/stat"] = stat_line(1, "game (main))", 120, 80, 5);
    files.directories["/proc/self/task"] = {"11", "22", "not-a-thread"};
    put_thread(files, 11, 100, 7, "render ) worker");
    put_thread(files, 22, 300, 8, "compile pool");
    const Mask mask = bit(kCpuProcess) | bit(kCpuThread);
    fixture.begin(1000000000, mask);
    reason(reader, kCpuProcess, Reason::Warmup);
    reason(reader, kCpuThread, Reason::Warmup);
    assert(reader.busiest_thread_name().empty());
    files.contents["/proc/self/stat"] = stat_line(1, "game (main))", 320, 80, 5);
    put_thread(files, 11, 125, 7, "render ) worker");
    put_thread(files, 22, 375, 8, "compile pool");
    fixture.begin(2000000000, mask);
    expect(reader, kCpuProcess, 200); // not divided by the PC's logical CPU count
    expect(reader, kCpuThread, 75);
    assert(reader.busiest_thread_name() == "compile pool");

    // A new thread and a reused TID cannot contribute lifetime CPU to this interval.
    files.directories["/proc/self/task"] = {"11", "22", "33"};
    put_thread(files, 11, 175, 7, "render ) worker");
    put_thread(files, 22, 9000, 99, "reused tid");
    put_thread(files, 33, 99999, 100, "new thread");
    fixture.begin(3000000000, mask);
    expect(reader, kCpuThread, 50);
    assert(reader.busiest_thread_name() == "render ) worker");
    put_thread(files, 11, 1, 7, "counter reset");
    files.contents.erase("/proc/self/task/22/stat");
    put_thread(files, 33, 100024, 100, "new thread");
    fixture.begin(4000000000, mask);
    expect(reader, kCpuThread, 25); // one unreadable/reset thread cannot hide another valid delta
    assert(reader.busiest_thread_name() == "new thread");

    files.directories["/proc/self/task"] = {"11"};
    put_thread(files, 11, 0, 7, "counter reset");
    files.contents["/proc/self/stat"] = stat_line(1, "main", 1, 0, 5);
    fixture.begin(5000000000, mask);
    reason(reader, kCpuProcess, Reason::CounterReset);
    reason(reader, kCpuThread, Reason::CounterReset);
    fixture.begin(5000000000, mask);
    reason(reader, kCpuProcess, Reason::CounterReset); // nonpositive interval
    reason(reader, kCpuThread, Reason::CounterReset);
    files.contents.erase("/proc/self/stat");
    files.directories.erase("/proc/self/task");
    fixture.begin(6000000000, mask);
    reason(reader, kCpuProcess, Reason::ReadError);
    reason(reader, kCpuThread, Reason::ReadError);
    files.contents["/proc/self/stat"] = stat_line(1, "main", 80, 0, 5);
    files.directories["/proc/self/task"] = {"11"};
    put_thread(files, 11, 100, 7, "from stat");
    files.contents.erase("/proc/self/task/11/comm");
    fixture.begin(7000000000, mask);
    reason(reader, kCpuProcess, Reason::Warmup);
    reason(reader, kCpuThread, Reason::Warmup);
    put_thread(files, 11, 125, 7, "from stat");
    files.contents.erase("/proc/self/task/11/comm");
    fixture.begin(8000000000, mask);
    expect(reader, kCpuThread, 25);
    assert(reader.busiest_thread_name() == "from stat");
    fixture.begin(9000000000, 0);
    fixture.begin(10000000000, mask);
    reason(reader, kCpuProcess, Reason::Warmup);
    reason(reader, kCpuThread, Reason::Warmup);
}

void proc_memory_and_selection() {
    LinuxFixture fixture;
    auto& files = *fixture.files;
    auto& reader = *fixture.reader;
    files.contents["/proc/self/status"] = "VmSize: 9000 kB\nVmRSS: 512 kB\n";
    files.contents["/proc/meminfo"] = "MemAvailable: 4096 kB\nSwapTotal: 512 kB\nSwapFree: 128 kB\n";
    files.contents["/proc/self/io"] = "rchar: 50000\nread_bytes: 4096\n";
    fixture.begin(1000000000, bit(kRss));
    expect(reader, kRss, 512 * 1024);
    assert(files.reads == std::vector<std::string>{"/proc/self/status"});
    assert(files.lists.empty());
    files.clear_calls();
    fixture.begin(2000000000, bit(kCommit));
    reason(reader, kCommit, Reason::Unsupported);
    assert(files.reads.empty() && files.lists.empty());
    const Mask mask = bit(kRss) | bit(kRamAvailable) | bit(kSwapUsed) | bit(kDiskRead);
    fixture.begin(3000000000, mask);
    expect(reader, kRamAvailable, 4096 * 1024);
    expect(reader, kSwapUsed, 384 * 1024);
    expect(reader, kDiskRead, 4096);
    files.contents.erase("/proc/self/status");
    files.contents["/proc/meminfo"] = "MemAvailable: 2048 kB\nSwapTotal: 128 kB\nSwapFree: 512 kB\n";
    fixture.begin(7000000000, mask);
    reason(reader, kRss, Reason::ReadError);
    reason(reader, kSwapUsed, Reason::Invalid);
    expect(reader, kRamAvailable, 2048 * 1024);
    expect(reader, kDiskRead, 4096); // cumulative, not an interval rate or delta
    files.contents["/proc/self/io"] = "rchar: 999\nread_bytes: -1\n";
    fixture.begin(8000000000, bit(kDiskRead));
    reason(reader, kDiskRead, Reason::Invalid);
}

void cpu_sensors_and_power() {
    LinuxFixture fixture;
    auto& files = *fixture.files;
    auto& reader = *fixture.reader;
    files.directories["/sys/class/hwmon"] = {"hwmon0", "hwmon1", "hwmon2", "hwmon3"};
    files.contents["/sys/class/hwmon/hwmon0/name"] = "coretemp\n";
    files.directories["/sys/class/hwmon/hwmon0"] = {"temp1_label", "temp2_label", "temp3_label"};
    files.contents["/sys/class/hwmon/hwmon0/temp1_label"] = "Package id 0\n";
    files.contents["/sys/class/hwmon/hwmon0/temp1_input"] = "65000\n";
    files.contents["/sys/class/hwmon/hwmon0/temp2_label"] = "Core 0\n";
    files.contents["/sys/class/hwmon/hwmon0/temp2_input"] = "99000\n";
    files.contents["/sys/class/hwmon/hwmon0/temp3_label"] = "Package id 1\n";
    files.contents["/sys/class/hwmon/hwmon0/temp3_input"] = "255000\n";
    files.contents["/sys/class/hwmon/hwmon1/name"] = "k10temp\n";
    files.directories["/sys/class/hwmon/hwmon1"] = {"temp1_label", "temp2_label"};
    files.contents["/sys/class/hwmon/hwmon1/temp1_label"] = "Tctl\n";
    files.contents["/sys/class/hwmon/hwmon1/temp1_input"] = "90000\n";
    files.contents["/sys/class/hwmon/hwmon1/temp2_label"] = "Tdie\n";
    files.contents["/sys/class/hwmon/hwmon1/temp2_input"] = "70000\n";
    files.contents["/sys/class/hwmon/hwmon2/name"] = "acpitz\n";
    files.contents["/sys/class/hwmon/hwmon2/temp1_input"] = "88000\n";
    files.directories["/sys/devices/system/cpu"] = {"cpu0", "cpu1", "cpu2", "cpu3", "cpufreq"};
    files.contents["/sys/devices/system/cpu/cpu0/cpufreq/scaling_cur_freq"] = "3200000\n";
    files.contents["/sys/devices/system/cpu/cpu1/cpufreq/scaling_cur_freq"] = "4500000\n";
    files.contents["/sys/devices/system/cpu/cpu2/cpufreq/scaling_cur_freq"] = "not-a-clock\n";
    fixture.begin(1, bit(kCpuTemp));
    expect(reader, kCpuTemp, 70);
    assert(std::find(files.lists.begin(), files.lists.end(), "/sys/devices/system/cpu") == files.lists.end());
    assert(std::find(files.reads.begin(), files.reads.end(), "/sys/class/hwmon/hwmon0/temp2_input") == files.reads.end());
    fixture.begin(2, bit(kCpuClock));
    expect(reader, kCpuClock, 4500);

    files.directories["/sys/class/power_supply"] = {"BAT0", "AC0"};
    files.contents["/sys/class/power_supply/BAT0/scope"] = "System\n";
    files.contents["/sys/class/power_supply/AC0/scope"] = "System\n";
    files.contents["/sys/class/power_supply/BAT0/type"] = "Battery\n";
    files.contents["/sys/class/power_supply/BAT0/capacity"] = "67\n";
    files.contents["/sys/class/power_supply/AC0/type"] = "Mains\n";
    files.contents["/sys/class/power_supply/AC0/online"] = "1\n";
    files.clear_calls();
    fixture.begin(3, bit(kAcOnline));
    expect(reader, kAcOnline, 1);
    assert(std::find(files.reads.begin(), files.reads.end(), "/sys/class/power_supply/BAT0/capacity") == files.reads.end());
    fixture.begin(4, bit(kBattery) | bit(kAcOnline));
    expect(reader, kBattery, 67);
    files.contents["/sys/class/power_supply/BAT0/capacity"] = "101\n";
    fixture.begin(5, bit(kBattery) | bit(kAcOnline));
    reason(reader, kBattery, Reason::Invalid);
    expect(reader, kAcOnline, 1);
    files.directories["/sys/class/power_supply"].push_back("BAT1");
    files.contents["/sys/class/power_supply/BAT1/scope"] = "System\n";
    files.contents["/sys/class/power_supply/BAT1/type"] = "Battery\n";
    files.contents["/sys/class/power_supply/BAT1/capacity"] = "23\n";
    fixture.begin(6, bit(kBattery) | bit(kAcOnline));
    reason(reader, kBattery, Reason::Unsupported); // no guessed average for multiple packs
    expect(reader, kAcOnline, 1);
    files.directories["/sys/class/power_supply"].clear();
    fixture.begin(7, bit(kBattery) | bit(kAcOnline));
    reason(reader, kBattery, Reason::NoDevice);
    reason(reader, kAcOnline, Reason::NoDevice);
    // Regression: a desktop's HID++ mouse battery was reported as the computer's charge.
    files.directories["/sys/class/power_supply"] = {"hidpp_battery_0"};
    files.contents["/sys/class/power_supply/hidpp_battery_0/scope"] = "Device\n";
    files.contents["/sys/class/power_supply/hidpp_battery_0/type"] = "Battery\n";
    files.contents["/sys/class/power_supply/hidpp_battery_0/capacity"] = "77\n";
    files.clear_calls();
    fixture.begin(8, bit(kBattery) | bit(kAcOnline));
    reason(reader, kBattery, Reason::NoDevice);
    reason(reader, kAcOnline, Reason::NoDevice);
    assert(std::find(files.reads.begin(), files.reads.end(), "/sys/class/power_supply/hidpp_battery_0/capacity") == files.reads.end());
    files.directories["/sys/class/power_supply"].push_back("BAT0");
    files.contents["/sys/class/power_supply/BAT0/capacity"] = "67\n";
    fixture.begin(9, bit(kBattery));
    expect(reader, kBattery, 67); // The peripheral does not turn one system pack into an ambiguous pair.
    files.contents.erase("/sys/class/power_supply/BAT0/scope");
    fixture.begin(10, bit(kBattery));
    reason(reader, kBattery, Reason::ReadError);
}

void sysfs_gpu_identity() {
    const std::string path = "/sys/bus/pci/devices/0000:03:00.1";
    LinuxFixture amd({0x1002, "", "0000:03:00.1"});
    auto& files = *amd.files;
    auto& reader = *amd.reader;
    files.contents[path + "/vendor"] = "0x1002\n";
    files.contents[path + "/gpu_busy_percent"] = "48\n";
    files.contents[path + "/mem_info_vram_used"] = "1048576\n";
    files.directories[path + "/hwmon"] = {"hwmon7"};
    const auto hwmon = path + "/hwmon/hwmon7";
    files.contents[hwmon + "/name"] = "amdgpu\n";
    files.contents[hwmon + "/temp1_input"] = "61000\n";
    files.contents[hwmon + "/fan1_input"] = "1500\n"; // cannot become gpu_fan_pct
    files.contents[hwmon + "/freq1_input"] = "1800000000\n";
    amd.begin(1, bit(kGpuUse));
    expect(reader, kGpuUse, 48);
    assert(files.lists.empty()); // no unrequested hwmon or global DRM probing
    assert((files.reads == std::vector<std::string>{path + "/vendor", path + "/gpu_busy_percent"}));
    amd.begin(2, gpu_fields);
    expect(reader, kGpuTemp, 61);
    expect(reader, kGpuClock, 1800);
    expect(reader, kGpuMemory, 1048576);
    reason(reader, kGpuPower, Reason::ReadError);
    reason(reader, kGpuFan, Reason::ReadError);
    reason(reader, kGpuThrottle, Reason::Unsupported);
    assert(std::find(files.reads.begin(), files.reads.end(), hwmon + "/fan1_input") == files.reads.end());
    files.contents[hwmon + "/pwm1"] = "0\n";
    files.contents[hwmon + "/pwm1_max"] = "255\n";
    files.contents[hwmon + "/power1_average"] = "125500000\n";
    files.contents[path + "/gpu_busy_percent"] = "101\n";
    files.contents[hwmon + "/temp1_input"] = "0\n";
    amd.begin(3, gpu_fields);
    expect(reader, kGpuFan, 0);
    expect(reader, kGpuPower, 125.5);
    reason(reader, kGpuUse, Reason::Invalid);
    reason(reader, kGpuTemp, Reason::Invalid);
    files.contents[hwmon + "/pwm1"] = "256\n";
    amd.begin(4, bit(kGpuFan));
    reason(reader, kGpuFan, Reason::Invalid);

    LinuxFixture wrong({0x1002, "", "0000:03:00.0"});
    wrong.files->contents["/sys/bus/pci/devices/0000:03:00.0/vendor"] = "0x8086\n";
    wrong.files->contents[path + "/gpu_busy_percent"] = "50\n";
    wrong.begin(1, bit(kGpuUse));
    reason(*wrong.reader, kGpuUse, Reason::NoDevice);
    assert(wrong.files->reads.size() == 1);
    LinuxFixture no_identity({0x1002, "", ""});
    no_identity.begin(1, gpu_fields);
    reason(*no_identity.reader, kGpuUse, Reason::NoDevice);
    assert(no_identity.files->reads.empty() && no_identity.files->lists.empty());

    LinuxFixture intel({0x8086, "", "0000:00:02.0"});
    const std::string ipath = "/sys/bus/pci/devices/0000:00:02.0";
    intel.files->contents[ipath + "/vendor"] = "0x8086\n";
    intel.files->directories[ipath + "/drm"] = {"renderD128", "card2", "card2-DP-1"};
    intel.files->contents[ipath + "/drm/card2/gt_act_freq_mhz"] = "0\n";
    intel.files->contents["/sys/class/drm/card0/gt_act_freq_mhz"] = "999\n";
    intel.begin(1, bit(kGpuUse) | bit(kGpuMemory));
    reason(*intel.reader, kGpuUse, Reason::Unsupported);
    reason(*intel.reader, kGpuMemory, Reason::Unsupported);
    assert(intel.files->reads.empty() && intel.files->lists.empty());
    intel.begin(2, bit(kGpuClock));
    expect(*intel.reader, kGpuClock, 0); // idle zero is valid; card0 is not selected
    assert(std::find(intel.files->reads.begin(), intel.files->reads.end(), "/sys/class/drm/card0/gt_act_freq_mhz") == intel.files->reads.end());
}

struct FakeInputs final : Inputs {
    Capture sample;
    unsigned calls = 0;
    Mask requested = 0;
    void capture(Mask mask, Capture& result) override {
        ++calls;
        requested = mask;
        result = sample;
    }
};
void fake_reader_interface() {
    auto input = std::make_unique<FakeInputs>();
    auto* source = input.get();
    std::unique_ptr<Reader> reader = std::make_unique<SystemReader>(std::move(input));
    source->sample.ticks_per_second = 10000000; // authored Windows FILETIME-unit counters, on both OSes
    source->sample.process = {17, 100, 30000000, Reason::None};
    source->sample.threads_reason = Reason::None;
    source->sample.threads = {{{18, 200, 10000000, Reason::None}, "render"}};
    source->sample.values[kRss] = Value::of(123456);
    source->sample.values[kCommit] = Value::of(654321);
    source->sample.values[kDiskRead] = Value::of(9000);
    source->sample.values[kSwapUsed] = unavailable(Reason::Unsupported);
    reader->begin(1000000000, 0);
    assert(source->calls == 0);
    const Mask mask = bit(kCpuProcess) | bit(kCpuThread) | bit(kRss) | bit(kCommit) | bit(kDiskRead) | bit(kSwapUsed);
    reader->begin(1000000000, mask);
    reason(*reader, kCpuProcess, Reason::Warmup);
    reason(*reader, kCpuThread, Reason::Warmup);
    assert(source->requested == mask);
    source->sample.process.ticks += 15000000;
    source->sample.threads[0].cpu.ticks += 5000000;
    reader->begin(2000000000, mask);
    expect(*reader, kCpuProcess, 150);
    expect(*reader, kCpuThread, 50);
    assert(reader->busiest_thread_name() == "render");
    expect(*reader, kRss, 123456);
    expect(*reader, kCommit, 654321);
    expect(*reader, kDiskRead, 9000);
    reason(*reader, kSwapUsed, Reason::Unsupported);
    source->sample.values[kRss] = Value::of(std::numeric_limits<double>::quiet_NaN());
    source->sample.values[kCommit] = Value::of(-1);
    source->sample.values[kGpuPower] = Value::of(std::numeric_limits<double>::infinity());
    reader->begin(3000000000, mask | bit(kGpuPower));
    reason(*reader, kRss, Reason::Invalid);
    reason(*reader, kCommit, Reason::Invalid);
    reason(*reader, kGpuPower, Reason::Invalid);
    expect(*reader, kDiskRead, 9000);
    reason(*reader, kBattery, Reason::Disabled);
}

struct NvState {
    unsigned opens = 0, inits = 0, shutdowns = 0, handles = 0;
    int init_status = 0, identity_status = 0;
    bool opens_ok = true;
    std::array<unsigned, kMetrics> calls{};
    std::array<int, kMetrics> errors{};
    std::array<unsigned, kMetrics> numbers{};
    std::set<std::string> missing;
    std::string uuid;
    NvMemory memory{1000, 600, 400};
    unsigned long long throttle = 4;
};
NvState* active_nv = nullptr;
NvDevice fake_device() { return reinterpret_cast<NvDevice>(active_nv); }
int nv_init() { ++active_nv->inits; return active_nv->init_status; }
int nv_shutdown() { ++active_nv->shutdowns; return 0; }
int nv_handle(const char* uuid, NvDevice* device) {
    ++active_nv->handles;
    active_nv->uuid = uuid;
    if (!active_nv->identity_status) *device = fake_device();
    return active_nv->identity_status;
}
int nv_value(NvDevice device, Metric metric, unsigned* value) {
    assert(device == fake_device());
    ++active_nv->calls[metric];
    *value = active_nv->numbers[metric];
    return active_nv->errors[metric];
}
int nv_util(NvDevice device, NvUtilization* value) { return nv_value(device, kGpuUse, &value->gpu); }
int nv_temp(NvDevice device, unsigned sensor, unsigned* value) { assert(sensor == 0); return nv_value(device, kGpuTemp, value); }
int nv_power(NvDevice device, unsigned* value) { return nv_value(device, kGpuPower, value); }
int nv_fan(NvDevice device, unsigned* value) { return nv_value(device, kGpuFan, value); }
int nv_clock(NvDevice device, unsigned type, unsigned* value) { assert(type == 0); return nv_value(device, kGpuClock, value); }
int nv_throttle(NvDevice device, unsigned long long* value) {
    assert(device == fake_device());
    ++active_nv->calls[kGpuThrottle];
    *value = active_nv->throttle;
    return active_nv->errors[kGpuThrottle];
}
int nv_memory(NvDevice device, NvMemory* value) {
    assert(device == fake_device());
    ++active_nv->calls[kGpuMemory];
    *value = active_nv->memory;
    return active_nv->errors[kGpuMemory];
}
template<class T> Symbol function_symbol(T function) {
    Symbol result = nullptr;
    static_assert(sizeof function == sizeof result);
    std::memcpy(&result, &function, sizeof result);
    return result;
}
struct FakeLibrary final : Library {
    NvState& state;
    explicit FakeLibrary(NvState& s) : state(s) {}
    bool open() override { active_nv = &state; ++state.opens; return state.opens_ok; }
    Symbol symbol(const char* name) override {
        assert(active_nv == &state);
        if (state.missing.contains(name)) return nullptr;
        const std::string_view symbol_name(name);
        if (symbol_name == "nvmlInit_v2") return function_symbol(nv_init);
        if (symbol_name == "nvmlShutdown") return function_symbol(nv_shutdown);
        if (symbol_name == "nvmlDeviceGetHandleByUUID") return function_symbol(nv_handle);
        if (symbol_name == "nvmlDeviceGetUtilizationRates") return function_symbol(nv_util);
        if (symbol_name == "nvmlDeviceGetTemperature") return function_symbol(nv_temp);
        if (symbol_name == "nvmlDeviceGetPowerUsage") return function_symbol(nv_power);
        if (symbol_name == "nvmlDeviceGetFanSpeed") return function_symbol(nv_fan);
        if (symbol_name == "nvmlDeviceGetClockInfo") return function_symbol(nv_clock);
        if (symbol_name == "nvmlDeviceGetCurrentClocksThrottleReasons") return function_symbol(nv_throttle);
        if (symbol_name == "nvmlDeviceGetMemoryInfo") return function_symbol(nv_memory);
        assert(false && "unexpected NVML API: no device-index or write calls permitted");
        return nullptr;
    }
};
std::unique_ptr<Reader> nv_reader(const Device& device, NvState& state) {
    return std::make_unique<SystemReader>(std::make_unique<LinuxInputs>(device, std::make_unique<FakeFiles>(), 100,
        std::make_unique<FakeLibrary>(state)));
}
void nvml_optional_identity_and_failures() {
    const Device selected{0x10de, "GPU-01234567-89ab-cdef-0123-456789abcdef", "0000:03:00.0"};
    NvState state;
    state.numbers[kGpuUse] = 31;
    state.numbers[kGpuTemp] = 64;
    state.numbers[kGpuPower] = 125500;
    state.numbers[kGpuFan] = 0;
    state.numbers[kGpuClock] = 0;
    {
        auto reader = nv_reader(selected, state);
        reader->begin(1, bit(kCommit));
        assert(state.opens == 0);
        reader->begin(2, bit(kGpuUse));
        expect(*reader, kGpuUse, 31);
        assert(state.opens == 1 && state.inits == 1 && state.handles == 1 && state.uuid == selected.uuid);
        for (unsigned i = kGpuTemp; i <= kGpuMemory; ++i) assert(state.calls[i] == 0);
        reader->begin(3, gpu_fields);
        expect(*reader, kGpuTemp, 64);
        expect(*reader, kGpuPower, 125.5);
        expect(*reader, kGpuFan, 0);
        expect(*reader, kGpuClock, 0);
        expect(*reader, kGpuThrottle, 4);
        expect(*reader, kGpuMemory, 400);
        state.errors[kGpuTemp] = 4; // permissions, independent of utilization/power
        state.errors[kGpuFan] = 3;
        state.memory.used = 1001;
        state.throttle = (uint64_t{1} << 53) + 1;
        reader->begin(4, gpu_fields);
        reason(*reader, kGpuTemp, Reason::ReadError);
        reason(*reader, kGpuFan, Reason::Unsupported);
        reason(*reader, kGpuMemory, Reason::Invalid);
        reason(*reader, kGpuThrottle, Reason::Overflow);
        expect(*reader, kGpuUse, 31);
        expect(*reader, kGpuPower, 125.5);
        assert(state.opens == 1);
    }
    assert(state.shutdowns == 1);
    for (const uint32_t vendor : {0x1002u, 0x8086u}) {
        NvState non_nvidia;
        auto reader = nv_reader({vendor, selected.uuid, ""}, non_nvidia);
        reader->begin(1, gpu_fields);
        assert(non_nvidia.opens == 0 && non_nvidia.calls[kGpuUse] == 0);
    }
    for (const auto& uuid : {std::string{}, std::string("GPU-short")}) {
        NvState invalid;
        auto reader = nv_reader({0x10de, uuid, selected.pci_address}, invalid);
        reader->begin(1, gpu_fields);
        reason(*reader, kGpuUse, Reason::NoDevice);
        assert(invalid.opens == 0); // UUID-only runtime contract; PCI cannot silently select another NVML device
    }
    NvState absent;
    absent.opens_ok = false;
    {
        auto reader = nv_reader(selected, absent);
        reader->begin(1, gpu_fields);
        reason(*reader, kGpuUse, Reason::Unsupported);
        reader->begin(2, gpu_fields);
        assert(absent.opens == 1 && absent.inits == 0);
    }
    assert(absent.shutdowns == 0);
    NvState missing;
    missing.missing.insert("nvmlDeviceGetPowerUsage");
    {
        auto reader = nv_reader(selected, missing);
        reader->begin(1, bit(kGpuPower) | bit(kGpuFan));
        reason(*reader, kGpuPower, Reason::Unsupported);
        expect(*reader, kGpuFan, 0);
        assert(missing.calls[kGpuPower] == 0);
    }
    NvState unidentified;
    unidentified.identity_status = 6;
    {
        auto reader = nv_reader(selected, unidentified);
        reader->begin(1, gpu_fields);
        reason(*reader, kGpuMemory, Reason::NoDevice);
        assert(unidentified.calls[kGpuMemory] == 0);
    }
    assert(unidentified.shutdowns == 1);
    NvState no_shutdown;
    no_shutdown.missing.insert("nvmlShutdown");
    {
        auto reader = nv_reader(selected, no_shutdown);
        reader->begin(1, gpu_fields);
        reason(*reader, kGpuUse, Reason::Unsupported);
        assert(no_shutdown.inits == 0);
    }
    NvState init_failed;
    init_failed.init_status = 4;
    {
        auto reader = nv_reader(selected, init_failed);
        reader->begin(1, gpu_fields);
        reason(*reader, kGpuUse, Reason::ReadError);
        assert(init_failed.handles == 0);
    }
    assert(init_failed.shutdowns == 0);
}
} // namespace

int main() {
    parsers();
    proc_counters();
    proc_memory_and_selection();
    cpu_sensors_and_power();
    sysfs_gpu_identity();
    fake_reader_interface();
    nvml_optional_identity_and_failures();
    std::puts("PASS: sampled readers use authored proc/sysfs/counter/NVML inputs, never native hardware");
}
