#!/usr/bin/env python3
"""
Automated Vulkan and Frame Pacing Benchmark Tool for Galaxy-Android.
Measures CPU/GPU frame times, percentiles, draw counts, pipeline cache hit rates,
and exports machine-readable JSON and CSV reports with honest data classification.
"""

import argparse
import json
import os
import re
import subprocess
import sys
import time

def get_git_commit():
    try:
        res = subprocess.run(["git", "rev-parse", "HEAD"], capture_output=True, text=True, timeout=3)
        if res.returncode == 0:
            return res.stdout.strip()
    except Exception:
        pass
    return "unknown"

def compute_percentiles(samples):
    """Computes percentiles using standard nearest-rank convention on raw samples."""
    if not samples:
        return None
    s = sorted(samples)
    n = len(s)
    def rank(p):
        idx = int(round(p * (n - 1)))
        return round(float(s[max(0, min(n - 1, idx))]), 2)
    return {
        "p50": rank(0.50),
        "p90": rank(0.90),
        "p95": rank(0.95),
        "p99": rank(0.99),
        "max": round(float(s[-1]), 2),
    }

def parse_benchmark_log(log_text):
    data = {
        "raw_intervals_ms": [],
        "reported_p50_ms": [],
        "reported_p95_ms": [],
        "reported_p99_ms": [],
        "fps_samples": [],
        "draw_counts": [],
        "set_binds": [],
        "diag_samples": [],
        "tex_staged_kb": [],
        "tex_recorded": [],
        "work_ms": [],
        "render_cpu_ms": [],
    }

    re_perf = re.compile(
        r"perf:\s+game\s+([\d.]+)\s+fps,\s+intervals\s+\[p50=([\d.]+)ms,\s+p95=([\d.]+)ms,\s+p99=([\d.]+)ms\],"
        r"\s+work\s+([\d.]+)\s+ms.*?renderer\s+([\d.]+)\s+fps,\s+cpu\s+([\d.]+)\s+ms"
    )
    re_draw = re.compile(
        r"VkDrawDiag:\s+frame=(\d+)\s+draws=(\d+).*?setBinds=(\d+).*?mruHit=(\d+)\s+mruMiss=(\d+)\s+rtNew=(\d+)"
    )
    re_tex = re.compile(
        r"VkRenderer textures:.*?(\d+)\s+recorded.*?(\d+)\s+KB staged"
    )
    re_raw_interval = re.compile(r"(?:interval|frame_dt|dt)=([\d.]+)\s*ms")

    for line in log_text.splitlines():
        m_perf = re_perf.search(line)
        if m_perf:
            fps, p50, p95, p99, work, r_fps, cpu = m_perf.groups()
            data["fps_samples"].append(float(fps))
            data["reported_p50_ms"].append(float(p50))
            data["reported_p95_ms"].append(float(p95))
            data["reported_p99_ms"].append(float(p99))
            data["work_ms"].append(float(work))
            data["render_cpu_ms"].append(float(cpu))

        m_draw = re_draw.search(line)
        if m_draw:
            frame, draws, binds, hits, misses, rt_new = m_draw.groups()
            data["draw_counts"].append(int(draws))
            data["set_binds"].append(int(binds))
            data["diag_samples"].append((int(frame), int(hits), int(misses), int(rt_new)))

        m_tex = re_tex.search(line)
        if m_tex:
            rec, kb = m_tex.groups()
            data["tex_recorded"].append(int(rec))
            data["tex_staged_kb"].append(int(kb))

        m_raw = re_raw_interval.search(line)
        if m_raw:
            data["raw_intervals_ms"].append(float(m_raw.group(1)))

    return data

def compute_cumulative_delta(samples):
    """Calculates delta across log window, properly accounting for counter resets."""
    if not samples:
        return 0
    if len(samples) == 1:
        return samples[0]
    total_delta = 0
    for prev, curr in zip(samples[:-1], samples[1:]):
        if curr >= prev:
            total_delta += (curr - prev)
        else:
            total_delta += curr
    return total_delta

def run_adb_capture(duration_sec=5, package="com.openport.galaxyandroid"):
    state_res = subprocess.run(["adb", "get-state"], capture_output=True, text=True)
    if state_res.returncode != 0 or "device" not in state_res.stdout:
        raise RuntimeError("No active Android device found over ADB.")

    pid_res = subprocess.run(["adb", "shell", f"pidof {package}"], capture_output=True, text=True)
    pid = pid_res.stdout.strip()
    device_info = {}
    try:
        model = subprocess.run(["adb", "shell", "getprop ro.product.model"], capture_output=True, text=True).stdout.strip()
        os_ver = subprocess.run(["adb", "shell", "getprop ro.build.version.release"], capture_output=True, text=True).stdout.strip()
        device_info = {"model": model, "android_version": os_ver}
    except Exception:
        pass

    subprocess.run(["adb", "logcat", "-c"], capture_output=True)
    time.sleep(duration_sec)
    cmd = ["adb", "logcat", "-d", "-v", "time"]
    if pid:
        cmd.extend(["--pid", pid])
    else:
        cmd.extend(["-s", "PetariVR:I", "PetariVK:I", "FramePacer:I"])
    res = subprocess.run(cmd, capture_output=True, text=True, errors="replace")
    stdout = res.stdout
    if not stdout or len(stdout.strip()) == 0:
        raise RuntimeError("ADB logcat capture produced 0 bytes of logs.")
    return stdout, device_info

def generate_report(data, out_json_path, out_csv_path, meta=None):
    if meta is None:
        meta = {}
    is_synth = meta.get("mode") == "synthetic"
    if is_synth:
        pct = compute_percentiles(data["raw_intervals_ms"]) if data["raw_intervals_ms"] else {
            "p50": 16.6, "p90": 16.7, "p95": 16.8, "p99": 17.1, "max": 17.5
        }
        pct_source = "synthetic_demo_data"
    elif data["raw_intervals_ms"]:
        pct = compute_percentiles(data["raw_intervals_ms"])
        pct_source = "measured_raw_samples"
    else:
        pct = {"p50": None, "p90": None, "p95": None, "p99": None, "max": None}
        pct_source = "unavailable"

    window_reports = [
        {"window": i, "p50_ms": p50, "p95_ms": p95, "p99_ms": p99}
        for i, (p50, p95, p99) in enumerate(zip(data["reported_p50_ms"], data["reported_p95_ms"], data["reported_p99_ms"]))
    ]
    fps_count = len(data["fps_samples"])
    avg_fps = round(sum(data["fps_samples"]) / fps_count, 2) if fps_count > 0 else None
    draw_count = len(data["draw_counts"])
    avg_draws = round(sum(data["draw_counts"]) / draw_count, 1) if draw_count > 0 else None
    bind_count = len(data["set_binds"])
    avg_binds = round(sum(data["set_binds"]) / bind_count, 1) if bind_count > 0 else None

    seen_frames, unique_diag = set(), []
    for s in data["diag_samples"]:
        if s[0] not in seen_frames:
            seen_frames.add(s[0])
            unique_diag.append(s)

    if unique_diag:
        hits_delta = compute_cumulative_delta([s[1] for s in unique_diag])
        misses_delta = compute_cumulative_delta([s[2] for s in unique_diag])
        rt_new_delta = compute_cumulative_delta([s[3] for s in unique_diag])
        tot_mru = hits_delta + misses_delta
        mru_hit_rate = round(hits_delta * 100.0 / tot_mru, 1) if tot_mru > 0 else None
    else:
        hits_delta = misses_delta = rt_new_delta = mru_hit_rate = None

    textures_staged_kb = data["tex_staged_kb"][-1] if data["tex_staged_kb"] else None
    textures_recorded = data["tex_recorded"][-1] if data["tex_recorded"] else None

    summary = {
        "metadata": {
            "mode": meta.get("mode", "input_file"),
            "commit_sha": meta.get("commit_sha", get_git_commit()),
            "benchmark_timestamp": time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime()),
            "capture_duration_sec": meta.get("duration", None),
            "device": meta.get("device", "unknown"),
            "percentile_source": pct_source,
            "samples_count": {
                "fps_samples": fps_count,
                "draw_samples": draw_count,
                "raw_intervals": len(data["raw_intervals_ms"]),
                "window_reports": len(window_reports),
            },
        },
        "metrics": {
            "average_fps": avg_fps,
            "frame_time_percentiles_ms": pct,
            "per_window_reported_intervals": window_reports,
            "draw_calls_per_frame_avg": avg_draws,
            "descriptor_set_binds_avg": avg_binds,
            "pipeline_mru_cache_hit_rate_pct": mru_hit_rate,
            "pipeline_mru_hits_delta": hits_delta,
            "pipeline_mru_misses_delta": misses_delta,
            "runtime_pipeline_creations_delta": rt_new_delta,
            "textures_staged_kb": textures_staged_kb,
            "textures_recorded": textures_recorded,
        }
    }

    if out_json_path:
        os.makedirs(os.path.dirname(os.path.abspath(out_json_path)), exist_ok=True)
        with open(out_json_path, "w", encoding="utf-8") as f:
            json.dump(summary, f, indent=2)

    if out_csv_path:
        os.makedirs(os.path.dirname(os.path.abspath(out_csv_path)), exist_ok=True)
        with open(out_csv_path, "w", encoding="utf-8") as f:
            f.write("metric,value,classification\n")
            def w(k, v, cls):
                f.write(f"{k},{'unavailable' if v is None else v},{cls}\n")
            m, p = summary["metrics"], summary["metrics"]["frame_time_percentiles_ms"]
            c = "synthetic" if is_synth else "measured"
            w("average_fps", m["average_fps"], c if m["average_fps"] is not None else "unavailable")
            for k in ("p50", "p90", "p95", "p99", "max"):
                w(f"frame_time_{k}_ms", p[k], pct_source)
            w("draw_calls_avg", m["draw_calls_per_frame_avg"], c if m["draw_calls_per_frame_avg"] is not None else "unavailable")
            w("descriptor_binds_avg", m["descriptor_set_binds_avg"], c if m["descriptor_set_binds_avg"] is not None else "unavailable")
            mru_c = "synthetic" if is_synth else "derived"
            w("mru_hit_rate_pct", m["pipeline_mru_cache_hit_rate_pct"], mru_c if m["pipeline_mru_cache_hit_rate_pct"] is not None else "unavailable")
            w("runtime_pipeline_creations", m["runtime_pipeline_creations_delta"], c if m["runtime_pipeline_creations_delta"] is not None else "unavailable")
            w("textures_staged_kb", m["textures_staged_kb"], c if m["textures_staged_kb"] is not None else "unavailable")

    return summary

def main():
    parser = argparse.ArgumentParser(description="Galaxy-Android Benchmark Runner")
    parser.add_argument("--device", action="store_true", help="Capture live metrics from connected Android device via ADB")
    parser.add_argument("--duration", type=int, default=5, help="Capture duration in seconds (default: 5)")
    parser.add_argument("--input-file", help="Input log file to parse instead of live device capture")
    parser.add_argument("--synthetic", "--demo", action="store_true", help="Run in explicitly labeled synthetic demo mode")
    parser.add_argument("--out-json", default="artifacts/benchmark_results.json", help="Output JSON path")
    parser.add_argument("--out-csv", default="artifacts/benchmark_results.csv", help="Output CSV path")
    args = parser.parse_args()

    if not args.device and not args.input_file and not args.synthetic:
        sys.stderr.write("[ERROR] Benchmark mode not specified.\nYou must supply --device, --input-file <path>, or --synthetic/--demo.\n")
        parser.print_usage(sys.stderr)
        sys.exit(2)

    meta = {"commit_sha": get_git_commit(), "duration": args.duration}

    if args.synthetic:
        meta["mode"] = "synthetic"
        meta["device"] = "synthetic_simulation"
        log_text = (
            "perf: game 60.0 fps, intervals [p50=16.6ms, p95=16.8ms, p99=17.1ms], work 4.2 ms avg; renderer 60.0 fps, cpu 3.8 ms\n"
            "VkDrawDiag: frame=100 draws=420 (hud=12 scSkip=2 setBinds=150) realTex=28 dummyTex=2 copySlot=1 copyHits=10 copyFallbacks=0 copyMisses=0 mruHit=400 mruMiss=10 rtNew=0 rtKnownUid=0 record=3.12ms\n"
            "VkDrawDiag: frame=200 draws=420 (hud=12 scSkip=2 setBinds=150) realTex=28 dummyTex=2 copySlot=1 copyHits=10 copyFallbacks=0 copyMisses=0 mruHit=780 mruMiss=20 rtNew=0 rtKnownUid=0 record=3.12ms\n"
            "VkRenderer textures: 48 attempted, 48 recorded, 0 failed, 12288 KB staged\n"
        )
    elif args.input_file:
        meta["mode"] = "input_file"
        meta["device"] = "file_playback"
        with open(args.input_file, "r", encoding="utf-8", errors="replace") as f:
            log_text = f.read()
    else:
        meta["mode"] = "live_device"
        try:
            log_text, dev_info = run_adb_capture(args.duration)
            meta["device"] = dev_info.get("model", "connected_android_device")
        except Exception as e:
            sys.stderr.write(f"[ERROR] Live ADB capture failed: {e}\n")
            sys.exit(1)

    data = parse_benchmark_log(log_text)
    summary = generate_report(data, args.out_json, args.out_csv, meta)

    m = summary["metrics"]
    p = m["frame_time_percentiles_ms"]
    print(f"=== Super Mario Galaxy Android Benchmark Results [Mode: {meta['mode']}] ===\n"
          f"Average FPS:          {'N/A (unavailable)' if m['average_fps'] is None else m['average_fps']}\n"
          f"Frame Time P50:       {'N/A (unavailable)' if p['p50'] is None else str(p['p50']) + ' ms'} ({summary['metadata']['percentile_source']})\n"
          f"Frame Time P95:       {'N/A (unavailable)' if p['p95'] is None else str(p['p95']) + ' ms'}\n"
          f"Frame Time P99:       {'N/A (unavailable)' if p['p99'] is None else str(p['p99']) + ' ms'}\n"
          f"Draw Calls / Frame:   {'N/A (unavailable)' if m['draw_calls_per_frame_avg'] is None else m['draw_calls_per_frame_avg']}\n"
          f"MRU Cache Hit Rate:   {'N/A (unavailable)' if m['pipeline_mru_cache_hit_rate_pct'] is None else str(m['pipeline_mru_cache_hit_rate_pct']) + '%'}\n"
          f"Runtime Creates:      {'N/A (unavailable)' if m['runtime_pipeline_creations_delta'] is None else m['runtime_pipeline_creations_delta']}\n"
          f"Reports saved to: {args.out_json} & {args.out_csv}")

if __name__ == "__main__":
    main()

