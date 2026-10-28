# Perci-Tuner

Perci-Tuner increases C program's runtime speed while keeping errors within an acceptable range, by reducing floating-point variables & C-Math function calls' precision.

## Architecture

```mermaid
flowchart TD
    subgraph inp ["输入"]
        C["C 源程序"]
        Pragma["精度 pragma / 初始配置"]
        Threshold["误差阈值 / 预算 / 搜索范围"]
    end

    subgraph llvm ["LLVM Pass"]
        Print["PrintConfig Pass<br/>提取可调剂精度元素"]
        Remove["RemovePragma Pass<br/>执行时移除 pragma"]
        IR["LLVM IR / 可执行二进制"]
    end

    subgraph algo ["搜索算法"]
        direction LR
        subgraph trad ["传统算法<br/>-algorithm=dd / hidd / pedd / exhaust"]
            DD["DD 二分"]
            HIDD["HIDD"]
            PEDD["PEDD"]
            EXH["Exhaust"]
        end
        subgraph llm ["LLM 策略<br/>-algorithm=strategy -agent=opencode"]
            Agent["OpencodeAdapter<br/>HTTP + SSE 流式"]
            Strategy["StrategyExecutor<br/>NDJSON DSL 执行器"]
            Telemetry["Telemetry<br/>run_history 数据摘要器"]
        end
    end

    subgraph loop ["评估回路"]
        Evaluator["Evaluator<br/>编译 → 运行 → 测量"]
        History["run_history.csv<br/>时间 / 误差 / 接受状态"]
    end

    subgraph out ["最终结果"]
        Best["最优配置<br/>满足误差阈值且最快"]
        Viz["t-SNE / UMAP 可视化"]
    end

    C --> Print
    Print --> IR
    Pragma -.-> Print
    C --> Remove

    Print --> trad
    Print --> Agent
    Threshold --> trad
    Threshold --> Strategy

    Agent -- "结构分析阶段" --> Strategy
    Strategy -- "检查点触发" --> Telemetry
    Telemetry -- "digest / adjustment" --> Agent
    Agent -- "执行期错误 + feedback" --> Agent

    trad --> Evaluator
    Strategy --> Evaluator
    Evaluator --> History
    History --> Best
    History --> Viz
```

### LLM Agent 内部时序

```mermaid
sequenceDiagram
    participant R as strategy_algorithm.py
    participant A as OpencodeAdapter
    participant S as opencode serve
    participant E as StrategyExecutor
    participant T as Telemetry
    participant Ev as Evaluator

    R->>E: run_structure_analysis(config, feedback)
    E->>A: analyze_structure(StructureRequest)
    A->>S: POST /api/session
    S-->>A: session_id, password
    A->>S: POST /api/session/{sid}/prompt<br/>结构分析 + DSL schema + 优化目标
    loop SSE 流式 token
        S-->>A: session.reasoning.delta
        A-->>R: LLM 思考 ...
        S-->>A: session.text.delta
        A-->>R: LLM 输出 ...
    end
    S-->>A: session.text.ended / execution.succeeded
    A->>A: parse_jsonl_strategy + validate_literal_pairs
    alt 解析或校验失败
        A->>S: 重试(带精确错误回喂)
    end
    A-->>E: advice + strategy (NDJSON)

    R->>E: run_dsl(strategy)
    E->>E: 顶层步骤 pending 队列
    loop 每个顶层步骤
        alt eval / eval_uniform
            E->>Ev: 编译 + 运行
            Ev-->>E: EvalRecord(time, rel_err, accepted)
        end
        alt 队列耗尽 且 触发 interval
            E->>T: analyze(records) -> digest
            E->>A: analyze_checkpoint(digest)
            A->>S: POST /api/session/{sid}/prompt<br/>run_history 数据摘要
            S-->>A: advice + adjustment (stop / max_evals / extra_steps)
            A-->>E: adjustment
            alt adjustment.extra_steps 存在
                E->>E: parse_jsonl_strategy(extra_steps)<br/>追加到 pending 队列末尾
            end
            alt adjustment.stop = true
                E-->>R: Done(agent-stop)
            end
        end
        alt 策略执行期抛出 StrategyError
            R->>A: analyze_structure(feedback=str(exc))
            Note over A: 一次重生成上限
        end
    end
    E-->>R: SearchOutcome
```

### NDJSON DSL（Tier A JSON 步骤）

LLM 输出的策略是**逐行 JSON 对象**，无闭合段，避免自回归生成时花括号提前闭合。顶层执行用 pending 队列，允许检查点通过 `adjustment.extra_steps` 动态追加步骤。块结构用显式 `end` 行配对：

```
{"advice": "结构分析结论"}
{"op": "eval_uniform", "type": "float", "as": "all_float"}
{"op": "foreach", "items": "[e for e in element_ids()]", "as": "e"}
{"op": "if", "when": "not all_float['accepted']"}
{"op": "eval", "base": "original", "pairs": "[[e, 'float']]", "as": "t"}
{"op": "end"}
{"op": "done", "reason": "收敛"}
```

| op | 说明 |
|---|---|
| `eval` / `eval_uniform` | 评估配置，结果存入变量 |
| `set` | 定义变量（支持 Python 受限表达式） |
| `foreach` / `for` / `while` | 循环 |
| `if` / `else` / `end` | 条件块（必须显式闭合） |
| `log` / `break` / `done` | 辅助 |

表达式可用：`element_ids()`、`types_of(e)`、`history`、`best`、`baseline`、`evals`、`improvement_age()`、`threshold` 等。精度只能从原始降级；`types_of(e)[0]` 是**最低精度**（通常 `__fp16`），想降 float 请用字面量 `"float"`。

## LLVM
Availability on LLVM 22.1.8 & LLVM 17.0.6 have been tested.
```sh
git clone https://github.com/llvm/llvm-project.git
cd llvm-project
git checkout llvmorg-22.1.8

cmake -G Ninja \
  -S llvm -B build \
  -DCMAKE_BUILD_TYPE=Release \
  -DLLVM_TARGETS_TO_BUILD="host" \
  -DLLVM_ENABLE_PROJECTS="clang;mlir" \
  -DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
  -DLLVM_BUILD_UTILS=ON \
  -DLLVM_BUILD_TOOLS=ON \
  -DBUILD_SHARED_LIBS=ON \
  -DLLVM_ENABLE_ASSERTIONS=OFF

cmake --build build
cmake --install build --prefix ${INSTALLDIR}
```
If you want to use -mxp option, install mxp version of LLVM from gitmodule.

## Run

```sh
export PERCI_TUNER="/path/to/perci-tuner/"
sh scripts/run.sh <print-config|change-by-config|search-optimize|check> src_files [Optional Flags]
```
Check run.sh for more help info.

## Test

```sh
cd $PERCI_TUNER
sh scripts/run.sh check
```
Or run specific test:
```sh
python3 -m lit -v test/xxx
```

## Visualize search

After finishing a search, generate t-SNE & UMAP visualization and `run_history.png` with:
```bash
python3 scripts/visualize.py
```
The PNG generated from `run_history.csv`: 
* Each pie is a runned config marked with `run_id`
* Each line is a generation of configs, they were born in a common parent.
* `parent` means the run_id that current config search from
* Outer cycle means bitwidth of elements:
```
16-green, 32-blue, 64-yellow, 128-red
```
* Inner cycle means run status: 
```
Black: RUNTIME-ERROR (unexpected)
Red: out-of-errorthreshold
Yellow: Run slower than original
Green: Run faster than original
✅: The fastest config until current
```