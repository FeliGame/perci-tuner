#!/usr/bin/env python3
# 遥测摘要 / 触发策略单元测试
#
# 遥测的职责是把动态搜索数据（时间、混精配置、数值结果、趋势）汇总成
# LLM 可读的摘要；异常信号只是摘要的一个字段。

import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "scripts"))

import telemetry
from strategy_runtime import EvalRecord, ElementSpec


def record(run_id, status=telemetry.STATUS_OK, accepted=True, time=1.0,
           result="1.0", rel_err=0.0, assignment=None, best=False):
    return EvalRecord(
        run_id=run_id,
        assignment=assignment or {f"e{run_id}": "double"},
        accepted=accepted,
        status=status,
        time=time,
        result=result,
        rel_err=rel_err,
        best=best,
    )


def spec(eid, original="double", search_types=("double", "float", "__fp16")):
    return ElementSpec(
        id=eid, kind="global", name=eid, file="a.c", line=1, column=1,
        original_type=original, search_types=list(search_types),
    )


class TelemetryDigestTest(unittest.TestCase):
    """摘要内容：时间 / 接受率 / 配置 / 数值 / 趋势。"""

    def setUp(self):
        self.records = [
            record(1, status=telemetry.STATUS_BASELINE, time=1.0,
                   assignment={"e": "double", "f": "double"}),
            record(2, time=0.9, assignment={"e": "float", "f": "double"},
                   rel_err=1e-7, best=True),
            record(3, status=telemetry.STATUS_THRESHOLD, accepted=False,
                   time=float("nan"), assignment={"e": "__fp16", "f": "double"},
                   rel_err=8e-4),
            record(4, time=0.5, assignment={"e": "float", "f": "float"},
                   rel_err=4e-7),
        ]
        self.specs = [spec("e"), spec("f")]
        self.digest = telemetry.analyze(
            self.records, eval_count=3, best_run_id=2,
            threshold=1e-4, specs=self.specs,
        )

    def test_acceptance_and_status(self):
        acc = self.digest.acceptance
        self.assertEqual((acc["total"], acc["accepted"], acc["rejected"]), (3, 2, 1))
        self.assertEqual(self.digest.status_counts[telemetry.STATUS_THRESHOLD], 1)

    def test_time_stats_and_speedup(self):
        ts = self.digest.time_stats
        self.assertAlmostEqual(ts["min"], 0.5)
        self.assertAlmostEqual(ts["max"], 0.9)
        self.assertAlmostEqual(ts["median"], 0.7)
        # 基准 1.0 / 中位数 0.7
        self.assertAlmostEqual(ts["speedup_vs_baseline"], 1.4286, places=3)

    def test_numeric_summary(self):
        num = self.digest.numeric
        self.assertEqual(num["count"], 3)
        self.assertAlmostEqual(num["max"], 8e-4)
        self.assertEqual(num["over_threshold"], 1)
        self.assertEqual(num["worst"][0]["run_id"], 3)

    def test_precision_summary(self):
        precision = self.digest.precision
        self.assertEqual(precision["changed_elements"], 2)
        by_id = {item["id"]: item for item in precision["elements"]}
        self.assertEqual(by_id["e"]["types"], {"float": 2, "__fp16": 1})
        self.assertTrue(by_id["f"]["always_original"] is False)

    def test_trend_and_best(self):
        self.assertEqual(self.digest.best_run_id, 2)
        self.assertEqual(self.digest.best["run_id"], 2)
        self.assertEqual([i["run_id"] for i in self.digest.trend["improvements"]], [2])
        self.assertEqual(self.digest.trend["since_best"], 2)
        self.assertEqual(self.digest.trend["recent_times"], [0.9, 0.5])

    def test_digest_to_dict_and_prompt(self):
        data = self.digest.to_dict()
        for key in ("time_stats", "acceptance", "precision", "numeric",
                    "trend", "anomalies", "summary"):
            self.assertIn(key, data)
        text = self.digest.to_prompt()
        self.assertIn("接受率", text)
        self.assertIn("数值误差", text)
        self.assertIn("配置变化", text)
        self.assertIn("异常信号", text)
        self.assertIn("best run 2", self.digest.summary())


class TelemetryAnomalyTest(unittest.TestCase):
    """异常信号（摘要中的一个字段）仍然要准确。"""

    def test_duplicate_invalid_result(self):
        records = [
            record(1, status=telemetry.STATUS_BASELINE, assignment={"e": "double"}),
            record(2, status=telemetry.STATUS_THRESHOLD, accepted=False,
                   result="-1.0", rel_err=1.0),
            record(3, status=telemetry.STATUS_THRESHOLD, accepted=False,
                   result="-1.0", rel_err=1.0),
        ]
        digest = telemetry.analyze(records, eval_count=2, best_run_id=1)
        codes = {f.code for f in digest.anomalies}
        self.assertIn("duplicate-invalid-result", codes)

    def test_failures_and_abnormal_values(self):
        records = [
            record(1, status=telemetry.STATUS_BASELINE),
            record(2, status=telemetry.STATUS_RUN_ERROR, accepted=False,
                   time=float("inf"), result=None, rel_err=float("inf")),
            record(3, status=telemetry.STATUS_OK, time=float("nan"),
                   result="nan", rel_err=float("nan")),
        ]
        digest = telemetry.analyze(records, eval_count=2, best_run_id=1)
        codes = {f.code for f in digest.anomalies}
        self.assertIn("failure:run-error", codes)
        self.assertIn("abnormal-result", codes)
        self.assertIn("abnormal-time", codes)

    def test_long_run(self):
        records = [
            record(1, status=telemetry.STATUS_BASELINE, time=1.0),
            record(2, time=5.0),
        ]
        digest = telemetry.analyze(records, eval_count=1, best_run_id=1)
        self.assertIn("long-run", {f.code for f in digest.anomalies})

    def test_stagnation(self):
        records = [record(1, status=telemetry.STATUS_BASELINE)]
        for i in range(2, 15):
            records.append(record(i, time=1.0 + i))
        digest = telemetry.analyze(
            records, eval_count=14, best_run_id=2,
            options=telemetry.TelemetryOptions(stagnation_window=10),
        )
        self.assertIn("stagnation", {f.code for f in digest.anomalies})

    def test_no_anomaly(self):
        records = [
            record(1, status=telemetry.STATUS_BASELINE, time=1.0),
            record(2, time=0.9),
            record(3, time=0.8),
        ]
        digest = telemetry.analyze(
            records, eval_count=2, best_run_id=3,
            options=telemetry.TelemetryOptions(stagnation_window=10,
                                               long_run_factor=10),
        )
        self.assertFalse(digest.has_anomalies)
        self.assertIn("no anomaly", digest.summary())


class TelemetryTriggerTest(unittest.TestCase):
    def test_interval(self):
        records = [record(1, status=telemetry.STATUS_BASELINE), record(2)]
        reasons = telemetry.should_fire(
            records, eval_count=2, best_run_id=2,
            last_fire_eval=0, last_fire_best_run_id=1,
            options=telemetry.TelemetryOptions(interval=2, stagnation_window=100),
        )
        self.assertIn("interval", reasons)

    def test_interval_fires_without_anomaly(self):
        # 固定间隔是主触发条件：即使没有任何异常，也要把数据交给 LLM 分析
        records = [record(1, status=telemetry.STATUS_BASELINE)]
        for i in range(2, 6):
            records.append(record(i, time=1.0 - i * 0.05))
        reasons = telemetry.should_fire(
            records, eval_count=4, best_run_id=5,
            last_fire_eval=0, last_fire_best_run_id=1,
            options=telemetry.TelemetryOptions(interval=4, stagnation_window=100),
        )
        self.assertEqual(reasons, ["interval"])

    def test_failure_streak(self):
        records = [
            record(1, status=telemetry.STATUS_BASELINE),
            record(2, status=telemetry.STATUS_RUN_ERROR, accepted=False),
            record(3, status=telemetry.STATUS_RUN_ERROR, accepted=False),
            record(4, status=telemetry.STATUS_RUN_ERROR, accepted=False),
        ]
        reasons = telemetry.should_fire(
            records, eval_count=3, best_run_id=1,
            last_fire_eval=0, last_fire_best_run_id=1,
            options=telemetry.TelemetryOptions(interval=100, failure_streak=3),
        )
        self.assertIn("failure_streak", reasons)

    def test_stagnation(self):
        records = [record(1, status=telemetry.STATUS_BASELINE)]
        for i in range(2, 13):
            records.append(record(i))
        reasons = telemetry.should_fire(
            records, eval_count=12, best_run_id=1,
            last_fire_eval=0, last_fire_best_run_id=1,
            options=telemetry.TelemetryOptions(interval=100, stagnation_window=10),
        )
        self.assertIn("stagnation", reasons)

    def test_no_fire(self):
        records = [record(1, status=telemetry.STATUS_BASELINE), record(2)]
        reasons = telemetry.should_fire(
            records, eval_count=1, best_run_id=2,
            last_fire_eval=0, last_fire_best_run_id=1,
            options=telemetry.TelemetryOptions(interval=25, stagnation_window=10),
        )
        self.assertEqual(reasons, [])


if __name__ == "__main__":
    unittest.main()
