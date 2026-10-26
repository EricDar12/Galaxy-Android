import json
import os
import subprocess
import sys
import tempfile
import unittest

from tools.benchmarks.run_benchmark import (
    compute_percentiles,
    compute_cumulative_delta,
    parse_benchmark_log,
    generate_report,
)


class TestBenchmarkSuite(unittest.TestCase):
    def test_compute_percentiles_empty(self):
        self.assertIsNone(compute_percentiles([]))

    def test_compute_percentiles_edge_cases(self):
        # Single element
        p1 = compute_percentiles([16.6])
        self.assertEqual(p1["p50"], 16.6)
        self.assertEqual(p1["max"], 16.6)

        # 2 elements
        p2 = compute_percentiles([10.0, 20.0])
        self.assertEqual(p2["p50"], 10.0)
        self.assertEqual(p2["max"], 20.0)

        # 10 elements
        samples = [16.6, 16.7, 16.8, 17.0, 33.3, 16.5, 16.6, 16.7, 16.9, 20.0]
        res = compute_percentiles(samples)
        self.assertEqual(res["max"], 33.3)
        self.assertGreaterEqual(res["p50"], 16.6)
        self.assertLessEqual(res["p50"], 16.8)

    def test_missing_metrics_empty_log(self):
        data = parse_benchmark_log("")
        with tempfile.TemporaryDirectory() as tmp_dir:
            json_file = os.path.join(tmp_dir, "empty.json")
            csv_file = os.path.join(tmp_dir, "empty.csv")
            summary = generate_report(data, json_file, csv_file)

            m = summary["metrics"]
            self.assertIsNone(m["average_fps"])
            self.assertIsNone(m["frame_time_percentiles_ms"]["p50"])
            self.assertIsNone(m["draw_calls_per_frame_avg"])
            self.assertIsNone(m["pipeline_mru_cache_hit_rate_pct"])

            with open(csv_file, "r", encoding="utf-8") as f:
                csv_text = f.read()
                self.assertIn("average_fps,unavailable,unavailable", csv_text)

    def test_cumulative_counter_delta_and_resets(self):
        self.assertEqual(compute_cumulative_delta([]), 0)
        self.assertEqual(compute_cumulative_delta([100]), 100)
        # Monotonic: 100 -> 150 -> 220 -> delta is 120
        self.assertEqual(compute_cumulative_delta([100, 150, 220]), 120)
        # Reset: 100 -> 150 -> reset to 10 -> 40 -> delta is (50 + 10 + 30) = 90
        self.assertEqual(compute_cumulative_delta([100, 150, 10, 40]), 90)

    def test_parse_benchmark_log_with_raw_intervals(self):
        log_sample = (
            "perf: game 59.8 fps, intervals [p50=16.6ms, p95=16.9ms, p99=17.2ms], "
            "work 4.1 ms avg; renderer 59.8 fps, cpu 3.7 ms\n"
            "interval=16.5ms\n"
            "interval=16.7ms\n"
            "interval=17.1ms\n"
            "VkDrawDiag: frame=600 draws=380 (hud=10 scSkip=1 setBinds=120) "
            "realTex=24 dummyTex=2 copySlot=1 copyHits=8 copyFallbacks=0 copyMisses=0 "
            "mruHit=300 mruMiss=10 rtNew=0 rtKnownUid=0 record=2.90ms\n"
            "VkDrawDiag: frame=601 draws=400 (hud=10 scSkip=1 setBinds=120) "
            "realTex=24 dummyTex=2 copySlot=1 copyHits=8 copyFallbacks=0 copyMisses=0 "
            "mruHit=390 mruMiss=10 rtNew=0 rtKnownUid=0 record=2.90ms\n"
            "VkRenderer textures: 10 attempted, 10 recorded, 0 failed, 2048 KB staged\n"
        )
        data = parse_benchmark_log(log_sample)
        self.assertIn(59.8, data["fps_samples"])
        self.assertEqual(len(data["raw_intervals_ms"]), 3)
        self.assertEqual(data["draw_counts"], [380, 400])

        with tempfile.TemporaryDirectory() as tmp_dir:
            json_file = os.path.join(tmp_dir, "bench.json")
            csv_file = os.path.join(tmp_dir, "bench.csv")
            summary = generate_report(data, json_file, csv_file)

            self.assertEqual(summary["metadata"]["percentile_source"], "measured_raw_samples")
            self.assertEqual(summary["metrics"]["pipeline_mru_hits_delta"], 90)
            self.assertEqual(summary["metrics"]["pipeline_mru_misses_delta"], 0)
            self.assertEqual(summary["metrics"]["pipeline_mru_cache_hit_rate_pct"], 100.0)

    def test_synthetic_mode_flag(self):
        with tempfile.TemporaryDirectory() as tmp_dir:
            json_file = os.path.join(tmp_dir, "synth.json")
            csv_file = os.path.join(tmp_dir, "synth.csv")
            cmd = [
                sys.executable,
                "tools/benchmarks/run_benchmark.py",
                "--synthetic",
                "--out-json",
                json_file,
                "--out-csv",
                csv_file,
            ]
            res = subprocess.run(cmd, capture_output=True, text=True)
            self.assertEqual(res.returncode, 0)
            self.assertIn("Mode: synthetic", res.stdout)
            with open(json_file, "r", encoding="utf-8") as f:
                j = json.load(f)
                self.assertEqual(j["metadata"]["mode"], "synthetic")

    def test_no_raw_intervals_reports_unavailable(self):
        log_sample = (
            "perf: game 60.0 fps, intervals [p50=16.6ms, p95=16.9ms, p99=17.2ms], "
            "work 4.1 ms avg; renderer 60.0 fps, cpu 3.7 ms\n"
        )
        data = parse_benchmark_log(log_sample)
        with tempfile.TemporaryDirectory() as tmp_dir:
            json_file = os.path.join(tmp_dir, "window_only.json")
            csv_file = os.path.join(tmp_dir, "window_only.csv")
            summary = generate_report(data, json_file, csv_file)
            self.assertEqual(summary["metadata"]["percentile_source"], "unavailable")
            self.assertIsNone(summary["metrics"]["frame_time_percentiles_ms"]["p50"])
            self.assertEqual(len(summary["metrics"]["per_window_reported_intervals"]), 1)
            self.assertEqual(summary["metrics"]["per_window_reported_intervals"][0]["p50_ms"], 16.6)

    def test_duplicate_snapshots_deduplicated(self):
        log_sample = (
            "VkDrawDiag: frame=10 draws=100 (hud=0 scSkip=0 setBinds=10) realTex=1 dummyTex=0 copySlot=0 copyHits=0 copyFallbacks=0 copyMisses=0 mruHit=100 mruMiss=10 rtNew=0 rtKnownUid=0 record=1.0ms\n"
            "VkDrawDiag: frame=10 draws=100 (hud=0 scSkip=0 setBinds=10) realTex=1 dummyTex=0 copySlot=0 copyHits=0 copyFallbacks=0 copyMisses=0 mruHit=100 mruMiss=10 rtNew=0 rtKnownUid=0 record=1.0ms\n"
            "VkDrawDiag: frame=20 draws=100 (hud=0 scSkip=0 setBinds=10) realTex=1 dummyTex=0 copySlot=0 copyHits=0 copyFallbacks=0 copyMisses=0 mruHit=150 mruMiss=10 rtNew=0 rtKnownUid=0 record=1.0ms\n"
        )
        data = parse_benchmark_log(log_sample)
        self.assertEqual(len(data["diag_samples"]), 3)
        with tempfile.TemporaryDirectory() as tmp_dir:
            summary = generate_report(data, os.path.join(tmp_dir, "dedup.json"), None)
            self.assertEqual(summary["metrics"]["pipeline_mru_hits_delta"], 50)

    def test_cli_no_args_exit_code_2(self):
        cmd = [sys.executable, "tools/benchmarks/run_benchmark.py"]
        res = subprocess.run(cmd, capture_output=True, text=True)
        self.assertEqual(res.returncode, 2)
        self.assertIn("Benchmark mode not specified", res.stderr)


if __name__ == "__main__":
    unittest.main()

