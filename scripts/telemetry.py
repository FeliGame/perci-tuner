#!/usr/bin/env python3
# 搜索过程遥测：把动态搜索期间的 run_history 汇总成结构化摘要（digest）。
#
# 定位：静态结构分析只能看到源码形状，看不到“某个降级到底快不快、准不准”。
# 遥测负责把实际执行数据（运行时间、混精配置、数值结果、接受/拒绝、趋势）
# 汇总成 LLM 能直接阅读的摘要，作为搜索中途检查点的决策依据。
# 异常（重复输出、超时、运行错误、异常值、停滞）只是摘要里的一个字段。
#
# 该模块不依赖 LLVM / 项目其它脚本，便于单元测试。

import math
from dataclasses import dataclass, field


# 评估状态（由 strategy_algorithm.ProductionEvaluator 写入）
STATUS_OK = "OK"
STATUS_THRESHOLD = "THRESHOLD"
STATUS_PASS_ERROR = "PASS-ERROR"
STATUS_RUN_ERROR = "RUN-ERROR"
STATUS_NO_TIME = "NO-TIME"
STATUS_NO_RESULT = "NO-RESULT"
STATUS_NO_OUTPUT = "NO-OUTPUT"
STATUS_BASELINE = "BASELINE"

# 视为“程序正常跑完”的状态
NORMAL_STATUS = (STATUS_OK, STATUS_THRESHOLD, STATUS_BASELINE)
# 视为“运行失败”的状态
FAILURE_STATUS = (
    STATUS_PASS_ERROR,
    STATUS_RUN_ERROR,
    STATUS_NO_TIME,
    STATUS_NO_RESULT,
    STATUS_NO_OUTPUT,
)

# 摘要里最多列出的条目数（避免 prompt 过长）
MAX_ELEMENTS = 12
MAX_WORST_RUNS = 5
MAX_TREND = 10


@dataclass
class TelemetryOptions:
    # 需要重复出现多少次才算“重复输出”
    duplicate_min: int = 2
    # 运行时间超过基线的多少倍视为“过长时间运行”
    long_run_factor: float = 3.0
    # 连续多少次评估未刷新最优解视为“停滞”
    stagnation_window: int = 10
    # 连续多少次失败触发检查点
    failure_streak: int = 3
    # 固定间隔检查点（按评估次数）
    interval: int = 25
    # 相对误差超过阈值的多少倍视为“异常误差”
    abnormal_rel_err_factor: float = 10.0


@dataclass
class Finding:
    code: str
    severity: str  # info / warning / error
    message: str
    run_ids: list = field(default_factory=list)

    def to_dict(self):
        return {
            "code": self.code,
            "severity": self.severity,
            "message": self.message,
            "run_ids": list(self.run_ids),
        }


def _is_finite(value):
    return isinstance(value, (int, float)) and not isinstance(value, bool) \
        and math.isfinite(value)


def _median(values):
    if not values:
        return None
    ordered = sorted(values)
    mid = len(ordered) // 2
    if len(ordered) % 2:
        return ordered[mid]
    return (ordered[mid - 1] + ordered[mid]) / 2.0


def _round(value, digits=6):
    if not _is_finite(value):
        return value
    return round(value, digits)


@dataclass
class SearchDigest:
    """动态搜索数据的结构化摘要。"""
    eval_count: int
    best_run_id: int
    baseline: dict = field(default_factory=dict)
    best: dict = field(default_factory=dict)
    time_stats: dict = field(default_factory=dict)
    status_counts: dict = field(default_factory=dict)
    acceptance: dict = field(default_factory=dict)
    precision: dict = field(default_factory=dict)
    numeric: dict = field(default_factory=dict)
    trend: dict = field(default_factory=dict)
    anomalies: list = field(default_factory=list)

    @property
    def has_anomalies(self):
        return bool(self.anomalies)

    def to_dict(self):
        return {
            "eval_count": self.eval_count,
            "best_run_id": self.best_run_id,
            "baseline": dict(self.baseline),
            "best": dict(self.best),
            "time_stats": dict(self.time_stats),
            "status_counts": dict(self.status_counts),
            "acceptance": dict(self.acceptance),
            "precision": dict(self.precision),
            "numeric": dict(self.numeric),
            "trend": dict(self.trend),
            "anomalies": [f.to_dict() for f in self.anomalies],
            "summary": self.summary(),
        }

    def summary(self):
        """一行式概览（日志用）。"""
        accepted = self.acceptance.get("accepted", 0)
        total = self.acceptance.get("total", 0)
        speedup = self.time_stats.get("speedup_vs_baseline")
        best_time = self.best.get("time")
        parts = [
            f"{self.eval_count} evals",
            f"accepted {accepted}/{total}",
            f"best run {self.best_run_id} @ {_fmt_num(best_time)}s",
        ]
        if speedup is not None:
            parts.append(f"{speedup:.3f}x baseline")
        if self.anomalies:
            codes = ", ".join(sorted({f.code for f in self.anomalies}))
            parts.append(f"anomalies: {codes}")
        else:
            parts.append("no anomaly")
        return ", ".join(parts)

    def to_prompt(self):
        """给 LLM 的紧凑文本摘要。"""
        lines = [f"评估次数 {self.eval_count}，最优 run {self.best_run_id}",
                 f"基准: {_fmt_num(self.baseline.get('time'))}s, "
                 f"result={_short(self.baseline.get('result'))}",
                 f"最优: time={_fmt_num(self.best.get('time'))}s, "
                 f"rel_err={_fmt_num(self.best.get('rel_err'))}, "
                 f"配置差异 {_short(self.best.get('assignment'))}"]

        acc = self.acceptance
        lines.append(
            f"接受率: {acc.get('accepted', 0)}/{acc.get('total', 0)}"
            f" (rejected {acc.get('rejected', 0)})"
        )
        if self.status_counts:
            lines.append("状态分布: " + ", ".join(
                f"{k}={v}" for k, v in sorted(self.status_counts.items())))

        ts = self.time_stats
        lines.append(
            "时间: accepted min={} med={} max={}s；相对基准 {}".format(
                _fmt_num(ts.get("min")), _fmt_num(ts.get("median")),
                _fmt_num(ts.get("max")),
                _fmt_ratio(ts.get("speedup_vs_baseline")),
            )
        )

        num = self.numeric
        if num.get("count"):
            lines.append(
                "数值误差: min={} med={} max={}；超阈值 {} 次".format(
                    _fmt_num(num.get("min")), _fmt_num(num.get("median")),
                    _fmt_num(num.get("max")), num.get("over_threshold", 0),
                )
            )
            if num.get("worst"):
                worst = ", ".join(
                    f"run {item['run_id']} ({_fmt_num(item['rel_err'])})"
                    for item in num["worst"]
                )
                lines.append(f"误差最大的评估: {worst}")

        precision = self.precision
        if precision.get("elements"):
            lines.append(
                f"配置变化: {precision.get('changed_elements', 0)} 个元素被降级，"
                f"各元素实际选用精度（次数）:"
            )
            for item in precision["elements"]:
                lines.append(
                    f"  - {item['id']} ({item['original_type']}): "
                    + ", ".join(f"{t}x{n}" for t, n in item["types"].items())
                )

        trend = self.trend
        if trend.get("improvements"):
            lines.append("刷新最优的评估: " + ", ".join(
                f"run {r['run_id']}={_fmt_num(r['time'])}s"
                for r in trend["improvements"]))
        lines.append(
            f"近 {len(trend.get('recent_times', []))} 次时间: "
            + ", ".join(_fmt_num(t) for t in trend.get("recent_times", []))
            + f"；自最优以来 {trend.get('since_best', 0)} 次未改进"
        )

        if self.anomalies:
            lines.append("异常信号:")
            for f in self.anomalies:
                lines.append(f"  - [{f.severity}] {f.code}: {f.message}")
        else:
            lines.append("异常信号: 无")
        return "\n".join(lines)


def _fmt_num(value):
    if value is None:
        return "n/a"
    if isinstance(value, float) and (math.isnan(value) or math.isinf(value)):
        return str(value)
    if isinstance(value, float):
        return f"{value:.6g}"
    return str(value)


def _fmt_ratio(value):
    return "n/a" if value is None else f"{value:.3f}x"


def _short(value, limit=120):
    text = str(value)
    return text if len(text) <= limit else text[:limit] + "..."


def _is_non_finite_number(value):
    try:
        v = float(value)
    except (TypeError, ValueError):
        return False
    return math.isnan(v) or math.isinf(v)


def _result_is_abnormal(result):
    if result is None:
        return False
    text = str(result).strip().lower()
    if text in ("nan", "+nan", "-nan", "inf", "+inf", "-inf",
                "infinity", "-infinity"):
        return True
    return _is_non_finite_number(text)


def _record_assignment_key(record):
    return tuple(sorted((record.assignment or {}).items()))


# ---------------------------------------------------------------------------
# 摘要构建
# ---------------------------------------------------------------------------

def _precision_digest(search_records, specs):
    if not specs:
        return {}
    by_id = {spec.id: spec for spec in specs}
    counters = {}
    for record in search_records:
        for eid, type_name in (record.assignment or {}).items():
            if eid not in by_id:
                continue
            counters.setdefault(eid, {}).setdefault(type_name, 0)
            counters[eid][type_name] += 1

    elements = []
    changed = 0
    for eid, types in counters.items():
        spec = by_id[eid]
        original = spec.original_type
        others = {t: n for t, n in types.items() if t != original}
        if others:
            changed += 1
        ordered = dict(sorted(types.items(), key=lambda kv: (-kv[1], kv[0])))
        elements.append({
            "id": eid,
            "original_type": original,
            "types": ordered,
            "always_original": original in types and len(types) == 1,
        })
    elements.sort(key=lambda item: (-sum(item["types"].values()), item["id"]))
    return {"changed_elements": changed,
            "elements": elements[:MAX_ELEMENTS],
            "total_elements": len(by_id)}


def analyze(records, eval_count, best_run_id, options=None, threshold=None,
            specs=None):
    """汇总 run_history，返回 SearchDigest。

    records: list[EvalRecord]（包含 run_id 1 的 baseline 记录）
    specs: list[ElementSpec]，用于统计每个元素实际选用的精度
    """
    options = options or TelemetryOptions()
    search_records = [r for r in records if r.run_id != 1]

    baseline_record = next((r for r in records if r.run_id == 1), None)
    baseline_time = baseline_record.time if baseline_record else None
    if not _is_finite(baseline_time):
        baseline_time = None
    baseline_result = baseline_record.result if baseline_record else None

    best_record = next((r for r in records if r.run_id == best_run_id), None)
    best_time = best_record.time if best_record else None

    # -- 时间 / 接受率 ---------------------------------------------------
    status_counts = {}
    for record in search_records:
        status_counts[record.status] = status_counts.get(record.status, 0) + 1
    accepted_records = [r for r in search_records if r.accepted]
    finished_times = [r.time for r in accepted_records if _is_finite(r.time)]
    rejected = len(search_records) - len(accepted_records)

    time_stats = {
        "min": _round(min(finished_times)) if finished_times else None,
        "median": _round(_median(finished_times)),
        "max": _round(max(finished_times)) if finished_times else None,
        "count": len(finished_times),
        "speedup_vs_baseline": None,
    }
    if baseline_time and time_stats["median"] is not None:
        time_stats["speedup_vs_baseline"] = _round(
            baseline_time / time_stats["median"], 4)

    acceptance = {
        "total": len(search_records),
        "accepted": len(accepted_records),
        "rejected": rejected,
        "rate": _round(len(accepted_records) / len(search_records), 4)
        if search_records else None,
    }

    # -- 数值结果 --------------------------------------------------------
    rel_errors = [
        (r.run_id, float(r.rel_err)) for r in search_records
        if r.rel_err is not None and _is_finite(r.rel_err)
    ]
    numeric = {"count": len(rel_errors)}
    if rel_errors:
        values = [value for _, value in rel_errors]
        numeric.update({
            "min": _round(min(values)),
            "median": _round(_median(values)),
            "max": _round(max(values)),
            "over_threshold": sum(
                1 for value in values
                if threshold is not None and threshold > 0 and value > threshold
            ),
            "worst": [
                {"run_id": run_id, "rel_err": _round(value)}
                for run_id, value in sorted(
                    rel_errors, key=lambda item: -item[1])[:MAX_WORST_RUNS]
            ],
        })

    # -- 趋势 ------------------------------------------------------------
    improvements = []
    running_best = None
    for record in search_records:
        if record.best:
            improvements.append({
                "run_id": record.run_id,
                "time": _round(record.time),
            })
        elif record.accepted and _is_finite(record.time):
            if running_best is None or record.time < running_best:
                running_best = record.time
    since_best = 0
    for record in reversed(search_records):
        if record.run_id == best_run_id:
            break
        since_best += 1
    trend = {
        "recent_times": [_round(r.time) for r in search_records[-MAX_TREND:]
                         if _is_finite(r.time)],
        "improvements": improvements[:MAX_TREND],
        "since_best": since_best,
    }

    digest = SearchDigest(
        eval_count=eval_count,
        best_run_id=best_run_id,
        baseline={
            "time": _round(baseline_time),
            "result": baseline_result,
        },
        best={
            "run_id": best_run_id,
            "time": _round(best_time),
            "rel_err": _round(best_record.rel_err) if best_record else None,
            "assignment": dict(best_record.assignment or {}) if best_record else {},
        },
        time_stats=time_stats,
        status_counts=status_counts,
        acceptance=acceptance,
        precision=_precision_digest(search_records, specs),
        numeric=numeric,
        trend=trend,
    )

    digest.anomalies = _detect_anomalies(
        records, search_records, eval_count, best_run_id,
        baseline_time, options, threshold,
    )
    return digest


# ---------------------------------------------------------------------------
# 异常信号（摘要的一个字段）
# ---------------------------------------------------------------------------

def _detect_anomalies(records, search_records, eval_count, best_run_id,
                      baseline_time, options, threshold):
    findings = []

    # 1) 运行失败 / 超时
    for status in FAILURE_STATUS:
        ids = [r.run_id for r in search_records if r.status == status]
        if ids:
            severity = "error" if status in (STATUS_PASS_ERROR,
                                             STATUS_RUN_ERROR) else "warning"
            findings.append(Finding(
                code="failure:" + status.lower().replace("_", "-"),
                severity=severity,
                message=f"{len(ids)} eval(s) ended with {status}",
                run_ids=ids,
            ))

    # 2) 异常输出值：nan / inf / 非有限结果
    abnormal_ids = [r.run_id for r in search_records
                    if _result_is_abnormal(r.result)]
    if abnormal_ids:
        findings.append(Finding(
            code="abnormal-result",
            severity="warning",
            message=f"{len(abnormal_ids)} eval(s) produced nan/inf-like results",
            run_ids=abnormal_ids,
        ))

    # 3) 异常运行时间：负数 / 正常状态下的非有限时间
    bad_time_ids = []
    for r in search_records:
        t = r.time
        if not isinstance(t, (int, float)) or isinstance(t, bool):
            continue
        if t < 0 or (r.status == STATUS_OK and not math.isfinite(t)):
            bad_time_ids.append(r.run_id)
    if bad_time_ids:
        findings.append(Finding(
            code="abnormal-time",
            severity="warning",
            message=f"{len(bad_time_ids)} eval(s) reported negative/NaN runtime",
            run_ids=bad_time_ids,
        ))

    # 4) 重复的“失败输出”：很多配置得到相同错误结果，通常意味着瓶颈元素
    result_groups = {}
    for r in search_records:
        if r.status in (STATUS_OK, STATUS_BASELINE) or r.result is None:
            continue
        result_groups.setdefault(str(r.result), []).append(r.run_id)
    for result, ids in result_groups.items():
        if len(ids) >= options.duplicate_min:
            findings.append(Finding(
                code="duplicate-invalid-result",
                severity="warning",
                message=f"result {result!r} repeated by {len(ids)} invalid eval(s)",
                run_ids=ids,
            ))

    # 5) 重复配置（评估器通常已去重，这里兜底检测）
    assignment_groups = {}
    for r in search_records:
        assignment_groups.setdefault(_record_assignment_key(r), []).append(r.run_id)
    for _, ids in assignment_groups.items():
        if len(ids) >= options.duplicate_min:
            findings.append(Finding(
                code="duplicate-assignment",
                severity="info",
                message=f"same assignment evaluated {len(ids)} times",
                run_ids=ids,
            ))

    # 6) 过长时间运行
    if baseline_time and baseline_time > 0:
        long_ids = [
            r.run_id for r in search_records
            if r.status in (STATUS_OK, STATUS_THRESHOLD)
            and _is_finite(r.time)
            and r.time > baseline_time * options.long_run_factor
        ]
        if long_ids:
            findings.append(Finding(
                code="long-run",
                severity="warning",
                message=(
                    f"{len(long_ids)} eval(s) ran more than "
                    f"{options.long_run_factor}x baseline time"
                ),
                run_ids=long_ids,
            ))

    # 7) 相对误差异常
    if threshold is not None and threshold > 0:
        limit = threshold * options.abnormal_rel_err_factor
        bad_err_ids = [
            r.run_id for r in search_records
            if _is_finite(r.rel_err) and r.rel_err > limit
        ]
        if bad_err_ids:
            findings.append(Finding(
                code="large-relative-error",
                severity="warning",
                message=f"{len(bad_err_ids)} eval(s) exceeded {limit:g} relative error",
                run_ids=bad_err_ids,
            ))

    # 8) 停滞：连续多次评估没有刷新最优解
    if eval_count > 0:
        best_index = 0
        for i, r in enumerate(records):
            if r.run_id == best_run_id:
                best_index = i + 1
                break
        if eval_count - best_index >= options.stagnation_window:
            findings.append(Finding(
                code="stagnation",
                severity="info",
                message=(
                    f"{eval_count - best_index} eval(s) since last improvement "
                    f"(best run {best_run_id})"
                ),
                run_ids=[best_run_id],
            ))

    return findings


def should_fire(records, eval_count, best_run_id,
                last_fire_eval=0, last_fire_best_run_id=None,
                options=None):
    """判断是否应该触发一次检查点分析，返回触发原因列表（空表示不触发）。

    固定间隔是主触发条件（把这一段的搜索数据交给 LLM 分析）；
    连续失败 / 停滞只是提前触发的附加条件。
    """
    options = options or TelemetryOptions()
    reasons = []

    if eval_count - last_fire_eval >= options.interval:
        reasons.append("interval")

    if last_fire_best_run_id is not None and best_run_id == last_fire_best_run_id:
        if eval_count - last_fire_eval >= options.stagnation_window:
            reasons.append("stagnation")
    elif last_fire_best_run_id is None and eval_count >= options.stagnation_window:
        reasons.append("stagnation")

    recent = records[-options.failure_streak:]
    if len(recent) >= options.failure_streak and all(
        r.status not in NORMAL_STATUS for r in recent
    ):
        reasons.append("failure_streak")

    return reasons
