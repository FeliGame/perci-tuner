// @Author: Guocheng - NUDT

#ifndef LLVM_VERIFY_SCALAR_UPCAST_H
#define LLVM_VERIFY_SCALAR_UPCAST_H

#include <llvm/Passes/PassBuilder.h>

namespace llvm {

struct AnalysisResult {};

class VerifyScalarUpcast : public AnalysisInfoMixin<VerifyScalarUpcast> {
public:
    using Result = AnalysisResult;

    Result run(Module &module, ModuleAnalysisManager &MAM);

    bool runOnFunction(Function &F);

private:
    friend AnalysisInfoMixin<VerifyScalarUpcast>;
    static AnalysisKey Key;

    bool isAlwaysUpcastBeforeCompute(Value *V);
    bool isComputation(Value *V);
    bool hasAvailableComputation(Value *V,
                          DenseSet<const Value *> &VisitedRaw,
                          DenseSet<const Value *> &VisitedUpcasted,
                          bool upcasted);
};

class VerifyScalarUpcastPass : public PassInfoMixin<VerifyScalarUpcastPass> {
public:
    PreservedAnalyses run(Module &M, ModuleAnalysisManager &MAM);
};

}

#endif // LLVM_VERIFY_SCALAR_UPCAST_H
