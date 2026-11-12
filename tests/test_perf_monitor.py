"""Pure performance-monitor values, configuration, CSV and authored statistics; no game or sensors.

The authored N=101 intervals expose ceil(1%) versus the workbench's floor(1%):
nightly/metrics.py uses ceil lows; regress/run_case.py and hs-run.sh use floor.
The first 100 of those same authored intervals also expose nearest-rank p99
(99 ms) versus regress's sorted[int(N * .99)] (100 ms), and nearest-rank p50
(50 ms) versus workbench median (50.5 ms). N=101 p99 happens to agree; it is
not a universal difference. N=1001 additionally exercises ceil(0.1%).
These are definitions reviewed in the Hookshot workbench on 2026-11-12, not
measurements or imports of a machine-local workbench installation.

python -I -B tests/test_perf_monitor.py [--cxx g++]
"""
import argparse
import csv
import io
import math
import os
import re
import statistics
import subprocess
import tempfile
from pathlib import Path

RUN_ALL_ARGS = ["--cxx", "{cxx}"]
ROOT = Path(__file__).resolve().parents[1]
COMMON = "kind,sample,monotonic_ns,elapsed_s,game_frame,renderer_present,frame,time_s,clock,thread_name,event,detail".split(",")
METRICS = (
    "mean_ms,mean_fps,low_1_fps,low_0_1_fps,p50_ms,p95_ms,p99_ms,max_ms,over16_7,over20,over25,over50,over100,frames,"
    "gpu_use_pct,gpu_temp_c,gpu_power_w,gpu_fan_pct,gpu_clock_mhz,gpu_throttle_mask,gpu_memory_used_bytes,"
    "vram_used_bytes,vram_budget_bytes,texture_used_bytes,texture_budget_bytes,gpu_ms,cpu_process_pct,cpu_thread_pct,"
    "rss_bytes,commit_bytes,ram_available_bytes,swap_used_bytes,disk_read_bytes,cpu_temp_c,cpu_clock_mhz,battery_pct,"
    "ac_online,pipelines,left_out,upload_bytes,gpu_wait_ms"
).split(",")


def need(condition, message):
    if not condition:
        raise SystemExit("FAIL: " + message)


def capture(text):
    reader = csv.reader(io.StringIO(text, newline=""), strict=True)
    header = next(reader)
    need(header == ["# hookshot_perf", "1"] + COMMON + METRICS, "exact frozen schema and metric order")
    comments, rows = [], []
    for row in reader:
        if row and row[0].startswith("#"):
            comments.append(row)
            continue
        need(len(row) == len(COMMON) + len(METRICS), "every data row has the identical column count")
        rows.append(dict(zip(COMMON + METRICS, row)))
    return comments, rows


def monitor_statistics(values):
    ordered = sorted(values)
    size = len(values)
    mean = math.fsum(values) / size
    result = {"mean_ms": mean, "mean_fps": 1000 / mean, "max_ms": max(values), "frames": size}
    for fraction, name in ((0.01, "low_1_fps"), (0.001, "low_0_1_fps")):
        slow = ordered[-max(1, math.ceil(size * fraction)):]
        result[name] = 1000 / (math.fsum(slow) / len(slow))
    for fraction, name in ((0.50, "p50_ms"), (0.95, "p95_ms"), (0.99, "p99_ms")):
        result[name] = ordered[math.ceil(size * fraction) - 1]
    for threshold, name in ((16.7, "over16_7"), (20, "over20"), (25, "over25"), (50, "over50"), (100, "over100")):
        result[name] = sum(value > threshold for value in values)
    return result


def workbench_statistics(values):
    # regress/run_case.py: floor low count, statistical median, zero-based int(N*.99).
    ordered = sorted(values)
    slow = sorted(values, reverse=True)[:max(1, len(values) // 100)]
    regress = {
        "mean_ms": statistics.fmean(values),
        "mean_fps": 1000 / statistics.fmean(values),
        "low_1_fps": 1000 / statistics.fmean(slow),
        "median_ms": statistics.median(values),
        "p99_ms": ordered[min(len(values) - 1, int(len(values) * 0.99))],
    }
    # hs-run.sh: the same selected intervals and floor low, expressed as N / sum(ms).
    wrapper = {"mean_fps": 1000 * len(values) / sum(values), "low_1_fps": 1000 * len(slow) / sum(slow)}
    # nightly/metrics.py: ceil for both lows, but statistical median rather than nearest-rank p50.
    nightly = {"mean_fps": 1000 / statistics.fmean(values), "median_ms": statistics.median(values)}
    for percent, name in ((1, "low_1_fps"), (0.1, "low_0_1_fps")):
        tail = sorted(values, reverse=True)[:max(1, math.ceil(len(values) * percent / 100))]
        nightly[name] = 1000 / statistics.fmean(tail)
    return regress, wrapper, nightly


def check_csv(path):
    text = path.read_bytes().decode("utf-8")
    comments, rows = capture(text)
    need(comments == [["# started", "2026-11-12T00:00:00Z", 'v1.2.3,"fixture"']], "quoted UTC/version metadata comment")
    need(len(rows) == 11, "all authored sample, marker and summary rows retained")
    need([row["sample"] for row in rows] == [str(i) for i in range(1, 12)], "sample integer sequence")
    # Unknown post-header comments are ignorable, without treating a quoted multiline cell as separate records.
    header_end = text.index("\n") + 1
    extra = text[:header_end] + "# future,ignored,metadata\n" + text[header_end:]
    extra_comments, extra_rows = capture(extra)
    need(extra_rows == rows and len(extra_comments) == 2, "forward-compatible comment handling")
    first = rows[0]
    need([first[name] for name in COMMON[:9]] == ["sample", "1", "1234567890", "1.25", "100", "200", "300", "0.5", "headless-renderer"],
         "independent clocks and frame populations retain exact authored values")
    need(first["thread_name"] == 'worker,"quoted"\r\né' and first["detail"] == 'path, "scene"', "UTF-8, comma, quote, CR/LF escaping")
    need(first["gpu_memory_used_bytes"] == "1048576" and first["rss_bytes"] == "1073741824", "CSV bytes are never display-scaled")
    need(first["gpu_throttle_mask"] == "3" and first["ac_online"] == "1", "integer mask and boolean CSV values")
    need(first["cpu_process_pct"] == "250", "process percentage above one core is legitimate")
    for metric in METRICS:
        need(math.isfinite(float(first[metric])), "every authored metric has a numeric CSV cell: " + metric)
    events = ["capture_start", "loading_start", "loading_end", "map_change", "capture_end"]
    for row, event in zip(rows[1:6], events):
        need(row["kind"] == "marker" and row["event"] == event and row["detail"] == 'scene, "fixture"', "exact marker record")
        need(row["clock"] == "swapchain", "marker clock is preserved")
        need(all(row[metric] == "na" for metric in METRICS), "markers do not manufacture telemetry or zero statistics")
        need(all(row[name] == "na" for name in ("game_frame", "renderer_present", "frame", "time_s")), "missing marker frame identities")
    authored = {
        101: [float(i) for i in range(1, 101)] + [1000.0],
        100: [float(i) for i in range(1, 101)],
        1001: [float(i) for i in range(1, 1001)] + [10000.0],
    }
    measured = {}
    for row in rows[6:9]:
        need(row["elapsed_s"] == "0.10000000000000001" and float(row["elapsed_s"]) == 0.1, "locale-independent max_digits10 precision")
        population = int(row["detail"].removeprefix("authored N="))
        values = authored[population]
        reference = monitor_statistics(values)
        measured[population] = {name: float(row[name]) for name in reference}
        for name, expected in reference.items():
            need(math.isclose(measured[population][name], expected, rel_tol=1e-12, abs_tol=1e-12),
                 "authored statistics N=%d %s" % (population, name))
            if name.startswith("over") or name == "frames":
                need(row[name] == str(expected), "counts are exact integers, not scientific or rounded")
        need(all(row[name] == "na" for name in METRICS[14:]), "frame statistics do not invent hardware readings")
        regress, wrapper, nightly = workbench_statistics(values)
        need(math.isclose(measured[population]["mean_fps"], regress["mean_fps"], rel_tol=1e-12), "same-population regress mean")
        need(math.isclose(measured[population]["mean_fps"], wrapper["mean_fps"], rel_tol=1e-12), "same-population wrapper mean")
        for name in ("low_1_fps", "low_0_1_fps"):
            need(math.isclose(measured[population][name], nightly[name], rel_tol=1e-12), "ceil lows agree with nightly")
    regress, wrapper, _ = workbench_statistics(authored[101])
    need(math.isclose(measured[101]["low_1_fps"], 1000 / 550, rel_tol=1e-12), "101 intervals choose two slowest for 1%")
    need(regress["low_1_fps"] == wrapper["low_1_fps"] == 1.0 < measured[101]["low_1_fps"], "documented ceil-versus-floor low difference")
    need(measured[101]["p99_ms"] == regress["p99_ms"] == 100, "N=101 percentile definitions happen to agree")
    regress, _, nightly = workbench_statistics(authored[100])
    need(measured[100]["p99_ms"] == 99 and regress["p99_ms"] == 100, "documented nearest-rank versus zero-based percentile difference")
    need(measured[100]["p50_ms"] == 50 and regress["median_ms"] == nightly["median_ms"] == 50.5, "documented nearest-rank versus median difference")
    need(math.isclose(measured[1001]["low_0_1_fps"], 1000 / 5500, rel_tol=1e-12) and
         measured[1001]["low_0_1_fps"] > 1000 / max(authored[1001]), "1001 intervals choose two slowest for 0.1%")
    invalid = rows[9]
    need(invalid["detail"] == "invalid values" and invalid["cpu_process_pct"] == "250", "valid process reading survives unrelated invalid readings")
    need(all(invalid[name] == "na" for name in METRICS if name != "cpu_process_pct"), "invalid and disabled metrics use na")
    need(all(invalid[name] == "na" for name in ("monotonic_ns", "elapsed_s", "game_frame", "renderer_present", "frame", "time_s")),
         "negative sentinels and nonfinite common numeric fields use na")
    summary = rows[-1]
    need(summary["kind"] == "summary" and summary["event"] == "end" and summary["detail"] == "complete", "explicit graceful summary")
    need(summary["frames"] == "1" and summary["mean_ms"] == "25" and summary["mean_fps"] == "40", "exact authored summary statistics")
    # A truncated capture has no fabricated end record. The reader retains already flushed rows.
    truncated = text[:text.rfind("summary,")]
    _, partial = capture(truncated)
    need(len(partial) == 10 and all(row["kind"] != "summary" for row in partial), "missing final summary is not a complete capture")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cxx", default="g++")
    args = parser.parse_args()
    source = (ROOT / "gpu" / "vk" / "perf_monitor.cpp").read_text(encoding="utf-8")
    need(not re.search(r'^\s*#\s*include\s*[<"][^>"\n]*(?:vulkan|vk_internal)', source, re.M | re.I), "pure core has no Vulkan includes")
    need(not re.search(r'\b(?:setlocale|uselocale|_configthreadlocale)\s*\(', source), "production serialization never changes locales")
    with tempfile.TemporaryDirectory(prefix="perf-monitor-") as tmp:
        exe = Path(tmp) / ("perf-monitor.exe" if os.name == "nt" else "perf-monitor")
        # List complete translation units explicitly: Windows-target fixture capture must see both, not an extracted fragment.
        subprocess.run([args.cxx, "-std=c++20", "-Wall", "-Wextra", "-Werror",
                        str(ROOT / "tests" / "test_perf_monitor.cpp"),
                        str(ROOT / "gpu" / "vk" / "perf_monitor.cpp"), "-o", str(exe)], check=True)
        output = Path(tmp) / "authored.csv"
        subprocess.run([str(exe), str(output)], check=True)
        check_csv(output)
    print("PASS: frozen perf CSV, authored monitor/workbench statistic definitions and portable core fixture")


if __name__ == "__main__":
    main()
