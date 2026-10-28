# agent_adapter 单元测试：用 mock opencode HTTP/SSE 服务验证适配器行为，
# 不依赖真实 LLM 服务。

import io
import json
import os
import shutil
import stat
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "scripts"))

import agent_adapter
import telemetry
from strategy_runtime import EvalRecord

MOCK_SERVER = ROOT / "test" / "strategy" / "opencode_mock_server.py"


def telemetry_record(run_id, status=telemetry.STATUS_OK, time=1.0, best=False):
    return EvalRecord(
        run_id=run_id, assignment={"e": "float"}, accepted=True,
        status=status, time=time, result="1.0", rel_err=0.0, best=best,
    )

STRATEGY = {
    "version": 1,
    "steps": [
        {"op": "eval_uniform", "type": "float", "as": "u"},
        {"op": "done", "reason": "mock-done"},
    ],
}

NDJSON_ANSWER = (
    '{"advice": "mock advice"}\n'
    '{"op": "eval_uniform", "type": "float", "as": "u"}\n'
    '{"op": "done", "reason": "mock-done"}\n'
)


def write_executable(path, content):
    with open(path, "w") as f:
        f.write(content)
    os.chmod(path, os.stat(path).st_mode | stat.S_IXUSR | stat.S_IXGRP | stat.S_IXOTH)


class _Capture(io.TextIOBase):
    """记录每次 write 的内容，用于断言流式输出是分段到达的。"""

    def __init__(self):
        self.pieces = []

    def write(self, text):
        self.pieces.append(text)
        return len(text)

    def flush(self):
        pass


class OpencodeAdapterTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.mkdtemp(prefix="opencode-mock-")
        self.addCleanup(lambda: shutil.rmtree(self.tmp, ignore_errors=True))
        self.log_path = os.path.join(self.tmp, "requests.log")
        self.binary = os.path.join(self.tmp, "opencode")
        write_executable(
            self.binary,
            "#!/bin/sh\nexec python3 \"%s\" \"$@\"\n" % MOCK_SERVER,
        )
        os.environ["MOCK_OPENCODE_DIR"] = self.tmp
        os.environ["MOCK_OPENCODE_LOG"] = self.log_path
        os.environ["MOCK_OPENCODE_STREAM_DELAY"] = "0"
        self.addCleanup(lambda: [
            os.environ.pop(k, None)
            for k in ("MOCK_OPENCODE_DIR", "MOCK_OPENCODE_LOG",
                      "MOCK_OPENCODE_STREAM_DELAY")
        ])

    def _adapter(self, answers=None, timeout=30, stream=True):
        if answers is None:
            answers = [NDJSON_ANSWER]
        os.makedirs(os.path.join(self.tmp, "answers"), exist_ok=True)
        for index, text in enumerate(answers, start=1):
            with open(os.path.join(self.tmp, "answers", f"{index}.txt"), "w") as f:
                f.write(text)
        return agent_adapter.OpencodeAdapter(
            binary=self.binary, timeout=timeout, stream=stream,
        )

    def _structure_request(self):
        return agent_adapter.StructureRequest(
            sources={"main.c": "int main(){return 0;}"},
            elements=[{
                "id": "global#0", "kind": "global", "name": "g",
                "file": "main.c", "line": 1, "column": 1,
                "original_type": "double", "search_types": ["float", "double"],
            }],
            threshold=1e-6,
            config_path="config.json",
            source_files=["main.c"],
        )

    def _checkpoint_request(self):
        digest = telemetry.analyze(
            [
                telemetry_record(1, status=telemetry.STATUS_BASELINE, time=1.0),
                telemetry_record(2, time=0.9, best=True),
            ],
            eval_count=1, best_run_id=2,
        )
        return agent_adapter.CheckpointRequest(
            digest=digest, records=[],
            elements=[{"id": "func:main#0", "kind": "local",
                       "search_types": ["float", "double"]}],
            best={"run_id": 2, "time": 0.9},
            eval_count=3, max_evals=10, reasons=["interval"],
        )

    def _requests(self):
        if not os.path.exists(self.log_path):
            return []
        with open(self.log_path) as f:
            return f.read().split()

    def test_structure_analysis_returns_strategy(self):
        adapter = self._adapter()
        self.addCleanup(adapter.close)
        result = adapter.analyze_structure(self._structure_request())
        self.assertEqual(result["advice"], "mock advice")
        self.assertEqual(result["strategy"]["steps"][1]["reason"], "mock-done")

    def test_checkpoint_returns_advice(self):
        adapter = self._adapter(
            answers=['{"advice": "reduce eval budget", "adjustment": null}'])
        self.addCleanup(adapter.close)
        result = adapter.analyze_checkpoint(self._checkpoint_request())
        self.assertEqual(result["advice"], "reduce eval budget")

    def test_checkpoint_merges_multi_object_output(self):
        """模型把策略阶段的 NDJSON 习惯带进检查点：advice 对象后跟 op 步骤行。"""
        adapter = self._adapter(answers=[
            '{"advice": "fp16 被拒，改试 float", "adjustment": {"note": "n1"}}\n'
            '{"op": "foreach", "items": "element_ids()", "as": "e"}\n'
            '{"op": "end"}\n'
            '{"adjustment": {"telemetry_interval": 10}}\n'
        ])
        self.addCleanup(adapter.close)
        result = adapter.analyze_checkpoint(self._checkpoint_request())
        self.assertEqual(result["advice"], "fp16 被拒，改试 float")
        self.assertEqual(result["adjustment"],
                         {"note": "n1", "telemetry_interval": 10})

    def test_checkpoint_requires_advice_object(self):
        adapter = self._adapter(answers=[
            '{"op": "done", "reason": "x"}\n{"adjustment": null}'])
        self.addCleanup(adapter.close)
        with self.assertRaises(RuntimeError) as ctx:
            adapter.analyze_checkpoint(self._checkpoint_request())
        self.assertIn("advice", str(ctx.exception))

    def test_checkpoint_extra_steps_validated(self):
        """extra_steps 是 NDJSON 字符串；元素 id 笔误在生成期即报错重试。"""
        good = ('{"advice": "换方向", "adjustment": {"extra_steps": '
                '"{\\"op\\": \\"eval\\", \\"pairs\\": [[\\"func:main#0\\", \\"float\\"]]}\\n"'
                '}}')
        adapter = self._adapter(answers=[good])
        self.addCleanup(adapter.close)
        result = adapter.analyze_checkpoint(self._checkpoint_request())
        self.assertIn("extra_steps", result["adjustment"])

        bad = ('{"advice": "笔误", "adjustment": {"extra_steps": '
               '"{\\"op\\": \\"eval\\", \\"pairs\\": [[\\"main#0\\", \\"float\\"]]}\\n"'
               '}}')
        adapter = self._adapter(answers=[bad, good])
        self.addCleanup(adapter.close)
        # 第一次笔误报错后重试，第二次 good 通过
        result = adapter.analyze_checkpoint(self._checkpoint_request())
        self.assertEqual(result["advice"], "换方向")

    def test_streams_tokens_progressively(self):
        adapter = self._adapter()
        self.addCleanup(adapter.close)
        capture = _Capture()
        original = sys.stdout
        sys.stdout = capture
        try:
            adapter.analyze_structure(self._structure_request())
        finally:
            sys.stdout = original
        streamed = [p for p in capture.pieces if p]
        self.assertTrue(any(">>> LLM" in p for p in streamed),
                        f"缺少流式输出: {streamed[:5]}")
        # 回答被拆成多个 delta，而不是结束时一次性打印
        text_pieces = [p for p in streamed if p.strip() and ">>>" not in p]
        self.assertGreater(len(text_pieces), 1, streamed)

    def test_session_continues_on_second_call(self):
        adapter = self._adapter(answers=[
            '{"advice": "a"}\n{"op": "done", "reason": "x"}\n',
            '{"advice": "b", "adjustment": null}',
        ])
        self.addCleanup(adapter.close)
        adapter.analyze_structure(self._structure_request())
        session_id = adapter.session_id
        adapter.analyze_checkpoint(self._checkpoint_request())
        self.assertEqual(adapter.session_id, session_id)
        self.assertEqual(self._requests().count("/api/session"), 1)

    def test_server_startup_failure_raises(self):
        broken = os.path.join(self.tmp, "broken")
        write_executable(broken, "#!/bin/sh\nexit 1\n")
        adapter = agent_adapter.OpencodeAdapter(binary=broken, timeout=5)
        with self.assertRaises(RuntimeError):
            adapter.analyze_structure(self._structure_request())

    def test_missing_binary_raises(self):
        adapter = agent_adapter.OpencodeAdapter(
            binary=os.path.join(self.tmp, "nonexistent"), timeout=5)
        with self.assertRaises(OSError):
            adapter.analyze_structure(self._structure_request())

    def test_invalid_strategy_output_raises(self):
        adapter = self._adapter(answers=['{"advice": "no strategy here"}'])
        self.addCleanup(adapter.close)
        with self.assertRaises(RuntimeError):
            adapter.analyze_structure(self._structure_request())

    def test_wrong_password_raises(self):
        adapter = self._adapter()
        self.addCleanup(adapter.close)
        adapter._ensure_server()
        adapter.password = "wrong"
        with self.assertRaises(RuntimeError):
            adapter.analyze_structure(self._structure_request())

    def test_extract_json_object_tolerates_noise(self):
        text = '前言 ```json {"advice": "a", "strategy": {"version": 1, "steps": []}} ``` 后记'
        parsed = agent_adapter._extract_json_object(text)
        self.assertEqual(parsed["advice"], "a")
        self.assertEqual(parsed["strategy"]["version"], 1)

    def test_parse_json_object_reports_position(self):
        # 裸标识符（LLM 常见错误）必须给出精确行列与片段，而不是静默返回内层对象
        obj, error = agent_adapter._parse_json_object('{"a": [[e, "float"]]}')
        self.assertIsNone(obj)
        self.assertIn("JSON 解析失败", error)
        self.assertIn("附近片段", error)

    def test_parse_json_object_strips_fence(self):
        obj, error = agent_adapter._parse_json_object('```json\n{"a": 1}\n```')
        self.assertIsNone(error)
        self.assertEqual(obj, {"a": 1})

    def test_retry_after_invalid_json(self):
        # 裸标识符行（LLM 常见错误）触发重试，第二次修正后通过
        bad = '{"advice": "a"}\n{"op": "eval", "pairs": [[e, "float"]]}\n'
        adapter = self._adapter(answers=[bad, NDJSON_ANSWER])
        self.addCleanup(adapter.close)
        result = adapter.analyze_structure(self._structure_request())
        self.assertEqual(result["advice"], "mock advice")
        # 第二次调用带上了错误反馈
        self.assertEqual(self._requests().count("/api/session/ses_mock/prompt"), 2)

    def test_close_stops_spawned_server(self):
        adapter = self._adapter()
        adapter._ensure_server()
        proc = adapter._proc
        self.assertIsNotNone(proc)
        adapter.close()
        self.assertIsNotNone(proc.poll(), "close() 应终止自己启动的 opencode 服务")

    def test_create_adapter(self):
        self.assertIsInstance(agent_adapter.create_adapter("none"),
                              agent_adapter.NullAdapter)
        self.assertIsInstance(agent_adapter.create_adapter("opencode"),
                              agent_adapter.OpencodeAdapter)
        with self.assertRaises(ValueError):
            agent_adapter.create_adapter("bogus")


class JsonlStrategyTest(unittest.TestCase):
    """行式 NDJSON 策略解析：无闭合段、if/else/end 配对、错误带行号。"""

    def test_flat_steps(self):
        strategy = agent_adapter.parse_jsonl_strategy(
            '{"op": "eval_uniform", "type": "float", "as": "u"}\n'
            '\n'
            '{"op": "done", "reason": "x"}\n'
        )
        self.assertEqual(
            strategy["steps"],
            [{"op": "eval_uniform", "type": "float", "as": "u"},
             {"op": "done", "reason": "x"}],
        )

    def test_if_else_end(self):
        strategy = agent_adapter.parse_jsonl_strategy(
            '{"op": "if", "when": "u[\'accepted\']"}\n'
            '{"op": "done", "reason": "then"}\n'
            '{"op": "else"}\n'
            '{"op": "done", "reason": "else"}\n'
            '{"op": "end"}\n'
        )
        step = strategy["steps"][0]
        self.assertEqual(step["then"], [{"op": "done", "reason": "then"}])
        self.assertEqual(step["else"], [{"op": "done", "reason": "else"}])

    def test_nested_foreach(self):
        strategy = agent_adapter.parse_jsonl_strategy(
            '{"op": "foreach", "items": "element_ids()", "as": "e"}\n'
            '{"op": "eval", "base": "best", "pairs": "[[e, \'float\']]", "as": "r"}\n'
            '{"op": "end"}\n'
        )
        body = strategy["steps"][0]["steps"]
        self.assertEqual(body[0]["op"], "eval")

    def test_dump_round_trip(self):
        text = (
            '{"advice": "a"}\n'
            '{"op": "if", "when": "True"}\n'
            '{"op": "done", "reason": "x"}\n'
            '{"op": "else"}\n'
            '{"op": "log", "message": "hi"}\n'
            '{"op": "end"}\n'
        )
        result = agent_adapter.parse_structure_answer(text)
        dumped = agent_adapter.dump_jsonl_strategy(
            result["advice"], result["strategy"])
        again = agent_adapter.parse_structure_answer(dumped)
        self.assertEqual(again["strategy"], result["strategy"])

    def test_invalid_line_reports_lineno(self):
        with self.assertRaises(ValueError) as ctx:
            agent_adapter.parse_jsonl_strategy(
                '{"op": "done"}\n{"op": "eval", "pairs": [[e, "float"]]}\n'
            )
        self.assertIn("line 2", str(ctx.exception))

    def test_end_without_block(self):
        with self.assertRaises(ValueError) as ctx:
            agent_adapter.parse_jsonl_strategy('{"op": "end"}\n')
        self.assertIn("line 1", str(ctx.exception))

    def test_missing_end(self):
        with self.assertRaises(ValueError) as ctx:
            agent_adapter.parse_jsonl_strategy(
                '{"op": "if", "when": "True"}\n{"op": "done"}\n'
            )
        self.assertIn("缺少 end", str(ctx.exception))

    def test_duplicate_else(self):
        with self.assertRaises(ValueError):
            agent_adapter.parse_jsonl_strategy(
                '{"op": "if", "when": "True"}\n'
                '{"op": "else"}\n{"op": "else"}\n{"op": "end"}\n'
            )

    def test_structure_answer_requires_header(self):
        with self.assertRaises(RuntimeError):
            agent_adapter.parse_structure_answer('{"op": "done"}\n')

    def test_structure_answer_strips_fence(self):
        result = agent_adapter.parse_structure_answer(
            '```json\n{"advice": "a"}\n{"op": "done"}\n```\n'
        )
        self.assertEqual(result["advice"], "a")

    def test_structure_answer_skips_prose_preamble(self):
        result = agent_adapter.parse_structure_answer(
            'Looking at this problem, I need to analyze...\n'
            '{"advice": "a"}\n{"op": "done", "reason": "x"}\n'
        )
        self.assertEqual(result["advice"], "a")
        self.assertEqual(result["strategy"]["steps"][0]["reason"], "x")

    def test_structure_answer_reports_header_typo(self):
        with self.assertRaises(RuntimeError) as ctx:
            agent_adapter.parse_structure_answer(
                '{"adice": "typo"}\n{"op": "done"}\n'
            )
        self.assertIn("advice", str(ctx.exception))


class LiteralPairsValidationTest(unittest.TestCase):
    """eval 字面 pairs 的类型必须在元素 search_types 内（数组/指针带后缀）。"""

    ELEMENTS = [
        {"id": "global#3", "search_types": ["__fp16*", "float*", "double*"]},
        {"id": "func:main#0", "search_types": ["float", "double"]},
    ]

    def test_rejects_missing_pointer_suffix(self):
        strategy = {"steps": [
            {"op": "eval", "base": "best",
             "pairs": [["global#3", "float"]], "as": "r"},
        ]}
        with self.assertRaises(RuntimeError) as ctx:
            agent_adapter._validate_literal_pairs(strategy, self.ELEMENTS)
        msg = str(ctx.exception)
        self.assertIn("global#3", msg)
        self.assertIn("float*", msg)

    def test_accepts_pointer_suffix(self):
        strategy = {"steps": [
            {"op": "eval", "base": "best",
             "pairs": [["global#3", "float*"]], "as": "r"},
        ]}
        agent_adapter._validate_literal_pairs(strategy, self.ELEMENTS)

    def test_skips_expression_pairs(self):
        strategy = {"steps": [
            {"op": "foreach", "items": "element_ids()", "as": "e", "steps": [
                {"op": "eval", "base": "best",
                 "pairs": "[[e, types_of(e)[0]]]", "as": "r"},
            ]},
        ]}
        agent_adapter._validate_literal_pairs(strategy, self.ELEMENTS)

    def test_checks_nested_steps(self):
        strategy = {"steps": [
            {"op": "if", "when": "True", "then": [
                {"op": "eval", "base": "best",
                 "pairs": [["func:main#0", "__fp16"]], "as": "r"},
            ], "else": []},
        ]}
        with self.assertRaises(RuntimeError) as ctx:
            agent_adapter._validate_literal_pairs(strategy, self.ELEMENTS)
        self.assertIn("then[0]", str(ctx.exception))

    def test_rejects_typo_element_id_in_literal_pairs(self):
        strategy = {"steps": [
            {"op": "eval", "base": "best",
             "pairs": [["main#0", "float"]], "as": "r"},
        ]}
        with self.assertRaises(RuntimeError) as ctx:
            agent_adapter._validate_literal_pairs(strategy, self.ELEMENTS)
        msg = str(ctx.exception)
        self.assertIn("main#0", msg)
        self.assertIn("func:main#0", msg)  # difflib 建议正确写法

    def test_rejects_typo_element_id_in_pairs_expression(self):
        strategy = {"steps": [
            {"op": "eval", "base": "best",
             "pairs": "[[e, types_of(e)[0]] for e in ['main#0', 'global#3']]",
             "as": "r"},
        ]}
        with self.assertRaises(RuntimeError) as ctx:
            agent_adapter._validate_literal_pairs(strategy, self.ELEMENTS)
        self.assertIn("main#0", str(ctx.exception))

    def test_accepts_valid_ids_in_pairs_expression(self):
        strategy = {"steps": [
            {"op": "eval", "base": "best",
             "pairs": "[[e, types_of(e)[0]] for e in ['func:main#0', 'global#3']]",
             "as": "r"},
        ]}
        agent_adapter._validate_literal_pairs(strategy, self.ELEMENTS)


class StructurePromptTest(unittest.TestCase):
    """结构分析 prompt 必须把优化目标与预算说清楚。"""

    class _Capture(agent_adapter.OpencodeAdapter):
        def __init__(self):
            super().__init__(binary="/bin/true", timeout=5)
            self.prompts = []

        def _prompt(self, text):
            self.prompts.append(text)
            return NDJSON_ANSWER

    def _request(self, elements, eval_budget=0):
        return agent_adapter.StructureRequest(
            sources={"a.c": "double x;"}, elements=elements,
            threshold=1e-6, eval_budget=eval_budget, baseline_time=0.01,
        )

    def _elements(self, count):
        return [{
            "id": f"global#{i}", "kind": "global", "name": f"g{i}",
            "file": "a.c", "line": 1, "column": 1,
            "original_type": "double", "search_types": ["float", "double"],
        } for i in range(count)]

    def test_prompt_states_objectives_and_budget(self):
        adapter = self._Capture()
        adapter.analyze_structure(self._request(self._elements(5)))
        prompt = adapter.prompts[0]
        self.assertIn("满足误差阈值的前提下", prompt)
        self.assertIn("禁止穷举", prompt)
        self.assertIn("由你自行设计试探路径", prompt)
        self.assertIn("细粒度", prompt)
        # 无硬上限时给出建议预算（2×元素数+4 = 14）
        self.assertIn("建议不超过 14 次评估", prompt)
        self.assertIn("10 次评估", prompt)  # 穷举规模 5 元素 × 2 类型

    def test_prompt_reports_hard_budget(self):
        adapter = self._Capture()
        adapter.analyze_structure(self._request(self._elements(3), eval_budget=8))
        prompt = adapter.prompts[0]
        self.assertIn("硬上限是 8 次评估", prompt)


if __name__ == "__main__":
    unittest.main()
