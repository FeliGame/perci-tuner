# 全 CIR 版浮点配置生成与精度转换（平替 PrintConfig / ChangeVariableDecl / ChangeMathCall）

## Context（背景与目标）

现有 perci-tuner 的三条核心链路全部在 LLVM IR 层实现（[lib/PrintConfig.cpp](file:///home/feli/perci-tuner/lib/PrintConfig.cpp)、[lib/ChangeVariableDecl.cpp](file:///home/feli/perci-tuner/lib/ChangeVariableDecl.cpp)、[lib/ChangeMathCall.cpp](file:///home/feli/perci-tuner/lib/ChangeMathCall.cpp)），强依赖 DWARF debug info（`dbg.declare`/`DbgVariableRecord`）识别变量、行号、类型与参数。LLVM 22 的调试记录机制反复变化（见 [ChangeVariableDecl.cpp:1402](file:///home/feli/perci-tuner/lib/ChangeVariableDecl.cpp#L1402) 附近的 LLVM19/22 分支），实现脆弱。

目标：在 **ClangIR (CIR)** 层重新实现「配置生成 + 精度转换」。CIR 中变量声明即 `cir.alloca !cir.float, ..., ["x", init] loc("file.c":line:col)`，变量名/类型/源码位置是一等信息，无需 debug info；math call 为 `cir.call @sinf(...)`，全局为 `cir.global`，结构体为 record type + `cir.get_member`，表达与源码一一对应，转换更直接可靠。

环境已验证（LLVM/Clang 22.1.8，aarch64）：`clang -emit-cir`、`cir-opt --load-pass-plugin`（MLIR 插件入口 `mlirGetPassPluginInfo`）、`cir-translate --cir-to-llvmir`、`libMLIRCIR.so` 等均可用。

### 已确认的范围决策

1. **测试策略（增量，不改旧逻辑）**：在 `test/print-and-transform/` 6 个测试文件中**追加**使用 `cir` 选项的 RUN 命令行，并写**独立的 CHECK 前缀**（如 `CHECK-CIR:` / `CHECK-CIR-TRANSFORM:`）验证 CIR 链路；现有 RUN 行与 CHECK/CHECK-TRANSFORM 行原样保留、继续覆盖 LLVM IR 链路。
2. **首版不含 MXP**：不做 `__autofp` / `#pragma precision region/range/error` 路径（现有 6 个测试均不涉及）。
3. **Pass 文件只有两个**：`CIRPrintConfig`（配置生成）与 `CIRTransform`（合并 ChangeVariableDecl + ChangeMathCall 的全部转换功能）。另有 `RegisterCIRPasses.cpp`（插件入口）与 `CIRConfig.hpp`（两个 Pass 共享的纯工具：JSON/类型名/math 集合/key 规则，非 Pass）。
4. **CIRTransform 暂不实现函数参数/签名变换**：不改变 `cir.func` 的形参类型、返回值类型及调用点签名；配置中 `isParam=true` 的项与 `ret.` 项在转换阶段直接跳过（CIRPrintConfig 仍照常打印，保证 config.json 契约一致）。function_declaration 的 CIR 追加用例只验证配置打印与转换不报错，不验证签名变化。
5. **run.sh 集成**：新增布尔选项 **`cir`**（写法：`sh scripts/run.sh cir print-config src.c` / `sh scripts/run.sh cir change-by-config -target=... src.c`）。启用时 print-config / change-by-config 走 CIR 链路；不启用时完全保持现有 LLVM IR 链路。

## config.json 契约（必须与现版本逐字节兼容）

顶层对象：`local`（按函数名分组的变量数组）、`global`（全局/静态变量数组）、`calls`（按函数分组的 math call 数组）、`structs`（结构体名→成员类型数组）。
变量项字段：`name,type,line,column,file,isStatic,isParam`，返回值项 `name="ret."`。类型字符串：`__fp16`/`float`/`double`/`long double`/`MyStruct`/`T*`/`T[N]`。key 规则：`file:line:column`（与 [ChangeVariableDecl.cpp:77](file:///home/feli/perci-tuner/lib/ChangeVariableDecl.cpp#L77)、[ChangeMathCall.cpp:64](file:///home/feli/perci-tuner/lib/ChangeMathCall.cpp#L64) 一致）。

## CIR 与源码映射（已实测确认）

- 局部变量/参数：`cir.alloca Ty, !cir.ptr<Ty>, ["name", init] loc(file:line:col)`；参数名在 alloca 的 name 属性上（`%arg0` 无名，但 alloca `["x"]`），参数标识用函数入口 alloca + store 到 `%argN`；浮点**指针**参数不转换（现版本同样跳过）。
- 返回值：`cir.func @f(...) -> !cir.float`。
- 全局/静态：文件作用域 `cir.global external/private @name : Ty loc(...)`；函数内 static 为 `cir.global "private" internal @main.f`（名带函数前缀，`isStatic=true`，去前缀得变量名）。
- math：`cir.func private @sinf(!cir.float)->!cir.float` + `cir.call @sinf(...)`，loc 即调用点。
- 结构体：record 类型（`!rec_P`）+ `cir.get_member %p[0] {name="a"}`；数组 `!cir.array<!cir.double x 3>` + `cir.get_element`；指针 `!cir.ptr<!cir.float>`。
- 类型字符串映射：`!cir.f16→__fp16`、`!cir.float→float`、`!cir.double→double`、`!cir.long_double<!cir.f128>→long double`；反向转换复用 [typeNameToBasicType](file:///home/feli/perci-tuner/lib/ChangeVariableDecl.cpp#L1324) 的优先级规则（注意 `float` 子串会误匹配 `long double`，按 f16→long double→float→double 顺序判定）。
- cast 插入：CIR 用 `cir.cast fpext/fptrunc/si_to_fp/fp_to_si`（实现时在 CIROps.td / CIROpsEnums 确认精确 kind 名）；binop 精度推断复用 [findHighestPrecisionType](file:///home/feli/perci-tuner/lib/ChangeVariableDecl.cpp#L390) 语义（浮点常量不锁定类型，cast 看源类型）。

## 实施步骤（按可独立验证的里程碑推进，每步以真实命令输出为证）

### M0 构建/插件骨架（打通 cir-opt 加载）

- 新建 `cir/` 目录：
  - `cir/CIRPrintConfig.{h,cpp}`：MLIR `OperationPass<mlir::ModuleOp>`，pass 名 `print-cir-config`，先实现空 run + `errs()` 打印。
  - `cir/CIRTransform.{h,cpp}`：MLIR `OperationPass<mlir::ModuleOp>`，pass 名 `transform-cir`，单文件内顺序执行「math call 改写」与「变量/全局/结构体精度改写」两个阶段（对应原 ChangeMathCall + ChangeVariableDecl），骨架先空跑。
  - `cir/RegisterCIRPasses.cpp`：`extern "C" mlir::PassPluginLibraryInfo mlirGetPassPluginInfo()`，注册两个 pass 的 pipeline 解析回调（参照 [lib/RegisterPasses.cpp](file:///home/feli/perci-tuner/lib/RegisterPasses.cpp) 与 `/usr/local/include/mlir/Tools/Plugins/PassPlugin.h` 示例）。
  - `cir/CIRConfig.hpp`：header-only 共享工具——JSON 读写（继续用 `llvm/Support/JSON.h`）、cl::opt（`variable-config`/`function-config`/include/exclude，参数名与现版本一致）、file:line:column key、类型名↔CIR Type 转换、math 函数集合（从 [ChangeMathCall.h:40](file:///home/feli/perci-tuner/lib/ChangeMathCall.h#L40) 原样搬入）、include/exclude glob（复用 [lib/utils/GlobMatch.hpp](file:///home/feli/perci-tuner/lib/utils/GlobMatch.hpp)）。
- [CMakeLists.txt](file:///home/feli/perci-tuner/CMakeLists.txt)：新增 `add_library(PerciTunerCIR MODULE ...)`（仅上述 3 个 cpp），include `${MLIR_INCLUDE_DIRS}` 与 `clang/CIR` 头目录；链接 MLIR/CIR 库（先查 `/usr/local/lib/cmake/mlir` 中可用 target：预计 `MLIRCIR`、`MLIRIR`、`MLIRPass`、`MLIRSupport`，按编译错误迭代修正）；保持 `-fno-rtti` 与 LLVM 一致。
- 验证：`cmake --build build` 成功；`cir-opt --load-pass-plugin build/PerciTunerCIR.so --passes=print-cir-config /tmp/t.cir` 能加载不报错。

### M1 CIRPrintConfig（与现有 print_config 输出对齐）

遍历 `mlir::ModuleOp`：
1. `cir.global` → global 数组（含 internal static、数组/指针/record、loc 行号文件、`isStatic`）；函数内 static 名 `F.var` 拆前缀，isStatic=true。
2. `cir.func`（非声明）→ local[func]：`cir.alloca` 具名变量（跳过 `__retval`）、参数识别（入口 alloca+store 到 `%argN`，指针浮点参数仅记录不参与转换配置——与现版本跳过一致）、`ret.` 返回值项。
3. `cir.call` 非声明、非内建 → calls[func]（mathFunctions 集合内的调用；loc 三元组）。
4. record 类型成员 → structs（从 `cir.get_member`/全局类型收集成员 index 与类型）。
5. include/exclude file/func 过滤（语义对齐 [PrintConfig.cpp:538](file:///home/feli/perci-tuner/lib/PrintConfig.cpp#L538)）。输出 `config.json`，`formatv("{0:4}")` 4 空格缩进。

### M2 run.sh `cir` 选项 + scalar.c 增量用例

- [scripts/run.sh](file:///home/feli/perci-tuner/scripts/run.sh)：新增 `CIR_FLAG=false`，参数解析中裸词 `cir` 置 true。
  - `compile_sources_to_bitcode` CIR 分支：`clang -g -emit-cir -c src -o src.cir`（`-Xclang -fnative-half-type` 保留）；测试均为单文件，先支持单文件，多文件直接报错提示。产出 `output.cir`。
  - print-config CIR 分支：`cir-opt --load-pass-plugin build/PerciTunerCIR.so -passes=print-cir-config output.cir`（pass 内写 config.json，IR 不变）；再 `cir-translate --cir-to-llvmir output.cir -o output.bc` + `clang -o output output.bc -lm` 产出基线可执行。
  - change-by-config CIR 分支：`cir-opt --load-pass-plugin build/PerciTunerCIR.so -passes=transform-cir --variable-config=.. --function-config=.. output.cir -o changed_output.cir`；`cir-translate --cir-to-llvmir changed_output.cir -o changed_output.bc`；`clang -O2 -o changed_output changed_output.bc -lm`。
- 在 `test/print-and-transform/scalar.c` **追加** CIR 的 RUN 行（用独立 tmp 子目录避免覆盖旧产物）与 `CHECK-CIR` / `CHECK-CIR-TRANSFORM` 行；旧 RUN/CHECK 一律不动。CIR 转换检查 `cir.alloca !cir.double, ..., ["l2d"]`。
- 证据：`python3 -m lit -v test/print-and-transform/scalar.c` 新旧两组 RUN 全绿 + changed_output.cir 片段。

### M3 CIRTransform 变量转换（scalar 全覆盖）

按 key 定位 `cir.alloca`（跳过 `isParam` 与 `ret.` 配置项）→ 换 alloca 类型；def-use 修复 `cir.store`（值侧插 `cir.cast`）、`cir.load`（结果类型 + 用户级联）、`cir.const`/`cir.binop`/`cir.unop`（最高精度推断 + cast）、`cir.return`（局部值 cast 回函数原返回类型）。参照 [repairAllocaUsers](file:///home/feli/perci-tuner/lib/ChangeVariableDecl.cpp#L334) 的工作清单逐条在 CIR 找对应 op，每加一类 op 用 scalar 变体手测。

### M4 math_call 增量用例（CIRTransform math 阶段）

定位 `cir.call`（key=loc），校验 base 名一致（[ChangeMathCall.cpp:117](file:///home/feli/perci-tuner/lib/ChangeMathCall.cpp#L117)），改/建 `cir.func` 的 math 库函数**外部声明**签名（f→float / l→long double / 无后缀→double），参数插 cast、返回值插 cast 回原类型。只改 math 外部声明，绝不改用户函数签名。在 `math_call.c` 追加 CIR RUN + `CHECK-CIR*`（检查 `cir.call @sin(` 与声明 `@sin(!cir.double)`）。

### M5 global_static + function_declaration 增量用例

- global/static：改 `cir.global` sym_type 与初始值、`cir.get_global` 用户链（store/load cast、对齐属性）；函数内 static 同名规则 `F.var`。
- function_declaration：追加的 CIR 用例仅验证 CIRPrintConfig 输出（含 isParam/ret. 项）与 transform-cir 对局部变量 `y` 的转换；**不期望** `@callee` 签名变化（参数/返回类型保持原样），`isParam`/`ret.` 项被跳过且 pass 不报错、不影响其余变量转换。

### M6 pointer + struct_type 增量用例（最复杂，最后做）

- 指针/数组：非参数的指针变量转换（`!cir.ptr<!cir.float>→!cir.ptr<!cir.f16>`）跟随 alloca；数组 alloca `!cir.array<T x N>` 改元素类型与 `cir.get_element`；`cir.get_member`/`cir.get_element` 结果类型级联。
- 结构体：CIR record 类型是 MLIR 类型（不可变），采用「重建类型 + 全模块类型转换」：定义 TypeConverter + 对 `cir.get_member`/`cir.global`/`cir.alloca`/load/store 的 ConversionPattern（或手工创建新 record 类型后 replaceOp）。若 record 重命名/重建阻碍过大，退路：仅对 struct 内被配置的**浮点成员访问点**插入 widening/narrowing cast 而不改 record 布局（语义等价的最小转换），并在 CHECK-CIR 期望中体现。
- 在 `pointer.c`、`struct_type.c` 追加 CIR RUN + `CHECK-CIR*`。

### M7 收尾

- `ninja check`（或 `sh scripts/run.sh check`）全量 lit 绿：每个测试文件的旧 RUN（LLVM IR）与新 RUN（CIR）都通过；未启用 `cir` 的既有链路行为不变。
- 清理临时输出；确认 6 个测试 Output/ 下历史产物不影响（lit 在 tmp 运行）。

## 关键文件

- 新增：`cir/RegisterCIRPasses.cpp`、`cir/CIRPrintConfig.{h,cpp}`、`cir/CIRTransform.{h,cpp}`、`cir/CIRConfig.hpp`（共 3 个编译单元 + 1 个共享头；全部变换逻辑集中在 CIRTransform.cpp 内分阶段）
- 修改：[CMakeLists.txt](file:///home/feli/perci-tuner/CMakeLists.txt)（新增 PerciTunerCIR MODULE 目标）、[scripts/run.sh](file:///home/feli/perci-tuner/scripts/run.sh)（`cir` 开关与三条 CIR 分支）、`test/print-and-transform/{scalar,math_call,global_static,function_declaration,pointer,struct_type}.c`（仅**追加** CIR RUN 与 CHECK-CIR* 行）
- 复用（不改）：[lib/utils/GlobMatch.hpp](file:///home/feli/perci-tuner/lib/utils/GlobMatch.hpp)、math 函数集合、类型名字符串契约

## 验证方式（端到端）

```sh
cd /home/feli/perci-tuner && sh scripts/build.sh          # 两个 .so 均编译通过
python3 -m lit -v test/print-and-transform                # 6 个文件：旧 RUN + 新 CIR RUN 全部通过
sh scripts/run.sh check                                   # 全量回归
# 手工端到端：
sh scripts/run.sh cir print-config test/print-and-transform/scalar.c -lm
sed 's/"type": "long double"/"type": "double"/' config.json > changed.json
sh scripts/run.sh cir change-by-config -target=changed.json test/print-and-transform/scalar.c -lm
./changed_output                                          # 可执行、结果正确
```

## 风险与退路

- **out-of-tree MLIR 插件链接**：CIR dialect 库的 cmake target 名可能不是 `MLIRCIR`，M0 以编译/加载错误为准迭代（已确认头文件在 `/usr/local/include/clang/CIR`，库 `libMLIRCIR.so` 存在）。
- **record 类型改写**：MLIR 类型不可变，M6 若类型转换器成本过高，采用成员访问点 cast 的语义等价退路。
- **long double ABI**：aarch64 上 long double=f128，CIR 已验证为 `!cir.long_double<!cir.f128>`，回降与链接行为在 M5 用 global_static 实跑验证。
- **sed 全局替换误伤**：测试用 `sed s/"type": "float"/"type": "double"/` 会同时改到 isParam/ret. 项；CIRTransform 必须显式跳过这些项，不能因跳过项中断其他变量转换（M5 用 function_declaration 覆盖验证）。
