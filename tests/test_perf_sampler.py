"""The sampled monitor's real worker with fake readings: no off worker, flush before stop, exact capture and safe paths."""
import argparse
import csv
import os
from pathlib import Path
import re
import subprocess
import tempfile

RUN_ALL_ARGS = ["--cxx", "{cxx}"]
ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cxx", default="g++")
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix="perf-sampler-") as temporary:
        folder = Path(temporary).resolve()
        executable = folder / "sampler.exe"
        sources = [ROOT / "tests/test_perf_sampler.cpp", ROOT / "gpu/vk/perf_sampler.cpp",
                   ROOT / "gpu/vk/perf_monitor.cpp", ROOT / "gpu/vk/perf_readers.cpp"]
        command = [args.cxx, "-std=c++20", "-Wall", "-Wextra", "-Werror", "-pthread", *map(str, sources),
                   "-o", str(executable)]
        if os.name != "nt":
            command.append("-ldl")
        subprocess.run(command, check=True)
        subprocess.run([str(executable), str(folder)], check=True, timeout=15)
        path = folder / "perf.csv"
        assert path.resolve().parent == folder
        lines = path.read_text(encoding="utf-8").splitlines()
        prefix = "# hookshot_perf,1,"
        assert lines[0].startswith(prefix)
        assert re.fullmatch(r'# started,\d{4}-\d\d-\d\dT\d\d:\d\d:\d\dZ,"test-build,quoted"', lines[1]), lines[1]
        columns = lines[0][len(prefix):].split(",")
        parsed = list(csv.reader(line for line in lines[1:] if not line.startswith("#")))
        assert all(len(row) == len(columns) for row in parsed)
        rows = [dict(zip(columns, row)) for row in parsed]
        samples = [row for row in rows if row["kind"] == "sample"]
        assert len(samples) >= 2
        assert all(row["gpu_temp_c"] == "na" and row["commit_bytes"] == "na" for row in samples)
        assert all(row["cpu_process_pct"] == "175" and row["rss_bytes"] == "123456789" for row in samples)
        assert samples[-1]["thread_name"] == 'thread, "quoted"'
        markers = [row for row in rows if row["kind"] == "marker"]
        assert [row["event"] for row in markers] == ["capture_start", "capture_end"]
        assert [row["frame"] for row in markers] == ["0", "2"]
        summary, = [row for row in rows if row["kind"] == "summary"]
        assert summary["detail"] == "complete" and summary["frames"] == "2"
        assert float(summary["mean_ms"]) == 15
        assert float(summary["low_1_fps"]) == 50
        assert summary["over16_7"] == "1" and summary["over20"] == "0"
        assert (folder / "perf-1.csv").read_text(encoding="utf-8") == "kept\n"
        assert (folder / "perf-2.csv").read_bytes() == b""
    # Keep the opt-in boundary visible: no menu level, no sensor/stdio call in the fixed producer hand-off.
    source = (ROOT / "gpu/vk/vk_perf.cpp").read_text(encoding="utf-8")
    assert source.index("if (!config.enabled()) return") < source.index("vkGetPhysicalDeviceProperties2")
    header = (ROOT / "gpu/vk/perf_sampler.h").read_text(encoding="utf-8")
    frame = header[header.index("    void frame("):]
    assert "queue->push" in frame and not any(word in frame for word in ("fopen", "fflush", "read(", "sleep"))
    print("PASS: sampler CSV metadata, capture boundaries, authored statistics, unavailable cells and durable rows")


if __name__ == "__main__":
    main()
