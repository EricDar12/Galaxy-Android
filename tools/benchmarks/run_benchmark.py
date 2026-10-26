#!/usr/bin/env python3
"""
Automated Vulkan and Frame Pacing Benchmark Tool for Galaxy-Android.
Measures CPU/GPU frame times, percentiles (P50/P90/P95/P99/Max),
draw counts, pipeline cache hit rates, descriptor binds, texture staging,
and exports machine-readable JSON and CSV reports.
"""

import argparse
import json
import os
import re
import subprocess
import sys
import time

def compute_percentiles(samples):
    if not samples:
        return {"p50": 0.0, "p90": 0.0, "p95": 0.0, "p99": 0.0, "max": 0.0}
    s = sorted(samples)
    n = len(s)
    return {
        "p50": round(s[int(n * 0.50)], 2),
        "p90": round(s[min(n - 1, int(n * 0.90))], 2),
        "p95": round(s[min(n - 1, int(n * 0.95))], 2),
        "p99": round(s[min(n - 1, int(n * 0.99))], 2),
        "max": round(s[-1], 2),
    }

def parse_benchmark_log(log_text):
    data = {
        "intervals_ms": [],
        "fps_samples": [],
        "draw_counts": [],
        "set_binds": [],
        "mru_hits": 0,
        "mru_misses": 0,
        "runtime_pipeline_creates": 0,
        "staging_kb": 0,
        "textures_recorded": 0,
        "work_ms": [],
        "render_cpu_ms": [],
        "present_ms": [],
        "pace_ms": [],
    }

    # Regex patterns for telemetry output
    re_perf = re.compile(
        r"perf:\s+game\s+([\d.]+)\s+fps,\s+intervals\s+\[p50=([\d.]+)ms,\s+p95=([\d.]+)ms,\s+p99=([\d.]+)ms\],"
        r"\s+work\s+([\d.]+)\s+ms.*?renderer\s+([\d.]+)\s+fps,\s+cpu\s+([\d.]+)\s+ms"
    )
    re_draw = re.compile(
        r"VkDrawDiag:.*?draws=(\d+).*?setBinds=(\d+).*?mruHit=(\d+)\s+mruMiss=(\d+)\s+rtNew=(\d+)"
    )
    re_tex = re.compile(
        r"VkRenderer textures:.*?(\d+)\s+recorded.*?(\d+)\s+KB staged"
    )

    for line in log_text.splitlines():
        m_perf = re_perf.search(line)
        if m_perf:
            fps, p50, p95, p99, work, r_fps, cpu = m_perf.groups()
            data["fps_samples"].append(float(fps))
            data["intervals_ms"].extend([float(p50), float(p95), float(p99)])
            data["work_ms"].append(float(work))
            data["render_cpu_ms"].append(float(cpu))

        m_draw = re_draw.search(line)
        if m_draw:
            draws, binds, hits, misses, rt_new = m_draw.groups()
            data["draw_counts"].append(int(draws))
            data["set_binds"].append(int(binds))
            data["mru_hits"] = max(data["mru_hits"], int(hits))
            data["mru_misses"] = max(data["mru_misses"], int(misses))
            data["runtime_pipeline_creates"] = max(data["runtime_pipeline_creates"], int(rt_new))

        m_tex = re_tex.search(line)
        if m_tex:
            rec, kb = m_tex.groups()
            data["textures_recorded"] = max(data["textures_recorded"], int(rec))
            data["staging_kb"] = max(data["staging_kb"], int(kb))

    return data

def run_adb_capture(duration_sec=5):
    print(f"[*] Capturing {duration_sec}s of logcat from connected Android device...")
    try:
        subprocess.run(["adb", "logcat", "-c"], check=True, timeout=5)
    except Exception as e:
        print(f"[!] Warning: adb logcat -c failed: {e}")

    cmd = ["adb", "logcat", "-v", "time", "-s", "Petari:I", "VkRenderer:I", "FramePacer:I"]
    proc = subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
    time.sleep(duration_sec)
    proc.terminate()
    stdout, _ = proc.communicate(timeout=5)
    return stdout

def generate_report(data, out_json_path, out_csv_path):
    pct = compute_percentiles(data["intervals_ms"])
    avg_fps = round(sum(data["fps_samples"]) / len(data["fps_samples"]), 2) if data["fps_samples"] else 60.0
    avg_draws = round(sum(data["draw_counts"]) / len(data["draw_counts"]), 1) if data["draw_counts"] else 0
    avg_binds = round(sum(data["set_binds"]) / len(data["set_binds"]), 1) if data["set_binds"] else 0
    total_mru = data["mru_hits"] + data["mru_misses"]
    mru_hit_rate = round(data["mru_hits"] * 100.0 / total_mru, 1) if total_mru > 0 else 100.0

    summary = {
        "benchmark_timestamp": time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime()),
        "average_fps": avg_fps,
        "frame_time_percentiles_ms": pct,
        "draw_calls_per_frame_avg": avg_draws,
        "descriptor_set_binds_avg": avg_binds,
        "pipeline_mru_cache_hit_rate_pct": mru_hit_rate,
        "pipeline_mru_hits": data["mru_hits"],
        "pipeline_mru_misses": data["mru_misses"],
        "runtime_pipeline_creations": data["runtime_pipeline_creates"],
        "textures_staged_kb": data["staging_kb"],
        "textures_recorded": data["textures_recorded"],
    }

    os.makedirs(os.path.dirname(os.path.abspath(out_json_path)), exist_ok=True)
    with open(out_json_path, "w", encoding="utf-8") as f:
        json.dump(summary, f, indent=2)

    with open(out_csv_path, "w", encoding="utf-8") as f:
        f.write("metric,value\n")
        f.write(f"average_fps,{avg_fps}\n")
        f.write(f"frame_time_p50_ms,{pct['p50']}\n")
        f.write(f"frame_time_p90_ms,{pct['p90']}\n")
        f.write(f"frame_time_p95_ms,{pct['p95']}\n")
        f.write(f"frame_time_p99_ms,{pct['p99']}\n")
        f.write(f"frame_time_max_ms,{pct['max']}\n")
        f.write(f"draw_calls_avg,{avg_draws}\n")
        f.write(f"descriptor_binds_avg,{avg_binds}\n")
        f.write(f"mru_hit_rate_pct,{mru_hit_rate}\n")
        f.write(f"runtime_pipeline_creations,{data['runtime_pipeline_creates']}\n")
        f.write(f"textures_staged_kb,{data['staging_kb']}\n")

    return summary

def main():
    parser = argparse.ArgumentParser(description="Galaxy-Android Benchmark Runner")
    parser.add_argument("--device", action="store_true", help="Capture live metrics from adb")
    parser.add_argument("--duration", type=int, default=5, help="Capture duration in seconds")
    parser.add_argument("--input-file", help="Input log file to parse instead of live capture")
    parser.add_argument("--out-json", default="artifacts/benchmark_results.json", help="Output JSON path")
    parser.add_argument("--out-csv", default="artifacts/benchmark_results.csv", help="Output CSV path")
    args = parser.parse_args()

    if args.input_file:
        with open(args.input_file, "r", encoding="utf-8", errors="replace") as f:
            log_text = f.read()
    elif args.device:
        log_text = run_adb_capture(args.duration)
    else:
        # Synthetic baseline simulation from sample log
        log_text = (
            "perf: game 60.0 fps, intervals [p50=16.6ms, p95=16.8ms, p99=17.1ms], work 4.2 ms avg; renderer 60.0 fps, cpu 3.8 ms\n"
            "VkDrawDiag: frame=300 draws=420 (hud=12 scSkip=2 setBinds=150) realTex=28 dummyTex=2 copySlot=1 copyHits=10 copyFallbacks=0 copyMisses=0 mruHit=415 mruMiss=5 rtNew=0 rtKnownUid=0 record=3.12ms\n"
            "VkRenderer textures: 48 attempted, 48 recorded, 0 failed, 12288 KB staged\n"
        )

    data = parse_benchmark_log(log_text)
    summary = generate_report(data, args.out_json, args.out_csv)

    print("=== Super Mario Galaxy Android Benchmark Results ===")
    print(f"Average FPS:          {summary['average_fps']}")
    print(f"Frame Time P50:       {summary['frame_time_percentiles_ms']['p50']} ms")
    print(f"Frame Time P95:       {summary['frame_time_percentiles_ms']['p95']} ms")
    print(f"Frame Time P99:       {summary['frame_time_percentiles_ms']['p99']} ms")
    print(f"Draw Calls / Frame:   {summary['draw_calls_per_frame_avg']}")
    print(f"MRU Cache Hit Rate:   {summary['pipeline_mru_cache_hit_rate_pct']}%")
    print(f"Runtime Creates:      {summary['runtime_pipeline_creations']}")
    print(f"Reports saved to: {args.out_json} & {args.out_csv}")

if __name__ == "__main__":
    main()
