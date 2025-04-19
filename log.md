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

# 3

* Add Pass: MergeMultiVersion.

# 4

* ChangeVariableDecl: Repair bug that const floating declarations won't be changed.
* Add Pass: ChangeFunctionCall(Unfinished).
* SearchFunctions is renamed to: SearchFunctionCall, update its output as function_call.json.
* ChangeVariableDecl: only depend to config.json, exclude searched_functions.txt.
* Accelerate Passes by using DenseSet rather than std::set.

# 5

* CreateVariableDecl: Safely search static Global vars to avoid invalid operand count. Add 'isParam' key into config.json.
* Make benchmarks runnable: FPBench, GSL. Pay attention that run GSL should add option: `-load /usr/local/lib/libgsl.so` in lli command (should install GSL).
* Rename dd.py to search.py.
* ChangeVariableDecl: RepairUser of StoreInst's alignment. Remove instRepaired, which caused Inst can't be changed multi times. (such as FCmp & BinOp, both ops' type changed).
* ParseDITypeName supports dwarf::DW_TAG_typedef (such as gsl/root.c: const gsl_root_fsolver_type*).
* search.py: Ignore pointer-type & non-floating-type to reduce change-set size (accelerate searching).

# 6

* ChangeFunctionCall: Repair bugs.
* Redesigned SearchFunctionCall: Iterate CallInst rather than visit recursively (Accelerate pass).
* SearchFunctionCall & ChangeFunctionCall all use config.json. Remove function_call.json.
* search.py, config_utils.py: Support change function CallInst.
* Remove passes' options about config.json.

# 7

* Accelerate search.py: Keep current run's result & time rather than IO in run_config.json.
* Users can set the last arg in search.py to disable run_history.csv.
* ChangeFunctionCall: Support 2-arg math function calls.
* ChangeVariableDecl: Use FPCast to change precisions rather than FPExt/FPtrunc.
* Rename passes.
* SearchVariableDecl: Search struct members' decl.

# 8

* ChangeVariableDecl: Repair crash when convert to 'long double'. Support change struct members' decl (in changeStructDeclarations(Module& module)).

# 9

* ChangeVariableDecl: Support static/global struct initializers.
* transform.py: Ignore run-time failed IR. This cause gsl/diff.c can be searched over.
* All python scripts are 4-space indented.

# 11

* Support search __fp16.
* search.py show accelerate rate before & after search.

# 14

* __fp16's arithmetic inst is casted, so cancel such support.
* ChangeVariableDecl: Solve incorrect store inst & struct alignment while change struct member's precision, no more fail in gsl/roots.c.
* transform.py: Record fail config into run_history.csv.

# 16

* Native __fp16 arithmetic inst support. Search is also available.
* ChangeVariableDecl: Remove repairFMulAdd(), because LLVM handle it automatically.

# 17

* ChangeVariableDecl: Use new AllocaInst to change type, rather than use the old one, add repairUserType() to GEP, CallInst, PHINode (Maybe LLVM handle them automatically, I don't know).

# 20

* ChangeVariableDecl: Merge repairLoadUsers() into repairUserType().

# 21

* Call repairUserType() unified recursively.

# 24

* ChangeVariableDecl: Decouple repairUserType().

# 24

* ChangeVariableDecl: repair Alloca & StoreInst in changeStructDeclarations().

# 30

* ChangeVariableDecl: Repair Alloca's users after all allocas changed. Remove redundant repairCasts() branch.
* ChangeVaraibleDecl: CRUCIAL!!! Remove redundant BinOp, FCmpInsts. I realize it by replacing inst like a chain, and delete insts without users after all allocas & their users changed.
* ChangeVaraibleDecl: CRUCIAL!!! Remove redundant CallInsts, I realize it by replacing CallInst params' type directly, rathen than (before) creating new CallInst.

## Apr, 2025

# 1

* CRUCIAL: Run executable rather than lli, use clang -O2 option.
* Update benchmarks.

# 3

* add: SearchByPragma Pass (need specified clang&LLVM version).
* update: correct acc rate & INITIAL_DIV_NUM (2 -> 1), generate valid config during search.

# 4

* add: Support __autofp, search-all-vars option in SearchVariableDecl.
* remove: SearchByPragma (merged into SearchVariableDecl), excluded_functions in SearchFunctionCall.

# 5

* add: exhaustive search algorithm.
* update: cmd arg naming style of search.py.

# 7

* update: Throw error if no output while search. Repair bugs of #pragma & __autofp.
* CRUCIAL update SearchVariableDecl: Function Parameters are no more searched, which might cause inconsistent alignment during assignment.
* add: mxpcc.sh, the driver of entire toolchain.

# 10

* add: More tests, -fpe mode.
* update: mxpcc.sh run as clang without -mode option.

# 12

* update: Repair project1/cs-5.
* CRUCIAL add: ChangeVariableDecl: Handle float-integer casts during BinOp & FCmp, which cause not consistent with changed precision in manual version.
* CRUCIAL update: search.py: opt IR is consistent with opt config now.

# 15

* add: project2 cs-1 tests.
* update: repair crash in repairCasts, repairFCmp, repairBinOp.

# 16
* update: rename mxpcc.sh -mode, SearchFunctionCall + SearchVariableDecl -> PrintConfig.
* add: baseline test with same var precisions (long double, double, float, __fp16).

# 18
* update: PrintConfig: won't print size_t and __intptr_t.
