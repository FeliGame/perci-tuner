// @Author: Guocheng - NUDT
// Usage: Record program's clock cycles while running IR.
// Output: ir_timing.txt

#include "IRTiming.h"
#include "utils/ParseTypeName.hpp"

#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/DebugInfo.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/Intrinsics.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/Type.h>
#include <llvm/Support/CommandLine.h>
#include <llvm/Support/raw_ostream.h>
#include <llvm/IR/ValueSymbolTable.h>
#include <llvm/Demangle/Demangle.h>

#include <fstream>

using namespace std;
using namespace llvm;

cl::opt<string> IRTimingResultName("ir-timing-result", cl::value_desc("filename"), cl::desc("Total program run cycles"), cl::init("ir_timing.txt"));

bool IRTiming::runOnModule(Module &M)
{
    Function *mainFuncPtr = M.getFunction("main");
    if (!mainFuncPtr || mainFuncPtr->isDeclaration()) {
        errs() << "Error: Main function not found or is a declaration.\n";
        return false;
    }

    IRBuilder<> Builder(&mainFuncPtr->getEntryBlock(), mainFuncPtr->getEntryBlock().begin());

#if LLVM_VERSION_MAJOR >= 19
    auto *fileNamePtr = Builder.CreateGlobalString(IRTimingResultName);
    auto *modePtr = Builder.CreateGlobalString("w"); // rewrite
    auto *fmtStrPtr = Builder.CreateGlobalString("%s:%lld\n");
    auto *funcNamePtr = Builder.CreateGlobalString("main");
#else
    auto *fileNamePtr = Builder.CreateGlobalStringPtr(IRTimingResultName);
    auto *modePtr = Builder.CreateGlobalStringPtr("w"); // rewrite
    auto *fmtStrPtr = Builder.CreateGlobalStringPtr("%s:%lld\n");
    auto *funcNamePtr = Builder.CreateGlobalStringPtr("main");
#endif

    auto fopenFunc = M.getOrInsertFunction(
        "fopen",
        FunctionType::get(
#if LLVM_VERSION_MAJOR >= 19
            PointerType::get(M.getContext(), 0), {PointerType::get(M.getContext(), 0), PointerType::get(M.getContext(), 0)}, true));
#else
            PointerType::get(Type::getInt8Ty(M.getContext()), 0), {PointerType::get(Type::getInt8Ty(M.getContext()), 0), PointerType::get(Type::getInt8Ty(M.getContext()), 0)}, true));
#endif
    auto *filePtr = Builder.CreateCall(fopenFunc, {fileNamePtr, modePtr});

    auto *startCycle = Builder.CreateCall(
#if LLVM_VERSION_MAJOR >= 19
        Intrinsic::getOrInsertDeclaration(&M, Intrinsic::readcyclecounter),
#else
        Intrinsic::getDeclaration(&M, Intrinsic::readcyclecounter),
#endif
            {}, "start_cycle");

    auto fcloseFunc = M.getOrInsertFunction(
        "fclose",
        FunctionType::get(Type::getVoidTy(M.getContext()),
#if LLVM_VERSION_MAJOR >= 19
            {PointerType::get(M.getContext(), 0)}, true));
#else
            {PointerType::get(Type::getInt8Ty(M.getContext()), 0)}, true));
#endif

    auto fprintfFunc = M.getOrInsertFunction(
        "fprintf",
        FunctionType::get(Type::getVoidTy(M.getContext()),
#if LLVM_VERSION_MAJOR >= 19
            {PointerType::get(M.getContext(), 0), PointerType::get(M.getContext(), 0)}, true));
#else
            {PointerType::get(Type::getInt8Ty(M.getContext()), 0), PointerType::get(Type::getInt8Ty(M.getContext()), 0)}, true));
#endif

    // Record & ouput cycles before each ret of function
    for (auto &BB : *mainFuncPtr)
    {
        for (auto &I : BB)
        {
            if (auto *RI = dyn_cast<ReturnInst>(&I))
            {
                IRBuilder<> ReturnBuilder(RI); // Insert inst before ret
                auto *endCycle = ReturnBuilder.CreateCall(
#if LLVM_VERSION_MAJOR >= 19
                    Intrinsic::getOrInsertDeclaration(&M, Intrinsic::readcyclecounter),
#else
                    Intrinsic::getDeclaration(&M, Intrinsic::readcyclecounter),
#endif
                        {}, "end_cycle");
                auto *elapsedCycle = ReturnBuilder.CreateSub(endCycle, startCycle, "elapsed_cycle");

                ReturnBuilder.CreateCall(fprintfFunc, {filePtr, fmtStrPtr, funcNamePtr, elapsedCycle});
                ReturnBuilder.CreateCall(fcloseFunc, {filePtr});
            }
        }
    }
    

    return true;
}

PreservedAnalyses IRTiming::run(Module &M, ModuleAnalysisManager &)
{
    errs() << ">>> Perci-Tuner: Running IRTiming Pass...\n";
    if (!runOnModule(M))
        return PreservedAnalyses::all();
    return PreservedAnalyses::none();
}

void IRTiming::getAnalysisUsage(AnalysisUsage &AU) const
{
    AU.setPreservesAll();
}
