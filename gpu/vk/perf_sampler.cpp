#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif
#include "perf_sampler.h"
#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cmath>
#include <cstring>
#include <ctime>
#include <deque>
#include <filesystem>
#include <utility>
#include <stdexcept>
#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#include <share.h>
#include <sys/stat.h>
#else
#include <fcntl.h>
#include <unistd.h>
#endif

namespace mkwvk::perf_monitor {
FILE* create_log(const std::string& requested, std::string& actual, std::string& error) {
    namespace fs = std::filesystem;
    std::error_code ec;
    fs::path base(reinterpret_cast<const char8_t*>(requested.c_str()));
    if (fs::is_directory(base, ec)) base /= "perf.csv";
    else if (ec && ec != std::errc::no_such_file_or_directory) {
        error = "cannot inspect output path: " + ec.message();
        return nullptr;
    }
    for (unsigned n = 0; n < 10000; n++) {
        fs::path path = base;
        if (n) {
            fs::path name = base.stem();
            name += "-" + std::to_string(n);
            name += base.extension();
            path = base.parent_path() / name;
        }
        int fd = -1;
#ifdef _WIN32
        const errno_t result = _wsopen_s(&fd, path.c_str(), _O_CREAT | _O_EXCL | _O_WRONLY | _O_BINARY,
                                         _SH_DENYNO, _S_IREAD | _S_IWRITE);
        if (result != 0) errno = result;
#else
        fd = open(path.c_str(), O_CREAT | O_EXCL | O_WRONLY | O_CLOEXEC | O_NOFOLLOW, 0600);
#endif
        if (fd < 0) {
            if (errno == EEXIST) continue;
            error = "cannot create output: " + std::string(std::strerror(errno));
            return nullptr;
        }
#ifdef _WIN32
        FILE* file = _fdopen(fd, "wb");
#else
        FILE* file = fdopen(fd, "wb");
#endif
        if (!file) {
            error = "cannot open output stream: " + std::string(std::strerror(errno));
#ifdef _WIN32
            _close(fd);
#else
            close(fd);
#endif
            return nullptr;
        }
        const auto utf8 = path.u8string();
        actual.assign(reinterpret_cast<const char*>(utf8.data()), utf8.size());
        return file;
    }
    error = "no unused numbered output name";
    return nullptr;
}

Sampler::Sampler(Config settings, Device gpu, std::string frame_clock, std::string build_version, Report logger,
                 std::atomic<bool>& active, ReaderFactory make)
    : config(std::move(settings)), device(std::move(gpu)), clock(std::move(frame_clock)), version(std::move(build_version)),
      report(logger), factory(make), collecting(active) {
    for (int i = kVramUsed; i <= kGpuMs; i++) renderer.values[i] = {0, Reason::Warmup};
    for (int i = kPipelines; i <= kGpuWaitMs; i++) renderer.values[i] = {0, Reason::Warmup};
}
Sampler::~Sampler() { stop(); }
bool Sampler::start() {
    if (!config.enabled() || worker.joinable()) return false;
    try {
        queue = std::make_unique<FrameQueue<65536>>();
        started_ns = monotonic_now_ns();
        collecting.store(true, std::memory_order_release);
        worker = std::thread(&Sampler::run, this);
    }
    catch (const std::exception& e) {
        collecting.store(false, std::memory_order_release);
        if (report) report((std::string("vk: perf: cannot start sampler: ") + e.what()).c_str());
        return false;
    }
    return true;
}
void Sampler::stop() {
    collecting.store(false, std::memory_order_release);
    { std::lock_guard lock(sleep_mu); stopping = true; }
    wake.notify_one();
    if (worker.joinable()) worker.join();
}
bool Sampler::publish(const RenderSample& snapshot) {
    std::unique_lock lock(render_mu, std::try_to_lock);
    if (!lock) { snapshot_wanted.store(true, std::memory_order_relaxed); return false; }
    renderer = snapshot;
    return true;
}

void Sampler::run() {
    lower_sampler_priority();
    const int64_t cpu_begin = sampler_thread_cpu_ns();
    uint64_t emitted_samples = 0;
    FILE* file = nullptr;
    const auto say = [&](const std::string& text) { if (report) report(("vk: perf: " + text).c_str()); };
    try {
        std::string actual, error;
        file = create_log(config.log_path, actual, error);
        if (!file) { say(error); collecting.store(false, std::memory_order_release); return; }
        csv_header(file);
        const std::time_t utc_now = std::time(nullptr);
        std::tm utc{};
#ifdef _WIN32
        const bool utc_ok = gmtime_s(&utc, &utc_now) == 0;
#else
        const bool utc_ok = gmtime_r(&utc_now, &utc) != nullptr;
#endif
        char started[32] = "na";
        if (utc_ok) std::strftime(started, sizeof started, "%Y-%m-%dT%H:%M:%SZ", &utc);
        if (!csv_started(file, started, version.c_str())) throw std::runtime_error("cannot write header");
        if (std::fprintf(file, "# gpu_vendor,0x%04x\n", static_cast<unsigned>(device.vendor_id)) < 0 || std::fflush(file) != 0)
            throw std::runtime_error("cannot write GPU vendor comment");
        say("writing " + actual + "; frame I/O stays on the sampler thread");
        std::unique_ptr<Reader> reader = factory(device);
        if (!reader) throw std::runtime_error("system reader unavailable");
        const int64_t begin = started_ns;
        int64_t next = begin, previous_render = -1;
        uint64_t sequence = 0, old_dropped = 0;
        std::array<Value, kMetrics> previous{};
        std::array<bool, kMetrics> told{};
        std::array<bool, kMetrics> told_invalid{};
        std::vector<double> all, window;
        const bool frame_stats = (config.fields & ((bit(kFrames) << 1) - 1)) != 0;
        std::deque<FrameSample> recent;
        FrameSample last;
        bool started_frames = false, incomplete = false, write_failed = false;
        const auto common = [&](Sample& s, int64_t now) {
            s.clock = clock; s.sample = sequence; s.monotonic_ns = now; s.elapsed_s = (now - begin) / 1e9;
            s.game_frame = last.game_frame; s.renderer_present = last.renderer_present;
            s.frame = last.frame; s.time_s = last.time_s;
        };
        const auto emit = [&](const Sample& s) {
            if (!csv_row(file, s)) { write_failed = true; throw std::runtime_error("cannot write or flush sample"); }
        };
        const auto drain = [&] {
            FrameSample f;
            while (queue->pop(f)) {
                last = f;
                if (!started_frames) {
                    started_frames = true;
                    Sample marker; common(marker, f.at_ns); marker.kind = "marker";
                    marker.event = "capture_start"; marker.detail = "session"; emit(marker);
                    continue; // The boundary is an endpoint, never an interval from before capture began.
                }
                if (frame_stats && f.ms > 0 && std::isfinite(f.ms)) { all.push_back(f.ms); recent.push_back(f); }
            }
            const uint64_t lost = queue->dropped();
            if (lost != old_dropped) {
                incomplete = true;
                if (!old_dropped) say("frame queue overflow; frame statistics unavailable for this capture");
                old_dropped = lost;
            }
        };
        const auto selected = [&](Sample& s, bool explain = true) {
            for (int i = 0; i < kMetrics; i++) {
                const Metric m = static_cast<Metric>(i);
                Value& value = s.values[i];
                if (!(config.fields & bit(m))) value = {0, Reason::Disabled};
                else value = checked(m, value);
                if (!explain || value.available() || value.reason == Reason::Disabled) continue;
                // Warm-up is expected and temporary; report it once, and still report a later invalid sensor once.
                bool& seen = value.reason == Reason::Invalid ? told_invalid[i] : told[i];
                if (!seen) { seen = true; say(std::string(metric_info(m).name) + ": " + reason_name(value.reason)); }
            }
        };
        bool stop_now = false;
        while (!stop_now) {
            drain();
            const int64_t now = monotonic_now_ns();
            while (!recent.empty() && recent.front().at_ns <= now - static_cast<int64_t>(config.window_ms) * 1000000)
                recent.pop_front();
            window.clear();
            window.reserve(recent.size());
            for (const auto& f : recent) window.push_back(f.ms);
            Sample s; common(s, now); s.sample = ++sequence;
            emitted_samples = sequence;
            s.values = frame_statistics(window);
            if (incomplete)
                for (int i = kMeanMs; i <= kFrames; i++) s.values[i] = {0, Reason::Overflow};
            reader->begin(now, config.fields);
            for (int i = kGpuUse; i < kMetrics; i++) s.values[i] = reader->read(static_cast<Metric>(i));
            s.thread_name = reader->busiest_thread_name();
            RenderSample r;
            { std::lock_guard lock(render_mu); r = renderer; }
            for (const Metric m : {kVramUsed, kVramBudget, kTextureUsed, kTextureBudget}) s.values[m] = r.values[m];
            for (const Metric m : {kGpuMs, kPipelines, kLeftOut, kUploadBytes, kGpuWaitMs}) {
                const Value a = r.values[m], b = previous[m];
                Value delta{0, Reason::Warmup};
                if (!a.available()) delta = a;
                else if (previous_render >= 0 && b.available()) {
                    if (a.number < b.number || r.present < previous_render) delta.reason = Reason::CounterReset;
                    else if (m == kGpuMs && r.present == previous_render) delta.reason = Reason::Warmup;
                    else delta = Value::of((a.number - b.number) / (m == kGpuMs ? r.present - previous_render : 1));
                }
                s.values[m] = delta;
            }
            previous = r.values; previous_render = r.present;
            snapshot_wanted.store(true, std::memory_order_relaxed);
            selected(s); emit(s);
            if (config.compact) {
                std::string text = "sample " + std::to_string(sequence);
                for (int i = 0; i < kMetrics; i++) if (s.values[i].available())
                    text += " " + std::string(metric_info(static_cast<Metric>(i)).name) + "=" +
                            format(static_cast<Metric>(i), s.values[i], config.si);
                say(text);
            }
            next += static_cast<int64_t>(config.interval_ms) * 1000000;
            const int64_t after = monotonic_now_ns();
            if (next <= after) next = after + static_cast<int64_t>(config.interval_ms) * 1000000;
            std::unique_lock lock(sleep_mu);
            stop_now = wake.wait_for(lock, std::chrono::nanoseconds(next - after), [&] { return stopping; });
        }
        drain();
        if (started_frames && !write_failed) {
            Sample marker; common(marker, monotonic_now_ns()); marker.kind = "marker";
            marker.event = "capture_end"; marker.detail = "session"; emit(marker);
            Sample summary; common(summary, marker.monotonic_ns); summary.kind = "summary";
            summary.event = "end"; summary.detail = incomplete ? "incomplete: frame queue overflow" : "complete";
            summary.values = frame_statistics(all);
            if (incomplete) for (int i = kMeanMs; i <= kFrames; i++) summary.values[i] = {0, Reason::Overflow};
            selected(summary, false); emit(summary);
        }
    } catch (const std::exception& e) { say(e.what()); }
    if (file && std::fclose(file) != 0) say("cannot close output");
    const int64_t cpu_end = sampler_thread_cpu_ns();
    if (cpu_begin >= 0 && cpu_end >= cpu_begin)
        say("sampler CPU " + format(kGpuWaitMs, Value::of((cpu_end - cpu_begin) / 1e6)) +
            ", " + std::to_string(emitted_samples) + " samples (this thread only)");
    else say("sampler CPU unavailable");
    collecting.store(false, std::memory_order_release);
}
} // namespace mkwvk::perf_monitor
