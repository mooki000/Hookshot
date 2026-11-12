"""Game-free Bug report fixtures, authored inputs only.

Compiles the writer and the locked raw-history accessor. Checks every diagnostic,
issue-form compatibility, complete attachment manifests/ZIPs, privacy, independent
file failures, pending crash/stall/unclean recovery, bounded structured ingestion,
late captures, save metadata and the explicit LOCAL-only save-copy boundary.
The same fixtures run on Linux and Windows; real platform/device probes and game
screenshots need separate runtime evidence.
"""
import argparse
import importlib.util
import os
import json
import time
import zipfile
from urllib.parse import unquote
import re
import subprocess
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
VERSION = "v2.1.0-rc.3-39-gbdfe09c (source 763271ad)"
EXTRAS = {"settings.txt", "system.txt", "display.txt", "software.txt", "game-state.txt", "area-history.txt",
          "frame-times.csv", "performance.txt", "input.txt", "save-metadata.txt", "issue-link.txt",
          "manifest.json", "report.zip"}
RUN_ALL_ARGS = ["--cxx", "{cxx}"]

DRIVER = r'''#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif
#include "vk_bug_report.h"
#include <atomic>
#include <chrono>
#include <filesystem>
#include <cstdio>
#include <string>
#include <thread>
using namespace mkwvk::bug_report;
static int fails;
#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #x); fails++; } } while (0)
static void say(const char* line) { std::printf("LOG %s\n", line); }
static std::thread painter;
static bool ask(const char* path, uint64_t ticket) {   // (vk_replay.cpp's part: the PNG of a later present, on a thread)
    if (painter.joinable()) painter.join();
    painter = std::thread([p = std::string(path), ticket] {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        if (FILE* f = std::fopen(p.c_str(), "wb")) std::fputs("PNG stand-in", f), std::fclose(f);
        picture_written(ticket, true);
    });
    return true;
}
static std::string late_path;   // (the late picture: asked for, and written only once the report has gone without it)
static uint64_t late_ticket;
static bool ask_late(const char* path, uint64_t ticket) { late_path = path, late_ticket = ticket; return true; }
static std::atomic<bool> held{true};   // (the menu's way: the report is still saving until the driver lets its picture ask go)
static bool ask_held(const char*, uint64_t) {   // (no frame comes: the report goes without a picture)
    while (held.load()) std::this_thread::sleep_for(std::chrono::milliseconds(5));
    return false;
}
static std::string failed_item, incident;
static bool ask_failure(const char* path, uint64_t) {
    const auto folder = std::filesystem::path((const char8_t*)path).parent_path();
    if (failed_item == "drop-record") std::filesystem::remove(std::filesystem::path((const char8_t*)incident.c_str()));
    else std::filesystem::create_directory(folder / failed_item);
    return false;
}

int main(int argc, char** argv) {
    if (argc < 7) return 2;
    const std::string mode = argv[1];
    Inputs in;
    in.version = "''' + VERSION + r'''";
    in.gpu = "NVIDIA GeForce RTX 4090, NVIDIA 580.95.05";
    in.options = "Upscaler: NVIDIA DLSS (default: Game's)\n";
    in.system = "13th Gen Intel(R) Core(TM) i9-13900K (32 threads), 94 GB, Arch Linux (Linux 7.2.15)";
    in.env = {"HOS_HEADLESS=1", std::string("RX_VK_SHOTS=") + argv[4] + "/shots", "RX_VK_MENU_SCRIPT=1:INS,2:ENTER",
              "PATH=/usr/bin", "MANGOHUD=1", "HOS_ENV_FILE=C:\\x\\env.txt", "SECRET_TOKEN=abc", "=C:=C:\\x",
              "RX_VK_SHOT_EVERY=60", "RX_VK_X_MODE=off", "HOS_OUTPUT_RES=3840x2160", "RX_VK_NEG=-1", "RX_VK_SCALE=1.5",
              "HOS_COOP_ROOM=93127", "RX_API_KEY=58213", "HOS_COOP_SERVER=47731", "HOS_COOP_USER=1", "HOS_COOP_PASSWORD=hunter2",
              "HOS_RELAY=192.168.1.5", "HOS_RELAY_HOST=relay.example.org:47732", "RX_VK_LONG=1234567890123"};
    if (mode == "many")   // (a long hookshot_env.txt: more switches than a link takes)
        for (int i = 0; i < 80; i++) in.env.push_back("RX_TEST_SWITCH_" + std::to_string(i) + "=" + std::string(20, 'v'));
    in.game_dir = argv[2];
    in.current_log = argv[3];
    in.home = argv[4];
    in.user = argv[5];
    in.windows = std::string(argv[6]) == "1";
    in.hostname = "private-machine";
    const std::string private_text = in.home + "/example " + in.user +
        " private-machine 192.168.42.9 fd00::1234 fe80::1%eth0 2001:db8:0:1:2:3:4:5 user@example.org";
    in.system_details = "kernel: Fixture 1.0\nFilesystem: btrfs\nRotational: no\nRAM in use: 1024 KiB\nSwap in use: 16 KiB\n"
                        "Power: battery\nSteam Deck hardware: yes\nCompositor: gamescope\nClock: 12:34:56\n" + private_text + "\n";
    in.displays = "Known desktop size: 1280x800\nrefresh_mHz: 60000\nHDR: unknown\nSwapchain: 1280x800\nPresent mode: FIFO\n" + private_text;
    in.software = "layer libraries mapped into the process: libVkLayer_fixture.so\n" + private_text;
    in.situation = "Last published age: child\n" + private_text;
    in.performance = "Texture budget_MiB: 4096\nCache hits: 25\nPreparation: completed fixture\n" + private_text + "\n";
    in.input = "Layout preset: Xbox / PC\nStick deadzone: 10%\nA key 1: F (default code 69)\n" + private_text;
    in.save_root = in.game_dir + "saves";
    for (int i = 0; i < 300; ++i) in.frames.push_back({(i - 300) / 60.0, i >= 297 ? 100.0 : 10.0, 3.0});
    if (mode == "save") in.save_slot = "slot_00";
    if (mode == "invalid-save") in.save_slot = "../private";
    if (mode == "missing-save") in.save_slot = "slot_99";
    if (mode.starts_with("fail-")) {
        failed_item = mode.substr(5);
        incident = in.game_dir + "bug-reports/pending/crash-20261112T120000Z.txt";
        in.ask_picture = ask_failure;
    }
    if (mode == "huge-fields") in.version = in.gpu = in.system = std::string(4000, 'X');
    if (mode == "process") in.collect_process = true;
    in.now = 1789000000;
    in.log = say;
    in.picture_wait_ms = mode == "picture" ? 30000 : 300;   // (a picture ends the wait as soon as it is written)
    if (mode == "picture") in.ask_picture = ask;
    if (mode == "late") in.ask_picture = ask_late;
    if (mode == "recover" || mode == "recover-empty") {
        const bool got = recover(in);
        CHECK(got == (mode == "recover"));
        for (int i = 0; i < 3000 && state() == kSaving; ++i) std::this_thread::sleep_for(std::chrono::milliseconds(10));
        if (got) CHECK(state() == kSaved);
        std::printf("FOLDER %s\n", got ? shown().c_str() : "");
        return fails ? 1 : 0;
    }
    if (mode == "start") {   // (the menu's way: on a thread of its own, its state read each frame)
        in.ask_picture = ask_held;
        CHECK(start(in));
        CHECK(state() == kSaving);
        CHECK(!start(in));   // (one at a time)
        held = false;
        for (int i = 0; i < 3000 && state() == kSaving; i++) std::this_thread::sleep_for(std::chrono::milliseconds(10));
        CHECK(state() == kSaved);
        std::printf("FOLDER %s\n", shown().c_str());
    } else {
        const std::string folder = write(in);
        std::printf("FOLDER %s\n", folder.c_str());
        if (mode == "terminal") std::printf("FOLDER %s\n", write(in).c_str());   // (a second one, the same second)
        if (mode == "late") {   // (the picture comes after the report went without it: removed again)
            if (FILE* f = std::fopen(late_path.c_str(), "wb")) std::fputs("PNG stand-in", f), std::fclose(f);
            picture_written(late_ticket, true);
            FILE* png = std::fopen((in.game_dir + folder + (in.windows ? "\\" : "/") + "picture.png").c_str(), "rb");
            std::printf("LATE %s %s\n", png ? "kept" : "removed", late_path == in.game_dir + folder + (in.windows ? "\\" : "/") + "picture.png" ? "(asked for the report's own picture.png)" : "(asked for another path)");
            if (png) std::fclose(png);
        }
    }
    if (painter.joinable()) painter.join();

    // the redaction itself: whole folders only, either slash on Windows, whatever the case there
    CHECK(redact("-> /home/al/Games/x\n", "/home/al", "al", false) == "-> ~/Games/x\n");
    CHECK(redact("/home/al", "/home/al/", "al", false) == "~");
    CHECK(redact("/home/alice/x /home/al", "/home/al", "al", false) == "/home/alice/x ~");
    CHECK(redact("/run/media/al/SD, /var/home/al/x, user\\al\\save", "/home/al", "al", false) ==
          "/run/media/<user>/SD, /var/home/<user>/x, user\\<user>\\save");
    CHECK(redact("Malice and al, /x/alx/ and vk: al", "/home/al", "al", false) == "Malice and al, /x/alx/ and vk: al");
    CHECK(redact("/HOME/AL/x", "/home/al", "al", false) == "/HOME/AL/x");   // (Linux: case counts)
    CHECK(redact("C:\\Users\\John Smith\\Games\\x", "C:\\Users\\John Smith", "John Smith", true) == "%USERPROFILE%\\Games\\x");
    CHECK(redact("c:/users/john smith/Games", "C:\\Users\\John Smith", "John Smith", true) == "%USERPROFILE%/Games");
    CHECK(redact("D:\\Users\\JOHN SMITH\\x", "C:\\Users\\John Smith", "John Smith", true) == "D:\\Users\\<user>\\x");
    CHECK(redact("nothing here", "", "", false) == "nothing here");
    CHECK(library_names("123 r-xp 000 00:00 0 /home/al/libVkLayer_fixture.so\n456 rw-p 000 00:00 0 /home/al/libVkLayer_fixture.so\n", true) ==
          "libVkLayer_fixture.so\n");
    CHECK(library_names("/home/al/liboverlay.so:/opt/libinject.so", false) == "libinject.so\nliboverlay.so\n");
    CHECK(memory_fields("MemTotal: 1000 kB\nMemAvailable: 750 kB\nSwapTotal: 100 kB\nSwapFree: 90 kB\nName: private\n", false).find("RAM in use: 250 KiB") != std::string::npos);
    CHECK(memory_fields("VmRSS: 123 kB\nVmSwap: 4 kB\nUid: 1000 1000\n", true) == "VmRSS: 123 KiB\nVmSwap: 4 KiB\n");
    CHECK(std::string(filesystem_name(0x9123683e)) == "btrfs");
    CHECK(std::string(filesystem_name(0x01021994)) == "tmpfs");
    CHECK(std::string(filesystem_name(0x12345678)) == "unknown");
    CHECK(system_power_supply("System\n") && !system_power_supply("Device\n") && !system_power_supply("Unknown\n"));

    // this PC's part: the system as the form asks it, and what fill_here finds
    std::printf("SYSTEM %s\n", system_text().c_str());
    Inputs here;
    fill_here(here);
    CHECK(here.system == system_text() && here.now > 0 && !here.env.empty());
    return fails ? 1 : 0;
}
'''


def load_hardware_report():
    spec = importlib.util.spec_from_file_location("hardware_report", ROOT / "scripts" / "hardware_report.py")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def check(ok, why):
    if not ok:
        raise SystemExit("bug report: " + why)


def run(exe, mode, game, current, home, user, windows):
    out = subprocess.run([str(exe), mode, game, current, home, user, "1" if windows else "0"], capture_output=True,
                         text=True, encoding="utf-8", timeout=60)
    if out.returncode:
        raise SystemExit("bug report driver (%s) failed:\n%s%s" % (mode, out.stdout, out.stderr))
    lines = out.stdout.splitlines()
    return [ln[7:] for ln in lines if ln.startswith("FOLDER ")], lines


def files_of(folder):
    return {p.name: p.read_text(encoding="utf-8", errors="replace") for p in folder.iterdir() if p.is_file() and p.suffix != ".zip"}


def no_private(files, home, user, game):
    for name, text in files.items():
        for needle in (home, home.replace("\\", "/"), game.rstrip("\\/")):
            check(needle.lower() not in text.lower(), "%s names %s" % (name, needle))
        check(not re.search(r"[/\\]" + re.escape(user) + r"(?![A-Za-z0-9._-])", text, re.I), "%s names the user as a folder" % name)
        for needle in (user, "private-machine", "192.168.42.9", "fd00::1234", "fe80::1%eth0", "2001:db8:0:1:2:3:4:5", "user@example.org"):
            check(not re.search(r"(?<![A-Za-z0-9._-])" + re.escape(needle) + r"(?![A-Za-z0-9._-])", unquote(text), re.I),
                  "%s holds private token %s" % (name, needle))


def bundle(folder, home, user, game):
    meta = json.loads((folder / "manifest.json").read_text(encoding="utf-8"))
    check(meta["schema"] == 1 and meta["report_id"] == folder.name, "manifest identity")
    expected = set()
    for entry in meta["files"]:
        name = entry["name"]
        check(not Path(name).is_absolute() and ".." not in Path(name).parts, "manifest path escaped folder")
        if entry["status"] == "included":
            check((folder / name).stat().st_size == entry["size"], "manifest size differs for " + name)
            if entry["shareable"] and name != "report.zip":
                expected.add(name)
        else:
            check(entry["reason"] and not entry["shareable"], "omission needs a reason and must not be shareable")
    with zipfile.ZipFile(folder / "report.zip") as zipped:
        check(set(zipped.namelist()) == expected and zipped.testzip() is None, "zip is not the manifest's explicit shareable list")
        for name in expected:
            check(zipped.read(name) == (folder / name).read_bytes(), "zip differs from folder for " + name)
        no_private({name: zipped.read(name).decode("utf-8", "replace") for name in expected}, home, user, game)
    no_private(files_of(folder), home, user, game)
    return meta


def history_fixture(cxx, tmp, windows):
    source = (ROOT / "gpu/vk/vk_fps.cpp").read_text(encoding="utf-8")
    start = source.index("int fps_recent(")
    function = source[start:source.index("\n}\n", start) + 3]
    check("std::lock_guard" in function and "ring_mu" in function, "history accessor must use the existing ring lock")
    presented = source[source.index("void fps_presented()"):source.index("fs::Summary fps_summary")]
    check("std::lock_guard" in presented and "ring_mu" in presented, "frame path no longer uses the same ring lock")
    fixture = r'''#include "fps_stats.h"
#include <mutex>
#include <thread>
#include <atomic>
namespace fs = mkwvk::fps_stats;
std::mutex ring_mu;
fs::Ring<fs::kRing> ring;
''' + function + r'''
int main() {
    fs::Frame copy[512];
    if (fps_recent(copy, 512) != 0 || fps_recent(nullptr, 512) != 0 || fps_recent(copy, 0) != 0) return 1;
    for (int i = 0; i < fs::kRing + 17; ++i) { fs::Frame f; f.at_ns = i; f.ms = float(i); ring.push(f); }
    if (fps_recent(copy, 512) != 512 || copy[0].at_ns != fs::kRing + 17 - 512 || copy[511].at_ns != fs::kRing + 16) return 2;
    std::atomic<bool> began{false}, ended{false};
    std::unique_lock held(ring_mu);
    std::thread t([&] { began = true; fps_recent(copy, 512); ended = true; });
    while (!began) std::this_thread::yield();
    if (ended) return 3;
    held.unlock(); t.join();
    return ended ? 0 : 4;
}
'''
    cpp, exe = tmp / "history.cpp", tmp / ("history.exe" if windows else "history")
    cpp.write_text(fixture, encoding="utf-8")
    subprocess.run([cxx, "-std=c++20", "-O1", "-Wall", "-Wextra", "-Werror", *([] if windows else ["-pthread"]),
                    "-I", str(ROOT / "gpu/vk"), str(cpp), "-o", str(exe)], check=True)
    return exe


def wiring():
    menu = (ROOT / "gpu" / "vk" / "vk_menu.cpp").read_text(encoding="utf-8")
    at = menu.index('{TAB_GAMEPLAY, "Bug report", nullptr')
    row = menu[at:menu.index("},", at)]
    for word in ("report.txt", "logs", "picture", "user name", "No saves", "nothing is sent"):
        check(word in row, "the row's help does not say " + word)
    check(menu.index('"Bug report"') < menu.index('{TAB_GAMEPLAY, "Restart"') and
          "constexpr int kBugReportRow = kRestartRow - 1;" in menu, "the row is not the one before Restart")
    check("&r == &rows[kBugReportRow]) bug_report_save()" in menu[menu.index("void menu_tick() {"):],
          "A or Enter on the row does not save the report")
    check(menu.count('&selr == &rows[kBugReportRow] || is_share_row(selr) ? "Save" : "Open"') +
          menu.count('&r == &rows[kBugReportRow] || is_share_row(r) ? "Save" : "Open"') >= 2,
          "a prompt (the menu's, or the compact bar's) does not say Save for the row")
    save = menu[menu.index("void bug_report_save() {"):]
    save = save[:save.index("\n}\n")]
    for call in ("fill_here(in)", "device_report_text()", "changed_rows_text()", "ini_path()", "bug_report::start("):
        check(call in save, "bug_report_save does not call " + call)
    picture = menu[menu.index("bool bug_report_picture(const char* path, uint64_t ticket) {"):]
    picture = picture[:picture.index("\n}\n")]
    check("in.ask_picture = bug_report_picture;" in save and "shot_ask_next(path," in picture and
          "bug_report::picture_written(" in picture, "the report's picture is not asked of shot_ask_next")
    replay = (ROOT / "gpu" / "vk" / "vk_replay.cpp").read_text(encoding="utf-8")
    nxt = replay[replay.index("int shot_ask_next("):]
    check("return shot_ask(path, -1, done, ctx);" in nxt[:nxt.index("\n")], "shot_ask_next does not ask for the next present")
    record = replay[replay.index("void shot_ask_record("):]
    record = record[:record.index("\n}\n")]
    check("blit_crop(cmd, src, sw, sh, crop, ask_image," in record and "menu" not in record and "ReportShot" not in replay,
          "the picture is not copied from the game's frame into shot_ask's own image, or the menu is drawn into it")
    host = (ROOT / "host" / "hos_main.c").read_text(encoding="utf-8")
    refresh = host[host.index("static void oot_inventory_refresh("):]
    refresh = refresh[:refresh.index("\n}\n")]
    check('hos_log("map: %s", line)' in refresh and "0x1d6189dau" in refresh,
          "the host does not log the map (Sequence_CurrentBanc) the report's place comes from")
    # (the switches the report lists are the names hookshot_env.txt takes: env_line_allowed's prefixes)
    allowed = re.search(r"prefixes\[\] = \{([^}]*)\}", host[host.index("static int env_line_allowed("):]).group(1)
    report = (ROOT / "gpu" / "vk" / "vk_bug_report.cpp").read_text(encoding="utf-8")
    listed = re.search(r"prefixes\[\] = \{([^}]*)\}", report[report.index("std::string switches("):]).group(1)
    check(listed == allowed, "the report lists switches by %s, hookshot_env.txt takes %s" % (listed, allowed))
    form = (ROOT / ".github" / "ISSUE_TEMPLATE" / "1-game-problem.yml").read_text(encoding="utf-8")
    for label in ("Hookshot version", "Where in the game", "Graphics card and driver version", "CPU, RAM and operating system",
                  "Changes from the defaults", "hookshot_log.txt", "Screenshots or video"):
        check("label: " + label + "\n" in form, "the issue form has no field " + label)
    for field in ("version", "where", "gpu", "system", "settings"):
        check("id: " + field + "\n" in form, "the issue form has no field id " + field)
    hid = (ROOT / "host/nn_hid.c").read_text(encoding="utf-8")
    for name in ("pad_default", "key_default"):
        pattern = name + r"\[[^;]+?\};"
        a = re.sub(r"\s+", "", re.search(pattern, menu).group())
        b = re.sub(r"\s+", "", re.search(pattern, hid).group())
        check(a == b, "report binding baseline drifted from host " + name)
    check("bug_report::recover(std::move(report))" in menu and "bug_report_snapshot(in)" in menu,
          "automatic recovery or report snapshot not wired")


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--cxx", default="g++")
    a = p.parse_args()
    wiring()
    hw = load_hardware_report()
    windows = os.name == "nt"
    with tempfile.TemporaryDirectory(prefix="bug-report-") as tmp:
        tmp = Path(tmp)
        history_exe = history_fixture(a.cxx, tmp, windows)
        cpp = tmp / "driver.cpp"
        exe = tmp / ("driver.exe" if windows else "driver")
        cpp.write_text(DRIVER, encoding="utf-8")
        # (_CRT_SECURE_NO_WARNINGS: the MSVC CRT's getenv deprecation, as the game's own Windows build passes)
        subprocess.run([a.cxx, "-std=c++20", "-O1", *([] if windows else ["-pthread"]), "-Wall", "-Wextra", "-Werror",
                        "-D_CRT_SECURE_NO_WARNINGS", "-I", str(ROOT / "gpu" / "vk"), str(cpp),
                        str(ROOT / "gpu" / "vk" / "vk_bug_report.cpp"), "-o", str(exe)], check=True)
        subprocess.run([str(history_exe)], check=True)
        user = "alice"
        home = str(tmp / "home" / user)
        game_path = Path(home) / "Games" / "Hookshot"
        game_path.mkdir(parents=True)
        game = str(game_path) + os.sep
        sep = "\\" if windows else "/"
        home_as = "%USERPROFILE%" if windows else "~"
        now_log = game_path / "hookshot_log.txt"
        now_log.write_text(
            "[hos host] hookshot " + VERSION + "\n"
            "[hos host] run_id: " + "b" * 32 + "\n"
            "[hos sead::MainThread] fs: mounted content: -> " + home + sep + "Games" + sep + "Hookshot" + sep + "romfs\n"
            "[hos sead::MainThread] fs: mounted save: -> " + home + "/Games/Hookshot/user\\save\\5a3c\n"
            "[hos MainThread] vk: pipeline cache /run/media/" + user + "/SD/Hookshot/mkw_vk_pipelines.bin (1 KB)\n"
            "[hos Main] map: KokiriForest_Child (child)\n"
            "[hos Main] vk: VRAM: 6784 MB used; textures 2333 MB (budget 6144 MB), 1152 textures\n"
            "[hos Main] vk: textures: decoded ASTC 1351 MB, rendered 319 MB, other 440 MB\n"
            "[hos Main] vk: status: 600 frames; VRAM 6.6 of 22.1 GB\n"
            "[hos host] Malice and alice123 stay, as /x/alice123/ does\n"
            "[hos Main] map: HyruleField_Child (child)\n", encoding="utf-8")
        (game_path / "hookshot_log.1.txt").write_text(
            "[hos host] hookshot v2.0.14\n"
            "[hos sead::MainThread] fs: mounted content: -> " + home + sep + "Games" + sep + "Hookshot" + sep + "romfs\n"
            "[hos Main] map: Market_Child (child)\n"
            "[hos host] CRASH: exception 0xc0000005 (read 0x0, guest 0x0) at hookshot+0x1234\n"
            "[hos host] CRASH: thread Main, sp 0x1000\n", encoding="utf-8")

        # 1. started from its icon: this run logs to hookshot_log.txt, the one before is hookshot_log.1.txt; a picture comes
        folders, out = run(exe, "picture", game, str(now_log), home, user, windows)
        check(len(folders) == 1 and folders[0].startswith("bug-reports" + sep), "no report folder: %s" % out)
        folder = game_path / folders[0]
        files = files_of(folder)
        check(set(files) == ({"report.txt", "hookshot_log.txt", "hookshot_log.1.txt", "picture.png", "picture-later.png"} | EXTRAS) - {"report.zip"},
              "the folder holds %s" % sorted(files))
        check(set(p.name for p in (game_path / "bug-reports").iterdir()) == {folder.name}, "more than the report's folder")
        no_private(files, home, user, game)
        bundle(folder, home, user, game)
        check("Clock: 12:34:56" in files["system.txt"], "ordinary clock time redacted as IPv6")
        for name, value in {"system.txt": "Rotational: no", "display.txt": "1280x800", "software.txt": "libVkLayer_fixture.so",
                            "game-state.txt": "Last published age: child", "area-history.txt": "HyruleField_Child",
                            "input.txt": "Stick deadzone: 10%", "performance.txt": "1% low_fps"}.items():
            check(value in files[name], name + " lost its authored input")
        rows = files["frame-times.csv"].splitlines()
        check(len(rows) == 301 and all(len(row.split(",")) == 3 for row in rows), "raw frame CSV")
        check("Mean frame_ms: 10.900" in files["performance.txt"] and "1% low_fps (1000 / mean of slowest ceil(N/100)): 10.000" in files["performance.txt"] and
              "Longest gap_ms: 100.000" in files["performance.txt"], "frame summary formula")
        copy = files["hookshot_log.txt"]
        for kept in (home_as + sep + "Games" + sep + "Hookshot" + sep + "romfs", home_as + "/Games/Hookshot/user\\save",
                     "/run/media/<user>/SD/Hookshot", "Malice and alice123 stay, as /x/alice123/ does"):
            check(kept in copy, "the copy of this run's log does not read " + kept)
        check(len(copy.splitlines()) == len(now_log.read_text(encoding="utf-8").splitlines()), "lines lost in the copy")
        report = files["report.txt"]
        section = lambda name: hw.section(report, name)
        check(section("Hookshot version") == VERSION, "the version: " + section("Hookshot version"))
        gpu = section("Graphics card")
        check(gpu == "NVIDIA GeForce RTX 4090, NVIDIA 580.95.05" and hw.classify(gpu) == ("NVIDIA", "RTX 40"),
              "hardware_report.py reads the GPU as %s from %r" % (hw.classify(gpu), gpu))
        check(hw.os_of(section("CPU, RAM and operating system")) == "Linux / SteamOS", "hardware_report.py's OS")
        where = section("Where in the game")
        check(where.startswith("HyruleField_Child (child)") and "Market_Child (child)" in where, "where: " + where)
        settings = section("Changes from the defaults")
        lines = settings.splitlines()
        for want in ("Upscaler: NVIDIA DLSS (default: Game's)", "HOS_HEADLESS=1", "MANGOHUD=1", "RX_VK_SHOT_EVERY=60",
                     "RX_VK_X_MODE=off", "HOS_OUTPUT_RES=3840x2160", "RX_VK_NEG=-1", "RX_VK_SCALE=1.5"):
            check(want in lines, "the settings do not read " + want)
        for left_out in ("RX_VK_SHOTS", "HOS_ENV_FILE", "RX_VK_MENU_SCRIPT", "HOS_COOP_ROOM", "RX_API_KEY", "HOS_COOP_SERVER",
                         "HOS_COOP_USER", "HOS_COOP_PASSWORD", "HOS_RELAY", "HOS_RELAY_HOST", "RX_VK_LONG"):
            check(left_out + "=(value left out)" in lines, "the settings do not leave out the value of " + left_out)
        for unwanted in ("PATH=", "SECRET_TOKEN", "=C:", "93127", "58213", "47731", "hunter2", "192.168", "relay.example", "47732",
                         "1234567890123", "1:INS"):
            check(unwanted not in report, "report.txt holds " + unwanted)
        logs = section("hookshot_log.txt")
        check("hookshot v2.0.14" in logs and "CRASH: exception 0xc0000005" in logs, "the logs' section: " + logs)
        check("picture.png" in section("Screenshots or video"), "the picture is not named")
        link = report[report.index("https://github.com/NotRexed/Hookshot/issues/new?template=1-game-problem.yml"):].split()[0]
        for part in ("&version=v2.1.0-rc.3-39-gbdfe09c%20%28source%20763271ad%29", "&gpu=NVIDIA%20GeForce%20RTX%204090",
                     "&where=HyruleField_Child%20%28child%29", "&system=", "&settings=Upscaler", "HOS_HEADLESS", "RX_VK_MENU_SCRIPT"):
            check(part in link, "the link does not fill in " + part)
        check("%3D" not in link and not any(v in link for v in ("93127", "58213", "hunter2", "192.168", "3840x2160", "1%3AINS")),
              "the link carries a switch's value: " + link)
        check(len(link) <= 2000 and "in short" not in report, "the link: %d characters" % len(link))
        check(any("menu: bug report written to " + folders[0] in ln for ln in out), "nothing logged: %s" % out)

        # 2. the menu's way: on a thread, its state read each frame; no frame comes, so no picture
        folders, out = run(exe, "start", game, str(now_log), home, user, windows)
        files = files_of(game_path / folders[0])
        check("picture.png" not in files and "No picture" in hw.section(files["report.txt"], "Screenshots or video"),
              "a report without a frame: %s" % sorted(files))

        # 2b. the picture comes after the report went without it (no present in time): the report says it has none, and the
        # file is removed when it comes
        folders, out = run(exe, "late", game, str(now_log), home, user, windows)
        files = files_of(game_path / folders[0])
        check("LATE removed (asked for the report's own picture.png)" in out and "picture.png" not in files and
              "No picture" in hw.section(files["report.txt"], "Screenshots or video"), "a late picture: %s %s" % (out, sorted(files)))

        # 3. started from a terminal: no log file this run; the run before's is the game folder's hookshot_log.txt
        folders, out = run(exe, "terminal", game, "", home, user, windows)
        check(len(folders) == 2 and folders[0] != folders[1] and all(folders), "two reports in a second: %s" % folders)
        files = files_of(game_path / folders[0])
        check(set(files) == ({"report.txt", "hookshot_log.1.txt"} | EXTRAS) - {"report.zip"}, "from a terminal the folder holds %s" % sorted(files))
        check("HyruleField_Child" in files["hookshot_log.1.txt"], "the run before's log is not the game folder's last one")
        report = files["report.txt"]
        check("terminal" in hw.section(report, "Where in the game") and "terminal" in hw.section(report, "hookshot_log.txt"),
              "the report does not say where this run's log is")
        no_private(files, home, user, game)

        # 4. a log over 10 MB: its first MB and its last 9 MB, and the map in its middle still counts
        big = tmp / "big.log"
        with open(big, "w", encoding="utf-8", newline="\n") as f:
            f.write("[hos host] hookshot " + VERSION + "\n")
            filler = "[hos host] vk: status: " + "x" * 100 + " " + home + "/Games\n"
            for _ in range(60000):
                f.write(filler)
            f.write("[hos Main] map: DeathMountainTrail_Child (child)\n")
            for _ in range(60000):
                f.write(filler)
            f.write("[hos host] the last line\n")
        folders, out = run(exe, "big", game, str(big), home, user, windows)
        copy_path = game_path / folders[0] / "hookshot_log.txt"
        copy = copy_path.read_text(encoding="utf-8")
        check(copy_path.stat().st_size <= (10 << 20) + 4096, "the long log's copy is %d bytes" % copy_path.stat().st_size)
        check(copy.startswith("[hos host] hookshot " + VERSION) and copy.endswith("[hos host] the last line\n") and
              "of this log's middle left out here" in copy, "the long log's copy lost its start, its end or the note")
        report = (game_path / folders[0] / "report.txt").read_text(encoding="utf-8")
        check(hw.section(report, "Where in the game").startswith("DeathMountainTrail_Child (child)"), "the middle's map")
        check(home not in copy, "the long log's copy names the home folder")

        # 4b. more switches than a link takes: the link stays within 2000 characters and carries the options and how many
        # switches there are; report.txt lists them all
        folders, out = run(exe, "many", game, str(now_log), home, user, windows)
        report = (game_path / folders[0] / "report.txt").read_text(encoding="utf-8")
        link = report[report.index("https://github.com/NotRexed/Hookshot/issues/new?template=1-game-problem.yml"):].split()[0]
        check(len(link) <= 2000, "the link with 98 switches: %d characters" % len(link))
        settings = hw.section(report, "Changes from the defaults")
        listed = [ln for ln in settings.splitlines() if re.match(r"[A-Z_][A-Z0-9_]*=", ln)]
        check("&settings=Upscaler%3A%20NVIDIA%20DLSS" in link and
              "Switches%%20set%%3A%%20%d%%2C%%20listed%%20in%%20the%%20attached%%20report.txt" % len(listed) in link and
              "RX_TEST_SWITCH" not in link and "in short" in report, "the shortened link: " + link)
        check(all("RX_TEST_SWITCH_%d=(value left out)" % i in listed for i in range(80)) and "HOS_HEADLESS=1" in listed and
              "v" * 20 not in report, "report.txt does not list every switch, or writes a value it should leave out")

        # 5. this PC: the system as the form asks it, read by hardware_report.py as this OS
        system = next(ln[7:] for ln in out if ln.startswith("SYSTEM "))
        check(hw.os_of(system) == ("Windows" if windows else "Linux / SteamOS") and " GB, " in system,
              "this PC's system reads " + system)

        # 6. NUL bytes, one in a line and a run of them at the end (what a power cut or a hard kill leaves): the copy is the
        # log byte for byte, its paths redacted, and the map after the NUL is the place. (The file is not named nul.log: on
        # Windows NUL with any extension is the null device, so nothing was written and an empty log was read back; the
        # read-back below says so if a name ever does that again)
        nul = tmp / "power-cut.log"
        nul_text = ("[hos host] hookshot " + VERSION + "\n[hos Main] fs: mounted content: -> " + home + "/Games\n"
                    "[hos Main] map: KokiriForest_Child (child)\n[hos Main] half a line\0 [hos Main] map: LostWoods_Child (child)\n" +
                    "\0" * 4096)
        nul.write_bytes(nul_text.encode("utf-8"))
        check(nul.read_bytes() == nul_text.encode("utf-8"), "the test's own log with NUL bytes was not written as given")
        folders, out = run(exe, "plain", game, str(nul), home, user, windows)
        copy = (game_path / folders[0] / "hookshot_log.txt").read_bytes()
        check(copy == nul_text.replace(home, home_as).encode("utf-8"), "the copy of a log with NUL bytes differs: %r" % copy[-200:])
        report = (game_path / folders[0] / "report.txt").read_text(encoding="utf-8")
        check(hw.section(report, "Where in the game").startswith("LostWoods_Child (child)"), "the map after a NUL is lost")

        # 7. a report's folder that cannot be made (bug-reports is a file here): the system's reason, GetLastError's on Windows
        blocked = tmp / "blocked" / "Hookshot"
        blocked.mkdir(parents=True)
        (blocked / "bug-reports").write_text("a file where the folder would go", encoding="utf-8")
        folders, out = run(exe, "plain", str(blocked) + os.sep, str(now_log), home, user, windows)
        said = next((ln for ln in out if "menu: bug report not written:" in ln), "")
        check(folders == [""] and "menu: bug report not written:" in said, "no folder: %s" % out)

        # Every optional attachment failure still leaves a readable report; pending
        # originals are consumed only after the complete manifest is published.
        pending = game_path / "bug-reports" / "pending"
        pending.mkdir()
        old_id, current_id = "a" * 32, "b" * 32
        previous_log = game_path / "hookshot_log.1.txt"
        old_text = previous_log.read_text(encoding="utf-8")
        old_text = old_text.replace("\n", "\n[hos host] run_id: " + old_id + "\n", 1)
        old_text += "[hos host] CRASH: [x0] 00 01 02 03\n"
        previous_log.write_text(old_text, encoding="utf-8")
        incident_name = "crash-20261112T120000Z.txt"
        record = pending / incident_name
        authored = ("Hookshot crash record 1\nrun_id: " + old_id + "\nversion: v2.0.14\nwhen: 20261112T120000Z\n"
                    "uptime_s: 42\nreason: fatal signal\nthread: alice private-machine\n"
                    "module_offset: hookshot+0x1234\nlast_present_age_ms: unknown\nlog: hookshot_log.txt\n"
                    "unknown_secret: never-share-this\nversion: " + "Q" * 200 + "\n"
                    "reason: /home/alice private-machine 192.168.42.9 fd00::1234\nrecord_complete: yes\n")
        for failed_name in sorted(EXTRAS - {"report.zip", "manifest.json"}) + ["report.zip"]:
            record.write_text(authored, encoding="utf-8")
            folders, out = run(exe, "fail-" + failed_name, game, str(now_log), home, user, windows)
            result = game_path / folders[0]
            check(record.exists(), "partial report consumed original " + failed_name)
            meta = json.loads((result / "manifest.json").read_text(encoding="utf-8"))
            check(any(x["name"] == failed_name and x["status"] == "failed" for x in meta["files"]),
                  "failure not named in manifest: " + failed_name)
            check(failed_name in (result / "report.txt").read_text(encoding="utf-8"), "failure absent from human manifest")
            if failed_name != "report.zip":
                bundle(result, home, user, game)
        record.write_text(authored, encoding="utf-8")
        folders, _ = run(exe, "fail-manifest.json", game, str(now_log), home, user, windows)
        check(record.exists() and "FAILED to publish" in (game_path / folders[0] / "report.txt").read_text(encoding="utf-8"),
              "manifest failure did not preserve original")
        record.write_text(authored, encoding="utf-8")
        folders, _ = run(exe, "fail-drop-record", game, str(now_log), home, user, windows)
        check("record missing" in (game_path / folders[0] / "report.txt").read_text(encoding="utf-8"), "missing record not named")

        record.write_text(authored, encoding="utf-8")
        dump = record.with_suffix(".dmp")
        dump.write_bytes(b"LOCAL-ONLY-DUMP")
        stack = record.with_suffix(".stacks.txt")
        stack.write_text('Hookshot stall stacks 1\nthreads: 1 (limit 512; best effort, not simultaneous)\n'
                         'thread 123 "alice private-machine" (partial stack)\n  0 libc.so.6+0x1234\n'
                         'private: never-share-this\n', encoding="utf-8")
        folders, _ = run(exe, "recover", game, str(now_log), home, user, windows)
        result = game_path / folders[0]
        meta = bundle(result, home, user, game)
        check(not record.exists() and not stack.exists() and dump.exists(), "record move or local dump boundary")
        check((result / incident_name).exists() and "never-share-this" not in (result / incident_name).read_text(encoding="utf-8"),
              "unknown pending field leaked")
        check("lines left out" in (result / incident_name).read_text(encoding="utf-8") and
              "Q" * 200 not in (result / incident_name).read_text(encoding="utf-8"), "over-long field retained")
        check(any(x.get("incident_id") == record.stem for x in meta["files"]), "stable incident ID lost")
        check("hookshot_log.txt" not in files_of(result) and "v2.0.14" in (result / "report.txt").read_text(encoding="utf-8"),
              "recovery substituted new run for old")
        check("picture.png" not in files_of(result), "recovery took a new-run picture")

        check("including bytes of memory near three registers" in (result / "report.txt").read_text(encoding="utf-8"),
              "existing crash memory lines not disclosed")
        check("CRASH: [x0] 00 01 02 03" in (result / "hookshot_log.1.txt").read_text(encoding="utf-8"),
              "original crash memory lines were stripped")
        # Rotation beyond .1 is matched by identity, never by filename or timestamps.
        previous_log.unlink()
        third_log = game_path / "hookshot_log.3.txt"
        third_log.write_text(old_text, encoding="utf-8")
        record.write_text(authored, encoding="utf-8")
        folders, _ = run(exe, "recover", game, str(now_log), home, user, windows)
        result = game_path / folders[0]
        bundle(result, home, user, game)
        check((result / "hookshot_log.3.txt").is_file() and not (result / "hookshot_log.txt").exists(),
              "rotation .3 matched to current run instead")
        third_log.unlink()
        record.write_text(authored, encoding="utf-8")
        folders, _ = run(exe, "recover", game, str(now_log), home, user, windows)
        result = game_path / folders[0]
        bundle(result, home, user, game)
        check("the log of that run has aged out" in (result / "report.txt").read_text(encoding="utf-8") and
              not any(result.glob("hookshot_log*.txt")), "aged-out incident received an unrelated log")
        record.write_text(authored.replace("run_id: " + old_id, "run_id: unknown"), encoding="utf-8")
        folders, _ = run(exe, "recover", game, str(now_log), home, user, windows)
        result = game_path / folders[0]
        check("no run id: the log of that record could not be matched" in (result / "report.txt").read_text(encoding="utf-8"),
              "legacy record guessed a log")
        resumed = pending / "stall-20261112T120001Z-7-1.txt"
        resumed.write_text("Hookshot stall record 1\nreason: no present\nresumed: yes, after 11 s\n", encoding="utf-8")
        before_folders = set((game_path / "bug-reports").iterdir())
        folders, _ = run(exe, "recover-empty", game, str(now_log), home, user, windows)
        check(folders == [""] and set((game_path / "bug-reports").iterdir()) == before_folders and resumed.exists(),
              "resumed stall announced at start")
        folders, _ = run(exe, "plain", game, str(now_log), home, user, windows)
        result = game_path / folders[0]
        bundle(result, home, user, game)
        check((result / resumed.name).exists() and not resumed.exists(), "manual report lost resumed stall")
        unclean = pending / "unclean-20261112T120002Z-7-2.txt"
        termination_note = "termination_note: ended from outside or lost power, not necessarily a crash\n"
        unclean.write_text("Hookshot unclean record 1\nreason: ended without a clean exit\nversion: v2.0.14\n" +
                           termination_note, encoding="utf-8")
        folders, _ = run(exe, "recover", game, str(now_log), home, user, windows)
        result = game_path / folders[0]
        bundle(result, home, user, game)
        check("last run did not end cleanly; not evidence of a crash" in (result / "report.txt").read_text(encoding="utf-8"),
              "unclean end misclassified")
        check(termination_note in (result / unclean.name).read_text(encoding="utf-8"), "unclean termination note lost")

        # Metadata never reads payloads; only an explicit, validated local slot
        # selection can copy them, and neither the ZIP nor its manifest marks them shareable.
        slot = game_path / "saves" / "slot_00"
        slot.mkdir(parents=True)
        payload = b"AUTHORED-SAVE-PAYLOAD"
        (slot / "progress.sav").write_bytes(payload)
        for mode in ("plain", "save", "invalid-save", "missing-save"):
            started = time.monotonic()
            folders, _ = run(exe, mode, game, str(now_log), home, user, windows)
            check(time.monotonic() - started < 6, "ordinary report exceeded a few seconds")
            result = game_path / folders[0]
            meta = bundle(result, home, user, game)
            check(f"slot_00/progress.sav: {len(payload)} bytes" in (result / "save-metadata.txt").read_text(encoding="utf-8"), "save metadata")
            check(re.search(r"last written \d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2} local time",
                            (result / "save-metadata.txt").read_text(encoding="utf-8")), "save modification clock time was lost")
            check("must not be shared" in (result / "report.txt").read_text(encoding="utf-8"), "local-save warning")
            check((result / "save/slot_00/progress.sav").exists() == (mode == "save"), "save opt-in boundary")
            with zipfile.ZipFile(result / "report.zip") as zipped:
                check(all(payload not in zipped.read(n) for n in zipped.namelist()), "save data entered zip")
            if mode == "save":
                check(any(x["name"] == "save/slot_00/progress.sav" and not x["shareable"] for x in meta["files"]), "save marked shareable")
        folders, _ = run(exe, "huge-fields", game, str(now_log), home, user, windows)
        check(len((game_path / folders[0] / "issue-link.txt").read_text(encoding="utf-8").strip()) <= 2000, "long hardware fields escaped link cap")
        folders, _ = run(exe, "process", game, str(now_log), home, user, windows)
        result = game_path / folders[0]
        bundle(result, home, user, game)
        system_snapshot = (result / "system.txt").read_text(encoding="utf-8")
        check("Filesystem:" in system_snapshot or "Filesystem/rotation: unavailable" in system_snapshot, "filesystem silently missing")
        perf = (result / "performance.txt").read_text(encoding="utf-8")
        check("textures 2333 MB (budget 6144 MB)" in perf and "decoded ASTC 1351 MB" in perf,
              "generic VRAM status overwrote detailed texture counters")
    print("bug report: ok (report.txt as the issue form, both logs redacted, the picture, the long log, the terminal start, "
          "switch values, NUL bytes, the folder's reason)")


if __name__ == "__main__":
    main()
