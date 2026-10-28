#!/usr/bin/env python3
# 策略运行时单元测试（不依赖 LLVM / 编译环境）

import copy
import json
import os
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "scripts"))

import agent_adapter
import strategy_runtime
import telemetry
from strategy_runtime import (
    ElementSpec,
    EvalRecord,
    StrategyExecutor,
    StrategyError,
    eval_expr,
)


class FakeEvaluator:
    """可预测的评估器：不调用 transform，也不需要 LLVM。"""

    def __init__(self, results=None):
        self.results = list(results or [])
        self.calls = []
        self.cache = {}
        self._index = 0

    def evaluate(self, assignment):
        full = dict(assignment)
        key = tuple(sorted(full.items()))
        if key in self.cache:
            record = copy.copy(self.cache[key])
            record.cached = True
            record.best = False
            return record

        if self._index < len(self.results):
            accepted, run_time, result = self.results[self._index]
        else:
            accepted, run_time, result = True, 10.0 - 0.1 * len(self.calls), "1.0"
        self._index += 1
        record = EvalRecord(
            run_id=len(self.calls) + 2,
            assignment=full,
            accepted=accepted,
            status=telemetry.STATUS_OK if accepted else telemetry.STATUS_THRESHOLD,
            time=run_time,
            result=result,
            rel_err=0.0 if accepted else 1.0,
            changes={},
        )
        self.calls.append(full)
        self.cache[key] = record
        return record


class RecordingAdapter(agent_adapter.NullAdapter):
    name = "recording"

    def __init__(self, advice=None):
        self.checkpoints = []
        self.advice = advice

    def analyze_checkpoint(self, request):
        self.checkpoints.append(request)
        return self.advice


def make_specs():
    return [
        ElementSpec(
            id="global#0", kind="global", name="g",
            original_type="double", search_types=["float", "double"],
        ),
        ElementSpec(
            id="func:f#0", kind="local", name="x", file="f.c", line=3,
            original_type="double",
            search_types=["__fp16", "float", "double"],
        ),
    ]


class SafeExpressionTest(unittest.TestCase):
    def test_basic_expression(self):
        env = {"last": {"accepted": True, "time": 3.0}, "best": {"time": 5.0}}
        self.assertTrue(eval_expr("last['accepted'] and last['time'] < best['time']", env))

    def test_helpers_and_literals(self):
        env = {"vars": {"p": "float"}, "a": [1, 2, 3]}
        self.assertEqual(eval_expr("vars['p']", env), "float")
        self.assertEqual(eval_expr("len(a) + 1", env), 4)

    def test_rejects_dunder_and_import(self):
        with self.assertRaises(StrategyError):
            eval_expr("__import__('os')", {})
        with self.assertRaises(StrategyError):
            eval_expr("(1).__class__", {})

    def test_comprehensions_and_methods(self):
        env = {"a": [1, 2, 3], "ids": ["x", "y"], "rec": {"k": 1}}
        self.assertEqual(eval_expr("[x * 2 for x in a if x > 1]", env), [4, 6])
        self.assertEqual(eval_expr("{e: 'float' for e in ids}", env),
                         {"x": "float", "y": "float"})
        self.assertEqual(eval_expr("rec.get('k', 0)", env), 1)
        self.assertEqual(eval_expr("rec.get('missing', 0)", env), 0)
        self.assertEqual(eval_expr("f\"{len(a)} it\"", env), "3 it")

    def test_bang_negation(self):
        env = {"r": {"accepted": False}}
        self.assertTrue(eval_expr("!r['accepted']", env))
        self.assertFalse(eval_expr("!r['accepted'] != True", env))
        # 字符串字面量里的 ! 不能被改写
        self.assertEqual(eval_expr("'wow!'", env), "wow!")

    def test_rejects_dunder_call_via_method(self):
        with self.assertRaises(StrategyError):
            eval_expr("{}.__class__", {})


class DslExecutorTest(unittest.TestCase):
    def test_eval_and_condition(self):
        fake = FakeEvaluator()
        executor = StrategyExecutor(
            make_specs(), fake, baseline_time=12.0, baseline_result="1.0",
            max_evals=5,
        )
        strategy = {
            "version": 1,
            "steps": [
                {
                    "op": "eval",
                    "base": "original",
                    "pairs": [["element_ids()[0]", "'float'"]],
                    "as": "r",
                },
                {
                    "op": "if",
                    "when": "r['accepted']",
                    "then": [{"op": "set", "name": "ok", "value": True}],
                    "else": [{"op": "set", "name": "ok", "value": False}],
                },
                {"op": "done", "reason": "finished"},
            ],
        }
        outcome = executor.run_dsl(strategy)

        self.assertEqual(outcome, "finished")
        self.assertEqual(len(fake.calls), 1)
        self.assertEqual(fake.calls[0]["global#0"], "float")
        self.assertEqual(fake.calls[0]["func:f#0"], "double")
        self.assertTrue(executor.dsl_vars["ok"])
        self.assertEqual(executor.evals, 1)

    def test_branch_variable_short_circuit(self):
        """变量只在某个 if 分支里绑定，另一分支用 and 短路引用不应报错。"""
        fake = FakeEvaluator(results=[(True, 9.0, "1.0")])
        executor = StrategyExecutor(
            make_specs(), fake, baseline_time=12.0, baseline_result="1.0",
            max_evals=6,
        )
        strategy = {
            "version": 1,
            "steps": [
                {"op": "eval_uniform", "type": "float", "as": "u1"},
                {"op": "if", "when": "not u1['accepted']", "then": [
                    {"op": "eval", "base": "original", "pairs": [], "as": "u2"},
                ]},
                {"op": "if",
                 "when": "not u1['accepted'] and u2['accepted']",
                 "then": [{"op": "set", "name": "path", "value": "b"}],
                 "else": [{"op": "set", "name": "path", "value": "a"}]},
                {"op": "done", "reason": "ok"},
            ],
        }
        self.assertEqual(executor.run_dsl(strategy), "ok")
        self.assertEqual(executor.dsl_vars["path"], "a")

    def test_unbound_branch_variable_raises(self):
        """短路保护之外真的读到未绑定变量时，必须抛出清晰的 StrategyError。"""
        executor = StrategyExecutor(
            make_specs(), FakeEvaluator(results=[(True, 9.0, "1.0")]),
            baseline_time=12.0, baseline_result="1.0", max_evals=6,
        )
        strategy = {
            "version": 1,
            "steps": [
                {"op": "eval_uniform", "type": "float", "as": "u1"},
                {"op": "if", "when": "not u1['accepted']", "then": [
                    {"op": "eval", "base": "original", "pairs": [], "as": "u2"},
                ]},
                {"op": "if", "when": "u2['accepted']", "then": []},
                {"op": "done", "reason": "ok"},
            ],
        }
        with self.assertRaises(StrategyError) as ctx:
            executor.run_dsl(strategy)
        self.assertIn("u2", str(ctx.exception))

    def test_eval_uniform_and_best_base(self):
        fake = FakeEvaluator()
        executor = StrategyExecutor(
            make_specs(), fake, baseline_time=12.0, baseline_result="1.0",
        )
        strategy = {
            "version": 1,
            "steps": [
                {"op": "eval_uniform", "type": "float", "as": "u"},
                {
                    "op": "eval",
                    "base": "best",
                    "pairs": [["'global#0'", "'__fp16'"]],
                    "as": "g",
                },
            ],
        }
        with self.assertRaises(StrategyError):
            # global#0 不支持 __fp16，应被拒绝
            executor.run_dsl(strategy)

        executor2 = StrategyExecutor(
            make_specs(), FakeEvaluator(), baseline_time=12.0,
            baseline_result="1.0",
        )
        strategy_ok = {
            "version": 1,
            "steps": [
                {"op": "eval_uniform", "type": "float", "as": "u"},
                {
                    "op": "eval",
                    "base": "best",
                    "pairs": [["'func:f#0'", "'__fp16'"]],
                    "as": "g",
                },
            ],
        }
        executor2.run_dsl(strategy_ok)
        self.assertEqual(len(executor2.evaluator.calls), 2)
        self.assertEqual(executor2.evaluator.calls[1]["func:f#0"], "__fp16")

    def test_foreach_and_for(self):
        fake = FakeEvaluator()
        executor = StrategyExecutor(
            make_specs(), fake, baseline_time=12.0, baseline_result="1.0",
        )
        strategy = {
            "version": 1,
            "steps": [
                {
                    "op": "foreach",
                    "items": "element_ids()",
                    "as": "e",
                    "steps": [
                        {
                            "op": "for",
                            "start": 0,
                            "stop": 2,
                            "as": "i",
                            "steps": [
                                {
                                    "op": "eval",
                                    "base": "original",
                                    "pairs": [["vars['e']", "types_of(vars['e'])[0]"]],
                                    "as": "r",
                                }
                            ],
                        }
                    ],
                }
            ],
        }
        executor.run_dsl(strategy)
        # 第一次评估后相同配置会被缓存，因此只有 2 次真实评估
        self.assertEqual(executor.evals, 2)

    def test_max_evals(self):
        fake = FakeEvaluator()
        executor = StrategyExecutor(
            make_specs(), fake, baseline_time=12.0, baseline_result="1.0",
            max_evals=1,
        )
        strategy = {
            "version": 1,
            "steps": [
                {
                    "op": "eval",
                    "pairs": [["'global#0'", "'float'"]],
                },
                {
                    "op": "eval",
                    "pairs": [["'func:f#0'", "'float'"]],
                },
            ],
        }
        outcome = executor.run_dsl(strategy)
        self.assertEqual(outcome, "max-evals")
        self.assertEqual(executor.evals, 1)

    def test_best_tracking(self):
        fake = FakeEvaluator(results=[
            (True, 8.0, "1.0"),
            (True, 4.0, "1.0"),
        ])
        executor = StrategyExecutor(
            make_specs(), fake, baseline_time=12.0, baseline_result="1.0",
        )
        strategy = {
            "version": 1,
            "steps": [
                {"op": "eval", "pairs": [["'global#0'", "'float'"]], "as": "a"},
                {"op": "eval", "pairs": [["'func:f#0'", "'float'"]], "as": "b"},
            ],
        }
        executor.run_dsl(strategy)
        self.assertEqual(executor.best.run_id, 3)
        self.assertEqual(executor.best.time, 4.0)
        self.assertTrue(executor.record_dicts[-1]["best"])

    def test_step_budget(self):
        executor = StrategyExecutor(
            make_specs(), FakeEvaluator(), baseline_time=12.0,
            baseline_result="1.0", max_steps=2,
        )
        strategy = {
            "version": 1,
            "steps": [
                {
                    "op": "while",
                    "when": "True",
                    "max_iterations": 10,
                    "steps": [{"op": "set", "name": "x", "value": 1}],
                }
            ],
        }
        with self.assertRaises(StrategyError):
            executor.run_dsl(strategy)

    def test_pairs_as_expression_string(self):
        fake = FakeEvaluator()
        executor = StrategyExecutor(
            make_specs(), fake, baseline_time=12.0, baseline_result="1.0",
        )
        strategy = {
            "version": 1,
            "steps": [
                {
                    "op": "foreach",
                    "items": "element_ids()",
                    "as": "e",
                    "steps": [
                        {"op": "eval", "pairs": "[[e, 'float']]", "as": "r"},
                        {"op": "break"},
                    ],
                }
            ],
        }
        executor.run_dsl(strategy)
        self.assertEqual(len(fake.calls), 1)
        self.assertEqual(fake.calls[0]["global#0"], "float")

    def test_base_from_record_variable(self):
        fake = FakeEvaluator()
        executor = StrategyExecutor(
            make_specs(), fake, baseline_time=12.0, baseline_result="1.0",
        )
        strategy = {
            "version": 1,
            "steps": [
                {"op": "eval", "pairs": [["global#0", "float"]], "as": "first"},
                # 在 first 的配置基础上继续修改另一个元素
                {"op": "eval", "base": "first", "pairs": [["func:f#0", "float"]]},
            ],
        }
        executor.run_dsl(strategy)
        self.assertEqual(len(fake.calls), 2)
        self.assertEqual(fake.calls[1]["global#0"], "float")
        self.assertEqual(fake.calls[1]["func:f#0"], "float")

    def test_set_value_literal_vs_expression(self):
        fake = FakeEvaluator()
        executor = StrategyExecutor(
            make_specs(), fake, baseline_time=12.0, baseline_result="1.0",
        )
        strategy = {
            "version": 1,
            "steps": [
                {"op": "eval", "pairs": [["global#0", "float"]], "as": "r"},
                # 纯单词按字面量
                {"op": "set", "name": "t", "value": "float"},
                # 含下标的字符串按表达式求值
                {"op": "set", "name": "bt", "value": "r['time']"},
                # 列表字符串按表达式求值
                {"op": "set", "name": "ids", "value": "element_ids()"},
                {"op": "done", "reason": "ok"},
            ],
        }
        executor.run_dsl(strategy)
        self.assertEqual(executor.dsl_vars["t"], "float")
        self.assertEqual(executor.dsl_vars["bt"], 10.0)
        self.assertEqual(executor.dsl_vars["ids"], ["global#0", "func:f#0"])

    def test_set_subscript_target(self):
        fake = FakeEvaluator()
        executor = StrategyExecutor(
            make_specs(), fake, baseline_time=12.0, baseline_result="1.0",
        )
        strategy = {
            "version": 1,
            "steps": [
                {"op": "set", "name": "acc", "expr": "{e: 'double' for e in element_ids()}"},
                {"op": "foreach", "items": "element_ids()", "as": "e", "steps": [
                    {"op": "set", "name": "acc[e]", "value": "types_of(e)[0]"},
                ]},
                # 用累加结果作为 base，并只覆盖其中一个元素
                {"op": "eval", "base": "acc", "pairs": [["global#0", "double"]]},
            ],
        }
        executor.run_dsl(strategy)
        self.assertEqual(executor.dsl_vars["acc"],
                         {"global#0": "float", "func:f#0": "__fp16"})
        self.assertEqual(fake.calls[0]["global#0"], "double")
        self.assertEqual(fake.calls[0]["func:f#0"], "__fp16")

    def test_telemetry_checkpoint_calls_adapter(self):
        adapter = RecordingAdapter()
        fake = FakeEvaluator()
        executor = StrategyExecutor(
            make_specs(), fake, baseline_time=12.0, baseline_result="1.0",
            telemetry_options=telemetry.TelemetryOptions(interval=2),
            adapter=adapter,
        )
        strategy = {
            "version": 1,
            "steps": [
                {"op": "eval", "pairs": [["'global#0'", "'float'"]]},
                {"op": "eval", "pairs": [["'func:f#0'", "'float'"]]},
            ],
        }
        executor.run_dsl(strategy)
        self.assertEqual(len(adapter.checkpoints), 1)
        self.assertEqual(adapter.checkpoints[0].eval_count, 2)
        # 检查点携带的是 run_history 数据摘要，而不只是异常列表
        digest = adapter.checkpoints[0].digest
        self.assertIsNotNone(digest)
        self.assertIn("min", digest.to_dict()["time_stats"])
        self.assertIn("acceptance", digest.to_dict())

    def _stop_strategy(self):
        # 每步都是不同配置，确保评估器不会命中缓存、evals 稳定递增
        combos = [
            ("float", "float"),
            ("float", "__fp16"),
            ("double", "__fp16"),
            ("float", "double"),
            ("double", "float"),
            ("double", "double"),
        ]
        return {
            "version": 1,
            "steps": [
                {"op": "eval", "pairs": [
                    ["'global#0'", repr(g)], ["'func:f#0'", repr(f)],
                ]}
                for g, f in combos
            ],
        }

    def test_checkpoint_adjustment_stop(self):
        adapter = RecordingAdapter(
            advice={"advice": "收敛够了", "adjustment": {"stop": True}})
        executor = StrategyExecutor(
            make_specs(), FakeEvaluator(), baseline_time=12.0,
            baseline_result="1.0",
            telemetry_options=telemetry.TelemetryOptions(interval=2),
            adapter=adapter,
        )
        reason = executor.run_dsl(self._stop_strategy())
        self.assertEqual(reason, "agent-stop")
        self.assertEqual(executor.evals, 2)

    def test_checkpoint_adjustment_max_evals(self):
        adapter = RecordingAdapter(
            advice={"advice": "预算收紧", "adjustment": {"max_evals": 3}})
        executor = StrategyExecutor(
            make_specs(), FakeEvaluator(), baseline_time=12.0,
            baseline_result="1.0", max_evals=100,
            telemetry_options=telemetry.TelemetryOptions(interval=2),
            adapter=adapter,
        )
        reason = executor.run_dsl(self._stop_strategy())
        self.assertEqual(reason, "max-evals")
        self.assertEqual(executor.evals, 3)
        self.assertEqual(executor.max_evals, 3)

    def test_checkpoint_adjustment_telemetry_interval(self):
        adapter = RecordingAdapter(
            advice={"advice": "减少打断", "adjustment": {"telemetry_interval": 50}})
        executor = StrategyExecutor(
            make_specs(), FakeEvaluator(), baseline_time=12.0,
            baseline_result="1.0",
            telemetry_options=telemetry.TelemetryOptions(interval=2),
            adapter=adapter,
        )
        executor.run_dsl(self._stop_strategy())
        self.assertEqual(executor.telemetry_options.interval, 50)
        self.assertEqual(len(adapter.checkpoints), 1)

    def test_checkpoint_adjustment_unknown_key_raises(self):
        adapter = RecordingAdapter(
            advice={"advice": "乱写", "adjustment": {"bogus": 1}})
        executor = StrategyExecutor(
            make_specs(), FakeEvaluator(), baseline_time=12.0,
            baseline_result="1.0",
            telemetry_options=telemetry.TelemetryOptions(interval=2),
            adapter=adapter,
        )
        with self.assertRaises(StrategyError):
            executor.run_dsl(self._stop_strategy())

    def test_checkpoint_adjustment_extra_steps_execute(self):
        """检查点注入的 extra_steps 追加到顶层队列并真正执行。"""
        injected = (
            '{"op": "eval", "pairs": [["\'global#0\'", "\'float\'"], '
            '["\'func:f#0\'", "\'float\'"]], "as": "x"}\n'
            '{"op": "set", "name": "y", "expr": "x[\'accepted\']"}\n'
        )
        adapter = RecordingAdapter(
            advice={"advice": "换方向重试",
                    "adjustment": {"extra_steps": injected}})
        evaluator = FakeEvaluator()
        executor = StrategyExecutor(
            make_specs(), evaluator, baseline_time=12.0,
            baseline_result="1.0",
            telemetry_options=telemetry.TelemetryOptions(interval=2),
            adapter=adapter,
        )
        # 原策略只有 2 步（触发一次 checkpoint 后队列耗尽），注入的 2 步应继续执行
        strategy = {
            "version": 1,
            "steps": [
                {"op": "eval", "pairs": [["'global#0'", "'float'"]]},
                {"op": "eval", "pairs": [["'func:f#0'", "'float'"]]},
            ],
        }
        reason = executor.run_dsl(strategy)
        self.assertEqual(reason, "completed")
        self.assertEqual(executor.evals, 3)  # 2 原有 + 1 注入（组合配置）
        # 注入步骤声明的变量可用（不抛 name not allowed）
        self.assertIn("x", executor.dsl_vars)
        self.assertIn("y", executor.dsl_vars)

    def test_checkpoint_adjustment_extra_steps_bad_ndjson_raises(self):
        adapter = RecordingAdapter(
            advice={"advice": "坏步骤",
                    "adjustment": {"extra_steps": '{"op": "if", "when": "True"}\n'}})
        executor = StrategyExecutor(
            make_specs(), FakeEvaluator(), baseline_time=12.0,
            baseline_result="1.0",
            telemetry_options=telemetry.TelemetryOptions(interval=2),
            adapter=adapter,
        )
        with self.assertRaises(StrategyError) as ctx:
            executor.run_dsl(self._stop_strategy())
        self.assertIn("extra_steps", str(ctx.exception))


class PythonSandboxTest(unittest.TestCase):
    def _write_strategy(self, text):
        handle = tempfile.NamedTemporaryFile(
            "w", suffix=".py", delete=False, encoding="utf-8",
        )
        handle.write(text)
        handle.close()
        self.addCleanup(lambda: os.path.exists(handle.name) and os.remove(handle.name))
        return handle.name

    def test_python_strategy(self):
        path = self._write_strategy(
            "def run(ctx):\n"
            "    e0, e1 = ctx['elements'][0]['id'], ctx['elements'][1]['id']\n"
            "    r0 = yield {'action': 'eval', 'assign': {e0: 'float'}}\n"
            "    assert r0['type'] == 'eval'\n"
            "    r1 = yield {'action': 'eval', 'base': 'best', 'assign': {e1: 'float'}}\n"
            "    assert r1['type'] == 'eval'\n"
            "    yield {'action': 'done', 'reason': 'python-ok'}\n"
        )
        fake = FakeEvaluator()
        executor = StrategyExecutor(
            make_specs(), fake, baseline_time=12.0, baseline_result="1.0",
            allow_python=True,
        )
        outcome = executor.run_file(path)
        self.assertEqual(outcome.reason, "python-ok")
        self.assertEqual(executor.evals, 2)

    def test_python_strategy_requires_flag(self):
        path = self._write_strategy(
            "def run(ctx):\n"
            "    yield {'action': 'done', 'reason': 'x'}\n"
        )
        executor = StrategyExecutor(
            make_specs(), FakeEvaluator(), baseline_time=12.0,
            baseline_result="1.0", allow_python=False,
        )
        with self.assertRaises(StrategyError):
            executor.run_file(path)

    def test_sandbox_blocks_network_import(self):
        path = self._write_strategy(
            "import socket\n"
            "def run(ctx):\n"
            "    yield {'action': 'done', 'reason': 'x'}\n"
        )
        executor = StrategyExecutor(
            make_specs(), FakeEvaluator(), baseline_time=12.0,
            baseline_result="1.0", allow_python=True,
        )
        with self.assertRaises(StrategyError):
            executor.run_file(path)


class ExampleStrategyTest(unittest.TestCase):
    def test_example_dsl_parses_and_runs(self):
        path = ROOT / "test" / "strategy" / "strategies" / "uniform_then_greedy.json"
        with open(path, "r") as f:
            strategy = json.load(f)
        fake = FakeEvaluator()
        executor = StrategyExecutor(
            make_specs(), fake, baseline_time=12.0, baseline_result="1.0",
            max_evals=20,
        )
        reason = executor.run_dsl(strategy)
        self.assertIn(reason, ("greedy-complete", "max-evals"))
        self.assertGreater(executor.evals, 0)

    def test_example_python_runs(self):
        path = ROOT / "test" / "strategy" / "strategies" / "greedy_reduce.py"
        fake = FakeEvaluator()
        executor = StrategyExecutor(
            make_specs(), fake, baseline_time=12.0, baseline_result="1.0",
            allow_python=True, max_evals=20,
        )
        outcome = executor.run_file(path)
        self.assertEqual(outcome.reason, "greedy-python-complete")
        self.assertGreater(executor.evals, 0)


if __name__ == "__main__":
    unittest.main()
