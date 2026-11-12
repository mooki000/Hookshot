"""Upscaler changes borrow a supported size without overwriting the saved render scale.

Compile the real menu rows, set_row, menu_setting_set, preset, save and options-file
reader with hardware/UI stand-ins. Exercise every saved scale through every engine,
including a restart while the game's own upscaler is selected. No game or GPU.
"""
import argparse
import re
import subprocess
import tempfile
from pathlib import Path

RUN_ALL_ARGS = ["--cxx", "{cxx}"]
ROOT = Path(__file__).resolve().parents[1]


def block(text, start):
    opening = text.index("{", start)
    depth = 0
    for at in range(opening, len(text)):
        depth += text[at] == "{"
        depth -= text[at] == "}"
        if depth == 0:
            return text[start:at + 1]
    raise SystemExit("unterminated block")


def definition(text, name):
    match = re.search(re.escape(name) + r"\([^;{]*\)\s*\{", text)
    if not match:
        raise SystemExit("missing definition: " + name)
    return block(text, match.start())


FIXTURE = r'''
#define _CRT_SECURE_NO_WARNINGS
#include "upscaler_rows.h"
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iterator>
#include <mutex>
#include <string>
#include <utility>
using namespace mkwvk;
namespace ur = mkwvk::upscaler_rows;
int failures = 0, checks = 0;
#define CHECK(x) do { ++checks; if (!(x)) { ++failures; std::fprintf(stderr, "FAIL %d: %s\n", __LINE__, #x); } } while (0)
/* OPTIONS */
Options opts;
/* TABS */
/* ROW */
unsigned available = (1u << ur::kUpscalers) - 1;
int upscaler() { return available & ur::bit(opts.dlss) ? opts.dlss : (int)ur::GAME; }
unsigned upscalers_available() { return available; }
bool row_fixed(const int*) { return false; }
struct Out { uint32_t w, h; };
Out shown_output() { return {1920, 1080}; }
Out output_at(int) { return shown_output(); }
int custom_index() { return 0; }
int output_choice(int v) { return v; }
int parse_output(const char*) { return 0; }
int step_output_choice(int v, int dir) { return v + dir; }
int output_for_aspect(int) { return opts.output_res; }
int aspect_for_choice(int) { return opts.ultrawide; }
int ultrawide_read = 0, aspect_started = 0;
bool aspect_follows_host(int) { return false; }
Out size_of(Out o) { return o; }
namespace dm { int ratio_name(Out) { return 0; } }
/* SCALE */
/* TEXT */
const char* tx_upscaler(int) { return ""; }
const char* hn_upscaler(int, int) { return ""; }
const char* tx_dlss_model(int) { return ""; }
const char* hn_dlss_model(int, int) { return ""; }
const char* tx_preset(int) { return ""; }
const char* hn_preset(int, int) { return ""; }
Row rows[] = {/* UPSCALER_ROW */, /* RESOLUTION_ROW */, /* MODEL_ROW */, /* PRESET_ROW */};
constexpr int kRows = sizeof rows / sizeof rows[0];
/* OVERRIDES */
/* INI_KEY */
std::string ini_file, mic_want;
std::string ini_path() { return ini_file; }
std::string mic_key(const char* v) { return v; }
void mic_tell_host() {}
void refresh_mics() {}
int rumble_step(int v) { return v; }
constexpr int HOS_KEY_N = 1, HOS_SW_N = 1, HOS_BIND_PAD = 0, HOS_BIND_KEY = 1;
constexpr int HOS_KEY_OC_NOTE_L = 1, HOS_KEY_OC_NOTE_X = 1;
const char* bind_names[1] = {"a"};
struct Host { int (*binding)(int, int, int); float (*aspect_ratio)(); } *host = nullptr;
bool can_bind() { return false; }
/* SAVE */
void restart() {
    opts = Options{};
    env_override_count = 0;
    bool has_options = false, has_vram_budget = false, has_memory_budget = false;
    /* LOAD */
    CHECK(has_options && has_memory_budget && !has_vram_budget);
}
void apply_pad_layout(int) {}
void sync_preset_budgets(int) {}
void vram_budget_sync() {}
constexpr int kVramMinimumOnly = 1;
namespace mr { constexpr int kMax = 8; struct State { void note(const int*) {} }; }
mr::State restart_state;
void restart_values(int*) {}
bool can_mic() { return false; }
void mic_chosen() {}
void samplers_changed() {}
void temporal_reset_failure() {}
void prompts_at_once(const Row&) {}
/* STOPS */
/* PRESET */
/* SET_ROW */
/* RESOLUTION_SCALE */
/* WALK */
std::recursive_mutex menu_mu;
bool menu_refresh = false;
void bridge_probe_first() {}
bool bridge_row(int) { return true; }
struct MkwVkSetting { int value; };
void bridge_fill(int i, MkwVkSetting* out) { out->value = *rows[i].value; }
/* BRIDGE */
int active_scale() { return ur::full_scale(upscaler()) ? 10000 : resolution_scale(); }
void arrow_to(int engine) {
    for (int n = 0; opts.dlss != engine && n < ur::kUpscalers; ++n) set_row(rows[0], 0, 1);
    CHECK(opts.dlss == engine);
}
void bridge_to(int engine) {
    MkwVkSetting out{};
    CHECK(menu_setting_set("dlss", engine, &out) == 1 && out.value == engine);
}
void fresh(int engine, int value) {
    opts = Options{};
    env_override_count = 0;
    opts.dlss = engine;
    opts.resolution = value;
}
int main(int argc, char** argv) {
    if (argc != 2) return 2;
    ini_file = argv[1];
    const int engines[] = {ur::GAME, ur::DLSS, ur::FSR3, ur::FSR4, ur::OFF};
    // Every saved value, including all named qualities and hand-chosen slider stops.
    for (int engine : engines) for (int value = 0; value < ur::kValues; ++value) {
        fresh(engine, value);
        const int before = active_scale();
        const std::string text = tx_resolution(value);
        const int away = engine == ur::GAME ? (int)ur::DLSS : (int)ur::GAME;
        arrow_to(away);
        const int via = active_scale();
        arrow_to(engine);
        const int after = active_scale();
        const int stored = opts.resolution;
        CHECK(stored == value && after == before && text == tx_resolution(opts.resolution));
        fresh(engine, value);
        arrow_to(away);
        save();
        restart();
        CHECK(opts.dlss == away && opts.resolution == value);
        arrow_to(engine);
        const int restarted = active_scale();
        CHECK(opts.resolution == value && restarted == before);
        fresh(engine, value);
        bridge_to(away);
        bridge_to(engine);
        const int bridged = active_scale();
        CHECK(opts.resolution == value && bridged == before);
        std::printf("ROUNDTRIP engine=%d value=%d percent=%d before=%d via=%d after=%d stored=%d restart=%d bridge=%d\n",
                    engine, value, ur::percent(value), before, via, after, stored, restarted, bridged);
        // Other intermediates and either arrow direction must also leave the choice alone.
        for (int other : engines) {
            fresh(engine, value);
            bridge_to(other);
            bridge_to(engine);
            CHECK(opts.resolution == value && active_scale() == before);
            for (int dir : {-1, 1}) {
                fresh(engine, value);
                set_row(rows[0], 0, dir);
                set_row(rows[0], 0, -dir);
                CHECK(opts.dlss == engine && opts.resolution == value && active_scale() == before);
            }
        }
    }
    // Fresh preset selections retain their current scales. Custom does not reapply a preset.
    const int preset_values[] = {5, 4, 2, 1, 1, 1};
    for (int engine : engines) for (int preset = 0; preset <= 5; ++preset) {
        fresh(engine, 5);
        set_row(rows[3], preset, 1);
        const int expected = preset ? ur::snap_render(engine, preset_values[preset]) : 5;
        CHECK(opts.resolution == expected && opts.dlss == engine);
        bridge_to(engine == ur::GAME ? (int)ur::DLSS : (int)ur::GAME);
        bridge_to(engine);
        CHECK(opts.resolution == expected);
    }
    // A change to the visible snapped size is deliberate; it replaces the old preference.
    fresh(ur::DLSS, 5);
    bridge_to(ur::GAME);
    CHECK(active_scale() == 5000);
    set_row(rows[1], 0, -1);
    CHECK(opts.resolution == 3);  // Left from the shown 50% is 67%, not the hidden 58%.
    bridge_to(ur::DLSS);
    CHECK(opts.resolution == 3 && active_scale() == 6667);
    // Selecting each named quality through the bridge still reaches its exact slider stop.
    for (int engine : {(int)ur::DLSS, (int)ur::FSR3, (int)ur::FSR4}) {
        for (int value : ur::kSlider) {
            fresh(engine, 0);
            MkwVkSetting out{};
            CHECK(menu_setting_set("resolution", value, &out) == 1 && out.value == value);
            CHECK(opts.dlss == engine && opts.resolution == value);
        }
    }
    // Model presets are not quality scales, and do not overwrite one.
    for (int model = 0; model < ur::kModels; ++model) {
        fresh(ur::DLSS, 5);
        set_row(rows[2], model, 1);
        bridge_to(ur::GAME);
        bridge_to(ur::DLSS);
        CHECK(opts.dlss_model == model && opts.resolution == 5);
    }
    std::printf("upscaler round trip: %d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
'''


def fixture(source):
    menu = (source / "gpu/vk/vk_menu.cpp").read_text(encoding="utf-8")
    header = (source / "gpu/vk/vk_menu.h").read_text(encoding="utf-8")
    init = definition(menu, "void menu_init")
    load_at = init.index('    if (FILE* f = fopen(ini_path().c_str(), "r")) {')
    walk_at = menu.index("template <class State, class At, class Step>")
    replacements = {
        "OPTIONS": block(header, header.index("struct Options {")) + ";",
        "TABS": block(menu, menu.index("enum MenuTab {")) + ";",
        "ROW": block(menu, menu.index("struct Row {")) + ";",
        "OVERRIDES": menu[menu.index("struct EnvOverride {"):menu.index("std::string ini_path() {")],
        "LOAD": init[load_at:init.index("    first_start = !has_options;", load_at)],
        "SCALE": definition(menu, "int twelfths") + "\n" + definition(menu, "void game_size"),
        "TEXT": definition(menu, "const char* tx_resolution") + "\n" + definition(menu, "const char* hn_resolution"),
        "WALK": block(menu, walk_at),
    }
    for marker, name in (("INI_KEY", "const char* ini_key"), ("SAVE", "void save"),
                         ("STOPS", "bool stops_at_ends"), ("PRESET", "void apply_preset"),
                         ("SET_ROW", "void set_row"), ("BRIDGE", "int menu_setting_set"),
                         ("RESOLUTION_SCALE", "int resolution_scale")):
        replacements[marker] = definition(menu, name)
    for marker, label in (("UPSCALER_ROW", "Upscaler"), ("RESOLUTION_ROW", "Render resolution"),
                          ("MODEL_ROW", "DLSS model"), ("PRESET_ROW", "Quality preset")):
        replacements[marker] = block(menu, menu.index('{TAB_GRAPHICS, "' + label + '"'))
    result = FIXTURE
    for marker, value in replacements.items():
        result = result.replace("/* " + marker + " */", value)
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, default=ROOT)
    parser.add_argument("--cxx", default="g++")
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix="upscaler-round-trip-") as tmp:
        src, exe = Path(tmp) / "fixture.cpp", Path(tmp) / "fixture.exe"
        src.write_text(fixture(args.source), encoding="utf-8")
        include = str((args.source / "gpu/vk").resolve())
        if Path(args.cxx).name.lower() in ("cl", "cl.exe", "clang-cl", "clang-cl.exe"):
            command = [args.cxx, "/nologo", "/std:c++20", "/EHsc", "/I" + include,
                       str(src), "/Fo" + str(Path(tmp) / "fixture.obj"), "/Fe" + str(exe)]
        else:
            command = [args.cxx, "-std=c++20", "-Wall", "-Wextra", "-Werror", "-I", include,
                       str(src), "-o", str(exe)]
        subprocess.run(command, cwd=tmp, check=True)
        raise SystemExit(subprocess.run([str(exe), str(Path(tmp) / "hookshot_options.ini")], cwd=tmp).returncode)


if __name__ == "__main__":
    main()
