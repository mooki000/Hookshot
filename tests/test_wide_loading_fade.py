"""Compile the real wide_sides policy and HUD projection patch around authored state.

Like test_widescreen_shapes.py, this needs neither the game nor a Vulkan device.
All three menu modes cover 16:9 and 21:9: the pause background may be centred,
but the wheel background, event fade and loading fade must keep the whole picture.
--source accepts another checkout for the same test against the pre-fix sources.
"""
import argparse
import importlib.util
from pathlib import Path
import subprocess
import tempfile

RUN_ALL_ARGS = ["--cxx", "{cxx}"]
ROOT = Path(__file__).resolve().parents[1]


def block(source, marker):
    if source.count(marker) != 1:
        raise ValueError("Expected exactly one " + marker)
    start = source.index(marker)
    end = source.index("{", start) + 1
    depth = 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end]


FIXTURE = r'''
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>

static struct { int wide_menus; } opts;
static float margin;
static uint32_t asked_mask;
volatile uint32_t hos_wide_keep_mask;
volatile float hos_wide_squeeze;
static float wide_menus(uint32_t mask) {
    asked_mask = mask;
    hos_wide_keep_mask = mask;
    return margin;
}
static struct Host { float (*wide_menus)(uint32_t); } callbacks{wide_menus}, *host = &callbacks;
std::atomic<int64_t> world_drawn_at{0}, wheel_drawn_at{0};
int64_t presents_now = 100;
static void logf(const char *, ...) {}
// WIDE_POLICY

struct Context { uint64_t x[1]; float vreg[5][4]; };
static uint64_t canvas_vtable;
static uint64_t recomp_load64(Context *, uint64_t) { return canvas_vtable; }
static void hud(Context *c) {
// HUD_PATCH
}

int main() {
    struct Canvas { const char *name; uint64_t vtable; bool centred; };
    const Canvas canvases[] = {
        {"wheel background", 0x84c0d098ULL, false},
        {"pause background", 0x84c04fb0ULL, true},
        {"event fade", 0x84c061c8ULL, false},
        {"loading fade", 0x84c07b30ULL, false},
        {"ordinary HUD/menu canvas", 0, true},
    };
    int failures = 0;
    for (int wide = 0; wide < 2; ++wide) {
        hos_wide_squeeze = wide ? 0.7442f : 0.0f;
        margin = wide ? (1.0f - hos_wide_squeeze) / 2.0f : 0.0f;
        for (int mode = 0; mode < 3; ++mode) {
            opts.wide_menus = mode;
            world_drawn_at = wheel_drawn_at = 0;
            const WideSides sides = wide_sides();
            const uint32_t expected_mask = mode ? 0xdu : 0xfu;
            if (asked_mask != expected_mask) {
                std::printf("FAIL: %s mode %d keep mask %#x, expected %#x\n",
                            wide ? "21:9" : "16:9", mode, asked_mask, expected_mask);
                ++failures;
            }
            const bool has_sides = wide && mode;
            if (sides.side != (has_sides ? mode : 0) || sides.x0 != (has_sides ? margin : 0.0f) ||
                wide_possible.load() != has_sides) {
                std::printf("FAIL: %s mode %d menu sides changed\n", wide ? "21:9" : "16:9", mode);
                ++failures;
            }
            for (const Canvas &canvas : canvases) {
                Context c{};
                c.x[0] = 0x200;
                const float row[4] = {2.0f / 3840.0f, 0.25f, -0.5f, 1.0f};
                std::memcpy(c.vreg[1], row, sizeof row);
                c.vreg[4][1] = 2.0f / 2160.0f;
                canvas_vtable = canvas.vtable;
                hud(&c);
                const bool squeeze = wide && canvas.centred && (mode || canvas.vtable == 0);
                for (int k = 0; k < 4; ++k) {
                    const float expected = row[k] * (squeeze ? hos_wide_squeeze : 1.0f);
                    if (std::fabs(c.vreg[1][k] - expected) > 0.0000001f) {
                        std::printf("FAIL: %s mode %d %s projection component %d\n",
                                    wide ? "21:9" : "16:9", mode, canvas.name, k);
                        ++failures;
                    }
                }
            }
            // The existing grace period and wheel exclusion do not change.
            world_drawn_at = presents_now - 3;
            if (wide_sides().side != 0) ++failures;
            world_drawn_at = 0;
            wheel_drawn_at = presents_now - 3;
            if (wide_sides().side != 0) ++failures;
        }
    }
    host = nullptr;
    if (wide_sides().side != 0) ++failures;
    if (failures) return 1;
    std::puts("PASS: loading/event fades and wheel stay full width; pause/HUD centring, sides and 16:9 unchanged");
}
'''


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cxx", default="g++")
    parser.add_argument("--source", type=Path, default=ROOT)
    args = parser.parse_args()
    source = (args.source / "gpu/vk/vk_framegen.cpp").read_text(encoding="utf-8")
    policy = (block(source, "struct WideSides") + ";\nstd::atomic<bool> wide_possible{false};\n" +
              block(source, "WideSides wide_sides("))
    spec = importlib.util.spec_from_file_location("oot_wide", args.source / "nx2/oot_wide.py")
    wide = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(wide)
    fixture = FIXTURE.replace("// WIDE_POLICY", policy).replace("// HUD_PATCH", wide.HUD)
    with tempfile.TemporaryDirectory(prefix="wide-loading-fade-") as tmp:
        cpp, exe = Path(tmp) / "fixture.cpp", Path(tmp) / "fixture.exe"
        cpp.write_text(fixture, encoding="utf-8")
        subprocess.run([args.cxx, "-std=c++20", "-O2", "-Wall", "-Wextra", "-Werror",
                        str(cpp), "-o", str(exe)], check=True)
        subprocess.run([str(exe)], check=True)


if __name__ == "__main__":
    main()
