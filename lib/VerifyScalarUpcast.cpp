// @Author: Guocheng NUDT
// Usage: Check if any scalar FP are full of upcast before ALU.
// Input: Mixed-Precision IR
// Output: True/False

#include "VerifyScalarUpcast.h"

#include <llvm/IR/Instructions.h>
#include <llvm/IR/Constants.h>
#include <llvm/IR/GlobalVariable.h>
#include <llvm/IR/Operator.h>
#include <llvm/IR/IntrinsicInst.h>
#include <llvm/Demangle/Demangle.h>
#include <llvm/ADT/DenseSet.h>

using namespace std;
using namespace llvm;

AnalysisKey VerifyScalarUpcast::Key;

bool VerifyScalarUpcast::isComputation(Value *V) {
    auto *I = dyn_cast<Instruction>(V);
    if (!I) return false;
    if (isa<BinaryOperator>(I) ||
        isa<UnaryOperator>(I) ||
        isa<FCmpInst>(I) ||
        isa<CallInst>(I) ||
        isa<ReturnInst>(I))
        return true;
    return false;
}

bool VerifyScalarUpcast::hasAvailableComputation(Value *V,
                          DenseSet<const Value *> &VisitedRaw,
                          DenseSet<const Value *> &VisitedUpcasted,
                          bool upcasted) {
    auto &Visited = upcasted ? VisitedUpcasted : VisitedRaw;
    // already existed before insert, it is a valid cycle.
    if (!Visited.insert(V).second) return true;

    for (auto *U : V->users()) {
        auto *UV = dyn_cast<Instruction>(U);
        if (!UV && !isa<ConstantExpr>(UV)) continue;

        if (isa<FPTruncInst>(U) || isComputation(UV))
            return true;

        // notice that store inst should pay attention if is a downcast
        // case 1: store other -> ptr V (handle this only)
        // case 2: store V -> ptr other (is this possible?)
        if (auto *store = dyn_cast<StoreInst>(U)) {
            auto *src = store->getOperand(0);
            if (isComputation(src) || isa<FPExtInst>(src))
                return true;
        }

        // recursive check load/gep/constantexpr users
        if (hasAvailableComputation(U, VisitedRaw, VisitedUpcasted, true))
            return true;
    }
    return false;
}

bool VerifyScalarUpcast::isAlwaysUpcastBeforeCompute(Value *V) {
    auto visitedRaw = DenseSet<const Value *>();
    auto visitedUpcasted = DenseSet<const Value *>();
    return !hasAvailableComputation(V, visitedRaw, visitedUpcasted, false);
}

bool VerifyScalarUpcast::runOnFunction(Function &F) {
    for (auto &BB : F) {
        for (auto &I : BB) {
            if (auto *alloca = dyn_cast<AllocaInst>(&I)) {
                auto *allocTy = alloca->getAllocatedType();
                if (!allocTy->isFloatingPointTy()) continue;
                if (alloca->getName().find(".") != string::npos) continue;
                if (isAlwaysUpcastBeforeCompute(alloca))
                    return false;
            }
        }
    }
    return true;
}

VerifyScalarUpcast::Result
VerifyScalarUpcast::run(Module &M, ModuleAnalysisManager &MAM) {
    errs() << ">>> Perci-Tuner: Running VerifyScalarUpcast Analysis...\n";
    auto invalidCount = 0;
    for (auto &F : M) {
        if (F.isDeclaration()) continue;
        if (!runOnFunction(F))
            ++invalidCount;
    }

    for (auto &GV : M.globals()) {
        if (GV.isDeclaration()) continue;
        auto name = demangle(GV.getName().str());
        if (name.find('.') != string::npos) continue;

        auto *valTy = GV.getValueType();
        if (!valTy->isFloatingPointTy()) continue;

        if (isAlwaysUpcastBeforeCompute(&GV))
            ++invalidCount;
    }

    errs() << ">>> VerifyScalarUpcast: " << invalidCount << " invalid.\n";
    return AnalysisResult{};
}

PreservedAnalyses VerifyScalarUpcastPass::run(Module &M,
                                               ModuleAnalysisManager &MAM) {
    MAM.getResult<VerifyScalarUpcast>(M);
    return PreservedAnalyses::all();
}
