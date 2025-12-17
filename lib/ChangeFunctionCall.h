// @Author: Guocheng - NUDT

#ifndef LLVM_CHANGE_FUNCTION_CALL_H
#define LLVM_CHANGE_FUNCTION_CALL_H

#include "nlohmann/json.hpp"

#include <llvm/Pass.h>
#include <llvm/IR/Function.h>
#include <llvm/Support/raw_ostream.h>
#include <llvm/Passes/PassBuilder.h>
#include <llvm/Passes/PassPlugin.h>
#include <llvm/Support/CommandLine.h>

#include <vector>
#include <set>

using namespace std;

namespace llvm {


class ChangeFunctionCall : public PassInfoMixin<ChangeFunctionCall> {
public:
    PreservedAnalyses run(Module &module, ModuleAnalysisManager &); // for New Pass Manager

    virtual bool runOnModule(Module &module);
    
    bool runOnFunction(Function &F);

    virtual void getAnalysisUsage(AnalysisUsage &AU) const;

private:
    nlohmann::json config;
    std::map<std::string, nlohmann::json> configMap;  // 存储config的map形式，key为file:line:column的拼接字符串
    void parseConfig();
    // Same as config_utils.py
    DenseSet<StringRef> mathFunctions = {
        // 三角函数
        "sin", "sinf", "sinl", "cos", "cosf", "cosl", "tan", "tanf", "tanl",
        "asin", "asinf", "asinl", "acos", "acosf", "acosl", "atan", "atanf", "atanl",

        // 双曲函数
        "sinh", "sinhf", "sinhl", "cosh", "coshf", "coshl", "tanh", "tanhf", "tanhl",

        // 指数对数
        "exp", "expf", "expl", "log", "logf", "logl", 
        "exp2", "exp2f", "exp2l", "log2", "log2f", "log2l",
        "log10", "log10f", "log10l",

        // 幂和根
        "sqrt","sqrtf","sqrtl", "cbrt", "cbrtf", "cbrtl",
        "pow", "powf", "powl", "hypot", "hypotf", "hypotl",

        // 其他
        "fabs", "fabsf", "fabsl",        // 绝对值
        // "ceil", "ceilf", "ceill",       // 向上取整（llvm.ceil.xx）
        // "floor", "floorf", "floorl",     // 向下取整（llvm.floor.xx）
        "round", "roundf", "roundl",     // 四舍五入
        "fmod", "fmodf", "fmodl",       // 浮点余数
        "atan2", "atan2f", "atan2l",    // 二参数反正切
        "erf", "erff", "erfl",          // 误差函数（C99）
        "tgamma", "tgammaf", "tgammal"  // gamma函数（C99）
    };
};

} // namespace llvm

#endif // LLVM_CHANGE_FUNCTION_CALL_H
