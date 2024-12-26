// @Author: Guocheng - NUDT
// Usage: Record each function's clock cycles while running IR.
// Output: ir_timing.txt

#include "IRTiming.h"
#include "utils/ParseTypeName.hpp"

#include "nlohmann/json.hpp"
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

cl::opt<string> IRTimingResultName("ir-timing-result", cl::value_desc("filename"), cl::desc("Each function run cycles"), cl::init("ir_timing.txt"));

bool IRTiming::runOnModule(Module &M)
{
    for (auto &F : M)
    {
        if (F.isDeclaration())
            continue;
        auto funcName = demangle(F.getName());
        // Each entry of function
        IRBuilder<> Builder(&F.getEntryBlock(), F.getEntryBlock().begin());
        auto *StartCycle = Builder.CreateCall(Intrinsic::getDeclaration(&M, Intrinsic::readcyclecounter), {}, "start_cycle");

        // Create a file pointer
        auto *FilePtr = Builder.CreateGlobalStringPtr(IRTimingResultName.c_str());
        auto *ModePtr = Builder.CreateGlobalStringPtr("a");

        // Write elapsed cycles to file using fprintf
        std::string fmtStr = "%s:%lld\n";
        auto *FmtStrPtr = Builder.CreateGlobalStringPtr(fmtStr);
        auto *FuncNamePtr = Builder.CreateGlobalStringPtr(funcName);

        // Create fopen & get file pointer
        auto FopenFunc = M.getOrInsertFunction(
            "fopen",
            FunctionType::get(PointerType::get(Type::getInt8Ty(M.getContext()), 0), {PointerType::get(Type::getInt8Ty(M.getContext()), 0), PointerType::get(Type::getInt8Ty(M.getContext()), 0)}, true));
        auto *File = Builder.CreateCall(FopenFunc, {FilePtr, ModePtr});

        // Create fclose
        auto FcloseFunc = M.getOrInsertFunction(
            "fclose",
            FunctionType::get(Type::getVoidTy(M.getContext()), {PointerType::get(Type::getInt8Ty(M.getContext()), 0)}, true));

        // Create a function to write to file using fprintf
        auto FprintfFunc = M.getOrInsertFunction(
            "fprintf",
            FunctionType::get(Type::getVoidTy(M.getContext()), {PointerType::get(Type::getInt8Ty(M.getContext()), 0), PointerType::get(Type::getInt8Ty(M.getContext()), 0)}, true));

        // Record & ouput cycles before each ret of function
        for (auto &BB : F)
        {
            for (auto &I : BB)
            {
                if (auto *RI = dyn_cast<ReturnInst>(&I))
                {
                    IRBuilder<> ReturnBuilder(RI); // Insert inst before ret
                    auto *EndCycle = ReturnBuilder.CreateCall(
                        Intrinsic::getDeclaration(&M, Intrinsic::readcyclecounter), {}, "end_cycle");
                    auto *ElapsedCycle = ReturnBuilder.CreateSub(EndCycle, StartCycle, "elapsed_cycle");

                    ReturnBuilder.CreateCall(FprintfFunc, {File, FmtStrPtr, FuncNamePtr, ElapsedCycle});
                    ReturnBuilder.CreateCall(FcloseFunc, {File});
                }
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
