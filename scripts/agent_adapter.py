#!/usr/bin/env python3
# Agent 适配层：把“程序结构分析 / 检查点分析”抽象成可插拔接口。
#
# - NullAdapter：不调用任何 LLM（默认，完全确定性）。
# - OpencodeAdapter：通过 opencode 的 HTTP/SSE 接口调用 LLM，
#   启动时生成搜索策略（Tier A JSON DSL），检查点给出调整建议。
#   使用 SSE 事件流（session.text.delta / session.reasoning.delta）实时输出
#   生成中的 token，而不是等 LLM 返回后一次性打印。
#   任何失败（连接拒绝、超时、输出无法解析）都直接抛异常，不做降级。

import ast
import base64
import difflib
import json
import os
import queue
import re
import shutil
import socket
import string
import subprocess
import sys
import threading
import time
import urllib.error
import urllib.request
from dataclasses import dataclass, field


@dataclass
class StructureRequest:
    """启动时的一次性程序结构分析请求。"""
    sources: dict = field(default_factory=dict)  # path -> source text
    elements: list = field(default_factory=list)  # ElementSpec.to_dict()
    threshold: float = 1e-6
    config_path: str = ""
    source_files: list = field(default_factory=list)
    # 搜索预算与基准信息，供 LLM 判断“多少评估算合理”
    eval_budget: int = 0            # 0 表示不限
    baseline_time: float = 0.0
    baseline_result: object = None
    # 上一轮策略在执行期报的错误（重新生成时携带，要求 LLM 修正后输出新策略）
    feedback: str = ""


@dataclass
class CheckpointRequest:
    """搜索过程中检查点分析请求。"""
    digest: object = None  # telemetry.SearchDigest：run_history 数据摘要
    records: list = field(default_factory=list)  # 最近的评估记录
    elements: list = field(default_factory=list)
    best: dict = field(default_factory=dict)
    eval_count: int = 0
    max_evals: int = 0
    reasons: list = field(default_factory=list)


class AgentAdapter:
    """Agent 适配器基类。

    约定：
    - analyze_structure 返回 None 或 {"advice": str, "strategy": dict|None}
    - analyze_checkpoint 返回 None 或 {"advice": str, "adjustment": dict|None}
    - 适配器不得直接执行搜索；搜索始终由 StrategyExecutor 完成。
    """
    name = "base"
    # 是否需要读取源码（NullAdapter 为 False，避免无意义的磁盘 IO）
    supports_structure = True

    def analyze_structure(self, request):
        return None

    def analyze_checkpoint(self, request):
        return None

    def close(self):
        pass


class NullAdapter(AgentAdapter):
    """不调用 LLM 的空适配器，用于无 Agent 的确定性搜索。"""
    name = "none"
    supports_structure = False

    def analyze_structure(self, request):
        return None

    def analyze_checkpoint(self, request):
        return None


# ---------------------------------------------------------------------------
# 行式（NDJSON）策略解析/序列化
# ---------------------------------------------------------------------------

_BLOCK_OPS = ("if", "foreach", "for", "while")


def parse_jsonl_strategy(text):
    """把行式 NDJSON 策略解析为嵌套步骤树 {"version": 1, "steps": [...]}。

    每行一个 JSON 对象；if/foreach/for/while 开启块，{"op":"else"} 切换 if 分支，
    {"op":"end"} 闭合最内层块。空行与 # 开头的行忽略；
    首个非空行若是 {"advice": ...}（无 op）则作为注释头跳过。
    解析失败抛 ValueError（带行号），由调用方决定重试或终止。
    """
    root = []
    frames = []  # [[step, branch]]；branch: if→"then"/"else"，循环→"steps"
    seen_step = False
    for lineno, raw in enumerate(text.splitlines(), 1):
        line = raw.strip()
        if not line or line.startswith("#"):
            continue
        try:
            step = json.loads(line)
        except json.JSONDecodeError as exc:
            raise ValueError(
                f"line {lineno}: 不是合法 JSON 行（col {exc.colno}: {exc.msg}），"
                f"附近片段: {line[max(0, exc.pos - 40):exc.pos + 40]!r}"
            ) from exc
        if not isinstance(step, dict):
            raise ValueError(f"line {lineno}: 每行必须是 JSON 对象")
        if not isinstance(step.get("op"), str):
            if not seen_step and "advice" in step:
                continue  # 可选的 advice 头行
            raise ValueError(f"line {lineno}: 每行必须是带 \"op\" 的 JSON 对象")
        seen_step = True
        op = step["op"]
        if op == "end":
            if not frames:
                raise ValueError(f"line {lineno}: end 没有匹配的 if/foreach/for/while")
            frames.pop()
            continue
        if op == "else":
            if not frames or frames[-1][0].get("op") != "if":
                raise ValueError(f"line {lineno}: else 不在 if 块内")
            if frames[-1][1] != "then":
                raise ValueError(f"line {lineno}: 重复的 else")
            frames[-1][1] = "else"
            continue
        container = root
        for frame_step, branch in frames:
            container = frame_step[branch]
        container.append(step)
        if op in _BLOCK_OPS:
            if op == "if":
                step.setdefault("then", [])
                step.setdefault("else", [])
                frames.append([step, "then"])
            else:
                step.setdefault("steps", [])
                frames.append([step, "steps"])
    if frames:
        opens = ", ".join(s.get("op", "?") for s, _ in frames)
        raise ValueError(f"缺少 end 闭合块: {opens}")
    return {"version": 1, "steps": root}


def parse_structure_answer(text):
    """解析 LLM 结构分析输出：第 1 个 JSON 行为 {"advice": ...}，其余为 NDJSON 步骤。

    模型偶发地在正文前输出散文/思考文字：跳过无法解析为 JSON 对象的前导行；
    一旦进入 JSON 区域后，坏行仍然报错。失败抛 RuntimeError（消息回喂给 LLM 重试）。
    """
    if not text or not text.strip():
        raise RuntimeError("LLM 输出为空")
    lines = [l for l in text.splitlines()
             if l.strip() and not l.strip().startswith("```")]
    header = None
    start = 0
    for i, line in enumerate(lines):
        try:
            candidate = json.loads(line)
        except json.JSONDecodeError:
            continue  # 前导散文行，跳过
        if isinstance(candidate, dict):
            header, start = candidate, i
            break
    if header is None:
        raise RuntimeError(
            "输出中找不到任何 JSON 行；第 1 行必须是 {\"advice\": \"...\"}，"
            "其后每行一个步骤对象"
        )
    if "advice" not in header:
        raise RuntimeError(
            '首个 JSON 行必须是 {"advice": "..."} 形式（注意拼写 advice），实际: '
            + lines[start][:120]
        )
    if start > 0:
        print(f">>> Strategy agent: 跳过 {start} 行非 JSON 前导内容")
    try:
        strategy = parse_jsonl_strategy("\n".join(lines[start + 1:]))
    except ValueError as exc:
        raise RuntimeError(str(exc)) from exc
    if not strategy["steps"]:
        raise RuntimeError("advice 头之后没有任何策略步骤行")
    return {"advice": str(header["advice"])[:500], "strategy": strategy}


def _flatten_steps(steps, out):
    for step in steps:
        if not isinstance(step, dict):
            continue
        op = step.get("op")
        if op in ("foreach", "for", "while"):
            head = {k: v for k, v in step.items() if k != "steps"}
            out.append(json.dumps(head, ensure_ascii=False))
            _flatten_steps(step.get("steps") or [], out)
            out.append('{"op": "end"}')
        elif op == "if":
            head = {k: v for k, v in step.items() if k not in ("then", "else")}
            out.append(json.dumps(head, ensure_ascii=False))
            _flatten_steps(step.get("then") or [], out)
            if step.get("else"):
                out.append('{"op": "else"}')
                _flatten_steps(step["else"], out)
            out.append('{"op": "end"}')
        else:
            out.append(json.dumps(step, ensure_ascii=False))


def dump_jsonl_strategy(advice, strategy):
    """把 {"advice", strategy} 序列化回行式 NDJSON（用于落盘生成策略）。"""
    lines = [json.dumps({"advice": advice or ""}, ensure_ascii=False)]
    _flatten_steps((strategy or {}).get("steps") or [], lines)
    return "\n".join(lines) + "\n"


def _looks_expression(text):
    """字符串含表达式特征（括号/引号/运算符）时视为动态表达式，跳过静态校验。"""
    return any(c in text for c in "()[]'\"+-*/%")


def _id_like(text):
    """字符串看起来像元素 id（含 # 序号或 kind: 前缀）。"""
    return "#" in text or text.startswith(("global", "func:", "struct:", "call:"))


def _check_element_id(elem, allowed, where):
    """元素 id 必须是已知元素；未知 id（通常是 LLM 笔误）报错并给出最接近的合法 id。"""
    if elem in allowed or not isinstance(elem, str) or not _id_like(elem):
        return
    suggestions = difflib.get_close_matches(elem, allowed, n=3, cutoff=0.5)
    hint = f"你是不是想写: {suggestions}？" if suggestions else ""
    raise RuntimeError(
        f"{where}: 未知元素 id {elem!r}（不在可调剂元素列表中）。{hint}"
        "元素 id 必须逐字取自元素表（含 func:/global:/struct:/call: 前缀与 #序号）"
    )


def _expr_string_constants(expr):
    """提取表达式里的字符串常量（用于校验 pairs 表达式中硬编码的元素 id）。"""
    try:
        tree = ast.parse(expr, mode="eval")
    except SyntaxError:
        return []
    return [n.value for n in ast.walk(tree)
            if isinstance(n, ast.Constant) and isinstance(n.value, str)]


def _validate_literal_pairs(strategy, elements):
    """静态校验 eval 步骤的 pairs：

    - 字面 pairs 的元素 id / 类型必须在元素表与 search_types 内；
    - pairs 表达式里硬编码的元素 id 字符串同样校验（LLM 常在这里写错前缀）。
    失败抛 RuntimeError（消息回喂给 LLM 重试）。
    """
    allowed = {e.get("id"): set(e.get("search_types") or [])
               for e in elements if e.get("id")}

    def check_pairs(pairs, path):
        if isinstance(pairs, list):
            for j, pair in enumerate(pairs):
                if not (isinstance(pair, (list, tuple)) and len(pair) == 2):
                    continue
                elem, typ = pair
                if not (isinstance(elem, str) and isinstance(typ, str)):
                    continue
                if not _looks_expression(elem):
                    _check_element_id(elem, allowed, f"{path}.pairs[{j}]")
                if elem in allowed and not _looks_expression(typ) \
                        and typ not in allowed[elem]:
                    raise RuntimeError(
                        f"{path}.pairs[{j}]: 元素 {elem} 的类型 {typ!r} 不在其 "
                        f"search_types 内；允许值: {sorted(allowed[elem])}。"
                        "注意数组/指针元素的类型带 * 或 [N] 后缀，必须原样使用，"
                        "或改用表达式 \"[[e, types_of(e)[k]]]\""
                    )
        elif isinstance(pairs, str):
            for const in _expr_string_constants(pairs):
                _check_element_id(const, allowed, f"{path}.pairs 表达式")

    def check(step, path):
        if not isinstance(step, dict):
            return
        if step.get("op") == "eval":
            check_pairs(step.get("pairs"), path)
        for key in ("steps", "then", "else"):
            for k, sub in enumerate(step.get(key) or []):
                check(sub, f"{path}.{key}[{k}]")

    for i, top in enumerate((strategy or {}).get("steps") or []):
        check(top, f"steps[{i}]")


# ---------------------------------------------------------------------------
# opencode 适配器
# ---------------------------------------------------------------------------

# 写给 LLM 的 DSL 说明（保持紧凑，控制 prompt 长度）
_DSL_SCHEMA = """\
策略用**行式格式（NDJSON）**：每行一个独立 JSON 对象表示一个步骤，没有嵌套大括号。
步骤按行顺序执行。op 可取：
- {"op":"eval", "base":"original|best|last|变量名", "pairs":[[元素ID, 类型],...], "as":"变量名"}
  评估一个配置：base 基础上应用 pairs 覆盖。
  base 也可以是保存过 eval 结果的变量名（表示在该记录的配置上继续修改）。
  pairs 是 JSON 数组，元素为 [元素, 类型]；元素/类型直接写字面量字符串
  （如 ["func:main#0","float"]），动态值用表达式（如 ["e", "types_of(e)[0]"]）；
  pairs 整体也可以是求值为列表的表达式字符串（如 "[[e, 'float']]"）。
  结果存入变量，用 r['accepted']/r['best']/r['time']/r['assignment'] 等下标读取字段。
- {"op":"eval_uniform", "type":"float", "as":"变量名"}  把所有元素统一设为某类型后评估。
- {"op":"set", "name":"x", "value":...} 或 {"op":"set","name":"x","expr":"表达式"}
  value 是纯单词（如 "float"、"func:main#0"）时按字符串字面量保存；
  其它字符串（含运算符/括号/引号，如 "best['time']"）按表达式求值；
  需要引用变量/算数时建议直接用 expr。name 可写成下标形式："acc[e]"。
- 块结构（唯一需要配对的结构，用独立的 end 行闭合，不用嵌套数组）：
    {"op":"if", "when":"表达式"}      → 开启分支；随后是条件为真时执行的行
    {"op":"else"}                    → 可选，切换到条件为假的分支
    {"op":"end"}                     → 闭合最近的 if/foreach/for/while
    {"op":"foreach", "items":"表达式", "as":"e"}  ...  {"op":"end"}
    {"op":"for", "start":0, "stop":N, "as":"i"}  ...  {"op":"end"}
    {"op":"while", "when":"表达式", "max_iterations":N}  ...  {"op":"end"}
- {"op":"log", "message":"表达式"} / {"op":"break"} / {"op":"done","reason":"原因"}
块可以嵌套（if 里放 foreach），但每层只是独立行 + 各自的 end 行，
**绝不需要写 ]} 之类的连续闭合符**。
表达式中可用：element_ids()、types_of(元素ID)、elements、history、best、baseline、last、evals、max_evals、threshold、vars、best_assignment()、original_assignment()、
count_status(状态)、count_result(值)、last_records(n)、improvement_age()、
以及用 as 保存的变量名（顶层可直接引用）。
**types_of(e) 按精度从低到高排序**：types_of(e)[0] 是最低精度（通常是 __fp16），
types_of(e)[-1] 通常是原始精度。想降到某一档请直接写字面量类型（如 "float"、"float*"），
不要假设 types_of(e)[0] 是 float——上次有模型因此把“先 float 试探”误写成全量 __fp16。
eval 记录（含 best/last 与 as 变量）可用字段：run_id、assignment、accepted、status、
time、compile_time、result、rel_err、changes、cached、best、note —— 误差字段是 rel_err
（不存在 max_error/error 字段）。
表达式按 Python 语法求值（受限白名单）：支持 not / ! 取反、布尔运算与比较、三元表达式、
下标、列表/集合/字典推导式、f-string、以及容器方法（x.get(k, d)、x.append(v)、x.items()）。
禁止 lambda、赋值表达式(:=)、下划线开头的属性（如 x.__class__）、import。
字段读取请用下标 r['accepted']，不要写 r.accepted。
每个元素只能取其 search_types 中的类型（精度只能降不能升）。
**数组/指针元素的 search_types 带 * 或 [N] 后缀**（如 float*、double[3]）：
pairs 里的类型必须原样使用 types_of(元素) 返回的字符串，不要省略后缀；
不确定时直接用表达式 "[[e, types_of(e)[0]]]" 而不是手写类型名。
变量在运行时才绑定：若某变量只在 if 的某个分支里赋值，另一分支引用它时
必须用 and/or 短路保护（如 "not u1['accepted'] and u2['accepted']"）。
"""

_STRUCTURE_PROMPT_TEMPLATE = """\
你是混合精度优化专家。下面是一个待做精度调优的程序的部分源码与可调剂精度元素列表。

## 优化目标（按优先级）
1. **正确性优先**：最终配置的输出相对误差必须 <= threshold = $threshold。
2. **性能其次**：在满足误差阈值的前提下，把程序运行时间降到最低（相对基准 $baseline）。
   注意（领域事实）：降精度不必然加速——类型转换开销、非规格化数、向量化差异都可能
   让 float/__fp16 版本反而更慢；**全量降级慢不代表局部降级也慢**（实测中常见只降少数
   热路径元素反而最快）。时间结论只能以实测为准，不能从一次全量试探外推。
3. **收敛效率**：每次评估都会真的编译并运行整个程序（成本很高），
   好的策略应该用尽量少的评估次数逼近全局最优，**禁止穷举所有元素 × 所有精度的组合**。
4. 期望的评估规模：$budget_hint
   元素共 $element_count 个，若穷举（每个元素试所有可选精度）约需 $brute_force 次评估 —— 这不可接受。

## 搜索约束（不规定具体战术，由你自行设计试探路径）
- 每次评估都要编译+运行整个程序，成本很高；试探粒度（整批/分组/逐元素）、
  被拒后是否细分、何时收敛（可参考 improvement_age() / 接受率）都由你根据实测数据自行决定。
- 必须支持细粒度：必要时用 foreach 对单个元素逐一试探，或用 if 按实测结果分支。
- 唯一硬约束：在预算内给出满足误差阈值且尽量快的配置，禁止无差别穷举。

## 可调剂元素（id/kind/name/file:line/original_type/search_types，search_types 按精度升序，最后一个通常是原始类型）
$elements_table

源文件摘录：
$sources_block

请基于程序结构（循环、累加、抵消、分支敏感性等）设计一个定制搜索策略。
$dsl_schema

要求：
1. 输出为**行式 NDJSON**，每行一个完整 JSON 对象：
   - 第 1 行是分析结论：{"advice": "一句话说明你的结构分析结论与策略思路"}
   - 从第 2 行起是策略步骤，每行一个步骤对象；
   - **不要输出任何其他文字、解释或 markdown 代码围栏**；每行必须能单独 json.loads。
2. 每行 JSON 的硬性要求：
   - 所有字符串必须带双引号；禁止裸标识符（如 "items": all_elements 非法）、
     禁止注释、禁止尾逗号、禁止单引号字符串、禁止行内原始换行；
   - pairs 引用变量时整条写成表达式字符串：
       {"op":"eval","base":"best","pairs":"[[e, types_of(e)[-1]]]","as":"r"}
     固定元素写数组字面量：{"op":"eval","base":"best","pairs":[["func:main#0","float"]],"as":"r"}；
   - JSON 字符串不能用 + 或任何运算符拼接（表达式字符串内部可以用 +）。
3. 块结构用 if/else/end 行配对（见上面 DSL 说明），**禁止把步骤塞进嵌套数组**；
   想表达“逐级试探、失败就停”时优先平铺多个顺序 eval：被拒绝的 eval 不会更新 best，
   后续 base="best" 自然从最近的成功配置继续，往往不需要 if；
   确实需要条件时整个策略最多 1~2 个 if 块，**总步骤（行数）不超过 12 行**。
4. 表达式限制（按 Python 语法，受限白名单求值）：
   - 记录字段只能用下标读取：r['accepted']、best['time']（写成 r.accepted 会报错）；
   - eval 的 base 可以是 "original" / "best" / "last" 或保存过 eval 记录的变量名；
   - eval 记录只有这些字段：run_id、assignment、accepted、status、time、compile_time、
     result、rel_err、changes（误差字段是 rel_err，不存在 max_error / error）。
5. 策略应以 original 配置为基准，先粗后细，利用 eval 结果（r['accepted']/r['best']/r['time']）剪枝；
   总评估次数要有上限意识（while 必须设 max_iterations），并在出现
   improvement_age() 较大、接受率偏低时主动 done 收敛。
6. 只使用上面列出的元素 ID 与其 search_types 内的类型。
7. **尽量不用 log 步骤**；需要说明的内容写进第 1 行的 advice 字段。

完整示例（注意逐行独立、end 配对）：
{"advice": "先全量降 float 试探，再逐项贪心降级"}
{"op": "eval_uniform", "type": "float", "as": "u"}
{"op": "foreach", "items": "element_ids()", "as": "e"}
{"op": "eval", "base": "best", "pairs": "[[e, types_of(e)[0]]]", "as": "r"}
{"op": "end"}
{"op": "done", "reason": "greedy pass complete"}
"""

_CHECKPOINT_PROMPT_TEMPLATE = """\
混合精度搜索进行中，这是一个检查点。触发原因：{reasons}

下面是本次搜索到目前为止的动态执行数据摘要（来自 run_history：运行时间、
混精配置、数值结果、接受/拒绝、趋势、异常信号）：

{digest}

最近评估记录（JSON）：{records}
当前最优：run_id={best_run_id}, time={best_time}
已用评估次数：{eval_count}/{max_evals}

请基于这些**实际执行数据**（不是源码静态结构）判断：
1) 哪些精度降级带来了实际加速、哪些降级被拒绝或误差偏大，瓶颈元素是谁；
2) 时间/误差趋势说明什么（例如被拒绝比例过高、最优久未刷新、误差接近阈值）；
3) 下一步该怎么调整搜索（继续降级哪些元素、放弃哪些、是否需要提前收敛）。

严格只输出一个 JSON 对象，不要任何解释文字；策略步骤只允许出现在 adjustment.extra_steps 字符串内部：
{{"advice": "一到三句话的具体分析，引用上面的数据", "adjustment": null}}

adjustment 可为 null，或是一个对象，只允许这些键（未知键会导致搜索直接报错）：
  - "stop": true 表示立即结束搜索（已找到满意的配置）
  - "max_evals": 正整数，调整后续评估预算（不得小于当前已评估次数）
  - "telemetry_interval": 正整数，调整后续检查点间隔
  - "note": 字符串，补充说明
  - "extra_steps": 字符串，NDJSON 格式的策略步骤（每行一个 {{"op": ...}} 对象，
    块结构用 {{"op":"if"}}/{{"op":"else"}}/{{"op":"end"}} 行配对），
    追加到策略队列末尾执行。当实测数据说明原定方向错误、需要换一批元素/精度
    重新试探时用它注入新步骤，而不是眼睁睁看着剩余预算烧在无效方向上。
示例：{{"advice": "float 降级普遍被拒，说明误差敏感的是 kernel 累加链；建议停止继续降 fp16", "adjustment": {{"stop": true}}}}
"""


def _match_brace(text, start):
    """返回从 start 处 '{' 起的配对右花括号位置；不匹配返回 None。"""
    depth = 0
    in_str = False
    escape = False
    for i in range(start, len(text)):
        ch = text[i]
        if escape:
            escape = False
            continue
        if ch == "\\" and in_str:
            escape = True
            continue
        if ch == '"':
            in_str = not in_str
            continue
        if in_str:
            continue
        if ch == "{":
            depth += 1
        elif ch == "}":
            depth -= 1
            if depth == 0:
                return i
    return None


def _describe_json_error(exc, chunk):
    pos = max(0, exc.pos)
    snippet = chunk[max(0, pos - 50):pos + 50]
    hint = ""
    if exc.msg.startswith("Expecting ',' delimiter") and pos < len(chunk) \
            and chunk[pos] in "}]":
        hint = (
            "；常见原因是括号层级错位、数组被提前闭合"
            "（例如 then/else 数组或 steps 数组少了一个 ]，被 } 提前关闭）"
        )
    return (
        f"JSON 解析失败（line {exc.lineno} col {exc.colno}: {exc.msg}）{hint}，"
        f"附近片段: {snippet!r}"
    )


def _parse_json_object(text):
    """取 LLM 输出里的第一个完整 JSON 对象。

    返回 (对象, None) 或 (None, 错误描述)。错误描述尽可能精确：
    - json.loads 的行列号与附近片段；
    - 顶层对象提前闭合（后面还有内容）时明确指出，这是 LLM 写嵌套 JSON
      最常见的错误，报成 "Expecting ',' delimiter" 会误导。
    """
    if not text or not text.strip():
        return None, "LLM 输出为空"
    bodies = [text]
    fenced = re.search(r"```(?:json)?\s*(.+?)```", text, re.S)
    if fenced:
        bodies.insert(0, fenced.group(1))

    errors = []
    decoder = json.JSONDecoder()
    for body in bodies:
        start = body.find("{")
        while start >= 0:
            try:
                obj, end = decoder.raw_decode(body, start)
            except json.JSONDecodeError as exc:
                errors.append(_describe_json_error(exc, body))
                break
            tail = body[end:]
            if tail.lstrip().startswith("{"):
                offset = end + (len(tail) - len(tail.lstrip()))
                prefix = body[:offset]
                line = prefix.count("\n") + 1
                col = offset - prefix.rfind("\n")
                errors.append(
                    f"JSON 解析失败（line {line} col {col}: 顶层对象提前闭合，"
                    f"其后还有额外内容，通常是花括号/方括号提前闭合），"
                    f"附近片段: {body[max(0, offset - 60):offset + 40]!r}"
                )
                break
            return obj, None
        if errors and not body.startswith("```"):
            # 第一个候选失败后仍尝试后续候选（围栏内文本 / 全文）
            continue

    if errors:
        return None, errors[0]
    return None, "未找到 JSON 对象"


def _extract_json_object(text):
    """解析失败返回 None（保留给单测/调试用的简化接口）。"""
    obj, _error = _parse_json_object(text)
    return obj


def _format_elements_table(elements, max_entries=200):
    lines = []
    for spec in elements[:max_entries]:
        loc = spec.get("file") or ""
        if spec.get("line"):
            loc = f"{loc}:{spec['line']}"
        lines.append(
            "- {id} | {kind} | {name} | {loc} | {orig} | {types}".format(
                id=spec.get("id"), kind=spec.get("kind"),
                name=spec.get("name"), loc=loc or "-",
                orig=spec.get("original_type"),
                types=",".join(spec.get("search_types") or []),
            )
        )
    if len(elements) > max_entries:
        lines.append(f"- ... 共 {len(elements)} 个元素，仅列出前 {max_entries} 个")
    return "\n".join(lines) or "(无可调剂元素)"


def _format_sources_block(sources, per_file_chars=6000, total_chars=30000):
    blocks = []
    total = 0
    for path, text in (sources or {}).items():
        snippet = text[:per_file_chars]
        if len(text) > per_file_chars:
            snippet += "\n... [truncated]"
        block = f"### {path}\n{snippet}"
        if total + len(block) > total_chars:
            break
        blocks.append(block)
        total += len(block)
    return "\n\n".join(blocks) or "(源码不可用)"


def _free_port():
    with socket.socket() as sock:
        sock.bind(("127.0.0.1", 0))
        return sock.getsockname()[1]


def _close_response(resp):
    """关闭 SSE 响应；先 shutdown 套接字，避免读取线程阻塞到超时。"""
    sock = getattr(getattr(getattr(resp, "fp", None), "raw", None), "_sock", None)
    if sock is not None:
        try:
            sock.shutdown(socket.SHUT_RDWR)
        except OSError:
            pass
    try:
        resp.close()
    except Exception:
        pass


_ANSI_RE = re.compile(r"\x1b\[[0-9;]*[A-Za-z]")


class OpencodeAdapter(AgentAdapter):
    """通过 opencode 的 HTTP/SSE 接口调用 LLM。

    - analyze_structure：返回 {"advice": str, "strategy": DSL dict|None}
    - analyze_checkpoint：返回 {"advice": str}
    - 会话在两次调用间延续（同一个 sessionID），检查点分析能看到启动时的上下文。
    - 生成过程中通过 SSE 事件流实时打印 text/reasoning 增量（token 级可观测）。
    任何失败（连接拒绝、超时、输出无法解析）直接抛异常 —— 不做降级。
    """
    name = "opencode"
    supports_structure = True

    def __init__(self, binary=None, model=None, timeout=600.0,
                 url=None, password=None, port=None, stream=True,
                 startup_timeout=60.0):
        self.binary = (
            binary
            or os.environ.get("PERCI_OPENCODE_BIN")
            or shutil.which("opencode")
            or os.path.expanduser("~/.opencode/bin/opencode")
        )
        # 模型由 opencode 配置决定；PERCI_OPENCODE_MODEL 仅作为提示（当前 API
        # 的模型选择走 opencode 配置，不再通过命令行参数覆盖）。
        self.model = model or os.environ.get("PERCI_OPENCODE_MODEL")
        self.timeout = float(os.environ.get("PERCI_OPENCODE_TIMEOUT", timeout))
        self.base_url = (url or os.environ.get("PERCI_OPENCODE_URL") or "").rstrip("/")
        self.password = password or os.environ.get("PERCI_OPENCODE_PASSWORD") or ""
        self.port = port
        self.stream = stream
        self.startup_timeout = float(startup_timeout)
        self.session_id = None
        self._proc = None
        self._stream_text_open = False
        self._stream_reason_open = False

    def available(self):
        return bool(self.binary) and (
            os.path.isfile(self.binary) or shutil.which(self.binary) is not None
        )

    # -- 服务生命周期 -----------------------------------------------------
    def _ensure_server(self):
        """返回可用的服务地址；必要时启动 ``opencode serve`` 并读取地址/密码。"""
        if self.base_url:
            if not self.password:
                raise RuntimeError(
                    "PERCI_OPENCODE_URL 已设置但缺少 PERCI_OPENCODE_PASSWORD"
                )
            return self.base_url

        port = self.port or _free_port()
        cmd = [self.binary, "serve", "--hostname", "127.0.0.1", "--port", str(port)]
        proc = subprocess.Popen(
            cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
            text=True, bufsize=1,
        )
        lines = queue.Queue()

        def pump():
            for line in proc.stdout:
                lines.put(line)
            lines.put(None)

        threading.Thread(target=pump, daemon=True).start()

        url = password = None
        deadline = time.time() + self.startup_timeout
        while time.time() < deadline and (url is None or password is None):
            try:
                line = lines.get(timeout=0.2)
            except queue.Empty:
                if proc.poll() is not None:
                    raise RuntimeError(
                        f"opencode serve 意外退出（code={proc.returncode}）"
                    )
                continue
            if line is None:
                raise RuntimeError("opencode serve 未输出服务地址/密码就退出了")
            text = _ANSI_RE.sub("", line).strip()
            match = re.search(r"server listening on (\S+)", text)
            if match:
                url = match.group(1).rstrip("/")
            match = re.search(r"server password (\S+)", text)
            if match:
                password = match.group(1)

        if not (url and password):
            proc.kill()
            raise RuntimeError(
                f"opencode serve 启动超时（{self.startup_timeout}s 未输出地址/密码）"
            )
        self._proc = proc
        self.base_url = url
        self.password = password
        return url

    def _headers(self):
        token = base64.b64encode(f"opencode:{self.password}".encode()).decode()
        return {"Authorization": "Basic " + token,
                "Content-Type": "application/json"}

    def _request(self, path, data=None, method=None):
        url = self._ensure_server() + path
        body = json.dumps(data).encode() if data is not None else None
        req = urllib.request.Request(
            url, data=body, headers=self._headers(),
            method=method or ("POST" if data is not None else "GET"),
        )
        try:
            with urllib.request.urlopen(req, timeout=self.timeout) as resp:
                payload = resp.read()
        except urllib.error.HTTPError as exc:
            detail = exc.read()[:300]
            raise RuntimeError(
                f"opencode API {path} 返回 HTTP {exc.code}: {detail!r}"
            ) from exc
        except urllib.error.URLError as exc:
            raise RuntimeError(f"无法连接 opencode API {url}: {exc}") from exc
        try:
            return json.loads(payload or b"{}")
        except json.JSONDecodeError as exc:
            raise RuntimeError(
                f"opencode API {path} 返回非 JSON: {payload[:200]!r}"
            ) from exc

    def _new_session(self):
        payload = self._request("/api/session", {})
        session = payload.get("data") or payload
        session_id = session.get("id")
        if not session_id:
            raise RuntimeError(f"opencode 未能创建会话: {payload}")
        self.session_id = session_id
        return session_id

    # -- 实时输出 ---------------------------------------------------------
    def _emit_text(self, delta):
        if not self.stream or not delta:
            return
        if not self._stream_text_open:
            self._end_stream()
            sys.stdout.write("\n>>> LLM ▸ ")
            self._stream_text_open = True
        sys.stdout.write(delta)
        sys.stdout.flush()

    def _emit_reasoning(self, delta):
        if not self.stream or not delta:
            return
        if not self._stream_reason_open:
            self._end_stream()
            sys.stdout.write("\n>>> LLM 思考 ▸ ")
            self._stream_reason_open = True
        sys.stdout.write(delta)
        sys.stdout.flush()

    def _end_stream(self):
        if self._stream_text_open or self._stream_reason_open:
            sys.stdout.write("\n")
            sys.stdout.flush()
        self._stream_text_open = self._stream_reason_open = False

    def _prompt(self, text):
        """发送一次 prompt，按 SSE 事件流实时输出增量，返回完整回答。"""
        session_id = self.session_id or self._new_session()
        events = queue.Queue()
        closed = threading.Event()
        holder = {}

        def reader():
            url = self.base_url + "/api/event"
            headers = {"Accept": "text/event-stream",
                       "Authorization": self._headers()["Authorization"]}
            try:
                request = urllib.request.Request(url, headers=headers)
                resp = urllib.request.urlopen(request, timeout=self.timeout)
                holder["resp"] = resp
                for raw in resp:
                    if closed.is_set():
                        break
                    line = raw.decode("utf-8", "replace").strip()
                    if line.startswith("data:"):
                        events.put(line[5:].strip())
            except Exception as exc:
                events.put({"__error__": f"{type(exc).__name__}: {exc}"})

        threading.Thread(target=reader, daemon=True).start()

        # 先确认事件流已建立，避免漏掉开头几个 delta
        try:
            hello = events.get(timeout=self.startup_timeout)
        except queue.Empty:
            raise RuntimeError("opencode 事件流未能建立连接")
        if isinstance(hello, dict):
            raise RuntimeError(f"opencode 事件流连接失败: {hello['__error__']}")

        self._request(f"/api/session/{session_id}/prompt", {"text": text})

        chunks = []
        ended = False
        deadline = time.time() + self.timeout
        try:
            while time.time() < deadline:
                try:
                    raw = events.get(timeout=0.5)
                except queue.Empty:
                    continue
                if isinstance(raw, dict):
                    raise RuntimeError(f"opencode 事件流中断: {raw['__error__']}")
                try:
                    event = json.loads(raw)
                except json.JSONDecodeError:
                    continue
                data = event.get("data") or {}
                if data.get("sessionID") and data["sessionID"] != session_id:
                    continue
                kind = event.get("type")
                if kind == "session.text.delta":
                    delta = data.get("delta") or ""
                    chunks.append(delta)
                    self._emit_text(delta)
                elif kind == "session.reasoning.delta":
                    self._emit_reasoning(data.get("delta") or "")
                elif kind == "session.text.ended":
                    if not chunks and data.get("text"):
                        chunks.append(data["text"])
                elif kind == "session.execution.succeeded":
                    ended = True
                    break
                elif kind == "session.execution.failed":
                    raise RuntimeError(
                        "LLM 执行失败: "
                        + json.dumps(data, ensure_ascii=False)[:500]
                    )
        finally:
            closed.set()
            resp = holder.get("resp")
            if resp is not None:
                _close_response(resp)
            self._end_stream()
        if not ended:
            raise RuntimeError(
                f"LLM 调用超时（{self.timeout}s 内未收到 execution.succeeded）"
            )
        return "".join(chunks)

    # -- 对外接口 ---------------------------------------------------------
    def close(self):
        """关闭由适配器自己启动的 opencode 服务。"""
        proc = self._proc
        if proc is None:
            return
        self._proc = None
        if proc.poll() is None:
            proc.terminate()
            try:
                proc.wait(timeout=10)
            except subprocess.TimeoutExpired:
                proc.kill()

    def _call(self, prompt):
        answer = self._prompt(prompt).strip()
        # 调试：保留最近一次 LLM 原始输出，便于诊断解析失败
        dump_path = os.environ.get(
            "PERCI_OPENCODE_DUMP", "/tmp/opencode_last_answer.txt"
        )
        try:
            with open(dump_path, "w") as fh:
                fh.write(answer)
        except OSError:
            pass
        if not answer:
            raise RuntimeError("LLM 未返回文本")
        return answer

    def _query_json(self, prompt, label, parser, attempts=3,
                    retry_hint=None):
        """调用 LLM 并用 parser 解析；解析失败时带着错误信息重试。

        parser(answer) 返回结果或抛 RuntimeError（消息回喂给 LLM）。
        仍然失败就抛 RuntimeError —— 不做降级，让问题暴露。
        """
        last_error = ""
        for attempt in range(1, attempts + 1):
            ask = prompt
            if last_error:
                ask = (
                    prompt
                    + "\n\n上一次输出无法使用，问题如下：\n"
                    + last_error
                    + "\n请重新输出修正后的内容：不要 markdown 代码围栏，"
                    "不要输出任何分析/思考文字，直接给出最终答案。"
                    + (retry_hint or "")
                )
            answer = self._call(ask)
            try:
                return parser(answer)
            except RuntimeError as exc:
                last_error = f"{exc}（原始输出 len={len(answer)}）"
            print(
                f">>> Strategy agent: {label} 失败"
                f"（第 {attempt}/{attempts} 次）：{last_error}"
            )
        raise RuntimeError(f"{label} 失败：{last_error}")

    def analyze_structure(self, request):
        element_count = len(request.elements)
        types_total = sum(
            len(item.get("search_types") or []) for item in request.elements
        )
        brute_force = max(types_total, 0)
        if request.eval_budget > 0:
            budget_hint = (
                f"本次搜索的硬上限是 {request.eval_budget} 次评估"
                f"（达到即终止），请把策略控制在 {request.eval_budget} 次以内"
            )
        else:
            # 没有硬上限时给一个经验预算，避免策略无节制穷举
            suggested = max(2 * element_count + 4, 8)
            budget_hint = (
                f"未设置硬上限，但建议不超过 {suggested} 次评估"
                f"（约 2×元素数+4）"
            )
        baseline = "未知"
        if request.baseline_time:
            baseline = f"{request.baseline_time:.6g}s"
        prompt = string.Template(_STRUCTURE_PROMPT_TEMPLATE).substitute(
            threshold=request.threshold,
            baseline=baseline,
            budget_hint=budget_hint,
            element_count=element_count,
            brute_force=brute_force,
            elements_table=_format_elements_table(request.elements),
            sources_block=_format_sources_block(request.sources),
            dsl_schema=_DSL_SCHEMA,
        )
        if request.feedback:
            prompt += (
                "\n\n## 重要：上一轮策略执行失败\n"
                "你上一轮生成的策略在执行期报错：\n"
                f"{request.feedback}\n"
                "请修正后重新输出完整策略（遵守上面的全部格式约束）。\n"
            )

        retry_hint = (
            "第 1 行必须是 {\"advice\": \"...\"}（注意拼写 advice），其后每行一个步骤对象；"
            "每行单独 json.loads 必须成功；每个 if/foreach/for/while 行都必须有对应的 "
            "{\"op\": \"end\"} 行——输出结束前逐块检查配对；"
            "禁止嵌套数组；总步骤不超过 12 行；"
            "pairs 的类型必须原样取自元素的 search_types（数组/指针带 * 或 [N] 后缀）。"
        )

        def parse(answer):
            result = parse_structure_answer(answer)
            _validate_literal_pairs(result["strategy"], request.elements)
            return result

        return self._query_json(prompt, "结构分析", parse, attempts=4,
                                retry_hint=retry_hint)

    def analyze_checkpoint(self, request):
        digest = request.digest
        prompt = _CHECKPOINT_PROMPT_TEMPLATE.format(
            reasons=", ".join(request.reasons or []) or "interval",
            digest=digest.to_prompt() if digest is not None else "(无)",
            records=json.dumps(request.records[-10:], ensure_ascii=False)[:6000],
            best_run_id=(request.best or {}).get("run_id"),
            best_time=(request.best or {}).get("time"),
            eval_count=request.eval_count,
            max_evals=request.max_evals or "unlimited",
        )

        def parse(answer):
            # 模型有时把策略阶段的 NDJSON 习惯带进来（advice 对象后跟若干 {"op":...} 行）：
            # 扫描所有顶层 JSON 对象，合并 advice / adjustment，忽略步骤行。
            body = re.sub(r"```(?:json)?|```", "", answer or "")
            decoder = json.JSONDecoder()
            objects = []
            idx = 0
            while True:
                start = body.find("{", idx)
                if start < 0:
                    break
                try:
                    obj, end = decoder.raw_decode(body, start)
                except json.JSONDecodeError:
                    idx = start + 1
                    continue
                if isinstance(obj, dict):
                    objects.append(obj)
                idx = end
            advice = None
            adjustment = None
            for obj in objects:
                if advice is None and obj.get("advice"):
                    advice = obj["advice"]
                adj = obj.get("adjustment")
                if adj is not None and not isinstance(adj, dict):
                    raise RuntimeError(
                        "LLM 检查点的 adjustment 必须是对象或 null，实际为 "
                        f"{type(adj).__name__}"
                    )
                if isinstance(adj, dict):
                    adjustment = {**(adjustment or {}), **adj}
            if advice is None:
                raise RuntimeError(
                    "LLM 检查点输出缺少 advice 字段"
                    "（未找到形如 {\"advice\": ...} 的 JSON 对象）"
                )
            if adjustment and "extra_steps" in adjustment:
                raw = adjustment["extra_steps"]
                if not isinstance(raw, str) or not raw.strip():
                    raise RuntimeError(
                        "adjustment.extra_steps 必须是 NDJSON 字符串"
                        "（每行一个 {\"op\": ...} 步骤对象）"
                    )
                try:
                    parsed = parse_jsonl_strategy(raw)
                except ValueError as exc:
                    raise RuntimeError(
                        f"adjustment.extra_steps 解析失败: {exc}"
                    ) from exc
                _validate_literal_pairs(parsed, request.elements)
            if len(objects) > 1:
                print(f">>> Strategy agent: 检查点输出含 {len(objects)} 个 "
                      "JSON 对象，已合并 advice/adjustment")
            return {"advice": str(advice)[:500], "adjustment": adjustment}

        return self._query_json(prompt, "检查点分析", parse)


def create_adapter(name):
    """按名字创建适配器：none / opencode。"""
    normalized = (name or "none").strip().lower()
    if normalized in ("none", "null", ""):
        return NullAdapter()
    if normalized in ("opencode", "oc"):
        return OpencodeAdapter()
    raise ValueError(
        f"unsupported agent adapter '{name}' (supported: none, opencode)"
    )
