# Development Log

## Nov, 2024

### 25

* 编写第一个Pass(CreateCallDependency), 以查询源文件的所有函数调用
* TODO: 无法查询被调用函数实现中的下一级调用

### 26

* 改进CreateCallDependency：通过递归的方式查询被调用函数实现中的下一级调用。
* 改进CreateCallDependency：为了可读性，在参数设置及输出时，使用llvm::demangle将IR的函数名称转换为源码形式，如：_Z3foov... 转换为 foo(int, int)，值得注意的是，在Pass内部中依然用IR形式记录，以避免多源文件调用时可能的重复符号。
* TIPS: Function*->isIntrinsic() 用于判断是否为LLVM内建函数，例如浮点数的a * b + c会被转换为llvm.fmuladd.f32操作。
* TIPS: Function*->isIntrinsic() 并不能判断C++的inline函数。