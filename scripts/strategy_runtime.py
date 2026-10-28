#!/usr/bin/env python3
# 混合精度搜索策略运行时。
#
# 设计要点：
# - LLM 只负责“生成策略”，不进入每次评估的内层循环。
# - Tier A：安全的 JSON 步骤 DSL，表达式用受限 AST 求值。
# - Tier B：LLM 生成的 Python 策略在受限子进程中运行，通过 JSON 行协议
#   与父进程交互；父进程才持有 LLVM/执行器的权限。
# - 评估器可注入，单元测试无需 LLVM。

import ast
import json
import math
import os
import queue
import re
import shutil
import subprocess
import sys
import tempfile
import threading
from dataclasses import dataclass, field

import telemetry
import agent_adapter


class StrategyError(Exception):
    """策略格式 / 语义错误。"""


class MaxEvalsReached(Exception):
    """达到最大评估次数。"""


class _Done(Exception):
    def __init__(self, reason):
        super().__init__(reason)
        self.reason = reason


class _BreakLoop(Exception):
    pass


# ---------------------------------------------------------------------------
# 数据结构
# ---------------------------------------------------------------------------

@dataclass
class ElementSpec:
    id: str
    kind: str
    name: str = ""
    file: str = ""
    line: object = None
    column: object = None
    original_type: str = ""
    search_types: list = field(default_factory=list)

    def to_dict(self):
        return {
            "id": self.id,
            "kind": self.kind,
            "name": self.name,
            "file": self.file,
            "line": self.line,
            "column": self.column,
            "original_type": self.original_type,
            "search_types": list(self.search_types),
        }


@dataclass
class EvalRecord:
    run_id: int
    assignment: dict
    accepted: bool = False
    status: str = telemetry.STATUS_OK
    time: float = float("inf")
    compile_time: float = float("inf")
    result: object = None
    rel_err: object = None
    changes: dict = field(default_factory=dict)
    cached: bool = False
    best: bool = False
    note: str = ""

    def to_dict(self):
        # 内部（DSL 表达式）保留原始 float，JSON 序列化时再由 json_safe 处理
        return {
            "run_id": self.run_id,
            "assignment": dict(self.assignment or {}),
            "accepted": bool(self.accepted),
            "status": self.status,
            "time": self.time,
            "compile_time": self.compile_time,
            "result": self.result,
            "rel_err": self.rel_err,
            "changes": dict(self.changes or {}),
            "cached": bool(self.cached),
            "best": bool(self.best),
            "note": self.note,
        }


@dataclass
class SearchOutcome:
    best: EvalRecord
    evals: int
    reason: str
    history: list = field(default_factory=list)


def json_safe(value):
    """递归把非 JSON 原生类型（set 等）转成可序列化形式。

    Python 的 json 模块原生支持 NaN/Infinity 字面量，因此浮点数保持原样；
    策略代码可以直接做数值比较，无需解析字符串标记。
    """
    if isinstance(value, dict):
        return {str(k): json_safe(v) for k, v in value.items()}
    if isinstance(value, (list, tuple)):
        return [json_safe(v) for v in value]
    if isinstance(value, set):
        return [json_safe(v) for v in sorted(value, key=str)]
    return value


def _is_finite_number(value):
    return isinstance(value, (int, float)) and not isinstance(value, bool) \
        and math.isfinite(value)


def _wrap(text, width=76):
    """按显示宽度折行（中文按 2 列计），用于打印 LLM 长文本。"""
    lines = []
    current, used = [], 0
    for ch in text:
        w = 2 if ord(ch) > 0x2E7F else 1
        if used + w > width and current:
            lines.append("".join(current))
            current, used = [], 0
        current.append(ch)
        used += w
    if current:
        lines.append("".join(current))
    return lines or [""]


# ---------------------------------------------------------------------------
# 受限表达式求值（Tier A）
# ---------------------------------------------------------------------------

_SAFE_BUILTINS = {
    "len": len,
    "min": min,
    "max": max,
    "abs": abs,
    "round": round,
    "int": int,
    "float": float,
    "str": str,
    "bool": bool,
    "sorted": sorted,
    "sum": sum,
    "any": any,
    "all": all,
    "list": list,
    "tuple": tuple,
    "set": set,
    "dict": dict,
}

_ALLOWED_BINOPS = (ast.Add, ast.Sub, ast.Mult, ast.Div, ast.FloorDiv, ast.Mod)
_ALLOWED_UNARYOPS = (ast.UAdd, ast.USub, ast.Not)
_ALLOWED_CMPOPS = (ast.Eq, ast.NotEq, ast.Lt, ast.LtE, ast.Gt, ast.GtE,
                   ast.In, ast.NotIn, ast.Is, ast.IsNot)


def _validate_target(node, allowed_names):
    """校验推导式目标，并把新建绑定的名字加入局部作用域。"""
    if isinstance(node, ast.Name):
        if node.id.startswith("_"):
            raise StrategyError(f"name not allowed in expression: {node.id}")
        allowed_names.add(node.id)
    elif isinstance(node, (ast.Tuple, ast.List)):
        for elt in node.elts:
            _validate_target(elt, allowed_names)
    elif isinstance(node, ast.Starred):
        _validate_target(node.value, allowed_names)
    else:
        raise StrategyError("unsupported comprehension target")


def _validate_expr(node, allowed_names):
    if isinstance(node, ast.Expression):
        _validate_expr(node.body, allowed_names)
    elif isinstance(node, ast.Constant):
        pass
    elif isinstance(node, ast.Name):
        if node.id not in allowed_names:
            raise StrategyError(f"name not allowed in expression: {node.id}")
    elif isinstance(node, (ast.List, ast.Tuple, ast.Set)):
        for elt in node.elts:
            _validate_expr(elt, allowed_names)
    elif isinstance(node, ast.Starred):
        _validate_expr(node.value, allowed_names)
    elif isinstance(node, ast.Attribute):
        if node.attr.startswith("_"):
            raise StrategyError(f"attribute not allowed in expression: .{node.attr}")
        _validate_expr(node.value, allowed_names)
    elif isinstance(node, ast.JoinedStr):
        for value in node.values:
            _validate_expr(value, allowed_names)
    elif isinstance(node, ast.FormattedValue):
        _validate_expr(node.value, allowed_names)
        if node.format_spec is not None:
            _validate_expr(node.format_spec, allowed_names)
    elif isinstance(node, (ast.ListComp, ast.SetComp, ast.GeneratorExp)):
        local = set(allowed_names)
        for generator in node.generators:
            _validate_expr(generator.iter, local)
            _validate_target(generator.target, local)
            for cond in generator.ifs:
                _validate_expr(cond, local)
        _validate_expr(node.elt, local)
    elif isinstance(node, ast.DictComp):
        local = set(allowed_names)
        for generator in node.generators:
            _validate_expr(generator.iter, local)
            _validate_target(generator.target, local)
            for cond in generator.ifs:
                _validate_expr(cond, local)
        _validate_expr(node.key, local)
        _validate_expr(node.value, local)
    elif isinstance(node, ast.Dict):
        for k, v in zip(node.keys, node.values):
            if k is None:
                raise StrategyError("dict unpacking is not allowed")
            _validate_expr(k, allowed_names)
            _validate_expr(v, allowed_names)
    elif isinstance(node, ast.Subscript):
        _validate_expr(node.value, allowed_names)
        _validate_expr(node.slice, allowed_names)
    elif isinstance(node, ast.Slice):
        for part in (node.lower, node.upper, node.step):
            if part is not None:
                _validate_expr(part, allowed_names)
    elif isinstance(node, ast.BinOp):
        if not isinstance(node.op, _ALLOWED_BINOPS):
            raise StrategyError("operator not allowed in expression")
        _validate_expr(node.left, allowed_names)
        _validate_expr(node.right, allowed_names)
    elif isinstance(node, ast.UnaryOp):
        if not isinstance(node.op, _ALLOWED_UNARYOPS):
            raise StrategyError("unary operator not allowed in expression")
        _validate_expr(node.operand, allowed_names)
    elif isinstance(node, ast.BoolOp):
        for value in node.values:
            _validate_expr(value, allowed_names)
    elif isinstance(node, ast.Compare):
        _validate_expr(node.left, allowed_names)
        for op, comparator in zip(node.ops, node.comparators):
            if not isinstance(op, _ALLOWED_CMPOPS):
                raise StrategyError("comparison not allowed in expression")
            _validate_expr(comparator, allowed_names)
    elif isinstance(node, ast.IfExp):
        _validate_expr(node.test, allowed_names)
        _validate_expr(node.body, allowed_names)
        _validate_expr(node.orelse, allowed_names)
    elif isinstance(node, ast.Call):
        if isinstance(node.func, ast.Name):
            if node.func.id not in allowed_names:
                raise StrategyError(
                    f"function not allowed in expression: {node.func.id}"
                )
        elif isinstance(node.func, ast.Attribute):
            # 允许容器方法（x.get(...)、x.items()），但禁止下划线开头的属性
            if node.func.attr.startswith("_"):
                raise StrategyError(
                    f"attribute not allowed in expression: .{node.func.attr}"
                )
            _validate_expr(node.func.value, allowed_names)
        else:
            raise StrategyError("only whitelisted function calls are allowed")
        for arg in node.args:
            if isinstance(arg, ast.Starred):
                raise StrategyError("*args is not allowed")
            _validate_expr(arg, allowed_names)
        for kw in node.keywords:
            if kw.arg is None:
                raise StrategyError("**kwargs is not allowed")
            _validate_expr(kw.value, allowed_names)
    elif isinstance(node, (ast.Load, ast.Store)):
        pass
    else:
        raise StrategyError(
            f"unsupported expression node: {type(node).__name__}"
        )


def _normalize_bang(expr):
    """把 C 风格的 !x 改写成 Python 的 not x（不动 != 与字符串字面量内部）。"""
    if "!" not in expr:
        return expr
    out = []
    quote = None
    escaped = False
    for i, ch in enumerate(expr):
        if quote:
            out.append(ch)
            if escaped:
                escaped = False
            elif ch == "\\":
                escaped = True
            elif ch == quote:
                quote = None
            continue
        if ch in ("'", '"'):
            quote = ch
            out.append(ch)
        elif ch == "!" and expr[i + 1:i + 2] != "=":
            out.append("not ")
        else:
            out.append(ch)
    return "".join(out)


def eval_expr(expr, env, declared=None):
    """在受限环境中求值 DSL 表达式。非字符串按字面量返回。

    declared 是本策略中会被绑定的变量名集合（as / set name）。它们可能只在
    某个分支里绑定，因此允许出现在表达式里（例如被 and/or 短路保护），
    运行时若真的取到未绑定变量会抛出明确的 StrategyError。
    """
    if not isinstance(expr, str):
        return expr
    if len(expr) > 4000:
        raise StrategyError("expression is too long")
    raw = expr
    text = _normalize_bang(expr)
    try:
        tree = ast.parse(text, mode="eval")
    except SyntaxError as exc:
        raise StrategyError(f"invalid expression {raw!r}: {exc}") from exc
    allowed = set(env.keys()) | set(_SAFE_BUILTINS.keys())
    if declared:
        allowed |= set(declared)
    _validate_expr(tree, allowed)
    globals_ = {"__builtins__": {}}
    globals_.update(_SAFE_BUILTINS)
    globals_.update(env)
    try:
        return eval(compile(tree, "<strategy-expr>", "eval"), globals_, {})
    except Exception as exc:
        raise StrategyError(f"failed to evaluate {raw!r}: {exc}") from exc


# 纯单词（类型名 / 元素 ID / 标识符风格的字面量）：set 的 value 按字面量处理。
_PLAIN_WORD_RE = re.compile(r"^[A-Za-z_][A-Za-z0-9_.:#\-]*$")


def set_value(raw, env, declared=None):
    """解析 set 步骤的 value：纯单词按字面量，其余字符串按表达式求值。

    这样 "float"、"func:main#0" 保持字面量，而 "best['time']"、"[...]"、
    "'msg' + str(x)" 会求值；非字符串（JSON 数字/布尔/数组/对象）原样返回。
    """
    if isinstance(raw, str):
        text = raw.strip()
        if _PLAIN_WORD_RE.match(text):
            return raw
    return eval_expr(raw, env, declared)


def collect_declared_names(steps, out=None):
    """递归收集策略中会绑定的变量名（as / set name），用于静态名字检查。

    变量可能只在某个分支里绑定，另一分支的表达式可能通过 and/or 短路引用它；
    因此名字检查应基于“是否在本策略中声明过”，而不是“当前是否已绑定”。
    """
    if out is None:
        out = set()
    if not isinstance(steps, list):
        return out
    for step in steps:
        if not isinstance(step, dict):
            continue
        name = step.get("name")
        if isinstance(name, str) and name:
            out.add(name.split("[", 1)[0].strip())
        alias = step.get("as")
        if isinstance(alias, str) and alias:
            out.add(alias)
        for key in ("steps", "then", "else"):
            collect_declared_names(step.get(key), out)
    return out


def split_set_target(name):
    """解析 set 目标：'x' 或 'x[下标表达式]'（单层下标）。"""
    text = name.strip()
    if not text.endswith("]"):
        if not text.isidentifier():
            raise StrategyError(f"invalid set target: {name!r}")
        return text, None
    start = text.find("[")
    if start <= 0:
        raise StrategyError(f"invalid set target: {name!r}")
    base = text[:start].strip()
    if not base.isidentifier():
        raise StrategyError(f"invalid set target: {name!r}")
    return base, text[start + 1:-1]


# ---------------------------------------------------------------------------
# 元素 ID / 源码读取
# ---------------------------------------------------------------------------

def element_id(item, fallback_index=0):
    """为 change_set 元素生成稳定 ID。

    global#<gvar_index>
    func:<func>#<fvar_index>
    call:<caller>#<fcall_index>
    struct:<struct>#<member_index>
    """
    if "global" in item:
        return f"global#{item.get('gvar_index')}"
    if "func" in item:
        return f"func:{item.get('func')}#{item.get('fvar_index')}"
    if "caller" in item:
        return f"call:{item.get('caller')}#{item.get('fcall_index')}"
    if "struct" in item:
        member = item.get("member_index")
        if member is None:
            # config_utils.parse_config 对结构体成员使用 None 作为索引键
            member = item.get(None)
        if member is None:
            member = fallback_index
        return f"struct:{item.get('struct')}#{member}"
    return f"unknown#{fallback_index}"


def build_element_specs(change_set):
    specs = []
    for index, item in enumerate(change_set):
        eid = element_id(item, index)
        kind = "global" if "global" in item else (
            "local" if "func" in item else (
                "call" if "caller" in item else "struct"
            )
        )
        specs.append(ElementSpec(
            id=eid,
            kind=kind,
            name=str(item.get("name", "")),
            file=str(item.get("file", "") or ""),
            line=item.get("line"),
            column=item.get("column"),
            original_type=item.get("type", ""),
            search_types=list(item.get("search_types", []) or []),
        ))
    ids = [spec.id for spec in specs]
    if len(ids) != len(set(ids)):
        raise StrategyError("duplicated element id generated from change set")
    return specs


def collect_sources(specs, max_file_bytes=200000, max_total_bytes=1000000):
    """按元素中记录的源文件路径读取源码，用于启动时结构分析。"""
    paths = []
    for spec in specs:
        if spec.file and spec.file not in paths:
            paths.append(spec.file)
    sources = {}
    total = 0
    for path in paths:
        try:
            if not os.path.isfile(path):
                continue
            size = os.path.getsize(path)
            if size > max_file_bytes or total + size > max_total_bytes:
                continue
            with open(path, "r", errors="replace") as f:
                sources[path] = f.read()
            total += size
        except OSError:
            continue
    return sources, paths


# ---------------------------------------------------------------------------
# 执行器
# ---------------------------------------------------------------------------

class StrategyExecutor:
    def __init__(self, specs, evaluator, baseline_time, baseline_result,
                 threshold=1e-6, max_evals=0, telemetry_options=None,
                 adapter=None, allow_python=False, action_timeout=120.0,
                 max_steps=1000000, logger=print):
        self.specs = list(specs)
        self.spec_by_id = {spec.id: spec for spec in self.specs}
        self.evaluator = evaluator
        self.threshold = threshold
        self.max_evals = int(max_evals or 0)
        self.telemetry_options = telemetry_options or telemetry.TelemetryOptions()
        self.adapter = adapter or agent_adapter.NullAdapter()
        self.allow_python = allow_python
        self.action_timeout = action_timeout
        self.max_steps = int(max_steps)
        self.steps = 0
        self.log = logger

        self.original_assignment = {
            spec.id: spec.original_type for spec in self.specs
        }
        self.baseline_record = EvalRecord(
            run_id=1,
            assignment=dict(self.original_assignment),
            accepted=True,
            status=telemetry.STATUS_BASELINE,
            time=baseline_time,
            result=baseline_result,
            rel_err=0.0,
        )
        self.best = self.baseline_record
        self.history = [self.baseline_record]
        self.record_dicts = [self.baseline_record.to_dict()]
        self.evals = 0
        self.last_record = None
        self.dsl_vars = {}
        # 策略里声明过的变量名（运行时可能尚未绑定，见 collect_declared_names）
        self.declared_names = set()
        self._fire_eval = 0
        self._fire_best_run_id = self.best.run_id
        self._last_report = None
        self._structure_advice = None
        # 检查点通过 adjustment.extra_steps 追加的步骤（顶层队列末尾）
        self._extra_steps = []

    # -- 基础配置 ---------------------------------------------------------

    def base_assignment(self, base):
        if base in (None, "", "original", "default"):
            return dict(self.original_assignment)
        if base == "best":
            return dict(self.best.assignment)
        if base == "last":
            if self.last_record is not None:
                return dict(self.last_record.assignment)
            return dict(self.original_assignment)
        # base 也可以是变量名：指向一条 eval 记录（取其 assignment）
        # 或直接指向一个 assignment 字典
        if isinstance(base, str) and base in self.dsl_vars:
            value = self.dsl_vars[base]
            if isinstance(value, dict):
                assignment = value.get("assignment", value)
                if isinstance(assignment, dict) and all(
                    k in self.spec_by_id for k in assignment
                ):
                    return dict(assignment)
        raise StrategyError(
            f"unknown base {base!r} (allowed: original/best/last/记录变量名)"
        )

    def _normalize_assignment(self, assignment):
        if assignment is None:
            assignment = {}
        if not isinstance(assignment, dict):
            raise StrategyError("assignment must be an object mapping element id to type")
        full = dict(self.original_assignment)
        for eid, type_name in assignment.items():
            spec = self.spec_by_id.get(eid)
            if spec is None:
                raise StrategyError(f"unknown element id: {eid}")
            if not isinstance(type_name, str):
                raise StrategyError(f"type for {eid} must be a string")
            if type_name not in spec.search_types:
                raise StrategyError(
                    f"type {type_name!r} is not allowed for {eid}; "
                    f"allowed: {spec.search_types}"
                )
            full[eid] = type_name
        return full

    def evaluate_with_base(self, base, overrides, note=""):
        assignment = self.base_assignment(base)
        if overrides:
            if not isinstance(overrides, dict):
                raise StrategyError("assign must be an object")
            normalized = self._normalize_assignment(overrides)
            # 只把 overrides 应用到 base 上；base 中未覆盖的元素保持 base 的值
            for eid, type_name in normalized.items():
                if eid in overrides:
                    assignment[eid] = type_name
        return self.evaluate(assignment, note=note)

    def evaluate(self, assignment, note=""):
        if self.max_evals > 0 and self.evals >= self.max_evals:
            raise MaxEvalsReached()
        full = self._normalize_assignment(assignment)
        record = self.evaluator.evaluate(full)
        if not isinstance(record, EvalRecord):
            raise StrategyError("evaluator must return an EvalRecord")
        record.assignment = full
        record.note = note
        record.best = False
        self.last_record = record
        if record.cached:
            return record

        self.evals += 1
        if (record.accepted and _is_finite_number(record.time)
                and record.time < self.best.time):
            self.best = record
            record.best = True

        self.history.append(record)
        self.record_dicts.append(record.to_dict())
        self._maybe_checkpoint()
        return record

    # -- 遥测 / Agent -----------------------------------------------------

    # LLM 在检查点可以施加的调整（白名单；未知键直接报错，不静默忽略）
    ADJUSTMENT_KEYS = ("stop", "max_evals", "telemetry_interval", "note",
                       "extra_steps")

    def _apply_adjustment(self, adjustment):
        """执行检查点给出的 adjustment，让它真正影响后续搜索。"""
        if not adjustment:
            return
        if not isinstance(adjustment, dict):
            raise StrategyError(
                "checkpoint adjustment must be an object, got "
                f"{type(adjustment).__name__}"
            )
        unknown = sorted(set(adjustment) - set(self.ADJUSTMENT_KEYS))
        if unknown:
            raise StrategyError(
                f"unknown checkpoint adjustment key(s): {unknown}; "
                f"allowed: {list(self.ADJUSTMENT_KEYS)}"
            )

        applied = []
        if "max_evals" in adjustment:
            value = adjustment["max_evals"]
            if not isinstance(value, int) or isinstance(value, bool) or value <= 0:
                raise StrategyError(
                    f"adjustment max_evals must be a positive integer, got {value!r}"
                )
            if value < self.evals:
                raise StrategyError(
                    f"adjustment max_evals={value} is below current eval count "
                    f"{self.evals}"
                )
            self.max_evals = value
            applied.append(f"max_evals={value}")

        if "telemetry_interval" in adjustment:
            value = adjustment["telemetry_interval"]
            if not isinstance(value, int) or isinstance(value, bool) or value <= 0:
                raise StrategyError(
                    "adjustment telemetry_interval must be a positive integer, "
                    f"got {value!r}"
                )
            self.telemetry_options.interval = value
            applied.append(f"telemetry_interval={value}")

        if adjustment.get("note"):
            applied.append(f"note={str(adjustment['note'])[:120]}")

        if "extra_steps" in adjustment:
            raw = adjustment["extra_steps"]
            if not isinstance(raw, str) or not raw.strip():
                raise StrategyError(
                    "adjustment extra_steps 必须是 NDJSON 字符串（每行一个步骤对象）"
                )
            try:
                parsed = agent_adapter.parse_jsonl_strategy(raw)
            except ValueError as exc:
                raise StrategyError(
                    f"adjustment extra_steps 解析失败: {exc}"
                ) from exc
            steps = parsed["steps"]
            # 注入步骤里声明的变量名纳入静态白名单（支持分支内绑定）
            self.declared_names |= collect_declared_names(steps)
            self._extra_steps.extend(steps)
            applied.append(f"extra_steps={len(steps)} 个步骤")

        if applied:
            self.log(">>> Strategy adjustment: " + ", ".join(applied))

        if adjustment.get("stop"):
            self.log(">>> Strategy adjustment: stop requested by agent")
            raise _Done("agent-stop")

    def _maybe_checkpoint(self):
        reasons = telemetry.should_fire(
            self.history, self.evals, self.best.run_id,
            last_fire_eval=self._fire_eval,
            last_fire_best_run_id=self._fire_best_run_id,
            options=self.telemetry_options,
        )
        if not reasons:
            return
        self._fire_eval = self.evals
        self._fire_best_run_id = self.best.run_id
        digest = self.analyze_telemetry()
        self._last_report = digest
        self.log(
            f">>> Strategy checkpoint ({', '.join(reasons)}): "
            f"{digest.summary()}"
        )
        requests = agent_adapter.CheckpointRequest(
            digest=digest,
            records=self.record_dicts[-10:],
            elements=[spec.to_dict() for spec in self.specs],
            best=self.best.to_dict(),
            eval_count=self.evals,
            max_evals=self.max_evals,
            reasons=reasons,
        )
        # 适配器失败直接暴露（抛出异常），不做降级
        advice = self.adapter.analyze_checkpoint(requests)
        if advice:
            self.log(f">>> Strategy advice: {advice.get('advice', '')}")
            self._apply_adjustment(advice.get("adjustment"))

    def analyze_telemetry(self):
        return telemetry.analyze(
            self.history, self.evals, self.best.run_id,
            options=self.telemetry_options, threshold=self.threshold,
            specs=self.specs,
        )

    def run_structure_analysis(self, config_path="", feedback=""):
        if not getattr(self.adapter, "supports_structure", False):
            return None
        sources, paths = collect_sources(self.specs)
        request = agent_adapter.StructureRequest(
            sources=sources,
            elements=[spec.to_dict() for spec in self.specs],
            threshold=self.threshold,
            config_path=config_path,
            source_files=paths,
            eval_budget=self.max_evals,
            baseline_time=self.baseline_record.time,
            baseline_result=self.baseline_record.result,
            feedback=feedback,
        )
        # 适配器失败直接暴露（抛出异常），不做降级
        advice = self.adapter.analyze_structure(request)
        if advice:
            self._structure_advice = advice
            self.report_structure_advice(advice)
        return advice

    def report_structure_advice(self, advice):
        """把结构分析结论与生成策略的形态打印成可读输出。"""
        text = (advice or {}).get("advice", "")
        if text:
            for line in _wrap(text, 76):
                self.log(f">>> LLM 结构分析: {line}")
        strategy = (advice or {}).get("strategy") or {}
        steps = strategy.get("steps")
        if isinstance(steps, list):
            ops = [step.get("op", "?") for step in steps if isinstance(step, dict)]
            self.log(
                f">>> LLM 策略: {len(steps)} 个顶层步骤 [{', '.join(ops)}]"
            )

    # -- 运行入口 ---------------------------------------------------------

    def run_file(self, strategy_path, strategy_format="auto"):
        # 结构分析由调用方（strategy_algorithm）在需要时显式触发，
        # 避免与生成的策略重复调用 LLM。
        fmt = (strategy_format or "auto").lower()
        if fmt == "auto":
            suffix = os.path.splitext(strategy_path)[1].lower()
            if suffix == ".py":
                fmt = "python"
            elif suffix == ".jsonl":
                fmt = "jsonl"
            else:
                fmt = "dsl"
        if fmt == "dsl":
            with open(strategy_path, "r") as f:
                try:
                    strategy = json.load(f)
                except json.JSONDecodeError as exc:
                    raise StrategyError(f"invalid strategy JSON: {exc}") from exc
            reason = self.run_dsl(strategy)
        elif fmt == "jsonl":
            with open(strategy_path, "r") as f:
                text = f.read()
            try:
                strategy = agent_adapter.parse_jsonl_strategy(text)
            except ValueError as exc:
                raise StrategyError(f"invalid jsonl strategy: {exc}") from exc
            reason = self.run_dsl(strategy)
        elif fmt == "python":
            if not self.allow_python:
                raise StrategyError(
                    "Python strategy execution is disabled; "
                    "pass -allow-generated-strategy to enable it"
                )
            reason = self.run_python(strategy_path)
        else:
            raise StrategyError(f"unknown strategy format: {strategy_format}")
        return SearchOutcome(best=self.best, evals=self.evals,
                             reason=reason, history=self.history)

    # -- Tier A: JSON DSL -------------------------------------------------

    def build_env(self):
        def element_ids(kind=None):
            return [spec.id for spec in self.specs
                    if kind is None or spec.kind == kind]

        def types_of(eid):
            spec = self.spec_by_id.get(eid)
            if spec is None:
                raise StrategyError(f"unknown element id: {eid}")
            return list(spec.search_types)

        def count_status(status):
            return sum(1 for r in self.history if r.status == status)

        def count_result(value):
            text = str(value)
            return sum(1 for r in self.history if str(r.result) == text)

        def last_records(n):
            return self.record_dicts[-int(n):] if n else []

        def improvement_age():
            for i, r in enumerate(self.history):
                if r.run_id == self.best.run_id:
                    return self.evals - i
            return self.evals

        env = {
            "elements": [spec.to_dict() for spec in self.specs],
            "history": self.record_dicts,
            "best": self.best.to_dict(),
            "baseline": self.baseline_record.to_dict(),
            "evals": self.evals,
            "max_evals": self.max_evals,
            "threshold": self.threshold,
            "vars": self.dsl_vars,
            "last": self.last_record.to_dict() if self.last_record else None,
            "element_ids": element_ids,
            "types_of": types_of,
            "original_assignment": lambda: dict(self.original_assignment),
            "best_assignment": lambda: dict(self.best.assignment),
            "count_status": count_status,
            "count_result": count_result,
            "last_records": last_records,
            "improvement_age": improvement_age,
        }
        # 用户变量同时以顶层名字暴露，方便写 "r['accepted']"；
        # 不覆盖内置环境名（elements/history/best/...）。
        for name, value in self.dsl_vars.items():
            if name not in env:
                env[name] = value
        return env

    def run_dsl(self, strategy):
        if not isinstance(strategy, dict):
            raise StrategyError("strategy JSON must be an object")
        version = strategy.get("version", 1)
        if version != 1:
            raise StrategyError(f"unsupported strategy version: {version}")
        steps = strategy.get("steps")
        if not isinstance(steps, list):
            raise StrategyError("strategy must contain a 'steps' list")
        init = strategy.get("init") or {}
        if not isinstance(init, dict):
            raise StrategyError("strategy 'init' must be an object")
        self.dsl_vars = dict(init)
        self.declared_names = collect_declared_names(steps) | set(init)
        self._extra_steps = []
        try:
            # 顶层步骤走队列：检查点可通过 adjustment.extra_steps 追加步骤
            pending = list(steps)
            while pending:
                step = pending.pop(0)
                if not isinstance(step, dict):
                    raise StrategyError("each step must be an object")
                self._run_step(step, self.build_env())
                if self._extra_steps:
                    pending.extend(self._extra_steps)
                    self._extra_steps = []
        except MaxEvalsReached:
            return "max-evals"
        except _Done as done:
            return done.reason
        except _BreakLoop:
            raise StrategyError("'break' used outside a loop")
        return "completed"

    def _run_steps(self, steps):
        if not isinstance(steps, list):
            raise StrategyError("steps must be a list")
        for step in steps:
            if not isinstance(step, dict):
                raise StrategyError("each step must be an object")
            env = self.build_env()
            self._run_step(step, env)

    def _expr(self, expr, env):
        """在表达式求值中带上本策略声明过的变量名（支持分支内绑定+短路引用）。"""
        return eval_expr(expr, env, self.declared_names)

    def _run_step(self, step, env):
        self.steps += 1
        if self.steps > self.max_steps:
            raise StrategyError(
                f"strategy exceeded step budget ({self.max_steps} steps)"
            )
        op = step.get("op")
        if op == "eval":
            self._step_eval(step, env)
        elif op == "eval_uniform":
            self._step_eval_uniform(step, env)
        elif op == "set":
            name = step.get("name")
            if not isinstance(name, str) or not name:
                raise StrategyError("'set' step requires a name")
            if "expr" in step:
                value = self._expr(step["expr"], env)
            else:
                value = set_value(step.get("value"), env, self.declared_names)
            base, index_expr = split_set_target(name)
            if index_expr is None:
                self.dsl_vars[base] = value
            elif base not in self.dsl_vars:
                raise StrategyError(
                    f"cannot assign {name!r}: unknown variable {base!r}"
                )
            else:
                container = self.dsl_vars[base]
                key = self._expr(index_expr, env)
                try:
                    container[key] = value
                except (TypeError, KeyError, IndexError) as exc:
                    raise StrategyError(f"cannot assign {name!r}: {exc}") from exc
        elif op == "foreach":
            self._step_foreach(step, env)
        elif op == "for":
            self._step_for(step, env)
        elif op == "while":
            self._step_while(step, env)
        elif op == "if":
            if bool(self._expr(step.get("when"), env)):
                self._run_steps(step.get("then") or [])
            else:
                self._run_steps(step.get("else") or [])
        elif op == "log":
            message = self._expr(step.get("message", ""), env)
            self.log(f">>> Strategy: {message}")
        elif op == "break":
            raise _BreakLoop()
        elif op == "done":
            reason = step.get("reason", "done")
            if isinstance(reason, str) and ("$" in reason):
                reason = self._expr(reason, env)
            raise _Done(str(reason))
        else:
            raise StrategyError(f"unknown strategy step op: {op!r}")

    def _is_known_type(self, name):
        return any(name in spec.search_types for spec in self.specs)

    def _eval_pair_value(self, raw, env, is_literal):
        """pairs 条目：字面量（已知元素 ID / 类型名）优先，否则按表达式求值。"""
        if isinstance(raw, str) and is_literal(raw):
            return raw
        value = self._expr(raw, env)
        if not isinstance(value, str):
            raise StrategyError(f"eval pair entry must be a string, got {raw!r}")
        return value

    def _step_eval(self, step, env):
        overrides = dict(step.get("assign") or {})
        pairs = step.get("pairs") or []
        if isinstance(pairs, str):
            # pairs 也可以是表达式字符串，求值后须为 [[元素, 类型], ...]
            pairs = self._expr(pairs, env)
        if not isinstance(pairs, (list, tuple)):
            raise StrategyError("'pairs' must be a list or an expression evaluating to one")
        for pair in pairs:
            if not isinstance(pair, (list, tuple)) or len(pair) != 2:
                raise StrategyError("'pairs' entries must be [element_expr, type_expr]")
            eid = self._eval_pair_value(
                pair[0], env, lambda v: v in self.spec_by_id
            )
            type_name = self._eval_pair_value(
                pair[1], env, self._is_known_type
            )
            overrides[eid] = type_name
        record = self.evaluate_with_base(
            step.get("base", "original"), overrides,
            note=str(step.get("note", "")),
        )
        if "as" in step:
            self.dsl_vars[step["as"]] = record.to_dict()
        return record

    def _step_eval_uniform(self, step, env):
        if "type_expr" in step:
            type_name = self._expr(step["type_expr"], env)
        else:
            type_name = step.get("type")
        if not isinstance(type_name, str):
            raise StrategyError("eval_uniform requires a type string")
        assignment = self.base_assignment(step.get("base", "original"))
        for spec in self.specs:
            if type_name in spec.search_types:
                assignment[spec.id] = type_name
        record = self.evaluate(assignment, note=str(step.get("note", "")))
        if "as" in step:
            self.dsl_vars[step["as"]] = record.to_dict()
        return record

    def _step_foreach(self, step, env):
        items = self._expr(step.get("items"), env)
        if not isinstance(items, (list, tuple, set)):
            raise StrategyError("'foreach' items must evaluate to a list")
        name = step.get("as")
        if not isinstance(name, str) or not name:
            raise StrategyError("'foreach' step requires 'as'")
        substeps = step.get("steps")
        if not isinstance(substeps, list):
            raise StrategyError("'foreach' step requires 'steps'")
        if len(items) > 100000:
            raise StrategyError("'foreach' has too many items")
        for item in items:
            self.dsl_vars[name] = item
            try:
                self._run_steps(substeps)
            except _BreakLoop:
                break

    def _step_for(self, step, env):
        start = self._expr(step.get("start", 0), env)
        stop = self._expr(step.get("stop"), env)
        step_by = self._expr(step.get("step", 1), env)
        if not isinstance(start, int) or not isinstance(stop, int) \
                or not isinstance(step_by, int) or step_by == 0:
            raise StrategyError("'for' requires integer start/stop/step")
        name = step.get("as")
        if not isinstance(name, str) or not name:
            raise StrategyError("'for' step requires 'as'")
        substeps = step.get("steps")
        if not isinstance(substeps, list):
            raise StrategyError("'for' step requires 'steps'")
        if abs(stop - start) // abs(step_by) > 100000:
            raise StrategyError("'for' has too many iterations")
        for i in range(start, stop, step_by):
            self.dsl_vars[name] = i
            try:
                self._run_steps(substeps)
            except _BreakLoop:
                break

    def _step_while(self, step, env):
        condition = step.get("when")
        substeps = step.get("steps")
        if not isinstance(substeps, list):
            raise StrategyError("'while' step requires 'steps'")
        max_iterations = int(step.get("max_iterations", 100))
        if max_iterations <= 0 or max_iterations > 100000:
            raise StrategyError("'while' max_iterations out of range")
        for _ in range(max_iterations):
            if not bool(self._expr(condition, self.build_env())):
                return
            try:
                self._run_steps(substeps)
            except _BreakLoop:
                return

    # -- Tier B: Python 子进程 -------------------------------------------

    def run_python(self, strategy_path):
        sandbox = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                               "strategy_sandbox.py")
        if not os.path.isfile(sandbox):
            raise StrategyError(f"strategy sandbox not found: {sandbox}")

        workdir = tempfile.mkdtemp(prefix="perci-strategy-")
        env = {
            "PATH": os.environ.get("PATH", "/usr/bin:/bin"),
            "HOME": workdir,
            "TMPDIR": workdir,
            "PYTHONHASHSEED": "0",
        }
        cmd = [sys.executable, "-I", "-B", sandbox,
               os.path.abspath(strategy_path)]
        proc = subprocess.Popen(
            cmd,
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=None,
            cwd=workdir,
            env=env,
            text=True,
            bufsize=1,
        )
        lines = queue.Queue()

        def _reader():
            try:
                for line in proc.stdout:
                    lines.put(line)
            finally:
                lines.put(None)

        reader = threading.Thread(target=_reader, daemon=True)
        reader.start()

        ctx = self.build_env()
        ctx.pop("history", None)  # Python 策略通过 history 动作按需获取
        ctx.pop("last", None)
        ctx.pop("vars", None)
        # 去掉不可序列化的 helper 函数
        ctx = {k: v for k, v in ctx.items() if not callable(v)}

        reason = "completed"
        try:
            self._send_json(proc, ctx)
            while True:
                line = self._read_line(lines)
                if line is None:
                    reason = "eof"
                    break
                try:
                    action = json.loads(line)
                except json.JSONDecodeError:
                    raise StrategyError(f"invalid action from strategy: {line!r}")
                op = action.get("action")
                if op == "done":
                    reason = str(action.get("reason", "done"))
                    self.log(f">>> Strategy: {reason}")
                    break
                if op == "error":
                    raise StrategyError(
                        f"strategy error: {action.get('message', 'unknown')}"
                    )
                try:
                    response = self.handle_action(action)
                except MaxEvalsReached:
                    self._send_json(proc, {
                        "type": "error",
                        "message": "max evals reached",
                        "terminal": True,
                    })
                    reason = "max-evals"
                    break
                except _Done as done:
                    self._send_json(proc, {
                        "type": "error",
                        "message": f"search stopped: {done.reason}",
                        "terminal": True,
                    })
                    reason = done.reason
                    break
                except StrategyError as exc:
                    self._send_json(proc, {
                        "type": "error",
                        "message": str(exc),
                        "terminal": True,
                    })
                    raise
                self._send_json(proc, response)
        finally:
            self._shutdown_process(proc, workdir)
        return reason

    def handle_action(self, action):
        op = action.get("action")
        if op == "eval":
            overrides = action.get("assign") or {}
            record = self.evaluate_with_base(
                action.get("base", "original"),
                overrides,
                note=str(action.get("note", "")),
            )
            return {
                "type": "eval",
                "record": record.to_dict(),
                "best": self.best.to_dict(),
                "evals": self.evals,
            }
        if op == "history":
            try:
                since = int(action.get("since", 0))
            except (TypeError, ValueError):
                since = 0
            if since < 0:
                since = 0
            return {
                "type": "history",
                "records": self.record_dicts[since:],
                "best": self.best.to_dict(),
                "evals": self.evals,
            }
        if op == "ask":
            digest = self.analyze_telemetry()
            self._last_report = digest
            request = agent_adapter.CheckpointRequest(
                digest=digest,
                records=self.record_dicts[-10:],
                elements=[spec.to_dict() for spec in self.specs],
                best=self.best.to_dict(),
                eval_count=self.evals,
                max_evals=self.max_evals,
                reasons=["strategy-request"],
            )
            advice = self.adapter.analyze_checkpoint(request)
            self._apply_adjustment((advice or {}).get("adjustment"))
            return {
                "type": "ask",
                "advice": json_safe(advice),
                "digest": digest.to_dict(),
            }
        if op == "log":
            self.log(f">>> Strategy: {action.get('message', '')}")
            return {"type": "log", "ok": True}
        raise StrategyError(f"unknown strategy action: {op!r}")

    @staticmethod
    def _send_json(proc, payload):
        if proc.stdin is None or proc.stdin.closed:
            raise StrategyError("strategy process is not writable")
        proc.stdin.write(json.dumps(json_safe(payload)) + "\n")
        proc.stdin.flush()

    def _read_line(self, lines):
        try:
            line = lines.get(timeout=self.action_timeout)
        except queue.Empty:
            raise StrategyError(
                f"strategy did not respond within {self.action_timeout}s"
            )
        return line

    @staticmethod
    def _shutdown_process(proc, workdir):
        try:
            if proc.stdin and not proc.stdin.closed:
                proc.stdin.close()
        except OSError:
            pass
        if proc.poll() is None:
            proc.terminate()
            try:
                proc.wait(timeout=5)
            except subprocess.TimeoutExpired:
                proc.kill()
                proc.wait()
        try:
            if proc.stdout is not None:
                proc.stdout.close()
        except OSError:
            pass
        try:
            shutil.rmtree(workdir, ignore_errors=True)
        except OSError:
            pass
