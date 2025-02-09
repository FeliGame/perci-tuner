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
* 编写样例测试了CCD在跨文件调用类方法的效果。
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

* CPC: Supports multiple pointers, such as 'MY_STRUCT**'.

### 2

* CPC: Supports array. All of them shown in one dimension, such as: double[3][6](src) -> double[18](config.json).

### 5

* Functions declared in IR generated from .c has no parameter lists.

### 8

* CPC: Supports const.

### 9

* CPC: Supports global vars but not formatted.
* Issue: Local string decl will cancel CCD's func search progress. (NOT ALLOWED!)
* TODO: Analyze struct/class

### 17

* All vars (except cmd paras) declared in passes should be private.
* CPC: Add var decl location into config.json, found that type of global pointers aren't defined.
* Add Pass: ChangePrecision (Unfinished).

### 19

* CPC: Variable declare location.
* CP: Change type of local vars in AllocInst.

### 20

* CP: Change StoreInst & Replace LoadInst.
* Issue: Replace inst may cause fpext/fptrunc crash, solve this need analyze Use Propagation.

### 25

* CP: Replace LLVM CallInst. (such as fmuladd.f32 < - > fmuladd.f64)
* Issue: While compiling C code, arithmetic inst as 'fmul' will throw error: 'unmatched operand type'.

### 27

* Add Pass: IRTiming, it records clock cycles while running each function in IR.
* CreateCallDependency is renamed: SearchFunctions.

## Jan, 2025

### 5

* IRTiming: Delete IRTimingResult file in main.
* Issue: Current IRTiming can't record accumulative cycles while execute a function multiple times.

### 8

* CP: Insert fpext/fptrunc while change precision.

## Feb, 2025

### 4

* Issue: for an equation 'a + b + c', if a and b changed from ld to d , and c keep ld; then casts comes to a wrong solution: d + d + d; Correct solution: ld + ld + ld. To solve this, I commit changes as below:
* CP: RepairUserType(): Find the highest operand precision in inst and make other operands consistent.

### 5

* CP: change precision of fmuladd won't crash (caused by wild pointers in instToErase , I keep it in DenseSet to solve this). However, multiple unreferenced insts are kept.

### 7

* CP: Support floating-point's array.
* Issue: ParseDITypeName parse 'const float[2]' as 'const float[0]'.
* Knowledge: local static will be marked as '@local_func_name.static_var_name internal global...', global static will be marked as '@static_var_name internal global...', global non-static will be marked as '@global_var_name dso_local global...'.
* CPC: Support static var.

### 8

* CP: Introduce DenseSet to record all insts repaired to avoid duplicate unreferenced insts. May cause some crash e.g. fadd operands type not same.
* SF: Support excluding functions to search.
* CP: ReturnInst's type mustn't be repaired.
* Python scripts: csv show changes.
* Introduce funarc benchmark from Precimonious.

### 9

* CP: Support GEP(getelementptr) inst.