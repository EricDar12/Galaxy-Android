import json
import os
import tempfile
import unittest

from tools.benchmarks.run_benchmark import (
    compute_percentiles,
    parse_benchmark_log,
    generate_report,
)


class TestBenchmarkSuite(unittest.TestCase):
    def test_compute_percentiles_empty(self):
        res = compute_percentiles([])
        self.assertEqual(res["p50"], 0.0)
        self.assertEqual(res["max"], 0.0)

    def test_compute_percentiles_values(self):
        samples = [16.6, 16.7, 16.8, 17.0, 33.3, 16.5, 16.6, 16.7, 16.9, 20.0]
        res = compute_percentiles(samples)
        self.assertGreaterEqual(res["p50"], 16.5)
        self.assertLessEqual(res["p50"], 17.0)
        self.assertEqual(res["max"], 33.3)

    def test_parse_benchmark_log(self):
        log_sample = (
            "perf: game 59.8 fps, intervals [p50=16.6ms, p95=16.9ms, p99=17.2ms], "
            "work 4.1 ms avg; renderer 59.8 fps, cpu 3.7 ms\n"
            "VkDrawDiag: frame=600 draws=380 (hud=10 scSkip=1 setBinds=120) "
            "realTex=24 dummyTex=2 copySlot=1 copyHits=8 copyFallbacks=0 copyMisses=0 "
            "mruHit=375 mruMiss=5 rtNew=0 rtKnownUid=0 record=2.90ms\n"
            "VkRenderer textures: 10 attempted, 10 recorded, 0 failed, 2048 KB staged\n"
        )
        data = parse_benchmark_log(log_sample)
        self.assertIn(59.8, data["fps_samples"])
        self.assertIn(16.6, data["intervals_ms"])
        self.assertIn(380, data["draw_counts"])
        self.assertIn(120, data["set_binds"])
        self.assertEqual(data["mru_hits"], 375)
        self.assertEqual(data["mru_misses"], 5)
        self.assertEqual(data["staging_kb"], 2048)

    def test_generate_report_exports(self):
        log_sample = (
            "perf: game 60.0 fps, intervals [p50=16.6ms, p95=16.8ms, p99=17.0ms], "
            "work 4.0 ms avg; renderer 60.0 fps, cpu 3.5 ms\n"
            "VkDrawDiag: frame=300 draws=400 (hud=10 scSkip=0 setBinds=100) "
            "realTex=20 dummyTex=0 copySlot=0 copyHits=5 copyFallbacks=0 copyMisses=0 "
            "mruHit=395 mruMiss=5 rtNew=0 rtKnownUid=0 record=2.50ms\n"
            "VkRenderer textures: 12 attempted, 12 recorded, 0 failed, 4096 KB staged\n"
        )
        data = parse_benchmark_log(log_sample)

        with tempfile.TemporaryDirectory() as tmp_dir:
            json_file = os.path.join(tmp_dir, "bench.json")
            csv_file = os.path.join(tmp_dir, "bench.csv")

            summary = generate_report(data, json_file, csv_file)
            self.assertEqual(summary["average_fps"], 60.0)
            self.assertEqual(summary["draw_calls_per_frame_avg"], 400.0)

            self.assertTrue(os.path.isfile(json_file))
            with open(json_file, "r", encoding="utf-8") as f:
                loaded_json = json.load(f)
                self.assertEqual(loaded_json["average_fps"], 60.0)
                self.assertIn("frame_time_percentiles_ms", loaded_json)

            self.assertTrue(os.path.isfile(csv_file))
            with open(csv_file, "r", encoding="utf-8") as f:
                csv_content = f.read()
                self.assertIn("average_fps,60.0", csv_content)
                f_lines = csv_content.splitlines()
                self.assertGreater(len(f_lines), 5)


if __name__ == "__main__":
    unittest.main()
