#!/usr/bin/env python3
# 策略搜索与生产评估器的接入层。
#
# 这里保持与既有 dd/pedd/hidd/exhaust 相同的外部行为：
# - 每次评估仍然调用 transform.transform，写出 config/{run_id}.json 等
# - run_history.csv 的列与含义不变
# - 最优配置仍通过 algorithm_utils.update_opt 更新

import copy
import json
import math
import os
import sys

import algorithm_utils
import config_utils
import data_utils
import transform
import telemetry
import agent_adapter
import strategy_runtime


class ProductionEvaluator:
    """把策略给出的完整 assignment 转换成 config 并执行一次真实评估。"""

    def __init__(self, bitcode, change_set, original_conf, specs):
        self.bitcode = bitcode
        self.change_set = change_set
        self.original_conf = original_conf
        self.specs = specs
        self.spec_by_id = {spec.id: spec for spec in specs}
        self.item_by_id = {}
        for index, item in enumerate(change_set):
            eid = strategy_runtime.element_id(item, index)
            self.item_by_id[eid] = item
        self.cache = {}
        self._seed_baseline()

    def _seed_baseline(self):
        """把 run 1（基准配置）放入缓存，避免策略重复评估原始配置。"""
        assignment = {spec.id: spec.original_type for spec in self.specs}
        key = self._config_key(self.original_conf)
        self.cache[key] = strategy_runtime.EvalRecord(
            run_id=1,
            assignment=assignment,
            accepted=True,
            status=telemetry.STATUS_BASELINE,
            time=data_utils.ORIGINAL_TIME,
            result=data_utils.ORIGINAL_RESULT,
            rel_err=0.0,
            cached=True,
        )

    @staticmethod
    def _config_key(config):
        return json.dumps(config, sort_keys=True, separators=(",", ":"))

    @staticmethod
    def _apply_type(config, item, new_type):
        if "global" in item:
            target = config.get("global")
            index = item.get("gvar_index")
        elif "func" in item:
            target = (config.get("local") or {}).get(item.get("func"))
            index = item.get("fvar_index")
        elif "caller" in item:
            target = (config.get("calls") or {}).get(item.get("caller"))
            index = item.get("fcall_index")
        elif "struct" in item:
            target = (config.get("structs") or {}).get(item.get("struct"))
            index = item.get("member_index")
            if index is None:
                index = item.get(None)
        else:
            raise strategy_runtime.StrategyError(
                f"unsupported change element: {item}"
            )
        if target is None or index is None:
            raise strategy_runtime.StrategyError(
                f"cannot locate element in config: {item}"
            )
        try:
            index = int(index)
        except (TypeError, ValueError):
            raise strategy_runtime.StrategyError(
                f"invalid element index: {index!r}"
            )
        if index < 0 or index >= len(target):
            raise strategy_runtime.StrategyError(
                f"element index {index} out of range for {item}"
            )
        member = target[index]
        is_pointer = str(member.get("type", "")).endswith("*")
        final_type = new_type
        if is_pointer and not final_type.endswith("*"):
            final_type += "*"
        member["type"] = final_type

    def build_config(self, full_assignment):
        config = copy.deepcopy(self.original_conf)
        for eid, new_type in full_assignment.items():
            item = self.item_by_id.get(eid)
            if item is None:
                raise strategy_runtime.StrategyError(f"unknown element id: {eid}")
            self._apply_type(config, item, new_type)
        return config

    @staticmethod
    def _status_of(code):
        mapping = {
            1: (telemetry.STATUS_OK, True),
            0: (telemetry.STATUS_THRESHOLD, False),
            -1: (telemetry.STATUS_PASS_ERROR, False),
            -2: (telemetry.STATUS_RUN_ERROR, False),
            -3: (telemetry.STATUS_NO_TIME, False),
            -4: (telemetry.STATUS_NO_RESULT, False),
        }
        return mapping.get(code, (telemetry.STATUS_NO_OUTPUT, False))

    def _rel_err(self, result):
        if result is None:
            return None
        if algorithm_utils.is_mxp:
            try:
                return float(result)
            except (TypeError, ValueError):
                return None
        try:
            return data_utils.get_rel_err(result, data_utils.ORIGINAL_RESULT)
        except Exception:
            return None

    def evaluate(self, full_assignment):
        config = self.build_config(full_assignment)
        key = self._config_key(config)
        if key in self.cache:
            cached = copy.copy(self.cache[key])
            cached.assignment = dict(full_assignment)
            cached.cached = True
            return cached

        diff = {}
        algorithm_utils.compare_json(self.original_conf, config, diff)
        algorithm_utils.run_times += 1
        run_id = algorithm_utils.run_times
        config_utils.CURRENT_CONFIG_PARENT = algorithm_utils.best_config_index

        try:
            code = transform.transform(
                run_id, self.bitcode, config, diff,
                algorithm_utils.is_mxp,
                config_utils.CURRENT_CONFIG_PARENT,
            )
        except SystemExit:
            # transform 在程序无输出时会 sys.exit(1)。策略搜索把它当作一次可分析的失败。
            code = "NO-OUTPUT"
        except Exception:
            raise

        status, accepted = self._status_of(code)
        if code == 1:
            run_time = transform.current_run_time
        elif code == 0:
            run_time = float("nan")
        else:
            run_time = float("inf")
        compile_time = transform.current_compile_time
        result = transform.current_run_result

        record = strategy_runtime.EvalRecord(
            run_id=run_id,
            assignment=dict(full_assignment),
            accepted=accepted,
            status=status,
            time=run_time,
            compile_time=compile_time,
            result=result,
            rel_err=self._rel_err(result),
            changes=diff,
        )

        if accepted and math.isfinite(run_time) and run_time < algorithm_utils.min_time:
            algorithm_utils.update_opt(run_time)

        self.cache[key] = record
        return record

    def close(self):
        self.cache.clear()


GENERATED_STRATEGY_NAME = "strategy_generated.jsonl"


def _strategy_format(args, path):
    fmt = (args.get("-strategy-format") or "auto").lower()
    if fmt == "auto":
        lower = path.lower()
        if lower.endswith(".py"):
            return "python"
        if lower.endswith(".jsonl"):
            return "jsonl"
        return "dsl"
    return fmt


def run(bitcode, change_set, args):
    strategy_path = args.get("-strategy") or ""

    try:
        adapter = agent_adapter.create_adapter(args.get("-agent", "none"))
    except ValueError as exc:
        print(f"Error: {exc}")
        sys.exit(1)

    if not strategy_path and not adapter.supports_structure:
        print("Error: -algorithm=strategy requires -strategy=<file> "
              "or -agent=opencode (LLM 生成策略)")
        sys.exit(1)
    if strategy_path and not os.path.isfile(strategy_path):
        print(f"Error: strategy file not found: {strategy_path}")
        sys.exit(1)

    fmt = _strategy_format(args, strategy_path) if strategy_path else "jsonl"
    if fmt not in ("dsl", "json", "jsonl", "python"):
        print(f"Error: invalid -strategy-format={fmt} (allowed: auto/json/jsonl/python)")
        sys.exit(1)
    if fmt == "json":
        fmt = "dsl"

    max_evals = int(args.get("-max-evals", 0) or 0)
    if max_evals < 0:
        print("Error: -max-evals must be >= 0")
        sys.exit(1)
    interval = int(args.get("-telemetry-interval", 25) or 25)
    if interval <= 0:
        print("Error: -telemetry-interval must be > 0")
        sys.exit(1)

    try:
        specs = strategy_runtime.build_element_specs(change_set)
    except strategy_runtime.StrategyError as exc:
        print(f"Error: {exc}")
        sys.exit(1)

    evaluator = ProductionEvaluator(
        bitcode, change_set, algorithm_utils.original_conf, specs
    )
    executor = strategy_runtime.StrategyExecutor(
        specs=specs,
        evaluator=evaluator,
        baseline_time=data_utils.ORIGINAL_TIME,
        baseline_result=data_utils.ORIGINAL_RESULT,
        threshold=config_utils.thres,
        max_evals=max_evals,
        telemetry_options=telemetry.TelemetryOptions(interval=interval),
        adapter=adapter,
        allow_python=bool(args.get("-allow-generated-strategy", False)),
    )

    llm_generated = False

    def _generate_strategy(feedback=""):
        """LLM 生成/重新生成策略并落盘。出错直接抛异常（无降级）。"""
        advice = executor.run_structure_analysis(
            config_path=args.get("-config", ""), feedback=feedback
        )
        strategy = advice.get("strategy") if isinstance(advice, dict) else None
        if strategy is None:
            raise strategy_runtime.StrategyError(
                "agent 未产出有效策略（无降级，直接失败）"
            )
        with open(GENERATED_STRATEGY_NAME, "w") as f:
            f.write(agent_adapter.dump_jsonl_strategy(
                advice.get("advice", ""), strategy))
        print(f">>> Strategy: LLM generated -> {GENERATED_STRATEGY_NAME}")
        return GENERATED_STRATEGY_NAME, "jsonl"

    try:
        if adapter.supports_structure:
            print(">>> Strategy agent: analyzing program structure ...")
            if not strategy_path:
                strategy_path, fmt = _generate_strategy()
                llm_generated = True
            else:
                executor.run_structure_analysis(
                    config_path=args.get("-config", ""))

        print(f">>> Strategy: {strategy_path} (format: {fmt})")
        print(f">>> Strategy max evals: {max_evals if max_evals > 0 else 'unlimited'}")
        print(f">>> Strategy agent: {adapter.name}")
        regen_left = 1 if llm_generated else 0
        while True:
            try:
                outcome = executor.run_file(strategy_path, fmt)
                break
            except strategy_runtime.StrategyError as exc:
                # LLM 生成的策略在执行期报错（如动态 pairs 给 call 元素赋了 float）：
                # 把错误回喂给 LLM 重新生成一次（已评估配置走缓存，代价低）。
                if regen_left <= 0:
                    raise
                regen_left -= 1
                print(f">>> Strategy agent: 策略执行期报错: {exc}")
                print(">>> Strategy agent: 要求 LLM 修正后重新生成策略 ...")
                strategy_path, fmt = _generate_strategy(feedback=str(exc))
    except strategy_runtime.StrategyError as exc:
        print(f">>> Perci-Tuner Error: {exc}")
        sys.exit(1)
    finally:
        evaluator.close()
        adapter.close()

    best = outcome.best
    print(
        f">>> Strategy finished ({outcome.reason}): "
        f"{outcome.evals} evals, best run {best.run_id}, "
        f"best time {best.time}"
    )
    digest = executor.analyze_telemetry()
    print(f">>> Strategy telemetry: {digest.summary()}")
    for finding in digest.anomalies:
        print(f"    [{finding.severity}] {finding.code}: {finding.message}")
    return outcome
