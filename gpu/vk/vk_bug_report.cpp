// The Insert menu's Bug report: what is read and how it is written (vk_bug_report.h). No Vulkan and no menu here:
// tests/test_bug_report.py compiles this file alone.
#include "vk_bug_report.h"
#include "pipeline_share.h"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cerrno>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <map>
#include <regex>
#include <set>
#include <sstream>
#include <string_view>
#include <mutex>
#include <thread>
#include <sys/stat.h>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <intrin.h>
#include <io.h>
#include <tlhelp32.h>
#include <psapi.h>
#include <winioctl.h>
#undef min
#undef max
#else
#if defined(__x86_64__) || defined(__i386__)
#include <cpuid.h>
#endif
#include <pwd.h>
#include <sys/utsname.h>
#include <unistd.h>
#include <sys/vfs.h>
#include <sys/sysmacros.h>
extern char** environ;
#endif

namespace mkwvk::bug_report {
namespace {

// (a log is copied whole up to 10 MB, GitHub taking files up to 25 MB; a longer one keeps its first MB, where the start, the
// device and the options are, and its last 9 MB, where the problem usually is: an hour of play logs about 10 MB)
constexpr uint64_t kLogCap = 10ull << 20, kLogHead = 1ull << 20;
constexpr const char* kForm = "https://github.com/NotRexed/Hookshot/issues/new?template=1-game-problem.yml";
// (the link's longest: a player who is not signed in is sent to GitHub's sign-in page with the whole address in it, encoded
// once more, and that page failed (HTTP 400 or 500, or the address dropped) from links of 3500 characters of escaped text on,
// signed in or not GitHub answers 414 from 8192; 2000 passes both, and every browser and chat program takes it)
constexpr size_t kLinkMax = 2000;
constexpr size_t kTextCap = 1u << 20;
namespace fs = std::filesystem;
fs::path path_of(const std::string& s) { return fs::path(std::u8string_view((const char8_t*)s.data(), s.size())); }
std::string path_text(const fs::path& p) {
    const auto s = p.u8string();
    return std::string(s.begin(), s.end());
}
FILE* open_file(const std::string& path, const char* mode) {
#ifdef _WIN32
    std::wstring m;
    for (; *mode; ++mode) m += (wchar_t)*mode;
    return _wfopen(path_of(path).c_str(), m.c_str());
#else
    return fopen(path.c_str(), mode);
#endif
}
bool read_text(const std::string& path, std::string& text, size_t cap = kTextCap) {
    text.clear();
    FILE* f = open_file(path, "rb");
    if (!f) return false;
    char b[8192];
    while (text.size() <= cap) {
        const size_t n = fread(b, 1, std::min(sizeof b, cap + 1 - text.size()), f);
        text.append(b, n);
        if (!n) break;
    }
    const bool ok = !ferror(f) && text.size() <= cap;
    fclose(f);
    if (!ok) text.clear();
    return ok;
}
/// Structured UTF-8 records may be written by Windows text tools. Normalize CRLF
/// once at intake, before header/key/resumed checks; never change logs or PNG bytes.
bool read_record(const std::string& path, std::string& text, size_t cap = kTextCap) {
    if (!read_text(path, text, cap)) return false;
    size_t out = 0;
    for (size_t at = 0; at < text.size(); ++at) {
        if (text[at] == '\r' && at + 1 < text.size() && text[at + 1] == '\n') continue;
        text[out++] = text[at];
    }
    text.resize(out);
    return true;
}
bool put_file(const std::string& path, const std::string& text) {
    FILE* f = open_file(path, "wb");
    if (!f) return false;
    const bool ok = fwrite(text.data(), 1, text.size(), f) == text.size();
    return fclose(f) == 0 && ok;
}

bool is_sep(char c) { return c == '/' || c == '\\'; }
/// (a character a file or folder name has: a name ends at any other)
bool name_char(char c) {
    const unsigned char u = (unsigned char)c;
    return std::isalnum(u) || c == '.' || c == '_' || c == '-' || u >= 0x80;
}
bool same_char(char a, char b, bool windows) {
    if (is_sep(a) && is_sep(b)) return windows || a == b;
    return windows ? std::tolower((unsigned char)a) == std::tolower((unsigned char)b) : a == b;
}
bool matches_at(const std::string& s, size_t at, const std::string& what, bool windows) {
    if (what.empty() || at + what.size() > s.size()) return false;
    for (size_t i = 0; i < what.size(); i++)
        if (!same_char(s[at + i], what[i], windows)) return false;
    return true;
}
std::string trimmed(std::string s) {
    while (!s.empty() && (s.back() == '\n' || s.back() == '\r' || s.back() == ' ')) s.pop_back();
    size_t i = 0;
    while (i < s.size() && s[i] == ' ') i++;
    return s.substr(i);
}
std::string megabytes(uint64_t n) {
    char b[32];
    if (n < (1u << 20)) snprintf(b, sizeof b, "%llu KB", (unsigned long long)((n + 1023) >> 10));
    else snprintf(b, sizeof b, "%.1f MB", n / 1048576.0);
    return b;
}
bool local_time(std::time_t t, std::tm* out) {
#ifdef _WIN32
    return localtime_s(out, &t) == 0;
#else
    return localtime_r(&t, out) != nullptr;
#endif
}
std::string when(std::time_t t, const char* format) {
    std::tm tm{};
    char b[64] = "";
    if (t && local_time(t, &tm)) strftime(b, sizeof b, format, &tm);
    return b;
}
bool exists(const std::string& path, std::time_t* written = nullptr) {
    std::error_code ec;
    if (!fs::is_regular_file(path_of(path), ec) && !fs::is_directory(path_of(path), ec)) return false;
    if (written) {
#ifdef _WIN32
        // MSVC's file clock lacks to_sys; use the CRT's UTF-16 path/stat API.
        struct _stat64 st{};
        if (_wstat64(path_of(path).c_str(), &st) == 0) *written = st.st_mtime;
#else
        const auto time = fs::last_write_time(path_of(path), ec);
        if (!ec) *written = std::chrono::system_clock::to_time_t(
            std::chrono::time_point_cast<std::chrono::system_clock::duration>(std::chrono::file_clock::to_sys(time)));
#endif
    }
    return true;
}
/// Makes a folder, or finds it made; why: the system's reason when it cannot be made
bool make_dir(const std::string& path, std::string* why) {
    std::error_code ec;
    const auto p = path_of(path);
    if (fs::create_directory(p, ec) || (!ec && fs::is_directory(p, ec))) return true;
    *why = ec.message();
    return false;
}

std::string lower(std::string s) {
    for (char& c : s) c = (char)std::tolower((unsigned char)c);
    return s;
}
std::string clean(const std::string& text, const Inputs& in) {
    std::string s = redact(text, in.home, in.user, in.windows);
    // Names can appear outside paths (controller names, crash text, custom OS names).
    for (const std::string* word : {&in.user, &in.hostname}) {
        if (word->empty()) continue;
        for (size_t at = 0; at + word->size() <= s.size();) {
            if ((at == 0 || !name_char(s[at - 1])) && matches_at(s, at, *word, true) &&
                (at + word->size() == s.size() || !name_char(s[at + word->size()]))) {
                s.replace(at, word->size(), "<private>");
                at += 9;
            } else ++at;
        }
    }
    // Never retain endpoints or e-mail addresses from a log. Hardware/version tokens
    // remain; IPv4 needs four octets and IPv6 at least two colons.
    std::string out;
    for (size_t at = 0; at < s.size();) {
        if (!(std::isalnum((unsigned char)s[at]) || s[at] == ':' || s[at] == '@')) {
            out += s[at++];
            continue;
        }
        size_t end = at;
        while (end < s.size() && (name_char(s[end]) || s[end] == ':' || s[end] == '@' || s[end] == '%')) ++end;
        const std::string token = s.substr(at, end - at);
        unsigned a, b, c, d; int used = 0;
        const bool ip4 = sscanf(token.c_str(), "%u.%u.%u.%u%n", &a, &b, &c, &d, &used) == 4 &&
                         a < 256 && b < 256 && c < 256 && d < 256 && used > 0 &&
                         ((size_t)used == token.size() || token[(size_t)used] == ':');
        // Two colons alone can be an ordinary HH:MM:SS timestamp. IPv6 either
        // compresses groups with "::" or spells all eight groups; a zone follows %.
        const std::string_view address = std::string_view(token).substr(0, token.find('%'));
        const bool ip6 = (address.find("::") != std::string::npos || std::count(address.begin(), address.end(), ':') >= 7) &&
                        address.find_first_not_of("0123456789abcdefABCDEF:.") == std::string::npos;
        const auto dot = token.rfind('.');
        const bool domain = dot != std::string::npos && dot + 1 < token.size() &&
                            (lower(token.substr(dot + 1)) == "local" || lower(token.substr(dot + 1)) == "lan" ||
                             lower(token.substr(dot + 1)) == "com" || lower(token.substr(dot + 1)) == "net" ||
                             lower(token.substr(dot + 1)) == "org");
        out += ip4 || ip6 || token.find('@') != std::string::npos || domain ? "<address>" : token;
        at = end;
    }
    // Values of known secret-bearing switches can have appeared in a diagnostic.
    for (const auto& kv : in.env) {
        const size_t eq = kv.find('=');
        if (eq == std::string::npos || eq + 1 == kv.size()) continue;
        const std::string key = lower(kv.substr(0, eq)), value = kv.substr(eq + 1);
        bool secret = false;
        for (const char* name : {"token", "key", "room", "secret", "pass", "server", "user", "host"})
            secret |= key.find(name) != std::string::npos;
        if (!secret || value.size() < 4) continue;
        size_t at = 0;
        while ((at = out.find(value, at)) != std::string::npos) out.replace(at, value.size(), "<private>"), at += 9;
    }
    return out;
}

std::string env_value(const Inputs& in, const char* key) {
    const std::string prefix = std::string(key) + "=";
    for (const auto& value : in.env)
        if (value.compare(0, prefix.size(), prefix) == 0) return value.substr(prefix.size());
    return "";
}

std::string system_details(const Inputs& in) {
    std::ostringstream out;
    out << in.system << "\n";
    std::error_code ec;
    const auto space = fs::space(path_of(in.game_dir), ec);
    if (!ec) out << "Game storage: capacity_bytes=" << space.capacity << " free_bytes=" << space.available << "\n";
    else out << "Game storage free space: unavailable\n";
#ifdef _WIN32
    MEMORYSTATUSEX m{};
    m.dwLength = sizeof m;
    if (GlobalMemoryStatusEx(&m))
        out << "RAM: total_bytes=" << m.ullTotalPhys << " used_bytes=" << m.ullTotalPhys - m.ullAvailPhys
            << "\nCommit: limit_bytes=" << m.ullTotalPageFile << " used_bytes=" << m.ullTotalPageFile - m.ullAvailPageFile << "\n";
    else out << "RAM/commit: unavailable\n";
    wchar_t root[MAX_PATH], type[64];
    const auto game = path_of(in.game_dir);
    if (GetVolumePathNameW(game.c_str(), root, MAX_PATH) &&
        GetVolumeInformationW(root, nullptr, 0, nullptr, nullptr, nullptr, type, 64)) {
        out << "Filesystem: " << path_text(fs::path(type)) << "\n";
        std::wstring volume(root);
        if (volume.size() >= 2 && volume[1] == L':') {
            volume = L"\\\\.\\" + volume.substr(0, 2);
            HANDLE disk = CreateFileW(volume.c_str(), 0, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
            STORAGE_PROPERTY_QUERY q{};
            q.PropertyId = StorageDeviceSeekPenaltyProperty;
            q.QueryType = PropertyStandardQuery;
            DEVICE_SEEK_PENALTY_DESCRIPTOR d{};
            DWORD n = 0;
            if (disk != INVALID_HANDLE_VALUE && DeviceIoControl(disk, IOCTL_STORAGE_QUERY_PROPERTY, &q, sizeof q,
                    &d, sizeof d, &n, nullptr) && n >= sizeof d)
                out << "Storage seek penalty: " << (d.IncursSeekPenalty ? "yes (rotational-like)" : "no") << "\n";
            else out << "Storage seek penalty: unavailable\n";
            if (disk != INVALID_HANDLE_VALUE) CloseHandle(disk);
        }
    } else out << "Filesystem/rotation: unavailable\n";
    SYSTEM_POWER_STATUS power{};
    if (GetSystemPowerStatus(&power))
        out << "Power: " << (power.ACLineStatus == 1 ? "AC" : power.ACLineStatus == 0 ? "battery" : "unknown")
            << "; battery_present=" << (!(power.BatteryFlag & 128) && power.BatteryFlag != 255)
            << "; charge_percent=" << (power.BatteryLifePercent == 255 ? -1 : (int)power.BatteryLifePercent) << "\n";
    else out << "Power: unavailable\n";
    out << "Steam Deck hardware: unknown (no firmware identifiers collected)\n";
#else
    std::string data;
    if (read_text("/proc/meminfo", data)) out << memory_fields(data, false);
    else out << "RAM/swap: unavailable\n";
    struct statfs storage{};
    if (!statfs(in.game_dir.c_str(), &storage))
        out << "Filesystem: " << filesystem_name((uint64_t)storage.f_type) << " (type 0x" << std::hex
            << (uint64_t)storage.f_type << std::dec << ")\n";
    else out << "Filesystem: unavailable\n";
    struct stat st{};
    std::string rotation = "unknown";
    if (!stat(in.game_dir.c_str(), &st)) {
        const std::string dev = "/sys/dev/block/" + std::to_string(major(st.st_dev)) + ":" + std::to_string(minor(st.st_dev));
        auto block = fs::canonical(path_of(dev), ec);
        for (int i = 0; !ec && i < 3; ++i, block = block.parent_path())
            if (read_text(path_text(block / "queue/rotational"), data, 32)) {
                if (trimmed(data) == "0") rotation = "no";
                if (trimmed(data) == "1") rotation = "yes";
                break;
            }
    }
    out << "Rotational: " << rotation << " (kernel block queue; stacked/network filesystems may be unknown)\n";
    const std::string session = lower(env_value(in, "XDG_SESSION_TYPE")), desktop = lower(env_value(in, "XDG_CURRENT_DESKTOP"));
    out << "Display server: " << (session == "wayland" || session == "x11" ? session : "unknown") << "\nCompositor/desktop: ";
    bool desktop_known = false;
    for (const char* known : {"gamescope", "hyprland", "sway", "kde", "gnome", "weston", "xfce", "cinnamon"})
        if (desktop.find(known) != std::string::npos) out << known << " ", desktop_known = true;
    if (!desktop_known) out << "unknown (unrecognised environment value not included)";
    const bool gamescope = desktop.find("gamescope") != std::string::npos || !env_value(in, "GAMESCOPE_WAYLAND_DISPLAY").empty();
    out << "\nNested compositor: " << (gamescope ? "gamescope indicated; nesting not proven" : "not known")
        << "\nSteam Deck hardware: ";
    std::string vendor, product;
    if (read_text("/sys/class/dmi/id/sys_vendor", vendor, 256) && read_text("/sys/class/dmi/id/product_name", product, 256))
        out << (trimmed(vendor) == "Valve" && (trimmed(product) == "Jupiter" || trimmed(product) == "Galileo") ? "yes" : "no");
    else out << "unknown";
    out << "\n";
    bool supply = false;
    for (fs::directory_iterator it("/sys/class/power_supply", ec), end; !ec && it != end; it.increment(ec)) {
        std::string type, online, status, capacity, scope;
        const auto root = it->path();
        if (!read_text(path_text(root / "scope"), scope, 64) || !system_power_supply(scope)) continue;
        if (!read_text(path_text(root / "type"), type, 64)) continue;
        if (trimmed(type) == "Mains" && read_text(path_text(root / "online"), online, 32)) {
            out << "AC online: " << (trimmed(online) == "1" ? "yes" : "no") << "\n";
            supply = true;
        } else if (trimmed(type) == "Battery") {
            if (read_text(path_text(root / "status"), status, 64))
                for (const char* known : {"Charging", "Discharging", "Not charging", "Full", "Unknown"})
                    if (trimmed(status) == known) out << "Battery: " << known << "\n";
            if (read_text(path_text(root / "capacity"), capacity, 32)) {
                int charge = -1;
                if (sscanf(capacity.c_str(), "%d", &charge) == 1 && charge >= 0 && charge <= 100)
                    out << "Battery charge_percent: " << charge << "\n";
            }
            supply = true;
        }
    }
    if (!supply) out << "Power source: unknown (no readable power-supply entry)\n";
#endif
    return out.str();
}

std::string process_memory() {
#ifdef _WIN32
    PROCESS_MEMORY_COUNTERS_EX m{};
    m.cb = sizeof m;
    using MemoryFn = BOOL(WINAPI*)(HANDLE, PPROCESS_MEMORY_COUNTERS, DWORD);
    const auto get = (MemoryFn)(void*)GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "K32GetProcessMemoryInfo");
    if (get && get(GetCurrentProcess(), (PPROCESS_MEMORY_COUNTERS)&m, sizeof m))
        return "Process working_set_bytes: " + std::to_string(m.WorkingSetSize) +
               "\nProcess private_commit_bytes: " + std::to_string(m.PrivateUsage) + "\n";
#else
    std::string data;
    if (read_text("/proc/self/status", data)) return memory_fields(data, true);
#endif
    return "Process memory: unavailable\n";
}

std::string software_text(const Inputs& in) {
    std::string out;
#ifdef _WIN32
    wchar_t system[MAX_PATH];
    const UINT n = GetWindowsDirectoryW(system, MAX_PATH);
    const std::string sys = n && n < MAX_PATH ? lower(path_text(fs::path(system))) + "\\" : "";
    const HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, GetCurrentProcessId());
    if (snap != INVALID_HANDLE_VALUE) {
        MODULEENTRY32W m{};
        m.dwSize = sizeof m;
        std::set<std::string> names;
        if (Module32FirstW(snap, &m)) do {
            const std::string path = lower(path_text(fs::path(m.szExePath)));
            const std::string name = path_text(fs::path(m.szModule));
            if ((!sys.empty() && path.compare(0, sys.size(), sys) == 0) ||
                lower(name) == "hookshot.exe" || lower(name) == "hookshot_vk.dll") continue;
            names.insert(name);
        } while (Module32NextW(snap, &m));
        for (const auto& name : names) out += name + "\n";
        CloseHandle(snap);
        if (out.empty()) out = "(none outside Windows and Hookshot's own modules)\n";
    } else out = "Loaded modules: unavailable\n";
    out = "Loaded modules outside Windows and Hookshot's own modules (file names only; not all are injectors):\n" + out;
#else
    std::string maps;
    out = "Mapped shared libraries (file names only; mapping is evidence of loading, not proof of injection):\n";
    out += read_text("/proc/self/maps", maps, 8u << 20) ? library_names(maps, true) : "unavailable\n";
    out += "\nLD_PRELOAD file names (requested, not proof of loading):\n" + library_names(env_value(in, "LD_PRELOAD"), false);
#endif
    out += "\nVulkan layer activation: unavailable from an existing Vulkan instance. Mapped layer libraries above and "
           "the validation setting below are evidence, not an enumeration of active layers.\n";
    for (const char* key : {"VK_INSTANCE_LAYERS", "VK_LOADER_LAYERS_ENABLE"}) {
        std::string value = env_value(in, key), token;
        std::istringstream words(value);
        while (std::getline(words, token, ':'))
            if (token.compare(0, 9, "VK_LAYER_") == 0 &&
                token.find_first_not_of("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789_") == std::string::npos)
                out += std::string(key) + " requested: " + token.substr(0, 128) + "\n";
    }
    out += "RX_VK_VALIDATION requested: " + (env_value(in, "RX_VK_VALIDATION").empty() ? std::string("unset") : "set") + "\n";
    return out;
}

std::vector<fs::path> pending_records(const Inputs& in) {
    std::vector<fs::path> out;
    std::error_code ec;
    const fs::path root = path_of(in.game_dir) / "bug-reports" / "pending";
    const std::regex name("(crash|stall|unclean)-[0-9]{8}T[0-9]{6}Z(-[0-9]+-[0-9]+)?\\.txt");
    for (fs::directory_iterator it(root, ec), end; !ec && it != end && out.size() < 32; it.increment(ec)) {
        if (!fs::is_regular_file(it->symlink_status(ec)) || ec || !std::regex_match(path_text(it->path().filename()), name)) continue;
        out.push_back(it->path());
    }
    std::sort(out.begin(), out.end());
    return out;
}

bool record_header(const std::string& text) {
    return text.starts_with("Hookshot crash record 1\n") || text.starts_with("Hookshot stall record 1\n") ||
           text.starts_with("Hookshot unclean record 1\n");
}
bool record_resumed(const std::string& text) {
    if (!text.starts_with("Hookshot stall record 1\n")) return false;
    static const std::regex resumed("\nresumed: yes(, after [0-9]+ s)?(\n|$)");
    return std::regex_search(text, resumed);
}
std::string record_text(const std::string& text, const Inputs& in) {
    static const std::set<std::string> fields = {
        "version", "when", "uptime_s", "reason", "thread", "module_offset", "last_present_age_ms", "log",
        "pid", "signal", "signal_code", "exception", "fault_address_class", "host_backtrace_method",
        "host_backtrace", "guest_register", "guest_backtrace", "log_size_bytes", "log_tail_bytes", "log_mtime_utc",
        "log_tail_fatal", "log_tail_crash", "log_tail_device_lost", "record_complete", "last_log_age_ms",
        "log_progress_age_ms", "vcore_progress_age_ms", "timeout_s", "presents", "state", "stacks", "resumed",
        "started", "crash_handler", "dump", "run_id", "termination_note"
    };
    std::istringstream lines(text);
    std::string line, out;
    std::getline(lines, out);
    out += "\n";
    unsigned omitted = 0, kept = 0;
    while (std::getline(lines, line)) {
        const auto colon = line.find(": ");
        if (colon == std::string::npos || !fields.count(line.substr(0, colon)) || line.size() - colon > 162 ||
            ++kept > 256 || line.find('\0') != std::string::npos) {
            ++omitted;
            continue;
        }
        out += clean(line, in) + "\n";
    }
    out += "Reader: " + std::to_string(omitted) + " lines left out (unknown field, over-long value or record limit).\n";
    return out;
}

std::string record_value(const std::string& text, const char* key) {
    const std::string prefix = "\n" + std::string(key) + ": ";
    const auto at = text.find(prefix);
    if (at == std::string::npos) return "";
    const auto end = text.find('\n', at + prefix.size());
    const std::string value = text.substr(at + prefix.size(), end - (at + prefix.size()));
    return value.size() <= 160 ? value : "";
}
bool valid_run_id(const std::string& id) {
    return id.size() == 32 && id.find_first_not_of("0123456789abcdef") == std::string::npos;
}
std::string log_run_id(const std::string& path) {
    FILE* f = open_file(path, "rb");
    if (!f) return "";
    // Startup identity is in the log head. Never scan the tail for a coincidental
    // diagnostic that quotes another run's ID.
    char head[65536];
    const size_t n = fread(head, 1, sizeof head, f);
    fclose(f);
    std::istringstream lines(std::string(head, n));
    std::string line;
    for (int i = 0; i < 64 && std::getline(lines, line); ++i) {
        const auto at = line.find("] run_id: ");
        if (at == std::string::npos) continue;
        const std::string id = trimmed(line.substr(at + 10));
        if (valid_run_id(id)) return id;
    }
    return "";
}

std::string uptime_text() {
#ifdef _WIN32
    FILETIME born{}, ended{}, kernel{}, user{}, now{};
    if (GetProcessTimes(GetCurrentProcess(), &born, &ended, &kernel, &user)) {
        GetSystemTimeAsFileTime(&now);
        const uint64_t start = (uint64_t)born.dwHighDateTime << 32 | born.dwLowDateTime;
        const uint64_t current = (uint64_t)now.dwHighDateTime << 32 | now.dwLowDateTime;
        if (current >= start) return "Process uptime_s: " + std::to_string((current - start) / 10000000) + "\n";
    }
#else
    std::string stat, uptime;
    if (read_text("/proc/self/stat", stat) && read_text("/proc/uptime", uptime)) {
        const auto end = stat.rfind(')');
        std::istringstream fields(end == std::string::npos ? "" : stat.substr(end + 2));
        std::string token;
        uint64_t ticks = 0;
        for (int field = 3; field <= 22 && fields >> token; ++field)
            if (field == 22) ticks = strtoull(token.c_str(), nullptr, 10);
        const long hz = sysconf(_SC_CLK_TCK);
        const double seconds = strtod(uptime.c_str(), nullptr);
        if (hz > 0 && ticks && seconds >= (double)ticks / hz)
            return "Process uptime_s: " + std::to_string((uint64_t)(seconds - (double)ticks / hz)) + "\n";
    }
#endif
    return "Process uptime_s: unknown\n";
}
uint64_t file_size(FILE* f) {
#ifdef _WIN32
    _fseeki64(f, 0, SEEK_END);
    const int64_t n = _ftelli64(f);
    _fseeki64(f, 0, SEEK_SET);
#else
    fseeko(f, 0, SEEK_END);
    const int64_t n = ftello(f);
    fseeko(f, 0, SEEK_SET);
#endif
    return n > 0 ? (uint64_t)n : 0;
}
/// Whether two paths name one file (the log this run writes, and hookshot_log.txt in the game folder)
bool same_file(const std::string& a, const std::string& b, bool windows) {
    if (a.empty() || b.empty()) return false;
    // File identity handles Windows short/long aliases and hard links as well as
    // POSIX aliases. Text comparison is only a fallback when identity is unavailable.
    std::error_code ec;
    const bool equivalent = fs::equivalent(path_of(a), path_of(b), ec);
    if (!ec) return equivalent;
    return a.size() == b.size() && matches_at(a, 0, b, windows);
}

// ---- the picture (Inputs::ask_picture: the renderer answers with picture_written once the PNG is written)
std::mutex pic_mu;
std::condition_variable pic_cv;
uint64_t pic_next = 0, pic_done = 0;
bool pic_ok = false;
std::map<uint64_t, std::string> late_pictures;

uint64_t picture_ticket() {
    std::lock_guard lk(pic_mu);
    return ++pic_next;
}
/// Whether the picture was written; one that has not come when the time is up is given up (picture_written removes it then)
bool wait_picture(uint64_t ticket, const std::string& path, int ms) {
    std::unique_lock lk(pic_mu);
    if (pic_cv.wait_for(lk, std::chrono::milliseconds(ms), [&] { return pic_done == ticket; })) return pic_ok;
    late_pictures[ticket] = path;
    return false;
}

// ---- the report's state, for the menu
std::atomic<int> report_state{kIdle};
std::mutex report_mu;
std::string report_shown, report_why;

/// A log as it was copied: what the report says of it
struct LogFacts {
    bool copied = false, cut = false;
    uint64_t size = 0;
    std::time_t written = 0;
    std::string version, map, trouble;   // (its first line's version, the last map it names, its first sign of trouble)
    std::vector<std::string> areas;
    std::map<std::string, std::string> metrics;
    std::string controllers, save_root;
};
void take_facts(const std::string& line, LogFacts& f) {
    size_t at;
    if (f.version.empty() && (at = line.find("] hookshot ")) != std::string::npos) f.version = trimmed(line.substr(at + 11));
    if ((at = line.find("] map: ")) != std::string::npos) f.map = trimmed(line.substr(at + 7));   // (host/hos_main.c, on Link's update)
    if ((at = line.find("] map: ")) != std::string::npos) {
        if (f.areas.size() == 16) f.areas.erase(f.areas.begin());
        f.areas.push_back(trimmed(line).substr(0, 512));
    }
    for (const char* key : {"texture cache:", "vk: VRAM:", "vk: textures:", "VRAM", "program cache:", "pipeline cache", "preparation:",
                            "precompile", "program census", "shaders'", "vk: frame", "vk: present:", "swapchain"})
        if (line.find(key) != std::string::npos) f.metrics[key] = trimmed(line).substr(0, 1024);
    if (line.find("hid: controller") != std::string::npos && line.find("mapping-string") == std::string::npos) {
        if (f.controllers.size() < 8192) f.controllers += trimmed(line).substr(0, 512) + "\n";
    }
    if (f.trouble.empty() && (line.find("FATAL:") != std::string::npos || line.find("CRASH:") != std::string::npos ||
                              line.find("device lost") != std::string::npos))
        f.trouble = trimmed(line).substr(0, 300);
}
/// Copies a log, its paths redacted, as much of it as there was when the copy began (the game goes on writing this run's).
/// Read by length, not as C strings: a log a power cut or a kill ended can hold NUL bytes, and they are copied as they are
LogFacts copy_log(const std::string& from, const std::string& to, const Inputs& in) {
    LogFacts f;
    FILE* src = open_file(from, "rb");
    if (!src) return f;
    exists(from, &f.written);
    f.size = file_size(src);
    FILE* dst = open_file(to, "wb");
    if (!dst) {
        fclose(src);
        return f;
    }
    f.cut = f.size > kLogCap;
    const uint64_t gap_from = f.cut ? kLogHead : f.size, gap_to = f.cut ? f.size - (kLogCap - kLogHead) : f.size;
    std::string line;
    std::vector<char> buf(1 << 16);
    uint64_t read = 0, start = 0;   // (bytes read; where the line being gathered starts in the log)
    bool said_gap = false;
    const auto put = [&] {   // (a whole line, its '\n' kept, or the last part of the log)
        const size_t mount = line.find("fs: mounted "), arrow = line.find(": -> ", mount);
        if (mount != std::string::npos && arrow != std::string::npos) {
            std::string root = trimmed(line.substr(arrow + 5));
            std::string normalized = root;
            std::replace(normalized.begin(), normalized.end(), '\\', '/');
            static const std::regex save_mount(".*/save/[0-9a-fA-F]{32}/?");
            if (std::regex_match(normalized, save_mount)) f.save_root = root;
        }
        const std::string safe = clean(line, in);
        take_facts(safe, f);
        if (start < gap_from || start >= gap_to) {
            fwrite(safe.data(), 1, safe.size(), dst);
        } else if (!said_gap) {
            said_gap = true;
            fprintf(dst, "[bug report: %s of this log's middle left out here: the copy keeps its first and its last part]\n",
                    megabytes(gap_to - gap_from).c_str());
        }
        start += line.size();
        line.clear();
    };
    while (read < f.size) {
        const size_t n = fread(buf.data(), 1, (size_t)std::min<uint64_t>(buf.size(), f.size - read), src);
        if (!n) break;
        read += n;
        for (size_t i = 0; i < n;) {
            const char* nl = (const char*)memchr(buf.data() + i, '\n', n - i);
            const size_t end = nl ? (size_t)(nl - buf.data()) + 1 : n;
            line.append(buf.data() + i, end - i);
            i = end;
            if (nl || line.size() >= (1u << 20)) put();   // (a line, or a MB of one that has no end)
        }
    }
    if (!line.empty()) put();
    fclose(src);
    f.copied = fclose(dst) == 0;
    return f;
}

/// Whether a switch's value goes into the report. CONTRIBUTING.md lists hookshot_env.txt among the files never to share, so
/// only a value that cannot hold anything private is written: a number of at most 12 characters (60, -1, 1.5), a size
/// (1920x1080), or on/off, true/false, yes/no; and never for a name that may hold a secret, a place or a person (TOKEN, KEY,
/// ROOM, SECRET, PASS, SERVER or USER in it). Every other value is left out, paths and addresses with them
bool value_shown(const std::string& name, const std::string& value) {
    std::string upper = name, lower = value;
    for (char& ch : upper) ch = (char)std::toupper((unsigned char)ch);
    for (char& ch : lower) ch = (char)std::tolower((unsigned char)ch);
    for (const char* word : {"TOKEN", "KEY", "ROOM", "SECRET", "PASS", "SERVER", "USER"})
        if (upper.find(word) != std::string::npos) return false;
    for (const char* word : {"", "on", "off", "true", "false", "yes", "no"})
        if (lower == word) return true;
    if (value.size() > 12) return false;
    const auto digits = [&](size_t& at) {
        const size_t from = at;
        while (at < value.size() && std::isdigit((unsigned char)value[at])) at++;
        return at > from;
    };
    const bool minus = value[0] == '-';
    size_t at = minus ? 1 : 0;
    if (!digits(at)) return false;
    if (at == value.size()) return true;   // (a whole number)
    const char mid = value[at++];
    return (mid == '.' || (mid == 'x' && !minus)) && digits(at) && at == value.size();   // (1.5, 1920x1080)
}
/// The switches set: the game's own (host/hos_main.c env_line_allowed's names) and what loads other code into it (the form
/// asks about software hooked in), a NAME=VALUE line each, the value as value_shown allows; names: the names alone
std::string switches(const std::vector<std::string>& env, std::string* names) {
    static const char* const prefixes[] = {"RX_", "HOS_", "HOOKSHOT_", "EDEN_", "NVN_", "SDL_"};
    static const char* const hooks[] = {"VK_INSTANCE_LAYERS", "VK_LOADER_LAYERS_ENABLE", "VK_DRIVER_FILES", "VK_ICD_FILENAMES",
                                        "ENABLE_VKBASALT", "MANGOHUD", "LD_PRELOAD"};
    std::vector<std::pair<std::string, std::string>> out;
    for (const std::string& kv : env) {
        const size_t eq = kv.find('=');
        if (eq == std::string::npos || eq == 0) continue;
        const std::string name = kv.substr(0, eq), value = kv.substr(eq + 1);
        bool take = false;
        for (const char* p : prefixes) take |= name.compare(0, strlen(p), p) == 0;
        for (const char* h : hooks) take |= name == h;
        if (take) out.emplace_back(name, value_shown(name, value) ? value : "(value left out)");
    }
    std::sort(out.begin(), out.end());
    std::string text;
    names->clear();
    for (const auto& [name, value] : out) {
        text += name + "=" + value + "\n";
        *names += (names->empty() ? "" : ", ") + name;
    }
    return text;
}

std::string url_part(const std::string& s) {
    static const char hex[] = "0123456789ABCDEF";
    std::string out;
    for (unsigned char c : s) {
        if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') out += (char)c;
        else out += '%', out += hex[c >> 4], out += hex[c & 15];
    }
    return out;
}

}  // namespace

const char* filesystem_name(uint64_t type) {
    // Linux statfs magic values; unlike mountinfo's device column these work for
    // Btrfs subvolumes too. Unknown types retain their numeric code in system.txt.
    switch (type) {
    case 0x9123683e: return "btrfs";
    case 0xef53: return "ext2/ext3/ext4";
    case 0x58465342: return "xfs";
    case 0x2fc12fc1: return "zfs";
    case 0xf2f52010: return "f2fs";
    case 0x01021994: return "tmpfs";
    case 0x794c7630: return "overlayfs";
    case 0x6969: return "nfs";
    case 0x65735546: return "fuse";
    default: return "unknown";
    }
}
bool system_power_supply(const std::string& scope) { return trimmed(scope) == "System"; }

std::string library_names(const std::string& text, bool maps) {
    std::set<std::string> names;
    std::istringstream lines(text);
    std::string line;
    while (std::getline(lines, line, maps ? '\n' : ':')) {
        if (maps) {
            const auto at = line.find('/');
            if (at == std::string::npos) continue;
            line = line.substr(at);
            if (line.ends_with(" (deleted)")) line.resize(line.size() - 10);
        }
        std::istringstream words(line);
        std::string word;
        while (maps ? (word = line, !word.empty()) : (bool)(words >> word)) {
            const auto slash = word.find_last_of("/\\");
            std::string name = word.substr(slash == std::string::npos ? 0 : slash + 1);
            if (!name.empty() && name.size() <= 256 && (!maps || name.find(".so") != std::string::npos))
                names.insert(name);
            if (maps) break;
        }
    }
    std::string out;
    for (const auto& name : names) out += name + "\n";
    return out.empty() ? "(none)\n" : out;
}

std::string memory_fields(const std::string& text, bool process) {
    std::istringstream lines(text);
    std::map<std::string, uint64_t> values;
    std::string line, key, unit;
    uint64_t value;
    while (std::getline(lines, line)) {
        std::istringstream fields(line);
        if (!(fields >> key >> value >> unit) || unit != "kB") continue;
        for (const char* allowed : {"MemTotal:", "MemAvailable:", "SwapTotal:", "SwapFree:", "Committed_AS:", "CommitLimit:",
                                    "VmRSS:", "VmSize:", "VmSwap:", "VmPeak:"})
            if (key == allowed) values[key] = value;
    }
    std::string out;
    for (const auto& [name, amount] : values)
        if (process == name.starts_with("Vm")) out += name + " " + std::to_string(amount) + " KiB\n";
    if (!process && values.count("MemTotal:") && values.count("MemAvailable:"))
        out += "RAM in use: " + std::to_string(values["MemTotal:"] - std::min(values["MemTotal:"], values["MemAvailable:"])) + " KiB\n";
    if (!process && values.count("SwapTotal:") && values.count("SwapFree:"))
        out += "Swap in use: " + std::to_string(values["SwapTotal:"] - std::min(values["SwapTotal:"], values["SwapFree:"])) + " KiB\n";
    return out.empty() ? "Memory: unavailable\n" : out;
}

std::string redact(const std::string& line, const std::string& home, const std::string& user, bool windows) {
    std::string h = home;
    while (h.size() > 1 && is_sep(h.back())) h.pop_back();
    const bool with_home = h.size() > 1;
    std::string out;
    out.reserve(line.size());
    for (size_t i = 0; i < line.size();) {
        const size_t after_home = i + h.size(), after_user = i + user.size();
        // (the home folder as a whole: not inside a longer name, not the end of another path)
        if (with_home && (i == 0 || (!name_char(line[i - 1]) && !is_sep(line[i - 1]))) && matches_at(line, i, h, windows) &&
            (after_home >= line.size() || !name_char(line[after_home]))) {
            out += windows ? "%USERPROFILE%" : "~";
            i = after_home;
            continue;
        }
        // (the user's name as a folder of any path: /run/media/<user>, D:\Users\<user>, /var/home/<user>)
        if (!user.empty() && i > 0 && is_sep(line[i - 1]) && matches_at(line, i, user, windows) &&
            (after_user >= line.size() || !name_char(line[after_user]))) {
            out += "<user>";
            i = after_user;
            continue;
        }
        out += line[i++];
    }
    return out;
}

std::string system_text() {
    std::string cpu;
#if defined(__x86_64__) || defined(__i386__) || defined(_M_X64) || defined(_M_IX86)
    {   // (the processor's own name, the same on every system)
        unsigned r[12] = {};
#ifdef _WIN32
        int v[4];
        __cpuid(v, (int)0x80000000);
        const unsigned top = (unsigned)v[0];
        for (unsigned k = 0; k < 3 && top >= 0x80000004u; k++) {
            __cpuid(v, (int)(0x80000002u + k));
            for (int j = 0; j < 4; j++) r[k * 4 + j] = (unsigned)v[j];
        }
#else
        unsigned a, b, c, d;
        if (__get_cpuid(0x80000000u, &a, &b, &c, &d) && a >= 0x80000004u)
            for (unsigned k = 0; k < 3; k++) __get_cpuid(0x80000002u + k, &r[k * 4], &r[k * 4 + 1], &r[k * 4 + 2], &r[k * 4 + 3]);
#endif
        char name[49] = {};
        memcpy(name, r, 48);
        cpu = trimmed(name);
    }
#endif
    if (cpu.empty()) cpu = "an unknown CPU";
    if (const unsigned n = std::thread::hardware_concurrency()) cpu += " (" + std::to_string(n) + " threads)";
    uint64_t ram = 0;
    std::string os;
#ifdef _WIN32
    MEMORYSTATUSEX m{};
    m.dwLength = sizeof m;
    if (GlobalMemoryStatusEx(&m)) ram = m.ullTotalPhys;
    {   // (RtlGetVersion: GetVersionEx answers as the program's manifest allows)
        typedef LONG(WINAPI * GetVersionFn)(OSVERSIONINFOW*);
        typedef const char*(__cdecl * WineFn)(void);
        const HMODULE nt = GetModuleHandleA("ntdll.dll");
        const GetVersionFn get = nt ? (GetVersionFn)(void*)GetProcAddress(nt, "RtlGetVersion") : nullptr;
        OSVERSIONINFOW v{};
        v.dwOSVersionInfoSize = sizeof v;
        if (get && get(&v) == 0) {
            const bool eleven = v.dwMajorVersion == 10 && v.dwBuildNumber >= 22000;
            os = v.dwMajorVersion == 10 ? (eleven ? "Windows 11" : "Windows 10")
                                        : "Windows " + std::to_string(v.dwMajorVersion) + "." + std::to_string(v.dwMinorVersion);
            os += " (build " + std::to_string(v.dwBuildNumber) + ")";
        } else {
            os = "Windows";
        }
        if (const WineFn wine = nt ? (WineFn)(void*)GetProcAddress(nt, "wine_get_version") : nullptr) os += ", Wine " + std::string(wine());
    }
#else
    {
        const long pages = sysconf(_SC_PHYS_PAGES), page = sysconf(_SC_PAGE_SIZE);
        if (pages > 0 && page > 0) ram = (uint64_t)pages * (uint64_t)page;
    }
    for (const char* file : {"/etc/os-release", "/usr/lib/os-release"}) {
        FILE* f = fopen(file, "r");
        if (!f) continue;
        char line[256];
        while (fgets(line, sizeof line, f))
            if (!strncmp(line, "PRETTY_NAME=", 12)) {
                std::string v = trimmed(line + 12);
                if (v.size() >= 2 && (v[0] == '"' || v[0] == '\'')) v = v.substr(1, v.size() - 2);
                os = v;
            }
        fclose(f);
        if (!os.empty()) break;
    }
    if (os.empty()) os = "Linux";
    struct utsname u;
    if (!uname(&u)) os += std::string(" (") + u.sysname + " " + u.release + ")";
    // Desktop environment values are not trusted names; system_details allow-lists them.
    if (getenv("APPIMAGE")) os += ", the AppImage";
#endif
    char gb[32] = "";
    if (ram) snprintf(gb, sizeof gb, "%llu GB", (unsigned long long)((ram + (1ull << 29)) >> 30));
    return cpu + ", " + (ram ? std::string(gb) + ", " : std::string()) + os;
}

void fill_here(Inputs& in) {
    in.system = system_text();
    in.now = std::time(nullptr);
    in.env.clear();
    in.collect_process = true;
#ifdef _WIN32
    in.windows = true;
    if (wchar_t* block = GetEnvironmentStringsW()) {
        for (const wchar_t* e = block; *e; e += wcslen(e) + 1) in.env.push_back(path_text(fs::path(e)));
        FreeEnvironmentStringsW(block);
    }
    in.home = env_value(in, "USERPROFILE");
    in.user = env_value(in, "USERNAME");
    wchar_t computer[256];
    DWORD computer_size = 256;
    if (GetComputerNameW(computer, &computer_size)) in.hostname = path_text(fs::path(computer));
    const HANDLE err = (HANDLE)_get_osfhandle(_fileno(stderr));   // (the game freopens stderr as hookshot_log.txt)
    wchar_t path[32768];
    if (err != INVALID_HANDLE_VALUE && GetFileType(err) == FILE_TYPE_DISK) {
        const DWORD n = GetFinalPathNameByHandleW(err, path, 32768, FILE_NAME_NORMALIZED);
        if (n && n < 32768) {
            in.current_log = path_text(fs::path(path));
            if (!in.current_log.compare(0, 8, "\\\\?\\UNC\\")) in.current_log = "\\\\" + in.current_log.substr(8);
            else if (!in.current_log.compare(0, 4, "\\\\?\\")) in.current_log = in.current_log.substr(4);
        }
    }
#else
    in.windows = false;
    for (char** e = environ; e && *e; e++) in.env.push_back(*e);
    const passwd* pw = getpwuid(getuid());
    const char* h = getenv("HOME");
    const char* u = getenv("USER");
    in.home = h && *h ? h : pw && pw->pw_dir ? pw->pw_dir : "";
    in.user = u && *u ? u : pw && pw->pw_name ? pw->pw_name : "";
    struct stat st;
    char path[4096];
    char computer[256] = {};
    if (gethostname(computer, sizeof computer - 1) == 0) in.hostname = computer;
    if (!fstat(2, &st) && S_ISREG(st.st_mode)) {   // (a terminal or a pipe: the log is not in a file)
        const ssize_t n = readlink("/proc/self/fd/2", path, sizeof path - 1);
        if (n > 0) in.current_log.assign(path, (size_t)n);
    }
#endif
    in.save_slot = env_value(in, "RX_VK_BUG_REPORT_SAVE");
}

std::string write(const Inputs& in) {
    const auto began = std::chrono::steady_clock::now();
    const char sep = in.windows ? '\\' : '/';
    const std::string stamp = when(in.now, "%Y-%m-%d_%H-%M-%S");
    std::string rel, dir, why;
    if (make_dir(in.game_dir + "bug-reports", &why)) {
        for (int k = 1; k < 100 && dir.empty() && why.empty(); ++k) {
            const std::string r = std::string("bug-reports") + sep + stamp + (k > 1 ? "-" + std::to_string(k) : "");
            if (!exists(in.game_dir + r) && make_dir(in.game_dir + r, &why)) rel = r, dir = in.game_dir + r + sep;
        }
    }
    if (dir.empty()) {
        std::lock_guard lk(report_mu);
        report_why = "the folder bug-reports could not be made in the game folder (" +
                     (why.empty() ? std::string("99 reports made in this second already") : why) + ")";
        if (in.log) in.log(("menu: bug report not written: " + report_why).c_str());
        return "";
    }
    struct Item { std::string name, status, reason, incident; uint64_t size = 0; bool shareable = false; };
    std::vector<Item> items;
    std::vector<pipeline_share::Entry> entries;
    std::vector<fs::path> consumed;
    const auto pending = pending_records(in); // a disappeared record is a named failure, not silently forgotten
    const std::string current_run_id = pending.empty() ? "" : log_run_id(in.current_log);
    bool failed = false;
    const auto omit = [&](const std::string& name, const std::string& reason, bool error = false) {
        items.push_back({name, error ? "failed" : "omitted", clean(reason, in), "", 0, false});
        failed |= error;
    };
    const auto keep = [&](const std::string& name, const std::string& data, const std::string& purpose,
                          bool binary = false, const std::string& incident = "") {
        std::string safe = binary ? data : clean(data, in);
        if (!put_file(dir + name, safe)) { omit(name, "could not write file", true); return false; }
        items.push_back({name, "included", purpose, incident, safe.size(), true});
        entries.push_back({name, std::move(safe)});
        return true;
    };
    const auto collect = [&](const std::string& name, const std::string& purpose, const auto& get) {
        try { keep(name, get(), purpose); }
        catch (const std::exception&) { omit(name, "collector failed", true); }
    };
    const auto deadline = began + std::chrono::milliseconds(std::clamp(in.picture_wait_ms, 0, 3000));
    const auto left_ms = [&] { return (int)std::max<int64_t>(0, std::chrono::duration_cast<std::chrono::milliseconds>(
        deadline - std::chrono::steady_clock::now()).count()); };
    const auto capture = [&](const char* name) {
        if (!in.ask_picture || in.previous_run) return false;
        const uint64_t ticket = picture_ticket();
        const std::string path = dir + name;
        if (!in.ask_picture(path.c_str(), ticket) || !wait_picture(ticket, path, left_ms())) return false;
        std::string png;
        if (!read_text(path, png, 8u << 20)) return false;
        items.push_back({name, "included", "next game-source frame, without the menu", "", png.size(), true});
        entries.push_back({name, std::move(png)});
        return true;
    };
    // (first: the frame on screen when the report was asked for)
    const bool picture = capture("picture.png");
    if (!picture) omit("picture.png", in.previous_run ? "not collected for the run that ended" : "no new frame or capture failed");
    // No pixel classifier is exposed by shot_ask. Always take a later frame, so
    // black/loading frames are covered without guessing from compressed PNG bytes.
    if (picture && left_ms() > 250) {
        std::this_thread::sleep_for(std::chrono::milliseconds(250));
        if (!capture("picture-later.png")) omit("picture-later.png", "no later frame or capture failed");
    } else omit("picture-later.png", "first picture unavailable or shared capture deadline reached");
    omit("picture-menu.png", "existing request capture excludes the menu; no new readback path enabled");

    // the logs: this run's, and the run before's (its own after a start from the icon, which moves the last one to .1)
    // Pending incidents additionally match by run_id; their log is never guessed from a rotation number.
    const std::string g0 = in.game_dir + "hookshot_log.txt", g1 = in.game_dir + "hookshot_log.1.txt";
    const std::string before = in.previous_run || same_file(in.current_log, g0, in.windows) ? g1 : exists(g0) ? g0 : g1;
    const auto log_copy = [&](const std::string& source, const char* name, const char* purpose) {
        LogFacts facts;
        if (!source.empty()) facts = copy_log(source, dir + name, in);
        std::string bytes;
        if (facts.copied && read_text(dir + name, bytes, kLogCap + 65536)) {
            std::string detail = purpose;
            if (facts.cut) detail += "; middle omitted (bounded copy)";
            if (bytes.find("CRASH: [x") != std::string::npos)
                detail += "; this log holds the game's own crash lines, including bytes of memory near three registers, as hookshot_log.txt itself does";
            items.push_back({name, "included", detail, "", bytes.size(), true});
            entries.push_back({name, std::move(bytes)});
        } else omit(name, source.empty() ? (!strcmp(name, "hookshot_log.txt") ?
            "this run's log is not a file (terminal or pipe)" : "no earlier/matching log file found") :
            "log could not be read or copied", !source.empty());
        return facts;
    };
    LogFacts now, last;
    std::string last_name = "hookshot_log.1.txt", ended_version;
    std::set<std::string> copied_logs;
    if (!in.previous_run) {
        now = log_copy(in.current_log, "hookshot_log.txt", "this run so far");
        copied_logs.insert("hookshot_log.txt");
        if (!same_file(before, in.current_log, in.windows)) {
            last = log_copy(exists(before) ? before : "", "hookshot_log.1.txt", "the run before; not automatically an incident's log");
            copied_logs.insert("hookshot_log.1.txt");
        }
    }
    std::map<std::string, std::string> log_ids;
    if (!pending.empty()) for (int rotation = 0; rotation <= 4; ++rotation) {
        const std::string name = rotation ? "hookshot_log." + std::to_string(rotation) + ".txt" : "hookshot_log.txt";
        const std::string id = log_run_id(in.game_dir + name);
        if (!id.empty()) log_ids[id] = name;
    }
    for (const auto& record : pending) {
        std::string text;
        if (!read_record(path_text(record), text) || !record_header(text) || (in.previous_run && record_resumed(text))) continue;
        const std::string id = record_value(text, "run_id"), incident = path_text(record.stem());
        if (in.previous_run && text.starts_with("Hookshot stall record 1\n") && valid_run_id(id) && id == current_run_id) continue;
        if (ended_version.empty()) ended_version = record_value(text, "version");
        const auto found = log_ids.find(id);
        if (!valid_run_id(id) || found == log_ids.end()) {
            omit(incident + "-log.txt", valid_run_id(id) ? "the log of that run has aged out (no matching run id among current and .1 to .4)" :
                 "no run id: the log of that record could not be matched; no log guessed");
            continue;
        }
        const std::string source = in.game_dir + found->second;
        std::string name = found->second;
        if (!in.previous_run && same_file(source, in.current_log, in.windows)) name = "hookshot_log.txt";
        else if (!in.previous_run && same_file(source, before, in.windows)) name = "hookshot_log.1.txt";
        else if (copied_logs.count(name)) name = incident + "-log.txt";
        if (copied_logs.insert(name).second) {
            const LogFacts facts = log_copy(source, name.c_str(), ("run id " + id + "; incident " + incident).c_str());
            if (in.previous_run && !last.copied) last = facts, last_name = name;
        } else {
            // Existing manual-report copy is the same file; annotate its identity,
            // never copy it twice or relabel an unrelated rotated log.
            for (auto& item : items) if (item.name == name && item.status == "included")
                item.reason += "; matched run id " + id + "; incident " + incident;
        }
    }
    const LogFacts& scene = in.previous_run ? last : now;
    const std::string version = clean(in.previous_run ? (last.version.empty() ?
        (ended_version.empty() ? "Unknown: see incident record" : ended_version) : last.version) : in.version, in);
    const std::string gpu = clean(in.gpu.empty() ? (in.previous_run ? "Unknown at recovery; see the matched run's startup log." :
        "Not known: the game has no Vulkan device.") : in.gpu, in);
    const std::string system = clean(in.system, in);
    std::string names;
    const std::string sw = switches(in.env, &names);
    const std::string options = in.options.empty() ? "Every Insert menu option is at its default.\n" : clean(in.options, in);
    const std::string settings = options + (sw.empty() ? "Switches set: none.\n" :
        "Switches set (only numeric/boolean values; paths, addresses, secrets and arbitrary values left out):\n" + sw);
    keep("settings.txt", settings, "menu differences and safe switch values; not raw configuration files");
    collect("system.txt", in.previous_run ? "system observed at recovery, not the ended run" : "system, memory, storage and power", [&] {
        return in.collect_process ? system_details(in) : in.system_details.empty() ? system + "\nDetailed system probes: unavailable\n" : in.system_details;
    });
    keep("display.txt", in.displays.empty() ? "Display inventory: unavailable\n" : in.displays,
         "only display and swapchain information the game knows");
    collect("software.txt", "mapped library basenames and configured layers; not proof of active layers", [&] {
        return in.collect_process ? software_text(in) : in.software.empty() ? std::string("Software snapshot: unavailable\n") : in.software;
    });
    std::string situation = in.previous_run ? "The run that ended; state below is from its rotated log/records.\n" : in.situation;
    situation += "Last logged map/age: " + (scene.map.empty() ? std::string("unknown") : scene.map) + "\n";
    situation += in.previous_run ? "Uptime: see pending record; current process uptime is not substituted.\n" :
                                  in.collect_process ? uptime_text() : "Uptime: unavailable in this snapshot\n";
    situation += "Position, room, quest, authoritative loading state and loaded save slot: unavailable without host work.\n";
    keep("game-state.txt", situation, "last-known menu and log state; unknown fields are explicit");
    std::string areas = "Last 16 area changes retained from this log, oldest first. Original timestamps are kept when present;\n"
                        "the normal map log has no transition clock, so missing times cannot be reconstructed.\n";
    for (const auto& area : scene.areas) areas += area + "\n";
    if (scene.areas.empty()) areas += "(no map line found)\n";
    keep("area-history.txt", areas, "bounded area-change log excerpts with original timing only");
    std::string performance = in.previous_run ? "No live performance attributed to the ended run.\n" : in.performance;
    if (in.collect_process && !in.previous_run) performance += process_memory();
    performance += "Latest existing log counters (not newly enabled measurements):\n";
    for (const auto& [key, line] : scene.metrics) performance += line + "\n";
    std::ostringstream csv, stats;
    csv << "seconds_from_report,frame_ms,gpu_recording_ms\n" << std::fixed << std::setprecision(6);
    std::vector<double> ms;
    double total = 0, longest = 0;
    if (!in.previous_run) for (const auto& frame : in.frames) {
        if (ms.size() == 512) break;
        if (!std::isfinite(frame.seconds) || !std::isfinite(frame.milliseconds) || frame.milliseconds <= 0) continue;
        csv << frame.seconds << "," << frame.milliseconds << ",";
        if (std::isfinite(frame.gpu_ms) && frame.gpu_ms > 0) csv << frame.gpu_ms;
        csv << "\n";
        ms.push_back(frame.milliseconds);
        total += frame.milliseconds;
        longest = std::max(longest, frame.milliseconds);
    }
    stats << std::fixed << std::setprecision(3);
    if (!ms.empty()) {
        std::sort(ms.begin(), ms.end(), std::greater<double>());
        const size_t slow = (ms.size() + 99) / 100;
        double low_total = 0;
        for (size_t i = 0; i < slow; ++i) low_total += ms[i];
        stats << "Existing frames: " << ms.size() << "\nMean frame_ms: " << total / ms.size()
              << "\n1% low_fps (1000 / mean of slowest ceil(N/100)): " << 1000 * slow / low_total
              << "\nLongest gap_ms: " << longest << "\n";
    } else stats << "Frame history: unavailable (Off/FPS collects no history; no measurement enabled for this report).\n";
    performance += stats.str() + "GPU column blank: not collected/unknown; GPU query results can lag the frame. "
                   "These are diagnostic samples, not a controlled benchmark or display latency.\n";
    keep("frame-times.csv", csv.str(), "up to 512 existing raw present intervals; no new measurement during play");
    keep("performance.txt", performance, "sample summary, process memory and existing renderer/cache counters");
    keep("input.txt", in.input + "\nController connection log (names as seen by the game; not a live device inventory):\n" +
         (scene.controllers.empty() ? "unavailable (no controller log line)\n" : scene.controllers),
         "layout, deadzones, bindings and bounded controller-name log evidence");

    const std::string save_rule = "LOCAL ONLY: any save/ copy is for retesting on this PC, must not be shared, and is never in report.zip.\n"
        "CONTRIBUTING.md: \"No game files, keys, or anything made from them leave your PC.\" This includes saves.\n";
    std::string saves = "Loaded slot: unknown (not exposed to the renderer; newest write is not the loaded slot).\n"
                        "Only names, sizes and modification times are collected by default; no save contents are read.\n";
    std::string root = in.save_root.empty() ? scene.save_root : in.save_root;
    if (!in.windows) std::replace(root.begin(), root.end(), '\\', '/');
    std::error_code ec;
    bool selected = false, any_slot = false;
    const std::regex slot_name("slot_[0-9]{2}");
    const bool valid_selection = std::regex_match(in.save_slot, slot_name);
    if (!root.empty()) {
        unsigned scanned = 0;
        for (fs::directory_iterator it(path_of(root), ec), end; !ec && it != end && scanned++ < 1024; it.increment(ec)) {
            const std::string slot = path_text(it->path().filename());
            if (!std::regex_match(slot, slot_name) || !fs::is_directory(it->symlink_status(ec))) continue;
            any_slot = true;
            selected |= valid_selection && slot == in.save_slot;
            unsigned files = 0;
            for (fs::directory_iterator file_it(it->path(), ec), file_end; !ec && file_it != file_end && files++ < 64; file_it.increment(ec)) {
                const fs::path file = file_it->path();
                const std::string name = path_text(file.filename());
                if (!fs::is_regular_file(file_it->symlink_status(ec))) { ec.clear(); continue; }
                // File names are metadata, but not arbitrary private text. Refuse a
                // copy whose name would need redaction rather than misname its payload.
                if (!std::regex_match(name, std::regex("[A-Za-z0-9_.-]{1,63}")) || clean(name, in) != name) {
                    saves += slot + ": one non-shareable file name omitted\n";
                    continue;
                }
                const uintmax_t size = fs::file_size(file, ec);
                std::time_t changed = 0;
                exists(path_text(file), &changed);
                saves += slot + "/" + name + ": " + std::to_string(size) + " bytes; last written " +
                         when(changed, "%Y-%m-%d %H:%M:%S") + " local time\n";
                if (valid_selection && slot == in.save_slot && !in.previous_run) {
                    std::string bytes, error;
                    const std::string local = "save/" + slot, relative = local + "/" + name;
                    const auto changed_before = fs::last_write_time(file, ec);
                    if (make_dir(dir + "save", &error) && make_dir(dir + local, &error) &&
                        read_text(path_text(file), bytes, 32u << 20) && bytes.size() == size &&
                        fs::last_write_time(file, ec) == changed_before && !ec && put_file(dir + relative, bytes))
                        items.push_back({relative, "included", "LOCAL ONLY; never shared or zipped", "", bytes.size(), false});
                    else omit(relative, "local copy failed (unreadable, changed or above 32 MiB)", true);
                }
            }
            if (ec) saves += slot + ": metadata enumeration failed\n", ec.clear();
        }
    }
    saves += "Copies are best-effort while play continues, not a save-drain or a proven resumable checkpoint.\n";
    if (!any_slot) saves += "Slot metadata: unavailable (no mounted save root or no readable slot directories).\n";
    if (!in.save_slot.empty()) {
        saves += "Copy selected by the player, not identified as loaded: " + (valid_selection ? in.save_slot : "(invalid slot name)") + ".\n";
        if (!valid_selection || !selected || in.previous_run) omit("save/", "copy refused: invalid/missing selected slot, or recovery report");
    } else omit("save/", "not requested (default); no save content read or copied");
    keep("save-metadata.txt", saves + save_rule, "slot file sizes/times, not game data; optional copy is local only");

    for (const fs::path& record : pending) {
        const std::string name = path_text(record.filename()), incident = path_text(record.stem());
        std::string text;
        if (!read_record(path_text(record), text)) { omit(name, "record missing, unreadable or above 1 MiB", true); continue; }
        if (!record_header(text)) { omit(name, "unknown record header"); continue; }
        if (in.previous_run && record_resumed(text)) continue;
        const std::string record_id = record_value(text, "run_id");
        if (in.previous_run && text.starts_with("Hookshot stall record 1\n") && valid_run_id(record_id) && record_id == current_run_id) continue;
        // The watchdog may still hold this file open and append "resumed". A
        // copy must not unlink that live inode (POSIX) or depend on share-delete.
        const bool retain_live = text.starts_with("Hookshot stall record 1\n") && !record_resumed(text) &&
            (!valid_run_id(current_run_id) || !valid_run_id(record_id) || record_id == current_run_id);
        const char* purpose = retain_live ? "unresolved current/unknown-run stall snapshot; pending source retained for resume updates" :
            name.starts_with("unclean-") ? "last run did not end cleanly; not evidence of a crash" :
            "pending diagnostic record (known bounded fields only)";
        if (keep(name, record_text(text, in), purpose, false, incident) && !retain_live) consumed.push_back(record);
        auto companion = record;
        companion.replace_extension(".dmp");
        if (exists(path_text(companion))) omit(path_text(companion.filename()), "binary dump stays local in pending; cannot be text-redacted");
        companion = record;
        companion.replace_extension(".stacks.txt");
        if (exists(path_text(companion))) {
            std::string stacks;
            if (!read_record(path_text(companion), stacks, 4u << 20) || !stacks.starts_with("Hookshot stall stacks 1\n")) {
                omit(path_text(companion.filename()), "stack file unreadable or unknown format", true);
            } else {
                std::istringstream lines(stacks);
                std::string line, safe = "Hookshot stall stacks 1\n";
                unsigned left = 0, count = 0;
                const std::regex summary("threads: [0-9]+ \\(limit 512; best effort, not simultaneous\\)|virtual core [0-7]: host thread [0-9]+|virtual cores: unavailable \\(lock busy\\)");
                const std::regex frame("  [0-9]{1,2} ([A-Za-z0-9_.-]{1,127}\\+0x[0-9a-fA-F]{1,16}|unknown \\(address omitted\\))");
                const std::regex thread("thread ([0-9]+) \"[^\"]{1,95}\" (\\(partial stack\\)|\\(unavailable or watchdog\\))");
                std::smatch match;
                std::getline(lines, line);
                while (std::getline(lines, line)) {
                    if (++count > 26000 || line.size() > 256) { ++left; continue; }
                    if (std::regex_match(line, match, thread)) safe += "thread " + match[1].str() + " " + match[2].str() + "\n";
                    else if (std::regex_match(line, summary) || std::regex_match(line, frame)) safe += clean(line, in) + "\n";
                    else if (!line.empty()) ++left;
                }
                safe += "Reader: " + std::to_string(left) + " lines left out; thread labels omitted.\n";
                if (keep(path_text(companion.filename()), safe, "bounded thread IDs and module offsets, no raw stack memory", false, incident) &&
                    !retain_live) consumed.push_back(companion);
            }
        }
    }

    // (the link fills in the form's one-line answers and, while the whole link stays within kLinkMax, the options changed and
    // the switches' names: never a switch's value, which report.txt and settings.txt hold. When they do not fit, the options
    // and how many switches there are, else only the two counts; oversized one-line fields are omitted, not a too-long URL)
    std::string link = kForm;
    bool link_short = false;
    const auto query = [&](const char* key, const std::string& value) {
        const std::string part = "&" + std::string(key) + "=" + url_part(clean(value, in));
        if (link.size() + part.size() <= kLinkMax) link += part;
        else link_short = true;
    };
    query("version", version); query("where", scene.map); query("gpu", gpu); query("system", system);
    const auto count = [](const std::string& s) { return std::to_string(std::count(s.begin(), s.end(), '\n')); };
    const std::string named = sw.empty() ? "Switches set: none.\n" : "Switches set (values in report.txt): " + names + ".\n";
    const std::string listed = "Switches set: " + count(sw) + ", listed in the attached report.txt.\n";
    for (const std::string& s : {options + named, options + listed, count(in.options) + " menu options changed; " + listed}) {
        if (link.size() + 10 + url_part(s).size() <= kLinkMax) { query("settings", s); break; }
        link_short = true;
    }
    // Redact before encoding; keep the trusted, constant form URL intact.
    if (put_file(dir + "issue-link.txt", link + "\n")) {
        entries.push_back({"issue-link.txt", link + "\n"});
        items.push_back({"issue-link.txt", "included", "form link, " + std::to_string(link.size()) +
                        " ASCII characters, capped at 2000; game opens nothing", "", link.size() + 1, true});
    } else omit("issue-link.txt", "could not write file", true);
    std::string logs = now.copied ? "hookshot_log.txt: this run's log.\n" : "This run's log absent (terminal, pipe, recovery or read failure).\n";
    if (last.copied) logs += last_name + ": " + (in.previous_run ? "matched incident run, hookshot " : "the run before, hookshot ") + last.version + ".\n";
    if (!now.trouble.empty()) logs += now.trouble + "\n";
    if (!last.trouble.empty()) logs += last.trouble + "\n";
    // (the first line answers the form's one-line field, as the link fills it in; the second says where it comes from)
    const std::string where = scene.map.empty() ? "Not known (no map in this run's log; terminal or no save loaded)." :
        scene.map + "\nLast logged, not a fresh state query. Add what you were doing there." +
        (!in.previous_run && !last.map.empty() ? " The run before was last in " + last.map + "." : "");
    const std::string intro = "Hookshot bug report, " + when(in.now, "%Y-%m-%d %H:%M:%S") + ".\n" +
        (in.previous_run ? "A local report is available for the last run's records. An unclean end or stall does not establish a crash or its cause.\n" : "") +
        "Inspect these files and pictures before attaching report.zip. Nothing is sent or opened automatically.\n" + save_rule +
        "\n### Hookshot version\n\n" + version + "\n\n### Where in the game\n\n" + where +
        "\n\n### Graphics card and driver version\n\n" + gpu + "\n\n### CPU, RAM and operating system\n\n" + system +
        "\n\n### Changes from the defaults\n\n" + clean(settings, in) + "\n### hookshot_log.txt\n\n" + logs +
        "\n### Screenshots or video\n\n" + (picture ? "picture.png and any picture-later.png: game-source frames without the menu.\n" :
        "No picture: no new frame or a recovery report.\n") +
        "A second picture is always requested 250 ms later; black/loading classification is unavailable.\n";
    const auto quoted = [](const std::string& s) {
        std::string out = "\"";
        for (unsigned char c : s) {
            if (c == '"' || c == '\\') out += '\\', out += (char)c;
            else if (c < 32) { char hex[7]; snprintf(hex, sizeof hex, "\\u%04x", c); out += hex; }
            else out += (char)c;
        }
        return out + "\"";
    };
    const size_t report_index = items.size();
    items.push_back({"report.txt", "included", "issue-form answers and human-readable manifest", "", 0, true});
    const size_t manifest_index = items.size();
    items.push_back({"manifest.json", "included", "attachment metadata; published last", "", 0, true});
    const size_t zip_index = items.size();
    items.push_back({"report.zip", "included", "shareable listed attachments only; no save or dump, no nested zip", "", 0, true});
    const auto render_report = [&] {
        std::string text = intro + "\n### What this folder holds\n\n";
        for (const auto& item : items)
            text += item.name + ": " + item.status + ", " + std::to_string(item.size) + " bytes, " +
                    (item.shareable ? "shareable; " : "not shared; ") + item.reason +
                    (item.incident.empty() ? "" : "; incident " + item.incident) + "\n";
        return text + "\nForm link" + std::string(link_short ? " (answers in short where they did not fit)" : "") + ":\n" + link + "\n";
    };
    const auto render_manifest = [&] {
        std::string text = "{\n  \"schema\": 1,\n  \"report_id\": " + quoted(path_text(path_of(dir).parent_path().filename())) +
            ",\n  \"when\": " + quoted(when(in.now, "%Y-%m-%dT%H:%M:%S%z")) + ",\n  \"files\": [\n";
        for (size_t i = 0; i < items.size(); ++i) {
            const auto& item = items[i];
            text += "    {\"name\": " + quoted(item.name) + ", \"size\": " + std::to_string(item.size) +
                    ", \"status\": " + quoted(item.status) + ", \"reason\": " + quoted(item.reason) +
                    ", \"shareable\": " + (item.shareable ? "true" : "false") +
                    (item.incident.empty() ? "" : ", \"incident_id\": " + quoted(item.incident)) + "}" +
                    (i + 1 < items.size() ? ",\n" : "\n");
        }
        return text + "  ]\n}\n";
    };
    // Only decimal digit widths depend on these three sizes. Stabilize the metadata
    // before writing, so the copies in the ZIP are byte-identical to the folder.
    std::string text, metadata;
    for (int pass = 0; pass < 16; ++pass) {
        text = render_report(); metadata = render_manifest();
        uint64_t size = 22;
        for (const auto& entry : entries) size += 76 + 2 * entry.name.size() + entry.data.size();
        size += 76 + 2 * strlen("report.txt") + text.size() + 76 + 2 * strlen("manifest.json") + metadata.size();
        if (items[report_index].size == text.size() && items[manifest_index].size == metadata.size() && items[zip_index].size == size) break;
        items[report_index].size = text.size(); items[manifest_index].size = metadata.size(); items[zip_index].size = size;
    }
    if (!put_file(dir + "report.txt", text)) {
        std::lock_guard lk(report_mu);
        report_why = "report.txt could not be written in " + rel;
        return "";
    }
    entries.push_back({"report.txt", text}); entries.push_back({"manifest.json", metadata});
    std::tm tm{}; local_time(in.now, &tm);
    const auto zip = pipeline_share::zip(entries, (uint16_t)((std::clamp(tm.tm_year, 80, 207) - 80) << 9 |
                    (tm.tm_mon + 1) << 5 | tm.tm_mday), (uint16_t)(tm.tm_hour << 11 | tm.tm_min << 5 | tm.tm_sec / 2));
    if (!put_file(dir + "report.zip", zip)) {
        failed = true;
        items[zip_index].status = "failed"; items[zip_index].size = 0; items[zip_index].shareable = false;
        items[zip_index].reason = "could not write bundle; individual files remain";
        for (int pass = 0; pass < 8; ++pass) {
            text = render_report(); metadata = render_manifest();
            items[report_index].size = text.size(); items[manifest_index].size = metadata.size();
        }
        put_file(dir + "report.txt", text);
    }
    // Publish the index last. Failed optional copies do not discard a usable report,
    // but no original incident is consumed unless every attempted write succeeded.
    const std::string staged_manifest = dir + "manifest.json.new";
    const bool staged = put_file(staged_manifest, metadata);
    ec.clear();
    if (staged) fs::rename(path_of(staged_manifest), path_of(dir + "manifest.json"), ec);
    if (!staged || ec) {
        failed = true;
        fs::remove(path_of(staged_manifest), ec);
        fs::remove(path_of(dir + "report.zip"), ec);
        put_file(dir + "report.txt", text + "\nmanifest.json: FAILED to publish; original pending records retained.\n"
                 "report.zip: omitted because the manifest could not be published.\n");
    }
    if (!failed) for (const auto& record : consumed) fs::remove(record, ec);
    if (in.log) in.log(("menu: bug report written to " + rel + (failed ? " (partial; pending records retained)" : "") +
                       (in.previous_run ? " (local report available for last run's records)" : "")).c_str());
    return rel;
}

bool recover(Inputs in) {
    std::string current_run_id;
    bool have_identity = false;
    for (const auto& record : pending_records(in)) {
        std::string text;
        if (!read_record(path_text(record), text) || !record_header(text) || record_resumed(text)) continue;
        if (!in.now) fill_here(in);
        if (!have_identity) current_run_id = log_run_id(in.current_log), have_identity = true;
        const std::string record_id = record_value(text, "run_id");
        if (text.starts_with("Hookshot stall record 1\n") && valid_run_id(record_id) && record_id == current_run_id) continue;
        in.previous_run = true;
        in.ask_picture = nullptr;
        return start(std::move(in));
    }
    return false;
}

bool start(Inputs in) {
    int was = report_state.load();
    if (was == kSaving || !report_state.compare_exchange_strong(was, kSaving)) return false;
    // The other user of this header is the synchronous pipeline-share menu action.
    // Initialize its CRC table here on that same menu thread, before the worker.
    pipeline_share::crc32("", 0);
    std::thread([in = std::move(in)] {
        std::string rel;
        try { rel = write(in); }
        catch (const std::exception&) {
            std::lock_guard lk(report_mu);
            report_why = "report collection failed; original pending records retained";
        }
        {
            std::lock_guard lk(report_mu);
            report_shown = rel.empty() ? report_why : rel;
        }
        report_state.store(rel.empty() ? kFailed : kSaved);
    }).detach();
    return true;
}

State state() { return (State)report_state.load(); }

std::string shown() {
    std::lock_guard lk(report_mu);
    return report_shown;
}

void picture_written(uint64_t ticket, bool ok) {
    std::string late;
    {
        std::lock_guard lk(pic_mu);
        const auto it = late_pictures.find(ticket);
        if (it != late_pictures.end()) late = std::move(it->second), late_pictures.erase(it);
        else pic_done = ticket, pic_ok = ok;
    }
    if (!late.empty()) { std::error_code ec; fs::remove(path_of(late), ec); }
    pic_cv.notify_all();
}

}  // namespace mkwvk::bug_report
