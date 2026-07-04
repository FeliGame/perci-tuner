# Development Log

## Nov, 2024

### 25

* 编写第一个Pass(CreateCallDependency), 以查询源文件的所有函数调用

### 26

* CreateCallDependency：支持通过递归的方式查询被调用函数实现中的下一级调用。
* CreateCallDependency：为了可读性，在参数设置及输出时，使用llvm::demangle将IR的函数名称转换为源码形式，如：_Z3foovii 转换为 foo(int, int)，_ZN7Snippet4foo1Eii 转换为 Snippet::foo1(int, int)，值得注意的是，在Pass内部中依然用IR形式记录，以避免多源文件调用时可能的重复符号。
* TIPS: Function*->isIntrinsic() 用于判断是否为LLVM内建函数，例如浮点数的a * b + c会被转换为llvm.fmuladd.f32操作。
* TIPS: Function*->isIntrinsic() 并不能判断C++的inline函数。

### 27

* CreateCallDependency：支持将结果输出到searched_functions.txt，值得注意的是，命令行调试输出的是人类可读形式，而文件中存储的是去重的IR符号形式。
* 编写样例测试了CreateCallDependency在跨文件调用类方法的效果。
* run.sh现支持多源文件命令行参数输入，以进行编译。

### 28

* 明确Precimonious CreateConfigFile printType 对struct的输出不完善。
* 新增Pass: CreatePrecisionConfig，类比Precimonious CreateConfigFile。
* 待验证：printType函数中分析的IR符号在不同架构下是否一致？例如 X86_FP80TyID 对应 long double。
* 存疑：Precimonious CreateConfigFile printType中有一行outfile << *type，不合理！

### 29

* opt命令传入多条pass的语法：-passes='passA,passB...'。
* -passes中传入的顺序直接影响执行顺序。
* CreatePrecisionConfig: 采用nlohmann/json序列化，以保证标准易用的JSON格式读写。

### 30
* CreatePrecisionConfig: LLVM17通过llvm::DbgDeclareInst而非AllocaInst或ValueSymbolTable来读取变量声明，同时须在生成IR的命令中启用-g选项才能读取到变量名。
* clean.sh: 清理所有非工具文件，该脚本应当在每次git commit前执行

## Dec, 2024

### 1

* CreateVariableDecl: Supports multiple pointers, such as 'MY_STRUCT**'.

### 2

* CreateVariableDecl: Supports array. All of them shown in one dimension, such as: double[3][6](src) -> double[18](config.json).

### 5

* Functions declared in IR generated from .c has no parameter lists.

### 8

* CreateVariableDecl: Supports const.

### 9

* CreateVariableDecl: Supports global vars but not formatted.
* Issue: Local string decl will cancel CreateCallDependency's func search progress. (NOT ALLOWED!)
* TODO: Analyze struct/class

### 17

* All vars (except cmd paras) declared in passes should be private.
* CreateVariableDecl: Add var decl location into config.json, found that type of global pointers aren't defined.
* Add Pass: ChangeVariableDecl (Unfinished).

### 19

* CreateVariableDecl: Variable declare location.
* ChangeVariableDecl: Change type of local vars in AllocInst.

### 20

* ChangeVariableDecl: Change StoreInst & Replace LoadInst.
* Issue: Replace inst may cause fpext/fptrunc crash, solve this need analyze Use Propagation.

### 25

* ChangeVariableDecl: Replace LLVM CallInst. (such as fmuladd.f32 < - > fmuladd.f64)
* Issue: While compiling C code, arithmetic inst as 'fmul' will throw error: 'unmatched operand type'.

### 27

* Add Pass: IRTiming, it records clock cycles while running each function in IR.
* CreateCallDependency is renamed: SearchFunctions.

## Jan, 2025

### 5

* IRTiming: Delete IRTimingResult file in main.
* Issue: Current IRTiming can't record accumulative cycles while execute a function multiple times.

### 8

* ChangeVariableDecl: Insert fpext/fptrunc while change precision.

## Feb, 2025

### 4

* Issue: for an equation 'a + b + c', if a and b changed from ld to d , and c keep ld; then casts comes to a wrong solution: d + d + d; Correct solution: ld + ld + ld. To solve this, I commit changes as below:
* ChangeVariableDecl: RepairUserType(): Find the highest operand precision in inst and make other operands consistent.

### 5

* ChangeVariableDecl: change precision of fmuladd won't crash (caused by wild pointers in instToErase , I keep it in DenseSet to solve this). However, multiple unreferenced insts are kept.

### 7

* ChangeVariableDecl: Support floating-point's array.
* Issue: ParseDITypeName parse 'const float[2]' as 'const float[0]'.
* Knowledge: local static will be marked as '@local_func_name.static_var_name internal global...', global static will be marked as '@static_var_name internal global...', global non-static will be marked as '@global_var_name dso_local global...'.
* CreateVariableDecl: Support static var.

### 8

* ChangeVariableDecl: Introduce DenseSet to record all insts repaired to avoid duplicate unreferenced insts. May cause some crash e.g. fadd operands type not same.
* SearchFunctionCall: Support excluding functions to search.
* ChangeVariableDecl: ReturnInst's type mustn't be repaired.
* Python scripts: csv show changes.
* Introduce funarc benchmark from Precimonious.

### 9

* ChangeVariableDecl: Support GEP(getelementptr) inst.
* IRTiming: Only record main() run cycles, to avoid too much file IO.

### 10

* ChangeVariableDecl: Repair BinOp/UnaryOp/FCmp/PHINode type mismatch.
* search.py: Prune runned configs to accelerate.
* search.py: Generate optimized IR.

### 19

* search.py: Only use change-precision pass while search config to accelerate tools.

### 20

* search.py: Correct delta-debugging algorithm.

### 23

* CreatePrecisionConfigByPragma(Unfinished): compatible with 3 new #pragma intrinsic functions.

### 26

* config_utils.py: recognize & transform array types.
* search.py: add parameter: run times of IR per iteration.
* search.py: supports transform floating-type const & array.
* Add Benchmarks: FPBench & GSL.
* ChangeVariableDecl: Repair crash when function args' type changed.

## Mar, 2025

### 3

* Add Pass: MergeMultiVersion.

### 4

* ChangeVariableDecl: Repair bug that const floating declarations won't be changed.
* Add Pass: ChangeFunctionCall(Unfinished).
* SearchFunctions is renamed to: SearchFunctionCall, update its output as function_call.json.
* ChangeVariableDecl: only depend to config.json, exclude searched_functions.txt.
* Accelerate Passes by using DenseSet rather than std::set.

### 5

* CreateVariableDecl: Safely search static Global vars to avoid invalid operand count. Add 'isParam' key into config.json.
* Make benchmarks runnable: FPBench, GSL. Pay attention that run GSL should add option: `-load /usr/local/lib/libgsl.so` in lli command (should install GSL).
* Rename dd.py to search.py.
* ChangeVariableDecl: RepairUser of StoreInst's alignment. Remove instRepaired, which caused Inst can't be changed multi times. (such as FCmp & BinOp, both ops' type changed).
* ParseDITypeName supports dwarf::DW_TAG_typedef (such as gsl/root.c: const gsl_root_fsolver_type*).
* search.py: Ignore pointer-type & non-floating-type to reduce change-set size (accelerate searching).

### 6

* ChangeFunctionCall: Repair bugs.
* Redesigned SearchFunctionCall: Iterate CallInst rather than visit recursively (Accelerate pass).
* SearchFunctionCall & ChangeFunctionCall all use config.json. Remove function_call.json.
* search.py, config_utils.py: Support change function CallInst.
* Remove passes' options about config.json.

### 7

* Accelerate search.py: Keep current run's result & time rather than IO in run_config.json.
* Users can set the last arg in search.py to disable run_history.csv.
* ChangeFunctionCall: Support 2-arg math function calls.
* ChangeVariableDecl: Use FPCast to change precisions rather than FPExt/FPtrunc.
* Rename passes.
* SearchVariableDecl: Search struct members' decl.

### 8

* ChangeVariableDecl: Repair crash when convert to 'long double'. Support change struct members' decl (in changeStructDeclarations(Module& module)).

### 9

* ChangeVariableDecl: Support static/global struct initializers.
* transform.py: Ignore run-time failed IR. This cause gsl/diff.c can be searched over.
* All python scripts are 4-space indented.

### 11

* Support search __fp16.
* search.py show accelerate rate before & after search.

### 14

* __fp16's arithmetic inst is casted, so cancel such support.
* ChangeVariableDecl: Solve incorrect store inst & struct alignment while change struct member's precision, no more fail in gsl/roots.c.
* transform.py: Record fail config into run_history.csv.

### 16

* Native __fp16 arithmetic inst support. Search is also available.
* ChangeVariableDecl: Remove repairFMulAdd(), because LLVM handle it automatically.

### 17

* ChangeVariableDecl: Use new AllocaInst to change type, rather than use the old one, add repairUserType() to GEP, CallInst, PHINode (Maybe LLVM handle them automatically, I don't know).

### 20

* ChangeVariableDecl: Merge repairLoadUsers() into repairUserType().

### 21

* Call repairUserType() unified recursively.

### 24

* ChangeVariableDecl: Decouple repairUserType().

### 24

* ChangeVariableDecl: repair Alloca & StoreInst in changeStructDeclarations().

### 30

* ChangeVariableDecl: Repair Alloca's users after all allocas changed. Remove redundant repairCasts() branch.
* ChangeVaraibleDecl: CRUCIAL!!! Remove redundant BinOp, FCmpInsts. I realize it by replacing inst like a chain, and delete insts without users after all allocas & their users changed.
* ChangeVaraibleDecl: CRUCIAL!!! Remove redundant CallInsts, I realize it by replacing CallInst params' type directly, rathen than (before) creating new CallInst.

## Apr, 2025

### 1

* CRUCIAL: Run executable rather than lli, use clang -O2 option.
* Update benchmarks.

### 3

* add: SearchByPragma Pass (need specified clang&LLVM version).
* update: correct acc rate & INITIAL_DIV_NUM (2 -> 1), generate valid config during search.

### 4

* add: Support __autofp, search-all-vars option in SearchVariableDecl.
* remove: SearchByPragma (merged into SearchVariableDecl), excluded_functions in SearchFunctionCall.

### 5

* add: exhaustive search algorithm.
* update: cmd arg naming style of search.py.

### 7

* update: Throw error if no output while search. Repair bugs of #pragma & __autofp.
* CRUCIAL update SearchVariableDecl: Function Parameters are no more searched, which might cause inconsistent alignment during assignment.
* add: mxpcc.sh, the driver of entire toolchain.

### 10

* add: More tests, -fpe mode.
* update: mxpcc.sh run as clang without -mode option.

### 12

* update: Repair project1/cs-5.
* CRUCIAL add: ChangeVariableDecl: Handle float-integer casts during BinOp & FCmp, which cause not consistent with changed precision in manual version.
* CRUCIAL update: search.py: opt IR is consistent with opt config now.

### 15

* add: project2 cs-1 tests.
* update: repair crash in repairCasts, repairFCmp, repairBinOp.

### 16
* update: rename mxpcc.sh -mode, SearchFunctionCall + SearchVariableDecl -> PrintConfig.
* add: baseline test with same var precisions (long double, double, float, __fp16).

### 18
* update: PrintConfig: won't print size_t and __intptr_t.

### 23
* add: all config info will be saved into json & into run_history.csv.

### 24
* repair bug: inconsistent struct member type while changing.

### 29
* repair bug: baseline_test don't cover funcs, use midval instead of mean run time.
* add: run time timeout threshold (seconds).

### 30
* add: -shuffled option, shuffle variables' order before search.

## May, 2025

### 4
* add: HiFPTuner search, decouple algorithms.

### 6
* repair bug: min_time wrong result.

### 8
* update: skip running IR not satisfy rel_err threshold.
* BETA add: RecordResult & RemovePragma pass, which customize result var by llvm.precision.error pragma in -mxp mode.

### 15
* BETA repair bug: mxpcc not generate json, wrong rel_err in run_history.
* BETA: build.sh will build check_result lib.

### 24
* add: -l option in mxpcc & search.py (link libraries).

### 27
* update: mxpcc help info, -fpe option.

## Jun, 2025

### 10
* update: rename mxpcc to run.sh.

### 17
* BETA add: -fp128todd (ChangeVariableDecl pass).
* remove build.sh.

### 18
* fix: check_result not work.

### 19
* fix: RecordResult recognize double-double printf (by 'printf_fp106').

### 22
* add: floating-types' pointer type (ChangeVariableDecl).

### 27
* fix: hidd_algorithm, pointer type config.
* add: -mxp option recognize array type.

## Jul, 2025

### 18
* add: search all c/cpp if given dir options.

## Sep, 2025

### 9
* update: libdd, -fno-builtin option.

### 10
* add: -check-result option in run.sh.
* add: DebugInfo for local vars in ChangeVariableDecl.

### 15
* remove: -O2 option, which cause unexpected result.
* update: data_utils.py use decimal to get more accurate relative error.

### 16
* update: -fp128todd call clang & libdd rather than ChangeVariableDecl.

## Oct, 2025

### 5
* add: -jN option to accelerate dd-algorithm.

### 7
* remove: FP128ToDDFlag in ChangeVariableDecl Pass.
* add: changable func args (sync with automxprec).

## Nov, 2025

### 7
* add: independent FP128ToDD pass, which means this option no more need MxPCC compiler.

### 8
* update ChangeFunctionCall: prevent nesting cast of para-type.

### 10
* add: hex-result error calc.
* repair: search-optimize unable when -fp128todd.

### 26
* add: support -fp128todd on x86_64.

## Dec, 2025

### 2
* update: perf_counter to count running cycles.

### 4
* add: variadic args cast in ChangeVariableDecl, accelarate cmake.

### 5
* add: compatible for LLVM 19.

### 10
* repair transform.py: run changeFunc pass before changeVar pass.
* CRUCIAL repair ChangeVariableDecl: inst-replace during BinOp & FCmp.
* repair ChangeVariableDecl: keep original args' type in var arg calls such as printf.
* repair transform.py: run original config without fp128todd pass.

### 11
* add: repairBitCast(), repairGEP().
* repair ChangeVariableDecl: optimize multi-op inst such as BinOp, FCmp, FMulAdd.

### 12
* repair ChangeVariableDecl: repair continuous casts.

### 15
* update FP128ToDD: implement fmuladd mix-precision.

### 16
* repair ChangeVariableDecl: repair fmuladd.f80 cast.

### 17
* repair config IO: recognize var in config by file+line+column, rather than line+name (mayconflict).
* accelerate search config: using configMap. (not compatible with config before)

### 18
* CRUCIAL repair dd_algorithm: the algorithm impl is wrong before!

### 22
* repair ChangeVariableDecl: static type & GEP inst, accelerate repair inst.
* update PrintConfig & ChangeVariableDecl: compatible with fortran decl.
* update ChangeVariableDecl: add int-to-fp cast.
* simplify ChangeVariableDecl: getAlign() & repairAllocaUsers().

### 23
* CRUCIAL add TEDD: Traceback-enhanced delta-debugging algorithm.

### 24
* rename TEDD to PEDD: Prune-enhanced delta-debugging algorithm.
* update PEDD: combine better set(if exists) with pruned change set.

### 25
* repair ChangeVariableDecl: replaced CallInst arg type not match.

### 26
* CRUCIAL update PrintConfig/ChangeFunctionDecl: recognize & change function return type.
* repair ChangeFunctionDecl: remove redundant cast in fmuladd, fcmp, binop, callInst.
* update PEDD: prune single-var set whenever is first seen, skip superset of existing reduced set.

### 29
* update search-optimize mode: support args for executable in search-optimize mode.

### 30 
* repair ChangeVariableDecl: infinite loop while running repairBinOp, reduce new multi-op inst creation (better performance).

## Jan, 2026

### 5
* update PEDD: optimize set operation performance O(n*m) -> O(n + m), optimize break loop timing.

### 9
* update search files: searched files saved in config/ exec/ IR/.

### 12
* repair FP128ToDD: stack dump when encounters scanf family.

### 16
* update pedd_algorithm.py: handle RUNTIME-ERROR correctly.

### 17
* repair ChangeVariableDecl: struct member users type.
* CRUCIAL update PrintConfig: show global ptr type, struct's type (global & local).

### 18
* repair ChangeVariableDecl: struct pointer type member users.
* CRUCIAL update ChangeVariableDecl & PrintConfig: print & change global ptr/array type.

### 19
* repair ChangeVariableDecl: same array-type & multi-level pointers.
* repair pedd: hash 'global'.

### 24
* CRUCIAL update PrintConfig & ChangeVariableDecl: print & change static ptr/array type.

### 25
* feature ChangeVariableDecl: support array type global/static intializers.
* repair transform.py: handle all exception results (RUNTIME-ERROR/TIMEOUT).

### 28
* feature PrintConfig: print struct's real type rather than typedef.
* feature ChangeVariableDecl: support struct intializers.

## Feb, 2026

### 10
* update CMakeLists.txt: print line/column in stack dump.
* feature run.sh: support -base-result, -base-time option.

## Mar, 2026

### 1
* repair dd_algorithm's bug.
* remove incorrect strategy of PEDD algorithm.

## Apr, 2026

### 14

* fix run.sh: add -u flag to python command in search-optimize mode for faster output.
* fix change-by-config mode: output.bc is human readable.

### 19

* CRUCIAL/INCOMPATIBLE feat: multi-version functions according to callinst's actual args' type.
* How to realize this:
  1. Detect and keep all callinsts actual arg base types.
  2. Clone called function definition with new arg base types and repair arg's user insts.
  3. Update callinsts to apply cloned functions.
  4. Remove old unused function definitions.
* remove from PrintConfig: never detect function args (but keep return type).
* rename ChangeFunctionCall to ChangeMathCall.

### 20

* fix ptr's StoreInst align: always 8.
* fix cloned old function, they will only be removed if no use.
* fix cloned function arg can't detect global intializer (global/static).
* fix function's ret type.

### 21

* fix no multi-version cloned function in nested callinsts.

### 22

* fix multi-version function replace map.

### 25

* feat: global floating-point pointer type recognition.

### 26

* fix: get args' basic type from map rather than DebugInfo (better performance).
* FATAL fix: repairStoreInst is unrunnable before.
* fix: change type for Callinst's arg like 'ptr getelementptr...'.
* fix: change array-type for repairGEPInst.

### 29

* fix no multi-version cloned function in nested callinsts.

## May, 2026

### 1

* feat: optimized config index.

### 4

* feat: always show acc rate if config runned.(The impl is FATAL WRONG!!!)
* feat PrintConfig: skip local-var decl in function without users.(The impl is FATAL WRONG!!!)

### 6

* fix ChangeVariableDecl: floating parameter type follow config rather than callinst's args, optimize getAlign() logic.
* FATAL fix transform.py: recover hidden acc rate if break error threshold.
* FATAL fix PrintConfig: main function is no more skipped.

### 7

* fix search: remove optimized config, better performance coz of less copy & json dump.
* fix: nested called function's ptr arg type transform (solve CG's RUNTIME-ERROR).

### 8

* fix: infinte repair fmuladd/multi-op in V1F1/V1F2/V2F1/V2F2 cases (introduced from May 7th commit).
* perf: using CloneFunctionInto rather than CloneFunction, reduce clone times (2 -> 1).

### 18

* CRUCIAL feat: CallGraph to solve nested call's ptr-arg base type consistency.

### 20

* fix store type when mix-precison inter-pointer assign , A(i) = B(j).
* TODO: GV & constant repairStore.

### 31

* feat: support -skip-scan src files to reduce search space size.

## Jun, 2026

### 12

* feat: mix-binop option to enable mixed precision binop for double & double-double operand. --enable-mix-binop in opt option.

### 16

* feat: rename -skip-scan to -skip-file, -skip-func specifies funcs to skip scanning.

### 19

* fix: x86_fp80 ret. seemed as 'long double'.

## Jul, 2026

### 1

* GPT-5.5 fix: reimplement scripts/run.sh.

### 3

* GPT fix: typedef struct won't be recognized correctly in PrintConfig.

### 4

* fix: use -O2, rename skip-func/skip-file to exclude-func/exclude-file
* feat: include-func/include-file, default scan all